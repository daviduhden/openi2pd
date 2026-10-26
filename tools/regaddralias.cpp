/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: regaddralias.
 *
 * Builds an alias registration record linking a new destination to an old one,
 * signing with both private-key files. The output carries `action=adddest`,
 * both destinations and both signatures.
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

namespace
{
bool LoadKeys (const char * path, i2p::data::PrivateKeys & keys)
{
	return static_cast<bool> (i2p::tools::LoadPrivateKeys (path, keys));
}
}

int main (int argc, char * argv[])
{
	if (argc != 4)
	{
		std::cerr << "Usage: regaddralias <oldfilename> <newfilename> <address>" << std::endl;
		return 1;
	}

	if (!IsValidRecordName (argv[3]))
	{
		std::cerr << "address must not be empty or contain whitespace, '#' or '='" << std::endl;
		return 2;
	}

	i2p::tools::CryptoGuard cryptoGuard;
	i2p::data::PrivateKeys oldKeys, newKeys;
	if (!LoadKeys (argv[1], oldKeys) || !LoadKeys (argv[2], newKeys))
	{
		std::cerr << "Failed to load keyfile" << std::endl;
		return 2;
	}
	if (!oldKeys.GetPublic () || !newKeys.GetPublic ())
	{
		std::cerr << "key file has no public identity" << std::endl;
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

	// Two signatures over one accumulated record, in this order:
	// `#oldsig=` covers `name=newdest#!action=adddest#olddest=&lt;old&gt;`, and the
	// final `#sig=` covers that plus the old signature. The new destination key
	// signs last because it is the key the record claims; the old key proves
	// the operator still controls the previous address. Both key files are read
	// before any output is produced, so a missing or empty key file cannot leave
	// a partially signed record on stdout.
	appendSignature (oldKeys, "#oldsig=");
	appendSignature (newKeys, "#sig=");
	std::cout << out.str () << std::endl;

	return 0;
}
