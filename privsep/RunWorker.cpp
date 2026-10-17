/*
* Copyright (c) 2026, The PurpleI2P Project
*
* This file is part of Open I2Pd and licensed under BSD3
*/

#include "RunWorker.h"
#include "LogWorker.h"

#ifndef _WIN32

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

namespace i2p::privsep
{
	int RunWorker (std::string_view role)
	{
		const char *fdText = getenv (LOG_WORKER_ENV_FD);
		if (!fdText || !*fdText)
		{
			std::fprintf (stderr, "privsep worker: missing %s\n", LOG_WORKER_ENV_FD);
			return 1;
		}
		char *end = nullptr;
		errno = 0;
		long fd = std::strtol (fdText, &end, 10);
		if (errno || !end || *end || fd < 3 || fd > 1024)
		{
			std::fprintf (stderr, "privsep worker: invalid channel fd\n");
			return 1;
		}
		if (role == "log") return RunLogWorker (static_cast<int> (fd));
		std::fprintf (stderr, "privsep worker: unknown role\n");
		return 1;
	}
}

#else
namespace i2p::privsep
{
	int RunWorker (std::string_view) { return 1; }
}
#endif
