/*
 * Copyright (c) 2013-2026, The PurpleI2P Project
 * Copyright (c) 2026, David Uhden Collado
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "Base.h"
#include "Crypto.h"
#include "Identity.h"
#include "common/file.hpp"

namespace
{
	void Help ()
	{
		std::cout << "Usage:" << std::endl
			<< "\tregaddr_3ld step1 privkey address" << std::endl
			<< "\tregaddr_3ld step2 step1file oldprivkey oldaddress" << std::endl
			<< "\tregaddr_3ld step3 step2file privkey" << std::endl;
	}

	bool LoadKeys (const char * path, i2p::data::PrivateKeys& keys)
	{
		auto input = i2p::tools::ReadFile (path);
		if (!input) return false;
		const bool ok = keys.FromBuffer (input->data (), input->size ());
		i2p::tools::Wipe (*input);
		return ok;
	}

	i2p::expected<std::string, std::string> ReadText (const char * path)
	{
		auto input = i2p::tools::ReadFile (path);
		if (!input) return i2p::unexpected (input.error ());
		return std::string (reinterpret_cast<const char *> (input->data ()), input->size ());
	}

	bool AppendSignature (std::ostringstream& out, i2p::data::PrivateKeys& keys, const char * marker)
	{
		const std::string signedPart = out.str ();
		std::vector<uint8_t> signature (keys.GetPublic ()->GetSignatureLen ());
		keys.Sign (reinterpret_cast<const uint8_t *> (signedPart.data ()),
			static_cast<int> (signedPart.size ()), signature.data ());
		out << marker << i2p::data::ByteStreamToBase64 (signature.data (), signature.size ());
		return true;
	}
}

int main (int argc, char * argv[])
{
	if (argc < 3)
	{
		Help ();
		return 1;
	}

	i2p::crypto::InitCrypto (false);
	i2p::data::PrivateKeys keys;
	const std::string action (argv[1]);
	int result = 0;

	if (action == "step1")
	{
		if (argc < 4)
		{
			Help ();
			result = 1;
		}
		else if (!LoadKeys (argv[2], keys))
		{
			std::cout << "Failed to load keyfile " << argv[2] << std::endl;
			result = 2;
		}
		else
		{
			std::cout << argv[3] << "=" << keys.GetPublic ()->ToBase64 ()
				<< "#!action=addsubdomain" << std::endl;
		}
	}
	else if (action == "step2")
	{
		if (argc < 5)
		{
			Help ();
			result = 1;
		}
		else
		{
			auto regtxt = ReadText (argv[2]);
			if (!regtxt || !LoadKeys (argv[3], keys))
			{
				std::cout << "Failed to read step1 output or keyfile" << std::endl;
				result = 2;
			}
			else
			{
				std::ostringstream out;
				out << *regtxt << "#date=" << std::time (nullptr)
					<< "#olddest=" << keys.GetPublic ()->ToBase64 ()
					<< "#oldname=" << argv[4];
				AppendSignature (out, keys, "#oldsig=");
				std::cout << out.str () << std::endl;
			}
		}
	}
	else if (action == "step3")
	{
		if (argc < 4)
		{
			Help ();
			result = 1;
		}
		else
		{
			auto regtxt = ReadText (argv[2]);
			if (!regtxt || !LoadKeys (argv[3], keys))
			{
				std::cout << "Failed to read step2 output or keyfile" << std::endl;
				result = 2;
			}
			else
			{
				std::ostringstream out;
				out << *regtxt;
				AppendSignature (out, keys, "#sig=");
				std::cout << out.str () << std::endl;
			}
		}
	}
	else
	{
		Help ();
		result = 1;
	}

	i2p::crypto::TerminateCrypto ();
	return result;
}
