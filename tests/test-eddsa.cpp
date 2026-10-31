/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test eddsa.
 *
 * Reproduces RFC 8032 test vector 1024 to check deterministic Ed25519
 * signing and verification for a 1023-byte message, then pins the
 * hand-rolled verifier's canonical-S (S < L) rejection and the
 * fail-closed point decoding used by encrypted LeaseSet2 blinding.
 *
 */

#include <array>
#include <cassert>
#include <inttypes.h>
#include <string.h>

#include <openssl/evp.h>

#include "Signature.h"
#include "Ed25519.h"

namespace
{
// value = le32(v) interpreted as a little-endian scalar, increased by
// L.
void AddOrder (uint8_t * littleEndian)
{
	// L = 2^252 + 27742317777372353535851937790883648493.
	static const uint8_t order[32]
		= { 0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14, 0x00, 0x00,
			  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10 };
	unsigned carry = 0;
	for (size_t i = 0; i < 32; ++i)
	{
		const unsigned sum = (unsigned)littleEndian[i] + (unsigned)order[i] + carry;
		littleEndian[i] = (uint8_t)sum;
		carry = sum >> 8;
	}
}

void Sha512 (const std::array<uint8_t, 32> & r, const std::array<uint8_t, 32> & pub, const uint8_t * msg, size_t len,
	uint8_t * digest)
{
	EVP_MD_CTX * ctx = EVP_MD_CTX_new ();
	assert (ctx);
	assert (EVP_DigestInit_ex (ctx, EVP_sha512 (), nullptr) == 1);
	assert (EVP_DigestUpdate (ctx, r.data (), r.size ()) == 1);
	assert (EVP_DigestUpdate (ctx, pub.data (), pub.size ()) == 1);
	assert (EVP_DigestUpdate (ctx, msg, len) == 1);
	unsigned int dl = 64;
	assert (EVP_DigestFinal_ex (ctx, digest, &dl) == 1);
	assert (dl == 64);
	EVP_MD_CTX_free (ctx);
}
} // namespace

// TEST 1024 from RFC-8032

int main ()
{
	uint8_t key[32], pub[32], msg[1024], sig[64];
	BIGNUM * input = BN_new ();
	BN_hex2bn (&input, "f5e5767cf153319517630f226876b86c8160cc583bc013744c6bf255f5cc0ee5");
	BN_bn2bin (input, key);
	BN_hex2bn (&input,
		"08b8b2b733424243760fe426a4b54908632110a66c2f6591eabd3345e3e4eb98"
		"fa6e264bf09efe12ee50f8f54e9f77b1e355f6c50544e23fb1433ddf73be84d8"
		"79de7c0046dc4996d9e773f4bc9efe5738829adb26c81b37c93a1b270b20329d"
		"658675fc6ea534e0810a4432826bf58c941efb65d57a338bbd2e26640f89ffbc"
		"1a858efcb8550ee3a5e1998bd177e93a7363c344fe6b199ee5d02e82d522c4fe"
		"ba15452f80288a821a579116ec6dad2b3b310da903401aa62100ab5d1a36553e"
		"06203b33890cc9b832f79ef80560ccb9a39ce767967ed628c6ad573cb116dbef"
		"efd75499da96bd68a8a97b928a8bbc103b6621fcde2beca1231d206be6cd9ec7"
		"aff6f6c94fcd7204ed3455c68c83f4a41da4af2b74ef5c53f1d8ac70bdcb7ed1"
		"85ce81bd84359d44254d95629e9855a94a7c1958d1f8ada5d0532ed8a5aa3fb2"
		"d17ba70eb6248e594e1a2297acbbb39d502f1a8c6eb6f1ce22b3de1a1f40cc24"
		"554119a831a9aad6079cad88425de6bde1a9187ebb6092cf67bf2b13fd65f270"
		"88d78b7e883c8759d2c4f5c65adb7553878ad575f9fad878e80a0c9ba63bcbcc"
		"2732e69485bbc9c90bfbd62481d9089beccf80cfe2df16a2cf65bd92dd597b07"
		"07e0917af48bbb75fed413d238f5555a7a569d80c3414a8d0859dc65a46128ba"
		"b27af87a71314f318c782b23ebfe808b82b0ce26401d2e22f04d83d1255dc51a"
		"ddd3b75a2b1ae0784504df543af8969be3ea7082ff7fc9888c144da2af58429e"
		"c96031dbcad3dad9af0dcbaaaf268cb8fcffead94f3c7ca495e056a9b47acdb7"
		"51fb73e666c6c655ade8297297d07ad1ba5e43f1bca32301651339e22904cc8c"
		"42f58c30c04aafdb038dda0847dd988dcda6f3bfd15c4b4c4525004aa06eeff8"
		"ca61783aacec57fb3d1f92b0fe2fd1a85f6724517b65e614ad6808d6f6ee34df"
		"f7310fdc82aebfd904b01e1dc54b2927094b2db68d6f903b68401adebf5a7e08"
		"d78ff4ef5d63653a65040cf9bfd4aca7984a74d37145986780fc0b16ac451649"
		"de6188a7dbdf191f64b5fc5e2ab47b57f7f7276cd419c17a3ca8e1b939ae49e4"
		"88acba6b965610b5480109c8b17b80e1b7b750dfc7598d5d5011fd2dcc5600a3"
		"2ef5b52a1ecc820e308aa342721aac0943bf6686b64b2579376504ccc493d97e"
		"6aed3fb0f9cd71a43dd497f01f17c0e2cb3797aa2a2f256656168e6c496afc5f"
		"b93246f6b1116398a346f1a641f3b041e989f7914f90cc2c7fff357876e506b5"
		"0d334ba77c225bc307ba537152f3f1610e4eafe595f6d9d90d11faa933a15ef1"
		"369546868a7f3a45a96768d40fd9d03412c091c6315cf4fde7cb68606937380d"
		"b2eaaa707b4c4185c32eddcdd306705e4dc1ffc872eeee475a64dfac86aba41c"
		"0618983f8741c5ef68d3a101e8a3b8cac60c905c15fc910840b94c00a0b9d0");
	BN_bn2bin (input, msg);
	BN_hex2bn (&input,
		"0aab4c900501b3e24d7cdf4663326a3a87df5e4843b2cbdb67cbf6e460fec350"
		"aa5371b1508f9f4528ecea23c436d94b5e8fcd4f681e30a6ac00a9704a188a03");
	BN_bn2bin (input, sig);
	BN_hex2bn (&input, "278117fc144c72340f67d0f2316e8386ceffbf2b2428c9c51fef7c597f1d426e");
	BN_bn2bin (input, pub);

	uint8_t s[64];
	i2p::crypto::EDDSA25519Signer signer (key);
	signer.Sign (msg, 1023, s);
	assert (memcmp (s, sig, 64) == 0);

	i2p::crypto::EDDSA25519Verifier verifier;
	verifier.SetPublicKey (pub);
	assert (verifier.Verify (msg, 1023, s));

	// Blinding must reject a y-encoding whose recovered x is not on the
	// curve.
	auto & ed25519 = i2p::crypto::GetEd25519 ();
	assert (ed25519->IsValidPublicKey (pub));
	uint8_t invalid[32] { };
	bool foundInvalid = false;
	for (unsigned i = 0; i < 256 && !foundInvalid; ++i)
	{
		invalid[0] = static_cast<uint8_t> (i);
		foundInvalid = !ed25519->IsValidPublicKey (invalid);
	}
	assert (foundInvalid);
	// Java also accepts the order-4 all-zero encoding; this check must be
	// an on-curve test, not an accidental low-order rejection.
	uint8_t allZero[32] { };
	assert (ed25519->IsValidPublicKey (allZero));

	// Point decoding fails closed: an off-curve encoding decodes to an
	// empty point and every consumer reports failure instead of hashing
	// through it.
	{
		BN_CTX * ctx = BN_CTX_new ();
		assert (ctx);
		// The last candidate from the loop above is off-curve.
		const auto bad = ed25519->DecodePublicKey (invalid, ctx);
		assert (!bad.x && !bad.y);
		uint8_t blinded[32];
		uint8_t seed[64] { };
		memset (blinded, 0xA5, sizeof (blinded));
		assert (!ed25519->BlindPublicKey (invalid, seed, blinded));
		for (auto byte : blinded) assert (byte == 0);
		// A null input point is rejected before any provider work.
		memset (blinded, 0xA5, sizeof (blinded));
		assert (!ed25519->BlindPublicKey (nullptr, seed, blinded));
		for (auto byte : blinded) assert (byte == 0);
		BN_CTX_free (ctx);
	}

	// The hand-rolled verifier must apply RFC 8032's canonical-S rule and
	// use a constant-time comparison. S + L would otherwise verify because
	// the base-point multiplication reduces the scalar modulo L.
	{
		std::array<uint8_t, 32> r { }, pubKey { }, digest { };
		memcpy (r.data (), s, r.size ());
		memcpy (pubKey.data (), pub, pubKey.size ());
		Sha512 (r, pubKey, msg, 1023, digest.data ());

		BN_CTX * ctx = BN_CTX_new ();
		assert (ctx);
		const auto point = ed25519->DecodePublicKey (pub, ctx);
		assert (point.x && point.y);
		uint8_t canonical[64];
		memcpy (canonical, s, sizeof (canonical));
		assert (ed25519->Verify (point, digest.data (), canonical));

		uint8_t malleated[64];
		memcpy (malleated, s, sizeof (malleated));
		AddOrder (malleated + 32); // S' = S + L
		assert (memcmp (malleated + 32, s + 32, 32) != 0);
		assert (!ed25519->Verify (point, digest.data (), malleated));

		// A wrong R must be rejected as well.
		uint8_t wrongR[64];
		memcpy (wrongR, s, sizeof (wrongR));
		wrongR[0] ^= 0x01;
		assert (!ed25519->Verify (point, digest.data (), wrongR));
		BN_CTX_free (ctx);
	}

	BN_free (input);
}
