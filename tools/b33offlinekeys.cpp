/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: b33offlinekeys.
 *
 * Generates a daily b33 offline-key batch from a destination key file and
 * writes the combined online/b33 key file that the router is given. The batch
 * is bounded by the 16-bit day count, and plaintext private material is
 * cleansed.
 *
 */

#include <iostream>
#include <stdlib.h>
#include <inttypes.h>
#include <charconv>
#include <vector>
#include <string.h>
#include <memory>
#include <array>
#include <openssl/crypto.h>
#include "Blinding.h"
#include "I2PEndian.h"
#include "Crypto.h"
#include "Identity.h"
#include "Timestamp.h"
#include "common/file.hpp"
#include "common/key.hpp"
#include "common/crypto.hpp"

const int kMaxDays = 0xFFFF; // the number of keys in the batch is two bytes

namespace
{
struct WipeGuard
{
	uint8_t * data;
	size_t size;
	~WipeGuard ()
	{
		if (data && size) OPENSSL_cleanse (data, size);
	}
};
}

static std::vector<uint8_t> createB33OfflineKeys (
	const i2p::data::PrivateKeys & keys, i2p::data::SigningKeyType transientSigType, int days)
{
	std::vector<uint8_t> buf;
	if (days < 1 || days > kMaxDays) return buf;
	auto identity = keys.GetPublic ();
	if (!identity) return buf;
	i2p::data::BlindedPublicKey blindedKey (identity);
	if (!blindedKey.IsValid ()) return buf;
	std::unique_ptr<i2p::crypto::Verifier> transientVerifier (i2p::data::IdentityEx::CreateVerifier (transientSigType));
	std::unique_ptr<i2p::crypto::Verifier> blindedVerifier (
		i2p::data::IdentityEx::CreateVerifier (blindedKey.GetBlindedSigType ()));
	if (!transientVerifier || !blindedVerifier) return buf;
	const size_t blindedSignatureLen = blindedVerifier->GetSignatureLen ();
	const size_t transientPublicKeyLen = transientVerifier->GetPublicKeyLen ();
	const size_t signedDataLen = i2p::data::OFFLINE_SIGNATURE_HEADER_LENGTH + transientPublicKeyLen;
	const size_t keyLen = signedDataLen + blindedSignatureLen + transientVerifier->GetPrivateKeyLen ();

	buf.resize (i2p::data::B33_OFFLINE_KEYS_HEADER_LENGTH + days * keyLen);
	size_t offset = 0;
	buf[offset] = i2p::data::B33_OFFLINE_KEYS_VERSION;
	offset++;
	memcpy (buf.data () + offset, identity->GetIdentHash (), 32);
	offset += 32;
	htobe16buf (buf.data () + offset, days);
	offset += 2;
	const uint64_t midnight
		= (i2p::util::GetSecondsSinceEpoch () / i2p::data::SECONDS_PER_DAY) * i2p::data::SECONDS_PER_DAY;
	for (int i = 0; i < days; i++)
	{
		char date[9];
		i2p::util::GetDateString (midnight + i * i2p::data::SECONDS_PER_DAY, date);
		uint8_t * signedData = buf.data () + offset;
		htobe32buf (signedData, midnight + (i + 1) * i2p::data::SECONDS_PER_DAY); // expires at the end of that day
		htobe16buf (signedData + 4, transientSigType);
		uint8_t blindedPriv[32], blindedPub[32];
		WipeGuard blindedPrivateGuard { blindedPriv, sizeof (blindedPriv) };
		if (!blindedKey.BlindPrivateKey (keys.GetSigningPrivateKey (), date, blindedPriv, blindedPub))
		{
			i2p::tools::Wipe (buf);
			return buf;
		}
		std::unique_ptr<i2p::crypto::Signer> blindedSigner (
			i2p::data::PrivateKeys::CreateSigner (blindedKey.GetBlindedSigType (), blindedPriv));
		if (!blindedSigner)
		{
			i2p::tools::Wipe (buf);
			return buf;
		}
		i2p::data::PrivateKeys::GenerateSigningKeyPair (transientSigType,
			signedData + signedDataLen + blindedSignatureLen, signedData + i2p::data::OFFLINE_SIGNATURE_HEADER_LENGTH);
		blindedSigner->Sign (signedData, signedDataLen, signedData + signedDataLen);
		offset += keyLen;
	}
	return buf;
}

int main (int argc, char * argv[])
{
	if (argc < 3)
	{
		std::cerr << "Usage: b33offlinekeys <router keys file> <destination keys file> <days>" << std::endl;
		return 1;
	}
	i2p::tools::CryptoGuard cryptoGuard;

	std::string fname (argv[2]);
	i2p::data::PrivateKeys keys;
	if (auto loaded = LoadPrivateKeys (fname, keys); !loaded)
	{
		std::cerr << loaded.error () << std::endl;
		return loaded.error () == "bad keys file format" ? 3 : 2;
	}
	if (keys.IsOfflineSignature ())
	{
		std::cout << "the destination's own keys are required, " << fname << " holds offline keys" << std::endl;
		return 4;
	}
	// only a blindable destination has a b33 address at all
	if (!i2p::data::BlindedPublicKey (keys.GetPublic ()).IsValid ())
	{
		std::cout << fname << " is a " << SigTypeToName (keys.GetPublic ()->GetSigningKeyType ())
				  << " destination and has no b33 address; ED25519-SHA512 or RED25519-SHA512 is required" << std::endl;
		return 5;
	}

	int days = 365; // 1 year by default
	if (argc > 3)
	{
		std::string_view input (argv[3]);
		auto result = std::from_chars (input.data (), input.data () + input.size (), days);
		if (result.ec != std::errc () || result.ptr != input.data () + input.size ())
		{
			std::cout << "days must be 1.." << kMaxDays << std::endl;
			return 6;
		}
	}
	if (days < 1 || days > kMaxDays)
	{
		std::cout << "days must be 1.." << kMaxDays << std::endl;
		return 6;
	}

	// The outer layer of an encrypted LeaseSet is built on blinded keys, which are RedDSA
	const i2p::data::SigningKeyType type = i2p::data::SIGNING_KEY_TYPE_REDDSA_SHA512_ED25519;
	auto b33Keys = createB33OfflineKeys (keys, type, days);
	if (b33Keys.empty ())
	{
		std::cout << "Can't create b33 offline keys" << std::endl;
		return 7;
	}
	// the transient of the inner LeaseSet lasts exactly as long as the batch
	const uint32_t expires
		= (i2p::util::GetSecondsSinceEpoch () / i2p::data::SECONDS_PER_DAY + days) * i2p::data::SECONDS_PER_DAY;
	auto onlineKeys = keys.CreateOfflineKeys (i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, expires);
	if (!onlineKeys.IsOfflineSignature ())
	{
		i2p::tools::Wipe (b33Keys);
		std::cerr << "cannot create the b33 online offline-signature block" << std::endl;
		return 8;
	}
	std::vector<uint8_t> out (onlineKeys.GetFullLen () + b33Keys.size ());
	size_t l = onlineKeys.ToBuffer (out.data (), out.size ());
	if (!l)
	{
		i2p::tools::Wipe (out);
		i2p::tools::Wipe (b33Keys);
		std::cout << "Can't serialize online keys" << std::endl;
		return 8;
	}
	memcpy (out.data () + l, b33Keys.data (), b33Keys.size ());
	out.resize (l + b33Keys.size ());
	const bool written = i2p::tools::WriteFile (argv[1], out.data (), out.size ());
	i2p::tools::Wipe (out);
	i2p::tools::Wipe (b33Keys);
	if (!written)
	{
		std::cout << "Can't create file " << argv[1] << std::endl;
		return 9;
	}

	i2p::data::BlindedPublicKey blindedKey (keys.GetPublic ());
	std::cout << "Address " << blindedKey.ToB33 () << ".b32.i2p, " << days << " days" << std::endl
			  << "Give the router this file and keep the destination keys offline:" << std::endl
			  << "  keys = " << argv[1] << std::endl
			  << "  i2cp.leaseSetType = 5" << std::endl;

	return 0;
}
