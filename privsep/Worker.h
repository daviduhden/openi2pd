/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Worker.
 *
 * Declares the child-process owner used by the core and supervisor, with
 * bounded spawn/reap, optional pidfd tracking and channel ownership. Also
 * declares the log-worker and supervisor spawn helpers, the frozen executable
 * path, the one-way process-creation barrier and the per-spawn identity
 * handshake that binds the exec'ed role and its uid/gid to the process that
 * forked it.
 *
 */

#ifndef I2PD_PRIVSEP_WORKER_H__
#define I2PD_PRIVSEP_WORKER_H__

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <vector>

#include "Channel.h"

#ifndef _WIN32
#include <algorithm>
#include <sys/stat.h>
#include <cerrno>
#include <chrono>
#include <limits>
#include <poll.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#endif

namespace i2p::privsep
{
/**
 * Worker roles as an enumerated identity. The spawner declares the role both
 * on the command line and in the identity hello written to the child's
 * descriptor 5; RunWorker refuses a process whose argv role and hello role do
 * not agree, so a hand-started executable cannot select a role by itself.
 */
enum class WorkerRole : uint8_t
{
	Log = 1,
	Supervisor = 2,
	RouterStorage = 3,
	RouterStorageInherited = 4,
	TorrentStorage = 5,
	TorrentStorageInherited = 6,
	TorrentStorageSupervisor = 7,
	DataStorage = 8,
	DataStorageInherited = 9
};

const char * WorkerRoleName (WorkerRole role);

/**
 * True for roles that receive an inherited storage root at descriptor 4.
 * A role outside this set must never be spawned with a root fd, and every role
 * in it must receive one; the spawn helper enforces both directions.
 */
bool WorkerRoleTakesInheritedRoot (WorkerRole role);
std::optional<WorkerRole> WorkerRoleFromName (std::string_view name);

inline constexpr size_t WORKER_HELLO_NONCE_SIZE = 16;
/** A big-endian 32-bit uid followed by a big-endian 32-bit gid. */
inline constexpr size_t WORKER_HELLO_IDENTITY_SIZE = 8;
/**
 * One role byte, the 16-byte nonce and the identity of the process that will
 * serve the channel: `[role u8 | nonce 16 | uid u32 BE | gid u32 BE]`, a fixed
 * 25-byte record. The uid/gid are written by the forked child after its
 * credential drop, so the exec'ed worker can prove both the spawner and the
 * identity it was started with.
 */
inline constexpr size_t WORKER_HELLO_SIZE = 1 + WORKER_HELLO_NONCE_SIZE + WORKER_HELLO_IDENTITY_SIZE;

/** Decoded spawn identity: the role and the identity the worker must have. */
struct WorkerHelloRecord
{
	WorkerRole role = WorkerRole::Log;
	uint32_t uid = 0;
	uint32_t gid = 0;
};

/**
 * Build the hello record for one spawn from an explicit nonce and identity.
 *
 * Returns nullopt for an unknown role, a zero nonce, a nonce of the wrong size
 * or uid 0: a predictable or partial nonce would make the handshake worthless,
 * and a uid-0 record must never be produced.
 */
std::optional<std::vector<uint8_t>> EncodeWorkerHello (
	WorkerRole role, std::span<const uint8_t> nonce, uint32_t uid, uint32_t gid);
/**
 * Validate a complete hello record. Requires exactly WORKER_HELLO_SIZE bytes,
 * a known role id, a non-zero nonce and a non-zero uid. Errors never contain
 * the nonce.
 */
std::optional<WorkerHelloRecord> DecodeWorkerHello (std::span<const uint8_t> hello);
/**
 * Generate a fresh 16-byte nonce from the platform CSPRNG and build the hello
 * for an explicit identity. Fails closed (nullopt) when the provider cannot
 * supply randomness.
 */
std::optional<std::vector<uint8_t>> CreateWorkerHello (WorkerRole role, uint32_t uid, uint32_t gid);
/**
 * Read, validate and consume the hello from @a fd, then close it.
 *
 * Exactly WORKER_HELLO_SIZE bytes are read and one further byte is peeked with
 * a non-blocking read: a missing descriptor, an empty/short record, a second
 * or duplicated record (one extra byte, or a writer that is still open), an
 * unknown role or uid 0 all return nullopt. The nonce is never logged,
 * formatted or retained.
 */
std::optional<WorkerHelloRecord> ReadWorkerHello (int fd);

#ifndef _WIN32
/**
 * Close inherited standard descriptors that are sockets. A foreground
 * service or socket-activated wrapper can otherwise hand the log/supervisor
 * worker a live network descriptor that seccomp/Landlock will not revoke.
 */
inline void SanitizeInheritedStdio ()
{
	for (int fd = 0; fd <= 2; ++fd)
	{
		struct stat status { };
		if (::fstat (fd, &status) == 0 && S_ISSOCK (status.st_mode)) ::close (fd);
	}
}

/**
 * Rebind descriptors 0, 1 and 2 to /dev/null.
 *
 * A worker must never write to the operator's terminal, a service-manager log
 * pipe or a socket-activation descriptor it merely inherited, and it must not
 * read the operator's stdin. /dev/null is opened once and duplicated onto each
 * differing descriptor. Returns false when it cannot be opened or duplicated;
 * callers must treat that as fatal instead of continuing with the inherited
 * descriptors.
 */
bool PinStandardDescriptors ();

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
	/**
	 * Release ownership without signalling. Used by the OpenBSD storage
	 * broker after it hands the worker channel to the core: the broker must
	 * not kill a worker that another process now owns.
	 */
	void Disown ()
	{
		ClosePidFd ();
		m_Pid = -1;
		m_Channel.Close ();
	}
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
/**
 * Spawn the supervisor process that owns the log worker.
 *
 * The child is bound to a fresh CSPRNG nonce over descriptor 5 and drops to the
 * configured service identity before execve; the caller keeps the channel end
 * and must Reattach/Configure it with a LogSink. On OpenBSD this must run before
 * the core's runtime pledge removes proc/exec.
 */
i2p::expected<ChildProcess, std::string> SpawnSupervisor ();
/**
 * Spawn one worker role with no inherited storage root.
 *
 * Descriptor contract for the exec'ed child: 3 = control channel, 5 = one-shot
 * identity record, 0/1/2 pinned to /dev/null, 4 closed. The caller receives the
 * parent endpoint with a peer expectation of the configured uid/gid and this
 * child's pid. Fails closed when process creation has been forbidden, when the
 * worker identity is unresolved (uid 0), when the frozen executable is not
 * usable, or when the CSPRNG fails.
 *
 * @param role role name accepted by WorkerRoleFromName; it must not require an
 *        inherited root.
 * @param executableOverride empty to use the frozen ExecutablePath(); the
 *        override is still validated as a spawnable path.
 */
i2p::expected<ChildProcess, std::string> SpawnWorker (const char * role, std::string_view executableOverride);
// Pre-sandbox only: orphan the supervisor via a reaped intermediate process.
// The returned channel is its lifetime token; no proc promise is needed later.
i2p::expected<Channel, std::string> SpawnDetachedWorker (const char * role);
/**
 * Pre-pledge startup only; inherited fd4, channel EOF owns worker lifetime.
 *
 * The worker is double-forked so it is not the daemon's child, so only the
 * configured uid/gid (not a pid) can be bound on the channel. Closing the
 * returned descriptor is the worker's lifetime signal, and the caller must not
 * expect a waitable child.
 */
i2p::expected<Channel, std::string> SpawnDetachedWorkerWithRoot (const char * role, int rootFd);
/**
 * Spawn a worker role that takes the delegated storage root as fd4.
 *
 * @param rootFd validated directory owned by the worker identity; it is
 *        duplicated above descriptor 5 and closed in the parent, so the child's
 *        capability cannot alias the identity record or the channel.
 * @pre The role must be one of the `*-inherited` roles; any other pairing is
 *      refused rather than silently downgraded.
 */
i2p::expected<ChildProcess, std::string> SpawnWorkerWithRoot (
	const char * role, std::string_view executable, int rootFd);

/**
 * Freeze the executable used for every spawn.
 *
 * Called once during startup with the resolved `argv[0]`. The candidate must
 * be absolute, free of NUL, a regular file owned by root or this process,
 * without group/world write permission, executable and of bounded size; the
 * stored value is the canonical `realpath()`. An unusable candidate clears the
 * value, so ExecutablePath() returns empty and every spawn fails closed. There
 * is no `$PATH` search and no `/proc/self/exe` fallback: on OpenBSD that
 * fallback was an invalid path, and a PATH search is a substitution surface.
 *
 * The process-wide process-creation gate consulted by every spawn helper is
 * declared in Supervisor.h (`ForbidProcessCreation()` /
 * `IsProcessCreationForbidden()`): one flag, one owner, one definition.
 */
void SetExecutablePath (std::string path);
/** Frozen executable path, or empty when it was never validated. */
std::string ExecutablePath ();
}

#endif
