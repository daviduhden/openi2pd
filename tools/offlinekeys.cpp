/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: offlinekeys.
 *
 * Creates an offline-signature key file from a destination's private
 * keys, with a selectable signature type and an expiry of 1 to 3650
 * days. Input and output key buffers are wiped after use.
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
#include "common/keys.hpp"
#include "common/crypto.hpp"

namespace
{
bool ParseDays (const char * text, int & days)
{
	auto value = ParseBounded<int> (text ? std::string_view (text) : std::string_view (), 1, 3650);
	if (!value) return false;
	days = *value;
	return true;
}
}

/// @brief Create an offline-signature key set for an existing
/// destination.
/// Reads the destination's own key file, derives an offline signing key
/// pair for the requested signature type and validity window, and
/// writes the resulting blob to a separate path. The input key file is
/// never modified, and an output path that resolves to the input is
/// refused before anything is read or written, because the blob
/// replaces rather than augments the file.
/// Defaults: Ed25519 (`EDDSA-SHA512-ED25519`) for 365 days. The private
/// part of the created blob is written only through the owner-only
/// atomic writer, so it is never briefly world-readable and a crash
/// cannot leave a truncated key.
/// Exit codes are part of the CLI contract: 0 success, 1 usage or an
/// output path that would overwrite the input, 2 an unreadable key file
/// or a bad signature-type/day argument, 3 a structurally invalid key
/// file or a failed write, 4 the core cannot create offline keys for
/// the requested type.
/// @post Secrets are held in a wiping buffer; only the destination hash
/// and the non-secret parameters are printed.
int main (int argc, char * argv[])
{
	constexpr const char * usage = "Usage: offlinekeys <output file> <keys file> [<signature type>] [<days>]\n";
	if (argc == 2 && (std::string_view (argv[1]) == "-h" || std::string_view (argv[1]) == "--help"))
	{
		std::cout << usage;
		return 0;
	}
	if (argc < 3 || argc > 5)
	{
		std::cerr << usage;
		return 1;
	}
	if (i2p::tools::SameFile (argv[1], argv[2]))
	{
		std::cerr << "refusing to overwrite the destination key file" << std::endl;
		return 1;
	}

	i2p::tools::CryptoGuard cryptoGuard;

	i2p::data::PrivateKeys keys;
	if (auto loaded = i2p::tools::LoadPrivateKeys (argv[2], keys); !loaded)
	{
		std::cerr << loaded.error () << std::endl;
		return loaded.error () == "bad keys file format" ? 3 : 2;
	}

	i2p::data::SigningKeyType type = i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519;
	if (argc > 3)
	{
		auto requested = NameToSigType (argv[3]);
		if (!requested)
		{
			std::cerr << "Incorrect signature type" << std::endl;
			return 2;
		}
		type = static_cast<i2p::data::SigningKeyType> (*requested);
	}

	int days = 365;
	if (argc > 4 && !ParseDays (argv[4], days))
	{
		std::cerr << "Days must be 1..3650" << std::endl;
		return 2;
	}

	const uint32_t expires = i2p::util::GetSecondsSinceEpoch () + static_cast<uint32_t> (days) * 24u * 60u * 60u;
	auto offlineKeys = keys.CreateOfflineKeys (type, expires);
	if (!offlineKeys.IsOfflineSignature ())
	{
		std::cerr << "cannot create offline signature keys for " << SigTypeToName (type) << std::endl;
		return 4;
	}
	i2p::tools::SecureBuffer buffer (offlineKeys.GetFullLen ());
	const size_t len = offlineKeys.ToBuffer (buffer.data (), buffer.size ());
	const bool written = len && i2p::tools::SavePrivateKeys (argv[1], buffer.span ().first (len));
	if (!written)
	{
		std::cerr << "Can't create " << argv[1] << ": the path must be a writable regular file" << std::endl;
		return 3;
	}

	std::cout << "Offline keys for destination " << offlineKeys.GetPublic ()->GetIdentHash ().ToBase32 () << " created"
			  << std::endl
			  << "Signature type: " << SigTypeToName (type) << " (" << type << ")" << std::endl
			  << "Days: " << days << std::endl;
	return 0;
}
