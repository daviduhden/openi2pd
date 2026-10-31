/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Daemon.
 *
 * Implements router initialisation, subsystem sequencing, shutdown and
 * the optional privilege-separated log path. Startup parses
 * configuration, installs the OpenBSD startup pledge and starts NetDB,
 * transports, tunnels, clients, TUI IPC, I2PControl, UPnP and NTP;
 * shutdown stops observers before the state they read. A monitor thread
 * replaces a dead log supervisor with bounded backoff where exec
 * remains available, and logging never falls back to a local file.
 *
 */

#include <thread>
#include <memory>
#include <regex>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include "Expected.h"
#include <iostream>
#include <signal.h>
#ifndef _WIN32
#include <pthread.h>
#include <unistd.h>
#endif

#ifdef __OpenBSD__
#include <unistd.h>
#include <limits.h>
#include <stdexcept>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <sstream>
#endif

#include "Daemon.h"
#include "DaemonInit.h"

#include "Config.h"
#include "Log.h"
#include "FS.h"
#include "CPU.h"
#include "Base.h"
#include "util.h"
#include "version.h"
#include "Transports.h"
#include "RouterInfo.h"
#include "RouterContext.h"
#include "PersistentStorage.h"
#include "Tunnel.h"
#include "HTTP.h"
#include "NetDb.hpp"
#include "Blocklist.h"
#include "Garlic.h"
#include "Streaming.h"
#include "Destination.h"
#include "I2PControl.h"
#if !defined(__ANDROID__)
#include "TuiServer.h"
#endif
#include "TuiServerCommon.h"
#if !defined(_WIN32) && !defined(I2PD_NO_PRIVSEP)
#include <filesystem>
#include <fstream>
#include "privsep/Credentials.h"
#include "privsep/LogWorker.h"
#include "privsep/OpenBSDSandbox.h"
#include "privsep/Worker.h"
#include "privsep/RouterStorage.h"
#include "privsep/DataStorage.h"
#include "PersistentStorage.h"
#endif
#include "ClientContext.h"
#include "Crypto.h"
#include "UPnP.h"
#include "Timestamp.h"
#include "I18N.h"

namespace i2p
{
namespace util
{
#ifdef __OpenBSD__
	// Keep paths stable across DaemonUnix::start()'s chdir(). The leaf
	// need not exist yet (log files, for example), so realpath() is
	// unsuitable.
	static std::string AbsolutePath (const std::string & path)
	{
		// reseed.file may be an HTTPS URL, not a filesystem path.
		if (path.empty () || path.front () == '/' || path.find ("://") != std::string::npos) return path;
		char cwd[PATH_MAX];
		if (!getcwd (cwd, sizeof (cwd))) throw std::runtime_error ("Daemon: Can't resolve working directory");
		return std::string (cwd) + "/" + path;
	}
#endif

	class Daemon_Singleton::Daemon_Singleton_Private
	{
	public:
		Daemon_Singleton_Private () { };
		~Daemon_Singleton_Private ()
		{
#if !defined(_WIN32) && !defined(I2PD_NO_PRIVSEP)
			// A joinable std::thread would terminate the process on destruction.
			logSupervisorStop.store (true, std::memory_order_relaxed);
			if (logSupervisor.joinable ()) logSupervisor.join ();
#endif
		};

#if !defined(__ANDROID__)
		std::unique_ptr<i2p::tui::Server> tuiServer;
#endif
#if !defined(_WIN32) && !defined(I2PD_NO_PRIVSEP)
		std::shared_ptr<i2p::privsep::RouterStorageClient> routerStorage;
		std::shared_ptr<i2p::privsep::DataStorageClient> dataStorage;
		std::unique_ptr<i2p::privsep::LogSink> logSink;
		std::unique_ptr<i2p::privsep::ChildProcess> logWorker;
		std::thread logSupervisor;
		std::atomic<bool> logSupervisorStop { false };
		std::chrono::steady_clock::time_point logWorkerStarted;
		bool privsepLog = false;
		bool privsepArtifacts = false;
		bool privsepSyslog = false;
		bool privsepCanRestart = true;
		std::string privsepLogPath;
#endif
		std::unique_ptr<i2p::client::I2PControlService> m_I2PControlService;
		std::unique_ptr<i2p::transport::UPnP> UPnP;
		std::unique_ptr<i2p::util::NTPTimeSync> m_NTPSync;
	};

	namespace
	{
		constexpr std::array<std::string_view, 6> ADMIN_LOG_LEVEL_NAMES
			= { "none", "critical", "error", "warn", "info", "debug" };

		// Bounded view of a NUL-terminated fixed buffer. Unlike string_view's
		// char* constructor this never scans past the array, so a malformed
		// embedder submission cannot over-read.
		std::string_view BoundedAdminText (const char * buffer, size_t size)
		{
			const auto end = std::find (buffer, buffer + size, '\0');
			return std::string_view (buffer, static_cast<size_t> (end - buffer));
		}

		void SetAdminResultMessage (i2p::tui::AdminCommandResult & result, const char * message)
		{
			const int written = std::snprintf (result.message, sizeof (result.message), "%s", message ? message : "");
			result.messageLength
				= written > 0 ? std::min<size_t> (static_cast<size_t> (written), sizeof (result.message) - 1) : 0;
		}

		/**
		 * Execute one typed administrator command.
		 *
		 * This runs on the daemon-owned administrative thread started by
		 * Daemon_Singleton::start(), never on the local-IPC thread. Reseed is
		 * already single-flight in NetDb; ReloadConfig is the same path
		 * SIGHUP uses; bandwidth, share and transit only touch atomic
		 * RouterContext state; the log level only touches the logger's atomic
		 * threshold.
		 */
		void ExecuteAdminCommand (const i2p::tui::AdminCommand & command, i2p::tui::AdminCommandResult & result)
		{
			using i2p::tui::AdminCommandKind;
			using i2p::tui::AdminOutcome;
			using i2p::tui::AdminResultCode;

			result = i2p::tui::AdminCommandResult { };
			result.code = AdminResultCode::Ok;
			switch (command.kind)
			{
			case AdminCommandKind::Reseed:
			{
				// Never blocks the administrative thread. RequestReseed() starts
				// the owned, cancellable NetDb job and returns immediately, so
				// a slow mirror cannot stall the local-IPC thread or delay
				// shutdown (NetDb::Stop() cancels and joins the job).
				if (i2p::data::netdb.RequestReseed ())
				{
					result.outcome = AdminOutcome::Started;
					result.value = 1;
					SetAdminResultMessage (result, "reseed started");
				}
				else if (i2p::data::netdb.IsReseeding ())
				{
					result.outcome = AdminOutcome::AlreadyRunning;
					result.value = 1;
					SetAdminResultMessage (result, "reseed already in progress");
				}
				else
				{
					result.code = AdminResultCode::Unavailable;
					result.outcome = AdminOutcome::Disabled;
					result.value = 0;
					SetAdminResultMessage (result, "reseed is disabled");
				}
				break;
			}
			case AdminCommandKind::TunnelStart:
			case AdminCommandKind::TunnelStop:
			{
				// One configured client or server tunnel, resolved by its
				// unique service name. The name is already bounded by the
				// parser and by the fixed command buffer.
				const auto name = BoundedAdminText (command.name, sizeof (command.name));
				const bool start = command.kind == AdminCommandKind::TunnelStart;
				if (name.empty ())
				{
					result.code = AdminResultCode::Invalid;
					result.outcome = AdminOutcome::Unknown;
					SetAdminResultMessage (result, "invalid tunnel service name");
					break;
				}
				bool found = false;
				bool running = false;
				for (const auto & service : i2p::client::context.GetTunnelServices ())
					if (service.name == name)
					{
						found = true;
						running = service.running;
						break;
					}
				if (!found)
				{
					result.code = AdminResultCode::Failed;
					result.outcome = AdminOutcome::Unknown;
					result.value = 0;
					SetAdminResultMessage (result, "unknown tunnel service name");
					break;
				}
				if (start && running)
				{
					result.outcome = AdminOutcome::AlreadyRunning;
					result.enabled = true;
					result.value = 1;
					SetAdminResultMessage (result, "tunnel is already running");
					break;
				}
				if (!start && !running)
				{
					result.outcome = AdminOutcome::AlreadyStopped;
					result.enabled = false;
					result.value = 0;
					SetAdminResultMessage (result, "tunnel is already stopped");
					break;
				}
				std::string error;
				const bool changed = start ? i2p::client::context.StartTunnel (name, error)
										   : i2p::client::context.StopTunnel (name, error);
				if (!changed)
				{
					result.code = AdminResultCode::Failed;
					result.enabled = running;
					result.value = running ? 1 : 0;
					SetAdminResultMessage (result, error.empty () ? "tunnel command failed" : error.c_str ());
					break;
				}
				bool nowRunning = start;
				for (const auto & service : i2p::client::context.GetTunnelServices ())
					if (service.name == name)
					{
						nowRunning = service.running;
						break;
					}
				result.enabled = nowRunning;
				result.value = nowRunning ? 1 : 0;
				result.outcome = start ? AdminOutcome::Started : AdminOutcome::Stopped;
				SetAdminResultMessage (result, start ? "tunnel started" : "tunnel stopped");
				break;
			}
			case AdminCommandKind::ShutdownGraceful:
				if (!Daemon.BeginGracefulShutdown ())
				{
					result.code = AdminResultCode::Failed;
					SetAdminResultMessage (result, "graceful shutdown is not supported");
				}
				else
				{
					result.value = Daemon.GetGracefulShutdownInterval ();
					SetAdminResultMessage (result, "graceful shutdown started");
				}
				break;
			case AdminCommandKind::ShutdownImmediate:
				Daemon.RequestImmediateShutdown ();
				SetAdminResultMessage (result, "immediate shutdown requested");
				break;
			case AdminCommandKind::ShutdownCancel:
				if (!Daemon.CancelGracefulShutdown ())
				{
					result.code = AdminResultCode::Failed;
					SetAdminResultMessage (result, "graceful shutdown is not supported");
				}
				else
					SetAdminResultMessage (result, "graceful shutdown cancelled");
				break;
			case AdminCommandKind::ReloadTunnels:
				i2p::client::context.ReloadConfig ();
				SetAdminResultMessage (result, "tunnel configuration reloaded");
				break;
			case AdminCommandKind::Bandwidth:
			{
				const auto token = static_cast<char> (command.value);
				if (token != 'K' && token != 'L' && token != 'M' && token != 'N' && token != 'O' && token != 'P'
					&& token != 'X')
				{
					result.code = AdminResultCode::Invalid;
					SetAdminResultMessage (result, "invalid bandwidth class");
					break;
				}
				i2p::context.SetBandwidth (token);
				result.value = static_cast<int32_t> (i2p::context.GetBandwidthLimit ());
				SetAdminResultMessage (result, "bandwidth class applied");
				break;
			}
			case AdminCommandKind::Share:
				if (command.value < 0 || command.value > 100)
				{
					result.code = AdminResultCode::Invalid;
					SetAdminResultMessage (result, "share percentage must be 0..100");
					break;
				}
				i2p::context.SetShareRatio (command.value);
				result.value = command.value;
				SetAdminResultMessage (result, "share ratio applied");
				break;
			case AdminCommandKind::Transit:
				i2p::context.SetAcceptsTunnels (command.enabled);
				result.enabled = command.enabled;
				SetAdminResultMessage (
					result, command.enabled ? "transit tunnels enabled" : "transit tunnels disabled");
				break;
			case AdminCommandKind::Online:
				i2p::transport::transports.SetOnline (command.enabled);
				result.enabled = i2p::transport::transports.IsOnline ();
				SetAdminResultMessage (
					result, result.enabled ? "transports online" : "transports offline; the router keeps running");
				break;
			case AdminCommandKind::LogReopen:
				i2p::log::Logger ().Reopen ();
				result.enabled = true;
				SetAdminResultMessage (result, "log file reopened");
				break;
			case AdminCommandKind::LogLevel:
				if (command.value < 0 || command.value >= static_cast<int32_t> (ADMIN_LOG_LEVEL_NAMES.size ()))
				{
					result.code = AdminResultCode::Invalid;
					SetAdminResultMessage (result, "invalid daemon log level");
					break;
				}
				i2p::log::Logger ().SetLogLevel (
					std::string (ADMIN_LOG_LEVEL_NAMES[static_cast<size_t> (command.value)]));
				result.value = command.value;
				SetAdminResultMessage (result, "daemon log level applied");
				break;
			case AdminCommandKind::AddressAdd:
			{
				// ADDRESS-ADD and ADDRESS-REPLACE share this path. Only the
				// explicit replace verb re-points an existing name; add reports
				// `exists` so a client can offer the replacement instead of
				// silently overwriting.
				auto & book = i2p::client::context.GetAddressBook ();
				const auto outcome = book.AddAddressChecked (BoundedAdminText (command.name, sizeof (command.name)),
					BoundedAdminText (command.destination, sizeof (command.destination)), command.enabled);
				switch (outcome)
				{
				case i2p::client::AddressAddResult::Ok:
					result.code = AdminResultCode::Ok;
					SetAdminResultMessage (result, command.enabled ? "address replaced" : "address added");
					break;
				case i2p::client::AddressAddResult::InvalidName:
					result.code = AdminResultCode::Invalid;
					SetAdminResultMessage (result, "invalid address name");
					break;
				case i2p::client::AddressAddResult::InvalidDestination:
					result.code = AdminResultCode::Invalid;
					SetAdminResultMessage (result, "invalid base64 destination");
					break;
				case i2p::client::AddressAddResult::Exists:
					result.code = AdminResultCode::Failed;
					result.outcome = AdminOutcome::Exists;
					SetAdminResultMessage (result, "address already exists; use replace to overwrite");
					break;
				case i2p::client::AddressAddResult::Unavailable:
					result.code = AdminResultCode::Unavailable;
					SetAdminResultMessage (result, "address book storage is unavailable");
					break;
				}
				break;
			}
			case AdminCommandKind::AddressRemove:
			case AdminCommandKind::AddressBlock:
			case AdminCommandKind::AddressUnblock:
			{
				auto & book = i2p::client::context.GetAddressBook ();
				i2p::client::AddressBookMutationResult outcome;
				const char * success = "address updated";
				switch (command.kind)
				{
				case AdminCommandKind::AddressRemove:
					outcome = book.RemoveAddress (command.name);
					success = "address removed";
					break;
				case AdminCommandKind::AddressBlock:
					outcome = book.BlockAddress (command.name);
					success = "address blocked";
					break;
				default:
					outcome = book.UnblockAddress (command.name);
					success = "address unblocked";
					break;
				}
				switch (outcome)
				{
				case i2p::client::AddressBookMutationResult::Ok:
					result.code = AdminResultCode::Ok;
					SetAdminResultMessage (result, success);
					break;
				case i2p::client::AddressBookMutationResult::InvalidName:
					result.code = AdminResultCode::Invalid;
					SetAdminResultMessage (result, "invalid address name");
					break;
				case i2p::client::AddressBookMutationResult::InvalidDestination:
					result.code = AdminResultCode::Invalid;
					SetAdminResultMessage (result, "invalid base64 destination");
					break;
				case i2p::client::AddressBookMutationResult::Exists:
					result.code = AdminResultCode::Failed;
					SetAdminResultMessage (result, "address already exists with another destination");
					break;
				case i2p::client::AddressBookMutationResult::NotFound:
					result.code = AdminResultCode::Failed;
					SetAdminResultMessage (result, "address not found");
					break;
				case i2p::client::AddressBookMutationResult::Unavailable:
					result.code = AdminResultCode::Unavailable;
					SetAdminResultMessage (result, "address book storage is unavailable");
					break;
				}
				break;
			}
			case AdminCommandKind::NetDbBan:
			case AdminCommandKind::NetDbUnban:
			{
				const auto text = BoundedAdminText (command.name, sizeof (command.name));
				i2p::data::IdentHash hash { };
				if (text.empty () || hash.FromBase64 (text) != i2p::data::IdentHash::len)
				{
					result.code = AdminResultCode::Invalid;
					SetAdminResultMessage (result, "invalid base64 router hash");
					break;
				}
				if (command.kind == AdminCommandKind::NetDbBan)
				{
					if (!i2p::data::AddRuntimeRouterBan (hash))
					{
						result.code = AdminResultCode::Failed;
						SetAdminResultMessage (result, "router ban list is full");
						break;
					}
					SetAdminResultMessage (result, "router banned until restart");
				}
				else
				{
					if (!i2p::data::RemoveRuntimeRouterBan (hash))
					{
						result.code = AdminResultCode::Failed;
						SetAdminResultMessage (result, "router is not in the runtime ban list");
						break;
					}
					SetAdminResultMessage (result, "router unbanned");
				}
				result.value = static_cast<int32_t> (i2p::data::GetRuntimeRouterBanCount ());
				break;
			}
			case AdminCommandKind::AddressSubAdd:
			case AdminCommandKind::AddressSubRemove:
			{
				const auto url = BoundedAdminText (command.destination, sizeof (command.destination));
				if (url.empty ())
				{
					result.code = AdminResultCode::Invalid;
					SetAdminResultMessage (result, "invalid subscription URL");
					break;
				}
				std::string error;
				auto & book = i2p::client::context.GetAddressBook ();
				const bool ok = command.kind == AdminCommandKind::AddressSubAdd ? book.AddSubscription (url, error)
																				: book.RemoveSubscription (url, error);
				if (!ok)
				{
					result.code = AdminResultCode::Failed;
					SetAdminResultMessage (result, error.empty () ? "subscription update failed" : error.c_str ());
					break;
				}
				result.value
					= static_cast<int32_t> (book.GetSubscriptions (i2p::client::MAX_SUBSCRIPTION_URLS).size ());
				SetAdminResultMessage (result,
					command.kind == AdminCommandKind::AddressSubAdd ? "subscription added" : "subscription removed");
				break;
			}
			default:
				result.code = AdminResultCode::Invalid;
				SetAdminResultMessage (result, "unknown admin command");
				break;
			}
		}
	}

#if !defined(_WIN32) && !defined(I2PD_NO_PRIVSEP)
	namespace
	{
		// Bounded policy for replacing a dead supervisor process. The
		// supervisor owns the log worker and its restart loop; network and
		// storage workers are still unimplemented.
		constexpr auto LOG_SUPERVISOR_TICK = std::chrono::milliseconds (1000);
		constexpr auto LOG_RESTART_INITIAL_BACKOFF = std::chrono::milliseconds (500);
		constexpr auto LOG_RESTART_MAX_BACKOFF = std::chrono::milliseconds (30000);
		constexpr auto LOG_WORKER_STABLE_PERIOD = std::chrono::milliseconds (60000);
		constexpr unsigned LOG_RESTART_MAX_ATTEMPTS = 8;

		/**
		 * Sleep in bounded slices so shutdown never waits for a full
		 * backoff. */
		bool SleepUnlessStopping (const std::atomic<bool> & stop, std::chrono::milliseconds total)
		{
			const auto deadline = std::chrono::steady_clock::now () + total;
			while (!stop.load (std::memory_order_relaxed))
			{
				const auto left = std::chrono::duration_cast<std::chrono::milliseconds> (
					deadline - std::chrono::steady_clock::now ());
				if (left <= std::chrono::milliseconds (0)) return true;
				std::this_thread::sleep_for (std::min (left, std::chrono::milliseconds (100)));
			}
			return false;
		}

	}
#endif

	/**
	 * Spawn, configure and install one worker. Any previous worker is
	 * stopped and reaped first, so at most one live child exists at any
	 * time even when a caller skips the supervisor's reap step.
	 */
	bool Daemon_Singleton::PreparePrivsepWorkers ()
	{
#if defined(I2PD_NO_PRIVSEP)
		return true;
#elif !defined(_WIN32)
		if (!d->privsepLog || d->logWorker) return true;
		return StartPrivsepLogWorker ();
#else
		return true;
#endif
	}

	void Daemon_Singleton::StopPrivsepWorkers ()
	{
#if !defined(_WIN32) && !defined(I2PD_NO_PRIVSEP)
		if (d->routerStorage) d->routerStorage->Stop ();
		if (d->privsepLog) StopPrivsepLogWorker ();
#endif
	}

#if !defined(_WIN32) && !defined(I2PD_NO_PRIVSEP)
	bool Daemon_Singleton::StartPrivsepLogWorker ()
	{
		// Enforce single ownership even if a future caller forgets to reap
		// the previous worker first.
		if (d->logWorker) d->logWorker->Stop (500);
		auto spawned = i2p::privsep::SpawnSupervisor ();
		if (!spawned)
		{
			std::cerr << "Daemon: cannot spawn privilege-separated supervisor: " << spawned.error () << std::endl;
			return false;
		}
		auto worker = std::make_unique<i2p::privsep::ChildProcess> (std::move (*spawned));
		auto channel = std::move (worker->ChannelFd ());
		i2p::expected<void, std::string> configured;
		if (!d->logSink)
		{
			d->logSink = std::make_unique<i2p::privsep::LogSink> (std::move (channel));
			configured = d->logSink->ConfigureProxy (d->privsepSyslog, d->privsepLogPath, "open-i2pd");
			if (!configured)
			{
				std::cerr << "Daemon: cannot configure supervisor: " << configured.error () << std::endl;
				d->logSink.reset ();
				worker->Stop (500);
				return false;
			}
		}
		else
		{
			configured = d->logSink->ReattachProxy (std::move (channel));
			if (!configured)
			{
				std::cerr << "Daemon: cannot reattach supervisor: " << configured.error () << std::endl;
				worker->Stop (500);
				return false;
			}
		}
		d->logWorker = std::move (worker);
		d->logWorkerStarted = std::chrono::steady_clock::now ();
		return true;
	}

	void Daemon_Singleton::StopPrivsepLogWorker ()
	{
		d->logSupervisorStop.store (true, std::memory_order_relaxed);
		// Join the monitor before touching the sink or worker: it may be in
		// StartPrivsepLogWorker() and about to replace d->logWorker. The stop
		// flag makes it return at the next bounded sleep. The worker itself
		// keeps running, so the graceful shutdown below can still flush.
		if (d->logSupervisor.joinable ()) d->logSupervisor.join ();
		// Central safety net: no path may destroy the sink while the logger
		// still holds a callback that captures its address. Normal stop and
		// start-failure paths already clear it; clearing here also covers an
		// early destructor or a future caller.
		i2p::log::Logger ().SetSink (nullptr);
		if (d->logSink) (void)d->logSink->Shutdown ();
		if (d->logSink) d->logSink.reset ();
		if (d->logWorker)
		{
			d->logWorker->Stop ();
			d->logWorker.reset ();
		}
	}

	void Daemon_Singleton::RunPrivsepLogSupervisor ()
	{
		i2p::util::SetThreadName ("privsep-mon");
		// Never steal process-directed shutdown signals from the main thread.
		// The worker child resets its mask after fork.
		sigset_t allSignals;
		sigfillset (&allSignals);
		pthread_sigmask (SIG_BLOCK, &allSignals, nullptr);
		unsigned attempts = 0;
		auto backoff = LOG_RESTART_INITIAL_BACKOFF;
		while (!d->logSupervisorStop.load (std::memory_order_relaxed))
		{
			if (!SleepUnlessStopping (d->logSupervisorStop, LOG_SUPERVISOR_TICK)) return;
			if (!d->logSink || !d->logWorker) continue;

			const bool running = d->logWorker->IsRunning ();
			const bool open = d->logSink->IsOpen ();
			if (running && open)
			{
				if (attempts != 0
					&& std::chrono::steady_clock::now () - d->logWorkerStarted >= LOG_WORKER_STABLE_PERIOD)
				{
					attempts = 0;
					backoff = LOG_RESTART_INITIAL_BACKOFF;
				}
				continue;
			}

			d->logWorker->Stop (500); // reap and release the old channel
			if (d->logSupervisorStop.load (std::memory_order_relaxed)) return;
			if (!d->privsepCanRestart)
			{
				// The OpenBSD runtime policy has proc only for reaping/signalling;
				// it has no exec. A dead supervisor cannot be replaced without
				// widening the core's capability set.
				std::cerr << "Daemon: supervisor exited; core has no exec capability to replace it" << std::endl;
				return;
			}
			std::cerr << "Daemon: supervisor " << (running ? "channel closed" : "exited") << "; restarting"
					  << std::endl;
			if (attempts >= LOG_RESTART_MAX_ATTEMPTS)
			{
				std::cerr << "Daemon: supervisor restart limit reached; "
							 "log records are dropped (no local fallback)"
						  << std::endl;
				return;
			}
			if (!SleepUnlessStopping (d->logSupervisorStop, backoff)) return;
			++attempts;
			backoff = std::min (backoff * 2, LOG_RESTART_MAX_BACKOFF);
			if (!StartPrivsepLogWorker ())
			{
				std::cerr << "Daemon: supervisor restart failed (attempt " << attempts << ")" << std::endl;
			}
		}
	}
#endif

	Daemon_Singleton::Daemon_Singleton ()
		: isDaemon (false)
		, running (true)
		, d (std::make_unique<Daemon_Singleton_Private> ())
	{
	}
	Daemon_Singleton::~Daemon_Singleton ()
	{
#if !defined(_WIN32) && !defined(I2PD_NO_PRIVSEP)
		if (d && d->routerStorage) d->routerStorage->Stop ();
		if (d && d->logSink)
		{
			// Normal shutdown calls stop(), but an early exit or a failed
			// start must not leave the logger thread invoking a sink whose
			// owner is about to be destroyed with this object.
			i2p::log::Logger ().Stop ();
			i2p::log::Logger ().SetSink (nullptr);
			StopPrivsepLogWorker ();
		}
#endif
	}

	bool Daemon_Singleton::IsService () const
	{
		bool service = false;
		i2p::config::GetOption ("service", service);
		return service;
	}

	bool Daemon_Singleton::BeginGracefulShutdown ()
	{
		if (!SupportsGracefulShutdown ()) return false;
		// Signal-driven and IPC-driven requests are idempotent: a second
		// request while the drain is active must not restart the countdown.
		if (GetGracefulShutdownInterval () > 0) return true;

		m_GracefulTransitRestore = i2p::context.AcceptsTunnels ();
		i2p::context.SetAcceptsTunnels (false);
		int timeout = i2p::tunnel::tunnels.GetTransitTunnelsExpirationTimeout ();
		if (timeout <= 0) timeout = 10 * 60;
		SetGracefulShutdownInterval (timeout);
		LogPrint (eLogInfo, "Daemon: Graceful shutdown after ", timeout, " seconds");
		return true;
	}

	bool Daemon_Singleton::CancelGracefulShutdown ()
	{
		if (!SupportsGracefulShutdown ()) return false;
		if (GetGracefulShutdownInterval () <= 0) return true;
		SetGracefulShutdownInterval (0);
		i2p::context.SetAcceptsTunnels (m_GracefulTransitRestore);
		m_GracefulTransitRestore = false;
		LogPrint (eLogInfo, "Daemon: Graceful shutdown cancelled");
		return true;
	}

	void Daemon_Singleton::setDataDir (std::string_view path)
	{
		if (!path.empty ()) DaemonDataDir = path;
	}

#if !defined(_WIN32) && !defined(I2PD_NO_PRIVSEP)
	namespace
	{
		/**
		 * A torrent tunnel may request isolated payload storage. The option
		 * is per tunnel section in tunnels.conf and tunnels.d (one .conf per
		 * tunnel) and is parsed by ClientContext much later, so the
		 * credential gate - which must run before the startup pledge and the
		 * first spawn - needs a bounded pre-scan. Only an explicit true
		 * counts; ClientContext remains the authority for the value that is
		 * actually used, and the storage broker re-validates the delegated
		 * root independently.
		 */
		constexpr size_t MAX_TUNNEL_SCAN_FILES = 64;
		constexpr size_t MAX_TUNNEL_SCAN_BYTES = 256 * 1024;

		std::string_view TrimScanText (std::string_view text)
		{
			const auto first = text.find_first_not_of (" \t\r");
			if (first == std::string_view::npos) return { };
			const auto last = text.find_last_not_of (" \t\r");
			return text.substr (first, last - first + 1);
		}

		std::string LowerScanText (std::string_view text)
		{
			std::string lowered (text);
			for (auto & character : lowered)
				if (character >= 'A' && character <= 'Z') character = static_cast<char> (character - 'A' + 'a');
			return lowered;
		}

		bool TunnelFileRequestsIsolatedStorage (const std::string & path)
		{
			std::ifstream file (path);
			if (!file) return false;
			size_t budget = MAX_TUNNEL_SCAN_BYTES;
			std::string line;
			while (std::getline (file, line))
			{
				if (line.size () >= budget) return false;
				budget -= line.size ();
				const auto separator = line.find_first_of ("=:");
				if (separator == std::string::npos) continue;
				const auto key = LowerScanText (TrimScanText (std::string_view (line).substr (0, separator)));
				if (key != "isolatedstorage") continue;
				const auto value = LowerScanText (TrimScanText (std::string_view (line).substr (separator + 1)));
				if (value == "true" || value == "1" || value == "yes") return true;
			}
			return false;
		}

		bool InstalledTunnelsRequestIsolatedStorage ()
		{
			std::string tunnelsConfig;
			i2p::config::GetOption ("tunconf", tunnelsConfig);
			if (tunnelsConfig.empty ()) tunnelsConfig = i2p::fs::DataDirPath ("tunnels.conf");
			if (i2p::fs::Exists (tunnelsConfig) && TunnelFileRequestsIsolatedStorage (tunnelsConfig)) return true;

			std::string tunnelsDir;
			i2p::config::GetOption ("tunnelsdir", tunnelsDir);
			if (tunnelsDir.empty ()) tunnelsDir = i2p::fs::DataDirPath ("tunnels.d");
			std::error_code ec;
			if (!std::filesystem::is_directory (tunnelsDir, ec)) return false;
			size_t scanned = 0;
			std::filesystem::directory_iterator iterator (tunnelsDir, ec);
			const std::filesystem::directory_iterator end;
			while (!ec && iterator != end && scanned < MAX_TUNNEL_SCAN_FILES)
			{
				if (iterator->path ().extension () == ".conf")
				{
					++scanned;
					if (TunnelFileRequestsIsolatedStorage (iterator->path ().string ())) return true;
				}
				iterator.increment (ec);
			}
			return false;
		}

		/**
		 * The isolation switches that require a worker identity.
		 *
		 * They are read independently: `privsep.enabled` selects the log
		 * supervisor (still gated by `privsep.log`), `privsep.routerstorage`
		 * the fixed-file writer, `privsep.artifacts` the hash-named artifact
		 * writer, and an installed tunnel that requests `isolatedstorage` the
		 * torrent payload worker. Any() therefore decides whether a root core
		 * without a configured `privsep.user` must refuse startup, not
		 * whether a specific worker is configured.
		 */
		struct PrivsepFeatures
		{
			bool enabled = false;
			bool routerStorage = false;
			bool artifacts = false;
			bool isolatedTorrentStorage = false;

			bool Any () const { return enabled || routerStorage || artifacts || isolatedTorrentStorage; }
		};

		PrivsepFeatures ReadPrivsepFeatures ()
		{
			PrivsepFeatures features;
			i2p::config::GetOption ("privsep.enabled", features.enabled);
			i2p::config::GetOption ("privsep.routerstorage", features.routerStorage);
			i2p::config::GetOption ("privsep.artifacts", features.artifacts);
			features.isolatedTorrentStorage = InstalledTunnelsRequestIsolatedStorage ();
			return features;
		}

		/**
		 * Resolve and install the worker identity.
		 *
		 * Runs before the startup pledge (name-service lookups need ambient
		 * filesystem access), before the logger thread and before the first
		 * spawn. A root core with any isolation feature enabled and no
		 * configured `privsep.user` fails startup: silently continuing would
		 * give the workers root's ambient authority while claiming isolation.
		 * No chroot is attempted; credential drop happens in the forked
		 * child.
		 */
		bool PreparePrivsepIdentity ()
		{
			std::string user, group;
			i2p::config::GetOption ("privsep.user", user);
			i2p::config::GetOption ("privsep.group", group);
			auto resolved = i2p::privsep::ResolveWorkerCredentials (user, group);
			if (!resolved)
			{
				std::cerr << "Daemon: privsep identity: " << resolved.error () << std::endl;
				return false;
			}
			// An unprivileged core cannot switch a child to a different uid/gid:
			// setuid/setgid would fail after fork, so reject the configuration
			// now with an explicit message instead of failing the first spawn.
			if (resolved->configured && ::geteuid () != 0
				&& (resolved->uid != static_cast<uint32_t> (::geteuid ())
					|| resolved->gid != static_cast<uint32_t> (::getegid ())))
			{
				std::cerr << "Daemon: privsep identity: an unprivileged daemon cannot switch to another "
							 "uid/gid"
						  << std::endl;
				return false;
			}
			// The resolver rejects uid/gid 0 for a configured identity, so the
			// installed pair is either the current identity or unprivileged.
			i2p::privsep::SetWorkerCredentials (*resolved);
			if (!ReadPrivsepFeatures ().Any ()) return true;
			if (::geteuid () == 0 && !resolved->configured)
			{
				std::cerr << "Daemon: worker isolation requires an unprivileged identity; "
							 "configure privsep.user or run as a dedicated user"
						  << std::endl;
				return false;
			}
			return true;
		}
	}
#endif

	bool Daemon_Singleton::init (int argc, char * argv[])
	{
		return init (argc, argv, nullptr);
	}

	bool Daemon_Singleton::init (int argc, char * argv[], std::shared_ptr<std::ostream> logstream)
	{
		auto rollbackPhase = [this] (DaemonInitPhase phase)
		{
			// The private-data access below is compiled out on Windows, Android
			// and I2PD_NO_PRIVSEP builds; keep the capture used there too.
			(void)d;
			switch (phase)
			{
			case DaemonInitPhase::Logging:
				// init() owns the destination setup; the logger worker starts
				// later in start(). Reset only this component, not the daemon.
				i2p::log::Logger ().Stop ();
				(void)i2p::log::Logger ().ResetDestination ();
				break;
			case DaemonInitPhase::RouterContext: i2p::context.ResetAfterFailedInit (); break;
			case DaemonInitPhase::RouterStorage:
				// Replace the callback before stopping its authority. A failed
				// isolated initialization must never fall back to core writes.
				i2p::data::InstallRouterPersistenceWriter ([] (auto, auto) { return false; });
#if !defined(_WIN32) && !defined(__ANDROID__) && !defined(I2PD_NO_PRIVSEP)
				if (d->routerStorage)
				{
					d->routerStorage->Stop ();
					d->routerStorage.reset ();
				}
#endif
				break;
			case DaemonInitPhase::DataStorage:
#if !defined(_WIN32) && !defined(__ANDROID__) && !defined(I2PD_NO_PRIVSEP)
				// Revoke artifact authority before stopping the worker that owns
				// it, for the same reason as the fixed files.
				i2p::data::ClearArtifactStorage ();
				if (d->dataStorage)
				{
					d->dataStorage->Stop ();
					d->dataStorage.reset ();
				}
#endif
				break;
			case DaemonInitPhase::Crypto: i2p::crypto::TerminateCrypto (); break;
			}
		};
		DaemonInitTransaction initTransaction (rollbackPhase);

		i2p::config::Init ();
		i2p::config::ParseCmdline (argc, argv);

		std::string config;
		i2p::config::GetOption ("conf", config);
		std::string datadir;
		if (DaemonDataDir != "")
		{
			datadir = DaemonDataDir;
		}
		else
		{
			i2p::config::GetOption ("datadir", datadir);
		}

		i2p::fs::DetectDataDir (datadir, IsService ());
#ifdef __OpenBSD__
		i2p::fs::DetectDataDir (AbsolutePath (i2p::fs::GetDataDir ()), IsService ());
#else
		i2p::fs::Init ();
#endif

		datadir = i2p::fs::GetDataDir ();

		if (config == "")
		{
			config = i2p::fs::DataDirPath ("i2pd.conf");
			if (!i2p::fs::Exists (config))
			{
				// use i2pd.conf only if exists
				config = ""; /* reset */
			}
		}

		i2p::config::ParseConfig (config);
		i2p::config::Finalize ();

#ifdef __OpenBSD__
		// A datadir in the configuration is not available during config
		// lookup.
		std::string configuredDataDir = DaemonDataDir;
		if (configuredDataDir.empty ()) i2p::config::GetOption ("datadir", configuredDataDir);
		i2p::fs::DetectDataDir (configuredDataDir, IsService ());
		i2p::fs::DetectDataDir (AbsolutePath (i2p::fs::GetDataDir ()), IsService ());
		datadir = i2p::fs::GetDataDir ();
		i2p::fs::Init ();
		i2p::config::SetOption ("conf", AbsolutePath (config));
		for (const char * option :
			{ "tunconf", "tunnelsdir", "certsdir", "logfile", "pidfile", "reseed.file", "reseed.zipfile",
				"addressbook.hostsfile", "tui.socket", "openbsd.pledge_file", "openbsd.unveil_file" })
		{
			std::string path;
			i2p::config::GetOption (option, path);
			if (!path.empty ()) i2p::config::SetOption (option, AbsolutePath (path));
		}
#endif

		i2p::config::GetOption ("daemon", isDaemon);
		bool foreground = false;
		i2p::config::GetOption ("foreground", foreground);
		// A service manager must retain the process. --foreground overrides a
		// daemon=true directive that a local administrator may have left in
		// the configuration file.
		if (foreground) isDaemon = false;

#if !defined(_WIN32) && !defined(I2PD_NO_PRIVSEP)
		// The worker identity must be resolved and installed before the
		// startup pledge (name-service lookups), before the logger thread
		// starts and before any spawn helper runs. A root core with isolation
		// requested and no configured `privsep.user` fails here instead of
		// degrading silently.
		if (!PreparePrivsepIdentity ()) return false;
#endif

		std::string certsdir;
		i2p::config::GetOption ("certsdir", certsdir);
		i2p::fs::SetCertsDir (certsdir);

		certsdir = i2p::fs::GetCertsDir ();

		std::string logs = "";
		i2p::config::GetOption ("log", logs);
		std::string logfile = "";
		i2p::config::GetOption ("logfile", logfile);
		std::string loglevel = "";
		i2p::config::GetOption ("loglevel", loglevel);
		bool logclftime;
		i2p::config::GetOption ("logclftime", logclftime);

		/* setup logging */
		initTransaction.Run (DaemonInitPhase::Logging, [] { });
		i2p::log::Logger ().SetTimeFormat (logclftime ? "[%d/%b/%Y:%H:%M:%S %z]" : "%H:%M:%S");

#if defined(WIN32_APP)
		// The Windows GUI logs to a file
		logs = "file";
#else
		if (isDaemon && (logs == "" || logs == "stdout")) logs = "file";
#endif

#if !defined(_WIN32) && !defined(I2PD_NO_PRIVSEP)
		bool privsepEnabled = false, privsepLogEnabled = false, privsepArtifacts = false;
		i2p::config::GetOption ("privsep.enabled", privsepEnabled);
		i2p::config::GetOption ("privsep.log", privsepLogEnabled);
		// `privsep.artifacts` is independent of the log worker: it moves the
		// hash-named runtime artifacts (RouterInfo cache, peer profiles and
		// destination keys) into the data-storage worker. Without it the core
		// keeps writing those files exactly as before.
		i2p::config::GetOption ("privsep.artifacts", privsepArtifacts);
		d->privsepLog = privsepEnabled && privsepLogEnabled && !logstream;
		d->privsepArtifacts = privsepEnabled && privsepArtifacts;
#ifdef __OpenBSD__
		// The runtime policy drops proc/exec. The supervisor itself owns
		// bounded log-worker replacement; the core must not attempt to fork a
		// replacement supervisor after that policy is installed.
		bool openbsdPledge = false, openbsdUnveil = false;
		i2p::config::GetOption ("openbsd.pledge_enabled", openbsdPledge);
		i2p::config::GetOption ("openbsd.unveil_enabled", openbsdUnveil);
		d->privsepCanRestart = !(openbsdPledge || openbsdUnveil);
#endif
#endif

		i2p::log::Logger ().SetLogLevel (loglevel);
		if (logstream)
		{
			LogPrint (eLogInfo, "Log: Sending messages to std::ostream");
			i2p::log::Logger ().SendTo (logstream);
		}
		else if (logs == "file")
		{
			if (logfile == "") logfile = i2p::fs::DataDirPath ("i2pd.log");
#ifdef __OpenBSD__
			i2p::config::SetOption ("logfile", logfile);
#endif
			LogPrint (eLogInfo, "Log: Sending messages to ", logfile);
#if !defined(_WIN32) && !defined(I2PD_NO_PRIVSEP)
			if (d->privsepLog)
				d->privsepLogPath = logfile;
			else
#endif
			{
				i2p::log::Logger ().SendTo (logfile);
			}
#if !defined(_WIN32) && !defined(I2PD_NO_PRIVSEP)
		}
		else if (logs == "syslog")
		{
			LogPrint (eLogInfo, "Log: Sending messages to syslog");
			if (d->privsepLog)
				d->privsepSyslog = true;
			else
			{
				i2p::log::Logger ().SendTo ("open-i2pd", LOG_DAEMON);
			}
#elif !defined(_WIN32)
		}
		else if (logs == "syslog")
		{
			LogPrint (eLogInfo, "Log: Sending messages to syslog");
			i2p::log::Logger ().SendTo ("open-i2pd", LOG_DAEMON);
#endif
		}
		else
		{
			// use stdout -- default
		}

		LogPrint (eLogNone, "openi2pd v", VERSION, " (", I2P_VERSION, ") starting...");
		LogPrint (eLogDebug, "FS: Main config file: ", config);
		LogPrint (eLogDebug, "FS: Data directory: ", datadir);
		LogPrint (eLogDebug, "FS: Certificates directory: ", certsdir);

		bool precomputation;
		i2p::config::GetOption ("precomputation.elgamal", precomputation);
		bool ssu;
		i2p::config::GetOption ("ssu", ssu);
		if (!ssu && i2p::config::IsDefault ("precomputation.elgamal"))
			precomputation = false; // we don't elgamal table if no ssu, unless it's specified explicitly
#if defined(__OpenBSD__) && !defined(I2PD_NO_PRIVSEP)
		// Keep startup-only promises until fork/setsid/setrlimit, unveil and
		// the first worker spawn. This window is the widest set the core ever
		// holds; UnixDaemon::start() later builds the runtime set from the
		// same table and verifies it is a subset (pledge can only drop
		// promises).
		bool usePledge, useUnveil;
		i2p::config::GetOption ("openbsd.pledge_enabled", usePledge);
		i2p::config::GetOption ("openbsd.unveil_enabled", useUnveil);
		if (usePledge)
		{
			bool upnp;
			i2p::config::GetOption ("upnp.enabled", upnp);
			i2p::privsep::OpenBSDPledgeWindow window;
			window.unveil = useUnveil;
			window.upnp = upnp;
			// Every startup worker (log supervisor, router storage, data storage
			// and the torrent storage broker) is forked/exec'ed before the
			// runtime sandbox. exec exists only in this window.
			window.execWorkers = true;
			// Supervised storage hands a worker channel or root back to the
			// core.
			window.descriptorReception = true;
			// The forked child drops supplementary groups, gid and uid.
			window.credentialDrop = i2p::privsep::GetWorkerCredentials ().configured;
			auto promises = i2p::privsep::OpenBSDPledgePromiseList (i2p::privsep::OpenBSDPledgePhase::Startup, window);
			std::string policy;
			i2p::config::GetOption ("openbsd.pledge_file", policy);
			if (!policy.empty ())
			{
				// One parser for both phases: an operator file may select from
				// the validated base set, never widen it, and must not contain
				// comments or blank lines that could look like active promises.
				std::vector<std::string_view> allowed;
				allowed.reserve (promises.size ());
				for (const auto & promise : promises) allowed.emplace_back (promise);
				auto accepted
					= i2p::privsep::ParseOpenBSDPledgeFile (policy, allowed, i2p::privsep::OpenBSDPledgePhase::Startup);
				if (!accepted)
				{
					std::cerr << "Daemon: openbsd.pledge_file: " << accepted.error () << std::endl;
					std::exit (EXIT_FAILURE);
				}
				for (const auto & promise : *accepted)
					if (std::find (promises.begin (), promises.end (), promise) == promises.end ())
						promises.push_back (promise);
			}
			std::string joined;
			for (const auto & promise : promises)
			{
				if (!joined.empty ()) joined.push_back (' ');
				joined += promise;
			}
			if (pledge (joined.c_str (), i2p::privsep::OPENBSD_PLEDGE_EXECPROMISES) == -1)
			{
				std::cerr << "Daemon: startup pledge: " << strerror (errno) << std::endl;
				std::exit (EXIT_FAILURE);
			}
		}
#endif
		initTransaction.Run (DaemonInitPhase::Crypto,
			[&]
			{
				if (!i2p::crypto::InitCrypto (precomputation))
					throw std::runtime_error ("Crypto: required libcrypto provider algorithms are unavailable");
			});

		i2p::transport::InitAddressFromIface (); // get address4/6 from interfaces

		int netID;
		i2p::config::GetOption ("netid", netID);
		i2p::context.SetNetID (netID);

		bool checkReserved;
		i2p::config::GetOption ("reservedrange", checkReserved);
		i2p::transport::transports.SetCheckReserved (checkReserved);

		bool routerStorage = false;
		i2p::config::GetOption ("privsep.routerstorage", routerStorage);
		if (routerStorage)
		{
			const bool storageReady = initTransaction.Run (DaemonInitPhase::RouterStorage,
				[&] ()
				{
					i2p::data::InstallRouterPersistenceWriter ([] (auto, auto) { return false; });
#if !defined(_WIN32) && !defined(__ANDROID__) && !defined(I2PD_NO_PRIVSEP)
					std::string error;
					d->routerStorage = std::make_shared<i2p::privsep::RouterStorageClient> ();
					if (!d->routerStorage->Start (i2p::fs::GetDataDir (), error))
					{
						LogPrint (eLogError, "Daemon: Required router storage failed: ", error);
						return false;
					}
					i2p::data::InstallRouterPersistenceWriter (
						[worker = d->routerStorage] (auto file, auto bytes) { return worker->Write (file, bytes); });
					return true;
#else
					LogPrint (eLogError, "Daemon: Router storage worker unavailable on this platform");
					return false;
#endif
				});
			if (!storageReady) return false;
		}
		else
			i2p::data::InstallRouterPersistenceWriter ({ });

#if !defined(_WIN32) && !defined(I2PD_NO_PRIVSEP)
		if (d->privsepArtifacts)
		{
			// The data worker joins the same isolation phase as the fixed-file
			// router storage: both are persistence authority, and a rollback
			// must revoke both before either process is stopped.
			initTransaction.Run (DaemonInitPhase::DataStorage,
				[&] ()
				{
					// Fail closed until the isolated writer is actually installed.
					i2p::data::ClearArtifactStorage ();
#if !defined(__ANDROID__)
					std::string error;
					d->dataStorage = std::make_shared<i2p::privsep::DataStorageClient> ();
					if (!d->dataStorage->Start (i2p::fs::GetDataDir (), error))
					{
						// A bounded, non-secret diagnostic: the message never
						// contains an artifact name, path or key material. The
						// router continues with artifact writes failing closed
						// instead of silently reverting to core writes.
						LogPrint (eLogError, "Daemon: Data storage worker unavailable: ", error);
						d->dataStorage->Stop ();
						d->dataStorage.reset ();
						i2p::data::ClearArtifactStorage ();
						return;
					}
					auto client = d->dataStorage;
					i2p::data::InstallArtifactStorage ([client] (i2p::data::StorageArtifact kind, std::string_view name,
														   std::span<const uint8_t> bytes)
						{ return client->Write (kind, name, bytes); },
						[client] (i2p::data::StorageArtifact kind, std::string_view name)
						{ return client->Remove (kind, name); });
					LogPrint (eLogInfo, "Daemon: Hash-named artifact writes are isolated");
#else
				LogPrint (eLogError, "Daemon: Data storage worker unavailable on this platform");
#endif
				});
		}
#endif

		initTransaction.Run (DaemonInitPhase::RouterContext, [] { i2p::context.Init (); });

		i2p::transport::InitTransports ();

		bool isFloodfill;
		i2p::config::GetOption ("floodfill", isFloodfill);
		if (isFloodfill)
		{
			LogPrint (eLogInfo, "Daemon: Router configured as floodfill");
			i2p::context.SetFloodfill (true);
		}
		else
			i2p::context.SetFloodfill (false);

		bool transit;
		i2p::config::GetOption ("notransit", transit);
		i2p::context.SetAcceptsTunnels (!transit);
		uint32_t transitTunnels;
		i2p::config::GetOption ("limits.transittunnels", transitTunnels);
		if (transitTunnels < 2) transitTunnels = 2;
		if (isFloodfill && i2p::config::IsDefault ("limits.transittunnels"))
			transitTunnels *= 2; // double default number of transit tunnels for floodfill
		i2p::tunnel::tunnels.SetMaxNumTransitTunnels (transitTunnels);

		/* this section also honors 'floodfill' flag, if set above */
		std::string bandwidth;
		i2p::config::GetOption ("bandwidth", bandwidth);
		if (bandwidth.length () > 0)
		{
			const auto NumBandwithRegex = std::regex (R"(^\d+$)");
			const auto BandwithRegex = std::regex (R"((\d+)(b|kb|mb|gb))");
			std::smatch bandWithMatch;

			if (bandwidth.length () == 1 && ((bandwidth[0] >= 'K' && bandwidth[0] <= 'P') || bandwidth[0] == 'X'))
			{
				i2p::context.SetBandwidth (bandwidth[0]);
				LogPrint (eLogInfo, "Daemon: Bandwidth set to ", i2p::context.GetBandwidthLimit (), "KBps");
			}
			else if (std::regex_match (bandwidth, bandWithMatch, BandwithRegex))
			{
				const auto number = bandWithMatch[1].str ();
				const auto unit = bandWithMatch[2].str ();
				int limit = std::atoi (number.c_str ());
				if (unit == "b")
				{
					limit /= 1000;
				}
				else if (unit == "mb")
				{
					limit *= 1000;
				}
				else if (unit == "gb")
				{
					limit *= 1000000;
				}
				// if limit more than 32 bits then its will be negative
				if (limit < 0)
				{
					LogPrint (eLogInfo, "Daemon: Unexpected bandwidth ", bandwidth, ". Set to 'low'");
					i2p::context.SetBandwidth (i2p::data::CAPS_FLAG_LOW_BANDWIDTH2);
				}
				else
				{
					i2p::context.SetBandwidth (limit);
				}
			}
			else if (std::regex_search (bandwidth, NumBandwithRegex))
			{
				auto value = std::atoi (bandwidth.c_str ());
				if (value > 0)
				{
					i2p::context.SetBandwidth (value);
					LogPrint (eLogInfo, "Daemon: Bandwidth set to ", i2p::context.GetBandwidthLimit (), " KBps");
				}
				else
				{
					LogPrint (eLogInfo, "Daemon: Unexpected bandwidth ", bandwidth, ". Set to 'low'");
					i2p::context.SetBandwidth (i2p::data::CAPS_FLAG_LOW_BANDWIDTH2);
				}
			}
		}
		else if (isFloodfill)
		{
			LogPrint (eLogInfo, "Daemon: Floodfill bandwidth set to 'extra'");
			i2p::context.SetBandwidth (i2p::data::CAPS_FLAG_EXTRA_BANDWIDTH2);
		}
		else
		{
#if IS_X86_64
			if (std::thread::hardware_concurrency () >= 4) // quad core or more
			{
				LogPrint (eLogInfo, "Daemon: bandwidth set to 'extra'");
				i2p::context.SetBandwidth (i2p::data::CAPS_FLAG_EXTRA_BANDWIDTH1);
			}
			else
			{
				LogPrint (eLogInfo, "Daemon: bandwidth set to 'high'");
				i2p::context.SetBandwidth (i2p::data::CAPS_FLAG_HIGH_BANDWIDTH);
			}
#else
			LogPrint (eLogInfo, "Daemon: bandwidth set to 'low'");
			i2p::context.SetBandwidth (i2p::data::CAPS_FLAG_LOW_BANDWIDTH2);
#endif
		}

		int shareRatio;
		i2p::config::GetOption ("share", shareRatio);
		i2p::context.SetShareRatio (shareRatio);

		std::string family;
		i2p::config::GetOption ("family", family);
		i2p::context.SetFamily (family);
		if (family.length () > 0) LogPrint (eLogInfo, "Daemon: Router family set to ", family);

		bool trust;
		i2p::config::GetOption ("trust.enabled", trust);
		if (trust)
		{
			LogPrint (eLogInfo, "Daemon: Explicit trust enabled");
			std::string f;
			i2p::config::GetOption ("trust.family", f);
			std::string_view fam (f);
			std::string routers;
			i2p::config::GetOption ("trust.routers", routers);
			bool restricted = false;
			if (fam.length () > 0)
			{
				std::vector<std::string_view> fams;
				size_t pos = 0, comma;
				do
				{
					comma = fam.find (',', pos);
					fams.push_back (fam.substr (pos, comma != std::string::npos ? comma - pos : std::string::npos));
					pos = comma + 1;
				} while (comma != std::string::npos);
				i2p::transport::transports.RestrictRoutesToFamilies (fams);
				restricted = fams.size () > 0;
			}
			if (!routers.empty ())
			{
				auto idents = i2p::data::ExtractIdentHashes (routers);
				LogPrint (eLogInfo, "Daemon: Setting restricted routes to use ", idents.size (), " trusted routers");
				i2p::transport::transports.RestrictRoutesToRouters (idents);
				restricted = idents.size () > 0;
			}
			if (!restricted) LogPrint (eLogError, "Daemon: No trusted routers of families specified");
			bool hidden;
			i2p::config::GetOption ("trust.hidden", hidden);
			if (hidden)
			{
				LogPrint (eLogInfo, "Daemon: Hidden mode enabled");
				i2p::context.SetHidden (true);
			}
		}

		std::string language;
		i2p::config::GetOption ("lang", language);
		i2p::i18n::SetLanguage (language);

		initTransaction.Commit ();
		return true;
	}

	bool Daemon_Singleton::start ()
	{
#if !defined(_WIN32) && !defined(I2PD_NO_PRIVSEP)
		if (d->privsepLog)
		{
			// The supervisor is normally prepared before the OpenBSD runtime
			// sandbox is installed. Keep the lazy path for non-OpenBSD builds
			// and for callers that did not use PreparePrivsepWorkers().
			if (!d->logWorker && !StartPrivsepLogWorker ()) return false;
			if (!d->logSink) return false;
			// The sink object lives until the logger thread is stopped, so the
			// callback below never observes a replaced or freed sink.
			auto * sink = d->logSink.get ();
			i2p::log::Logger ().SetSink ([sink] (LogLevel level, std::time_t timestamp, std::string_view text)
				{ sink->Write (static_cast<uint8_t> (level), timestamp, text); });
		}
#endif
		i2p::log::Logger ().Start ();
		LogPrint (eLogInfo, "Daemon: Starting NetDB");
		i2p::data::netdb.Start ();

		bool upnp;
		i2p::config::GetOption ("upnp.enabled", upnp);
		if (upnp)
		{
			d->UPnP = std::make_unique<i2p::transport::UPnP> ();
			d->UPnP->Start ();
		}

		bool nettime;
		i2p::config::GetOption ("nettime.enabled", nettime);
		if (nettime)
		{
			d->m_NTPSync = std::make_unique<i2p::util::NTPTimeSync> ();
			d->m_NTPSync->Start ();
		}

		bool ntcp2;
		i2p::config::GetOption ("ntcp2.enabled", ntcp2);
		bool ssu2;
		i2p::config::GetOption ("ssu2.enabled", ssu2);
		LogPrint (eLogInfo, "Daemon: Starting Transports");
		if (!ssu2) LogPrint (eLogInfo, "Daemon: SSU2 disabled");
		if (!ntcp2) LogPrint (eLogInfo, "Daemon: NTCP2 disabled");

		i2p::transport::transports.Start (ntcp2, ssu2);
		if (i2p::transport::transports.IsBoundSSU2 () || i2p::transport::transports.IsBoundNTCP2 ())
			LogPrint (eLogInfo, "Daemon: Transports started");
		else
		{
			LogPrint (eLogCritical, "Daemon: Failed to start Transports");
			// UPnP and NTP own threads too. start() is not followed by stop()
			// on failure, so tear them down here before their observers.
			if (d->UPnP)
			{
				d->UPnP->Stop ();
				d->UPnP = nullptr;
			}
			if (d->m_NTPSync)
			{
				d->m_NTPSync->Stop ();
				d->m_NTPSync = nullptr;
			}
			/** shut down netdb right away */
			i2p::transport::transports.Stop ();
			i2p::data::netdb.Stop ();
#if !defined(_WIN32) && !defined(I2PD_NO_PRIVSEP)
			if (d->privsepLog)
			{
				// start() is not followed by stop() on failure; tear the sink
				// down here so the logger callback cannot dangle.
				i2p::log::Logger ().Stop ();
				i2p::log::Logger ().SetSink (nullptr);
				StopPrivsepLogWorker ();
			}
#endif
			return false;
		}

		LogPrint (eLogInfo, "Daemon: Starting Tunnels");
		i2p::tunnel::tunnels.Start ();

		LogPrint (eLogInfo, "Daemon: Starting Router context");
		i2p::context.Start ();

		LogPrint (eLogInfo, "Daemon: Starting Client");
		i2p::client::context.Start ();

		// Destructive and router-wide TUI operations are serialized on this
		// daemon-owned executor, not executed on the local-IPC thread.
		if (!i2p::tui::StartAdminCommandQueue (&ExecuteAdminCommand))
			LogPrint (eLogWarning, "Daemon: Admin command queue unavailable");

#if !defined(__ANDROID__)
		// Android has no AF_UNIX TUI client. The in-app bridge calls the
		// shared request dispatcher directly; do not start an unused local
		// listener inside the app sandbox.
		bool tuiEnabled;
		i2p::config::GetOption ("tui.enabled", tuiEnabled);
		if (tuiEnabled)
		{
			std::string socketPath;
			i2p::config::GetOption ("tui.socket", socketPath);
			if (socketPath.empty ()) socketPath = i2p::fs::DataDirPath ("i2pd-tui.sock");
			// The TUI is optional: a failure to create its local IPC
			// endpoint must never prevent the router from running headless.
			d->tuiServer = std::make_unique<i2p::tui::Server> (socketPath);
			if (!d->tuiServer->Start ())
			{
				LogPrint (eLogWarning, "Daemon: TUI IPC disabled, router continues without it");
				d->tuiServer.reset ();
			}
		}
#endif

		// I2P Control Protocol
		bool i2pcontrol;
		i2p::config::GetOption ("i2pcontrol.enabled", i2pcontrol);
		if (i2pcontrol)
		{
			std::string i2pcpAddr;
			i2p::config::GetOption ("i2pcontrol.address", i2pcpAddr);
			uint16_t i2pcpPort;
			i2p::config::GetOption ("i2pcontrol.port", i2pcpPort);
			LogPrint (eLogInfo, "Daemon: Starting I2PControl at ", i2pcpAddr, ":", i2pcpPort);
			try
			{
				d->m_I2PControlService = std::make_unique<i2p::client::I2PControlService> (i2pcpAddr, i2pcpPort);
				d->m_I2PControlService->Start ();
			}
			catch (std::exception & ex)
			{
				LogPrint (eLogCritical, "Daemon: Failed to start I2PControl: ", ex.what ());
				ThrowFatal ("Unable to start I2PControl service at ", i2pcpAddr, ":", i2pcpPort, ": ", ex.what ());
			}
		}
#if !defined(_WIN32) && !defined(I2PD_NO_PRIVSEP)
		if (d->privsepLog && d->logSink && d->logWorker && !d->logSupervisor.joinable ())
		{
			// The monitor starts last so every earlier failure path can tear
			// down the worker without also having to stop a thread.
			d->logSupervisorStop.store (false, std::memory_order_relaxed);
			d->logSupervisor = std::thread ([this] { RunPrivsepLogSupervisor (); });
		}
#endif
		return true;
	}

	bool Daemon_Singleton::stop ()
	{
		LogPrint (eLogInfo, "Daemon: Shutting down");
		// These two own threads and read router state, so they must stop
		// before the subsystems they observe are torn down.
#if !defined(__ANDROID__)
		if (d->tuiServer)
		{
			LogPrint (eLogInfo, "Daemon: Stopping TUI IPC");
			d->tuiServer->Stop ();
			d->tuiServer = nullptr;
		}
#endif
		// No IPC thread can submit after the server above has been joined.
		i2p::tui::StopAdminCommandQueue ();
		if (d->m_I2PControlService)
		{
			LogPrint (eLogInfo, "Daemon: Stopping I2PControl");
			d->m_I2PControlService->Stop ();
			d->m_I2PControlService = nullptr;
		}

		// UPnP and NTP read router state from their own threads, so stop them
		// before the router context and its io_service are torn down.
		if (d->UPnP)
		{
			d->UPnP->Stop ();
			d->UPnP = nullptr;
		}

		if (d->m_NTPSync)
		{
			d->m_NTPSync->Stop ();
			d->m_NTPSync = nullptr;
		}

		LogPrint (eLogInfo, "Daemon: Stopping Client");
		i2p::client::context.Stop ();
		LogPrint (eLogInfo, "Daemon: Stopping Router context");
		i2p::context.Stop ();
		LogPrint (eLogInfo, "Daemon: Stopping Tunnels");
		i2p::tunnel::tunnels.Stop ();

		LogPrint (eLogInfo, "Daemon: Stopping Transports");
		i2p::transport::transports.Stop ();
		LogPrint (eLogInfo, "Daemon: Stopping NetDB");
		i2p::data::netdb.Stop ();
#if !defined(_WIN32) && !defined(I2PD_NO_PRIVSEP)
		// Revoke artifact authority before the worker that owns it is stopped
		// and before the data directory is torn down. A write that arrives
		// after this point fails closed instead of reaching a dying channel.
		i2p::data::ClearArtifactStorage ();
		if (d->dataStorage)
		{
			d->dataStorage->Stop ();
			d->dataStorage.reset ();
		}
		if (d->routerStorage) d->routerStorage->Stop ();
#endif
		i2p::crypto::TerminateCrypto ();
		// Keep the worker and its monitor alive while the logger drains the
		// queue. StopPrivsepLogWorker then joins the monitor before sending
		// the graceful shutdown to the worker.
		i2p::log::Logger ().Stop ();
		i2p::log::Logger ().SetSink (nullptr);
#if !defined(_WIN32) && !defined(I2PD_NO_PRIVSEP)
		if (d->privsepLog) StopPrivsepLogWorker ();
#endif

		return true;
	}

	static void ShowUptime (std::stringstream & s, int seconds)
	{
		int num;

		if ((num = seconds / 86400) > 0)
		{
			s << num << " days, ";
			seconds -= num * 86400;
		}
		if ((num = seconds / 3600) > 0)
		{
			s << num << " hours, ";
			seconds -= num * 3600;
		}
		if ((num = seconds / 60) > 0)
		{
			s << num << " min, ";
			seconds -= num * 60;
		}
		s << seconds << " seconds\n";
	}

	static void ShowTransfered (std::stringstream & s, size_t transfer)
	{
		auto bytes = transfer & 0x03ff;
		transfer >>= 10;
		auto kbytes = transfer & 0x03ff;
		transfer >>= 10;
		auto mbytes = transfer & 0x03ff;
		transfer >>= 10;
		auto gbytes = transfer;

		if (gbytes) s << gbytes << " GB, ";
		if (mbytes) s << mbytes << " MB, ";
		if (kbytes) s << kbytes << " KB, ";
		s << bytes << " Bytes\n";
	}

	static void ShowNetworkStatus (std::stringstream & s, RouterStatus status, bool testing, RouterError error)
	{
		switch (status)
		{
		case eRouterStatusOK: s << "OK"; break;
		case eRouterStatusFirewalled: s << "FW"; break;
		case eRouterStatusUnknown: s << "Unk"; break;
		case eRouterStatusProxy: s << "Proxy"; break;
		case eRouterStatusMesh: s << "Mesh"; break;
		case eRouterStatusStan: s << "Stan"; break;
		default: s << "Unk";
		};
		if (testing) s << " (Test)";
		if (error != eRouterErrorNone)
		{
			switch (error)
			{
			case eRouterErrorClockSkew: s << " - " << tr ("Clock skew"); break;
			case eRouterErrorOffline: s << " - " << tr ("Offline"); break;
			case eRouterErrorSymmetricNAT: s << " - " << tr ("Symmetric NAT"); break;
			case eRouterErrorFullConeNAT: s << " - " << tr ("Full cone NAT"); break;
			case eRouterErrorNoDescriptors: s << " - " << tr ("No Descriptors"); break;
			default:;
			}
		}
	}

	void PrintMainWindowText (std::stringstream & s)
	{
		s << "\n";
		s << "Status: ";
		ShowNetworkStatus (s, i2p::context.GetStatus (), i2p::context.GetTesting (), i2p::context.GetError ());
		if (i2p::context.SupportsV6 ())
		{
			s << " / ";
			ShowNetworkStatus (
				s, i2p::context.GetStatusV6 (), i2p::context.GetTestingV6 (), i2p::context.GetErrorV6 ());
		}
		s << "; ";
		s << "Caps: " << i2p::context.GetRouterInfoProperty ("caps") << "; ";
		s << "Success Rate: " << i2p::tunnel::tunnels.GetTunnelCreationSuccessRate () << "%\n";
		s << "Uptime: ";
		ShowUptime (s, i2p::context.GetUptime ());
		auto gracefulTimeLeft = Daemon.GetGracefulShutdownInterval ();
		if (gracefulTimeLeft > 0)
		{
			s << "Graceful shutdown, time left: ";
			ShowUptime (s, gracefulTimeLeft);
		}
		else
			s << "\n";
		s << "Inbound: " << i2p::transport::transports.GetInBandwidth () / 1024 << " KiB/s; ";
		s << "Outbound: " << i2p::transport::transports.GetOutBandwidth () / 1024 << " KiB/s\n";
		s << "Received: ";
		ShowTransfered (s, i2p::transport::transports.GetTotalReceivedBytes ());
		s << "Sent: ";
		ShowTransfered (s, i2p::transport::transports.GetTotalSentBytes ());
		s << "\n";
		s << "Routers: " << i2p::data::netdb.GetNumRouters () << "; ";
		s << "Floodfills: " << i2p::data::netdb.GetNumFloodfills () << "; ";
		s << "LeaseSets: " << i2p::data::netdb.GetNumLeaseSets () << "\n";
		s << "Tunnels: ";
		s << "In: " << i2p::tunnel::tunnels.CountInboundTunnels () << "; ";
		s << "Out: " << i2p::tunnel::tunnels.CountOutboundTunnels () << "; ";
		s << "Transit: " << i2p::tunnel::tunnels.CountTransitTunnels () << "\n";
		s << "\n";
	}
}
}
