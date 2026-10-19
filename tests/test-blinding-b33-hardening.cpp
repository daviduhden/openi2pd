/*
 * Copyright (c) 2026, David Uhden Collado
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Proposal 149 b33 checks:
 *   - a secret-required address cannot be treated as an ordinary address
 *     (there is no secret in this representation);
 *   - the per-client-auth flag survives a parse/encode round trip;
 *   - unknown flag bits and non-canonical Base32 are rejected;
 *   - the two-byte sigtype form parses to the same key as the one-byte form.
 */

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>

#include "Base.h"
#include "I2PEndian.h"
#include "Identity.h"
#include "Blinding.h"

namespace
{
uint32_t Crc32 (const uint8_t * data, size_t len)
{
	uint32_t crc = 0xFFFFFFFFu;
	for (size_t i = 0; i < len; ++i)
	{
		crc ^= data[i];
		for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
	}
	return ~crc;
}

std::string Reencode (const std::string & b33, const std::function<void (uint8_t *, size_t)> & mutate)
{
	std::array<uint8_t, 64> raw { };
	size_t len = i2p::data::Base32ToByteStream (b33, raw.data (), raw.size ());
	assert (len == 35);

	uint32_t checksum = Crc32 (raw.data () + 3, len - 3);
	raw[0] ^= (uint8_t)checksum;
	raw[1] ^= (uint8_t)(checksum >> 8);
	raw[2] ^= (uint8_t)(checksum >> 16);

	mutate (raw.data (), len);

	checksum = Crc32 (raw.data () + 3, len - 3);
	raw[0] ^= (uint8_t)checksum;
	raw[1] ^= (uint8_t)(checksum >> 8);
	raw[2] ^= (uint8_t)(checksum >> 16);
	return i2p::data::ByteStreamToBase32 (raw.data (), len);
}

void TestFlags ()
{
	using namespace i2p::data;

	auto keys = PrivateKeys::CreateRandomKeys (SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519);
	BlindedPublicKey original (keys.GetPublic ());
	assert (original.IsValid ());
	const std::string b33 = original.ToB33 ();
	assert (!b33.empty ());

	// Secret-required addresses need GenerateAlpha()'s secret input. The
	// class has no secret parameter, so accepting the flag would produce a
	// store hash that cannot be looked up.
	const std::string secret = Reencode (b33, [] (uint8_t * raw, size_t) { raw[0] |= 0x02; });
	BlindedPublicKey secretKey (secret);
	assert (!secretKey.IsValid ());
	assert (secretKey.ToB33 ().empty ());
	auto storeHash = secretKey.GetStoreHash ("20260101");
	assert (storeHash.IsZero ());

	// Per-client authentication is supported and must survive re-encoding.
	const std::string auth = Reencode (b33, [] (uint8_t * raw, size_t) { raw[0] |= 0x04; });
	BlindedPublicKey authKey (auth);
	assert (authKey.IsValid ());
	assert (authKey.ToB33 () == auth);

	// Bits 3-7 are undefined by proposal 149.
	const std::string unknown = Reencode (b33, [] (uint8_t * raw, size_t) { raw[0] |= 0x08; });
	BlindedPublicKey unknownKey (unknown);
	assert (!unknownKey.IsValid ());

	// Base32 decoders drop unused trailing bits. A 57th character must not
	// alias the canonical 56-character address.
	BlindedPublicKey trailing (b33 + "a");
	assert (!trailing.IsValid ());
	assert (trailing.ToB33 ().empty ());
	BlindedPublicKey trailingBits (b33 + "b");
	assert (!trailingBits.IsValid ());
}

void TestTwoByteSigTypes ()
{
	using namespace i2p::data;

	auto keys = PrivateKeys::CreateRandomKeys (SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519);
	BlindedPublicKey original (keys.GetPublic ());
	assert (original.IsValid ());

	std::array<uint8_t, 37> raw { };
	raw[0] = 0x01; // two-byte sigtypes
	htobe16buf (raw.data () + 1, SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519);
	htobe16buf (raw.data () + 3, SIGNING_KEY_TYPE_REDDSA_SHA512_ED25519);
	memcpy (raw.data () + 5, original.GetPublicKey (), 32);
	uint32_t checksum = Crc32 (raw.data () + 3, raw.size () - 3);
	raw[0] ^= (uint8_t)checksum;
	raw[1] ^= (uint8_t)(checksum >> 8);
	raw[2] ^= (uint8_t)(checksum >> 16);

	const std::string b33 = i2p::data::ByteStreamToBase32 (raw.data (), raw.size ());
	assert (b33.size () == 60);
	BlindedPublicKey twoByte (b33);
	assert (twoByte.IsValid ());
	assert (twoByte.GetSigType () == SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519);
	assert (twoByte.GetBlindedSigType () == SIGNING_KEY_TYPE_REDDSA_SHA512_ED25519);
	assert (twoByte.GetPublicKeyLen () == 32);
	assert (!memcmp (twoByte.GetPublicKey (), original.GetPublicKey (), 32));
	// The canonical encoder emits the one-byte form.
	assert (twoByte.ToB33 () == original.ToB33 ());
}
} // namespace

int main ()
{
	TestFlags ();
	TestTwoByteSigTypes ();
	return 0;
}
