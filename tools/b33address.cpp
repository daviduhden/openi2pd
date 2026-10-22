/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: b33address.
 *
 * Reads a base64 destination from stdin and prints its b33 address and the
 * current daily store hash. Only ED25519-SHA512 and RED25519-SHA512
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
	// base64 input, b33 and store key output, 11->11 only. Bound the line
	// before allocation: a terminal or pipe must not be able to make this
	// diagnostic tool buffer arbitrary input.
	constexpr size_t MAX_BASE64_INPUT = 4096;
	std::cout << "Waiting for base64 from stdin..." << std::endl;
	std::array<char, MAX_BASE64_INPUT + 2> input { };
	std::cin.getline (input.data (), input.size ());
	if (std::cin.fail () && !std::cin.eof ())
	{
		std::cerr << "Invalid base64 address (input too long)" << std::endl;
		return 1;
	}
	std::string_view base64 (input.data ());
	while (!base64.empty () && (base64.back () == '\r' || base64.back () == '\n')) base64.remove_suffix (1);
	if (base64.empty () || base64.size () > MAX_BASE64_INPUT)
	{
		std::cerr << "Invalid base64 address" << std::endl;
		return 1;
	}
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
