/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: CryptoKey.
 *
 * Wraps the I2P asymmetric key types as encryptor/decryptor objects: ElGamal,
 * ECIES-P256, ECIES-GOST R 34.10 and the ECIES-X25519 ratchet key agreement.
 * Key import rejects invalid curve points, secret material is cleared on
 * destruction or generation failure, and LocalEncryptionKey owns a local
 * destination pair whose decryptor is created lazily.
 *
 */

#include <string.h>
#include <algorithm>
#include <openssl/crypto.h>
#include "Log.h"
#include "Gost.h"
#include "CryptoKey.h"

namespace i2p
{
namespace crypto
{
	ElGamalEncryptor::ElGamalEncryptor (const uint8_t * pub)
	{
		if (pub)
			memcpy (m_PublicKey, pub, 256);
		else
			memset (m_PublicKey, 0, sizeof (m_PublicKey)); // rejected by ElGamalEncrypt
	}

	bool ElGamalEncryptor::Encrypt (const uint8_t * data, uint8_t * encrypted)
	{
		if (!data || !encrypted) return false;
		return ElGamalEncrypt (m_PublicKey, data, encrypted);
	}

	ElGamalDecryptor::ElGamalDecryptor (const uint8_t * priv)
	{
		if (priv)
			memcpy (m_PrivateKey, priv, 256);
		else
			memset (m_PrivateKey, 0, sizeof (m_PrivateKey));
	}

	ElGamalDecryptor::~ElGamalDecryptor ()
	{
		OPENSSL_cleanse (m_PrivateKey, sizeof (m_PrivateKey));
	}

	bool ElGamalDecryptor::Decrypt (const uint8_t * encrypted, uint8_t * data)
	{
		return ElGamalDecrypt (m_PrivateKey, encrypted, data);
	}

	namespace
	{
		constexpr size_t ECIES_ENCRYPTED_SIZE = 514;

		void ClearECIESOutput (uint8_t * encrypted)
		{
			if (encrypted) memset (encrypted, 0, ECIES_ENCRYPTED_SIZE);
		}

		/**
		 * @brief Decode an I2P 64-byte X||Y point onto an OpenSSL curve.
		 * @param curve group the point belongs to.
		 * @param point pre-allocated destination point.
		 * @param pub 64-byte big-endian affine coordinates.
		 * @return false for a null argument, a provider failure, an off-curve
		 *         point or the point at infinity.
		 *
		 * I2P transmits affine coordinates without an uncompressed-point tag, so
		 * the encoding itself carries no curve information and the caller must
		 * already know which group is expected. OpenSSL validates that the
		 * coordinates satisfy the curve equation in
		 * EC_POINT_set_affine_coordinates(); the point at infinity is rejected
		 * explicitly. No separate subgroup check is performed, which matches the I2P
		 * key formats (P-256 has cofactor 1) and keeps the accepted encodings
		 * identical to Java's. The coordinates are public input, so the temporary
		 * BIGNUMs are freed without cleansing.
		 */
		bool LoadECPublicKey (const EC_GROUP * curve, EC_POINT * point, const uint8_t * pub)
		{
			if (!curve || !point || !pub) return false;
			BIGNUM * x = BN_bin2bn (pub, 32, nullptr);
			BIGNUM * y = BN_bin2bn (pub + 32, 32, nullptr);
			bool valid = x && y && EC_POINT_set_affine_coordinates (curve, point, x, y, nullptr) == 1
				&& !EC_POINT_is_at_infinity (curve, point);
			BN_free (x);
			BN_free (y);
			return valid;
		}
	}

	ECIESP256Encryptor::ECIESP256Encryptor (const uint8_t * pub)
	{
		m_Curve = EC_GROUP_new_by_curve_name (NID_X9_62_prime256v1);
		m_PublicKey = m_Curve ? EC_POINT_new (m_Curve) : nullptr;
		if (!LoadECPublicKey (m_Curve, m_PublicKey, pub))
		{
			LogPrint (eLogError, "ECIES P256 invalid public key");
			if (m_PublicKey) EC_POINT_free (m_PublicKey);
			m_PublicKey = nullptr;
		}
	}

	ECIESP256Encryptor::~ECIESP256Encryptor ()
	{
		if (m_PublicKey) EC_POINT_free (m_PublicKey);
		if (m_Curve) EC_GROUP_free (m_Curve);
	}

	bool ECIESP256Encryptor::Encrypt (const uint8_t * data, uint8_t * encrypted)
	{
		if (m_Curve && m_PublicKey && data && encrypted) return ECIESEncrypt (m_Curve, m_PublicKey, data, encrypted);
		ClearECIESOutput (encrypted);
		return false;
	}

	ECIESP256Decryptor::ECIESP256Decryptor (const uint8_t * priv)
	{
		m_Curve = EC_GROUP_new_by_curve_name (NID_X9_62_prime256v1);
		m_PrivateKey = priv ? BN_bin2bn (priv, 32, nullptr) : nullptr;
	}

	ECIESP256Decryptor::~ECIESP256Decryptor ()
	{
		if (m_PrivateKey) BN_clear_free (m_PrivateKey);
		if (m_Curve) EC_GROUP_free (m_Curve);
	}

	bool ECIESP256Decryptor::Decrypt (const uint8_t * encrypted, uint8_t * data)
	{
		if (m_Curve && m_PrivateKey) return ECIESDecrypt (m_Curve, m_PrivateKey, encrypted, data);
		return false;
	}

	void CreateECIESP256RandomKeys (uint8_t * priv, uint8_t * pub)
	{
		if (!priv || !pub) return;
		memset (priv, 0, 256);
		memset (pub, 0, 256);
		EC_GROUP * curve = EC_GROUP_new_by_curve_name (NID_X9_62_prime256v1);
		if (!curve) return;
		EC_POINT * p = nullptr;
		BIGNUM * key = nullptr;
		GenerateECIESKeyPair (curve, key, p);
		BIGNUM * x = BN_new ();
		BIGNUM * y = BN_new ();
		uint8_t scalar[32];
		bool ok = key && p && x && y && bn2buf (key, scalar, sizeof (scalar))
			&& EC_POINT_get_affine_coordinates (curve, p, x, y, NULL) == 1 && bn2buf (x, pub, 32)
			&& bn2buf (y, pub + 32, 32) && RAND_bytes (priv + 32, 224) == 1 && RAND_bytes (pub + 64, 192) == 1;
		if (ok)
			memcpy (priv, scalar, sizeof (scalar));
		else
		{
			OPENSSL_cleanse (priv, 256);
			memset (pub, 0, 256);
			LogPrint (eLogError, "ECIES P256: key generation failed");
		}
		OPENSSL_cleanse (scalar, sizeof (scalar));
		if (key) BN_clear_free (key);
		if (p) EC_POINT_free (p);
		BN_free (x);
		BN_free (y);
		EC_GROUP_free (curve);
	}

	ECIESGOSTR3410Encryptor::ECIESGOSTR3410Encryptor (const uint8_t * pub)
	{
		auto & curve = GetGOSTR3410Curve (eGOSTR3410CryptoProA);
		if (!curve || !curve->GetGroup ())
		{
			LogPrint (eLogError, "ECIES GOST R 34.10 curve is unavailable");
			m_PublicKey = nullptr;
			return;
		}
		m_PublicKey = EC_POINT_new (curve->GetGroup ());
		if (!LoadECPublicKey (curve->GetGroup (), m_PublicKey, pub))
		{
			LogPrint (eLogError, "ECIES GOST R 34.10 invalid public key");
			if (m_PublicKey) EC_POINT_free (m_PublicKey);
			m_PublicKey = nullptr;
		}
	}

	ECIESGOSTR3410Encryptor::~ECIESGOSTR3410Encryptor ()
	{
		if (m_PublicKey) EC_POINT_free (m_PublicKey);
	}

	bool ECIESGOSTR3410Encryptor::Encrypt (const uint8_t * data, uint8_t * encrypted)
	{
		auto & curve = GetGOSTR3410Curve (eGOSTR3410CryptoProA);
		if (m_PublicKey && data && encrypted && curve && curve->GetGroup ())
			return ECIESEncrypt (curve->GetGroup (), m_PublicKey, data, encrypted);
		ClearECIESOutput (encrypted);
		return false;
	}

	ECIESGOSTR3410Decryptor::ECIESGOSTR3410Decryptor (const uint8_t * priv)
	{
		m_PrivateKey = priv ? BN_bin2bn (priv, 32, nullptr) : nullptr;
	}

	ECIESGOSTR3410Decryptor::~ECIESGOSTR3410Decryptor ()
	{
		if (m_PrivateKey) BN_clear_free (m_PrivateKey);
	}

	bool ECIESGOSTR3410Decryptor::Decrypt (const uint8_t * encrypted, uint8_t * data)
	{
		if (!m_PrivateKey) return false;
		auto & curve = GetGOSTR3410Curve (eGOSTR3410CryptoProA);
		return curve && curve->GetGroup () && ECIESDecrypt (curve->GetGroup (), m_PrivateKey, encrypted, data);
	}

	void CreateECIESGOSTR3410RandomKeys (uint8_t * priv, uint8_t * pub)
	{
		if (!priv || !pub) return;
		memset (priv, 0, 256);
		memset (pub, 0, 256);
		auto & curve = GetGOSTR3410Curve (eGOSTR3410CryptoProA);
		if (!curve || !curve->GetGroup ())
		{
			OPENSSL_cleanse (priv, 256);
			memset (pub, 0, 256);
			LogPrint (eLogError, "ECIES GOST R 34.10: curve is unavailable");
			return;
		}
		EC_POINT * p = nullptr;
		BIGNUM * key = nullptr;
		GenerateECIESKeyPair (curve->GetGroup (), key, p);
		BIGNUM * x = BN_new ();
		BIGNUM * y = BN_new ();
		bool ok = key && p && x && y && bn2buf (key, priv, 32)
			&& EC_POINT_get_affine_coordinates (curve->GetGroup (), p, x, y, NULL) == 1 && bn2buf (x, pub, 32)
			&& bn2buf (y, pub + 32, 32) && RAND_bytes (priv + 32, 224) == 1 && RAND_bytes (pub + 64, 192) == 1;
		if (!ok)
		{
			OPENSSL_cleanse (priv, 256);
			memset (pub, 0, 256);
			LogPrint (eLogError, "ECIES GOST R 34.10: key generation failed");
		}
		if (key) BN_clear_free (key);
		if (p) EC_POINT_free (p);
		BN_free (x);
		BN_free (y);
	}

	ECIESX25519AEADRatchetEncryptor::ECIESX25519AEADRatchetEncryptor (const uint8_t * pub)
	{
		if (pub)
			memcpy (m_PublicKey, pub, 32);
		else
			memset (m_PublicKey, 0, sizeof (m_PublicKey));
	}

	bool ECIESX25519AEADRatchetEncryptor::Encrypt (const uint8_t *, uint8_t * pub)
	{
		if (!pub) return false;
		memcpy (pub, m_PublicKey, 32);
		return true;
	}

	ECIESX25519AEADRatchetDecryptor::ECIESX25519AEADRatchetDecryptor (const uint8_t * priv, bool calculatePublic)
	{
		m_StaticKeys.SetPrivateKey (priv, calculatePublic);
	}

	bool ECIESX25519AEADRatchetDecryptor::Decrypt (const uint8_t * epub, uint8_t * sharedSecret)
	{
		return m_StaticKeys.Agree (epub, sharedSecret);
	}

	bool CreateECIESX25519AEADRatchetRandomKeys (uint8_t * priv, uint8_t * pub)
	{
		if (!priv || !pub)
		{
			if (priv) OPENSSL_cleanse (priv, 32);
			if (pub) memset (pub, 0, 32);
			LogPrint (eLogError, "Crypto: null X25519 key output buffer");
			return false;
		}
		// Fail closed: a provider that cannot generate a key must not leave a
		// partially initialised pair that a caller would publish.
		OPENSSL_cleanse (priv, 32);
		memset (pub, 0, 32);
		X25519Keys k;
		if (!k.GenerateKeys () || !k.HasPrivateKey ())
		{
			LogPrint (eLogError, "Crypto: X25519 key generation failed");
			return false;
		}
		k.GetPrivateKey (priv);
		memcpy (pub, k.GetPublicKey (), 32);
		uint8_t acc = 0;
		for (size_t i = 0; i < 32; ++i) acc |= priv[i] | pub[i];
		if (!acc)
		{
			LogPrint (eLogError, "Crypto: generated X25519 key pair is empty");
			OPENSSL_cleanse (priv, 32);
			memset (pub, 0, 32);
			return false;
		}
		return true;
	}

	LocalEncryptionKey::LocalEncryptionKey (i2p::data::CryptoKeyType t)
		: keyType (t)
	{
		pub.resize (GetCryptoPublicKeyLen (keyType));
		priv.resize (GetCryptoPrivateKeyLen (keyType));
	}

	bool LocalEncryptionKey::GenerateKeys ()
	{
		if (priv.empty () || pub.empty ())
		{
			LogPrint (eLogError, "Crypto: unsupported local encryption key type ", (int)keyType);
			return false;
		}
		if (!i2p::data::PrivateKeys::GenerateCryptoKeyPair (keyType, priv.data (), pub.data ()))
		{
			// Never leave a zeroed key available: it would be published as a
			// key section that no peer can use.
			std::fill (priv.begin (), priv.end (), 0);
			std::fill (pub.begin (), pub.end (), 0);
			return false;
		}
		return true;
	}

	/**
	 * @brief Build the decryptor for this local key pair on first use.
	 *
	 * Lazy creation keeps the long-term private key out of a second object until
	 * a peer actually sends a message. An empty or all-zero private field is the
	 * fail-closed result of GenerateKeys() and never produces a decryptor: such a
	 * key was never published, so silently decrypting with it would only hide a
	 * generation failure. Not synchronized; the owning destination must call it
	 * before the key is used from another thread.
	 */
	void LocalEncryptionKey::CreateDecryptor ()
	{
		if (priv.empty ())
		{
			LogPrint (eLogError, "Crypto: no private key for encryption type ", (int)keyType);
			decryptor.reset ();
			return;
		}
		// A zeroed private key is the fail-closed result of a failed
		// generation, not a usable key: never build a decryptor for it.
		uint8_t acc = 0;
		for (auto byte : priv) acc |= byte;
		if (!acc)
		{
			LogPrint (eLogError, "Crypto: empty private key for encryption type ", (int)keyType);
			decryptor.reset ();
			return;
		}
		decryptor = i2p::data::PrivateKeys::CreateDecryptor (keyType, priv.data ());
	}
}
}
