/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: regaddr 3ld.
 *
 * Implements the three-step third-level-domain registration workflow:
 * step1 creates the subdomain record, step2 counter-signs with the old
 * destination, and step3 signs with the new key. Each step prints the
 * resulting record to stdout.
 *
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
#include "common/key.hpp"
#include "common/keys.hpp"
#include "common/crypto.hpp"

namespace
{
void Help ()
{
	std::cout << "Usage:" << std::endl
			  << "\tregaddr_3ld step1 privkey address" << std::endl
			  << "\tregaddr_3ld step2 step1file oldprivkey oldaddress" << std::endl
			  << "\tregaddr_3ld step3 step2file privkey" << std::endl;
}

bool LoadKeys (const char * path, i2p::data::PrivateKeys & keys)
{
	return static_cast<bool> (i2p::tools::LoadPrivateKeys (path, keys));
}

i2p::expected<std::string, std::string> ReadText (const char * path)
{
	auto input = i2p::tools::ReadFile (path);
	if (!input) return i2p::unexpected (input.error ());
	std::string text (reinterpret_cast<const char *> (input->data ()), input->size ());
	i2p::tools::Wipe (*input);
	// A step file is exactly one record. Trailing CR/LF is a line
	// terminator, not signed data; an embedded newline would emit two
	// records from one invocation and never verify on the other platform.
	//
	// Only trailing terminators are stripped: leading text is part of the
	// record and would change the signed bytes, so a step file must not be
	// re-indented or wrapped. The signed record itself never contains a
	// terminator; the ending written to stdout follows the platform, so a
	// redirected Windows record reaches the next step with CRLF and is
	// normalized here before signing.
	while (!text.empty () && (text.back () == '\n' || text.back () == '\r')) text.pop_back ();
	if (text.find ('\n') != std::string::npos || text.find ('\r') != std::string::npos)
		return i2p::unexpected ("step file must contain exactly one line");
	if (text.empty ()) return i2p::unexpected ("step file is empty");
	return text;
}

/**
	 * Sign everything accumulated so far, then append the marker and the
	 * base64 signature.
	 *
	 * Call order is the wire contract: each step signs the complete
	 * previous record, so step2 covers step1 plus
	 * `#date=`/`#olddest=`/`#oldname=` and step3 covers the step2 record
	 * including `#oldsig=`. Appending a field after a signature therefore
	 * invalidates that signature instead of silently changing what was
	 * signed.
	 * @return Always true today. `PrivateKeys::Sign` is still a void
	 * interface, so a provider that cannot sign is not observable here and
	 * would produce a zero-filled signature field; a fallible signing
	 * result (tracked as CRY-236) must be propagated through this return
	 * value rather than ignored.
	 */
bool AppendSignature (std::ostringstream & out, i2p::data::PrivateKeys & keys, const char * marker)
{
	const std::string signedPart = out.str ();
	std::vector<uint8_t> signature (keys.GetPublic ()->GetSignatureLen ());
	keys.Sign (reinterpret_cast<const uint8_t *> (signedPart.data ()), static_cast<int> (signedPart.size ()),
		signature.data ());
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

	i2p::tools::CryptoGuard cryptoGuard;
	i2p::data::PrivateKeys keys;
	const std::string action (argv[1]);
	int result = 0;

	if (action == "step1")
	{
		if (argc != 4)
		{
			Help ();
			result = 1;
		}
		else if (!IsValidRecordName (argv[3]))
		{
			std::cerr << "address must not be empty or contain whitespace, '#' or '='" << std::endl;
			result = 2;
		}
		else if (!LoadKeys (argv[2], keys) || !keys.GetPublic ())
		{
			std::cerr << "Failed to load keyfile " << argv[2] << std::endl;
			result = 2;
		}
		else
		{
			std::cout << argv[3] << "=" << keys.GetPublic ()->ToBase64 () << "#!action=addsubdomain" << std::endl;
		}
	}
	else if (action == "step2")
	{
		if (argc != 5)
		{
			Help ();
			result = 1;
		}
		else if (!IsValidRecordName (argv[4]))
		{
			std::cerr << "old name must not be empty or contain whitespace, '#' or '='" << std::endl;
			result = 2;
		}
		else
		{
			auto regtxt = ReadText (argv[2]);
			if (!regtxt || !LoadKeys (argv[3], keys) || !keys.GetPublic ())
			{
				std::cerr << "Failed to read step1 output or keyfile" << std::endl;
				result = 2;
			}
			else
			{
				std::ostringstream out;
				// Field order is fixed: the step1 record, then the observed time,
				// the counter-signing destination and the old name, and finally the
				// old signature. `#date=` is the local wall clock in seconds; this
				// tool neither validates it nor compares clocks, the registration
				// service decides whether the record is fresh enough.
				out << *regtxt << "#date=" << std::time (nullptr) << "#olddest=" << keys.GetPublic ()->ToBase64 ()
					<< "#oldname=" << argv[4];
				AppendSignature (out, keys, "#oldsig=");
				std::cout << out.str () << std::endl;
			}
		}
	}
	else if (action == "step3")
	{
		if (argc != 4)
		{
			Help ();
			result = 1;
		}
		else
		{
			auto regtxt = ReadText (argv[2]);
			if (!regtxt || !LoadKeys (argv[3], keys) || !keys.GetPublic ())
			{
				std::cerr << "Failed to read step2 output or keyfile" << std::endl;
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

	return result;
}
