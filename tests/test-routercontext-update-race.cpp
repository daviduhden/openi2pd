/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test routercontext update race.
 *
 * Overlaps SetFamily/UpdateTimestamp with property reads and RouterInfo snapshots to exercise RouterContext locking; the test has no router identity, so signing and file persistence remain uncovered. Intended for TSan.
 *
 */

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include "RouterContext.h"

int main ()
{
	i2p::RouterContext router;
	std::atomic<bool> start (false), failed (false);
	constexpr int NUM_ITERATIONS = 256;

	auto waitForStart = [&start] ()
	{
		while (!start.load (std::memory_order_acquire)) std::this_thread::yield ();
	};

	std::thread mutator (
		[&] ()
		{
			waitForStart ();
			for (int i = 0; i < NUM_ITERATIONS; i++)
			{
				router.SetFamily ("");
				// If the timestamp advances, UpdateTimestamp() takes the locked
				// RouterInfo rebuild path. 1<<62 stays ahead of any real epoch so
				// every iteration enters it.
				router.UpdateTimestamp (1ULL << 62);
			}
		});
	std::thread reader (
		[&] ()
		{
			waitForStart ();
			for (int i = 0; i < NUM_ITERATIONS; i++)
			{
				if (!router.GetFamily ().empty ()) failed.store (true, std::memory_order_relaxed);
				(void)router.SupportsV4 ();
				(void)router.SupportsV6 ();
				(void)router.SupportsMesh ();
				(void)router.IsUnreachable ();
			}
		});
	std::thread snapshotter (
		[&] ()
		{
			waitForStart ();
			for (int i = 0; i < NUM_ITERATIONS; i++) (void)router.CopyRouterInfoBuffer ();
		});

	start.store (true, std::memory_order_release);
	mutator.join ();
	reader.join ();
	snapshotter.join ();

	return failed.load (std::memory_order_relaxed) ? 1 : 0;
}
