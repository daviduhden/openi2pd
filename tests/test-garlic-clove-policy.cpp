/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: garlic clove grammar and session concurrency.
 *
 * Checks the shared clove walker decision (deliver, skip a stale clove
 * and continue, skip a clove for another destination, stop on broken
 * framing), the clove-count bound, the destination-hash check, the
 * bounded clove replay filter, the per-thread HKDF context and the
 * serialized ElGamal wrapping path. No sockets and no sleeps are used;
 * the concurrency checks are deterministic and TSan-ready.
 *
 */

#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

#include "Garlic.h"
#include "I2NPProtocol.h"
#include "Tag.h"
#include "Timestamp.h"

namespace
{
using namespace i2p;

void TestClassifyCloveMatrix ()
{
	using garlic::ClassifyGarlicClove;
	using garlic::GarlicCloveDisposition;

	// Broken framing stops the walk: the remaining payload cannot be
	// located.
	assert (ClassifyGarlicClove (false, true, true) == GarlicCloveDisposition::RejectMessage);
	assert (ClassifyGarlicClove (false, false, true) == GarlicCloveDisposition::RejectMessage);
	assert (ClassifyGarlicClove (false, true, false) == GarlicCloveDisposition::RejectMessage);

	// A well-framed stale clove is skipped and the walk continues with the
	// next clove of the same payload.
	assert (ClassifyGarlicClove (true, false, true) == GarlicCloveDisposition::SkipStale);
	// A clove addressed to another destination is skipped for the same
	// reason.
	assert (ClassifyGarlicClove (true, true, false) == GarlicCloveDisposition::SkipWrongDestination);
	// A well-framed, fresh, locally addressed clove is delivered.
	assert (ClassifyGarlicClove (true, true, true) == GarlicCloveDisposition::Deliver);
}

void TestCloveCountBound ()
{
	assert (!garlic::IsValidCloveCount (0));
	assert (garlic::IsValidCloveCount (1));
	assert (garlic::IsValidCloveCount (static_cast<unsigned int> (I2NP_MAX_CLOVES)));
	assert (!garlic::IsValidCloveCount (static_cast<unsigned int> (I2NP_MAX_CLOVES) + 1));
	assert (!garlic::IsValidCloveCount (255)); // one-byte count field
}

void TestCloveDestinationCheck ()
{
	uint8_t localBytes[i2p::data::IdentHash::len] = { };
	i2p::data::IdentHash local (localBytes);
	uint8_t same[i2p::data::IdentHash::len] = { };
	assert (garlic::IsCloveForDestination (same, local));
	uint8_t other[i2p::data::IdentHash::len] = { };
	other[i2p::data::IdentHash::len - 1] = 1;
	assert (!garlic::IsCloveForDestination (other, local));
	assert (!garlic::IsCloveForDestination (nullptr, local));
}

void TestCloveReplayFilterIsSeparated ()
{
	// A destination-scoped clove filter must not see a router-scoped key
	// as a duplicate even for identical message IDs and expirations.
	I2NPDuplicateFilter cloveFilter (8, 300);
	const uint64_t expiration = i2p::util::GetMillisecondsSinceEpoch ();
	const uint32_t routerKey = GetI2NPDuplicateKey (7, expiration, I2NP_DUPLICATE_SALT_ROUTER);
	const uint32_t cloveKey = GetI2NPDuplicateKey (7, expiration, I2NP_DUPLICATE_SALT_DESTINATION);
	assert (routerKey != cloveKey);
	assert (!cloveFilter.CheckAndInsert (cloveKey, 1000));
	assert (cloveFilter.CheckAndInsert (cloveKey, 1000));
	// The same ID with a different expiration is a different clove.
	assert (!cloveFilter.CheckAndInsert (
		GetI2NPDuplicateKey (7, expiration + 1000, I2NP_DUPLICATE_SALT_DESTINATION), 1000));
}

void TestThreadLocalHKDFContext ()
{
	// The provider derivation context is stateful and not reentrant. Every
	// thread must observe its own instance and produce the reference
	// output.
	constexpr int NUM_THREADS = 4;
	auto makeSalt = [] (int index)
	{
		std::array<uint8_t, 32> salt { };
		for (size_t i = 0; i < salt.size (); i++)
			salt[i] = (i == 0) ? static_cast<uint8_t> (index) : static_cast<uint8_t> (i + 1);
		return salt;
	};
	std::array<uint8_t, 32> key { };
	for (size_t i = 0; i < key.size (); i++) key[i] = static_cast<uint8_t> (0x80 + i);

	// Reference output per thread index, derived on the main thread.
	std::vector<std::array<uint8_t, 64>> reference (NUM_THREADS);
	for (int i = 0; i < NUM_THREADS; i++)
	{
		const auto salt = makeSalt (i);
		garlic::GetThreadLocalHKDFContext () (
			salt.data (), key.data (), key.size (), "RatchetTagSet", reference[i].data (), reference[i].size ());
	}
	// The main thread always sees the same context object.
	i2p::crypto::HKDFContext * mainContext = &garlic::GetThreadLocalHKDFContext ();
	assert (mainContext == &garlic::GetThreadLocalHKDFContext ());

	std::atomic<int> mismatches { 0 };
	std::vector<const void *> contexts (NUM_THREADS, nullptr);
	std::vector<std::thread> threads;
	for (int i = 0; i < NUM_THREADS; i++)
	{
		threads.emplace_back (
			[i, &reference, &mismatches, &contexts, &key, makeSalt] ()
			{
				i2p::crypto::HKDFContext & ctx = garlic::GetThreadLocalHKDFContext ();
				contexts[i] = &ctx;
				assert (&ctx == &garlic::GetThreadLocalHKDFContext ());
				const auto salt = makeSalt (i);
				// Several rounds so an unsynchronized shared context would
				// interleave its provider state across threads.
				for (int round = 0; round < 32; round++)
				{
					std::array<uint8_t, 64> out { };
					ctx (salt.data (), key.data (), key.size (), "RatchetTagSet", out.data (), out.size ());
					if (out != reference[i]) mismatches.fetch_add (1);
				}
			});
	}
	for (auto & t : threads) t.join ();
	assert (mismatches.load () == 0);
	assert (mainContext == &garlic::GetThreadLocalHKDFContext ());
	assert (contexts[0] != mainContext);
	for (int i = 0; i < NUM_THREADS; i++)
		for (int j = i + 1; j < NUM_THREADS; j++) assert (contexts[i] != contexts[j]);
}

void TestElGamalWrapIsSerialized ()
{
	// One session tag can be consumed exactly once. Concurrent wrapping on
	// the same session must therefore produce exactly one message and must
	// not corrupt the tag list, the CBC state or the IV.
	uint8_t sessionKey[32];
	for (int i = 0; i < 32; i++) sessionKey[i] = static_cast<uint8_t> (i * 3 + 1);
	uint8_t tagBytes[32];
	for (int i = 0; i < 32; i++) tagBytes[i] = static_cast<uint8_t> (0x40 + i);
	garlic::SessionTag tag (tagBytes);
	garlic::ElGamalAESSession session (sessionKey, tag);
	assert (session.GetNumOutgoingTags () == 1);

	const uint8_t payload[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
	auto msg = CreateI2NPMessage (eI2NPData, payload, sizeof (payload), 0x01020304);
	assert (msg);

	constexpr int NUM_THREADS = 4;
	std::atomic<int> wrapped { 0 };
	std::vector<std::thread> threads;
	for (int i = 0; i < NUM_THREADS; i++)
	{
		threads.emplace_back (
			[&session, &msg, &wrapped] ()
			{
				auto out = session.WrapSingleMessage (msg);
				if (out) wrapped.fetch_add (1);
			});
	}
	for (auto & t : threads) t.join ();
	assert (wrapped.load () == 1);
	assert (session.GetNumOutgoingTags () == 0);
}

void TestSecondWrapFailsClosed ()
{
	// A session whose only tag was consumed must not fall back to a shared
	// generator or emit a second message: without a destination the
	// ElGamal path is unavailable, so the wrap fails closed instead of
	// reusing state.
	uint8_t sessionKey[32] = { };
	uint8_t tagBytes[32] = { };
	garlic::SessionTag tag (tagBytes);
	garlic::ElGamalAESSession session (sessionKey, tag);
	const uint8_t payload[8] = { 0 };
	auto msg = CreateI2NPMessage (eI2NPData, payload, sizeof (payload), 0x05060708);
	assert (msg);
	assert (session.WrapSingleMessage (msg) != nullptr);
	assert (session.WrapSingleMessage (msg) == nullptr);
}
} // namespace

int main ()
{
	TestClassifyCloveMatrix ();
	TestCloveCountBound ();
	TestCloveDestinationCheck ();
	TestCloveReplayFilterIsSeparated ();
	TestThreadLocalHKDFContext ();
	TestElGamalWrapIsSerialized ();
	TestSecondWrapFailsClosed ();
	return 0;
}
