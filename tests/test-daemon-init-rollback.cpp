/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test daemon initialization rollback.
 *
 * Exercises the same phase ledger used by Daemon_Singleton::init with
 * injected acquisition and cleanup operations.
 *
 */

#include <cassert>
#include <stdexcept>
#include <string>
#include <vector>

#include "daemon/DaemonInit.h"

using i2p::util::DaemonInitPhase;
using i2p::util::DaemonInitTransaction;

int main ()
{
	std::vector<DaemonInitPhase> released;
	int loggingResources = 0;
	int cryptoResources = 0;
	int storageResources = 0;
	int contextResources = 0;

	const auto failedAttempt = [&] (bool throwAfterContext, bool throwDuringCleanup)
	{
		DaemonInitTransaction transaction (
			[&] (DaemonInitPhase phase)
			{
				released.push_back (phase);
				switch (phase)
				{
				case DaemonInitPhase::Logging: --loggingResources; break;
				case DaemonInitPhase::RouterContext: --contextResources; break;
				case DaemonInitPhase::RouterStorage:
					--storageResources;
					if (throwDuringCleanup) throw std::runtime_error ("injected cleanup failure");
					break;
				case DaemonInitPhase::Crypto: --cryptoResources; break;
				}
			});
		transaction.Run (DaemonInitPhase::Logging, [&] { ++loggingResources; });
		transaction.Run (DaemonInitPhase::Crypto, [&] { ++cryptoResources; });
		transaction.Run (DaemonInitPhase::RouterStorage, [&] { ++storageResources; });
		transaction.Run (DaemonInitPhase::RouterContext, [&] { ++contextResources; });
		if (throwAfterContext) throw std::runtime_error ("injected init failure");
		return false;
	};

	const bool reportedFailure = failedAttempt (false, false);
	assert (!reportedFailure);
	assert (loggingResources == 0 && cryptoResources == 0 && storageResources == 0 && contextResources == 0);
	assert (released.size () == 4);
	assert (released[0] == DaemonInitPhase::RouterContext);
	assert (released[1] == DaemonInitPhase::RouterStorage);
	assert (released[2] == DaemonInitPhase::Crypto);
	assert (released[3] == DaemonInitPhase::Logging);

	released.clear ();
	bool originalErrorPreserved = false;
	try
	{
		(void)failedAttempt (true, true);
	}
	catch (const std::runtime_error & error)
	{
		originalErrorPreserved = std::string (error.what ()) == "injected init failure";
	}
	assert (originalErrorPreserved);
	assert (loggingResources == 0 && cryptoResources == 0 && storageResources == 0 && contextResources == 0);
	assert (released.size () == 4);
	assert (released[0] == DaemonInitPhase::RouterContext);
	assert (released[1] == DaemonInitPhase::RouterStorage);
	assert (released[2] == DaemonInitPhase::Crypto);
	assert (released[3] == DaemonInitPhase::Logging);

	// The unsupported isolated-storage path returns false before the router
	// context phase begins; only phases already acquired are unwound.
	released.clear ();
	const auto unsupportedStorageFailure = [&] ()
	{
		DaemonInitTransaction transaction ([&] (DaemonInitPhase phase) { released.push_back (phase); });
		transaction.Run (DaemonInitPhase::Logging, [] { });
		transaction.Run (DaemonInitPhase::Crypto, [] { });
		return transaction.Run (DaemonInitPhase::RouterStorage, [] { return false; });
	};
	const bool unsupportedStorageStarted = unsupportedStorageFailure ();
	assert (!unsupportedStorageStarted);
	assert (released.size () == 3);
	assert (released[0] == DaemonInitPhase::RouterStorage);
	assert (released[1] == DaemonInitPhase::Crypto);
	assert (released[2] == DaemonInitPhase::Logging);

	// A clean retry commits the same acquired phases without invoking rollback.
	released.clear ();
	{
		DaemonInitTransaction transaction ([&] (DaemonInitPhase phase) { released.push_back (phase); });
		transaction.Run (DaemonInitPhase::Logging, [] { });
		transaction.Run (DaemonInitPhase::Crypto, [] { });
		transaction.Run (DaemonInitPhase::RouterStorage, [] { });
		transaction.Run (DaemonInitPhase::RouterContext, [] { });
		transaction.Commit ();
	}
	assert (released.empty ());
	return 0;
}
