/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Worker.
 *
 * Implements worker spawning via fork/exec with the inherited descriptor 3,
 * close-on-exec cleanup and async-signal-safe child setup. ChildProcess
 * tracks the PID and optional pidfd so liveness checks, bounded
 * SIGTERM/SIGKILL escalation and reaping avoid PID reuse and unbounded waits.
 *
 */

#include "Worker.h"
#include "LogWorker.h"

#ifndef _WIN32

#include <cerrno>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <poll.h>
#include <signal.h>
#include <cstdio>
#include <sys/resource.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#if defined(__OpenBSD__)
// getprogname(3) is a global stdlib.h interface on OpenBSD; <cstdlib> does
// not necessarily expose it to the global namespace.
#include <stdlib.h>
#endif

#if defined(__linux__)
#include <sys/prctl.h>
#include <sys/syscall.h>
#endif

namespace i2p::privsep
{
namespace
{
	void CloseFrom (int lowfd)
	{
#if defined(__OpenBSD__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__APPLE__)
		closefrom (lowfd);
#else
		// Linux: close_range is preferred; fall back to a bounded close
		// loop. BSD closefrom(3) covers the whole descriptor table and is
		// usable in the post-fork child.
#if defined(__linux__) && defined(SYS_close_range)
		if (syscall (SYS_close_range, static_cast<unsigned int> (lowfd), ~0U, 0) == 0) return;
#endif
		if (lowfd < 0) lowfd = 0;
		// Callers pass a floor above the preserved worker descriptor, so
		// no descriptor is special-cased here. Older Linux kernels lack
		// close_range and a router with a raised limits.openfiles can hold
		// descriptors above 1023; use the actual soft limit so they cannot
		// leak into the exec'ed worker.
		// A 1024 ceiling was unsafe under RLIMIT_NOFILE=infinity: accepted
		// sockets/DB handles above it survived into the worker. Use the
		// derived soft limit; for infinity use the kernel's usual nr_open
		// order of magnitude and document the residual on pre-5.9 kernels.
		rlim_t ceiling = 1024;
		struct rlimit limit { };
		if (getrlimit (RLIMIT_NOFILE, &limit) == 0)
			ceiling = limit.rlim_cur == RLIM_INFINITY ? (static_cast<rlim_t> (1) << 20) : limit.rlim_cur;
		for (rlim_t fd = static_cast<rlim_t> (lowfd); fd < ceiling; ++fd) close (static_cast<int> (fd));
#endif
	}
}

std::string ExecutablePath ()
{
#if defined(__linux__)
	char path[4096];
	auto length = readlink ("/proc/self/exe", path, sizeof (path) - 1);
	if (length > 0)
	{
		path[length] = 0;
		return path;
	}
#elif defined(__OpenBSD__)
	// OpenBSD 7.9 has getprogname(3) but not getexecpath(3). Resolve the
	// program name through PATH while the startup pledge still permits it.
	const char * name = getprogname ();
	if (name && *name)
	{
		char resolved[4096];
		if (strchr (name, '/')) return realpath (name, resolved) ? resolved : name;
		const char * pathEnv = getenv ("PATH");
		const std::string searchPath = pathEnv && *pathEnv ? pathEnv : "/usr/local/bin:/usr/bin:/bin";
		size_t start = 0;
		while (start <= searchPath.size ())
		{
			const size_t end = searchPath.find (':', start);
			const std::string directory
				= searchPath.substr (start, end == std::string::npos ? std::string::npos : end - start);
			const std::string candidate = (directory.empty () ? std::string (".") : directory) + "/" + name;
			if (access (candidate.c_str (), X_OK) == 0)
				return realpath (candidate.c_str (), resolved) ? resolved : candidate;
			if (end == std::string::npos) break;
			start = end + 1;
		}
	}
#elif defined(__APPLE__)
	// Not a supported platform for this fork.
#endif
	const char * argv0 = getenv ("I2PD_EXECUTABLE");
	if (argv0 && *argv0) return argv0;
	return "/proc/self/exe";
}

ChildProcess::~ChildProcess ()
{
	Stop ();
}

ChildProcess::ChildProcess (ChildProcess && other) noexcept
	: m_Pid (other.m_Pid)
	, m_PidFd (other.m_PidFd)
	, m_Channel (std::move (other.m_Channel))
{
	other.m_Pid = -1;
	other.m_PidFd = -1;
}

ChildProcess & ChildProcess::operator= (ChildProcess && other) noexcept
{
	if (this != &other)
	{
		Stop ();
		m_Pid = other.m_Pid;
		m_PidFd = other.m_PidFd;
		m_Channel = std::move (other.m_Channel);
		other.m_Pid = -1;
		other.m_PidFd = -1;
	}
	return *this;
}

void ChildProcess::ClosePidFd ()
{
	if (m_PidFd >= 0)
	{
		close (m_PidFd);
		m_PidFd = -1;
	}
}

ChildProcess ChildProcess::Adopt (pid_t pid, Channel channel)
{
	ChildProcess process;
	process.m_Pid = pid;
	process.m_Channel = std::move (channel);
#if defined(__linux__) && defined(SYS_pidfd_open)
	// Open eagerly so a later restart decision can never be confused by PID
	// reuse after the child has exited.
	if (pid > 0)
	{
		auto pidFd = syscall (SYS_pidfd_open, pid, 0);
		if (pidFd >= 0) process.m_PidFd = static_cast<int> (pidFd);
	}
#endif
	return process;
}

bool ChildProcess::WaitForExit (int timeoutMs)
{
	// WaitForChildExitBounded() already reaps the child. A second waitpid()
	// here could consume a newly forked child that reused the PID, so only
	// clear the local handle.
	if (!WaitForChildExitBounded (m_Pid, m_PidFd, timeoutMs)) return false;
	m_Pid = -1;
	ClosePidFd ();
	return true;
}

bool ChildProcess::ReapNoHang ()
{
	if (m_Pid <= 0) return true;
	int status = 0;
	pid_t result;
	do result = waitpid (m_Pid, &status, WNOHANG);
	while (result < 0 && errno == EINTR);
	if (result == m_Pid || (result < 0 && errno == ECHILD))
	{
		// All reap paths are sequential and non-blocking: either this call
		// or WaitForChildExitBounded() consumed the status. A second waitpid
		// therefore observes ECHILD and can never see a recycled PID.
		m_Pid = -1;
		ClosePidFd ();
		return true;
	}
	return false;
}

bool ChildProcess::IsRunning ()
{
	if (m_Pid <= 0) return false;
#if defined(__linux__) && defined(SYS_pidfd_open)
	if (m_PidFd < 0) m_PidFd = static_cast<int> (syscall (SYS_pidfd_open, m_Pid, 0));
	if (m_PidFd >= 0)
	{
		pollfd pfd { m_PidFd, POLLIN, 0 };
		int result;
		do result = poll (&pfd, 1, 0);
		while (result < 0 && errno == EINTR);
		if (result == 0) return true;
		if (result > 0 && (pfd.revents & POLLIN))
		{
			// The pidfd reports exit. Reap immediately so a restart never
			// leaves a zombie and never races waitpid from another path.
			ReapNoHang ();
			return m_Pid > 0;
		}
	}
#endif
	return !ReapNoHang ();
}

void ChildProcess::Stop (int timeoutMs)
{
	if (m_Channel.IsOpen ())
	{
		Message shutdown { MessageType::Shutdown, 0, { } };
		(void)m_Channel.Send (shutdown, 500);
		m_Channel.Close ();
	}
	if (m_Pid > 0)
	{
		// Prefer pidfd_send_signal so a retained PID number cannot race with
		// recycle; fall back to kill() when the kernel or descriptor cannot
		// provide it (the unreaped child still holds the PID).
		auto signalChild = [this] (int signal)
		{
#if defined(__linux__) && defined(SYS_pidfd_send_signal)
			if (m_PidFd >= 0 && syscall (SYS_pidfd_send_signal, m_PidFd, signal, nullptr, 0) == 0) return;
#endif
			kill (m_Pid, signal);
		};
		const int gracefulMs = timeoutMs > 0 ? timeoutMs : 2000;
		if (!WaitForExit (gracefulMs))
		{
			signalChild (SIGTERM);
			if (!WaitForExit (500))
			{
				signalChild (SIGKILL);
				// SIGKILL is asynchronous and cannot interrupt an
				// uninterruptible (D state) task. Bound the reap instead of
				// calling waitpid(..., 0): a wedged storage/NFS task must
				// never block the daemon's shutdown path forever. If the
				// child is still not reaped, keep m_Pid/m_PidFd so a later
				// Stop()/IsRunning() can retry; the zombie holds the PID, so
				// the retained number cannot be recycled in the meantime.
				const int reapMs = std::clamp (timeoutMs > 0 ? timeoutMs : 500, 100, 1000);
				if (!WaitForExit (reapMs))
				{
					// Do not close the pidfd: it is the only race-free way to
					// learn about a later exit. The process entry remains
					// until the next retry or router exit.
					return;
				}
			}
		}
	}
	ClosePidFd ();
}

static i2p::expected<ChildProcess, std::string> SpawnWorkerInternal (const char * role, std::string_view executableOverride, int rootFd)
{
	if (!role || !*role) return i2p::unexpected ("missing worker role");
	const std::string executable = executableOverride.empty () ? ExecutablePath () : std::string (executableOverride);
	auto pair = Channel::CreatePair ();
	if (!pair) return i2p::unexpected (pair.error ());
	auto & parent = pair->first;
	auto & child = pair->second;
	// https://man.openbsd.org/pledge.2: sendfd/recvfd exclude directories.
	// Preserve the validated root through exec as fd4, never through SCM_RIGHTS.
	const int inheritedRoot = rootFd >= 0 ? ::fcntl (rootFd, F_DUPFD_CLOEXEC, 5) : -1;
	if (rootFd >= 0 && inheritedRoot < 0) return i2p::unexpected (std::string ("cannot duplicate inherited root"));
	pid_t pid = fork ();
	if (pid < 0)
	{
		if (inheritedRoot >= 0) ::close (inheritedRoot);
		return i2p::unexpected (std::string ("fork: ") + strerror (errno));
	}
	if (pid == 0)
	{
		// Restarts fork from the multi-threaded router. The child may only
		// use async-signal-safe operations until execv: no allocation, no
		// setenv, no stdio and no C++ objects with locks.
		sigset_t empty;
		sigemptyset (&empty);
		sigprocmask (SIG_SETMASK, &empty, nullptr);
#if defined(__linux__)
		// Do not let a setuid/file-capability executable raise privileges
		// before the worker installs its own seccomp/capability policy.
		if (prctl (PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) _exit (127);
#endif
		int fd = child.Fd ();
		// The socketpair may have been allocated below descriptor 3 if the
		// caller inherited closed standard descriptors. Close the parent
		// endpoint explicitly: CloseFrom(4) would not reach it, and dup2 to
		// descriptor 3 only closes it when it happens to be descriptor 3.
		const int parentFd = parent.Fd ();
		if (parentFd >= 0 && parentFd != fd) close (parentFd);
		if (fd != 3)
		{
			if (dup2 (fd, 3) < 0) _exit (127);
			close (fd);
		}
		int flags = fcntl (3, F_GETFD);
		if (flags < 0 || fcntl (3, F_SETFD, flags & ~FD_CLOEXEC) < 0) _exit (127);
		if (inheritedRoot >= 0)
		{
			if (::dup2 (inheritedRoot, 4) < 0) _exit (127);
			CloseFrom (5); // dup2 clears FD_CLOEXEC on fd4
		}
		else CloseFrom (4);
		// Parent death is detected through EOF on the channel; a
		// PR_SET_PDEATHSIG would be tied to the forking thread and could
		// kill a restarted worker when that thread is joined.
		char fdText[] = "3";
		char * const argv[] = { const_cast<char *> ("i2pd"), const_cast<char *> ("--privsep-worker"),
			const_cast<char *> (role), fdText, nullptr };
		execv (executable.c_str (), argv);
		_exit (127);
	}
	// Parent
	if (inheritedRoot >= 0) ::close (inheritedRoot);
	child.Close ();
	return ChildProcess::Adopt (pid, std::move (parent));
}

i2p::expected<ChildProcess, std::string> SpawnWorker (const char * role, std::string_view executable)
{
 return SpawnWorkerInternal (role, executable, -1);
}
i2p::expected<ChildProcess, std::string> SpawnWorkerWithRoot (const char * role, std::string_view executable, int rootFd)
{
 if (rootFd < 0) return i2p::unexpected (std::string ("missing inherited root"));
 return SpawnWorkerInternal (role, executable, rootFd);
}

static i2p::expected<Channel, std::string> SpawnDetachedWorkerInternal (const char * role, int rootFd)
{
 if (!role || !*role) return i2p::unexpected (std::string ("missing worker role"));
 const auto executable = ExecutablePath ();
 auto pair = Channel::CreatePair ();
 if (!pair) return i2p::unexpected (pair.error ());
 const int inheritedRoot = rootFd >= 0 ? ::fcntl (rootFd, F_DUPFD_CLOEXEC, 5) : -1;
 if (rootFd >= 0 && inheritedRoot < 0) return i2p::unexpected (std::string ("cannot duplicate worker root"));
 const pid_t intermediate = ::fork ();
 if (intermediate < 0) { if (inheritedRoot >= 0) ::close (inheritedRoot); return i2p::unexpected (std::string ("cannot fork worker intermediary")); }
 if (intermediate == 0)
 {
  // Async-signal-safe only between forks and exec. The intermediate is reaped
  // before pledge; init subsequently reaps the broker when its channel closes.
  const pid_t broker = ::fork ();
  if (broker < 0) _exit (127);
  if (broker > 0) _exit (0);
  sigset_t empty;
  sigemptyset (&empty); sigprocmask (SIG_SETMASK, &empty, nullptr);
#if defined(__linux__)
  if (prctl (PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) _exit (127);
#endif
  const int fd = pair->second.Fd ();
  const int parent = pair->first.Fd ();
  if (parent >= 0 && parent != fd) ::close (parent);
  if (fd != 3)
  {
   if (::dup2 (fd, 3) < 0) _exit (127);
   ::close (fd);
  }
  int flags = ::fcntl (3, F_GETFD);
  if (flags < 0 || ::fcntl (3, F_SETFD, flags & ~FD_CLOEXEC) < 0) _exit (127);
  if (inheritedRoot >= 0)
  {
   if (::dup2 (inheritedRoot, 4) < 0) _exit (127);
   CloseFrom (5);
  }
  else CloseFrom (4);
  char fdText[] = "3";
  char * const argv[] = { const_cast<char *> ("i2pd"), const_cast<char *> ("--privsep-worker"),
   const_cast<char *> (role), fdText, nullptr };
  ::execv (executable.c_str (), argv);
  _exit (127);
 }
 if (inheritedRoot >= 0) ::close (inheritedRoot);
 pair->second.Close ();
 if (!WaitForChildExitBounded (intermediate, -1, 5000))
 {
  ::kill (intermediate, SIGKILL);
  (void)WaitForChildExitBounded (intermediate, -1, 500);
  return i2p::unexpected (std::string ("worker intermediary did not exit"));
 }
 return std::move (pair->first);
}

i2p::expected<Channel, std::string> SpawnDetachedWorker (const char * role)
{ return SpawnDetachedWorkerInternal (role, -1); }
i2p::expected<Channel, std::string> SpawnDetachedWorkerWithRoot (const char * role, int rootFd)
{
 if (rootFd < 0) return i2p::unexpected (std::string ("invalid worker root"));
 return SpawnDetachedWorkerInternal (role, rootFd);
}

i2p::expected<ChildProcess, std::string> SpawnLogWorker (std::string_view executable)
{
	return SpawnWorker ("log", executable);
}

i2p::expected<ChildProcess, std::string> SpawnSupervisor ()
{
	return SpawnWorker ("supervisor", { });
}

}

#endif
