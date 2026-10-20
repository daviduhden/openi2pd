/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Worker.
 *
 * Declares the child-process owner used by the core and supervisor, with
 * bounded spawn/reap, optional pidfd tracking and channel ownership. Also
 * declares the log-worker and supervisor spawn helpers and executable
 * resolution.
 *
 */

#ifndef I2PD_PRIVSEP_WORKER_H__
#define I2PD_PRIVSEP_WORKER_H__

#include <string>
#include <string_view>
#include <sys/types.h>

#include "Channel.h"

#ifndef _WIN32
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <limits>
#include <poll.h>
#include <sys/wait.h>
#include <thread>
#endif

namespace i2p::privsep
{
#ifndef _WIN32
/**
 * Wait for a child until an absolute deadline without ever blocking past it.
 *
 * @param pidFd an optional pidfd for @a pid, or -1 when pidfd_open is
 *        unavailable. When present it is polled for exit before falling back
 *        to WNOHANG; the caller still owns and closes the descriptor.
 * @return true when the child was reaped (including ECHILD), false on timeout.
 *
 * This is deliberately header-inline so the bounded-reap regression can use
 * the production logic without linking the daemon-only Worker.cpp. A task in
 * uninterruptible sleep (D state) can exceed any deadline; callers must treat
 * false as "not reaped yet" and retry later rather than block.
 */
inline bool WaitForChildExitBounded (pid_t pid, int pidFd, int timeoutMs)
{
	if (pid <= 0) return true;
	const auto deadline = std::chrono::steady_clock::now () + std::chrono::milliseconds (std::max (timeoutMs, 0));
	bool usePidFd = pidFd >= 0;
	while (true)
	{
		int status = 0;
		pid_t result;
		do result = ::waitpid (pid, &status, WNOHANG);
		while (result < 0 && errno == EINTR);
		if (result == pid || (result < 0 && errno == ECHILD)) return true;

		const auto now = std::chrono::steady_clock::now ();
		if (now >= deadline) return false;
		const auto remainingMs = std::chrono::duration_cast<std::chrono::milliseconds> (deadline - now).count ();

		if (usePidFd)
		{
			pollfd pfd { pidFd, POLLIN, 0 };
			const int waitMs = static_cast<int> (
				std::min<int64_t> (std::max<int64_t> (remainingMs, 1), std::numeric_limits<int>::max ()));
			int polled;
			do polled = ::poll (&pfd, 1, waitMs);
			while (polled < 0 && errno == EINTR && std::chrono::steady_clock::now () < deadline);
			if (polled > 0)
			{
				if (pfd.revents & POLLNVAL)
					usePidFd = false; // keep the caller's descriptor, fall back below
				else if (pfd.revents & (POLLIN | POLLERR | POLLHUP))
					continue; // exit (or terminal condition): loop and reap
			}
			else if (polled == 0)
				continue; // deadline reached at the loop head
		}
		std::this_thread::sleep_for (std::chrono::milliseconds (std::min<int64_t> (remainingMs, 20)));
	}
}
#endif

class ChildProcess
{
public:
	ChildProcess () = default;
	~ChildProcess ();
	ChildProcess (ChildProcess && other) noexcept;
	ChildProcess & operator= (ChildProcess && other) noexcept;
	ChildProcess (const ChildProcess &) = delete;
	ChildProcess & operator= (const ChildProcess &) = delete;

	/**
	 * Adopt an existing child and channel. Used by the spawn path to apply
	 * the shared ownership and pidfd setup.
	 */
	static ChildProcess Adopt (pid_t pid, Channel channel);

	bool IsRunning ();
	/**
	 * Stop and reap the child within a bounded time.
	 *
	 * After SIGTERM/SIGKILL the wait is still bounded by an absolute deadline
	 * (pidfd or WNOHANG polling); it never performs an unbounded waitpid().
	 * A task stuck in uninterruptible sleep (D state) cannot run its exit
	 * path even after SIGKILL, so blocking until the kernel schedules it
	 * could stall the daemon indefinitely. In that case the PID and pidfd are
	 * retained and a later Stop()/IsRunning() retries the non-blocking reap;
	 * the child stays a zombie until then or until the router exits.
	 */
	void Stop (int timeoutMs = 2000);
	pid_t Pid () const { return m_Pid; }
	Channel & ChannelFd () { return m_Channel; }

private:
	friend i2p::expected<ChildProcess, std::string> SpawnLogWorker (std::string_view executable);
	/** Reap without blocking. Returns true when the child no longer exists. */
	bool ReapNoHang ();
	/** Wait for exit until an absolute deadline. Never blocks indefinitely. */
	bool WaitForExit (int timeoutMs);
	void ClosePidFd ();
	pid_t m_Pid = -1;
	int m_PidFd = -1;
	Channel m_Channel;
};

/**
 * Spawn a log worker. On OpenBSD the supervisor resolves the executable path
 * before applying its own pledge policy and passes it here, so the supervisor
 * does not need rpath after the sandbox is installed.
 */
i2p::expected<ChildProcess, std::string> SpawnLogWorker (std::string_view executable = { });
i2p::expected<ChildProcess, std::string> SpawnSupervisor ();
i2p::expected<ChildProcess, std::string> SpawnWorker (const char * role, std::string_view executableOverride);
// Pre-sandbox only: orphan the supervisor via a reaped intermediate process.
// The returned channel is its lifetime token; no proc promise is needed later.
i2p::expected<Channel, std::string> SpawnDetachedWorker (const char * role);
/** Pre-pledge startup only; inherited fd4, channel EOF owns worker lifetime. */
i2p::expected<Channel, std::string> SpawnDetachedWorkerWithRoot (const char * role, int rootFd);
i2p::expected<ChildProcess, std::string> SpawnWorkerWithRoot (const char * role, std::string_view executable, int rootFd);
std::string ExecutablePath ();
}

#endif
