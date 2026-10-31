/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Identity.
 *
 * I2P identity and key material: the fixed 387-byte Identity layout,
 * extended identities with signing and crypto key types, IdentHash
 * derivation, sign/verify and encryption helpers, and Base64/buffer
 * codecs. Private key containers cleanse transient secrets with
 * OPENSSL_cleanse and support b33 per-day offline signing keys.
 *
 */

#ifndef IDENTITY_H__
#define IDENTITY_H__

#include <inttypes.h>
#include <string.h>
#include <string>
#include <string_view>
#include <memory>
#include <vector>
#include <span>
#include <openssl/crypto.h>
#include "Base.h"
#include "I2PEndian.h"
#include "Signature.h"
#include "Tag.h"

namespace i2p
{
namespace crypto
{
	class CryptoKeyEncryptor;
	class CryptoKeyDecryptor;
}
namespace data
{
	typedef Tag<32> IdentHash;
	inline std::string GetIdentHashAbbreviation (const IdentHash & ident)
	{
		return ident.ToBase64 ().substr (0, 4);
	}

	std::vector<IdentHash> ExtractIdentHashes (std::string_view hashes);

	struct Keys
	{
		uint8_t privateKey[256];
		uint8_t signingPrivateKey[20];
		uint8_t publicKey[256];
		uint8_t signingKey[128];
	};

	const uint8_t CERTIFICATE_TYPE_NULL = 0;
	const uint8_t CERTIFICATE_TYPE_HASHCASH = 1;
	const uint8_t CERTIFICATE_TYPE_HIDDEN = 2;
	const uint8_t CERTIFICATE_TYPE_SIGNED = 3;
	const uint8_t CERTIFICATE_TYPE_MULTIPLE = 4;
	const uint8_t CERTIFICATE_TYPE_KEY = 5;

	struct Identity
	{
		uint8_t publicKey[256] { };
		uint8_t signingKey[128] { };
		uint8_t certificate[3] { }; // byte 1 - type, bytes 2-3 - length

		Identity () = default;
		Identity (const Keys & keys) { *this = keys; };
		Identity & operator= (const Keys & keys);
		size_t FromBuffer (const uint8_t * buf, size_t len);
		IdentHash Hash () const;
		operator uint8_t *() { return reinterpret_cast<uint8_t *> (this); }
		operator const uint8_t *() const { return reinterpret_cast<const uint8_t *> (this); }
	};

	Keys CreateRandomKeys ();

	const size_t DEFAULT_IDENTITY_SIZE = sizeof (Identity); // 387 bytes

	const uint16_t CRYPTO_KEY_TYPE_ELGAMAL = 0;
	const uint16_t CRYPTO_KEY_TYPE_ECIES_P256_SHA256_AES256CBC = 1;
	const uint16_t CRYPTO_KEY_TYPE_ECIES_X25519_AEAD = 4;
	const uint16_t CRYPTO_KEY_TYPE_ECIES_MLKEM512_X25519_AEAD = 5;
	const uint16_t CRYPTO_KEY_TYPE_ECIES_MLKEM768_X25519_AEAD = 6;
	const uint16_t CRYPTO_KEY_TYPE_ECIES_MLKEM1024_X25519_AEAD = 7;

	const uint16_t SIGNING_KEY_TYPE_DSA_SHA1 = 0;
	const uint16_t SIGNING_KEY_TYPE_ECDSA_SHA256_P256 = 1;
	const uint16_t SIGNING_KEY_TYPE_ECDSA_SHA384_P384 = 2;
	const uint16_t SIGNING_KEY_TYPE_ECDSA_SHA512_P521 = 3;
	const uint16_t SIGNING_KEY_TYPE_RSA_SHA256_2048 = 4;
	const uint16_t SIGNING_KEY_TYPE_RSA_SHA384_3072 = 5;
	const uint16_t SIGNING_KEY_TYPE_RSA_SHA512_4096 = 6;
	const uint16_t SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519 = 7;
	const uint16_t SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519ph = 8; // since openssl 3.0.0
	const uint16_t SIGNING_KEY_TYPE_GOSTR3410_CRYPTO_PRO_A_GOSTR3411_256 = 9;
	const uint16_t SIGNING_KEY_TYPE_GOSTR3410_TC26_A_512_GOSTR3411_512 = 10; // approved by FSB
	const uint16_t SIGNING_KEY_TYPE_REDDSA_SHA512_ED25519 = 11; // for LeaseSet2 only

	typedef uint16_t SigningKeyType;
	typedef uint16_t CryptoKeyType;

	const size_t MAX_EXTENDED_BUFFER_SIZE = 8; // cryptoKeyType + signingKeyType + 4 extra bytes of P521
	/// Parsed I2P identity, including its key certificate and computed
	/// hash. The certificate may extend the fixed identity with
	/// signing-key bytes; the verifier and extended storage are owned by
	/// this object.
	/// @see https://i2p.net/en/docs/specs/common-structures/
	class IdentityEx
	{
	public:
		IdentityEx ();
		IdentityEx (const uint8_t * publicKey, const uint8_t * signingKey,
			SigningKeyType type = SIGNING_KEY_TYPE_DSA_SHA1, CryptoKeyType cryptoType = CRYPTO_KEY_TYPE_ELGAMAL);
		IdentityEx (const uint8_t * buf, size_t len);
		IdentityEx (const IdentityEx & other);
		IdentityEx (const Identity & standard);
		~IdentityEx ();
		IdentityEx & operator= (const IdentityEx & other);
		IdentityEx & operator= (const Identity & standard);

		/// Replace the identity from a bounded serialized input.
		/// @return Bytes consumed, or zero on malformed/truncated
		///         input; on failure IsValid() is false and the previous
		///         parsed identity is lost.
		size_t FromBuffer (const uint8_t * buf, size_t len);
		/// Write the complete serialized identity without partial output.
		/// @return Bytes written, or zero if invalid or the buffer is too
		///         small.
		size_t ToBuffer (uint8_t * buf, size_t len) const;
		size_t FromBase64 (std::string_view s);
		std::string ToBase64 () const;
		const Identity & GetStandardIdentity () const { return m_StandardIdentity; };
		bool IsValid () const { return m_IsValid; };

		const IdentHash & GetIdentHash () const { return m_IdentHash; };
		const uint8_t * GetEncryptionPublicKey () const { return m_StandardIdentity.publicKey; };
		uint8_t * GetEncryptionPublicKeyBuffer () { return m_StandardIdentity.publicKey; };
		std::shared_ptr<i2p::crypto::CryptoKeyEncryptor> CreateEncryptor (const uint8_t * key) const;
		size_t GetFullLen () const { return m_IsValid ? m_ExtendedLen + DEFAULT_IDENTITY_SIZE : 0; };
		size_t GetSigningPublicKeyLen () const;
		const uint8_t * GetSigningPublicKeyBuffer () const; // returns NULL for P521
		size_t GetSigningPrivateKeyLen () const;
		size_t GetSignatureLen () const;
		/// Verify using this identity's signing key; false also covers an
		/// unsupported or unavailable verifier. The signature buffer must
		/// hold GetSignatureLen() bytes.
		bool Verify (const uint8_t * buf, size_t len, const uint8_t * signature) const;
		SigningKeyType GetSigningKeyType () const;
		bool IsRSA () const; // signing key type
		CryptoKeyType GetCryptoKeyType () const;

		bool operator== (const IdentityEx & other) const { return GetIdentHash () == other.GetIdentHash (); }
		void RecalculateIdentHash (uint8_t * buff = nullptr);

		static i2p::crypto::Verifier * CreateVerifier (SigningKeyType keyType);
		static std::shared_ptr<i2p::crypto::CryptoKeyEncryptor> CreateEncryptor (
			CryptoKeyType keyType, const uint8_t * key);

	private:
		void CreateVerifier ();
		const uint8_t * GetExtendedBuffer () const
		{
			return m_ExtendedLen <= MAX_EXTENDED_BUFFER_SIZE ? m_ExtendedBuffer : m_ExtendedBufferPtr.get ();
		}

	private:
		Identity m_StandardIdentity;
		IdentHash m_IdentHash;
		std::unique_ptr<i2p::crypto::Verifier> m_Verifier;
		size_t m_ExtendedLen;
		uint8_t m_ExtendedBuffer[MAX_EXTENDED_BUFFER_SIZE] { };
		std::unique_ptr<uint8_t[]> m_ExtendedBufferPtr;
		bool m_IsValid = false;
	};

	size_t GetIdentityBufferLen (const uint8_t * buf, size_t len); // return actual identity length in buffer

	const size_t OFFLINE_SIGNATURE_HEADER_LENGTH = 4 + 2; // expires, transient signature type
	const uint8_t B33_OFFLINE_KEYS_VERSION = 1;
	const size_t B33_OFFLINE_KEYS_HEADER_LENGTH = 1 + IdentHash::len + 2; // version, ident hash, number of keys
	const uint64_t SECONDS_PER_DAY = 24 * 60 * 60;

	// version || ident hash || number of keys, then an offline signature
	// per day. Appended to the keys file, where a router without b33
	// offline keys does not look for it
	class B33OfflineKeys
	{
	public:
		~B33OfflineKeys ()
		{
			if (!m_Buf.empty ()) OPENSSL_cleanse (m_Buf.data (), m_Buf.size ());
		}
		size_t GetLen () const { return m_Buf.size (); };
		const uint8_t * GetBuffer () const { return m_Buf.data (); };
		bool operator== (const B33OfflineKeys & other) const { return m_Buf == other.m_Buf; };
		/// Adopt a trailing keys-file section.
		/// The section is accepted only when its version byte matches and the
		/// embedded identity hash equals `ident`, so a keys file copied from
		/// another destination cannot introduce foreign per-day signing keys.
		/// @return Bytes consumed, or zero when rejected; the
		///         previous contents are cleared before validation, so a
		///         rejection leaves no key behind.
		size_t FromBuffer (const uint8_t * buf, size_t len, const IdentHash & ident);
		size_t ToBuffer (uint8_t * buf, size_t len) const;

	private:
		std::vector<uint8_t> m_Buf;
	};

	class PrivateKeys // for eepsites
	{
	public:
		PrivateKeys () = default;
		PrivateKeys (const PrivateKeys & other) { *this = other; };
		PrivateKeys (const Keys & keys) { *this = keys; };
		PrivateKeys & operator= (const Keys & keys);
		PrivateKeys & operator= (const PrivateKeys & other);
		~PrivateKeys ()
		{
			m_Signer.reset ();
			OPENSSL_cleanse (m_PrivateKey, sizeof (m_PrivateKey));
			if (!m_SigningPrivateKey.empty ())
				OPENSSL_cleanse (m_SigningPrivateKey.data (), m_SigningPrivateKey.size ());
			if (!m_OfflineSignature.empty ()) OPENSSL_cleanse (m_OfflineSignature.data (), m_OfflineSignature.size ());
		}

		std::shared_ptr<const IdentityEx> GetPublic () const { return m_Public; };
		const uint8_t * GetPrivateKey () const { return m_PrivateKey; };
		const uint8_t * GetSigningPrivateKey () const { return m_SigningPrivateKey.data (); };
		size_t GetSignatureLen () const; // might not match identity
		bool IsOfflineSignature () const { return m_TransientSignatureLen > 0; };
		uint8_t * GetPadding ();
		void RecalculateIdentHash (uint8_t * buf = nullptr) { m_Public->RecalculateIdentHash (buf); }
		/// Sign with the identity's own signer, created on first use. A
		/// missing identity, an unsupported signing type or a malformed
		/// length zeroes the signature and returns without signing, because
		/// the signer interface itself is `void`; a provider failure is
		/// therefore only visible as an all-zero signature. Callers must not
		/// publish a signature they did not verify. See `CRY-236` for the
		/// planned fallible interface.
		void Sign (const uint8_t * buf, int len, uint8_t * signature) const;

		size_t GetFullLen () const;
		/// Parse a complete keys file: identity, crypto private key and
		/// signing private key, optionally followed by the offline-signature
		/// and b33 sections.
		/// @return Bytes consumed, or zero for unsupported key types,
		///         truncated input or an invalid identity. Every intermediate
		///         failure path clears the partially parsed key material
		///         through a scope guard.
		size_t FromBuffer (const uint8_t * buf, size_t len);
		size_t ToBuffer (uint8_t * buf, size_t len) const;

		size_t FromBase64 (std::string_view s);
		std::string ToBase64 () const;

		std::shared_ptr<i2p::crypto::CryptoKeyDecryptor> CreateDecryptor (const uint8_t * key) const;

		static std::shared_ptr<i2p::crypto::CryptoKeyDecryptor> CreateDecryptor (
			CryptoKeyType cryptoType, const uint8_t * key);
		static PrivateKeys CreateRandomKeys (SigningKeyType type = SIGNING_KEY_TYPE_DSA_SHA1,
			CryptoKeyType cryptoType = CRYPTO_KEY_TYPE_ELGAMAL, bool isDestination = false);
		static bool GenerateSigningKeyPair (SigningKeyType type, uint8_t * priv, uint8_t * pub);
		static bool GenerateCryptoKeyPair (
			CryptoKeyType type, uint8_t * priv, uint8_t * pub); // priv and pub are 256 bytes long
		static i2p::crypto::Signer * CreateSigner (SigningKeyType keyType, const uint8_t * priv);

		// offline keys
		PrivateKeys CreateOfflineKeys (SigningKeyType type, uint32_t expires) const;
		const std::vector<uint8_t> & GetOfflineSignature () const { return m_OfflineSignature; };
		const B33OfflineKeys & GetB33OfflineKeys () const { return m_B33OfflineKeys; };
		void UpdateOfflineSignature (const PrivateKeys & other); // refresh transient material, keep identity

	private:
		void CreateSigner () const;
		void CreateSigner (SigningKeyType keyType) const;
		size_t GetPrivateKeyLen () const;

	private:
		std::shared_ptr<IdentityEx> m_Public;
		uint8_t m_PrivateKey[256] { };
		std::vector<uint8_t> m_SigningPrivateKey;
		mutable std::unique_ptr<i2p::crypto::Signer> m_Signer;
		std::vector<uint8_t> m_OfflineSignature; // non zero length, if applicable
		size_t m_TransientSignatureLen = 0;
		size_t m_TransientSigningPrivateKeyLen = 0;
		B33OfflineKeys m_B33OfflineKeys;
	};

	// the destination's signing key is offline: a transient per day out of
	// the b33 offline keys, each authorized by the blinded key of its own
	// day
	class OfflinePrivateKeys : public PrivateKeys
	{
	public:
		OfflinePrivateKeys (const PrivateKeys & keys, const char * date); // date is 8 chars "YYYYMMDD"

		bool IsOfflineSignature () const
		{
			return !m_TransientPrivateKey.empty ();
		}; // false if that day can't be signed
		const uint8_t * GetSigningPrivateKey () const
		{
			return m_TransientPrivateKey.empty () ? PrivateKeys::GetSigningPrivateKey ()
												  : m_TransientPrivateKey.data ();
		};
		void Sign (const uint8_t * buf, int len, uint8_t * signature) const
		{
			if (m_Signer)
				m_Signer->Sign (buf, len, signature);
			else
				PrivateKeys::Sign (buf, len, signature);
		};
		size_t GetSignatureLen () const { return m_Signer ? m_SignatureLen : PrivateKeys::GetSignatureLen (); };
		const std::vector<uint8_t> & GetOfflineSignature () const { return m_OfflineSignature; };

	private:
		size_t m_SignatureLen = 0;
		std::span<const uint8_t> m_TransientPrivateKey; // points into the b33 offline keys
		std::vector<uint8_t> m_OfflineSignature;
		std::unique_ptr<i2p::crypto::Signer> m_Signer;
	};

	// destination for delivery instructions
	class RoutingDestination
	{
	public:
		RoutingDestination () { };
		virtual ~RoutingDestination () { };

		virtual std::shared_ptr<const IdentityEx> GetIdentity () const = 0;
		virtual bool Encrypt (const uint8_t * data, uint8_t * encrypted) const = 0; // encrypt data for
		virtual bool IsDestination () const = 0; // for garlic

		const IdentHash & GetIdentHash () const { return GetIdentity ()->GetIdentHash (); };
		virtual CryptoKeyType GetEncryptionType () const
		{
			return GetIdentity ()->GetCryptoKeyType ();
		}; // override in LeaseSet2
	};

	class LocalDestination
	{
	public:
		virtual ~LocalDestination () { };
		virtual bool Decrypt (const uint8_t * encrypted, uint8_t * data,
			CryptoKeyType preferredCrypto = CRYPTO_KEY_TYPE_ELGAMAL) const = 0;
		virtual std::shared_ptr<const IdentityEx> GetIdentity () const = 0;

		const IdentHash & GetIdentHash () const { return GetIdentity ()->GetIdentHash (); };
		virtual bool SupportsEncryptionType (CryptoKeyType keyType) const
		{
			const auto local = GetIdentity ()->GetCryptoKeyType ();
			if (local == keyType) return true;
			// An X25519-capable local router can use the X25519 component of a
			// hybrid router identity even when the advertised type is 5-7.
			return local >= CRYPTO_KEY_TYPE_ECIES_X25519_AEAD && keyType >= CRYPTO_KEY_TYPE_ECIES_X25519_AEAD;
		}; // override for LeaseSet
		virtual const uint8_t * GetEncryptionPublicKey (CryptoKeyType /* keyType */) const
		{
			return GetIdentity ()->GetEncryptionPublicKey ();
		}; // override for LeaseSet
	};
}
}

#endif
