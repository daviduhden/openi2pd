/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test LeaseSet2 encryption-type selection.
 *
 * Reserved EC types and unavailable hybrid parameter sets must never become
 * the active LeaseSet2 encryptor; unknown advertised key types are retained
 * inert so the key list matches Java. Also pins the strict i2cp.leaseSetEncType
 * option grammar: exact decimal tokens, at most one hybrid type and no
 * ElGamal/hybrid combination. This pins the policy used by the local
 * destination configuration and the I2CP LeaseSet2 key installer.
 */

#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "Crypto.h"
#include "Destination.h"
#include "I2PEndian.h"
#include "Identity.h"
#include "LeaseSet.h"
#include "PostQuantum.h"
#include "Timestamp.h"

namespace
{
using namespace i2p::data;
using i2p::client::LeaseSetEncryptionTypes;
using i2p::client::ParseLeaseSetEncryptionTypes;

void TestSelectableTypes ()
{
	// ElGamal/AES and ECIES-X25519 are the two universal encryptors.
	assert (IsSelectableLS2EncryptionKeyType (CRYPTO_KEY_TYPE_ELGAMAL));
	assert (IsSelectableLS2EncryptionKeyType (CRYPTO_KEY_TYPE_ECIES_X25519_AEAD));

	// Java EncType.isAvailable() is false for EC_P256/P384/P521; i2pd must
	// not create a P-256 section that only looks usable.
	assert (!IsSelectableLS2EncryptionKeyType (1)); // EC_P256
	assert (!IsSelectableLS2EncryptionKeyType (2)); // EC_P384
	assert (!IsSelectableLS2EncryptionKeyType (3)); // EC_P521

#if OPENSSL_PQ
	assert (IsSelectableLS2EncryptionKeyType (CRYPTO_KEY_TYPE_ECIES_MLKEM512_X25519_AEAD)
		== i2p::crypto::IsMLKEMSupported (CRYPTO_KEY_TYPE_ECIES_MLKEM512_X25519_AEAD));
	assert (IsSelectableLS2EncryptionKeyType (CRYPTO_KEY_TYPE_ECIES_MLKEM768_X25519_AEAD)
		== i2p::crypto::IsMLKEMSupported (CRYPTO_KEY_TYPE_ECIES_MLKEM768_X25519_AEAD));
	assert (IsSelectableLS2EncryptionKeyType (CRYPTO_KEY_TYPE_ECIES_MLKEM1024_X25519_AEAD)
		== i2p::crypto::IsMLKEMSupported (CRYPTO_KEY_TYPE_ECIES_MLKEM1024_X25519_AEAD));
#else
	assert (!IsSelectableLS2EncryptionKeyType (CRYPTO_KEY_TYPE_ECIES_MLKEM512_X25519_AEAD));
	assert (!IsSelectableLS2EncryptionKeyType (CRYPTO_KEY_TYPE_ECIES_MLKEM768_X25519_AEAD));
	assert (!IsSelectableLS2EncryptionKeyType (CRYPTO_KEY_TYPE_ECIES_MLKEM1024_X25519_AEAD));
#endif

#if OPENSSL_PQ
	// The PQ selection helper must never return a version above its bound or
	// one that cannot actually be published.
	for (int maximum = 2; maximum <= 5; ++maximum)
	{
		const int highest = i2p::crypto::HighestSupportedPQVersion (5, maximum);
		assert (highest <= maximum);
		if (highest)
		{
			assert (highest >= 3);
			assert (i2p::crypto::CanPublishPQ (highest));
		}
	}
#endif
}

void TestEncTypeOptionGrammar ()
{
	// Absent/empty option: the destination uses its provider defaults.
	{
		const auto parsed = ParseLeaseSetEncryptionTypes ("");
		assert (!parsed.specified);
		assert (parsed.valid);
		assert (parsed.types.empty ());
		assert (parsed.preferred == 0);
	}

	// Well-formed lists are accepted; only selectable types are installed.
	{
		const auto parsed = ParseLeaseSetEncryptionTypes ("0");
		assert (parsed.specified && parsed.valid);
		assert (parsed.types.size () == 1 && parsed.types.count (CRYPTO_KEY_TYPE_ELGAMAL));
		assert (parsed.preferred == CRYPTO_KEY_TYPE_ELGAMAL);
	}
	{
		const auto parsed = ParseLeaseSetEncryptionTypes ("4");
		assert (parsed.valid);
		assert (parsed.types.size () == 1 && parsed.types.count (CRYPTO_KEY_TYPE_ECIES_X25519_AEAD));
		assert (parsed.preferred == CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
	}
	{
		// The default value. The hybrid entry is only installed when the active
		// provider supports it, but the grammar itself stays valid.
		const auto parsed = ParseLeaseSetEncryptionTypes ("4,0");
		assert (parsed.valid);
		assert (parsed.types.count (CRYPTO_KEY_TYPE_ECIES_X25519_AEAD));
		assert (parsed.types.count (CRYPTO_KEY_TYPE_ELGAMAL));
		assert (parsed.preferred == CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
	}
	{
		const auto parsed = ParseLeaseSetEncryptionTypes ("6");
		assert (parsed.valid);
		assert (parsed.types.count (CRYPTO_KEY_TYPE_ECIES_MLKEM768_X25519_AEAD)
			== IsSelectableLS2EncryptionKeyType (CRYPTO_KEY_TYPE_ECIES_MLKEM768_X25519_AEAD));
	}

	// Exact decimal tokens only. std::stoi() used to accept all of these.
	for (const char * malformed : { "6x", " 6", "+6", "6 ", "0x6", "4,", ",4", "4,,0", "6.0", "-6", "65536", "6\t" })
	{
		const auto parsed = ParseLeaseSetEncryptionTypes (malformed);
		assert (parsed.specified);
		assert (!parsed.valid);
		assert (parsed.types.empty ());
		assert (parsed.preferred == 0);
	}

	// More than one hybrid type is rejected (Java ClientConnectionRunner), as
	// is a hybrid combined with ElGamal.
	for (const char * mixed : { "4,6", "6,4", "6,7", "6,6", "0,6", "6,0", "0,4,6", "5,6" })
	{
		const auto parsed = ParseLeaseSetEncryptionTypes (mixed);
		assert (parsed.specified);
		assert (!parsed.valid);
		assert (parsed.types.empty ());
	}

	// Unavailable-but-well-formed types keep the configuration valid; the
	// installation step then skips them and fails closed only when nothing is
	// left (covered by the destination, not by this parser).
	{
		const auto parsed = ParseLeaseSetEncryptionTypes ("1,4");
		assert (parsed.valid);
		assert (!parsed.types.count (CRYPTO_KEY_TYPE_ECIES_P256_SHA256_AES256CBC));
		assert (parsed.types.count (CRYPTO_KEY_TYPE_ECIES_X25519_AEAD));
		assert (parsed.preferred == CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
	}
	{
		const auto parsed = ParseLeaseSetEncryptionTypes ("1");
		assert (parsed.valid);
		assert (parsed.types.empty ());
		assert (parsed.preferred == 0);
	}
}

// Builds a signed standard LeaseSet2 with the given key sections. The returned
// vector starts with the NetDb store type, which is part of the signed data.
std::vector<uint8_t> BuildLeaseSet2 (
	const PrivateKeys & keys, const std::vector<std::pair<uint16_t, std::vector<uint8_t>>> & sections)
{
	auto now = static_cast<uint32_t> (i2p::util::GetMillisecondsSinceEpoch () / 1000);
	std::vector<uint8_t> buffer;
	buffer.push_back (NETDB_STORE_TYPE_STANDARD_LEASESET2);
	auto identity = keys.GetPublic ();
	assert (identity);
	std::vector<uint8_t> identityBuffer (identity->GetFullLen ());
	assert (identity->ToBuffer (identityBuffer.data (), identityBuffer.size ()) == identityBuffer.size ());
	buffer.insert (buffer.end (), identityBuffer.begin (), identityBuffer.end ());
	uint8_t encoded[4];
	htobe32buf (encoded, now - 60);
	buffer.insert (buffer.end (), encoded, encoded + 4); // published
	htobe16buf (encoded, 300);
	buffer.insert (buffer.end (), encoded, encoded + 2); // expires
	htobe16buf (encoded, 0);
	buffer.insert (buffer.end (), encoded, encoded + 2); // flags
	htobe16buf (encoded, 0);
	buffer.insert (buffer.end (), encoded, encoded + 2); // empty properties
	assert (sections.size () <= 255);
	buffer.push_back (static_cast<uint8_t> (sections.size ()));
	for (const auto & section : sections)
	{
		uint8_t header[4];
		htobe16buf (header, section.first);
		htobe16buf (header + 2, static_cast<uint16_t> (section.second.size ()));
		buffer.insert (buffer.end (), header, header + 4);
		buffer.insert (buffer.end (), section.second.begin (), section.second.end ());
	}
	buffer.push_back (1); // one lease
	buffer.resize (buffer.size () + 32, 0x20); // gateway
	htobe32buf (encoded, 0x2000);
	buffer.insert (buffer.end (), encoded, encoded + 4); // tunnel ID
	htobe32buf (encoded, now + 300);
	buffer.insert (buffer.end (), encoded, encoded + 4); // end date
	std::vector<uint8_t> signature (keys.GetSignatureLen ());
	keys.Sign (buffer.data (), static_cast<int> (buffer.size ()), signature.data ());
	buffer.insert (buffer.end (), signature.begin (), signature.end ());
	return buffer;
}

LeaseSet2 ParseLeaseSet2 (const std::vector<uint8_t> & buffer, CryptoKeyType preferred, bool storeLeases = true)
{
	assert (buffer.size () > 1);
	return LeaseSet2 (buffer[0], buffer.data () + 1, buffer.size () - 1, storeLeases, nullptr, preferred);
}

void TestUnknownKeyTypeIsRetainedInert ()
{
	auto keys = PrivateKeys::CreateRandomKeys (
		SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
	// Type 99 is not defined by proposal 144/145. Java keeps the section in its
	// key list, so the count and IsEncryptionKey() must match, but it must never
	// become the active encryptor.
	const uint16_t unknownType = 99;
	std::vector<uint8_t> unknownKey (40, 0x5A);
	{
		const auto buffer = BuildLeaseSet2 (keys, { { unknownType, unknownKey } });
		auto leaseSet = ParseLeaseSet2 (buffer, static_cast<CryptoKeyType> (unknownType));
		assert (leaseSet.IsValid ());
		assert (leaseSet.GetNumEncryptionKeys () == 1);
		assert (leaseSet.GetEncryptionKeyType (0) == unknownType);
		assert (leaseSet.IsEncryptionKey (unknownType, unknownKey.data ()));
		// Never selectable: no encryptor and therefore no compatible crypto.
		assert (!IsSelectableLS2EncryptionKeyType (unknownType));
		assert (leaseSet.IsIncompatibleCrypto ());
		assert (leaseSet.GetEncryptionType () == 0);
	}

	// A known usable type next to it still wins, and the wire order is kept.
	{
		std::vector<uint8_t> x25519Key (32, 0x44);
		const auto buffer
			= BuildLeaseSet2 (keys, { { unknownType, unknownKey }, { CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, x25519Key } });
		auto leaseSet = ParseLeaseSet2 (buffer, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
		assert (leaseSet.IsValid ());
		assert (leaseSet.GetNumEncryptionKeys () == 2);
		assert (leaseSet.GetEncryptionKeyType (0) == unknownType);
		assert (leaseSet.GetEncryptionKeyType (1) == CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
		assert (leaseSet.IsEncryptionKey (unknownType, unknownKey.data ()));
		assert (leaseSet.IsEncryptionKey (CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, x25519Key.data ()));
		// IsEncryptionKey compares the advertised opaque bytes; a different
		// value does not match.
		std::vector<uint8_t> different (40, 0x5B);
		assert (!leaseSet.IsEncryptionKey (unknownType, different.data ()));
		assert (!leaseSet.IsIncompatibleCrypto ());
		assert (leaseSet.GetEncryptionType () == CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
	}

	// A known type with the wrong explicit length is still rejected before any
	// signature work, even though unknown types are retained.
	{
		std::vector<uint8_t> shortX25519 (31, 0x44);
		const auto buffer = BuildLeaseSet2 (keys, { { CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, shortX25519 } });
		auto leaseSet = ParseLeaseSet2 (buffer, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
		assert (!leaseSet.IsValid ());
	}
}
} // namespace

int main ()
{
	TestSelectableTypes ();
	TestEncTypeOptionGrammar ();
	TestUnknownKeyTypeIsRetainedInert ();
	return 0;
}
