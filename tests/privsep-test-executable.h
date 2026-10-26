/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test executable path.
 *
 * Resolves the test binary so a privilege-separation test can spawn itself as
 * a worker after Worker.cpp stopped trusting $PATH and /proc. Linux uses
 * /proc/self/exe; every other platform resolves argv[0] against the current
 * directory. This header is test-only and never installed.
 *
 */

#ifndef I2PD_TESTS_PRIVSEP_TEST_EXECUTABLE_H__
#define I2PD_TESTS_PRIVSEP_TEST_EXECUTABLE_H__

#include <cstdlib>
#include <string>

#if defined(__linux__)
#include <unistd.h>
#else
#include <limits.h>
#endif

#include "privsep/Worker.h"

namespace i2p::privsep
{
inline std::string TestExecutablePath (const char * argv0)
{
	std::string path;
#if defined(__linux__)
	char buffer[4096];
	const ssize_t length = ::readlink ("/proc/self/exe", buffer, sizeof (buffer) - 1);
	if (length > 0)
	{
		buffer[length] = '\0';
		path.assign (buffer, static_cast<size_t> (length));
	}
	if (!path.empty ()) return path;
#endif
	if (!argv0 || !*argv0) return { };
#if defined(__OpenBSD__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__APPLE__)
	char resolved[PATH_MAX];
	if (::realpath (argv0, resolved)) path.assign (resolved);
#else
	char resolved[4096];
	if (::realpath (argv0, resolved)) path.assign (resolved);
#endif
	return path;
}

/** Freeze the test binary before the first spawn helper call. */
inline void PinTestExecutable (const char * argv0)
{
	SetExecutablePath (TestExecutablePath (argv0));
}
}

#endif
