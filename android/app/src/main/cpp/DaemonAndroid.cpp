/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: DaemonAndroid.
 *
 * Implements the native daemon lifecycle behind JNI: it waits for the
 * assets.ready marker, initialises and starts i2p::Daemon and returns a
 * bounded error string on failure. A mutex serialises control and status
 * calls, and an atomic lifecycle mirror is published before each transition
 * so a torn-down router is never touched.
 *
 */

#include <atomic>
#include <iostream>
#include <chrono>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>
#include <exception>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/exception_ptr.hpp>
//#include "mainwindow.h"
#include "FS.h"
#include "DaemonAndroid.h"
#include "Daemon.h"

namespace i2p
{
namespace android
{
	namespace
	{
		std::mutex stateMutex;
		std::string dataDir;

		/** Serialises all JNI-visible control and status calls. */
		std::mutex nativeCallMutex;
		std::atomic<DaemonLifecycle> lifecycle { DaemonLifecycle::Stopped };
	}

	NativeCallGuard::NativeCallGuard ()
		: m_Lock (nativeCallMutex)
	{
	}

	NativeCallGuard::~NativeCallGuard () = default;

	DaemonLifecycle NativeCallGuard::GetLifecycle () const noexcept
	{
		return lifecycle.load (std::memory_order_acquire);
	}

	DaemonLifecycle GetDaemonLifecycle () noexcept
	{
		return lifecycle.load (std::memory_order_acquire);
	}

	DaemonAndroidImpl::DaemonAndroidImpl () { }

	DaemonAndroidImpl::~DaemonAndroidImpl () { }

	bool DaemonAndroidImpl::init (int argc, char * argv[])
	{
		return Daemon.init (argc, argv);
	}

	bool DaemonAndroidImpl::start ()
	{
		lifecycle.store (DaemonLifecycle::Starting, std::memory_order_release);
		const bool started = Daemon.start ();
		lifecycle.store (started ? DaemonLifecycle::Running : DaemonLifecycle::Stopped, std::memory_order_release);
		return started;
	}

	void DaemonAndroidImpl::stop ()
	{
		// Publish Stopping before the daemon starts tearing down subsystems.
		lifecycle.store (DaemonLifecycle::Stopping, std::memory_order_release);
		Daemon.stop ();
		lifecycle.store (DaemonLifecycle::Stopped, std::memory_order_release);
	}

	void DaemonAndroidImpl::restart ()
	{
		stop ();
		if (!start ()) stop ();
	}

	void DaemonAndroidImpl::setDataDir (std::string path)
	{
		Daemon.setDataDir (path);
	}

	static DaemonAndroidImpl daemon;
	static char programName[] = "i2pd";
	static char * argv[] = { programName, nullptr };

	namespace
	{
		/**
		 * Tear down a partially started daemon after an exception. The
		 * lifecycle is published as Stopping before Daemon.stop(), exactly
		 * like the regular shutdown path.
		 */
		void StopAfterFailedStart (bool initialized) noexcept
		{
			if (initialized)
			{
				lifecycle.store (DaemonLifecycle::Stopping, std::memory_order_release);
				try
				{
					daemon.stop ();
				}
				catch (...)
				{
					// Preserve the original start error; the JNI boundary
					// still must not see a second exception.
				}
			}
			lifecycle.store (DaemonLifecycle::Stopped, std::memory_order_release);
		}
	}

	/**
	 * returns error details if failed
	 * returns "ok" if daemon initialized and started okay
	 */
	std::string start (/*int argc, char* argv[]*/)
	{
		lifecycle.store (DaemonLifecycle::Starting, std::memory_order_release);
		bool initialized = false;
		bool started = false;
		try
		{
			{
				std::string dataDirCopy;
				{
					std::lock_guard<std::mutex> lock (stateMutex);
					dataDirCopy = dataDir;
				}
				// make sure assets are ready before proceed
				i2p::fs::DetectDataDir (dataDirCopy, false);
				bool assetsReady = false;
				int numAttempts = 0;
				do
				{
					if (i2p::fs::Exists (i2p::fs::DataDirPath ("assets.ready")))
					{
						assetsReady = true;
						break;
					}
					numAttempts++;
					std::this_thread::sleep_for (std::chrono::seconds (1)); // otherwise wait for 1 more second
				} while (numAttempts <= 10); // 10 seconds max
				if (!assetsReady)
				{
					lifecycle.store (DaemonLifecycle::Stopped, std::memory_order_release);
					return "Assets not ready";
				}

				// Set application directory
				daemon.setDataDir (dataDirCopy);

				initialized = daemon.init (1, argv);
				if (!initialized)
				{
					lifecycle.store (DaemonLifecycle::Stopped, std::memory_order_release);
					return "Daemon init failed";
				}

				started = daemon.start ();
				if (!started)
				{
					// A failed start must not leave transport/netDb threads
					// behind for the next Android start attempt.
					StopAfterFailedStart (initialized);
					return "Daemon start failed";
				}
				lifecycle.store (DaemonLifecycle::Running, std::memory_order_release);
			}
		}
		catch (boost::exception & ex)
		{
			if (!started) StopAfterFailedStart (initialized);
			std::stringstream ss;
			ss << boost::diagnostic_information (ex);
			return ss.str ();
		}
		catch (std::exception & ex)
		{
			if (!started) StopAfterFailedStart (initialized);
			std::stringstream ss;
			ss << ex.what ();
			return ss.str ();
		}
		catch (...)
		{
			if (!started) StopAfterFailedStart (initialized);
			return "unknown exception";
		}
		return "ok";
	}

	void stop ()
	{
		// Publish Stopping before touching the daemon so any observer that
		// cannot take nativeCallMutex (for example the Java lifecycle mirror)
		// knows the router is going away. A repeated stop after the daemon is
		// already down is a no-op: Daemon.stop() must not be re-run on a
		// fully torn-down router (the Java graceful-stop timer and the worker
		// can both request a stop).
		const auto previous = lifecycle.exchange (DaemonLifecycle::Stopping, std::memory_order_acq_rel);
		if (previous == DaemonLifecycle::Stopped)
		{
			lifecycle.store (DaemonLifecycle::Stopped, std::memory_order_release);
			return;
		}
		try
		{
			daemon.stop ();
		}
		catch (...)
		{
			lifecycle.store (DaemonLifecycle::Stopped, std::memory_order_release);
			throw;
		}
		lifecycle.store (DaemonLifecycle::Stopped, std::memory_order_release);
	}

	void SetDataDir (std::string jdataDir)
	{
		std::lock_guard<std::mutex> lock (stateMutex);
		dataDir = std::move (jdataDir);
	}

	std::string GetDataDir (void)
	{
		std::lock_guard<std::mutex> lock (stateMutex);
		return dataDir;
	}

}
}
