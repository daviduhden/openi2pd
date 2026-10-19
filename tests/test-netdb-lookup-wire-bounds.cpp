/*
* Copyright (c) 2026, David Uhden Collado
* SPDX-License-Identifier: BSD-3-Clause
*
* This file is part of openi2pd and licensed under BSD-3-Clause
*
* See full license text in LICENSE file at top of project tree
*/

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <unordered_set>
#include <vector>

#include <openssl/sha.h>

#include "I2NPProtocol.h"
#include "I2PEndian.h"
#include "Identity.h"
#include "RouterContext.h"

// Static wire checks for I2NP DatabaseLookup (type 2) and
// DatabaseSearchReply (type 3) serialization and parsing. No network, netDb
// thread or tunnel is started.
//
// References:
//   I2NP DatabaseLookup: key (32) || from (32) || flags (1) ||
//     reply_tunnelId (4, only if flag bit 0) || size (2) ||
//     size*32 excluded hashes || optional reply key block.
//     size valid range 0-512.
//   I2NP DatabaseSearchReply: key (32) || num (1) || num*32 peer hashes ||
//     from (32). num valid range 0-255; recommended maximum 16.
//   Java 2.13.0 DatabaseLookupMessage.java (MAX_NUM_PEERS = 512, readMessage)
//   Java 2.13.0 DatabaseSearchReplyMessage.java (readMessage).

namespace
{
using namespace i2p;
using namespace i2p::data;

IdentHash MakeHash (uint32_t value)
{
	IdentHash hash;
	hash.Fill (0);
	htobe32buf (hash, value);
	return hash;
}

void InitializeLocalIdentity ()
{
	// CreateDatabaseSearchReply() writes our router hash into the trailing
	// "from" field. RouterInfo::GetIdentHash() dereferences m_RouterIdentity,
	// so install a real identity instead of relying on test-only defaults.
	auto keys = PrivateKeys::CreateRandomKeys (
		SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, false);
	context.GetRouterInfo ().SetRouterIdentity (keys.GetPublic ());
}

void AssertHeader (const std::shared_ptr<I2NPMessage> & msg, I2NPMessageType type)
{
	assert (msg);
	assert (msg->GetTypeID () == type);
	// I2NP standard header: the size field is the payload length and the
	// checksum is the first byte of SHA256(payload).
	assert (msg->GetSize () == msg->GetPayloadLength ());
	uint8_t digest[SHA256_DIGEST_LENGTH];
	SHA256 (msg->GetPayload (), msg->GetPayloadLength (), digest);
	assert (msg->GetHeader ()[I2NP_HEADER_CHKS_OFFSET] == digest[0]);
}

void TestLookupFlagConstants ()
{
	// I2NP DatabaseLookup flag bits and Java DatabaseLookupMessage constants.
	static_assert (DATABASE_LOOKUP_DELIVERY_FLAG == 0x01);
	static_assert (DATABASE_LOOKUP_ENCRYPTION_FLAG == 0x02);
	static_assert (DATABASE_LOOKUP_TYPE_FLAGS_MASK == 0x0c);
	static_assert (DATABASE_LOOKUP_TYPE_NORMAL_LOOKUP == 0x00);
	static_assert (DATABASE_LOOKUP_TYPE_LEASESET_LOOKUP == 0x04);
	static_assert (DATABASE_LOOKUP_TYPE_ROUTERINFO_LOOKUP == 0x08);
	static_assert (DATABASE_LOOKUP_TYPE_EXPLORATORY_LOOKUP == 0x0c);
	static_assert (DATABASE_LOOKUP_RATCHET_FLAG == 0x10);
	static_assert (DATABASE_LOOKUP_MAX_NUM_PEERS == 512);
	// 0x02 (AES/session key), 0x10 (ECIES tag) and 0x12 (proposal 154
	// public-key form) are receiver-side reply-key forms; the bits must not
	// overlap with the lookup type mask or the tunnel flag.
	assert ((DATABASE_LOOKUP_ENCRYPTION_FLAG & DATABASE_LOOKUP_TYPE_FLAGS_MASK) == 0);
	assert ((DATABASE_LOOKUP_RATCHET_FLAG & DATABASE_LOOKUP_TYPE_FLAGS_MASK) == 0);
	assert ((DATABASE_LOOKUP_RATCHET_FLAG & DATABASE_LOOKUP_DELIVERY_FLAG) == 0);
}

void TestRouterInfoLookupDirectLayout ()
{
	std::array<uint8_t, 32> key { };
	std::array<uint8_t, 32> from { };
	key.fill (0x11);
	from.fill (0x22);

	std::unordered_set<IdentHash> excluded { MakeHash (1), MakeHash (2), MakeHash (3) };
	auto msg = CreateRouterInfoDatabaseLookupMsg (key.data (), from.data (), 0, false, &excluded);
	AssertHeader (msg, eI2NPDatabaseLookup);

	// key + from + flags + count + 3*32 excluded peers.
	assert (msg->GetPayloadLength () == 67 + excluded.size () * 32);
	const uint8_t * payload = msg->GetPayload ();
	assert (!memcmp (payload, key.data (), 32));
	assert (!memcmp (payload + 32, from.data (), 32));
	assert (payload[64] == DATABASE_LOOKUP_TYPE_ROUTERINFO_LOOKUP);
	assert (bufbe16toh (payload + 65) == excluded.size ());

	std::unordered_set<IdentHash> emitted;
	for (size_t i = 0; i < excluded.size (); i++)
	{
		const IdentHash peer (payload + 67 + i * 32);
		assert (excluded.count (peer) == 1);
		assert (emitted.insert (peer).second); // no duplicate/partial entries
	}

	// A null exclusion set is the same as an empty one.
	auto empty = CreateRouterInfoDatabaseLookupMsg (key.data (), from.data (), 0, false, nullptr);
	assert (empty->GetPayloadLength () == 67);
	assert (bufbe16toh (empty->GetPayload () + 65) == 0);
}

void TestRouterInfoLookupTunnelLayout ()
{
	std::array<uint8_t, 32> key { };
	std::array<uint8_t, 32> from { };
	key.fill (0x33);
	from.fill (0x44);

	std::unordered_set<IdentHash> excluded { MakeHash (7), MakeHash (8) };
	const uint32_t tunnelID = 0x01020304;
	auto msg = CreateRouterInfoDatabaseLookupMsg (key.data (), from.data (), tunnelID, false, &excluded);
	AssertHeader (msg, eI2NPDatabaseLookup);
	assert (msg->GetPayloadLength () == 71 + excluded.size () * 32);

	const uint8_t * payload = msg->GetPayload ();
	assert (payload[64] == (DATABASE_LOOKUP_DELIVERY_FLAG | DATABASE_LOOKUP_TYPE_ROUTERINFO_LOOKUP));
	assert (bufbe32toh (payload + 65) == tunnelID);
	assert (bufbe16toh (payload + 69) == excluded.size ());
}

void TestExploratoryLookupDirectLayout ()
{
	// I2NP lookup type bits 3-2: 11 = exploratory. Java ExploreJob always sets
	// Type.EXPL for a real exploration, whether the DLM is direct or tunneled.
	std::array<uint8_t, 32> key { };
	std::array<uint8_t, 32> from { };
	key.fill (0x55);
	from.fill (0x66);
	auto msg = CreateRouterInfoDatabaseLookupMsg (key.data (), from.data (), 0, true, nullptr);
	AssertHeader (msg, eI2NPDatabaseLookup);
	assert (msg->GetPayloadLength () == 67);
	assert (msg->GetPayload ()[64] == DATABASE_LOOKUP_TYPE_EXPLORATORY_LOOKUP);
	assert (bufbe16toh (msg->GetPayload () + 65) == 0);
}

void TestLookupExcludedPeerCap ()
{
	// I2NP DatabaseLookup "size" is a 2-byte integer with valid range 0-512;
	// Java DatabaseLookupMessage.MAX_NUM_PEERS is 512.
	std::array<uint8_t, 32> key { };
	std::array<uint8_t, 32> from { };
	key.fill (0x77);
	from.fill (0x88);

	std::unordered_set<IdentHash> excluded;
	for (uint32_t i = 0; i < 600; i++) excluded.insert (MakeHash (i + 1));

	auto msg = CreateRouterInfoDatabaseLookupMsg (key.data (), from.data (), 0, false, &excluded);
	AssertHeader (msg, eI2NPDatabaseLookup);
	const uint8_t * payload = msg->GetPayload ();
	const uint16_t count = bufbe16toh (payload + 65);
	assert (count == DATABASE_LOOKUP_MAX_NUM_PEERS);
	assert (msg->GetPayloadLength () == 67 + DATABASE_LOOKUP_MAX_NUM_PEERS * 32);

	std::unordered_set<IdentHash> emitted;
	for (size_t i = 0; i < count; i++)
	{
		const IdentHash peer (payload + 67 + i * 32);
		assert (excluded.count (peer) == 1);
		assert (emitted.insert (peer).second);
	}

	// The tunnel form has the same cap even though the fixed header is larger.
	auto tunneled = CreateRouterInfoDatabaseLookupMsg (key.data (), from.data (), 1, true, &excluded);
	assert (bufbe16toh (tunneled->GetPayload () + 69) == DATABASE_LOOKUP_MAX_NUM_PEERS);
	assert (tunneled->GetPayloadLength () == 71 + DATABASE_LOOKUP_MAX_NUM_PEERS * 32);
}

void TestParseDatabaseSearchReplyValid ()
{
	std::vector<uint8_t> body (DATABASE_SEARCH_REPLY_PEERS_OFFSET, 0);
	const IdentHash key = MakeHash (0x12345678);
	memcpy (body.data (), key, 32);
	body[DATABASE_SEARCH_REPLY_COUNT_OFFSET] = 2;
	for (uint32_t i = 1; i <= 2; i++)
	{
		const IdentHash peer = MakeHash (i);
		body.insert (body.end (), peer (), peer () + 32);
	}
	const IdentHash from = MakeHash (0xabcdef01);
	body.insert (body.end (), from (), from () + 32);

	DatabaseSearchReply reply;
	assert (ParseDatabaseSearchReply (body.data (), body.size (), reply));
	assert (reply.key == key);
	assert (reply.from == from);
	assert (reply.peers.size () == 2);
	assert (reply.peers[0] == MakeHash (1));
	assert (reply.peers[1] == MakeHash (2));
}

void TestParseDatabaseSearchReplyBounds ()
{
	// Zero peers is the minimum valid body: key (32) + num (1) + from (32).
	std::vector<uint8_t> body (DATABASE_SEARCH_REPLY_MIN_SIZE, 0);
	body[DATABASE_SEARCH_REPLY_COUNT_OFFSET] = 0;
	DatabaseSearchReply reply;
	assert (ParseDatabaseSearchReply (body.data (), body.size (), reply));
	assert (reply.peers.empty ());

	// Declared peer count whose bytes (or the mandatory "from") are missing.
	body[DATABASE_SEARCH_REPLY_COUNT_OFFSET] = 1;
	assert (!ParseDatabaseSearchReply (body.data (), body.size (), reply));
	body.resize (DATABASE_SEARCH_REPLY_MIN_SIZE + 31); // one byte short of peer+from
	assert (!ParseDatabaseSearchReply (body.data (), body.size (), reply));
	body.resize (DATABASE_SEARCH_REPLY_MIN_SIZE + 32); // peer + from
	assert (ParseDatabaseSearchReply (body.data (), body.size (), reply));
	assert (reply.peers.size () == 1);

	// Every prefix shorter than the minimum must be rejected without reading.
	std::array<uint8_t, 64> one { };
	for (size_t len = 0; len < DATABASE_SEARCH_REPLY_MIN_SIZE; len++)
		assert (!ParseDatabaseSearchReply (one.data (), len, reply));
	assert (!ParseDatabaseSearchReply (nullptr, DATABASE_SEARCH_REPLY_MIN_SIZE, reply));

	// Maximum 1-byte count (255): 33 + 255*32 + 32 bytes.
	std::vector<uint8_t> maximum (DATABASE_SEARCH_REPLY_PEERS_OFFSET + 255 * 32 + 32, 0);
	maximum[DATABASE_SEARCH_REPLY_COUNT_OFFSET] = 255;
	assert (ParseDatabaseSearchReply (maximum.data (), maximum.size (), reply));
	assert (reply.peers.size () == 255);
	maximum.pop_back ();
	assert (!ParseDatabaseSearchReply (maximum.data (), maximum.size (), reply));

	// Java readMessage() ignores bytes after the defined body. Pin that
	// compatibility: the "from" hash stays at 33 + num*32, not at the end.
	body.assign (DATABASE_SEARCH_REPLY_MIN_SIZE + 1, 0x5a);
	body[DATABASE_SEARCH_REPLY_COUNT_OFFSET] = 0;
	const IdentHash from = MakeHash (0x01020304);
	memcpy (body.data () + DATABASE_SEARCH_REPLY_PEERS_OFFSET, from, 32);
	assert (ParseDatabaseSearchReply (body.data (), body.size (), reply));
	assert (reply.from == from);
	assert (reply.peers.empty ());
}

void TestDatabaseSearchReplySerialization ()
{
	InitializeLocalIdentity ();

	std::vector<IdentHash> peers;
	for (uint32_t i = 1; i <= 300; i++) peers.push_back (MakeHash (i));
	const IdentHash key = MakeHash (0xa0b0c0d0);
	auto msg = CreateDatabaseSearchReply (key, peers);
	AssertHeader (msg, eI2NPDatabaseSearchReply);

	// num is one byte; the serializer must cap at 255 rather than wrap.
	const uint8_t * payload = msg->GetPayload ();
	assert (payload[DATABASE_SEARCH_REPLY_COUNT_OFFSET] == 255);
	assert (msg->GetPayloadLength () == DATABASE_SEARCH_REPLY_PEERS_OFFSET + 255 * 32 + 32);
	for (size_t i = 0; i < 255; i++)
	{
		const IdentHash peer (payload + DATABASE_SEARCH_REPLY_PEERS_OFFSET + i * 32);
		assert (peer == peers[i]);
	}
	const IdentHash & local = context.GetRouterInfo ().GetIdentHash ();
	assert (!memcmp (payload + DATABASE_SEARCH_REPLY_PEERS_OFFSET + 255 * 32, local, 32));

	DatabaseSearchReply parsed;
	assert (ParseDatabaseSearchReply (payload, msg->GetPayloadLength (), parsed));
	assert (parsed.key == key);
	assert (parsed.from == local);
	assert (parsed.peers.size () == 255);
	assert (parsed.peers.front () == peers.front ());
	assert (parsed.peers.back () == peers[254]);
}
}

int main ()
{
	TestLookupFlagConstants ();
	TestRouterInfoLookupDirectLayout ();
	TestRouterInfoLookupTunnelLayout ();
	TestExploratoryLookupDirectLayout ();
	TestLookupExcludedPeerCap ();
	TestParseDatabaseSearchReplyValid ();
	TestParseDatabaseSearchReplyBounds ();
	TestDatabaseSearchReplySerialization ();
	return 0;
}
