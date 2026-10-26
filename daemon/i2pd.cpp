/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: i2pd.
 *
 * Process entry point: the frozen worker executable path is captured from
 * argv[0] before any spawn, then a validated --privsep-worker invocation is
 * dispatched to the isolated worker runner (a Windows worker is dispatched to
 * the pipe transport with its spawn nonce); normal execution runs the Daemon
 * init/start/run/stop sequence. The Windows WinMain wrapper forwards to main.
 *
 */

#include <exception>
#include <iostream>
#include <stdlib.h>
#include <string_view>
#ifndef _WIN32
#include <climits>
#include <cstring>
#include "privsep/RunWorker.h"
#include "privsep/Worker.h"
#else
// The Windows worker entry point lives with the pipe transport: its identity is
// the pipe connection plus the WorkerHello handshake, not an inherited socket.
#include "privsep/WindowsIpc.h"
#endif
#include "Daemon.h"

namespace
{
#ifndef _WIN32
/**
 * Freeze the executable path workers are spawned from.
 *
 * argv[0] is captured once, before any daemon path can spawn. A bare program
 * name is not resolved through $PATH, and there is no /proc fallback (OpenBSD
 * has no getexecpath(3) and /proc/self/exe does not exist there): a PATH search
 * is a substitution surface and resolving a bare name against the current
 * directory could execute an unrelated file. A failed realpath() leaves the
 * path unset, and every spawn then fails closed.
 */
void FreezeWorkerExecutable (const char * argv0)
{
	if (!argv0 || !*argv0 || !std::strchr (argv0, '/')) return;
	char resolved[PATH_MAX] = { };
	if (!realpath (argv0, resolved)) return;
	i2p::privsep::SetExecutablePath (resolved);
}
#endif

int RunI2PD (int argc, char * argv[])
{
#ifndef _WIN32
	// Runs in the daemon and in every exec'ed worker: a worker re-freezes its
	// own path from argv[0], which the spawner sets to the frozen absolute
	// path, so a worker that spawns children (the supervisor) needs neither a
	// PATH search nor /proc.
	if (argc > 0) FreezeWorkerExecutable (argv[0]);
	if (argc == 4 && std::string_view (argv[1]) == "--privsep-worker")
		return i2p::privsep::RunWorker (argv[2], argv[3]);
#else
	// A Windows worker is spawned with a role, a pipe name, the parent identity
	// and the per-spawn nonce. The pipe connection (bound to that PID/session)
	// and the WorkerHello frame are the only authority, so a hand-started
	// process cannot act as a worker and this path never initialises Daemon.
	if (argc == 7 && std::string_view (argv[1]) == "--privsep-worker")
		return i2p::privsep::RunWindowsWorker (argv[2], argv[3], argv[4], argv[5], argv[6]);
#endif
	if (!Daemon.init (argc, argv)) return EXIT_FAILURE;
	if (!Daemon.start ()) return EXIT_FAILURE;
	Daemon.run ();
	Daemon.stop ();
	return EXIT_SUCCESS;
}
} // namespace

int main (int argc, char * argv[])
{
	// An exception that escapes init/start/run/stop must become a normal
	// failure exit instead of std::terminate()/SIGABRT. The service manager
	// then restarts the router through its own failure policy, and a crash is
	// never reported as a clean stop. Do not print exception text: parser
	// diagnostics can quote configuration operands.
	try
	{
		return RunI2PD (argc, argv);
	}
	catch (const std::exception &)
	{
		std::cerr << "i2pd: fatal error: unhandled exception" << std::endl;
	}
	catch (...)
	{
		std::cerr << "i2pd: fatal error: unknown exception" << std::endl;
	}
	return EXIT_FAILURE;
}

#ifdef _WIN32
#include <windows.h>

int CALLBACK WinMain (
	_In_ HINSTANCE /*hInstance*/, _In_ HINSTANCE /*hPrevInstance*/, _In_ LPSTR /*lpCmdLine*/, _In_ int /*nCmdShow*/)
{
	return RunI2PD (__argc, __argv);
}
#endif
