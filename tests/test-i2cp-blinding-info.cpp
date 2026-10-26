/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test i2cp blinding info.
 *
 * Offline checks for the BlindingInfo (type 42) wire parser, the bounded
 * registry reader (store, prune/expiry, lookup), the client-auth secret
 * selection and the HostReply payload/result encoding. No socket, session or
 * router is started.
 *
 */

#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "I2CP.h"
#include "Identity.h"

namespace
{
i2p::data::PrivateKeys MakeDestinationKeys ()
{
	return i2p::data::PrivateKeys::CreateRandomKeys (
		i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
}

void Append16 (std::vector<uint8_t> & buf, uint16_t value)
{
	buf.push_back ((uint8_t)(value >> 8));
	buf.push_back ((uint8_t)(value & 0xff));
}

void Append32 (std::vector<uint8_t> & buf, uint32_t value)
{
	buf.push_back ((uint8_t)(value >> 24));
	buf.push_back ((uint8_t)((value >> 16) & 0xff));
	buf.push_back ((uint8_t)((value >> 8) & 0xff));
	buf.push_back ((uint8_t)(value & 0xff));
}

// sessionId(2), flags(1), endpointType(1), blindType(2), expiration(4)
std::vector<uint8_t> MakeHeader (uint8_t flags, uint8_t endpointType, uint16_t blindType, uint32_t expiration)
{
	std::vector<uint8_t> buf;
	Append16 (buf, 0x1234);
	buf.push_back (flags);
	buf.push_back (endpointType);
	Append16 (buf, blindType);
	Append32 (buf, expiration);
	return buf;
}

void AppendIdentity (std::vector<uint8_t> & buf, const i2p::data::IdentityEx & identity)
{
	std::vector<uint8_t> raw (identity.GetFullLen ());
	const size_t written = identity.ToBuffer (raw.data (), raw.size ());
	assert (written == raw.size ());
	(void)written;
	buf.insert (buf.end (), raw.begin (), raw.end ());
}

std::vector<uint8_t> MakeDestinationMessage (uint8_t flags, const i2p::data::IdentityEx & identity,
	uint32_t expiration)
{
	// blindType = EdDSA (7), endpoint type = destination (2)
	auto buf = MakeHeader (flags, i2p::client::eI2CPBlindingEndpointDestination, 7, expiration);
	AppendIdentity (buf, identity);
	return buf;
}

void AppendAuthKey (std::vector<uint8_t> & buf, uint8_t seed)
{
	for (size_t i = 0; i < 32; i++) buf.push_back ((uint8_t)(seed + i));
}

void AppendSecret (std::vector<uint8_t> & buf, const std::string & secret)
{
	buf.push_back ((uint8_t)secret.size ());
	buf.insert (buf.end (), secret.begin (), secret.end ());
}

std::string Secret32 (char base)
{
	std::string secret;
	for (int i = 0; i < 32; i++) secret.push_back ((char)(base + i));
	return secret;
}

void TestDestinationEndpoints (const i2p::data::IdentityEx & identity)
{
	using namespace i2p::client;
	std::string error;

	// authType none, no secret, no expiry.
	{
		auto buf = MakeDestinationMessage (0, identity, 0);
		I2CPBlindingInfo info;
		assert (ParseI2CPBlindingInfoMessage (buf.data (), buf.size (), info, error) == eI2CPBlindingInfoOk);
		assert (info.endpointType == eI2CPBlindingEndpointDestination);
		assert (info.identity);
		assert (info.identity->GetIdentHash () == identity.GetIdentHash ());
		assert ((uint16_t)info.signingKeyType == (uint16_t)identity.GetSigningKeyType ());
		assert (info.signingKey.size () == identity.GetSigningPublicKeyLen ());
		assert (!memcmp (info.signingKey.data (), identity.GetSigningPublicKeyBuffer (), info.signingKey.size ()));
		assert (info.authType == eI2CPBlindingAuthNone);
		assert (info.authPrivateKey.empty ());
		assert (info.secret.empty ());
		assert (info.expiration == 0);
		assert (GetI2CPBlindingAuthSecret (info).empty ());
	}

	// authType DH (1): one 32-byte auth private key, no secret.
	{
		auto buf = MakeDestinationMessage (eI2CPBlindingAuthDH, identity, 0);
		AppendAuthKey (buf, 0x10);
		I2CPBlindingInfo info;
		assert (ParseI2CPBlindingInfoMessage (buf.data (), buf.size (), info, error) == eI2CPBlindingInfoOk);
		assert (info.authType == eI2CPBlindingAuthDH);
		assert (info.authPrivateKey.size () == 32);
		assert (info.authPrivateKey[0] == 0x10);
		assert (info.secret.empty ());
		const auto secret = GetI2CPBlindingAuthSecret (info);
		assert (secret.size () == 32);
		assert (secret[0] == 0x10);
	}

	// authType PSK (2): only the secret flag carries material, no auth key.
	{
		auto buf = MakeDestinationMessage (0x10 | eI2CPBlindingAuthPSK, identity, 0);
		AppendSecret (buf, Secret32 ('a'));
		I2CPBlindingInfo info;
		assert (ParseI2CPBlindingInfoMessage (buf.data (), buf.size (), info, error) == eI2CPBlindingInfoOk);
		assert (info.authType == eI2CPBlindingAuthPSK);
		assert (info.authPrivateKey.empty ());
		assert (info.secret.size () == 32);
		assert (GetI2CPBlindingAuthSecret (info) == std::vector<uint8_t> (info.secret.begin (), info.secret.end ()));
	}

	// authType DH+PSK (3): both present, the private key wins as the usable
	// secret (either one decrypts, the key is the stronger material).
	{
		auto buf = MakeDestinationMessage (0x10 | eI2CPBlindingAuthDHAndPSK, identity, 0);
		AppendAuthKey (buf, 0x20);
		AppendSecret (buf, Secret32 ('0'));
		I2CPBlindingInfo info;
		assert (ParseI2CPBlindingInfoMessage (buf.data (), buf.size (), info, error) == eI2CPBlindingInfoOk);
		assert (info.authType == eI2CPBlindingAuthDHAndPSK);
		assert (info.authPrivateKey.size () == 32);
		assert (info.secret.size () == 32);
		const auto secret = GetI2CPBlindingAuthSecret (info);
		assert (secret.size () == 32);
		assert (secret[0] == 0x20);
	}

	// Expiration is seconds on the wire and milliseconds in the record.
	{
		auto buf = MakeDestinationMessage (0, identity, 7200);
		I2CPBlindingInfo info;
		assert (ParseI2CPBlindingInfoMessage (buf.data (), buf.size (), info, error) == eI2CPBlindingInfoOk);
		assert (info.expiration == 7200ull * 1000ull);
	}

	// Bad auth type (4) is a corrupt flags byte.
	{
		auto buf = MakeDestinationMessage (4, identity, 0);
		I2CPBlindingInfo info;
		assert (ParseI2CPBlindingInfoMessage (buf.data (), buf.size (), info, error) == eI2CPBlindingInfoMalformed);
		assert (!error.empty ());
	}

	// Truncation at every length must fail without reading past the buffer.
	{
		auto full = MakeDestinationMessage (0x10 | eI2CPBlindingAuthDHAndPSK, identity, 0);
		AppendAuthKey (full, 0x30);
		AppendSecret (full, Secret32 ('A'));
		for (size_t len = 0; len < full.size (); len++)
		{
			I2CPBlindingInfo info;
			const auto result = ParseI2CPBlindingInfoMessage (full.data (), len, info, error);
			assert (result != eI2CPBlindingInfoOk);
		}
		I2CPBlindingInfo info;
		assert (ParseI2CPBlindingInfoMessage (full.data (), full.size (), info, error) == eI2CPBlindingInfoOk);

		// One trailing byte is rejected.
		auto trailing = full;
		trailing.push_back (0);
		assert (ParseI2CPBlindingInfoMessage (trailing.data (), trailing.size (), info, error)
			== eI2CPBlindingInfoMalformed);
	}

	// An unknown blind type is unsupported, not malformed.
	{
		auto buf = MakeDestinationMessage (0, identity, 0);
		buf[4] = 0xff;
		buf[5] = 0xff;
		I2CPBlindingInfo info;
		assert (ParseI2CPBlindingInfoMessage (buf.data (), buf.size (), info, error)
			== eI2CPBlindingInfoUnsupported);
	}
}

void TestOtherEndpoints (const i2p::data::IdentityEx & identity)
{
	using namespace i2p::client;
	std::string error;

	// Endpoint type hash: 32 opaque bytes, no signing key.
	{
		auto buf = MakeHeader (0, eI2CPBlindingEndpointHash, 7, 0);
		for (int i = 0; i < 32; i++) buf.push_back ((uint8_t)i);
		I2CPBlindingInfo info;
		assert (ParseI2CPBlindingInfoMessage (buf.data (), buf.size (), info, error) == eI2CPBlindingInfoOk);
		assert (info.endpointType == eI2CPBlindingEndpointHash);
		assert (!info.identity);
		assert (info.signingKey.empty ());
	}

	// Endpoint type host: 1-byte length + name.
	{
		const std::string host = "example.i2p";
		auto buf = MakeHeader (0, eI2CPBlindingEndpointHost, 7, 0);
		buf.push_back ((uint8_t)host.size ());
		buf.insert (buf.end (), host.begin (), host.end ());
		I2CPBlindingInfo info;
		assert (ParseI2CPBlindingInfoMessage (buf.data (), buf.size (), info, error) == eI2CPBlindingInfoOk);
		assert (info.endpointType == eI2CPBlindingEndpointHost);
		assert (info.host == host);
		assert (info.signingKey.empty ());

		// Empty host name is malformed.
		auto empty = MakeHeader (0, eI2CPBlindingEndpointHost, 7, 0);
		empty.push_back (0);
		assert (ParseI2CPBlindingInfoMessage (empty.data (), empty.size (), info, error)
			== eI2CPBlindingInfoMalformed);
	}

	// Endpoint type key: signing key type + public key, no identity.
	{
		auto buf = MakeHeader (0, eI2CPBlindingEndpointKey, 7, 0);
		Append16 (buf, (uint16_t)identity.GetSigningKeyType ());
		buf.insert (buf.end (), identity.GetSigningPublicKeyBuffer (),
			identity.GetSigningPublicKeyBuffer () + identity.GetSigningPublicKeyLen ());
		I2CPBlindingInfo info;
		assert (ParseI2CPBlindingInfoMessage (buf.data (), buf.size (), info, error) == eI2CPBlindingInfoOk);
		assert (info.endpointType == eI2CPBlindingEndpointKey);
		assert (!info.identity);
		assert (info.signingKey.size () == identity.GetSigningPublicKeyLen ());

		// A truncated key is malformed.
		auto truncated = buf;
		truncated.pop_back ();
		assert (ParseI2CPBlindingInfoMessage (truncated.data (), truncated.size (), info, error)
			== eI2CPBlindingInfoMalformed);
	}

	// Unknown endpoint type (4) is rejected.
	{
		auto buf = MakeHeader (0, 4, 7, 0);
		I2CPBlindingInfo info;
		assert (ParseI2CPBlindingInfoMessage (buf.data (), buf.size (), info, error) == eI2CPBlindingInfoMalformed);
	}
}

void TestRegistry (const i2p::data::IdentityEx & identity)
{
	using namespace i2p::client;
	std::string error;

	ClearI2CPBlindingInfos ();
	assert (GetI2CPBlindingInfoCount () == 0);

	// A live record is readable by its unblinded signing key.
	auto buf = MakeDestinationMessage (eI2CPBlindingAuthDH, identity, 0);
	AppendAuthKey (buf, 0x40);
	I2CPBlindingInfo stored;
	assert (ParseI2CPBlindingInfoMessage (buf.data (), buf.size (), stored, error) == eI2CPBlindingInfoOk);
	StoreI2CPBlindingInfo (stored);

	I2CPBlindingInfo found;
	assert (FindI2CPBlindingInfoBySigningKey (identity.GetSigningKeyType (),
		identity.GetSigningPublicKeyBuffer (), identity.GetSigningPublicKeyLen (), found));
	assert (found.authType == eI2CPBlindingAuthDH);
	assert (found.authPrivateKey.size () == 32);
	assert (GetI2CPBlindingAuthSecret (found).size () == 32);

	// An unrelated signing key finds nothing.
	auto otherKeys = MakeDestinationKeys ();
	assert (!FindI2CPBlindingInfoBySigningKey (otherKeys.GetPublic ()->GetSigningKeyType (),
		otherKeys.GetPublic ()->GetSigningPublicKeyBuffer (),
		otherKeys.GetPublic ()->GetSigningPublicKeyLen (), found));

	// An expired record is pruned on access and never returned.
	{
		I2CPBlindingInfo expired = stored;
		expired.expiration = 1; // 1970, long past
		StoreI2CPBlindingInfo (expired);
		assert (!FindI2CPBlindingInfoBySigningKey (identity.GetSigningKeyType (),
			identity.GetSigningPublicKeyBuffer (), identity.GetSigningPublicKeyLen (), found));
		assert (GetI2CPBlindingInfoCount () == 0);
	}

	// The registry is bounded: many distinct registrations cannot grow it
	// without limit.
	ClearI2CPBlindingInfos ();
	for (int i = 0; i < 200; i++)
	{
		I2CPBlindingInfo info;
		info.endpointType = eI2CPBlindingEndpointKey;
		info.signingKeyType = i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519;
		info.signingKey.assign (32, (uint8_t)i);
		info.authType = eI2CPBlindingAuthNone;
		StoreI2CPBlindingInfo (info);
	}
	assert (GetI2CPBlindingInfoCount () > 0);
	assert (GetI2CPBlindingInfoCount () <= 64);
	ClearI2CPBlindingInfos ();
	assert (GetI2CPBlindingInfoCount () == 0);
}

void TestHostReplyEncoding (const i2p::data::IdentityEx & identity)
{
	using namespace i2p::client;

	// Auth scheme -> HostReply result mapping.
	assert (GetI2CPHostReplyResultForAuth (eI2CPBlindingAuthNone, false) == eI2CPHostReplySuccess);
	assert (GetI2CPHostReplyResultForAuth (eI2CPBlindingAuthDH, false) == eI2CPHostReplyKeyRequired);
	assert (GetI2CPHostReplyResultForAuth (eI2CPBlindingAuthPSK, false) == eI2CPHostReplySecretRequired);
	assert (GetI2CPHostReplyResultForAuth (eI2CPBlindingAuthDHAndPSK, false) == eI2CPHostReplySecretAndKeyRequired);
	assert (GetI2CPHostReplyResultForAuth (eI2CPBlindingAuthDH, true) == eI2CPHostReplySuccess);
	assert (GetI2CPHostReplyResultForAuth (eI2CPBlindingAuthPSK, true) == eI2CPHostReplySuccess);
	assert (GetI2CPHostReplyResultForAuth (eI2CPBlindingAuthDHAndPSK, true) == eI2CPHostReplySuccess);
	assert (GetI2CPHostReplyResultForAuth (9, false) == eI2CPHostReplyFailure);

	auto identityCopy = std::make_shared<i2p::data::IdentityEx> (identity);

	// Success layout: sessionId, requestId, result 0, identity, options.
	{
		const std::vector<uint8_t> options { 0, 0 };
		auto payload = BuildI2CPHostReplyPayload (0x11223344, 0x5566, identityCopy, options);
		assert (payload.size () == identity.GetFullLen () + 7 + options.size ());
		assert (payload[0] == 0x55 && payload[1] == 0x66);
		assert (payload[2] == 0x11 && payload[3] == 0x22 && payload[4] == 0x33 && payload[5] == 0x44);
		assert (payload[6] == eI2CPHostReplySuccess);
		i2p::data::IdentityEx parsed (payload.data () + 7, payload.size () - 7);
		assert (parsed.GetIdentHash () == identity.GetIdentHash ());
		assert (payload[payload.size () - 2] == 0 && payload[payload.size () - 1] == 0);
	}

	// Every failure is the 7-byte layout with the Java result code; an identity
	// passed alongside a failure result is dropped.
	const uint8_t failures[] = { eI2CPHostReplySecretRequired, eI2CPHostReplyKeyRequired,
		eI2CPHostReplySecretAndKeyRequired, eI2CPHostReplyDecryptionFailure, eI2CPHostReplyLeaseSetLookupFailure,
		eI2CPHostReplyFailure };
	for (auto result : failures)
	{
		auto payload = BuildI2CPHostReplyPayload (0x01020304, 0x0a0b, identityCopy, { 0, 0 }, result);
		assert (payload.size () == 7);
		assert (payload[6] == result);
	}
	{
		// No identity and no explicit result keeps the generic failure.
		auto payload = BuildI2CPHostReplyPayload (1, 2, nullptr);
		assert (payload.size () == 7);
		assert (payload[6] == eI2CPHostReplyFailure);
	}
}

void TestSendMessageSeams (const i2p::data::IdentityEx & identity)
{
	using namespace i2p::client;

	std::vector<uint8_t> identityBuf (identity.GetFullLen ());
	const size_t identityWritten = identity.ToBuffer (identityBuf.data (), identityBuf.size ());
	assert (identityWritten == identityBuf.size ());
	(void)identityWritten;
	const std::vector<uint8_t> payload { 1, 2, 3, 4 };

	auto makeRequest = [&] (bool expires, uint64_t expiration) {
		std::vector<uint8_t> buf;
		Append16 (buf, 0x0102);
		buf.insert (buf.end (), identityBuf.begin (), identityBuf.end ());
		Append32 (buf, (uint32_t)payload.size ());
		buf.insert (buf.end (), payload.begin (), payload.end ());
		Append32 (buf, 0xdeadbeef);
		if (expires)
		{
			Append16 (buf, 0); // flags
			buf.push_back ((uint8_t)((expiration >> 40) & 0xff));
			buf.push_back ((uint8_t)((expiration >> 32) & 0xff));
			buf.push_back ((uint8_t)((expiration >> 24) & 0xff));
			buf.push_back ((uint8_t)((expiration >> 16) & 0xff));
			buf.push_back ((uint8_t)((expiration >> 8) & 0xff));
			buf.push_back ((uint8_t)(expiration & 0xff));
		}
		return buf;
	};

	// SEND MESSAGE: parsed, accepted and expired-free.
	{
		auto buf = makeRequest (false, 0);
		I2CPSendMessageRequest request;
		assert (ParseI2CPSendMessage (buf.data (), buf.size (), false, request));
		assert (request.sessionID == 0x0102);
		assert (request.hasNonce && request.nonce == 0xdeadbeef);
		assert (request.identityLen == identityBuf.size ());
		assert (request.payloadLen == payload.size ());
		assert (request.expiration == 0 && request.flags == 0);
		assert (CheckI2CPSendMessage (request, 0x0102, true, 1000) == I2CPSendMessageCheck::Ok);
		// Foreign session id and a missing session object are BAD_SESSION.
		assert (CheckI2CPSendMessage (request, 0x0203, true, 1000) == I2CPSendMessageCheck::BadSession);
		assert (CheckI2CPSendMessage (request, 0x0102, false, 1000) == I2CPSendMessageCheck::BadSession);
	}

	// SEND MESSAGE EXPIRES: expiration is milliseconds since epoch, 0 = never.
	{
		auto buf = makeRequest (true, 5000);
		I2CPSendMessageRequest request;
		assert (ParseI2CPSendMessage (buf.data (), buf.size (), true, request));
		assert (request.expiration == 5000);
		assert (CheckI2CPSendMessage (request, 0x0102, true, 4000) == I2CPSendMessageCheck::Ok);
		// Java uses "expiration <= now".
		assert (CheckI2CPSendMessage (request, 0x0102, true, 5000) == I2CPSendMessageCheck::Expired);
		assert (CheckI2CPSendMessage (request, 0x0102, true, 6000) == I2CPSendMessageCheck::Expired);

		auto noExpiry = makeRequest (true, 0);
		assert (ParseI2CPSendMessage (noExpiry.data (), noExpiry.size (), true, request));
		assert (CheckI2CPSendMessage (request, 0x0102, true, 6000) == I2CPSendMessageCheck::Ok);
	}

	// Malformed layouts are rejected and the session identifier is preserved.
	{
		auto buf = makeRequest (false, 0);
		I2CPSendMessageRequest request;
		assert (!ParseI2CPSendMessage (buf.data (), 1, false, request));
		assert (!ParseI2CPSendMessage (buf.data (), 2, false, request));
		// Trailing bytes are malformed (Java requires an exact payload size).
		auto trailing = buf;
		trailing.push_back (0);
		assert (!ParseI2CPSendMessage (trailing.data (), trailing.size (), false, request));
		// An expires layout parsed as SEND MESSAGE has extra bytes too.
		auto expires = makeRequest (true, 1000);
		assert (!ParseI2CPSendMessage (expires.data (), expires.size (), false, request));
		assert (ParseI2CPSendMessage (expires.data (), expires.size (), true, request));
		// A truncated identity is rejected.
		assert (!ParseI2CPSendMessage (buf.data (), 2 + identityBuf.size () - 1, false, request));
	}

	// The statuses the handler maps to are the Java codes 10/11/14.
	assert ((int)eI2CPMessageStatusBadSession == 10);
	assert ((int)eI2CPMessageStatusBadMessage == 11);
	assert ((int)eI2CPMessageStatusSendFailureExpired == 14);
}

void TestAdmissionPolicy ()
{
	using namespace i2p::client;

	auto defaults = MakeI2CPAdmissionPolicy (0, 0);
	assert (defaults.maxConnections == I2CP_MAX_CONNECTIONS_DEFAULT);
	assert (defaults.maxConnections == 50);
	assert (defaults.maxSessions == I2CP_MAX_SESSIONS_DEFAULT);
	assert (defaults.AcceptConnection (0));
	assert (defaults.AcceptConnection (defaults.maxConnections - 1));
	assert (!defaults.AcceptConnection (defaults.maxConnections));
	assert (!defaults.AcceptConnection (defaults.maxConnections + 1));
	assert (defaults.AcceptSession (0));
	assert (!defaults.AcceptSession (defaults.maxSessions));

	// A configured value is honoured up to the hard ceiling.
	auto configured = MakeI2CPAdmissionPolicy (4, 2);
	assert (configured.maxConnections == 4);
	assert (configured.maxSessions == 2);
	assert (configured.AcceptConnection (3));
	assert (!configured.AcceptConnection (4));
	assert (configured.AcceptSession (1));
	assert (!configured.AcceptSession (2));

	auto clamped = MakeI2CPAdmissionPolicy (I2CP_MAX_CONNECTIONS_LIMIT + 1000, I2CP_MAX_SESSIONS_LIMIT + 1000);
	assert (clamped.maxConnections == I2CP_MAX_CONNECTIONS_LIMIT);
	assert (clamped.maxSessions == I2CP_MAX_SESSIONS_LIMIT);
	assert (clamped.maxConnections == 2048);
	assert (clamped.AcceptConnection (I2CP_MAX_CONNECTIONS_LIMIT - 1));
	assert (!clamped.AcceptConnection (I2CP_MAX_CONNECTIONS_LIMIT));

	// The read deadlines are the documented bounds.
	assert (I2CP_HANDSHAKE_TIMEOUT == 30);
	assert (I2CP_BODY_TIMEOUT == 30);
}
} // namespace

int main ()
{
	auto keys = MakeDestinationKeys ();
	const auto & identity = *keys.GetPublic ();

	TestDestinationEndpoints (identity);
	TestOtherEndpoints (identity);
	TestRegistry (identity);
	TestHostReplyEncoding (identity);
	TestSendMessageSeams (identity);
	TestAdmissionPolicy ();

	return 0;
}
