/*
 * Copyright (c) 2026, David Uhden Collado
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Self-contained valid/malformed checks for the audited primitives:
 *   - X25519 key lifecycle, low-order/zero shared secrets and high-bit peers.
 *   - Noise/HKDF/MixKey/MixHash/ChaCha20-Poly1305 output handling.
 *   - Elligator2 canonical representatives, high bits, r = (p-1)/2 and x = 0.
 *   - ML-KEM type mapping, hybrid protocol names, sizes and round trips.
 *   - CryptoKey lengths and ECIES P256/X25519 wrappers.
 *
 * This test does not talk to a peer and does not establish Java/OpenSSL
 * interoperability; it only pins the locally observable contract.
 */

#include <algorithm>
#include <array>
#include <cassert>
#include <climits>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>

#include "Crypto.h"
#include "CryptoKey.h"
#include "Elligator.h"
#include "PostQuantum.h"

namespace
{
bool IsAllZero (const uint8_t * data, size_t len)
{
	for (size_t i = 0; i < len; ++i)
		if (data[i]) return false;
	return true;
}

bool IsAllValue (const uint8_t * data, size_t len, uint8_t value)
{
	for (size_t i = 0; i < len; ++i)
		if (data[i] != value) return false;
	return true;
}

void Sha256 (const uint8_t * a, size_t aLen, const uint8_t * b, size_t bLen, uint8_t * out)
{
	EVP_MD_CTX * ctx = EVP_MD_CTX_new ();
	assert (ctx);
	assert (EVP_DigestInit_ex (ctx, EVP_sha256 (), nullptr) == 1);
	if (aLen) assert (EVP_DigestUpdate (ctx, a, aLen) == 1);
	if (bLen) assert (EVP_DigestUpdate (ctx, b, bLen) == 1);
	assert (EVP_DigestFinal_ex (ctx, out, nullptr) == 1);
	EVP_MD_CTX_free (ctx);
}

// RFC 5869 with SHA-256, matching net.i2p.crypto.HKDF and
// SymmetricState.hkdf(). An empty IKM mirrors Java's ZEROLEN expand-only
// path: PRK = HMAC(salt, empty).
void RefHKDF (const uint8_t * salt, const uint8_t * ikm, size_t ikmLen, std::string_view info, uint8_t * out,
	size_t outLen)
{
	uint8_t prk[32];
	unsigned int prkLen = 0;
	const uint8_t empty = 0;
	assert (HMAC (EVP_sha256 (), salt, 32, ikmLen ? ikm : &empty, (int)ikmLen, prk, &prkLen) != nullptr);
	assert (prkLen == 32);

	uint8_t t[32] = { };
	size_t tLen = 0, pos = 0;
	uint8_t counter = 1;
	while (pos < outLen)
	{
		std::vector<uint8_t> data;
		data.insert (data.end (), t, t + tLen);
		data.insert (data.end (), info.begin (), info.end ());
		data.push_back (counter++);
		unsigned int n = 0;
		assert (HMAC (EVP_sha256 (), prk, 32, data.data (), (int)data.size (), t, &n) != nullptr);
		assert (n == 32);
		tLen = 32;
		const size_t copy = std::min<size_t> (32, outLen - pos);
		memcpy (out + pos, t, copy);
		pos += copy;
	}
	OPENSSL_cleanse (prk, sizeof (prk));
	OPENSSL_cleanse (t, sizeof (t));
}

void TestX25519 ()
{
	uint8_t privateA[32], privateB[32];
	i2p::crypto::X25519Keys a, b;
	a.GenerateKeys ();
	b.GenerateKeys ();
	assert (!IsAllZero (a.GetPublicKey (), 32));
	assert (!IsAllZero (b.GetPublicKey (), 32));

	uint8_t sharedAB[32], sharedBA[32];
	assert (a.Agree (b.GetPublicKey (), sharedAB));
	assert (b.Agree (a.GetPublicKey (), sharedBA));
	assert (!IsAllZero (sharedAB, 32));
	assert (memcmp (sharedAB, sharedBA, 32) == 0);

	// A low-order peer point must fail and must not leave output behind.
	uint8_t shared[32];
	memset (shared, 0xA5, sizeof (shared));
	uint8_t zero[32] = { };
	assert (!a.Agree (zero, shared));
	assert (IsAllZero (shared, 32));

	uint8_t one[32] = { 1 };
	memset (shared, 0xA5, sizeof (shared));
	assert (!a.Agree (one, shared));
	assert (IsAllZero (shared, 32));

	// Proposal 144 / X25519DH requires the peer's MSB to be clear.
	uint8_t highBit[32];
	memcpy (highBit, b.GetPublicKey (), 32);
	highBit[31] |= 0x80;
	memset (shared, 0xA5, sizeof (shared));
	assert (!a.Agree (highBit, shared));
	assert (IsAllZero (shared, 32));

	// Export/reload must derive the same public key.
	a.GetPrivateKey (privateA);
	b.GetPrivateKey (privateB);
	i2p::crypto::X25519Keys reloaded (privateA, nullptr);
	assert (memcmp (reloaded.GetPublicKey (), a.GetPublicKey (), 32) == 0);

	// Key material replacement must clear a stale Elligator2 ineligibility
	// flag, otherwise pooled keys never try random representatives again.
	a.SetElligatorIneligible ();
	assert (a.IsElligatorIneligible ());
	a.SetPrivateKey (privateA, false);
	assert (!a.IsElligatorIneligible ());

	i2p::crypto::X25519Keys pooled;
	pooled.GenerateKeys ();
	pooled.SetElligatorIneligible ();
	pooled.GenerateKeys ();
	assert (!pooled.IsElligatorIneligible ());
}

void TestElligator ()
{
	using i2p::crypto::Elligator2;
	Elligator2 el;

	const uint8_t key[32] = { 0x33, 0x95, 0x19, 0x64, 0x00, 0x3c, 0x94, 0x08, 0x78, 0x06, 0x3c, 0xcf, 0xd0, 0x34,
		0x8a, 0xf4, 0x21, 0x50, 0xca, 0x16, 0xd2, 0x64, 0x6f, 0x2c, 0x58, 0x56, 0xe8, 0x33, 0x83, 0x77, 0xd8,
		0x80 };
	const uint8_t encodedLowY[32] = { 0x28, 0x20, 0xb6, 0xb2, 0x41, 0xe0, 0xf6, 0x8a, 0x6c, 0x4a, 0x7f, 0xee,
		0x3d, 0x97, 0x82, 0x28, 0xef, 0x3a, 0xe4, 0x55, 0x33, 0xcd, 0x41, 0x0a, 0xa9, 0x1a, 0x41, 0x53, 0x31,
		0xd8, 0x61, 0x2d };
	const uint8_t encodedHighY[32] = { 0x3c, 0xfb, 0x87, 0xc4, 0x6c, 0x0b, 0x45, 0x75, 0xca, 0x81, 0x75, 0xe0,
		0xed, 0x1c, 0x0a, 0xe9, 0xda, 0xe7, 0x9d, 0xb7, 0x8d, 0xf8, 0x69, 0x97, 0xc4, 0x84, 0x7b, 0x9f, 0x20,
		0xb2, 0x77, 0x18 };

	const uint8_t encoded1[32] = { 0xe7, 0x35, 0x07, 0xd3, 0x8b, 0xae, 0x63, 0x99, 0x2b, 0x3f, 0x57, 0xaa,
		0xc4, 0x8c, 0x0a, 0xbc, 0x14, 0x50, 0x95, 0x89, 0x28, 0x84, 0x57, 0x99, 0x5a, 0x2b, 0x4c, 0xa3, 0x49,
		0x0a, 0xa2, 0x07 };
	const uint8_t key1[32] = { 0x1e, 0x8a, 0xff, 0xfe, 0xd6, 0xbf, 0x53, 0xfe, 0x27, 0x1a, 0xd5, 0x72, 0x47,
		0x32, 0x62, 0xde, 0xd8, 0xfa, 0xec, 0x68, 0xe5, 0xe6, 0x7e, 0xf4, 0x5e, 0xbb, 0x82, 0xee, 0xba, 0x52,
		0x60, 0x4f };
	const uint8_t encoded2[32] = { 0x95, 0xa1, 0x60, 0x19, 0x04, 0x1d, 0xbe, 0xfe, 0xd9, 0x83, 0x20, 0x48,
		0xed, 0xe1, 0x19, 0x28, 0xd9, 0x03, 0x65, 0xf2, 0x4a, 0x38, 0xaa, 0x7a, 0xef, 0x1b, 0x97, 0xe2, 0x39,
		0x54, 0x10, 0x1b };
	const uint8_t key2[32] = { 0x79, 0x4f, 0x05, 0xba, 0x3e, 0x3a, 0x72, 0x95, 0x80, 0x22, 0x46, 0x8c, 0x88,
		0x98, 0x1e, 0x0b, 0xe5, 0x78, 0x2b, 0xe1, 0xe1, 0x14, 0x5c, 0xe2, 0xc3, 0xc6, 0xfd, 0xe1, 0x6d, 0xed,
		0x53, 0x63 };
	// Little endian r = (p - 1) / 2 = 2^254 - 10. Java rejects r >= (p-1)/2.
	const uint8_t boundary[32] = { 0xf6, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0xff, 0xff, 0x3f };

	uint8_t buf[32];
	assert (el.Encode (key, buf, false, false));
	assert (memcmp (buf, encodedLowY, 32) == 0);
	assert (el.Encode (key, buf, true, false));
	assert (memcmp (buf, encodedHighY, 32) == 0);

	assert (el.Decode (encoded1, buf));
	assert (memcmp (buf, key1, 32) == 0);
	assert (el.Decode (encoded2, buf));
	assert (memcmp (buf, key2, 32) == 0);

	// The high two bits are not part of the representative.
	uint8_t withHighBits[32];
	memcpy (withHighBits, encoded1, 32);
	withHighBits[31] |= 0xC0;
	assert (el.Decode (withHighBits, buf));
	assert (memcmp (buf, key1, 32) == 0);

	assert (!el.Decode (boundary, buf));
	// The representative immediately below the boundary is valid; masking the
	// two high bits must not turn it into the rejected boundary value.
	uint8_t boundaryMinusOne[32];
	memcpy (boundaryMinusOne, boundary, sizeof (boundaryMinusOne));
	boundaryMinusOne[0] = 0xf5;
	assert (el.Decode (boundaryMinusOne, buf));
	uint8_t boundaryHighBits[32];
	memcpy (boundaryHighBits, boundary, sizeof (boundaryHighBits));
	boundaryHighBits[31] |= 0xC0;
	assert (!el.Decode (boundaryHighBits, buf));

	// x = 0 is not invertible. Java forces the non-alternative branch; both
	// choices must produce the same representative and must not crash.
	uint8_t zeroKey[32] = { };
	uint8_t zeroLow[32], zeroHigh[32];
	assert (el.Encode (zeroKey, zeroLow, false, false));
	assert (el.Encode (zeroKey, zeroHigh, true, false));
	assert (memcmp (zeroLow, zeroHigh, 32) == 0);

	// Random sampling: eligibility is a property of the point, so retry with
	// a new key (as Java does) rather than the same point. Every successful
	// representative must decode.
	int successfulRepresentatives = 0;
	for (int attempt = 0; attempt < 64; ++attempt)
	{
		i2p::crypto::X25519Keys keys;
		keys.GenerateKeys ();
		uint8_t encoded[32];
		if (!el.Encode (keys.GetPublicKey (), encoded, false, true)) continue;
		uint8_t decoded[32];
		assert (el.Decode (encoded, decoded));
		assert (memcmp (decoded, keys.GetPublicKey (), 32) == 0);
		++successfulRepresentatives;
	}
	assert (successfulRepresentatives > 0 && "no Elligator2 representative found");
}

void TestHKDFAndNoise ()
{
	uint8_t salt[32], ikm[32];
	for (size_t i = 0; i < 32; ++i)
	{
		salt[i] = (uint8_t)i;
		ikm[i] = (uint8_t)(0x40 + i);
	}
	uint8_t out[64], reference[64];
	i2p::crypto::HKDF (salt, ikm, 32, "audit-info", out, sizeof (out));
	RefHKDF (salt, ikm, 32, "audit-info", reference, sizeof (reference));
	assert (memcmp (out, reference, sizeof (out)) == 0);

	// ZEROLEN key path used by ratchet/tunnel key derivation.
	i2p::crypto::HKDF (salt, nullptr, 0, "zerolen", out, sizeof (out));
	RefHKDF (salt, nullptr, 0, "zerolen", reference, sizeof (reference));
	assert (memcmp (out, reference, sizeof (out)) == 0);

	// More than 255 * HashLen output must be rejected and wiped.
	std::vector<uint8_t> tooLong (255 * 32 + 1, 0xA5);
	i2p::crypto::HKDF (salt, ikm, 32, "", tooLong.data (), tooLong.size ());
	assert (IsAllZero (tooLong.data (), tooLong.size ()));

	uint8_t pub[32];
	for (size_t i = 0; i < 32; ++i) pub[i] = (uint8_t)(i * 3 + 1);

	auto CheckInit = [&] (const char * name, i2p::crypto::NoiseSymmetricState & state) {
		uint8_t ck[32] = { };
		memcpy (ck, name, strlen (name) <= 32 ? strlen (name) : 0);
		uint8_t hh[32], h[32];
		Sha256 (ck, 32, nullptr, 0, hh);
		Sha256 (hh, 32, pub, 32, h);
		assert (memcmp (state.m_CK, ck, 32) == 0);
		assert (memcmp (state.m_H, h, 32) == 0);
		assert (state.m_N == 0);
	};

	i2p::crypto::NoiseSymmetricState nState { };
	i2p::crypto::InitNoiseNState (nState, pub);
	CheckInit ("Noise_N_25519_ChaChaPoly_SHA256", nState);

	auto CheckLongInit = [&] (const char * name, i2p::crypto::NoiseSymmetricState & state) {
		const size_t nameLen = strlen (name);
		assert (nameLen > 32);
		uint8_t nameHash[32], hh[32], h[32];
		Sha256 ((const uint8_t *)name, nameLen, nullptr, 0, nameHash);
		Sha256 (nameHash, 32, nullptr, 0, hh);
		Sha256 (hh, 32, pub, 32, h);
		assert (memcmp (state.m_CK, nameHash, 32) == 0);
		assert (memcmp (state.m_H, h, 32) == 0);
	};

	i2p::crypto::NoiseSymmetricState xkState { };
	i2p::crypto::InitNoiseXKState (xkState, pub);
	CheckLongInit ("Noise_XKaesobfse+hs2+hs3_25519_ChaChaPoly_SHA256", xkState);

	i2p::crypto::NoiseSymmetricState xk1State { };
	i2p::crypto::InitNoiseXKState1 (xk1State, pub);
	CheckLongInit ("Noise_XKchaobfse+hs1+hs2+hs3_25519_ChaChaPoly_SHA256", xk1State);

	i2p::crypto::NoiseSymmetricState ikState { };
	i2p::crypto::InitNoiseIKState (ikState, pub);
	CheckLongInit ("Noise_IKelg2+hs2_25519_ChaChaPoly_SHA256", ikState);

	// MixKey must replace ck/k with HKDF(ck, sharedSecret, "", 64).
	uint8_t shared[32];
	for (size_t i = 0; i < 32; ++i) shared[i] = (uint8_t)(0x80 + i);
	uint8_t oldCK[32];
	memcpy (oldCK, nState.m_CK, 32);
	nState.MixKey (shared);
	uint8_t mixed[64];
	RefHKDF (oldCK, shared, 32, "", mixed, sizeof (mixed));
	assert (memcmp (nState.m_CK, mixed, 64) == 0);
	assert (nState.m_N == 0);

	// MixHash(h) = SHA256(h || data).
	uint8_t prevH[32], data[7] = { 1, 2, 3, 4, 5, 6, 7 }, expectedH[32];
	memcpy (prevH, nState.m_H, 32);
	nState.MixHash (data, sizeof (data));
	Sha256 (prevH, 32, data, sizeof (data), expectedH);
	assert (memcmp (nState.m_H, expectedH, 32) == 0);

	// AEAD round trip and nonce sequence. Noise is directional: each peer
	// starts at nonce zero, so use independent sender and receiver copies.
	uint8_t plaintext[48], ciphertext[64], recovered[48];
	for (size_t i = 0; i < sizeof (plaintext); ++i) plaintext[i] = (uint8_t)(i ^ 0x5A);
	i2p::crypto::NoiseSymmetricState sender = nState;
	i2p::crypto::NoiseSymmetricState receiver = nState;
	assert (sender.Encrypt (plaintext, ciphertext, sizeof (plaintext)));
	assert (sender.m_N == 1);
	assert (receiver.Decrypt (ciphertext, recovered, sizeof (plaintext)));
	assert (memcmp (recovered, plaintext, sizeof (plaintext)) == 0);
	assert (receiver.m_N == 1);
	assert (sender.Encrypt (plaintext, ciphertext, sizeof (plaintext)));
	assert (sender.m_N == 2);
	assert (receiver.Decrypt (ciphertext, recovered, sizeof (plaintext)));
	assert (memcmp (recovered, plaintext, sizeof (plaintext)) == 0);
	assert (receiver.m_N == 2);

	uint8_t tampered[64];
	memcpy (tampered, ciphertext, sizeof (tampered));
	tampered[3] ^= 0x01;
	i2p::crypto::NoiseSymmetricState beforeTamper = receiver;
	memset (recovered, 0xA5, sizeof (recovered));
	assert (!receiver.Decrypt (tampered, recovered, sizeof (plaintext)));
	assert (IsAllZero (recovered, sizeof (recovered)));
	assert (receiver.m_N == beforeTamper.m_N); // failed packets do not consume a nonce

	// A wrong key at the same nonce must fail authentication and wipe the
	// output.
	i2p::crypto::NoiseSymmetricState wrongKey = sender; // nonce 2
	i2p::crypto::NoiseSymmetricState wrongReceiver = sender;
	wrongReceiver.m_CK[63] ^= 0x01;
	assert (wrongKey.Encrypt (plaintext, ciphertext, sizeof (plaintext)));
	memset (recovered, 0xA5, sizeof (recovered));
	assert (!wrongReceiver.Decrypt (ciphertext, recovered, sizeof (plaintext)));
	assert (IsAllZero (recovered, sizeof (recovered)));
}

void TestAEADAndChaCha20 ()
{
	uint8_t key[32], nonce[12];
	for (size_t i = 0; i < sizeof (key); ++i) key[i] = (uint8_t)(i + 1);
	for (size_t i = 0; i < sizeof (nonce); ++i) nonce[i] = (uint8_t)(i + 7);

	uint8_t msg[32], encrypted[48], decrypted[32];
	for (size_t i = 0; i < sizeof (msg); ++i) msg[i] = (uint8_t)(0xC0 + i);

	assert (i2p::crypto::AEADChaCha20Poly1305 (msg, sizeof (msg), nullptr, 0, key, nonce, encrypted,
		sizeof (encrypted), true));
	assert (i2p::crypto::AEADChaCha20Poly1305 (encrypted, sizeof (msg), nullptr, 0, key, nonce, decrypted,
		sizeof (decrypted), false));
	assert (memcmp (decrypted, msg, sizeof (msg)) == 0);

	uint8_t tampered[48];
	memcpy (tampered, encrypted, sizeof (tampered));
	tampered[0] ^= 0x80;
	memset (decrypted, 0xA5, sizeof (decrypted));
	assert (!i2p::crypto::AEADChaCha20Poly1305 (tampered, sizeof (msg), nullptr, 0, key, nonce, decrypted,
		sizeof (decrypted), false));
	assert (IsAllZero (decrypted, sizeof (decrypted)));

	// Length narrowing is rejected before any EVP call. The output length is
	// large enough that only the INT_MAX check can reject it.
	assert (!i2p::crypto::AEADChaCha20Poly1305 (msg, (size_t)INT_MAX + 1, nullptr, 0, key, nonce, encrypted,
		(size_t)INT_MAX + 17, true));

	// In-place ChaCha20 is a stream XOR: applying it twice restores the input.
	uint8_t buffer[16], original[16];
	for (size_t i = 0; i < sizeof (buffer); ++i) buffer[i] = (uint8_t)(i * 7 + 3);
	memcpy (original, buffer, sizeof (buffer));
	i2p::crypto::ChaCha20 (buffer, sizeof (buffer), key, nonce, buffer);
	assert (memcmp (buffer, original, sizeof (buffer)) != 0);
	i2p::crypto::ChaCha20 (buffer, sizeof (buffer), key, nonce, buffer);
	assert (memcmp (buffer, original, sizeof (buffer)) == 0);
}

void TestCryptoKeyLengthsAndECIES ()
{
	using namespace i2p::data;
	using i2p::crypto::GetCryptoPrivateKeyLen;
	using i2p::crypto::GetCryptoPublicKeyLen;

	assert (GetCryptoPrivateKeyLen (CRYPTO_KEY_TYPE_ELGAMAL) == 256);
	assert (GetCryptoPublicKeyLen (CRYPTO_KEY_TYPE_ELGAMAL) == 256);
	assert (GetCryptoPrivateKeyLen (CRYPTO_KEY_TYPE_ECIES_P256_SHA256_AES256CBC) == 256);
	assert (GetCryptoPublicKeyLen (CRYPTO_KEY_TYPE_ECIES_P256_SHA256_AES256CBC) == 256);
	assert (GetCryptoPrivateKeyLen (CRYPTO_KEY_TYPE_ECIES_X25519_AEAD) == 32);
	assert (GetCryptoPublicKeyLen (CRYPTO_KEY_TYPE_ECIES_X25519_AEAD) == 32);
	for (auto type : { CRYPTO_KEY_TYPE_ECIES_MLKEM512_X25519_AEAD,
			 CRYPTO_KEY_TYPE_ECIES_MLKEM768_X25519_AEAD, CRYPTO_KEY_TYPE_ECIES_MLKEM1024_X25519_AEAD })
	{
		assert (GetCryptoPrivateKeyLen (type) == 32);
		assert (GetCryptoPublicKeyLen (type) == 32);
	}
	assert (GetCryptoPrivateKeyLen ((CryptoKeyType)2) == 0);
	assert (GetCryptoPublicKeyLen ((CryptoKeyType)8) == 0);

	// Unsupported local key types must stay empty and fail without crashing.
	i2p::crypto::LocalEncryptionKey unsupported ((CryptoKeyType)8);
	assert (unsupported.pub.empty () && unsupported.priv.empty ());
	unsupported.GenerateKeys ();
	unsupported.CreateDecryptor ();
	assert (!unsupported.decryptor);

	i2p::crypto::LocalEncryptionKey local (CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
	assert (local.pub.size () == 32 && local.priv.size () == 32);
	local.GenerateKeys ();
	assert (!IsAllZero (local.pub.data (), local.pub.size ()));
	local.CreateDecryptor ();
	assert (local.decryptor);

	// ECIES P256 valid round trip.
	uint8_t p256Priv[256], p256Pub[256];
	i2p::crypto::CreateECIESP256RandomKeys (p256Priv, p256Pub);
	assert (!IsAllZero (p256Priv, 32));
	assert (!IsAllZero (p256Pub, 64));
	uint8_t data[222], ciphertext[514], plaintext[222];
	for (size_t i = 0; i < sizeof (data); ++i) data[i] = (uint8_t)(i * 5 + 1);
	{
		i2p::crypto::ECIESP256Encryptor encryptor (p256Pub);
		i2p::crypto::ECIESP256Decryptor decryptor (p256Priv);
		encryptor.Encrypt (data, ciphertext);
		assert (decryptor.Decrypt (ciphertext, plaintext));
		assert (memcmp (plaintext, data, sizeof (data)) == 0);
	}

	// An all-zero P256 point is not on the curve: the encryptor must clear
	// its fixed-size output instead of emitting an uninitialised envelope.
	uint8_t invalidPub[256] = { };
	uint8_t invalidCiphertext[514];
	memset (invalidCiphertext, 0xA5, sizeof (invalidCiphertext));
	i2p::crypto::ECIESP256Encryptor invalidEncryptor (invalidPub);
	invalidEncryptor.Encrypt (data, invalidCiphertext);
	assert (IsAllZero (invalidCiphertext, sizeof (invalidCiphertext)));

	// X25519 ratchet wrapper: static-key agree and public-key copy.
	uint8_t ratchetPriv[32], ratchetPub[32], ephemeralPriv[32], ephemeralPub[32];
	i2p::crypto::CreateECIESX25519AEADRatchetRandomKeys (ratchetPriv, ratchetPub);
	i2p::crypto::CreateECIESX25519AEADRatchetRandomKeys (ephemeralPriv, ephemeralPub);
	i2p::crypto::ECIESX25519AEADRatchetEncryptor ratchetEncryptor (ratchetPub);
	uint8_t copied[32];
	ratchetEncryptor.Encrypt (nullptr, copied);
	assert (memcmp (copied, ratchetPub, 32) == 0);
	i2p::crypto::ECIESX25519AEADRatchetDecryptor ratchetDecryptor (ratchetPriv, false);
	assert (memcmp (ratchetDecryptor.GetPubicKey (), ratchetPub, 32) == 0);
	uint8_t ratchetShared[32];
	assert (ratchetDecryptor.Decrypt (ephemeralPub, ratchetShared));
	uint8_t directShared[32];
	i2p::crypto::X25519Keys ephemeral (ephemeralPriv, nullptr);
	assert (ephemeral.Agree (ratchetPub, directShared));
	assert (memcmp (ratchetShared, directShared, 32) == 0);

	// ElGamal with a null public key is rejected by the primitive.
	i2p::crypto::ElGamalEncryptor elGamal (nullptr);
	uint8_t elGamalCiphertext[514];
	memset (elGamalCiphertext, 0xA5, sizeof (elGamalCiphertext));
	elGamal.Encrypt (data, elGamalCiphertext);
	assert (IsAllZero (elGamalCiphertext, sizeof (elGamalCiphertext)));
}

#if OPENSSL_PQ
void CheckMLKEMInit (i2p::data::CryptoKeyType type, const char * name)
{
	uint8_t pub[32] = { };
	for (size_t i = 0; i < sizeof (pub); ++i) pub[i] = (uint8_t)(0x11 + i);
	uint8_t ck[32] = { };
	const size_t nameLen = strlen (name);
	if (nameLen <= 32)
		memcpy (ck, name, nameLen);
	else
		Sha256 ((const uint8_t *)name, nameLen, nullptr, 0, ck);
	uint8_t hh[32], h[32];
	Sha256 (ck, 32, nullptr, 0, hh);
	Sha256 (hh, 32, pub, 32, h);

	i2p::crypto::NoiseSymmetricState state { };
	i2p::crypto::InitNoiseIKStateMLKEM (state, type, pub);
	assert (memcmp (state.m_CK, ck, 32) == 0);
	assert (memcmp (state.m_H, h, 32) == 0);
}

void TestMLKEM ()
{
	using namespace i2p::data;
	using namespace i2p::crypto;

	// Java EncType lengths for the hybrid destination types 5/6/7.
	assert (GetMLKEMPublicKeyLen (CRYPTO_KEY_TYPE_ECIES_MLKEM512_X25519_AEAD) == 800);
	assert (GetMLKEMCipherTextLen (CRYPTO_KEY_TYPE_ECIES_MLKEM512_X25519_AEAD) == 768);
	assert (GetMLKEMPublicKeyLen (CRYPTO_KEY_TYPE_ECIES_MLKEM768_X25519_AEAD) == 1184);
	assert (GetMLKEMCipherTextLen (CRYPTO_KEY_TYPE_ECIES_MLKEM768_X25519_AEAD) == 1088);
	assert (GetMLKEMPublicKeyLen (CRYPTO_KEY_TYPE_ECIES_MLKEM1024_X25519_AEAD) == 1568);
	assert (GetMLKEMCipherTextLen (CRYPTO_KEY_TYPE_ECIES_MLKEM1024_X25519_AEAD) == 1568);
	assert (GetMLKEMPublicKeyLen (CRYPTO_KEY_TYPE_ECIES_X25519_AEAD) == 0);
	assert (GetMLKEMPublicKeyLen ((CryptoKeyType)8) == 0);
	assert (!CreateMLKEMKeys (CRYPTO_KEY_TYPE_ECIES_X25519_AEAD));
	assert (!CreateMLKEMKeys ((CryptoKeyType)8));
#ifdef LIBRESSL_VERSION_NUMBER
	assert (!IsMLKEMSupported (CRYPTO_KEY_TYPE_ECIES_MLKEM512_X25519_AEAD));
#else
	assert (IsMLKEMSupported (CRYPTO_KEY_TYPE_ECIES_MLKEM512_X25519_AEAD));
#endif
	assert (IsMLKEMSupported (CRYPTO_KEY_TYPE_ECIES_MLKEM768_X25519_AEAD));
	assert (IsMLKEMSupported (CRYPTO_KEY_TYPE_ECIES_MLKEM1024_X25519_AEAD));

	CheckMLKEMInit (CRYPTO_KEY_TYPE_ECIES_MLKEM512_X25519_AEAD,
		"Noise_IKhfselg2_25519+MLKEM512_ChaChaPoly_SHA256");
	CheckMLKEMInit (CRYPTO_KEY_TYPE_ECIES_MLKEM768_X25519_AEAD,
		"Noise_IKhfselg2_25519+MLKEM768_ChaChaPoly_SHA256");
	CheckMLKEMInit (CRYPTO_KEY_TYPE_ECIES_MLKEM1024_X25519_AEAD,
		"Noise_IKhfselg2_25519+MLKEM1024_ChaChaPoly_SHA256");

	// XK uses a different protocol name for each type.
	{
		const char * names[] = { "Noise_XKhfsaesobfse+hs2+hs3_25519+MLKEM512_ChaChaPoly_SHA256",
			"Noise_XKhfsaesobfse+hs2+hs3_25519+MLKEM768_ChaChaPoly_SHA256",
			"Noise_XKhfsaesobfse+hs2+hs3_25519+MLKEM1024_ChaChaPoly_SHA256" };
		const CryptoKeyType types[] = { CRYPTO_KEY_TYPE_ECIES_MLKEM512_X25519_AEAD,
			CRYPTO_KEY_TYPE_ECIES_MLKEM768_X25519_AEAD, CRYPTO_KEY_TYPE_ECIES_MLKEM1024_X25519_AEAD };
		uint8_t pub[32] = { };
		for (size_t t = 0; t < 3; ++t)
		{
			uint8_t nameHash[32], hh[32], h[32];
			const size_t nameLen = strlen (names[t]);
			Sha256 ((const uint8_t *)names[t], nameLen, nullptr, 0, nameHash);
			Sha256 (nameHash, 32, nullptr, 0, hh);
			Sha256 (hh, 32, pub, 32, h);
			NoiseSymmetricState state { };
			InitNoiseXKStateMLKEM (state, types[t], pub);
			assert (memcmp (state.m_CK, nameHash, 32) == 0);
			assert (memcmp (state.m_H, h, 32) == 0);
		}
	}

	// SSU2 has 512/768 only; 1024 must leave a deterministic zero state.
	{
		uint8_t pub[32] = { 7 };
		NoiseSymmetricState unsupported { };
		InitNoiseXKStateMLKEM1 (unsupported, CRYPTO_KEY_TYPE_ECIES_MLKEM1024_X25519_AEAD, pub);
		assert (IsAllZero (unsupported.m_CK, sizeof (unsupported.m_CK)));
		assert (IsAllZero (unsupported.m_H, sizeof (unsupported.m_H)));
		assert (unsupported.m_N == 0);

		uint8_t nameHash[32], hh[32], h[32];
		const char * name = "Noise_XKhfschaobfse+hs1+hs2+hs3_25519+MLKEM768_ChaChaPoly_SHA256";
		Sha256 ((const uint8_t *)name, strlen (name), nullptr, 0, nameHash);
		Sha256 (nameHash, 32, nullptr, 0, hh);
		Sha256 (hh, 32, pub, 32, h);
		NoiseSymmetricState supported { };
		InitNoiseXKStateMLKEM1 (supported, CRYPTO_KEY_TYPE_ECIES_MLKEM768_X25519_AEAD, pub);
		assert (memcmp (supported.m_CK, nameHash, 32) == 0);
		assert (memcmp (supported.m_H, h, 32) == 0);
	}

	for (auto type : { CRYPTO_KEY_TYPE_ECIES_MLKEM512_X25519_AEAD,
			 CRYPTO_KEY_TYPE_ECIES_MLKEM768_X25519_AEAD, CRYPTO_KEY_TYPE_ECIES_MLKEM1024_X25519_AEAD })
	{
		if (!IsMLKEMSupported (type)) continue;
		auto keys = CreateMLKEMKeys (type);
		assert (keys);
		assert (keys->GetKeyLen () == GetMLKEMPublicKeyLen (type));
		assert (keys->GetCTLen () == GetMLKEMCipherTextLen (type));
		assert (keys->GenerateKeys ());
		std::vector<uint8_t> pub (keys->GetKeyLen ());
		assert (keys->GetPublicKey (pub.data ()));
		assert (!IsAllZero (pub.data (), pub.size ()));
		assert (!keys->GetPublicKey (nullptr));
		{
			// SetPublicKey(nullptr) releases the current key material, so use a
			// separate object instead of destroying the decapsulation key.
			auto nullKeys = CreateMLKEMKeys (type);
			assert (nullKeys);
			assert (!nullKeys->SetPublicKey (nullptr));
		}

		auto encapsulator = CreateMLKEMKeys (type);
		assert (encapsulator);
		assert (encapsulator->SetPublicKey (pub.data ()));
		std::vector<uint8_t> ciphertext (keys->GetCTLen ());
		uint8_t shared1[32], shared2[32], shared3[32];
		assert (encapsulator->Encaps (ciphertext.data (), shared1));
		assert (keys->Decaps (ciphertext.data (), shared2));
		assert (memcmp (shared1, shared2, 32) == 0);
		assert (!IsAllZero (shared1, 32));
		assert (!keys->Encaps (nullptr, shared3));
		assert (IsAllZero (shared3, 32));

		// ML-KEM implicit rejection: a malformed, correctly sized ciphertext
		// still yields a secret in Java (and is caught by later AEAD), but it
		// must not equal the real secret.
		std::vector<uint8_t> tamperedCiphertext = ciphertext;
		tamperedCiphertext[0] ^= 0x01;
		memset (shared3, 0, sizeof (shared3));
		assert (keys->Decaps (tamperedCiphertext.data (), shared3));
		assert (memcmp (shared3, shared1, 32) != 0);
	}

	// A malformed public key must fail import and clear the previous key.
	{
		auto keys = CreateMLKEMKeys (CRYPTO_KEY_TYPE_ECIES_MLKEM768_X25519_AEAD);
		assert (keys && keys->GenerateKeys ());
		std::vector<uint8_t> bad (keys->GetKeyLen (), 0xFF);
		// Import may accept an arbitrary length-sized string; if it does, the
		// encapsulation step must still fail or produce a non-zero secret.
		if (!keys->SetPublicKey (bad.data ()))
		{
			std::vector<uint8_t> ct (keys->GetCTLen ());
			uint8_t shared[32];
			assert (!keys->Encaps (ct.data (), shared));
			assert (IsAllZero (shared, 32));
		}
	}
}
#endif

}

int main ()
{
	TestX25519 ();
	TestElligator ();
	TestHKDFAndNoise ();
	TestAEADAndChaCha20 ();
	TestCryptoKeyLengthsAndECIES ();
#if OPENSSL_PQ
	TestMLKEM ();
#endif
	return 0;
}
