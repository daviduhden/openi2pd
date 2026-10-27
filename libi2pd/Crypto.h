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
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
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
#include <openssl/x509.h>
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
#ifndef I2PD_HAVE_SSL_SET1_DNSNAME
#define I2PD_HAVE_SSL_SET1_DNSNAME 0
#endif
#ifndef I2PD_HAVE_SSL_SET1_HOST
#define I2PD_HAVE_SSL_SET1_HOST 0
#endif
#ifndef I2PD_HAVE_OPENSSL_MLKEM_H
#define I2PD_HAVE_OPENSSL_MLKEM_H 0
#endif
#ifndef LIBRESSL_VERSION_NUMBER
#define OPENSSL_SIPHASH I2PD_HAVE_EVP_PKEY_SIPHASH
#else
#define OPENSSL_SIPHASH 0
#endif
#if (!defined(LIBRESSL_VERSION_NUMBER) && OPENSSL_VERSION_NUMBER >= 0x030500000)                                       \
	|| ((defined(LIBRESSL_VERSION_NUMBER) && (LIBRESSL_VERSION_NUMBER >= 0x4030000fL)                                  \
		&& I2PD_HAVE_OPENSSL_MLKEM_H)) // 3.5.0 openssl, 4.3.0 libressl with mlkem.h
#define OPENSSL_PQ 1
#endif
#ifndef OPENSSL_PQ
#define OPENSSL_PQ 0
#endif

namespace i2p
{
namespace crypto
{
	bool bn2buf (const BIGNUM * bn, uint8_t * buf, size_t len);

	/**
	 * Copy one attribute from a read-only X.509 name as UTF-8 into a bounded buffer.
	 *
	 * OpenSSL 4 deprecates `X509_NAME_get_text_by_NID`; this uses the stable
	 * index/entry/ASN1 accessors instead. @return the copied byte count, or
	 * -1 when the attribute is absent or the ASN.1 value cannot be converted.
	 * @pre @a out is non-null and @a size is positive; output is always
	 * NUL-terminated on success.
	 */
	inline int GetX509NameTextByNID (const X509_NAME * name, int nid, char * out, int size)
	{
		if (!name || !out || size <= 0) return -1;
		const int index = X509_NAME_get_index_by_NID (name, nid, -1);
		if (index < 0) return -1;
		const X509_NAME_ENTRY * entry = X509_NAME_get_entry (name, index);
		if (!entry) return -1;
		const ASN1_STRING * data = X509_NAME_ENTRY_get_data (entry);
		if (!data) return -1;
		unsigned char * utf8 = nullptr;
		const int length = ASN1_STRING_to_UTF8 (&utf8, data);
		if (length < 0) return -1;
		const int copied = length < size - 1 ? length : size - 1;
		std::memcpy (out, utf8, static_cast<size_t> (copied));
		out[copied] = '\0';
		OPENSSL_free (utf8);
		return copied;
	}

	// DSA
#ifndef LIBRESSL_VERSION_NUMBER // OpenSSL 3.0.2 is the minimum
	EVP_PKEY * CreateDSA (BIGNUM * pubKey = nullptr, BIGNUM * privKey = nullptr);
#else
	DSA * CreateDSA ();
#endif

	// RSA
	const BIGNUM * GetRSAE ();

	// x25519
	//
	// The object owns an EVP_PKEY and its context, so it is neither copyable
	// nor assignable: a shallow copy would double-free the provider objects.
	// A move transfers the ownership and leaves the source in the same
	// fail-closed state as a default-constructed object.
	class X25519Keys
	{
	public:
		X25519Keys ();
		// A non-null pub is not trusted: it is compared with the public key
		// derived from priv and the object is left unusable on any mismatch.
		X25519Keys (const uint8_t * priv, const uint8_t * pub);
		X25519Keys (const X25519Keys &) = delete;
		X25519Keys & operator= (const X25519Keys &) = delete;
		X25519Keys (X25519Keys && other) noexcept;
		X25519Keys & operator= (X25519Keys && other) noexcept;
		~X25519Keys ();

		// Returns false when the provider cannot generate a key. On failure no
		// private key is held and the public key is an all-zero value, so the
		// caller must abort instead of publishing the key material.
		bool GenerateKeys ();
		bool HasPrivateKey () const { return m_Pkey != nullptr; };
		const uint8_t * GetPublicKey () const { return m_PublicKey; };
		void GetPrivateKey (uint8_t * priv) const;
		void SetPrivateKey (const uint8_t * priv, bool calculatePublic = false);
		/// Derive a 32-byte shared secret from a 32-byte peer public key.
		/// @return False for an unusable local key, invalid peer or all-zero
		/// result. A non-null output is zeroed on failure; never use that output
		/// as a handshake key.
		bool Agree (const uint8_t * pub, uint8_t * shared);

		bool IsElligatorIneligible () const { return m_IsElligatorIneligible; }
		void SetElligatorIneligible () { m_IsElligatorIneligible = true; }

	private:
		void Reset ();

		uint8_t m_PublicKey[32];
		EVP_PKEY_CTX * m_Ctx;
		EVP_PKEY * m_Pkey;
		bool m_IsElligatorIneligible = false; // true if definitely ineligible
	};

	// ElGamal
	bool ElGamalEncrypt (
		const uint8_t * key, const uint8_t * data, uint8_t * encrypted); // 222 bytes data, 514 bytes encrypted
	bool ElGamalDecrypt (
		const uint8_t * key, const uint8_t * encrypted, uint8_t * data); // 514 bytes encrypted, 222 data
	void GenerateElGamalKeyPair (uint8_t * priv, uint8_t * pub);

	// ECIES
	bool ECIESEncrypt (const EC_GROUP * curve, const EC_POINT * key, const uint8_t * data,
		uint8_t * encrypted); // 222 bytes data, 514 bytes encrypted; false on failure
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

	/// ChaCha20-Poly1305 with a 32-byte key and 12-byte nonce.
	/// @param msgLen Plaintext/ciphertext length excluding the 16-byte tag.
	/// @param len Capacity of buf: at least msgLen + 16 for encryption and
	/// at least msgLen for decryption. For decryption msg must also contain the
	/// tag immediately after its msgLen ciphertext bytes.
	/// @return True only when encryption or tag-verified decryption succeeds.
	/// @warning Discard output on false, including argument-validation failures.
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

	/// RFC 5869 HKDF-SHA256 over a 32-byte salt.
	/// @param key Input keying material; a null pointer together with keyLen 0
	/// selects the expand-only form, where HMAC-SHA256(salt, empty) is used as
	/// the PRK because the handshake already produced a pseudorandom key.
	/// @param info Protocol context string; it separates otherwise identical
	/// derivations and must never carry secret material.
	/// @param out Destination holding at least outLen bytes.
	/// @param outLen Requested length, bounded by RFC 5869's 255 * 32 bytes.
	/// @warning A failed or rejected derivation leaves the whole output
	/// zeroed; callers must treat that as failure rather than as a key.
	void HKDF (const uint8_t * salt, const uint8_t * key, size_t keyLen, std::string_view info, uint8_t * out,
		size_t outLen = 64); // salt - 32, out - 32 or 64, info <= 32

	/// Reusable HKDF context. Unlike the free function it owns a provider
	/// context and is therefore stateful and not reentrant: every thread that
	/// derives keys must use its own instance, never one shared context.
	class HKDFContext final
	{
	public:
		HKDFContext ();
		~HKDFContext ();

		/// Derive with the same contract as the free HKDF() function.
		void operator() (const uint8_t * salt, const uint8_t * key, size_t keyLen, std::string_view info, uint8_t * out,
			size_t outLen = 64); // salt - 32, out - 32 or 64, info <= 32

	private:
#if I2PD_HAVE_EVP_KDF_FETCH
		EVP_KDF_CTX * m_KdfCtx;
#else
		EVP_PKEY_CTX * m_Ctx;
#endif
	};

	// Noise

	/// Mutable Noise transcript and chain state used during I2P handshakes.
	/// The caller owns the protocol transition order and must abort a handshake
	/// when Encrypt or Decrypt fails. The chain-key storage is wiped on destruction.
	struct NoiseSymmetricState
	{
		uint8_t m_H[32] /*h*/ { }, m_CK[64] /*[ck, k]*/ { };
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
		bool ed25519;
	};

	const CryptoCapabilities & GetCryptoCapabilities ();

	// init and terminate
	bool InitCrypto (bool precomputation); // false when a required provider algorithm is unavailable
	void TerminateCrypto ();
}
}

#endif
