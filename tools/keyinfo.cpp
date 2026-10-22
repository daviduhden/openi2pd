/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: keyinfo.
 *
 * Inspects an I2P private-key file and prints destination, signature,
 * encryption and offline-signature details. Flags select the b32 address
 * (default), base64 destination, raw private key, or blinded b33 address plus
 * any b33 offline-key range.
 *
 */

#include "Identity.h"
#include "I2PEndian.h"
#include "LeaseSet.h"
#include "Crypto.h"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <memory>
#include "common/getopt.h"
#include <stdio.h>
#include <time.h>
#include "common/file.hpp"
#include "common/key.hpp"

namespace
{
struct InputWiper
{
	std::vector<uint8_t> & data;
	~InputWiper () { i2p::tools::Wipe (data); }
};

struct CryptoGuard
{
	CryptoGuard () { i2p::crypto::InitCrypto (false); }
	~CryptoGuard () { i2p::crypto::TerminateCrypto (); }
};
}

// version || ident hash || number of keys, then per day expires || transient signature type ||
// transient public key || signature by that day's blinded key || transient private key
static void printB33OfflineKeys (const i2p::data::PrivateKeys & keys, const i2p::data::BlindedPublicKey & blindedKey)
{
	const auto & batch = keys.GetB33OfflineKeys ();
	const size_t batchLen = batch.GetLen ();
	if (batchLen < i2p::data::B33_OFFLINE_KEYS_HEADER_LENGTH) return;

	std::unique_ptr<i2p::crypto::Verifier> blindedVerifier (
		i2p::data::IdentityEx::CreateVerifier (blindedKey.GetBlindedSigType ()));
	if (!blindedVerifier) return;

	const uint8_t * buf = batch.GetBuffer ();
	if (!buf || batch.GetLen () < i2p::data::B33_OFFLINE_KEYS_HEADER_LENGTH) return;

	size_t offset = i2p::data::B33_OFFLINE_KEYS_HEADER_LENGTH;
	uint16_t numKeys = bufbe16toh (buf + offset - 2);
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
		std::cout << "b33 offline keys: invalid or truncated data" << std::endl;
		return;
	}
	std::cout << "b33 offline keys: " << numKeys << " days, " << firstDate << " to " << lastDate << std::endl;
}

static int printHelp (const char * exe, int exitcode)
{
	std::cout << "usage: " << exe << " [-v] [-d] [-p] [-b] privatekey.dat" << std::endl;
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

	CryptoGuard cryptoGuard;

	std::string fname (argv[optind]);
	i2p::data::PrivateKeys keys;
	auto input = i2p::tools::ReadFile (fname);
	if (!input)
	{
		std::cout << input.error () << std::endl;
		return 2;
	}
	InputWiper inputWiper { *input };
	if (!keys.FromBuffer (input->data (), input->size ()))
	{
		std::cout << "bad key file format" << std::endl;
		return 3;
	}

	auto dest = keys.GetPublic ();
	if (!dest)
	{
		std::cout << "failed to extract public key" << std::endl;
		return 3;
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
			const auto & offlineSignature = keys.GetOfflineSignature ();
			if (offlineSignature.size () < i2p::data::OFFLINE_SIGNATURE_HEADER_LENGTH)
			{
				std::cout << "Invalid offline signature" << std::endl;
				return 3;
			}
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
		if (dest->GetSigningKeyType () == i2p::data::SIGNING_KEY_TYPE_REDDSA_SHA512_ED25519
			|| dest->GetSigningKeyType () == i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519) // 11 or 7
		{
			i2p::data::BlindedPublicKey blindedKey (dest);
			std::cout << "b33 address: " << blindedKey.ToB33 () << ".b32.i2p" << std::endl;
			std::cout << "Today's store hash: " << blindedKey.GetStoreHash ().ToBase64 () << std::endl;
			printB33OfflineKeys (keys, blindedKey);
		}
		else
			std::cout << "Invalid signature type " << SigTypeToName (dest->GetSigningKeyType ()) << std::endl;
	}

	return 0;
}
