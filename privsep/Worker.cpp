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
 * The executable is a frozen, validated absolute path (no $PATH search and no
 * /proc fallback), the forked child drops to the configured service identity
 * and pins its standard descriptors, and the dedicated descriptor 5 carries a
 * CSPRNG nonce plus the child's post-drop uid/gid so the exec'ed worker can
 * prove both the spawner and the identity it was started with.
 *
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "Worker.h"
#include "Credentials.h"
#include "LinuxSandbox.h"
#include "LogWorker.h"
#include "Supervisor.h"

#ifndef _WIN32

#include <cerrno>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <mutex>
#include <poll.h>
#include <signal.h>
#include <sched.h>
#include <cstdio>
#include <sys/resource.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#if defined(__linux__)
#include <sys/random.h>
#elif defined(__OpenBSD__)
// arc4random_buf(3) is the base-system CSPRNG on OpenBSD.
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

	/**
	 * Retrieve CSPRNG bytes without depending on an OpenSSL symbol in this
	 * archive. getrandom(2) is the kernel CSPRNG on Linux; arc4random_buf(3)
	 * is the base-system CSPRNG on OpenBSD. Any short or failed read is a
	 * failure: a truncated nonce must never be used.
	 */
	bool PlatformRandomBytes (uint8_t * buffer, size_t size)
	{
		if (!buffer || size == 0) return false;
#if defined(__linux__)
		size_t done = 0;
		while (done < size)
		{
			const auto count = ::getrandom (buffer + done, size - done, 0);
			if (count < 0)
			{
				if (errno == EINTR) continue;
				return false;
			}
			if (count == 0) return false;
			done += static_cast<size_t> (count);
		}
		return true;
#elif defined(__OpenBSD__)
		::arc4random_buf (buffer, size);
		return true;
#else
		(void)buffer;
		(void)size;
		return false;
#endif
	}

	/** Best-effort wipe of a stack copy of the spawn nonce or record. */
	void WipeBytes (uint8_t * buffer, size_t size) noexcept
	{
		volatile uint8_t * wipe = buffer;
		for (size_t i = 0; i < size; ++i) wipe[i] = 0;
	}

	void WriteU32BE (uint8_t * out, uint32_t value) noexcept
	{
		out[0] = static_cast<uint8_t> (value >> 24);
		out[1] = static_cast<uint8_t> (value >> 16);
		out[2] = static_cast<uint8_t> (value >> 8);
		out[3] = static_cast<uint8_t> (value);
	}

	uint32_t ReadU32BE (const uint8_t * in) noexcept
	{
		return (static_cast<uint32_t> (in[0]) << 24) | (static_cast<uint32_t> (in[1]) << 16)
			| (static_cast<uint32_t> (in[2]) << 8) | static_cast<uint32_t> (in[3]);
	}

	/**
	 * Format a hello record into a fixed buffer.
	 *
	 * Async-signal-safe (no allocation), so the forked child can build the
	 * record it writes after its credential drop. An unknown role, an all-zero
	 * nonce and uid 0 are refused.
	 */
	bool FormatWorkerHello (std::array<uint8_t, WORKER_HELLO_SIZE> & record, WorkerRole role,
		const std::array<uint8_t, WORKER_HELLO_NONCE_SIZE> & nonce, uint32_t uid, uint32_t gid) noexcept
	{
		const auto * name = WorkerRoleName (role);
		if (!name || !*name || uid == 0) return false;
		bool nonZero = false;
		for (auto byte : nonce) nonZero = nonZero || byte != 0;
		if (!nonZero) return false;
		record[0] = static_cast<uint8_t> (role);
		std::copy (nonce.begin (), nonce.end (), record.begin () + 1);
		WriteU32BE (record.data () + 1 + WORKER_HELLO_NONCE_SIZE, uid);
		WriteU32BE (record.data () + 1 + WORKER_HELLO_NONCE_SIZE + 4, gid);
		return true;
	}

	/**
	 * Post-fork: publish the identity record at descriptor 5.
	 *
	 * Async-signal-safe only (dup2/fcntl/_exit). A spawn without a record
	 * aborts the exec instead of starting an unbound worker.
	 */
	void InstallWorkerHelloFd (int readFd)
	{
		if (readFd < 0) _exit (127);
		if (::dup2 (readFd, 5) < 0) _exit (127);
		const int descriptorFlags = ::fcntl (5, F_GETFD);
		if (descriptorFlags < 0 || ::fcntl (5, F_SETFD, descriptorFlags & ~FD_CLOEXEC) < 0) _exit (127);
		const int statusFlags = ::fcntl (5, F_GETFL);
		if (statusFlags < 0 || ::fcntl (5, F_SETFL, statusFlags & ~O_NONBLOCK) < 0) _exit (127);
	}

	/**
	 * Post-fork: create the one-shot identity pipe and write the record.
	 *
	 * The pipe is created in the process that execs, and only that process
	 * holds the write end, so the worker reads exactly one record and observes
	 * EOF: no other process can append a second record or hold the pipe open.
	 * The nonce arrives through the fork from the parent, which generated it
	 * before forking; the uid/gid are the child's own post-drop identity.
	 *
	 * Async-signal-safe only (pipe/fcntl/dup2/write/close), no allocation.
	 */
	bool WriteWorkerHelloRecord (
		WorkerRole role, const std::array<uint8_t, WORKER_HELLO_NONCE_SIZE> & nonce, uint32_t uid, uint32_t gid)
	{
		int fds[2] = { -1, -1 };
#if defined(__linux__)
		if (::pipe2 (fds, O_CLOEXEC) != 0) return false;
#else
		if (::pipe (fds) != 0) return false;
		if (::fcntl (fds[0], F_SETFD, FD_CLOEXEC) != 0 || ::fcntl (fds[1], F_SETFD, FD_CLOEXEC) != 0)
		{
			::close (fds[0]);
			::close (fds[1]);
			return false;
		}
#endif
		// Keep both ends above the worker's fixed descriptors 3 (channel),
		// 4 (inherited root) and 5 (record), so a later dup2 cannot alias them.
		const int readEnd = ::fcntl (fds[0], F_DUPFD_CLOEXEC, 6);
		const int writeEnd = ::fcntl (fds[1], F_DUPFD_CLOEXEC, 6);
		::close (fds[0]);
		::close (fds[1]);
		if (readEnd < 0 || writeEnd < 0)
		{
			if (readEnd >= 0) ::close (readEnd);
			if (writeEnd >= 0) ::close (writeEnd);
			return false;
		}
		std::array<uint8_t, WORKER_HELLO_SIZE> record { };
		if (!FormatWorkerHello (record, role, nonce, uid, gid))
		{
			::close (readEnd);
			::close (writeEnd);
			return false;
		}
		InstallWorkerHelloFd (readEnd);
		::close (readEnd);
		size_t done = 0;
		bool written = true;
		while (done < record.size ())
		{
			const auto count = ::write (writeEnd, record.data () + done, record.size () - done);
			if (count < 0 && errno == EINTR) continue;
			if (count <= 0)
			{
				written = false;
				break;
			}
			done += static_cast<size_t> (count);
		}
		// Closing the only write end is what makes the worker's oversized-record
		// peek observe EOF instead of a live writer.
		::close (writeEnd);
		WipeBytes (record.data (), record.size ());
		return written;
	}

	// Forward declarations: the child setup below runs before the definitions
	// at the bottom of this translation unit.
	bool RoleWantsNetworkNamespace (WorkerRole role);
	extern dev_t g_ExecutableDevice;
	extern ino_t g_ExecutableInode;
	extern off_t g_ExecutableSize;

	/**
	 * Post-fork child setup shared by the direct and detached spawn paths.
	 *
	 * Async-signal-safe only: no allocation, no stdio, no setenv and no C++
	 * objects that can take a lock. Never returns: every failure is `_exit`
	 * so no half-configured worker can reach execve.
	 */
	[[noreturn]] void ExecWorkerChild (WorkerRole role, const std::string & executable, int channelFd,
		int parentChannelFd, int inheritedRootFd, const std::array<uint8_t, WORKER_HELLO_NONCE_SIZE> & nonce,
		const WorkerCredentials & credentials)
	{
		// Restarts fork from the multi-threaded router. Drop to the configured
		// service identity first, so every later descriptor operation already
		// runs without the daemon's privilege.
		sigset_t empty;
		sigemptyset (&empty);
		sigprocmask (SIG_SETMASK, &empty, nullptr);
		// An ignored disposition survives execve. Reset the signals the
		// supervisor uses for stop so a worker cannot resist shutdown.
		struct sigaction defaultAction { };
		defaultAction.sa_handler = SIG_DFL;
		sigemptyset (&defaultAction.sa_mask);
		for (int signal : { SIGTERM, SIGINT, SIGHUP, SIGPIPE, SIGCHLD }) ::sigaction (signal, &defaultAction, nullptr);
#if defined(__linux__)
		// Do not let a setuid/file-capability executable raise privileges
		// before the worker installs its own seccomp/capability policy.
		if (prctl (PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) _exit (127);
		// A fresh network namespace is only possible while this process still
		// holds CAP_SYS_ADMIN. It is best-effort because seccomp denies every
		// socket for storage/log; never claim it when the kernel refuses.
#ifdef SYS_unshare
#ifdef CLONE_NEWNET
		if (RoleWantsNetworkNamespace (role)) (void)::syscall (SYS_unshare, CLONE_NEWNET);
#endif
#endif
		// Capabilities must be dropped here, while still privileged. Doing it
		// after setuid/capset in the exec'ed worker is a no-op (EPERM), which
		// would let a file capability or setuid binary regain authority.
		if (!PrepareCapabilitiesBeforeCredentialDrop ()) _exit (127);
#endif
		if (!DropWorkerCredentialsInChild (credentials)) _exit (127);
#if defined(__linux__)
		if (!ClearCapabilitiesAfterCredentialDrop ()) _exit (127);
#endif
		// The socketpair may have been allocated below descriptor 3 if the
		// caller inherited closed standard descriptors. Install the fixed slots
		// before pinning stdio, so a low-numbered channel cannot be overwritten
		// by /dev/null.
		if (parentChannelFd >= 0 && parentChannelFd != channelFd) ::close (parentChannelFd);
		if (channelFd != 3)
		{
			if (::dup2 (channelFd, 3) < 0) _exit (127);
			::close (channelFd);
		}
		int flags = ::fcntl (3, F_GETFD);
		if (flags < 0 || ::fcntl (3, F_SETFD, flags & ~FD_CLOEXEC) < 0) _exit (127);
		if (inheritedRootFd >= 0)
		{
			if (::dup2 (inheritedRootFd, 4) < 0) _exit (127);
		}
		else
		{
			// A later dup2 or CloseFrom(6) would not remove an inherited fd4, and
			// a role without a storage root must not receive one.
			::close (4);
		}
		// A terminal, service-manager log pipe or socket-activation descriptor
		// must never be writable by a worker.
		if (!PinStandardDescriptors ()) _exit (127);
		// The record carries the identity the worker actually has, so a partial
		// drop cannot be hidden from the exec'ed process.
		if (!WriteWorkerHelloRecord (
				role, nonce, static_cast<uint32_t> (::geteuid ()), static_cast<uint32_t> (::getegid ())))
			_exit (127);
		// dup2 cleared FD_CLOEXEC on fd3/fd4/fd5; everything above is foreign.
		CloseFrom (6);
#if defined(__linux__)
		// Close the freeze/exec gap: a path replacement between SetExecutablePath
		// and this spawn must not execute a different image. OpenBSD uses an
		// x-only unveil of the frozen path and has no rpath at this point.
		struct stat frozenStatus { };
		if (::stat (executable.c_str (), &frozenStatus) != 0 || !S_ISREG (frozenStatus.st_mode)
			|| frozenStatus.st_dev != g_ExecutableDevice || frozenStatus.st_ino != g_ExecutableInode
			|| frozenStatus.st_size != g_ExecutableSize)
			_exit (127);
#endif
		// argv[0] is the frozen absolute path, so an exec'ed worker (the
		// supervisor) can freeze the same path without a PATH or /proc lookup.
		char fdText[] = "3";
		char * const argv[] = { const_cast<char *> (executable.c_str ()), const_cast<char *> ("--privsep-worker"),
			const_cast<char *> (WorkerRoleName (role)), fdText, nullptr };
		::execv (executable.c_str (), argv);
		_exit (127);
	}
}

bool PinStandardDescriptors ()
{
	// Opened without O_CLOEXEC: whichever descriptor ends up on 0, 1 or 2 must
	// survive execve, and dup2 only clears the flag on the target.
	const int nullFd = ::open ("/dev/null", O_RDWR);
	if (nullFd < 0) return false;
	for (int fd = 0; fd <= 2; ++fd)
	{
		if (fd == nullFd) continue;
		if (::dup2 (nullFd, fd) < 0)
		{
			::close (nullFd);
			return false;
		}
	}
	if (nullFd > 2) ::close (nullFd);
	return true;
}

bool WorkerRoleTakesInheritedRoot (WorkerRole role)
{
	switch (role)
	{
	case WorkerRole::RouterStorageInherited:
	case WorkerRole::DataStorageInherited:
	case WorkerRole::TorrentStorageInherited:
	// The pre-pledge broker receives the immutable storage root at fd4 and
	// passes it to each worker it spawns.
	case WorkerRole::TorrentStorageSupervisor: return true;
	case WorkerRole::Log:
	case WorkerRole::Supervisor:
	case WorkerRole::RouterStorage:
	case WorkerRole::DataStorage:
	case WorkerRole::TorrentStorage: return false;
	}
	return false;
}

namespace
{
	/** Roles that benefit from an empty network namespace when one is available. */
	bool RoleWantsNetworkNamespace (WorkerRole role)
	{
		switch (role)
		{
		case WorkerRole::Log:
		case WorkerRole::RouterStorage:
		case WorkerRole::RouterStorageInherited:
		case WorkerRole::DataStorage:
		case WorkerRole::DataStorageInherited:
		case WorkerRole::TorrentStorage:
		case WorkerRole::TorrentStorageInherited: return true;
		default: return false;
		}
	}
}

const char * WorkerRoleName (WorkerRole role)
{
	switch (role)
	{
	case WorkerRole::Log: return "log";
	case WorkerRole::Supervisor: return "supervisor";
	case WorkerRole::RouterStorage: return "router-storage";
	case WorkerRole::RouterStorageInherited: return "router-storage-inherited";
	case WorkerRole::TorrentStorage: return "torrent-storage";
	case WorkerRole::TorrentStorageInherited: return "torrent-storage-inherited";
	case WorkerRole::TorrentStorageSupervisor: return "torrent-storage-supervisor";
	case WorkerRole::DataStorage: return "data-storage";
	case WorkerRole::DataStorageInherited: return "data-storage-inherited";
	}
	return nullptr;
}

std::optional<WorkerRole> WorkerRoleFromName (std::string_view name)
{
	// A fixed table keeps the argv spelling and the hello identity in one
	// place: adding a role without a wire id is impossible here.
	static constexpr std::array<WorkerRole, 9> roles { WorkerRole::Log, WorkerRole::Supervisor,
		WorkerRole::RouterStorage, WorkerRole::RouterStorageInherited, WorkerRole::TorrentStorage,
		WorkerRole::TorrentStorageInherited, WorkerRole::TorrentStorageSupervisor, WorkerRole::DataStorage,
		WorkerRole::DataStorageInherited };
	for (auto role : roles)
		if (name == WorkerRoleName (role)) return role;
	return std::nullopt;
}

std::optional<std::vector<uint8_t>> EncodeWorkerHello (
	WorkerRole role, std::span<const uint8_t> nonce, uint32_t uid, uint32_t gid)
{
	if (nonce.size () != WORKER_HELLO_NONCE_SIZE) return std::nullopt;
	std::array<uint8_t, WORKER_HELLO_NONCE_SIZE> fixed { };
	std::copy (nonce.begin (), nonce.end (), fixed.begin ());
	std::array<uint8_t, WORKER_HELLO_SIZE> record { };
	if (!FormatWorkerHello (record, role, fixed, uid, gid)) return std::nullopt;
	std::vector<uint8_t> hello (record.begin (), record.end ());
	WipeBytes (record.data (), record.size ());
	return hello;
}

std::optional<WorkerHelloRecord> DecodeWorkerHello (std::span<const uint8_t> hello)
{
	if (hello.size () != WORKER_HELLO_SIZE) return std::nullopt;
	const auto role = static_cast<WorkerRole> (hello[0]);
	if (!WorkerRoleName (role)) return std::nullopt;
	bool nonZero = false;
	for (auto byte : hello.subspan (1, WORKER_HELLO_NONCE_SIZE)) nonZero = nonZero || byte != 0;
	if (!nonZero) return std::nullopt;
	WorkerHelloRecord record;
	record.role = role;
	record.uid = ReadU32BE (hello.data () + 1 + WORKER_HELLO_NONCE_SIZE);
	record.gid = ReadU32BE (hello.data () + 1 + WORKER_HELLO_NONCE_SIZE + 4);
	// A worker must never run as the superuser; such a record is unusable.
	if (record.uid == 0) return std::nullopt;
	return record;
}

std::optional<std::vector<uint8_t>> CreateWorkerHello (WorkerRole role, uint32_t uid, uint32_t gid)
{
	if (!WorkerRoleName (role) || uid == 0) return std::nullopt;
	std::array<uint8_t, WORKER_HELLO_NONCE_SIZE> nonce { };
	if (!PlatformRandomBytes (nonce.data (), nonce.size ()))
	{
		// Fail closed: a worker without a fresh spawn nonce is not startable.
		// The error is reported by the caller; the nonce itself never exists.
		std::fprintf (stderr, "privsep worker: cannot obtain spawn identity randomness\n");
		return std::nullopt;
	}
	std::array<uint8_t, WORKER_HELLO_SIZE> record { };
	const bool encoded = FormatWorkerHello (record, role, nonce, uid, gid);
	WipeBytes (nonce.data (), nonce.size ());
	if (!encoded) return std::nullopt;
	std::vector<uint8_t> hello (record.begin (), record.end ());
	WipeBytes (record.data (), record.size ());
	return hello;
}

std::optional<WorkerHelloRecord> ReadWorkerHello (int fd)
{
	if (fd < 0) return std::nullopt;
	std::array<uint8_t, WORKER_HELLO_SIZE> buffer { };
	size_t done = 0;
	while (done < buffer.size ())
	{
		const auto count = ::read (fd, buffer.data () + done, buffer.size () - done);
		if (count < 0 && errno == EINTR) continue;
		if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
		{
			pollfd ready { fd, POLLIN, 0 };
			int polled;
			do polled = ::poll (&ready, 1, 5000);
			while (polled < 0 && errno == EINTR);
			if (polled <= 0) break;
			continue;
		}
		if (count <= 0) break;
		done += static_cast<size_t> (count);
	}
	if (done != WORKER_HELLO_SIZE)
	{
		::close (fd);
		return std::nullopt;
	}
	// Peek one byte without blocking. The spawn path creates the pipe in the
	// exec'ing child only, so a valid record is followed by EOF. A byte, or a
	// writer that is still open, means a duplicated or oversized hello.
	const int statusFlags = ::fcntl (fd, F_GETFL);
	bool exactlyOne = false;
	if (statusFlags >= 0 && ::fcntl (fd, F_SETFL, statusFlags | O_NONBLOCK) == 0)
	{
		uint8_t extra = 0;
		for (int attempt = 0; attempt < 2; ++attempt)
		{
			const auto count = ::read (fd, &extra, 1);
			if (count == 0)
			{
				exactlyOne = true;
				break;
			}
			if (count < 0 && errno == EINTR) continue;
			// count > 0: a second record. EAGAIN: the writer is still open.
			break;
		}
		(void)::fcntl (fd, F_SETFL, statusFlags);
	}
	::close (fd);
	if (!exactlyOne) return std::nullopt;
	return DecodeWorkerHello (buffer);
}

namespace
{
	/** Fixed caps so a hostile path or binary cannot exhaust a spawn. */
	constexpr size_t MAX_EXECUTABLE_PATH_SIZE = 4096;
	constexpr uint64_t MAX_EXECUTABLE_SIZE = 512ull * 1024ull * 1024ull;

	std::mutex g_ExecutableMutex;
	std::string g_ExecutablePath;
	// Identity of the frozen image. Re-checked in the Linux child immediately
	// before execve so a replacement between freeze and spawn is refused.
	dev_t g_ExecutableDevice = 0;
	ino_t g_ExecutableInode = 0;
	off_t g_ExecutableSize = 0;

	/**
	 * Validate a candidate executable before it is frozen.
	 *
	 * Absolute, free of NUL, a regular file owned by root or this process,
	 * without group/world write permission, executable and of bounded size.
	 * `realpath()` stores the canonical value so a later relative-path or
	 * symlink ambiguity cannot redirect a spawn.
	 */
	bool ValidateExecutablePath (const std::string & candidate, std::string & canonical)
	{
		canonical.clear ();
		if (candidate.empty () || candidate.size () >= MAX_EXECUTABLE_PATH_SIZE) return false;
		if (candidate.find ('\0') != std::string::npos) return false;
		// A bare program name would require a $PATH search, which is exactly the
		// substitution surface this function removes.
		if (candidate.front () != '/') return false;
		char resolved[MAX_EXECUTABLE_PATH_SIZE] = { };
		if (!realpath (candidate.c_str (), resolved)) return false;
		struct stat status { };
		if (stat (resolved, &status) != 0) return false;
		if (!S_ISREG (status.st_mode)) return false;
		if (status.st_uid != 0 && status.st_uid != ::geteuid ()) return false;
		if ((status.st_mode & (S_IWGRP | S_IWOTH)) != 0) return false;
		if (status.st_size <= 0 || static_cast<uint64_t> (status.st_size) > MAX_EXECUTABLE_SIZE) return false;
#if defined(AT_EACCESS)
		if (faccessat (AT_FDCWD, resolved, X_OK, AT_EACCESS) != 0) return false;
#else
		if (access (resolved, X_OK) != 0) return false;
#endif
		// The containing directory must not be writable by another uid: a
		// writable parent can swap the image between this check and execve.
		const std::string path (resolved);
		const auto slash = path.rfind ('/');
		const std::string parent = slash == 0 ? std::string ("/") : path.substr (0, slash);
		struct stat parentStatus { };
		if (stat (parent.c_str (), &parentStatus) != 0 || !S_ISDIR (parentStatus.st_mode)
			|| (parentStatus.st_uid != 0 && parentStatus.st_uid != ::geteuid ())
			|| (parentStatus.st_mode & (S_IWGRP | S_IWOTH)) != 0)
			return false;
		canonical = resolved;
		return true;
	}

	/**
	 * Cheap syntactic check used on every spawn.
	 *
	 * No filesystem access: the core or supervisor may already be past its
	 * runtime pledge, where only the already-frozen value may be used.
	 */
	bool IsSpawnableExecutablePath (const std::string & path)
	{
		return !path.empty () && path.size () < MAX_EXECUTABLE_PATH_SIZE && path.front () == '/'
			&& path.find ('\0') == std::string::npos;
	}
}

void SetExecutablePath (std::string path)
{
	std::string canonical;
	if (!ValidateExecutablePath (path, canonical)) canonical.clear ();
	struct stat status { };
	const bool haveStatus = !canonical.empty () && stat (canonical.c_str (), &status) == 0 && S_ISREG (status.st_mode);
	std::lock_guard lock (g_ExecutableMutex);
	g_ExecutablePath = std::move (canonical);
	g_ExecutableDevice = haveStatus ? status.st_dev : 0;
	g_ExecutableInode = haveStatus ? status.st_ino : 0;
	g_ExecutableSize = haveStatus ? status.st_size : 0;
}

std::string ExecutablePath ()
{
	std::lock_guard lock (g_ExecutableMutex);
	return g_ExecutablePath;
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

static i2p::expected<ChildProcess, std::string> SpawnWorkerInternal (
	const char * role, std::string_view executableOverride, int rootFd)
{
	if (IsProcessCreationForbidden ())
		return i2p::unexpected (std::string ("worker process creation is forbidden after the runtime sandbox"));
	if (!role || !*role) return i2p::unexpected ("missing worker role");
	const auto roleId = WorkerRoleFromName (role);
	if (!roleId) return i2p::unexpected ("unknown worker role");
	// The daemon must have resolved its worker identity before the first
	// spawn. An unresolved (or unauthorised root) identity is refused here
	// instead of being discovered by the exec'ed worker.
	const WorkerCredentials credentials = GetWorkerCredentials ();
	if (credentials.uid == 0)
		return i2p::unexpected (std::string ("worker identity is unresolved: refusing to spawn as uid 0"));
	if (rootFd >= 0 && !WorkerRoleTakesInheritedRoot (*roleId))
		return i2p::unexpected (std::string ("worker role does not accept an inherited root"));
	if (rootFd < 0 && WorkerRoleTakesInheritedRoot (*roleId))
		return i2p::unexpected (std::string ("worker role requires an inherited root"));
	if (rootFd >= 0)
	{
		struct stat rootStatus { };
		if (::fstat (rootFd, &rootStatus) != 0 || !S_ISDIR (rootStatus.st_mode)
			|| rootStatus.st_uid != static_cast<uid_t> (credentials.uid) || (rootStatus.st_mode & 0022))
			return i2p::unexpected (std::string ("worker root is not a safe delegated directory"));
	}
	const std::string executable = executableOverride.empty () ? ExecutablePath () : std::string (executableOverride);
	if (!IsSpawnableExecutablePath (executable))
		return i2p::unexpected (std::string ("worker executable path is not frozen"));
	// Bind the exec'ed role to this spawn. The nonce is generated here and
	// travels to the child through fork memory; only the child, which knows its
	// post-drop identity, writes the record.
	std::array<uint8_t, WORKER_HELLO_NONCE_SIZE> nonce { };
	if (!PlatformRandomBytes (nonce.data (), nonce.size ()))
		return i2p::unexpected (std::string ("cannot obtain spawn identity randomness"));
	auto pair = Channel::CreatePair ();
	if (!pair) return i2p::unexpected (pair.error ());
	auto & parent = pair->first;
	auto & child = pair->second;
	// https://man.openbsd.org/pledge.2: sendfd/recvfd exclude directories.
	// Preserve the validated root through exec as fd4, never through SCM_RIGHTS.
	// Keep it above descriptor 5 so it cannot alias the identity record.
	const int inheritedRoot = rootFd >= 0 ? ::fcntl (rootFd, F_DUPFD_CLOEXEC, 6) : -1;
	if (rootFd >= 0 && inheritedRoot < 0) return i2p::unexpected (std::string ("cannot duplicate inherited root"));
	const int channelFd = child.Fd ();
	const int parentChannelFd = parent.Fd ();
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
		ExecWorkerChild (*roleId, executable, channelFd, parentChannelFd, inheritedRoot, nonce, credentials);
	}
	// Parent
	WipeBytes (nonce.data (), nonce.size ());
	if (inheritedRoot >= 0) ::close (inheritedRoot);
	child.Close ();
	// The worker runs as `credentials.uid` and, for a direct fork, is exactly
	// `pid`. Binding both closes the stale-creator ambiguity of SO_PEERCRED and
	// the same-uid frame-injection window.
	parent.ExpectPeer (
		PeerExpectation { credentials.uid, credentials.gid, static_cast<uint32_t> (pid), true, true, false, true });
	return ChildProcess::Adopt (pid, std::move (parent));
}

i2p::expected<ChildProcess, std::string> SpawnWorker (const char * role, std::string_view executable)
{
	if (IsProcessCreationForbidden ())
		return i2p::unexpected (std::string ("worker process creation is forbidden after the runtime sandbox"));
	return SpawnWorkerInternal (role, executable, -1);
}
i2p::expected<ChildProcess, std::string> SpawnWorkerWithRoot (
	const char * role, std::string_view executable, int rootFd)
{
	if (IsProcessCreationForbidden ())
		return i2p::unexpected (std::string ("worker process creation is forbidden after the runtime sandbox"));
	if (rootFd < 0) return i2p::unexpected (std::string ("missing inherited root"));
	return SpawnWorkerInternal (role, executable, rootFd);
}

static i2p::expected<Channel, std::string> SpawnDetachedWorkerInternal (const char * role, int rootFd)
{
	if (IsProcessCreationForbidden ())
		return i2p::unexpected (std::string ("worker process creation is forbidden after the runtime sandbox"));
	if (!role || !*role) return i2p::unexpected (std::string ("missing worker role"));
	const auto roleId = WorkerRoleFromName (role);
	if (!roleId) return i2p::unexpected ("unknown worker role");
	const WorkerCredentials credentials = GetWorkerCredentials ();
	if (credentials.uid == 0)
		return i2p::unexpected (std::string ("worker identity is unresolved: refusing to spawn as uid 0"));
	const auto executable = ExecutablePath ();
	if (!IsSpawnableExecutablePath (executable))
		return i2p::unexpected (std::string ("worker executable path is not frozen"));
	std::array<uint8_t, WORKER_HELLO_NONCE_SIZE> nonce { };
	if (!PlatformRandomBytes (nonce.data (), nonce.size ()))
		return i2p::unexpected (std::string ("cannot obtain spawn identity randomness"));
	auto pair = Channel::CreatePair ();
	if (!pair) return i2p::unexpected (pair.error ());
	if (rootFd >= 0 && !WorkerRoleTakesInheritedRoot (*roleId))
		return i2p::unexpected (std::string ("worker role does not accept an inherited root"));
	if (rootFd < 0 && WorkerRoleTakesInheritedRoot (*roleId))
		return i2p::unexpected (std::string ("worker role requires an inherited root"));
	if (rootFd >= 0)
	{
		struct stat rootStatus { };
		if (::fstat (rootFd, &rootStatus) != 0 || !S_ISDIR (rootStatus.st_mode)
			|| rootStatus.st_uid != static_cast<uid_t> (credentials.uid) || (rootStatus.st_mode & 0022))
			return i2p::unexpected (std::string ("worker root is not a safe delegated directory"));
	}
	const int inheritedRoot = rootFd >= 0 ? ::fcntl (rootFd, F_DUPFD_CLOEXEC, 6) : -1;
	if (rootFd >= 0 && inheritedRoot < 0) return i2p::unexpected (std::string ("cannot duplicate worker root"));
	const int channelFd = pair->second.Fd ();
	const int parentChannelFd = pair->first.Fd ();
	const pid_t intermediate = ::fork ();
	if (intermediate < 0)
	{
		if (inheritedRoot >= 0) ::close (inheritedRoot);
		return i2p::unexpected (std::string ("cannot fork worker intermediary"));
	}
	if (intermediate == 0)
	{
		// Async-signal-safe only between forks and exec. The intermediate is reaped
		// before pledge; init subsequently reaps the broker when its channel closes.
		const pid_t broker = ::fork ();
		if (broker < 0) _exit (127);
		if (broker > 0) _exit (0);
		ExecWorkerChild (*roleId, executable, channelFd, parentChannelFd, inheritedRoot, nonce, credentials);
	}
	WipeBytes (nonce.data (), nonce.size ());
	if (inheritedRoot >= 0) ::close (inheritedRoot);
	pair->second.Close ();
	if (!WaitForChildExitBounded (intermediate, -1, 5000))
	{
		::kill (intermediate, SIGKILL);
		(void)WaitForChildExitBounded (intermediate, -1, 500);
		return i2p::unexpected (std::string ("worker intermediary did not exit"));
	}
	// The detached grandchild's pid is not the intermediary's, so bind the
	// uid/gid only. A same-uid frame still has to come from the worker's
	// delegation; the pid is checked on every direct (non-detached) spawn.
	pair->first.ExpectPeer (PeerExpectation { credentials.uid, credentials.gid, 0, true, false, false, true });
	return std::move (pair->first);
}

i2p::expected<Channel, std::string> SpawnDetachedWorker (const char * role)
{
	if (IsProcessCreationForbidden ())
		return i2p::unexpected (std::string ("worker process creation is forbidden after the runtime sandbox"));
	return SpawnDetachedWorkerInternal (role, -1);
}
i2p::expected<Channel, std::string> SpawnDetachedWorkerWithRoot (const char * role, int rootFd)
{
	if (IsProcessCreationForbidden ())
		return i2p::unexpected (std::string ("worker process creation is forbidden after the runtime sandbox"));
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
