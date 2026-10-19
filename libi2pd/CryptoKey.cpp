/*
* Copyright (c) 2013-2025, The PurpleI2P Project
* Copyright (c) 2026, David Uhden Collado
* SPDX-License-Identifier: BSD-3-Clause
*
* This file is part of openi2pd and licensed under BSD-3-Clause
*
* See full license text in LICENSE file at top of project tree
*/

#include <string.h>
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

	void ElGamalEncryptor::Encrypt (const uint8_t * data, uint8_t * encrypted)
	{
		ElGamalEncrypt (m_PublicKey, data, encrypted);
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

	void ECIESP256Encryptor::Encrypt (const uint8_t * data, uint8_t * encrypted)
	{
		if (m_Curve && m_PublicKey && data && encrypted)
			ECIESEncrypt (m_Curve, m_PublicKey, data, encrypted);
		else
			ClearECIESOutput (encrypted);
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
			&& EC_POINT_get_affine_coordinates (curve, p, x, y, NULL) == 1
			&& bn2buf (x, pub, 32) && bn2buf (y, pub + 32, 32) && RAND_bytes (priv + 32, 224) == 1
			&& RAND_bytes (pub + 64, 192) == 1;
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

	void ECIESGOSTR3410Encryptor::Encrypt (const uint8_t * data, uint8_t * encrypted)
	{
		auto & curve = GetGOSTR3410Curve (eGOSTR3410CryptoProA);
		if (m_PublicKey && data && encrypted && curve && curve->GetGroup ())
			ECIESEncrypt (curve->GetGroup (), m_PublicKey, data, encrypted);
		else
			ClearECIESOutput (encrypted);
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
			&& EC_POINT_get_affine_coordinates (curve->GetGroup (), p, x, y, NULL) == 1
			&& bn2buf (x, pub, 32) && bn2buf (y, pub + 32, 32) && RAND_bytes (priv + 32, 224) == 1
			&& RAND_bytes (pub + 64, 192) == 1;
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

	void ECIESX25519AEADRatchetEncryptor::Encrypt (const uint8_t *, uint8_t * pub)
	{
		if (pub) memcpy (pub, m_PublicKey, 32);
	}

	ECIESX25519AEADRatchetDecryptor::ECIESX25519AEADRatchetDecryptor (const uint8_t * priv, bool calculatePublic)
	{
		m_StaticKeys.SetPrivateKey (priv, calculatePublic);
	}

	bool ECIESX25519AEADRatchetDecryptor::Decrypt (const uint8_t * epub, uint8_t * sharedSecret)
	{
		return m_StaticKeys.Agree (epub, sharedSecret);
	}

	void CreateECIESX25519AEADRatchetRandomKeys (uint8_t * priv, uint8_t * pub)
	{
		X25519Keys k;
		k.GenerateKeys ();
		k.GetPrivateKey (priv);
		memcpy (pub, k.GetPublicKey (), 32);
	}

	LocalEncryptionKey::LocalEncryptionKey (i2p::data::CryptoKeyType t)
		: keyType (t)
	{
		pub.resize (GetCryptoPublicKeyLen (keyType));
		priv.resize (GetCryptoPrivateKeyLen (keyType));
	}

	void LocalEncryptionKey::GenerateKeys ()
	{
		if (priv.empty () || pub.empty ())
		{
			LogPrint (eLogError, "Crypto: unsupported local encryption key type ", (int)keyType);
			return;
		}
		i2p::data::PrivateKeys::GenerateCryptoKeyPair (keyType, priv.data (), pub.data ());
	}

	void LocalEncryptionKey::CreateDecryptor ()
	{
		if (priv.empty ())
		{
			LogPrint (eLogError, "Crypto: no private key for encryption type ", (int)keyType);
			decryptor.reset ();
			return;
		}
		decryptor = i2p::data::PrivateKeys::CreateDecryptor (keyType, priv.data ());
	}
}
}
