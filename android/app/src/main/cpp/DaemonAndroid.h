/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: DaemonAndroid.
 *
 * Declares the native daemon lifecycle mirror, the mutex-backed call guard and
 * the start/stop/data-directory helpers used by the JNI layer. The guard is
 * non-recursive: a JNI entry point holds exactly one instance and internal
 * helpers never nest another.
 *
 */

#ifndef DAEMON_ANDROID_H
#define DAEMON_ANDROID_H

#include "DaemonLifecycle.h"

#include <atomic>
#include <mutex>
#include <string>

namespace i2p
{
namespace android
{
	/**
	 * The one role a process may serve. Android runs the router and the
	 * opt-in SAF storage broker in different processes of the same UID, so
	 * the role cannot be inferred from credentials, arguments or the loaded
	 * library: every process claims exactly one role before it reaches its
	 * first router- or document-capable entry point.
	 *
	 * A claim is process memory only. It never appears on a command line, in
	 * the environment, in a log record or in a persisted file, and it is not
	 * inherited across a deliberate process restart.
	 */
	enum class ProcessRole : int
	{
		None = 0,
		Router = 1,
		StorageBroker = 2
	};

	/**
	 * Fixed, non-secret token reported when a role claim conflicts with the
	 * role already claimed by this process. It carries no path, identity,
	 * grant or credential detail and is safe to display and to log.
	 */
	inline constexpr const char * PROCESS_ROLE_CONFLICT = "process-role-conflict";

	/**
	 * Result token returned by start() when a stop request aborted the start
	 * before the router reached Running. Java maps it to the stopped state
	 * instead of a start failure.
	 */
	inline constexpr const char * START_CANCELED_RESULT = "start-canceled";

	namespace detail
	{
		/**
		 * One process-wide role slot. It is a function-local static so the
		 * header stays ODR-safe and a host regression can include it without
		 * linking the daemon. It is deliberately never reset: a process role
		 * is irreversible, which is what makes the guard effective.
		 */
		inline std::atomic<int> & ProcessRoleSlot () noexcept
		{
			static std::atomic<int> slot { static_cast<int> (ProcessRole::None) };
			return slot;
		}
	}

	/** Role claimed by this process, or None before the first claim. */
	inline ProcessRole CurrentProcessRole () noexcept
	{
		return static_cast<ProcessRole> (detail::ProcessRoleSlot ().load (std::memory_order_acquire));
	}

	/**
	 * Claim `role` exactly once per process.
	 *
	 * @return true when this call claimed the role or this process already
	 *         serves exactly that role; false for a conflicting role or an
	 *         unknown value. A false result is terminal: the caller must not
	 *         start a router and must not install document authority.
	 */
	inline bool ClaimProcessRole (ProcessRole role) noexcept
	{
		if (role != ProcessRole::Router && role != ProcessRole::StorageBroker) return false;
		auto & slot = detail::ProcessRoleSlot ();
		int expected = static_cast<int> (ProcessRole::None);
		if (slot.compare_exchange_strong (
				expected, static_cast<int> (role), std::memory_order_acq_rel, std::memory_order_acquire))
			return true;
		return expected == static_cast<int> (role);
	}

	/** True when this process has already claimed exactly `role`. */
	inline bool HasProcessRole (ProcessRole role) noexcept
	{
		return CurrentProcessRole () == role;
	}

	/**
	 * Request cancellation of an in-progress start transition. It sets one
	 * process-global atomic and returns immediately: the caller (a Java stop
	 * request) may run while start() still owns the native-call guard, so this
	 * function must never take a lock.
	 */
	void RequestStartCancel () noexcept;

	/** True when a start cancellation is pending for the current attempt. */
	bool IsStartCancelRequested () noexcept;

	/**
	 * Serialises every JNI-visible control/status call (start, stop, reload,
	 * status, snapshots, network state and proxy probes) against each other.
	 *
	 * The guard is not recursive: a JNI entry point creates exactly one
	 * instance and calls the internal helpers directly underneath it. Internal
	 * helpers (i2p::android::start/stop, DaemonAndroidImpl::restart) must never
	 * construct another guard.
	 */
	class NativeCallGuard
	{
	public:
		NativeCallGuard ();
		~NativeCallGuard ();

		NativeCallGuard (const NativeCallGuard &) = delete;
		NativeCallGuard & operator= (const NativeCallGuard &) = delete;

		DaemonLifecycle GetLifecycle () const noexcept;
		bool IsRunning () const noexcept { return GetLifecycle () == DaemonLifecycle::Running; }
		bool IsActive () const noexcept
		{
			const auto state = GetLifecycle ();
			return state == DaemonLifecycle::Starting || state == DaemonLifecycle::Running;
		}

	private:
		std::unique_lock<std::mutex> m_Lock;
	};

	/**
	 * Current lifecycle without taking the native-call lock (diagnostics).
	 *
	 * Native mirror of the Java daemon state. It is published before the
	 * corresponding transition, so a status or control call that does not hold
	 * the native-call lock can tell that the router is going away instead of
	 * touching a torn-down RouterContext.
	 *
	 * Java owns the user-visible state machine (DaemonWrapper.State):
	 * DaemonWrapper publishes "stopping" before stopDaemon(), and RouterService
	 * schedules the graceful-stop deadline on its main handler before enqueuing
	 * the actual stop on its single worker. Start still runs on the wrapper's
	 * dedicated start thread so asset copying and native initialisation do not
	 * block status polling. The native side cannot enforce either Java executor,
	 * so it keeps this defensive mirror and serialises the calls it receives;
	 * the guard is defence in depth for callers that bypass the Java contract,
	 * not a substitute for it.
	 *
	 * Because start() holds the guard for the whole native transition, a stop
	 * request cannot be expressed by calling stop(): it would wait for the start
	 * to finish. RequestStartCancel() is therefore the only lock-free signal;
	 * start() observes it during the packaged-asset wait and between
	 * Daemon.init and Daemon.start, rolls the initialized daemon back and
	 * reports START_CANCELED_RESULT. Stop-vs-start ordering is: publish the Java
	 * stop token, set the native cancel token, then take the Java state monitor
	 * and, only for a daemon that already reached Running, call the guarded
	 * native stop.
	 */
	DaemonLifecycle GetDaemonLifecycle () noexcept;

	class DaemonAndroidImpl
	{
	public:
		DaemonAndroidImpl ();
		~DaemonAndroidImpl ();

		/**
		 * @return success
		 */
		bool init (int argc, char * argv[]);
		/** Invoke Daemon.start(); the enclosing lifecycle owner changes state. */
		bool start ();
		void stop ();

		void setDataDir (std::string path);
	};

	/**
	 * Returns "ok" only after initialization and start succeeded; otherwise
	 * returns a bounded human-readable error for Java to display. Callers must
	 * hold a NativeCallGuard.
	 */
	std::string start ();

	/** Publishes Stopping before Daemon.stop(). Callers must hold the guard. */
	void stop ();

	/**
	 * Record the application data directory used by the next start.
	 *
	 * The value is a path inside app-specific storage, already validated by
	 * Java, and is only read by start(); it is guarded by stateMutex and never
	 * crosses a log line or a status record.
	 */
	void SetDataDir (std::string jdataDir);

	/** Copy of the recorded data directory; safe on the UI thread. */
	std::string GetDataDir (void);
}
}

#endif // DAEMON_ANDROID_H
