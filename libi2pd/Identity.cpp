/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Identity.
 *
 * Implements standard and extended I2P identities: key and option parsing,
 * serialisation, signature verification for the supported signing types,
 * offline-signature sections and b33 offline keys, plus private-key
 * generation, signing and decryption. Untrusted key blobs and offline
 * signatures are length-checked and expiry-validated before use.
 *
 */

#include "Crypto.h"
#include "I2PEndian.h"
#include "Log.h"
#include "Timestamp.h"
#include "CryptoKey.h"
#include "Blinding.h"
#include "Identity.h"

namespace i2p
{
namespace data
{
	std::vector<IdentHash> ExtractIdentHashes (std::string_view hashes)
	{
		std::vector<IdentHash> idents;
		if (!hashes.empty ())
		{
			size_t pos = 0, comma;
			do
			{
				comma = hashes.find (',', pos);
				i2p::data::IdentHash ident;
				if (ident.FromBase64 (
						hashes.substr (pos, comma != std::string_view::npos ? comma - pos : std::string_view::npos)))
					idents.push_back (ident);
				pos = comma + 1;
			} while (comma != std::string_view::npos);
		}
		return idents;
	}

	namespace
	{
		struct SigningKeySizes
		{
			size_t publicKey;
			size_t privateKey;
			size_t signature;
		};

		// Wire lengths from the I2P common-structures specification and
		// SigType.java. They are independent of whether this build has a
		// verifier for the type: an identity still has a defined layout even
		// when its signatures cannot be checked locally.
		bool GetSigningKeySizes (SigningKeyType type, SigningKeySizes & sizes)
		{
			switch (type)
			{
			case SIGNING_KEY_TYPE_DSA_SHA1: sizes = { 128, 20, 40 }; return true;
			case SIGNING_KEY_TYPE_ECDSA_SHA256_P256: sizes = { 64, 32, 64 }; return true;
			case SIGNING_KEY_TYPE_ECDSA_SHA384_P384: sizes = { 96, 48, 96 }; return true;
			case SIGNING_KEY_TYPE_ECDSA_SHA512_P521: sizes = { 132, 66, 132 }; return true;
			case SIGNING_KEY_TYPE_RSA_SHA256_2048: sizes = { 256, 512, 256 }; return true;
			case SIGNING_KEY_TYPE_RSA_SHA384_3072: sizes = { 384, 768, 384 }; return true;
			case SIGNING_KEY_TYPE_RSA_SHA512_4096: sizes = { 512, 1024, 512 }; return true;
			case SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519: sizes = { 32, 32, 64 }; return true;
			case SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519ph: sizes = { 32, 32, 64 }; return true;
			case SIGNING_KEY_TYPE_GOSTR3410_CRYPTO_PRO_A_GOSTR3411_256: sizes = { 64, 32, 64 }; return true;
			case SIGNING_KEY_TYPE_GOSTR3410_TC26_A_512_GOSTR3411_512: sizes = { 128, 64, 128 }; return true;
			case SIGNING_KEY_TYPE_REDDSA_SHA512_ED25519: sizes = { 32, 32, 64 }; return true;
			default: return false;
			}
		}
	} // namespace

	Identity & Identity::operator= (const Keys & keys)
	{
		// copy public and signing keys together
		memcpy (publicKey, keys.publicKey, sizeof (publicKey));
		memcpy (signingKey, keys.signingKey, sizeof (signingKey));
		memset (certificate, 0, sizeof (certificate));
		return *this;
	}

	size_t Identity::FromBuffer (const uint8_t * buf, size_t len)
	{
		if (len < DEFAULT_IDENTITY_SIZE) return 0; // buffer too small, don't overflow
		memcpy (this, buf, DEFAULT_IDENTITY_SIZE);
		return DEFAULT_IDENTITY_SIZE;
	}

	IdentHash Identity::Hash () const
	{
		IdentHash hash;
		SHA256 ((const uint8_t *)this, DEFAULT_IDENTITY_SIZE, hash);
		return hash;
	}

	IdentityEx::IdentityEx ()
		: m_ExtendedLen (0)
	{
		m_IdentHash.Fill (0);
	}

	IdentityEx::IdentityEx (
		const uint8_t * publicKey, const uint8_t * signingKey, SigningKeyType type, CryptoKeyType cryptoType)
		: m_ExtendedLen (0)
		, m_IsValid (true)
	{
		uint8_t randomPaddingBlock[32] { };
		RAND_bytes (randomPaddingBlock, 32);
		const bool shortEncryptionKey = cryptoType == CRYPTO_KEY_TYPE_ECIES_X25519_AEAD
			|| cryptoType == CRYPTO_KEY_TYPE_ECIES_MLKEM512_X25519_AEAD
			|| cryptoType == CRYPTO_KEY_TYPE_ECIES_MLKEM768_X25519_AEAD
			|| cryptoType == CRYPTO_KEY_TYPE_ECIES_MLKEM1024_X25519_AEAD;
		if (shortEncryptionKey)
		{
			memcpy (m_StandardIdentity.publicKey, publicKey ? publicKey : randomPaddingBlock, 32);
			for (int i = 0; i < 7; i++) // 224 bytes
				memcpy (m_StandardIdentity.publicKey + 32 * (i + 1), randomPaddingBlock, 32);
		}
		else
		{
			if (publicKey)
				memcpy (m_StandardIdentity.publicKey, publicKey, 256);
			else
				for (int i = 0; i < 8; i++) // 256 bytes
					memcpy (m_StandardIdentity.publicKey + 32 * i, randomPaddingBlock, 32);
		}
		if (type != SIGNING_KEY_TYPE_DSA_SHA1)
		{
			size_t excessLen = 0;
			std::unique_ptr<uint8_t[]> excessBuf;
			switch (type)
			{
			case SIGNING_KEY_TYPE_ECDSA_SHA256_P256:
			{
				size_t padding = 128 - i2p::crypto::ECDSAP256_KEY_LENGTH; // 64 = 128 - 64
				RAND_bytes (m_StandardIdentity.signingKey, static_cast<int> (padding));
				memcpy (m_StandardIdentity.signingKey + padding, signingKey, i2p::crypto::ECDSAP256_KEY_LENGTH);
				break;
			}
			case SIGNING_KEY_TYPE_ECDSA_SHA384_P384:
			{
				size_t padding = 128 - i2p::crypto::ECDSAP384_KEY_LENGTH; // 32 = 128 - 96
				RAND_bytes (m_StandardIdentity.signingKey, static_cast<int> (padding));
				memcpy (m_StandardIdentity.signingKey + padding, signingKey, i2p::crypto::ECDSAP384_KEY_LENGTH);
				break;
			}
			case SIGNING_KEY_TYPE_ECDSA_SHA512_P521:
			{
				memcpy (m_StandardIdentity.signingKey, signingKey, 128);
				excessLen = i2p::crypto::ECDSAP521_KEY_LENGTH - 128; // 4 = 132 - 128
				excessBuf = std::make_unique<uint8_t[]> (excessLen);
				memcpy (excessBuf.get (), signingKey + 128, excessLen);
				break;
			}
			case SIGNING_KEY_TYPE_RSA_SHA256_2048:
			case SIGNING_KEY_TYPE_RSA_SHA384_3072:
			case SIGNING_KEY_TYPE_RSA_SHA512_4096:
			{
				SigningKeySizes sizes { };
				// RSA-4096 is the largest defined signing public key (512
				// bytes). Bound the excess explicitly so the allocation and
				// copy below cannot be widened by a malformed size value.
				if (!GetSigningKeySizes (type, sizes) || sizes.publicKey <= 128 || sizes.publicKey > 512) break;
				memcpy (m_StandardIdentity.signingKey, signingKey, 128);
				excessLen = sizes.publicKey - 128;
				excessBuf = std::make_unique<uint8_t[]> (excessLen);
				memcpy (excessBuf.get (), signingKey + 128, excessLen);
				break;
			}
			case SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519:
			case SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519ph:
			case SIGNING_KEY_TYPE_REDDSA_SHA512_ED25519:
			{
				size_t padding = 128 - i2p::crypto::EDDSA25519_PUBLIC_KEY_LENGTH; // 96 = 128 - 32
				for (int i = 0; i < 3; i++) // 96 bytes
					memcpy (m_StandardIdentity.signingKey + 32 * i, randomPaddingBlock, 32);
				memcpy (m_StandardIdentity.signingKey + padding, signingKey, i2p::crypto::EDDSA25519_PUBLIC_KEY_LENGTH);
				break;
			}
			case SIGNING_KEY_TYPE_GOSTR3410_CRYPTO_PRO_A_GOSTR3411_256:
			{
				// 256
				size_t padding = 128 - i2p::crypto::GOSTR3410_256_PUBLIC_KEY_LENGTH; // 64 = 128 - 64
				RAND_bytes (m_StandardIdentity.signingKey, static_cast<int> (padding));
				memcpy (
					m_StandardIdentity.signingKey + padding, signingKey, i2p::crypto::GOSTR3410_256_PUBLIC_KEY_LENGTH);
				break;
			}
			case SIGNING_KEY_TYPE_GOSTR3410_TC26_A_512_GOSTR3411_512:
			{
				// 512
				// no padding, key length is 128
				memcpy (m_StandardIdentity.signingKey, signingKey, i2p::crypto::GOSTR3410_512_PUBLIC_KEY_LENGTH);
				break;
			}
			default: LogPrint (eLogError, "Identity: Signing key type ", (int)type, " is not supported");
			}
			// The largest defined signing key excess is RSA-4096's 384
			// bytes. Keep the derived allocation explicitly bounded so a
			// compromised or future size table cannot widen this copy.
			const size_t extendedLen = 4 + excessLen;
			if (extendedLen > 4 + 384)
			{
				m_IsValid = false;
				m_ExtendedLen = 0;
				return;
			}
			m_ExtendedLen = extendedLen; // 4 bytes extra + excess length
			// fill certificate
			m_StandardIdentity.certificate[0] = CERTIFICATE_TYPE_KEY;
			htobe16buf (m_StandardIdentity.certificate + 1, static_cast<uint16_t> (m_ExtendedLen));
			// fill extended buffer
			htobe16buf (m_ExtendedBuffer, type);
			htobe16buf (m_ExtendedBuffer + 2, cryptoType);
			if (excessLen && excessBuf)
			{
				if (m_ExtendedLen > MAX_EXTENDED_BUFFER_SIZE)
				{
					auto newBuf = std::make_unique<uint8_t[]> (extendedLen);
					memcpy (newBuf.get (), m_ExtendedBuffer, 4);
					memcpy (newBuf.get () + 4, excessBuf.get (), excessLen);
					m_ExtendedBufferPtr = std::move (newBuf);
				}
				else
					memcpy (m_ExtendedBuffer + 4, excessBuf.get (), excessLen);
			}
			// calculate ident hash
			RecalculateIdentHash ();
		}
		else // DSA-SHA1
		{
			memcpy (m_StandardIdentity.signingKey, signingKey, sizeof (m_StandardIdentity.signingKey));
			if (cryptoType == CRYPTO_KEY_TYPE_ELGAMAL)
			{
				// A null certificate means DSA-SHA1 + ElGamal.
				memset (m_StandardIdentity.certificate, 0, sizeof (m_StandardIdentity.certificate));
				m_IdentHash = m_StandardIdentity.Hash ();
				m_ExtendedLen = 0;
			}
			else
			{
				// A non-default crypto type needs a key certificate even for
				// DSA-SHA1. The legacy null certificate cannot express it.
				m_ExtendedLen = 4;
				m_StandardIdentity.certificate[0] = CERTIFICATE_TYPE_KEY;
				htobe16buf (m_StandardIdentity.certificate + 1, static_cast<uint16_t> (m_ExtendedLen));
				htobe16buf (m_ExtendedBuffer, type);
				htobe16buf (m_ExtendedBuffer + 2, cryptoType);
				RecalculateIdentHash ();
			}
		}
		CreateVerifier ();
	}

	void IdentityEx::RecalculateIdentHash (uint8_t * buf)
	{
		if (!m_IsValid)
		{
			m_IdentHash.Fill (0);
			return;
		}
		size_t sz = GetFullLen ();
		std::unique_ptr<uint8_t[]> tmp;
		if (!buf)
		{
			tmp = std::make_unique<uint8_t[]> (sz);
			buf = tmp.get ();
		}
		ToBuffer (buf, sz);
		SHA256 (buf, sz, m_IdentHash);
	}

	IdentityEx::IdentityEx (const uint8_t * buf, size_t len)
		: m_ExtendedLen (0)
	{
		FromBuffer (buf, len);
	}

	IdentityEx::IdentityEx (const IdentityEx & other)
		: m_ExtendedLen (0)
	{
		*this = other;
	}

	IdentityEx::IdentityEx (const Identity & standard)
		: m_ExtendedLen (0)
	{
		*this = standard;
	}

	IdentityEx::~IdentityEx () = default;

	IdentityEx & IdentityEx::operator= (const IdentityEx & other)
	{
		if (this == &other) return *this;

		// Copy the owned extended part first, so a failed allocation leaves
		// this object unchanged.
		std::unique_ptr<uint8_t[]> extended;
		if (other.m_IsValid && other.m_ExtendedLen > MAX_EXTENDED_BUFFER_SIZE && other.m_ExtendedBufferPtr)
		{
			extended = std::make_unique<uint8_t[]> (other.m_ExtendedLen);
			memcpy (extended.get (), other.m_ExtendedBufferPtr.get (), other.m_ExtendedLen);
		}

		memcpy (&m_StandardIdentity, &other.m_StandardIdentity, DEFAULT_IDENTITY_SIZE);
		m_IdentHash = other.m_IdentHash;
		m_ExtendedLen = other.m_ExtendedLen;
		m_IsValid = other.m_IsValid;
		if (m_ExtendedLen > MAX_EXTENDED_BUFFER_SIZE)
		{
			m_ExtendedBufferPtr = std::move (extended);
			if (!m_ExtendedBufferPtr) m_IsValid = false;
		}
		else
		{
			m_ExtendedBufferPtr.reset ();
			if (m_ExtendedLen > 0) memcpy (m_ExtendedBuffer, other.m_ExtendedBuffer, m_ExtendedLen);
		}
		m_Verifier = nullptr;
		CreateVerifier ();

		return *this;
	}

	IdentityEx & IdentityEx::operator= (const Identity & standard)
	{
		m_StandardIdentity = standard;
		m_IdentHash = m_StandardIdentity.Hash ();
		m_ExtendedLen = 0;
		m_ExtendedBufferPtr.reset ();
		memset (m_ExtendedBuffer, 0, sizeof (m_ExtendedBuffer));
		m_IsValid = true;

		m_Verifier = nullptr;
		CreateVerifier ();

		return *this;
	}

	size_t IdentityEx::FromBuffer (const uint8_t * buf, size_t len)
	{
		m_IsValid = false;
		m_ExtendedLen = 0;
		m_ExtendedBufferPtr.reset ();
		memset (m_ExtendedBuffer, 0, sizeof (m_ExtendedBuffer));
		m_IdentHash.Fill (0);
		m_Verifier = nullptr;

		if (!buf || len < DEFAULT_IDENTITY_SIZE)
		{
			LogPrint (eLogError, "Identity: Buffer length ", len, " is too small");
			return 0;
		}
		memcpy (&m_StandardIdentity, buf, DEFAULT_IDENTITY_SIZE);

		size_t extendedLen = bufbe16toh (m_StandardIdentity.certificate + 1);
		if (m_StandardIdentity.certificate[0] == CERTIFICATE_TYPE_KEY && extendedLen < 4)
		{
			// A key certificate always starts with the two signature and two
			// crypto key type bytes. Java's KeyCertificate rejects a shorter
			// payload; without this check the identity was silently treated as
			// a DSA identity.
			LogPrint (eLogError, "Identity: Key certificate length ", extendedLen, " is too small");
			return 0;
		}
		if (extendedLen)
		{
			if (extendedLen > len - DEFAULT_IDENTITY_SIZE)
			{
				LogPrint (eLogError, "Identity: Certificate length ", extendedLen, " exceeds buffer length ",
					len - DEFAULT_IDENTITY_SIZE);
				return 0;
			}
			if (extendedLen > MAX_EXTENDED_BUFFER_SIZE)
			{
				auto extended = std::make_unique<uint8_t[]> (extendedLen);
				memcpy (extended.get (), buf + DEFAULT_IDENTITY_SIZE, extendedLen);
				m_ExtendedBufferPtr = std::move (extended);
			}
			else
				memcpy (m_ExtendedBuffer, buf + DEFAULT_IDENTITY_SIZE, extendedLen);
			m_ExtendedLen = extendedLen;
		}

		if (m_StandardIdentity.certificate[0] == CERTIFICATE_TYPE_KEY)
		{
			// KeyCertificate layout: sig type, crypto type, then excess signing
			// key bytes (and optional excess crypto key bytes). Validate the
			// signing part before any verifier reads it. Java's
			// SigningPublicKey.toTypedKey() additionally rejects any excess data
			// when the crypto type is ElGamal (0).
			const uint8_t * extended = GetExtendedBuffer ();
			SigningKeyType signingType = bufbe16toh (extended);
			CryptoKeyType cryptoType = bufbe16toh (extended + 2);
			SigningKeySizes sizes { };
			if (GetSigningKeySizes (signingType, sizes))
			{
				size_t signingExtra = sizes.publicKey > 128 ? sizes.publicKey - 128 : 0;
				if (extendedLen < 4 + signingExtra)
				{
					LogPrint (eLogError, "Identity: Key certificate length ", extendedLen,
						" is too short for signing key type ", (int)signingType);
					m_ExtendedLen = 0;
					m_ExtendedBufferPtr.reset ();
					return 0;
				}
				if (cryptoType == CRYPTO_KEY_TYPE_ELGAMAL && extendedLen != 4 + signingExtra)
				{
					LogPrint (
						eLogError, "Identity: Key certificate has excess data for crypto key type ", (int)cryptoType);
					m_ExtendedLen = 0;
					m_ExtendedBufferPtr.reset ();
					return 0;
				}
			}
		}

		const size_t fullLen = DEFAULT_IDENTITY_SIZE + m_ExtendedLen;
		SHA256 (buf, fullLen, m_IdentHash);
		m_IsValid = true;

		CreateVerifier ();

		return fullLen;
	}

	size_t IdentityEx::ToBuffer (uint8_t * buf, size_t len) const
	{
		const size_t fullLen = GetFullLen ();
		if (!fullLen || !buf || fullLen > len) return 0; // invalid or buffer is too small
		memcpy (buf, &m_StandardIdentity, DEFAULT_IDENTITY_SIZE);
		if (m_ExtendedLen > 0)
		{
			if (m_ExtendedLen > MAX_EXTENDED_BUFFER_SIZE)
				memcpy (buf + DEFAULT_IDENTITY_SIZE, m_ExtendedBufferPtr.get (), m_ExtendedLen);
			else
				memcpy (buf + DEFAULT_IDENTITY_SIZE, m_ExtendedBuffer, m_ExtendedLen);
		}
		return fullLen;
	}

	size_t IdentityEx::FromBase64 (std::string_view s)
	{
		std::vector<uint8_t> buf (s.length ()); // binary data can't exceed base64
		auto len = Base64ToByteStream (s, buf.data (), buf.size ());
		return FromBuffer (buf.data (), len);
	}

	std::string IdentityEx::ToBase64 () const
	{
		const size_t bufLen = GetFullLen ();
		std::vector<uint8_t> buf (bufLen);
		size_t l = ToBuffer (buf.data (), bufLen);
		return i2p::data::ByteStreamToBase64 (buf.data (), l);
	}

	size_t IdentityEx::GetSigningPublicKeyLen () const
	{
		if (m_Verifier) return m_Verifier->GetPublicKeyLen ();
		SigningKeySizes sizes { };
		if (GetSigningKeySizes (GetSigningKeyType (), sizes)) return sizes.publicKey;
		return 128;
	}

	const uint8_t * IdentityEx::GetSigningPublicKeyBuffer () const
	{
		auto keyLen = GetSigningPublicKeyLen ();
		if (keyLen > 128) return nullptr; // P521, RSA or PQ
		return m_StandardIdentity.signingKey + 128 - keyLen;
	}

	size_t IdentityEx::GetSigningPrivateKeyLen () const
	{
		if (m_Verifier) return m_Verifier->GetPrivateKeyLen ();
		SigningKeySizes sizes { };
		if (GetSigningKeySizes (GetSigningKeyType (), sizes)) return sizes.privateKey;
		return GetSignatureLen () / 2;
	}

	size_t IdentityEx::GetSignatureLen () const
	{
		if (m_Verifier) return m_Verifier->GetSignatureLen ();
		SigningKeySizes sizes { };
		if (GetSigningKeySizes (GetSigningKeyType (), sizes)) return sizes.signature;
		return i2p::crypto::DSA_SIGNATURE_LENGTH;
	}
	bool IdentityEx::Verify (const uint8_t * buf, size_t len, const uint8_t * signature) const
	{
		if (m_Verifier) return m_Verifier->Verify (buf, len, signature);
		return false;
	}

	SigningKeyType IdentityEx::GetSigningKeyType () const
	{
		if (m_StandardIdentity.certificate[0] == CERTIFICATE_TYPE_KEY && m_ExtendedLen >= 2)
			return bufbe16toh (GetExtendedBuffer ()); // signing key
		return SIGNING_KEY_TYPE_DSA_SHA1;
	}

	bool IdentityEx::IsRSA () const
	{
		auto sigType = GetSigningKeyType ();
		return sigType <= SIGNING_KEY_TYPE_RSA_SHA512_4096 && sigType >= SIGNING_KEY_TYPE_RSA_SHA256_2048;
	}

	CryptoKeyType IdentityEx::GetCryptoKeyType () const
	{
		if (m_StandardIdentity.certificate[0] == CERTIFICATE_TYPE_KEY && m_ExtendedLen >= 4)
			return bufbe16toh (GetExtendedBuffer () + 2); // crypto key
		return CRYPTO_KEY_TYPE_ELGAMAL;
	}

	i2p::crypto::Verifier * IdentityEx::CreateVerifier (SigningKeyType keyType)
	{
		switch (keyType)
		{
		case SIGNING_KEY_TYPE_DSA_SHA1: return new i2p::crypto::DSAVerifier ();
		case SIGNING_KEY_TYPE_ECDSA_SHA256_P256: return new i2p::crypto::ECDSAP256Verifier ();
		case SIGNING_KEY_TYPE_ECDSA_SHA384_P384: return new i2p::crypto::ECDSAP384Verifier ();
		case SIGNING_KEY_TYPE_ECDSA_SHA512_P521: return new i2p::crypto::ECDSAP521Verifier ();
		case SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519: return new i2p::crypto::EDDSA25519Verifier ();
		case SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519ph: return new i2p::crypto::EDDSA25519phVerifier ();
		case SIGNING_KEY_TYPE_GOSTR3410_CRYPTO_PRO_A_GOSTR3411_256:
			return new i2p::crypto::GOSTR3410_256_Verifier (i2p::crypto::eGOSTR3410CryptoProA);
		case SIGNING_KEY_TYPE_GOSTR3410_TC26_A_512_GOSTR3411_512:
			return new i2p::crypto::GOSTR3410_512_Verifier (i2p::crypto::eGOSTR3410TC26A512);
		case SIGNING_KEY_TYPE_REDDSA_SHA512_ED25519: return new i2p::crypto::RedDSA25519Verifier ();
		case SIGNING_KEY_TYPE_RSA_SHA256_2048: return new i2p::crypto::RSAVerifier (256, EVP_sha256 ());
		case SIGNING_KEY_TYPE_RSA_SHA384_3072: return new i2p::crypto::RSAVerifier (384, EVP_sha384 ());
		case SIGNING_KEY_TYPE_RSA_SHA512_4096: return new i2p::crypto::RSAVerifier (512, EVP_sha512 ());
		default: LogPrint (eLogError, "Identity: Signing key type ", (int)keyType, " is not supported");
		}
		return nullptr;
	}

	void IdentityEx::CreateVerifier ()
	{
		if (!m_IsValid)
		{
			m_Verifier = nullptr;
			return;
		}
		if (!m_Verifier)
		{
			std::unique_ptr<i2p::crypto::Verifier> verifier (CreateVerifier (GetSigningKeyType ()));
			if (verifier)
			{
				auto keyLen = verifier->GetPublicKeyLen ();
				const auto keyType = GetSigningKeyType ();
				const bool isRSA
					= keyType >= SIGNING_KEY_TYPE_RSA_SHA256_2048 && keyType <= SIGNING_KEY_TYPE_RSA_SHA512_4096;
				if (keyLen <= 128)
					verifier->SetPublicKey (m_StandardIdentity.signingKey + 128 - keyLen);
				else if (isRSA)
				{
					// RSA public keys are the modulus: 128 bytes in the
					// standard identity and the remainder in the key
					// certificate, like P-521. Handle this before the PQ
					// branch, which uses a different layout.
					std::vector<uint8_t> signingKey (keyLen);
					memcpy (signingKey.data (), m_StandardIdentity.signingKey, 128);
					size_t excessLen = keyLen - 128;
					const uint8_t * extended = GetExtendedBuffer ();
					if (!extended || m_ExtendedLen < 4 + excessLen)
					{
						LogPrint (
							eLogError, "Identity: Key certificate has no room for RSA signing key type ", (int)keyType);
						m_Verifier = nullptr;
						return;
					}
					memcpy (signingKey.data () + 128, extended + 4, excessLen);
					verifier->SetPublicKey (signingKey.data ());
				}
#if OPENSSL_PQ
				else if (keyLen > 384)
				{
					// for post-quantum
					size_t excessLen = keyLen - 384;
					const uint8_t * extended = GetExtendedBuffer ();
					if (!extended || m_ExtendedLen < 4 + excessLen)
					{
						LogPrint (eLogError, "Identity: Key certificate has no room for signing key type ",
							(int)GetSigningKeyType ());
						return;
					}
					std::vector<uint8_t> signingKey (keyLen);
					memcpy (signingKey.data (), m_StandardIdentity, 384);
					memcpy (signingKey.data () + 384, extended + 4,
						excessLen); // right after signing and crypto key types
					verifier->SetPublicKey (signingKey.data ());
				}
#endif
				else
				{
					// for P521
					std::vector<uint8_t> signingKey (keyLen);
					memcpy (signingKey.data (), m_StandardIdentity.signingKey, 128);
					size_t excessLen = keyLen - 128;
					const uint8_t * extended = GetExtendedBuffer ();
					if (!extended || m_ExtendedLen < 4 + excessLen)
					{
						LogPrint (eLogError, "Identity: Key certificate has no room for signing key type ",
							(int)GetSigningKeyType ());
						m_Verifier = nullptr;
						return;
					}
					memcpy (signingKey.data () + 128, extended + 4,
						excessLen); // right after signing and crypto key types
					verifier->SetPublicKey (signingKey.data ());
				}
			}
			m_Verifier = std::move (verifier);
		}
	}

	std::shared_ptr<i2p::crypto::CryptoKeyEncryptor> IdentityEx::CreateEncryptor (
		CryptoKeyType keyType, const uint8_t * key)
	{
		switch (keyType)
		{
		case CRYPTO_KEY_TYPE_ELGAMAL: return std::make_shared<i2p::crypto::ElGamalEncryptor> (key); break;
		case CRYPTO_KEY_TYPE_ECIES_X25519_AEAD:
		case CRYPTO_KEY_TYPE_ECIES_MLKEM512_X25519_AEAD:
		case CRYPTO_KEY_TYPE_ECIES_MLKEM768_X25519_AEAD:
		case CRYPTO_KEY_TYPE_ECIES_MLKEM1024_X25519_AEAD:
			return std::make_shared<i2p::crypto::ECIESX25519AEADRatchetEncryptor> (key);
			break;
		case CRYPTO_KEY_TYPE_ECIES_P256_SHA256_AES256CBC:
			return std::make_shared<i2p::crypto::ECIESP256Encryptor> (key);
			break;
		default: LogPrint (eLogError, "Identity: Unknown crypto key type ", (int)keyType);
		};
		return nullptr;
	}

	std::shared_ptr<i2p::crypto::CryptoKeyEncryptor> IdentityEx::CreateEncryptor (const uint8_t * key) const
	{
		if (!key) key = GetEncryptionPublicKey (); // use publicKey
		return CreateEncryptor (GetCryptoKeyType (), key);
	}

	size_t GetIdentityBufferLen (const uint8_t * buf, size_t len)
	{
		if (len < DEFAULT_IDENTITY_SIZE) return 0;
		size_t l = DEFAULT_IDENTITY_SIZE + bufbe16toh (buf + DEFAULT_IDENTITY_SIZE - 2);
		if (l > len) return 0;
		return l;
	}

	size_t B33OfflineKeys::FromBuffer (const uint8_t * buf, size_t len, const IdentHash & ident)
	{
		m_Buf.clear ();
		if (len < B33_OFFLINE_KEYS_HEADER_LENGTH || buf[0] != B33_OFFLINE_KEYS_VERSION
			|| memcmp (buf + 1, ident, IdentHash::len))
		{
			LogPrint (
				eLogWarning, "Identity: ", len, " bytes behind the keys are not b33 offline keys of this destination");
			return 0;
		}
		m_Buf.assign (buf, buf + len);
		return len;
	}

	size_t B33OfflineKeys::ToBuffer (uint8_t * buf, size_t len) const
	{
		if (m_Buf.empty () || !buf || m_Buf.size () > len) return 0;
		memcpy (buf, m_Buf.data (), m_Buf.size ());
		return m_Buf.size ();
	}

	OfflinePrivateKeys::OfflinePrivateKeys (const PrivateKeys & keys, const char * date)
		: PrivateKeys (keys)
	{
		const uint8_t * buf = GetB33OfflineKeys ().GetBuffer ();
		size_t len = GetB33OfflineKeys ().GetLen ();
		if (!len) return;
		BlindedPublicKey blindedKey (GetPublic ());
		std::unique_ptr<i2p::crypto::Verifier> blindedVerifier (
			IdentityEx::CreateVerifier (blindedKey.GetBlindedSigType ()));
		uint8_t blindedPub[i2p::crypto::EDDSA25519_PUBLIC_KEY_LENGTH]; // 32 max
		if (!blindedVerifier || !blindedKey.GetBlindedKey (date, blindedPub)) return;
		blindedVerifier->SetPublicKey (blindedPub);
		uint16_t numKeys = bufbe16toh (buf + B33_OFFLINE_KEYS_HEADER_LENGTH - 2);
		size_t offset = B33_OFFLINE_KEYS_HEADER_LENGTH;
		for (uint16_t i = 0; i < numKeys && offset + OFFLINE_SIGNATURE_HEADER_LENGTH < len; i++)
		{
			const uint8_t * key = buf + offset;
			SigningKeyType transientSigType = bufbe16toh (key + 4);
			std::unique_ptr<i2p::crypto::Verifier> transientVerifier (IdentityEx::CreateVerifier (transientSigType));
			if (!transientVerifier) break;
			size_t signedLen = OFFLINE_SIGNATURE_HEADER_LENGTH + transientVerifier->GetPublicKeyLen ();
			size_t offlineSignatureLen = signedLen + blindedVerifier->GetSignatureLen ();
			if (offset + offlineSignatureLen + transientVerifier->GetPrivateKeyLen () > len) break;
			char keyDate[9];
			// a key is for the day its signature expires at the end of
			uint32_t keyExpires = bufbe32toh (key);
			if (keyExpires < SECONDS_PER_DAY)
			{
				offset += offlineSignatureLen + transientVerifier->GetPrivateKeyLen ();
				continue;
			}
			i2p::util::GetDateString (keyExpires - SECONDS_PER_DAY, keyDate);
			if (!strncmp (keyDate, date, 8))
			{
				if (!blindedVerifier->Verify (key, signedLen, key + signedLen))
				{
					LogPrint (eLogError, "Identity: b33 offline key for ", date,
						" is not signed by the blinded key of that day");
					return;
				}
				m_Signer.reset (CreateSigner (transientSigType, key + offlineSignatureLen));
				if (!m_Signer) return;
				m_SignatureLen = transientVerifier->GetSignatureLen ();
				m_OfflineSignature.assign (key, key + offlineSignatureLen);
				m_TransientPrivateKey
					= std::span<const uint8_t> (key + offlineSignatureLen, transientVerifier->GetPrivateKeyLen ());
				return;
			}
			offset += offlineSignatureLen + transientVerifier->GetPrivateKeyLen ();
		}
		LogPrint (eLogError, "Identity: No b33 offline key for ", date);
	}

	PrivateKeys & PrivateKeys::operator= (const Keys & keys)
	{
		m_Public = std::make_shared<IdentityEx> (Identity (keys));
		memcpy (m_PrivateKey, keys.privateKey, 256); // 256
		size_t keyLen = m_Public->GetSigningPrivateKeyLen ();
		m_SigningPrivateKey.resize (keyLen);
		memcpy (m_SigningPrivateKey.data (), keys.signingPrivateKey, keyLen);
		m_OfflineSignature.resize (0);
		m_TransientSignatureLen = 0;
		m_TransientSigningPrivateKeyLen = 0;
		m_B33OfflineKeys = B33OfflineKeys ();
		m_Signer = nullptr;
		CreateSigner ();
		return *this;
	}

	PrivateKeys & PrivateKeys::operator= (const PrivateKeys & other)
	{
		if (!other.m_Public)
		{
			m_Public = nullptr;
			m_Signer = nullptr;
			m_OfflineSignature.clear ();
			m_SigningPrivateKey.clear ();
			m_TransientSignatureLen = 0;
			m_TransientSigningPrivateKeyLen = 0;
			m_B33OfflineKeys = B33OfflineKeys ();
			memset (m_PrivateKey, 0, sizeof (m_PrivateKey));
			return *this;
		}
		m_Public = std::make_shared<IdentityEx> (*other.m_Public);
		memcpy (m_PrivateKey, other.m_PrivateKey, 256); // 256
		m_OfflineSignature = other.m_OfflineSignature;
		m_TransientSignatureLen = other.m_TransientSignatureLen;
		m_TransientSigningPrivateKeyLen = other.m_TransientSigningPrivateKeyLen;
		m_B33OfflineKeys = other.m_B33OfflineKeys;
		m_SigningPrivateKey = other.m_SigningPrivateKey;
		m_Signer = nullptr;
		CreateSigner ();
		return *this;
	}

	size_t PrivateKeys::GetFullLen () const
	{
		if (!m_Public) return 0;
		size_t ret = m_Public->GetFullLen () + GetPrivateKeyLen () + m_Public->GetSigningPrivateKeyLen ();
		if (IsOfflineSignature ()) ret += m_OfflineSignature.size () + m_TransientSigningPrivateKeyLen;
		return ret + m_B33OfflineKeys.GetLen ();
	}

	size_t PrivateKeys::FromBuffer (const uint8_t * buf, size_t len)
	{
		m_Public = nullptr;
		m_Signer = nullptr;
		m_OfflineSignature.clear ();
		m_SigningPrivateKey.clear ();
		m_TransientSignatureLen = 0;
		m_TransientSigningPrivateKeyLen = 0;
		m_B33OfflineKeys = B33OfflineKeys ();
		memset (m_PrivateKey, 0, sizeof (m_PrivateKey));

		auto fail = [this] ()
		{
			m_Public = nullptr;
			m_Signer = nullptr;
			m_OfflineSignature.clear ();
			m_SigningPrivateKey.clear ();
			m_TransientSignatureLen = 0;
			m_TransientSigningPrivateKeyLen = 0;
			m_B33OfflineKeys = B33OfflineKeys ();
			memset (m_PrivateKey, 0, sizeof (m_PrivateKey));
			return static_cast<size_t> (0);
		};

		if (!buf) return 0;
		auto pub = std::make_shared<IdentityEx> ();
		size_t ret = pub->FromBuffer (buf, len);
		if (!ret) return 0;
		m_Public = std::move (pub);

		auto cryptoKeyLen = GetPrivateKeyLen ();
		if (!cryptoKeyLen)
		{
			LogPrint (eLogError, "Identity: Unsupported crypto key type ", (int)m_Public->GetCryptoKeyType (),
				" in private keys");
			return fail ();
		}
		if (ret > len || cryptoKeyLen > len - ret) return fail ();
		memcpy (m_PrivateKey, buf + ret, cryptoKeyLen);
		ret += cryptoKeyLen;
		size_t signingPrivateKeySize = m_Public->GetSigningPrivateKeyLen ();
		if (ret > len || signingPrivateKeySize > len - ret) return fail ();
		m_SigningPrivateKey.resize (signingPrivateKeySize);
		memcpy (m_SigningPrivateKey.data (), buf + ret, signingPrivateKeySize);
		ret += signingPrivateKeySize;
		m_Signer = nullptr;
		// check if signing private key is all zeros
		bool allzeros = true;
		for (size_t i = 0; i < signingPrivateKeySize; i++)
			if (m_SigningPrivateKey[i])
			{
				allzeros = false;
				break;
			}
		if (allzeros)
		{
			// offline information
			if (len - ret < OFFLINE_SIGNATURE_HEADER_LENGTH) return fail (); // expires(4) + key type(2)
			const uint8_t * offlineInfo = buf + ret;
			uint32_t expires = bufbe32toh (buf + ret);
			ret += 4; // expires timestamp
			if (expires < i2p::util::GetSecondsSinceEpoch ())
			{
				LogPrint (eLogError, "Identity: Offline signature expired");
				return fail ();
			}
			SigningKeyType keyType = bufbe16toh (buf + ret);
			ret += 2; // key type
			std::unique_ptr<i2p::crypto::Verifier> transientVerifier (IdentityEx::CreateVerifier (keyType));
			if (!transientVerifier) return fail ();
			auto keyLen = transientVerifier->GetPublicKeyLen ();
			if (ret > len || keyLen > len - ret) return fail ();
			transientVerifier->SetPublicKey (buf + ret);
			ret += keyLen;
			auto offlineSignatureLen = m_Public->GetSignatureLen ();
			if (ret > len || offlineSignatureLen > len - ret) return fail ();
			if (!m_Public->Verify (offlineInfo, keyLen + 6, buf + ret))
			{
				LogPrint (eLogError, "Identity: Offline signature verification failed");
				return fail ();
			}
			ret += offlineSignatureLen;
			m_TransientSignatureLen = transientVerifier->GetSignatureLen ();
			// copy offline signature
			size_t offlineInfoLen = buf + ret - offlineInfo;
			m_OfflineSignature.resize (offlineInfoLen);
			memcpy (m_OfflineSignature.data (), offlineInfo, offlineInfoLen);
			// override signing private key
			m_TransientSigningPrivateKeyLen = transientVerifier->GetPrivateKeyLen ();
			if (ret > len || m_TransientSigningPrivateKeyLen > len - ret) return fail ();
			if (m_TransientSigningPrivateKeyLen > m_SigningPrivateKey.size ())
				m_SigningPrivateKey.resize (m_TransientSigningPrivateKeyLen);
			memcpy (m_SigningPrivateKey.data (), buf + ret, m_TransientSigningPrivateKeyLen);
			ret += m_TransientSigningPrivateKeyLen;
			CreateSigner (keyType);
			if (!m_Signer) return fail ();
		}
		else
			CreateSigner (m_Public->GetSigningKeyType ());
		if (ret < len)
		{
			size_t b33Len = m_B33OfflineKeys.FromBuffer (buf + ret, len - ret, m_Public->GetIdentHash ());
			if (b33Len) ret += b33Len;
		}
		return ret;
	}

	size_t PrivateKeys::ToBuffer (uint8_t * buf, size_t len) const
	{
		if (!m_Public || !buf) return 0;
		size_t ret = m_Public->ToBuffer (buf, len);
		if (!ret) return 0;
		auto cryptoKeyLen = GetPrivateKeyLen ();
		if (!cryptoKeyLen || ret > len || cryptoKeyLen > len - ret) return 0;
		memcpy (buf + ret, m_PrivateKey, cryptoKeyLen);
		ret += cryptoKeyLen;
		size_t signingPrivateKeySize = m_Public->GetSigningPrivateKeyLen ();
		if (ret > len || signingPrivateKeySize > len - ret) return 0;
		if (IsOfflineSignature ())
			memset (buf + ret, 0, signingPrivateKeySize);
		else
		{
			if (signingPrivateKeySize > m_SigningPrivateKey.size ()) return 0; // private key is not set
			memcpy (buf + ret, m_SigningPrivateKey.data (), signingPrivateKeySize);
		}
		ret += signingPrivateKeySize;
		if (IsOfflineSignature ())
		{
			// offline signature
			auto offlineSignatureLen = m_OfflineSignature.size ();
			if (ret > len || offlineSignatureLen > len - ret) return 0;
			memcpy (buf + ret, m_OfflineSignature.data (), offlineSignatureLen);
			ret += offlineSignatureLen;
			// transient private key
			if (ret > len || m_TransientSigningPrivateKeyLen > len - ret
				|| m_TransientSigningPrivateKeyLen > m_SigningPrivateKey.size ())
				return 0;
			memcpy (buf + ret, m_SigningPrivateKey.data (), m_TransientSigningPrivateKeyLen);
			ret += m_TransientSigningPrivateKeyLen;
		}
		if (m_B33OfflineKeys.GetLen () && !m_B33OfflineKeys.ToBuffer (buf + ret, len - ret)) return 0;
		ret += m_B33OfflineKeys.GetLen ();
		return ret;
	}

	size_t PrivateKeys::FromBase64 (std::string_view s)
	{
		std::vector<uint8_t> buf (s.length ());
		size_t l = i2p::data::Base64ToByteStream (s, buf.data (), buf.size ());
		return FromBuffer (buf.data (), l);
	}

	std::string PrivateKeys::ToBase64 () const
	{
		std::vector<uint8_t> buf (GetFullLen ());
		size_t l = ToBuffer (buf.data (), buf.size ());
		return i2p::data::ByteStreamToBase64 (buf.data (), l);
	}

	void PrivateKeys::Sign (const uint8_t * buf, int len, uint8_t * signature) const
	{
		if (!m_Public || !signature) return;
		if (!m_Signer) CreateSigner ();
		if (!m_Signer)
		{
			// Unsupported signing type (for example RSA, or a malformed
			// identity). Never dereference a null
			// signer or emit uninitialised signature bytes.
			LogPrint (eLogError, "Identity: No signer for signing key type ", (int)m_Public->GetSigningKeyType ());
			memset (signature, 0, GetSignatureLen ());
			return;
		}
		m_Signer->Sign (buf, len, signature);
	}

	void PrivateKeys::UpdateOfflineSignature (const PrivateKeys & other)
	{
		// same identity (m_Public): refresh only the transient material and the signer
		m_SigningPrivateKey = other.m_SigningPrivateKey;
		m_OfflineSignature = other.m_OfflineSignature;
		m_TransientSignatureLen = other.m_TransientSignatureLen;
		m_TransientSigningPrivateKeyLen = other.m_TransientSigningPrivateKeyLen;
		m_B33OfflineKeys = other.m_B33OfflineKeys;
		m_Signer = nullptr;
		CreateSigner ();
	}

	void PrivateKeys::CreateSigner () const
	{
		if (!m_Public) return;
		if (IsOfflineSignature ())
			CreateSigner (bufbe16toh (m_OfflineSignature.data () + 4)); // key type
		else
			CreateSigner (m_Public->GetSigningKeyType ());
	}

	void PrivateKeys::CreateSigner (SigningKeyType keyType) const
	{
		if (!m_Public || m_Signer) return;
		SigningKeySizes sizes { };
		if (!GetSigningKeySizes (keyType, sizes) || m_SigningPrivateKey.size () < sizes.privateKey)
		{
			LogPrint (eLogError, "Identity: Signing private key is missing or truncated for type ", (int)keyType);
			return;
		}
		if (keyType == SIGNING_KEY_TYPE_DSA_SHA1)
			m_Signer.reset (
				new i2p::crypto::DSASigner (m_SigningPrivateKey.data (), m_Public->GetStandardIdentity ().signingKey));
		else if (keyType == SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519 && !IsOfflineSignature ())
			m_Signer.reset (new i2p::crypto::EDDSA25519Signer (m_SigningPrivateKey.data (),
				m_Public->GetStandardIdentity ().signingKey
					+ (sizeof (Identity::signingKey)
						- i2p::crypto::EDDSA25519_PUBLIC_KEY_LENGTH))); // TODO: remove public key check
		else
		{
			// public key is not required
			auto signer = CreateSigner (keyType, m_SigningPrivateKey.data ());
			if (signer) m_Signer.reset (signer);
		}
	}

	i2p::crypto::Signer * PrivateKeys::CreateSigner (SigningKeyType keyType, const uint8_t * priv)
	{
		switch (keyType)
		{
		case SIGNING_KEY_TYPE_ECDSA_SHA256_P256: return new i2p::crypto::ECDSAP256Signer (priv); break;
		case SIGNING_KEY_TYPE_ECDSA_SHA384_P384: return new i2p::crypto::ECDSAP384Signer (priv); break;
		case SIGNING_KEY_TYPE_ECDSA_SHA512_P521: return new i2p::crypto::ECDSAP521Signer (priv); break;
		case SIGNING_KEY_TYPE_RSA_SHA256_2048:
		case SIGNING_KEY_TYPE_RSA_SHA384_3072:
		case SIGNING_KEY_TYPE_RSA_SHA512_4096:
			LogPrint (eLogError, "Identity: RSA signing key type ", (int)keyType,
				" is verification-only; no RSA signer is available");
			break;
		case SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519: return new i2p::crypto::EDDSA25519Signer (priv, nullptr); break;
		case SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519ph: return new i2p::crypto::EDDSA25519phSigner (priv); break;
		case SIGNING_KEY_TYPE_GOSTR3410_CRYPTO_PRO_A_GOSTR3411_256:
			return new i2p::crypto::GOSTR3410_256_Signer (i2p::crypto::eGOSTR3410CryptoProA, priv);
			break;
		case SIGNING_KEY_TYPE_GOSTR3410_TC26_A_512_GOSTR3411_512:
			return new i2p::crypto::GOSTR3410_512_Signer (i2p::crypto::eGOSTR3410TC26A512, priv);
			break;
		case SIGNING_KEY_TYPE_REDDSA_SHA512_ED25519: return new i2p::crypto::RedDSA25519Signer (priv); break;
		default: LogPrint (eLogError, "Identity: Signing key type ", (int)keyType, " is not supported");
		}
		return nullptr;
	}

	size_t PrivateKeys::GetSignatureLen () const
	{
		if (!m_Public) return 0;
		return IsOfflineSignature () ? m_TransientSignatureLen : m_Public->GetSignatureLen ();
	}

	size_t PrivateKeys::GetPrivateKeyLen () const
	{
		if (!m_Public) return 0;
		return i2p::crypto::GetCryptoPrivateKeyLen (m_Public->GetCryptoKeyType ());
	}

	uint8_t * PrivateKeys::GetPadding ()
	{
		if (!m_Public) return nullptr;
		if (m_Public->GetSigningKeyType () == SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519)
			return m_Public->GetEncryptionPublicKeyBuffer () + 256;
		else
			return nullptr; // TODO: implement me
	}

	std::shared_ptr<i2p::crypto::CryptoKeyDecryptor> PrivateKeys::CreateDecryptor (const uint8_t * key) const
	{
		if (!m_Public) return nullptr;
		if (!key) key = m_PrivateKey; // use privateKey
		return CreateDecryptor (m_Public->GetCryptoKeyType (), key);
	}

	std::shared_ptr<i2p::crypto::CryptoKeyDecryptor> PrivateKeys::CreateDecryptor (
		CryptoKeyType cryptoType, const uint8_t * key)
	{
		if (!key) return nullptr;
		switch (cryptoType)
		{
		case CRYPTO_KEY_TYPE_ELGAMAL: return std::make_shared<i2p::crypto::ElGamalDecryptor> (key); break;
		case CRYPTO_KEY_TYPE_ECIES_X25519_AEAD:
		case CRYPTO_KEY_TYPE_ECIES_MLKEM512_X25519_AEAD:
		case CRYPTO_KEY_TYPE_ECIES_MLKEM768_X25519_AEAD:
		case CRYPTO_KEY_TYPE_ECIES_MLKEM1024_X25519_AEAD:
			return std::make_shared<i2p::crypto::ECIESX25519AEADRatchetDecryptor> (key);
			break;
		case CRYPTO_KEY_TYPE_ECIES_P256_SHA256_AES256CBC:
			return std::make_shared<i2p::crypto::ECIESP256Decryptor> (key);
			break;
		default: LogPrint (eLogError, "Identity: Unknown crypto key type ", (int)cryptoType);
		};
		return nullptr;
	}

	PrivateKeys PrivateKeys::CreateRandomKeys (SigningKeyType type, CryptoKeyType cryptoType, bool isDestination)
	{
		// A certificate must not name a crypto type whose key layout is
		// unknown: private-key parsing would consume zero bytes and the
		// identity would be unusable. Fall back to the default instead of
		// advertising an unsupported type.
		if (i2p::crypto::GetCryptoPublicKeyLen (cryptoType) == 0)
		{
			LogPrint (eLogError, "Identity: Crypto key type ", (int)cryptoType, " is not supported. Use ElGamal");
			cryptoType = CRYPTO_KEY_TYPE_ELGAMAL;
		}
		if (type != SIGNING_KEY_TYPE_DSA_SHA1)
		{
			PrivateKeys keys;
			// signature
			std::unique_ptr<i2p::crypto::Verifier> verifier (IdentityEx::CreateVerifier (type));
			if (!verifier)
			{
				LogPrint (eLogError, "Identity: Can't create keys with signing key type ", (int)type, ". Use DSA-SHA1");
				return PrivateKeys (i2p::data::CreateRandomKeys ()); // DSA-SHA1
			}
			std::vector<uint8_t> signingPublicKey (verifier->GetPublicKeyLen ());
			keys.m_SigningPrivateKey.resize (verifier->GetPrivateKeyLen ());
			if (!GenerateSigningKeyPair (type, keys.m_SigningPrivateKey.data (), signingPublicKey.data ()))
			{
				LogPrint (eLogError, "Identity: Can't generate signing keys of type ", (int)type, ". Use DSA-SHA1");
				return PrivateKeys (i2p::data::CreateRandomKeys ()); // DSA-SHA1
			}
			// encryption
			uint8_t publicKey[256] = { };
			if (isDestination)
			{
				if (RAND_bytes (keys.m_PrivateKey, 256) != 1)
					return PrivateKeys (i2p::data::CreateRandomKeys ()); // DSA-SHA1
			}
			else if (!GenerateCryptoKeyPair (cryptoType, keys.m_PrivateKey, publicKey))
			{
				LogPrint (
					eLogError, "Identity: Can't generate crypto keys of type ", (int)cryptoType, ". Use DSA-SHA1");
				return PrivateKeys (i2p::data::CreateRandomKeys ()); // DSA-SHA1
			}
			// identity
			keys.m_Public = std::make_shared<IdentityEx> (
				isDestination ? nullptr : publicKey, signingPublicKey.data (), type, cryptoType);

			keys.CreateSigner ();
			return keys;
		}
		if (cryptoType == CRYPTO_KEY_TYPE_ELGAMAL)
			return PrivateKeys (i2p::data::CreateRandomKeys ()); // legacy DSA-SHA1 + ElGamal

		// DSA-SHA1 with a non-ElGamal encryption type needs a key certificate
		// so that the signing and crypto types are both represented.
		PrivateKeys keys;
		keys.m_SigningPrivateKey.resize (i2p::crypto::DSA_PRIVATE_KEY_LENGTH);
		uint8_t signingPublicKey[i2p::crypto::DSA_PUBLIC_KEY_LENGTH] { };
		if (!i2p::crypto::CreateDSARandomKeys (keys.m_SigningPrivateKey.data (), signingPublicKey))
		{
			LogPrint (eLogError, "Identity: Can't generate DSA-SHA1 signing keys. Use ElGamal");
			return PrivateKeys (i2p::data::CreateRandomKeys ());
		}
		uint8_t publicKey[256] { };
		if (isDestination)
		{
			if (RAND_bytes (keys.m_PrivateKey, 256) != 1) return PrivateKeys (i2p::data::CreateRandomKeys ());
		}
		else if (!GenerateCryptoKeyPair (cryptoType, keys.m_PrivateKey, publicKey))
			return PrivateKeys (i2p::data::CreateRandomKeys ());
		keys.m_Public = std::make_shared<IdentityEx> (
			isDestination ? nullptr : publicKey, signingPublicKey, SIGNING_KEY_TYPE_DSA_SHA1, cryptoType);
		keys.CreateSigner ();
		return keys;
	}

	bool PrivateKeys::GenerateSigningKeyPair (SigningKeyType type, uint8_t * priv, uint8_t * pub)
	{
		auto fail = [&] ()
		{
			SigningKeySizes sizes { };
			if (priv && pub && GetSigningKeySizes (type, sizes))
			{
				memset (priv, 0, sizes.privateKey);
				memset (pub, 0, sizes.publicKey);
			}
			return false;
		};
		switch (type)
		{
		case SIGNING_KEY_TYPE_ECDSA_SHA256_P256: return i2p::crypto::CreateECDSAP256RandomKeys (priv, pub);
		case SIGNING_KEY_TYPE_ECDSA_SHA384_P384: return i2p::crypto::CreateECDSAP384RandomKeys (priv, pub);
		case SIGNING_KEY_TYPE_ECDSA_SHA512_P521: return i2p::crypto::CreateECDSAP521RandomKeys (priv, pub);
		case SIGNING_KEY_TYPE_RSA_SHA256_2048:
		case SIGNING_KEY_TYPE_RSA_SHA384_3072:
		case SIGNING_KEY_TYPE_RSA_SHA512_4096:
			// RSA verification is implemented, but RSA signing/key generation
			// is not. Do not label an EdDSA key as RSA: the certificate and
			// the key would disagree.
			LogPrint (eLogError, "Identity: RSA signature type ", (int)type,
				" supports verification only; key generation is not available");
			return fail ();
		case SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519: return i2p::crypto::CreateEDDSA25519RandomKeys (priv, pub);
		case SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519ph: return i2p::crypto::CreateEDDSA25519RandomKeys (priv, pub);
		case SIGNING_KEY_TYPE_GOSTR3410_CRYPTO_PRO_A_GOSTR3411_256:
			return i2p::crypto::CreateGOSTR3410RandomKeys (i2p::crypto::eGOSTR3410CryptoProA, priv, pub);
		case SIGNING_KEY_TYPE_GOSTR3410_TC26_A_512_GOSTR3411_512:
			return i2p::crypto::CreateGOSTR3410RandomKeys (i2p::crypto::eGOSTR3410TC26A512, priv, pub);
		case SIGNING_KEY_TYPE_REDDSA_SHA512_ED25519: return i2p::crypto::CreateRedDSA25519RandomKeys (priv, pub);
		default: LogPrint (eLogError, "Identity: Signing key type ", (int)type, " is not supported"); return fail ();
		}
	}

	bool PrivateKeys::GenerateCryptoKeyPair (CryptoKeyType type, uint8_t * priv, uint8_t * pub)
	{
		switch (type)
		{
		case CRYPTO_KEY_TYPE_ELGAMAL: i2p::crypto::GenerateElGamalKeyPair (priv, pub); return true;
		case CRYPTO_KEY_TYPE_ECIES_P256_SHA256_AES256CBC:
			i2p::crypto::CreateECIESP256RandomKeys (priv, pub);
			return true;
		case CRYPTO_KEY_TYPE_ECIES_X25519_AEAD:
		case CRYPTO_KEY_TYPE_ECIES_MLKEM512_X25519_AEAD:
		case CRYPTO_KEY_TYPE_ECIES_MLKEM768_X25519_AEAD:
		case CRYPTO_KEY_TYPE_ECIES_MLKEM1024_X25519_AEAD:
			i2p::crypto::CreateECIESX25519AEADRatchetRandomKeys (priv, pub);
			return true;
		default: LogPrint (eLogError, "Identity: Crypto key type ", (int)type, " is not supported");
		}
		return false;
	}

	PrivateKeys PrivateKeys::CreateOfflineKeys (SigningKeyType type, uint32_t expires) const
	{
		if (!m_Public) return PrivateKeys ();
		if (!m_Signer) CreateSigner ();
		if (!m_Signer)
		{
			LogPrint (eLogError, "Identity: No signer for the destination signing key type ",
				(int)m_Public->GetSigningKeyType ());
			return PrivateKeys (*this);
		}
		PrivateKeys keys (*this);
		std::unique_ptr<i2p::crypto::Verifier> verifier (IdentityEx::CreateVerifier (type));
		if (verifier)
		{
			size_t pubKeyLen = verifier->GetPublicKeyLen ();
			keys.m_TransientSigningPrivateKeyLen = verifier->GetPrivateKeyLen ();
			keys.m_TransientSignatureLen = verifier->GetSignatureLen ();
			keys.m_OfflineSignature.resize (pubKeyLen + m_Public->GetSignatureLen () + 6);
			keys.m_SigningPrivateKey.resize (verifier->GetPrivateKeyLen ());
			htobe32buf (keys.m_OfflineSignature.data (), expires); // expires
			htobe16buf (keys.m_OfflineSignature.data () + 4, type); // type
			if (!GenerateSigningKeyPair (
					type, keys.m_SigningPrivateKey.data (), keys.m_OfflineSignature.data () + 6)) // public key
			{
				LogPrint (eLogError, "Identity: Can't generate offline signing keys");
				keys.m_SigningPrivateKey = m_SigningPrivateKey;
				keys.m_OfflineSignature.clear ();
				keys.m_TransientSignatureLen = 0;
				keys.m_TransientSigningPrivateKeyLen = 0;
				keys.m_Signer = nullptr;
				keys.CreateSigner ();
				return keys;
			}
			Sign (keys.m_OfflineSignature.data (), static_cast<int> (pubKeyLen + 6),
				keys.m_OfflineSignature.data () + 6 + pubKeyLen); // signature
			if (!m_Public->Verify (
					keys.m_OfflineSignature.data (), pubKeyLen + 6, keys.m_OfflineSignature.data () + 6 + pubKeyLen))
			{
				LogPrint (eLogError, "Identity: Generated offline signature does not verify");
				keys.m_SigningPrivateKey = m_SigningPrivateKey;
				keys.m_OfflineSignature.clear ();
				keys.m_TransientSignatureLen = 0;
				keys.m_TransientSigningPrivateKeyLen = 0;
				keys.m_Signer = nullptr;
				keys.CreateSigner ();
				return keys;
			}
			// recreate signer
			keys.m_Signer = nullptr;
			keys.CreateSigner (type);
			if (!keys.m_Signer)
			{
				LogPrint (eLogError, "Identity: Can't create offline signer");
				keys.m_OfflineSignature.clear ();
				keys.m_TransientSignatureLen = 0;
				keys.m_TransientSigningPrivateKeyLen = 0;
				keys.m_SigningPrivateKey = m_SigningPrivateKey;
				keys.CreateSigner ();
			}
		}
		return keys;
	}

	Keys CreateRandomKeys ()
	{
		Keys keys;
		// encryption
		i2p::crypto::GenerateElGamalKeyPair (keys.privateKey, keys.publicKey);
		// signing
		i2p::crypto::CreateDSARandomKeys (keys.signingPrivateKey, keys.signingKey);
		return keys;
	}
}
}
