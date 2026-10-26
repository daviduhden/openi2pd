/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: RunWorker.
 *
 * Validates the worker identity hello, the worker role, the worker identity
 * and channel descriptor, requiring an inherited SOCK_SEQPACKET socket in
 * descriptor 3 and refusing euid 0 or a hello whose uid/gid is not this
 * process's, then dispatches to the requested worker. The identity hello
 * arrives on descriptor 5 and is consumed before any frame is served, so a
 * process cannot choose a role by invoking the executable by hand. Standard
 * descriptors are pinned to /dev/null first, so a directly invoked worker
 * cannot write to the terminal or a service-manager pipe. Windows builds
 * provide a failing stub.
 *
 */

#include "RunWorker.h"
#include "LinuxSandbox.h"
#include "Credentials.h"
#include "DataStorage.h"
#include "LogWorker.h"
#include "Supervisor.h"
#include "TorrentStorage.h"
#include "RouterStorage.h"
#include "Worker.h"

#ifndef _WIN32

#include <cerrno>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <string>
#include <system_error>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

namespace i2p::privsep
{
namespace
{
	/** Descriptor the spawner uses for the one-shot identity record. */
	constexpr int WORKER_HELLO_FD = 5;
}

int RunWorker (std::string_view role, std::string_view fdArgument)
{
	// Pin the standard descriptors before anything else: a directly invoked
	// worker must not read the operator's stdin or write to a terminal, a
	// service-manager log pipe or an inherited socket-activation descriptor.
	// Diagnostics from here on are deliberately discarded.
	if (!PinStandardDescriptors ())
	{
		std::fprintf (stderr, "privsep worker: cannot pin standard descriptors\n");
		return 1;
	}
	if (fdArgument.empty ())
	{
		std::fprintf (stderr, "privsep worker: invalid channel fd\n");
		return 1;
	}
	// The role on the command line is only a claim. Consume the spawn record
	// first and require it to name the same role; a missing, short, duplicated
	// or unknown record exits before the channel or the sandbox is touched.
	const auto declared = WorkerRoleFromName (role);
	const auto hello = ReadWorkerHello (WORKER_HELLO_FD);
	if (!declared || !hello || hello->role != *declared)
	{
		// Never include the record or its nonce in the diagnostic.
		std::fprintf (stderr, "privsep worker: missing or invalid spawn identity record\n");
		return 1;
	}
	int fd = -1;
	const char * begin = fdArgument.data ();
	const char * end = begin + fdArgument.size ();
	auto parsed = std::from_chars (begin, end, fd);
	// SpawnWorkerInternal always moves the channel to descriptor 3 and uses
	// descriptor 4 for an inherited storage root. Rejecting any other number
	// prevents a manually invoked worker from aliasing the control channel and
	// its root authority or landing on an unrelated inherited descriptor.
	if (parsed.ec != std::errc () || parsed.ptr != end || fd != 3)
	{
		std::fprintf (stderr, "privsep worker: channel fd must be 3\n");
		return 1;
	}
	// The descriptor must already be an inherited socket. This prevents a
	// manually invoked worker from treating a regular file as its channel.
	struct stat info { };
	int socketType = 0;
	socklen_t socketTypeLength = sizeof (socketType);
	if (fstat (fd, &info) != 0 || !S_ISSOCK (info.st_mode) || fcntl (fd, F_GETFD) < 0
		|| getsockopt (fd, SOL_SOCKET, SO_TYPE, &socketType, &socketTypeLength) != 0 || socketType != SOCK_SEQPACKET)
	{
		std::fprintf (stderr, "privsep worker: channel fd is not a SOCK_SEQPACKET socket\n");
		return 1;
	}
	if (geteuid () == 0)
	{
		// Worker policies assume an already unprivileged service identity. A
		// root-owned worker would turn every sandbox bypass into a full
		// privilege compromise; refuse instead of silently running as root.
		std::fprintf (stderr, "privsep worker: refusing to run with euid 0\n");
		return 1;
	}
	// The record is written by the forked child after its credential drop, so a
	// mismatch means the spawner dropped to a different identity than the one
	// recorded, or the record was not produced by this spawn at all.
	if (hello->uid != static_cast<uint32_t> (geteuid ()) || hello->gid != static_cast<uint32_t> (getegid ()))
	{
		std::fprintf (stderr, "privsep worker: spawn identity does not match the worker credentials\n");
		return 1;
	}
#if defined(__linux__)
	// The spawn child must have set no_new_privs and emptied the capability
	// set before execve. Verify it here, before any role code reads a frame:
	// a partially applied spawn must not serve untrusted input.
	auto privileges = VerifyWorkerPrivileges ();
	if (!privileges)
	{
		std::fprintf (stderr, "privsep worker: %s\n", privileges.error ().c_str ());
		return 1;
	}
#endif
	// A role that does not own a storage root must never inherit descriptor 4.
	// The spawn helper already refuses the pairing; close it here as defence in
	// depth for a manually invoked or misconfigured worker.
	if (!WorkerRoleTakesInheritedRoot (hello->role)) ::close (4);
	// The identity does not survive execve except in this record. Republish the
	// verified uid/gid so a worker that spawns its own children (the supervisor
	// or the storage broker) checks the same identity. It is deliberately not
	// marked "configured": this process is already running as that identity, so
	// its children inherit it and need no further setuid/setgroups. That keeps
	// the `id` promise out of every post-exec worker policy.
	SetWorkerCredentials (WorkerCredentials { false, hello->uid, hello->gid });
	// Dispatch on the validated identity, not on the raw argument, so the two
	// cannot diverge if a role is renamed on one side only.
	switch (hello->role)
	{
	case WorkerRole::TorrentStorageSupervisor: return RunTorrentStorageSupervisor (fd);
#if defined(__OpenBSD__)
	// Directory descriptors only arrive as fd4 on OpenBSD, where pledge
	// forbids directory SCM_RIGHTS. On Linux these roles would interpret any
	// inherited fd4 as the write root, so they must not be dispatchable.
	case WorkerRole::TorrentStorageInherited: return RunTorrentStorageWorker (fd, 4);
	case WorkerRole::RouterStorageInherited: return RunRouterStorageWorker (fd, 4);
	case WorkerRole::DataStorageInherited: return RunDataStorageWorker (fd, 4);
#else
	case WorkerRole::TorrentStorageInherited:
	case WorkerRole::RouterStorageInherited:
	case WorkerRole::DataStorageInherited: return 1;
#endif
	case WorkerRole::TorrentStorage: return RunTorrentStorageWorker (fd);
	case WorkerRole::RouterStorage: return RunRouterStorageWorker (fd);
	case WorkerRole::DataStorage: return RunDataStorageWorker (fd);
	case WorkerRole::Log: return RunLogWorker (fd);
	case WorkerRole::Supervisor: return RunSupervisor (fd);
	}
	std::fprintf (stderr, "privsep worker: unknown role\n");
	return 1;
}
}

#else
namespace i2p::privsep
{
int RunWorker (std::string_view, std::string_view)
{
	return 1;
}
}
#endif
