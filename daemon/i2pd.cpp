/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: i2pd.
 *
 * Process entry point: the frozen worker executable path is captured
 * from argv[0] before any spawn, then a validated --privsep-worker
 * invocation is dispatched to the isolated worker runner (a Windows
 * worker is dispatched to the pipe transport with its spawn nonce);
 * informational command-line requests are answered before any daemon
 * state exists, and normal execution runs the Daemon init/start/run/stop
 * sequence. Both Windows entry wrappers convert the authoritative
 * command line before the shared parser classifies informational flags
 * or rejects credential-bearing options.
 */

#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <stdlib.h>
#include <string>
#include <string_view>
#include <vector>
#ifndef _WIN32
#include <climits>
#include <cstring>
#include "privsep/RunWorker.h"
#include "privsep/Worker.h"
#else
// The Windows worker entry point lives with the pipe transport: its
// identity is the pipe connection plus the WorkerHello handshake, not
// an inherited socket.
#include "privsep/WindowsIpc.h"
#endif
#include "Config.h"
#include "Daemon.h"

#ifdef _WIN32
#include <windows.h>
// CommandLineToArgvW: the authoritative GUI-subsystem command line.
#include <shellapi.h>
#endif

namespace
{
#ifndef _WIN32
/*
 * Freeze the executable path workers are spawned from.
 *
 * argv[0] is captured once, before any daemon path can spawn. A bare
 * program name is not resolved through $PATH, and there is no /proc
 * fallback (OpenBSD has no getexecpath(3) and /proc/self/exe does not
 * exist there): a PATH search is a substitution surface and resolving a
 * bare name against the current directory could execute an unrelated
 * file. A failed realpath() leaves the path unset, and every spawn then
 * fails closed.
 */
void FreezeWorkerExecutable (const char * argv0)
{
	if (!argv0 || !*argv0 || !std::strchr (argv0, '/')) return;
	char resolved[PATH_MAX] = { };
	if (!realpath (argv0, resolved)) return;
	i2p::privsep::SetExecutablePath (resolved);
}
#else
/*
 * Never let the operating system block an unattended start on a modal
 * dialog.
 *
 * SEM_FAILCRITICALERRORS turns a hard error into a failure this process
 * reports through its exit status, SEM_NOGPFAULTERRORBOX suppresses the
 * fault dialog and SEM_NOOPENFILEERRORBOX suppresses the "insert disk"
 * style file-error dialog nobody can dismiss on a headless runner or in
 * the services session. This is a presentation policy, not a security
 * control: the failing operation still fails closed.
 */
void SuppressInteractiveErrorDialogs ()
{
	SetErrorMode (SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
}

/*
 * Report a startup failure on the inherited standard error.
 *
 * A GUI-subsystem image cannot assume a console, so the message goes to
 * whatever standard-error handle the caller provided (a service manager
 * or a shell redirection) and is dropped when there is none. The write
 * is bounded, uses only handle operations and never allocates a window
 * or waits on console state.
 */
void WriteStandardError (std::string_view text)
{
	const HANDLE handle = GetStdHandle (STD_ERROR_HANDLE);
	if (handle == nullptr || handle == INVALID_HANDLE_VALUE) return;
	DWORD written = 0;
	WriteFile (handle, text.data (), static_cast<DWORD> (text.size ()), &written, nullptr);
}

/*
 * Authoritative UTF-8 argument vector for both Windows entry points.
 *
 * The GUI image can reach main or WinMain depending on its CRT. Both
 * paths use GetCommandLineW() so argument parsing has one authority.
 * Conversion failure aborts instead of substituting CRT globals and
 * accidentally starting a router with a different command line.
 *
 * The conversion is UTF-8 because every consumer of argv (the option
 * parser, path handling) treats arguments as UTF-8; an unrepresentable
 * wide character becomes U+FFFD instead of failing the conversion.
 */
class WindowsCommandLine
{
public:
	WindowsCommandLine ()
	{
		int count = 0;
		using WideArguments = std::unique_ptr<LPWSTR, decltype (&LocalFree)>;
		WideArguments wide (CommandLineToArgvW (GetCommandLineW (), &count), LocalFree);
		if (!wide || count <= 0) return;
		m_Storage.reserve (static_cast<size_t> (count));
		for (int i = 0; i < count; ++i)
		{
			// -1 includes the terminating NUL, which the vector must not
			// keep: capacity is length bytes and the stored string is one
			// byte shorter.
			const auto value = wide.get ()[i];
			const int length = value ? WideCharToMultiByte (CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr) : 0;
			if (length <= 0)
			{
				m_Storage.clear ();
				break;
			}
			std::string argument (static_cast<size_t> (length), '\0');
			const int written = WideCharToMultiByte (
				CP_UTF8, 0, value, -1, argument.data (), length, nullptr, nullptr);
			if (written != length)
			{
				m_Storage.clear ();
				return;
			}
			argument.pop_back ();
			m_Storage.push_back (std::move (argument));
		}
		// Build the pointer array only after the last string exists: a
		// reallocation would otherwise invalidate an earlier pointer.
		m_Arguments.reserve (m_Storage.size () + 1);
		for (auto & argument : m_Storage) m_Arguments.push_back (argument.data ());
		m_Arguments.push_back (nullptr);
	}

	/// True after every argument converts; failure must abort.
	/// CRT globals may discard a rejected operand or silently change the
	/// original request into a router start.
	bool Valid () const { return m_Arguments.size () > 1; }
	int argc () const { return static_cast<int> (m_Storage.size ()); }
	/// Borrow strings and the trailing null for this object's lifetime.
	char ** argv () { return m_Arguments.data (); }

private:
	std::vector<std::string> m_Storage;
	std::vector<char *> m_Arguments;
};
#endif

int RunI2PD (int argc, char * argv[])
{
#ifdef _WIN32
	// The entry point sets this before it reads the raw command line; the
	// re-assertion keeps any future caller of this runner on the same
	// presentation policy, so a failure is reported through the exit
	// status instead of a dialog nobody dismisses on a headless runner.
	SuppressInteractiveErrorDialogs ();
#endif
#ifndef _WIN32
	// Runs in the daemon and in every exec'ed worker: a worker re-freezes
	// its own path from argv[0], which the spawner sets to the frozen
	// absolute path, so a worker that spawns children (the supervisor)
	// needs neither a PATH search nor /proc.
	if (argc > 0) FreezeWorkerExecutable (argv[0]);
	if (argc == 4 && std::string_view (argv[1]) == "--privsep-worker")
		return i2p::privsep::RunWorker (argv[2], argv[3]);
#else
	// A Windows worker is spawned with a role, a pipe name, the parent
	// identity and the per-spawn nonce. The pipe connection (bound to that
	// PID/session) and the WorkerHello frame are the only authority, so a
	// hand-started process cannot act as a worker and this path never
	// initialises Daemon.
	if (argc == 7 && std::string_view (argv[1]) == "--privsep-worker")
		return i2p::privsep::RunWindowsWorker (argv[2], argv[3], argv[4], argv[5], argv[6]);
#endif
	// Answer --version and --help here, before the daemon installs its
	// fatal-error reporter, changes the console code page, resolves the
	// worker identity or acquires any service. The banner must not depend
	// on console, GUI or teardown state, and these flags must never start
	// a router.
	const auto informational = i2p::config::ClassifyInformationalRequest (argc, argv);
	if (informational != i2p::config::CmdlineOutcome::Proceed)
	{
		// Nothing has been acquired: no logger, worker, socket, console
		// code page or GUI window exists. There is therefore nothing to
		// tear down, and skipping exit-time destructors keeps the answer
		// independent of router teardown that could wait on state this
		// process never started.
		std::_Exit (i2p::config::PublishInformationalResponse (informational));
	}

#ifdef _WIN32
	// Reject invalid CLI operands before Win32 installs a modal reporter.
	// Daemon initialization resets and parses the same option table again
	// before loading configuration; worker dispatch has already returned.
	i2p::config::ParseCmdline (argc, argv);
#endif
	if (!Daemon.init (argc, argv)) return EXIT_FAILURE;
	if (!Daemon.start ()) return EXIT_FAILURE;
	Daemon.run ();
	Daemon.stop ();
	return EXIT_SUCCESS;
}

#ifdef _WIN32
/*
 * Both CRT entry variants use one authoritative Windows command line.
 * MinGW can select main even for the GUI image, so WinMain alone cannot
 * own informational classification or worker argument conversion.
 */
int RunWindowsEntryPoint ()
{
	try
	{
		SuppressInteractiveErrorDialogs ();
		// Informational flags use the complete parser after conversion.
		// A second raw grammar could bypass credential rejection or
		// disagree about later options. A CRT fallback is forbidden.
		WindowsCommandLine commandLine;
		if (!commandLine.Valid ())
		{
			WriteStandardError ("i2pd: fatal error: cannot read the command line\n");
			return EXIT_FAILURE;
		}
		return RunI2PD (commandLine.argc (), commandLine.argv ());
	}
	catch (const std::exception &)
	{
		WriteStandardError ("i2pd: fatal error: unhandled exception\n");
	}
	catch (...)
	{
		WriteStandardError ("i2pd: fatal error: unknown exception\n");
	}
	return EXIT_FAILURE;
}
#endif
} // namespace

#ifdef _WIN32
int main ()
{
	return RunWindowsEntryPoint ();
}
#else
int main (int argc, char * argv[])
{
	// An exception that escapes init/start/run/stop must become a normal
	// failure exit instead of std::terminate()/SIGABRT. The service
	// manager then restarts the router through its own failure policy, and
	// a crash is never reported as a clean stop. Do not print exception
	// text: parser diagnostics can quote configuration operands.
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
#endif

#ifdef _WIN32
int CALLBACK WinMain (
	_In_ HINSTANCE /*hInstance*/, _In_ HINSTANCE /*hPrevInstance*/, _In_ LPSTR /*lpCmdLine*/, _In_ int /*nCmdShow*/)
{
	return RunWindowsEntryPoint ();
}
#endif
