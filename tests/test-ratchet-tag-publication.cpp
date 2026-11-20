/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * Receive-tag publication must not skip chain state when allocation
 * fails. ElGamal confirmation must not publish a partial one-time tag
 * list. These deterministic fixtures use no executor or network peer.
 */

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <new>
#include <utility>
#include <vector>
#include "ECIESX25519AEADRatchetSession.h"
#include "Garlic.h"
#include "Timestamp.h"

namespace i2p::garlic
{
struct RatchetTagPublicationTestAccess
{
	struct Failure final : std::bad_alloc
	{
	};
	struct State
	{
		std::array<uint8_t, 64> chain;
		int next;
		bool operator== (const State &) const = default;
	};
	inline static bool reached = false;
	inline static unsigned remaining = 0;
	static void Fail ()
	{
		if (remaining-- == 0)
		{
			reached = true;
			throw Failure { };
		}
	}
	static State Capture (const RatchetTagSet & tagset)
	{
		State state { { }, tagset.m_NextIndex };
		std::memcpy (state.chain.data (), tagset.m_SessionTagKeyData.data (), state.chain.size ());
		return state;
	}
	static void SetIndex (RatchetTagSet & tagset, int index) { tagset.m_NextIndex = index; }
	static void CheckInPlaceDerivation (RatchetTagSet & tagset)
	{
		// Preserve the pre-transaction in-place HKDF transcript exactly.
		auto expected = tagset.m_SessionTagKeyData;
		const auto previousIndex = tagset.m_NextIndex;
		i2p::crypto::HKDFContext reference, actual;
		reference (expected, tagset.m_SessTagConstant, 32, "SessionTagKeyGen", expected);
		const auto expectedTag = expected.GetLL ()[4];
		assert (expectedTag);
		assert (tagset.GetNextSessionTag (actual) == expectedTag);
		assert (tagset.m_SessionTagKeyData == expected);
		assert (tagset.m_NextIndex == previousIndex + 1);
		expected.Wipe ();
	}

	static bool FailPublication (GarlicDestination & destination, ReceiveRatchetTagSetPtr tagset)
	{
		reached = false;
		remaining = 0;
		try
		{
			destination.AddECIESx25519SessionNextTagImpl (std::move (tagset), &Fail);
		}
		catch (const Failure &)
		{
			return reached;
		}
		return false;
	}
	static void CheckEntry (
		const GarlicDestination & destination, uint64_t tag, int index, const ReceiveRatchetTagSetPtr & tagset)
	{
		const auto it = destination.m_ECIESx25519Tags.find (tag);
		assert (it != destination.m_ECIESx25519Tags.end ());
		assert (it->second.index == index && it->second.tagset == tagset);
	}
	static void CheckElGamalConfirmation (unsigned after)
	{
		std::array<uint8_t, 32> key, tagBytes;
		key.fill (0x51);
		tagBytes.fill (0x61);
		ElGamalAESSession session (key.data (), SessionTag (tagBytes.data ()));
		const std::vector<SessionTag> incumbent (session.m_SessionTags.begin (), session.m_SessionTags.end ());
		const auto now = static_cast<uint32_t> (i2p::util::GetSecondsSinceEpoch ());
		auto pending = std::make_unique<ElGamalAESSession::UnconfirmedTags> (3);
		pending->tagsCreationTime = now;
		for (size_t i = 0; i < pending->sessionTags.size (); ++i)
		{
			pending->sessionTags[i].Fill (static_cast<uint8_t> (i + 0x71));
			pending->sessionTags[i].creationTime = now;
		}
		const auto announced = pending->sessionTags;
		constexpr uint32_t message = 42;
		session.m_UnconfirmedTagsMsgs.emplace (message, std::move (pending));
		reached = false;
		remaining = after;
		bool threw = false;
		try
		{
			session.TagsConfirmedImpl (message, &Fail);
		}
		catch (const Failure &)
		{
			threw = true;
		}
		assert (threw && reached);
		assert (std::vector<SessionTag> (session.m_SessionTags.begin (), session.m_SessionTags.end ()) == incumbent);
		assert (session.m_UnconfirmedTagsMsgs.size () == 1);
		assert (session.m_UnconfirmedTagsMsgs.at (message)->sessionTags == announced);
		session.TagsConfirmed (message);
		assert (session.m_UnconfirmedTagsMsgs.empty ());
		std::vector<SessionTag> expected = incumbent;
		expected.insert (expected.end (), announced.begin (), announced.end ());
		const auto actual = std::vector<SessionTag> (session.m_SessionTags.begin (), session.m_SessionTags.end ());
		assert (actual == expected);
		session.TagsConfirmed (message);
		assert (std::vector<SessionTag> (session.m_SessionTags.begin (), session.m_SessionTags.end ()) == expected);
		// Expired and unknown confirmations cannot publish any additional tag.
		auto expired = std::make_unique<ElGamalAESSession::UnconfirmedTags> (1);
		expired->tagsCreationTime = now - OUTGOING_TAGS_EXPIRATION_TIMEOUT - 1;
		expired->sessionTags[0].Fill (0x7f);
		session.m_UnconfirmedTagsMsgs.emplace (message + 1, std::move (expired));
		session.TagsConfirmed (message + 1);
		session.TagsConfirmed (message + 2);
		assert (session.m_UnconfirmedTagsMsgs.empty ());
		assert (std::vector<SessionTag> (session.m_SessionTags.begin (), session.m_SessionTags.end ()) == expected);
	}
	static void InsertCollision (GarlicDestination & destination, uint64_t tag)
	{
		destination.m_ECIESx25519Tags.emplace (tag, ECIESX25519AEADRatchetIndexTagset { 17, nullptr });
	}
};
}

namespace
{
class Destination final : public i2p::garlic::GarlicDestination
{
public:
	bool Decrypt (const uint8_t *, uint8_t *, i2p::data::CryptoKeyType) const override { return false; }
	std::shared_ptr<const i2p::data::IdentityEx> GetIdentity () const override { return nullptr; }
	std::shared_ptr<const i2p::data::LocalLeaseSet> GetLeaseSet () override { return nullptr; }
	std::shared_ptr<i2p::tunnel::TunnelPool> GetTunnelPool () const override { return nullptr; }

protected:
	void HandleI2NPMessage (const uint8_t *, size_t) override { }
	bool HandleCloveI2NPMessage (
		i2p::I2NPMessageType, const uint8_t *, size_t, uint32_t, i2p::garlic::ECIESX25519AEADRatchetSession *) override
	{
		return false;
	}
};

using Access = i2p::garlic::RatchetTagPublicationTestAccess;

void Initialize (i2p::garlic::RatchetTagSet & tagset)
{
	std::array<uint8_t, 32> root, input;
	root.fill (0x19);
	input.fill (0x42);
	tagset.DHInitialize (root.data (), input.data ());
	tagset.NextSessionTagRatchet ();
}

void CheckFailure (bool warm)
{
	Destination destination;
	auto candidate = std::make_shared<i2p::garlic::ReceiveRatchetTagSet> (nullptr);
	i2p::garlic::RatchetTagSet reference;
	Initialize (*candidate);
	Initialize (reference);
	i2p::crypto::HKDFContext context;
	if (warm)
		for (unsigned i = 0; i < 2; ++i)
		{
			const auto expected = reference.GetNextSessionTag (context);
			assert (expected && destination.AddECIESx25519SessionNextTag (candidate) == expected);
		}
	const auto before = Access::Capture (*candidate);
	const auto count = destination.GetNumIncomingECIESx25519Tags ();
	assert (Access::FailPublication (destination, candidate));
	assert (Access::Capture (*candidate) == before);
	assert (destination.GetNumIncomingECIESx25519Tags () == count);
	const auto expected = reference.GetNextSessionTag (context);
	assert (expected); // Do not let an unavailable provider pass vacuously.
	assert (destination.AddECIESx25519SessionNextTag (candidate) == expected);
	assert (Access::Capture (*candidate) == Access::Capture (reference));
	assert (destination.GetNumIncomingECIESx25519Tags () == count + 1);
	Access::CheckEntry (destination, expected, before.next, candidate);
}

void CheckCollisionAndExhaustion ()
{
	Destination destination;
	auto candidate = std::make_shared<i2p::garlic::ReceiveRatchetTagSet> (nullptr);
	i2p::garlic::RatchetTagSet reference;
	Initialize (*candidate);
	Initialize (reference);
	i2p::crypto::HKDFContext context;
	const auto collision = reference.GetNextSessionTag (context);
	assert (collision);
	Access::InsertCollision (destination, collision);
	assert (destination.AddECIESx25519SessionNextTag (candidate) == collision);
	Access::CheckEntry (destination, collision, 17, nullptr);
	assert (Access::Capture (*candidate) == Access::Capture (reference));

	Access::SetIndex (*candidate, 0xffff);
	Access::SetIndex (reference, 0xffff);
	const auto last = reference.GetNextSessionTag (context);
	assert (last && destination.AddECIESx25519SessionNextTag (candidate) == last);
	assert (candidate->GetNextIndex () == 0x10000);
	const auto exhausted = Access::Capture (*candidate);
	const auto count = destination.GetNumIncomingECIESx25519Tags ();
	assert (!destination.AddECIESx25519SessionNextTag (candidate));
	assert (Access::Capture (*candidate) == exhausted);
	assert (destination.GetNumIncomingECIESx25519Tags () == count);
}
}

int main ()
{
	i2p::garlic::RatchetTagSet compatible;
	Initialize (compatible);
	for (unsigned i = 0; i < 3; ++i) Access::CheckInPlaceDerivation (compatible);
	CheckFailure (false);
	CheckFailure (true);
	CheckCollisionAndExhaustion ();
	for (unsigned after : { 0U, 1U, 2U }) Access::CheckElGamalConfirmation (after);
}
