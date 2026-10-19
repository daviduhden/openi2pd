/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test routercontext service lifecycle.
 *
 * Overlaps SubmitECIESx25519Key() and uptime reads with repeated idempotent Start()/Stop() cycles, requiring posts to observe a live or null service and never a reset pointer.
 *
 */

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>

#include "RouterContext.h"

int main ()
{
	i2p::RouterContext router;
	std::atomic<bool> stop { false };
	std::atomic<bool> finished { false };

	std::thread watchdog (
		[&] ()
		{
			const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (30);
			while (!finished.load (std::memory_order_acquire) && std::chrono::steady_clock::now () < deadline)
				std::this_thread::sleep_for (std::chrono::milliseconds (10));
			if (!finished.load (std::memory_order_acquire))
			{
				std::cerr << "RouterContext service lifecycle timed out" << std::endl;
				std::_Exit (2);
			}
		});

	std::thread poster (
		[&] ()
		{
			uint8_t key[32];
			std::memset (key, 0x42, sizeof (key));
			uint64_t tag = 1;
			while (!stop.load (std::memory_order_relaxed))
			{
				router.SubmitECIESx25519Key (key, tag++);
				(void)router.GetUptime ();
				std::this_thread::yield ();
			}
		});

	for (int round = 0; round < 8; round++)
	{
		router.Start ();
		router.Start (); // idempotent
		std::this_thread::sleep_for (std::chrono::milliseconds (5));
		router.Stop ();
		router.Stop (); // idempotent
	}

	stop.store (true, std::memory_order_release);
	poster.join ();

	// Final Start()/Stop() after the poster has finished.
	router.Start ();
	router.Stop ();

	finished.store (true, std::memory_order_release);
	watchdog.join ();
	return 0;
}
