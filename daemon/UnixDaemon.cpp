/*
* Copyright (c) 2013-2025, The PurpleI2P Project
*
* This file is part of Open I2Pd and licensed under BSD3
*
* See full license text in LICENSE file at top of project tree
*/

#include "Daemon.h"

#ifndef _WIN32

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <thread>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/resource.h>

#ifdef __OpenBSD__
#include <fstream>
#include <vector>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <limits.h>
#include <map>
#include <syslog.h>
#include <pthread.h>
#endif

#include "Config.h"
#include "FS.h"
#include "Log.h"
#include "Tunnel.h"
#include "RouterContext.h"
#include "ClientContext.h"
#include "Transports.h"
#include "util.h"

#ifdef __OpenBSD__
// Blocked before service threads are created; consumed only by run().
static sigset_t daemonSignals;

static bool SignalError (const char * operation, int error)
{
	std::cerr << "Daemon: " << operation << ": " << strerror (error) << std::endl;
	syslog (LOG_DAEMON | LOG_ERR, "Daemon: %s: %s", operation, strerror (error));
	return false;
}
#endif

const int GRACEFUL_SHUTDOWN_INTERVAL = 10*60; // in seconds

// a handler may only touch volatile sig_atomic_t, everything else is done by the daemon loop
static volatile sig_atomic_t g_PendingSignals[NSIG] = {};

void handle_signal(int sig)
{
	int savedErrno = errno;
	if (sig > 0 && sig < NSIG)
		g_PendingSignals[sig] = 1;
	errno = savedErrno;
}

// Remove the PID file only if the path still names the inode we locked.
// A stale path may have been replaced by a symlink or an unrelated file
// while the daemon was starting or shutting down.
static bool RemovePidFile (int fd, const std::string& path)
{
	if (fd < 0 || path.empty ()) return false;
	struct stat opened {}, named {};
	if (fstat (fd, &opened) != 0 || lstat (path.c_str (), &named) != 0) return false;
	if (!S_ISREG (opened.st_mode) || !S_ISREG (named.st_mode) ||
		opened.st_dev != named.st_dev || opened.st_ino != named.st_ino)
		return false;
	return unlink (path.c_str ()) == 0;
}

static void HandlePendingSignals()
{
	for (int sig = 1; sig < NSIG; sig++)
	{
		if (!g_PendingSignals[sig]) continue;
		g_PendingSignals[sig] = 0;
		switch (sig)
		{
			case SIGHUP:
				LogPrint(eLogInfo, "Daemon: Got SIGHUP, reopening tunnel configuration...");
				i2p::client::context.ReloadConfig();
			break;
			case SIGUSR1:
				LogPrint(eLogInfo, "Daemon: Got SIGUSR1, reopening logs...");
				i2p::log::Logger().Reopen ();
			break;
			case SIGINT:
				if (i2p::context.AcceptsTunnels () && !Daemon.gracefulShutdownInterval)
				{
					i2p::context.SetAcceptsTunnels (false);
					Daemon.gracefulShutdownInterval = GRACEFUL_SHUTDOWN_INTERVAL;
					LogPrint(eLogInfo, "Graceful shutdown after ", Daemon.gracefulShutdownInterval, " seconds");
				}
				else
					Daemon.running = 0;
			break;
			case SIGABRT:
			case SIGTERM:
				Daemon.running = 0; // Exit loop
			break;
			case SIGPIPE:
				LogPrint(eLogInfo, "SIGPIPE received");
			break;
			case SIGTSTP:
				LogPrint(eLogInfo, "Daemon: Got SIGTSTP, disconnecting from network...");
				i2p::transport::transports.SetOnline(false);
			break;
			case SIGCONT:
				LogPrint(eLogInfo, "Daemon: Got SIGCONT, restoring connection to network...");
				i2p::transport::transports.SetOnline(true);
			break;
		}
	}
}

namespace i2p
{
	namespace util
	{
		bool DaemonUnix::start()
		{
			pidFH = -1; // no PID lock yet
			if (isDaemon)
			{
				pid_t pid;
				pid = fork();
				if (pid > 0) // parent
				{
					// Do not run atexit handlers or flush duplicated buffers in
					// the parent after fork; the child owns the service state.
					_exit (EXIT_SUCCESS);
				}

				if (pid < 0) // error
				{
					LogPrint(eLogError, "Daemon: Could not fork: ", strerror(errno));
					std::cerr << "i2pd: Could not fork: " << strerror(errno) << std::endl;
					return false;
				}

				// child
				umask(S_IWGRP | S_IRWXO); // 0027
				int sid = setsid();
				if (sid < 0)
				{
					LogPrint(eLogError, "Daemon: Could not create process group.");
					std::cerr << "i2pd: Could not create process group." << std::endl;
					return false;
				}
				std::string d = i2p::fs::GetDataDir();
				if (chdir(d.c_str()) != 0)
				{
					LogPrint(eLogError, "Daemon: Could not chdir: ", strerror(errno));
					std::cerr << "i2pd: Could not chdir: " << strerror(errno) << std::endl;
					return false;
				}

				// point std{in,out,err} descriptors to /dev/null
				if (!freopen("/dev/null", "r", stdin) ||
					!freopen("/dev/null", "w", stdout) ||
					!freopen("/dev/null", "w", stderr))
				{
					LogPrint(eLogError, "Daemon: Could not redirect standard descriptors: ", strerror(errno));
					return false;
				}
			}

			// set proc limits
			struct rlimit limit;
			uint16_t nfiles; i2p::config::GetOption("limits.openfiles", nfiles);
			if (getrlimit(RLIMIT_NOFILE, &limit) != 0)
			{
				LogPrint(eLogError, "Daemon: Can't get max number of open files: ", strerror(errno));
			}
			else if (nfiles == 0) {
				LogPrint(eLogInfo, "Daemon: Using system limit in ", limit.rlim_cur, " max open files");
			} else if (nfiles <= limit.rlim_max) {
				limit.rlim_cur = nfiles;
				if (setrlimit(RLIMIT_NOFILE, &limit) == 0) {
					LogPrint(eLogInfo, "Daemon: Set max number of open files to ",
						nfiles, " (system limit is ", limit.rlim_max, ")");
				} else {
					LogPrint(eLogError, "Daemon: Can't set max number of open files: ", strerror(errno));
				}
			} else {
				LogPrint(eLogError, "Daemon: limits.openfiles exceeds system limit: ", limit.rlim_max);
			}
			uint32_t cfsize; i2p::config::GetOption("limits.coresize", cfsize);
			if (cfsize) // core file size set
			{
				uint64_t coreSize = (uint64_t)cfsize * 1024;
				if (getrlimit(RLIMIT_CORE, &limit) != 0)
					LogPrint(eLogError, "Daemon: Can't get max size of coredump: ", strerror(errno));
				else if (coreSize <= limit.rlim_max) {
					limit.rlim_cur = coreSize;
					if (setrlimit(RLIMIT_CORE, &limit) != 0) {
						LogPrint(eLogError, "Daemon: Can't set max size of coredump: ", strerror(errno));
					} else {
						LogPrint(eLogInfo, "Daemon: Set max size of core files to ", cfsize, "Kb");
					}
				} else {
					LogPrint(eLogError, "Daemon: limits.coresize exceeds system limit: ", limit.rlim_max);
				}
			}

			// Pidfile
			// this code is c-styled and a bit ugly, but we need fd for locking pidfile

			// Close and remove the PID file if startup fails after locking it.
			auto abortStart = [this]() {
				// If startup failed after Log::Start(), join the logger thread
				// here. Log::~Log() deletes a joinable std::thread, which would
				// call std::terminate() during exit.
				i2p::log::Logger().Stop();
				if (pidFH >= 0)
				{
					// Keep the descriptor open until the identity check is done.
					RemovePidFile (pidFH, pidfile);
					close(pidFH);
					pidFH = -1;
				}
				return false;
			};

			i2p::config::GetOption("pidfile", pidfile);
			if (pidfile == "") {
				pidfile = i2p::fs::DataDirPath("i2pd.pid");
			}
			if (pidfile != "") {
				int flags = O_RDWR | O_CREAT | O_CLOEXEC;
#ifdef O_NOFOLLOW
				// Do not follow a symlink planted in a writable directory.
				flags |= O_NOFOLLOW;
#endif
				pidFH = open(pidfile.c_str(), flags, 0600);
				if (pidFH < 0)
				{
					LogPrint(eLogError, "Daemon: Could not create pid file ", pidfile, ": ", strerror(errno));
					std::cerr << "i2pd: Could not create pid file " << pidfile << ": " << strerror(errno) << std::endl;
					return false;
				}

				// An existing file might not have been created by us and might
				// have wider permissions; only a regular file is acceptable.
				struct stat pidStat;
				if (fstat(pidFH, &pidStat) != 0 || !S_ISREG(pidStat.st_mode))
				{
					LogPrint(eLogError, "Daemon: Pid file ", pidfile, " is not a regular file");
					std::cerr << "i2pd: Pid file " << pidfile << " is not a regular file" << std::endl;
					close(pidFH); pidFH = -1;
					return false;
				}
				if (fchmod(pidFH, 0600) != 0)
				{
					LogPrint(eLogError, "Daemon: Can't restrict permissions of pid file ", pidfile, ": ", strerror(errno));
					std::cerr << "i2pd: Can't restrict permissions of pid file " << pidfile << ": " << strerror(errno) << std::endl;
					close(pidFH); pidFH = -1;
					return false;
				}

#ifndef ANDROID
				if (lockf(pidFH, F_TLOCK, 0) != 0)
#else
				struct flock fl;
				fl.l_len = 0;
				fl.l_type = F_WRLCK;
				fl.l_whence = SEEK_SET;
				fl.l_start = 0;

				if (fcntl(pidFH, F_SETLK, &fl) != 0)
#endif
				{
					LogPrint(eLogError, "Daemon: Could not lock pid file ", pidfile, ": ", strerror(errno));
					std::cerr << "i2pd: Could not lock pid file " << pidfile << ": " << strerror(errno) << std::endl;
					// Another instance owns the file, so do not unlink it.
					close(pidFH); pidFH = -1;
					return false;
				}

				char pid[16];
				int pidLen = snprintf(pid, sizeof(pid), "%d\n", getpid());
				if (pidLen <= 0 || pidLen >= (int)sizeof(pid) || ftruncate(pidFH, 0) != 0)
				{
					LogPrint(eLogCritical, "Daemon: Could not write pidfile ", pidfile, ": ", strerror(errno));
					std::cerr << "i2pd: Could not write pidfile " << pidfile << ": " << strerror(errno) << std::endl;
					return abortStart();
				}
				size_t written = 0;
				while (written < (size_t)pidLen)
				{
					ssize_t n = pwrite(pidFH, pid + written, pidLen - written, written);
					if (n < 0 && errno == EINTR) continue;
					if (n <= 0) break;
					written += n;
				}
				if (written != (size_t)pidLen)
				{
					LogPrint(eLogCritical, "Daemon: Could not write pidfile ", pidfile, ": ", strerror(errno));
					std::cerr << "i2pd: Could not write pidfile " << pidfile << ": " << strerror(errno) << std::endl;
					return abortStart();
				}
			}
			gracefulShutdownInterval = 0; // not specified

			// handle signal TSTP
			bool handleTSTP = false; i2p::config::GetOption("unix.handle_sigtstp", handleTSTP);

#ifdef __OpenBSD__
			// ReloadConfig() can stop and join destination threads. Never run
			// it from an asynchronous handler on an arbitrary service thread.
			sigemptyset (&daemonSignals);
			for (int sig : {SIGHUP, SIGUSR1, SIGABRT, SIGTERM, SIGINT, SIGPIPE})
				sigaddset (&daemonSignals, sig);
			if (handleTSTP)
			{
				sigaddset (&daemonSignals, SIGTSTP);
				sigaddset (&daemonSignals, SIGCONT);
			}
			int error = pthread_sigmask (SIG_BLOCK, &daemonSignals, nullptr);
			if (error) { SignalError ("pthread_sigmask", error); return abortStart(); }
			// Inherited SIG_IGN dispositions could discard a blocked signal.
			struct sigaction sa{};
			sa.sa_handler = SIG_DFL;
			sigemptyset (&sa.sa_mask);
			for (int sig = 1; sig < NSIG; ++sig)
				if (sigismember (&daemonSignals, sig) == 1 && sigaction (sig, &sa, nullptr) == -1)
				{
					SignalError ("sigaction", errno);
					return abortStart();
				}
#else
			// Signal handler
			struct sigaction sa{};
			sa.sa_handler = handle_signal;
			sigemptyset(&sa.sa_mask);
			sa.sa_flags = SA_RESTART;
			for (int sig : {SIGHUP, SIGUSR1, SIGABRT, SIGTERM, SIGINT, SIGPIPE})
				if (sigaction(sig, &sa, nullptr) == -1)
				{
					LogPrint(eLogError, "Daemon: Can't install handler for signal ", sig, ": ", strerror(errno));
					return abortStart();
				}
			if (handleTSTP)
			{
				if (sigaction(SIGTSTP, &sa, nullptr) == -1 || sigaction(SIGCONT, &sa, nullptr) == -1)
				{
					LogPrint(eLogError, "Daemon: Can't install handler for job-control signals: ", strerror(errno));
					return abortStart();
				}
			}

#endif

#ifdef __OpenBSD__
			// Resolve/read sandbox policy before the first unveil hides it.
			bool useUnveil, usePledge;
			i2p::config::GetOption ("openbsd.unveil_enabled", useUnveil);
			i2p::config::GetOption ("openbsd.pledge_enabled", usePledge);
			// CreatePrivateFile() uses chmod when saving router and destination keys.
			std::string promises = "stdio rpath wpath cpath fattr inet dns unix flock";
			bool upnp; i2p::config::GetOption ("upnp.enabled", upnp);
			if (upnp) promises += " mcast";
			std::vector<std::string> extraPaths;
			auto fail = [](const std::string& message) {
				std::cerr << message << std::endl;
				// The logger thread has not started, and daemon stderr is /dev/null.
				syslog (LOG_DAEMON | LOG_ERR, "%s", message.c_str ());
				return false;
			};
			// The log worker is created with fork()+execv() from
			// Daemon_Singleton::start(), i.e. after this pledge is applied. The
			// runtime policy deliberately omits "proc" and "exec", so spawning it
			// would abort the process. Refuse instead of silently running without
			// the requested isolation (or silently disabling the sandbox).
			bool privsepEnabled = false, privsepLog = false;
			i2p::config::GetOption ("privsep.enabled", privsepEnabled);
			i2p::config::GetOption ("privsep.log", privsepLog);
			if (privsepEnabled && privsepLog && (usePledge || useUnveil))
			{
				fail ("Daemon: privsep.log requires fork/exec after the OpenBSD sandbox is applied; "
					"set privsep.log = false or disable openbsd.pledge_enabled/openbsd.unveil_enabled");
				return abortStart ();
			}
			auto readPolicy = [&fail](const char * option, std::vector<std::string>& lines) {
				std::string path; i2p::config::GetOption (option, path);
				if (path.empty ()) return true;
				std::ifstream f (path);
				if (!f)
				{
					return fail (std::string ("Daemon: Can't open ") + option + ": " + path);
				}
				std::string line;
				while (std::getline (f, line))
					if (!line.empty ()) lines.push_back (line);
				if (f.bad ())
				{
					return fail (std::string ("Daemon: Can't read ") + option + ": " + path);
				}
				return true;
			};
			if (usePledge)
			{
				std::string policy; i2p::config::GetOption ("openbsd.pledge_file", policy);
				if (!policy.empty ())
				{
					std::vector<std::string> lines;
					if (!readPolicy ("openbsd.pledge_file", lines)) return abortStart();
					promises.clear ();
					for (const auto& line : lines) promises += line + " ";
				}
			}
			if (useUnveil && !readPolicy ("openbsd.unveil_file", extraPaths)) return abortStart();
			if (useUnveil)
			{
				// Coalesce aliases before unveil: repeated rules may only reduce
				// permissions. For example, certsdir may equal datadir.
				std::map<std::string, std::string> rules;
				auto allow = [&rules, &fail](std::string path, const char * permissions) {
					if (path.empty ()) return true;
					while (path.size () > 1 && path.back () == '/') path.pop_back ();
					char resolved[PATH_MAX];
					if (realpath (path.c_str (), resolved)) path = resolved;
					else
					{
						if (errno != ENOENT)
							return fail ("Daemon: resolve " + path + ": " + strerror (errno));
						// A missing leaf is remembered in its existing parent by
						// unveil, without giving access to sibling files.
						auto slash = path.find_last_of ('/');
						std::string parent = slash == std::string::npos ? "." : path.substr (0, slash + 1);
						if (!realpath (parent.c_str (), resolved))
							return fail ("Daemon: resolve parent of " + path + ": " + strerror (errno));
						path = std::string (resolved) + "/" + path.substr (slash == std::string::npos ? 0 : slash + 1);
					}
					auto& mask = rules[path];
					for (const char * p = permissions; *p; ++p)
						if (mask.find (*p) == std::string::npos) mask += *p;
					return true;
				};
				if (!allow (i2p::fs::GetDataDir (), "rwc") ||
					!allow (i2p::fs::GetCertsDir (), "r") || !allow (pidfile, "rwc")) return abortStart();
				for (const char * option : {"conf", "tunconf", "tunnelsdir", "reseed.file", "reseed.zipfile"})
				{
					std::string path; i2p::config::GetOption (option, path);
					// reseed.file accepts an HTTPS URL; a URL is not a path and
					// must not abort startup by failing realpath().
					if (path.find ("://") != std::string::npos) continue;
					if (!allow (path, "r")) return abortStart();
				}
				// Mirror Daemon_Singleton::init(): a daemon with no explicit
				// destination logs to a file, otherwise stdout stays stdout.
				std::string logDestination; i2p::config::GetOption ("log", logDestination);
				const bool logToFile = logDestination == "file" ||
					(isDaemon && (logDestination.empty () || logDestination == "stdout"));
				if (logToFile)
				{
					std::string path; i2p::config::GetOption ("logfile", path);
					if (!allow (path, "rwc")) return abortStart();
				}
				bool tuiEnabled = false; i2p::config::GetOption ("tui.enabled", tuiEnabled);
				if (tuiEnabled)
				{
					std::string path; i2p::config::GetOption ("tui.socket", path);
					if (!allow (path, "rwc")) return abortStart();
				}
				bool persistAddressbook = false; i2p::config::GetOption ("persist.addressbook", persistAddressbook);
				if (persistAddressbook)
				{
					std::string path; i2p::config::GetOption ("addressbook.hostsfile", path);
					if (!allow (path, "rwc")) return abortStart();
				}
				// unveil(2) gates connect(2) to AF_UNIX sockets by path; the
				// "unix" pledge alone does not grant the filesystem lookup.
				if (logDestination == "syslog" && !allow ("/dev/log", "w")) return abortStart();
				bool control; i2p::config::GetOption ("i2pcontrol.enabled", control);
				if (control)
				{
					for (const char * option : {"i2pcontrol.cert", "i2pcontrol.key"})
					{
						std::string path; i2p::config::GetOption (option, path);
						if (!path.empty () && path.front () != '/') path = i2p::fs::DataDirPath (path);
						if (!allow (path, "rwc")) return abortStart();
					}
					uint16_t port; i2p::config::GetOption ("i2pcontrol.port", port);
					if (!port)
					{
						std::string path; i2p::config::GetOption ("i2pcontrol.address", path);
						if (!allow (path, "rwc")) return abortStart();
					}
				}
				bool outproxy; i2p::config::GetOption ("socksproxy.outproxy.enabled", outproxy);
				uint16_t port; i2p::config::GetOption ("socksproxy.outproxyport", port);
				if (outproxy && !port)
				{
					std::string path; i2p::config::GetOption ("socksproxy.outproxy", path);
					if (!allow (path, "w")) return abortStart();
				}
				// miniupnpc consults MiniSSDPD before multicast discovery.
				if (upnp && !allow ("/var/run/minissdpd.sock", "w")) return abortStart();
				for (const auto& path : extraPaths)
					if (!allow (path, "rwc")) return abortStart();
				for (const auto& rule : rules)
					if (unveil (rule.first.c_str (), rule.second.c_str ()) == -1)
					{
						fail ("Daemon: unveil " + rule.first + ": " + strerror (errno));
						return abortStart();
					}
				if (unveil (nullptr, nullptr) == -1)
				{
					fail (std::string ("Daemon: unveil lock: ") + strerror (errno));
					return abortStart();
				}
			}
			// fork/setsid/setrlimit and PID locking are complete; no proc,
			// id, chown, descriptor passing or error promise is needed.
			if (usePledge && pledge (promises.c_str (), nullptr) == -1)
			{
				fail (std::string ("Daemon: pledge: ") + strerror (errno));
				return abortStart();
			}
#endif

			if (!Daemon_Singleton::start())
				return abortStart();
			return true;
		}

		bool DaemonUnix::stop()
		{
#ifdef __OpenBSD__
			// Keep the PID lock while router data is still being saved.
			bool stopped = Daemon_Singleton::stop();
			RemovePidFile (pidFH, pidfile);
			if (pidFH >= 0)
			{
				close (pidFH);
				pidFH = -1;
			}
			return stopped;
#else
			i2p::fs::Remove(pidfile);
			return Daemon_Singleton::stop();
#endif
		}

		void DaemonUnix::run ()
		{
			i2p::util::SetThreadName ("i2pd-daemon");
			while (running)
			{
				std::this_thread::sleep_for (std::chrono::seconds(1));
#ifdef __OpenBSD__
				// Poll at the existing one-second shutdown tick. Only this thread
				// consumes these signals, so a pending signal cannot be stolen
				// between sigpending() and sigwait().
				sigset_t pending;
				if (sigpending (&pending) == -1)
				{
					SignalError ("sigpending", errno);
					running = false;
					break;
				}
				for (int sig = 1; sig < NSIG; ++sig)
					if (sigismember (&daemonSignals, sig) == 1 && sigismember (&pending, sig) == 1)
					{
						sigset_t one;
						sigemptyset (&one);
						sigaddset (&one, sig);
						int received;
						int error = sigwait (&one, &received);
						if (error)
						{
							SignalError ("sigwait", error);
							running = false;
							break;
						}
						g_PendingSignals[(received > 0 && received < NSIG) ? received : sig] = 1;
						if (!running) break;
					}
				if (!running) break;
#endif
				HandlePendingSignals();
				if (gracefulShutdownInterval)
				{
					gracefulShutdownInterval--; // - 1 second
					if (gracefulShutdownInterval <= 0 || i2p::tunnel::tunnels.CountTransitTunnels() <= 0)
					{
						LogPrint(eLogInfo, "Graceful shutdown");
						return;
					}
				}
			}
		}
	}
}
#endif
