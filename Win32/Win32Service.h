/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Win32Service.
 *
 * Declares the Windows service wrapper: service-table dispatch, control
 * handler callbacks, service-status reporting, and overridable start, stop,
 * pause, continue and shutdown hooks around the router daemon.
 *
 */

#ifndef WIN_32_SERVICE_H__
#define WIN_32_SERVICE_H__

#include <thread>
#include <windows.h>

#define SERVICE_NAME "i2pdService"

class I2PService
{
public:
	I2PService (PSTR pszServiceName, BOOL fCanStop = TRUE, BOOL fCanShutdown = TRUE, BOOL fCanPauseContinue = FALSE);

	virtual ~I2PService (void);

	static BOOL isService ();
	static BOOL Run (I2PService & service);
	void Stop ();

protected:
	virtual void OnStart (DWORD dwArgc, PSTR * pszArgv);
	virtual void OnStop ();
	virtual void OnPause ();
	virtual void OnContinue ();
	virtual void OnShutdown ();
	void SetServiceStatus (DWORD dwCurrentState, DWORD dwWin32ExitCode = NO_ERROR, DWORD dwWaitHint = 0);

private:
	static void WINAPI ServiceMain (DWORD dwArgc, LPSTR * lpszArgv);
	static void WINAPI ServiceCtrlHandler (DWORD dwCtrl);
	void WorkerThread ();
	void Start (DWORD dwArgc, PSTR * pszArgv);
	void Pause ();
	void Continue ();
	void Shutdown ();
	static I2PService * s_service;
	PSTR m_name;
	SERVICE_STATUS m_status;
	SERVICE_STATUS_HANDLE m_statusHandle;

	BOOL m_fStopping;
	HANDLE m_hStoppedEvent;

	std::thread * _worker;
};

#endif // WIN_32_SERVICE_H__
