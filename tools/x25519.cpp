/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: x25519.
 *
 * Generates an X25519 key pair for encrypted-LeaseSet client authentication
 * and prints the base64 public and private keys. The private scalar is cleansed
 * after printing; --help shows the corresponding I2CP server/client options.
 *
 */

#include <openssl/evp.h>
#include <openssl/bn.h>
#include <openssl/crypto.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include "Base.h"
#include "Crypto.h"
#include "common/crypto.hpp"

constexpr std::size_t KEYSIZE = 32;

int main (int argc, char * argv[])
{
	if (argc > 2)
	{
		std::cerr << "Usage: x25519 [--help|--usage|-h]" << std::endl;
		return 1;
	}
	if (argc > 1)
	{
		std::string arg (argv[1]);
		if (arg == "--usage" || arg == "--help" || arg == "-h")
		{
			std::cout << "The x25519 keys are used for authentication with an encrypted LeaseSet.\n"
					  << "Server side:\n"
					  << "  signaturetype = 11\n"
					  << "  i2cp.leaseSetType = 5\n"
					  << "  i2cp.leaseSetAuthType = 1\n"
					  << "  i2cp.leaseSetClient.dh.210 = clientName:PublicKey\n"
					  << "Client side:\n"
					  << "  i2cp.leaseSetPrivKey = PrivateKey\n\n"
					  << "https://github.com/daviduhden/openi2pd/blob/main/docs/tutorials/blinded-addresse.md"
					  << std::endl;

			return 0;
		}
		std::cerr << "Unknown option: " << arg << "\nUsage: x25519 [--help|--usage|-h]" << std::endl;
		return 1;
	}

	// The core key type fails closed: it never publishes all-zero private or
	// public material, and the private scalar is owned by the RAII object.
	i2p::tools::CryptoGuard cryptoGuard;
	i2p::crypto::X25519Keys keys;
	if (!keys.GenerateKeys () || !keys.HasPrivateKey ())
	{
		std::cerr << "failed to generate an X25519 key pair" << std::endl;
		return 1;
	}
	std::array<uint8_t, KEYSIZE> privateKey { };
	keys.GetPrivateKey (privateKey.data ());

	// Output contract: `PublicKey:` is safe to publish and belongs in
	// `i2cp.leaseSetClient.dh.210`; `PrivateKey:` is the client credential for
	// `i2cp.leaseSetPrivKey` and is printed deliberately, only to stdout, so an
	// operator can capture it. Redirect it into an owner-only file instead of
	// leaving it in a terminal scrollback, and never log or paste it elsewhere.
	// Both are I2P base64 of the raw 32-byte values, not PEM. The local copy and
	// its base64 rendering are cleansed below; the public bytes are not secret.
	// Exit status is 0 for help or a generated pair and 1 for an extra operand,
	// an unknown option (reported on stderr) or a key generation failure.
	auto b64Public = i2p::data::ByteStreamToBase64 (std::span<const uint8_t> (keys.GetPublicKey (), KEYSIZE));
	std::cout << "PublicKey: " << b64Public;

	auto b64Private = i2p::data::ByteStreamToBase64 (std::span<const uint8_t> (privateKey));
	std::cout << "\nPrivateKey: " << b64Private << std::endl;

	OPENSSL_cleanse (privateKey.data (), privateKey.size ());
	OPENSSL_cleanse (b64Private.data (), b64Private.size ());
	return 0;
}
