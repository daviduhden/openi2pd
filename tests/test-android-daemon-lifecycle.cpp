/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test Android daemon lifecycle transition.
 *
 * Exercises the production lifecycle owner with injected daemon operations,
 * without starting the router or touching the network.
 *
 */

#include <atomic>
#include <cassert>

#include "android/app/src/main/cpp/DaemonLifecycle.h"

using i2p::android::DaemonLifecycle;
using i2p::android::DaemonStartResult;
using i2p::android::StartDaemonLifecycle;

int main ()
{
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
	return 0;
}
