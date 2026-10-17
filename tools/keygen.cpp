#include <fstream>
#include <iostream>
#include <vector>

#include "Crypto.h"
#include "Identity.h"
#include "common/file.hpp"
#include "common/key.hpp"

int main (int argc, char * argv[])
{
	if (argc < 2)
	{
		std::cout << "Usage: keygen filename <signature type>" << std::endl;
		return 1;
	}

	i2p::crypto::InitCrypto (false);
	i2p::data::SigningKeyType type = i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519;
	if (argc > 2)
	{
		auto requested = NameToSigType (argv[2]);
		if (!requested)
		{
			std::cerr << "Incorrect signature type" << std::endl;
			i2p::crypto::TerminateCrypto ();
			return 2;
		}
		type = static_cast<i2p::data::SigningKeyType> (*requested);
	}

	auto keys = i2p::data::PrivateKeys::CreateRandomKeys (type);
	std::vector<uint8_t> buffer (keys.GetFullLen ());
	const size_t len = keys.ToBuffer (buffer.data (), buffer.size ());
	const bool written = len && i2p::tools::WriteFile (argv[1], buffer.data (), len);
	i2p::tools::Wipe (buffer);
	if (!written)
	{
		std::cout << "Can't create file " << argv[1] << std::endl;
		i2p::crypto::TerminateCrypto ();
		return 3;
	}

	std::cout << "Destination " << keys.GetPublic ()->GetIdentHash ().ToBase32 () << " created" << std::endl;
	std::cout << "Signature type: " << SigTypeToName (type) << " (" << type << ")" << std::endl;
	i2p::crypto::TerminateCrypto ();
	return 0;
}
