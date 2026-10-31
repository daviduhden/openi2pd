/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Family.
 *
 * Handles I2P family certificates: loads trusted family signing keys
 * and verifies that a destination identity and signature belong to a
 * named family. Also creates the family signature attached to published
 * destinations.
 *
 */

#ifndef FAMILY_H__
#define FAMILY_H__

#include <map>
#include <string>
#include <string_view>
#include <memory>
#include <shared_mutex>
#include <openssl/evp.h>
#include "Identity.h"

namespace i2p
{
namespace data
{
	typedef int FamilyID;

	struct FamilySigningKey
	{
		std::unique_ptr<EVP_PKEY, decltype (&EVP_PKEY_free)> pkey { nullptr, EVP_PKEY_free };
		FamilyID id = 0;
	};

	class Families
	{
	public:
		Families ();
		~Families ();
		void LoadCertificates ();
		bool VerifyFamily (const std::string & family, const IdentHash & ident, std::string_view signature,
			const char * key = nullptr) const;
		FamilyID GetFamilyID (const std::string & family) const;

	private:
		void LoadCertificate (const std::string & filename);

	private:
		mutable std::shared_mutex m_SigningKeysMutex;
		std::map<std::string, FamilySigningKey> m_SigningKeys; // family -> verification key and id
	};

	std::string CreateFamilySignature (const std::string & family, const IdentHash & ident);
	// return base64 signature of empty string in case of failure
}
}

#endif
