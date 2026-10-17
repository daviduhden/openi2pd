/*
* Copyright (c) 2013-2026, The PurpleI2P Project
*
* This file is part of Open I2Pd and licensed under BSD3
*
* See full license text in LICENSE file at top of project tree
*/

#include <thread>
#include <memory>
#include <regex>

#ifdef __OpenBSD__
#include <unistd.h>
#include <limits.h>
#include <stdexcept>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#endif

#include "Daemon.h"

#include "Config.h"
#include "Log.h"
#include "FS.h"
#include "CPU.h"
#include "Base.h"
#include "version.h"
#include "Transports.h"
#include "RouterInfo.h"
#include "RouterContext.h"
#include "Tunnel.h"
#include "HTTP.h"
#include "NetDb.hpp"
#include "Garlic.h"
#include "Streaming.h"
#include "Destination.h"
#include "I2PControl.h"
#ifndef _WIN32
#include "TuiServer.h"
#include "privsep/LogWorker.h"
#include "privsep/Worker.h"
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
	static std::string AbsolutePath (const std::string& path)
	{
		if (path.empty () || path.front () == '/') return path;
		char cwd[PATH_MAX];
		if (!getcwd (cwd, sizeof (cwd)))
			throw std::runtime_error ("Daemon: Can't resolve working directory");
		return std::string (cwd) + "/" + path;
	}
#endif

	class Daemon_Singleton::Daemon_Singleton_Private
	{
	public:
		Daemon_Singleton_Private() {};
		~Daemon_Singleton_Private() {};

#ifndef _WIN32
		std::unique_ptr<i2p::tui::Server> tuiServer;
		std::unique_ptr<i2p::privsep::LogSink> logSink;
		std::unique_ptr<i2p::privsep::ChildProcess> logWorker;
		bool privsepLog = false;
		bool privsepSyslog = false;
		std::string privsepLogPath;
#endif
		std::unique_ptr<i2p::client::I2PControlService> m_I2PControlService;
		std::unique_ptr<i2p::transport::UPnP> UPnP;
		std::unique_ptr<i2p::util::NTPTimeSync> m_NTPSync;
	};

	Daemon_Singleton::Daemon_Singleton() : isDaemon(false), running(true), d(*new Daemon_Singleton_Private()) {}
	Daemon_Singleton::~Daemon_Singleton() {
		delete &d;
	}

	bool Daemon_Singleton::IsService () const
	{
		bool service = false;
		i2p::config::GetOption("service", service);
		return service;
	}

	void Daemon_Singleton::setDataDir(std::string_view path)
	{
		if (!path.empty ())
			DaemonDataDir = path;
	}

	bool Daemon_Singleton::init(int argc, char* argv[]) {
		return init(argc, argv, nullptr);
	}

	bool Daemon_Singleton::init(int argc, char* argv[], std::shared_ptr<std::ostream> logstream)
	{
		i2p::config::Init();
		i2p::config::ParseCmdline(argc, argv);

		std::string config; i2p::config::GetOption("conf", config);
		std::string datadir;
		if(DaemonDataDir != "") {
			datadir = DaemonDataDir;
		} else {
			i2p::config::GetOption("datadir", datadir);
		}

		i2p::fs::DetectDataDir(datadir, IsService());
#ifdef __OpenBSD__
		i2p::fs::DetectDataDir (AbsolutePath (i2p::fs::GetDataDir ()), IsService ());
#else
		i2p::fs::Init();
#endif

		datadir = i2p::fs::GetDataDir();

		if (config == "")
		{
			config = i2p::fs::DataDirPath("i2pd.conf");
			if (!i2p::fs::Exists (config)) {
				// use i2pd.conf only if exists
				config = ""; /* reset */
			}
		}

		i2p::config::ParseConfig(config);
		i2p::config::Finalize();

#ifdef __OpenBSD__
		// A datadir in the configuration is not available during config lookup.
		std::string configuredDataDir = DaemonDataDir;
		if (configuredDataDir.empty ())
			i2p::config::GetOption ("datadir", configuredDataDir);
		i2p::fs::DetectDataDir (configuredDataDir, IsService ());
		i2p::fs::DetectDataDir (AbsolutePath (i2p::fs::GetDataDir ()), IsService ());
		datadir = i2p::fs::GetDataDir ();
		i2p::fs::Init ();
		i2p::config::SetOption ("conf", AbsolutePath (config));
		for (const char * option : {"tunconf", "tunnelsdir", "certsdir", "logfile", "pidfile",
			"reseed.file", "reseed.zipfile", "addressbook.hostsfile",
			"tui.socket", "openbsd.pledge_file", "openbsd.unveil_file"})
		{
			std::string path;
			i2p::config::GetOption (option, path);
			if (!path.empty ()) i2p::config::SetOption (option, AbsolutePath (path));
		}
#endif

		i2p::config::GetOption("daemon", isDaemon);

		std::string certsdir; i2p::config::GetOption("certsdir", certsdir);
		i2p::fs::SetCertsDir(certsdir);

		certsdir = i2p::fs::GetCertsDir();

		std::string logs     = ""; i2p::config::GetOption("log",        logs);
		std::string logfile  = ""; i2p::config::GetOption("logfile",    logfile);
		std::string loglevel = ""; i2p::config::GetOption("loglevel",   loglevel);
		bool logclftime;           i2p::config::GetOption("logclftime", logclftime);

		/* setup logging */
		if (logclftime)
			i2p::log::Logger().SetTimeFormat ("[%d/%b/%Y:%H:%M:%S %z]");

#if defined(WIN32_APP)
		// The Windows GUI logs to a file
		logs = "file";
#else
		if (isDaemon && (logs == "" || logs == "stdout"))
			logs = "file";
#endif

#ifndef _WIN32
		bool privsepEnabled = false, privsepLogEnabled = false;
		i2p::config::GetOption ("privsep.enabled", privsepEnabled);
		i2p::config::GetOption ("privsep.log", privsepLogEnabled);
		d.privsepLog = privsepEnabled && privsepLogEnabled && !logstream;
#endif

		i2p::log::Logger().SetLogLevel(loglevel);
		if (logstream) {
			LogPrint(eLogInfo, "Log: Sending messages to std::ostream");
			i2p::log::Logger().SendTo (logstream);
		} else if (logs == "file") {
			if (logfile == "")
				logfile = i2p::fs::DataDirPath("i2pd.log");
#ifdef __OpenBSD__
			i2p::config::SetOption ("logfile", logfile);
#endif
			LogPrint(eLogInfo, "Log: Sending messages to ", logfile);
#ifndef _WIN32
			if (d.privsepLog) d.privsepLogPath = logfile;
			else
#endif
				i2p::log::Logger().SendTo (logfile);
#ifndef _WIN32
		} else if (logs == "syslog") {
			LogPrint(eLogInfo, "Log: Sending messages to syslog");
			if (d.privsepLog) d.privsepSyslog = true;
			else
				i2p::log::Logger().SendTo("open-i2pd", LOG_DAEMON);
#endif
		} else {
			// use stdout -- default
		}

		LogPrint(eLogNone,  "Open I2Pd v", VERSION, " (", I2P_VERSION, ") starting...");
		LogPrint(eLogDebug, "FS: Main config file: ", config);
		LogPrint(eLogDebug, "FS: Data directory: ", datadir);
		LogPrint(eLogDebug, "FS: Certificates directory: ", certsdir);

		bool precomputation; i2p::config::GetOption("precomputation.elgamal", precomputation);
		bool ssu; i2p::config::GetOption("ssu", ssu);
		if (!ssu && i2p::config::IsDefault ("precomputation.elgamal"))
			precomputation = false; // we don't elgamal table if no ssu, unless it's specified explicitly
#ifdef __OpenBSD__
		// Keep startup-only promises until fork/setsid/setrlimit and unveil.
		// Explicit pledge policies retain their existing override semantics.
		bool usePledge; i2p::config::GetOption ("openbsd.pledge_enabled", usePledge);
		std::string policy; i2p::config::GetOption ("openbsd.pledge_file", policy);
		if (usePledge && policy.empty ())
		{
			std::string promises = "stdio rpath wpath cpath fattr inet dns unix flock proc";
			bool useUnveil; i2p::config::GetOption ("openbsd.unveil_enabled", useUnveil);
			if (useUnveil) promises += " unveil";
			bool upnp; i2p::config::GetOption ("upnp.enabled", upnp);
			if (upnp) promises += " mcast";
			if (pledge (promises.c_str (), nullptr) == -1)
			{
				std::cerr << "Daemon: startup pledge: " << strerror (errno) << std::endl;
				std::exit (EXIT_FAILURE);
			}
		}
#endif
		i2p::crypto::InitCrypto (precomputation);

		i2p::transport::InitAddressFromIface (); // get address4/6 from interfaces

		int netID; i2p::config::GetOption("netid", netID);
		i2p::context.SetNetID (netID);

		bool checkReserved; i2p::config::GetOption("reservedrange", checkReserved);
		i2p::transport::transports.SetCheckReserved(checkReserved);

		i2p::context.Init ();

		i2p::transport::InitTransports ();

		bool isFloodfill; i2p::config::GetOption("floodfill", isFloodfill);
		if (isFloodfill)
		{
			LogPrint(eLogInfo, "Daemon: Router configured as floodfill");
			i2p::context.SetFloodfill (true);
		}
		else
			i2p::context.SetFloodfill (false);

		bool transit; i2p::config::GetOption("notransit", transit);
		i2p::context.SetAcceptsTunnels (!transit);
		uint32_t transitTunnels; i2p::config::GetOption("limits.transittunnels", transitTunnels);
		if (transitTunnels < 2)
			transitTunnels = 2;
		if (isFloodfill && i2p::config::IsDefault ("limits.transittunnels"))
			transitTunnels *= 2; // double default number of transit tunnels for floodfill
		i2p::tunnel::tunnels.SetMaxNumTransitTunnels (transitTunnels);

		/* this section also honors 'floodfill' flag, if set above */
		std::string bandwidth; i2p::config::GetOption("bandwidth", bandwidth);
		if (bandwidth.length () > 0)
		{
			const auto NumBandwithRegex = std::regex(R"(^\d+$)");
			const auto BandwithRegex = std::regex(R"((\d+)(b|kb|mb|gb))");
			std::smatch bandWithMatch;

			if (bandwidth.length () == 1 && ((bandwidth[0] >= 'K' && bandwidth[0] <= 'P') || bandwidth[0] == 'X' ))
			{
				i2p::context.SetBandwidth (bandwidth[0]);
				LogPrint(eLogInfo, "Daemon: Bandwidth set to ", i2p::context.GetBandwidthLimit (), "KBps");
			}
			else if (std::regex_match(bandwidth, bandWithMatch, BandwithRegex))
			{
				const auto number = bandWithMatch[1].str();
				const auto unit   = bandWithMatch[2].str();
				int limit = std::atoi(number.c_str());
				std::cout << unit;
				if (unit == "b")
				{
					limit /= 1000;
				}
				else if(unit == "mb")
				{
					limit *= 1000;
				} else if(unit == "gb")
				{
					limit *= 1000000;
				}
				// if limit more than 32 bits then its will be negative
				if (limit < 0)
				{
					LogPrint(eLogInfo, "Daemon: Unexpected bandwidth ", bandwidth, ". Set to 'low'");
					i2p::context.SetBandwidth (i2p::data::CAPS_FLAG_LOW_BANDWIDTH2);
				} else {
					i2p::context.SetBandwidth(limit);
				}
			}
			else if(std::regex_search(bandwidth, NumBandwithRegex))
			{
				auto value = std::atoi(bandwidth.c_str());
				if (value > 0)
				{
					i2p::context.SetBandwidth (value);
					LogPrint(eLogInfo, "Daemon: Bandwidth set to ", i2p::context.GetBandwidthLimit (), " KBps");
				}
				else
				{
					LogPrint(eLogInfo, "Daemon: Unexpected bandwidth ", bandwidth, ". Set to 'low'");
					i2p::context.SetBandwidth (i2p::data::CAPS_FLAG_LOW_BANDWIDTH2);
				}
			}
		}
		else if (isFloodfill)
		{
			LogPrint(eLogInfo, "Daemon: Floodfill bandwidth set to 'extra'");
			i2p::context.SetBandwidth (i2p::data::CAPS_FLAG_EXTRA_BANDWIDTH2);
		}
		else
		{
#if IS_X86_64
			if (std::thread::hardware_concurrency() >= 4) // quad core or more
			{
				LogPrint(eLogInfo, "Daemon: bandwidth set to 'extra'");
				i2p::context.SetBandwidth (i2p::data::CAPS_FLAG_EXTRA_BANDWIDTH1);
			}
			else
			{
				LogPrint(eLogInfo, "Daemon: bandwidth set to 'high'");
				i2p::context.SetBandwidth (i2p::data::CAPS_FLAG_HIGH_BANDWIDTH);
			}
#else
			LogPrint(eLogInfo, "Daemon: bandwidth set to 'low'");
			i2p::context.SetBandwidth (i2p::data::CAPS_FLAG_LOW_BANDWIDTH2);
#endif
		}

		int shareRatio; i2p::config::GetOption("share", shareRatio);
		i2p::context.SetShareRatio (shareRatio);

		std::string family; i2p::config::GetOption("family", family);
		i2p::context.SetFamily (family);
		if (family.length () > 0)
			LogPrint(eLogInfo, "Daemon: Router family set to ", family);

		bool trust; i2p::config::GetOption("trust.enabled", trust);
		if (trust)
		{
			LogPrint(eLogInfo, "Daemon: Explicit trust enabled");
			std::string f; i2p::config::GetOption("trust.family", f); std::string_view fam(f);
			std::string routers; i2p::config::GetOption("trust.routers", routers);
			bool restricted = false;
			if (fam.length() > 0)
			{
				std::vector<std::string_view> fams;
				size_t pos = 0, comma;
				do
				{
					comma = fam.find (',', pos);
					fams.push_back (fam.substr (pos, comma != std::string::npos ? comma - pos : std::string::npos));
					pos = comma + 1;
				}
				while (comma != std::string::npos);
				i2p::transport::transports.RestrictRoutesToFamilies(fams);
				restricted = fams.size() > 0;
			}
			if (!routers.empty ())
			{
				auto idents = i2p::data::ExtractIdentHashes (routers);
				LogPrint(eLogInfo, "Daemon: Setting restricted routes to use ", idents.size(), " trusted routers");
				i2p::transport::transports.RestrictRoutesToRouters(idents);
				restricted = idents.size() > 0;
			}
			if(!restricted)
				LogPrint(eLogError, "Daemon: No trusted routers of families specified");
			bool hidden; i2p::config::GetOption("trust.hidden", hidden);
			if (hidden)
			{
				LogPrint(eLogInfo, "Daemon: Hidden mode enabled");
				i2p::context.SetHidden(true);
			}
		}

		i2p::i18n::SetLanguage("english");

		return true;
	}

	bool Daemon_Singleton::start()
	{
#ifndef _WIN32
		if (d.privsepLog)
		{
			auto spawned = i2p::privsep::SpawnLogWorker ();
			if (!spawned)
			{
				std::cerr << "Daemon: cannot spawn privilege-separated log worker: "
					<< spawned.error () << std::endl;
				return false;
			}
			d.logWorker = std::make_unique<i2p::privsep::ChildProcess> (std::move (*spawned));
			d.logSink = std::make_unique<i2p::privsep::LogSink> (
				std::move (d.logWorker->ChannelFd ()));
			auto configured = d.logSink->Configure (d.privsepSyslog, d.privsepLogPath, "open-i2pd");
			if (!configured)
			{
				std::cerr << "Daemon: cannot configure log worker: " << configured.error () << std::endl;
				d.logSink.reset ();
				d.logWorker.reset ();
				return false;
			}
			auto* sink = d.logSink.get ();
			i2p::log::Logger ().SetSink ([sink] (LogLevel level, std::time_t timestamp,
				std::string_view text) { sink->Write (static_cast<uint8_t> (level), timestamp, text); });
		}
#endif
		i2p::log::Logger().Start();
		LogPrint(eLogInfo, "Daemon: Starting NetDB");
		i2p::data::netdb.Start();

		bool upnp; i2p::config::GetOption("upnp.enabled", upnp);
		if (upnp) {
			d.UPnP = std::unique_ptr<i2p::transport::UPnP>(new i2p::transport::UPnP);
			d.UPnP->Start ();
		}

		bool nettime; i2p::config::GetOption("nettime.enabled", nettime);
		if (nettime)
		{
			d.m_NTPSync = std::unique_ptr<i2p::util::NTPTimeSync>(new i2p::util::NTPTimeSync);
			d.m_NTPSync->Start ();
		}

		bool ntcp2; i2p::config::GetOption("ntcp2.enabled", ntcp2);
		bool ssu2; i2p::config::GetOption("ssu2.enabled", ssu2);
		LogPrint(eLogInfo, "Daemon: Starting Transports");
		if(!ssu2) LogPrint(eLogInfo, "Daemon: SSU2 disabled");
		if(!ntcp2) LogPrint(eLogInfo, "Daemon: NTCP2 disabled");

		i2p::transport::transports.Start(ntcp2, ssu2);
		if (i2p::transport::transports.IsBoundSSU2() || i2p::transport::transports.IsBoundNTCP2())
			LogPrint(eLogInfo, "Daemon: Transports started");
		else
		{
			LogPrint(eLogCritical, "Daemon: Failed to start Transports");
			/** shut down netdb right away */
			i2p::transport::transports.Stop();
			i2p::data::netdb.Stop();
			return false;
		}

		LogPrint(eLogInfo, "Daemon: Starting Tunnels");
		i2p::tunnel::tunnels.Start();

		LogPrint(eLogInfo, "Daemon: Starting Router context");
		i2p::context.Start();

		LogPrint(eLogInfo, "Daemon: Starting Client");
		i2p::client::context.Start ();

#ifndef _WIN32
		bool tuiEnabled; i2p::config::GetOption ("tui.enabled", tuiEnabled);
		if (tuiEnabled)
		{
			std::string socketPath; i2p::config::GetOption ("tui.socket", socketPath);
			if (socketPath.empty ())
				socketPath = i2p::fs::DataDirPath ("i2pd-tui.sock");
			// The TUI is optional: a failure to create its local socket must
			// never prevent the router from running headless.
			d.tuiServer = std::make_unique<i2p::tui::Server> (socketPath);
			if (!d.tuiServer->Start ())
			{
				LogPrint (eLogWarning, "Daemon: TUI IPC disabled, router continues without it");
				d.tuiServer.reset ();
			}
		}
#endif

		// I2P Control Protocol
		bool i2pcontrol; i2p::config::GetOption("i2pcontrol.enabled", i2pcontrol);
		if (i2pcontrol) {
			std::string i2pcpAddr; i2p::config::GetOption("i2pcontrol.address", i2pcpAddr);
			uint16_t    i2pcpPort; i2p::config::GetOption("i2pcontrol.port",    i2pcpPort);
			LogPrint(eLogInfo, "Daemon: Starting I2PControl at ", i2pcpAddr, ":", i2pcpPort);
			try
			{
				d.m_I2PControlService = std::unique_ptr<i2p::client::I2PControlService>(new i2p::client::I2PControlService (i2pcpAddr, i2pcpPort));
				d.m_I2PControlService->Start ();
			}
			catch (std::exception& ex)
			{
				LogPrint (eLogCritical, "Daemon: Failed to start I2PControl: ", ex.what ());
				ThrowFatal ("Unable to start I2PControl service at ", i2pcpAddr, ":", i2pcpPort, ": ", ex.what ());
			}
		}
		return true;
	}

	bool Daemon_Singleton::stop()
	{
		LogPrint(eLogInfo, "Daemon: Shutting down");
		// These two own threads and read router state, so they must stop
		// before the subsystems they observe are torn down.
#ifndef _WIN32
		if (d.tuiServer) {
			LogPrint(eLogInfo, "Daemon: Stopping TUI IPC");
			d.tuiServer->Stop();
			d.tuiServer = nullptr;
		}
#endif
		if (d.m_I2PControlService)
		{
			LogPrint(eLogInfo, "Daemon: Stopping I2PControl");
			d.m_I2PControlService->Stop ();
			d.m_I2PControlService = nullptr;
		}
		LogPrint(eLogInfo, "Daemon: Stopping Client");
		i2p::client::context.Stop();
		LogPrint(eLogInfo, "Daemon: Stopping Router context");
		i2p::context.Stop();
		LogPrint(eLogInfo, "Daemon: Stopping Tunnels");
		i2p::tunnel::tunnels.Stop();

		if (d.UPnP)
		{
			d.UPnP->Stop ();
			d.UPnP = nullptr;
		}

		if (d.m_NTPSync)
		{
			d.m_NTPSync->Stop ();
			d.m_NTPSync = nullptr;
		}

		LogPrint(eLogInfo, "Daemon: Stopping Transports");
		i2p::transport::transports.Stop();
		LogPrint(eLogInfo, "Daemon: Stopping NetDB");
		i2p::data::netdb.Stop();
		i2p::crypto::TerminateCrypto ();
		i2p::log::Logger().Stop();
#ifndef _WIN32
		if (d.logSink)
		{
			(void) d.logSink->Shutdown ();
			d.logSink.reset ();
		}
		if (d.logWorker)
		{
			d.logWorker->Stop ();
			d.logWorker.reset ();
		}
#endif

		return true;
	}

	static void ShowUptime (std::stringstream& s, int seconds)
	{
		int num;

		if ((num = seconds / 86400) > 0) {
			s << num << " days, ";
			seconds -= num * 86400;
		}
		if ((num = seconds / 3600) > 0) {
			s << num << " hours, ";
			seconds -= num * 3600;
		}
		if ((num = seconds / 60) > 0) {
			s << num << " min, ";
			seconds -= num * 60;
		}
		s << seconds << " seconds\n";
	}

	static void ShowTransfered (std::stringstream& s, size_t transfer)
	{
		auto bytes = transfer & 0x03ff;
		transfer >>= 10;
		auto kbytes = transfer & 0x03ff;
		transfer >>= 10;
		auto mbytes = transfer & 0x03ff;
		transfer >>= 10;
		auto gbytes = transfer;

		if (gbytes)
			s << gbytes << " GB, ";
		if (mbytes)
			s << mbytes << " MB, ";
		if (kbytes)
			s << kbytes << " KB, ";
		s << bytes << " Bytes\n";
	}

	static void ShowNetworkStatus (std::stringstream& s, RouterStatus status, bool testing, RouterError error)
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
		if (testing)
			s << " (Test)";
		if (error != eRouterErrorNone)
		{
			switch (error)
			{
				case eRouterErrorClockSkew:
					s << " - " << tr("Clock skew");
				break;
				case eRouterErrorOffline:
					s << " - " << tr("Offline");
				break;
				case eRouterErrorSymmetricNAT:
					s << " - " << tr("Symmetric NAT");
				break;
				case eRouterErrorFullConeNAT:
					s << " - " << tr("Full cone NAT");
				break;
				case eRouterErrorNoDescriptors:
					s << " - " << tr("No Descriptors");
				break;
				default: ;
			}
		}
	}

	void PrintMainWindowText (std::stringstream& s)
	{
		s << "\n";
		s << "Status: ";
		ShowNetworkStatus (s, i2p::context.GetStatus (), i2p::context.GetTesting(), i2p::context.GetError ());
		if (i2p::context.SupportsV6 ())
		{
			s << " / ";
			ShowNetworkStatus (s, i2p::context.GetStatusV6 (), i2p::context.GetTestingV6(), i2p::context.GetErrorV6 ());
		}
		s << "; ";
		s << "Caps: " << i2p::context.GetRouterInfo().GetProperty("caps") << "; ";
		s << "Success Rate: " << i2p::tunnel::tunnels.GetTunnelCreationSuccessRate() << "%\n";
		s << "Uptime: "; ShowUptime(s, i2p::context.GetUptime ());
		auto gracefulTimeLeft = Daemon.GetGracefulShutdownInterval ();
		if (gracefulTimeLeft > 0)
		{
			s << "Graceful shutdown, time left: "; ShowUptime(s, gracefulTimeLeft);
		}
		else
			s << "\n";
		s << "Inbound: " << i2p::transport::transports.GetInBandwidth() / 1024 << " KiB/s; ";
		s << "Outbound: " << i2p::transport::transports.GetOutBandwidth() / 1024 << " KiB/s\n";
		s << "Received: "; ShowTransfered (s, i2p::transport::transports.GetTotalReceivedBytes());
		s << "Sent: "; ShowTransfered (s, i2p::transport::transports.GetTotalSentBytes());
		s << "\n";
		s << "Routers: " << i2p::data::netdb.GetNumRouters () << "; ";
		s << "Floodfills: " << i2p::data::netdb.GetNumFloodfills () << "; ";
		s << "LeaseSets: " << i2p::data::netdb.GetNumLeaseSets () << "\n";
		s << "Tunnels: ";
		s << "In: " << i2p::tunnel::tunnels.CountInboundTunnels() << "; ";
		s << "Out: " << i2p::tunnel::tunnels.CountOutboundTunnels() << "; ";
		s << "Transit: " << i2p::tunnel::tunnels.CountTransitTunnels() << "\n";
		s << "\n";
	}
}
}
