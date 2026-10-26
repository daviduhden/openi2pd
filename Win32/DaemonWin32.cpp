/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: DaemonWin32.
 *
 * Implements the Win32 daemon hooks around the shared Daemon singleton: locale
 * and console code-page setup, service dispatch when running as a service, and
 * GUI start, stop and message-loop handling when built with WIN32_APP. It also
 * applies the insomnia execution-state request and hides standard handles when
 * file logging is active.
 *
 */

#include <thread>
#include <clocale>
#include "Config.h"
#include "Daemon.h"
#include "util.h"
#include "Log.h"

#ifdef _WIN32
#include "Win32Service.h"
#ifdef WIN32_APP
#include <windows.h>
#include "Win32App.h"
#endif

namespace i2p
{
namespace util
{
	bool DaemonWin32::init (int argc, char * argv[])
	{
		// The console code page is pinned before the shared init reads any
		// configuration text; LC_TIME stays "C" so log timestamps are stable.
		setlocale (LC_CTYPE, "");
		SetConsoleCP (1251);
		SetConsoleOutputCP (1251);
		setlocale (LC_TIME, "C");

		i2p::log::SetThrowFunction ([] (const std::string & s)
			{ MessageBox (0, TEXT (s.c_str ()), TEXT ("openi2pd"), MB_ICONERROR | MB_TASKMODAL | MB_OK); });

		if (!Daemon_Singleton::init (argc, argv)) return false;

		// Service mode hands the process to the SCM dispatcher, which blocks
		// until the service stops; reporting success here would let main() call
		// start() on a router the service already owns.
		if (isDaemon)
		{
			LogPrint (eLogDebug, "Daemon: running as service");
			I2PService service ((PSTR)SERVICE_NAME);
			if (!I2PService::Run (service))
			{
				LogPrint (eLogCritical, "Daemon: Service failed to run w/err 0x%08lx\n", GetLastError ());
				return false;
			}
			return false;
		}

		return true;
	}

	bool DaemonWin32::start ()
	{
		setlocale (LC_CTYPE, "");
		SetConsoleCP (1251);
		SetConsoleOutputCP (1251);
		setlocale (LC_TIME, "C");
#ifdef WIN32_APP
		// The tray/window is created before the router starts; a failure here
		// aborts the start instead of running a GUI build without its shell.
		if (!i2p::win32::StartWin32App (isDaemon)) return false;
#endif
		bool ret = Daemon_Singleton::start ();
		if (ret && i2p::log::Logger ().GetLogType () == eLogFile)
		{
			// TODO: find out where this garbage to console comes from
			SetStdHandle (STD_OUTPUT_HANDLE, INVALID_HANDLE_VALUE);
			SetStdHandle (STD_ERROR_HANDLE, INVALID_HANDLE_VALUE);
		}
		bool insomnia;
		i2p::config::GetOption ("insomnia", insomnia);
		// Execution-state request, not a security control: it may be ignored by
		// the OS or overridden by a later power policy.
		if (insomnia) SetThreadExecutionState (ES_CONTINUOUS | ES_SYSTEM_REQUIRED);
		return ret;
	}

	bool DaemonWin32::stop ()
	{
#ifdef WIN32_APP
		// The GUI window is stopped before the ordered daemon teardown so no
		// timer or menu action can touch a router that is closing.
		i2p::win32::StopWin32App ();
#endif
		return Daemon_Singleton::stop ();
	}

	void DaemonWin32::run ()
	{
#ifdef WIN32_APP
		i2p::win32::RunWin32App ();
#else
		// Console/service builds have no message loop to own; wait in bounded
		// slices so a requested shutdown is observed promptly.
		while (running)
		{
			std::this_thread::sleep_for (std::chrono::seconds (1));
		}
#endif
	}

	int DaemonWin32::GetGracefulShutdownInterval () const
	{
#ifdef WIN32_APP
		return i2p::win32::GetGracefulShutdownRemainingTime ();
#else
		return Daemon_Singleton::GetGracefulShutdownInterval ();
#endif
	}
}
}
#endif //_WIN32
