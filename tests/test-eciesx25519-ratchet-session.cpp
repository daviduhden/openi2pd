/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test eciesx25519 ratchet session.
 *
 * Runs the ECIES-X25519-AEAD-Ratchet New Session/New Session Reply handshake between in-process destinations, validating payload block ordering, reply-split loss in both directions, delayed replies, tagset exhaustion, the router-N envelope, replay handling, retransmission and established-session traffic.
 *
 */

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "Crypto.h"
#include "ECIESX25519AEADRatchetSession.h"
#include "Garlic.h"
#include "I2NPProtocol.h"
#include "I2PEndian.h"
#include "Identity.h"
#include "Timestamp.h"
#include "TunnelPool.h"

namespace
{
using i2p::data::CryptoKeyType;
using i2p::data::IdentityEx;
using i2p::garlic::ECIESX25519AEADRatchetSession;

struct ReceivedClove
{
	i2p::I2NPMessageType type;
	uint32_t msgID;
	std::vector<uint8_t> payload;
};

class TestDestination : public i2p::garlic::GarlicDestination
{
public:
	explicit TestDestination (CryptoKeyType type)
		: m_Type (type)
	{
		m_Keys.GenerateKeys ();
		uint8_t signingKey[32] = { };
		for (size_t i = 0; i < sizeof (signingKey); i++) signingKey[i] = static_cast<uint8_t> (0x40 + i);
		m_Identity = std::make_shared<IdentityEx> (
			m_Keys.GetPublicKey (), signingKey, i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, type);
	}

	const uint8_t * GetPublicKey () const { return m_Keys.GetPublicKey (); }
	CryptoKeyType GetCryptoKeyType () const { return m_Type; }
	const std::vector<ReceivedClove> & Received () const { return m_Received; }
	void ClearReceived () { m_Received.clear (); }

	bool Decrypt (const uint8_t * encrypted, uint8_t * data, CryptoKeyType /*preferredCrypto*/) const override
	{
		return m_Keys.Agree (encrypted, data);
	}

	std::shared_ptr<const IdentityEx> GetIdentity () const override { return m_Identity; }

	bool SupportsEncryptionType (CryptoKeyType keyType) const override { return keyType == m_Type; }
	const uint8_t * GetEncryptionPublicKey (CryptoKeyType keyType) const override
	{
		return keyType == m_Type ? m_Keys.GetPublicKey () : nullptr;
	}
	CryptoKeyType GetRatchetsHighestCryptoType () const override { return m_Type; }

	std::shared_ptr<const i2p::data::LocalLeaseSet> GetLeaseSet () override { return nullptr; }
	std::shared_ptr<i2p::tunnel::TunnelPool> GetTunnelPool () const override { return nullptr; }

protected:
	void HandleI2NPMessage (const uint8_t *, size_t) override { }

	bool HandleCloveI2NPMessage (i2p::I2NPMessageType typeID, const uint8_t * payload, size_t len, uint32_t msgID,
		ECIESX25519AEADRatchetSession * /*from*/) override
	{
		m_Received.push_back ({ typeID, msgID, std::vector<uint8_t> (payload, payload + len) });
		return true;
	}

private:
	CryptoKeyType m_Type;
	mutable i2p::crypto::X25519Keys m_Keys;
	std::shared_ptr<const IdentityEx> m_Identity;
	std::vector<ReceivedClove> m_Received;
};

class TestRoutingDestination : public i2p::data::RoutingDestination
{
public:
	TestRoutingDestination (std::shared_ptr<const IdentityEx> identity, const uint8_t * publicKey, CryptoKeyType type)
		: m_Identity (std::move (identity))
		, m_Type (type)
	{
		std::memcpy (m_PublicKey, publicKey, sizeof (m_PublicKey));
	}

	std::shared_ptr<const IdentityEx> GetIdentity () const override { return m_Identity; }
	bool Encrypt (const uint8_t *, uint8_t * encrypted) const override
	{
		if (!encrypted) return false;
		std::memcpy (encrypted, m_PublicKey, sizeof (m_PublicKey));
		return true;
	}
	bool IsDestination () const override { return true; }
	CryptoKeyType GetEncryptionType () const override { return m_Type; }

private:
	std::shared_ptr<const IdentityEx> m_Identity;
	uint8_t m_PublicKey[32] = { };
	CryptoKeyType m_Type;
};

std::shared_ptr<i2p::I2NPMessage> MakeDataMessage (const std::vector<uint8_t> & body, uint32_t msgID)
{
	return i2p::CreateI2NPMessage (i2p::eI2NPData, body.data (), body.size (), msgID);
}

struct Peer
{
	std::unique_ptr<TestDestination> destination;
	std::shared_ptr<TestRoutingDestination> routing;
	std::shared_ptr<ECIESX25519AEADRatchetSession> session;
};

Peer MakePeer (CryptoKeyType type)
{
	Peer peer;
	peer.destination = std::make_unique<TestDestination> (type);
	peer.routing = std::make_shared<TestRoutingDestination> (
		peer.destination->GetIdentity (), peer.destination->GetPublicKey (), type);
	return peer;
}

std::shared_ptr<i2p::I2NPMessage> Establish (Peer & alice, Peer & bob, const std::vector<uint8_t> & firstBody)
{
	alice.session = std::static_pointer_cast<ECIESX25519AEADRatchetSession> (
		alice.destination->GetRoutingSession (bob.routing, true));
	assert (alice.session);
	alice.session->SetRemoteStaticKey (bob.destination->GetCryptoKeyType (), bob.destination->GetPublicKey ());
	alice.session->SetDestination (bob.destination->GetIdentHash ());

	auto ns = alice.session->WrapSingleMessage (MakeDataMessage (firstBody, 0x11110001));
	assert (ns);
	assert (ns->GetPayloadLength () >= 96);
	bob.destination->ProcessGarlicMessage (ns);
	assert (bob.destination->Received ().size () == 1);
	assert (bob.destination->Received ().back ().payload == firstBody);

	bob.session = std::static_pointer_cast<ECIESX25519AEADRatchetSession> (
		bob.destination->GetRoutingSession (alice.routing, false));
	assert (bob.session);
	return ns;
}

void CheckHandshakePayloadValidation ()
{
	const uint64_t now = i2p::util::GetSecondsSinceEpoch ();
	std::vector<uint8_t> payload;

	auto append = [&payload] (uint8_t type, const std::vector<uint8_t> & data)
	{
		payload.push_back (type);
		payload.push_back (static_cast<uint8_t> (data.size () >> 8));
		payload.push_back (static_cast<uint8_t> (data.size ()));
		payload.insert (payload.end (), data.begin (), data.end ());
	};
	auto dateTime = [now] ()
	{
		const uint32_t ts = static_cast<uint32_t> (now);
		return std::vector<uint8_t> { static_cast<uint8_t> (ts >> 24), static_cast<uint8_t> (ts >> 16),
			static_cast<uint8_t> (ts >> 8), static_cast<uint8_t> (ts) };
	};

	// Java accepts an empty NS clove set without creating a session; openi2pd
	// deliberately requires a non-empty Garlic block, so DateTime-only fails.
	append (i2p::garlic::eECIESx25519BlkDateTime, dateTime ());
	assert (!i2p::garlic::ValidateNewSessionPayload (payload.data (), payload.size ()));

	// Mandatory and unique.
	assert (!i2p::garlic::ValidateNewSessionPayload (nullptr, 0));
	payload.clear ();
	append (i2p::garlic::eECIESx25519BlkOptions, { 0 });
	assert (!i2p::garlic::ValidateNewSessionPayload (payload.data (), payload.size ()));
	append (i2p::garlic::eECIESx25519BlkDateTime, dateTime ());
	assert (!i2p::garlic::ValidateNewSessionPayload (payload.data (), payload.size ()));

	// Stale and future DateTime are rejected (5 minutes / 2 minutes).
	{
		std::vector<uint8_t> stale;
		const uint32_t ts = static_cast<uint32_t> (now - 5 * 60 - 1);
		stale = { i2p::garlic::eECIESx25519BlkDateTime, 0, 4, static_cast<uint8_t> (ts >> 24),
			static_cast<uint8_t> (ts >> 16), static_cast<uint8_t> (ts >> 8), static_cast<uint8_t> (ts) };
		assert (!i2p::garlic::ValidateNewSessionPayload (stale.data (), stale.size ()));
		const uint32_t future = static_cast<uint32_t> (now + 2 * 60 + 1);
		stale = { i2p::garlic::eECIESx25519BlkDateTime, 0, 4, static_cast<uint8_t> (future >> 24),
			static_cast<uint8_t> (future >> 16), static_cast<uint8_t> (future >> 8), static_cast<uint8_t> (future) };
		assert (!i2p::garlic::ValidateNewSessionPayload (stale.data (), stale.size ()));
	}

	// A second DateTime, a Termination/SessionID/unknown block, a zero-length
	// Garlic block, a malformed ACK size and any block after Padding fail.
	payload.clear ();
	append (i2p::garlic::eECIESx25519BlkDateTime, dateTime ());
	append (i2p::garlic::eECIESx25519BlkDateTime, dateTime ());
	assert (!i2p::garlic::ValidateNewSessionPayload (payload.data (), payload.size ()));

	payload.clear ();
	append (i2p::garlic::eECIESx25519BlkDateTime, dateTime ());
	append (i2p::garlic::eECIESx25519BlkTermination, { 0 });
	assert (!i2p::garlic::ValidateNewSessionPayload (payload.data (), payload.size ()));

	payload.clear ();
	append (i2p::garlic::eECIESx25519BlkDateTime, dateTime ());
	append (i2p::garlic::eECIESx25519BlkSessionID, { 0, 0, 0, 1 });
	assert (!i2p::garlic::ValidateNewSessionPayload (payload.data (), payload.size ()));

	payload.clear ();
	append (i2p::garlic::eECIESx25519BlkDateTime, dateTime ());
	append (i2p::garlic::eECIESx25519BlkGalicClove, { });
	assert (!i2p::garlic::ValidateNewSessionPayload (payload.data (), payload.size ()));

	payload.clear ();
	append (i2p::garlic::eECIESx25519BlkDateTime, dateTime ());
	append (i2p::garlic::eECIESx25519BlkAck, { 0, 0, 0 });
	assert (!i2p::garlic::ValidateNewSessionPayload (payload.data (), payload.size ()));

	payload.clear ();
	append (i2p::garlic::eECIESx25519BlkDateTime, dateTime ());
	append (i2p::garlic::eECIESx25519BlkPadding, { 0, 0, 0 });
	append (i2p::garlic::eECIESx25519BlkOptions, { 0 });
	assert (!i2p::garlic::ValidateNewSessionPayload (payload.data (), payload.size ()));

	// Valid: DateTime, Garlic, ACK and ACK request, Padding last.
	payload.clear ();
	append (i2p::garlic::eECIESx25519BlkDateTime, dateTime ());
	append (i2p::garlic::eECIESx25519BlkGalicClove, { 0, 20, 0, 0, 0, 1, 0, 0, 0, 0 });
	append (i2p::garlic::eECIESx25519BlkAck, { 0, 0, 0, 1 });
	append (i2p::garlic::eECIESx25519BlkAckRequest, { 0 });
	append (i2p::garlic::eECIESx25519BlkPadding, { 0, 0 });
	assert (i2p::garlic::ValidateNewSessionPayload (payload.data (), payload.size ()));

	// Truncated header and block overrun fail.
	payload = { i2p::garlic::eECIESx25519BlkDateTime, 0 };
	assert (!i2p::garlic::ValidateNewSessionPayload (payload.data (), payload.size ()));
	payload = { i2p::garlic::eECIESx25519BlkDateTime, 0, 4, 0, 0, 0 };
	assert (!i2p::garlic::ValidateNewSessionPayload (payload.data (), payload.size ()));

	// Established/NSR payloads are validated before any clove, ACK or ratchet
	// side effect. A stale DateTime must reject the whole payload.
	payload.clear ();
	{
		const uint32_t stale = static_cast<uint32_t> (now - 5 * 60 - 1);
		append (i2p::garlic::eECIESx25519BlkDateTime,
			{ static_cast<uint8_t> (stale >> 24), static_cast<uint8_t> (stale >> 16), static_cast<uint8_t> (stale >> 8),
				static_cast<uint8_t> (stale) });
	}
	append (i2p::garlic::eECIESx25519BlkGalicClove, { 0, 20, 0, 0, 0, 1, 0, 0, 0, 0 });
	assert (!i2p::garlic::ValidateRatchetPayload (payload.data (), payload.size ()));

	// Unknown blocks are forward-compatible outside the handshake, but not
	// after Padding or in a New Session.
	payload.clear ();
	append (i2p::garlic::eECIESx25519BlkDateTime, dateTime ());
	append (i2p::garlic::eECIESx25519BlkGalicClove, { 0, 20, 0, 0, 0, 1, 0, 0, 0, 0 });
	append (0x7f, { 0 });
	assert (i2p::garlic::ValidateRatchetPayload (payload.data (), payload.size ()));
	assert (!i2p::garlic::ValidateNewSessionPayload (payload.data (), payload.size ()));
	append (i2p::garlic::eECIESx25519BlkPadding, { 0 });
	append (i2p::garlic::eECIESx25519BlkOptions, { 0 });
	assert (!i2p::garlic::ValidateRatchetPayload (payload.data (), payload.size ()));

	// An established payload may be empty, but a handshake may not.
	assert (i2p::garlic::ValidateRatchetPayload (nullptr, 0));
	assert (!i2p::garlic::ValidateNewSessionPayload (nullptr, 0));

	// A New Session Reply may carry only DateTime, Garlic, Options and
	// Padding. An empty reply is accepted like Java, which merely warns.
	assert (i2p::garlic::ValidateNewSessionReplyPayload (nullptr, 0));
	payload.clear ();
	append (i2p::garlic::eECIESx25519BlkDateTime, dateTime ());
	append (i2p::garlic::eECIESx25519BlkGalicClove, { 0, 20, 0, 0, 0, 1, 0, 0, 0, 0 });
	append (i2p::garlic::eECIESx25519BlkOptions, { 0 });
	append (i2p::garlic::eECIESx25519BlkPadding, { 0, 0 });
	assert (i2p::garlic::ValidateNewSessionReplyPayload (payload.data (), payload.size ()));

	// Termination, NEXT_KEY, SessionID, ACK, ACK_REQUEST and unknown blocks
	// are all rejected before any split or session side effect. In particular
	// a Termination block must never reach RemoveECIESx25519Session from an
	// NSR.
	{
		struct RejectedBlock
		{
			uint8_t type;
			std::vector<uint8_t> data;
		};
		const RejectedBlock rejected[] = {
			{ i2p::garlic::eECIESx25519BlkTermination, { 0 } },
			{ i2p::garlic::eECIESx25519BlkNextKey, { 0, 0, 0 } },
			{ i2p::garlic::eECIESx25519BlkSessionID, { 0, 0, 0, 1 } },
			{ i2p::garlic::eECIESx25519BlkAck, { 0, 0, 0, 1 } },
			{ i2p::garlic::eECIESx25519BlkAckRequest, { 0 } },
			{ 0x7f, { 0 } },
		};
		for (const auto & block : rejected)
		{
			payload.clear ();
			append (i2p::garlic::eECIESx25519BlkDateTime, dateTime ());
			append (block.type, block.data);
			assert (!i2p::garlic::ValidateNewSessionReplyPayload (payload.data (), payload.size ()));
		}
		// The same well-framed NEXT_KEY/ACK blocks stay valid for an
		// established session.
		payload.clear ();
		append (i2p::garlic::eECIESx25519BlkDateTime, dateTime ());
		append (i2p::garlic::eECIESx25519BlkAck, { 0, 0, 0, 1 });
		append (i2p::garlic::eECIESx25519BlkAckRequest, { 0 });
		assert (i2p::garlic::ValidateRatchetPayload (payload.data (), payload.size ()));
		assert (!i2p::garlic::ValidateNewSessionReplyPayload (payload.data (), payload.size ()));
	}
}

void InteropLoop (CryptoKeyType type, const char * label)
{
	Peer alice = MakePeer (type);
	Peer bob = MakePeer (type);
	const std::vector<uint8_t> first { 1, 2, 3, 4, 5 };
	Establish (alice, bob, first);

	// Existing session in both directions.
	bob.destination->ClearReceived ();
	const std::vector<uint8_t> reply { 0x80, 0x81, 0x82 };
	auto bobMessage = bob.session->WrapSingleMessage (MakeDataMessage (reply, 0x22220002));
	assert (bobMessage);
	alice.destination->ProcessGarlicMessage (bobMessage);
	assert (alice.destination->Received ().size () == 1);
	assert (alice.destination->Received ().back ().payload == reply);

	alice.destination->ClearReceived ();
	const std::vector<uint8_t> again { 9, 8, 7, 6 };
	auto aliceMessage = alice.session->WrapSingleMessage (MakeDataMessage (again, 0x33330003));
	assert (aliceMessage);
	bob.destination->ProcessGarlicMessage (aliceMessage);
	assert (bob.destination->Received ().size () == 1);
	assert (bob.destination->Received ().back ().payload == again);

	// Duplicate New Session ephemeral key is rejected before any new session
	// is created, even when the caller bypasses the per-destination rate
	// limiter (as a replay could).
	{
		Peer carol = MakePeer (type);
		Peer dave = MakePeer (type);
		auto ns = Establish (carol, dave, first);
		// The destination may decrypt in place, so keep an untouched copy.
		auto replayMessage = i2p::CopyI2NPMessage (ns);
		auto replay = std::make_shared<ECIESX25519AEADRatchetSession> (dave.destination.get (), false);
		uint8_t * replayData = replayMessage->GetPayload () + 4;
		const size_t replayLen = replayMessage->GetPayloadLength () - 4;
		assert (!replay->HandleNextMessage (replayData, replayLen, nullptr, 0));

		// A tampered copy of a fresh New Session fails authentication and is
		// not added to the replay cache, so the genuine message still works.
		Peer erin = MakePeer (type);
		Peer frank = MakePeer (type);
		erin.session = std::static_pointer_cast<ECIESX25519AEADRatchetSession> (
			erin.destination->GetRoutingSession (frank.routing, true));
		erin.session->SetRemoteStaticKey (type, frank.destination->GetPublicKey ());
		erin.session->SetDestination (frank.destination->GetIdentHash ());
		auto genuine = erin.session->WrapSingleMessage (MakeDataMessage (first, 0x55550005));
		assert (genuine);
		auto tampered = i2p::CopyI2NPMessage (genuine);
		uint8_t * tamperedData = tampered->GetPayload () + 4;
		const size_t tamperedLen = tampered->GetPayloadLength () - 4;
		tamperedData[tamperedLen - 1] ^= 0x01; // payload MAC
		frank.destination->ProcessGarlicMessage (tampered);
		assert (frank.destination->Received ().empty ());
		frank.destination->ProcessGarlicMessage (genuine);
		assert (frank.destination->Received ().size () == 1);
	}

	// New Session Reply retransmission: two replies with different tags and
	// different ephemeral keys; the ES chain follows the latest accepted one.
	{
		Peer lisa = MakePeer (type);
		Peer mark = MakePeer (type);
		Establish (lisa, mark, first);

		const std::vector<uint8_t> one { 0x31 };
		const std::vector<uint8_t> two { 0x32 };
		auto nsr1 = mark.session->WrapSingleMessage (MakeDataMessage (one, 0x66660006));
		auto nsr2 = mark.session->WrapSingleMessage (MakeDataMessage (two, 0x77770007));
		assert (nsr1 && nsr2);

		const uint8_t * p1 = nsr1->GetPayload () + 4;
		const uint8_t * p2 = nsr2->GetPayload () + 4;
		assert (std::memcmp (p1, p2, 8) != 0); // fresh tag
		assert (std::memcmp (p1 + 8, p2 + 8, 32) != 0); // fresh Elligator2 BEPK

		// Both replies arrive; the second one is the newest split.
		lisa.destination->ProcessGarlicMessage (nsr1);
		lisa.destination->ProcessGarlicMessage (nsr2);
		assert (lisa.destination->Received ().size () == 2);

		lisa.destination->ClearReceived ();
		mark.destination->ClearReceived ();
		const std::vector<uint8_t> after { 0x41, 0x42 };
		auto es = lisa.session->WrapSingleMessage (MakeDataMessage (after, 0x88880008));
		assert (es);
		mark.destination->ProcessGarlicMessage (es);
		assert (mark.destination->Received ().size () == 1);
		assert (mark.destination->Received ().back ().payload == after);

		// A lost first reply must not prevent the handshake from completing.
		Peer nina = MakePeer (type);
		Peer oscar = MakePeer (type);
		Establish (nina, oscar, first);
		oscar.destination->ClearReceived ();
		auto nsrA = oscar.session->WrapSingleMessage (MakeDataMessage (one, 0x99990009));
		auto nsrB = oscar.session->WrapSingleMessage (MakeDataMessage (two, 0xAAAA000A));
		assert (nsrA && nsrB);
		nina.destination->ProcessGarlicMessage (nsrB); // nsrA is lost
		const std::vector<uint8_t> afterLost { 0x51 };
		auto esLost = nina.session->WrapSingleMessage (MakeDataMessage (afterLost, 0xBBBB000B));
		assert (esLost);
		oscar.destination->ProcessGarlicMessage (esLost);
		assert (oscar.destination->Received ().size () == 1);
		assert (oscar.destination->Received ().back ().payload == afterLost);

		// The mirror case: the last reply is lost. Bob must not keep sending
		// on a split Alice never saw; he retains both splits and promotes the
		// one her first ES message actually uses.
		Peer quinn = MakePeer (type);
		Peer rita = MakePeer (type);
		Establish (quinn, rita, first);
		quinn.destination->ClearReceived ();
		rita.destination->ClearReceived ();
		auto nsrC = rita.session->WrapSingleMessage (MakeDataMessage (one, 0xCCCC0001));
		auto nsrD = rita.session->WrapSingleMessage (MakeDataMessage (two, 0xDDDD0002));
		assert (nsrC && nsrD);
		assert (rita.session->GetNumPendingReplyTagsets () == 2);
		quinn.destination->ProcessGarlicMessage (nsrC); // nsrD is lost
		assert (quinn.session->GetNumPendingReplyTagsets () == 0);
		const std::vector<uint8_t> forward { 0x61 };
		auto esC = quinn.session->WrapSingleMessage (MakeDataMessage (forward, 0xEEEE0003));
		assert (esC);
		rita.destination->ProcessGarlicMessage (esC);
		assert (rita.destination->Received ().size () == 1);
		// Bob now sends on the promoted split, which Alice can decrypt.
		rita.destination->ClearReceived ();
		quinn.destination->ClearReceived ();
		const std::vector<uint8_t> backward { 0x62, 0x63 };
		auto esD = rita.session->WrapSingleMessage (MakeDataMessage (backward, 0xFFFF0004));
		assert (esD);
		quinn.destination->ProcessGarlicMessage (esD);
		assert (quinn.destination->Received ().size () == 1);
		assert (quinn.destination->Received ().back ().payload == backward);

		// A delayed New Session Reply after establishment must not rewind the
		// outbound tagset ID or its tag index.
		Peer sam = MakePeer (type);
		Peer tom = MakePeer (type);
		Establish (sam, tom, first);
		sam.destination->ClearReceived ();
		tom.destination->ClearReceived ();
		auto nsrE = tom.session->WrapSingleMessage (MakeDataMessage (one, 0x12340001));
		auto nsrF = tom.session->WrapSingleMessage (MakeDataMessage (two, 0x12340002));
		auto nsrG = tom.session->WrapSingleMessage (MakeDataMessage (one, 0x12340003));
		assert (nsrE && nsrF && nsrG);
		sam.destination->ProcessGarlicMessage (nsrE);
		assert (sam.session->GetSendTagsetID () == 0);
		assert (sam.session->GetSendTagsetNextIndex () == 0);
		assert (sam.session->GetNumPendingReplyTagsets () == 0);
		const std::vector<uint8_t> samBody { 0x71 };
		auto samEs1 = sam.session->WrapSingleMessage (MakeDataMessage (samBody, 0x12340004));
		assert (samEs1);
		assert (sam.session->GetSendTagsetNextIndex () == 1);
		tom.destination->ProcessGarlicMessage (samEs1);
		// Delayed reply arrives after the session is established.
		sam.destination->ProcessGarlicMessage (nsrG);
		assert (sam.session->GetSendTagsetID () == 0);
		assert (sam.session->GetSendTagsetNextIndex () == 1);
		assert (sam.session->GetNumPendingReplyTagsets () == 1);
		tom.destination->ClearReceived ();
		auto samEs2 = sam.session->WrapSingleMessage (MakeDataMessage (samBody, 0x12340005));
		assert (samEs2);
		assert (sam.session->GetSendTagsetNextIndex () == 2);
		tom.destination->ProcessGarlicMessage (samEs2);
		assert (tom.destination->Received ().size () == 1);
	}

	std::printf ("ok: %s\n", label);
}

void CheckRouterEnvelope ()
{
	// The router N envelope is exactly 32-byte ephemeral key + garlic payload
	// + 16-byte Poly1305 tag. It must never gain the spec-1c 32-byte flags
	// section that Java's N decoder does not parse.
	Peer bob = MakePeer (i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
	const std::vector<uint8_t> body { 0x77, 0x66, 0x55 };
	auto msg = MakeDataMessage (body, 0x0E0E0001);
	auto wrapped = i2p::garlic::WrapECIESX25519MessageForRouter (msg, bob.destination->GetPublicKey ());
	assert (wrapped);
	const size_t total = wrapped->GetPayloadLength (); // includes the 4-byte garlic length
	assert (total > 4 + 32 + 16);
	const size_t len = total - 4;
	assert (bufbe32toh (wrapped->GetPayload ()) == len);
	const uint8_t * inner = wrapped->GetPayload () + 4 + 32;
	const size_t innerLen = len - 32 - 16;

	size_t offset = 0;
	bool gotDateTime = false, gotGarlic = false, gotPadding = false;
	while (offset < innerLen)
	{
		assert (offset + 3 <= innerLen);
		const uint8_t type = inner[offset++];
		const uint16_t size = static_cast<uint16_t> ((inner[offset] << 8) | inner[offset + 1]);
		offset += 2;
		assert (offset + size <= innerLen);
		if (gotPadding) assert (false); // nothing may follow Padding
		if (type == i2p::garlic::eECIESx25519BlkDateTime)
		{
			assert (size == 4);
			gotDateTime = true;
		}
		else if (type == i2p::garlic::eECIESx25519BlkGalicClove)
		{
			assert (size == body.size () + 10);
			gotGarlic = true;
		}
		else if (type == i2p::garlic::eECIESx25519BlkPadding)
			gotPadding = true;
		else
			assert (false); // no flags/static or unknown block
		offset += size;
	}
	assert (offset == innerLen);
	assert (gotDateTime && gotGarlic);
}

void CheckInboundTagClamp ()
{
	const CryptoKeyType type = i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD;
	Peer peer = MakePeer (type);
	auto * destination = peer.destination.get ();
	destination->SetNumRatchetInboundTags (100000);
	assert (destination->GetNumRatchetInboundTags () == i2p::garlic::ECIESX25519_MAX_INBOUND_TAGS);
	destination->SetNumRatchetInboundTags (500);
	assert (destination->GetNumRatchetInboundTags () == i2p::garlic::ECIESX25519_MAX_INBOUND_TAGS);
	destination->SetNumRatchetInboundTags (-1);
	assert (destination->GetNumRatchetInboundTags () == 0);
	destination->SetNumRatchetInboundTags (24);
	assert (destination->GetNumRatchetInboundTags () == 24);
}

void TagsetExhaustion (CryptoKeyType type)
{
	Peer alice = MakePeer (type);
	Peer bob = MakePeer (type);
	const std::vector<uint8_t> body { 0x10, 0x20 };
	Establish (alice, bob, body);
	alice.destination->ClearReceived ();
	bob.destination->ClearReceived ();

	// Complete the handshake so Alice owns an established send tagset.
	auto nsr = bob.session->WrapSingleMessage (MakeDataMessage (body, 0x0A0A0001));
	assert (nsr);
	alice.destination->ProcessGarlicMessage (nsr);
	auto es = alice.session->WrapSingleMessage (MakeDataMessage (body, 0x0A0A0002));
	assert (es);
	bob.destination->ProcessGarlicMessage (es);
	assert (alice.session->IsReadyToSend ());
	assert (!alice.session->IsExhausted ());

	// The 16-bit tag space holds 65536 tags. One was already used by the ES
	// above, so exactly 65535 more can be sent. Exhaustion must mark the
	// session unusable for sending without removing or terminating it.
	int sent = 0;
	while (sent <= 0x10000)
	{
		auto message = alice.session->WrapSingleMessage (
			MakeDataMessage (body, 0x0B0B0000u + static_cast<uint32_t> (sent)));
		if (!message) break;
		sent++;
	}
	assert (sent == 0xFFFF);
	assert (alice.session->GetSendTagsetNextIndex () == 0x10000);
	assert (!alice.session->WrapSingleMessage (MakeDataMessage (body, 0x0C0C0001)));
	assert (alice.session->IsExhausted ());
	assert (!alice.session->IsReadyToSend ());
	assert (!alice.session->IsTerminated ());

	// The destination must hand out a fresh session for the next send while
	// the exhausted object stays alive to receive delayed messages.
	auto replacement = alice.destination->GetRoutingSession (bob.routing, true);
	assert (replacement);
	assert (replacement != alice.session);

	bob.destination->ClearReceived ();
	alice.destination->ClearReceived ();
	const std::vector<uint8_t> late { 0x30, 0x40 };
	auto lateMessage = bob.session->WrapSingleMessage (MakeDataMessage (late, 0x0D0D0001));
	assert (lateMessage);
	alice.destination->ProcessGarlicMessage (lateMessage);
	assert (alice.destination->Received ().size () == 1);
	assert (alice.destination->Received ().back ().payload == late);
}
}

int main ()
{
	CheckHandshakePayloadValidation ();
	CheckRouterEnvelope ();
	CheckInboundTagClamp ();

	InteropLoop (i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, "ECIES-X25519");
	TagsetExhaustion (i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
#if OPENSSL_PQ
	if (i2p::crypto::IsMLKEMSupported (i2p::data::CRYPTO_KEY_TYPE_ECIES_MLKEM768_X25519_AEAD))
	{
		InteropLoop (i2p::data::CRYPTO_KEY_TYPE_ECIES_MLKEM768_X25519_AEAD, "ECIES-MLKEM768-X25519");
		TagsetExhaustion (i2p::data::CRYPTO_KEY_TYPE_ECIES_MLKEM768_X25519_AEAD);
	}
	else
		std::printf ("skip: ML-KEM-768 not supported by this OpenSSL\n");
#endif
	std::printf ("all ECIES-X25519-AEAD-Ratchet session checks passed\n");
	return 0;
}
