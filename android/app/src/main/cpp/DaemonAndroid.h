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

#include <mutex>
#include <string>

namespace i2p
{
namespace android
{
	/**
	 * Native mirror of the Java daemon state. It is published before the
	 * corresponding transition, so a status/control call that is not holding
	 * the native-call lock can tell that the router is going away instead of
	 * touching a torn-down RouterContext.
	 *
	 * Java owns the user-visible state machine (DaemonWrapper.State). In
	 * particular DaemonWrapper publishes the "stopping" state before
	 * stopDaemon(), and RouterService enqueues stop/status operations on its
	 * single worker. Start still runs on DaemonWrapper's dedicated start
	 * thread so asset copying and native initialisation do not block status
	 * polling; the native side cannot enforce either Java executor, so it
	 * keeps this defensive mirror and serialises the calls it receives.
	 *
	 * Java now publishes DaemonWrapper.State.stopping before native teardown,
	 * and RouterService schedules the graceful-stop deadline on its main
	 * handler before enqueuing the actual stop on the single-thread service
	 * worker. The native guard remains defence in depth for any caller that
	 * bypasses that path (for example service destruction); it is not a
	 * substitute for the Java lifecycle contract.
	 */
	enum class DaemonLifecycle
	{
		Stopped,
		Starting,
		Running,
		Stopping
	};

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

	/** Current lifecycle without taking the native-call lock (diagnostics). */
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
		bool start ();
		void stop ();
		void restart ();

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

	// set datadir received from jni
	void SetDataDir (std::string jdataDir);
	// get datadir
	std::string GetDataDir (void);
}
}

#endif // DAEMON_ANDROID_H
