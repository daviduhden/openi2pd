/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test timestamp offset.
 *
 * Concurrent readers and writers of the global NTP time offset must not race; balanced AdjustTimeOffset calls must cancel and epoch getters must remain valid and monotonic. Intended for TSan.
 *
 */

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

#include "Timestamp.h"

int main ()
{
	using namespace i2p::util;

	const uint64_t before = GetSecondsSinceEpoch ();
	const uint64_t beforeMs = GetMillisecondsSinceEpoch ();

	constexpr int NUM_THREADS = 8;
	constexpr int ITERATIONS = 4000;
	std::atomic<bool> start { false };
	std::atomic<bool> failed { false };
	std::vector<std::thread> threads;

	for (int t = 0; t < NUM_THREADS; t++)
	{
		threads.emplace_back (
			[&] ()
			{
				while (!start.load (std::memory_order_acquire)) std::this_thread::yield ();
				for (int i = 0; i < ITERATIONS; i++)
				{
					AdjustTimeOffset (1);
					auto seconds = GetSecondsSinceEpoch ();
					auto minutes = GetMinutesSinceEpoch ();
					auto hours = GetHoursSinceEpoch ();
					auto millis = GetMillisecondsSinceEpoch ();
					if (seconds == 0 || minutes == 0 || hours == 0 || millis == 0)
						failed.store (true, std::memory_order_relaxed);
					AdjustTimeOffset (-1);
				}
			});
	}

	const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (30);
	start.store (true, std::memory_order_release);
	for (auto & thread : threads) thread.join ();

	if (std::chrono::steady_clock::now () > deadline)
	{
		std::cerr << "timestamp offset test timed out" << std::endl;
		return 2;
	}

	const int64_t drift = static_cast<int64_t> (GetSecondsSinceEpoch ()) - static_cast<int64_t> (before);
	// The balanced adjustments must cancel. Allow a small clock jump while
	// the test runs, but not a leaked accumulation of ITERATIONS increments.
	if (drift < -5 || drift > 5)
	{
		std::cerr << "time offset leaked: drift=" << drift << " seconds" << std::endl;
		return 1;
	}

	if (GetMillisecondsSinceEpoch () < beforeMs)
	{
		std::cerr << "millisecond epoch moved backwards" << std::endl;
		return 1;
	}

	return failed.load (std::memory_order_relaxed) ? 1 : 0;
}
