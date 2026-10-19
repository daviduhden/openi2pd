/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: offlinekeys.
 *
 * Creates an offline-signature key file from a destination's private keys,
 * with a selectable signature type and an expiry of 1 to 3650 days. Input and
 * output key buffers are wiped after use.
 *
 */

#include <charconv>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

#include "Crypto.h"
#include "Identity.h"
#include "Timestamp.h"
#include "common/file.hpp"
#include "common/key.hpp"

namespace
{
bool ParseDays (const char * text, int & days)
{
	int value = 0;
	std::string_view input (text);
	auto result = std::from_chars (input.data (), input.data () + input.size (), value);
	if (result.ec != std::errc () || result.ptr != input.data () + input.size ()) return false;
	if (value < 1 || value > 3650) return false;
	days = value;
	return true;
}
}

int main (int argc, char * argv[])
{
	if (argc < 3)
	{
		std::cout << "Usage: offlinekeys <output file> <keys file> <signature type> <days>" << std::endl;
		return 1;
	}

	i2p::crypto::InitCrypto (false);

	auto input = i2p::tools::ReadFile (argv[2]);
	if (!input)
	{
		std::cerr << input.error () << std::endl;
		i2p::crypto::TerminateCrypto ();
		return 2;
	}

	i2p::data::PrivateKeys keys;
	if (!keys.FromBuffer (input->data (), input->size ()))
	{
		i2p::tools::Wipe (*input);
		std::cout << "bad keys file format" << std::endl;
		i2p::crypto::TerminateCrypto ();
		return 3;
	}
	i2p::tools::Wipe (*input);

	i2p::data::SigningKeyType type = i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519;
	if (argc > 3)
	{
		auto requested = NameToSigType (argv[3]);
		if (!requested)
		{
			std::cerr << "Incorrect signature type" << std::endl;
			i2p::crypto::TerminateCrypto ();
			return 2;
		}
		type = static_cast<i2p::data::SigningKeyType> (*requested);
	}

	int days = 365;
	if (argc > 4 && !ParseDays (argv[4], days))
	{
		std::cerr << "Days must be 1..3650" << std::endl;
		i2p::crypto::TerminateCrypto ();
		return 2;
	}

	const uint32_t expires = i2p::util::GetSecondsSinceEpoch () + static_cast<uint32_t> (days) * 24u * 60u * 60u;
	auto offlineKeys = keys.CreateOfflineKeys (type, expires);
	std::vector<uint8_t> buffer (offlineKeys.GetFullLen ());
	const size_t len = offlineKeys.ToBuffer (buffer.data (), buffer.size ());
	const bool written = len && i2p::tools::WriteFile (argv[1], buffer.data (), len);
	i2p::tools::Wipe (buffer);
	if (!written)
	{
		std::cout << "Can't create file " << argv[1] << std::endl;
		i2p::crypto::TerminateCrypto ();
		return 3;
	}

	std::cout << "Offline keys for destination " << offlineKeys.GetPublic ()->GetIdentHash ().ToBase32 () << " created"
			  << std::endl
			  << "Signature type: " << SigTypeToName (type) << " (" << type << ")" << std::endl
			  << "Days: " << days << std::endl;
	i2p::crypto::TerminateCrypto ();
	return 0;
}
