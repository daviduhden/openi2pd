/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: routerinfo fixture generator.
 *
 * Creates a signed RouterInfo for the native tool smoke test using the
 * production LocalRouterInfo serializer. Its documentation-range
 * endpoints and signing identity are test-only; the identity is
 * ephemeral.
 */

#include <array>
#include <iostream>
#include <string>

#include <boost/asio/ip/address.hpp>

#include "Crypto.h"
#include "Identity.h"
#include "RouterContext.h"
#include "RouterInfo.h"

namespace
{
struct CryptoGuard
{
	CryptoGuard () { i2p::crypto::InitCrypto (false); }
	~CryptoGuard () { i2p::crypto::TerminateCrypto (); }
};
}

int main (int argc, char * argv[])
{
	if (argc != 2)
	{
		std::cerr << "usage: routerinfo-fixture output-path" << std::endl;
		return 2;
	}

	CryptoGuard crypto;
	auto keys = i2p::data::PrivateKeys::CreateRandomKeys (
		i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
	if (!keys.GetPublic ())
	{
		std::cerr << "routerinfo-fixture: cannot create signing identity" << std::endl;
		return 1;
	}

	i2p::data::LocalRouterInfo router;
	router.SetRouterIdentity (keys.GetPublic ());
	router.SetProperty ("netId", std::to_string (i2p::context.GetNetID ()));
	router.SetProperty ("router.version", "2.50.0");
	router.SetProperty ("caps", "O");

	const std::array<uint8_t, 32> staticKey = { 0x11 };
	const std::array<uint8_t, 16> iv = { 0x22 };
	const std::array<uint8_t, 32> introKey = { 0x33 };
	router.AddNTCP2Address (staticKey.data (), iv.data (), boost::asio::ip::make_address ("192.0.2.10"), 12345);
	router.AddSSU2Address (staticKey.data (), introKey.data (), boost::asio::ip::make_address ("198.51.100.20"), 12346);
	router.AddNTCP2Address (staticKey.data (), iv.data (), boost::asio::ip::make_address ("2001:db8::10"), 12347);
	router.AddSSU2Address (staticKey.data (), introKey.data (), boost::asio::ip::make_address ("2001:db8::20"), 12348);
	router.CreateBuffer (keys);
	if (!router.SaveToFile (argv[1]))
	{
		std::cerr << "routerinfo-fixture: cannot write public RouterInfo" << std::endl;
		return 1;
	}
	return 0;
}
