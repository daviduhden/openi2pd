/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: transports lifecycle and admission guards.
 *
 * Three contracts are pinned here without starting a service:
 *
 * - A component may try to send before Transports::Start() created its
 *   io_context. The online flag defaults to true, so the guard must test
 *   the service itself; otherwise boost::asio::post dereferences a null
 *   unique_ptr and the process crashes. Both send entry points must report
 *   an invalid future and drop the message, exactly like the offline path.
 * - A staggered peer-test timer survives Stop()/Start() because the
 *   transports io_context is restarted, not rebuilt. Its handler must be
 *   discarded unless it belongs to the current run and SSU2 exists.
 * - The trusted-router set is replaced by an administrative call while
 *   netDb and transport threads query it, so every read takes its mutex.
 *
 */

#include <atomic>
#include <cassert>
#include <cstdint>
#include <list>
#include <memory>
#include <thread>

#include "Identity.h"
#include "I2NPProtocol.h"
#include "Transports.h"

namespace
{
	void CheckSendGuards ()
	{
		i2p::transport::Transports & transports = i2p::transport::transports;

		// Transports are online by default but not running yet: Start() is
		// what creates the service, and this test never calls it.
		assert (transports.IsOnline ());
		assert (!transports.IsRunning ());

		i2p::data::IdentHash peer;
		peer.Fill (0x11);

		auto message = i2p::NewI2NPShortMessage ();
		assert (message);

		auto single = transports.SendMessage (peer, message);
		assert (!single.valid ());

		std::list<std::shared_ptr<i2p::I2NPMessage>> batch;
		batch.push_back (message);
		auto many = transports.SendMessages (peer, std::move (batch));
		assert (!many.valid ());

		// An explicit offline switch keeps the same contract.
		transports.SetOnline (false);
		assert (!transports.SendMessage (peer, message).valid ());
	}

	void CheckPeerTestRunGeneration ()
	{
		using i2p::transport::IsPeerTestRunCurrent;
		// Only the run that scheduled the timer may act on it.
		assert (IsPeerTestRunCurrent (7, 7, true, true));
		assert (!IsPeerTestRunCurrent (6, 7, true, true)); // previous instance
		assert (!IsPeerTestRunCurrent (8, 7, true, true)); // future instance
		assert (!IsPeerTestRunCurrent (7, 7, false, true)); // stopped
		assert (!IsPeerTestRunCurrent (7, 7, true, false)); // SSU2 disabled
	}

	void CheckTrustedRouterSnapshot ()
	{
		i2p::transport::Transports & transports = i2p::transport::transports;

		i2p::data::IdentHash trusted;
		trusted.Fill (0x21);
		i2p::data::IdentHash other;
		other.Fill (0x22);

		transports.RestrictRoutesToRouters ({});
		assert (!transports.RoutesRestricted ());
		assert (!transports.IsTrustedRouter (trusted));

		transports.RestrictRoutesToRouters ({ trusted });
		assert (transports.RoutesRestricted ());
		assert (transports.IsTrustedRouter (trusted));
		assert (!transports.IsTrustedRouter (other));

		// The set is replaced while other threads query it (netDb selection,
		// session admission). An unsynchronised emptiness test here is a data
		// race that only the TSan lane can observe, so drive the interleaving.
		std::atomic<bool> stop { false };
		std::thread reader ([&] {
			while (!stop.load (std::memory_order_relaxed))
			{
				(void)transports.IsTrustedRouter (trusted);
				(void)transports.IsTrustedRouter (other);
			}
		});
		for (int i = 0; i < 2000; i++)
		{
			transports.RestrictRoutesToRouters ({});
			transports.RestrictRoutesToRouters ({ trusted });
		}
		stop.store (true, std::memory_order_relaxed);
		reader.join ();

		// A non-empty restricted list is the last state written.
		assert (transports.IsTrustedRouter (trusted));
		assert (!transports.IsTrustedRouter (other));
		transports.RestrictRoutesToRouters ({});
	}
} // namespace

int main ()
{
	CheckSendGuards ();
	CheckPeerTestRunGeneration ();
	CheckTrustedRouterSnapshot ();
	return 0;
}
