/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: CryptoKey.
 *
 * Declares the polymorphic encryptor and decryptor interfaces used by
 * destinations, with ElGamal, ECIES-P256, ECIES-GOST and
 * ECIES-X25519-AEAD-Ratchet implementations. Also defines key-length
 * helpers and local encryption-key containers for the supported crypto
 * key types.
 *
 */

#ifndef CRYPTO_KEY_H__
#define CRYPTO_KEY_H__

#include <inttypes.h>
#include <memory>
#include <vector>
#include "Crypto.h"
#include "Identity.h"

namespace i2p
{
namespace crypto
{
	/// Destination encryption interface selected by the identity's key
	/// type. Implementations have different wire sizes and, for ratchets,
	/// different semantics; callers must provide buffers sized for the
	/// selected type.
	class CryptoKeyEncryptor
	{
	public:
		virtual ~CryptoKeyEncryptor () { };
		virtual bool Encrypt (const uint8_t * data, uint8_t * encrypted) = 0;
	};

	/// Matching destination decryption or key-agreement interface. A false
	/// return from Decrypt must stop processing of the result.
	/// Implementations hold long-term private key material, so an instance
	/// must never be copied, cached in a log or reused after the owning
	/// destination's keys were regenerated.
	class CryptoKeyDecryptor
	{
	public:
		virtual ~CryptoKeyDecryptor () { };
		virtual bool Decrypt (const uint8_t * encrypted, uint8_t * data) = 0;
		virtual size_t GetPublicKeyLen () const = 0; // we need it to set key in LS2
	};

	// ElGamal
	class ElGamalEncryptor : public CryptoKeyEncryptor // for destination
	{
	public:
		ElGamalEncryptor (const uint8_t * pub);
		bool Encrypt (const uint8_t * data, uint8_t * encrypted) override; // 222 bytes data, 514 bytes encrypted

	private:
		uint8_t m_PublicKey[256];
	};

	class ElGamalDecryptor : public CryptoKeyDecryptor // for destination
	{
	public:
		ElGamalDecryptor (const uint8_t * priv);
		~ElGamalDecryptor () override;
		bool Decrypt (const uint8_t * encrypted, uint8_t * data) override; // 514 bytes encrypted, 222 bytes data
		size_t GetPublicKeyLen () const override { return 256; };

	private:
		uint8_t m_PrivateKey[256];
	};

	// ECIES P256

	class ECIESP256Encryptor : public CryptoKeyEncryptor
	{
	public:
		ECIESP256Encryptor (const uint8_t * pub);
		~ECIESP256Encryptor ();
		bool Encrypt (const uint8_t * data, uint8_t * encrypted) override;

	private:
		EC_GROUP * m_Curve;
		EC_POINT * m_PublicKey;
	};

	class ECIESP256Decryptor : public CryptoKeyDecryptor
	{
	public:
		ECIESP256Decryptor (const uint8_t * priv);
		~ECIESP256Decryptor ();
		bool Decrypt (const uint8_t * encrypted, uint8_t * data) override;
		size_t GetPublicKeyLen () const override { return 64; };

	private:
		EC_GROUP * m_Curve;
		BIGNUM * m_PrivateKey;
	};

	void CreateECIESP256RandomKeys (uint8_t * priv, uint8_t * pub);

	// ECIES GOST R 34.10

	class ECIESGOSTR3410Encryptor : public CryptoKeyEncryptor
	{
	public:
		ECIESGOSTR3410Encryptor (const uint8_t * pub);
		~ECIESGOSTR3410Encryptor ();
		bool Encrypt (const uint8_t * data, uint8_t * encrypted) override;

	private:
		EC_POINT * m_PublicKey;
	};

	class ECIESGOSTR3410Decryptor : public CryptoKeyDecryptor
	{
	public:
		ECIESGOSTR3410Decryptor (const uint8_t * priv);
		~ECIESGOSTR3410Decryptor ();
		bool Decrypt (const uint8_t * encrypted, uint8_t * data) override;
		size_t GetPublicKeyLen () const override { return 64; };

	private:
		BIGNUM * m_PrivateKey;
	};

	void CreateECIESGOSTR3410RandomKeys (uint8_t * priv, uint8_t * pub);

	// ECIES-X25519-AEAD-Ratchet

	class ECIESX25519AEADRatchetEncryptor : public CryptoKeyEncryptor
	{
	public:
		ECIESX25519AEADRatchetEncryptor (const uint8_t * pub);
		~ECIESX25519AEADRatchetEncryptor () { };
		bool Encrypt (const uint8_t *, uint8_t * pub) override;
		// copies m_PublicKey to pub

	private:
		uint8_t m_PublicKey[32];
	};

	/// Static-key decryptor for the ECIES-X25519-AEAD-Ratchet handshake.
	/// The 32-byte static public key is always derived from the configured
	/// private key, so GetPubicKey() reports the key this instance actually
	/// holds and an unusable (null or all-zero) private key yields an
	/// all-zero public key.
	class ECIESX25519AEADRatchetDecryptor : public CryptoKeyDecryptor
	{
	public:
		/// @param priv 32-byte static private key, or null for an empty
		///        decryptor whose Decrypt() always fails.
		/// @param calculatePublic Retained for source compatibility only; the
		///        public key is always derived.
		ECIESX25519AEADRatchetDecryptor (const uint8_t * priv, bool calculatePublic = false);
		~ECIESX25519AEADRatchetDecryptor () { };
		bool Decrypt (const uint8_t * epub, uint8_t * sharedSecret) override;
		// agree with static and return in sharedSecret (32 bytes)
		size_t GetPublicKeyLen () const override { return 32; };
		/// @return The static public key derived from the configured private
		///         key, or all zero for an empty decryptor. The misspelling
		///         is part of the existing internal API and is kept stable.
		const uint8_t * GetPubicKey () const { return m_StaticKeys.GetPublicKey (); };

	private:
		X25519Keys m_StaticKeys;
	};

	// Generates an X25519 (or hybrid X25519 first hop) key pair. Returns
	// false when the provider cannot generate a key or when either output
	// pointer is null. Every non-null output buffer is zeroed on failure,
	// including the buffer whose sibling pointer was null, so a caller must
	// abort and must not reuse the buffer it passed in.
	bool CreateECIESX25519AEADRatchetRandomKeys (uint8_t * priv, uint8_t * pub); // including hybrid

	constexpr size_t GetCryptoPrivateKeyLen (i2p::data::CryptoKeyType type)
	{
		switch (type)
		{
		case i2p::data::CRYPTO_KEY_TYPE_ELGAMAL: return 256;
		case i2p::data::CRYPTO_KEY_TYPE_ECIES_P256_SHA256_AES256CBC:
			return 256; // actual size is 32, but we use 256 for compatibility with old keys files
		case i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD: return 32;
		// ML-KEM hybrid
		case i2p::data::CRYPTO_KEY_TYPE_ECIES_MLKEM512_X25519_AEAD:
		case i2p::data::CRYPTO_KEY_TYPE_ECIES_MLKEM768_X25519_AEAD:
		case i2p::data::CRYPTO_KEY_TYPE_ECIES_MLKEM1024_X25519_AEAD: return 32;
		};
		return 0;
	}

	constexpr size_t GetCryptoPublicKeyLen (i2p::data::CryptoKeyType type)
	{
		switch (type)
		{
		case i2p::data::CRYPTO_KEY_TYPE_ELGAMAL: return 256;
		case i2p::data::CRYPTO_KEY_TYPE_ECIES_P256_SHA256_AES256CBC:
			return 256; // actual size is 32, but we use 256 for compatibility with old keys files
		case i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD: return 32;
		// ML-KEM hybrid
		case i2p::data::CRYPTO_KEY_TYPE_ECIES_MLKEM512_X25519_AEAD:
		case i2p::data::CRYPTO_KEY_TYPE_ECIES_MLKEM768_X25519_AEAD:
		case i2p::data::CRYPTO_KEY_TYPE_ECIES_MLKEM1024_X25519_AEAD: return 32;
		};
		return 0;
	}

	struct LocalEncryptionKey
	{
		std::vector<uint8_t> pub, priv;
		i2p::data::CryptoKeyType keyType;
		std::shared_ptr<CryptoKeyDecryptor> decryptor;

		LocalEncryptionKey (i2p::data::CryptoKeyType t);
		// Returns false for an unsupported type or a provider failure. pub
		// and priv are then all-zero/empty, so a caller must not add the key
		// to a LeaseSet2 key section.
		bool GenerateKeys ();
		void CreateDecryptor ();
	};
}
}

#endif
