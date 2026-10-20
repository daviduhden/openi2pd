/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Blinding.
 *
 * Implements Proposal 149 destination blinding for EdDSA and RedDSA keys:
 * b33 address parsing, credential/subcredential derivation, daily key
 * blinding and store-hash calculation. Only canonical base32, supported
 * signature types and defined flag bits are accepted; malformed, non-blindable
 * or secret-requiring addresses produce an invalid key instead of a usable
 * address.
 *
 */

#include <cstdint>
#include <cctype>
#include <string>
#include <zlib.h> // for crc32
#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/bn.h>
#include "Base.h"
#include "Crypto.h"
#include "Log.h"
#include "Timestamp.h"
#include "I2PEndian.h"
#include "Ed25519.h"
#include "Signature.h"
#include "Blinding.h"

namespace i2p
{
namespace data
{
	const uint8_t B33_TWO_BYTES_SIGTYPE_FLAG = 0x01;
	const uint8_t B33_PER_SECRET_FLAG = 0x02;
	const uint8_t B33_PER_CLIENT_AUTH_FLAG = 0x04;

	BlindedPublicKey::BlindedPublicKey (std::shared_ptr<const IdentityEx> identity, bool clientAuth)
		: m_IsClientAuth (clientAuth)
	{
		if (!identity) return;
		m_SigType = identity->GetSigningKeyType ();
		m_BlindedSigType = i2p::data::SIGNING_KEY_TYPE_REDDSA_SHA512_ED25519; // always 11
		if (m_SigType == i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519
			|| m_SigType == i2p::data::SIGNING_KEY_TYPE_REDDSA_SHA512_ED25519)
		{
			const uint8_t * signingKey = identity->GetSigningPublicKeyBuffer ();
			if (signingKey && identity->GetSigningPublicKeyLen () == m_PublicKey.size ())
			{
				memcpy (m_PublicKey.data (), signingKey, m_PublicKey.size ());
				m_IsValid = true;
			}
			else
				LogPrint (eLogError, "Blinding: Signing key of type ", (int)m_SigType, " has the wrong length");
		}
		else
			LogPrint (eLogError, "Blinding: Unsupported signature type ", (int)m_SigType);
	}

	BlindedPublicKey::BlindedPublicKey (std::string_view b33)
	{
		// Base32 addresses are case-insensitive; Java lowercases before
		// decoding. Normalise first so the canonical re-encoding check below
		// compares like with like.
		std::string address (b33);
		for (char & ch : address) ch = (char)std::tolower ((unsigned char)ch);
		// Decode with room for the largest b33 we accept and reject inputs that
		// decode to more than 40 bytes. The old fixed 40-byte destination
		// silently truncated overlong addresses.
		std::array<uint8_t, 64> addr { };
		size_t l = i2p::data::Base32ToByteStream (address, addr.data (), addr.size ());
		if (l < 32 || l > 40)
		{
			LogPrint (eLogError, "Blinding: Malformed b33 ", b33);
			return;
		}
		// Base32 decoders may silently drop unused trailing bits. Require the
		// canonical encoding so a non-standard string cannot alias a different
		// b33 address.
		if (i2p::data::ByteStreamToBase32 (addr.data (), l) != address)
		{
			LogPrint (eLogError, "Blinding: Non-canonical b33 ", b33);
			return;
		}
		uint32_t checksum = crc32 (0, addr.data () + 3, static_cast<uInt> (l - 3));
		// checksum is Little Endian
		addr[0] ^= checksum;
		addr[1] ^= (checksum >> 8);
		addr[2] ^= (checksum >> 16);
		uint8_t flags = addr[0];
		if (flags & 0xF8)
		{
			// Proposal 149 defines only bits 0, 1 and 2. Java rejects any
			// other bit as a corrupt or unsupported address, and two
			// different flag sets must not map to the same store key.
			LogPrint (eLogError, "Blinding: Unsupported b33 flags ", (int)flags);
			m_SigType = i2p::data::SIGNING_KEY_TYPE_DSA_SHA1;
			m_PublicKey.fill (0);
			m_IsValid = false;
			return;
		}
		if (flags & B33_PER_SECRET_FLAG)
		{
			// This class has no way to carry the blinding secret. Treating a
			// secret-required address as if it had no secret would derive a
			// store key that no peer can ever serve.
			LogPrint (eLogError, "Blinding: b33 requires a blinding secret, which is not supported");
			m_SigType = i2p::data::SIGNING_KEY_TYPE_DSA_SHA1;
			m_PublicKey.fill (0);
			m_IsValid = false;
			return;
		}
		size_t offset = 1;
		if (flags & B33_TWO_BYTES_SIGTYPE_FLAG) // two bytes signatures
		{
			m_SigType = bufbe16toh (addr.data () + offset);
			offset += 2;
			m_BlindedSigType = bufbe16toh (addr.data () + offset);
			offset += 2;
		}
		else // one byte sig
		{
			m_SigType = addr[offset];
			offset++;
			m_BlindedSigType = addr[offset];
			offset++;
		}
		m_IsClientAuth = flags & B33_PER_CLIENT_AUTH_FLAG;

		// The b33 encoding of a 32-byte signing key is exactly the header plus
		// the key. Reject trailing bytes so two different strings cannot
		// denote the same blinded destination.
		std::unique_ptr<i2p::crypto::Verifier> blindedVerifier (i2p::data::IdentityEx::CreateVerifier (m_SigType));
		// Proposal 149 only defines EdDSA/RedDSA inputs and a RedDSA blinded
		// key. Accepting another signature or blinded type would hash the
		// wrong store key while still reporting a usable address.
		const bool blindable = m_SigType == i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519
			|| m_SigType == i2p::data::SIGNING_KEY_TYPE_REDDSA_SHA512_ED25519;
		if (blindedVerifier && blindable && m_BlindedSigType == i2p::data::SIGNING_KEY_TYPE_REDDSA_SHA512_ED25519
			&& offset + m_PublicKey.size () == l)
		{
			memcpy (m_PublicKey.data (), addr.data () + offset, m_PublicKey.size ());
			m_IsValid = true;
		}
		else
		{
			if (!blindedVerifier)
				LogPrint (eLogError, "Blinding: Unknown signature type ", (int)m_SigType, " in b33");
			else if (!blindable || m_BlindedSigType != i2p::data::SIGNING_KEY_TYPE_REDDSA_SHA512_ED25519)
				LogPrint (eLogError, "Blinding: Unsupported b33 signature types ", (int)m_SigType, " -> ",
					(int)m_BlindedSigType);
			else
				LogPrint (eLogError, "Blinding: Unexpected b33 length for signature type ", (int)m_SigType);
			m_SigType = i2p::data::SIGNING_KEY_TYPE_DSA_SHA1;
			m_PublicKey.fill (0);
			m_IsValid = false;
		}
	}

	bool BlindedPublicKey::IsValid () const
	{
		if (!m_IsValid || m_BlindedSigType != i2p::data::SIGNING_KEY_TYPE_REDDSA_SHA512_ED25519) return false;
		switch (m_SigType)
		{
		case i2p::data::SIGNING_KEY_TYPE_REDDSA_SHA512_ED25519:
		case i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519: return true;
		default: return false;
		}
	}

	std::string BlindedPublicKey::ToB33 () const
	{
		if (!m_IsValid || m_PublicKey.size () > 32) return ""; // assume 25519
		uint8_t addr[35];
		uint8_t flags = 0;
		if (m_IsClientAuth) flags |= B33_PER_CLIENT_AUTH_FLAG;
		addr[0] = flags; // flags
		addr[1] = static_cast<uint8_t> (m_SigType); // sig type
		addr[2] = static_cast<uint8_t> (m_BlindedSigType); // blinded sig type
		memcpy (addr + 3, m_PublicKey.data (), m_PublicKey.size ());
		uint32_t checksum = crc32 (0, addr + 3, static_cast<uInt> (m_PublicKey.size ()));
		// checksum is Little Endian
		addr[0] ^= checksum;
		addr[1] ^= (checksum >> 8);
		addr[2] ^= (checksum >> 16);
		return ByteStreamToBase32 (addr, m_PublicKey.size () + 3);
	}

	void BlindedPublicKey::GetCredential (uint8_t * credential) const
	{
		// A = destination's signing public key
		// stA = signature type of A, 2 bytes big endian
		uint16_t stA = htobe16 (GetSigType ());
		// stA1 = signature type of blinded A, 2 bytes big endian
		uint16_t stA1 = htobe16 (GetBlindedSigType ());
		// credential = H("credential", A || stA || stA1)
		H ("credential",
			{ { GetPublicKey (), GetPublicKeyLen () }, { (const uint8_t *)&stA, 2 }, { (const uint8_t *)&stA1, 2 } },
			credential);
	}

	void BlindedPublicKey::GetSubcredential (const uint8_t * blinded, size_t len, uint8_t * subcredential) const
	{
		uint8_t credential[32];
		GetCredential (credential);
		// subcredential = H("subcredential", credential || blindedPublicKey)
		H ("subcredential", { { credential, 32 }, { blinded, len } }, subcredential);
	}

	void BlindedPublicKey::GenerateAlpha (const char * date, uint8_t * seed) const
	{
		if (!date)
		{
			memset (seed, 0, 64);
			return;
		}
		uint16_t stA = htobe16 (GetSigType ()), stA1 = htobe16 (GetBlindedSigType ());
		uint8_t salt[32];
		//seed = HKDF(H("I2PGenerateAlpha", keydata), datestring || secret, "i2pblinding1", 64)
		H ("I2PGenerateAlpha",
			{ { GetPublicKey (), GetPublicKeyLen () }, { (const uint8_t *)&stA, 2 }, { (const uint8_t *)&stA1, 2 } },
			salt);
		i2p::crypto::HKDF (salt, (const uint8_t *)date, 8, "i2pblinding1", seed);
	}

	size_t BlindedPublicKey::GetBlindedKey (const char * date, uint8_t * blindedKey) const
	{
		if (!date || !blindedKey) return 0;
		uint8_t seed[64] { };
		GenerateAlpha (date, seed);

		size_t publicKeyLength = 0;
		switch (m_SigType)
		{
		case i2p::data::SIGNING_KEY_TYPE_REDDSA_SHA512_ED25519:
		case i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519:
			i2p::crypto::GetEd25519 ()->BlindPublicKey (GetPublicKey (), seed, blindedKey);
			publicKeyLength = i2p::crypto::EDDSA25519_PUBLIC_KEY_LENGTH;
			break;
		default: LogPrint (eLogError, "Blinding: Can't blind signature type ", (int)m_SigType);
		}
		return publicKeyLength;
	}

	size_t BlindedPublicKey::BlindPrivateKey (
		const uint8_t * priv, const char * date, uint8_t * blindedPriv, uint8_t * blindedPub) const
	{
		if (!priv || !date || !blindedPriv || !blindedPub) return 0;
		uint8_t seed[64] { };
		GenerateAlpha (date, seed);
		size_t publicKeyLength = 0;
		switch (m_SigType)
		{
		case i2p::data::SIGNING_KEY_TYPE_REDDSA_SHA512_ED25519:
			i2p::crypto::GetEd25519 ()->BlindPrivateKey (priv, seed, blindedPriv, blindedPub);
			publicKeyLength = i2p::crypto::EDDSA25519_PUBLIC_KEY_LENGTH;
			break;
		case i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519:
		{
			uint8_t exp[64];
			i2p::crypto::Ed25519::ExpandPrivateKey (priv, exp);
			i2p::crypto::GetEd25519 ()->BlindPrivateKey (exp, seed, blindedPriv, blindedPub);
			publicKeyLength = i2p::crypto::EDDSA25519_PUBLIC_KEY_LENGTH;
			break;
		}
		default: LogPrint (eLogError, "Blinding: Can't blind signature type ", (int)m_SigType);
		}
		return publicKeyLength;
	}

	void BlindedPublicKey::H (
		const std::string & p, const std::vector<std::pair<const uint8_t *, size_t>> & bufs, uint8_t * hash) const
	{
		EVP_MD_CTX * ctx = EVP_MD_CTX_new ();
		if (!ctx)
		{
			memset (hash, 0, 32);
			return;
		}

		bool ok = EVP_DigestInit_ex (ctx, EVP_sha256 (), NULL) == 1;
		if (ok) ok = EVP_DigestUpdate (ctx, p.c_str (), p.length ()) == 1;
		if (ok)
			for (const auto & it : bufs)
				if (EVP_DigestUpdate (ctx, it.first, it.second) != 1)
				{
					ok = false;
					break;
				}
		if (ok) ok = EVP_DigestFinal_ex (ctx, (uint8_t *)hash, nullptr) == 1;
		if (!ok) memset (hash, 0, 32);
		EVP_MD_CTX_free (ctx);
	}

	i2p::data::IdentHash BlindedPublicKey::GetStoreHash (const char * date) const
	{
		i2p::data::IdentHash hash { };
		uint8_t blinded[i2p::crypto::EDDSA25519_PUBLIC_KEY_LENGTH]; // 32 max
		size_t publicKeyLength = 0;
		if (date)
			publicKeyLength = GetBlindedKey (date, blinded);
		else
		{
			char currentDate[9];
			i2p::util::GetCurrentDate (currentDate);
			publicKeyLength = GetBlindedKey (currentDate, blinded);
		}
		if (publicKeyLength)
		{
			auto stA1 = htobe16 (m_BlindedSigType);
			EVP_MD_CTX * ctx = EVP_MD_CTX_new ();
			if (!ctx) return hash;
			bool ok = EVP_DigestInit_ex (ctx, EVP_sha256 (), NULL) == 1
				&& EVP_DigestUpdate (ctx, (const uint8_t *)&stA1, 2) == 1
				&& EVP_DigestUpdate (ctx, blinded, publicKeyLength) == 1
				&& EVP_DigestFinal_ex (ctx, (uint8_t *)hash, nullptr) == 1;
			if (!ok) hash.Fill (0);
			EVP_MD_CTX_free (ctx);
		}
		else
			LogPrint (eLogError, "Blinding: Blinded key type ", (int)m_BlindedSigType, " is not supported");
		return hash;
	}

}
}
