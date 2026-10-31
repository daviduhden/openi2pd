/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test tunnel peer-selector lifetime.
 *
 * The custom peer selector is registered with an owner and must stay
 * alive for the whole SelectPeers() call, while a released owner must
 * never be called. Deterministic: the owner is dropped explicitly, no
 * threads or sleeps.
 *
 */

#include <cassert>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "Tunnel.h"
#include "TunnelPool.h"

namespace
{
using namespace i2p::tunnel;

class CountingSelector final : public ITunnelPeerSelector
{
public:
	bool SelectPeers (Path & /* peers */, int /* hops */, bool /* isInbound */) override
	{
		++calls;
		if (onCall) onCall ();
		return accept;
	}

	int calls = 0;
	bool accept = true;
	std::function<void ()> onCall;
};

struct Owner
{
	~Owner ()
	{
		if (onDestroy) onDestroy ();
	}
	std::function<void ()> onDestroy;
};

void TestNoSelector ()
{
	Path path;
	TunnelPeerSelectorHandle empty;
	assert (!empty.HasSelector ());
	assert (!empty.HasOwner ());
	assert (empty.TrySelectPeers (path, 1, true) == TunnelPeerSelectorHandle::Result::NoSelector);
	assert (path.peers.empty ());
}

void TestSelectorWithoutOwner ()
{
	CountingSelector selector;
	TunnelPeerSelectorHandle handle (&selector, nullptr);
	assert (handle.HasSelector ());
	assert (!handle.HasOwner ());
	Path path;
	assert (handle.TrySelectPeers (path, 2, false) == TunnelPeerSelectorHandle::Result::Accepted);
	assert (selector.calls == 1);

	selector.accept = false;
	assert (handle.TrySelectPeers (path, 2, false) == TunnelPeerSelectorHandle::Result::Rejected);
	assert (selector.calls == 2);
}

void TestOwnerPinnedForTheWholeCall ()
{
	// The selector drops the last external reference to its own owner
	// while it runs. Only the handle's pin keeps it alive; without the pin
	// the selector would be used after its owner was destroyed.
	CountingSelector selector;
	auto owner = std::make_shared<Owner> ();
	bool destroyed = false;
	owner->onDestroy = [&destroyed] () { destroyed = true; };
	std::shared_ptr<Owner> external = owner;
	selector.onCall = [&external, &destroyed] ()
	{
		external.reset ();
		assert (!destroyed); // still pinned by TrySelectPeers()
	};

	TunnelPeerSelectorHandle handle (&selector, owner);
	owner.reset ();
	Path path;
	assert (handle.TrySelectPeers (path, 1, true) == TunnelPeerSelectorHandle::Result::Accepted);
	assert (selector.calls == 1);
	assert (destroyed); // released when the call returned

	// With the owner gone the selector is never called again.
	assert (handle.TrySelectPeers (path, 1, true) == TunnelPeerSelectorHandle::Result::OwnerGone);
	assert (selector.calls == 1);
}

void TestLiveOwnerKeepsSelectorUsable ()
{
	CountingSelector selector;
	auto owner = std::make_shared<int> (0);
	TunnelPeerSelectorHandle handle (&selector, owner);
	Path path;
	assert (handle.TrySelectPeers (path, 1, true) == TunnelPeerSelectorHandle::Result::Accepted);
	assert (selector.calls == 1);
	owner.reset ();
	assert (handle.TrySelectPeers (path, 1, true) == TunnelPeerSelectorHandle::Result::OwnerGone);
	assert (selector.calls == 1);
}

void TestPoolRegistration ()
{
	auto pool = std::make_shared<TunnelPool> (2, 2, 1, 1, 0, 0, false);
	assert (!pool->HasCustomPeerSelector ());

	CountingSelector selector;
	auto owner = std::make_shared<int> (0);
	pool->SetCustomPeerSelector (&selector, owner);
	assert (pool->HasCustomPeerSelector ());

	auto handle = pool->GetCustomPeerSelector ();
	assert (handle.HasSelector ());
	assert (handle.HasOwner ());
	Path path;
	assert (handle.TrySelectPeers (path, 1, false) == TunnelPeerSelectorHandle::Result::Accepted);

	// Dropping the owner retires the registration without a dangling
	// pointer.
	owner.reset ();
	auto retired = pool->GetCustomPeerSelector ();
	assert (retired.HasSelector ());
	assert (retired.TrySelectPeers (path, 1, false) == TunnelPeerSelectorHandle::Result::OwnerGone);
	assert (selector.calls == 1);

	pool->UnsetCustomPeerSelector ();
	assert (!pool->HasCustomPeerSelector ());
	assert (
		pool->GetCustomPeerSelector ().TrySelectPeers (path, 1, false) == TunnelPeerSelectorHandle::Result::NoSelector);

	tunnels.DeleteTunnelPool (pool);
}
}

int main ()
{
	TestNoSelector ();
	TestSelectorWithoutOwner ();
	TestOwnerPinnedForTheWholeCall ();
	TestLiveOwnerKeepsSelectorUsable ();
	TestPoolRegistration ();
	return 0;
}
