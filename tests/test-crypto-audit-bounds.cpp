/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test crypto audit bounds.
 *
 * Regression checks the audited bounds of bignum-to-buffer conversion,
 * Base32/Base64 decoding, X25519 and ElGamal key validation, AEAD
 * lengths and AES output sizing, all in-process.
 *
 */

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <memory>

#include <openssl/obj_mac.h>

#include "Base.h"
#include "Crypto.h"
#include "Gost.h"

namespace
{
void TestBn2BufBounds ()
{
	BIGNUM * bn = BN_new ();
	assert (bn);
	assert (BN_set_word (bn, 0x0102) == 1);

	uint8_t tooSmall[1] = { 0xAA };
	assert (!i2p::crypto::bn2buf (bn, tooSmall, sizeof (tooSmall)));
	assert (tooSmall[0] == 0xAA); // rejected before any write

	uint8_t exact[2] = { };
	assert (i2p::crypto::bn2buf (bn, exact, sizeof (exact)));
	assert (exact[0] == 0x01 && exact[1] == 0x02);

	uint8_t padded[4] = { 0xAA, 0xAA, 0xAA, 0xAA };
	assert (i2p::crypto::bn2buf (bn, padded, sizeof (padded)));
	assert (padded[0] == 0 && padded[1] == 0 && padded[2] == 0x01 && padded[3] == 0x02);

	assert (!i2p::crypto::bn2buf (nullptr, padded, sizeof (padded)));
	BN_free (bn);
}

void TestBaseDecoders ()
{
	using namespace i2p::data;

	uint8_t out[8] = { };
	assert (Base64ToByteStream ("dGVzdA==", out, sizeof (out)) == 4);
	assert (memcmp (out, "test", 4) == 0);
	assert (Base64ToByteStream ("dGVzdA==", out, 3) == 0); // no partial write

	// 4 base32 characters carry 20 bits, i.e. two full bytes.
	assert (Base32ToByteStream ("aaaa", out, sizeof (out)) == 2);
	assert (Base32ToByteStream ("aaa", out, 1) == 1);
	assert (Base32ToByteStream ("a!", out, sizeof (out)) == 0);

	uint8_t raw[7] = { 0x00, 0x01, 0x02, 0x03, 0xFE, 0xFF, 0x7A };
	const auto encoded = ByteStreamToBase32 (raw, sizeof (raw));
	uint8_t decoded[7] = { };
	assert (Base32ToByteStream (encoded, decoded, sizeof (decoded)) == sizeof (decoded));
	assert (memcmp (raw, decoded, sizeof (raw)) == 0);
}

void TestX25519RejectsInvalidPeer ()
{
	uint8_t priv[32];
	memset (priv, 0x42, sizeof (priv));
	i2p::crypto::X25519Keys keys (priv, nullptr);
	bool publicKeyIsZero = true;
	for (size_t i = 0; i < 32; ++i)
		if (keys.GetPublicKey ()[i])
		{
			publicKeyIsZero = false;
			break;
		}
	// SetPrivateKey/constructor must derive the public key instead of
	// leaving the buffer uninitialised.
	assert (!publicKeyIsZero);

	uint8_t shared[32] = { };
	uint8_t zero[32] = { };
	assert (!keys.Agree (zero, shared)); // low-order point must fail, not return true

	uint8_t highBit[32] = { };
	highBit[31] = 0x80;
	assert (!keys.Agree (highBit, shared));

	uint8_t self[32] = { };
	assert (keys.Agree (keys.GetPublicKey (), self));
}

void TestElGamalRejectsTrivialPublicKeys ()
{
	uint8_t data[222];
	memset (data, 0x5A, sizeof (data));
	uint8_t encrypted[514];

	// y = 0
	uint8_t publicKey[256] = { };
	memset (encrypted, 0xA5, sizeof (encrypted));
	i2p::crypto::ElGamalEncrypt (publicKey, data, encrypted);
	for (size_t i = 0; i < sizeof (encrypted); ++i) assert (encrypted[i] == 0);

	// y = 1 would make b = m
	publicKey[255] = 1;
	memset (encrypted, 0xA5, sizeof (encrypted));
	i2p::crypto::ElGamalEncrypt (publicKey, data, encrypted);
	for (size_t i = 0; i < sizeof (encrypted); ++i) assert (encrypted[i] == 0);

	uint8_t privateKey[256] = { };
	privateKey[255] = 7;
	uint8_t decrypted[222];
	auto rejectWithoutPlaintext = [&] (const uint8_t * candidateKey, const uint8_t * candidateCiphertext)
	{
		memset (decrypted, 0xA5, sizeof (decrypted));
		assert (!i2p::crypto::ElGamalDecrypt (candidateKey, candidateCiphertext, decrypted));
		for (auto byte : decrypted) assert (byte == 0);
	};
	rejectWithoutPlaintext (privateKey, encrypted);
	rejectWithoutPlaintext (nullptr, encrypted);
	rejectWithoutPlaintext (privateKey, nullptr);
	uint8_t zeroPrivate[256] = { };
	rejectWithoutPlaintext (zeroPrivate, encrypted);
	// All-one limbs exceed the group modulus and must fail before use.
	uint8_t oversizedPrivate[256];
	memset (oversizedPrivate, 0xFF, sizeof (oversizedPrivate));
	rejectWithoutPlaintext (oversizedPrivate, encrypted);
	// A zero second ciphertext component cannot pass integrity checking.
	encrypted[256] = 1;
	rejectWithoutPlaintext (privateKey, encrypted);

	// A generated key pair must still round-trip; the range checks above
	// must not reject legitimate ElGamal keys.
	uint8_t generatedPrivate[256] = { };
	uint8_t generatedPublic[256] = { };
	i2p::crypto::GenerateElGamalKeyPair (generatedPrivate, generatedPublic);
	assert (i2p::crypto::ElGamalEncrypt (generatedPublic, data, encrypted));
	assert (i2p::crypto::ElGamalDecrypt (generatedPrivate, encrypted, decrypted));
	assert (memcmp (decrypted, data, sizeof (data)) == 0);
	// Inputs are fully consumed before plaintext publication, including
	// when the caller deliberately reuses an input buffer as output.
	uint8_t overlappingCiphertext[514];
	memcpy (overlappingCiphertext, encrypted, sizeof (encrypted));
	assert (i2p::crypto::ElGamalDecrypt (generatedPrivate, overlappingCiphertext, overlappingCiphertext));
	assert (memcmp (overlappingCiphertext, data, sizeof (data)) == 0);
	uint8_t overlappingPrivate[256];
	memcpy (overlappingPrivate, generatedPrivate, sizeof (generatedPrivate));
	assert (i2p::crypto::ElGamalDecrypt (overlappingPrivate, encrypted, overlappingPrivate));
	assert (memcmp (overlappingPrivate, data, sizeof (data)) == 0);
}

void TestECIESIntegrityPublication ()
{
	std::unique_ptr<EC_GROUP, decltype (&EC_GROUP_free)> curve (
		EC_GROUP_new_by_curve_name (NID_X9_62_prime256v1), EC_GROUP_free);
	std::unique_ptr<BIGNUM, decltype (&BN_clear_free)> key (BN_new (), BN_clear_free);
	assert (curve && key && BN_set_word (key.get (), 7) == 1);
	std::unique_ptr<EC_POINT, decltype (&EC_POINT_free)> publicKey (EC_POINT_new (curve.get ()), EC_POINT_free);
	assert (publicKey && EC_POINT_mul (curve.get (), publicKey.get (), key.get (), nullptr, nullptr, nullptr) == 1);
	std::array<uint8_t, 222> plaintext;
	plaintext.fill (0x5A);
	std::array<uint8_t, 514> envelope;
	assert (i2p::crypto::ECIESEncrypt (curve.get (), publicKey.get (), plaintext.data (), envelope.data ()));
	std::array<uint8_t, 222> output;
	assert (i2p::crypto::ECIESDecrypt (curve.get (), key.get (), envelope.data (), output.data ()));
	assert (output == plaintext);
	// Publication occurs only after all envelope reads and integrity checks.
	auto overlapping = envelope;
	assert (i2p::crypto::ECIESDecrypt (curve.get (), key.get (), overlapping.data (), overlapping.data ()));
	assert (memcmp (overlapping.data (), plaintext.data (), plaintext.size ()) == 0);
	// CBC changes to the final plaintext block leave its stored digest
	// unchanged. Failed integrity must leave the caller output untouched.
	envelope[258 + 224] ^= 1;
	output.fill (0xA5);
	assert (!i2p::crypto::ECIESDecrypt (curve.get (), key.get (), envelope.data (), output.data ()));
	for (auto byte : output) assert (byte == 0xA5);
	assert (EC_POINT_set_to_infinity (curve.get (), publicKey.get ()) == 1);
	envelope.fill (0xA5);
	assert (!i2p::crypto::ECIESEncrypt (curve.get (), publicKey.get (), plaintext.data (), envelope.data ()));
	for (auto byte : envelope) assert (byte == 0);
}

void TestAEADBounds ()
{
	uint8_t key[32] = { };
	uint8_t nonce[12] = { };
	constexpr size_t msgLen = 16;
	uint8_t msg[msgLen + 16] = { };
	uint8_t out[64] = { };

	// An encrypt buffer shorter than msgLen + tag is rejected.
	assert (!i2p::crypto::AEADChaCha20Poly1305 (msg, msgLen, nullptr, 0, key, nonce, out, msgLen + 15, true));
	assert (!i2p::crypto::AEADChaCha20Poly1305 (msg, msgLen + 1, nullptr, 0, key, nonce, out, msgLen, true));
	// A decrypt destination shorter than msgLen is rejected.
	assert (!i2p::crypto::AEADChaCha20Poly1305 (msg, msgLen + 16, nullptr, 0, key, nonce, out, msgLen - 1, false));

	// The tag is read from msg + msgLen, so decryption needs a message
	// pointer even when the plaintext is empty.
	assert (!i2p::crypto::AEADChaCha20Poly1305 (nullptr, 0, nullptr, 0, key, nonce, out, 0, false));

	// The exact size still works.
	assert (i2p::crypto::AEADChaCha20Poly1305 (msg, msgLen, nullptr, 0, key, nonce, out, msgLen + 16, true));
}

void TestAESRejectsInvalidLengths ()
{
	uint8_t key[32] = { 0x11 };
	uint8_t iv[16] = { 0x22 };
	uint8_t in[32] = { 0x33 };
	uint8_t out[32] = { };

	i2p::crypto::CBCEncryption encryption;
	encryption.SetKey (key);
	// A misaligned length is rejected without touching the output: the
	// helper cannot safely infer the caller's buffer capacity from a
	// length that is not a valid AES block multiple.
	std::memset (out, 0xA5, sizeof (out));
	assert (!encryption.EncryptChecked (in, 15, iv, out));
	for (auto byte : out) assert (byte == 0xA5);

	// An unrepresentable length must be rejected before any output wipe
	// and before it is converted to an EVP int.
	std::memset (out, 0xA5, sizeof (out));
	assert (!encryption.EncryptChecked (in, static_cast<size_t> (std::numeric_limits<int>::max ()) + 1, iv, out));
	for (auto byte : out) assert (byte == 0xA5);

	std::memset (out, 0, sizeof (out));
	assert (encryption.EncryptChecked (in, 16, iv, out));

	i2p::crypto::CBCDecryption decryption;
	decryption.SetKey (key);
	std::memset (out, 0xA5, sizeof (out));
	assert (!decryption.DecryptChecked (in, 15, iv, out));
	for (auto byte : out) assert (byte == 0xA5);

	i2p::crypto::ECBEncryption ecbEncryption;
	ecbEncryption.SetKey (key);
	std::memset (out, 0xA5, sizeof (out));
	assert (!ecbEncryption.EncryptChecked (nullptr, out));
	for (size_t i = 0; i < 16; ++i) assert (out[i] == 0);

	i2p::crypto::ECBDecryption ecbDecryption;
	ecbDecryption.SetKey (key);
	std::memset (out, 0xA5, sizeof (out));
	assert (!ecbDecryption.DecryptChecked (nullptr, out));
	for (size_t i = 0; i < 16; ++i) assert (out[i] == 0);
}

void TestGostSigningRejectsInvalidInputs ()
{
	using namespace i2p::crypto;
	using SecretBN = std::unique_ptr<BIGNUM, decltype (&BN_clear_free)>;
	SecretBN priv (BN_new (), BN_clear_free);
	SecretBN digest (BN_new (), BN_clear_free);
	SecretBN r (BN_new (), BN_clear_free), s (BN_new (), BN_clear_free);
	SecretBN order (BN_new (), BN_clear_free);
	assert (priv && digest && r && s && order);
	std::unique_ptr<BN_CTX, decltype (&BN_CTX_free)> ctx (BN_CTX_new (), BN_CTX_free);
	assert (ctx);
	auto seed = [&]
	{
		assert (BN_one (priv.get ()) == 1);
		BN_zero (digest.get ());
		assert (BN_set_word (r.get (), 17) == 1);
		assert (BN_set_word (s.get (), 29) == 1);
	};
	auto rejected = [&] { assert (BN_is_zero (r.get ()) && BN_is_zero (s.get ())); };
	for (auto parameters : std::array { eGOSTR3410CryptoProA, eGOSTR3410TC26A512 })
	{
		auto & curve = GetGOSTR3410Curve (parameters);
		assert (curve && curve->GetGroup ());
		assert (EC_GROUP_get_order (curve->GetGroup (), order.get (), ctx.get ()) == 1);
		seed ();
		curve->Sign (nullptr, digest.get (), r.get (), s.get ());
		rejected ();
		seed ();
		curve->Sign (priv.get (), nullptr, r.get (), s.get ());
		rejected ();
		seed ();
		BN_zero (priv.get ());
		curve->Sign (priv.get (), digest.get (), r.get (), s.get ());
		rejected ();
		seed ();
		BN_set_negative (priv.get (), 1);
		curve->Sign (priv.get (), digest.get (), r.get (), s.get ());
		rejected ();
		seed ();
		assert (BN_copy (priv.get (), order.get ()));
		curve->Sign (priv.get (), digest.get (), r.get (), s.get ());
		rejected ();
		seed ();
		assert (BN_one (digest.get ()) == 1);
		BN_set_negative (digest.get (), 1);
		curve->Sign (priv.get (), digest.get (), r.get (), s.get ());
		rejected ();
		seed ();
		curve->Sign (priv.get (), digest.get (), nullptr, s.get ());
		assert (BN_is_zero (s.get ()));
		seed ();
		curve->Sign (priv.get (), digest.get (), r.get (), nullptr);
		assert (BN_is_zero (r.get ()));
		seed ();
		curve->Sign (priv.get (), digest.get (), r.get (), r.get ());
		assert (BN_is_zero (r.get ()));
		// Aliased borrowed inputs stay intact; other outputs still clear.
		seed ();
		curve->Sign (priv.get (), digest.get (), priv.get (), s.get ());
		assert (BN_is_one (priv.get ()) && BN_is_zero (s.get ()));
		seed ();
		assert (BN_set_word (digest.get (), 41) == 1);
		curve->Sign (priv.get (), digest.get (), r.get (), digest.get ());
		assert (BN_is_zero (r.get ()) && BN_is_word (digest.get (), 41));
		// RFC 7091 normalizes a zero digest to one; valid signing survives.
		seed ();
		curve->Sign (priv.get (), digest.get (), r.get (), s.get ());
		assert (!BN_is_zero (r.get ()) && !BN_is_zero (s.get ()));
		std::unique_ptr<EC_POINT, decltype (&EC_POINT_free)> publicKey (curve->MulP (priv.get ()), EC_POINT_free);
		assert (publicKey && curve->Verify (publicKey.get (), digest.get (), r.get (), s.get ()));
	}
	seed ();
	GOSTR3410Curve invalid (nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
	invalid.Sign (priv.get (), digest.get (), r.get (), s.get ());
	rejected ();
}

void TestGostVerificationAndRecoveryBounds ()
{
	using namespace i2p::crypto;
	using BN = std::unique_ptr<BIGNUM, decltype (&BN_clear_free)>;
	BN privateKey (BN_new (), BN_clear_free), digest (BN_new (), BN_clear_free);
	BN r (BN_new (), BN_clear_free), s (BN_new (), BN_clear_free);
	BN order (BN_new (), BN_clear_free);
	std::unique_ptr<BN_CTX, decltype (&BN_CTX_free)> ctx (BN_CTX_new (), BN_CTX_free);
	assert (privateKey && digest && r && s && order && ctx);
	assert (BN_one (privateKey.get ()) == 1);
	for (auto parameters : std::array { eGOSTR3410CryptoProA, eGOSTR3410TC26A512 })
	{
		auto & curve = GetGOSTR3410Curve (parameters);
		assert (curve && curve->GetGroup ());
		std::unique_ptr<EC_POINT, decltype (&EC_POINT_free)> publicKey (curve->MulP (privateKey.get ()), EC_POINT_free);
		assert (publicKey);
		assert (EC_GROUP_get_order (curve->GetGroup (), order.get (), ctx.get ()) == 1);
		auto validSignature = [&]
		{
			// Fixed test scalar and nonce one: r = Px, s = r + 1.
			// Both groups have Px < q, so recovery needs no x = r + q.
			BN_zero (digest.get ());
			assert (curve->GetXY (publicKey.get (), r.get (), nullptr));
			assert (!BN_is_zero (r.get ()) && !BN_is_negative (r.get ()) && BN_cmp (r.get (), order.get ()) < 0);
			assert (BN_mod_add (s.get (), r.get (), BN_value_one (), order.get (), ctx.get ()) == 1);
			assert (!BN_is_zero (s.get ()) && !BN_is_negative (s.get ()));
		};
		validSignature ();
		assert (curve->Verify (publicKey.get (), digest.get (), r.get (), s.get ()));
		bool matchedIdentity = false;
		for (bool parity : { false, true })
		{
			std::unique_ptr<EC_POINT, decltype (&EC_POINT_free)> candidate (
				curve->RecoverPublicKey (digest.get (), r.get (), s.get (), parity), EC_POINT_free);
			if (candidate && EC_POINT_cmp (curve->GetGroup (), candidate.get (), publicKey.get (), ctx.get ()) == 0)
				matchedIdentity = true;
		}
		assert (matchedIdentity);
		assert (!curve->Verify (nullptr, digest.get (), r.get (), s.get ()));
		assert (!curve->Verify (publicKey.get (), nullptr, r.get (), s.get ()));
		assert (!curve->Verify (publicKey.get (), digest.get (), nullptr, s.get ()));
		assert (!curve->Verify (publicKey.get (), digest.get (), r.get (), nullptr));
		assert (!curve->RecoverPublicKey (nullptr, r.get (), s.get ()));
		assert (!curve->RecoverPublicKey (digest.get (), nullptr, s.get ()));
		assert (!curve->RecoverPublicKey (digest.get (), r.get (), nullptr));
		for (auto * scalar : { r.get (), s.get () })
		{
			validSignature ();
			BN_set_negative (scalar, 1);
			assert (!curve->Verify (publicKey.get (), digest.get (), r.get (), s.get ()));
			assert (!curve->RecoverPublicKey (digest.get (), r.get (), s.get ()));
			validSignature ();
			BN_zero (scalar);
			assert (!curve->Verify (publicKey.get (), digest.get (), r.get (), s.get ()));
			assert (!curve->RecoverPublicKey (digest.get (), r.get (), s.get ()));
			validSignature ();
			assert (BN_copy (scalar, order.get ()));
			assert (!curve->Verify (publicKey.get (), digest.get (), r.get (), s.get ()));
			assert (!curve->RecoverPublicKey (digest.get (), r.get (), s.get ()));
		}
		validSignature ();
		assert (BN_one (digest.get ()) == 1);
		BN_set_negative (digest.get (), 1);
		assert (!curve->Verify (publicKey.get (), digest.get (), r.get (), s.get ()));
		assert (!curve->RecoverPublicKey (digest.get (), r.get (), s.get ()));
		validSignature ();
		std::unique_ptr<EC_POINT, decltype (&EC_POINT_free)> infinity (
			EC_POINT_new (curve->GetGroup ()), EC_POINT_free);
		assert (infinity && EC_POINT_is_at_infinity (curve->GetGroup (), infinity.get ()) == 1);
		assert (!curve->Verify (infinity.get (), digest.get (), r.get (), s.get ()));
	}
}
}

int main ()
{
	TestBn2BufBounds ();
	TestBaseDecoders ();
	TestX25519RejectsInvalidPeer ();
	TestElGamalRejectsTrivialPublicKeys ();
	TestECIESIntegrityPublication ();
	TestAEADBounds ();
	TestAESRejectsInvalidLengths ();
	TestGostSigningRejectsInvalidInputs ();
	TestGostVerificationAndRecoveryBounds ();
	return 0;
}
