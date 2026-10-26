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

/**
 * Windows service wrapper for the router process.
 *
 * One instance is created on the stack of DaemonWin32::init and handed to the
 * SCM dispatcher, which then calls back into this object. The service runs the
 * router on one worker thread while the SCM control handler runs on the
 * dispatcher thread, so state transitions are serialized through SetServiceStatus
 * and the stop flag is published with Interlocked operations.
 *
 * Scope: this class starts and drains the router daemon only. It does not create
 * a Windows worker supervisor or any AppContainer worker role, so a Windows
 * service does not currently run a privilege-separated worker even when the
 * isolation and supervisor types exist (see privsep/WindowsSupervisor.h).
 *
 * No exception may cross the SCM boundary: every callback catches and converts
 * a failure into a service exit code, and a failed start unwinds the daemon
 * before reporting STOPPED.
 */
class I2PService
{
public:
	I2PService (PSTR pszServiceName, BOOL fCanStop = TRUE, BOOL fCanShutdown = TRUE, BOOL fCanPauseContinue = FALSE);

	virtual ~I2PService (void);

	/** Heuristic service-mode probe (window station visibility), not an identity check. */
	static BOOL isService ();
	/**
	 * Register the service table and hand control to the SCM. Blocks for the
	 * lifetime of the service; @a service must outlive this call.
	 * @return the SCM dispatcher result, non-zero on success.
	 */
	static BOOL Run (I2PService & service);
	/**
	 * Run the stop path synchronously on the calling thread: report STOP_PENDING
	 * with a wait hint, drain the daemon and its monitor thread, then report
	 * STOPPED. A no-op when the service is already stopped or stopping.
	 */
	void Stop ();

protected:
	/** Start the router. Throw a DWORD to report a specific service exit code. */
	virtual void OnStart (DWORD dwArgc, PSTR * pszArgv);
	/** Drain the router and join its monitor thread within a bounded budget. */
	virtual void OnStop ();
	virtual void OnPause ();
	virtual void OnContinue ();
	/** Machine shutdown: delegates to the stop path so exactly one drain exists. */
	virtual void OnShutdown ();
	/**
	 * Publish one SCM state.
	 *
	 * @param dwCurrentState SERVICE_* state; RUNNING and STOPPED are terminal and
	 *        carry no wait hint or checkpoint.
	 * @param dwWin32ExitCode NO_ERROR or a Win32 error code for a failure.
	 * @param dwWaitHint suggested pending budget in milliseconds; clamped to at
	 *        least 1 because a zero hint makes the SCM apply its own short timeout.
	 * @pre Called only from the dispatcher or control thread; the status structure
	 *      is not otherwise synchronized.
	 */
	void SetServiceStatus (DWORD dwCurrentState, DWORD dwWin32ExitCode = NO_ERROR, DWORD dwWaitHint = 0);

private:
	static void WINAPI ServiceMain (DWORD dwArgc, LPSTR * lpszArgv);
	static void WINAPI ServiceCtrlHandler (DWORD dwCtrl);
	/** Sleep in one-second slices until the stop flag is published, then signal it. */
	void WorkerThread ();
	void Start (DWORD dwArgc, PSTR * pszArgv);
	void Pause ();
	void Continue ();
	void Shutdown ();
	static I2PService * s_service;
	PSTR m_name;
	SERVICE_STATUS m_status;
	SERVICE_STATUS_HANDLE m_statusHandle;

	/** Stop flag read through InterlockedCompareExchange; 0 running, 1 stopping. */
	BOOL m_fStopping;
	/** Manual-reset event signalled by WorkerThread once it observed the stop flag. */
	HANDLE m_hStoppedEvent;

	std::thread * _worker;
};

#endif // WIN_32_SERVICE_H__
