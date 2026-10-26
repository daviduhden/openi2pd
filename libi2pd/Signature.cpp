/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Signature.
 *
 * Implements the I2P signature suites, including ECDSA DER-to-fixed signature
 * conversion, raw RSA PKCS#1 v1.5 verification, Ed25519/Ed25519ph and RedDSA
 * handling, and GOST R 34.10 variants, with OpenSSL 3 and LibreSSL code paths
 * and scoped EVP resource ownership.
 *
 */

#include <memory>
#include <vector>
#include <openssl/evp.h>
#include <openssl/crypto.h>
#ifndef LIBRESSL_VERSION_NUMBER // OpenSSL 3.0.2 is the minimum
#include <openssl/core_names.h>
#include <openssl/param_build.h>
#endif
#include <cstring>
#include "Log.h"
#include "Signature.h"

namespace
{
// Digest contexts owned by the verifier/signer entry points. OpenSSL's
// EVP_MD_CTX is release-only, so a scoped owner removes the early-return
// and exception paths that previously leaked it.
struct MDCtxDeleter
{
	void operator() (EVP_MD_CTX * ctx) const { EVP_MD_CTX_free (ctx); }
};

using MDCtxPtr = std::unique_ptr<EVP_MD_CTX, MDCtxDeleter>;

// Encode an ECDSA_SIG into a buffer that is sized from the DER length
// reported by OpenSSL. A fixed keyLen + 8 buffer is too small for P-521:
// two 66-byte integers with their high bit set need 141 bytes, not 140.
[[maybe_unused]] bool EncodeDSASignature (const DSA_SIG * sig, std::vector<uint8_t> & der)
{
	if (!sig) return false;
	const int derLen = i2d_DSA_SIG (sig, nullptr);
	if (derLen <= 0) return false;
	der.resize (static_cast<size_t> (derLen));
	uint8_t * out = der.data ();
	const int written = i2d_DSA_SIG (sig, &out);
	return written > 0 && written <= derLen && static_cast<size_t> (written) == der.size ();
}

[[maybe_unused]] bool EncodeECDSASignature (const ECDSA_SIG * sig, std::vector<uint8_t> & der)
{
	if (!sig) return false;
	const int derLen = i2d_ECDSA_SIG (sig, nullptr);
	if (derLen <= 0) return false;
	der.resize (static_cast<size_t> (derLen));
	uint8_t * out = der.data ();
	const int written = i2d_ECDSA_SIG (sig, &out);
	return written > 0 && written <= derLen && static_cast<size_t> (written) == der.size ();
}
}

namespace i2p
{
namespace crypto
{
#ifndef LIBRESSL_VERSION_NUMBER // OpenSSL 3.0.2 is the minimum
	DSAVerifier::DSAVerifier ()
		: m_PublicKey (nullptr)
	{
	}

	DSAVerifier::~DSAVerifier ()
	{
		if (m_PublicKey) EVP_PKEY_free (m_PublicKey);
	}

	void DSAVerifier::SetPublicKey (const uint8_t * signingKey)
	{
		if (m_PublicKey)
		{
			EVP_PKEY_free (m_PublicKey);
			m_PublicKey = nullptr;
		}
		if (!signingKey) return;
		BIGNUM * pub = BN_bin2bn (signingKey, DSA_PUBLIC_KEY_LENGTH, NULL);
		if (!pub) return;
		m_PublicKey = CreateDSA (pub);
		BN_free (pub);
		if (!m_PublicKey) LogPrint (eLogError, "DSA can't create public key");
	}

	bool DSAVerifier::Verify (const uint8_t * buf, size_t len, const uint8_t * signature) const
	{
		if (!m_PublicKey || !signature) return false;
		// signature
		DSA_SIG * sig = DSA_SIG_new ();
		BIGNUM * r = BN_bin2bn (signature, DSA_SIGNATURE_LENGTH / 2, NULL);
		BIGNUM * s = BN_bin2bn (signature + DSA_SIGNATURE_LENGTH / 2, DSA_SIGNATURE_LENGTH / 2, NULL);
		if (!sig || !r || !s || DSA_SIG_set0 (sig, r, s) != 1)
		{
			BN_free (r);
			BN_free (s);
			DSA_SIG_free (sig);
			return false;
		}
		// to DER format, sized exactly from the encoder
		std::vector<uint8_t> sign;
		const bool encoded = EncodeDSASignature (sig, sign);
		DSA_SIG_free (sig);
		if (!encoded || sign.empty ()) return false;
		// verify
		MDCtxPtr ctx (EVP_MD_CTX_new ());
		return ctx && EVP_DigestVerifyInit (ctx.get (), NULL, EVP_sha1 (), NULL, m_PublicKey) == 1
			&& EVP_DigestVerify (ctx.get (), sign.data (), sign.size (), buf, len) == 1;
	}

	DSASigner::DSASigner (const uint8_t * signingPrivateKey, const uint8_t * /* signingPublicKey */)
		: m_PrivateKey (nullptr)
	{
		if (!signingPrivateKey) return;
		BIGNUM * priv = BN_bin2bn (signingPrivateKey, DSA_PRIVATE_KEY_LENGTH, NULL);
		if (!priv) return;
		m_PrivateKey = CreateDSA (nullptr, priv);
		BN_free (priv);
		if (!m_PrivateKey) LogPrint (eLogError, "DSA can't create private key");
	}

	DSASigner::~DSASigner ()
	{
		if (m_PrivateKey) EVP_PKEY_free (m_PrivateKey);
	}

	void DSASigner::Sign (const uint8_t * buf, int len, uint8_t * signature) const
	{
		if (!m_PrivateKey || !signature)
		{
			if (signature) memset (signature, 0, DSA_SIGNATURE_LENGTH);
			return;
		}
		uint8_t sign[DSA_SIGNATURE_LENGTH + 8];
		size_t l = sizeof (sign);
		MDCtxPtr ctx (EVP_MD_CTX_new ());
		bool ok = ctx && EVP_DigestSignInit (ctx.get (), NULL, EVP_sha1 (), NULL, m_PrivateKey) == 1
			&& EVP_DigestSign (ctx.get (), sign, &l, buf, len) == 1 && l > 0 && l <= sizeof (sign);
		// decode r and s
		DSA_SIG * sig = nullptr;
		if (ok)
		{
			const uint8_t * s1 = sign;
			sig = d2i_DSA_SIG (NULL, &s1, static_cast<long> (l));
		}
		const BIGNUM *r = nullptr, *s = nullptr;
		if (sig) DSA_SIG_get0 (sig, &r, &s);
		if (!r || !s || !bn2buf (r, signature, DSA_SIGNATURE_LENGTH / 2)
			|| !bn2buf (s, signature + DSA_SIGNATURE_LENGTH / 2, DSA_SIGNATURE_LENGTH / 2))
		{
			memset (signature, 0, DSA_SIGNATURE_LENGTH);
			LogPrint (eLogError, "DSA signing failed");
		}
		DSA_SIG_free (sig);
	}

	bool CreateDSARandomKeys (uint8_t * signingPrivateKey, uint8_t * signingPublicKey)
	{
		if (!signingPrivateKey || !signingPublicKey) return false;
		auto clear = [&] ()
		{
			memset (signingPrivateKey, 0, DSA_PRIVATE_KEY_LENGTH);
			memset (signingPublicKey, 0, DSA_PUBLIC_KEY_LENGTH);
		};
		EVP_PKEY * paramskey = CreateDSA ();
		EVP_PKEY_CTX * ctx = paramskey ? EVP_PKEY_CTX_new_from_pkey (NULL, paramskey, NULL) : nullptr;
		EVP_PKEY * pkey = nullptr;
		bool ok = ctx && EVP_PKEY_keygen_init (ctx) == 1 && EVP_PKEY_keygen (ctx, &pkey) == 1 && pkey;
		BIGNUM *pub = NULL, *priv = NULL;
		ok = ok && EVP_PKEY_get_bn_param (pkey, OSSL_PKEY_PARAM_PUB_KEY, &pub) == 1 && pub
			&& EVP_PKEY_get_bn_param (pkey, OSSL_PKEY_PARAM_PRIV_KEY, &priv) == 1 && priv
			&& bn2buf (pub, signingPublicKey, DSA_PUBLIC_KEY_LENGTH)
			&& bn2buf (priv, signingPrivateKey, DSA_PRIVATE_KEY_LENGTH);
		BN_free (pub);
		BN_free (priv);
		EVP_PKEY_free (pkey);
		EVP_PKEY_free (paramskey);
		EVP_PKEY_CTX_free (ctx);
		if (!ok)
		{
			clear ();
			return false;
		}
		return true;
	}
#else

	DSAVerifier::DSAVerifier ()
	{
		m_PublicKey = CreateDSA ();
	}

	DSAVerifier::~DSAVerifier ()
	{
		DSA_free (m_PublicKey);
	}

	void DSAVerifier::SetPublicKey (const uint8_t * signingKey)
	{
		if (!signingKey) return;
		DSA_set0_key (m_PublicKey, BN_bin2bn (signingKey, DSA_PUBLIC_KEY_LENGTH, NULL), NULL);
	}

	bool DSAVerifier::Verify (const uint8_t * buf, size_t len, const uint8_t * signature) const
	{
		if (!m_PublicKey || !signature) return false;
		// calculate SHA1 digest
		uint8_t digest[20];
		SHA1 (buf, len, digest);
		// signature
		DSA_SIG * sig = DSA_SIG_new ();
		BIGNUM * r = BN_bin2bn (signature, DSA_SIGNATURE_LENGTH / 2, NULL);
		BIGNUM * s = BN_bin2bn (signature + DSA_SIGNATURE_LENGTH / 2, DSA_SIGNATURE_LENGTH / 2, NULL);
		if (!sig || !r || !s || DSA_SIG_set0 (sig, r, s) != 1)
		{
			BN_free (r);
			BN_free (s);
			DSA_SIG_free (sig);
			return false;
		}
		// DSA verification
		int ret = DSA_do_verify (digest, 20, sig, m_PublicKey) == 1;
		DSA_SIG_free (sig);
		return ret;
	}

	DSASigner::DSASigner (const uint8_t * signingPrivateKey, const uint8_t * signingPublicKey)
		: m_PrivateKey (nullptr)
	{
		if (!signingPrivateKey || !signingPublicKey) return;
		m_PrivateKey = CreateDSA ();
		if (m_PrivateKey)
			DSA_set0_key (m_PrivateKey, BN_bin2bn (signingPublicKey, DSA_PUBLIC_KEY_LENGTH, NULL),
				BN_bin2bn (signingPrivateKey, DSA_PRIVATE_KEY_LENGTH, NULL));
	}

	DSASigner::~DSASigner ()
	{
		DSA_free (m_PrivateKey);
	}

	void DSASigner::Sign (const uint8_t * buf, int len, uint8_t * signature) const
	{
		if (!m_PrivateKey || !signature)
		{
			if (signature) memset (signature, 0, DSA_SIGNATURE_LENGTH);
			return;
		}
		uint8_t digest[20];
		SHA1 (buf, len, digest);
		DSA_SIG * sig = DSA_do_sign (digest, 20, m_PrivateKey);
		const BIGNUM *r = nullptr, *s = nullptr;
		if (sig) DSA_SIG_get0 (sig, &r, &s);
		if (!r || !s || !bn2buf (r, signature, DSA_SIGNATURE_LENGTH / 2)
			|| !bn2buf (s, signature + DSA_SIGNATURE_LENGTH / 2, DSA_SIGNATURE_LENGTH / 2))
		{
			memset (signature, 0, DSA_SIGNATURE_LENGTH);
			LogPrint (eLogError, "DSA signing failed");
		}
		DSA_SIG_free (sig);
	}

	bool CreateDSARandomKeys (uint8_t * signingPrivateKey, uint8_t * signingPublicKey)
	{
		if (!signingPrivateKey || !signingPublicKey) return false;
		auto clear = [&] ()
		{
			memset (signingPrivateKey, 0, DSA_PRIVATE_KEY_LENGTH);
			memset (signingPublicKey, 0, DSA_PUBLIC_KEY_LENGTH);
		};
		DSA * dsa = CreateDSA ();
		if (!dsa || DSA_generate_key (dsa) != 1)
		{
			DSA_free (dsa);
			clear ();
			return false;
		}
		const BIGNUM *pub_key, *priv_key;
		DSA_get0_key (dsa, &pub_key, &priv_key);
		bool ok = pub_key && priv_key && bn2buf (priv_key, signingPrivateKey, DSA_PRIVATE_KEY_LENGTH)
			&& bn2buf (pub_key, signingPublicKey, DSA_PUBLIC_KEY_LENGTH);
		DSA_free (dsa);
		if (!ok)
		{
			clear ();
			return false;
		}
		return true;
	}
#endif

#ifndef LIBRESSL_VERSION_NUMBER // OpenSSL 3.0.2 is the minimum
	ECDSAVerifier::ECDSAVerifier (int curve, size_t keyLen, const EVP_MD * hash)
		: m_Curve (curve)
		, m_KeyLen (keyLen)
		, m_Hash (hash)
		, m_PublicKey (nullptr)
	{
	}

	ECDSAVerifier::~ECDSAVerifier ()
	{
		if (m_PublicKey) EVP_PKEY_free (m_PublicKey);
	}

	void ECDSAVerifier::SetPublicKey (const uint8_t * signingKey)
	{
		if (m_PublicKey)
		{
			EVP_PKEY_free (m_PublicKey);
			m_PublicKey = nullptr;
		}
		if (!signingKey) return;
		auto plen = GetPublicKeyLen ();
		std::vector<uint8_t> pub (plen + 1);
		pub[0] = POINT_CONVERSION_UNCOMPRESSED;
		memcpy (pub.data () + 1, signingKey, plen); // 0x04|x|y
		OSSL_PARAM_BLD * paramBld = OSSL_PARAM_BLD_new ();
		if (!paramBld)
		{
			LogPrint (eLogError, "ECDSA can't allocate PKEY parameters");
			return;
		}
		bool built
			= OSSL_PARAM_BLD_push_utf8_string (paramBld, OSSL_PKEY_PARAM_GROUP_NAME, OBJ_nid2ln (m_Curve), 0) == 1
			&& OSSL_PARAM_BLD_push_octet_string (paramBld, OSSL_PKEY_PARAM_PUB_KEY, pub.data (), pub.size ()) == 1;
		OSSL_PARAM * params = built ? OSSL_PARAM_BLD_to_param (paramBld) : nullptr;

		EVP_PKEY_CTX * ctx = params ? EVP_PKEY_CTX_new_from_name (NULL, "EC", NULL) : nullptr;
		if (ctx)
		{
			EVP_PKEY * pkey = nullptr;
			if (EVP_PKEY_fromdata_init (ctx) <= 0 || EVP_PKEY_fromdata (ctx, &pkey, EVP_PKEY_PUBLIC_KEY, params) <= 0)
			{
				LogPrint (eLogError, "ECDSA can't create PKEY from params");
				EVP_PKEY_free (pkey);
			}
			else
				m_PublicKey = pkey;
			EVP_PKEY_CTX_free (ctx);
		}
		else
			LogPrint (eLogError, "ECDSA can't create PKEY context");

		OSSL_PARAM_free (params);
		OSSL_PARAM_BLD_free (paramBld);
	}

	bool ECDSAVerifier::Verify (const uint8_t * buf, size_t len, const uint8_t * signature) const
	{
		if (!m_PublicKey || !signature) return false;
		// signature
		ECDSA_SIG * sig = ECDSA_SIG_new ();
		BIGNUM * r = BN_bin2bn (signature, static_cast<int> (GetSignatureLen () / 2), NULL);
		BIGNUM * s = BN_bin2bn (signature + GetSignatureLen () / 2, static_cast<int> (GetSignatureLen () / 2), NULL);
		if (!sig || !r || !s || ECDSA_SIG_set0 (sig, r, s) != 1)
		{
			BN_free (r);
			BN_free (s);
			ECDSA_SIG_free (sig);
			return false;
		}
		// to DER format, sized exactly from the encoder
		std::vector<uint8_t> sign;
		const bool encoded = EncodeECDSASignature (sig, sign);
		ECDSA_SIG_free (sig);
		if (!encoded || sign.empty ()) return false;
		// verify
		MDCtxPtr ctx (EVP_MD_CTX_new ());
		return ctx && EVP_DigestVerifyInit (ctx.get (), NULL, m_Hash, NULL, m_PublicKey) == 1
			&& EVP_DigestVerify (ctx.get (), sign.data (), sign.size (), buf, len) == 1;
	}

	ECDSASigner::ECDSASigner (int curve, size_t keyLen, const EVP_MD * hash, const uint8_t * signingPrivateKey)
		: m_KeyLen (keyLen)
		, m_Hash (hash)
		, m_PrivateKey (nullptr)
	{
		if (!signingPrivateKey) return;
		BIGNUM * priv = BN_bin2bn (signingPrivateKey, static_cast<int> (keyLen / 2), NULL);
		if (!priv) return;
		OSSL_PARAM_BLD * paramBld = OSSL_PARAM_BLD_new ();
		if (!paramBld)
		{
			BN_free (priv);
			return;
		}
		bool built = OSSL_PARAM_BLD_push_utf8_string (paramBld, OSSL_PKEY_PARAM_GROUP_NAME, OBJ_nid2ln (curve), 0) == 1
			&& OSSL_PARAM_BLD_push_BN (paramBld, OSSL_PKEY_PARAM_PRIV_KEY, priv) == 1;
		OSSL_PARAM * params = built ? OSSL_PARAM_BLD_to_param (paramBld) : nullptr;

		EVP_PKEY_CTX * ctx = params ? EVP_PKEY_CTX_new_from_name (NULL, "EC", NULL) : nullptr;
		if (ctx)
		{
			EVP_PKEY * pkey = nullptr;
			if (EVP_PKEY_fromdata_init (ctx) <= 0 || EVP_PKEY_fromdata (ctx, &pkey, EVP_PKEY_KEYPAIR, params) <= 0)
			{
				LogPrint (eLogError, "ECDSA can't create PKEY from params");
				EVP_PKEY_free (pkey);
			}
			else
				m_PrivateKey = pkey;
			EVP_PKEY_CTX_free (ctx);
		}
		else
			LogPrint (eLogError, "ECDSA can't create PKEY context");

		OSSL_PARAM_free (params);
		OSSL_PARAM_BLD_free (paramBld);
		BN_free (priv);
	}

	ECDSASigner::~ECDSASigner ()
	{
		if (m_PrivateKey) EVP_PKEY_free (m_PrivateKey);
	}

	void ECDSASigner::Sign (const uint8_t * buf, int len, uint8_t * signature) const
	{
		if (!m_PrivateKey || !signature)
		{
			if (signature) memset (signature, 0, m_KeyLen);
			return;
		}
		// DER needs up to keyLen + 9 bytes (two high-bit integers plus the
		// long-form SEQUENCE header), so keep a conservative margin.
		std::vector<uint8_t> sign (m_KeyLen + 16);
		size_t l = sign.size ();
		MDCtxPtr ctx (EVP_MD_CTX_new ());
		bool ok = ctx && EVP_DigestSignInit (ctx.get (), NULL, m_Hash, NULL, m_PrivateKey) == 1
			&& EVP_DigestSign (ctx.get (), sign.data (), &l, buf, len) == 1 && l > 0 && l <= sign.size ();
		// decode r and s
		ECDSA_SIG * sig = nullptr;
		if (ok)
		{
			const uint8_t * s1 = sign.data ();
			sig = d2i_ECDSA_SIG (NULL, &s1, static_cast<long> (l));
		}
		const BIGNUM *r = nullptr, *s = nullptr;
		if (sig) ECDSA_SIG_get0 (sig, &r, &s);
		if (!r || !s || !bn2buf (r, signature, m_KeyLen / 2) || !bn2buf (s, signature + m_KeyLen / 2, m_KeyLen / 2))
		{
			memset (signature, 0, m_KeyLen);
			LogPrint (eLogError, "ECDSA signing failed");
		}
		ECDSA_SIG_free (sig);
	}

	bool CreateECDSARandomKeys (int curve, size_t keyLen, uint8_t * signingPrivateKey, uint8_t * signingPublicKey)
	{
		if (!signingPrivateKey || !signingPublicKey) return false;
		auto clear = [&] ()
		{
			memset (signingPrivateKey, 0, keyLen / 2);
			memset (signingPublicKey, 0, keyLen);
		};
		EVP_PKEY * pkey = EVP_EC_gen (OBJ_nid2ln (curve));
		if (!pkey)
		{
			clear ();
			return false;
		}
		// private
		BIGNUM * priv = nullptr;
		// public
		BIGNUM *x = nullptr, *y = nullptr;
		bool ok = EVP_PKEY_get_bn_param (pkey, OSSL_PKEY_PARAM_PRIV_KEY, &priv) == 1 && priv
			&& EVP_PKEY_get_bn_param (pkey, OSSL_PKEY_PARAM_EC_PUB_X, &x) == 1 && x
			&& EVP_PKEY_get_bn_param (pkey, OSSL_PKEY_PARAM_EC_PUB_Y, &y) == 1 && y
			&& bn2buf (priv, signingPrivateKey, keyLen / 2) && bn2buf (x, signingPublicKey, keyLen / 2)
			&& bn2buf (y, signingPublicKey + keyLen / 2, keyLen / 2);
		BN_free (priv);
		BN_free (x);
		BN_free (y);
		EVP_PKEY_free (pkey);
		if (!ok)
		{
			clear ();
			return false;
		}
		return true;
	}

#endif

	RSAVerifier::RSAVerifier (size_t keyLen, const EVP_MD * hash)
		: m_KeyLen (keyLen)
		, m_Hash (hash)
		, m_PublicKey (nullptr)
	{
	}

	RSAVerifier::~RSAVerifier () = default;

	void RSAVerifier::SetPublicKey (const uint8_t * signingKey)
	{
		m_PublicKey.reset ();
		if (!signingKey || !m_KeyLen || !m_Hash) return;
		BIGNUM * n = BN_bin2bn (signingKey, static_cast<int> (m_KeyLen), nullptr);
		if (!n || BN_is_zero (n) || !BN_is_odd (n)
			|| BN_num_bytes (n) != static_cast<int> (m_KeyLen))
		{
			// BN_bin2bn strips leading zero bytes. A modulus that no longer
			// fills the wire field is not the fixed-size I2P RSA identity.
			BN_free (n);
			LogPrint (eLogError, "RSA public modulus is invalid");
			return;
		}
		BIGNUM * e = BN_dup (GetRSAE ());
		if (!e)
		{
			BN_free (n);
			LogPrint (eLogError, "RSA can't allocate public exponent");
			return;
		}
#ifndef LIBRESSL_VERSION_NUMBER // OpenSSL 3.0.2 is the minimum
		OSSL_PARAM_BLD * paramBld = OSSL_PARAM_BLD_new ();
		const bool built = paramBld && OSSL_PARAM_BLD_push_BN (paramBld, OSSL_PKEY_PARAM_RSA_N, n) == 1
			&& OSSL_PARAM_BLD_push_BN (paramBld, OSSL_PKEY_PARAM_RSA_E, e) == 1;
		OSSL_PARAM * params = built ? OSSL_PARAM_BLD_to_param (paramBld) : nullptr;
		EVP_PKEY_CTX * ctx = params ? EVP_PKEY_CTX_new_from_name (nullptr, "RSA", nullptr) : nullptr;
		if (ctx)
		{
			EVP_PKEY * pkey = nullptr;
			if (EVP_PKEY_fromdata_init (ctx) <= 0 || EVP_PKEY_fromdata (ctx, &pkey, EVP_PKEY_PUBLIC_KEY, params) <= 0)
			{
				LogPrint (eLogError, "RSA can't create PKEY from params");
				EVP_PKEY_free (pkey);
			}
			else
				m_PublicKey.reset (pkey);
			EVP_PKEY_CTX_free (ctx);
		}
		else
			LogPrint (eLogError, "RSA can't create PKEY context");
		OSSL_PARAM_free (params);
		OSSL_PARAM_BLD_free (paramBld);
		BN_free (n);
		BN_free (e);
#else
		RSA * rsa = RSA_new ();
		if (!rsa || RSA_set0_key (rsa, n, e, nullptr) != 1)
		{
			RSA_free (rsa);
			BN_free (n);
			BN_free (e);
			LogPrint (eLogError, "RSA can't set public key");
			return;
		}
		EVP_PKEY * pkey = EVP_PKEY_new ();
		if (!pkey || EVP_PKEY_assign_RSA (pkey, rsa) != 1)
		{
			EVP_PKEY_free (pkey);
			RSA_free (rsa);
			LogPrint (eLogError, "RSA can't create PKEY");
			return;
		}
		m_PublicKey.reset (pkey);
#endif
	}

	bool RSAVerifier::Verify (const uint8_t * buf, size_t len, const uint8_t * signature) const
	{
		if (!m_PublicKey || !m_Hash || !signature || (!buf && len)) return false;
		// Java verifies the digest of the signed data with NONEwithRSA (raw
		// PKCS#1 v1.5), so hash exactly once and do not set a signature MD.
		uint8_t digest[EVP_MAX_MD_SIZE];
		unsigned int digestLength = 0;
		if (EVP_Digest (buf, len, digest, &digestLength, m_Hash, nullptr) != 1 || !digestLength
			|| digestLength + 11 > m_KeyLen) // PKCS#1 v1.5 DigestInfo + padding
		{
			OPENSSL_cleanse (digest, sizeof (digest));
			return false;
		}
		EVP_PKEY_CTX * ctx = EVP_PKEY_CTX_new (m_PublicKey.get (), nullptr);
		bool verified = ctx && EVP_PKEY_verify_init (ctx) > 0
			&& EVP_PKEY_CTX_set_rsa_padding (ctx, RSA_PKCS1_PADDING) > 0
			&& EVP_PKEY_verify (ctx, signature, m_KeyLen, digest, digestLength) == 1;
		EVP_PKEY_CTX_free (ctx);
		OPENSSL_cleanse (digest, sizeof (digest));
		if (!verified) LogPrint (eLogWarning, "RSA signature verification failed");
		return verified;
	}

	EDDSA25519Verifier::EDDSA25519Verifier ()
		: m_Pkey (nullptr)
	{
	}

	EDDSA25519Verifier::~EDDSA25519Verifier () = default;

	void EDDSA25519Verifier::SetPublicKey (const uint8_t * signingKey)
	{
		m_Pkey.reset ();
		if (!signingKey) return;
		m_Pkey.reset (EVP_PKEY_new_raw_public_key (EVP_PKEY_ED25519, NULL, signingKey, EDDSA25519_PUBLIC_KEY_LENGTH));
		if (!m_Pkey) LogPrint (eLogError, "EdDSA can't create public key");
	}

	bool EDDSA25519Verifier::Verify (const uint8_t * buf, size_t len, const uint8_t * signature) const
	{
		if (!signature) return false;
		if (m_Pkey)
		{
			MDCtxPtr ctx (EVP_MD_CTX_new ());
			return ctx && EVP_DigestVerifyInit (ctx.get (), NULL, NULL, NULL, m_Pkey.get ()) == 1
				&& EVP_DigestVerify (ctx.get (), signature, EDDSA25519_SIGNATURE_LENGTH, buf, len) == 1;
		}
		else
			LogPrint (eLogError, "EdDSA verification key is not set");
		return false;
	}

	EDDSA25519SignerCompat::EDDSA25519SignerCompat (const uint8_t * signingPrivateKey, const uint8_t * signingPublicKey)
	{
		// expand key
		Ed25519::ExpandPrivateKey (signingPrivateKey, m_ExpandedPrivateKey);
		// generate and encode public key
		BN_CTX * ctx = BN_CTX_new ();
		auto publicKey = GetEd25519 ()->GeneratePublicKey (m_ExpandedPrivateKey, ctx);
		GetEd25519 ()->EncodePublicKey (publicKey, m_PublicKeyEncoded, ctx);

		if (signingPublicKey && memcmp (m_PublicKeyEncoded, signingPublicKey, EDDSA25519_PUBLIC_KEY_LENGTH))
		{
			// keys don't match, it means older key with 0x1F
			LogPrint (eLogWarning, "Older EdDSA key detected");
			m_ExpandedPrivateKey[EDDSA25519_PRIVATE_KEY_LENGTH - 1] &= 0xDF; // drop third bit
			publicKey = GetEd25519 ()->GeneratePublicKey (m_ExpandedPrivateKey, ctx);
			GetEd25519 ()->EncodePublicKey (publicKey, m_PublicKeyEncoded, ctx);
		}
		BN_CTX_free (ctx);
	}

	EDDSA25519SignerCompat::~EDDSA25519SignerCompat ()
	{
		// The expanded private scalar is key material; do not leave it in the
		// heap block that the fallback signer occupied.
		OPENSSL_cleanse (m_ExpandedPrivateKey, sizeof (m_ExpandedPrivateKey));
	}

	void EDDSA25519SignerCompat::Sign (const uint8_t * buf, int len, uint8_t * signature) const
	{
		GetEd25519 ()->Sign (m_ExpandedPrivateKey, m_PublicKeyEncoded, buf, len, signature);
	}

	EDDSA25519Signer::EDDSA25519Signer (const uint8_t * signingPrivateKey, const uint8_t * signingPublicKey)
		: m_Pkey (nullptr)
		, m_Fallback (nullptr)
	{
		if (!signingPrivateKey)
		{
			LogPrint (eLogError, "EdDSA private key is not set");
			return;
		}
		m_Pkey.reset (
			EVP_PKEY_new_raw_private_key (EVP_PKEY_ED25519, NULL, signingPrivateKey, EDDSA25519_PRIVATE_KEY_LENGTH));
		if (!m_Pkey)
		{
			if (signingPublicKey)
			{
				// The compatibility signer does not depend on the provider and
				// can still validate the private/public key pair.
				m_Fallback = std::make_unique<EDDSA25519SignerCompat> (signingPrivateKey, signingPublicKey);
				return;
			}
			LogPrint (eLogError, "EdDSA can't create private key");
			return;
		}
		uint8_t publicKey[EDDSA25519_PUBLIC_KEY_LENGTH] = { };
		size_t len = EDDSA25519_PUBLIC_KEY_LENGTH;
		bool havePublicKey
			= EVP_PKEY_get_raw_public_key (m_Pkey.get (), publicKey, &len) == 1 && len == EDDSA25519_PUBLIC_KEY_LENGTH;
		if (signingPublicKey && (!havePublicKey || memcmp (publicKey, signingPublicKey, EDDSA25519_PUBLIC_KEY_LENGTH)))
		{
			LogPrint (eLogWarning, "EdDSA public key mismatch. Fallback");
			m_Fallback = std::make_unique<EDDSA25519SignerCompat> (signingPrivateKey, signingPublicKey);
			m_Pkey.reset ();
		}
		else if (!havePublicKey)
			LogPrint (eLogWarning, "EdDSA can't read the public key from the private key");
	}

	EDDSA25519Signer::~EDDSA25519Signer () = default;

	void EDDSA25519Signer::Sign (const uint8_t * buf, int len, uint8_t * signature) const
	{
		if (!signature) return;
		if (m_Fallback)
			return m_Fallback->Sign (buf, len, signature);
		else if (m_Pkey)
		{
			MDCtxPtr ctx (EVP_MD_CTX_new ());
			size_t l = EDDSA25519_SIGNATURE_LENGTH;
			uint8_t sig[EDDSA25519_SIGNATURE_LENGTH] = { }; // openssl issue #7232
			bool ok = ctx && EVP_DigestSignInit (ctx.get (), NULL, NULL, NULL, m_Pkey.get ()) == 1
				&& EVP_DigestSign (ctx.get (), sig, &l, buf, len) == 1 && l == sizeof (sig);
			if (ok)
				memcpy (signature, sig, sizeof (sig));
			else
			{
				memset (signature, 0, EDDSA25519_SIGNATURE_LENGTH);
				LogPrint (eLogError, "EdDSA signing failed");
			}
			OPENSSL_cleanse (sig, sizeof (sig));
		}
		else
		{
			memset (signature, 0, EDDSA25519_SIGNATURE_LENGTH);
			LogPrint (eLogError, "EdDSA signing key is not set");
		}
	}

	bool EDDSA25519phVerifier::Verify (const uint8_t * buf, size_t len, const uint8_t * signature) const
	{
		if (!signature) return false;
		auto pkey = GetPkey ();
		MDCtxPtr ctx (EVP_MD_CTX_new ());
		// Java SigType 8 uses EdDSAEngine over the supplied bytes; it does not
		// apply RFC 8032 dom2/prehash. Use the same plain Ed25519 primitive as
		// SigType 7. (SU3 callers prehash the payload themselves.)
		return pkey && ctx && EVP_DigestVerifyInit (ctx.get (), nullptr, nullptr, nullptr, pkey) == 1
			&& EVP_DigestVerify (ctx.get (), signature, 64, buf, len) == 1;
	}

	EDDSA25519phSigner::EDDSA25519phSigner (const uint8_t * signingPrivateKey)
		: EDDSA25519Signer (signingPrivateKey)
	{
	}

	void EDDSA25519phSigner::Sign (const uint8_t * buf, int len, uint8_t * signature) const
	{
		if (!signature) return;
		auto pkey = GetPkey ();
		MDCtxPtr ctx (EVP_MD_CTX_new ());
		size_t l = 64;
		uint8_t sig[64] = { };
		bool ok = pkey && ctx && len >= 0 && EVP_DigestSignInit (ctx.get (), nullptr, nullptr, nullptr, pkey) == 1
			&& EVP_DigestSign (ctx.get (), sig, &l, buf, len) == 1 && l == sizeof (sig);
		if (ok)
			memcpy (signature, sig, sizeof (sig));
		else
		{
			memset (signature, 0, sizeof (sig));
			LogPrint (eLogError, "Ed25519ph-compatible signing failed");
		}
		OPENSSL_cleanse (sig, sizeof (sig));
	}
}
}
