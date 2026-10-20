/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Daemon.
 *
 * Implements router initialisation, subsystem sequencing, shutdown and the
 * optional privilege-separated log path. Startup parses configuration,
 * installs the OpenBSD startup pledge and starts NetDB, transports, tunnels,
 * clients, TUI IPC, I2PControl, UPnP and NTP; shutdown stops observers before
 * the state they read. A monitor thread replaces a dead log supervisor with
 * bounded backoff where exec remains available, and logging never falls back
 * to a local file.
 *
 */

#include <thread>
#include <memory>
#include <regex>
#include <algorithm>
#include <atomic>
#include <chrono>
#include "Expected.h"
#include <iostream>
#include <signal.h>
#ifndef _WIN32
#include <pthread.h>
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
#include "Garlic.h"
#include "Streaming.h"
#include "Destination.h"
#include "I2PControl.h"
#include "TuiServer.h"
#if !defined(_WIN32) && !defined(I2PD_NO_PRIVSEP)
#include "privsep/LogWorker.h"
#include "privsep/Worker.h"
#include "privsep/RouterStorage.h"
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
	// Keep paths stable across DaemonUnix::start()'s chdir(). The leaf need
	// not exist yet (log files, for example), so realpath() is unsuitable.
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

		std::unique_ptr<i2p::tui::Server> tuiServer;
#if !defined(_WIN32) && !defined(I2PD_NO_PRIVSEP)
		std::shared_ptr<i2p::privsep::RouterStorageClient> routerStorage;
		std::unique_ptr<i2p::privsep::LogSink> logSink;
		std::unique_ptr<i2p::privsep::ChildProcess> logWorker;
		std::thread logSupervisor;
		std::atomic<bool> logSupervisorStop { false };
		std::chrono::steady_clock::time_point logWorkerStarted;
		bool privsepLog = false;
		bool privsepSyslog = false;
		bool privsepCanRestart = true;
		std::string privsepLogPath;
#endif
		std::unique_ptr<i2p::client::I2PControlService> m_I2PControlService;
		std::unique_ptr<i2p::transport::UPnP> UPnP;
		std::unique_ptr<i2p::util::NTPTimeSync> m_NTPSync;
	};

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

		/** Sleep in bounded slices so shutdown never waits for a full backoff. */
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
	 * Spawn, configure and install one worker. Any previous worker is stopped
	 * and reaped first, so at most one live child exists at any time even when
	 * a caller skips the supervisor's reap step.
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

	void Daemon_Singleton::setDataDir (std::string_view path)
	{
		if (!path.empty ()) DaemonDataDir = path;
	}

	bool Daemon_Singleton::init (int argc, char * argv[])
	{
		return init (argc, argv, nullptr);
	}

	bool Daemon_Singleton::init (int argc, char * argv[], std::shared_ptr<std::ostream> logstream)
	{
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
		// A datadir in the configuration is not available during config lookup.
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
		// daemon=true directive that a local administrator may have left in the
		// configuration file.
		if (foreground) isDaemon = false;

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
		if (logclftime) i2p::log::Logger ().SetTimeFormat ("[%d/%b/%Y:%H:%M:%S %z]");

#if defined(WIN32_APP)
		// The Windows GUI logs to a file
		logs = "file";
#else
		if (isDaemon && (logs == "" || logs == "stdout")) logs = "file";
#endif

#if !defined(_WIN32) && !defined(I2PD_NO_PRIVSEP)
		bool privsepEnabled = false, privsepLogEnabled = false;
		i2p::config::GetOption ("privsep.enabled", privsepEnabled);
		i2p::config::GetOption ("privsep.log", privsepLogEnabled);
		d->privsepLog = privsepEnabled && privsepLogEnabled && !logstream;
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
				i2p::log::Logger ().SendTo (logfile);
#if !defined(_WIN32) && !defined(I2PD_NO_PRIVSEP)
		}
		else if (logs == "syslog")
		{
			LogPrint (eLogInfo, "Log: Sending messages to syslog");
			if (d->privsepLog)
				d->privsepSyslog = true;
			else
				i2p::log::Logger ().SendTo ("open-i2pd", LOG_DAEMON);
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
		// Keep startup-only promises until fork/setsid/setrlimit and unveil.
		// A custom policy is additive at this phase too: it may request more
		// promises, but it can never remove proc/unveil or the fixed baseline
		// needed before the runtime sandbox is installed.
		bool usePledge;
		i2p::config::GetOption ("openbsd.pledge_enabled", usePledge);
		std::string policy;
		i2p::config::GetOption ("openbsd.pledge_file", policy);
		if (usePledge)
		{
			std::string promises = "stdio rpath wpath cpath fattr inet dns unix flock proc";
			// The supervisor/log-worker pair is spawned before the runtime
			// sandbox. Keep exec only for that startup window; the runtime
			// policy in UnixDaemon::start() deliberately omits proc and exec.
			if (privsepEnabled && privsepLogEnabled) promises += " exec";
			bool useUnveil;
			i2p::config::GetOption ("openbsd.unveil_enabled", useUnveil);
			if (useUnveil) promises += " unveil";
			bool upnp;
			i2p::config::GetOption ("upnp.enabled", upnp);
			if (upnp) promises += " mcast";
			if (!policy.empty ())
			{
				std::ifstream file (policy);
				if (!file)
				{
					std::cerr << "Daemon: can't open openbsd.pledge_file: " << policy << std::endl;
					std::exit (EXIT_FAILURE);
				}
				std::string line;
				while (std::getline (file, line))
				{
					auto comment = line.find ('#');
					if (comment != std::string::npos) line.resize (comment);
					std::istringstream tokens (line);
					std::string token;
					while (tokens >> token)
					{
						promises.push_back (' ');
						promises += token;
					}
				}
				if (file.bad ())
				{
					std::cerr << "Daemon: can't read openbsd.pledge_file: " << policy << std::endl;
					std::exit (EXIT_FAILURE);
				}
			}
			if (pledge (promises.c_str (), nullptr) == -1)
			{
				std::cerr << "Daemon: startup pledge: " << strerror (errno) << std::endl;
				std::exit (EXIT_FAILURE);
			}
		}
#endif
		i2p::crypto::InitCrypto (precomputation);

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
			i2p::data::InstallRouterPersistenceWriter ([] (auto, auto) { return false; });
#if !defined(_WIN32) && !defined(__ANDROID__) && !defined(I2PD_NO_PRIVSEP)
			std::string error;
			d->routerStorage = std::make_shared<i2p::privsep::RouterStorageClient> ();
			if (!d->routerStorage->Start (i2p::fs::GetDataDir (), error))
			{
				LogPrint (eLogError, "Daemon: Required router storage failed: ", error);
				return false;
			}
			i2p::data::InstallRouterPersistenceWriter ([worker = d->routerStorage] (auto file, auto bytes)
				{ return worker->Write (file, bytes); });
#else
			LogPrint (eLogError, "Daemon: Router storage worker unavailable on this platform");
			return false;
#endif
		}
		else i2p::data::InstallRouterPersistenceWriter ({});

		i2p::context.Init ();

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
				std::cout << unit;
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

		i2p::i18n::SetLanguage ("english");

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
		if (d->tuiServer)
		{
			LogPrint (eLogInfo, "Daemon: Stopping TUI IPC");
			d->tuiServer->Stop ();
			d->tuiServer = nullptr;
		}
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
