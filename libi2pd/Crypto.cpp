/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Crypto.
 *
 * Implements the OpenSSL-backed primitives shared by the I2P protocols:
 * ElGamal and ECIES-P256 encryption, DSA/RSA parameters, X25519 agreement,
 * AES-ECB/CBC tunnel layers, ChaCha20-Poly1305 AEAD, HKDF and Noise symmetric
 * state operations. Lengths are checked before conversion to OpenSSL's
 * int-sized interfaces, failed tunnel/Noise authentication clears its output,
 * and process-wide crypto initialisation and locking are managed here.
 *
 */

#include <string.h>
#include <string>
#include <vector>
#include <mutex>
#include <memory>
#include <limits>
#include <openssl/crypto.h>
#include "TunnelBase.h"
#include <openssl/ssl.h>
#include <openssl/hmac.h>
#include <openssl/kdf.h>
#if I2PD_HAVE_EVP_KDF_FETCH
#include <openssl/core_names.h>
#endif
#ifndef LIBRESSL_VERSION_NUMBER // OpenSSL 3.0.2 is the minimum
#include <openssl/param_build.h>
#include <openssl/core_names.h>
#endif
#include "CPU.h"
#include "Crypto.h"
#include "Ed25519.h"
#include "I2PEndian.h"
#include "Log.h"

namespace i2p
{
namespace crypto
{
	constexpr uint8_t elgp_[256] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xC9, 0x0F, 0xDA, 0xA2, 0x21, 0x68,
		0xC2, 0x34, 0xC4, 0xC6, 0x62, 0x8B, 0x80, 0xDC, 0x1C, 0xD1, 0x29, 0x02, 0x4E, 0x08, 0x8A, 0x67, 0xCC, 0x74,
		0x02, 0x0B, 0xBE, 0xA6, 0x3B, 0x13, 0x9B, 0x22, 0x51, 0x4A, 0x08, 0x79, 0x8E, 0x34, 0x04, 0xDD, 0xEF, 0x95,
		0x19, 0xB3, 0xCD, 0x3A, 0x43, 0x1B, 0x30, 0x2B, 0x0A, 0x6D, 0xF2, 0x5F, 0x14, 0x37, 0x4F, 0xE1, 0x35, 0x6D,
		0x6D, 0x51, 0xC2, 0x45, 0xE4, 0x85, 0xB5, 0x76, 0x62, 0x5E, 0x7E, 0xC6, 0xF4, 0x4C, 0x42, 0xE9, 0xA6, 0x37,
		0xED, 0x6B, 0x0B, 0xFF, 0x5C, 0xB6, 0xF4, 0x06, 0xB7, 0xED, 0xEE, 0x38, 0x6B, 0xFB, 0x5A, 0x89, 0x9F, 0xA5,
		0xAE, 0x9F, 0x24, 0x11, 0x7C, 0x4B, 0x1F, 0xE6, 0x49, 0x28, 0x66, 0x51, 0xEC, 0xE4, 0x5B, 0x3D, 0xC2, 0x00,
		0x7C, 0xB8, 0xA1, 0x63, 0xBF, 0x05, 0x98, 0xDA, 0x48, 0x36, 0x1C, 0x55, 0xD3, 0x9A, 0x69, 0x16, 0x3F, 0xA8,
		0xFD, 0x24, 0xCF, 0x5F, 0x83, 0x65, 0x5D, 0x23, 0xDC, 0xA3, 0xAD, 0x96, 0x1C, 0x62, 0xF3, 0x56, 0x20, 0x85,
		0x52, 0xBB, 0x9E, 0xD5, 0x29, 0x07, 0x70, 0x96, 0x96, 0x6D, 0x67, 0x0C, 0x35, 0x4E, 0x4A, 0xBC, 0x98, 0x04,
		0xF1, 0x74, 0x6C, 0x08, 0xCA, 0x18, 0x21, 0x7C, 0x32, 0x90, 0x5E, 0x46, 0x2E, 0x36, 0xCE, 0x3B, 0xE3, 0x9E,
		0x77, 0x2C, 0x18, 0x0E, 0x86, 0x03, 0x9B, 0x27, 0x83, 0xA2, 0xEC, 0x07, 0xA2, 0x8F, 0xB5, 0xC5, 0x5D, 0xF0,
		0x6F, 0x4C, 0x52, 0xC9, 0xDE, 0x2B, 0xCB, 0xF6, 0x95, 0x58, 0x17, 0x18, 0x39, 0x95, 0x49, 0x7C, 0xEA, 0x95,
		0x6A, 0xE5, 0x15, 0xD2, 0x26, 0x18, 0x98, 0xFA, 0x05, 0x10, 0x15, 0x72, 0x8E, 0x5A, 0x8A, 0xAC, 0xAA, 0x68,
		0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

	constexpr int elgg_ = 2;

	constexpr uint8_t dsap_[128] = { 0x9c, 0x05, 0xb2, 0xaa, 0x96, 0x0d, 0x9b, 0x97, 0xb8, 0x93, 0x19, 0x63, 0xc9, 0xcc,
		0x9e, 0x8c, 0x30, 0x26, 0xe9, 0xb8, 0xed, 0x92, 0xfa, 0xd0, 0xa6, 0x9c, 0xc8, 0x86, 0xd5, 0xbf, 0x80, 0x15,
		0xfc, 0xad, 0xae, 0x31, 0xa0, 0xad, 0x18, 0xfa, 0xb3, 0xf0, 0x1b, 0x00, 0xa3, 0x58, 0xde, 0x23, 0x76, 0x55,
		0xc4, 0x96, 0x4a, 0xfa, 0xa2, 0xb3, 0x37, 0xe9, 0x6a, 0xd3, 0x16, 0xb9, 0xfb, 0x1c, 0xc5, 0x64, 0xb5, 0xae,
		0xc5, 0xb6, 0x9a, 0x9f, 0xf6, 0xc3, 0xe4, 0x54, 0x87, 0x07, 0xfe, 0xf8, 0x50, 0x3d, 0x91, 0xdd, 0x86, 0x02,
		0xe8, 0x67, 0xe6, 0xd3, 0x5d, 0x22, 0x35, 0xc1, 0x86, 0x9c, 0xe2, 0x47, 0x9c, 0x3b, 0x9d, 0x54, 0x01, 0xde,
		0x04, 0xe0, 0x72, 0x7f, 0xb3, 0x3d, 0x65, 0x11, 0x28, 0x5d, 0x4c, 0xf2, 0x95, 0x38, 0xd9, 0xe3, 0xb6, 0x05,
		0x1f, 0x5b, 0x22, 0xcc, 0x1c, 0x93 };

	constexpr uint8_t dsaq_[20] = { 0xa5, 0xdf, 0xc2, 0x8f, 0xef, 0x4c, 0xa1, 0xe2, 0x86, 0x74, 0x4c, 0xd8, 0xee, 0xd9,
		0xd2, 0x9d, 0x68, 0x40, 0x46, 0xb7 };

	constexpr uint8_t dsag_[128] = { 0x0c, 0x1f, 0x4d, 0x27, 0xd4, 0x00, 0x93, 0xb4, 0x29, 0xe9, 0x62, 0xd7, 0x22, 0x38,
		0x24, 0xe0, 0xbb, 0xc4, 0x7e, 0x7c, 0x83, 0x2a, 0x39, 0x23, 0x6f, 0xc6, 0x83, 0xaf, 0x84, 0x88, 0x95, 0x81,
		0x07, 0x5f, 0xf9, 0x08, 0x2e, 0xd3, 0x23, 0x53, 0xd4, 0x37, 0x4d, 0x73, 0x01, 0xcd, 0xa1, 0xd2, 0x3c, 0x43,
		0x1f, 0x46, 0x98, 0x59, 0x9d, 0xda, 0x02, 0x45, 0x18, 0x24, 0xff, 0x36, 0x97, 0x52, 0x59, 0x36, 0x47, 0xcc,
		0x3d, 0xdc, 0x19, 0x7d, 0xe9, 0x85, 0xe4, 0x3d, 0x13, 0x6c, 0xdc, 0xfc, 0x6b, 0xd5, 0x40, 0x9c, 0xd2, 0xf4,
		0x50, 0x82, 0x11, 0x42, 0xa5, 0xe6, 0xf8, 0xeb, 0x1c, 0x3a, 0xb5, 0xd0, 0x48, 0x4b, 0x81, 0x29, 0xfc, 0xf1,
		0x7b, 0xce, 0x4f, 0x7f, 0x33, 0x32, 0x1c, 0x3c, 0xb3, 0xdb, 0xb1, 0x4a, 0x90, 0x5e, 0x7b, 0x2b, 0x3e, 0x93,
		0xbe, 0x47, 0x08, 0xcb, 0xcc, 0x82 };

	constexpr int rsae_ = 65537;

	struct CryptoConstants
	{
		// DH/ElGamal
		BIGNUM * elgp;
		BIGNUM * elgg;

		// DSA
		BIGNUM * dsap;
		BIGNUM * dsaq;
		BIGNUM * dsag;

		// RSA
		BIGNUM * rsae;

		CryptoConstants (const uint8_t * elgpBytes, int elggValue, const uint8_t * dsapBytes, const uint8_t * dsaqBytes,
			const uint8_t * dsagBytes, int rsaeValue)
		{
			elgp = BN_new ();
			BN_bin2bn (elgpBytes, 256, elgp);
			elgg = BN_new ();
			BN_set_word (elgg, elggValue);
			dsap = BN_new ();
			BN_bin2bn (dsapBytes, 128, dsap);
			dsaq = BN_new ();
			BN_bin2bn (dsaqBytes, 20, dsaq);
			dsag = BN_new ();
			BN_bin2bn (dsagBytes, 128, dsag);
			rsae = BN_new ();
			BN_set_word (rsae, rsaeValue);
		}

		~CryptoConstants ()
		{
			BN_free (elgp);
			BN_free (elgg);
			BN_free (dsap);
			BN_free (dsaq);
			BN_free (dsag);
			BN_free (rsae);
		}
	};

	static const CryptoConstants & GetCryptoConstants ()
	{
		static CryptoConstants cryptoConstants (elgp_, elgg_, dsap_, dsaq_, dsag_, rsae_);
		return cryptoConstants;
	}

	bool bn2buf (const BIGNUM * bn, uint8_t * buf, size_t len)
	{
		if (!bn) return false; // BN_num_bytes dereferences its argument
		const int numBytes = BN_num_bytes (bn);
		if (numBytes < 0 || static_cast<size_t> (numBytes) > len) return false;
		if (!buf && (len || numBytes)) return false;
		const size_t offset = len - static_cast<size_t> (numBytes);
		if (numBytes && BN_bn2bin (bn, buf + offset) != numBytes) return false;
		if (offset) memset (buf, 0, offset);
		return true;
	}

	// RSA
#define rsae GetCryptoConstants ().rsae
	const BIGNUM * GetRSAE ()
	{
		return rsae;
	}

	// DSA
#define dsap GetCryptoConstants ().dsap
#define dsaq GetCryptoConstants ().dsaq
#define dsag GetCryptoConstants ().dsag
#ifndef LIBRESSL_VERSION_NUMBER // OpenSSL 3.0.2 is the minimum
	EVP_PKEY * CreateDSA (BIGNUM * pubKey, BIGNUM * privKey)
	{
		EVP_PKEY * pkey = nullptr;
		int selection = EVP_PKEY_KEY_PARAMETERS;
		OSSL_PARAM_BLD * bld = OSSL_PARAM_BLD_new ();
		if (!bld) return nullptr;
		bool built = OSSL_PARAM_BLD_push_BN (bld, OSSL_PKEY_PARAM_FFC_P, dsap) == 1
			&& OSSL_PARAM_BLD_push_BN (bld, OSSL_PKEY_PARAM_FFC_Q, dsaq) == 1
			&& OSSL_PARAM_BLD_push_BN (bld, OSSL_PKEY_PARAM_FFC_G, dsag) == 1;
		if (pubKey)
		{
			built = built && OSSL_PARAM_BLD_push_BN (bld, OSSL_PKEY_PARAM_PUB_KEY, pubKey) == 1;
			selection = EVP_PKEY_PUBLIC_KEY;
		}
		if (privKey)
		{
			built = built && OSSL_PARAM_BLD_push_BN (bld, OSSL_PKEY_PARAM_PRIV_KEY, privKey) == 1;
			selection = EVP_PKEY_KEYPAIR;
		}
		OSSL_PARAM * params = built ? OSSL_PARAM_BLD_to_param (bld) : nullptr;
		EVP_PKEY_CTX * ctx = params ? EVP_PKEY_CTX_new_from_name (nullptr, "DSA", nullptr) : nullptr;
		const bool ok
			= ctx && EVP_PKEY_fromdata_init (ctx) == 1 && EVP_PKEY_fromdata (ctx, &pkey, selection, params) == 1;
		if (!ok && pkey)
		{
			EVP_PKEY_free (pkey);
			pkey = nullptr;
		}
		EVP_PKEY_CTX_free (ctx);
		OSSL_PARAM_free (params);
		OSSL_PARAM_BLD_free (bld);
		return pkey;
	}
#else
	DSA * CreateDSA ()
	{
		DSA * dsa = DSA_new ();
		DSA_set0_pqg (dsa, BN_dup (dsap), BN_dup (dsaq), BN_dup (dsag));
		DSA_set0_key (dsa, NULL, NULL);
		return dsa;
	}
#endif

	// DH/ElGamal

#if !IS_X86_64
	const int ELGAMAL_SHORT_EXPONENT_NUM_BITS = 226;
	const int ELGAMAL_SHORT_EXPONENT_NUM_BYTES = ELGAMAL_SHORT_EXPONENT_NUM_BITS / 8 + 1;
#endif
	[[maybe_unused]] const int ELGAMAL_FULL_EXPONENT_NUM_BITS = 2048;
	[[maybe_unused]] const int ELGAMAL_FULL_EXPONENT_NUM_BYTES = ELGAMAL_FULL_EXPONENT_NUM_BITS / 8;

#define elgp GetCryptoConstants ().elgp
#define elgg GetCryptoConstants ().elgg

	static BN_MONT_CTX * g_MontCtx = nullptr;
	static void PrecalculateElggTable (BIGNUM * table[][255], int len) // table is len's array of array of 255 bignums
	{
		if (len <= 0) return;
		BN_CTX * ctx = BN_CTX_new ();
		g_MontCtx = BN_MONT_CTX_new ();
		BN_MONT_CTX_set (g_MontCtx, elgp, ctx);
		auto montCtx = BN_MONT_CTX_new ();
		BN_MONT_CTX_copy (montCtx, g_MontCtx);
		for (int i = 0; i < len; i++)
		{
			table[i][0] = BN_new ();
			if (!i)
				BN_to_montgomery (table[0][0], elgg, montCtx, ctx);
			else
				BN_mod_mul_montgomery (table[i][0], table[i - 1][254], table[i - 1][0], montCtx, ctx);
			for (int j = 1; j < 255; j++)
			{
				table[i][j] = BN_new ();
				BN_mod_mul_montgomery (table[i][j], table[i][j - 1], table[i][0], montCtx, ctx);
			}
		}
		BN_MONT_CTX_free (montCtx);
		BN_CTX_free (ctx);
	}

	static void DestroyElggTable (BIGNUM * table[][255], int len)
	{
		for (int i = 0; i < len; i++)
			for (int j = 0; j < 255; j++)
			{
				BN_free (table[i][j]);
				table[i][j] = nullptr;
			}
		BN_MONT_CTX_free (g_MontCtx);
	}

	static BIGNUM * ElggPow (const uint8_t * exp, int len, BIGNUM * table[][255], BN_CTX * ctx)
	// exp is in Big Endian
	{
		if (len <= 0) return nullptr;
		auto montCtx = BN_MONT_CTX_new ();
		BN_MONT_CTX_copy (montCtx, g_MontCtx);
		BIGNUM * res = nullptr;
		for (int i = 0; i < len; i++)
		{
			if (res)
			{
				if (exp[i]) BN_mod_mul_montgomery (res, res, table[len - 1 - i][exp[i] - 1], montCtx, ctx);
			}
			else if (exp[i])
				res = BN_dup (table[len - i - 1][exp[i] - 1]);
		}
		if (res) BN_from_montgomery (res, res, montCtx, ctx);
		BN_MONT_CTX_free (montCtx);
		return res;
	}

	static BIGNUM * ElggPow (const BIGNUM * exp, BIGNUM * table[][255], BN_CTX * ctx)
	{
		auto len = BN_num_bytes (exp);
		if (len <= 0) return nullptr;
		// The exponent is the secret ElGamal nonce; the vector owns it only for
		// the duration of the table walk and is cleansed before it is released.
		std::vector<uint8_t> buf (len);
		if (BN_bn2bin (exp, buf.data ()) != len)
		{
			OPENSSL_cleanse (buf.data (), buf.size ());
			return nullptr;
		}
		auto ret = ElggPow (buf.data (), len, table, ctx);
		OPENSSL_cleanse (buf.data (), buf.size ());
		return ret;
	}

	static BIGNUM * (*g_ElggTable)[255] = nullptr;

	// x25519
	X25519Keys::X25519Keys ()
	{
		m_Ctx = EVP_PKEY_CTX_new_id (NID_X25519, NULL);
		m_Pkey = nullptr;
		memset (m_PublicKey, 0, sizeof (m_PublicKey));
	}

	X25519Keys::X25519Keys (const uint8_t * priv, const uint8_t * pub)
	{
		memset (m_PublicKey, 0, sizeof (m_PublicKey));
		m_Pkey = priv ? EVP_PKEY_new_raw_private_key (EVP_PKEY_X25519, NULL, priv, 32) : nullptr;
		m_Ctx = m_Pkey ? EVP_PKEY_CTX_new (m_Pkey, NULL) : nullptr;
		if (!m_Pkey || !m_Ctx)
		{
			Reset ();
			return;
		}
		// Always derive the public key from the private key. A caller-supplied
		// public key is not authority for anything: copying it unchecked used
		// to publish a key that does not belong to the private scalar.
		uint8_t derived[32] = { };
		size_t len = 32;
		if (EVP_PKEY_get_raw_public_key (m_Pkey, derived, &len) != 1 || len != 32)
		{
			OPENSSL_cleanse (derived, sizeof (derived));
			Reset ();
			return;
		}
		if (pub && CRYPTO_memcmp (derived, pub, sizeof (derived)) != 0)
		{
			LogPrint (eLogError, "X25519: public key does not match the private key");
			OPENSSL_cleanse (derived, sizeof (derived));
			Reset ();
			return;
		}
		memcpy (m_PublicKey, derived, sizeof (m_PublicKey));
		OPENSSL_cleanse (derived, sizeof (derived));
	}

	X25519Keys::X25519Keys (X25519Keys && other) noexcept
		: m_Ctx (other.m_Ctx)
		, m_Pkey (other.m_Pkey)
		, m_IsElligatorIneligible (other.m_IsElligatorIneligible)
	{
		memcpy (m_PublicKey, other.m_PublicKey, sizeof (m_PublicKey));
		other.m_Ctx = nullptr;
		other.m_Pkey = nullptr;
		memset (other.m_PublicKey, 0, sizeof (other.m_PublicKey));
		other.m_IsElligatorIneligible = false;
	}

	X25519Keys & X25519Keys::operator= (X25519Keys && other) noexcept
	{
		if (this != &other)
		{
			Reset ();
			m_Ctx = other.m_Ctx;
			m_Pkey = other.m_Pkey;
			m_IsElligatorIneligible = other.m_IsElligatorIneligible;
			memcpy (m_PublicKey, other.m_PublicKey, sizeof (m_PublicKey));
			other.m_Ctx = nullptr;
			other.m_Pkey = nullptr;
			memset (other.m_PublicKey, 0, sizeof (other.m_PublicKey));
			other.m_IsElligatorIneligible = false;
		}
		return *this;
	}

	X25519Keys::~X25519Keys ()
	{
		Reset ();
	}

	void X25519Keys::Reset ()
	{
		if (m_Ctx)
		{
			EVP_PKEY_CTX_free (m_Ctx);
			m_Ctx = nullptr;
		}
		if (m_Pkey)
		{
			EVP_PKEY_free (m_Pkey);
			m_Pkey = nullptr;
		}
		memset (m_PublicKey, 0, sizeof (m_PublicKey));
	}

	bool X25519Keys::GenerateKeys ()
	{
		// The pool reuses X25519Keys objects. A key that was ineligible for
		// Elligator2 must become eligible again once its key material is
		// replaced, otherwise the flag outlives the key it described.
		m_IsElligatorIneligible = false;
		if (m_Pkey)
		{
			EVP_PKEY_free (m_Pkey);
			m_Pkey = nullptr;
		}
		if (!m_Ctx) m_Ctx = EVP_PKEY_CTX_new_id (NID_X25519, NULL);
		if (!m_Ctx || EVP_PKEY_keygen_init (m_Ctx) != 1 || EVP_PKEY_keygen (m_Ctx, &m_Pkey) != 1)
		{
			LogPrint (eLogError, "X25519: key generation failed");
			memset (m_PublicKey, 0, sizeof (m_PublicKey));
			if (m_Pkey)
			{
				EVP_PKEY_free (m_Pkey);
				m_Pkey = nullptr;
			}
			return false;
		}
		EVP_PKEY_CTX_free (m_Ctx);
		m_Ctx = EVP_PKEY_CTX_new (m_Pkey, NULL); // TODO: do we really need to re-create m_Ctx?
		size_t len = 32;
		if (!m_Ctx || EVP_PKEY_get_raw_public_key (m_Pkey, m_PublicKey, &len) != 1 || len != 32)
		{
			LogPrint (eLogError, "X25519: failed to export generated public key");
			memset (m_PublicKey, 0, sizeof (m_PublicKey));
			EVP_PKEY_free (m_Pkey);
			m_Pkey = nullptr;
			if (m_Ctx)
			{
				EVP_PKEY_CTX_free (m_Ctx);
				m_Ctx = nullptr;
			}
			return false;
		}
		return true;
	}

	bool X25519Keys::Agree (const uint8_t * pub, uint8_t * shared)
	{
		// No output buffer means there is nothing to report.
		if (!shared) return false;
		// Clear the output before every failure path, including the early
		// peer-key validation below.
		memset (shared, 0, 32);
		// not x25519 key, no local key or invalid peer encoding
		if (!pub || (pub[31] & 0x80) || !m_Ctx || !m_Pkey) return false;
		if (EVP_PKEY_derive_init (m_Ctx) != 1) return false;
		auto pkey = EVP_PKEY_new_raw_public_key (EVP_PKEY_X25519, NULL, pub, 32);
		if (!pkey) return false;
		size_t len = 32;
		bool ok
			= EVP_PKEY_derive_set_peer (m_Ctx, pkey) == 1 && EVP_PKEY_derive (m_Ctx, shared, &len) == 1 && len == 32;
		EVP_PKEY_free (pkey);
		if (ok)
		{
			// Java's Curve25519.eval() throws GeneralSecurityException on an
			// all-zero result. Some LibreSSL/OpenSSL builds return success for
			// low-order peers, so enforce the RFC 7748 check here as well.
			uint8_t acc = 0;
			for (size_t i = 0; i < 32; ++i) acc |= shared[i];
			ok = acc != 0;
		}
		if (!ok) memset (shared, 0, 32);
		return ok;
	}

	void X25519Keys::GetPrivateKey (uint8_t * priv) const
	{
		if (!priv) return;
		size_t len = 32;
		if (!m_Pkey || EVP_PKEY_get_raw_private_key (m_Pkey, priv, &len) != 1 || len != 32) memset (priv, 0, 32);
	}

	void X25519Keys::SetPrivateKey (const uint8_t * priv, bool calculatePublic)
	{
		m_IsElligatorIneligible = false;
		Reset ();
		if (!priv) return;
		m_Pkey = EVP_PKEY_new_raw_private_key (EVP_PKEY_X25519, NULL, priv, 32);
		m_Ctx = m_Pkey ? EVP_PKEY_CTX_new (m_Pkey, NULL) : nullptr;
		// Always derive the public key. Leaving m_PublicKey untouched when the
		// caller passed calculatePublic=false used to expose uninitialised
		// bytes through GetPublicKey().
		(void)calculatePublic;
		if (m_Pkey)
		{
			size_t len = 32;
			if (!m_Ctx || EVP_PKEY_get_raw_public_key (m_Pkey, m_PublicKey, &len) != 1 || len != 32) Reset ();
		}
	}

	// ElGamal
	bool ElGamalEncrypt (const uint8_t * key, const uint8_t * data, uint8_t * encrypted)
	{
		if (!key || !data || !encrypted) return false;
		BN_CTX * ctx = BN_CTX_new ();
		if (!ctx)
		{
			memset (encrypted, 0, 514);
			return false;
		}
		BN_CTX_start (ctx);
		// everything, but a, because a might come from table
		BIGNUM * k = BN_CTX_get (ctx);
		BIGNUM * y = BN_CTX_get (ctx);
		BIGNUM * b1 = BN_CTX_get (ctx);
		BIGNUM * b = BN_CTX_get (ctx);
		BIGNUM * pm1 = BN_CTX_get (ctx);
		bool validKey = k && y && b1 && b && pm1;
		// restore y from key and reject the trivial subgroup elements. y = 0,
		// y = 1 and y = p - 1 make the ciphertext independent of the random
		// exponent and would leak the plaintext.
		validKey = validKey && BN_bin2bn (key, 256, y) != nullptr && BN_copy (pm1, elgp) != nullptr
			&& BN_sub_word (pm1, 1) == 1 && BN_cmp (y, BN_value_one ()) > 0 && BN_cmp (y, pm1) < 0;
		// select random k
#if IS_X86_64
		validKey = validKey && BN_rand (k, ELGAMAL_FULL_EXPONENT_NUM_BITS, -1, 1) == 1; // full exponent for x64
#else
		validKey = validKey && BN_rand (k, ELGAMAL_SHORT_EXPONENT_NUM_BITS, -1, 1) == 1; // short exponent of 226 bits
#endif
		if (!validKey)
		{
			LogPrint (eLogError, "ElGamal: invalid public key or random exponent");
			memset (encrypted, 0, 514);
			BN_CTX_end (ctx);
			BN_CTX_free (ctx);
			return false;
		}
		// calculate a
		BIGNUM * a;
		if (g_ElggTable)
			a = ElggPow (k, g_ElggTable, ctx);
		else
		{
			a = BN_new ();
			BN_mod_exp (a, elgg, k, elgp, ctx);
		}
		// calculate b1
		bool ok = a && BN_mod_exp (b1, y, k, elgp, ctx) == 1;
		// create m
		uint8_t m[255] = { };
		m[0] = 0xFF;
		memcpy (m + 33, data, 222);
		SHA256 (m + 33, 222, m + 1);
		// calculate b = b1*m mod p
		ok = ok && BN_bin2bn (m, 255, b) && BN_mod_mul (b, b1, b, elgp, ctx) == 1;
		// copy a and b
		encrypted[0] = 0;
		encrypted[257] = 0;
		ok = ok && bn2buf (a, encrypted + 1, 256) && bn2buf (b, encrypted + 258, 256);
		if (!ok) memset (encrypted, 0, 514);
		OPENSSL_cleanse (m, sizeof (m));

		if (a) BN_free (a);
		BN_CTX_end (ctx);
		BN_CTX_free (ctx);
		return ok;
	}

	bool ElGamalDecrypt (const uint8_t * key, const uint8_t * encrypted, uint8_t * data)
	{
		if (!key || !encrypted || !data) return false;
		BN_CTX * ctx = BN_CTX_new ();
		if (!ctx) return false;
		BN_CTX_start (ctx);
		BIGNUM *x = BN_CTX_get (ctx), *a = BN_CTX_get (ctx), *b = BN_CTX_get (ctx);
		if (!x || !a || !b)
		{
			BN_CTX_end (ctx);
			BN_CTX_free (ctx);
			return false;
		}
		if (!BN_bin2bn (key, 256, x) || BN_is_zero (x) || BN_cmp (x, elgp) >= 0)
		{
			// 0 and values >= p are not valid private exponents
			BN_CTX_end (ctx);
			BN_CTX_free (ctx);
			return false;
		}
		BN_sub (x, elgp, x);
		BN_sub_word (x, 1); // x = elgp - x- 1
		if (BN_is_zero (x)) // private exponent p-1 maps to the trivial key y = 1
		{
			BN_CTX_end (ctx);
			BN_CTX_free (ctx);
			return false;
		}
		BN_bin2bn (encrypted + 1, 256, a);
		BN_bin2bn (encrypted + 258, 256, b);
		// m = b*(a^x mod p) mod p
		BN_mod_exp (x, a, x, elgp, ctx);
		BN_mod_mul (b, b, x, elgp, ctx);
		uint8_t m[255] = { };
		if (!bn2buf (b, m, sizeof (m)))
		{
			// b does not fit the 255-byte message space. Previously the
			// uninitialised stack buffer was hashed and compared.
			BN_CTX_end (ctx);
			BN_CTX_free (ctx);
			return false;
		}
		BN_CTX_end (ctx);
		BN_CTX_free (ctx);
		uint8_t hash[32];
		SHA256 (m + 33, 222, hash);
		if (CRYPTO_memcmp (m + 1, hash, 32)) // hash doesn't match, decryption failed
			return false;
		memcpy (data, m + 33, 222);
		return true;
	}

	void GenerateElGamalKeyPair (uint8_t * priv, uint8_t * pub)
	{
		if (!priv || !pub) return;
		// A failed RNG must never turn into an all-zero (or partially random)
		// private exponent that still gets published as a valid key.
		memset (pub, 0, 256);
#if IS_X86 || defined(_MSC_VER)
		bool randomOk = RAND_bytes (priv, 256) == 1;
#else
		// lower 226 bits (28 bytes and 2 bits) only. short exponent
		auto numBytes = (ELGAMAL_SHORT_EXPONENT_NUM_BITS) / 8 + 1; // 29
		auto numZeroBytes = 256 - numBytes;
		bool randomOk = RAND_bytes (priv + numZeroBytes, numBytes) == 1;
		if (randomOk)
		{
			memset (priv, 0, numZeroBytes);
			priv[numZeroBytes] &= 0x03;
		}
#endif
		if (!randomOk)
		{
			OPENSSL_cleanse (priv, 256);
			LogPrint (eLogError, "ElGamal: random key generation failed");
			return;
		}
		BN_CTX * ctx = BN_CTX_new ();
		BIGNUM * p = BN_new ();
		bool ok
			= ctx && p && BN_bin2bn (priv, 256, p) && BN_mod_exp (p, elgg, p, elgp, ctx) == 1 && bn2buf (p, pub, 256);
		if (p) BN_free (p);
		if (ctx) BN_CTX_free (ctx);
		if (!ok)
		{
			OPENSSL_cleanse (priv, 256);
			memset (pub, 0, 256);
			LogPrint (eLogError, "ElGamal: public key generation failed");
		}
	}

	// ECIES
	namespace
	{
		// The I2P ECIES envelope is fixed at 514 bytes: one leading zero,
		// x/y coordinates padded into 256 bytes, the unused byte and 256
		// bytes of CBC ciphertext.
		constexpr size_t ECIES_ENCRYPTED_SIZE = 514;
		// P-521 coordinates are 66 bytes; 64 bytes was too small for the
		// generic ECIES helper even though the I2P default is P-256.
		constexpr size_t ECIES_KEY_BUF_SIZE = 128;

		void ClearECIESOutput (uint8_t * encrypted)
		{
			if (encrypted) memset (encrypted, 0, ECIES_ENCRYPTED_SIZE);
		}
	}

	bool ECIESEncrypt (const EC_GROUP * curve, const EC_POINT * key, const uint8_t * data, uint8_t * encrypted)
	{
		if (!curve || !key || !data || !encrypted || EC_POINT_is_at_infinity (curve, key))
		{
			// Never leave the caller's fixed-size envelope uninitialised or
			// encrypt to the point at infinity (whose shared secret is public).
			ClearECIESOutput (encrypted);
			return false;
		}
		BN_CTX * ctx = BN_CTX_new ();
		if (!ctx)
		{
			ClearECIESOutput (encrypted);
			return false;
		}
		BN_CTX_start (ctx);
		BIGNUM * q = BN_CTX_get (ctx);
		BIGNUM * k = BN_CTX_get (ctx);
		BIGNUM * x = BN_CTX_get (ctx);
		BIGNUM * y = BN_CTX_get (ctx);
		EC_POINT * p = EC_POINT_new (curve);
		bool ok = q && k && x && y && p;
		int len = 0;
		if (ok && EC_GROUP_get_order (curve, q, ctx) == 1)
		{
			len = BN_num_bytes (q);
			ok = len > 0 && static_cast<size_t> (len) <= ECIES_KEY_BUF_SIZE && 2 * len <= 256;
		}
		else
			ok = false;
		if (ok)
		{
			// point for shared secret
			ok = BN_rand_range (k, q) == 1; // 0 < k < q
			ok = ok && EC_POINT_mul (curve, p, k, nullptr, nullptr, ctx) == 1
				&& EC_POINT_get_affine_coordinates (curve, p, x, y, nullptr) == 1;
		}
		if (ok)
		{
			encrypted[0] = 0;
			ok = bn2buf (x, encrypted + 1, len) && bn2buf (y, encrypted + 1 + len, len)
				&& RAND_bytes (encrypted + 1 + 2 * len, 256 - 2 * len) == 1;
		}
		if (ok)
		{
			// encryption key and iv
			ok = EC_POINT_mul (curve, p, nullptr, key, k, ctx) == 1
				&& EC_POINT_get_affine_coordinates (curve, p, x, y, nullptr) == 1;
		}
		if (ok)
		{
			uint8_t keyBuf[ECIES_KEY_BUF_SIZE], iv[ECIES_KEY_BUF_SIZE], shared[32];
			uint8_t m[256] = { };
			ok = bn2buf (x, keyBuf, len) && bn2buf (y, iv, len);
			if (ok)
			{
				SHA256 (keyBuf, len, shared);
				// create buffer
				m[0] = 0xFF;
				m[255] = 0xFF;
				memcpy (m + 33, data, 222);
				SHA256 (m + 33, 222, m + 1);
				// encrypt
				CBCEncryption encryption;
				encryption.SetKey (shared);
				encrypted[257] = 0;
				ok = encryption.EncryptChecked (m, 256, iv, encrypted + 258);
			}
			OPENSSL_cleanse (keyBuf, sizeof (keyBuf));
			OPENSSL_cleanse (iv, sizeof (iv));
			OPENSSL_cleanse (shared, sizeof (shared));
			OPENSSL_cleanse (m, sizeof (m));
		}
		if (!ok) ClearECIESOutput (encrypted);
		if (p) EC_POINT_free (p);
		BN_CTX_end (ctx);
		BN_CTX_free (ctx);
		return ok;
	}

	bool ECIESDecrypt (const EC_GROUP * curve, const BIGNUM * key, const uint8_t * encrypted, uint8_t * data)
	{
		if (!curve || !key || !encrypted || !data) return false;
		bool ret = false;
		BN_CTX * ctx = BN_CTX_new ();
		if (!ctx) return false;
		BN_CTX_start (ctx);
		BIGNUM * q = BN_CTX_get (ctx);
		BIGNUM * x = BN_CTX_get (ctx);
		BIGNUM * y = BN_CTX_get (ctx);
		EC_POINT * p = EC_POINT_new (curve);
		EC_POINT * s = nullptr;
		if (!q || !x || !y || !p)
		{
			if (p) EC_POINT_free (p);
			BN_CTX_end (ctx);
			BN_CTX_free (ctx);
			return false;
		}
		if (EC_GROUP_get_order (curve, q, ctx) != 1)
		{
			EC_POINT_free (p);
			BN_CTX_end (ctx);
			BN_CTX_free (ctx);
			return false;
		}
		const int len = BN_num_bytes (q);
		if (len <= 0 || static_cast<size_t> (len) > ECIES_KEY_BUF_SIZE || 2 * len > 256)
		{
			EC_POINT_free (p);
			BN_CTX_end (ctx);
			BN_CTX_free (ctx);
			return false;
		}
		// point for shared secret
		if (BN_bin2bn (encrypted + 1, len, x) && BN_bin2bn (encrypted + 1 + len, len, y)
			&& EC_POINT_set_affine_coordinates (curve, p, x, y, nullptr) == 1 && !EC_POINT_is_at_infinity (curve, p))
		{
			s = EC_POINT_new (curve);
			if (s && EC_POINT_mul (curve, s, nullptr, p, key, ctx) == 1
				&& EC_POINT_get_affine_coordinates (curve, s, x, y, nullptr) == 1)
			{
				uint8_t keyBuf[ECIES_KEY_BUF_SIZE], iv[ECIES_KEY_BUF_SIZE], shared[32];
				uint8_t m[256] = { };
				if (bn2buf (x, keyBuf, len) && bn2buf (y, iv, len))
				{
					SHA256 (keyBuf, len, shared);
					// decrypt
					CBCDecryption decryption;
					decryption.SetKey (shared);
					if (decryption.DecryptChecked (encrypted + 258, 256, iv, m))
					{
						// verify and copy
						uint8_t hash[32];
						SHA256 (m + 33, 222, hash);
						if (!CRYPTO_memcmp (m + 1, hash, 32))
						{
							memcpy (data, m + 33, 222);
							ret = true;
						}
						else
							LogPrint (eLogError, "ECIES decrypt hash doesn't match");
						OPENSSL_cleanse (hash, sizeof (hash));
					}
				}
				OPENSSL_cleanse (keyBuf, sizeof (keyBuf));
				OPENSSL_cleanse (iv, sizeof (iv));
				OPENSSL_cleanse (shared, sizeof (shared));
				OPENSSL_cleanse (m, sizeof (m));
			}
		}
		else
			LogPrint (eLogError, "ECIES decrypt point is invalid");

		if (s) EC_POINT_free (s);
		EC_POINT_free (p);
		BN_CTX_end (ctx);
		BN_CTX_free (ctx);
		return ret;
	}

	void GenerateECIESKeyPair (const EC_GROUP * curve, BIGNUM *& priv, EC_POINT *& pub)
	{
		priv = nullptr;
		pub = nullptr;
		if (!curve) return;
		BN_CTX * ctx = BN_CTX_new ();
		BIGNUM * q = BN_new ();
		if (!ctx || !q)
		{
			if (q) BN_free (q);
			if (ctx) BN_CTX_free (ctx);
			return;
		}
		if (EC_GROUP_get_order (curve, q, ctx) != 1 || BN_is_zero (q))
		{
			BN_free (q);
			BN_CTX_free (ctx);
			return;
		}
		BIGNUM * key = BN_new ();
		EC_POINT * point = EC_POINT_new (curve);
		bool ok = key && point;
		if (ok)
		{
			// BN_rand_range() may return zero; a zero scalar maps to the
			// point at infinity whose "shared secret" is public.
			do
			{
				if (BN_rand_range (key, q) != 1)
				{
					ok = false;
					break;
				}
			} while (BN_is_zero (key));
		}
		if (ok)
			ok = EC_POINT_mul (curve, point, key, nullptr, nullptr, ctx) == 1
				&& !EC_POINT_is_at_infinity (curve, point);
		if (ok)
		{
			priv = key;
			pub = point;
		}
		else
		{
			if (point) EC_POINT_free (point);
			if (key) BN_clear_free (key);
		}
		BN_free (q);
		BN_CTX_free (ctx);
	}

	// AES
	namespace
	{
		constexpr size_t I2P_AES_BLOCK_SIZE = 16;

		bool IsValidAESLength (size_t len)
		{
			return len <= static_cast<size_t> (std::numeric_limits<int>::max ()) && len % I2P_AES_BLOCK_SIZE == 0;
		}

		void WipeAESOutput (uint8_t * out, size_t len)
		{
			// Never wipe based on an unvalidated length: a caller that passed a
			// huge or misaligned size may not own that much output storage.
			if (out && IsValidAESLength (len) && len) OPENSSL_cleanse (out, len);
		}
	}

	ECBEncryption::ECBEncryption ()
	{
		m_Ctx = EVP_CIPHER_CTX_new ();
	}

	ECBEncryption::~ECBEncryption ()
	{
		if (m_Ctx) EVP_CIPHER_CTX_free (m_Ctx);
		m_Key.Wipe ();
	}

	bool ECBEncryption::EncryptChecked (const uint8_t * in, uint8_t * out)
	{
		if (!m_Ctx || !in || !out)
		{
			if (out) OPENSSL_cleanse (out, I2P_AES_BLOCK_SIZE);
			return false;
		}
		int len = 0, finalLen = 0;
		const bool ok = EVP_EncryptInit_ex (m_Ctx, EVP_aes_256_ecb (), nullptr, m_Key, nullptr) == 1
			&& EVP_CIPHER_CTX_set_padding (m_Ctx, 0) == 1
			&& EVP_EncryptUpdate (m_Ctx, out, &len, in, I2P_AES_BLOCK_SIZE) == 1
			&& len == static_cast<int> (I2P_AES_BLOCK_SIZE) && EVP_EncryptFinal_ex (m_Ctx, out + len, &finalLen) == 1
			&& finalLen == 0;
		if (!ok)
		{
			OPENSSL_cleanse (out, I2P_AES_BLOCK_SIZE);
			LogPrint (eLogError, "AES: ECB encryption failed");
		}
		return ok;
	}

	void ECBEncryption::Encrypt (const uint8_t * in, uint8_t * out)
	{
		(void)EncryptChecked (in, out);
	}

	ECBDecryption::ECBDecryption ()
	{
		m_Ctx = EVP_CIPHER_CTX_new ();
	}

	ECBDecryption::~ECBDecryption ()
	{
		if (m_Ctx) EVP_CIPHER_CTX_free (m_Ctx);
		m_Key.Wipe ();
	}

	bool ECBDecryption::DecryptChecked (const uint8_t * in, uint8_t * out)
	{
		if (!m_Ctx || !in || !out)
		{
			if (out) OPENSSL_cleanse (out, I2P_AES_BLOCK_SIZE);
			return false;
		}
		int len = 0, finalLen = 0;
		const bool ok = EVP_DecryptInit_ex (m_Ctx, EVP_aes_256_ecb (), nullptr, m_Key, nullptr) == 1
			&& EVP_CIPHER_CTX_set_padding (m_Ctx, 0) == 1
			&& EVP_DecryptUpdate (m_Ctx, out, &len, in, I2P_AES_BLOCK_SIZE) == 1
			&& len == static_cast<int> (I2P_AES_BLOCK_SIZE) && EVP_DecryptFinal_ex (m_Ctx, out + len, &finalLen) == 1
			&& finalLen == 0;
		if (!ok)
		{
			OPENSSL_cleanse (out, I2P_AES_BLOCK_SIZE);
			LogPrint (eLogError, "AES: ECB decryption failed");
		}
		return ok;
	}

	void ECBDecryption::Decrypt (const uint8_t * in, uint8_t * out)
	{
		(void)DecryptChecked (in, out);
	}

	CBCEncryption::CBCEncryption ()
	{
		m_Ctx = EVP_CIPHER_CTX_new ();
	}

	CBCEncryption::~CBCEncryption ()
	{
		if (m_Ctx) EVP_CIPHER_CTX_free (m_Ctx);
		m_Key.Wipe ();
	}

	bool CBCEncryption::EncryptChecked (const uint8_t * in, size_t len, const uint8_t * iv, uint8_t * out)
	{
		if (!m_Ctx || !iv || !IsValidAESLength (len) || (len && (!in || !out)))
		{
			WipeAESOutput (out, len);
			return false;
		}
		if (!len) return true;
		int l = 0, finalLen = 0;
		const bool ok = EVP_EncryptInit_ex (m_Ctx, EVP_aes_256_cbc (), nullptr, m_Key, iv) == 1
			&& EVP_CIPHER_CTX_set_padding (m_Ctx, 0) == 1
			&& EVP_EncryptUpdate (m_Ctx, out, &l, in, static_cast<int> (len)) == 1 && l == static_cast<int> (len)
			&& EVP_EncryptFinal_ex (m_Ctx, out + l, &finalLen) == 1 && finalLen == 0;
		if (!ok)
		{
			WipeAESOutput (out, len);
			LogPrint (eLogError, "AES: CBC encryption failed");
		}
		return ok;
	}

	void CBCEncryption::Encrypt (const uint8_t * in, size_t len, const uint8_t * iv, uint8_t * out)
	{
		(void)EncryptChecked (in, len, iv, out);
	}

	CBCDecryption::CBCDecryption ()
	{
		m_Ctx = EVP_CIPHER_CTX_new ();
	}

	CBCDecryption::~CBCDecryption ()
	{
		if (m_Ctx) EVP_CIPHER_CTX_free (m_Ctx);
		m_Key.Wipe ();
	}

	bool CBCDecryption::DecryptChecked (const uint8_t * in, size_t len, const uint8_t * iv, uint8_t * out)
	{
		if (!m_Ctx || !iv || !IsValidAESLength (len) || (len && (!in || !out)))
		{
			WipeAESOutput (out, len);
			return false;
		}
		if (!len) return true;
		int l = 0, finalLen = 0;
		const bool ok = EVP_DecryptInit_ex (m_Ctx, EVP_aes_256_cbc (), nullptr, m_Key, iv) == 1
			&& EVP_CIPHER_CTX_set_padding (m_Ctx, 0) == 1
			&& EVP_DecryptUpdate (m_Ctx, out, &l, in, static_cast<int> (len)) == 1 && l == static_cast<int> (len)
			&& EVP_DecryptFinal_ex (m_Ctx, out + l, &finalLen) == 1 && finalLen == 0;
		if (!ok)
		{
			WipeAESOutput (out, len);
			LogPrint (eLogError, "AES: CBC decryption failed");
		}
		return ok;
	}

	void CBCDecryption::Decrypt (const uint8_t * in, size_t len, const uint8_t * iv, uint8_t * out)
	{
		(void)DecryptChecked (in, len, iv, out);
	}

	void TunnelEncryption::Encrypt (const uint8_t * in, uint8_t * out)
	{
		uint8_t iv[16];
		const bool ok = m_IVEncryption.EncryptChecked (in, iv) // iv
			&& m_LayerEncryption.EncryptChecked (in + 16, i2p::tunnel::TUNNEL_DATA_ENCRYPTED_SIZE, iv, out + 16) // data
			&& m_IVEncryption.EncryptChecked (iv, out); // double iv
		if (!ok)
		{
			// There is no return value. A caller must never forward a partially
			// encrypted tunnel payload.
			if (out) OPENSSL_cleanse (out, 16 + i2p::tunnel::TUNNEL_DATA_ENCRYPTED_SIZE);
			LogPrint (eLogError, "AES: tunnel encryption failed");
		}
	}

	void TunnelDecryption::Decrypt (const uint8_t * in, uint8_t * out)
	{
		uint8_t iv[16];
		const bool ok = m_IVDecryption.DecryptChecked (in, iv) // iv
			&& m_LayerDecryption.DecryptChecked (in + 16, i2p::tunnel::TUNNEL_DATA_ENCRYPTED_SIZE, iv, out + 16) // data
			&& m_IVDecryption.DecryptChecked (iv, out); // double iv
		if (!ok)
		{
			if (out) OPENSSL_cleanse (out, 16 + i2p::tunnel::TUNNEL_DATA_ENCRYPTED_SIZE);
			LogPrint (eLogError, "AES: tunnel decryption failed");
		}
	}

	// AEAD/ChaCha20/Poly1305

	static bool AEADChaCha20Poly1305 (EVP_CIPHER_CTX * ctx, const uint8_t * msg, size_t msgLen, const uint8_t * ad,
		size_t adLen, const uint8_t * key, const uint8_t * nonce, uint8_t * buf, size_t len, bool encrypt)
	{
		if (!ctx || !key || !nonce || !buf) return false;
		// The authentication tag lives at msg + msgLen, so decryption always
		// needs a message pointer even for a zero-length plaintext.
		if (!msg && (!encrypt || msgLen)) return false;
		if (adLen && !ad) return false;
		if (len < msgLen || (encrypt && len - msgLen < 16)) return false;
		// EVP update lengths are int in both OpenSSL and LibreSSL.
		if (msgLen > (size_t)std::numeric_limits<int>::max () || adLen > (size_t)std::numeric_limits<int>::max ())
			return false;
		const size_t outputLen = encrypt ? msgLen + 16 : msgLen;
		bool ok = false;
		int outlen = 0, finalLen = 0;
		if (encrypt)
		{
			ok = EVP_EncryptInit_ex (ctx, EVP_chacha20_poly1305 (), nullptr, nullptr, nullptr) == 1
				&& EVP_CIPHER_CTX_ctrl (ctx, EVP_CTRL_AEAD_SET_IVLEN, 12, nullptr) == 1
				&& EVP_EncryptInit_ex (ctx, nullptr, nullptr, key, nonce) == 1;
			if (ok && adLen) ok = EVP_EncryptUpdate (ctx, nullptr, &outlen, ad, (int)adLen) == 1;
			if (ok)
			{
				outlen = 0;
				ok = !msgLen || EVP_EncryptUpdate (ctx, buf, &outlen, msg, (int)msgLen) == 1;
			}
			if (ok)
			{
				finalLen = 0;
				ok = outlen == (int)msgLen && EVP_EncryptFinal_ex (ctx, buf + outlen, &finalLen) == 1 && finalLen == 0;
			}
			if (ok) ok = EVP_CIPHER_CTX_ctrl (ctx, EVP_CTRL_AEAD_GET_TAG, 16, buf + msgLen) == 1;
		}
		else
		{
			ok = EVP_DecryptInit_ex (ctx, EVP_chacha20_poly1305 (), nullptr, nullptr, nullptr) == 1
				&& EVP_CIPHER_CTX_ctrl (ctx, EVP_CTRL_AEAD_SET_IVLEN, 12, nullptr) == 1
				&& EVP_DecryptInit_ex (ctx, nullptr, nullptr, key, nonce) == 1
				&& EVP_CIPHER_CTX_ctrl (ctx, EVP_CTRL_AEAD_SET_TAG, 16, (void *)(msg + msgLen)) == 1;
			if (ok && adLen) ok = EVP_DecryptUpdate (ctx, nullptr, &outlen, ad, (int)adLen) == 1;
			if (ok)
			{
				outlen = 0;
				ok = !msgLen || EVP_DecryptUpdate (ctx, buf, &outlen, msg, (int)msgLen) == 1;
			}
			if (ok)
			{
				finalLen = 0;
				ok = outlen == (int)msgLen && EVP_DecryptFinal_ex (ctx, buf + outlen, &finalLen) == 1 && finalLen == 0;
			}
		}
		// OpenSSL may have written unauthenticated plaintext (or partial
		// ciphertext) before the tag check failed. Never expose it.
		if (!ok && outputLen) OPENSSL_cleanse (buf, outputLen);
		return ok;
	}

	bool AEADChaCha20Poly1305 (const uint8_t * msg, size_t msgLen, const uint8_t * ad, size_t adLen,
		const uint8_t * key, const uint8_t * nonce, uint8_t * buf, size_t len, bool encrypt)
	{
		EVP_CIPHER_CTX * ctx = EVP_CIPHER_CTX_new ();
		auto ret = AEADChaCha20Poly1305 (ctx, msg, msgLen, ad, adLen, key, nonce, buf, len, encrypt);
		EVP_CIPHER_CTX_free (ctx);
		return ret;
	}

	AEADChaCha20Poly1305Encryptor::AEADChaCha20Poly1305Encryptor ()
	{
		m_Ctx = EVP_CIPHER_CTX_new ();
	}

	AEADChaCha20Poly1305Encryptor::~AEADChaCha20Poly1305Encryptor ()
	{
		if (m_Ctx) EVP_CIPHER_CTX_free (m_Ctx);
	}

	bool AEADChaCha20Poly1305Encryptor::Encrypt (const uint8_t * msg, size_t msgLen, const uint8_t * ad, size_t adLen,
		const uint8_t * key, const uint8_t * nonce, uint8_t * buf, size_t len)
	{
		return AEADChaCha20Poly1305 (m_Ctx, msg, msgLen, ad, adLen, key, nonce, buf, len, true);
	}

	bool AEADChaCha20Poly1305Encryptor::Encrypt (const std::vector<std::pair<uint8_t *, size_t>> & bufs,
		const uint8_t * key, const uint8_t * nonce, uint8_t * mac)
	{
		auto fail = [&] () -> bool
		{
			// A partially updated in-place buffer may contain ciphertext for
			// earlier entries and plaintext for later ones. Never let the
			// caller transmit that frame with a zero tag; wipe every output
			// buffer and report failure.
			for (const auto & it : bufs)
				if (it.first && it.second) OPENSSL_cleanse (it.first, it.second);
			if (mac) OPENSSL_cleanse (mac, 16);
			LogPrint (eLogError, "AEAD: multi-buffer encryption failed");
			return false;
		};
		if (bufs.empty () || !m_Ctx || !key || !nonce || !mac) return fail ();
		bool ok = EVP_EncryptInit_ex (m_Ctx, EVP_chacha20_poly1305 (), nullptr, nullptr, nullptr) == 1
			&& EVP_CIPHER_CTX_ctrl (m_Ctx, EVP_CTRL_AEAD_SET_IVLEN, 12, nullptr) == 1
			&& EVP_EncryptInit_ex (m_Ctx, nullptr, nullptr, key, nonce) == 1;
		for (const auto & it : bufs)
		{
			if (!ok) break;
			if (!it.second) continue;
			if (it.second > (size_t)std::numeric_limits<int>::max () || !it.first)
			{
				ok = false;
				break;
			}
			int outlen = 0;
			ok = EVP_EncryptUpdate (m_Ctx, it.first, &outlen, it.first, (int)it.second) == 1
				&& outlen == (int)it.second;
		}
		if (ok)
		{
			uint8_t finalBuf[16];
			int outlen = 0;
			ok = EVP_EncryptFinal_ex (m_Ctx, finalBuf, &outlen) == 1 && outlen == 0
				&& EVP_CIPHER_CTX_ctrl (m_Ctx, EVP_CTRL_AEAD_GET_TAG, 16, mac) == 1;
		}
		return ok ? true : fail ();
	}

	AEADChaCha20Poly1305Decryptor::AEADChaCha20Poly1305Decryptor ()
	{
		m_Ctx = EVP_CIPHER_CTX_new ();
	}

	AEADChaCha20Poly1305Decryptor::~AEADChaCha20Poly1305Decryptor ()
	{
		if (m_Ctx) EVP_CIPHER_CTX_free (m_Ctx);
	}

	bool AEADChaCha20Poly1305Decryptor::Decrypt (const uint8_t * msg, size_t msgLen, const uint8_t * ad, size_t adLen,
		const uint8_t * key, const uint8_t * nonce, uint8_t * buf, size_t len)
	{
		return AEADChaCha20Poly1305 (m_Ctx, msg, msgLen, ad, adLen, key, nonce, buf, len, false);
	}

	static bool ChaCha20 (EVP_CIPHER_CTX * ctx, const uint8_t * msg, size_t msgLen, const uint8_t * key,
		const uint8_t * nonce, uint8_t * out)
	{
		if (!ctx || !key || !nonce || (msgLen && (!msg || !out)) || msgLen > (size_t)std::numeric_limits<int>::max ())
			return false;
		uint32_t iv[4];
		iv[0] = htole32 (1);
		memcpy (iv + 1, nonce, 12); // counter | nonce
		if (EVP_EncryptInit_ex (ctx, EVP_chacha20 (), NULL, key, (const uint8_t *)iv) != 1) return false;
		int outlen = 0;
		if (msgLen && EVP_EncryptUpdate (ctx, out, &outlen, msg, (int)msgLen) != 1) return false;
		if (outlen != (int)msgLen) return false;
		int finalLen = 0;
		uint8_t finalBuf[16];
		return EVP_EncryptFinal_ex (ctx, finalBuf, &finalLen) == 1 && finalLen == 0;
	}

	static void ChaCha20AndWipeOnFailure (EVP_CIPHER_CTX * ctx, const uint8_t * msg, size_t msgLen, const uint8_t * key,
		const uint8_t * nonce, uint8_t * out)
	{
		if (!ChaCha20 (ctx, msg, msgLen, key, nonce, out))
		{
			// No return value: never leave a partially transformed buffer
			// (which can be plaintext) for the caller to transmit.
			if (out && msgLen) OPENSSL_cleanse (out, msgLen);
			LogPrint (eLogError, "ChaCha20: encryption failed");
		}
	}

	void ChaCha20 (const uint8_t * msg, size_t msgLen, const uint8_t * key, const uint8_t * nonce, uint8_t * out)
	{
		EVP_CIPHER_CTX * ctx = EVP_CIPHER_CTX_new ();
		ChaCha20AndWipeOnFailure (ctx, msg, msgLen, key, nonce, out);
		EVP_CIPHER_CTX_free (ctx);
	}

	ChaCha20Context::ChaCha20Context ()
	{
		m_Ctx = EVP_CIPHER_CTX_new ();
	}

	ChaCha20Context::~ChaCha20Context ()
	{
		if (m_Ctx) EVP_CIPHER_CTX_free (m_Ctx);
	}

	void ChaCha20Context::operator() (
		const uint8_t * msg, size_t msgLen, const uint8_t * key, const uint8_t * nonce, uint8_t * out)
	{
		ChaCha20AndWipeOnFailure (m_Ctx, msg, msgLen, key, nonce, out);
	}

	void HKDF (
		const uint8_t * salt, const uint8_t * key, size_t keyLen, std::string_view info, uint8_t * out, size_t outLen)
	{
		HKDFContext ctx;
		ctx (salt, key, keyLen, info, out, outLen);
	}

	HKDFContext::HKDFContext ()
	{
#if I2PD_HAVE_EVP_KDF_FETCH
		EVP_KDF * kdf = EVP_KDF_fetch (nullptr, "HKDF", nullptr);
		m_KdfCtx = kdf ? EVP_KDF_CTX_new (kdf) : nullptr;
		EVP_KDF_free (kdf);
#else
		m_Ctx = EVP_PKEY_CTX_new_id (EVP_PKEY_HKDF, nullptr);
#endif
	}

	HKDFContext::~HKDFContext ()
	{
#if I2PD_HAVE_EVP_KDF_FETCH
		if (m_KdfCtx) EVP_KDF_CTX_free (m_KdfCtx);
#else
		if (m_Ctx) EVP_PKEY_CTX_free (m_Ctx);
#endif
	}

	void HKDFContext::operator() (
		const uint8_t * salt, const uint8_t * key, size_t keyLen, std::string_view info, uint8_t * out, size_t outLen)
	{
		// RFC 5869 allows at most 255 * HashLen output bytes.
		constexpr size_t MAX_HKDF_OUTPUT = 255 * 32;
		if (!out || !outLen || outLen > MAX_HKDF_OUTPUT || !salt || (!key && keyLen))
		{
			if (out && outLen) OPENSSL_cleanse (out, outLen);
			return;
		}
#if I2PD_HAVE_EVP_KDF_FETCH
		if (!m_KdfCtx)
		{
			OPENSSL_cleanse (out, outLen);
			return;
		}
		uint8_t tempKey[32];
		const bool zeroKey = !key || !keyLen;
		if (zeroKey)
		{
			unsigned int len = 0;
			if (!HMAC (EVP_sha256 (), salt, 32, nullptr, 0, tempKey, &len) || len != sizeof (tempKey))
			{
				OPENSSL_cleanse (tempKey, sizeof (tempKey));
				OPENSSL_cleanse (out, outLen);
				LogPrint (eLogError, "HKDF: expand-only PRK derivation failed");
				return;
			}
		}
		OSSL_PARAM params[6];
		size_t n = 0;
		params[n++] = OSSL_PARAM_construct_utf8_string (OSSL_KDF_PARAM_DIGEST, const_cast<char *> ("SHA256"), 0);
		params[n++] = OSSL_PARAM_construct_utf8_string (
			OSSL_KDF_PARAM_MODE, const_cast<char *> (zeroKey ? "EXPAND_ONLY" : "EXTRACT_AND_EXPAND"), 0);
		if (!zeroKey)
		{
			params[n++] = OSSL_PARAM_construct_octet_string (OSSL_KDF_PARAM_SALT, const_cast<uint8_t *> (salt), 32);
			params[n++] = OSSL_PARAM_construct_octet_string (OSSL_KDF_PARAM_KEY, const_cast<uint8_t *> (key), keyLen);
		}
		else
			params[n++] = OSSL_PARAM_construct_octet_string (OSSL_KDF_PARAM_KEY, tempKey, sizeof (tempKey));
		if (!info.empty ())
			params[n++] = OSSL_PARAM_construct_octet_string (
				OSSL_KDF_PARAM_INFO, const_cast<char *> (info.data ()), info.size ());
		params[n] = OSSL_PARAM_construct_end ();
		const int ok = EVP_KDF_derive (m_KdfCtx, out, outLen, params);
		OPENSSL_cleanse (tempKey, sizeof (tempKey));
		if (ok != 1)
		{
			OPENSSL_cleanse (out, outLen);
			LogPrint (eLogError, "HKDF: derivation failed");
		}
#else
		if (!m_Ctx || keyLen > (size_t)std::numeric_limits<int>::max ()
			|| info.size () > (size_t)std::numeric_limits<int>::max ())
		{
			OPENSSL_cleanse (out, outLen);
			return;
		}
		bool ok = EVP_PKEY_derive_init (m_Ctx) == 1 && EVP_PKEY_CTX_set_hkdf_md (m_Ctx, EVP_sha256 ()) == 1;
		if (ok && key && keyLen)
		{
			ok = EVP_PKEY_CTX_set1_hkdf_salt (m_Ctx, salt, 32) == 1
				&& EVP_PKEY_CTX_set1_hkdf_key (m_Ctx, key, (int)keyLen) == 1;
		}
		else if (ok)
		{
			// zerolen
			ok = EVP_PKEY_CTX_hkdf_mode (m_Ctx, EVP_PKEY_HKDEF_MODE_EXPAND_ONLY) == 1;
			uint8_t tempKey[32];
			unsigned int len = 0;
			if (ok)
			{
				ok = HMAC (EVP_sha256 (), salt, 32, nullptr, 0, tempKey, &len) != nullptr && len == 32;
				if (ok) ok = EVP_PKEY_CTX_set1_hkdf_key (m_Ctx, tempKey, (int)len) == 1;
			}
			OPENSSL_cleanse (tempKey, sizeof (tempKey));
		}
		if (ok && !info.empty ())
			ok = EVP_PKEY_CTX_add1_hkdf_info (m_Ctx, (const uint8_t *)info.data (), (int)info.size ()) == 1;
		if (ok)
		{
			size_t derived = outLen;
			ok = EVP_PKEY_derive (m_Ctx, out, &derived) == 1 && derived == outLen;
		}
		if (!ok)
		{
			OPENSSL_cleanse (out, outLen);
			LogPrint (eLogError, "HKDF: derivation failed");
		}
#endif
	}

	// Noise

	void NoiseSymmetricState::Init (const uint8_t * ck, const uint8_t * hh, const uint8_t * pub)
	{
		// pub is Bob's public static key, hh = SHA256(h)
		if (!ck || !hh || !pub)
		{
			OPENSSL_cleanse (m_CK, sizeof (m_CK));
			memset (m_H, 0, sizeof (m_H));
			m_N = 0;
			return;
		}
		// m_CK[32:63] becomes the cipher key only after MixKey(). Clear it so
		// a caller that reads the state early cannot observe stale bytes.
		OPENSSL_cleanse (m_CK, sizeof (m_CK));
		memcpy (m_CK, ck, 32);

		EVP_MD_CTX * ctx = EVP_MD_CTX_new ();
		if (!ctx || EVP_DigestInit_ex (ctx, EVP_sha256 (), NULL) != 1 || EVP_DigestUpdate (ctx, hh, 32) != 1
			|| EVP_DigestUpdate (ctx, pub, 32) != 1
			|| EVP_DigestFinal_ex (ctx, m_H, nullptr) != 1) // h = MixHash(pub) = SHA256(hh || pub)
		{
			memset (m_H, 0, sizeof (m_H));
			LogPrint (eLogError, "Noise: failed to initialise symmetric state");
		}
		EVP_MD_CTX_free (ctx);
		m_N = 0;
	}

	void NoiseSymmetricState::MixHash (const uint8_t * buf, size_t len)
	{
		if (len && !buf) return;
		EVP_MD_CTX * ctx = EVP_MD_CTX_new ();
		bool ok = ctx && EVP_DigestInit_ex (ctx, EVP_sha256 (), NULL) == 1 && EVP_DigestUpdate (ctx, m_H, 32) == 1
			&& (!len || EVP_DigestUpdate (ctx, buf, len) == 1) && EVP_DigestFinal_ex (ctx, m_H, nullptr) == 1;
		if (!ok) LogPrint (eLogError, "Noise: MixHash failed");
		EVP_MD_CTX_free (ctx);
	}

	void NoiseSymmetricState::MixHash (const std::vector<std::pair<uint8_t *, size_t>> & bufs)
	{
		EVP_MD_CTX * ctx = EVP_MD_CTX_new ();
		bool ok = ctx && EVP_DigestInit_ex (ctx, EVP_sha256 (), NULL) == 1 && EVP_DigestUpdate (ctx, m_H, 32) == 1;
		for (const auto & it : bufs)
		{
			if (!ok) break;
			if (it.second && !it.first)
			{
				ok = false;
				break;
			}
			ok = !it.second || EVP_DigestUpdate (ctx, it.first, it.second) == 1;
		}
		ok = ok && EVP_DigestFinal_ex (ctx, m_H, nullptr) == 1;
		if (!ok) LogPrint (eLogError, "Noise: MixHash failed");
		EVP_MD_CTX_free (ctx);
	}

	void NoiseSymmetricState::MixKey (const uint8_t * sharedSecret)
	{
		if (!sharedSecret)
		{
			LogPrint (eLogError, "Noise: MixKey called without a shared secret");
			return;
		}
		HKDF (m_CK, sharedSecret, 32, "", m_CK);
		// new ck is m_CK[0:31], key is m_CK[32:63]
		m_N = 0;
	}

	bool NoiseSymmetricState::Encrypt (const uint8_t * in, uint8_t * out, size_t len)
	{
		if (!in || !out || len > std::numeric_limits<size_t>::max () - 16
			|| m_N == std::numeric_limits<uint64_t>::max ())
			return false;
		uint8_t nonce[12];
		if (m_N)
		{
			memset (nonce, 0, 4);
			htole64buf (nonce + 4, m_N);
		}
		else
			memset (nonce, 0, 12);
		auto ret = AEADChaCha20Poly1305 (in, len, m_H, 32, m_CK + 32, nonce, out, len + 16, true);
		if (ret) m_N++;
		return ret;
	}

	bool NoiseSymmetricState::Decrypt (const uint8_t * in, uint8_t * out, size_t len)
	{
		// Java's ChaChaPolyCipherState rejects nonce 2^64-1 before use.
		if (!in || !out || m_N == std::numeric_limits<uint64_t>::max ()) return false;
		uint8_t nonce[12];
		if (m_N)
		{
			memset (nonce, 0, 4);
			htole64buf (nonce + 4, m_N);
		}
		else
			memset (nonce, 0, 12);
		auto ret = AEADChaCha20Poly1305 (in, len, m_H, 32, m_CK + 32, nonce, out, len, false);
		if (ret) m_N++;
		return ret;
	}

	void InitNoiseNState (NoiseSymmetricState & state, const uint8_t * pub)
	{
		static constexpr char protocolName[] = "Noise_N_25519_ChaChaPoly_SHA256"; // 31 chars
		static constexpr uint8_t hh[32] = { 0x69, 0x4d, 0x52, 0x44, 0x5a, 0x27, 0xd9, 0xad, 0xfa, 0xd2, 0x9c, 0x76,
			0x32, 0x39, 0x5d, 0xc1, 0xe4, 0x35, 0x4c, 0x69, 0xb4, 0xf9, 0x2e, 0xac, 0x8a, 0x1e, 0xe4, 0x6a, 0x9e, 0xd2,
			0x15, 0x54 }; // hh = SHA256(protocol_name || 0)
		state.Init ((const uint8_t *)protocolName, hh, pub); // ck = protocol_name || 0
	}

	void InitNoiseXKState (NoiseSymmetricState & state, const uint8_t * pub)
	{
		static constexpr uint8_t protocolNameHash[32] = { 0x72, 0xe8, 0x42, 0xc5, 0x45, 0xe1, 0x80, 0x80, 0xd3, 0x9c,
			0x44, 0x93, 0xbb, 0x91, 0xd7, 0xed, 0xf2, 0x28, 0x98, 0x17, 0x71, 0x21, 0x8c, 0x1f, 0x62, 0x4e, 0x20, 0x6f,
			0x28, 0xd3, 0x2f, 0x71 }; // SHA256 ("Noise_XKaesobfse+hs2+hs3_25519_ChaChaPoly_SHA256")
		static constexpr uint8_t hh[32] = { 0x49, 0xff, 0x48, 0x3f, 0xc4, 0x04, 0xb9, 0xb2, 0x6b, 0x11, 0x94, 0x36,
			0x72, 0xff, 0x05, 0xb5, 0x61, 0x27, 0x03, 0x31, 0xba, 0x89, 0xb8, 0xfc, 0x33, 0x15, 0x93, 0x87, 0x57, 0xdd,
			0x3d, 0x1e }; // SHA256 (protocolNameHash)
		state.Init (protocolNameHash, hh, pub);
	}

	void InitNoiseXKState1 (NoiseSymmetricState & state, const uint8_t * pub)
	{
		static constexpr uint8_t protocolNameHash[32] = { 0xb1, 0x37, 0x22, 0x81, 0x74, 0x23, 0xa8, 0xfd, 0xf4, 0x2d,
			0xf2, 0xe6, 0x0e, 0xd1, 0xed, 0xf4, 0x1b, 0x93, 0x07, 0x1d, 0xb1, 0xec, 0x24, 0xa3, 0x67, 0xf7, 0x84, 0xec,
			0x27, 0x0d, 0x81, 0x32 }; // SHA256 ("Noise_XKchaobfse+hs1+hs2+hs3_25519_ChaChaPoly_SHA256")
		static constexpr uint8_t hh[32] = { 0xdc, 0x85, 0xe6, 0xaf, 0x7b, 0x02, 0x65, 0x0c, 0xf1, 0xf9, 0x0d, 0x71,
			0xfb, 0xc6, 0xd4, 0x53, 0xa7, 0xcf, 0x6d, 0xbf, 0xbd, 0x52, 0x5e, 0xa5, 0xb5, 0x79, 0x1c, 0x47, 0xb3, 0x5e,
			0xbc, 0x33 }; // SHA256 (protocolNameHash)
		state.Init (protocolNameHash, hh, pub);
	}

	void InitNoiseIKState (NoiseSymmetricState & state, const uint8_t * pub)
	{
		static constexpr uint8_t protocolNameHash[32] = { 0x4c, 0xaf, 0x11, 0xef, 0x2c, 0x8e, 0x36, 0x56, 0x4c, 0x53,
			0xe8, 0x88, 0x85, 0x06, 0x4d, 0xba, 0xac, 0xbe, 0x00, 0x54, 0xad, 0x17, 0x8f, 0x80, 0x79, 0xa6, 0x46, 0x82,
			0x7e, 0x6e, 0xe4, 0x0c }; // SHA256("Noise_IKelg2+hs2_25519_ChaChaPoly_SHA256"), 40 bytes
		static constexpr uint8_t hh[32] = { 0x9c, 0xcf, 0x85, 0x2c, 0xc9, 0x3b, 0xb9, 0x50, 0x44, 0x41, 0xe9, 0x50,
			0xe0, 0x1d, 0x52, 0x32, 0x2e, 0x0d, 0x47, 0xad, 0xd1, 0xe9, 0xa5, 0x55, 0xf7, 0x55, 0xb5, 0x69, 0xae, 0x18,
			0x3b, 0x5c }; // SHA256 (protocolNameHash)
		state.Init (protocolNameHash, hh, pub);
	}

	// init and terminate

	/*	std::vector <std::unique_ptr<std::mutex> > m_OpenSSLMutexes;
	static void OpensslLockingCallback(int mode, int type, const char * file, int line)
	{
		if (type > 0 && (size_t)type < m_OpenSSLMutexes.size ())
		{
			if (mode & CRYPTO_LOCK)
				m_OpenSSLMutexes[type]->lock ();
			else
				m_OpenSSLMutexes[type]->unlock ();
		}
	}*/

	namespace
	{
		[[maybe_unused]] bool FetchDigest (const char * name)
		{
#if I2PD_HAVE_EVP_MD_FETCH
			EVP_MD * md = EVP_MD_fetch (nullptr, name, nullptr);
			if (!md) return false;
			EVP_MD_free (md);
			return true;
#else
			(void)name;
			return false;
#endif
		}

		[[maybe_unused]] bool FetchCipher (const char * name)
		{
#if I2PD_HAVE_EVP_CIPHER_FETCH
			EVP_CIPHER * cipher = EVP_CIPHER_fetch (nullptr, name, nullptr);
			if (!cipher) return false;
			EVP_CIPHER_free (cipher);
			return true;
#else
			(void)name;
			return false;
#endif
		}

		[[maybe_unused]] bool FetchPkeyAlgorithm (const char * name)
		{
#ifndef LIBRESSL_VERSION_NUMBER
			EVP_PKEY_CTX * ctx = EVP_PKEY_CTX_new_from_name (nullptr, name, nullptr);
			if (!ctx) return false;
			EVP_PKEY_CTX_free (ctx);
			return true;
#else
			(void)name;
			return true; // LibreSSL uses its legacy Ed25519 implementation
#endif
		}

		bool FetchHKDF ()
		{
#if I2PD_HAVE_EVP_KDF_FETCH
			EVP_KDF * kdf = EVP_KDF_fetch (nullptr, "HKDF", nullptr);
			if (!kdf) return false;
			EVP_KDF_CTX * ctx = EVP_KDF_CTX_new (kdf);
			const bool available = ctx != nullptr;
			EVP_KDF_CTX_free (ctx);
			EVP_KDF_free (kdf);
			return available;
#else
#ifdef EVP_PKEY_HKDF
			EVP_PKEY_CTX * ctx = EVP_PKEY_CTX_new_id (EVP_PKEY_HKDF, nullptr);
			if (!ctx) return false;
			EVP_PKEY_CTX_free (ctx);
			return true;
#else
			return false;
#endif
#endif
		}

		bool FetchSipHash ()
		{
#if OPENSSL_SIPHASH
			EVP_PKEY * key = EVP_PKEY_new_raw_private_key (
				EVP_PKEY_SIPHASH, nullptr, reinterpret_cast<const unsigned char *> ("0123456789abcdef"), 16);
			if (!key) return false;
			EVP_MD_CTX * md = EVP_MD_CTX_new ();
			EVP_PKEY_CTX * pctx = nullptr;
			size_t macSize = 16;
			OSSL_PARAM params[2]
				= { OSSL_PARAM_construct_size_t (OSSL_MAC_PARAM_SIZE, &macSize), OSSL_PARAM_construct_end () };
			uint8_t input[32] = { 0 }, output[16] = { 0 };
			size_t outputLen = sizeof (output);
			bool ok = md && EVP_DigestSignInit (md, &pctx, nullptr, nullptr, key) == 1
				&& EVP_PKEY_CTX_set_params (pctx, params) == 1
				&& EVP_DigestSign (md, output, &outputLen, input, sizeof (input)) == 1 && outputLen == sizeof (output);
			if (md) EVP_MD_CTX_free (md);
			EVP_PKEY_free (key);
			OPENSSL_cleanse (output, sizeof (output));
			return ok;
#else
			// The project-owned SipHash-2-4 implementation is the fallback for
			// LibreSSL and for OpenSSL builds without this provider algorithm.
			return true;
#endif
		}

		CryptoCapabilities ProbeCryptoCapabilities ()
		{
			CryptoCapabilities capabilities { };
#if I2PD_HAVE_EVP_MD_FETCH
			capabilities.sha1 = FetchDigest ("SHA1");
			capabilities.sha256 = FetchDigest ("SHA256");
			capabilities.sha512 = FetchDigest ("SHA512");
#else
			capabilities.sha1 = EVP_sha1 () != nullptr;
			capabilities.sha256 = EVP_sha256 () != nullptr;
			capabilities.sha512 = EVP_sha512 () != nullptr;
#endif
#if I2PD_HAVE_EVP_CIPHER_FETCH
			capabilities.aes256Cbc = FetchCipher ("AES-256-CBC");
			capabilities.aes256Ecb = FetchCipher ("AES-256-ECB");
			capabilities.aes256Ctr = FetchCipher ("AES-256-CTR");
			capabilities.chacha20 = FetchCipher ("ChaCha20");
			capabilities.chacha20Poly1305 = FetchCipher ("ChaCha20-Poly1305");
#else
			capabilities.aes256Cbc = EVP_aes_256_cbc () != nullptr;
			capabilities.aes256Ecb = EVP_aes_256_ecb () != nullptr;
			capabilities.aes256Ctr = EVP_aes_256_ctr () != nullptr;
			capabilities.chacha20 = EVP_chacha20 () != nullptr;
			capabilities.chacha20Poly1305 = EVP_chacha20_poly1305 () != nullptr;
#endif
			capabilities.hkdf = FetchHKDF ();
			capabilities.sipHash = FetchSipHash ();
			capabilities.ed25519 = FetchPkeyAlgorithm ("ED25519");
			return capabilities;
		}

		CryptoCapabilities g_CryptoCapabilities { };
		std::once_flag g_CryptoCapabilitiesOnce;
	}

	const CryptoCapabilities & GetCryptoCapabilities ()
	{
		std::call_once (g_CryptoCapabilitiesOnce, [] () { g_CryptoCapabilities = ProbeCryptoCapabilities (); });
		return g_CryptoCapabilities;
	}

	bool InitCrypto (bool precomputation)
	{
		/*		auto numLocks = CRYPTO_num_locks();
		for (int i = 0; i < numLocks; i++)
			m_OpenSSLMutexes.emplace_back (new std::mutex);
		CRYPTO_set_locking_callback (OpensslLockingCallback);*/
		const auto & capabilities = GetCryptoCapabilities ();
		if (!capabilities.sha256 || !capabilities.aes256Cbc || !capabilities.aes256Ecb || !capabilities.chacha20
			|| !capabilities.chacha20Poly1305 || !capabilities.hkdf || !capabilities.ed25519
			|| (OPENSSL_SIPHASH && !capabilities.sipHash))
		{
			LogPrint (eLogCritical,
				"Crypto: active libcrypto provider is missing a required algorithm "
				"(SHA256/AES-256-CBC/AES-256-ECB/ChaCha20/ChaCha20-Poly1305/HKDF/Ed25519/SipHash)");
			return false;
		}
		if (precomputation)
		{
#if IS_X86_64
			g_ElggTable = new BIGNUM *[ELGAMAL_FULL_EXPONENT_NUM_BYTES][255];
			PrecalculateElggTable (g_ElggTable, ELGAMAL_FULL_EXPONENT_NUM_BYTES);
#else
			g_ElggTable = new BIGNUM *[ELGAMAL_SHORT_EXPONENT_NUM_BYTES][255];
			PrecalculateElggTable (g_ElggTable, ELGAMAL_SHORT_EXPONENT_NUM_BYTES);
#endif
		}
		return true;
	}

	void TerminateCrypto ()
	{
		if (g_ElggTable)
		{
			DestroyElggTable (g_ElggTable,
#if IS_X86_64
				ELGAMAL_FULL_EXPONENT_NUM_BYTES
#else
				ELGAMAL_SHORT_EXPONENT_NUM_BYTES
#endif
			);
			delete[] g_ElggTable;
			g_ElggTable = nullptr;
		}
		/*		CRYPTO_set_locking_callback (nullptr);
		m_OpenSSLMutexes.clear ();*/
	}
}
}
