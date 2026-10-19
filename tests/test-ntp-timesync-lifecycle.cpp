/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test ntp timesync lifecycle.
 *
 * Checks NTPTimeSync start/stop restartability with no configured servers, repeated cycles and destruction while the worker runs, without contacting a time server.
 *
 */

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "Timestamp.h"

namespace i2p
{
namespace util
{
	struct NTPTimeSyncTestAccess
	{
		static void SetServers (NTPTimeSync & sync, std::vector<std::string> servers)
		{
			sync.m_NTPServersList = std::move (servers);
		}
		static void SetInterval (NTPTimeSync & sync, int hours) { sync.m_SyncInterval = hours; }
	};
}
}

int main ()
{
	using namespace i2p::util;

	// No configured servers: Start() is a no-op and Stop() is harmless.
	{
		NTPTimeSync sync;
		NTPTimeSyncTestAccess::SetServers (sync, { });
		sync.Start ();
		sync.Start ();
		sync.Stop ();
		sync.Stop ();
	}

	// Repeated start/stop cycles must restart the io_context.
	NTPTimeSync sync;
	NTPTimeSyncTestAccess::SetServers (sync, { "127.0.0.1" });
	NTPTimeSyncTestAccess::SetInterval (sync, 1);

	for (int round = 0; round < 3; round++)
	{
		sync.Start ();
		sync.Start (); // already running: no-op
		const auto started = std::chrono::steady_clock::now ();
		sync.Stop ();
		sync.Stop (); // idempotent
		const auto elapsed = std::chrono::steady_clock::now () - started;
		if (elapsed > std::chrono::seconds (5))
		{
			std::cerr << "NTPTimeSync::Stop() blocked for "
					  << std::chrono::duration_cast<std::chrono::seconds> (elapsed).count () << " seconds" << std::endl;
			return 1;
		}
	}

	// Destruction while running must join the worker.
	{
		NTPTimeSync running;
		NTPTimeSyncTestAccess::SetServers (running, { "127.0.0.1" });
		NTPTimeSyncTestAccess::SetInterval (running, 1);
		running.Start ();
	}

	return 0;
}
