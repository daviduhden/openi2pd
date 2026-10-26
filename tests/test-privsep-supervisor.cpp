/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test privsep supervisor.
 *
 * Exercises the supervisor's pure state machines: the single log-worker
 * restart budget (initial delay, doubling, cap, stable reset, exhaustion), the
 * bounded status snapshot (start/ready/restarting/failed transitions and the
 * dropping-count publication) and the storage-broker reply classifier that
 * rejects a future request id without tearing the channel down. No child
 * process, socket or broker is started.
 *
 */

#include <cassert>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

#include "privsep/Supervisor.h"

using namespace i2p::privsep;

namespace
{
using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;

Clock::time_point At (int64_t millisecondsSinceStart)
{
	return Clock::time_point { } + milliseconds (millisecondsSinceStart);
}

void TestStateNames ()
{
	assert (SupervisorWorkerStateName (SupervisorWorkerState::Starting) == "starting");
	assert (SupervisorWorkerStateName (SupervisorWorkerState::Ready) == "ready");
	assert (SupervisorWorkerStateName (SupervisorWorkerState::Restarting) == "restarting");
	assert (SupervisorWorkerStateName (SupervisorWorkerState::Failed) == "failed");
}

void TestRestartBudgetSchedule ()
{
	LogWorkerRestartBudget budget;
	assert (budget.Restarts () == 0);
	assert (!budget.Exhausted ());
	assert (budget.NextDelay () == LogWorkerRestartBudget::INITIAL_DELAY);

	// Documented ladder: 250 ms, 500 ms, 1 s, 2 s, 4 s, 8 s, 16 s, 30 s.
	const milliseconds expected[] = { milliseconds (250), milliseconds (500), milliseconds (1000), milliseconds (2000),
		milliseconds (4000), milliseconds (8000), milliseconds (16000), milliseconds (30000) };
	int64_t elapsed = 0;
	for (uint32_t index = 0; index < LogWorkerRestartBudget::MAX_RESTARTS; ++index)
	{
		const auto delay = budget.NoteFailure (At (elapsed));
		assert (delay.has_value ());
		assert (*delay == expected[index]);
		elapsed += delay->count ();
		budget.NoteReady (At (elapsed));
		assert (budget.Restarts () == index + 1);
		assert (!budget.Exhausted ());
	}
	// All eight waits happen inside one 60 s window, which is why the budget is
	// expressible as "at most eight restarts per stable period".
	assert (elapsed < LogWorkerRestartBudget::STABLE_PERIOD.count () * 1000);
	// The cap holds instead of doubling past 30 s.
	assert (budget.NextDelay () == LogWorkerRestartBudget::MAXIMUM_DELAY);

	// The ninth failure is refused and the state is terminal.
	assert (!budget.NoteFailure (At (elapsed)).has_value ());
	assert (budget.Exhausted ());
	assert (!budget.NoteFailure (At (elapsed + 1)).has_value ());
}

void TestRestartBudgetStableReset ()
{
	LogWorkerRestartBudget budget;
	const auto first = At (0);
	assert (budget.NoteFailure (first).has_value ());
	assert (budget.NoteFailure (At (100)).has_value ());
	assert (budget.Restarts () == 2);
	budget.NoteReady (At (200));

	// Less than the stable period does not reset anything.
	assert (!budget.NoteStable (At (200 + 59000)));
	assert (budget.Restarts () == 2);
	// Exactly the stable period resets the ladder.
	assert (budget.NoteStable (At (200 + 60000)));
	assert (budget.Restarts () == 0);
	assert (budget.NextDelay () == LogWorkerRestartBudget::INITIAL_DELAY);
	assert (!budget.Exhausted ());

	// A budget that never became ready cannot be called stable.
	LogWorkerRestartBudget fresh;
	assert (!fresh.NoteStable (At (1000000)));
}

void TestRestartBudgetReset ()
{
	LogWorkerRestartBudget budget;
	for (uint32_t index = 0; index < LogWorkerRestartBudget::MAX_RESTARTS; ++index)
	{
		assert (budget.NoteFailure (At (index)).has_value ());
	}
	assert (!budget.NoteFailure (At (100)).has_value ());
	assert (budget.Exhausted ());
	budget.Reset ();
	assert (!budget.Exhausted ());
	assert (budget.Restarts () == 0);
	assert (budget.NextDelay () == LogWorkerRestartBudget::INITIAL_DELAY);
	assert (budget.NoteFailure (At (200)).has_value ());
}

void TestStatusTransitions ()
{
	SupervisorStatus status;
	assert (status.Size () == 0);
	assert (!status.Get ("log").has_value ());

	status.NoteStarting ("log", 1, "configuring log worker");
	auto entry = status.Get ("log");
	assert (entry.has_value ());
	assert (entry->worker == "log");
	assert (entry->state == SupervisorWorkerState::Starting);
	assert (entry->generation == 1);
	assert (entry->restarts == 0);
	assert (entry->droppedRecords == 0);
	assert (entry->reason == "configuring log worker");

	status.NoteReady ("log", 1);
	entry = status.Get ("log");
	assert (entry->state == SupervisorWorkerState::Ready);
	assert (entry->reason.empty ());

	status.NoteRestarting ("log", 2, 1, "log worker exited");
	entry = status.Get ("log");
	assert (entry->state == SupervisorWorkerState::Restarting);
	assert (entry->generation == 2);
	assert (entry->restarts == 1);
	assert (entry->reason == "log worker exited");

	status.NoteRestarting ("log", 3, 2, "log worker exited");
	entry = status.Get ("log");
	assert (entry->generation == 3);
	assert (entry->restarts == 2);
	assert (status.Size () == 1); // transitions update one entry, never append

	status.NoteDroppedRecords ("log", 42);
	entry = status.Get ("log");
	assert (entry->droppedRecords == 42);
	assert (entry->state == SupervisorWorkerState::Restarting);
}

void TestStatusExhaustionIsPublishedOnce ()
{
	// Drive the production sequence for a worker that never gets stable: each
	// failure consumes one restart, and the first refusal publishes Failed.
	LogWorkerRestartBudget budget;
	SupervisorStatus status;
	status.NoteStarting ("log", 1);
	status.NoteReady ("log", 1);
	budget.NoteReady (At (0));
	uint32_t generation = 1;
	int64_t elapsed = 0;
	bool failed = false;
	size_t failedTransitions = 0;
	while (true)
	{
		const auto delay = budget.NoteFailure (At (elapsed));
		if (!delay)
		{
			status.NoteFailed ("log", generation, budget.Restarts (), "restart budget exhausted");
			++failedTransitions;
			failed = true;
			break;
		}
		elapsed += delay->count ();
		++generation;
		status.NoteRestarting ("log", generation, budget.Restarts (), "log worker exited");
		budget.NoteReady (At (elapsed));
	}
	assert (failed);
	assert (failedTransitions == 1);
	assert (budget.Restarts () == LogWorkerRestartBudget::MAX_RESTARTS);

	const auto entry = status.Get ("log");
	assert (entry.has_value ());
	assert (entry->state == SupervisorWorkerState::Failed);
	assert (entry->reason == "restart budget exhausted");
	assert (entry->restarts == LogWorkerRestartBudget::MAX_RESTARTS);
	assert (status.Size () == 1);

	// A repeated publication stays a single failed entry, so a caller cannot
	// observe two states for the same worker.
	status.NoteFailed ("log", generation, budget.Restarts (), "restart budget exhausted");
	assert (status.Size () == 1);
	assert (status.Get ("log")->state == SupervisorWorkerState::Failed);
}

void TestStatusBounds ()
{
	SupervisorStatus status;
	// Names and reasons are truncated, never unbounded.
	const std::string longName (SupervisorStatus::MAX_WORKER_NAME * 3, 'n');
	const std::string longReason (SupervisorStatus::MAX_REASON * 3, 'r');
	status.NoteStarting (longName, 7, longReason);
	const auto entries = status.Snapshot ();
	assert (entries.size () == 1);
	assert (entries.front ().worker.size () == SupervisorStatus::MAX_WORKER_NAME);
	assert (entries.front ().reason.size () == SupervisorStatus::MAX_REASON);
	assert (entries.front ().generation == 7);

	// The snapshot is a copy: mutating it cannot change the published state.
	auto copy = status.Snapshot ();
	copy.front ().state = SupervisorWorkerState::Failed;
	copy.front ().reason = "tampered";
	assert (status.Snapshot ().front ().state == SupervisorWorkerState::Starting);
	assert (status.Snapshot ().front ().reason.size () == SupervisorStatus::MAX_REASON);

	// The container stops growing at MAX_WORKERS.
	SupervisorStatus bounded;
	for (size_t index = 0; index < SupervisorStatus::MAX_WORKERS * 2; ++index)
	{
		bounded.NoteStarting ("worker" + std::to_string (index), 1);
	}
	assert (bounded.Size () == SupervisorStatus::MAX_WORKERS);
	assert (bounded.Get ("worker0").has_value ());
	// An entry that never fitted is not silently invented on read.
	assert (!bounded.Get ("worker" + std::to_string (SupervisorStatus::MAX_WORKERS)).has_value ());
}

void TestReplyClassifier ()
{
	// The pending request is the only acceptable match.
	auto decision = ClassifySupervisorReply (5, 5);
	assert (decision.kind == SupervisorReplyKind::Matching);
	assert (decision.code == ErrorCode::None);
	assert (decision.detail.empty ());

	// An earlier reply is drained and ignored.
	decision = ClassifySupervisorReply (5, 4);
	assert (decision.kind == SupervisorReplyKind::Stale);
	assert (decision.code == ErrorCode::None);

	// A future id cannot answer this request. It is a typed BadField rejection
	// of one call, not a reason to tear the broker channel down: the caller
	// keeps the channel and every worker behind it.
	for (const uint32_t future : { 6u, 7u, 1000u, UINT32_MAX })
	{
		decision = ClassifySupervisorReply (5, future);
		assert (decision.kind == SupervisorReplyKind::OutOfOrder);
		assert (decision.code == ErrorCode::BadField);
		assert (decision.detail == "storage supervisor replied out of order");
	}

	// Request id zero is a legal pending id: the first spawn reply is id 2 in
	// production, so exercises the boundary rather than skipping it.
	decision = ClassifySupervisorReply (0, 0);
	assert (decision.kind == SupervisorReplyKind::Matching);
	decision = ClassifySupervisorReply (0, 1);
	assert (decision.kind == SupervisorReplyKind::OutOfOrder);
}

void TestProcessStatusStartsEmpty ()
{
	// No supervisor runs in this test process, so the published snapshot is
	// empty; the type is exercised above with local instances.
	assert (GetSupervisorStatus ().Size () == 0);
}
}

int main ()
{
	TestStateNames ();
	TestRestartBudgetSchedule ();
	TestRestartBudgetStableReset ();
	TestRestartBudgetReset ();
	TestStatusTransitions ();
	TestStatusExhaustionIsPublishedOnce ();
	TestStatusBounds ();
	TestReplyClassifier ();
	TestProcessStatusStartsEmpty ();
	return 0;
}
