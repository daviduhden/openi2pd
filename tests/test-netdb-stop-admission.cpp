/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: netDb stopped-message admission regression.
 *
 */

#include <atomic>
#include <cassert>
#include <memory>
#include <thread>
#include <vector>

#include "I2NPProtocol.h"
#include "NetDb.hpp"

namespace i2p::data
{
	struct NetDbLifecycleTestAccess
	{
		static void OpenAdmission (NetDb & database)
		{
			std::lock_guard lock (database.m_QueueAdmissionMutex);
			database.m_AcceptMessages = true;
		}
		static bool LookupThrottled (NetDb & database, const IdentHash & hash)
		{
			return database.ShouldThrottleLookup (hash, 1);
		}
		static bool FloodThrottled (NetDb & database, const IdentHash & hash)
		{
			return database.ShouldThrottleFlood (hash);
		}
	};
}

int main ()
{
	// The fixture opens admission with a paused worker: real Post/Stop code is
	// exercised without starting storage, router context, transports or reseed.
	using Access = i2p::data::NetDbLifecycleTestAccess;
	i2p::data::NetDb database;
	auto message = i2p::NewI2NPMessage ();
	database.PostI2NPMsg (message);
	assert (message.use_count () == 1);

	i2p::data::IdentHash hash;
	hash.Fill (0x42);
	for (int i = 0; i < 14; ++i) assert (!Access::LookupThrottled (database, hash));
	assert (Access::LookupThrottled (database, hash));
	for (int i = 0; i < 3; ++i) assert (!Access::FloodThrottled (database, hash));
	assert (Access::FloodThrottled (database, hash));

	for (int round = 0; round < 4; ++round)
	{
		Access::OpenAdmission (database);
		database.PostI2NPMsg (message);
		assert (message.use_count () == 2);
		std::atomic<bool> ready { false };
		std::vector<std::jthread> producers;
		for (int n = 0; n < 4; ++n)
			producers.emplace_back ([&]
				{
					while (!ready.load (std::memory_order_acquire)) std::this_thread::yield ();
					for (int i = 0; i < 1000; ++i) database.PostI2NPMsg (message);
				});
		ready.store (true, std::memory_order_release);
		database.Stop ();
		producers.clear ();
		// Neither an accepted backlog nor a post after the drain may survive.
		database.PostI2NPMsg (message);
		assert (message.use_count () == 1);
	}
	assert (!Access::LookupThrottled (database, hash));
	assert (!Access::FloodThrottled (database, hash));
	return 0;
}
