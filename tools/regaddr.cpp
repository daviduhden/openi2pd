/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: regaddr.
 *
 * Signs a `name=destination` host-registration record with a private-key file
 * and prints it to stdout. The signature covers the name and base64
 * destination for submission to an address registration service.
 *
 */

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "Base.h"
#include "Crypto.h"
#include "Identity.h"
#include "common/file.hpp"
#include "common/key.hpp"
#include "common/keys.hpp"
#include "common/crypto.hpp"

int main (int argc, char * argv[])
{
	if (argc != 3)
	{
		std::cerr << "Usage: regaddr <filename> <address>" << std::endl;
		return 1;
	}
	if (!IsValidRecordName (argv[2]))
	{
		std::cerr << "address must not be empty or contain whitespace, '#' or '='" << std::endl;
		return 2;
	}

	i2p::tools::CryptoGuard cryptoGuard;
	i2p::data::PrivateKeys keys;
	if (auto loaded = LoadPrivateKeys (argv[1], keys); !loaded)
	{
		std::cerr << "Failed to load keyfile " << argv[1] << ": " << loaded.error () << std::endl;
		return 2;
	}
	if (!keys.GetPublic ())
	{
		std::cerr << "key file has no public identity" << std::endl;
		return 2;
	}

	std::ostringstream out;
	out << argv[2] << "=" << keys.GetPublic ()->ToBase64 ();
	const std::string signedPart = out.str ();
	std::vector<uint8_t> signature (keys.GetPublic ()->GetSignatureLen ());
	keys.Sign (reinterpret_cast<const uint8_t *> (signedPart.data ()), signedPart.size (), signature.data ());
	out << "#!sig=" << i2p::data::ByteStreamToBase64 (signature.data (), signature.size ());
	std::cout << out.str () << std::endl;

	return 0;
}
