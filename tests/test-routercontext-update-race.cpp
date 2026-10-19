/*
 * Copyright (c) 2026, David Uhden Collado
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Regression test for RouterContext::m_RouterInfo synchronization.
 *
 * It exercises the locking contract between property mutation (SetFamily),
 * property and transport-flag reads (GetFamily, SupportsV4/V6/Mesh,
 * IsUnreachable), RouterInfo snapshots (CopyRouterInfoBuffer) and the
 * signed-rebuild path (UpdateTimestamp -> UpdateRouterInfo).
 *
 * Limitation: the test never initializes a router and therefore has no
 * router keys or data directory. LocalRouterInfo::CreateBuffer() returns early
 * without a public identity, so serialization, signing and the actual file
 * save are not covered here; those remain pending manual CI with a real
 * router. Under a race detector (TSan) the test detects an unlocked
 * m_Properties/snapshot access. In an ordinary build it asserts that the
 * public API stays callable and internally consistent while those operations
 * overlap.
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
		while (!start.load (std::memory_order_acquire))
			std::this_thread::yield ();
	};

	std::thread mutator ([&] ()
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
	std::thread reader ([&] ()
	{
		waitForStart ();
		for (int i = 0; i < NUM_ITERATIONS; i++)
		{
			if (!router.GetFamily ().empty ()) failed.store (true, std::memory_order_relaxed);
			(void) router.SupportsV4 ();
			(void) router.SupportsV6 ();
			(void) router.SupportsMesh ();
			(void) router.IsUnreachable ();
		}
	});
	std::thread snapshotter ([&] ()
	{
		waitForStart ();
		for (int i = 0; i < NUM_ITERATIONS; i++) (void) router.CopyRouterInfoBuffer ();
	});

	start.store (true, std::memory_order_release);
	mutator.join ();
	reader.join ();
	snapshotter.join ();

	return failed.load (std::memory_order_relaxed) ? 1 : 0;
}
