/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test routercontext static keys.
 *
 * Spawns 16 threads racing NTCP2/SSU2 static-key access, requiring call-once initialization to yield one key object with stable public bytes and no regeneration on later reads. Uses a temporary HOME.
 *
 */

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

#include "FS.h"
#include "RouterContext.h"

int main ()
{
#ifndef _WIN32
	const auto home
		= std::filesystem::temp_directory_path () / ("openi2pd-routercontext-keys-" + std::to_string (::getpid ()));
	std::filesystem::create_directories (home);
	setenv ("HOME", home.c_str (), 1);
	i2p::fs::DetectDataDir (home.string (), false);
	i2p::fs::Init ();
#endif

	i2p::RouterContext router;

	constexpr int NUM_THREADS = 16;
	std::atomic<bool> start { false };
	std::vector<i2p::crypto::X25519Keys *> ntcp2 (NUM_THREADS, nullptr);
	std::vector<i2p::crypto::X25519Keys *> ssu2 (NUM_THREADS, nullptr);
	std::vector<std::thread> threads;

	for (int i = 0; i < NUM_THREADS; i++)
	{
		threads.emplace_back (
			[&, i] ()
			{
				while (!start.load (std::memory_order_acquire)) std::this_thread::yield ();
				ntcp2[i] = &router.GetNTCP2StaticKeys ();
				ssu2[i] = &router.GetSSU2StaticKeys ();
			});
	}

	start.store (true, std::memory_order_release);
	for (auto & thread : threads) thread.join ();

	for (int i = 1; i < NUM_THREADS; i++)
	{
		if (ntcp2[i] != ntcp2[0] || ssu2[i] != ssu2[0])
		{
			std::cerr << "static key object was reset concurrently" << std::endl;
			return 1;
		}
	}

	auto checkPublicKey = [] (const uint8_t * key) -> bool
	{
		if (!key) return false;
		for (int i = 0; i < 32; i++)
			if (key[i] != 0) return true;
		return false;
	};

	if (!checkPublicKey (router.GetNTCP2StaticKeys ().GetPublicKey ())
		|| !checkPublicKey (router.GetSSU2StaticKeys ().GetPublicKey ()))
	{
		std::cerr << "static keys are empty" << std::endl;
		return 1;
	}

	// Repeated access must not regenerate the keys.
	auto * ntcp2Key = &router.GetNTCP2StaticKeys ();
	auto * ssu2Key = &router.GetSSU2StaticKeys ();
	if (ntcp2Key != ntcp2[0] || ssu2Key != ssu2[0])
	{
		std::cerr << "Get*StaticKeys() replaced an existing object" << std::endl;
		return 1;
	}

	return 0;
}
