/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test tunnels restart.
 *
 * Checks tunnel service restart: Stop() is idempotent, joins both workers and clears runtime maps so Start() can run again without replacing a joinable thread; no transports are started.
 *
 */

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <list>
#include <memory>
#include <thread>
#include <vector>

#include "Tunnel.h"

int main ()
{
	using namespace i2p::tunnel;
	using i2p::I2NPMessage;
	using i2p::I2NPMessageBuffer;
	using i2p::eI2NPData;
	auto makeMessage = [] () -> std::shared_ptr<i2p::I2NPMessage>
	{
		auto msg = std::make_shared<i2p::I2NPMessageBuffer<32>> ();
		msg->FillI2NPMessageHeader (eI2NPData, 1, false); // valid header, safely ignored by the worker
		return msg;
	};

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

	auto beforeStart = makeMessage ();
	if (tunnels.PostTunnelData (beforeStart) || !beforeStart || tunnels.GetQueueSize () != 0)
	{
		std::cerr << "PostTunnelData accepted data before Start()" << std::endl;
		finished.store (true, std::memory_order_release);
		watchdog.join ();
		return 1;
	}
	std::list<std::shared_ptr<I2NPMessage>> beforeStartBatch { makeMessage () };
	if (tunnels.PostTunnelData (beforeStartBatch) || !beforeStartBatch.empty () || tunnels.GetQueueSize () != 0)
	{
		std::cerr << "stopped list post was not rejected and consumed" << std::endl;
		finished.store (true, std::memory_order_release);
		watchdog.join ();
		return 1;
	}

	tunnels.Stop (); // safe before the first Start()
	tunnels.Stop ();

	for (int round = 0; round < 3; round++)
	{
		tunnels.Start ();
		tunnels.Start (); // second Start() must be a no-op
		if (!tunnels.PostTunnelData (makeMessage ()))
		{
			std::cerr << "PostTunnelData rejected data after Start()" << std::endl;
			finished.store (true, std::memory_order_release);
			watchdog.join ();
			return 1;
		}
		std::list<std::shared_ptr<I2NPMessage>> batch { makeMessage (), makeMessage () };
		if (!tunnels.PostTunnelData (batch) || !batch.empty ())
		{
			std::cerr << "running list post was not accepted and consumed" << std::endl;
			finished.store (true, std::memory_order_release);
			watchdog.join ();
			return 1;
		}

		std::atomic<bool> raceStarted { false };
		std::atomic<bool> raceSemanticsOk { true };
		std::vector<std::thread> producers;
		for (int producer = 0; producer < 2; ++producer)
		{
			producers.emplace_back ([&] ()
			{
				while (!raceStarted.load (std::memory_order_acquire)) std::this_thread::yield ();
				for (int post = 0; post < 128; ++post)
				{
					tunnels.PostTunnelData (makeMessage ());
					std::list<std::shared_ptr<I2NPMessage>> queued { makeMessage () };
					tunnels.PostTunnelData (queued);
					if (!queued.empty ()) raceSemanticsOk.store (false, std::memory_order_release);
				}
			});
		}
		raceStarted.store (true, std::memory_order_release);
		std::this_thread::sleep_for (std::chrono::milliseconds (25));
		tunnels.Stop ();
		for (auto & producer : producers) producer.join ();
		std::list<std::shared_ptr<I2NPMessage>> afterStopBatch { makeMessage () };
		if (!raceSemanticsOk.load (std::memory_order_acquire) || tunnels.GetQueueSize () != 0
			|| tunnels.PostTunnelData (makeMessage ()) || tunnels.PostTunnelData (afterStopBatch)
			|| !afterStopBatch.empty ())
		{
			std::cerr << "Stop() retained queued data or admitted a post-stop message" << std::endl;
			finished.store (true, std::memory_order_release);
			watchdog.join ();
			return 1;
		}
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
