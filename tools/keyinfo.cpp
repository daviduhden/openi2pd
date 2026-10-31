/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: keyinfo.
 *
 * Inspects an I2P private-key file and prints destination, signature,
 * encryption and offline-signature details. Flags select the b32
 * address (default), base64 destination, raw private key, or blinded
 * b33 address plus any b33 offline-key range.
 *
 * The b33 offline-key batch is optional, so a key file without one is
 * summarized successfully. Every section is validated before the first
 * stdout byte: data goes to stdout, diagnostics to stderr, and a failing
 * invocation leaves stdout empty.
 *
 */

#include "Identity.h"
#include "I2PEndian.h"
#include "LeaseSet.h"
#include "Crypto.h"
#include <iostream>
#include <fstream>
#include <string>
#include <utility>
#include <vector>
#include <memory>
#include "common/getopt.h"
#include <stdio.h>
#include <time.h>
#include "common/file.hpp"
#include "common/key.hpp"
#include "common/keys.hpp"
#include "common/crypto.hpp"

// version || ident hash || number of keys, then per day expires ||
// transient signature type || transient public key || signature by that
// day's blinded key || transient private key
/// @brief Result of validating the b33 offline-key batch of a key file.
struct B33OfflineKeySummary
{
	bool valid = false; ///< false: a diagnostic was written, print nothing.
	std::string line; ///< stdout summary; empty when the file has no batch.
};

/// @brief Validate and summarize the b33 offline-key batch in a key
/// file.
/// A key file is local but not necessarily well formed (an old file, a
/// truncated copy or a hand-edited blob), so every field is
/// bounds-checked against the remaining length before it is read: the
/// day count, the per-record header, the public/private key and
/// signature lengths derived from the declared verifiers, and the
/// resulting record stride. A record whose derived length does not fit
/// fails the whole batch instead of being summarized partially, and an
/// expiry below one day is rejected as structurally invalid.
/// The batch is optional: `keygen` and every deployed destination key
/// file carries none, so absence is a note on stderr and a successful
/// summary rather than a tool failure. Only structurally invalid data is
/// an error, because reporting a truncated range would misrepresent the
/// file.
/// The summary is returned instead of printed so main() can validate
/// every section before it writes the first stdout byte; a failing
/// invocation must not leave a partial listing (or, with -p, private key
/// material) on stdout.
/// Output lists one UTC date per covered day; dates are derived from
/// the expiry minus one day so the first day of the batch is reported
/// as its start.
/// @param keys Destination keys that may carry the batch.
/// @param blindedKey Supplies the blinded-key signature length used to
/// derive each record's stride; the batch is not otherwise
/// cryptographically verified here.
/// @return `valid` false after a stderr diagnostic; otherwise `line`
/// holds the stdout summary, or is empty when the key file carries no
/// batch.
static B33OfflineKeySummary DescribeB33OfflineKeys (
	const i2p::data::PrivateKeys & keys, const i2p::data::BlindedPublicKey & blindedKey)
{
	B33OfflineKeySummary summary;
	const auto & batch = keys.GetB33OfflineKeys ();
	const size_t batchLen = batch.GetLen ();
	if (batchLen < i2p::data::B33_OFFLINE_KEYS_HEADER_LENGTH)
	{
		std::cerr << "b33 offline keys: no batch in this key file" << std::endl;
		summary.valid = true;
		return summary;
	}

	std::unique_ptr<i2p::crypto::Verifier> blindedVerifier (
		i2p::data::IdentityEx::CreateVerifier (blindedKey.GetBlindedSigType ()));
	if (!blindedVerifier)
	{
		std::cerr << "b33 offline keys: unsupported blinded signature type" << std::endl;
		return summary;
	}

	const uint8_t * buf = batch.GetBuffer ();
	if (!buf || batch.GetLen () < i2p::data::B33_OFFLINE_KEYS_HEADER_LENGTH)
	{
		std::cerr << "b33 offline keys: missing batch data" << std::endl;
		return summary;
	}

	size_t offset = i2p::data::B33_OFFLINE_KEYS_HEADER_LENGTH;
	uint16_t numKeys = bufbe16toh (buf + offset - 2);
	if (!numKeys)
	{
		// A header without records covers no day and contributes no key,
		// so report the same absence as a key file without the section.
		std::cerr << "b33 offline keys: no usable days in this key file" << std::endl;
		summary.valid = true;
		return summary;
	}

	std::string firstDate, lastDate;
	bool complete = true;
	for (uint16_t i = 0; i < numKeys; i++)
	{
		// the record starts with expires(4) + transient signature type(2)
		if (offset > batchLen || batchLen - offset < i2p::data::OFFLINE_SIGNATURE_HEADER_LENGTH)
		{
			complete = false;
			break;
		}

		std::unique_ptr<i2p::crypto::Verifier> transientVerifier (
			i2p::data::IdentityEx::CreateVerifier (bufbe16toh (buf + offset + 4)));
		if (!transientVerifier)
		{
			complete = false;
			break;
		}

		const size_t publicKeyLen = transientVerifier->GetPublicKeyLen ();
		const size_t offlineSignatureLen = blindedVerifier->GetSignatureLen ();
		const size_t privateKeyLen = transientVerifier->GetPrivateKeyLen ();
		if (!publicKeyLen || !offlineSignatureLen || !privateKeyLen)
		{
			complete = false;
			break;
		}

		const size_t recordLen
			= i2p::data::OFFLINE_SIGNATURE_HEADER_LENGTH + publicKeyLen + offlineSignatureLen + privateKeyLen;
		if (recordLen > batchLen - offset)
		{
			complete = false;
			break;
		}

		const uint32_t expires = bufbe32toh (buf + offset);
		if (expires < i2p::data::SECONDS_PER_DAY)
		{
			complete = false;
			break;
		}

		char date[9] { };
		i2p::util::GetDateString (expires - i2p::data::SECONDS_PER_DAY, date);
		if (!i) firstDate = date;
		lastDate = date;
		offset += recordLen;
	}

	if (!complete)
	{
		std::cerr << "b33 offline keys: invalid or truncated data" << std::endl;
		return summary;
	}
	summary.valid = true;
	summary.line = "b33 offline keys: " + std::to_string (numKeys) + " days, " + firstDate + " to " + lastDate;
	return summary;
}

/// @brief Print the usage synopsis to an explicit stream.
/// `-h` writes it to stdout with status 0; every rejected invocation
/// writes it to stderr so stdout stays empty.
static int printHelp (const char * exe, int exitcode)
{
	std::ostream & out = (exitcode == 0) ? std::cout : std::cerr;
	out << "usage: " << exe << " [-v] [-d] [-p] [-b] privatekey.dat" << std::endl;
	return exitcode;
}

std::string ConvertTime (time_t t)
{
	struct tm tm { };
#ifdef _WIN32
	if (localtime_s (&tm, &t) != 0) return "unknown";
#else
	if (!localtime_r (&t, &tm)) return "unknown";
#endif
	char date[128];
	snprintf (date, sizeof (date), "%02d/%02d/%d %02d:%02d:%02d", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900,
		tm.tm_hour, tm.tm_min, tm.tm_sec);
	return date;
}

int main (int argc, char * argv[])
{
	if (argc == 1)
	{
		return printHelp (argv[0], 2);
	}

	int opt;
	bool print_dest = false;
	bool print_private = false;
	bool print_blinded = false;
	bool verbose = false;
	while ((opt = getopt (argc, argv, "hvdpb")) != -1)
	{
		switch (opt)
		{
		case 'h': return printHelp (argv[0], 0);
		case 'v': verbose = true; break;
		case 'd': print_dest = true; break;
		case 'p': print_private = true; break;
		case 'b': print_blinded = true; break;
		default: return printHelp (argv[0], 2);
		}
	}

	if (optind >= argc)
	{
		std::cerr << "missing private key file" << std::endl;
		return 2;
	}
	if (optind + 1 != argc)
	{
		std::cerr << "unexpected extra argument: " << argv[optind + 1] << std::endl;
		return printHelp (argv[0], 2);
	}

	i2p::tools::CryptoGuard cryptoGuard;

	std::string fname (argv[optind]);
	i2p::data::PrivateKeys keys;
	auto loaded = i2p::tools::LoadPrivateKeys (fname, keys);
	if (!loaded)
	{
		if (loaded.error () == "bad keys file format")
		{
			std::cerr << "bad key file format" << std::endl;
			return 3;
		}
		std::cerr << loaded.error () << std::endl;
		return 2;
	}

	auto dest = keys.GetPublic ();
	if (!dest)
	{
		std::cerr << "failed to extract public key" << std::endl;
		return 3;
	}

	// Validate every section before the first stdout byte. Diagnostics go
	// to stderr, so a failing invocation leaves stdout empty instead of
	// partial: with -p that would otherwise publish private key material
	// for a request the tool is about to reject.
	const auto & offlineSignature = keys.GetOfflineSignature ();
	if (keys.IsOfflineSignature () && offlineSignature.size () < i2p::data::OFFLINE_SIGNATURE_HEADER_LENGTH)
	{
		std::cerr << "Invalid offline signature" << std::endl;
		return 3;
	}

	// -b is a summary mode for keys that have a blinded form. Rejecting
	// an unblindable key here keeps the refusal independent of the
	// selected output mode and before any material is printed.
	std::string b33Address, b33StoreHash, b33BatchSummary;
	if (print_blinded)
	{
		const auto sigType = dest->GetSigningKeyType ();
		if (!IsBlindableSigningKeyType (sigType))
		{
			std::cerr << "Invalid signature type " << SigTypeToName (sigType) << std::endl;
			return 3;
		}
		i2p::data::BlindedPublicKey blindedKey (dest);
		if (!blindedKey.IsValid ())
		{
			// A blindable type with unusable key material: report the data
			// problem instead of mislabelling the signature type.
			std::cerr << "Invalid blinded public key" << std::endl;
			return 3;
		}
		b33Address = "b33 address: " + blindedKey.ToB33 () + ".b32.i2p";
		b33StoreHash = "Today's store hash: " + blindedKey.GetStoreHash ().ToBase64 ();
		auto summary = DescribeB33OfflineKeys (keys, blindedKey);
		if (!summary.valid) return 3;
		b33BatchSummary = std::move (summary.line);
	}

	const auto & ident = dest->GetIdentHash ();
	if (verbose)
	{
		std::cout << "Destination: " << dest->ToBase64 () << std::endl;
		std::cout << "Destination Hash: " << ident.ToBase64 () << std::endl;
		std::cout << "B32 Address: " << ident.ToBase32 () << ".b32.i2p" << std::endl;
		std::cout << "Signature Type: " << SigTypeToName (dest->GetSigningKeyType ()) << std::endl;
		std::cout << "Encryption Type: " << (int)dest->GetCryptoKeyType () << std::endl;
		if (keys.IsOfflineSignature ())
		{
			std::cout << "Offline signature" << std::endl;
			std::cout << "Expires: " << ConvertTime (bufbe32toh (offlineSignature.data ())) << std::endl;
			std::cout << "Transient Signature Type: " << SigTypeToName (bufbe16toh (offlineSignature.data () + 4))
					  << std::endl;
		}
	}
	else
	{
		if (print_private)
		{
			std::cout << keys.ToBase64 () << std::endl;
		}
		else if (print_dest)
		{
			std::cout << dest->ToBase64 () << std::endl;
		}
		else
		{
			std::cout << ident.ToBase32 () << ".b32.i2p" << std::endl;
		}
	}

	if (print_blinded)
	{
		std::cout << b33Address << std::endl;
		std::cout << b33StoreHash << std::endl;
		if (!b33BatchSummary.empty ()) std::cout << b33BatchSummary << std::endl;
	}

	return 0;
}
