/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: fuzz signature der.
 *
 * Mutates a valid P-521 signature and verifies it to exercise ECDSA DER conversion at and beyond the historical keyLen + 8 buffer bound, using a fixed generator identity instead of key generation.
 *
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <openssl/bn.h>

#include "Crypto.h"
#include "Signature.h"

namespace
{
constexpr size_t KEY_LEN = i2p::crypto::ECDSAP521_KEY_LENGTH;

const uint8_t MESSAGE[37] = { 0x49, 0x32, 0x50, 0x20, 0x64, 0x65, 0x72, 0x20, 0x62, 0x6f, 0x75, 0x6e, 0x64, 0x73, 0x20,
	0x66, 0x75, 0x7a, 0x7a, 0x20, 0x76, 0x65, 0x63, 0x74, 0x6f, 0x72, 0x20, 0x62, 0x79, 0x74, 0x65, 0x73, 0x21, 0x21,
	0x21, 0x21, 0x21 };

void LoadP521Generator (uint8_t * publicKey)
{
	static const char * const GX = "00C6858E06B70404E9CD9E3ECB662395B4429C648139053FB521F828AF606B4D3DBAA14B5E77EFE759"
								   "28FE1DC127A2FFA8DE3348B3C1856A429BF97E7E31C2E5BD66";
	static const char * const GY = "011839296A789A3BC0045C8A5FB42C7D1BD998F54449579B446817AFBD17273E662C97EE72995EF426"
								   "40C550B9013FAD0761353C7086A272C24088BE94769FD16650";

	BIGNUM * x = nullptr;
	BIGNUM * y = nullptr;
	if (BN_hex2bn (&x, GX) > 0 && BN_hex2bn (&y, GY) > 0 && i2p::crypto::bn2buf (x, publicKey, KEY_LEN / 2)
		&& i2p::crypto::bn2buf (y, publicKey + KEY_LEN / 2, KEY_LEN / 2))
	{
		BN_free (x);
		BN_free (y);
		return;
	}
	BN_free (x);
	BN_free (y);
	std::memset (publicKey, 0, KEY_LEN);
}
}

extern "C" int LLVMFuzzerTestOneInput (const uint8_t * data, size_t size)
{
	static uint8_t publicKey[KEY_LEN] = { };
	static const bool keyReady = [] ()
	{
		LoadP521Generator (publicKey);
		return true;
	}();
	(void)keyReady;

	static i2p::crypto::ECDSAP521Verifier verifier;
	static const bool verifierReady = [] ()
	{
		verifier.SetPublicKey (publicKey);
		return true;
	}();
	(void)verifierReady;

	static uint8_t validSignature[KEY_LEN] = { };
	static const bool signatureReady = [] ()
	{
		uint8_t privateKey[KEY_LEN / 2] = { };
		privateKey[sizeof (privateKey) - 1] = 1; // public key is the generator
		i2p::crypto::ECDSAP521Signer signer (privateKey);
		signer.Sign (MESSAGE, sizeof (MESSAGE), validSignature);
		return true;
	}();
	(void)signatureReady;

	uint8_t signature[KEY_LEN];
	std::memcpy (signature, validSignature, sizeof (signature));
	// Mutate the known-valid signature. Include every input byte so a
	// zero-length input still flips at least one bit.
	for (size_t i = 0; i < size; ++i)
	{
		const size_t pos = (static_cast<size_t> (data[i]) + 31 * i) % KEY_LEN;
		signature[pos] ^= static_cast<uint8_t> (data[i] | 0x01);
	}
	if (!size) signature[0] ^= 0x01;

	(void)verifier.Verify (MESSAGE, sizeof (MESSAGE), signature);
	return 0;
}
