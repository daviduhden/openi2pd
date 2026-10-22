/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test tunnel-config ownership.
 *
 * Checks the owned hop chain, ordering and phony-hop add/remove transitions
 * without starting a router or a tunnel pool.
 */

#include <cassert>
#include <memory>
#include <vector>

#include "Identity.h"
#include "TunnelConfig.h"

int main ()
{
	using namespace i2p::data;
	using namespace i2p::tunnel;

	std::vector<std::shared_ptr<const IdentityEx>> peers;
	for (int i = 0; i < 3; ++i)
	{
		auto keys = PrivateKeys::CreateRandomKeys (
			SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
		peers.push_back (keys.GetPublic ());
	}

	TunnelConfig config (peers, 7, IdentHash { });
	assert (config.GetNumHops () == 3);
	assert (!config.IsEmpty ());
	assert (config.GetFirstHop () != nullptr);
	assert (config.GetLastHop () != nullptr);
	assert (config.GetFirstHop ()->ident == peers.front ());
	assert (config.GetLastHop ()->ident == peers.back ());
	assert (config.GetFirstHop ()->next != nullptr);
	assert (config.GetFirstHop ()->next->prev == config.GetFirstHop ());

	// A phony hop is always appended and removed from the tail.
	config.CreatePhonyHop ();
	assert (config.GetNumHops () == 4);
	assert (config.GetLastHop ()->ident == nullptr);
	config.DeletePhonyHop ();
	assert (config.GetNumHops () == 3);
	assert (config.GetLastHop ()->ident == peers.back ());
	config.DeletePhonyHop (); // non-phony tail is a no-op
	assert (config.GetNumHops () == 3);

	TunnelConfig empty (std::vector<std::shared_ptr<const IdentityEx>> { }, 1, IdentHash { });
	assert (empty.IsEmpty ());
	assert (empty.GetNumHops () == 0);
	return 0;
}
