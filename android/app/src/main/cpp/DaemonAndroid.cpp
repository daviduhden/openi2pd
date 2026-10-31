/*
 * See the LICENSE file at the top of the project tree for copyright and
 * license details.
 *
 * openi2pd: DaemonAndroid.
 *
 * Implements the native daemon lifecycle behind JNI: it waits for the
 * assets.ready marker, initializes and starts i2p::Daemon and returns a
 * bounded error string on failure. A mutex serializes control and
 * status calls, and an atomic lifecycle mirror is published before each
 * transition so a torn-down router is never touched. A separate
 * lock-free cancellation token aborts a start that currently owns that
 * mutex.
 *
 */

#include <algorithm>
#include <atomic>
#include <iostream>
#include <chrono>
#include <mutex>
#include <sstream>
#include <string_view>
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

		std::string BoundedDiagnostic (std::string_view text, size_t limit = 512)
		{
			std::string out;
			out.reserve (std::min (text.size (), limit));
			for (char ch : text)
			{
				if (out.size () >= limit) break;
				const unsigned char value = static_cast<unsigned char> (ch);
				out.push_back (value < 0x20 || value == 0x7f ? ' ' : ch);
			}
			if (text.size () > limit) out += "...";
			return out;
		}

		/** Serialises all JNI-visible control and status calls. */
		std::mutex nativeCallMutex;
		std::atomic<DaemonLifecycle> lifecycle { DaemonLifecycle::Stopped };

		/**
		 * Start-cancellation token. It is published by RequestStartCancel()
		 * without nativeCallMutex, so a Java stop request can abort a start
		 * that currently owns the guard. It is cleared exactly once, at the
		 * beginning of the next start transition.
		 */
		std::atomic<bool> startCancelRequested { false };
	}

	void RequestStartCancel () noexcept
	{
		startCancelRequested.store (true, std::memory_order_release);
	}

	bool IsStartCancelRequested () noexcept
	{
		return startCancelRequested.load (std::memory_order_acquire);
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
		return Daemon.start ();
	}

	void DaemonAndroidImpl::stop ()
	{
		// Publish Stopping before the daemon starts tearing down subsystems.
		lifecycle.store (DaemonLifecycle::Stopping, std::memory_order_release);
		Daemon.stop ();
		lifecycle.store (DaemonLifecycle::Stopped, std::memory_order_release);
	}

	void DaemonAndroidImpl::setDataDir (std::string path)
	{
		Daemon.setDataDir (path);
	}

	static DaemonAndroidImpl daemon;
	static char programName[] = "i2pd";
	static char * argv[] = { programName, nullptr };

	/**
	 * Run one complete start transition: wait for the packaged assets, set
	 * the application data directory, Daemon.init(), then Daemon.start().
	 *
	 * The caller must already hold a NativeCallGuard. A start-cancellation
	 * token is cleared first, so only a request that arrives after this
	 * point aborts this attempt. Cancellation is observed during the
	 * bounded asset wait and between init and start; an aborted transition
	 * returns START_CANCELED_RESULT and the lifecycle helper rolls the
	 * already initialized daemon back exactly once.
	 *
	 * @return "ok" after initialization and start succeeded, a bounded
	 * diagnostic on failure, or START_CANCELED_RESULT when a stop request
	 * won the race
	 */
	std::string start (/*int argc, char* argv[]*/)
	{
		// A token left behind by an earlier stop belongs to the previous
		// lifecycle. Only a request that arrives after this point aborts this
		// start; the packaged-asset wait and the init/start boundary observe
		// it.
		startCancelRequested.store (false, std::memory_order_release);
		std::string setupFailure;
		bool canceled = false;
		try
		{
			const auto result = StartDaemonLifecycle (
				lifecycle,
				[&] ()
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
						if (IsStartCancelRequested ())
						{
							canceled = true;
							return false;
						}
						if (i2p::fs::Exists (i2p::fs::DataDirPath ("assets.ready")))
						{
							assetsReady = true;
							break;
						}
						numAttempts++;
						// Sleep in bounded slices so a stop request that arrives during
						// the wait is observed within ~100 ms instead of after the whole
						// second.
						for (int slice = 0; slice < 10 && !IsStartCancelRequested (); ++slice)
							std::this_thread::sleep_for (std::chrono::milliseconds (100));
					} while (numAttempts <= 10); // 10 seconds max

					if (IsStartCancelRequested ())
					{
						canceled = true;
						return false;
					}
					if (!assetsReady)
					{
						setupFailure = "Assets not ready";
						return false;
					}

					// Set application directory
					daemon.setDataDir (dataDirCopy);
					if (!daemon.init (1, argv))
					{
						setupFailure = "Daemon init failed";
						return false;
					}
					return true;
				},
				[&] ()
				{
					// Between init and start: an aborted start returns false so the
					// lifecycle owner rolls the initialized daemon back (exactly one
					// stop) before publishing Stopped.
					if (IsStartCancelRequested ())
					{
						canceled = true;
						return false;
					}
					if (!daemon.start ()) return false;
					if (IsStartCancelRequested ())
					{
						canceled = true;
						return false;
					}
					return true;
				},
				[] () { daemon.stop (); });

			// A cancellation owns the result even though the lifecycle helper
			// reports it as an initialization or start failure.
			if (canceled) return START_CANCELED_RESULT;

			switch (result)
			{
			case DaemonStartResult::Started: return "ok";
			case DaemonStartResult::InitializationFailed:
			case DaemonStartResult::StartFailed: return setupFailure.empty () ? "Daemon start failed" : setupFailure;
			case DaemonStartResult::AlreadyRunning: return "Daemon already running";
			case DaemonStartResult::AlreadyStarting: return "Daemon start already in progress";
			case DaemonStartResult::Stopping: return "Daemon stopping";
			}
			return "unknown daemon lifecycle state";
		}
		catch (boost::exception & ex)
		{
			return BoundedDiagnostic (boost::diagnostic_information (ex));
		}
		catch (std::exception & ex)
		{
			return BoundedDiagnostic (ex.what ());
		}
		catch (...)
		{
			return "unknown exception";
		}
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
