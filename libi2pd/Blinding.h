/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Blinding.
 *
 * Computes blinded public keys, subcredentials and store hashes for
 * encrypted LeaseSet2 destinations, including per-client authorization.
 * Keys can derive from an identity or a b33 address, and validity
 * requires a blindable signing type.
 *
 */

#ifndef BLINDING_H__
#define BLINDING_H__

#include <inttypes.h>
#include <string>
#include <string_view>
#include <array>
#include <memory>
#include <vector>
#include "Crypto.h"
#include "Identity.h"

namespace i2p
{
namespace data
{
	class BlindedPublicKey // for encrypted LS2
	{
	public:
		BlindedPublicKey (std::shared_ptr<const IdentityEx> identity, bool clientAuth = false);
		BlindedPublicKey (std::string_view b33); // from b33 without .b32.i2p
		std::string ToB33 () const;

		const uint8_t * GetPublicKey () const { return m_PublicKey.data (); };
		size_t GetPublicKeyLen () const { return m_PublicKey.size (); };
		SigningKeyType GetSigType () const { return m_SigType; };
		SigningKeyType GetBlindedSigType () const { return m_BlindedSigType; };
		bool IsValid () const; // signature type must be blindable

		void GetSubcredential (const uint8_t * blinded, size_t len, uint8_t * subcredential) const; // 32 bytes
		size_t GetBlindedKey (
			const char * date, uint8_t * blindedKey) const; // date is 8 chars "YYYYMMDD", return public key length
		size_t BlindPrivateKey (const uint8_t * priv, const char * date, uint8_t * blindedPriv,
			uint8_t * blindedPub) const; // date is 8 chars "YYYYMMDD", return public key length
		i2p::data::IdentHash GetStoreHash (
			const char * date = nullptr) const; // date is 8 chars "YYYYMMDD", use current if null

	private:
		void GetCredential (uint8_t * credential) const; // 32 bytes
		void GenerateAlpha (const char * date, uint8_t * seed) const; // 64 bytes, date is 8 chars "YYYYMMDD"
		void H (
			const std::string & p, const std::vector<std::pair<const uint8_t *, size_t>> & bufs, uint8_t * hash) const;

	private:
		std::array<uint8_t, i2p::crypto::EDDSA25519_PUBLIC_KEY_LENGTH> m_PublicKey { };
		i2p::data::SigningKeyType m_SigType = i2p::data::SIGNING_KEY_TYPE_DSA_SHA1;
		i2p::data::SigningKeyType m_BlindedSigType = i2p::data::SIGNING_KEY_TYPE_DSA_SHA1;
		bool m_IsClientAuth = false;
		bool m_IsValid = false;
	};
}
}

#endif
