/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test blinding b33 hardening.
 *
 * Checks proposal 149 b33 flag handling: secret-required, two-byte
 * signature-type and undefined flags are rejected, per-client
 * authentication survives re-encoding, and off-curve or identity key
 * encodings are handled fail-closed.
 *
 */

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>

#include "Base.h"
#include "Ed25519.h"
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
	// Java 2.14.0 rejects the two-byte signature-type form, so the same
	// destination must not gain a second encoding (and store key) here.
	BlindedPublicKey twoByte (b33);
	assert (!twoByte.IsValid ());
	assert (twoByte.ToB33 ().empty ());
	assert (twoByte.GetStoreHash ("20260101").IsZero ());
}

void TestOffCurveAndIdentityKeys ()
{
	using namespace i2p::data;

	auto keys = PrivateKeys::CreateRandomKeys (SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519);
	BlindedPublicKey original (keys.GetPublic ());
	assert (original.IsValid ());
	const std::string b33 = original.ToB33 ();
	assert (!b33.empty ());

	// The all-zero encoding is the order-4 identity point: Java accepts it
	// as a valid group element, so blinding must not treat it as
	// "invalid".
	uint8_t allZero[32] { };
	assert (i2p::crypto::GetEd25519 ()->IsValidPublicKey (allZero));
	const std::string identity = Reencode (b33, [] (uint8_t * raw, size_t) { memset (raw + 3, 0, 32); });
	BlindedPublicKey identityKey (identity);
	assert (identityKey.IsValid ());
	assert (identityKey.GetPublicKeyLen () == 32);
	assert (!memcmp (identityKey.GetPublicKey (), allZero, 32));

	// An off-curve y encoding must be rejected instead of becoming a store
	// key that no peer can derive.
	uint8_t offCurve[32] { };
	for (unsigned i = 0; i < 256; ++i)
	{
		offCurve[0] = static_cast<uint8_t> (i);
		if (!i2p::crypto::GetEd25519 ()->IsValidPublicKey (offCurve)) break;
	}
	assert (!i2p::crypto::GetEd25519 ()->IsValidPublicKey (offCurve));
	const std::string offCurveAddress
		= Reencode (b33, [&offCurve] (uint8_t * raw, size_t) { memcpy (raw + 3, offCurve, sizeof (offCurve)); });
	BlindedPublicKey offCurveKey (offCurveAddress);
	assert (!offCurveKey.IsValid ());
	assert (offCurveKey.ToB33 ().empty ());
	assert (offCurveKey.GetStoreHash ("20260101").IsZero ());

	// Blinding an off-curve key reports failure and publishes nothing.
	uint8_t seed[64] { }, blinded[32];
	memset (blinded, 0xA5, sizeof (blinded));
	assert (!i2p::crypto::GetEd25519 ()->BlindPublicKey (offCurve, seed, blinded));
	for (auto byte : blinded) assert (byte == 0);
}
} // namespace

int main ()
{
	TestFlags ();
	TestTwoByteSigTypes ();
	TestOffCurveAndIdentityKeys ();
	return 0;
}
