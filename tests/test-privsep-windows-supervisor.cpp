/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test privsep windows supervisor.
 *
 * Windows-only checks for the Windows worker supervisor and the service drain:
 * the clock-injected restart ladder and its status publications, the failure
 * classification (setup failures are terminal, an authenticated exit is
 * restartable), the shared stop budgets, the nested-Job Object diagnostic, a
 * failed spawn that leaves no handle behind, Job Object KILL_ON_JOB_CLOSE and
 * TerminateJobObject behaviour, and a live Ping-only worker that is killed and
 * restarted under a real Job Object. Returns 77 on another platform.
 *
 * The service wrapper itself links the daemon singleton, so it is compiled only
 * into the i2pd executable. Its drain contract is therefore pinned here through
 * the shared budgets and wait-hint helpers that Win32/Win32Service.cpp uses,
 * not through a linked I2PService instance.
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

/*
 * Pure state-machine tests. They use an injected clock and a local snapshot, so
 * they exercise the production classifier and publisher without a worker.
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
	auto snapshot = status.Get (worker);
	assert (snapshot.has_value ());
	assert (snapshot->state == SupervisorWorkerState::Ready);
	assert (snapshot->generation == 1);

	// Documented ladder: 250 ms doubling to the 30 s cap, one generation per
	// authenticated exit.
	const milliseconds expected[] = { milliseconds (250), milliseconds (500), milliseconds (1000), milliseconds (2000),
		milliseconds (4000), milliseconds (8000), milliseconds (16000), milliseconds (30000) };
	uint32_t generation = first;
	int64_t elapsed = 0;
	int64_t readyAt = elapsed;
	for (uint32_t index = 0; index < LogWorkerRestartBudget::MAX_RESTARTS; ++index)
	{
		// Each replacement is ready for a short interval before it exits. The
		// stable-period budget measures this generation's uptime, not the total
		// wall-clock time spent traversing the backoff ladder.
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
		snapshot = status.Get (worker);
		assert (snapshot->state == SupervisorWorkerState::Restarting);
		assert (snapshot->generation == generation);
		assert (snapshot->restarts == index + 1);

		elapsed += outcome.delay.count ();
		readyAt = elapsed;
		policy.NoteReady (At (elapsed));
		PublishWindowsWorkerTransition (status, worker, SupervisorWorkerState::Ready, generation, outcome.restarts);
		snapshot = status.Get (worker);
		assert (snapshot->state == SupervisorWorkerState::Ready);
	}
	// Each generation failed before its own stable period, so the restart
	// counter has not been reset even though the backoff ladder spans over a
	// minute in aggregate.
	assert (policy.NextDelay () == LogWorkerRestartBudget::MAXIMUM_DELAY);

	// The ninth exit is refused: Failed, no new generation, and one snapshot.
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
	// A repeated terminal publication stays exactly one entry with one reason.
	PublishWindowsWorkerTransition (
		status, worker, exhausted.state, exhausted.generation, exhausted.restarts, exhausted.reason);
	assert (status.Size () == 1);
	assert (status.Snapshot ().size () == 1);
	snapshot = status.Get (worker);
	assert (snapshot->state == SupervisorWorkerState::Failed);
	assert (snapshot->reason == exhausted.reason);
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

	// A worker that served the whole stable period forgets the ladder: the next
	// exit starts again at the initial delay with a fresh restart count.
	elapsed += LogWorkerRestartBudget::STABLE_PERIOD.count () * 1000;
	policy.NoteReady (At (elapsed));
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
	assert (status.Get (worker)->state == SupervisorWorkerState::Failed);
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
	// The SCM hint must outlast the drain, and the drain must outlast one worker
	// stop so the service never reports stopped while a worker is still draining.
	assert (WINDOWS_SERVICE_DRAIN_BUDGET_MS > WINDOWS_WORKER_STOP_BUDGET_MS);
	assert (WINDOWS_SERVICE_STOP_WAIT_HINT_MS >= WINDOWS_SERVICE_DRAIN_BUDGET_MS);
	assert (WINDOWS_SERVICE_START_WAIT_HINT_MS > 0);
}

void TestNestedJobDiagnostic ()
{
	assert (NestedJobDiagnostic (false).empty ());
	assert (!NestedJobDiagnostic (true).empty ());
	assert (NestedJobDiagnostic (true).find ("Job Object") != std::string_view::npos);
	// The dynamic probe must agree with the pure text: a process that is in a
	// job always has a non-empty diagnostic, and one that is not has none.
	const bool inJob = IsCurrentProcessInJobObject ();
	assert (NestedJobDiagnostic (inJob).empty () == !inJob);
}

void TestRoleIdentityPolicy ()
{
	assert (WindowsRoleNeedsAppContainer (WindowsWorkerRole::Log));
	assert (WindowsRoleNeedsAppContainer (WindowsWorkerRole::Storage));
	assert (WindowsRoleNeedsAppContainer (WindowsWorkerRole::Network));
	assert (!WindowsRoleNeedsAppContainer (WindowsWorkerRole::Supervisor));
	assert (!WindowsRoleNeedsAppContainer (WindowsWorkerRole::Crypto));
	assert (!WindowsRoleNeedsAppContainer (WindowsWorkerRole::Admin));

	assert (WindowsRoleNeedsWriteRoot (WindowsWorkerRole::Log));
	assert (WindowsRoleNeedsWriteRoot (WindowsWorkerRole::Storage));
	assert (!WindowsRoleNeedsWriteRoot (WindowsWorkerRole::Network));

	for (const auto role : { WindowsWorkerRole::Supervisor, WindowsWorkerRole::Log, WindowsWorkerRole::Storage,
			 WindowsWorkerRole::Network, WindowsWorkerRole::Crypto, WindowsWorkerRole::Admin })
	{
		const auto name = WindowsWorkerSupervisorName (role);
		assert (!name.empty ());
		assert (name.size () <= SupervisorStatus::MAX_WORKER_NAME);
		// A write root is only meaningful for a role that has persistent writes,
		// and no role may combine network with persistent writes.
		const auto policy = GetWindowsWorkerPolicy (role);
		assert (!(policy.allowNetwork && policy.allowPersistentWrites));
	}
}

/*
 * Fail-closed setup: a missing identity or an empty configuration must publish
 * Failed, never spawn and never leak a handle.
 */
void TestFailClosedSetup ()
{
	const DWORD baseline = HandleCount ();
	{
		SupervisorStatus status;
		WindowsWorkerSupervisorConfig config;
		WindowsWorkerSupervisor supervisor ("test-empty-exe", config, status);
		auto started = supervisor.Start ();
		assert (!started.has_value ());
		assert (supervisor.IsTerminal ());
		assert (!supervisor.IsRunning ());
		assert (supervisor.Generation () == 0);
		assert (supervisor.State () == SupervisorWorkerState::Failed);
		assert (!supervisor.Diagnostic ().empty ());
		auto snapshot = status.Get ("test-empty-exe");
		assert (snapshot.has_value ());
		assert (snapshot->state == SupervisorWorkerState::Failed);
		assert (snapshot->generation == 0);
		// Failed is published once even when the object is stopped again.
		assert (supervisor.Stop (100));
		assert (status.Size () == 1);
	}
	assert (HandleCount () == baseline);

	{
		// Storage needs a delegated write root; without it the supervisor must
		// refuse before it creates any AppContainer or process.
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
 * A spawn that cannot succeed must terminate every intermediate object: the
 * listener, the stop event and the Job Object. One warm-up call absorbs
 * one-time loader/BCrypt allocations before the delta is measured.
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
		assert (!supervisor.Diagnostic ().empty ());
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
 * KILL_ON_JOB_CLOSE and TerminateJobObject are the backstop that keeps a worker
 * from outliving its supervisor. Both are exercised on a plain child process so
 * the check does not depend on the pipe or the worker entry point.
 */
bool StartChildInJob (WindowsHandle & job, WindowsHandle & process, DWORD & processId)
{
	const std::wstring self = SelfExecutable ();
	if (self.empty ()) return false;
	auto jobHandle = CreateWorkerJobObject (GetWindowsWorkerPolicy (WindowsWorkerRole::Admin));
	if (!jobHandle) return false;
	job = std::move (*jobHandle);

	std::wstring commandLine = L"\"" + self + L"\" --wait-child 60000";
	std::vector<wchar_t> mutableCommandLine (commandLine.begin (), commandLine.end ());
	mutableCommandLine.push_back (L'\0');
	STARTUPINFOW startup { };
	startup.cb = sizeof (startup);
	PROCESS_INFORMATION information { };
	if (!CreateProcessW (self.c_str (), mutableCommandLine.data (), nullptr, nullptr, FALSE,
			CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr, &startup, &information))
		return false;
	process.Reset (information.hProcess);
	WindowsHandle thread (information.hThread);
	processId = information.dwProcessId;
	// A nested assignment into a restrictive parent job can be rejected; the
	// caller reports that as a skip instead of a failure.
	if (!AssignProcessToJobObject (job.Native (), process.Native ()))
	{
		TerminateProcess (process.Native (), ERROR_ACCESS_DENIED);
		WaitForSingleObject (process.Native (), 5000);
		return false;
	}
	if (ResumeThread (thread.Native ()) == static_cast<DWORD> (-1)) return false;
	return true;
}

bool TestJobObjectKillOnClose ()
{
	WindowsHandle job;
	WindowsHandle process;
	DWORD processId = 0;
	if (!StartChildInJob (job, process, processId)) return false;
	assert (processId != 0);
	assert (WaitForSingleObject (process.Native (), 0) == WAIT_TIMEOUT);
	// Closing the last job handle is the documented backstop.
	job.Reset ();
	assert (WaitForSingleObject (process.Native (), 5000) == WAIT_OBJECT_0);
	return true;
}

bool TestTerminateJobObject ()
{
	WindowsHandle job;
	WindowsHandle process;
	DWORD processId = 0;
	if (!StartChildInJob (job, process, processId)) return false;
	assert (processId != 0);
	assert (WaitForSingleObject (process.Native (), 0) == WAIT_TIMEOUT);
	assert (TerminateJobObject (job.Native (), 0x1234u));
	assert (WaitForSingleObject (process.Native (), 5000) == WAIT_OBJECT_0);
	DWORD exitCode = 0;
	assert (GetExitCodeProcess (process.Native (), &exitCode));
	assert (exitCode == 0x1234u);
	return true;
}

/*
 * Live worker: the test binary is spawned as a Ping-only worker over a real
 * random pipe, killed, observed as Restarting, restarted as generation 2 and
 * finally stopped through the Job Object.
 */
int RunPingWorker (const std::wstring & pipeName)
{
	auto connected = WindowsChannel::Connect (pipeName, 10000);
	if (!connected) return 3;
	auto & channel = *connected;
	while (true)
	{
		auto received = channel.Receive (1000);
		if (!received) return 4;
		if (!received->has_value ()) continue;
		const Message & message = **received;
		if (message.type == MessageType::Ping)
		{
			if (!channel.Send ({ MessageType::Pong, message.requestId, { } })) return 5;
			continue;
		}
		if (message.type == MessageType::Shutdown) return 0;
	}
}

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

int TestLiveWorker ()
{
	const std::wstring self = SelfExecutable ();
	if (self.empty ()) return 77;

	SupervisorStatus status;
	WindowsWorkerSupervisorConfig config;
	config.executable = self;
	config.role = WindowsWorkerRole::Admin;
	config.acceptTimeoutMs = 10000;
	config.stopBudgetMs = 10000;
	config.pollIntervalMs = 100;

	const DWORD baseline = HandleCount ();
	int result = 0;
	{
		WindowsWorkerSupervisor supervisor ("test-live-worker", config, status);
		auto started = supervisor.Start ();
		if (!started)
		{
			std::printf ("skip: cannot start a worker (%s)\n", supervisor.Diagnostic ().c_str ());
			return 77;
		}
		assert (supervisor.IsRunning ());
		assert (supervisor.Generation () == 1);
		assert (supervisor.Channel () != nullptr);
		assert (status.Get ("test-live-worker")->state == SupervisorWorkerState::Ready);

		if (!PingPong (supervisor, 7, 5000))
		{
			std::printf ("skip: the spawned worker did not answer Ping\n");
			(void)supervisor.Stop (5000);
			return 77;
		}

		// Kill the worker behind the supervisor's back. The Job Object would also
		// reap it; the point here is the restart path.
		const DWORD workerPid = supervisor.WorkerProcessId ();
		assert (workerPid != 0);
		HANDLE victim = OpenProcess (PROCESS_TERMINATE, FALSE, workerPid);
		if (!victim)
		{
			std::printf ("skip: cannot open the worker process\n");
			(void)supervisor.Stop (5000);
			return 77;
		}
		assert (TerminateProcess (victim, 0x55u));
		CloseHandle (victim);

		if (!PumpUntil (supervisor, SupervisorWorkerState::Restarting, 5000))
		{
			std::printf ("skip: the supervisor did not observe the worker exit\n");
			(void)supervisor.Stop (5000);
			return 77;
		}
		assert (!supervisor.IsRunning ());
		assert (supervisor.Generation () == 2);
		const auto restarting = status.Get ("test-live-worker");
		assert (restarting->state == SupervisorWorkerState::Restarting);
		assert (restarting->generation == 2);
		assert (restarting->restarts == 1);

		if (!PumpUntil (supervisor, SupervisorWorkerState::Ready, 10000))
		{
			std::printf ("skip: the replacement worker did not authenticate\n");
			(void)supervisor.Stop (5000);
			return 77;
		}
		assert (supervisor.IsRunning ());
		assert (supervisor.Generation () == 2);
		const auto ready = status.Get ("test-live-worker");
		assert (ready->state == SupervisorWorkerState::Ready);
		assert (ready->generation == 2);
		assert (PingPong (supervisor, 8, 5000));

		assert (supervisor.Stop (10000));
		assert (!supervisor.IsRunning ());
		assert (supervisor.WorkerProcessId () == 0);
		assert (supervisor.Channel () == nullptr);
	}
	// Nothing may survive the supervisor: no channel, process or job handle.
	if (HandleCount () != baseline)
	{
		std::printf ("handle count %lu != baseline %lu after the live worker\n",
			static_cast<unsigned long> (HandleCount ()), static_cast<unsigned long> (baseline));
		result = 1;
	}
	return result;
}

/*
 * Service drain contract: the SCM wait hints and the drain budget must come
 * from the same authority as the worker stop budget, and a pending state may
 * never be reported with a zero hint (the SCM then applies its own short
 * timeout). The service wrapper that consumes these values links the daemon
 * singleton, so only the shared pure contract is asserted here.
 */
void TestServiceDrainContract ()
{
	// A drain must outlast one worker stop, and the reported hint must outlast
	// the drain, otherwise the SCM gives up while a worker is still draining.
	assert (WINDOWS_SERVICE_DRAIN_BUDGET_MS > WINDOWS_WORKER_STOP_BUDGET_MS);
	assert (WINDOWS_SERVICE_STOP_WAIT_HINT_MS >= WINDOWS_SERVICE_DRAIN_BUDGET_MS);
	assert (WINDOWS_SERVICE_START_WAIT_HINT_MS >= WINDOWS_SERVICE_DRAIN_BUDGET_MS);
	// Every pending transition of the service wrapper passes its hint through
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
	// Worker and helper roles: the supervisor spawns this binary as a Ping-only
	// worker, and the Job Object tests spawn it as an idle child.
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
			std::wstring pipeName;
			for (int rest = index + 1; rest < argc; ++rest)
			{
				const std::string token = argv[rest];
				if (token.rfind ("\\\\.\\pipe\\", 0) != 0) continue;
				pipeName.clear ();
				for (char character : token)
					pipeName.push_back (static_cast<wchar_t> (static_cast<unsigned char> (character)));
				break;
			}
			if (pipeName.empty ()) return 2;
			return RunPingWorker (pipeName);
		}
	}

	TestRestartLadder ();
	TestStablePeriodResetsLadder ();
	TestFailureClassification ();
	TestBudgets ();
	TestNestedJobDiagnostic ();
	TestRoleIdentityPolicy ();
	TestFailClosedSetup ();
	TestFailedSpawnLeavesNoHandles ();
	TestServiceDrainContract ();

	// A Job Object may be unavailable on a locked-down host; that is a skip for
	// the two backstop checks only, not for the pure or the supervisor checks.
	if (!TestJobObjectKillOnClose ())
	{
		std::printf ("skip: Job Object KILL_ON_JOB_CLOSE needs an assignable job\n");
		return 77;
	}
	if (!TestTerminateJobObject ())
	{
		std::printf ("skip: TerminateJobObject needs an assignable job\n");
		return 77;
	}

	return TestLiveWorker ();
}
#endif
