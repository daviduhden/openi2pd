/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test tunnelpool detach race.
 *
 * Stress test overlapping DetachTunnels() with administrative getters, requiring snapshot iteration to avoid invalidated sets and all tunnels to be released; intended for TSan.
 *
 */

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include "Tunnel.h"
#include "TunnelPool.h"

namespace
{
void AddZeroHopTunnels (const std::shared_ptr<i2p::tunnel::TunnelPool> & pool)
{
	auto inbound = i2p::tunnel::tunnels.CreateInboundTunnel (nullptr, pool, nullptr);
	pool->TunnelCreated (inbound);
	auto outbound = i2p::tunnel::tunnels.CreateOutboundTunnel (nullptr, pool);
	pool->TunnelCreated (outbound);
}
}

int main ()
{
	using namespace i2p::tunnel;

	auto pool = std::make_shared<TunnelPool> (0, 0, 2, 2, 0, 0, false);

	std::atomic<bool> start { false }, stop { false };
	std::atomic<bool> failed { false };

	std::vector<std::thread> readers;
	for (int i = 0; i < 4; i++)
	{
		readers.emplace_back (
			[&] ()
			{
				while (!start.load (std::memory_order_acquire)) std::this_thread::yield ();
				while (!stop.load (std::memory_order_relaxed))
				{
					auto inbound = pool->GetInboundTunnelsList ();
					auto outbound = pool->GetOutboundTunnelsList ();
					for (const auto & tunnel : inbound)
						if (!tunnel) failed.store (true, std::memory_order_relaxed);
					for (const auto & tunnel : outbound)
						if (!tunnel) failed.store (true, std::memory_order_relaxed);
					(void)pool->HasInboundTunnels ();
					(void)pool->HasOutboundTunnels ();
					(void)pool->GetNextInboundTunnel ();
					(void)pool->GetNextOutboundTunnel ();
					(void)pool->GetLowestLatencyInboundTunnel ();
					(void)pool->GetLowestLatencyOutboundTunnel ();
				}
			});
	}

	std::thread writer (
		[&] ()
		{
			while (!start.load (std::memory_order_acquire)) std::this_thread::yield ();
			for (int i = 0; i < 500; i++)
			{
				AddZeroHopTunnels (pool);
				pool->DetachTunnels ();
			}
			stop.store (true, std::memory_order_release);
		});

	start.store (true, std::memory_order_release);

	const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (30);
	while (!stop.load (std::memory_order_acquire) && std::chrono::steady_clock::now () < deadline)
		std::this_thread::sleep_for (std::chrono::milliseconds (1));

	if (!stop.load (std::memory_order_acquire))
	{
		std::cerr << "detach/read stress test timed out" << std::endl;
		std::_Exit (2);
	}

	writer.join ();
	for (auto & reader : readers) reader.join ();

	pool->DetachTunnels ();
	if (pool->HasInboundTunnels () || pool->HasOutboundTunnels ())
	{
		std::cerr << "DetachTunnels() left tunnels behind" << std::endl;
		return 1;
	}

	tunnels.DeleteTunnelPool (pool);
	return failed.load (std::memory_order_relaxed) ? 1 : 0;
}
