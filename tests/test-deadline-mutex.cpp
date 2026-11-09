/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * Exercise portable transaction admission without sockets or workers.
 * Synchronization establishes contention; no scheduling sleeps are used.
 */

#include <cassert>
#include <chrono>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

#include "privsep/DeadlineMutex.h"

namespace
{
using Clock = std::chrono::steady_clock;
using Gate = i2p::privsep::DeadlineMutex;

void TestExpiredAndImmediateAdmission ()
{
	Gate gate;
	std::unique_lock owner (gate);
	// A distinct caller must fail while admission belongs to owner.
	auto refused = std::async (std::launch::async, [&]
		{
			assert (!gate.try_lock ());
			return gate.try_lock_until (Clock::now ());
		});
	assert (!refused.get ());
	owner.unlock ();
	// An expired deadline may still acquire immediately, like a mutex.
	std::unique_lock admitted (gate, std::defer_lock);
	assert (admitted.try_lock_until (Clock::now ()));
}

void TestReleaseWakesContender ()
{
	Gate gate;
	std::unique_lock owner (gate);
	std::promise<void> entered;
	auto ready = entered.get_future ();
	auto contender = std::async (std::launch::async, [&]
		{
			std::unique_lock lock (gate, std::defer_lock);
			entered.set_value ();
			return lock.try_lock_until (
				Clock::now () + std::chrono::seconds (5));
		});
	ready.get ();
	assert (contender.wait_for (std::chrono::seconds (0)) ==
		std::future_status::timeout);
	owner.unlock ();
	assert (contender.get ());
	assert (gate.try_lock ());
	gate.unlock ();
}

void TestMutualExclusion ()
{
	Gate gate;
	unsigned counter = 0;
	{
		std::vector<std::jthread> threads;
		for (unsigned worker = 0; worker < 4; ++worker)
			threads.emplace_back ([&]
				{
					for (unsigned item = 0; item < 1000; ++item)
					{
						std::lock_guard lock (gate);
						++counter;
					}
				});
	}
	assert (counter == 4000);
}
}

int main ()
{
	TestExpiredAndImmediateAdmission ();
	TestReleaseWakesContender ();
	TestMutualExclusion ();
	return 0;
}
