/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test i2cp b33 lookup.
 *
 * Offline checks that AddressBook::GetAddress resolves the explicit
 * ".b33.i2p" form, the implicit long-final-label form and the ".i2p.alt"
 * alias, and that malformed, truncated, over-long, reserved or unsigned b33
 * labels fail instead of falling through to base32/base64 parsing. No storage,
 * resolver, destination or network is started.
 *
 */

#include <cassert>
#include <cstdint>
#include <string>

#include "AddressBook.h"
#include "Base.h"
#include "Blinding.h"
#include "Identity.h"

namespace
{
i2p::data::PrivateKeys MakeDestinationKeys ()
{
	return i2p::data::PrivateKeys::CreateRandomKeys (
		i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
}

std::string MakeB33 (const i2p::data::PrivateKeys & keys)
{
	i2p::data::BlindedPublicKey blinded (keys.GetPublic ());
	assert (blinded.IsValid ());
	const std::string b33 = blinded.ToB33 ();
	assert (!b33.empty ());
	assert (b33.size () > i2p::client::B33_ADDRESS_THRESHOLD);
	return b33;
}

// Standard CRC-32 (the b33 checksum) without a zlib dependency in the test.
uint32_t Crc32 (const uint8_t * data, size_t len)
{
	uint32_t crc = 0xFFFFFFFFu;
	for (size_t i = 0; i < len; i++)
	{
		crc ^= data[i];
		for (int bit = 0; bit < 8; bit++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
	}
	return ~crc;
}

// Re-encodes a valid b33 with a different header. A b33 stores the CRC32 of the
// key XORed into flags/signature types, so a crafted header still has one
// canonical encoding and exercises the parser's own validation.
std::string MakeB33WithHeader (
	const std::string & validB33, uint8_t flags, uint8_t signatureType, uint8_t blindedSignatureType)
{
	uint8_t raw[35] = { 0 };
	const size_t decoded = i2p::data::Base32ToByteStream (validB33, raw, sizeof (raw));
	assert (decoded == sizeof (raw));
	(void)decoded;
	const uint32_t checksum = Crc32 (raw + 3, sizeof (raw) - 3);
	raw[0] = (uint8_t)(flags ^ (checksum & 0xff));
	raw[1] = (uint8_t)(signatureType ^ ((checksum >> 8) & 0xff));
	raw[2] = (uint8_t)(blindedSignatureType ^ ((checksum >> 16) & 0xff));
	return i2p::data::ByteStreamToBase32 (raw, sizeof (raw));
}

std::string Upper (std::string value)
{
	for (auto & ch : value)
		if (ch >= 'a' && ch <= 'z') ch = static_cast<char> (ch - 'a' + 'A');
	return value;
}
} // namespace

int main ()
{
	using namespace i2p::client;

	const auto keys = MakeDestinationKeys ();
	const std::string b33 = MakeB33 (keys);
	i2p::client::AddressBook book;

	// Explicit ".b33.i2p" suffix: Java's LookupDestJob strips the suffix and
	// decodes the remaining label as a blinded destination.
	{
		auto addr = book.GetAddress (b33 + ".b33.i2p");
		assert (addr);
		assert (!addr->IsIdentHash ());
		assert (addr->blindedPublicKey);
		assert (addr->blindedPublicKey->IsValid ());
		assert (addr->ToBase32 () == b33);

		// The suffix is case-insensitive.
		auto mixedSuffix = book.GetAddress (b33 + ".B33.I2P");
		assert (mixedSuffix);
		assert (mixedSuffix->ToBase32 () == b33);

		// RFC 9476 lookup-only alias.
		auto alt = book.GetAddress (b33 + ".b33.i2p.alt");
		assert (alt);
		assert (alt->ToBase32 () == b33);
	}

	// Implicit long final label: "<b33>.i2p" is the same destination. A b33 is
	// base32, so a differently cased label decodes to the same key.
	{
		auto addr = book.GetAddress (b33 + ".i2p");
		assert (addr);
		assert (!addr->IsIdentHash ());
		assert (addr->ToBase32 () == b33);
		auto upper = book.GetAddress (Upper (b33) + ".i2p");
		assert (upper);
		assert (upper->ToBase32 () == b33);
	}

	// Rejections: a b33 name never becomes a base32 or base64 destination.
	{
		// Non-base32 character inside the label.
		std::string invalid = b33;
		invalid[0] = '*';
		assert (book.GetAddress (invalid + ".b33.i2p") == nullptr);
		assert (book.GetAddress (invalid + ".i2p") == nullptr);

		// Short label behind the explicit suffix must not be decoded as b32.
		assert (book.GetAddress ("abcdefghijklmnop.b33.i2p") == nullptr);
		assert (book.GetAddress (".b33.i2p") == nullptr);

		// Truncated and over-long labels.
		assert (book.GetAddress (b33.substr (0, b33.size () - 1) + ".b33.i2p") == nullptr);
		assert (book.GetAddress (std::string (B33_LABEL_MAX_LENGTH + 1, 'a') + ".i2p") == nullptr);
		assert (book.GetAddress (std::string (B33_LABEL_MAX_LENGTH + 1, 'a') + ".b33.i2p") == nullptr);

		// A reserved b33 flag bit is rejected instead of producing a destination
		// no peer can address.
		assert (book.GetAddress (MakeB33WithHeader (b33, 0x08, 7, 11) + ".b33.i2p") == nullptr);

		// A signature type that cannot be blinded (DSA) fails closed, both in
		// the explicit and in the implicit form.
		assert (book.GetAddress (MakeB33WithHeader (b33, 0, 0, 0) + ".b33.i2p") == nullptr);
		assert (book.GetAddress (MakeB33WithHeader (b33, 0, 0, 0) + ".i2p") == nullptr);

		// A blindable input type with an unknown blinded type is not usable.
		assert (book.GetAddress (MakeB33WithHeader (b33, 0, 7, 0) + ".i2p") == nullptr);
	}

	// Regression: .b32.i2p still resolves to the destination hash.
	{
		const std::string b32 = keys.GetPublic ()->GetIdentHash ().ToBase32 ();
		auto addr = book.GetAddress (b32 + ".b32.i2p");
		assert (addr);
		assert (addr->IsIdentHash ());
		assert (addr->identHash == keys.GetPublic ()->GetIdentHash ());
		assert (book.GetAddress (Upper (b32) + ".B32.I2P"));
		assert (book.GetAddress (b32 + ".b32.i2p.alt"));
	}

	// A b33 name is decoded locally; a rejected b33 must not be remembered as a
	// resolver miss either.
	{
		const auto before = book.GetNegativeCacheSize ();
		assert (book.GetAddress ("abcdefghijklmnop.b33.i2p") == nullptr);
		assert (book.GetAddress (std::string (B33_LABEL_MAX_LENGTH + 1, 'a') + ".i2p") == nullptr);
		assert (book.GetNegativeCacheSize () == before);

		// A plain name goes through the address book, not the b33 parser, and its
		// miss is the only case the bounded negative cache may remember.
		assert (book.GetAddress ("example.I2P") == nullptr);
		assert (book.GetNegativeCacheSize () == before + 1);
	}

	return 0;
}
