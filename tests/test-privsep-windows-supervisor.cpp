/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test privsep windows supervisor.
 *
 * Windows-only checks for the Windows worker supervisor and the service
 * drain: the clock-injected restart ladder and its status publications,
 * the failure classification (setup failures are terminal, an
 * authenticated exit is restartable), the shared stop budgets, the
 * nested-Job Object and nested-UI-restriction diagnostics, the
 * mandatory containment limits of the created worker job, a failed
 * spawn that leaves no handle behind, Job Object KILL_ON_JOB_CLOSE and
 * TerminateJobObject behaviour, and a live worker started through the
 * production `RunWindowsWorker` entry point that is killed and
 * restarted under a real Job Object. Returns 77 on another
 * platform.
 *
 * The service wrapper itself links the daemon singleton, so it is
 * compiled only into the i2pd executable. Its drain contract is
 * therefore pinned here through the shared budgets and wait-hint
 * helpers that Win32/Win32Service.cpp uses, not through a linked
 * I2PService instance.
 *
 */

#ifndef _WIN32
#include <cstdio>
int main ()
{
	std::fprintf (stderr, "test-privsep-windows-supervisor: Windows only, skipping\n");
	return 77;
}
#else

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <string>
#include <vector>

#include "privsep/WindowsSupervisor.h"

using namespace i2p::privsep;

namespace
{
using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;

Clock::time_point At (int64_t ms)
{
	return Clock::time_point { } + milliseconds (ms);
}

std::wstring SelfExecutable ()
{
	wchar_t buffer[MAX_PATH * 4] { };
	const DWORD length = GetModuleFileNameW (nullptr, buffer, static_cast<DWORD> (std::size (buffer)));
	if (length == 0 || length >= std::size (buffer)) return { };
	return std::wstring (buffer, length);
}

DWORD HandleCount ()
{
	DWORD count = 0;
	if (!GetProcessHandleCount (GetCurrentProcess (), &count)) return 0xffffffffu;
	return count;
}

// Inspect the inherited handles before the worker repins its stdio.
// Invalid parent-only values must fail this check rather than be hidden
// by RunWindowsWorker opening new handles after CRT initialization.
bool HasUsableWorkerStandardHandles ()
{
	STARTUPINFOW startup { };
	GetStartupInfoW (&startup);
	if (!(startup.dwFlags & STARTF_USESTDHANDLES)) return false;
	for (HANDLE handle : { startup.hStdInput, startup.hStdOutput,
		startup.hStdError })
	{
		DWORD flags = 0;
		if (!handle || handle == INVALID_HANDLE_VALUE
			|| !GetHandleInformation (handle, &flags)
			|| GetFileType (handle) != FILE_TYPE_CHAR) return false;
	}
	return true;
}

// This parent path never enters normal tests or creates an IPC nonce.
// Child debugger tracing is safe only for this closed diagnostic mode.
int DiagnoseWorkerInitialization ()
{
	const auto executable = SelfExecutable ();
	if (executable.empty ()) return 1;
	const auto deadline = Clock::now () + std::chrono::seconds (15);
	auto spawned = SpawnWindowsWorkerStartupProbe (executable);
	if (!spawned)
	{
		std::printf ("startup probe: contained spawn failed\n");
		return 1;
	}
	if (!spawned->WaitUntil (deadline))
	{
		const bool reaped = spawned->TerminateAndWaitUntil (
			Clock::now () + std::chrono::seconds (2));
		std::printf ("startup probe: wait failed, terminated=%u\n",
			reaped ? 1u : 0u);
		return 1;
	}
	DWORD code = 0;
	if (!GetExitCodeProcess (spawned->process.Native (), &code))
	{
		std::printf ("startup probe: exit query failed\n");
		return 1;
	}
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits { };
	if (!QueryInformationJobObject (spawned->job.Native (),
		JobObjectExtendedLimitInformation, &limits, sizeof (limits), nullptr))
	{
		std::printf ("startup probe: memory query failed, exit=%lu\n",
			static_cast<unsigned long> (code));
		return 1;
	}
	// These counters contain no arguments or authentication data.
	// A near-limit peak does not establish allocation failure.
	std::printf ("startup probe: exit=%lu peak_process=%llu peak_job=%llu limit_process=%llu limit_job=%llu\n",
		static_cast<unsigned long> (code),
		static_cast<unsigned long long> (limits.PeakProcessMemoryUsed),
		static_cast<unsigned long long> (limits.PeakJobMemoryUsed),
		static_cast<unsigned long long> (limits.ProcessMemoryLimit),
		static_cast<unsigned long long> (limits.JobMemoryLimit));
	return code == 0 ? 0 : 1;
}

/*
 * The system text of one Win32 error, trimmed the way the product's
 * diagnostic trims it. It lets a check name a failure class without
 * hard-coding English.
 */
std::string SystemErrorText (DWORD error)
{
	char * message = nullptr;
	const DWORD length = FormatMessageA (
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error,
		MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPSTR> (&message), 0, nullptr);
	if (!length || !message) return { };
	std::string text (message, length);
	LocalFree (message);
	while (!text.empty () && (text.back () == '\r' || text.back () == '\n' || text.back () == ' '))
		text.pop_back ();
	return text;
}

/*
 * Read one published snapshot.
 *
 * Publishing a transition always leaves exactly one entry for that worker,
 * so an absent entry means the publisher and the reader disagree about the
 * worker name (or the MAX_WORKERS bound was reached) rather than that the
 * state is legitimately unknown. Reading the optional without checking it
 * would be undefined behaviour, and a build without the assertions would
 * reach the same dereference silently, so the helper reports the missing
 * entry and stops before any field is read.
 */
SupervisorWorkerSnapshot RequireSnapshot (const SupervisorStatus & status, const std::string & worker)
{
	const auto snapshot = status.Get (worker);
	assert (snapshot.has_value ());
	if (!snapshot)
	{
		std::printf ("missing supervisor snapshot for %s\n", worker.c_str ());
		std::abort ();
	}
	return *snapshot;
}

/*
 * Pure state-machine tests. They use an injected clock and a local
 * snapshot, so they exercise the production classifier and publisher
 * without a worker.
 */
void TestRestartLadder ()
{
	WindowsWorkerRestartPolicy policy;
	SupervisorStatus status;
	const std::string worker = "test-ladder";

	const uint32_t first = policy.BeginGeneration ();
	assert (first == 1);
	assert (policy.Generation () == 1);
	PublishWindowsWorkerTransition (status, worker, SupervisorWorkerState::Starting, first, 0);
	policy.NoteReady (At (0));
	PublishWindowsWorkerTransition (status, worker, SupervisorWorkerState::Ready, first, 0);
	const auto published = RequireSnapshot (status, worker);
	assert (published.state == SupervisorWorkerState::Ready);
	assert (published.generation == 1);

	// Documented ladder: 250 ms doubling to the 30 s cap, one generation
	// per
	// authenticated exit.
	const milliseconds expected[] = { milliseconds (250), milliseconds (500), milliseconds (1000), milliseconds (2000),
		milliseconds (4000), milliseconds (8000), milliseconds (16000), milliseconds (30000) };
	uint32_t generation = first;
	int64_t elapsed = 0;
	int64_t readyAt = elapsed;
	for (uint32_t index = 0; index < LogWorkerRestartBudget::MAX_RESTARTS; ++index)
	{
		// Each replacement is ready for a short interval before it exits. The
		// stable-period budget measures this generation's uptime, not the
		// total wall-clock time spent traversing the backoff ladder.
		elapsed += 10;
		assert (elapsed - readyAt < LogWorkerRestartBudget::STABLE_PERIOD.count () * 1000);
		auto outcome = PlanWindowsWorkerFailure (policy, WindowsWorkerFailureKind::Exit, At (elapsed));
		assert (!outcome.terminal);
		assert (outcome.state == SupervisorWorkerState::Restarting);
		assert (outcome.delay == expected[index]);
		assert (outcome.generation == generation + 1);
		assert (outcome.restarts == index + 1);
		// The published reason is static text and fits the snapshot bound.
		assert (!outcome.reason.empty ());
		assert (outcome.reason.size () <= SupervisorStatus::MAX_REASON);
		PublishWindowsWorkerTransition (
			status, worker, outcome.state, outcome.generation, outcome.restarts, outcome.reason);
		generation = outcome.generation;
		const auto restarting = RequireSnapshot (status, worker);
		assert (restarting.state == SupervisorWorkerState::Restarting);
		assert (restarting.generation == generation);
		assert (restarting.restarts == index + 1);

		elapsed += outcome.delay.count ();
		readyAt = elapsed;
		policy.NoteReady (At (elapsed));
		PublishWindowsWorkerTransition (status, worker, SupervisorWorkerState::Ready, generation, outcome.restarts);
		const auto recovered = RequireSnapshot (status, worker);
		assert (recovered.state == SupervisorWorkerState::Ready);
	}
	// Each generation failed before its own stable period, so the restart
	// counter has not been reset even though the backoff ladder spans over
	// a minute in aggregate. The budget measures per-generation uptime,
	// never the wall clock spent traversing the ladder, and the total is
	// exactly the shared ladder sum.
	assert (elapsed >= LogWorkerRestartBudget::TotalLadderDelay ().count ());
	assert (LogWorkerRestartBudget::TotalLadderDelay () >= LogWorkerRestartBudget::STABLE_PERIOD);
	assert (policy.NextDelay () == LogWorkerRestartBudget::MAXIMUM_DELAY);

	// The ninth exit is refused: Failed, no new generation, and one
	// snapshot.
	elapsed += 10;
	assert (elapsed - readyAt < LogWorkerRestartBudget::STABLE_PERIOD.count () * 1000);
	auto exhausted = PlanWindowsWorkerFailure (policy, WindowsWorkerFailureKind::Exit, At (elapsed));
	assert (exhausted.terminal);
	assert (exhausted.state == SupervisorWorkerState::Failed);
	assert (exhausted.generation == generation);
	assert (exhausted.restarts == LogWorkerRestartBudget::MAX_RESTARTS);
	assert (exhausted.delay == milliseconds (0));
	assert (exhausted.reason == WindowsWorkerFailureReason (WindowsWorkerFailureKind::Budget));
	assert (policy.Exhausted ());
	PublishWindowsWorkerTransition (
		status, worker, exhausted.state, exhausted.generation, exhausted.restarts, exhausted.reason);
	// A repeated terminal publication stays exactly one entry with one
	// reason.
	PublishWindowsWorkerTransition (
		status, worker, exhausted.state, exhausted.generation, exhausted.restarts, exhausted.reason);
	assert (status.Size () == 1);
	assert (status.Snapshot ().size () == 1);
	const auto failed = RequireSnapshot (status, worker);
	assert (failed.state == SupervisorWorkerState::Failed);
	assert (failed.reason == exhausted.reason);
	assert (policy.Generation () == generation);
}

void TestStablePeriodResetsLadder ()
{
	WindowsWorkerRestartPolicy policy;
	int64_t elapsed = 0;
	policy.BeginGeneration ();
	policy.NoteReady (At (elapsed));

	auto first = PlanWindowsWorkerFailure (policy, WindowsWorkerFailureKind::Exit, At (elapsed));
	assert (!first.terminal);
	assert (first.delay == milliseconds (250));
	elapsed += 10;
	policy.NoteReady (At (elapsed));

	auto second = PlanWindowsWorkerFailure (policy, WindowsWorkerFailureKind::Exit, At (elapsed));
	assert (!second.terminal);
	assert (second.delay == milliseconds (500));
	assert (second.restarts == 2);

	// A worker that served the whole stable period forgets the ladder: the
	// next exit starts again at the initial delay with a fresh restart
	// count.
	elapsed += LogWorkerRestartBudget::STABLE_PERIOD.count () * 1000;
	auto afterStable = PlanWindowsWorkerFailure (policy, WindowsWorkerFailureKind::Exit, At (elapsed));
	assert (!afterStable.terminal);
	assert (afterStable.delay == LogWorkerRestartBudget::INITIAL_DELAY);
	assert (afterStable.restarts == 1);
	// The generation stays monotonic across a reset.
	assert (afterStable.generation == second.generation + 1);
	assert (!policy.Exhausted ());
}

void TestFailureClassification ()
{
	assert (!IsTerminalWindowsWorkerFailure (WindowsWorkerFailureKind::Exit));
	assert (IsTerminalWindowsWorkerFailure (WindowsWorkerFailureKind::Spawn));
	assert (IsTerminalWindowsWorkerFailure (WindowsWorkerFailureKind::Assign));
	assert (IsTerminalWindowsWorkerFailure (WindowsWorkerFailureKind::Accept));
	assert (IsTerminalWindowsWorkerFailure (WindowsWorkerFailureKind::Budget));

	SupervisorStatus status;
	const std::string worker = "test-terminal";
	WindowsWorkerRestartPolicy policy;
	const uint32_t generation = policy.BeginGeneration ();
	policy.NoteReady (At (0));

	for (const auto kind : { WindowsWorkerFailureKind::Spawn, WindowsWorkerFailureKind::Assign,
			 WindowsWorkerFailureKind::Accept, WindowsWorkerFailureKind::Budget })
	{
		auto outcome = PlanWindowsWorkerFailure (policy, kind, At (1));
		assert (outcome.terminal);
		assert (outcome.state == SupervisorWorkerState::Failed);
		assert (outcome.generation == generation);
		assert (outcome.restarts == 0);
		assert (!outcome.reason.empty ());
		assert (outcome.reason == WindowsWorkerFailureReason (kind));
	}
	// A terminal setup failure never consumes the restart budget.
	assert (policy.Restarts () == 0);
	assert (!policy.Exhausted ());
	PublishWindowsWorkerTransition (status, worker, SupervisorWorkerState::Failed, generation, 0,
		WindowsWorkerFailureReason (WindowsWorkerFailureKind::Accept));
	// The lookup contract the helper depends on: an unknown worker has no
	// entry at all, so a missing snapshot is never confused with a default
	// state and a name mismatch cannot read uninitialised storage.
	assert (!status.Get ("never-published").has_value ());
	assert (RequireSnapshot (status, worker).state == SupervisorWorkerState::Failed);
}

void TestBudgets ()
{
	assert (RemainingBudgetMs (0, 5000) == 5000);
	assert (RemainingBudgetMs (4999, 5000) == 1);
	assert (RemainingBudgetMs (5000, 5000) == 0);
	assert (RemainingBudgetMs (5001, 5000) == 0);
	assert (RemainingBudgetMs (0, 0) == 0);

	assert (NonZeroWaitHint (0) == 1);
	assert (NonZeroWaitHint (30000) == 30000);
	assert (WINDOWS_WORKER_ACCEPT_TIMEOUT_MS > 0);
	assert (WINDOWS_WORKER_SEND_TIMEOUT_MS > 0);
	assert (WINDOWS_WORKER_STOP_BUDGET_MS > 0);
	assert (WINDOWS_WORKER_POLL_MS > 0);
	assert (WINDOWS_WORKER_SHUTDOWN_SEND_MS < WINDOWS_WORKER_STOP_BUDGET_MS);
	assert (WINDOWS_WORKER_GRACEFUL_STOP_MS > WINDOWS_WORKER_SHUTDOWN_SEND_MS);
	assert (WINDOWS_WORKER_GRACEFUL_STOP_MS < WINDOWS_WORKER_STOP_BUDGET_MS);
	// The SCM hint must outlast the drain, and the drain must outlast one
	// worker
	// stop so the service never reports stopped while a worker is still
	// draining.
	assert (WINDOWS_SERVICE_DRAIN_BUDGET_MS > WINDOWS_WORKER_STOP_BUDGET_MS);
	assert (WINDOWS_SERVICE_STOP_WAIT_HINT_MS >= WINDOWS_SERVICE_DRAIN_BUDGET_MS);
	assert (WINDOWS_SERVICE_START_WAIT_HINT_MS > 0);
}

void TestNestedJobDiagnostic ()
{
	assert (NestedJobDiagnostic (false).empty ());
	assert (!NestedJobDiagnostic (true).empty ());
	assert (NestedJobDiagnostic (true).find ("Job Object") != std::string_view::npos);
	// The dynamic probe must agree with the pure text: a process that is
	// in a job always has a non-empty diagnostic, and one that is not has
	// none.
	const bool inJob = IsCurrentProcessInJobObject ();
	assert (NestedJobDiagnostic (inJob).empty () == !inJob);

	// The nested-topology note is the inverse: it is reported exactly when
	// the worker job could not carry UI restrictions. Containment is not
	// affected, so the text never claims the spawn failed.
	assert (NestedJobUiDiagnostic (true).empty ());
	assert (!NestedJobUiDiagnostic (false).empty ());
	assert (NestedJobUiDiagnostic (false) == WINDOWS_NESTED_JOB_UI_DIAGNOSTIC);
	assert (NestedJobUiDiagnostic (false).find ("Job Object") != std::string_view::npos);
}

/*
 * The worker Job Object is the containment boundary. Its kill/die flags
 * and the per-role limits are always applied; the UI restrictions are
 * applied only when this process is not already inside a job, because a
 * nested job may not set UI limits. Querying the created job pins both
 * halves, so a future "make the spawn work" change cannot silently
 * trade KILL_ON_JOB_CLOSE for the UI restrictions.
 */
void TestWorkerJobContainment ()
{
	const auto policy = GetWindowsWorkerPolicy (WindowsWorkerRole::Admin);
	bool uiRestricted = false;
	auto job = CreateWorkerJobObject (policy, &uiRestricted);
	assert (job.has_value ());
	assert (job->IsValid ());
	assert (uiRestricted == !IsCurrentProcessInJobObject ());

	JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits { };
	assert (QueryInformationJobObject (
		job->Native (), JobObjectExtendedLimitInformation, &limits, sizeof (limits), nullptr));
	assert ((limits.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE) != 0);
	assert ((limits.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION) != 0);
	assert ((limits.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_ACTIVE_PROCESS) != 0);
	assert ((limits.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_PROCESS_MEMORY) != 0);
	assert (limits.BasicLimitInformation.ActiveProcessLimit == policy.processLimit);
	assert (limits.ProcessMemoryLimit == static_cast<SIZE_T> (policy.memoryLimit));

	JOBOBJECT_BASIC_UI_RESTRICTIONS ui { };
	assert (QueryInformationJobObject (job->Native (), JobObjectBasicUIRestrictions, &ui, sizeof (ui), nullptr));
	if (uiRestricted)
		assert ((ui.UIRestrictionsClass & JOB_OBJECT_UILIMIT_HANDLES) != 0);
	else
		assert (ui.UIRestrictionsClass == 0);
	assert (NestedJobUiDiagnostic (uiRestricted).empty () == uiRestricted);

	// A null out-parameter is allowed: the caller that only wants the job
	// must not be forced to observe the variant.
	auto plain = CreateWorkerJobObject (policy);
	assert (plain.has_value ());
	assert (plain->IsValid ());
}

void TestRoleIdentityPolicy ()
{
	assert (WindowsRoleNeedsAppContainer (WindowsWorkerRole::Log));
	assert (WindowsRoleNeedsAppContainer (WindowsWorkerRole::Storage));
	assert (WindowsRoleNeedsAppContainer (WindowsWorkerRole::Network));
	assert (!WindowsRoleNeedsAppContainer (WindowsWorkerRole::Supervisor));
	assert (WindowsRoleNeedsAppContainer (WindowsWorkerRole::Crypto));
	assert (WindowsRoleNeedsAppContainer (WindowsWorkerRole::Admin));

	assert (WindowsRoleNeedsWriteRoot (WindowsWorkerRole::Log));
	assert (WindowsRoleNeedsWriteRoot (WindowsWorkerRole::Storage));
	assert (!WindowsRoleNeedsWriteRoot (WindowsWorkerRole::Network));

	for (const auto role : { WindowsWorkerRole::Supervisor, WindowsWorkerRole::Log, WindowsWorkerRole::Storage,
			 WindowsWorkerRole::Network, WindowsWorkerRole::Crypto, WindowsWorkerRole::Admin })
	{
		const auto name = WindowsWorkerSupervisorName (role);
		assert (!name.empty ());
		assert (name.size () <= SupervisorStatus::MAX_WORKER_NAME);
		// A write root is only meaningful for a role that has persistent
		// writes,
		// and no role may combine network with persistent writes.
		const auto policy = GetWindowsWorkerPolicy (role);
		assert (!(policy.allowNetwork && policy.allowPersistentWrites));
	}
}

/*
 * Fail-closed setup: a missing identity or an empty configuration must
 * publish Failed, never spawn and never leak a handle.
 */
void TestFailClosedSetup ()
{
	const DWORD baseline = HandleCount ();
	{
		SupervisorStatus status;
		WindowsWorkerSupervisorConfig config;
		WindowsWorkerSupervisor supervisor ("test-empty-exe", config, status);
		// The topology probe is published at construction, so an operator
		// reading the diagnostic knows whether a nested job is possible
		// before the first spawn.
		assert (supervisor.JobDiagnostic ().empty () == !supervisor.IsInsideJobObject ());
		assert (supervisor.IsInsideJobObject () == IsCurrentProcessInJobObject ());
		auto started = supervisor.Start ();
		assert (!started.has_value ());
		assert (supervisor.IsTerminal ());
		assert (!supervisor.IsRunning ());
		assert (supervisor.Generation () == 0);
		assert (supervisor.State () == SupervisorWorkerState::Failed);
		assert (!supervisor.Diagnostic ().empty ());
		// A terminal failure owns no generation: no listener, no pipe name
		// and no process id may survive it, otherwise the next supervisor
		// would collide with a name this one still believes it owns.
		assert (supervisor.PipeName ().empty ());
		assert (supervisor.WorkerProcessId () == 0);
		auto snapshot = status.Get ("test-empty-exe");
		assert (snapshot.has_value ());
		assert (snapshot->state == SupervisorWorkerState::Failed);
		assert (snapshot->generation == 0);
		// Failed is published once even when the object is stopped again.
		assert (supervisor.Stop (100));
		assert (supervisor.PipeName ().empty ());
		assert (status.Size () == 1);
	}
	assert (HandleCount () == baseline);

	{
		// Storage needs a delegated write root; without it the supervisor
		// must refuse before it creates any AppContainer or process.
		SupervisorStatus status;
		WindowsWorkerSupervisorConfig config;
		config.executable = SelfExecutable ();
		config.role = WindowsWorkerRole::Storage;
		WindowsWorkerSupervisor supervisor ("test-storage-no-root", config, status);
		auto started = supervisor.Start ();
		assert (!started.has_value ());
		assert (supervisor.IsTerminal ());
		assert (supervisor.Diagnostic ().find ("root") != std::string::npos);
		assert (HandleCount () == baseline);
	}
}

/*
 * A spawn that cannot succeed must terminate every intermediate object:
 * the listener, the stop event and the Job Object. One warm-up call
 * absorbs one-time loader/BCrypt allocations before the delta is
 * measured. The diagnostic must also not be a process-attribute
 * rejection, which the kernel reports before it looks for the image.
 */
void TestFailedSpawnLeavesNoHandles ()
{
	WindowsWorkerSupervisorConfig config;
	config.executable = L"\\\\?\\C:\\openi2pd\\does-not-exist\\openi2pd-worker.exe";
	config.role = WindowsWorkerRole::Admin;

	auto attempt = [&config] () -> bool
	{
		SupervisorStatus status;
		WindowsWorkerSupervisor supervisor ("test-missing-worker", config, status);
		auto started = supervisor.Start ();
		if (started.has_value ()) return true;
		assert (supervisor.IsTerminal ());
		const std::string diagnostic = supervisor.Diagnostic ();
		assert (!diagnostic.empty ());
		// The failed generation must not keep a listener or a pipe name: the
		// next spawn has to allocate a fresh one instead of joining a name
		// this supervisor already owns.
		assert (supervisor.PipeName ().empty ());
		assert (supervisor.WorkerProcessId () == 0);
		// The kernel validates the process attribute list before it looks
		// for the image, so a dangling or malformed attribute value fails
		// with ERROR_INVALID_PARAMETER for any path. A host that cannot
		// create the AppContainer fails earlier; in every case this
		// diagnostic must not be the parameter-rejection text.
		const std::string badParameter = SystemErrorText (ERROR_INVALID_PARAMETER);
		assert (badParameter.empty () || diagnostic.find (badParameter) == std::string::npos);
		return false;
	};

	// Warm up once so lazy runtime allocations are not counted as a leak.
	(void)attempt ();
	const DWORD baseline = HandleCount ();
	assert (!attempt ());
	assert (!attempt ());
	assert (HandleCount () == baseline);
}

/*
 * Only an observed nested-job assignment denial is a capability skip.
 * Startup and authenticated lifecycle failures must fail regressions.
 * Diagnostic text and mere membership in a job cannot grant a skip.
 */
enum class FixtureOutcome { Passed = 0, Failed = 1, CapabilityDenied = 77 };

constexpr FixtureOutcome ClassifyJobAssignment (DWORD error, bool nested)
{
	return error == ERROR_SUCCESS ? FixtureOutcome::Passed
		: error == ERROR_ACCESS_DENIED && nested
			? FixtureOutcome::CapabilityDenied : FixtureOutcome::Failed;
}

constexpr int FixtureExitCode (FixtureOutcome first, FixtureOutcome second,
	int live)
{
	if (live != 0 || first == FixtureOutcome::Failed
		|| second == FixtureOutcome::Failed) return 1;
	return first == FixtureOutcome::CapabilityDenied
		|| second == FixtureOutcome::CapabilityDenied ? 77 : 0;
}

void TestFixtureOutcomeClassification ()
{
	assert (FixtureExitCode (FixtureOutcome::CapabilityDenied,
		FixtureOutcome::Passed, 1) == 1);
	assert (FixtureExitCode (FixtureOutcome::CapabilityDenied,
		FixtureOutcome::Passed, 0) == 77);
	assert (ClassifyJobAssignment (ERROR_SUCCESS, true) == FixtureOutcome::Passed);
	assert (ClassifyJobAssignment (ERROR_ACCESS_DENIED, true)
		== FixtureOutcome::CapabilityDenied);
	assert (ClassifyJobAssignment (ERROR_ACCESS_DENIED, false)
		== FixtureOutcome::Failed);
	assert (ClassifyJobAssignment (0xc0000142u, true) == FixtureOutcome::Failed);
	assert (ClassifyJobAssignment (ERROR_INVALID_HANDLE, true)
		== FixtureOutcome::Failed);
}

/*
 * KILL_ON_JOB_CLOSE and TerminateJobObject are the backstop that keeps
 * a worker from outliving its supervisor. Both are exercised on a plain
 * child process so the check does not depend on the pipe or the worker
 * entry point.
 */
FixtureOutcome StartChildInJob (WindowsHandle & job, WindowsHandle & process, DWORD & processId)
{
	const std::wstring self = SelfExecutable ();
	if (self.empty ()) return FixtureOutcome::Failed;
	bool uiRestricted = false;
	auto jobHandle = CreateWorkerJobObject (GetWindowsWorkerPolicy (WindowsWorkerRole::Admin), &uiRestricted);
	if (!jobHandle) return FixtureOutcome::Failed;
	job = std::move (*jobHandle);

	std::wstring commandLine = L"\"" + self + L"\" --wait-child 60000";
	std::vector<wchar_t> mutableCommandLine (commandLine.begin (), commandLine.end ());
	mutableCommandLine.push_back (L'\0');
	STARTUPINFOW startup { };
	startup.cb = sizeof (startup);
	PROCESS_INFORMATION information { };
	if (!CreateProcessW (self.c_str (), mutableCommandLine.data (), nullptr, nullptr, FALSE,
			CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr, &startup, &information))
	{
		std::printf ("failure: cannot create the helper child (Win32 error %lu)\n",
			static_cast<unsigned long> (GetLastError ()));
		return FixtureOutcome::Failed;
	}
	process.Reset (information.hProcess);
	WindowsHandle thread (information.hThread);
	processId = information.dwProcessId;
	// Preserve the native assignment outcome before cleanup. Only a
	// proven access denial in an existing job permits a capability skip.
	if (!AssignProcessToJobObject (job.Native (), process.Native ()))
	{
		const DWORD error = GetLastError ();
		const BOOL terminated = TerminateProcess (process.Native (), ERROR_ACCESS_DENIED);
		const DWORD waited = WaitForSingleObject (process.Native (), 5000);
		if (!terminated || waited != WAIT_OBJECT_0) return FixtureOutcome::Failed;
		std::printf ("cannot assign the helper child to a worker job (Win32 error %lu)\n",
			static_cast<unsigned long> (error));
		return ClassifyJobAssignment (error, IsCurrentProcessInJobObject ());
	}
	if (ResumeThread (thread.Native ()) == static_cast<DWORD> (-1)) return FixtureOutcome::Failed;
	return FixtureOutcome::Passed;
}

FixtureOutcome TestJobObjectKillOnClose ()
{
	WindowsHandle job;
	WindowsHandle process;
	DWORD processId = 0;
	const auto started = StartChildInJob (job, process, processId);
	if (started != FixtureOutcome::Passed) return started;
	if (!(processId != 0)) return FixtureOutcome::Failed;
	if (!(WaitForSingleObject (process.Native (), 0) == WAIT_TIMEOUT)) return FixtureOutcome::Failed;
	// Closing the last job handle is the documented backstop.
	job.Reset ();
	if (!(WaitForSingleObject (process.Native (), 5000) == WAIT_OBJECT_0)) return FixtureOutcome::Failed;
	return FixtureOutcome::Passed;
}

FixtureOutcome TestTerminateJobObject ()
{
	WindowsHandle job;
	WindowsHandle process;
	DWORD processId = 0;
	const auto started = StartChildInJob (job, process, processId);
	if (started != FixtureOutcome::Passed) return started;
	if (!(processId != 0)) return FixtureOutcome::Failed;
	if (!(WaitForSingleObject (process.Native (), 0) == WAIT_TIMEOUT)) return FixtureOutcome::Failed;
	if (!(TerminateJobObject (job.Native (), 0x1234u))) return FixtureOutcome::Failed;
	if (!(WaitForSingleObject (process.Native (), 5000) == WAIT_OBJECT_0)) return FixtureOutcome::Failed;
	DWORD exitCode = 0;
	if (!(GetExitCodeProcess (process.Native (), &exitCode))) return FixtureOutcome::Failed;
	if (!(exitCode == 0x1234u)) return FixtureOutcome::Failed;
	return FixtureOutcome::Passed;
}

/* One request/reply round trip on the authenticated channel. */
bool PingPong (WindowsWorkerSupervisor & supervisor, uint32_t requestId, uint32_t deadlineMs)
{
	if (!supervisor.Send ({ MessageType::Ping, requestId, { } })) return false;
	const auto deadline = Clock::now () + milliseconds (deadlineMs);
	while (Clock::now () < deadline)
	{
		auto received = supervisor.Receive (250);
		if (!received) return false;
		if (!received->has_value ()) continue;
		const Message & message = **received;
		if (message.type == MessageType::Pong) return message.requestId == requestId;
	}
	return false;
}

bool PumpUntil (WindowsWorkerSupervisor & supervisor, SupervisorWorkerState state, uint32_t deadlineMs)
{
	const auto deadline = Clock::now () + milliseconds (deadlineMs);
	while (Clock::now () < deadline)
	{
		if (supervisor.State () == state) return true;
		if (supervisor.IsTerminal ()) return false;
		if (!supervisor.PumpOnce (100)) return false;
	}
	return supervisor.State () == state;
}

/*
 * Live worker: the test binary is spawned as a real worker through the
 * production entry point (`RunWindowsWorker`), so the spawn nonce
 * handshake, the role binding and the Ping/Pong/Shutdown loop are the
 * same code the daemon uses. The worker is killed, observed as
 * Restarting, restarted as generation 2 and finally stopped through the
 * Job Object.
 */
int TestLiveWorker ()
{
	const std::wstring self = SelfExecutable ();
	if (self.empty ()) return 1;

	SupervisorStatus status;
	WindowsWorkerSupervisorConfig config;
	config.executable = self;
	// The role decides how the child is isolated, and that decides whether
	// this check can run at all. Every role except the supervisor is an
	// AppContainer, and a runner whose test image is not readable from an
	// AppContainer-visible path makes the child exit before it can connect.
	// That turned a real spawn/accept defect into a silent skip for whole
	// batches. The supervisor role uses the restricted-token path, so the
	// live worker (connect, nonce handshake, Ping, restart) is exercised
	// instead of skipped.
	config.role = WindowsWorkerRole::Supervisor;
	config.acceptTimeoutMs = 10000;
	config.stopBudgetMs = 10000;
	config.pollIntervalMs = 100;

	const DWORD baseline = HandleCount ();
	// The supervisor scope is inside the lambda so that every outcome,
	// including startup failure, reaches the handle-count check below.
	// Returning outside this scope would conceal resource leaks.
	const int outcome = [&] () -> int
	{
		WindowsWorkerSupervisor supervisor ("test-live-worker", config, status);
		auto started = supervisor.Start ();
		if (!started)
		{
			std::printf ("failure: cannot start a worker (%s)\n", supervisor.Diagnostic ().c_str ());
			return 1;
		}
		if (!supervisor.IsRunning ()) return 1;
		if (!(supervisor.Generation () == 1)) return 1;
		if (supervisor.Channel () == nullptr) return 1;
		if (supervisor.PipeName ().empty ()) return 1;
		const auto initial = status.Get ("test-live-worker");
		if (!initial || initial->state != SupervisorWorkerState::Ready) return 1;

		if (!PingPong (supervisor, 7, 5000))
		{
			std::printf ("failure: the spawned worker did not answer Ping\n");
			(void)supervisor.Stop (5000);
			return 1;
		}

		// Kill the worker to exercise observation and replacement rather
		// than the Job Object shutdown path.
		const DWORD workerPid = supervisor.WorkerProcessId ();
		if (workerPid == 0) return 1;
		HANDLE victim = OpenProcess (PROCESS_TERMINATE, FALSE, workerPid);
		if (!victim)
		{
			std::printf ("failure: cannot open the worker process\n");
			(void)supervisor.Stop (5000);
			return 1;
		}
		const BOOL terminated = TerminateProcess (victim, 0x55u);
		CloseHandle (victim);
		if (!terminated) return 1;

		if (!PumpUntil (supervisor, SupervisorWorkerState::Restarting, 5000))
		{
			std::printf ("failure: the supervisor did not observe the worker exit\n");
			(void)supervisor.Stop (5000);
			return 1;
		}
		if (supervisor.IsRunning ()) return 1;
		if (!(supervisor.Generation () == 2)) return 1;
		const auto restarting = status.Get ("test-live-worker");
		if (!restarting) return 1;
		if (!(restarting->state == SupervisorWorkerState::Restarting)) return 1;
		if (!(restarting->generation == 2)) return 1;
		if (!(restarting->restarts == 1)) return 1;

		if (!PumpUntil (supervisor, SupervisorWorkerState::Ready, 10000))
		{
			std::printf ("failure: the replacement worker did not authenticate\n");
			(void)supervisor.Stop (5000);
			return 1;
		}
		if (!supervisor.IsRunning ()) return 1;
		if (!(supervisor.Generation () == 2)) return 1;
		const auto ready = status.Get ("test-live-worker");
		if (!ready) return 1;
		if (!(ready->state == SupervisorWorkerState::Ready)) return 1;
		if (!(ready->generation == 2)) return 1;
		if (!PingPong (supervisor, 8, 5000)) return 1;

		if (!supervisor.Stop (10000)) return 1;
		if (supervisor.IsRunning ()) return 1;
		if (!(supervisor.WorkerProcessId () == 0)) return 1;
		if (supervisor.Channel () != nullptr) return 1;
		// The stopped supervisor owns no listener and no pipe name either.
		if (!supervisor.PipeName ().empty ()) return 1;
		return 0;
	} ();

	// Nothing may survive the supervisor: no channel, process or job
	// handle. This runs for success and failure alike.
	if (HandleCount () != baseline)
	{
		std::printf ("handle count %lu != baseline %lu after the live worker\n",
			static_cast<unsigned long> (HandleCount ()), static_cast<unsigned long> (baseline));
		return 1;
	}
	return outcome;
}

/*
 * Service drain contract: the SCM wait hints and the drain budget must
 * come from the same authority as the worker stop budget, and a pending
 * state may never be reported with a zero hint (the SCM then applies
 * its own short timeout). The service wrapper that consumes these
 * values links the daemon singleton, so only the shared pure contract
 * is asserted here.
 */
void TestServiceDrainContract ()
{
	// A drain must outlast one worker stop, and the reported hint must
	// outlast the drain, otherwise the SCM gives up while a worker is
	// still draining.
	assert (WINDOWS_SERVICE_DRAIN_BUDGET_MS > WINDOWS_WORKER_STOP_BUDGET_MS);
	assert (WINDOWS_SERVICE_STOP_WAIT_HINT_MS >= WINDOWS_SERVICE_DRAIN_BUDGET_MS);
	assert (WINDOWS_SERVICE_START_WAIT_HINT_MS >= WINDOWS_SERVICE_DRAIN_BUDGET_MS);
	// Every pending transition of the service wrapper passes its hint
	// through
	// NonZeroWaitHint; zero must never reach the SCM.
	assert (NonZeroWaitHint (0) != 0);
	assert (NonZeroWaitHint (WINDOWS_SERVICE_STOP_WAIT_HINT_MS) == WINDOWS_SERVICE_STOP_WAIT_HINT_MS);
	// A spent budget leaves nothing for another blocking step.
	assert (RemainingBudgetMs (WINDOWS_SERVICE_DRAIN_BUDGET_MS, WINDOWS_SERVICE_DRAIN_BUDGET_MS) == 0);
	assert (RemainingBudgetMs (WINDOWS_SERVICE_DRAIN_BUDGET_MS - 1, WINDOWS_SERVICE_DRAIN_BUDGET_MS) == 1);
}
}

int main (int argc, char ** argv)
{
	// Exact diagnostic modes return before any secret-bearing worker
	// dispatch or ordinary tests spawning authenticated children.
	if (argc > 1 && std::strcmp (argv[1], "--diagnose-worker-initialization") == 0)
		return argc == 2 ? DiagnoseWorkerInitialization () : 2;
	if (argc > 1 && std::strcmp (argv[1], "--windows-startup-probe") == 0)
		return argc == 2 && HasUsableWorkerStandardHandles () ? 0 : 3;
	// Helper roles: the supervisor spawns this binary as a real worker and
	// the Job Object tests spawn it as an idle child. The worker role is
	// forwarded to the production entry point instead of a local
	// re-implementation: a hand-rolled loop would silently skip the
	// `WorkerHello` spawn proof that `AcceptBound` requires, which made the
	// live-worker check unreachable while still reporting a skip.
	for (int index = 1; index < argc; ++index)
	{
		const std::string argument = argv[index];
		if (argument == "--wait-child")
		{
			const int waitMs = (index + 1 < argc) ? std::atoi (argv[index + 1]) : 1000;
			Sleep (waitMs > 0 ? static_cast<DWORD> (waitMs) : 1000);
			return 0;
		}
		if (argument == "--privsep-worker")
		{
			if (!HasUsableWorkerStandardHandles ()) return 3;
			// role pipe parentPid parentSession nonce
			if (index + 5 >= argc) return 2;
			return RunWindowsWorker (argv[index + 1], argv[index + 2], argv[index + 3], argv[index + 4],
				argv[index + 5]);
		}
	}

	TestRestartLadder ();
	TestStablePeriodResetsLadder ();
	TestFailureClassification ();
	TestBudgets ();
	TestNestedJobDiagnostic ();
	TestWorkerJobContainment ();
	TestRoleIdentityPolicy ();
	TestFailClosedSetup ();
	TestFailedSpawnLeavesNoHandles ();
	TestServiceDrainContract ();
	TestFixtureOutcomeClassification ();

	// Run the live lifecycle even when a backstop capability is denied.
	// A capability skip must never conceal an unrelated worker failure.
	const auto killOnClose = TestJobObjectKillOnClose ();
	const auto terminate = TestTerminateJobObject ();
	const int live = TestLiveWorker ();
	const int outcome = FixtureExitCode (killOnClose, terminate, live);
	if (outcome == 77)
	{
		std::printf ("skip: nested Job Object assignment was denied\n");
	}
	return outcome;
}
#endif
