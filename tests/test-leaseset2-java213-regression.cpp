/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test leaseset2 java213 regression.
 *
 * Static checks for Java 2.13 LeaseSet, LeaseSet2 and MetaLeaseSet parsing: offline signatures, truncated blocks, lease freshness, key-section lengths and duplicate or overrunning mappings.
 *
 */

#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include "I2PEndian.h"
#include "Identity.h"
#include "LeaseSet.h"
#include "Timestamp.h"

namespace
{
using namespace i2p::data;

using KeySection = std::pair<uint16_t, std::vector<uint8_t>>;

struct MetaEntry
{
	uint32_t endDate;
	uint8_t type;
	uint8_t cost;
};

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
	std::vector<uint8_t> signature (keys.GetSignatureLen ());
	assert (!signature.empty ());
	keys.Sign (buffer.data (), static_cast<int> (buffer.size ()), signature.data ());
	buffer.insert (buffer.end (), signature.begin (), signature.end ());
}

std::vector<uint8_t> BuildLeaseSet1 (const PrivateKeys & keys, const std::vector<uint64_t> & leaseEnds)
{
	std::vector<uint8_t> buffer;
	AppendIdentity (buffer, keys);
	buffer.resize (buffer.size () + 256, 0); // encryption key
	buffer.resize (buffer.size () + keys.GetPublic ()->GetSigningPublicKeyLen (), 0); // revocation key
	buffer.push_back (static_cast<uint8_t> (leaseEnds.size ()));
	for (size_t i = 0; i < leaseEnds.size (); ++i)
	{
		buffer.resize (buffer.size () + 32, static_cast<uint8_t> (0x10 + i)); // gateway
		AppendBe32 (buffer, static_cast<uint32_t> (0x1000 + i)); // tunnel ID
		AppendBe64 (buffer, leaseEnds[i]);
	}
	AppendSignature (buffer, keys);
	return buffer;
}

// The returned vector starts with the NetDb store type, which is covered by
// the LeaseSet2 signature but is not part of LeaseSet2::GetBuffer().
std::vector<uint8_t> BuildLeaseSet2 (const PrivateKeys & keys, uint32_t published, uint16_t expires, uint16_t flags,
	const std::vector<KeySection> & keySections, const std::vector<uint32_t> & leaseEnds,
	const std::vector<uint8_t> & properties = { })
{
	std::vector<uint8_t> buffer;
	buffer.push_back (NETDB_STORE_TYPE_STANDARD_LEASESET2);
	AppendIdentity (buffer, keys);
	AppendBe32 (buffer, published);
	AppendBe16 (buffer, expires);
	AppendBe16 (buffer, flags);
	if (flags & LEASESET2_FLAG_OFFLINE_KEYS)
	{
		const auto & offlineSignature = keys.GetOfflineSignature ();
		assert (!offlineSignature.empty ());
		buffer.insert (buffer.end (), offlineSignature.begin (), offlineSignature.end ());
	}
	assert (properties.size () <= 0xFFFF);
	AppendBe16 (buffer, static_cast<uint16_t> (properties.size ()));
	buffer.insert (buffer.end (), properties.begin (), properties.end ());
	assert (keySections.size () <= 255);
	buffer.push_back (static_cast<uint8_t> (keySections.size ()));
	for (const auto & section : keySections)
	{
		AppendBe16 (buffer, section.first);
		AppendBe16 (buffer, static_cast<uint16_t> (section.second.size ()));
		buffer.insert (buffer.end (), section.second.begin (), section.second.end ());
	}
	assert (leaseEnds.size () <= 255);
	buffer.push_back (static_cast<uint8_t> (leaseEnds.size ()));
	for (size_t i = 0; i < leaseEnds.size (); ++i)
	{
		buffer.resize (buffer.size () + 32, static_cast<uint8_t> (0x20 + i)); // gateway
		AppendBe32 (buffer, static_cast<uint32_t> (0x2000 + i)); // tunnel ID
		AppendBe32 (buffer, leaseEnds[i]);
	}
	AppendSignature (buffer, keys);
	return buffer;
}

std::vector<uint8_t> BuildMetaLeaseSet (
	const PrivateKeys & keys, uint32_t published, uint16_t expires, const std::vector<MetaEntry> & entries)
{
	std::vector<uint8_t> buffer;
	buffer.push_back (NETDB_STORE_TYPE_META_LEASESET2);
	AppendIdentity (buffer, keys);
	AppendBe32 (buffer, published);
	AppendBe16 (buffer, expires);
	AppendBe16 (buffer, 0); // flags
	AppendBe16 (buffer, 0); // empty properties
	assert (entries.size () <= 255);
	buffer.push_back (static_cast<uint8_t> (entries.size ()));
	for (size_t i = 0; i < entries.size (); ++i)
	{
		buffer.resize (buffer.size () + 32, static_cast<uint8_t> (0x30 + i)); // target hash
		AppendBe16 (buffer, 0); // flags
		buffer.push_back (entries[i].type);
		buffer.push_back (entries[i].cost);
		AppendBe32 (buffer, entries[i].endDate);
	}
	buffer.push_back (0); // revocations
	AppendSignature (buffer, keys);
	return buffer;
}

LeaseSet2 ParseLeaseSet2 (const std::vector<uint8_t> & buffer, bool storeLeases = false)
{
	assert (!buffer.empty ());
	return LeaseSet2 (buffer[0], buffer.data () + 1, buffer.size () - 1, storeLeases);
}

void TestOfflineSignatureAtEndOfBuffer ()
{
	auto destination = PrivateKeys::CreateRandomKeys (
		SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
	auto now = static_cast<uint32_t> (i2p::util::GetMillisecondsSinceEpoch () / 1000);
	// CreateOfflineKeys() produces the proposal-123 block signed by the
	// destination key; the final LeaseSet2 signature is made by the transient
	// key and is the last field in the buffer.
	auto keys = destination.CreateOfflineKeys (SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, now + 3600);
	std::vector<uint8_t> key (32, 0x44);
	auto buffer = BuildLeaseSet2 (keys, now - 60, 300, LEASESET2_FLAG_OFFLINE_KEYS,
		{ { CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, key } }, { now + 300 });
	auto leaseSet = ParseLeaseSet2 (buffer);
	assert (leaseSet.IsValid ());
	assert (leaseSet.GetTransientVerifier () != nullptr);
}

void TestTruncatedSignatureWithSkipVerify ()
{
	// Update(..., verifySignature=false) is used after a caller validated the
	// signature once. The signature bytes are still structural and must be
	// present before the object is marked valid.
	auto keys = PrivateKeys::CreateRandomKeys (
		SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
	auto now = static_cast<uint32_t> (i2p::util::GetMillisecondsSinceEpoch () / 1000);
	std::vector<uint8_t> key (32, 0x55);
	auto buffer
		= BuildLeaseSet2 (keys, now - 60, 300, 0, { { CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, key } }, { now + 300 });
	auto leaseSet = ParseLeaseSet2 (buffer);
	assert (leaseSet.IsValid ());
	leaseSet.Update (buffer.data () + 1, buffer.size () - 2, nullptr, false);
	assert (!leaseSet.IsValid ());
}

void TestStandardLeaseFreshness ()
{
	auto keys = PrivateKeys::CreateRandomKeys (
		SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
	auto now = static_cast<uint32_t> (i2p::util::GetMillisecondsSinceEpoch () / 1000);
	std::vector<uint8_t> key (32, 0x66);

	// Java validates the earliest lease (<= now - 10 min rejects), not only
	// the latest lease.
	{
		auto buffer = BuildLeaseSet2 (
			keys, now - 60, 360, 0, { { CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, key } }, { now + 300, now - 660 });
		auto leaseSet = ParseLeaseSet2 (buffer);
		assert (!leaseSet.IsValid ());
	}

	// A standard LS2 with an old published timestamp but current leases is
	// accepted by Java's validate(); only the leases are dated there.
	{
		auto buffer = BuildLeaseSet2 (
			keys, now - 20 * 60, 1500, 0, { { CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, key } }, { now + 300 });
		auto leaseSet = ParseLeaseSet2 (buffer);
		assert (leaseSet.IsValid ());
	}

	// A stale earliest lease plus one current lease is also rejected for LS1.
	{
		auto buffer = BuildLeaseSet1 (keys, { now * 1000ULL + 300000, now * 1000ULL - 660000 });
		uint64_t expires = 0;
		assert (!LeaseSetBufferValidate (buffer.data (), buffer.size (), expires));
		LeaseSet leaseSet (buffer.data (), buffer.size (), false);
		assert (!leaseSet.IsValid ());
	}
}

void TestLeaseSet2KeySections ()
{
	auto keys = PrivateKeys::CreateRandomKeys (
		SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
	auto now = static_cast<uint32_t> (i2p::util::GetMillisecondsSinceEpoch () / 1000);

	// A known encryption type with the wrong explicit length is malformed in
	// Java (PublicKey(type, data) throws); it must not be silently skipped.
	{
		std::vector<uint8_t> key (31, 0x77);
		auto buffer
			= BuildLeaseSet2 (keys, now - 60, 300, 0, { { CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, key } }, { now + 300 });
		auto leaseSet = ParseLeaseSet2 (buffer);
		assert (!leaseSet.IsValid ());
	}

	// Unknown key types are explicitly length-delimited and must remain
	// parseable (they are simply unusable for encryption).
	{
		std::vector<uint8_t> key (5, 0x78);
		auto buffer = BuildLeaseSet2 (keys, now - 60, 300, 0, { { 65000, key } }, { now + 300 });
		auto leaseSet = ParseLeaseSet2 (buffer, true);
		assert (leaseSet.IsValid ());
		assert (leaseSet.IsIncompatibleCrypto ());
	}

	// EC_P256 uses the 64-byte wire key, not the 256-byte legacy key-file
	// representation.
	{
		std::vector<uint8_t> key (64, 0x79);
		auto buffer = BuildLeaseSet2 (
			keys, now - 60, 300, 0, { { CRYPTO_KEY_TYPE_ECIES_P256_SHA256_AES256CBC, key } }, { now + 300 });
		auto leaseSet = ParseLeaseSet2 (buffer, true);
		assert (leaseSet.IsValid ());
		assert (leaseSet.IsEncryptionKey (CRYPTO_KEY_TYPE_ECIES_P256_SHA256_AES256CBC, key.data ()));
	}

	// Mapping entries are length-delimited; a string that overruns the
	// declared mapping size is malformed (Java readProperties throws).
	{
		std::vector<uint8_t> key (32, 0x7C);
		std::vector<uint8_t> properties { 5, 'a' };
		auto buffer = BuildLeaseSet2 (
			keys, now - 60, 300, 0, { { CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, key } }, { now + 300 }, properties);
		auto leaseSet = ParseLeaseSet2 (buffer);
		assert (!leaseSet.IsValid ());
	}

	// Duplicate property keys are also rejected by Java.
	{
		std::vector<uint8_t> key (32, 0x7C);
		std::vector<uint8_t> properties { 1, 'a', '=', 1, '1', ';', 1, 'a', '=', 1, '2', ';' };
		auto buffer = BuildLeaseSet2 (
			keys, now - 60, 300, 0, { { CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, key } }, { now + 300 }, properties);
		auto leaseSet = ParseLeaseSet2 (buffer);
		assert (!leaseSet.IsValid ());
	}

	// A well-formed property is still accepted.
	{
		std::vector<uint8_t> key (32, 0x7C);
		std::vector<uint8_t> properties { 1, 'a', '=', 1, '1', ';' };
		auto buffer = BuildLeaseSet2 (
			keys, now - 60, 300, 0, { { CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, key } }, { now + 300 }, properties);
		auto leaseSet = ParseLeaseSet2 (buffer);
		assert (leaseSet.IsValid ());
	}
}

void TestMetaLeaseValidation ()
{
	auto keys = PrivateKeys::CreateRandomKeys (
		SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
	auto now = static_cast<uint32_t> (i2p::util::GetMillisecondsSinceEpoch () / 1000);

	// MetaLeaseSet.readBytes() adds every entry through LeaseSet.addLease(),
	// which enforces MAX_LEASES = 16.
	{
		std::vector<MetaEntry> entries (17, MetaEntry { now + 3000, NETDB_STORE_TYPE_STANDARD_LEASESET2, 1 });
		auto buffer = BuildMetaLeaseSet (keys, now, 3600, entries);
		auto leaseSet = ParseLeaseSet2 (buffer);
		assert (!leaseSet.IsValid ());
	}

	// Java's earliest = min(earliest entry, published) rejects a Meta LS2
	// whose oldest entry is more than 10 minutes old even if another entry is
	// current.
	{
		std::vector<MetaEntry> entries {
			{ now + 300, NETDB_STORE_TYPE_STANDARD_LEASESET2, 1 },
			{ now - 660, NETDB_STORE_TYPE_STANDARD_LEASESET2, 1 },
		};
		auto buffer = BuildMetaLeaseSet (keys, now, 300, entries);
		auto leaseSet = ParseLeaseSet2 (buffer);
		assert (!leaseSet.IsValid ());
	}

	// A normal, current Meta LS2 remains valid.
	{
		std::vector<MetaEntry> entries {
			{ now + 300, NETDB_STORE_TYPE_STANDARD_LEASESET2, 1 },
			{ now + 600, NETDB_STORE_TYPE_META_LEASESET2, 5 },
		};
		auto buffer = BuildMetaLeaseSet (keys, now, 600, entries);
		auto leaseSet = ParseLeaseSet2 (buffer);
		assert (leaseSet.IsValid ());
		// MetaLeaseSet derives its earliest lease from the entries; the
		// cross-type LS1/Meta replacement rule must not use the encrypted
		// outer-layer zero sentinel.
		assert (leaseSet.GetEarliestLeaseDate () == static_cast<uint64_t> (now + 300) * 1000);
	}
}

void TestEncryptedOuterAndInnerLayers ()
{
	auto keys = PrivateKeys::CreateRandomKeys (
		SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
	auto now = static_cast<uint32_t> (i2p::util::GetMillisecondsSinceEpoch () / 1000);
	std::vector<uint8_t> key (32, 0x7A);
	auto innerBuffer = BuildLeaseSet2 (keys, now - 60, 300, LEASESET2_FLAG_PUBLISHED_ENCRYPTED,
		{ { CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, key } }, { now + 300 });
	auto inner = std::make_shared<LocalLeaseSet2> (
		NETDB_STORE_TYPE_STANDARD_LEASESET2, keys.GetPublic (), innerBuffer.data () + 1, innerBuffer.size () - 1);

	// Java rejects per-client auth without client keys (or with an invalid
	// auth type). The previous code silently published an unauthenticated LS2.
	{
		auto emptyKeys = std::make_shared<std::vector<AuthPublicKey>> ();
		LocalEncryptedLeaseSet2 encrypted (inner, keys, ENCRYPTED_LEASESET_AUTH_TYPE_DH, emptyKeys);
		assert (encrypted.GetBuffer () == nullptr);
	}
	{
		LocalEncryptedLeaseSet2 encrypted (inner, keys, 99, nullptr);
		assert (encrypted.GetBuffer () == nullptr);
	}

	// Outer-only verification (the floodfill path) and full decryption of the
	// inner layer. The raw buffer deliberately has no type byte in front of
	// it, exercising the signature-prefix path without writing buf[-1].
	LocalEncryptedLeaseSet2 encrypted (inner, keys, ENCRYPTED_LEASESET_AUTH_TYPE_NONE, nullptr);
	assert (encrypted.GetBuffer () != nullptr);
	LeaseSet2 outer (NETDB_STORE_TYPE_ENCRYPTED_LEASESET2, encrypted.GetBuffer (), encrypted.GetBufferLen (), false);
	assert (outer.IsValid ());
	assert (outer.GetStoreType () == NETDB_STORE_TYPE_ENCRYPTED_LEASESET2);
	assert (outer.IsStoreKey (encrypted.GetStoreHash ()));

	LeaseSet2 copied (NETDB_STORE_TYPE_ENCRYPTED_LEASESET2, encrypted.GetBuffer (), encrypted.GetBufferLen (), false);
	assert (copied.IsValid ());
	auto blindedKey = std::make_shared<BlindedPublicKey> (keys.GetPublic ());
	LeaseSet2 decrypted (copied.GetBuffer (), copied.GetBufferLen (), blindedKey, nullptr, nullptr);
	assert (decrypted.IsValid ());
	assert (decrypted.GetStoreType () == NETDB_STORE_TYPE_STANDARD_LEASESET2);
	assert (decrypted.GetPublishedTimestamp () >= now);
	assert (decrypted.GetExpirationTime () <= copied.GetExpirationTime ());
	assert (decrypted.GetNonExpiredLeases ().size () == 1);
	assert (decrypted.IsPublishedEncrypted ());
}

void TestAuthProtectedInnerNeedsSecret ()
{
	auto keys = PrivateKeys::CreateRandomKeys (
		SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
	auto now = static_cast<uint32_t> (i2p::util::GetMillisecondsSinceEpoch () / 1000);
	std::vector<uint8_t> key (32, 0x7B);
	auto innerBuffer
		= BuildLeaseSet2 (keys, now - 60, 300, 0, { { CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, key } }, { now + 300 });
	auto inner = std::make_shared<LocalLeaseSet2> (
		NETDB_STORE_TYPE_STANDARD_LEASESET2, keys.GetPublic (), innerBuffer.data () + 1, innerBuffer.size () - 1);
	auto authKeys = std::make_shared<std::vector<AuthPublicKey>> ();
	AuthPublicKey authKey { };
	memset (authKey, 0x5A, sizeof (authKey));
	authKeys->push_back (authKey);

	LocalEncryptedLeaseSet2 encrypted (inner, keys, ENCRYPTED_LEASESET_AUTH_TYPE_DH, authKeys);
	assert (encrypted.GetBuffer () != nullptr);
	auto blindedKey = std::make_shared<BlindedPublicKey> (keys.GetPublic ());
	LeaseSet2 undecryptable (encrypted.GetBuffer (), encrypted.GetBufferLen (), blindedKey, nullptr, nullptr);
	assert (!undecryptable.IsValid ());
}

void TestNullAndEmptyBuffers ()
{
	uint64_t expires = 123;
	assert (!LeaseSetBufferValidate (nullptr, 0, expires));
	assert (expires == 0);

	LeaseSet leaseSet (nullptr, 0, false);
	assert (!leaseSet.IsValid ());

	LeaseSet2 leaseSet2 (NETDB_STORE_TYPE_STANDARD_LEASESET2, nullptr, 0, false);
	assert (!leaseSet2.IsValid ());
}
}

int main ()
{
	TestOfflineSignatureAtEndOfBuffer ();
	TestTruncatedSignatureWithSkipVerify ();
	TestStandardLeaseFreshness ();
	TestLeaseSet2KeySections ();
	TestMetaLeaseValidation ();
	TestEncryptedOuterAndInnerLayers ();
	TestAuthProtectedInnerNeedsSecret ();
	TestNullAndEmptyBuffers ();
	return 0;
}
