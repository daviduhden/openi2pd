/*
 * Copyright (c) 2013-2026, The PurpleI2P Project
 * Copyright (c) 2026, David Uhden Collado
 * SPDX-License-Identifier: BSD-3-Clause
 */
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
bool LoadKeys (const char * path, i2p::data::PrivateKeys & keys)
{
	auto input = i2p::tools::ReadFile (path);
	if (!input) return false;
	const bool ok = keys.FromBuffer (input->data (), input->size ());
	i2p::tools::Wipe (*input);
	return ok;
}
}

int main (int argc, char * argv[])
{
	if (argc < 4)
	{
		std::cout << "Usage: regaddralias oldfilename newfilename address" << std::endl;
		return 1;
	}

	i2p::crypto::InitCrypto (false);
	i2p::data::PrivateKeys oldKeys, newKeys;
	if (!LoadKeys (argv[1], oldKeys) || !LoadKeys (argv[2], newKeys))
	{
		std::cout << "Failed to load keyfile" << std::endl;
		i2p::crypto::TerminateCrypto ();
		return 2;
	}

	std::ostringstream out;
	out << argv[3] << "=" << newKeys.GetPublic ()->ToBase64 ()
		<< "#!action=adddest#olddest=" << oldKeys.GetPublic ()->ToBase64 ();

	const auto appendSignature = [&out] (i2p::data::PrivateKeys & keys, const char * marker)
	{
		const std::string signedPart = out.str ();
		std::vector<uint8_t> signature (keys.GetPublic ()->GetSignatureLen ());
		keys.Sign (reinterpret_cast<const uint8_t *> (signedPart.data ()), static_cast<int> (signedPart.size ()),
			signature.data ());
		out << marker << i2p::data::ByteStreamToBase64 (signature.data (), signature.size ());
	};

	appendSignature (oldKeys, "#oldsig=");
	appendSignature (newKeys, "#sig=");
	std::cout << out.str () << std::endl;

	i2p::crypto::TerminateCrypto ();
	return 0;
}
