/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * Pins reload-local identity reuse without sockets or key generation.
 */

#include <cassert>
#include <initializer_list>
#include <memory>
#include <string>

#include "TunnelDestinationCache.h"

int main ()
{
	using i2p::client::TunnelDestinationCache;
	using i2p::client::UsesTransientTunnelKeys;
	static_assert (UsesTransientTunnelKeys ("transient"));
	static_assert (UsesTransientTunnelKeys ("transient-client"));
	static_assert (!UsesTransientTunnelKeys ("keys/transient.dat"));
	static_assert (!UsesTransientTunnelKeys ("server.dat"));

	auto first = std::make_shared<int> (1);
	auto second = std::make_shared<int> (2);
	std::weak_ptr<int> released = first;
	{
		TunnelDestinationCache<int> cache;
		for (const std::string & marker : { std::string ("transient"), std::string ("transient-client") })
		{
			cache.Remember (marker, first);
			assert (!cache.Find (marker));
			cache.Remember (marker, second);
			assert (!cache.Find (marker));
		}
		cache.Remember ("shared.dat", first);
		assert (cache.Find ("shared.dat") == first);
		cache.Remember ("shared.dat", second);
		assert (cache.Find ("shared.dat") == second);
		cache.Remember ("missing.dat", nullptr);
		assert (!cache.Find ("missing.dat"));
		cache.Remember ("shared.dat", nullptr);
		assert (cache.Find ("shared.dat") == second);
		first.reset ();
		assert (released.expired ());
	}
	assert (second.use_count () == 1);
	return 0;
}
