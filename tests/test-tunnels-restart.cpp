/*
 * Copyright (c) 2026, David Uhden Collado
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Restart contract for i2p::tunnel::tunnels.
 *
 * Stop() must be idempotent, must join both workers, and must clear runtime
 * tunnel maps so Start() can be called again without replacing a joinable
 * thread. No transports are started, so this covers lifecycle state only,
 * not traffic through a restarted tunnel.
 */
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

#include "Tunnel.h"

int main ()
{
	using namespace i2p::tunnel;

	std::atomic<bool> finished { false };
	std::thread watchdog (
		[&] ()
		{
			const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (30);
			while (!finished.load (std::memory_order_acquire) && std::chrono::steady_clock::now () < deadline)
				std::this_thread::sleep_for (std::chrono::milliseconds (10));
			if (!finished.load (std::memory_order_acquire))
			{
				std::cerr << "tunnel restart timed out" << std::endl;
				std::_Exit (2);
			}
		});

	tunnels.Stop (); // safe before the first Start()
	tunnels.Stop ();

	for (int round = 0; round < 3; round++)
	{
		tunnels.Start ();
		tunnels.Start (); // second Start() must be a no-op
		std::this_thread::sleep_for (std::chrono::milliseconds (25));
		tunnels.Stop ();
		tunnels.Stop (); // idempotent

		if (tunnels.CountInboundTunnels () != 0 || tunnels.CountOutboundTunnels () != 0
			|| tunnels.CountTransitTunnels () != 0)
		{
			std::cerr << "Stop() left runtime tunnels in round " << round << std::endl;
			finished.store (true, std::memory_order_release);
			watchdog.join ();
			return 1;
		}
		if (tunnels.GetExploratoryPool ())
		{
			std::cerr << "unexpected exploratory pool without transports" << std::endl;
			finished.store (true, std::memory_order_release);
			watchdog.join ();
			return 1;
		}
	}

	tunnels.Start ();
	tunnels.Stop ();

	finished.store (true, std::memory_order_release);
	watchdog.join ();
	return 0;
}
