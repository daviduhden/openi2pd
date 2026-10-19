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
#include <string>
#include <string_view>
#include <vector>

#include "Base.h"
#include "I2NPProtocol.h"
#include "I2PEndian.h"
#include "Identity.h"
#include "LeaseSet.h"
#include "NetDb.hpp"
#include "RouterContext.h"
#include "RouterInfo.h"
#include "Timestamp.h"

// Static regression checks for the netDb store/replacement and DatabaseLookup
// reply-key rules. No network, filesystem, netDb thread or request manager is
// started.

namespace
{
using namespace i2p::data;

void AppendBe16 (std::vector<uint8_t> & out, uint16_t value)
{
	uint8_t encoded[2];
	htobe16buf (encoded, value);
	out.insert (out.end (), encoded, encoded + sizeof (encoded));
}

void AppendBe32 (std::vector<uint8_t> & out, uint32_t value)
{
	uint8_t encoded[4];
	htobe32buf (encoded, value);
	out.insert (out.end (), encoded, encoded + sizeof (encoded));
}

void AppendBe64 (std::vector<uint8_t> & out, uint64_t value)
{
	uint8_t encoded[8];
	htobe64buf (encoded, value);
	out.insert (out.end (), encoded, encoded + sizeof (encoded));
}

void AppendString (std::vector<uint8_t> & out, std::string_view value)
{
	assert (value.size () <= 255);
	out.push_back (static_cast<uint8_t> (value.size ()));
	out.insert (out.end (), value.begin (), value.end ());
}

void AppendParam (std::string & out, std::string_view key, std::string_view value)
{
	assert (key.size () <= 255 && value.size () <= 255);
	out.push_back (static_cast<char> (key.size ()));
	out.append (key);
	out.push_back ('=');
	out.push_back (static_cast<char> (value.size ()));
	out.append (value);
	out.push_back (';');
}

void AppendIdentity (std::vector<uint8_t> & out, const PrivateKeys & keys)
{
	auto identity = keys.GetPublic ();
	assert (identity);
	auto offset = out.size ();
	out.resize (offset + identity->GetFullLen ());
	assert (identity->ToBuffer (out.data () + offset, identity->GetFullLen ()) == identity->GetFullLen ());
}

void AppendSignature (std::vector<uint8_t> & out, const PrivateKeys & keys)
{
	auto identity = keys.GetPublic ();
	assert (identity);
	std::vector<uint8_t> signature (identity->GetSignatureLen ());
	keys.Sign (out.data (), static_cast<int> (out.size ()), signature.data ());
	out.insert (out.end (), signature.begin (), signature.end ());
}

std::vector<uint8_t> BuildLeaseSet1 (const PrivateKeys & keys, const std::vector<uint64_t> & endDates)
{
	assert (!endDates.empty () && endDates.size () <= MAX_NUM_LEASES);
	std::vector<uint8_t> buffer;
	AppendIdentity (buffer, keys);
	buffer.resize (buffer.size () + 256, 0); // encryption key
	buffer.resize (buffer.size () + keys.GetPublic ()->GetSigningPublicKeyLen (), 0); // revocation key
	buffer.push_back (static_cast<uint8_t> (endDates.size ()));
	for (size_t i = 0; i < endDates.size (); ++i)
	{
		buffer.resize (buffer.size () + 32, static_cast<uint8_t> (0x30 + i)); // gateway
		AppendBe32 (buffer, static_cast<uint32_t> (0x01020304 + i)); // tunnel ID
		AppendBe64 (buffer, endDates[i]);
	}
	AppendSignature (buffer, keys);
	return buffer;
}

std::vector<uint8_t> BuildLeaseSet2 (
	const PrivateKeys & keys, uint32_t published, uint32_t leaseEnd, uint16_t flags = 0)
{
	assert (leaseEnd >= published);
	std::vector<uint8_t> buffer;
	buffer.push_back (NETDB_STORE_TYPE_STANDARD_LEASESET2); // covered by the LS2 signature
	AppendIdentity (buffer, keys);
	AppendBe32 (buffer, published);
	AppendBe16 (buffer, static_cast<uint16_t> (leaseEnd - published));
	AppendBe16 (buffer, flags);
	AppendBe16 (buffer, 0); // properties
	buffer.push_back (1); // one encryption key
	AppendBe16 (buffer, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
	AppendBe16 (buffer, 32);
	buffer.resize (buffer.size () + 32, 0x44);
	buffer.push_back (1); // one lease
	buffer.resize (buffer.size () + 32, 0x55); // gateway
	AppendBe32 (buffer, 0x01020304); // tunnel ID
	AppendBe32 (buffer, leaseEnd);
	AppendSignature (buffer, keys);
	return buffer;
}

std::string BuildSSU2AddressOptions ()
{
	std::array<uint8_t, 32> staticKey { };
	std::array<uint8_t, 32> introKey { };
	std::string options;
	AppendParam (options, "host", "8.8.8.8");
	AppendParam (options, "port", "12345");
	AppendParam (options, "s", ByteStreamToBase64 (staticKey.data (), staticKey.size ()));
	AppendParam (options, "i", ByteStreamToBase64 (introKey.data (), introKey.size ()));
	AppendParam (options, "v", "2");
	AppendParam (options, "caps", "BC");
	return options;
}

std::vector<uint8_t> BuildRouterInfo (
	const PrivateKeys & keys, uint64_t published, bool floodfill, const char * version)
{
	std::vector<uint8_t> body;
	AppendBe64 (body, published);
	body.push_back (1); // one address
	body.push_back (0); // cost
	AppendBe64 (body, 0); // address date
	AppendString (body, "SSU2");
	auto addressOptions = BuildSSU2AddressOptions ();
	AppendBe16 (body, static_cast<uint16_t> (addressOptions.size ()));
	body.insert (body.end (), addressOptions.begin (), addressOptions.end ());
	body.push_back (0); // no peer hashes

	std::string properties;
	AppendParam (properties, "caps", floodfill ? "f" : "O");
	AppendParam (properties, "netId", std::to_string (i2p::context.GetNetID ()));
	AppendParam (properties, "router.version", version);
	AppendBe16 (body, static_cast<uint16_t> (properties.size ()));
	body.insert (body.end (), properties.begin (), properties.end ());

	std::vector<uint8_t> buffer;
	AppendIdentity (buffer, keys);
	buffer.insert (buffer.end (), body.begin (), body.end ());
	AppendSignature (buffer, keys);
	return buffer;
}

void TestRouterInfoReplacementPreservesValidEntry ()
{
	auto keys
		= PrivateKeys::CreateRandomKeys (SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
	auto ident = keys.GetPublic ()->GetIdentHash ();
	auto now = i2p::util::GetMillisecondsSinceEpoch ();

	NetDb db;
	auto originalBuffer = BuildRouterInfo (keys, now - 60 * 1000, false, "2.13.0");
	assert (db.AddRouterInfo (ident, originalBuffer.data (), originalBuffer.size ()));
	auto original = db.FindRouter (ident);
	assert (original && !original->IsUnreachable ());
	assert (db.GetNumRouters () == 1);

	// A newer RI from the future is rejected before the stored object is
	// touched. The old code updated first and then erased the entry.
	auto futureBuffer = BuildRouterInfo (keys, now + 60 * 60 * 1000, false, "2.13.0");
	assert (!db.AddRouterInfo (ident, futureBuffer.data (), futureBuffer.size ()));
	auto afterFuture = db.FindRouter (ident);
	assert (afterFuture == original);
	assert (afterFuture->GetTimestamp () == now - 60 * 1000);

	// A newer RI with an invalid signature is also rejected without erasing.
	auto badSignature = BuildRouterInfo (keys, now, false, "2.13.0");
	badSignature.back () ^= 0x80;
	assert (!db.AddRouterInfo (ident, badSignature.data (), badSignature.size ()));
	assert (db.FindRouter (ident) == original);

	// A valid newer RI replaces the entry contents. The stored object is
	// updated in place so existing shared_ptr holders see the new state.
	auto newerBuffer = BuildRouterInfo (keys, now, false, "2.13.0");
	assert (db.AddRouterInfo (ident, newerBuffer.data (), newerBuffer.size ()));
	auto newer = db.FindRouter (ident);
	assert (newer == original);
	assert (newer->GetTimestamp () == now);
}

void TestFloodfillMembershipFollowsEligibility ()
{
	auto keys
		= PrivateKeys::CreateRandomKeys (SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
	auto ident = keys.GetPublic ()->GetIdentHash ();
	auto now = i2p::util::GetMillisecondsSinceEpoch ();

	NetDb db;
	auto floodfill = BuildRouterInfo (keys, now - 60 * 1000, true, "2.13.0");
	assert (db.AddRouterInfo (ident, floodfill.data (), floodfill.size ()));
	assert (db.GetNumFloodfills () == 1);

	// A newer RI that still declares 'f' but fails IsEligibleFloodfill()
	// (version below 0.9.62) must be removed from the floodfill table.
	auto ineligible = BuildRouterInfo (keys, now, true, "0.9.50");
	assert (db.AddRouterInfo (ident, ineligible.data (), ineligible.size ()));
	assert (db.GetNumFloodfills () == 0);

	// Becoming a floodfill again re-inserts it.
	auto eligible = BuildRouterInfo (keys, now + 1000, true, "2.13.0");
	assert (db.AddRouterInfo (ident, eligible.data (), eligible.size ()));
	assert (db.GetNumFloodfills () == 1);

	// Losing the 'f' cap removes it as well.
	auto notFloodfill = BuildRouterInfo (keys, now + 2000, false, "2.13.0");
	assert (db.AddRouterInfo (ident, notFloodfill.data (), notFloodfill.size ()));
	assert (db.GetNumFloodfills () == 0);

	// ClearRouterInfos() takes the container locks and clears both views.
	db.ClearRouterInfos ();
	assert (db.GetNumRouters () == 0);
	assert (db.GetNumFloodfills () == 0);
}

void TestLeaseSet1StoreAndReplacement ()
{
	auto keys
		= PrivateKeys::CreateRandomKeys (SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
	auto ident = keys.GetPublic ()->GetIdentHash ();
	auto now = i2p::util::GetMillisecondsSinceEpoch ();

	NetDb db;
	auto current = BuildLeaseSet1 (keys, { now + 5 * 60 * 1000, now + 10 * 60 * 1000 });
	assert (db.AddLeaseSet (ident, current.data (), current.size ()));
	auto stored = db.FindLeaseSet (ident);
	assert (stored && stored->IsValid ());

	// Java validate(): the earliest lease may not already be older than
	// 10 minutes, even if another lease in the same set is still current.
	auto staleEarliest = BuildLeaseSet1 (keys, { now - 20 * 60 * 1000, now + 12 * 60 * 1000 });
	assert (!db.AddLeaseSet (ident, staleEarliest.data (), staleEarliest.size ()));
	assert (db.FindLeaseSet (ident) == stored);

	// A far-future lease is invalid; the old entry stays valid.
	auto futureLease = BuildLeaseSet1 (keys, { now + 20 * 60 * 1000 });
	assert (!db.AddLeaseSet (ident, futureLease.data (), futureLease.size ()));
	assert (db.FindLeaseSet (ident) == stored);

	// A replay with an older earliest lease is rejected by IsNewer().
	auto replay = BuildLeaseSet1 (keys, { now + 1 * 60 * 1000, now + 11 * 60 * 1000 });
	assert (!db.AddLeaseSet (ident, replay.data (), replay.size ()));
	assert (db.FindLeaseSet (ident) == stored);

	// A genuinely newer set (later earliest lease) replaces it.
	auto newer = BuildLeaseSet1 (keys, { now + 6 * 60 * 1000, now + 12 * 60 * 1000 });
	assert (db.AddLeaseSet (ident, newer.data (), newer.size ()));
	auto updated = db.FindLeaseSet (ident);
	assert (updated && updated != stored);
	assert (updated->GetExpirationTime () > stored->GetExpirationTime ());
}

void TestLeaseSet2ReplacementAndReplay ()
{
	auto keys
		= PrivateKeys::CreateRandomKeys (SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
	auto ident = keys.GetPublic ()->GetIdentHash ();
	auto now = static_cast<uint32_t> (i2p::util::GetMillisecondsSinceEpoch () / 1000);

	NetDb db;
	auto current = BuildLeaseSet2 (keys, now - 60, now + 300);
	assert (db.AddLeaseSet2 (ident, current.data () + 1, current.size () - 1, NETDB_STORE_TYPE_STANDARD_LEASESET2));
	auto stored = db.FindLeaseSet (ident);
	assert (stored && stored->IsValid ());

	// A far-future replacement is rejected during validation.
	auto future = BuildLeaseSet2 (keys, now - 60, now + 20 * 60);
	assert (!db.AddLeaseSet2 (ident, future.data () + 1, future.size () - 1, NETDB_STORE_TYPE_STANDARD_LEASESET2));
	assert (db.FindLeaseSet (ident) == stored);

	// An older published timestamp is a replay, even if still current.
	auto replay = BuildLeaseSet2 (keys, now - 120, now + 300);
	assert (!db.AddLeaseSet2 (ident, replay.data () + 1, replay.size () - 1, NETDB_STORE_TYPE_STANDARD_LEASESET2));
	assert (db.FindLeaseSet (ident) == stored);

	// A newer published timestamp replaces it.
	auto newer = BuildLeaseSet2 (keys, now - 30, now + 300);
	assert (db.AddLeaseSet2 (ident, newer.data () + 1, newer.size () - 1, NETDB_STORE_TYPE_STANDARD_LEASESET2));
	auto updated = db.FindLeaseSet (ident);
	assert (updated && updated != stored);
}

void TestLeaseSetCrossTypeFreshness ()
{
	auto keys
		= PrivateKeys::CreateRandomKeys (SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
	auto ident = keys.GetPublic ()->GetIdentHash ();
	auto now = i2p::util::GetMillisecondsSinceEpoch ();
	auto nowSec = static_cast<uint32_t> (now / 1000);

	NetDb db;
	auto ls1 = BuildLeaseSet1 (keys, { now + 5 * 60 * 1000, now + 10 * 60 * 1000 });
	assert (db.AddLeaseSet (ident, ls1.data (), ls1.size ()));
	auto storedLs1 = db.FindLeaseSet (ident);
	assert (storedLs1 && storedLs1->GetStoreType () == NETDB_STORE_TYPE_LEASESET);

	// Java isNewer() uses the earliest lease date for an LS1<->LS2 transition.
	auto ls2Newer = BuildLeaseSet2 (keys, nowSec - 60, nowSec + 6 * 60);
	assert (db.AddLeaseSet2 (ident, ls2Newer.data () + 1, ls2Newer.size () - 1,
		NETDB_STORE_TYPE_STANDARD_LEASESET2));
	auto storedLs2 = db.FindLeaseSet (ident);
	assert (storedLs2 && storedLs2 != storedLs1);
	assert (storedLs2->GetStoreType () == NETDB_STORE_TYPE_STANDARD_LEASESET2);

	// The reverse direction also compares earliest leases, not header expiry.
	auto ls1Newer = BuildLeaseSet1 (keys, { now + 7 * 60 * 1000, now + 12 * 60 * 1000 });
	assert (db.AddLeaseSet (ident, ls1Newer.data (), ls1Newer.size ()));
	auto storedLs1Again = db.FindLeaseSet (ident);
	assert (storedLs1Again && storedLs1Again != storedLs2);
	assert (storedLs1Again->GetStoreType () == NETDB_STORE_TYPE_LEASESET);

	// An older LS2 must not evict the newer LS1.
	auto ls2Older = BuildLeaseSet2 (keys, nowSec - 60, nowSec + 4 * 60);
	assert (!db.AddLeaseSet2 (ident, ls2Older.data () + 1, ls2Older.size () - 1,
		NETDB_STORE_TYPE_STANDARD_LEASESET2));
	assert (db.FindLeaseSet (ident) == storedLs1Again);
}

void TestDatabaseLookupReplyKeyParser ()
{
	DatabaseLookupReplyKey parsed;
	std::array<uint8_t, 200> block { };
	for (size_t i = 0; i < block.size (); ++i) block[i] = static_cast<uint8_t> (i);

	// No encryption flags: no block is present, and this is not an error.
	assert (ParseDatabaseLookupReplyKey (nullptr, 0, 0, parsed));
	assert (parsed.mode == DatabaseLookupReplyKey::Mode::None);

	// 0x02: 32-byte key, count, count*32-byte ElGamal/AES tags.
	block[32] = 1;
	assert (ParseDatabaseLookupReplyKey (block.data (), 33 + 32, i2p::DATABASE_LOOKUP_ENCRYPTION_FLAG, parsed));
	assert (parsed.mode == DatabaseLookupReplyKey::Mode::ElGamalAES);
	assert (!memcmp (parsed.key.data (), block.data (), 32));
	assert (!memcmp (parsed.tag.data (), block.data () + 33, 32));
	assert (!ParseDatabaseLookupReplyKey (block.data (), 33, i2p::DATABASE_LOOKUP_ENCRYPTION_FLAG, parsed));
	assert (!ParseDatabaseLookupReplyKey (block.data (), 33 + 31, i2p::DATABASE_LOOKUP_ENCRYPTION_FLAG, parsed));
	block[32] = 0;
	assert (!ParseDatabaseLookupReplyKey (block.data (), 33, i2p::DATABASE_LOOKUP_ENCRYPTION_FLAG, parsed));
	block[32] = i2p::DATABASE_LOOKUP_MAX_NUM_TAGS + 1;
	assert (!ParseDatabaseLookupReplyKey (block.data (), 33 + 32, i2p::DATABASE_LOOKUP_ENCRYPTION_FLAG, parsed));

	// 0x10: 32-byte key, count, count*8-byte ECIES tags.
	block[32] = 1;
	assert (ParseDatabaseLookupReplyKey (block.data (), 33 + 8, i2p::DATABASE_LOOKUP_RATCHET_FLAG, parsed));
	assert (parsed.mode == DatabaseLookupReplyKey::Mode::ECIESAEAD);
	assert (!memcmp (parsed.tag.data (), block.data () + 33, 8));
	assert (!ParseDatabaseLookupReplyKey (block.data (), 33, i2p::DATABASE_LOOKUP_RATCHET_FLAG, parsed));
	assert (!ParseDatabaseLookupReplyKey (block.data (), 33 + 7, i2p::DATABASE_LOOKUP_RATCHET_FLAG, parsed));

	// 0x12: proposal 154 derived keys are TBD. There is no tag/count and no
	// specified KDF, so this must fail closed instead of producing a reply.
	block[32] = 0;
	assert (!ParseDatabaseLookupReplyKey (
		block.data (), 32, i2p::DATABASE_LOOKUP_ENCRYPTION_FLAG | i2p::DATABASE_LOOKUP_RATCHET_FLAG, parsed));

	// A 0x10-only reply must not be parsed as an ElGamal tag block.
	assert (!ParseDatabaseLookupReplyKey (block.data (), 33 + 8, i2p::DATABASE_LOOKUP_ENCRYPTION_FLAG, parsed));
}
}

int main ()
{
	TestRouterInfoReplacementPreservesValidEntry ();
	TestFloodfillMembershipFollowsEligibility ();
	TestLeaseSet1StoreAndReplacement ();
	TestLeaseSet2ReplacementAndReplay ();
	TestLeaseSetCrossTypeFreshness ();
	TestDatabaseLookupReplyKeyParser ();
	return 0;
}
