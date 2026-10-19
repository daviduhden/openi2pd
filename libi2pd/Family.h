/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Family.
 *
 * Handles I2P family certificates: loads trusted family signing keys and
 * verifies that a destination identity and signature belong to a named family.
 * Also creates the family signature attached to published destinations.
 *
 */

#ifndef FAMILY_H__
#define FAMILY_H__

#include <map>
#include <string>
#include <string_view>
#include <memory>
#include <openssl/evp.h>
#include "Identity.h"

namespace i2p
{
namespace data
{
	typedef int FamilyID;
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
		std::map<std::string, std::pair<EVP_PKEY *, FamilyID>> m_SigningKeys; // family -> (verification pkey, id)
	};

	std::string CreateFamilySignature (const std::string & family, const IdentHash & ident);
	// return base64 signature of empty string in case of failure
}
}

#endif
