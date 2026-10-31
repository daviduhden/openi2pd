/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test leaseset2 multikey negotiation 2026.
 *
 * Checks LeaseSet2 encryption-key negotiation: publisher order versus
 * client preference, unsupported subsets, repeated types, reserved
 * P-256 and key selection for ratchet validation.
 *
 */

/*
 * Static regression checks for LeaseSet2 encryption-key negotiation:
 * multiple advertised key sections, repeated key types, client
 * preference, publisher (server) order, reserved EC key types, and
 * local key-section publishing. These vectors are self-contained: no
 * netDb, network, tunnel or router state is used.
 */

#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <set>
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

// The returned vector starts with the NetDb store type, which is
// covered by the LeaseSet2 signature but is not part of
// LeaseSet2::GetBuffer().
std::vector<uint8_t> BuildLeaseSet2 (const PrivateKeys & keys, const std::vector<KeySection> & keySections)
{
	auto now = static_cast<uint32_t> (i2p::util::GetMillisecondsSinceEpoch () / 1000);
	std::vector<uint8_t> buffer;
	buffer.push_back (NETDB_STORE_TYPE_STANDARD_LEASESET2);
	AppendIdentity (buffer, keys);
	AppendBe32 (buffer, now - 60); // published
	AppendBe16 (buffer, 300); // expires, relative to published
	AppendBe16 (buffer, 0); // flags
	AppendBe16 (buffer, 0); // empty properties
	assert (keySections.size () <= 255);
	buffer.push_back (static_cast<uint8_t> (keySections.size ()));
	for (const auto & section : keySections)
	{
		AppendBe16 (buffer, section.first);
		AppendBe16 (buffer, static_cast<uint16_t> (section.second.size ()));
		buffer.insert (buffer.end (), section.second.begin (), section.second.end ());
	}
	buffer.push_back (1); // one lease
	buffer.resize (buffer.size () + 32, 0x20); // gateway
	AppendBe32 (buffer, 0x2000); // tunnel ID
	AppendBe32 (buffer, now + 300); // end date
	AppendSignature (buffer, keys);
	return buffer;
}

LeaseSet2 ParseLeaseSet2 (const std::vector<uint8_t> & buffer, std::shared_ptr<LocalDestination> dest,
	uint16_t preferred, bool storeLeases = true)
{
	assert (!buffer.empty ());
	assert (buffer[0] == NETDB_STORE_TYPE_STANDARD_LEASESET2);
	return LeaseSet2 (buffer[0], buffer.data () + 1, buffer.size () - 1, storeLeases, dest, preferred);
}

// Minimal LocalDestination that only reports which key types it
// published. The LeaseSet2 parser never calls Decrypt() on it.
class MockLocalDestination : public LocalDestination
{
public:
	explicit MockLocalDestination (std::initializer_list<CryptoKeyType> supported)
	{
		for (auto type : supported) m_Supported.insert (type);
	}

	bool Decrypt (const uint8_t *, uint8_t *, CryptoKeyType = CRYPTO_KEY_TYPE_ELGAMAL) const override { return false; }
	std::shared_ptr<const IdentityEx> GetIdentity () const override { return nullptr; }
	bool SupportsEncryptionType (CryptoKeyType keyType) const override { return m_Supported.count (keyType) > 0; }
	const uint8_t * GetEncryptionPublicKey (CryptoKeyType) const override { return nullptr; }

private:
	std::set<CryptoKeyType> m_Supported;
};

std::shared_ptr<LocalDestination> MakeLocal (std::initializer_list<CryptoKeyType> supported)
{
	return std::make_shared<MockLocalDestination> (supported);
}

std::vector<uint8_t> KeyBytes (uint8_t value)
{
	return std::vector<uint8_t> (32, value);
}

void TestClientPreferenceAndServerOrder ()
{
	auto keys = PrivateKeys::CreateRandomKeys (
		SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
	auto key4 = KeyBytes (0x44);
	auto key6 = KeyBytes (0x66);
	// Publisher order: ML-KEM-768 first, X25519 second.
	auto buffer = BuildLeaseSet2 (
		keys, { { CRYPTO_KEY_TYPE_ECIES_MLKEM768_X25519_AEAD, key6 }, { CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, key4 } });

	// A client that prefers X25519 may select it even though the publisher
	// listed ML-KEM-768 first (proposal 123 permits client preference).
	{
		auto leaseSet = ParseLeaseSet2 (buffer,
			MakeLocal ({ CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, CRYPTO_KEY_TYPE_ECIES_MLKEM768_X25519_AEAD }),
			CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
		assert (leaseSet.IsValid ());
		assert (leaseSet.GetEncryptionType () == CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
		// Both advertised sections remain available for ratchet validation.
		assert (leaseSet.IsEncryptionKey (CRYPTO_KEY_TYPE_ECIES_MLKEM768_X25519_AEAD, key6.data ()));
		assert (leaseSet.IsEncryptionKey (CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, key4.data ()));
		uint8_t selected[32];
		memset (selected, 0xAA, sizeof (selected));
		leaseSet.Encrypt (nullptr, selected); // copies the selected static key
		assert (!memcmp (selected, key4.data (), 32));
	}

	// The same LeaseSet parsed for a peer whose ratchet session chose the
	// higher ML-KEM type selects that type (Destination passes the remote
	// static key type as preferredCrypto).
#if OPENSSL_PQ
	{
		auto leaseSet = ParseLeaseSet2 (buffer,
			MakeLocal ({ CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, CRYPTO_KEY_TYPE_ECIES_MLKEM768_X25519_AEAD }),
			CRYPTO_KEY_TYPE_ECIES_MLKEM768_X25519_AEAD);
		assert (leaseSet.IsValid ());
		assert (leaseSet.GetEncryptionType () == CRYPTO_KEY_TYPE_ECIES_MLKEM768_X25519_AEAD);
		uint8_t selected[32];
		memset (selected, 0xAA, sizeof (selected));
		leaseSet.Encrypt (nullptr, selected);
		assert (!memcmp (selected, key6.data (), 32));
	}
#endif

	// If the local preference is not advertised, the first supported key
	// in the publisher's order wins (proposal 123 server preference).
	{
		auto x25519First = BuildLeaseSet2 (keys,
			{ { CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, key4 }, { CRYPTO_KEY_TYPE_ECIES_MLKEM768_X25519_AEAD, key6 } });
		auto leaseSet = ParseLeaseSet2 (x25519First,
			MakeLocal ({ CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, CRYPTO_KEY_TYPE_ECIES_MLKEM768_X25519_AEAD }),
			CRYPTO_KEY_TYPE_ECIES_MLKEM1024_X25519_AEAD); // not advertised
		assert (leaseSet.IsValid ());
		assert (leaseSet.GetEncryptionType () == CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
	}

	// A key type that the publisher did not list is never selected, even
	// if it is locally preferred and supported.
	{
		auto only4 = BuildLeaseSet2 (keys, { { CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, key4 } });
		auto leaseSet = ParseLeaseSet2 (only4,
			MakeLocal ({ CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, CRYPTO_KEY_TYPE_ECIES_MLKEM768_X25519_AEAD }),
			CRYPTO_KEY_TYPE_ECIES_MLKEM768_X25519_AEAD);
		assert (leaseSet.IsValid ());
		assert (leaseSet.GetEncryptionType () == CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
	}
}

void TestSupportedSubsetAndIncompatible ()
{
	auto keys = PrivateKeys::CreateRandomKeys (
		SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
	auto key4 = KeyBytes (0x41);
	auto key6 = KeyBytes (0x61);
	auto buffer = BuildLeaseSet2 (
		keys, { { CRYPTO_KEY_TYPE_ECIES_MLKEM768_X25519_AEAD, key6 }, { CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, key4 } });

	// The client only has X25519: skip the unsupported first key, do not
	// declare the LeaseSet incompatible.
	{
		auto leaseSet = ParseLeaseSet2 (
			buffer, MakeLocal ({ CRYPTO_KEY_TYPE_ECIES_X25519_AEAD }), CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
		assert (leaseSet.IsValid ());
		assert (!leaseSet.IsIncompatibleCrypto ());
		assert (leaseSet.GetEncryptionType () == CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
	}

	// No advertised type is usable: valid signature, but no encryptor.
	{
		auto leaseSet = ParseLeaseSet2 (buffer, MakeLocal ({ CRYPTO_KEY_TYPE_ECIES_MLKEM512_X25519_AEAD }),
			CRYPTO_KEY_TYPE_ECIES_MLKEM512_X25519_AEAD);
		assert (leaseSet.IsValid ());
		assert (leaseSet.IsIncompatibleCrypto ());
		assert (leaseSet.GetEncryptionType () == 0);
		uint8_t untouched[32];
		memset (untouched, 0x5A, sizeof (untouched));
		leaseSet.Encrypt (nullptr, untouched);
		for (auto byte : untouched) assert (byte == 0x5A);
	}
}

void TestRepeatedKeyTypeIsRetained ()
{
	auto keys = PrivateKeys::CreateRandomKeys (
		SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
	auto first = KeyBytes (0x11);
	auto second = KeyBytes (0x22);
	// A repeated type is odd but wire-valid; Java keeps both in its List.
	auto buffer = BuildLeaseSet2 (
		keys, { { CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, first }, { CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, second } });
	auto leaseSet
		= ParseLeaseSet2 (buffer, MakeLocal ({ CRYPTO_KEY_TYPE_ECIES_X25519_AEAD }), CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
	assert (leaseSet.IsValid ());
	// Either advertised key may be the one the peer's ratchet selected, so
	// IsEncryptionKey() must accept both. A map keyed by type used to keep
	// only the last section and reject the first key.
	assert (leaseSet.IsEncryptionKey (CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, first.data ()));
	assert (leaseSet.IsEncryptionKey (CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, second.data ()));
	uint8_t selected[32];
	memset (selected, 0xAA, sizeof (selected));
	leaseSet.Encrypt (nullptr, selected);
	assert (!memcmp (selected, first.data (), 32)); // first supported section
}

void TestReservedEcTypeIsNeverSelected ()
{
	auto keys = PrivateKeys::CreateRandomKeys (
		SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
	std::vector<uint8_t> p256 (64, 0x33);
	// P256 is a reserved LS2 type and Java's EncType.isAvailable() is
	// false. i2pd can parse the section but must not select it: the garlic
	// path would send a 514-byte ElGamal block to a P256 encryptor.
	auto buffer = BuildLeaseSet2 (keys, { { CRYPTO_KEY_TYPE_ECIES_P256_SHA256_AES256CBC, p256 } });
	auto leaseSet = ParseLeaseSet2 (buffer, MakeLocal ({ CRYPTO_KEY_TYPE_ECIES_P256_SHA256_AES256CBC }),
		CRYPTO_KEY_TYPE_ECIES_P256_SHA256_AES256CBC);
	assert (leaseSet.IsValid ());
	assert (leaseSet.IsEncryptionKey (CRYPTO_KEY_TYPE_ECIES_P256_SHA256_AES256CBC, p256.data ()));
	assert (leaseSet.IsIncompatibleCrypto ());
	assert (leaseSet.GetEncryptionType () == 0);

	// A reserved EC section must not shadow a later usable key, even when
	// it is the local preference.
	auto key4 = KeyBytes (0x34);
	auto mixed = BuildLeaseSet2 (
		keys, { { CRYPTO_KEY_TYPE_ECIES_P256_SHA256_AES256CBC, p256 }, { CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, key4 } });
	auto fallback = ParseLeaseSet2 (mixed,
		MakeLocal ({ CRYPTO_KEY_TYPE_ECIES_P256_SHA256_AES256CBC, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD }),
		CRYPTO_KEY_TYPE_ECIES_P256_SHA256_AES256CBC);
	assert (fallback.IsValid ());
	assert (!fallback.IsIncompatibleCrypto ());
	assert (fallback.GetEncryptionType () == CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
	uint8_t selected[32];
	memset (selected, 0xAA, sizeof (selected));
	fallback.Encrypt (nullptr, selected);
	assert (!memcmp (selected, key4.data (), 32));
}

void TestMalformedKeySections ()
{
	auto keys = PrivateKeys::CreateRandomKeys (
		SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
	auto key4 = KeyBytes (0x77);

	// Zero key sections: LeaseSet2.java rejects numKeys <= 0.
	{
		auto buffer = BuildLeaseSet2 (keys, { });
		auto leaseSet = ParseLeaseSet2 (
			buffer, MakeLocal ({ CRYPTO_KEY_TYPE_ECIES_X25519_AEAD }), CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
		assert (!leaseSet.IsValid ());
	}

	// More than MAX_KEYS = 8 sections.
	{
		std::vector<KeySection> sections;
		for (int i = 0; i < 9; i++) sections.push_back ({ CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, key4 });
		auto buffer = BuildLeaseSet2 (keys, sections);
		auto leaseSet = ParseLeaseSet2 (
			buffer, MakeLocal ({ CRYPTO_KEY_TYPE_ECIES_X25519_AEAD }), CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
		assert (!leaseSet.IsValid ());
	}

	// A known type with a length that overruns the remaining section data
	// must be rejected before any signature work.
	{
		std::vector<uint8_t> buffer;
		buffer.push_back (NETDB_STORE_TYPE_STANDARD_LEASESET2);
		AppendIdentity (buffer, keys);
		auto now = static_cast<uint32_t> (i2p::util::GetMillisecondsSinceEpoch () / 1000);
		AppendBe32 (buffer, now - 60);
		AppendBe16 (buffer, 300);
		AppendBe16 (buffer, 0);
		AppendBe16 (buffer, 0); // empty properties
		buffer.push_back (1); // one key section
		AppendBe16 (buffer, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
		AppendBe16 (buffer, 32); // declared
		buffer.resize (buffer.size () + 10, 0x55); // only 10 bytes present
		auto leaseSet = ParseLeaseSet2 (
			buffer, MakeLocal ({ CRYPTO_KEY_TYPE_ECIES_X25519_AEAD }), CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
		assert (!leaseSet.IsValid ());
	}
}

uint16_t ReadBe16 (const uint8_t * buf)
{
	return static_cast<uint16_t> ((buf[0] << 8) | buf[1]);
}

void TestLocalKeySectionPublishing ()
{
	auto keys = PrivateKeys::CreateRandomKeys (
		SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
	auto now = static_cast<uint32_t> (i2p::util::GetMillisecondsSinceEpoch () / 1000);

	auto key4 = std::make_shared<i2p::crypto::LocalEncryptionKey> (CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
	key4->pub.assign (32, 0x44);
	auto key6 = std::make_shared<i2p::crypto::LocalEncryptionKey> (CRYPTO_KEY_TYPE_ECIES_MLKEM768_X25519_AEAD);
	key6->pub.assign (32, 0x66);
	auto unusable = std::make_shared<i2p::crypto::LocalEncryptionKey> (CRYPTO_KEY_TYPE_ECIES_MLKEM1024_X25519_AEAD);
	// Deliberately short public material: must be skipped instead of being
	// published as a truncated (malformed) key section.
	unusable->pub.assign (16, 0x77);

	i2p::data::LocalLeaseSet2::EncryptionKeys sections { key4, unusable, key6 };
	i2p::data::LocalLeaseSet2 local (NETDB_STORE_TYPE_STANDARD_LEASESET2, keys, sections, { }, false, now);
	assert (local.GetBuffer () != nullptr);
	assert (local.GetBufferLen () > 0);

	// key sections start after the LS2 header (identity +
	// published/expires/
	// flags), the two-byte properties length and the one-byte key count.
	size_t offset = keys.GetPublic ()->GetFullLen () + 4 + 2 + 2 + 2;
	const uint8_t * buf = local.GetBuffer ();
	assert (buf[offset] == 2); // unusable section skipped
	offset++;
	assert (ReadBe16 (buf + offset) == CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
	offset += 2;
	assert (ReadBe16 (buf + offset) == 32);
	offset += 2 + 32;
	assert (ReadBe16 (buf + offset) == CRYPTO_KEY_TYPE_ECIES_MLKEM768_X25519_AEAD);
	offset += 2;
	assert (ReadBe16 (buf + offset) == 32);
	offset += 2 + 32;

	// The serialized LeaseSet2 remains parseable and both sections match.
	std::vector<uint8_t> buffer;
	buffer.push_back (NETDB_STORE_TYPE_STANDARD_LEASESET2);
	buffer.insert (buffer.end (), buf, buf + local.GetBufferLen ());
	auto leaseSet = ParseLeaseSet2 (buffer,
		MakeLocal ({ CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, CRYPTO_KEY_TYPE_ECIES_MLKEM768_X25519_AEAD }),
		CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, false);
	assert (leaseSet.IsEncryptionKey (CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, key4->pub.data ()));
	assert (leaseSet.IsEncryptionKey (CRYPTO_KEY_TYPE_ECIES_MLKEM768_X25519_AEAD, key6->pub.data ()));
}

void TestEncryptedInnerNullHandling ()
{
	auto keys = PrivateKeys::CreateRandomKeys (
		SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
	// Previously dereferenced the null inner LeaseSet in the base-class
	// initialiser.
	i2p::data::LocalEncryptedLeaseSet2 encrypted (nullptr, keys, ENCRYPTED_LEASESET_AUTH_TYPE_NONE, nullptr);
	assert (encrypted.GetBuffer () == nullptr);

	// An inner LeaseSet2 with no usable key is not serialized, so wrapping
	// it must not publish an empty encrypted payload.
	i2p::data::LocalLeaseSet2::EncryptionKeys empty;
	std::vector<std::shared_ptr<i2p::tunnel::InboundTunnel>> noTunnels;
	i2p::data::LocalEncryptedLeaseSet2 wrapped (
		std::make_shared<i2p::data::LocalLeaseSet2> (
			NETDB_STORE_TYPE_STANDARD_LEASESET2, keys, empty, noTunnels, false, 0),
		keys, ENCRYPTED_LEASESET_AUTH_TYPE_NONE, nullptr);
	assert (wrapped.GetBuffer () == nullptr);
}
} // namespace

int main ()
{
	TestClientPreferenceAndServerOrder ();
	TestSupportedSubsetAndIncompatible ();
	TestRepeatedKeyTypeIsRetained ();
	TestReservedEcTypeIsNeverSelected ();
	TestMalformedKeySections ();
	TestLocalKeySectionPublishing ();
	TestEncryptedInnerNullHandling ();
	return 0;
}
