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

constexpr std::size_t KEYSIZE = 32;

struct PkeyCtxDeleter
{
	void operator() (EVP_PKEY_CTX * ctx) const noexcept { EVP_PKEY_CTX_free (ctx); }
};

struct PkeyDeleter
{
	void operator() (EVP_PKEY * pkey) const noexcept { EVP_PKEY_free (pkey); }
};

using PkeyCtxPtr = std::unique_ptr<EVP_PKEY_CTX, PkeyCtxDeleter>;
using PkeyPtr = std::unique_ptr<EVP_PKEY, PkeyDeleter>;

struct BoxKeys
{
	std::array<uint8_t, KEYSIZE> PublicKey { };
	std::array<uint8_t, KEYSIZE> PrivateKey { };
};

std::optional<BoxKeys> getKeyPair ()
{
	PkeyCtxPtr Ctx (EVP_PKEY_CTX_new_id (NID_X25519, nullptr));
	if (!Ctx) return std::nullopt;

	if (EVP_PKEY_keygen_init (Ctx.get ()) <= 0) return std::nullopt;

	EVP_PKEY * RawPkey = nullptr;
	const int generated = EVP_PKEY_keygen (Ctx.get (), &RawPkey);
	// Own the key as soon as OpenSSL may have produced it, even on failure.
	PkeyPtr Pkey (RawPkey);
	if (generated <= 0 || !Pkey) return std::nullopt;

	BoxKeys keys;

	size_t PublicKeyLen = keys.PublicKey.size ();
	if (EVP_PKEY_get_raw_public_key (Pkey.get (), keys.PublicKey.data (), &PublicKeyLen) <= 0
		|| PublicKeyLen != keys.PublicKey.size ())
		return std::nullopt;

	size_t PrivateKeyLen = keys.PrivateKey.size ();
	if (EVP_PKEY_get_raw_private_key (Pkey.get (), keys.PrivateKey.data (), &PrivateKeyLen) <= 0
		|| PrivateKeyLen != keys.PrivateKey.size ())
		return std::nullopt;

	return keys;
}

int main (int argc, char * argv[])
{
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
	}

	auto newKeys = getKeyPair ();
	if (!newKeys)
	{
		std::cerr << "failed to generate an X25519 key pair" << std::endl;
		return 1;
	}

	auto b64Public = i2p::data::ByteStreamToBase64 (std::span<const uint8_t> (newKeys->PublicKey));

	std::cout << "PublicKey: " << b64Public;

	auto b64Private = i2p::data::ByteStreamToBase64 (std::span<const uint8_t> (newKeys->PrivateKey));

	std::cout << "\nPrivateKey: " << b64Private << std::endl;

	OPENSSL_cleanse (newKeys->PrivateKey.data (), newKeys->PrivateKey.size ());
	OPENSSL_cleanse (b64Private.data (), b64Private.size ());
	return 0;
}
