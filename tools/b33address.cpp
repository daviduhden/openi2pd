/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: b33address.
 *
 * Reads a base64 destination from stdin and prints its b33 address and
 * the current daily store hash. Only ED25519-SHA512 and RED25519-SHA512
 * destinations can be blinded; the input length is bounded.
 *
 */

#include <array>
#include <iostream>
#include <string>
#include <string_view>
#include <memory>
#include "Identity.h"
#include "LeaseSet.h"
#include "common/key.hpp"
#include "common/crypto.hpp"

int main (int argc, char * argv[])
{
	if (argc > 1)
	{
		const std::string_view option (argv[1]);
		if (option == "-h" || option == "--help")
		{
			std::cout << "Usage: b33address < base64-destination" << std::endl;
			return 0;
		}
		std::cerr << "Usage: b33address < base64-destination" << std::endl;
		return 1;
	}
	// Contract: the prompt goes to stderr so stdout carries only the two
	// data lines a pipeline can parse, and only the first line of stdin is
	// read (a pasted multi-address input uses the first destination, not
	// an error). Exit status is 0 for `--help` or a derived address, 1 for
	// a usage error, oversized input, non-printable byte, undecodable
	// base64 or a non-blindable key.
	//
	// base64 input, b33 and store key output, 11->11 only. Read one byte
	// at a time so the cap is enforced before allocation and an embedded
	// NUL can never silently truncate the value the way
	// getline()+string_view did.
	constexpr size_t MAX_BASE64_INPUT = 4096;
	std::cerr << "Waiting for base64 from stdin..." << std::endl;
	std::string base64;
	base64.reserve (MAX_BASE64_INPUT);
	bool tooLong = false;
	char ch = 0;
	while (std::cin.get (ch))
	{
		if (ch == '\n') break;
		if (base64.size () >= MAX_BASE64_INPUT)
		{
			tooLong = true;
			break;
		}
		base64.push_back (ch);
	}
	if (tooLong)
	{
		std::cerr << "Invalid base64 address (input too long)" << std::endl;
		return 1;
	}
	while (!base64.empty () && (base64.back () == '\r' || base64.back () == '\n')) base64.pop_back ();
	if (base64.empty ())
	{
		std::cerr << "Invalid base64 address" << std::endl;
		return 1;
	}
	for (unsigned char byte : base64)
	{
		if (byte < 0x20 || byte == 0x7f)
		{
			std::cerr << "Invalid base64 address" << std::endl;
			return 1;
		}
	}

	i2p::tools::CryptoGuard cryptoGuard;
	auto ident = std::make_shared<i2p::data::IdentityEx> ();
	if (ident->FromBase64 (base64))
	{
		if (ident->GetSigningKeyType () == i2p::data::SIGNING_KEY_TYPE_REDDSA_SHA512_ED25519
			|| ident->GetSigningKeyType () == i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519)
		{
			i2p::data::BlindedPublicKey blindedKey (ident);
			std::cout << "b33 address: " << blindedKey.ToB33 () << ".b32.i2p" << std::endl;
			std::cout << "Today's store hash: " << blindedKey.GetStoreHash ().ToBase64 () << std::endl;
		}
		else
		{
			std::cerr << "Invalid signature type " << SigTypeToName (ident->GetSigningKeyType ()) << std::endl;
			return 1;
		}
	}
	else
	{
		std::cerr << "Invalid base64 address" << std::endl;
		return 1;
	}

	return 0;
}
