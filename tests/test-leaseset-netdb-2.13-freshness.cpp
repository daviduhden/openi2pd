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

#include "I2NPProtocol.h"
#include "I2PEndian.h"
#include "Identity.h"
#include "LeaseSet.h"
#include "Timestamp.h"

// Static regression checks for the Java 2.13 lease-set store rules and the
// DatabaseStore type byte. No network or netDb thread is started.

namespace
{
using namespace i2p::data;

void AppendBe16 (std::vector<uint8_t> & buffer, uint16_t value)
{
	uint8_t encoded[2];
	htobe16buf (encoded, value);
	buffer.insert (buffer.end (), encoded, encoded + sizeof (encoded));
}

void AppendBe32 (std::vector<uint8_t> & buffer, uint32_t value)
{
	uint8_t encoded[4];
	htobe32buf (encoded, value);
	buffer.insert (buffer.end (), encoded, encoded + sizeof (encoded));
}

void AppendBe64 (std::vector<uint8_t> & buffer, uint64_t value)
{
	uint8_t encoded[8];
	htobe64buf (encoded, value);
	buffer.insert (buffer.end (), encoded, encoded + sizeof (encoded));
}

void AppendIdentity (std::vector<uint8_t> & buffer, const PrivateKeys & keys)
{
	auto identity = keys.GetPublic ();
	assert (identity);
	auto len = identity->GetFullLen ();
	auto offset = buffer.size ();
	buffer.resize (offset + len);
	assert (identity->ToBuffer (buffer.data () + offset, len) == len);
}

void AppendSignature (std::vector<uint8_t> & buffer, const PrivateKeys & keys)
{
	auto identity = keys.GetPublic ();
	assert (identity);
	std::vector<uint8_t> signature (identity->GetSignatureLen ());
	keys.Sign (buffer.data (), static_cast<int> (buffer.size ()), signature.data ());
	buffer.insert (buffer.end (), signature.begin (), signature.end ());
}

std::vector<uint8_t> BuildLeaseSet1 (const PrivateKeys & keys, uint64_t endDate)
{
	std::vector<uint8_t> buffer;
	AppendIdentity (buffer, keys);
	buffer.resize (buffer.size () + 256, 0); // encryption key (unused here)
	buffer.resize (buffer.size () + keys.GetPublic ()->GetSigningPublicKeyLen (), 0); // revocation key
	buffer.push_back (1); // number of leases
	buffer.resize (buffer.size () + 32, 0x11); // gateway
	AppendBe32 (buffer, 0x01020304); // tunnel ID
	AppendBe64 (buffer, endDate);
	AppendSignature (buffer, keys);
	return buffer;
}

std::vector<uint8_t> BuildLeaseSet2 (const PrivateKeys & keys, uint32_t published, uint32_t leaseEnd, uint16_t keyType,
	const std::vector<uint8_t> & encryptionKey, uint16_t flags = 0)
{
	assert (leaseEnd >= published);
	std::vector<uint8_t> buffer;
	buffer.push_back (NETDB_STORE_TYPE_STANDARD_LEASESET2); // covered by the LS2 signature
	AppendIdentity (buffer, keys);
	AppendBe32 (buffer, published);
	AppendBe16 (buffer, static_cast<uint16_t> (leaseEnd - published)); // expires (seconds)
	AppendBe16 (buffer, flags);
	AppendBe16 (buffer, 0); // properties length
	buffer.push_back (1); // one encryption key
	AppendBe16 (buffer, keyType);
	AppendBe16 (buffer, static_cast<uint16_t> (encryptionKey.size ()));
	buffer.insert (buffer.end (), encryptionKey.begin (), encryptionKey.end ());
	buffer.push_back (1); // one lease
	buffer.resize (buffer.size () + 32, 0x22); // gateway
	AppendBe32 (buffer, 0x01020304); // tunnel ID
	AppendBe32 (buffer, leaseEnd);
	AppendSignature (buffer, keys);
	return buffer;
}

void TestLeaseSet1FutureLease ()
{
	auto keys = PrivateKeys::CreateRandomKeys (
		SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
	auto now = i2p::util::GetMillisecondsSinceEpoch ();

	// A current lease is still accepted.
	{
		auto buffer = BuildLeaseSet1 (keys, now + 5 * 60 * 1000);
		uint64_t expires = 0;
		assert (LeaseSetBufferValidate (buffer.data (), buffer.size (), expires));
		LeaseSet leaseSet (buffer.data (), buffer.size (), false);
		assert (leaseSet.IsValid ());
	}

	// Java KademliaNetworkDatabaseFacade.validate() rejects leases beyond
	// CLOCK_FUDGE + MAX_LEASE_FUTURE (15 minutes). Before the fix this LS1 was
	// accepted and would never expire in ManageLeaseSets().
	{
		auto buffer = BuildLeaseSet1 (keys, now + 17 * 60 * 1000);
		uint64_t expires = 0;
		assert (!LeaseSetBufferValidate (buffer.data (), buffer.size (), expires));
		LeaseSet leaseSet (buffer.data (), buffer.size (), false);
		assert (!leaseSet.IsValid ());
	}
}

void TestLeaseSet2FutureLease ()
{
	auto keys = PrivateKeys::CreateRandomKeys (
		SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
	auto now = static_cast<uint32_t> (i2p::util::GetMillisecondsSinceEpoch () / 1000);
	auto published = now - 60;
	std::vector<uint8_t> x25519Key (32, 0x33);

	// A lease 14 minutes ahead is within Java's 15 minute + fudge window.
	// The previous 12 minute bound rejected a valid LS2 from a peer whose
	// clock was a few minutes fast.
	{
		auto buffer = BuildLeaseSet2 (keys, published, now + 14 * 60, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, x25519Key);
		LeaseSet2 leaseSet (NETDB_STORE_TYPE_STANDARD_LEASESET2, buffer.data () + 1, buffer.size () - 1, false);
		assert (leaseSet.IsValid ());
	}

	// Beyond 15 minutes + fudge is still rejected.
	{
		auto buffer = BuildLeaseSet2 (keys, published, now + 17 * 60, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, x25519Key);
		LeaseSet2 leaseSet (NETDB_STORE_TYPE_STANDARD_LEASESET2, buffer.data () + 1, buffer.size () - 1, false);
		assert (!leaseSet.IsValid ());
	}
}

void TestLeaseSet2P256WireKey ()
{
	auto keys = PrivateKeys::CreateRandomKeys (
		SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
	auto now = static_cast<uint32_t> (i2p::util::GetMillisecondsSinceEpoch () / 1000);
	// Java EncType.EC_P256 carries a 64-byte uncompressed point, not the
	// 256-byte legacy key-file representation.
	std::vector<uint8_t> p256Key (64, 0x44);
	auto buffer = BuildLeaseSet2 (keys, now - 60, now + 5 * 60, CRYPTO_KEY_TYPE_ECIES_P256_SHA256_AES256CBC, p256Key);
	LeaseSet2 leaseSet (NETDB_STORE_TYPE_STANDARD_LEASESET2, buffer.data () + 1, buffer.size () - 1, false);
	assert (leaseSet.IsValid ());
	assert (leaseSet.IsEncryptionKey (CRYPTO_KEY_TYPE_ECIES_P256_SHA256_AES256CBC, p256Key.data ()));
}

void TestLeaseSet2UnpublishedFlag ()
{
	auto keys = PrivateKeys::CreateRandomKeys (
		SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
	auto now = static_cast<uint32_t> (i2p::util::GetMillisecondsSinceEpoch () / 1000);
	std::vector<uint8_t> x25519Key (32, 0x55);

	// 0x02 is UNPUBLISHED and 0x04 is FLAG_BLINDED ("will be blinded when
	// published"). Java keeps such an entry unpublished; setting 0x04 must
	// not clear 0x02, or a floodfill would store and serve it.
	auto hidden = BuildLeaseSet2 (keys, now - 60, now + 5 * 60, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, x25519Key, 0x0006);
	LeaseSet2 hiddenLeaseSet (NETDB_STORE_TYPE_STANDARD_LEASESET2, hidden.data () + 1, hidden.size () - 1, false);
	assert (hiddenLeaseSet.IsValid ());
	assert (!hiddenLeaseSet.IsPublic ());

	auto blinded = BuildLeaseSet2 (keys, now - 60, now + 5 * 60, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, x25519Key, 0x0004);
	LeaseSet2 blindedLeaseSet (NETDB_STORE_TYPE_STANDARD_LEASESET2, blinded.data () + 1, blinded.size () - 1, false);
	assert (blindedLeaseSet.IsValid ());
	assert (blindedLeaseSet.IsPublic ());
	assert (blindedLeaseSet.IsPublishedEncrypted ());
}

void TestDatabaseLookupPeerLimit ()
{
	std::array<uint8_t, 32> key { };
	std::array<uint8_t, 32> from { };
	std::unordered_set<IdentHash> excluded;
	for (int i = 0; i < 600; ++i)
	{
		IdentHash peer;
		peer.Fill (0);
		htobe32buf (peer, static_cast<uint32_t> (i + 1));
		excluded.insert (peer);
	}

	// Java DatabaseLookupMessage rejects more than MAX_NUM_PEERS (512), so a
	// serializer must not emit a larger exclusion block.
	auto msg = i2p::CreateRouterInfoDatabaseLookupMsg (key.data (), from.data (), 0, false, &excluded);
	assert (msg);
	const uint8_t * payload = msg->GetPayload ();
	assert (bufbe16toh (payload + 65) <= i2p::DATABASE_LOOKUP_MAX_NUM_PEERS);
}

void TestDatabaseStoreTypeMask ()
{
	std::array<uint8_t, i2p::DATABASE_STORE_HEADER_SIZE> payload { };
	payload[i2p::DATABASE_STORE_TYPE_OFFSET] = 0x80; // RouterInfo with future option bits
	auto routerInfo = i2p::CreateI2NPMessage (i2p::eI2NPDatabaseStore, payload.data (), payload.size ());
	assert (i2p::IsRouterInfoMsg (routerInfo));

	payload[i2p::DATABASE_STORE_TYPE_OFFSET] = 0x83; // standard LeaseSet2 + future option bits
	auto leaseSet2 = i2p::CreateI2NPMessage (i2p::eI2NPDatabaseStore, payload.data (), payload.size ());
	assert (!i2p::IsRouterInfoMsg (leaseSet2));

	// A truncated DatabaseStore must not be inspected as a full header.
	auto truncated = i2p::CreateI2NPMessage (i2p::eI2NPDatabaseStore, payload.data (), 8);
	assert (!i2p::IsRouterInfoMsg (truncated));
}
}

int main ()
{
	TestLeaseSet1FutureLease ();
	TestLeaseSet2FutureLease ();
	TestLeaseSet2P256WireKey ();
	TestLeaseSet2UnpublishedFlag ();
	TestDatabaseLookupPeerLimit ();
	TestDatabaseStoreTypeMask ();
	return 0;
}
