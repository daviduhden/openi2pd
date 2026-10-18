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

int main (int argc, char * argv[])
{
	if (argc < 3)
	{
		std::cout << "Usage: regaddr filename address" << std::endl;
		return 1;
	}

	i2p::crypto::InitCrypto (false);
	i2p::data::PrivateKeys keys;

	auto input = i2p::tools::ReadFile (argv[1]);
	if (!input || !keys.FromBuffer (input->data (), input->size ()))
	{
		if (input) i2p::tools::Wipe (*input);
		std::cout << "Failed to load keyfile " << argv[1] << std::endl;
		i2p::crypto::TerminateCrypto ();
		return 2;
	}
	i2p::tools::Wipe (*input);

	std::ostringstream out;
	out << argv[2] << "=" << keys.GetPublic ()->ToBase64 ();
	const std::string signedPart = out.str ();
	std::vector<uint8_t> signature (keys.GetPublic ()->GetSignatureLen ());
	keys.Sign (reinterpret_cast<const uint8_t *> (signedPart.data ()), signedPart.size (), signature.data ());
	out << "#!sig=" << i2p::data::ByteStreamToBase64 (signature.data (), signature.size ());
	std::cout << out.str () << std::endl;

	i2p::crypto::TerminateCrypto ();
	return 0;
}
