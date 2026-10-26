/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: WindowsSupervisor.
 *
 * Declares the production consumer of the Windows worker spawn primitive: one
 * supervisor object owns one worker role, creates the per-generation listener,
 * authenticates the spawned child, replaces it under the shared
 * LogWorkerRestartBudget and publishes the platform-free SupervisorStatus
 * snapshot. The restart classifier, the status publisher, the failure
 * classification and the stop budgets are header-only and clock-injected, so
 * the unit tests exercise the exact production state machine without a live
 * Windows service. The Win32 part is compiled only on Windows.
 *
 */

#ifndef I2PD_PRIVSEP_WINDOWS_SUPERVISOR_H__
#define I2PD_PRIVSEP_WINDOWS_SUPERVISOR_H__

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include "Supervisor.h"
// Both headers are self-guarded by _WIN32; including them here keeps every
// declaration inside the project namespace instead of nesting headers in it.
#include "WindowsIpc.h"
#include "WindowsIsolation.h"

namespace i2p::privsep
{
/*
 * Bounded budgets, all in milliseconds. These are local deadlines, never wire
 * values. `ACCEPT` bounds how long an authenticated child may take to connect,
 * `STOP` bounds the drain of one worker, `SHUTDOWN_SEND` bounds the one
 * advisory Shutdown frame, `POLL` bounds one monitor slice and the two service
 * values are the SCM wait hints.
 */
inline constexpr uint32_t WINDOWS_WORKER_ACCEPT_TIMEOUT_MS = 5000;
inline constexpr uint32_t WINDOWS_WORKER_SEND_TIMEOUT_MS = 5000;
inline constexpr uint32_t WINDOWS_WORKER_STOP_BUDGET_MS = 5000;
inline constexpr uint32_t WINDOWS_WORKER_SHUTDOWN_SEND_MS = 250;
/** Graceful window after the Shutdown frame before the Job Object is used. */
inline constexpr uint32_t WINDOWS_WORKER_GRACEFUL_STOP_MS = 1000;
inline constexpr uint32_t WINDOWS_WORKER_POLL_MS = 250;
inline constexpr uint32_t WINDOWS_SERVICE_START_WAIT_HINT_MS = 30000;
inline constexpr uint32_t WINDOWS_SERVICE_STOP_WAIT_HINT_MS = 30000;
inline constexpr uint32_t WINDOWS_SERVICE_DRAIN_BUDGET_MS = 30000;

/**
 * Time left inside a caller-supplied budget. A zero result means the budget is
 * spent: the caller may not perform another blocking step on that budget.
 */
constexpr uint32_t RemainingBudgetMs (uint64_t elapsedMs, uint32_t budgetMs)
{
	return elapsedMs >= budgetMs ? 0 : budgetMs - static_cast<uint32_t> (elapsedMs);
}

/**
 * A pending SCM state must never be reported with a zero wait hint: the SCM
 * then applies its own short default timeout. Clamp to at least 1 ms.
 */
constexpr uint32_t NonZeroWaitHint (uint32_t requestedMs)
{
	return requestedMs == 0 ? 1 : requestedMs;
}

/**
 * Static, non-secret diagnostic for a supervisor that runs inside an existing
 * Job Object. `AssignProcessToJobObject` can fail inside a restrictive parent
 * job, so the topology is reported *before* the spawn instead of guessing from
 * the assignment error afterwards. Empty when the process is not in a job.
 */
inline constexpr std::string_view WINDOWS_NESTED_JOB_DIAGNOSTIC =
	"the service process already belongs to a Job Object; a nested worker job can be rejected by a restrictive parent job";

constexpr std::string_view NestedJobDiagnostic (bool insideJobObject)
{
	return insideJobObject ? WINDOWS_NESTED_JOB_DIAGNOSTIC : std::string_view { };
}

/** Why the current worker generation ended. */
enum class WindowsWorkerFailureKind : uint8_t
{
	/** Token, job-object or process creation failed. */
	Spawn,
	/** The process exists but job assignment or resume failed. */
	Assign,
	/** The spawned child never authenticated the pipe. */
	Accept,
	/** The authenticated worker left. The only restartable kind. */
	Exit,
	/** The restart ladder is exhausted for the current stable window. */
	Budget
};

/**
 * Only a worker that was authenticated and then exited is replaced. Setup
 * failures are terminal for the current supervisor: respawning would repeat
 * the same failure and could mask a policy or identity defect.
 */
constexpr bool IsTerminalWindowsWorkerFailure (WindowsWorkerFailureKind kind)
{
	return kind != WindowsWorkerFailureKind::Exit;
}

/**
 * Static text for the published supervisor reason. The detailed diagnostic
 * (which may contain a path) never reaches the snapshot.
 */
constexpr std::string_view WindowsWorkerFailureReason (WindowsWorkerFailureKind kind)
{
	switch (kind)
	{
	case WindowsWorkerFailureKind::Spawn:
		return "worker spawn failed";
	case WindowsWorkerFailureKind::Assign:
		return "worker job assignment failed";
	case WindowsWorkerFailureKind::Accept:
		return "worker authentication failed";
	case WindowsWorkerFailureKind::Exit:
		return "worker exited";
	case WindowsWorkerFailureKind::Budget:
		return "restart budget exhausted";
	}
	return "worker failure";
}

/**
 * Restart ladder of one supervised worker. It reuses the single
 * LogWorkerRestartBudget (250 ms doubling to 30 s, at most 8 restarts, reset
 * after 60 s of stable service) and owns the monotonic spawn generation, so the
 * delay schedule and the reported generation cannot drift apart.
 *
 * Every entry point takes the caller's `now`, which keeps the progression
 * deterministic in tests and identical in production.
 */
class WindowsWorkerRestartPolicy
{
public:
	using Clock = std::chrono::steady_clock;

	/** The worker of the current generation authenticated at @a now. */
	void NoteReady (Clock::time_point now) { m_Budget.NoteReady (now); }

	/** Start a new generation and return its 1-based number. */
	uint32_t BeginGeneration () { return ++m_Generation; }

	/** Account one authenticated worker exit; nullopt when the budget is spent. */
	std::optional<std::chrono::milliseconds> NoteExit (Clock::time_point now)
	{
		return m_Budget.NoteFailure (now);
	}

	/** Reset the ladder when the current worker served the stable period. */
	bool NoteStable (Clock::time_point now) { return m_Budget.NoteStable (now); }

	void Reset ()
	{
		m_Budget.Reset ();
		m_Generation = 0;
	}

	uint32_t Generation () const { return m_Generation; }
	uint32_t Restarts () const { return m_Budget.Restarts (); }
	bool Exhausted () const { return m_Budget.Exhausted (); }
	std::chrono::milliseconds NextDelay () const { return m_Budget.NextDelay (); }

private:
	LogWorkerRestartBudget m_Budget;
	uint32_t m_Generation = 0;
};

/** One decision of the restart classifier. */
struct WindowsWorkerFailureOutcome
{
	SupervisorWorkerState state = SupervisorWorkerState::Failed;
	/** Terminal failures are published as Failed and never respawned. */
	bool terminal = true;
	/** Generation to publish; the next one for a restart, the last for Failed. */
	uint32_t generation = 0;
	uint32_t restarts = 0;
	/** Bounded backoff before the next spawn; zero for a terminal failure. */
	std::chrono::milliseconds delay { 0 };
	/** Static reason, never derived from a payload, path or credential. */
	std::string_view reason;
};

/**
 * The single restart classifier. A restartable exit first offers the stable
 * reset, then charges the ladder, then allocates the next generation. A spent
 * budget becomes `Budget` (terminal). Setup failures are terminal immediately
 * and leave the generation untouched.
 */
inline WindowsWorkerFailureOutcome PlanWindowsWorkerFailure (WindowsWorkerRestartPolicy & policy,
	WindowsWorkerFailureKind kind, std::chrono::steady_clock::time_point now)
{
	WindowsWorkerFailureOutcome outcome;
	outcome.generation = policy.Generation ();
	outcome.restarts = policy.Restarts ();
	outcome.reason = WindowsWorkerFailureReason (kind);
	if (IsTerminalWindowsWorkerFailure (kind)) return outcome;

	// A worker that served STABLE_PERIOD forgets its earlier failures first;
	// this is the only path that leaves the terminal state.
	(void)policy.NoteStable (now);
	const auto delay = policy.NoteExit (now);
	if (!delay)
	{
		outcome.restarts = policy.Restarts ();
		outcome.reason = WindowsWorkerFailureReason (WindowsWorkerFailureKind::Budget);
		return outcome;
	}
	outcome.state = SupervisorWorkerState::Restarting;
	outcome.terminal = false;
	outcome.generation = policy.BeginGeneration ();
	outcome.restarts = policy.Restarts ();
	outcome.delay = *delay;
	return outcome;
}

/**
 * Publish one transition into a SupervisorStatus. This is the only place that
 * maps a state onto the snapshot writer, so the production supervisor and the
 * unit tests share one publisher. A repeated Failed publication stays a single
 * entry with a stable reason.
 */
inline void PublishWindowsWorkerTransition (SupervisorStatus & status, std::string_view worker,
	SupervisorWorkerState state, uint32_t generation, uint32_t restarts, std::string_view reason = { })
{
	switch (state)
	{
	case SupervisorWorkerState::Starting:
		status.NoteStarting (worker, generation, reason);
		return;
	case SupervisorWorkerState::Ready:
		status.NoteReady (worker, generation, restarts);
		return;
	case SupervisorWorkerState::Restarting:
		status.NoteRestarting (worker, generation, restarts, reason);
		return;
	case SupervisorWorkerState::Failed:
		status.NoteFailed (worker, generation, restarts, reason);
		return;
	}
}

#ifdef _WIN32

/**
 * A role needs the AppContainer identity when it holds external network access
 * or persistent writes: a restricted token alone is a privilege reduction, not
 * a network/filesystem boundary.
 */
inline bool WindowsRoleNeedsAppContainer (WindowsWorkerRole role)
{
	// The isolation table is the single authority; do not duplicate a subset.
	return GetWindowsRoleIsolation (role).appContainer;
}

/** Storage and log additionally need an explicitly delegated write root. */
inline bool WindowsRoleNeedsWriteRoot (WindowsWorkerRole role)
{
	return GetWindowsRoleIsolation (role).persistentWrites;
}

/** Stable snapshot name of a role; bounded to SupervisorStatus::MAX_WORKER_NAME. */
constexpr std::string_view WindowsWorkerSupervisorName (WindowsWorkerRole role)
{
	switch (role)
	{
	case WindowsWorkerRole::Supervisor:
		return "windows-supervisor";
	case WindowsWorkerRole::Log:
		return "windows-log";
	case WindowsWorkerRole::Storage:
		return "windows-storage";
	case WindowsWorkerRole::Network:
		return "windows-network";
	case WindowsWorkerRole::Crypto:
		return "windows-crypto";
	case WindowsWorkerRole::Admin:
		return "windows-admin";
	}
	return "windows-worker";
}

struct WindowsWorkerSupervisorConfig
{
	/** Worker image; it is passed to CreateProcess as an absolute path. */
	std::wstring executable;
	WindowsWorkerRole role = WindowsWorkerRole::Admin;
	/**
	 * Delegated write root for a role that needs persistent writes. The
	 * supervisor grants the container SID and refuses to start without it.
	 */
	std::wstring persistentWriteRoot;
	/** Fixed pipe name for tests; empty selects a per-spawn random name. */
	std::wstring pipeName;
	uint32_t acceptTimeoutMs = WINDOWS_WORKER_ACCEPT_TIMEOUT_MS;
	uint32_t stopBudgetMs = WINDOWS_WORKER_STOP_BUDGET_MS;
	uint32_t pollIntervalMs = WINDOWS_WORKER_POLL_MS;
};

/**
 * Owns exactly one worker role for its lifetime: creates the listener with the
 * AppContainer SID of the role, spawns the child, accepts only that child
 * (PID, session, role and nonce) and exposes the authenticated channel.
 *
 * Lifecycle: `Start()` publishes Starting, authenticates generation 1 and
 * publishes Ready. `RunMonitor()` (or repeated `PumpOnce()`) replaces an exited
 * worker under the restart ladder, publishing Restarting with the next
 * generation, and publishes Failed once when the budget is spent or a setup
 * step fails. `Stop()` wakes the monitor, sends the advisory Shutdown frame,
 * closes the channel, terminates the Job Object and waits within the supplied
 * budget; KILL_ON_JOB_CLOSE remains the backstop if the wait expires.
 *
 * Threading: status accessors are lock-free (atomics). Start/PumpOnce/
 * RunMonitor/Stop/Send/Receive share one lifecycle mutex, so a control thread
 * may Stop a supervisor whose monitor loop runs on another thread: Stop waits at
 * most one poll slice for the loop, then performs the bounded drain itself. The
 * type never posts a callback and never captures `this`, so no completion can
 * outlive the object.
 */
class WindowsWorkerSupervisor
{
public:
	using Clock = std::chrono::steady_clock;

	WindowsWorkerSupervisor (std::string workerName, WindowsWorkerSupervisorConfig config,
		SupervisorStatus & status = GetSupervisorStatus ());
	~WindowsWorkerSupervisor ();

	WindowsWorkerSupervisor (const WindowsWorkerSupervisor &) = delete;
	WindowsWorkerSupervisor & operator= (const WindowsWorkerSupervisor &) = delete;

	/**
	 * Validate the role policy and identity, create the stop event, then spawn
	 * and authenticate generation 1. Any failure is terminal and published.
	 */
	i2p::expected<void, std::string> Start ();

	/** Drain until Stop() or a terminal failure. Bounded by pollIntervalMs. */
	void RunMonitor ();

	/**
	 * One monitor slice: honour a pending backoff, spawn when due, or wait for
	 * the worker to exit. Returns false once the supervisor is terminal.
	 */
	bool PumpOnce (uint32_t maxWaitMs);

	/** Authenticated channel of the current generation, or nullptr. */
	WindowsChannel * Channel () { return m_Channel ? &*m_Channel : nullptr; }
	const WindowsChannel * Channel () const { return m_Channel ? &*m_Channel : nullptr; }

	i2p::expected<void, std::string> Send (const Message & message);
	i2p::expected<std::optional<Message>, std::string> Receive (uint32_t timeoutMs);

	/** Stop with the caller's budget; idempotent. */
	bool Stop (uint32_t budgetMs);
	bool Stop () { return Stop (m_Config.stopBudgetMs); }

	bool IsRunning () const { return m_Running.load (std::memory_order_acquire); }
	bool IsTerminal () const { return m_Terminal.load (std::memory_order_acquire); }
	bool StopRequested () const { return m_StopRequested.load (std::memory_order_acquire); }
	SupervisorWorkerState State () const { return m_State.load (std::memory_order_acquire); }
	uint32_t Generation () const { return m_Policy.Generation (); }
	uint32_t Restarts () const { return m_Policy.Restarts (); }
	/** Not a destructor barrier; the caller must stop before dropping it. */
	const WindowsWorkerRestartPolicy & Policy () const { return m_Policy; }
	const std::string & Name () const { return m_Name; }
	const std::wstring & PipeName () const { return m_PipeName; }
	DWORD WorkerProcessId () const { return m_ProcessId; }
	bool IsInsideJobObject () const { return m_InsideJob; }

	/** Copy of the last diagnostic (may contain a path; never a secret operand). */
	std::string Diagnostic () const;
	/** Nested-job diagnostic of this process, empty when not in a job. */
	std::string JobDiagnostic () const;

private:
	i2p::expected<void, std::string> SpawnGeneration (uint32_t generation);
	void HandleWorkerExit ();
	/** Publish Failed once, remember the diagnostic, release every handle. */
	i2p::expected<void, std::string> FailTerminal (
		WindowsWorkerFailureKind kind, uint32_t generation, std::string detail);
	/** Terminal publication is idempotent for the supervisor lifetime. */
	void PublishFailedOnce (const WindowsWorkerFailureOutcome & outcome);
	/** Close the channel, terminate the Job Object and wait before returning. */
	void ReleaseWorker (uint32_t budgetMs = WINDOWS_WORKER_STOP_BUDGET_MS);
	void SetDiagnostic (std::string detail);

	std::string m_Name;
	WindowsWorkerSupervisorConfig m_Config;
	SupervisorStatus & m_Status;
	WindowsWorkerRestartPolicy m_Policy;

	std::atomic<bool> m_StopRequested { false };
	std::atomic<bool> m_Terminal { false };
	std::atomic<bool> m_Running { false };
	std::atomic<SupervisorWorkerState> m_State { SupervisorWorkerState::Starting };

	std::optional<WindowsAppContainer> m_Container;
	std::optional<WindowsServer> m_Server;
	std::unique_ptr<WindowsWorkerProcess> m_Worker;
	std::unique_ptr<WindowsChannel> m_Channel;
	WindowsHandle m_StopEvent;
	DWORD m_ProcessId = 0;
	std::wstring m_PipeName;
	bool m_InsideJob = false;

	Clock::time_point m_RestartDue { };
	uint32_t m_PendingGeneration = 0;
	bool m_RestartPending = false;
	bool m_Started = false;
	/** A Failed snapshot is published at most once per supervisor lifetime. */
	bool m_FailedPublished = false;

	mutable std::mutex m_Mutex;
	std::string m_Diagnostic;
	std::string m_JobDiagnostic;
	/** Serialises the generation state between the monitor and a control stop. */
	std::mutex m_LifecycleMutex;
};

/** Dynamic Job Object topology probe used by the spawn preflight. */
bool IsCurrentProcessInJobObject ();

#endif // _WIN32

}

#endif
