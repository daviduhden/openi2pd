/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Crypto.
 *
 * Declares the OpenSSL-backed primitives used across the router: X25519,
 * ElGamal and ECIES key agreement, AES tunnel layers, ChaCha20-Poly1305,
 * HKDF and Noise symmetric states. Feature macros adapt the interface to the
 * detected OpenSSL or LibreSSL version.
 *
 */

#ifndef CRYPTO_H__
#define CRYPTO_H__

#include <inttypes.h>
#include <string>
#include <string_view>
#include <vector>
#include <openssl/bn.h>
#include <openssl/crypto.h>
#include <openssl/dh.h>
#include <openssl/aes.h>
#include <openssl/dsa.h>
#include <openssl/ecdsa.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/opensslv.h>

#include "Base.h"
#include "Random.h"
#include "Tag.h"

// Feature macros come from compile-time capability probes in CMake, not from
// a guess that every non-LibreSSL build provides the same algorithm set.
#ifndef I2PD_HAVE_TLS_METHOD
#define I2PD_HAVE_TLS_METHOD 0
#endif
#ifndef I2PD_HAVE_ASN1_STRING_GET0_DATA
#define I2PD_HAVE_ASN1_STRING_GET0_DATA 0
#endif
#ifndef I2PD_HAVE_EVP_PKEY_IS_A
#define I2PD_HAVE_EVP_PKEY_IS_A 0
#endif
#ifndef I2PD_HAVE_EVP_PKEY_GROUP_NAME
#define I2PD_HAVE_EVP_PKEY_GROUP_NAME 0
#endif
#ifndef I2PD_HAVE_EVP_PKEY_BN_PARAM
#define I2PD_HAVE_EVP_PKEY_BN_PARAM 0
#endif
#ifndef I2PD_HAVE_EVP_MD_FETCH
#define I2PD_HAVE_EVP_MD_FETCH 0
#endif
#ifndef I2PD_HAVE_EVP_CIPHER_FETCH
#define I2PD_HAVE_EVP_CIPHER_FETCH 0
#endif
#ifndef I2PD_HAVE_EVP_KDF_FETCH
#define I2PD_HAVE_EVP_KDF_FETCH 0
#endif
#ifndef I2PD_HAVE_EVP_PKEY_SIPHASH
#define I2PD_HAVE_EVP_PKEY_SIPHASH 0
#endif
#ifndef I2PD_HAVE_EVP_EC_GEN
#define I2PD_HAVE_EVP_EC_GEN 0
#endif
#ifndef LIBRESSL_VERSION_NUMBER
#define OPENSSL_SIPHASH I2PD_HAVE_EVP_PKEY_SIPHASH
#else
#define OPENSSL_SIPHASH 0
#endif
#if (!defined(LIBRESSL_VERSION_NUMBER) && OPENSSL_VERSION_NUMBER >= 0x030500000)                                       \
	|| ((defined(LIBRESSL_VERSION_NUMBER)                                                                              \
		&& (LIBRESSL_VERSION_NUMBER >= 0x4030000fL))) // 3.5.0 openssl, 4.3.0 libressl
#define OPENSSL_PQ 1
#endif

namespace i2p
{
namespace crypto
{
	bool bn2buf (const BIGNUM * bn, uint8_t * buf, size_t len);

	// DSA
#ifndef LIBRESSL_VERSION_NUMBER // OpenSSL 3.0.2 is the minimum
	EVP_PKEY * CreateDSA (BIGNUM * pubKey = nullptr, BIGNUM * privKey = nullptr);
#else
	DSA * CreateDSA ();
#endif

	// RSA
	const BIGNUM * GetRSAE ();

	// x25519
	class X25519Keys
	{
	public:
		X25519Keys ();
		X25519Keys (const uint8_t * priv, const uint8_t * pub); // if pub is null, derive from priv
		~X25519Keys ();

		void GenerateKeys ();
		const uint8_t * GetPublicKey () const { return m_PublicKey; };
		void GetPrivateKey (uint8_t * priv) const;
		void SetPrivateKey (const uint8_t * priv, bool calculatePublic = false);
		bool Agree (const uint8_t * pub, uint8_t * shared);

		bool IsElligatorIneligible () const { return m_IsElligatorIneligible; }
		void SetElligatorIneligible () { m_IsElligatorIneligible = true; }

	private:
		uint8_t m_PublicKey[32];
		EVP_PKEY_CTX * m_Ctx;
		EVP_PKEY * m_Pkey;
		bool m_IsElligatorIneligible = false; // true if definitely ineligible
	};

	// ElGamal
	void ElGamalEncrypt (
		const uint8_t * key, const uint8_t * data, uint8_t * encrypted); // 222 bytes data, 514 bytes encrypted
	bool ElGamalDecrypt (
		const uint8_t * key, const uint8_t * encrypted, uint8_t * data); // 514 bytes encrypted, 222 data
	void GenerateElGamalKeyPair (uint8_t * priv, uint8_t * pub);

	// ECIES
	void ECIESEncrypt (const EC_GROUP * curve, const EC_POINT * key, const uint8_t * data,
		uint8_t * encrypted); // 222 bytes data, 514 bytes encrypted
	bool ECIESDecrypt (const EC_GROUP * curve, const BIGNUM * key, const uint8_t * encrypted,
		uint8_t * data); // 514 bytes encrypted, 222 data
	void GenerateECIESKeyPair (const EC_GROUP * curve, BIGNUM *& priv, EC_POINT *& pub);

	// AES
	typedef i2p::data::Tag<32> AESKey;

	class ECBEncryption
	{
	public:
		ECBEncryption ();
		~ECBEncryption ();

		void SetKey (const uint8_t * key) { m_Key = key; };
		void Encrypt (const uint8_t * in, uint8_t * out);
		bool EncryptChecked (const uint8_t * in, uint8_t * out);

	private:
		AESKey m_Key;
		EVP_CIPHER_CTX * m_Ctx;
	};

	class ECBDecryption
	{
	public:
		ECBDecryption ();
		~ECBDecryption ();

		void SetKey (const uint8_t * key) { m_Key = key; };
		void Decrypt (const uint8_t * in, uint8_t * out);
		bool DecryptChecked (const uint8_t * in, uint8_t * out);

	private:
		AESKey m_Key;
		EVP_CIPHER_CTX * m_Ctx;
	};

	class CBCEncryption
	{
	public:
		CBCEncryption ();
		~CBCEncryption ();

		void SetKey (const uint8_t * key) { m_Key = key; }; // 32 bytes
		void Encrypt (const uint8_t * in, size_t len, const uint8_t * iv, uint8_t * out);
		bool EncryptChecked (const uint8_t * in, size_t len, const uint8_t * iv, uint8_t * out);

	private:
		AESKey m_Key;
		EVP_CIPHER_CTX * m_Ctx;
	};

	class CBCDecryption
	{
	public:
		CBCDecryption ();
		~CBCDecryption ();

		void SetKey (const uint8_t * key) { m_Key = key; }; // 32 bytes
		void Decrypt (const uint8_t * in, size_t len, const uint8_t * iv, uint8_t * out);
		bool DecryptChecked (const uint8_t * in, size_t len, const uint8_t * iv, uint8_t * out);

	private:
		AESKey m_Key;
		EVP_CIPHER_CTX * m_Ctx;
	};

	class TunnelEncryption // with double IV encryption
	{
	public:
		void SetKeys (const AESKey & layerKey, const AESKey & ivKey)
		{
			m_LayerEncryption.SetKey (layerKey);
			m_IVEncryption.SetKey (ivKey);
		}

		void Encrypt (const uint8_t * in, uint8_t * out); // 1024 bytes (16 IV + 1008 data)

	private:
		ECBEncryption m_IVEncryption;
		CBCEncryption m_LayerEncryption;
	};

	class TunnelDecryption // with double IV encryption
	{
	public:
		void SetKeys (const AESKey & layerKey, const AESKey & ivKey)
		{
			m_LayerDecryption.SetKey (layerKey);
			m_IVDecryption.SetKey (ivKey);
		}

		void Decrypt (const uint8_t * in, uint8_t * out); // 1024 bytes (16 IV + 1008 data)

	private:
		ECBDecryption m_IVDecryption;
		CBCDecryption m_LayerDecryption;
	};

	// AEAD/ChaCha20/Poly1305

	class AEADChaCha20Poly1305Encryptor
	{
	public:
		AEADChaCha20Poly1305Encryptor ();
		~AEADChaCha20Poly1305Encryptor ();

		bool Encrypt (const uint8_t * msg, size_t msgLen, const uint8_t * ad, size_t adLen, const uint8_t * key,
			const uint8_t * nonce, uint8_t * buf, size_t len); // msgLen is len without tag

		bool Encrypt (const std::vector<std::pair<uint8_t *, size_t>> & bufs, const uint8_t * key,
			const uint8_t * nonce, uint8_t * mac); // encrypt multiple buffers with zero ad; false = write must abort

	private:
		EVP_CIPHER_CTX * m_Ctx;
	};

	class AEADChaCha20Poly1305Decryptor
	{
	public:
		AEADChaCha20Poly1305Decryptor ();
		~AEADChaCha20Poly1305Decryptor ();

		bool Decrypt (const uint8_t * msg, size_t msgLen, const uint8_t * ad, size_t adLen, const uint8_t * key,
			const uint8_t * nonce, uint8_t * buf, size_t len); // msgLen is len without tag

	private:
		EVP_CIPHER_CTX * m_Ctx;
	};

	bool AEADChaCha20Poly1305 (const uint8_t * msg, size_t msgLen, const uint8_t * ad, size_t adLen,
		const uint8_t * key, const uint8_t * nonce, uint8_t * buf, size_t len,
		bool encrypt); // msgLen is len without tag

	// ChaCha20
	void ChaCha20 (const uint8_t * msg, size_t msgLen, const uint8_t * key, const uint8_t * nonce, uint8_t * out);

	class ChaCha20Context final
	{
	public:
		ChaCha20Context ();
		~ChaCha20Context ();
		void operator() (const uint8_t * msg, size_t msgLen, const uint8_t * key, const uint8_t * nonce, uint8_t * out);

	private:
		EVP_CIPHER_CTX * m_Ctx;
	};

	// HKDF

	void HKDF (const uint8_t * salt, const uint8_t * key, size_t keyLen, std::string_view info, uint8_t * out,
		size_t outLen = 64); // salt - 32, out - 32 or 64, info <= 32

	class HKDFContext final
	{
	public:
		HKDFContext ();
		~HKDFContext ();

		void operator() (const uint8_t * salt, const uint8_t * key, size_t keyLen, std::string_view info, uint8_t * out,
			size_t outLen = 64); // salt - 32, out - 32 or 64, info <= 32

	private:
		EVP_PKEY_CTX * m_Ctx;
	};

	// Noise

	struct NoiseSymmetricState
	{
		uint8_t m_H[32] /*h*/ {}, m_CK[64] /*[ck, k]*/ {};
		uint64_t m_N = 0;

		~NoiseSymmetricState () { OPENSSL_cleanse (m_CK, sizeof (m_CK)); }

		void Init (const uint8_t * ck, const uint8_t * hh, const uint8_t * pub);

		void MixHash (const uint8_t * buf, size_t len);
		void MixHash (const std::vector<std::pair<uint8_t *, size_t>> & bufs);
		void MixKey (const uint8_t * sharedSecret);

		bool Encrypt (const uint8_t * in, uint8_t * out, size_t len); // out length = len + 16
		bool Decrypt (const uint8_t * in, uint8_t * out, size_t len); // len without 16 bytes tag
	};

	void InitNoiseNState (NoiseSymmetricState & state, const uint8_t * pub); // Noise_N (tunnels, router)
	void InitNoiseXKState (NoiseSymmetricState & state, const uint8_t * pub); // Noise_XK (NTCP2)
	void InitNoiseXKState1 (NoiseSymmetricState & state, const uint8_t * pub); // Noise_XK (SSU2)
	void InitNoiseIKState (NoiseSymmetricState & state, const uint8_t * pub); // Noise_IK (ratchets)

	// Provider/algorithm availability as observed by the active libcrypto.
	// Algorithm names are fetched through the EVP provider API where it exists;
	// the legacy helpers are only the compatibility branch for LibreSSL.
	struct CryptoCapabilities
	{
		bool sha1;
		bool sha256;
		bool sha512;
		bool aes256Cbc;
		bool aes256Ecb;
		bool aes256Ctr;
		bool chacha20;
		bool chacha20Poly1305;
		bool hkdf;
		bool sipHash;
	};

	const CryptoCapabilities & GetCryptoCapabilities ();

	// init and terminate
	void InitCrypto (bool precomputation);
	void TerminateCrypto ();
}
}

#endif
