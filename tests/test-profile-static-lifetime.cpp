/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * Exercises actual process-exit NetDb cleanup with a pending profile
 * callback. The callback capture must be released by NetDb destruction
 * before the observer member is destroyed; no router or disk is started.
 */

#include <cassert>
#include <memory>

#include "NetDb.hpp"
#include "Profiling.h"

namespace
{
struct CaptureObserver
{
	std::weak_ptr<int> capture;
	~CaptureObserver () { assert (capture.expired ()); }
};

struct ExitProfileOwner
{
	// Members are destroyed in reverse order: NetDb must release the
	// callback before this observer checks its capture.
	CaptureObserver observer;
	i2p::data::NetDb database;

	~ExitProfileOwner ()
	{
		i2p::data::IdentHash peer;
		peer.Fill (0xe2);
		auto capture = std::make_shared<int> (42);
		observer.capture = capture;
		const bool applied = i2p::data::UpdateRouterProfile (peer,
			[capture] (std::shared_ptr<i2p::data::RouterProfile>)
			{
				assert (*capture == 42);
				// An unstarted NetDb discards without loading profiles.
				assert (false && "profile callback executed during shutdown");
			});
		assert (!applied);
		capture.reset ();
		assert (!observer.capture.expired ());
	}
};

ExitProfileOwner owner;
}

int main ()
{
	// External ownership keeps the recycler alive even after its NetDb
	// has destroyed every member. Releasing the profile exercises the
	// retained deleter, rather than only reading the still-live object.
	std::shared_ptr<i2p::data::RouterProfile> retained;
	{
		i2p::data::NetDb temporary;
		retained = temporary.NewRouterProfile ();
		retained->Connected ();
	}
	retained->Connected ();
	retained.reset ();

	// The global cache intentionally retains this profile until runtime
	// destruction, after the global NetDb pool member is gone.
	i2p::data::IdentHash cachedPeer;
	cachedPeer.Fill (0xd3);
	auto cached = i2p::data::GetRouterProfile (cachedPeer);
	assert (cached);
	cached->Connected ();
	cached.reset ();

	// The regression runs through static destruction after main returns.
	return 0;
}
