/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test Android daemon lifecycle transition.
 *
 * Exercises the production lifecycle owner with injected daemon operations,
 * without starting the router or touching the network. It also pins the
 * process-role contract (one irreversible claim per process) and the
 * start-cancellation ordering that lets a stop abort a start which still owns
 * the native-call guard.
 *
 */

#include <atomic>
#include <cassert>
#include <string_view>

#include "android/app/src/main/cpp/DaemonAndroid.h"
#include "android/app/src/main/cpp/DaemonLifecycle.h"

using i2p::android::ClaimProcessRole;
using i2p::android::CurrentProcessRole;
using i2p::android::DaemonLifecycle;
using i2p::android::DaemonStartResult;
using i2p::android::HasProcessRole;
using i2p::android::PROCESS_ROLE_CONFLICT;
using i2p::android::ProcessRole;
using i2p::android::START_CANCELED_RESULT;
using i2p::android::StartDaemonLifecycle;

/** Process-role contract: one claim per process, irreversible, non-secret. */
static void TestProcessRoles ()
{
	using namespace std::string_view_literals;
	assert (std::string_view (PROCESS_ROLE_CONFLICT) == "process-role-conflict"sv);
	assert (std::string_view (START_CANCELED_RESULT) == "start-canceled"sv);

	// An unclaimed process serves no role and can claim none of the sentinels.
	assert (CurrentProcessRole () == ProcessRole::None);
	assert (!HasProcessRole (ProcessRole::Router));
	assert (!HasProcessRole (ProcessRole::StorageBroker));
	assert (!ClaimProcessRole (ProcessRole::None));
	assert (!ClaimProcessRole (static_cast<ProcessRole> (99)));
	assert (CurrentProcessRole () == ProcessRole::None);

	// The first accepted claim is the process role; a repeat is idempotent and
	// a different role is refused without changing the claimed one.
	assert (ClaimProcessRole (ProcessRole::Router));
	assert (ClaimProcessRole (ProcessRole::Router));
	assert (HasProcessRole (ProcessRole::Router));
	assert (!HasProcessRole (ProcessRole::StorageBroker));
	assert (!ClaimProcessRole (ProcessRole::StorageBroker));
	assert (CurrentProcessRole () == ProcessRole::Router);
}

int main ()
{
	TestProcessRoles ();

	std::atomic<DaemonLifecycle> lifecycle { DaemonLifecycle::Stopped };
	int initializeCalls = 0;
	int startCalls = 0;
	int stopCalls = 0;
	int nestedInitializeCalls = 0;
	int nestedStartCalls = 0;
	int nestedStopCalls = 0;
	DaemonStartResult nestedResult = DaemonStartResult::Started;

	const auto started = StartDaemonLifecycle (
		lifecycle,
		[&] ()
		{
			++initializeCalls;
			assert (lifecycle.load () == DaemonLifecycle::Starting);
			nestedResult = StartDaemonLifecycle (
				lifecycle,
				[&] ()
				{
					++nestedInitializeCalls;
					return true;
				},
				[&] ()
				{
					++nestedStartCalls;
					return true;
				},
				[&] () { ++nestedStopCalls; });
			return true;
		},
		[&] ()
		{
			++startCalls;
			assert (lifecycle.load () == DaemonLifecycle::Starting);
			return true;
		},
		[&] () { ++stopCalls; });
	assert (started == DaemonStartResult::Started);
	assert (nestedResult == DaemonStartResult::AlreadyStarting);
	assert (initializeCalls == 1);
	assert (startCalls == 1);
	assert (stopCalls == 0);
	assert (nestedInitializeCalls == 0);
	assert (nestedStartCalls == 0);
	assert (nestedStopCalls == 0);
	assert (lifecycle.load () == DaemonLifecycle::Running);

	const auto duplicate = StartDaemonLifecycle (
		lifecycle,
		[&] ()
		{
			++initializeCalls;
			return true;
		},
		[&] ()
		{
			++startCalls;
			return true;
		},
		[&] () { ++stopCalls; });
	assert (duplicate == DaemonStartResult::AlreadyRunning);
	assert (initializeCalls == 1);
	assert (startCalls == 1);
	assert (stopCalls == 0);

	lifecycle.store (DaemonLifecycle::Stopped);
	const auto failed = StartDaemonLifecycle (
		lifecycle,
		[&] ()
		{
			++initializeCalls;
			return true;
		},
		[&] ()
		{
			++startCalls;
			assert (lifecycle.load () == DaemonLifecycle::Starting);
			return false;
		},
		[&] ()
		{
			++stopCalls;
			assert (lifecycle.load () == DaemonLifecycle::Stopping);
		});
	assert (failed == DaemonStartResult::StartFailed);
	assert (stopCalls == 1);
	assert (lifecycle.load () == DaemonLifecycle::Stopped);

	const auto initFailed = StartDaemonLifecycle (
		lifecycle,
		[&] ()
		{
			++initializeCalls;
			return false;
		},
		[&] ()
		{
			++startCalls;
			return true;
		},
		[&] () { ++stopCalls; });
	assert (initFailed == DaemonStartResult::InitializationFailed);
	assert (startCalls == 2);
	assert (stopCalls == 1);
	assert (lifecycle.load () == DaemonLifecycle::Stopped);

	bool initializeExceptionObserved = false;
	try
	{
		StartDaemonLifecycle (
			lifecycle, [] () -> bool { throw 17; },
			[&] ()
			{
				++startCalls;
				return true;
			},
			[&] () { ++stopCalls; });
	}
	catch (int error)
	{
		initializeExceptionObserved = error == 17;
	}
	assert (initializeExceptionObserved);
	assert (startCalls == 2);
	assert (stopCalls == 1);
	assert (lifecycle.load () == DaemonLifecycle::Stopped);

	bool startExceptionObserved = false;
	try
	{
		StartDaemonLifecycle (
			lifecycle, [] () { return true; }, [] () -> bool { throw 23; },
			[&] ()
			{
				++stopCalls;
				assert (lifecycle.load () == DaemonLifecycle::Stopping);
				throw 29;
			});
	}
	catch (int error)
	{
		startExceptionObserved = error == 23;
	}
	assert (startExceptionObserved);
	assert (stopCalls == 2);
	assert (lifecycle.load () == DaemonLifecycle::Stopped);

	lifecycle.store (DaemonLifecycle::Stopping);
	const auto stopping = StartDaemonLifecycle (
		lifecycle,
		[&] ()
		{
			++initializeCalls;
			return true;
		},
		[&] ()
		{
			++startCalls;
			return true;
		},
		[&] () { ++stopCalls; });
	assert (stopping == DaemonStartResult::Stopping);
	assert (initializeCalls == 3);
	assert (startCalls == 2);
	assert (stopCalls == 2);
	assert (lifecycle.load () == DaemonLifecycle::Stopping);

	// A stop request that arrives while the packaged assets are still being
	// awaited aborts the transition before the daemon is initialised: the
	// start callback never runs, no rollback stop is needed, the lifecycle is
	// back to Stopped, and the token can be cleared for the next attempt.
	std::atomic<DaemonLifecycle> cancelLifecycle { DaemonLifecycle::Stopped };
	std::atomic<bool> cancelRequested { false };
	int cancelInitializeCalls = 0;
	int cancelStartCalls = 0;
	int cancelStopCalls = 0;

	const auto canceledDuringInitialize = StartDaemonLifecycle (
		cancelLifecycle,
		[&] ()
		{
			++cancelInitializeCalls;
			// Models the production asset-wait slice observing the token.
			cancelRequested.store (true);
			return !cancelRequested.load ();
		},
		[&] ()
		{
			++cancelStartCalls;
			return true;
		},
		[&] () { ++cancelStopCalls; });
	assert (canceledDuringInitialize == DaemonStartResult::InitializationFailed);
	assert (cancelInitializeCalls == 1);
	assert (cancelStartCalls == 0);
	assert (cancelStopCalls == 0);
	assert (cancelLifecycle.load () == DaemonLifecycle::Stopped);

	// The same transition proceeds once the token is cleared, which is the
	// production "token reset" (native start clears it at the next claim).
	cancelRequested.store (false);
	const auto restarted = StartDaemonLifecycle (
		cancelLifecycle,
		[&] ()
		{
			++cancelInitializeCalls;
			return !cancelRequested.load ();
		},
		[&] ()
		{
			++cancelStartCalls;
			return !cancelRequested.load ();
		},
		[&] () { ++cancelStopCalls; });
	assert (restarted == DaemonStartResult::Started);
	assert (cancelInitializeCalls == 2);
	assert (cancelStartCalls == 1);
	assert (cancelStopCalls == 0);
	assert (cancelLifecycle.load () == DaemonLifecycle::Running);

	// A stop request that arrives between initialization and start makes the
	// production start callback return false, so the lifecycle owner rolls the
	// initialized daemon back with exactly one stop and publishes Stopped. The
	// JNI layer reports this as the distinct START_CANCELED_RESULT token.
	cancelLifecycle.store (DaemonLifecycle::Stopped);
	cancelRequested.store (false);
	const auto canceledBeforeStart = StartDaemonLifecycle (
		cancelLifecycle,
		[&] ()
		{
			++cancelInitializeCalls;
			return true;
		},
		[&] ()
		{
			// The production start callback observes the token here and returns
			// false, which makes the lifecycle owner roll back exactly once.
			cancelRequested.store (true);
			return !cancelRequested.load ();
		},
		[&] ()
		{
			++cancelStopCalls;
			assert (cancelLifecycle.load () == DaemonLifecycle::Stopping);
		});
	assert (canceledBeforeStart == DaemonStartResult::StartFailed);
	assert (cancelInitializeCalls == 3);
	assert (cancelStartCalls == 1);
	assert (cancelStopCalls == 1);
	assert (cancelLifecycle.load () == DaemonLifecycle::Stopped);

	// The canceled transition leaves the owner restartable: the next claim
	// succeeds once the request is cleared, and it still needs no rollback.
	cancelRequested.store (false);
	const auto recovered = StartDaemonLifecycle (
		cancelLifecycle,
		[&] ()
		{
			++cancelInitializeCalls;
			return true;
		},
		[&] ()
		{
			++cancelStartCalls;
			return true;
		},
		[&] () { ++cancelStopCalls; });
	assert (recovered == DaemonStartResult::Started);
	assert (cancelStartCalls == 2);
	assert (cancelStopCalls == 1);
	assert (cancelLifecycle.load () == DaemonLifecycle::Running);
	return 0;
}
