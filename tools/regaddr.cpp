/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: regaddr.
 *
 * Signs a `name=destination` host-registration record with a
 * private-key file and prints it to stdout. The signature covers the
 * name and base64 destination for submission to an address registration
 * service.
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
	if (auto loaded = i2p::tools::LoadPrivateKeys (argv[1], keys); !loaded)
	{
		std::cerr << "Failed to load keyfile " << argv[1] << ": " << loaded.error () << std::endl;
		return 2;
	}
	if (!keys.GetPublic ())
	{
		std::cerr << "key file has no public identity" << std::endl;
		return 2;
	}

	// Record grammar: `name=&lt;base64 destination&gt;#!sig=&lt;base64
	// signature&gt;`. The signed bytes are exactly the `name=destination`
	// text built so far, with no line terminator and no `#!sig=` field, so
	// the record must be captured verbatim and submitted as one line.
	// Appending the `#!sig=` marker afterwards is what makes the signature
	// verifiable by the registration service and by `verifyhost`.
	//
	// The signature length is taken from the loaded public identity rather
	// than assumed: a non-Ed25519 destination signs with its own length.
	// Only the record goes to stdout; diagnostics go to stderr, and
	// nothing is written to the filesystem. Exit status is 1 for a usage
	// error and 2 for a rejected name, an unreadable key file or a key
	// file without a public identity. The name is validated before the key
	// is read, so a malformed name cannot consume the private key at all.
	std::ostringstream out;
	out << argv[2] << "=" << keys.GetPublic ()->ToBase64 ();
	const std::string signedPart = out.str ();
	std::vector<uint8_t> signature (keys.GetPublic ()->GetSignatureLen ());
	keys.Sign (reinterpret_cast<const uint8_t *> (signedPart.data ()), signedPart.size (), signature.data ());
	out << "#!sig=" << i2p::data::ByteStreamToBase64 (signature.data (), signature.size ());
	std::cout << out.str () << std::endl;

	return 0;
}
