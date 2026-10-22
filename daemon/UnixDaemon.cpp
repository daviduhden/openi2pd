/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: UnixDaemon.
 *
 * Implements the UNIX daemon lifecycle: optional fork/setsid daemonisation
 * with a readiness pipe, pidfile locking and rlimit setup, signal-driven
 * reload and shutdown, and graceful tunnel draining. On OpenBSD it installs
 * the startup pledge, spawns privilege-separated workers before the sandbox,
 * then applies validated unveil rules and a fixed runtime pledge; an add-on
 * policy file may not grant proc or exec.
 *
 */

#include "Daemon.h"

#ifndef _WIN32

#include <cstdio>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <thread>
#include <chrono>
#include <utility>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
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
#if defined(__OpenBSD__) && !defined(I2PD_NO_PRIVSEP)
#include "../privsep/Supervisor.h"
#endif

namespace
{
// Owns a short-lived file descriptor for the daemon startup paths. It is
// local to this translation unit: the daemon only needs deterministic
// close-on-return, not a general-purpose global descriptor type.
class UniqueFd
{
public:
	UniqueFd () = default;
	explicit UniqueFd (int fd)
		: m_Fd (fd)
	{
	}
	~UniqueFd () { Reset (); }

	UniqueFd (const UniqueFd &) = delete;
	UniqueFd & operator= (const UniqueFd &) = delete;

	UniqueFd (UniqueFd && other) noexcept
		: m_Fd (other.Release ())
	{
	}
	UniqueFd & operator= (UniqueFd && other) noexcept
	{
		if (this != &other) Reset (other.Release ());
		return *this;
	}

	int Get () const { return m_Fd; }
	bool IsValid () const { return m_Fd >= 0; }
	int Release ()
	{
		int fd = m_Fd;
		m_Fd = -1;
		return fd;
	}
	void Reset (int fd = -1)
	{
		if (m_Fd >= 0) close (m_Fd);
		m_Fd = fd;
	}

private:
	int m_Fd = -1;
};
}

#ifdef __OpenBSD__
// Blocked before service threads are created; consumed only by run().
static sigset_t daemonSignals;

static bool SignalError (const char * operation, int error)
{
	std::cerr << "Daemon: " << operation << ": " << strerror (error) << std::endl;
	syslog (LOG_DAEMON | LOG_ERR, "Daemon: %s: %s", operation, strerror (error));
	return false;
}

// The runtime sandbox is fixed and additive. A custom openbsd.pledge_file may
// request more, but it can never remove a promise the daemon needs to run and
// shut down safely. Keep this list in sync with the code paths below.
static const char * g_RequiredPledgePromises[] = {
	"stdio", // queue, sockets, signals and already-open descriptors
	"rpath", // read router data, certificates and configuration
	"wpath", // write router data and logs
	"cpath", // create and remove files, sockets and the pidfile
	"fattr", // chmod/chflags when saving keys and restricting files
	"inet", // NTCP2/SSU2, HTTP proxies and reseed
	"dns", // resolver support
	"unix", // local IPC and /dev/log
	"recvfd", // bounded worker channels returned by that supervisor
	"flock" // pidfile lockf(3)
};

// Current pledge(2) promise names. Unknown tokens are rejected instead of
// being passed to pledge(), so a typo or malformed file fails closed.
static const char * g_KnownPledgePromises[]
	= { "audio", "bpf", "chown", "cpath", "disklabel", "dns", "dpath", "drm", "error", "exec", "fattr", "flock",
		  "getpw", "id", "inet", "mcast", "pf", "proc", "prot_exec", "ps", "recvfd", "route", "rpath", "sendfd",
		  "settime", "stdio", "tape", "tty", "unix", "unveil", "video", "vminfo", "vmm", "wpath", "wroute" };

static bool IsKnownPledgePromise (const std::string & promise)
{
	for (const char * known : g_KnownPledgePromises)
		if (promise == known) return true;
	return false;
}

static void AddPledgePromise (std::vector<std::string> & promises, const std::string & promise)
{
	for (const auto & existing : promises)
		if (existing == promise) return;
	promises.push_back (promise);
}
#endif

const int GRACEFUL_SHUTDOWN_INTERVAL = 10 * 60; // in seconds

// a handler may only touch volatile sig_atomic_t, everything else is done by the daemon loop
static volatile sig_atomic_t g_PendingSignals[NSIG] = { };

void handle_signal (int sig)
{
	int savedErrno = errno;
	if (sig > 0 && sig < NSIG) g_PendingSignals[sig] = 1;
	errno = savedErrno;
}

// Remove the PID file only if the path still names the inode we locked.
// A stale path may have been replaced by a symlink or an unrelated file
// while the daemon was starting or shutting down.
static bool RemovePidFile (int fd, const std::string & path)
{
	if (fd < 0 || path.empty ()) return false;
	struct stat opened { }, named { };
	if (fstat (fd, &opened) != 0 || lstat (path.c_str (), &named) != 0) return false;
	if (!S_ISREG (opened.st_mode) || !S_ISREG (named.st_mode) || opened.st_dev != named.st_dev
		|| opened.st_ino != named.st_ino)
		return false;
	return unlink (path.c_str ()) == 0;
}

static void HandlePendingSignals ()
{
	for (int sig = 1; sig < NSIG; sig++)
	{
		if (!g_PendingSignals[sig]) continue;
		g_PendingSignals[sig] = 0;
		switch (sig)
		{
		case SIGHUP:
			LogPrint (eLogInfo, "Daemon: Got SIGHUP, reopening tunnel configuration...");
			i2p::client::context.ReloadConfig ();
			break;
		case SIGUSR1:
			LogPrint (eLogInfo, "Daemon: Got SIGUSR1, reopening logs...");
			i2p::log::Logger ().Reopen ();
			break;
		case SIGINT:
			if (i2p::context.AcceptsTunnels () && Daemon.gracefulShutdownInterval.load () == 0)
			{
				i2p::context.SetAcceptsTunnels (false);
				Daemon.gracefulShutdownInterval.store (GRACEFUL_SHUTDOWN_INTERVAL);
				LogPrint (eLogInfo, "Graceful shutdown after ",
					Daemon.gracefulShutdownInterval.load (), " seconds");
			}
			else
				Daemon.running = 0;
			break;
		case SIGABRT:
		case SIGTERM:
			Daemon.running = 0; // Exit loop
			break;
		case SIGPIPE: LogPrint (eLogInfo, "SIGPIPE received"); break;
		case SIGTSTP:
			LogPrint (eLogInfo, "Daemon: Got SIGTSTP, disconnecting from network...");
			i2p::transport::transports.SetOnline (false);
			break;
		case SIGCONT:
			LogPrint (eLogInfo, "Daemon: Got SIGCONT, restoring connection to network...");
			i2p::transport::transports.SetOnline (true);
			break;
		}
	}
}

// The self-daemonizing parent waits for a single status byte from the child.
// The child only reports success after the pidfile lock, sandbox setup, logger
// startup and Daemon_Singleton::start() have all completed. The first NetDb
// start can synchronously reseed, so the timeout must cover a slow network
// download rather than killing a healthy but still-starting router.
static constexpr int DAEMON_READY_TIMEOUT_MS = 5 * 60 * 1000;

enum class DaemonReadyStatus
{
	Ready,
	Failed,
	TimedOut
};

static DaemonReadyStatus WaitForDaemonReady (int fd)
{
	const auto deadline = std::chrono::steady_clock::now () + std::chrono::milliseconds (DAEMON_READY_TIMEOUT_MS);
	for (;;)
	{
		auto remaining
			= std::chrono::duration_cast<std::chrono::milliseconds> (deadline - std::chrono::steady_clock::now ())
				  .count ();
		if (remaining <= 0)
		{
			std::cerr << "i2pd: daemon startup timed out" << std::endl;
			return DaemonReadyStatus::TimedOut;
		}
		struct pollfd pfd { };
		pfd.fd = fd;
		pfd.events = POLLIN;
		int result = poll (&pfd, 1, static_cast<int> (remaining));
		if (result == 0)
		{
			std::cerr << "i2pd: daemon startup timed out" << std::endl;
			return DaemonReadyStatus::TimedOut;
		}
		if (result < 0)
		{
			if (errno == EINTR) continue;
			std::cerr << "i2pd: daemon readiness wait: " << strerror (errno) << std::endl;
			return DaemonReadyStatus::Failed;
		}

		char status = 0;
		ssize_t n;
		do n = read (fd, &status, 1);
		while (n < 0 && errno == EINTR);
		if (n == 1)
		{
			if (status == '1') return DaemonReadyStatus::Ready;
			std::cerr << "i2pd: daemon failed to start; see syslog for details" << std::endl;
			return DaemonReadyStatus::Failed;
		}
		if (n < 0)
			std::cerr << "i2pd: daemon readiness read: " << strerror (errno) << std::endl;
		else
			std::cerr << "i2pd: daemon exited before reporting readiness" << std::endl;
		return DaemonReadyStatus::Failed;
	}
}

// Writes a failure status unless the child reaches the successful ready point.
// This makes every early return in start() propagate to the waiting parent.
// The notifier owns the write end, so closing it is not duplicated elsewhere.
class DaemonReadyNotifier
{
public:
	explicit DaemonReadyNotifier (UniqueFd fd)
		: m_Fd (std::move (fd))
	{
	}
	~DaemonReadyNotifier () { Notify ('0'); }

	DaemonReadyNotifier (const DaemonReadyNotifier &) = delete;
	DaemonReadyNotifier & operator= (const DaemonReadyNotifier &) = delete;

	void Ready () { Notify ('1'); }

private:
	void Notify (char status)
	{
		if (!m_Fd.IsValid ()) return;
		ssize_t written;
		do written = write (m_Fd.Get (), &status, 1);
		while (written < 0 && errno == EINTR);
		m_Fd.Reset ();
	}

	UniqueFd m_Fd;
};

namespace i2p
{
namespace util
{
	bool DaemonUnix::start ()
	{
		pidFH = -1; // no PID lock yet
		UniqueFd readyFd;
		if (isDaemon)
		{
			int readyPipe[2];
			if (pipe (readyPipe) != 0)
			{
				LogPrint (eLogError, "Daemon: Could not create readiness pipe: ", strerror (errno));
				std::cerr << "i2pd: Could not create readiness pipe: " << strerror (errno) << std::endl;
				return false;
			}
			UniqueFd readEnd (readyPipe[0]);
			UniqueFd writeEnd (readyPipe[1]);
			for (int fd : readyPipe)
			{
				if (fcntl (fd, F_SETFD, FD_CLOEXEC) != -1) continue;
				LogPrint (eLogError, "Daemon: Could not set close-on-exec on readiness pipe: ", strerror (errno));
				std::cerr << "i2pd: Could not set close-on-exec on readiness pipe: " << strerror (errno) << std::endl;
				return false;
			}

			pid_t pid = fork ();
			if (pid > 0) // parent
			{
				// The child owns the write end; close ours before waiting so
				// the read reports EOF if the child exits without reporting.
				writeEnd.Reset ();
				DaemonReadyStatus status = WaitForDaemonReady (readEnd.Get ());
				readEnd.Reset ();
				if (status == DaemonReadyStatus::TimedOut)
				{
					// Startup can block before the daemon installs its signal
					// handling. SIGKILL guarantees a timed-out child cannot
					// keep running after the parent reported failure.
					kill (pid, SIGKILL);
				}
				// Do not run atexit handlers or flush duplicated buffers in
				// the parent after fork; the child owns the service state.
				_exit (status == DaemonReadyStatus::Ready ? EXIT_SUCCESS : EXIT_FAILURE);
			}

			if (pid < 0) // error
			{
				int error = errno;
				readEnd.Reset ();
				writeEnd.Reset ();
				LogPrint (eLogError, "Daemon: Could not fork: ", strerror (error));
				std::cerr << "i2pd: Could not fork: " << strerror (error) << std::endl;
				return false;
			}

			readEnd.Reset ();
			readyFd = std::move (writeEnd);
		}
		// A daemon child only reports success after Daemon_Singleton::start().
		// The notifier is created before the remaining daemonization steps, so
		// a setsid(), chdir() or freopen() failure also reaches the waiting
		// parent instead of leaving it to time out.
		DaemonReadyNotifier ready (std::move (readyFd));

		if (isDaemon)
		{
			// child
			umask (S_IWGRP | S_IRWXO); // 0027
			int sid = setsid ();
			if (sid < 0)
			{
				LogPrint (eLogError, "Daemon: Could not create process group.");
				std::cerr << "i2pd: Could not create process group." << std::endl;
				return false;
			}
			std::string d = i2p::fs::GetDataDir ();
			if (chdir (d.c_str ()) != 0)
			{
				LogPrint (eLogError, "Daemon: Could not chdir: ", strerror (errno));
				std::cerr << "i2pd: Could not chdir: " << strerror (errno) << std::endl;
				return false;
			}

			// point std{in,out,err} descriptors to /dev/null
			if (!freopen ("/dev/null", "r", stdin) || !freopen ("/dev/null", "w", stdout)
				|| !freopen ("/dev/null", "w", stderr))
			{
				LogPrint (eLogError, "Daemon: Could not redirect standard descriptors: ", strerror (errno));
				return false;
			}
		}

		// set proc limits
		struct rlimit limit;
		uint16_t nfiles;
		i2p::config::GetOption ("limits.openfiles", nfiles);
		if (getrlimit (RLIMIT_NOFILE, &limit) != 0)
		{
			LogPrint (eLogError, "Daemon: Can't get max number of open files: ", strerror (errno));
		}
		else if (nfiles == 0)
		{
			LogPrint (eLogInfo, "Daemon: Using system limit in ", limit.rlim_cur, " max open files");
		}
		else if (nfiles <= limit.rlim_max)
		{
			limit.rlim_cur = nfiles;
			if (setrlimit (RLIMIT_NOFILE, &limit) == 0)
			{
				LogPrint (eLogInfo, "Daemon: Set max number of open files to ", nfiles, " (system limit is ",
					limit.rlim_max, ")");
			}
			else
			{
				LogPrint (eLogError, "Daemon: Can't set max number of open files: ", strerror (errno));
			}
		}
		else
		{
			LogPrint (eLogError, "Daemon: limits.openfiles exceeds system limit: ", limit.rlim_max);
		}
		uint32_t cfsize;
		i2p::config::GetOption ("limits.coresize", cfsize);
		if (cfsize) // core file size set
		{
			uint64_t coreSize = (uint64_t)cfsize * 1024;
			if (getrlimit (RLIMIT_CORE, &limit) != 0)
				LogPrint (eLogError, "Daemon: Can't get max size of coredump: ", strerror (errno));
			else if (coreSize <= limit.rlim_max)
			{
				limit.rlim_cur = coreSize;
				if (setrlimit (RLIMIT_CORE, &limit) != 0)
				{
					LogPrint (eLogError, "Daemon: Can't set max size of coredump: ", strerror (errno));
				}
				else
				{
					LogPrint (eLogInfo, "Daemon: Set max size of core files to ", cfsize, "Kb");
				}
			}
			else
			{
				LogPrint (eLogError, "Daemon: limits.coresize exceeds system limit: ", limit.rlim_max);
			}
		}

		// Pidfile
		// The descriptor must stay open for the lifetime of the daemon because
		// lockf(3)/fcntl(2) ownership is tied to the open file description.

		// Until startup succeeds the pidfile descriptor is owned by this guard;
		// every early return closes it without duplicating close() calls.
		UniqueFd pidFile;

		// Close and remove the PID file if startup fails after locking it.
		auto abortStart = [this, &pidFile] ()
		{
			// If startup failed after Log::Start(), join the logger thread
			// here. Log::~Log() deletes a joinable std::thread, which would
			// call std::terminate() during exit.
			i2p::log::Logger ().Stop ();
			StopPrivsepWorkers ();
			if (pidFile.IsValid ())
			{
				// Keep the descriptor open until the identity check is done.
				RemovePidFile (pidFile.Get (), pidfile);
				pidFile.Reset ();
			}
			return false;
		};

		i2p::config::GetOption ("pidfile", pidfile);
		if (pidfile == "")
		{
			pidfile = i2p::fs::DataDirPath ("i2pd.pid");
		}
		if (pidfile != "")
		{
			int flags = O_RDWR | O_CREAT | O_CLOEXEC;
#ifdef O_NOFOLLOW
			// Do not follow a symlink planted in a writable directory.
			flags |= O_NOFOLLOW;
#endif
			pidFile.Reset (open (pidfile.c_str (), flags, 0600));
			if (!pidFile.IsValid ())
			{
				LogPrint (eLogError, "Daemon: Could not create pid file ", pidfile, ": ", strerror (errno));
				std::cerr << "i2pd: Could not create pid file " << pidfile << ": " << strerror (errno) << std::endl;
				return false;
			}

			// An existing file might not have been created by us and might
			// have wider permissions; only a regular file is acceptable.
			struct stat pidStat;
			if (fstat (pidFile.Get (), &pidStat) != 0 || !S_ISREG (pidStat.st_mode))
			{
				LogPrint (eLogError, "Daemon: Pid file ", pidfile, " is not a regular file");
				std::cerr << "i2pd: Pid file " << pidfile << " is not a regular file" << std::endl;
				return false;
			}
			if (fchmod (pidFile.Get (), 0600) != 0)
			{
				LogPrint (
					eLogError, "Daemon: Can't restrict permissions of pid file ", pidfile, ": ", strerror (errno));
				std::cerr << "i2pd: Can't restrict permissions of pid file " << pidfile << ": " << strerror (errno)
						  << std::endl;
				return false;
			}

#ifndef ANDROID
			if (lockf (pidFile.Get (), F_TLOCK, 0) != 0)
#else
			struct flock fl;
			fl.l_len = 0;
			fl.l_type = F_WRLCK;
			fl.l_whence = SEEK_SET;
			fl.l_start = 0;

			if (fcntl (pidFile.Get (), F_SETLK, &fl) != 0)
#endif
			{
				LogPrint (eLogError, "Daemon: Could not lock pid file ", pidfile, ": ", strerror (errno));
				std::cerr << "i2pd: Could not lock pid file " << pidfile << ": " << strerror (errno) << std::endl;
				// Another instance owns the file, so do not unlink it.
				return false;
			}

			char pid[16];
			int pidLen = snprintf (pid, sizeof (pid), "%d\n", getpid ());
			if (pidLen <= 0 || pidLen >= (int)sizeof (pid) || ftruncate (pidFile.Get (), 0) != 0)
			{
				LogPrint (eLogCritical, "Daemon: Could not write pidfile ", pidfile, ": ", strerror (errno));
				std::cerr << "i2pd: Could not write pidfile " << pidfile << ": " << strerror (errno) << std::endl;
				return abortStart ();
			}
			size_t written = 0;
			while (written < (size_t)pidLen)
			{
				ssize_t n = pwrite (pidFile.Get (), pid + written, pidLen - written, written);
				if (n < 0 && errno == EINTR) continue;
				if (n <= 0) break;
				written += n;
			}
			if (written != (size_t)pidLen)
			{
				LogPrint (eLogCritical, "Daemon: Could not write pidfile ", pidfile, ": ", strerror (errno));
				std::cerr << "i2pd: Could not write pidfile " << pidfile << ": " << strerror (errno) << std::endl;
				return abortStart ();
			}
		}
		gracefulShutdownInterval = 0; // not specified

		// handle signal TSTP
		bool handleTSTP = false;
		i2p::config::GetOption ("unix.handle_sigtstp", handleTSTP);

#ifdef __OpenBSD__
		// ReloadConfig() can stop and join destination threads. Never run
		// it from an asynchronous handler on an arbitrary service thread.
		sigemptyset (&daemonSignals);
		for (int sig : { SIGHUP, SIGUSR1, SIGABRT, SIGTERM, SIGINT, SIGPIPE }) sigaddset (&daemonSignals, sig);
		if (handleTSTP)
		{
			sigaddset (&daemonSignals, SIGTSTP);
			sigaddset (&daemonSignals, SIGCONT);
		}
		int error = pthread_sigmask (SIG_BLOCK, &daemonSignals, nullptr);
		if (error)
		{
			SignalError ("pthread_sigmask", error);
			return abortStart ();
		}
		// Inherited SIG_IGN dispositions could discard a blocked signal.
		struct sigaction sa { };
		sa.sa_handler = SIG_DFL;
		sigemptyset (&sa.sa_mask);
		for (int sig = 1; sig < NSIG; ++sig)
			if (sigismember (&daemonSignals, sig) == 1 && sigaction (sig, &sa, nullptr) == -1)
			{
				SignalError ("sigaction", errno);
				return abortStart ();
			}
#else
		// Signal handler
		struct sigaction sa { };
		sa.sa_handler = handle_signal;
		sigemptyset (&sa.sa_mask);
		sa.sa_flags = SA_RESTART;
		for (int sig : { SIGHUP, SIGUSR1, SIGABRT, SIGTERM, SIGINT, SIGPIPE })
			if (sigaction (sig, &sa, nullptr) == -1)
			{
				LogPrint (eLogError, "Daemon: Can't install handler for signal ", sig, ": ", strerror (errno));
				return abortStart ();
			}
		if (handleTSTP)
		{
			if (sigaction (SIGTSTP, &sa, nullptr) == -1 || sigaction (SIGCONT, &sa, nullptr) == -1)
			{
				LogPrint (eLogError, "Daemon: Can't install handler for job-control signals: ", strerror (errno));
				return abortStart ();
			}
		}

#endif

#ifdef __OpenBSD__
		// Spawn supervisor/log workers while the startup pledge still carries
		// proc/exec. Pledge and unveil are cleared by execve; every worker
		// installs its own policy after exec instead of inheriting this one.
		if (!PreparePrivsepWorkers ()) return abortStart ();
#if !defined(I2PD_NO_PRIVSEP) && !defined(NO_TORRENTS)
		// Separate broker is created before runtime pledge removes exec. Its root
		// authority is immutable; roots outside datadir are deliberately rejected.
		if (::geteuid () != 0)
		{
			std::string storageError;
			if (!i2p::privsep::PrepareTorrentStorageSupervisor (i2p::fs::GetDataDir (), storageError))
			{
				std::cerr << "storage supervisor: " << storageError << std::endl;
				return abortStart ();
			}
		}
#endif
		// Resolve/read sandbox policy before the first unveil hides it.
		bool useUnveil, usePledge;
		i2p::config::GetOption ("openbsd.unveil_enabled", useUnveil);
		i2p::config::GetOption ("openbsd.pledge_enabled", usePledge);
		bool privsepEnabled = false, privsepLog = false;
		i2p::config::GetOption ("privsep.enabled", privsepEnabled);
		i2p::config::GetOption ("privsep.log", privsepLog);
		// CreatePrivateFile() uses chmod when saving router and destination keys.
		// The required promises are always present; a custom file may only add
		// to this set (see openbsd.pledge_file handling below).
		std::vector<std::string> promises;
		for (const char * promise : g_RequiredPledgePromises) promises.emplace_back (promise);
		if (privsepEnabled && privsepLog)
			// The core monitor still uses waitpid()/kill() to reap and, when
			// necessary, stop the supervisor. proc does not permit exec, so the
			// core cannot create a replacement worker after this point. The
			// supervisor remains the process-creation boundary.
			promises.emplace_back ("proc");
		bool upnp;
		i2p::config::GetOption ("upnp.enabled", upnp);
		if (upnp) promises.emplace_back ("mcast");
		std::vector<std::string> extraPaths;
		auto fail = [] (const std::string & message)
		{
			std::cerr << message << std::endl;
			// The logger thread has not started, and daemon stderr is /dev/null.
			syslog (LOG_DAEMON | LOG_ERR, "%s", message.c_str ());
			return false;
		};
		// The supervisor/log-worker pair has already been spawned above. The
		// runtime policy has no exec; proc is retained only for supervisor
		// reaping/signalling while the log worker is enabled.
		auto readPolicy = [&fail] (const char * option, std::vector<std::string> & lines)
		{
			std::string path;
			i2p::config::GetOption (option, path);
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
			std::string policy;
			i2p::config::GetOption ("openbsd.pledge_file", policy);
			if (!policy.empty ())
			{
				std::vector<std::string> lines;
				if (!readPolicy ("openbsd.pledge_file", lines)) return abortStart ();
				// Parse whitespace-separated tokens and treat '#' as a comment.
				// Duplicates are ignored, unknown names are rejected, and the
				// fixed required set above stays in place.
				for (const auto & line : lines)
				{
					size_t pos = 0;
					while (pos < line.size ())
					{
						size_t start = line.find_first_not_of (" \t\r", pos);
						if (start == std::string::npos || line[start] == '#') break;
						size_t end = line.find_first_of (" \t\r#", start);
						std::string token
							= line.substr (start, end == std::string::npos ? std::string::npos : end - start);
						if (!IsKnownPledgePromise (token))
						{
							fail ("Daemon: Unknown pledge promise \"" + token + "\" in openbsd.pledge_file");
							return abortStart ();
						}
						if (token == "proc" || token == "exec")
						{
							// Workers are spawned before this runtime policy. The
							// core must not regain process-creation capability
							// through an additive policy file.
							fail ("Daemon: runtime openbsd.pledge_file may not add proc or exec");
							return abortStart ();
						}
						AddPledgePromise (promises, token);
						if (end == std::string::npos || line[end] == '#') break;
						pos = end;
					}
				}
			}
		}
		if (useUnveil && !readPolicy ("openbsd.unveil_file", extraPaths)) return abortStart ();
		if (useUnveil)
		{
			// Coalesce aliases before unveil: repeated rules may only reduce
			// permissions. For example, certsdir may equal datadir.
			std::map<std::string, std::string> rules;
			auto allow = [&rules, &fail] (std::string path, const char * permissions)
			{
				if (path.empty ()) return true;
				while (path.size () > 1 && path.back () == '/') path.pop_back ();
				char resolved[PATH_MAX];
				if (realpath (path.c_str (), resolved))
					path = resolved;
				else
				{
					if (errno != ENOENT) return fail ("Daemon: resolve " + path + ": " + strerror (errno));
					// A missing leaf is remembered in its existing parent by
					// unveil, without giving access to sibling files.
					auto slash = path.find_last_of ('/');
					std::string parent = slash == std::string::npos ? "." : path.substr (0, slash + 1);
					if (!realpath (parent.c_str (), resolved))
						return fail ("Daemon: resolve parent of " + path + ": " + strerror (errno));
					path = std::string (resolved) + "/" + path.substr (slash == std::string::npos ? 0 : slash + 1);
				}
				auto & mask = rules[path];
				for (const char * p = permissions; *p; ++p)
					if (mask.find (*p) == std::string::npos) mask += *p;
				return true;
			};
			if (!allow (i2p::fs::GetDataDir (), "rwc") || !allow (i2p::fs::GetCertsDir (), "r")
				|| !allow (pidfile, "rwc"))
				return abortStart ();
			for (const char * option : { "conf", "tunconf", "tunnelsdir", "reseed.file", "reseed.zipfile" })
			{
				std::string path;
				i2p::config::GetOption (option, path);
				// reseed.file accepts an HTTPS URL; a URL is not a path and
				// must not abort startup by failing realpath().
				if (path.find ("://") != std::string::npos) continue;
				if (!allow (path, "r")) return abortStart ();
			}
			// Mirror Daemon_Singleton::init(): a daemon with no explicit
			// destination logs to a file, otherwise stdout stays stdout.
			std::string logDestination;
			i2p::config::GetOption ("log", logDestination);
			const bool logToFile
				= logDestination == "file" || (isDaemon && (logDestination.empty () || logDestination == "stdout"));
			// When the log worker owns the sink, the core must not retain a
			// write rule for the log file (or /dev/log). The supervisor path
			// is configured before this runtime sandbox is installed.
			const bool privsepOwnsLog = privsepEnabled && privsepLog;
			if (logToFile && !privsepOwnsLog)
			{
				std::string path;
				i2p::config::GetOption ("logfile", path);
				if (!allow (path, "rwc")) return abortStart ();
			}
			bool tuiEnabled = false;
			i2p::config::GetOption ("tui.enabled", tuiEnabled);
			if (tuiEnabled)
			{
				std::string path;
				i2p::config::GetOption ("tui.socket", path);
				if (!allow (path, "rwc")) return abortStart ();
			}
			bool persistAddressbook = false;
			i2p::config::GetOption ("persist.addressbook", persistAddressbook);
			if (persistAddressbook)
			{
				std::string path;
				i2p::config::GetOption ("addressbook.hostsfile", path);
				if (!allow (path, "rwc")) return abortStart ();
			}
			// unveil(2) gates connect(2) to AF_UNIX sockets by path; the
			// "unix" pledge alone does not grant the filesystem lookup.
			if (logDestination == "syslog" && !privsepOwnsLog && !allow ("/dev/log", "w")) return abortStart ();
			bool control;
			i2p::config::GetOption ("i2pcontrol.enabled", control);
			if (control)
			{
				for (const char * option : { "i2pcontrol.cert", "i2pcontrol.key" })
				{
					std::string path;
					i2p::config::GetOption (option, path);
					if (!path.empty () && path.front () != '/') path = i2p::fs::DataDirPath (path);
					if (!allow (path, "rwc")) return abortStart ();
				}
				uint16_t port;
				i2p::config::GetOption ("i2pcontrol.port", port);
				if (!port)
				{
					std::string path;
					i2p::config::GetOption ("i2pcontrol.address", path);
					if (!allow (path, "rwc")) return abortStart ();
				}
			}
			bool outproxy;
			i2p::config::GetOption ("socksproxy.outproxy.enabled", outproxy);
			uint16_t port;
			i2p::config::GetOption ("socksproxy.outproxyport", port);
			if (outproxy && !port)
			{
				std::string path;
				i2p::config::GetOption ("socksproxy.outproxy", path);
				if (!allow (path, "w")) return abortStart ();
			}
			// miniupnpc consults MiniSSDPD before multicast discovery.
			if (upnp && !allow ("/var/run/minissdpd.sock", "w")) return abortStart ();
			for (const auto & path : extraPaths)
				if (!allow (path, "rwc")) return abortStart ();
			for (const auto & rule : rules)
				if (unveil (rule.first.c_str (), rule.second.c_str ()) == -1)
				{
					fail ("Daemon: unveil " + rule.first + ": " + strerror (errno));
					return abortStart ();
				}
			if (unveil (nullptr, nullptr) == -1)
			{
				fail (std::string ("Daemon: unveil lock: ") + strerror (errno));
				return abortStart ();
			}
		}
		// fork/setsid/setrlimit and PID locking are complete; no proc,
		// id, chown, descriptor passing or error promise is needed.
		std::string pledgePromises;
		for (const auto & promise : promises)
		{
			if (!pledgePromises.empty ()) pledgePromises += ' ';
			pledgePromises += promise;
		}
		if (usePledge && pledge (pledgePromises.c_str (), nullptr) == -1)
		{
			fail (std::string ("Daemon: pledge: ") + strerror (errno));
			return abortStart ();
		}
#endif

		if (!Daemon_Singleton::start ()) return abortStart ();
		// Startup completed: the long-lived PID lock moves to the member that
		// stop() uses. Before this point the local guard owns the descriptor.
		pidFH = pidFile.Release ();
		ready.Ready ();
		return true;
	}

	bool DaemonUnix::stop ()
	{
		// Keep the PID lock while router data is still being saved. The
		// identity check also prevents removing an unrelated file that
		// replaced the pidfile path during the daemon's lifetime.
		bool stopped = Daemon_Singleton::stop ();
		RemovePidFile (pidFH, pidfile);
		if (pidFH >= 0)
		{
			close (pidFH);
			pidFH = -1;
		}
		return stopped;
	}

	void DaemonUnix::run ()
	{
		i2p::util::SetThreadName ("i2pd-daemon");
		while (running)
		{
			std::this_thread::sleep_for (std::chrono::seconds (1));
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
			HandlePendingSignals ();
			if (gracefulShutdownInterval)
			{
				gracefulShutdownInterval--; // - 1 second
				if (gracefulShutdownInterval <= 0 || i2p::tunnel::tunnels.CountTransitTunnels () <= 0)
				{
					LogPrint (eLogInfo, "Graceful shutdown");
					return;
				}
			}
		}
	}
}
}
#endif
