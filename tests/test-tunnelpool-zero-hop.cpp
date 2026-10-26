/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test tunnelpool zero hop.
 *
 * Injects a tunnel test that expired on its last strike and runs
 * TestTunnels(), requiring synchronous replacement publication without
 * self-deadlock and non-empty inbound/outbound pools.
 *
 */

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

#include "Tunnel.h"
#include "TunnelPool.h"
#include "Timestamp.h"

namespace i2p
{
namespace tunnel
{
	struct TunnelPoolTestAccess
	{
		static void AddExpiredTestOnLastStrike (
			TunnelPool & pool, std::shared_ptr<OutboundTunnel> outbound, std::shared_ptr<InboundTunnel> inbound)
		{
			// One more failed test reaches TUNNEL_POOL_TEST_STRIKES.
			for (int i = 0; i + 1 < TUNNEL_POOL_TEST_STRIKES; i++)
			{
				outbound->AddTestStrike ();
				inbound->AddTestStrike ();
			}
			TunnelPool::TunnelTest test;
			test.outbound = outbound;
			test.inbound = inbound;
			test.sentTime = i2p::util::GetMillisecondsSinceEpoch ();
			test.expiration = test.sentTime; // already expired
			std::lock_guard<std::mutex> l (pool.m_TestsMutex);
			pool.m_Tests[0x5A5A5A5AU] = test;
		}

		static void RunTestTunnels (TunnelPool & pool, uint64_t ts) { pool.TestTunnels (ts); }

		static size_t InboundSize (const TunnelPool & pool)
		{
			std::lock_guard<std::mutex> l (pool.m_InboundTunnelsMutex);
			return pool.m_InboundTunnels.size ();
		}

		static size_t OutboundSize (const TunnelPool & pool)
		{
			std::lock_guard<std::mutex> l (pool.m_OutboundTunnelsMutex);
			return pool.m_OutboundTunnels.size ();
		}
	};
}
}

int main ()
{
	using namespace i2p::tunnel;

	auto pool = std::make_shared<TunnelPool> (0, 0, 1, 1, 0, 0, false);

	auto inbound = tunnels.CreateInboundTunnel (nullptr, pool, nullptr);
	pool->TunnelCreated (inbound);
	auto outbound = tunnels.CreateOutboundTunnel (nullptr, pool);
	pool->TunnelCreated (outbound);

	TunnelPoolTestAccess::AddExpiredTestOnLastStrike (*pool, outbound, inbound);

	std::atomic<bool> done { false };
	std::thread worker (
		[&] ()
		{
			TunnelPoolTestAccess::RunTestTunnels (*pool, i2p::util::GetSecondsSinceEpoch ());
			done.store (true, std::memory_order_release);
		});

	const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (5);
	while (!done.load (std::memory_order_acquire) && std::chrono::steady_clock::now () < deadline)
		std::this_thread::sleep_for (std::chrono::milliseconds (1));

	if (!done.load (std::memory_order_acquire))
	{
		std::cerr << "TunnelPool::TestTunnels() deadlocked for a zero-hop pool" << std::endl;
		std::_Exit (2);
	}

	worker.join ();

	if (TunnelPoolTestAccess::InboundSize (*pool) == 0 || TunnelPoolTestAccess::OutboundSize (*pool) == 0)
	{
		std::cerr << "zero-hop replacement tunnels were not published" << std::endl;
		return 1;
	}

	tunnels.DeleteTunnelPool (pool);
	return 0;
}
