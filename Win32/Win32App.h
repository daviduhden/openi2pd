/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Win32App.
 *
 * Declares the desktop integration used by the Windows GUI build: start, stop
 * and message-loop entry points for the hidden main window, graceful-shutdown
 * controls, and the shutdown countdown queried by the daemon.
 *
 */

#ifndef WIN32APP_H__
#define WIN32APP_H__

#define I2PD_WIN32_CLASSNAME "i2pd main window"

namespace i2p
{
namespace win32
{
	bool StartWin32App (bool isWinService);
	void StopWin32App ();
	int RunWin32App ();
	bool GracefulShutdown ();
	bool StopGracefulShutdown ();
	int GetGracefulShutdownRemainingTime ();
}
}
#endif // WIN32APP_H__
