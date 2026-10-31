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
#include <limits>

#include "Base.h"
#include "Crypto.h"

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
	assert (!i2p::crypto::ElGamalDecrypt (privateKey, encrypted, decrypted));

	// A generated key pair must still round-trip; the range checks above
	// must not reject legitimate ElGamal keys.
	uint8_t generatedPrivate[256] = { };
	uint8_t generatedPublic[256] = { };
	i2p::crypto::GenerateElGamalKeyPair (generatedPrivate, generatedPublic);
	i2p::crypto::ElGamalEncrypt (generatedPublic, data, encrypted);
	assert (i2p::crypto::ElGamalDecrypt (generatedPrivate, encrypted, decrypted));
	assert (memcmp (decrypted, data, sizeof (data)) == 0);
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
}

int main ()
{
	TestBn2BufBounds ();
	TestBaseDecoders ();
	TestX25519RejectsInvalidPeer ();
	TestElGamalRejectsTrivialPublicKeys ();
	TestAEADBounds ();
	TestAESRejectsInvalidLengths ();
	return 0;
}
