/*
* Copyright (c) 2026, David Uhden Collado
* SPDX-License-Identifier: BSD-3-Clause
*
* This file is part of openi2pd and licensed under BSD-3-Clause
*/

#include "RunWorker.h"
#include "LogWorker.h"
#include "Supervisor.h"

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
	if (parsed.ec != std::errc () || parsed.ptr != end || fd < 3 || fd > 1024)
	{
		std::fprintf (stderr, "privsep worker: invalid channel fd\n");
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
