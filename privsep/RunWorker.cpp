/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: RunWorker.
 *
 * Validates the worker role and channel descriptor, requiring an inherited
 * SOCK_SEQPACKET socket in descriptor 3 and refusing euid 0, then dispatches
 * to the log worker or supervisor. Windows builds provide a failing stub.
 *
 */

#include "RunWorker.h"
#include "LogWorker.h"
#include "Supervisor.h"
#include "TorrentStorage.h"
#include "RouterStorage.h"

#ifndef _WIN32

#include <cerrno>
#include <charconv>
#include <cstdio>
#include <fcntl.h>
#include <string>
#include <system_error>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

namespace i2p::privsep
{
int RunWorker (std::string_view role, std::string_view fdArgument)
{
	if (fdArgument.empty ())
	{
		std::fprintf (stderr, "privsep worker: invalid channel fd\n");
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
	if (role == "torrent-storage-supervisor") return RunTorrentStorageSupervisor (fd);
#if defined(__OpenBSD__)
	// Directory descriptors only arrive as fd4 on OpenBSD, where pledge
	// forbids directory SCM_RIGHTS. On Linux these roles would interpret any
	// inherited fd4 as the write root, so they must not be dispatchable.
	if (role == "torrent-storage-inherited") return RunTorrentStorageWorker (fd, 4);
	if (role == "router-storage-inherited") return RunRouterStorageWorker (fd, 4);
#else
	if (role == "torrent-storage-inherited" || role == "router-storage-inherited") return 1;
#endif
	if (role == "torrent-storage") return RunTorrentStorageWorker (fd);
	if (role == "router-storage") return RunRouterStorageWorker (fd);
	if (role == "log") return RunLogWorker (fd);
	if (role == "supervisor") return RunSupervisor (fd);
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
