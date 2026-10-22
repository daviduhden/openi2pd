/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: keygen.
 *
 * Creates a random I2P destination key file, defaulting to ED25519-SHA512 and
 * accepting a signature type by name or number. It prints the destination hash
 * and wipes the serialized private key after writing.
 *
 */

#include <fstream>
#include <iostream>
#include <vector>

#include "Crypto.h"
#include "Identity.h"
#include "common/file.hpp"
#include "common/key.hpp"
#include "common/crypto.hpp"

int main (int argc, char * argv[])
{
	if (argc < 2 || argc > 3 || std::string_view (argv[1]) == "-h" || std::string_view (argv[1]) == "--help")
	{
		std::cerr << "Usage: keygen filename <signature type>" << std::endl;
		return 1;
	}
	for (int i = 2; i < argc; ++i)
		if (std::string_view (argv[i]) == "-h" || std::string_view (argv[i]) == "--help")
		{
			std::cerr << "Usage: keygen filename <signature type>" << std::endl;
			return 1;
		}

	i2p::tools::CryptoGuard cryptoGuard;
	i2p::data::SigningKeyType type = i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519;
	if (argc > 2)
	{
		auto requested = NameToSigType (argv[2]);
		if (!requested)
		{
			std::cerr << "Incorrect signature type" << std::endl;
			return 2;
		}
		type = static_cast<i2p::data::SigningKeyType> (*requested);
	}

	auto keys = i2p::data::PrivateKeys::CreateRandomKeys (type);
	// CreateRandomKeys historically falls back to a DSA identity when a
	// requested generator is unavailable. Never write a mislabelled key.
	if (!keys.GetPublic () || keys.GetPublic ()->GetSigningKeyType () != type)
	{
		std::cerr << "unsupported signature type " << SigTypeToName (type) << " (" << type << ")" << std::endl;
		return 2;
	}
	std::vector<uint8_t> buffer (keys.GetFullLen ());
	const size_t len = keys.ToBuffer (buffer.data (), buffer.size ());
	const bool written = len && i2p::tools::WriteFile (argv[1], buffer.data (), len);
	i2p::tools::Wipe (buffer);
	if (!written)
	{
		std::cout << "Can't create file " << argv[1] << std::endl;
		return 3;
	}

	std::cout << "Destination " << keys.GetPublic ()->GetIdentHash ().ToBase32 () << " created" << std::endl;
	std::cout << "Signature type: " << SigTypeToName (type) << " (" << type << ")" << std::endl;
	return 0;
}
