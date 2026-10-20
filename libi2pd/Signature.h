/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Signature.
 *
 * Declares the Verifier/Signer interfaces and concrete I2P signature types:
 * DSA, ECDSA P-256/P-384/P-521, RSA PKCS#1 v1.5 verification, Ed25519 and
 * Ed25519ph, RedDSA and GOST R 34.10, together with random-key generation
 * helpers.
 *
 */

#ifndef SIGNATURE_H__
#define SIGNATURE_H__

#include <inttypes.h>
#include <string.h>
#include <memory>
#include <openssl/dsa.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include "Crypto.h"
#include "Ed25519.h"
#include "Gost.h"

namespace i2p
{
namespace crypto
{
	// EVP_PKEY is reference counted by OpenSSL. Keep the raw accessor used by
	// the derived Ed25519ph classes while making the owner release it exactly
	// once on every constructor and Sign failure path.
	struct EVP_PKEY_Deleter
	{
		void operator() (EVP_PKEY * pkey) const { EVP_PKEY_free (pkey); }
	};
	using EVP_PKEY_ptr = std::unique_ptr<EVP_PKEY, EVP_PKEY_Deleter>;

	class Verifier
	{
	public:
		virtual ~Verifier () { };
		virtual bool Verify (const uint8_t * buf, size_t len, const uint8_t * signature) const = 0;
		virtual size_t GetPublicKeyLen () const = 0;
		virtual size_t GetSignatureLen () const = 0;
		virtual size_t GetPrivateKeyLen () const { return GetSignatureLen () / 2; };
		virtual void SetPublicKey (const uint8_t * signingKey) = 0;
	};

	class Signer
	{
	public:
		virtual ~Signer () { };
		virtual void Sign (const uint8_t * buf, int len, uint8_t * signature) const = 0;
	};

	// DSA
	const size_t DSA_PUBLIC_KEY_LENGTH = 128;
	const size_t DSA_SIGNATURE_LENGTH = 40;
	const size_t DSA_PRIVATE_KEY_LENGTH = DSA_SIGNATURE_LENGTH / 2;
	class DSAVerifier : public Verifier
	{
	public:
		DSAVerifier ();
		~DSAVerifier ();

		// implements Verifier
		void SetPublicKey (const uint8_t * signingKey) override;
		bool Verify (const uint8_t * buf, size_t len, const uint8_t * signature) const override;
		size_t GetPublicKeyLen () const override { return DSA_PUBLIC_KEY_LENGTH; };
		size_t GetSignatureLen () const override { return DSA_SIGNATURE_LENGTH; };

	private:
#ifndef LIBRESSL_VERSION_NUMBER // OpenSSL 3.0.2 is the minimum
		EVP_PKEY * m_PublicKey;
#else
		DSA * m_PublicKey;
#endif
	};

	class DSASigner : public Signer
	{
	public:
		DSASigner (const uint8_t * signingPrivateKey, const uint8_t * signingPublicKey);
		// openssl 1.1 always requires DSA public key even for signing
		~DSASigner ();

		// implements Signer
		void Sign (const uint8_t * buf, int len, uint8_t * signature) const override;

	private:
#ifndef LIBRESSL_VERSION_NUMBER // OpenSSL 3.0.2 is the minimum
		EVP_PKEY * m_PrivateKey;
#else
		DSA * m_PrivateKey;
#endif
	};

	bool CreateDSARandomKeys (uint8_t * signingPrivateKey, uint8_t * signingPublicKey);

	// ECDSA
	constexpr size_t ECDSAP256_KEY_LENGTH = 64;
	constexpr size_t ECDSAP384_KEY_LENGTH = 96;
	constexpr size_t ECDSAP521_KEY_LENGTH = 132;

#ifndef LIBRESSL_VERSION_NUMBER // OpenSSL 3.0.2 is the minimum
	class ECDSAVerifier : public Verifier
	{
	public:
		ECDSAVerifier (int curve, size_t keyLen, const EVP_MD * hash);
		~ECDSAVerifier ();

		void SetPublicKey (const uint8_t * signingKey);
		bool Verify (const uint8_t * buf, size_t len, const uint8_t * signature) const;

		size_t GetPublicKeyLen () const { return m_KeyLen; };
		size_t GetSignatureLen () const { return m_KeyLen; }; // signature length = key length

	private:
		int m_Curve;
		size_t m_KeyLen;
		const EVP_MD * m_Hash;
		EVP_PKEY * m_PublicKey;
	};

	class ECDSASigner : public Signer
	{
	public:
		ECDSASigner (int curve, size_t keyLen, const EVP_MD * hash, const uint8_t * signingPrivateKey);
		~ECDSASigner ();

		void Sign (const uint8_t * buf, int len, uint8_t * signature) const;

	private:
		size_t m_KeyLen;
		const EVP_MD * m_Hash;
		EVP_PKEY * m_PrivateKey;
	};

	bool CreateECDSARandomKeys (int curve, size_t keyLen, uint8_t * signingPrivateKey, uint8_t * signingPublicKey);

	// ECDSA_SHA256_P256
	class ECDSAP256Verifier : public ECDSAVerifier
	{
	public:
		ECDSAP256Verifier ()
			: ECDSAVerifier (NID_X9_62_prime256v1, ECDSAP256_KEY_LENGTH, EVP_sha256 ()) { };
	};

	class ECDSAP256Signer : public ECDSASigner
	{
	public:
		ECDSAP256Signer (const uint8_t * signingPrivateKey)
			: ECDSASigner (NID_X9_62_prime256v1, ECDSAP256_KEY_LENGTH, EVP_sha256 (), signingPrivateKey) { };
	};

	inline bool CreateECDSAP256RandomKeys (uint8_t * signingPrivateKey, uint8_t * signingPublicKey)
	{
		return CreateECDSARandomKeys (NID_X9_62_prime256v1, ECDSAP256_KEY_LENGTH, signingPrivateKey, signingPublicKey);
	}

	// ECDSA_SHA384_P384
	class ECDSAP384Verifier : public ECDSAVerifier
	{
	public:
		ECDSAP384Verifier ()
			: ECDSAVerifier (NID_secp384r1, ECDSAP384_KEY_LENGTH, EVP_sha384 ()) { };
	};

	class ECDSAP384Signer : public ECDSASigner
	{
	public:
		ECDSAP384Signer (const uint8_t * signingPrivateKey)
			: ECDSASigner (NID_secp384r1, ECDSAP384_KEY_LENGTH, EVP_sha384 (), signingPrivateKey) { };
	};

	inline bool CreateECDSAP384RandomKeys (uint8_t * signingPrivateKey, uint8_t * signingPublicKey)
	{
		return CreateECDSARandomKeys (NID_secp384r1, ECDSAP384_KEY_LENGTH, signingPrivateKey, signingPublicKey);
	}

	// ECDSA_SHA512_P521
	class ECDSAP521Verifier : public ECDSAVerifier
	{
	public:
		ECDSAP521Verifier ()
			: ECDSAVerifier (NID_secp521r1, ECDSAP521_KEY_LENGTH, EVP_sha512 ()) { };
	};

	class ECDSAP521Signer : public ECDSASigner
	{
	public:
		ECDSAP521Signer (const uint8_t * signingPrivateKey)
			: ECDSASigner (NID_secp521r1, ECDSAP521_KEY_LENGTH, EVP_sha512 (), signingPrivateKey) { };
	};

	inline bool CreateECDSAP521RandomKeys (uint8_t * signingPrivateKey, uint8_t * signingPublicKey)
	{
		return CreateECDSARandomKeys (NID_secp521r1, ECDSAP521_KEY_LENGTH, signingPrivateKey, signingPublicKey);
	}

#else

	struct SHA256Hash
	{
		static void CalculateHash (const uint8_t * buf, size_t len, uint8_t * digest) { SHA256 (buf, len, digest); }

		enum
		{
			hashLen = 32
		};
	};

	struct SHA384Hash
	{
		static void CalculateHash (const uint8_t * buf, size_t len, uint8_t * digest) { SHA384 (buf, len, digest); }

		enum
		{
			hashLen = 48
		};
	};

	struct SHA512Hash
	{
		static void CalculateHash (const uint8_t * buf, size_t len, uint8_t * digest) { SHA512 (buf, len, digest); }

		enum
		{
			hashLen = 64
		};
	};

	template <typename Hash, int curve, size_t keyLen> class ECDSAVerifier : public Verifier
	{
	public:
		ECDSAVerifier () { m_PublicKey = EC_KEY_new_by_curve_name (curve); }

		void SetPublicKey (const uint8_t * signingKey)
		{
			if (!m_PublicKey || !signingKey) return;
			BIGNUM * x = BN_bin2bn (signingKey, keyLen / 2, NULL);
			BIGNUM * y = BN_bin2bn (signingKey + keyLen / 2, keyLen / 2, NULL);
			if (!x || !y || EC_KEY_set_public_key_affine_coordinates (m_PublicKey, x, y) != 1)
			{
				BN_free (x);
				BN_free (y);
				return;
			}
			BN_free (x);
			BN_free (y);
		}

		~ECDSAVerifier () { EC_KEY_free (m_PublicKey); }

		bool Verify (const uint8_t * buf, size_t len, const uint8_t * signature) const
		{
			if (!m_PublicKey || !signature) return false;
			uint8_t digest[Hash::hashLen];
			Hash::CalculateHash (buf, len, digest);
			ECDSA_SIG * sig = ECDSA_SIG_new ();
			BIGNUM * r = BN_bin2bn (signature, GetSignatureLen () / 2, NULL);
			BIGNUM * s = BN_bin2bn (signature + GetSignatureLen () / 2, GetSignatureLen () / 2, NULL);
			if (!sig || !r || !s || ECDSA_SIG_set0 (sig, r, s) != 1)
			{
				BN_free (r);
				BN_free (s);
				ECDSA_SIG_free (sig);
				return false;
			}
			// ECDSA verification
			int ret = ECDSA_do_verify (digest, Hash::hashLen, sig, m_PublicKey) == 1;
			ECDSA_SIG_free (sig);
			return ret;
		}

		size_t GetPublicKeyLen () const { return keyLen; };
		size_t GetSignatureLen () const { return keyLen; }; // signature length = key length

	private:
		EC_KEY * m_PublicKey;
	};

	template <typename Hash, int curve, size_t keyLen> class ECDSASigner : public Signer
	{
	public:
		ECDSASigner (const uint8_t * signingPrivateKey)
		{
			m_PrivateKey = EC_KEY_new_by_curve_name (curve);
			if (!m_PrivateKey || !signingPrivateKey) return;
			BIGNUM * privKey = BN_bin2bn (signingPrivateKey, keyLen / 2, nullptr);
			if (privKey)
			{
				EC_KEY_set_private_key (m_PrivateKey, privKey);
				BN_free (privKey);
			}
		}

		~ECDSASigner () { EC_KEY_free (m_PrivateKey); }

		void Sign (const uint8_t * buf, int len, uint8_t * signature) const
		{
			if (!m_PrivateKey || !signature)
			{
				if (signature) memset (signature, 0, keyLen);
				return;
			}
			uint8_t digest[Hash::hashLen];
			Hash::CalculateHash (buf, len, digest);
			ECDSA_SIG * sig = ECDSA_do_sign (digest, Hash::hashLen, m_PrivateKey);
			const BIGNUM *r = nullptr, *s = nullptr;
			if (sig) ECDSA_SIG_get0 (sig, &r, &s);
			// signatureLen = keyLen
			if (!r || !s || !bn2buf (r, signature, keyLen / 2) || !bn2buf (s, signature + keyLen / 2, keyLen / 2))
				memset (signature, 0, keyLen);
			ECDSA_SIG_free (sig);
		}

	private:
		EC_KEY * m_PrivateKey;
	};

	inline bool CreateECDSARandomKeys (
		int curve, size_t keyLen, uint8_t * signingPrivateKey, uint8_t * signingPublicKey)
	{
		if (!signingPrivateKey || !signingPublicKey) return false;
		auto clear = [&] ()
		{
			memset (signingPrivateKey, 0, keyLen / 2);
			memset (signingPublicKey, 0, keyLen);
		};
		EC_KEY * signingKey = EC_KEY_new_by_curve_name (curve);
		if (!signingKey || EC_KEY_generate_key (signingKey) != 1)
		{
			EC_KEY_free (signingKey);
			clear ();
			return false;
		}
		BIGNUM *x = BN_new (), *y = BN_new ();
		bool ok = x && y && EC_KEY_get0_private_key (signingKey)
			&& bn2buf (EC_KEY_get0_private_key (signingKey), signingPrivateKey, keyLen / 2)
			&& EC_POINT_get_affine_coordinates_GFp (
				   EC_KEY_get0_group (signingKey), EC_KEY_get0_public_key (signingKey), x, y, NULL)
				== 1
			&& bn2buf (x, signingPublicKey, keyLen / 2) && bn2buf (y, signingPublicKey + keyLen / 2, keyLen / 2);
		BN_free (x);
		BN_free (y);
		EC_KEY_free (signingKey);
		if (!ok)
		{
			clear ();
			return false;
		}
		return true;
	}

	// ECDSA_SHA256_P256
	typedef ECDSAVerifier<SHA256Hash, NID_X9_62_prime256v1, ECDSAP256_KEY_LENGTH> ECDSAP256Verifier;
	typedef ECDSASigner<SHA256Hash, NID_X9_62_prime256v1, ECDSAP256_KEY_LENGTH> ECDSAP256Signer;

	inline bool CreateECDSAP256RandomKeys (uint8_t * signingPrivateKey, uint8_t * signingPublicKey)
	{
		return CreateECDSARandomKeys (NID_X9_62_prime256v1, ECDSAP256_KEY_LENGTH, signingPrivateKey, signingPublicKey);
	}

	// ECDSA_SHA384_P384
	typedef ECDSAVerifier<SHA384Hash, NID_secp384r1, ECDSAP384_KEY_LENGTH> ECDSAP384Verifier;
	typedef ECDSASigner<SHA384Hash, NID_secp384r1, ECDSAP384_KEY_LENGTH> ECDSAP384Signer;

	inline bool CreateECDSAP384RandomKeys (uint8_t * signingPrivateKey, uint8_t * signingPublicKey)
	{
		return CreateECDSARandomKeys (NID_secp384r1, ECDSAP384_KEY_LENGTH, signingPrivateKey, signingPublicKey);
	}

	// ECDSA_SHA512_P521
	typedef ECDSAVerifier<SHA512Hash, NID_secp521r1, ECDSAP521_KEY_LENGTH> ECDSAP521Verifier;
	typedef ECDSASigner<SHA512Hash, NID_secp521r1, ECDSAP521_KEY_LENGTH> ECDSAP521Signer;

	inline bool CreateECDSAP521RandomKeys (uint8_t * signingPrivateKey, uint8_t * signingPublicKey)
	{
		return CreateECDSARandomKeys (NID_secp521r1, ECDSAP521_KEY_LENGTH, signingPrivateKey, signingPublicKey);
	}

#endif

	// RSA PKCS#1 v1.5 verification (types 4/5/6). Java hashes the signed data
	// and verifies the resulting digest with NONEwithRSA, so this verifier
	// hashes once and then applies the raw PKCS#1 v1.5 operation. Signing and
	// key generation are intentionally not provided.
	class RSAVerifier : public Verifier
	{
	public:
		RSAVerifier (size_t keyLen, const EVP_MD * hash);
		~RSAVerifier ();

		void SetPublicKey (const uint8_t * signingKey) override;
		bool Verify (const uint8_t * buf, size_t len, const uint8_t * signature) const override;

		size_t GetPublicKeyLen () const override { return m_KeyLen; };
		size_t GetSignatureLen () const override { return m_KeyLen; }; // modulus-sized raw signature
		// I2P RSA private keys are n || d, so twice the modulus size.
		size_t GetPrivateKeyLen () const override { return 2 * m_KeyLen; };

	private:
		const size_t m_KeyLen;
		const EVP_MD * m_Hash;
		EVP_PKEY_ptr m_PublicKey;
	};

	// EdDSA
	class EDDSA25519Verifier : public Verifier
	{
	public:
		EDDSA25519Verifier ();
		void SetPublicKey (const uint8_t * signingKey);
		~EDDSA25519Verifier ();

		bool Verify (const uint8_t * buf, size_t len, const uint8_t * signature) const;

		size_t GetPublicKeyLen () const { return EDDSA25519_PUBLIC_KEY_LENGTH; };
		size_t GetSignatureLen () const { return EDDSA25519_SIGNATURE_LENGTH; };

	private:
		EVP_PKEY_ptr m_Pkey;

	protected:
		EVP_PKEY * GetPkey () const { return m_Pkey.get (); };
	};

	// I2P SigType 8 is named Ed25519ph, but Java's generic signer uses plain
	// Ed25519 over the supplied bytes. Keep the same key/signature layout and
	// primitive here; callers that need a prehash (for example SU3) provide it
	// explicitly. No private cryptographic primitive is implemented locally.
	class EDDSA25519phVerifier : public EDDSA25519Verifier
	{
	public:
		bool Verify (const uint8_t * buf, size_t len, const uint8_t * signature) const;
	};

	class EDDSA25519SignerCompat : public Signer
	{
	public:
		EDDSA25519SignerCompat (const uint8_t * signingPrivateKey, const uint8_t * signingPublicKey = nullptr);
		// we pass signingPublicKey to check if it matches private key
		~EDDSA25519SignerCompat ();

		void Sign (const uint8_t * buf, int len, uint8_t * signature) const;
		const uint8_t * GetPublicKey () const { return m_PublicKeyEncoded; }; // for keys creation

	private:
		uint8_t m_ExpandedPrivateKey[64];
		uint8_t m_PublicKeyEncoded[EDDSA25519_PUBLIC_KEY_LENGTH];
	};

	class EDDSA25519Signer : public Signer
	{
	public:
		EDDSA25519Signer (const uint8_t * signingPrivateKey, const uint8_t * signingPublicKey = nullptr);
		// we pass signingPublicKey to check if it matches private key
		~EDDSA25519Signer ();

		void Sign (const uint8_t * buf, int len, uint8_t * signature) const;

	protected:
		EVP_PKEY * GetPkey () const { return m_Pkey.get (); };

	private:
		EVP_PKEY_ptr m_Pkey;
		std::unique_ptr<EDDSA25519SignerCompat> m_Fallback;
	};

	class EDDSA25519phSigner : public EDDSA25519Signer
	{
	public:
		EDDSA25519phSigner (const uint8_t * signingPrivateKey);

		void Sign (const uint8_t * buf, int len, uint8_t * signature) const;
	};

	inline bool CreateEDDSA25519RandomKeys (uint8_t * signingPrivateKey, uint8_t * signingPublicKey)
	{
		if (!signingPrivateKey || !signingPublicKey) return false;
		auto clear = [&] ()
		{
			memset (signingPrivateKey, 0, EDDSA25519_PRIVATE_KEY_LENGTH);
			memset (signingPublicKey, 0, EDDSA25519_PUBLIC_KEY_LENGTH);
		};
		EVP_PKEY_CTX * pctx = EVP_PKEY_CTX_new_id (EVP_PKEY_ED25519, NULL);
		EVP_PKEY * pkey = nullptr;
		bool ok = pctx && EVP_PKEY_keygen_init (pctx) == 1 && EVP_PKEY_keygen (pctx, &pkey) == 1 && pkey;
		EVP_PKEY_CTX_free (pctx);
		if (ok)
		{
			size_t len = EDDSA25519_PUBLIC_KEY_LENGTH;
			ok = EVP_PKEY_get_raw_public_key (pkey, signingPublicKey, &len) == 1 && len == EDDSA25519_PUBLIC_KEY_LENGTH;
			len = EDDSA25519_PRIVATE_KEY_LENGTH;
			ok = ok && EVP_PKEY_get_raw_private_key (pkey, signingPrivateKey, &len) == 1
				&& len == EDDSA25519_PRIVATE_KEY_LENGTH;
		}
		EVP_PKEY_free (pkey);
		if (!ok)
		{
			clear ();
			return false;
		}
		return true;
	}

	// GOST R 34.11
	struct GOSTR3411_256_Hash
	{
		static void CalculateHash (const uint8_t * buf, size_t len, uint8_t * digest)
		{
			GOSTR3411_2012_256 (buf, len, digest);
		}

		enum
		{
			hashLen = 32
		};
	};

	struct GOSTR3411_512_Hash
	{
		static void CalculateHash (const uint8_t * buf, size_t len, uint8_t * digest)
		{
			GOSTR3411_2012_512 (buf, len, digest);
		}

		enum
		{
			hashLen = 64
		};
	};

	// GOST R 34.10
	const size_t GOSTR3410_256_PUBLIC_KEY_LENGTH = 64;
	const size_t GOSTR3410_512_PUBLIC_KEY_LENGTH = 128;

	template <typename Hash> class GOSTR3410Verifier : public Verifier
	{
	public:
		enum
		{
			keyLen = Hash::hashLen
		};

		GOSTR3410Verifier (GOSTR3410ParamSet paramSet)
			: m_ParamSet (paramSet)
			, m_PublicKey (nullptr)
		{
		}

		void SetPublicKey (const uint8_t * signingKey)
		{
			if (m_PublicKey)
			{
				EC_POINT_free (m_PublicKey);
				m_PublicKey = nullptr;
			}
			if (!signingKey) return;
			auto & curve = GetGOSTR3410Curve (m_ParamSet);
			if (!curve) return;
			BIGNUM * x = BN_bin2bn (signingKey, static_cast<int> (GetPublicKeyLen () / 2), NULL);
			BIGNUM * y
				= BN_bin2bn (signingKey + GetPublicKeyLen () / 2, static_cast<int> (GetPublicKeyLen () / 2), NULL);
			if (x && y) m_PublicKey = curve->CreatePoint (x, y);
			BN_free (x);
			BN_free (y);
		}
		~GOSTR3410Verifier ()
		{
			if (m_PublicKey) EC_POINT_free (m_PublicKey);
		}

		bool Verify (const uint8_t * buf, size_t len, const uint8_t * signature) const
		{
			if (!m_PublicKey || !signature) return false;
			auto & curve = GetGOSTR3410Curve (m_ParamSet);
			if (!curve) return false;
			uint8_t digest[Hash::hashLen];
			Hash::CalculateHash (buf, len, digest);
			BIGNUM * d = BN_bin2bn (digest, Hash::hashLen, nullptr);
			BIGNUM * r = BN_bin2bn (signature, static_cast<int> (GetSignatureLen () / 2), NULL);
			BIGNUM * s
				= BN_bin2bn (signature + GetSignatureLen () / 2, static_cast<int> (GetSignatureLen () / 2), NULL);
			bool ret = curve->Verify (m_PublicKey, d, r, s);
			BN_free (d);
			BN_free (r);
			BN_free (s);
			return ret;
		}

		size_t GetPublicKeyLen () const { return keyLen * 2; }
		size_t GetSignatureLen () const { return keyLen * 2; }

	private:
		GOSTR3410ParamSet m_ParamSet;
		EC_POINT * m_PublicKey;
	};

	template <typename Hash> class GOSTR3410Signer : public Signer
	{
	public:
		enum
		{
			keyLen = Hash::hashLen
		};

		GOSTR3410Signer (GOSTR3410ParamSet paramSet, const uint8_t * signingPrivateKey)
			: m_ParamSet (paramSet)
		{
			m_PrivateKey = BN_bin2bn (signingPrivateKey, keyLen, nullptr);
		}
		~GOSTR3410Signer () { BN_free (m_PrivateKey); }

		void Sign (const uint8_t * buf, int len, uint8_t * signature) const
		{
			uint8_t digest[Hash::hashLen];
			Hash::CalculateHash (buf, len, digest);
			BIGNUM * d = BN_bin2bn (digest, Hash::hashLen, nullptr);
			BIGNUM *r = BN_new (), *s = BN_new ();
			auto & curve = GetGOSTR3410Curve (m_ParamSet);
			if (m_PrivateKey && d && r && s && curve)
			{
				curve->Sign (m_PrivateKey, d, r, s);
				if (!BN_is_zero (r) && !BN_is_zero (s) && bn2buf (r, signature, keyLen)
					&& bn2buf (s, signature + keyLen, keyLen))
				{
					BN_free (d);
					BN_free (r);
					BN_free (s);
					return;
				}
			}
			// Never emit the all-zero signature that the verifier must reject.
			memset (signature, 0, keyLen * 2);
			BN_free (d);
			BN_free (r);
			BN_free (s);
		}

	private:
		GOSTR3410ParamSet m_ParamSet;
		BIGNUM * m_PrivateKey;
	};

	inline bool CreateGOSTR3410RandomKeys (
		GOSTR3410ParamSet paramSet, uint8_t * signingPrivateKey, uint8_t * signingPublicKey)
	{
		if (!signingPrivateKey || !signingPublicKey) return false;
		auto & curve = GetGOSTR3410Curve (paramSet);
		if (!curve) return false;
		auto keyLen = curve->GetKeyLen ();
		auto clear = [&] ()
		{
			memset (signingPrivateKey, 0, keyLen);
			memset (signingPublicKey, 0, keyLen * 2);
		};
		if (RAND_bytes (signingPrivateKey, static_cast<int> (keyLen)) != 1)
		{
			clear ();
			return false;
		}
		BN_CTX * ctx = BN_CTX_new ();
		BIGNUM * priv = BN_bin2bn (signingPrivateKey, static_cast<int> (keyLen), nullptr);
		BIGNUM * q = BN_new ();
		if (!ctx || !priv || !q || EC_GROUP_get_order (curve->GetGroup (), q, ctx) != 1)
		{
			BN_free (q);
			BN_free (priv);
			BN_CTX_free (ctx);
			clear ();
			return false;
		}
		// A GOST private key is in [1, q-1].
		bool normalised = BN_mod (priv, priv, q, ctx) == 1;
		if (normalised && BN_is_zero (priv)) normalised = BN_one (priv) == 1;
		normalised = normalised && bn2buf (priv, signingPrivateKey, keyLen);
		BN_free (q);
		BN_CTX_free (ctx);
		if (!normalised)
		{
			BN_free (priv);
			clear ();
			return false;
		}

		auto pub = curve->MulP (priv);
		BN_free (priv);
		BIGNUM *x = BN_new (), *y = BN_new ();
		bool ok = pub && x && y && curve->GetXY (pub, x, y) && bn2buf (x, signingPublicKey, keyLen)
			&& bn2buf (y, signingPublicKey + keyLen, keyLen);
		if (pub) EC_POINT_free (pub);
		BN_free (x);
		BN_free (y);
		if (!ok)
		{
			clear ();
			return false;
		}
		return true;
	}

	typedef GOSTR3410Verifier<GOSTR3411_256_Hash> GOSTR3410_256_Verifier;
	typedef GOSTR3410Signer<GOSTR3411_256_Hash> GOSTR3410_256_Signer;
	typedef GOSTR3410Verifier<GOSTR3411_512_Hash> GOSTR3410_512_Verifier;
	typedef GOSTR3410Signer<GOSTR3411_512_Hash> GOSTR3410_512_Signer;

	// RedDSA
	typedef EDDSA25519Verifier RedDSA25519Verifier;
	class RedDSA25519Signer : public Signer
	{
	public:
		RedDSA25519Signer (const uint8_t * signingPrivateKey)
		{
			memcpy (m_PrivateKey, signingPrivateKey, EDDSA25519_PRIVATE_KEY_LENGTH);
			BN_CTX * ctx = BN_CTX_new ();
			auto publicKey = GetEd25519 ()->GeneratePublicKey (m_PrivateKey, ctx);
			GetEd25519 ()->EncodePublicKey (publicKey, m_PublicKeyEncoded, ctx);
			BN_CTX_free (ctx);
		}
		~RedDSA25519Signer () { OPENSSL_cleanse (m_PrivateKey, sizeof (m_PrivateKey)); };

		void Sign (const uint8_t * buf, int len, uint8_t * signature) const
		{
			GetEd25519 ()->SignRedDSA (m_PrivateKey, m_PublicKeyEncoded, buf, len, signature);
		}

		const uint8_t * GetPublicKey () const { return m_PublicKeyEncoded; }; // for keys creation

	private:
		uint8_t m_PrivateKey[EDDSA25519_PRIVATE_KEY_LENGTH];
		uint8_t m_PublicKeyEncoded[EDDSA25519_PUBLIC_KEY_LENGTH];
	};

	inline bool CreateRedDSA25519RandomKeys (uint8_t * signingPrivateKey, uint8_t * signingPublicKey)
	{
		if (!signingPrivateKey || !signingPublicKey) return false;
		if (!GetEd25519 ()->CreateRedDSAPrivateKey (signingPrivateKey))
		{
			memset (signingPrivateKey, 0, EDDSA25519_PRIVATE_KEY_LENGTH);
			memset (signingPublicKey, 0, EDDSA25519_PUBLIC_KEY_LENGTH);
			return false;
		}
		RedDSA25519Signer signer (signingPrivateKey);
		memcpy (signingPublicKey, signer.GetPublicKey (), EDDSA25519_PUBLIC_KEY_LENGTH);
		return true;
	}
}
}

#endif
