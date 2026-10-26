/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Win32Service.
 *
 * Implements the Windows service lifecycle: registers the control handler,
 * reports pending, running and stopped states, and starts the router on a
 * worker thread that Stop waits for with a bounded budget. A failed start
 * unwinds the daemon before it reports a real exit code, and stop, shutdown and
 * start-pending states carry a non-zero SCM wait hint. No callback throws a
 * C++ exception across the SCM boundary.
 *
 */

#include "Win32Service.h"
#include <assert.h>
#include <windows.h>

#include <chrono>

#include "Daemon.h"
#include "Log.h"
#include "privsep/WindowsSupervisor.h"

namespace
{
	/**
	 * The service stop flag is published with Interlocked operations so the
	 * monitor thread and the control handler never race on a plain BOOL. The
	 * value is 0 (running) or 1 (stopping).
	 */
	BOOL IsStopping (volatile LONG * flag)
	{
		return InterlockedCompareExchange (flag, 0, 0) != 0;
	}

	/**
	 * Drain the router and join its bounded monitor thread.
	 *
	 * Order is load-bearing: `Daemon.stop()` tears down router subsystems on
	 * this thread, then the flag is published so the monitor leaves its sleep,
	 * then the event is awaited with a bounded budget (never INFINITE) and the
	 * thread joined. The monitor observes the flag within one sleep slice, so
	 * the join is bounded even when the event wait expires.
	 */
	void DrainDaemonAndJoinWorker (
		volatile LONG * stopping, HANDLE stoppedEvent, std::thread *& worker, DWORD budgetMs)
	{
		LogPrint (eLogInfo, "Win32Service: draining the router before reporting stopped");
		Daemon.stop ();
		InterlockedExchange (stopping, 1);
		if (stoppedEvent) SetEvent (stoppedEvent);
		if (stoppedEvent && WaitForSingleObject (stoppedEvent, budgetMs) != WAIT_OBJECT_0)
		{
			// The daemon is already stopped; a monitor that missed its slice is
			// reported instead of extending the SCM wait indefinitely.
			LogPrint (eLogCritical, "Win32Service: worker did not signal stop within ", budgetMs, " ms");
		}
		if (worker)
		{
			if (worker->joinable ()) worker->join ();
			delete worker;
			worker = NULL;
		}
	}
}

I2PService * I2PService::s_service = NULL;

BOOL I2PService::isService ()
{
	BOOL bIsService = FALSE;
	HWINSTA hWinStation = GetProcessWindowStation ();
	if (hWinStation != NULL)
	{
		USEROBJECTFLAGS uof = { FALSE, FALSE, 0 };
		if (GetUserObjectInformation (hWinStation, UOI_FLAGS, &uof, sizeof (USEROBJECTFLAGS), NULL)
			&& ((uof.dwFlags & WSF_VISIBLE) == 0))
		{
			bIsService = TRUE;
		}
	}
	return bIsService;
}

BOOL I2PService::Run (I2PService & service)
{
	s_service = &service;
	SERVICE_TABLE_ENTRY serviceTable[] = { { service.m_name, ServiceMain }, { NULL, NULL } };
	return StartServiceCtrlDispatcher (serviceTable);
}

void WINAPI I2PService::ServiceMain (DWORD dwArgc, PSTR * pszArgv)
{
	// Nothing may escape this callback: an exception thrown out of ServiceMain
	// terminates the process before the service can report an exit code.
	try
	{
		if (s_service == NULL) return;
		s_service->_worker = NULL;
		s_service->m_statusHandle = RegisterServiceCtrlHandler (s_service->m_name, ServiceCtrlHandler);
		if (s_service->m_statusHandle == NULL)
		{
			LogPrint (eLogCritical, "Win32Service: cannot register the control handler: ", GetLastError ());
			return;
		}
		s_service->Start (dwArgc, pszArgv);
	}
	catch (...)
	{
		LogPrint (eLogCritical, "Win32Service: service main failed");
	}
}

void WINAPI I2PService::ServiceCtrlHandler (DWORD dwCtrl)
{
	if (s_service == NULL) return;
	switch (dwCtrl)
	{
	case SERVICE_CONTROL_STOP: s_service->Stop (); break;
	case SERVICE_CONTROL_PAUSE: s_service->Pause (); break;
	case SERVICE_CONTROL_CONTINUE: s_service->Continue (); break;
	case SERVICE_CONTROL_SHUTDOWN: s_service->Shutdown (); break;
	case SERVICE_CONTROL_INTERROGATE: break;
	default: break;
	}
}

I2PService::I2PService (PSTR pszServiceName, BOOL fCanStop, BOOL fCanShutdown, BOOL fCanPauseContinue)
{
	m_name = (pszServiceName == NULL) ? (PSTR) "" : pszServiceName;
	m_statusHandle = NULL;
	m_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	m_status.dwCurrentState = SERVICE_START_PENDING;

	DWORD dwControlsAccepted = 0;
	if (fCanStop) dwControlsAccepted |= SERVICE_ACCEPT_STOP;
	if (fCanShutdown) dwControlsAccepted |= SERVICE_ACCEPT_SHUTDOWN;
	if (fCanPauseContinue) dwControlsAccepted |= SERVICE_ACCEPT_PAUSE_CONTINUE;

	m_status.dwControlsAccepted = dwControlsAccepted;
	m_status.dwWin32ExitCode = NO_ERROR;
	m_status.dwServiceSpecificExitCode = 0;
	m_status.dwCheckPoint = 0;
	m_status.dwWaitHint = 0;
	m_fStopping = FALSE;
	_worker = NULL;
	// Create a manual-reset event that is not signaled at first to indicate
	// the stopped signal of the service. A failure here is reported instead of
	// thrown: the drain falls back to its bounded wait and join.
	m_hStoppedEvent = CreateEvent (NULL, TRUE, FALSE, NULL);
	if (m_hStoppedEvent == NULL)
	{
		LogPrint (eLogCritical, "Win32Service: cannot create the stop event: ", GetLastError ());
	}
}

I2PService::~I2PService (void)
{
	if (m_hStoppedEvent)
	{
		CloseHandle (m_hStoppedEvent);
		m_hStoppedEvent = NULL;
	}
}

void I2PService::Start (DWORD dwArgc, PSTR * pszArgv)
{
	assert (_worker == NULL);
	SetServiceStatus (SERVICE_START_PENDING, NO_ERROR,
		i2p::privsep::NonZeroWaitHint (i2p::privsep::WINDOWS_SERVICE_START_WAIT_HINT_MS));

	DWORD failure = NO_ERROR;
	try
	{
		OnStart (dwArgc, pszArgv);
	}
	catch (DWORD dwError)
	{
		failure = dwError ? dwError : ERROR_GEN_FAILURE;
	}
	catch (...)
	{
		failure = ERROR_GEN_FAILURE;
	}

	if (failure != NO_ERROR)
	{
		// A failed start must not leave the router running behind a STOPPED
		// service: unwind Daemon.start() (and any monitor thread) first, then
		// report a real service exit code.
		LogPrint (eLogCritical, "Win32Service: start failed: ", failure);
		try
		{
			DrainDaemonAndJoinWorker (
				reinterpret_cast<volatile LONG *> (&m_fStopping), m_hStoppedEvent, _worker,
				i2p::privsep::WINDOWS_SERVICE_DRAIN_BUDGET_MS);
		}
		catch (...)
		{
			LogPrint (eLogCritical, "Win32Service: start unwind failed");
		}
		SetServiceStatus (SERVICE_STOPPED, failure);
		return;
	}
	SetServiceStatus (SERVICE_RUNNING);
}

void I2PService::OnStart (DWORD /*dwArgc*/, PSTR * /*pszArgv*/)
{
	LogPrint (eLogInfo, "Win32Service: in OnStart (", EVENTLOG_INFORMATION_TYPE, ")");
	if (!Daemon.start ())
	{
		// Report a real startup failure instead of announcing RUNNING for a
		// router that never started. Start() unwinds the daemon.
		m_status.dwServiceSpecificExitCode = ERROR_GEN_FAILURE;
		throw static_cast<DWORD> (ERROR_SERVICE_SPECIFIC_ERROR);
	}
	_worker = new std::thread (&I2PService::WorkerThread, this);
}

void I2PService::WorkerThread ()
{
	// The flag is read with an interlocked compare so the control handler's
	// write is visible immediately instead of relying on a cached BOOL.
	while (!IsStopping (reinterpret_cast<volatile LONG *> (&m_fStopping)))
	{
		::Sleep (1000); // Monitor slice; the drain waits no longer than one tick.
	}
	// Signal the stopped event.
	if (m_hStoppedEvent) SetEvent (m_hStoppedEvent);
}

void I2PService::Stop ()
{
	if (m_status.dwCurrentState == SERVICE_STOPPED || m_status.dwCurrentState == SERVICE_STOP_PENDING) return;
	try
	{
		// A non-zero wait hint is mandatory: with a zero hint the SCM applies
		// its own short timeout while the router is still draining.
		SetServiceStatus (SERVICE_STOP_PENDING, NO_ERROR,
			i2p::privsep::NonZeroWaitHint (i2p::privsep::WINDOWS_SERVICE_STOP_WAIT_HINT_MS));
		OnStop ();
		SetServiceStatus (SERVICE_STOPPED);
	}
	catch (DWORD dwError)
	{
		LogPrint (eLogCritical, "Win32Service: Stop error: ", dwError);
		SetServiceStatus (SERVICE_STOPPED, dwError ? dwError : ERROR_GEN_FAILURE);
	}
	catch (...)
	{
		LogPrint (eLogCritical, "Win32Service: failed to stop");
		SetServiceStatus (SERVICE_STOPPED, ERROR_GEN_FAILURE);
	}
}

void I2PService::OnStop ()
{
	// Log a service stop message to the Application log.
	LogPrint (eLogInfo, "Win32Service: in OnStop (", EVENTLOG_INFORMATION_TYPE, ")");
	DrainDaemonAndJoinWorker (reinterpret_cast<volatile LONG *> (&m_fStopping), m_hStoppedEvent, _worker,
		i2p::privsep::WINDOWS_SERVICE_DRAIN_BUDGET_MS);
}

void I2PService::Pause ()
{
	try
	{
		SetServiceStatus (SERVICE_PAUSE_PENDING, NO_ERROR,
			i2p::privsep::NonZeroWaitHint (i2p::privsep::WINDOWS_SERVICE_STOP_WAIT_HINT_MS));
		OnPause ();
		SetServiceStatus (SERVICE_PAUSED);
	}
	catch (DWORD dwError)
	{
		LogPrint (eLogCritical, "Win32Service: Pause error: ", dwError);
		SetServiceStatus (SERVICE_RUNNING);
	}
	catch (...)
	{
		LogPrint (eLogCritical, "Win32Service: failed to pause");
		SetServiceStatus (SERVICE_RUNNING);
	}
}

void I2PService::OnPause () { }

void I2PService::Continue ()
{
	try
	{
		SetServiceStatus (SERVICE_CONTINUE_PENDING, NO_ERROR,
			i2p::privsep::NonZeroWaitHint (i2p::privsep::WINDOWS_SERVICE_STOP_WAIT_HINT_MS));
		OnContinue ();
		SetServiceStatus (SERVICE_RUNNING);
	}
	catch (DWORD dwError)
	{
		LogPrint (eLogCritical, "Win32Service: Continue error: ", dwError);
		SetServiceStatus (SERVICE_PAUSED);
	}
	catch (...)
	{
		LogPrint (eLogCritical, "Win32Service: failed to resume");
		SetServiceStatus (SERVICE_PAUSED);
	}
}

void I2PService::OnContinue () { }

void I2PService::Shutdown ()
{
	if (m_status.dwCurrentState == SERVICE_STOPPED || m_status.dwCurrentState == SERVICE_STOP_PENDING) return;
	try
	{
		// A machine shutdown is a stop request: report pending with a real hint,
		// drain the daemon, and only then report stopped.
		SetServiceStatus (SERVICE_STOP_PENDING, NO_ERROR,
			i2p::privsep::NonZeroWaitHint (i2p::privsep::WINDOWS_SERVICE_STOP_WAIT_HINT_MS));
		OnShutdown ();
		SetServiceStatus (SERVICE_STOPPED);
	}
	catch (DWORD dwError)
	{
		LogPrint (eLogCritical, "Win32Service: Shutdown error: ", dwError);
		SetServiceStatus (SERVICE_STOPPED, dwError ? dwError : ERROR_GEN_FAILURE);
	}
	catch (...)
	{
		LogPrint (eLogCritical, "Win32Service: failed to shut down");
		SetServiceStatus (SERVICE_STOPPED, ERROR_GEN_FAILURE);
	}
}

void I2PService::OnShutdown ()
{
	// The default shutdown hook stops the daemon. It delegates to OnStop so a
	// subclass that owns the drain keeps exactly one implementation; without
	// this the service reported STOPPED while the router kept running.
	LogPrint (eLogInfo, "Win32Service: in OnShutdown (", EVENTLOG_INFORMATION_TYPE, ")");
	OnStop ();
}

void I2PService::SetServiceStatus (DWORD dwCurrentState, DWORD dwWin32ExitCode, DWORD dwWaitHint)
{
	static DWORD dwCheckPoint = 1;
	m_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	m_status.dwCurrentState = dwCurrentState;
	m_status.dwWin32ExitCode = dwWin32ExitCode;
	// Running and stopped are terminal observations and carry no hint; every
	// pending state needs a non-zero hint.
	m_status.dwWaitHint = (dwCurrentState == SERVICE_RUNNING || dwCurrentState == SERVICE_STOPPED)
		? 0
		: i2p::privsep::NonZeroWaitHint (dwWaitHint);
	m_status.dwCheckPoint
		= ((dwCurrentState == SERVICE_RUNNING) || (dwCurrentState == SERVICE_STOPPED)) ? 0 : dwCheckPoint++;

	// Before the control handler exists (unit tests, an early registration
	// failure) the status stays observable in this object and is not sent.
	if (m_statusHandle == NULL) return;
	::SetServiceStatus (m_statusHandle, &m_status);
}

//*****************************************************************************

void FreeHandles (SC_HANDLE schSCManager, SC_HANDLE schService)
{
	if (schSCManager)
	{
		CloseServiceHandle (schSCManager);
		schSCManager = NULL;
	}
	if (schService)
	{
		CloseServiceHandle (schService);
		schService = NULL;
	}
}
