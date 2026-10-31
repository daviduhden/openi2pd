/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: WindowsSupervisor.
 *
 * Implements the Windows worker supervisor: per-role identity
 * validation and AppContainer setup, a fresh private listener per
 * generation, spawn and authenticated accept bound to the spawned
 * PID/session/role/nonce, restart under the shared ladder with a
 * bounded backoff, and a stop that sends the advisory Shutdown frame,
 * closes the channel, terminates the Job Object and waits with the
 * caller's budget. Every failure releases its handles before it
 * returns, so a refused spawn leaves no process and no handle behind.
 */

#include "WindowsSupervisor.h"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <utility>

namespace i2p::privsep
{
namespace
{
	using Clock = std::chrono::steady_clock;

	std::string Win32ErrorText (DWORD error)
	{
		char * message = nullptr;
		const DWORD length = FormatMessageA (
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error,
			MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPSTR> (&message), 0, nullptr);
		if (!length || !message) return "Windows error " + std::to_string (error);
		std::string text (message, length);
		LocalFree (message);
		while (!text.empty () && (text.back () == '\r' || text.back () == '\n' || text.back () == ' '))
			text.pop_back ();
		return text;
	}

	uint64_t ElapsedMs (Clock::time_point since)
	{
		const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds> (Clock::now () - since).count ();
		return elapsed > 0 ? static_cast<uint64_t> (elapsed) : 0;
	}

	DWORD CurrentProcessSessionId ()
	{
		DWORD session = 0;
		if (!ProcessIdToSessionId (GetCurrentProcessId (), &session)) return 0;
		return session;
	}
}

bool IsCurrentProcessInJobObject ()
{
	BOOL insideJob = FALSE;
	return IsProcessInJob (GetCurrentProcess (), nullptr, &insideJob) && insideJob != FALSE;
}

WindowsWorkerSupervisor::WindowsWorkerSupervisor (
	std::string workerName, WindowsWorkerSupervisorConfig config, SupervisorStatus & status)
	: m_Name (std::move (workerName))
	, m_Config (std::move (config))
	, m_Status (status)
{
	BOOL insideJob = FALSE;
	if (IsProcessInJob (GetCurrentProcess (), nullptr, &insideJob)) m_InsideJob = insideJob != FALSE;
	HANDLE stopEvent = CreateEventW (nullptr, TRUE, FALSE, nullptr);
	if (!stopEvent)
		SetDiagnostic ("CreateEvent(stop): " + Win32ErrorText (GetLastError ()));
	else
		m_StopEvent.Reset (stopEvent);
}

WindowsWorkerSupervisor::~WindowsWorkerSupervisor ()
{
	(void)Stop (m_Config.stopBudgetMs);
}

i2p::expected<void, std::string> WindowsWorkerSupervisor::Start ()
{
	if (m_Config.executable.empty ())
		return FailTerminal (WindowsWorkerFailureKind::Spawn, 0, "empty worker executable");
	if (!m_StopEvent.IsValid ()) return FailTerminal (WindowsWorkerFailureKind::Spawn, 0, "stop event unavailable");
	const auto isolation = GetWindowsRoleIsolation (m_Config.role);
	if (isolation.appContainer)
	{
		auto container = CreateWindowsAppContainer (m_Config.role);
		if (!container) return FailTerminal (WindowsWorkerFailureKind::Spawn, 0, container.error ());
		m_Container.emplace (std::move (*container));
	}
	if (isolation.persistentWrites)
	{
		if (!m_Container)
			return FailTerminal (WindowsWorkerFailureKind::Spawn, 0,
				"role requires an AppContainer identity and cannot run with a restricted token");
		if (m_Config.persistentWriteRoot.empty ())
			return FailTerminal (
				WindowsWorkerFailureKind::Spawn, 0, "role requires an explicitly delegated persistent-write root");
		auto granted = GrantAppContainerPath (*m_Container, m_Config.persistentWriteRoot, true);
		if (!granted) return FailTerminal (WindowsWorkerFailureKind::Spawn, 0, granted.error ());
	}
	const uint32_t generation = m_Policy.BeginGeneration ();
	return SpawnGeneration (generation);
}

// One generation owns exactly one listener and one nonce: the pipe is
// created here (random per spawn unless a test pins the name), the DACL
// carries the role's AppContainer SID, and the accept is bound to this
// child's PID, session, role and nonce. After a successful accept the
// server object is dropped because the single pipe instance now belongs
// to the channel; the next generation creates a new one.
i2p::expected<void, std::string> WindowsWorkerSupervisor::SpawnGeneration (uint32_t generation)
{
	m_State.store (SupervisorWorkerState::Starting, std::memory_order_release);
	PublishWindowsWorkerTransition (
		m_Status, m_Name, SupervisorWorkerState::Starting, generation, m_Policy.Restarts ());

	std::wstring pipeName = m_Config.pipeName;
	if (pipeName.empty ())
	{
		auto randomName = MakeRandomPipeName ();
		if (!randomName) return FailTerminal (WindowsWorkerFailureKind::Spawn, generation, randomName.error ());
		pipeName = *randomName;
	}
	const WindowsAppContainer * container = m_Container ? &*m_Container : nullptr;
	const std::wstring containerSid = WindowsAppContainerSidString (container);
	auto server = WindowsServer::Create (pipeName, containerSid);
	if (!server) return FailTerminal (WindowsWorkerFailureKind::Spawn, generation, server.error ());
	m_Server.emplace (std::move (*server));
	m_PipeName = pipeName;

	// The WindowsIpc helper owns the 32-byte CSPRNG nonce and its hex
	// codec.
	auto nonce = GenerateWindowsWorkerNonce ();
	if (!nonce) return FailTerminal (WindowsWorkerFailureKind::Spawn, generation, nonce.error ());

	auto spawned = SpawnWindowsWorker (m_Config.executable, m_Config.role, pipeName, GetCurrentProcessId (),
		CurrentProcessSessionId (), *nonce, container, GetWindowsWorkerPolicy (m_Config.role));
	if (!spawned)
	{
		// A nested-job topology is the usual cause of a rejected assignment,
		// so report it with the spawn error instead of leaving the operator
		// with a bare Win32 code.
		std::string detail = spawned.error ();
		if (m_InsideJob)
		{
			detail += " (";
			detail += std::string (NestedJobDiagnostic (true));
			detail += ")";
		}
		return FailTerminal (WindowsWorkerFailureKind::Spawn, generation, std::move (detail));
	}

	m_Worker = std::make_unique<WindowsWorkerProcess> (std::move (*spawned));
	m_ProcessId = m_Worker->processId;
	// The spawn helper is authoritative for the value the child actually
	// received; fall back to the requested value when it reports none.
	const std::wstring acceptedNonce = m_Worker->nonceHex.empty () ? *nonce : m_Worker->nonceHex;
	auto accepted = m_Server->AcceptBound (m_Config.acceptTimeoutMs, m_Worker->processId, CurrentProcessSessionId (),
		m_Config.role, acceptedNonce, container);
	if (!accepted)
	{
		// The child exists but never authenticated: terminate the Job Object
		// and wait for the process before returning, so nothing outlives this
		// call.
		return FailTerminal (WindowsWorkerFailureKind::Accept, generation, accepted.error ());
	}
	m_Channel = std::make_unique<WindowsChannel> (std::move (*accepted));
	// The accepted instance is consumed; the listener handle now lives in
	// the channel and the server object must not try to close it again.
	m_Server.reset ();

	m_Policy.NoteReady (Clock::now ());
	m_State.store (SupervisorWorkerState::Ready, std::memory_order_release);
	m_Running.store (true, std::memory_order_release);
	PublishWindowsWorkerTransition (m_Status, m_Name, SupervisorWorkerState::Ready, generation, m_Policy.Restarts ());
	return { };
}

void WindowsWorkerSupervisor::RunMonitor ()
{
	while (!StopRequested () && !IsTerminal ())
	{
		if (!PumpOnce (m_Config.pollIntervalMs)) return;
	}
}

bool WindowsWorkerSupervisor::PumpOnce (uint32_t maxWaitMs)
{
	std::lock_guard<std::mutex> lock (m_LifecycleMutex);
	if (IsTerminal ()) return false;
	if (StopRequested ()) return false;

	const auto now = Clock::now ();
	if (m_RestartPending)
	{
		if (now < m_RestartDue)
		{
			const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds> (m_RestartDue - now).count ();
			// Never sleep through a stop request and never block past the
			// caller's slice.
			const auto sleep = std::min<int64_t> (
				static_cast<int64_t> (std::max<uint32_t> (maxWaitMs, 1)), std::max<int64_t> (remaining, 1));
			if (m_StopEvent.IsValid ())
				WaitForSingleObject (m_StopEvent.Native (), static_cast<DWORD> (sleep));
			else
				Sleep (static_cast<DWORD> (sleep));
			return !IsTerminal () && !StopRequested ();
		}
		m_RestartPending = false;
		const uint32_t generation = m_PendingGeneration;
		m_PendingGeneration = 0;
		return SpawnGeneration (generation).has_value ();
	}

	if (!m_Worker) return false;

	HANDLE handles[2] { };
	DWORD handleCount = 0;
	if (m_Worker->process.IsValid ()) handles[handleCount++] = m_Worker->process.Native ();
	if (m_StopEvent.IsValid ()) handles[handleCount++] = m_StopEvent.Native ();
	if (handleCount == 0)
	{
		(void)FailTerminal (
			WindowsWorkerFailureKind::Assign, m_Policy.Generation (), "worker process handle is invalid");
		return false;
	}

	const DWORD wait = WaitForMultipleObjects (handleCount, handles, FALSE, maxWaitMs);
	if (wait == WAIT_TIMEOUT) return true;
	if (wait == WAIT_FAILED)
	{
		(void)FailTerminal (WindowsWorkerFailureKind::Assign, m_Policy.Generation (),
			"WaitForMultipleObjects(worker): " + Win32ErrorText (GetLastError ()));
		return false;
	}
	if (wait == WAIT_OBJECT_0 && m_Worker && m_Worker->process.IsValid () && handles[0] == m_Worker->process.Native ())
	{
		HandleWorkerExit ();
		return !IsTerminal ();
	}
	// The stop event was signalled (or an unexpected index arrived): the
	// caller observes StopRequested() and leaves the loop.
	return false;
}

void WindowsWorkerSupervisor::HandleWorkerExit ()
{
	// The exit code is diagnostic only (never a secret operand). It comes
	// from the retained process handle, not from the PID.
	DWORD exitCode = ERROR_PROCESS_ABORTED;
	if (m_Worker && m_Worker->process.IsValid ()) (void)GetExitCodeProcess (m_Worker->process.Native (), &exitCode);
	// Release the dead generation first: the next spawn needs the pipe
	// name and the Job Object back, and the handles must be closed only
	// after the wait.
	ReleaseWorker (m_Config.stopBudgetMs);
	if (StopRequested ()) return;

	const auto now = Clock::now ();
	const auto outcome = PlanWindowsWorkerFailure (m_Policy, WindowsWorkerFailureKind::Exit, now);
	m_State.store (outcome.state, std::memory_order_release);
	if (outcome.terminal)
	{
		m_Terminal.store (true, std::memory_order_release);
		m_Running.store (false, std::memory_order_release);
		m_RestartPending = false;
		m_PendingGeneration = 0;
		PublishFailedOnce (outcome);
		std::cerr << "windows worker supervisor (" << m_Name << "): " << outcome.reason << " (exit code " << exitCode
				  << ")" << std::endl;
		return;
	}

	m_Running.store (false, std::memory_order_release);
	m_RestartPending = true;
	m_PendingGeneration = outcome.generation;
	m_RestartDue = now + outcome.delay;
	PublishWindowsWorkerTransition (
		m_Status, m_Name, outcome.state, outcome.generation, outcome.restarts, outcome.reason);
	std::cerr << "windows worker supervisor (" << m_Name << "): " << outcome.reason << " (exit code " << exitCode
			  << "); restarting generation " << outcome.generation << " in " << outcome.delay.count () << " ms"
			  << std::endl;
}

void WindowsWorkerSupervisor::ReleaseWorker (uint32_t budgetMs)
{
	if (m_Channel)
	{
		// Close the channel before the job is released: the peer must never
		// see a channel whose process is already being torn down.
		m_Channel->Close ();
		m_Channel.reset ();
	}
	if (m_Worker)
	{
		// Terminate the whole Job Object first so no grandchild survives,
		// then reap and wait on the process handle. The handles stay alive
		// until the wait returns; closing the job is the KILL_ON_JOB_CLOSE
		// backstop.
		if (m_Worker->job.IsValid ()) (void)TerminateJobObject (m_Worker->job.Native (), ERROR_PROCESS_ABORTED);
		(void)m_Worker->Stop (budgetMs);
		m_Worker.reset ();
	}
	m_ProcessId = 0;
	m_Running.store (false, std::memory_order_release);
}

// Every terminal path funnels through here so the object ends
// handle-neutral: worker, channel, listener, stop event and container
// are released before the single Failed snapshot is published, and the
// detail is kept only in the bounded diagnostic (it may contain a path,
// never a secret operand). The supervisor stays terminal for its
// lifetime; only a new object may try again.
i2p::expected<void, std::string> WindowsWorkerSupervisor::FailTerminal (
	WindowsWorkerFailureKind kind, uint32_t generation, std::string detail)
{
	ReleaseWorker (m_Config.stopBudgetMs);
	// The listener, the stop event and the container are not needed after
	// a terminal failure; releasing them keeps a failed spawn
	// handle-neutral.
	m_Server.reset ();
	m_StopEvent.Reset ();
	if (m_Container) m_Container.reset ();
	m_ProcessId = 0;
	m_RestartPending = false;
	m_PendingGeneration = 0;
	m_Terminal.store (true, std::memory_order_release);
	m_Running.store (false, std::memory_order_release);
	m_State.store (SupervisorWorkerState::Failed, std::memory_order_release);
	SetDiagnostic (detail);
	const WindowsWorkerFailureOutcome outcome { SupervisorWorkerState::Failed, true, generation, m_Policy.Restarts (),
		std::chrono::milliseconds { 0 }, WindowsWorkerFailureReason (kind) };
	PublishFailedOnce (outcome);
	std::cerr << "windows worker supervisor (" << m_Name << "): " << outcome.reason << ": " << detail << std::endl;
	return i2p::unexpected (std::move (detail));
}

void WindowsWorkerSupervisor::PublishFailedOnce (const WindowsWorkerFailureOutcome & outcome)
{
	// Terminal state is published once: a repeated failure of the same
	// generation must not turn into an unbounded stream of snapshots.
	if (m_FailedPublished) return;
	m_FailedPublished = true;
	PublishWindowsWorkerTransition (
		m_Status, m_Name, SupervisorWorkerState::Failed, outcome.generation, outcome.restarts, outcome.reason);
}

// Stop supports two callers at once (a control thread and the monitor
// loop) and is idempotent: the stop flag is published and the stop
// event signalled before anything else, so a monitor waiting on it
// leaves its slice. The ordering is intentional: advisory Shutdown
// frame, short graceful window with the channel still open, channel
// close, Job Object termination, bounded wait. The Job Object remains
// the authority; a worker that ignores the frame is terminated, and the
// caller's budget bounds the whole sequence.
bool WindowsWorkerSupervisor::Stop (uint32_t budgetMs)
{
	std::lock_guard<std::mutex> lock (m_LifecycleMutex);
	m_StopRequested.store (true, std::memory_order_release);
	if (m_StopEvent.IsValid ()) SetEvent (m_StopEvent.Native ());

	const auto started = Clock::now ();
	bool clean = true;

	// One bounded, advisory Shutdown frame. The worker may already be
	// gone; a failed send is not an error because the Job Object is the
	// authority.
	if (m_Channel && m_Channel->IsOpen ())
	{
		const uint32_t sendBudget
			= std::min (RemainingBudgetMs (ElapsedMs (started), budgetMs), WINDOWS_WORKER_SHUTDOWN_SEND_MS);
		if (sendBudget > 0) (void)m_Channel->Send ({ MessageType::Shutdown, 0, { } }, sendBudget);
	}
	if (m_Worker)
	{
		// Graceful window: an authenticated worker normally leaves on the
		// Shutdown frame. The channel stays open for it, and the wait uses
		// the process handle (never the PID).
		const uint32_t remaining = RemainingBudgetMs (ElapsedMs (started), budgetMs);
		if (remaining > WINDOWS_WORKER_GRACEFUL_STOP_MS && m_Worker->process.IsValid ())
			(void)WaitForSingleObject (m_Worker->process.Native (), WINDOWS_WORKER_GRACEFUL_STOP_MS);
	}
	if (m_Channel)
	{
		m_Channel->Close ();
		m_Channel.reset ();
	}
	if (m_Worker)
	{
		// Forced path: terminate the whole Job Object (no grandchild
		// survives), then reap and wait with what is left of the caller's
		// budget. Closing the job afterwards is the KILL_ON_JOB_CLOSE
		// backstop. A worker that already left is a clean stop even if the
		// forced reap reports nothing left to terminate.
		const bool alreadyExited
			= m_Worker->process.IsValid () && WaitForSingleObject (m_Worker->process.Native (), 0) == WAIT_OBJECT_0;
		const uint32_t remaining = RemainingBudgetMs (ElapsedMs (started), budgetMs);
		if (m_Worker->job.IsValid ()) (void)TerminateJobObject (m_Worker->job.Native (), ERROR_PROCESS_ABORTED);
		if (!m_Worker->Stop (remaining) && !alreadyExited)
		{
			clean = false;
			std::cerr << "windows worker supervisor (" << m_Name
					  << "): worker did not leave within the stop budget; closing the Job Object" << std::endl;
		}
		m_Worker.reset ();
	}
	m_Server.reset ();
	m_StopEvent.Reset ();
	if (m_Container) m_Container.reset ();
	m_ProcessId = 0;
	m_RestartPending = false;
	m_PendingGeneration = 0;
	m_Running.store (false, std::memory_order_release);
	return clean;
}

i2p::expected<void, std::string> WindowsWorkerSupervisor::Send (const Message & message)
{
	std::lock_guard<std::mutex> lock (m_LifecycleMutex);
	if (!m_Channel || !m_Channel->IsOpen ()) return i2p::unexpected ("windows worker channel is not open");
	return m_Channel->Send (message, WINDOWS_WORKER_SEND_TIMEOUT_MS);
}

i2p::expected<std::optional<Message>, std::string> WindowsWorkerSupervisor::Receive (uint32_t timeoutMs)
{
	std::lock_guard<std::mutex> lock (m_LifecycleMutex);
	if (!m_Channel || !m_Channel->IsOpen ()) return i2p::unexpected ("windows worker channel is not open");
	return m_Channel->Receive (timeoutMs);
}

void WindowsWorkerSupervisor::SetDiagnostic (std::string detail)
{
	std::lock_guard<std::mutex> lock (m_Mutex);
	m_Diagnostic = std::move (detail);
}

std::string WindowsWorkerSupervisor::Diagnostic () const
{
	std::lock_guard<std::mutex> lock (m_Mutex);
	return m_Diagnostic;
}

std::string WindowsWorkerSupervisor::JobDiagnostic () const
{
	std::lock_guard<std::mutex> lock (m_Mutex);
	return m_JobDiagnostic;
}

}

#endif // _WIN32
