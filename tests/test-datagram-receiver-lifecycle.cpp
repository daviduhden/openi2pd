/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test datagram receiver lifecycle.
 *
 * Covers identity-bearing receiver reset, no port-to-wildcard promotion,
 * protocol/port keyed identity and hash dispatch, draining callbacks before
 * owner teardown, and reset from inside a callback.
 *
 */

#include <cassert>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <boost/asio.hpp>

#include "Datagram.h"
#include "Destination.h"
#include "Gzip.h"

namespace
{
std::vector<uint8_t> MakeEmptyStoredGzipMember ()
{
	// DatagramDestination inflates every incoming protocol. This minimal
	// stored empty gzip member uses the parser's documented zero-block form.
	return { 0x1f, 0x8b, 0x08, 0, 0, 0, 0, 0, 0, 0xff, 0x01, 0, 0, 0xff, 0xff, 0, 0, 0, 0, 0, 0, 0, 0 };
}

std::shared_ptr<i2p::datagram::DatagramDestination> MakeDatagramDestination ()
{
	// Create the destination directly: storing it through
	// ClientDestination::CreateDatagramDestination would form the owner's
	// intentional destination ownership cycle in this isolated fixture.
	auto keys = i2p::data::PrivateKeys::CreateRandomKeys (
		i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
	static boost::asio::io_context service;
	auto owner = std::make_shared<i2p::client::ClientDestination> (service, keys, false);
	return std::make_shared<i2p::datagram::DatagramDestination> (owner, false, i2p::datagram::eDatagramV1);
}

void DispatchRaw (const std::shared_ptr<i2p::datagram::DatagramDestination> & destination, uint16_t port)
{
	auto payload = MakeEmptyStoredGzipMember ();
	destination->HandleDataMessagePayload (
		0, port, payload.data (), payload.size (), i2p::client::PROTOCOL_TYPE_RAW, nullptr);
}

// Identity/hash dispatch needs a real remote peer: a Datagram1 frame carries
// the sender's full identity and signature, Datagram3 the sender hash.
struct Sender
{
	i2p::data::PrivateKeys keys;
	std::shared_ptr<const i2p::data::IdentityEx> identity;
	std::vector<uint8_t> identityBuffer;
};

Sender MakeSender ()
{
	Sender sender;
	sender.keys = i2p::data::PrivateKeys::CreateRandomKeys (
		i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
	sender.identity = sender.keys.GetPublic ();
	sender.identityBuffer.resize (sender.identity->GetFullLen ());
	sender.identity->ToBuffer (sender.identityBuffer.data (), sender.identityBuffer.size ());
	return sender;
}

std::vector<uint8_t> Store (const std::vector<uint8_t> & frame)
{
	std::vector<uint8_t> out (frame.size () + 23);
	auto len
		= i2p::data::GzipNoCompression (frame.data (), static_cast<uint16_t> (frame.size ()), out.data (), out.size ());
	assert (len == out.size ());
	return out;
}

void DispatchDatagram1 (
	const std::shared_ptr<i2p::datagram::DatagramDestination> & destination, const Sender & sender, uint16_t port)
{
	const uint8_t payload[] = { 0x11, 0x22, 0x33 };
	std::vector<uint8_t> signature (sender.keys.GetSignatureLen ());
	sender.keys.Sign (payload, sizeof (payload), signature.data ());
	std::vector<uint8_t> frame = sender.identityBuffer;
	frame.insert (frame.end (), signature.begin (), signature.end ());
	frame.insert (frame.end (), payload, payload + sizeof (payload));
	auto compressed = Store (frame);
	destination->HandleDataMessagePayload (
		0, port, compressed.data (), compressed.size (), i2p::client::PROTOCOL_TYPE_DATAGRAM, nullptr);
}

void DispatchDatagram3 (
	const std::shared_ptr<i2p::datagram::DatagramDestination> & destination, const Sender & sender, uint16_t port)
{
	const uint8_t flags[] = { 0x00, 0x03 };
	const auto & hash = sender.identity->GetIdentHash ();
	std::vector<uint8_t> frame (hash.data (), hash.data () + 32);
	frame.insert (frame.end (), flags, flags + 2);
	frame.push_back (0x44);
	auto compressed = Store (frame);
	destination->HandleDataMessagePayload (
		0, port, compressed.data (), compressed.size (), i2p::client::PROTOCOL_TYPE_DATAGRAM3, nullptr);
}

void TestIdentityRegistrationAndNoPromotion ()
{
	auto destination = MakeDatagramDestination ();
	auto sender = MakeSender ();
	int firstCalls = 0, secondPortCalls = 0, replacementCalls = 0, hashCalls = 0;
	auto first = destination->SetReceiver (
		[&] (const i2p::data::IdentityEx &, uint16_t, uint16_t, const uint8_t *, size_t, const i2p::util::Mapping *)
		{ ++firstCalls; }, 41);
	auto second = destination->SetReceiver (
		[&] (const i2p::data::IdentityEx &, uint16_t, uint16_t, const uint8_t *, size_t, const i2p::util::Mapping *)
		{ ++secondPortCalls; }, 42);

	DispatchDatagram1 (destination, sender, 41);
	DispatchDatagram1 (destination, sender, 42);
	assert (firstCalls == 1);
	assert (secondPortCalls == 1);

	// A port-specific registration is never promoted to the destination-wide
	// fallback (Java only falls back to an explicit PROTO_ANY/PORT_ANY
	// listener), so an unrelated port and the default port stay undelivered.
	DispatchDatagram1 (destination, sender, 99);
	DispatchDatagram1 (destination, sender, 0);
	assert (firstCalls == 1);
	assert (secondPortCalls == 1);

	// Port keys remain last-wins.
	auto replacement = destination->SetReceiver (
		[&] (const i2p::data::IdentityEx &, uint16_t, uint16_t, const uint8_t *, size_t, const i2p::util::Mapping *)
		{ ++replacementCalls; }, 41);
	DispatchDatagram1 (destination, sender, 41);
	assert (replacementCalls == 1);

	// A stale owner's token cannot remove a replacement at the same port or
	// the replacement's independent lifetime.
	assert (first.Reset ());
	DispatchDatagram1 (destination, sender, 41);
	assert (replacementCalls == 2);
	replacement.Reset ();
	DispatchDatagram1 (destination, sender, 41);
	assert (replacementCalls == 2);

	// Hash (Datagram3) receivers follow the same keying: only the addressed
	// port delivers, and the default slot stays empty.
	auto hash = destination->SetHashReceiver (
		[&] (const i2p::data::IdentHash &, uint16_t, uint16_t, const uint8_t *, size_t, const i2p::util::Mapping *)
		{ ++hashCalls; }, 41);
	DispatchDatagram3 (destination, sender, 41);
	assert (hashCalls == 1);
	DispatchDatagram3 (destination, sender, 42);
	DispatchDatagram3 (destination, sender, 0);
	assert (hashCalls == 1);

	hash.Reset ();
	second.Reset ();
}

void TestResetDrainsInFlightCallback ()
{
	auto destination = MakeDatagramDestination ();
	std::mutex mutex;
	std::condition_variable changed;
	bool entered = false, release = false;
	auto registration = destination->SetRawReceiver (
		[&] (uint8_t, uint16_t, uint16_t, const uint8_t *, size_t)
		{
			std::unique_lock<std::mutex> lock (mutex);
			entered = true;
			changed.notify_all ();
			changed.wait (lock, [&] { return release; });
		},
		73);

	std::thread dispatch ([&] { DispatchRaw (destination, 73); });
	{
		std::unique_lock<std::mutex> lock (mutex);
		changed.wait (lock, [&] { return entered; });
	}
	std::atomic<bool> resetStarted { false }, resetFinished { false }, resetDrained { false };
	std::thread reset (
		[&]
		{
			resetStarted.store (true, std::memory_order_release);
			resetDrained.store (registration.Reset (), std::memory_order_release);
			resetFinished.store (true, std::memory_order_release);
		});
	while (!resetStarted.load (std::memory_order_acquire)) std::this_thread::yield ();
	std::this_thread::sleep_for (std::chrono::milliseconds (10));
	assert (!resetFinished.load (std::memory_order_acquire));
	{
		std::lock_guard<std::mutex> lock (mutex);
		release = true;
	}
	changed.notify_all ();
	dispatch.join ();
	reset.join ();
	assert (resetFinished.load (std::memory_order_acquire));
	assert (resetDrained.load (std::memory_order_acquire));
	DispatchRaw (destination, 73);
}

void TestSelfResetDoesNotDeadlock ()
{
	auto destination = MakeDatagramDestination ();
	i2p::datagram::DatagramDestination::ReceiverRegistration registration;
	bool invoked = false;
	registration = destination->SetRawReceiver (
		[&] (uint8_t, uint16_t, uint16_t, const uint8_t *, size_t)
		{
			invoked = true;
			assert (!registration.Reset ());
		},
		74);
	DispatchRaw (destination, 74);
	assert (invoked);
	invoked = false;
	DispatchRaw (destination, 74);
	assert (!invoked);
}

void TestConcurrentSelfResetDoesNotDeadlock ()
{
	using Callback = std::function<void ()>;
	using Slot = i2p::datagram::detail::ReceiverSlot<Callback>;
	std::atomic<unsigned> entered { 0 };
	Slot * slotPtr = nullptr;
	auto slot = std::make_shared<Slot> (
		[&]
		{
			entered.fetch_add (1, std::memory_order_acq_rel);
			while (entered.load (std::memory_order_acquire) < 2) std::this_thread::yield ();
			slotPtr->DeactivateAndWait ();
		});
	slotPtr = slot.get ();
	std::thread first ([&] { slot->Invoke (); });
	std::thread second ([&] { slot->Invoke (); });
	first.join ();
	second.join ();
	assert (entered.load (std::memory_order_acquire) == 2);
	assert (!slot->Invoke ());
}

void TestCrossSlotSelfResetDoesNotDeadlock ()
{
	using Callback = std::function<void ()>;
	using Slot = i2p::datagram::detail::ReceiverSlot<Callback>;
	std::atomic<unsigned> entered { 0 };
	std::shared_ptr<Slot> first, second;
	auto resetOther = [&] (const std::shared_ptr<Slot> & other)
	{
		entered.fetch_add (1, std::memory_order_acq_rel);
		while (entered.load (std::memory_order_acquire) < 2) std::this_thread::yield ();
		assert (!other->DeactivateAndWait ());
	};
	first = std::make_shared<Slot> ([&] { resetOther (second); });
	second = std::make_shared<Slot> ([&] { resetOther (first); });
	std::thread a ([&] { first->Invoke (); });
	std::thread b ([&] { second->Invoke (); });
	a.join ();
	b.join ();
	assert (entered.load (std::memory_order_acquire) == 2);
}

void TestEachDispatchUsesCallbackSnapshot ()
{
	using Callback = std::function<void ()>;
	using Slot = i2p::datagram::detail::ReceiverSlot<Callback>;
	std::vector<unsigned> observations;
	auto slot = std::make_shared<Slot> ([calls = 0U, &observations] () mutable { observations.push_back (++calls); });
	slot->Invoke ();
	slot->Invoke ();
	assert (observations.size () == 2);
	assert (observations[0] == 1 && observations[1] == 1);
}

void TestReentrantCallbackDestructionOutsideRegistryLock ()
{
	auto destination = MakeDatagramDestination ();
	struct Reentrant
	{
		std::shared_ptr<i2p::datagram::DatagramDestination> destination;
		~Reentrant ()
		{
			destination->SetRawReceiver ([] (uint8_t, uint16_t, uint16_t, const uint8_t *, size_t) { }, 89);
		}
	};
	auto captured = std::make_shared<Reentrant> ();
	captured->destination = destination;
	// Retain only the registration/map references after this scope. Replacing
	// both map and fallback entries releases the callback outside the lock.
	auto original
		= destination->SetRawReceiver ([captured] (uint8_t, uint16_t, uint16_t, const uint8_t *, size_t) { }, 89);
	captured.reset ();
	auto portReplacement
		= destination->SetRawReceiver ([] (uint8_t, uint16_t, uint16_t, const uint8_t *, size_t) { }, 89);
	auto wildcardReplacement
		= destination->SetRawReceiver ([] (uint8_t, uint16_t, uint16_t, const uint8_t *, size_t) { });
	original.Reset ();
	portReplacement.Reset ();
	wildcardReplacement.Reset ();
}
}

int main ()
{
	TestIdentityRegistrationAndNoPromotion ();
	TestResetDrainsInFlightCallback ();
	TestSelfResetDoesNotDeadlock ();
	TestConcurrentSelfResetDoesNotDeadlock ();
	TestCrossSlotSelfResetDoesNotDeadlock ();
	TestEachDispatchUsesCallbackSnapshot ();
	TestReentrantCallbackDestructionOutsideRegistryLock ();
	return 0;
}
