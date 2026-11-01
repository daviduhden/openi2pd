/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Supervisor.
 *
 * Declares the supervisor entry point plus the process-local status
 * snapshot, the single log-worker restart budget, the storage-broker
 * reply classifier and the process-creation admission gate. The
 * supervisor owns the log-worker process, forwards framed log records
 * and never opens the log file itself. The status and policy types are
 * header inline and platform-free so the unit tests exercise the exact
 * production state machine without spawning a worker.
 */

#ifndef I2PD_PRIVSEP_SUPERVISOR_H__
#define I2PD_PRIVSEP_SUPERVISOR_H__

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Protocol.h"
#include "Worker.h"

namespace i2p::privsep
{
/**
 * Entry point for the supervisor process. The core owns the other
 * endpoint of the control socketpair. The supervisor owns the log
 * worker process and forwards structured records; it never opens the
 * log file, so it has no persistent-write capability of its own.
 */
int RunSupervisor (int fd);

/**
 * Process-wide admission gate for worker creation.
 *
 * The daemon calls `ForbidProcessCreation()` immediately before it
 * installs the runtime pledge, which is the last moment at which every
 * allowed worker is already running. Every spawn helper must consult
 * `IsProcessCreationForbidden()` before it forks, so the kernel policy
 * is not the only barrier against re-opening process creation: a code
 * path that tries to spawn after this point fails closed with a bounded
 * error instead of depending on pledge(2)/seccomp/Landlock to abort it.
 *
 * The flag is inherited across fork and reset by execve; an exec'ed
 * worker therefore starts with creation allowed by default and is
 * expected to set its own gate before serving frames.
 */
void ForbidProcessCreation ();
bool IsProcessCreationForbidden ();

// Prepared by the daemon before runtime pledge. The immutable root
// bounds all later worker delegation; this supervisor never has
// persistent-write access.
bool PrepareTorrentStorageSupervisor (const std::string & root, std::string & error);
// The broker's immutable data root is installed before spawn and
// inherited as fd 4. For each worker, only the relative path and fstat
// identity travel over IPC; the caller's rootFd stays local. OpenBSD
// sendfd/recvfd explicitly exclude directory descriptors.
i2p::expected<Channel, std::string> SpawnSupervisedTorrentStorage (int rootFd, std::string_view rootPath);
int RunTorrentStorageSupervisor (int fd);

/**
 * Lifecycle state of one worker owned by the supervisor. `Failed` is
 * terminal for the current spawn budget: the supervisor stops replacing
 * that worker and reports the state once instead of retrying forever.
 */
enum class SupervisorWorkerState : uint8_t
{
	Starting,
	Ready,
	Restarting,
	Failed
};

constexpr std::string_view SupervisorWorkerStateName (SupervisorWorkerState state)
{
	switch (state)
	{
	case SupervisorWorkerState::Starting: return "starting";
	case SupervisorWorkerState::Ready: return "ready";
	case SupervisorWorkerState::Restarting: return "restarting";
	case SupervisorWorkerState::Failed: return "failed";
	}
	return "unknown";
}

/** One immutable observation of an owned worker. */
struct SupervisorWorkerSnapshot
{
	std::string worker;
	SupervisorWorkerState state = SupervisorWorkerState::Starting;
	/** Spawn generation: increments on every replacement. */
	uint32_t generation = 0;
	/** Restarts performed since the worker was last stable. */
	uint32_t restarts = 0;
	/** Records the client gave up on; 0 when the caller cannot know. */
	uint64_t droppedRecords = 0;
	/**
	 * Last non-secret reason. Callers must pass static text only: a reason
	 * is never derived from a payload, a path, a log record or a
	 * credential.
	 */
	std::string reason;
};

/**
 * Bounded, mutex-published snapshot of the workers the supervisor owns.
 * Readers take a copy; they never hold a reference into the container.
 * The insertion order is stable and the container stops growing at
 * MAX_WORKERS, so a compromised or confused caller cannot make it
 * allocate without bound.
 */
class SupervisorStatus
{
public:
	static constexpr size_t MAX_WORKERS = 8;
	static constexpr size_t MAX_WORKER_NAME = 32;
	static constexpr size_t MAX_REASON = 96;

	void NoteStarting (std::string_view worker, uint32_t generation, std::string_view reason = { })
	{
		std::lock_guard<std::mutex> lock (m_Mutex);
		auto * entry = FindLocked (worker);
		if (!entry) return;
		entry->state = SupervisorWorkerState::Starting;
		entry->generation = generation;
		// The restart count is owned by LogWorkerRestartBudget; it is
		// published by NoteRestarting/NoteReady so a replacement does not
		// silently reset the history a reader uses to judge the worker's
		// health.
		entry->droppedRecords = 0;
		SetReason (*entry, reason);
	}

	void NoteReady (std::string_view worker, uint32_t generation, uint32_t restarts = 0)
	{
		std::lock_guard<std::mutex> lock (m_Mutex);
		auto * entry = FindLocked (worker);
		if (!entry) return;
		entry->state = SupervisorWorkerState::Ready;
		entry->generation = generation;
		entry->restarts = restarts;
		entry->reason.clear ();
	}

	void NoteRestarting (std::string_view worker, uint32_t generation, uint32_t restarts, std::string_view reason)
	{
		std::lock_guard<std::mutex> lock (m_Mutex);
		auto * entry = FindLocked (worker);
		if (!entry) return;
		entry->state = SupervisorWorkerState::Restarting;
		entry->generation = generation;
		entry->restarts = restarts;
		SetReason (*entry, reason);
	}

	/**
	 * Terminal: the restart budget is exhausted. Repeated calls stay
	 * one entry. */
	void NoteFailed (std::string_view worker, uint32_t generation, uint32_t restarts, std::string_view reason)
	{
		std::lock_guard<std::mutex> lock (m_Mutex);
		auto * entry = FindLocked (worker);
		if (!entry) return;
		entry->state = SupervisorWorkerState::Failed;
		entry->generation = generation;
		entry->restarts = restarts;
		SetReason (*entry, reason);
	}

	void NoteDroppedRecords (std::string_view worker, uint64_t droppedRecords)
	{
		std::lock_guard<std::mutex> lock (m_Mutex);
		auto * entry = FindLocked (worker);
		if (!entry) return;
		entry->droppedRecords = droppedRecords;
	}

	std::optional<SupervisorWorkerSnapshot> Get (std::string_view worker) const
	{
		std::lock_guard<std::mutex> lock (m_Mutex);
		for (const auto & entry : m_Workers)
		{
			if (std::string_view (entry.worker) == worker) return entry;
		}
		return std::nullopt;
	}

	std::vector<SupervisorWorkerSnapshot> Snapshot () const
	{
		std::lock_guard<std::mutex> lock (m_Mutex);
		return m_Workers;
	}

	size_t Size () const
	{
		std::lock_guard<std::mutex> lock (m_Mutex);
		return m_Workers.size ();
	}

private:
	/** Find-or-append. Returns nullptr once MAX_WORKERS is reached. */
	SupervisorWorkerSnapshot * FindLocked (std::string_view worker)
	{
		for (auto & entry : m_Workers)
		{
			if (std::string_view (entry.worker) == worker) return &entry;
		}
		if (m_Workers.size () >= MAX_WORKERS) return nullptr;
		m_Workers.emplace_back ();
		auto & entry = m_Workers.back ();
		entry.worker.assign (worker.substr (0, MAX_WORKER_NAME));
		return &entry;
	}

	static void SetReason (SupervisorWorkerSnapshot & entry, std::string_view reason)
	{
		entry.reason.assign (reason.substr (0, MAX_REASON));
	}

	mutable std::mutex m_Mutex;
	std::vector<SupervisorWorkerSnapshot> m_Workers;
};

/**
 * Process-local status of the running supervisor. It is a diagnostic
 * snapshot, not a wire protocol: another process cannot read this
 * memory, and exposing it over IPC would require a new message type
 * that has deliberately not been added. The daemon therefore sees
 * supervisor progress only through its own liveness checks and stderr.
 */
inline SupervisorStatus & GetSupervisorStatus ()
{
	static SupervisorStatus status;
	return status;
}

/**
 * The single restart policy for the supervisor's log worker and for one
 * Windows worker role.
 *
 * Initial delay 250 ms, doubling to a 30 s cap, reset after 60 s of
 * *stable service*, and at most 8 restarts before the supervisor gives
 * up. The budget is a restart count, not a clock window: a worker that
 * keeps exiting is replaced at most 8 times in the process lifetime and
 * the ladder then becomes terminal, so a permanently broken worker is
 * never respawned forever.
 * `TotalLadderDelay()` is the sum of the eight waits (61.75 s: the eighth
 * wait is the 30 s cap, not the unclamped 32 s). It is deliberately at
 * least one STABLE_PERIOD, so a worker that never stabilises is given a
 * full stable period of wall clock before the supervisor gives up, and
 * less than two, so the terminal state is reached in bounded time. Every
 * single wait also stays below STABLE_PERIOD, so no wait in the ladder
 * can be mistaken for stable service even by a caller that measured the
 * reset from a spawn instead of from the ready time.
 * The caller supplies `now`, so the progression is deterministic in
 * tests and identical in production; the reset is measured from the
 * `NoteReady` of the current generation, never from a wait or a spawn.
 */
class LogWorkerRestartBudget
{
public:
	static constexpr std::chrono::milliseconds INITIAL_DELAY { 250 };
	static constexpr std::chrono::milliseconds MAXIMUM_DELAY { 30000 };
	static constexpr std::chrono::seconds STABLE_PERIOD { 60 };
	static constexpr uint32_t MAX_RESTARTS = 8;

	/**
	 * Sum of every wait the ladder can hand out before the budget is
	 * spent. Exposed so tests and diagnostics quote the same arithmetic
	 * the scheduler uses instead of a hand-computed constant that can
	 * drift away from MAX_RESTARTS/MAXIMUM_DELAY.
	 */
	static constexpr std::chrono::milliseconds TotalLadderDelay ()
	{
		auto delay = INITIAL_DELAY;
		auto total = std::chrono::milliseconds (0);
		for (uint32_t index = 0; index < MAX_RESTARTS; ++index)
		{
			total += delay;
			delay = std::min (delay * 2, MAXIMUM_DELAY);
		}
		return total;
	}

	/** The worker for generation @a generation became ready at @a now. */
	void NoteReady (std::chrono::steady_clock::time_point now)
	{
		m_ReadyAt = now;
		m_HasReady = true;
	}

	/**
	 * Reset the budget when the current worker has served STABLE_PERIOD.
	 * Returns true when a reset happened (used to publish a ready state).
	 * Exhaustion is terminal until the owner calls Reset(): a late
	 * stability observation must never revive a spent budget.
	 */
	bool NoteStable (std::chrono::steady_clock::time_point now)
	{
		if (m_Exhausted) return false;
		if (!m_HasReady || m_Restarts == 0) return false;
		if (now - m_ReadyAt < STABLE_PERIOD) return false;
		Reset ();
		return true;
	}

	/**
	 * Account one worker failure at @a now and return the bounded delay to
	 * honour before the next spawn, or nullopt when the budget is
	 * exhausted. The timestamp keeps the call clock-injected and uniform
	 * with NoteReady/NoteStable; the ladder itself is a count, and the
	 * stable decision is charged by NoteStable, so nothing here depends on
	 * the clock.
	 */
	std::optional<std::chrono::milliseconds> NoteFailure (std::chrono::steady_clock::time_point now)
	{
		(void)now;
		if (m_Exhausted) return std::nullopt;
		if (m_Restarts >= MAX_RESTARTS)
		{
			m_Exhausted = true;
			return std::nullopt;
		}
		const auto delay = m_Delay;
		++m_Restarts;
		m_Delay = std::min (m_Delay * 2, MAXIMUM_DELAY);
		return delay;
	}

	void Reset ()
	{
		m_Restarts = 0;
		m_Delay = INITIAL_DELAY;
		m_HasReady = false;
		m_Exhausted = false;
	}

	uint32_t Restarts () const { return m_Restarts; }
	bool Exhausted () const { return m_Exhausted; }
	std::chrono::milliseconds NextDelay () const { return m_Delay; }

private:
	uint32_t m_Restarts = 0;
	std::chrono::milliseconds m_Delay { INITIAL_DELAY };
	std::chrono::steady_clock::time_point m_ReadyAt { };
	bool m_HasReady = false;
	bool m_Exhausted = false;
};

// A single wait must stay below the stable period, so the ladder is
// always distinguishable from service even if the reset were measured
// from a spawn instead of from the current generation's ready time.
static_assert (LogWorkerRestartBudget::MAXIMUM_DELAY < LogWorkerRestartBudget::STABLE_PERIOD,
	"a restart wait must stay shorter than the stable period");
// The terminal state is reached only after repeated failures that span
// at least one stable period, and within two of them.
static_assert (LogWorkerRestartBudget::TotalLadderDelay () >= LogWorkerRestartBudget::STABLE_PERIOD,
	"the restart ladder must span at least one stable period");
static_assert (LogWorkerRestartBudget::TotalLadderDelay () < LogWorkerRestartBudget::STABLE_PERIOD * 2,
	"the restart ladder must stay within two stable periods");

/**
 * How one received frame relates to the request the client is
 * waiting for. */
enum class SupervisorReplyKind : uint8_t
{
	/**
	 * A reply for an earlier request that this client already
	 * abandoned. */
	Stale,
	Matching,
	/**
	 * A reply for a request id this client has not sent yet. It cannot
	 * answer the pending request, so it is a bounded protocol fault: the
	 * reply and its descriptor are discarded and the request is rejected,
	 * but the broker channel and every worker behind it stay alive. The
	 * same kind is returned for an unbounded run of stale replies, which
	 * is a fault of the run rather than of one frame.
	 */
	OutOfOrder
};

struct SupervisorReplyDecision
{
	SupervisorReplyKind kind = SupervisorReplyKind::Matching;
	ErrorCode code = ErrorCode::None;
	std::string_view detail;
};

/**
 * Stale replies one request may drain before the run is itself treated as
 * a fault.
 *
 * A late duplicate for an already answered request is legitimate and is
 * drained so it cannot be mistaken for the pending answer. An endless
 * stream of them is not: draining it would let the broker keep one call
 * blocked forever, which is exactly what the per-frame deadline cannot
 * prevent. The channel is kept in both cases; only the call is rejected.
 */
inline constexpr unsigned MAX_DRAINED_STALE_REPLIES = 4;

constexpr SupervisorReplyDecision ClassifySupervisorReply (
	uint32_t pendingRequest, uint32_t receivedRequest, unsigned drainedStaleReplies = 0)
{
	if (receivedRequest < pendingRequest)
	{
		if (drainedStaleReplies >= MAX_DRAINED_STALE_REPLIES)
			return SupervisorReplyDecision { SupervisorReplyKind::OutOfOrder, ErrorCode::Busy,
				"storage supervisor reply flood" };
		return SupervisorReplyDecision { SupervisorReplyKind::Stale, ErrorCode::None,
			"stale storage supervisor reply" };
	}
	if (receivedRequest > pendingRequest)
		return SupervisorReplyDecision { SupervisorReplyKind::OutOfOrder, ErrorCode::BadField,
			"storage supervisor replied out of order" };
	return SupervisorReplyDecision { };
}
static_assert (MAX_DRAINED_STALE_REPLIES >= 1, "a request must be able to drain at least one late duplicate");
}

#endif
