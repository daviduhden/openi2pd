/*
 * See the LICENSE file at the top of the project tree for copyright and
 * license details.
 *
 * openi2pd: Android daemon lifecycle transition.
 *
 * Provides the owner-side start transition shared by Android JNI and
 * its deterministic native regression. The caller serializes lifecycle
 * operations; this helper also rejects re-entry through the atomic
 * state.
 *
 */

#ifndef I2PD_ANDROID_DAEMON_LIFECYCLE_H
#define I2PD_ANDROID_DAEMON_LIFECYCLE_H

#include <atomic>
#include <exception>
#include <utility>

namespace i2p::android
{
enum class DaemonLifecycle
{
	Stopped,
	Starting,
	Running,
	Stopping
};

enum class DaemonStartResult
{
	Started,
	InitializationFailed,
	StartFailed,
	AlreadyRunning,
	AlreadyStarting,
	Stopping
};

/// Own one start transition after atomically claiming the stopped
/// lifecycle. Initialization failure returns to Stopped without calling
/// stop; the initializer owns rollback of any partially acquired
/// phases. After a successful initialization, a failed or throwing
/// start attempts stop once. Stop exceptions during rollback are
/// suppressed to preserve the failure.
/// @pre The caller holds the native-call serialization guard. The
/// atomic claim rejects re-entry but does not serialize other daemon
/// operations.
/// @param lifecycle Atomic lifecycle state owned by the caller.
/// @param initialize Callable returning whether initialization
/// succeeded.
/// @param start Callable returning whether the daemon started.
/// @param stop Cleanup callable after a successfully initialized start
/// fails.
/// @return The result of the transition or the already-active state.
/// Exceptions from initialization or start propagate after lifecycle
/// reset.
template <typename Initialize, typename Start, typename Stop>
DaemonStartResult StartDaemonLifecycle (
	std::atomic<DaemonLifecycle> & lifecycle, Initialize && initialize, Start && start, Stop && stop)
{
	auto expected = DaemonLifecycle::Stopped;
	if (!lifecycle.compare_exchange_strong (
			expected, DaemonLifecycle::Starting, std::memory_order_acq_rel, std::memory_order_acquire))
	{
		switch (expected)
		{
		case DaemonLifecycle::Running: return DaemonStartResult::AlreadyRunning;
		case DaemonLifecycle::Starting: return DaemonStartResult::AlreadyStarting;
		case DaemonLifecycle::Stopping: return DaemonStartResult::Stopping;
		case DaemonLifecycle::Stopped: break;
		}
		return DaemonStartResult::Stopping;
	}

	bool initialized = false;
	bool rollbackAttempted = false;
	const auto rollback = [&] () noexcept
	{
		if (initialized && !rollbackAttempted)
		{
			rollbackAttempted = true;
			lifecycle.store (DaemonLifecycle::Stopping, std::memory_order_release);
			try
			{
				std::forward<Stop> (stop) ();
			}
			catch (...)
			{
				// Preserve the original initialization/start failure.
			}
		}
		lifecycle.store (DaemonLifecycle::Stopped, std::memory_order_release);
	};

	try
	{
		initialized = std::forward<Initialize> (initialize) ();
		if (!initialized)
		{
			lifecycle.store (DaemonLifecycle::Stopped, std::memory_order_release);
			return DaemonStartResult::InitializationFailed;
		}

		if (!std::forward<Start> (start) ())
		{
			rollback ();
			return DaemonStartResult::StartFailed;
		}
	}
	catch (...)
	{
		rollback ();
		throw;
	}

	lifecycle.store (DaemonLifecycle::Running, std::memory_order_release);
	return DaemonStartResult::Started;
}
}

#endif // I2PD_ANDROID_DAEMON_LIFECYCLE_H
