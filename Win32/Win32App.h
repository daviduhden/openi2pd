/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Win32App.
 *
 * Declares the desktop integration used by the Windows GUI build:
 * start, stop and message-loop entry points for the hidden main window,
 * graceful-shutdown controls, and the shutdown countdown queried by the
 * daemon.
 */

#ifndef WIN32APP_H__
#define WIN32APP_H__

#define I2PD_WIN32_CLASSNAME "i2pd main window"

namespace i2p
{
namespace win32
{
	/**
	 * Create the hidden main window and, outside service mode, start COM
	 * network-state monitoring.
	 *
	 * The window exists only to own the tray icon, the popup menu and the
	 * status window; it is never the router owner. The class name is used
	 * as the single-instance marker, so a second GUI start reports
	 * "already running" and fails instead of creating a competing router.
	 *
	 * @param isWinService true when the SCM launched this process; COM
	 * event delivery needs a message loop, which service mode does not
	 * have, so no subscription is made.
	 * @return false when the window class/window cannot be created or
	 * another instance owns the class name; the caller must not start the
	 * router.
	 * @pre Called on the thread that will run RunWin32App().
	 */
	bool StartWin32App (bool isWinService);
	/**
	 * Ask the message loop to exit and release the window class.
	 *
	 * Posts (rather than sends) an exit command, so it is safe from a
	 * non-GUI thread while the loop is running; the COM subscription is
	 * released directly only when no window was found. It does not stop
	 * the daemon: DaemonWin32::stop performs the ordered teardown.
	 */
	void StopWin32App ();
	/**
	 * Run the Win32 message loop until WM_QUIT and return its exit code.
	 * @pre StartWin32App() succeeded on this thread.
	 */
	int RunWin32App ();
	/**
	 * Post the graceful-shutdown command; false when no window is
	 * present. */
	bool GracefulShutdown ();
	/**
	 * Post the cancel-graceful-shutdown command; false when no window
	 * is present. */
	bool StopGracefulShutdown ();
	/**
	 * Remaining graceful-drain seconds, or 0 when no countdown is armed.
	 * The value is derived from GetTickCount() and can be stale by one
	 * timer tick.
	 */
	int GetGracefulShutdownRemainingTime ();
}
}
#endif // WIN32APP_H__
