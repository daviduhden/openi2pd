/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: hash-named runtime artifact storage.
 *
 * Declares the artifact-name/kind semantics, the validated target predicate
 * and the restart backoff shared by the isolated storage clients, plus the
 * data-storage worker entry point and its generation-fenced client. The
 * worker owns the three hash-named directories (netDb, peerProfiles and
 * destinations) and has no network capability; startup reads stay in the
 * core, so this boundary covers writes and removals only.
 */
#ifndef I2PD_PRIVSEP_DATA_STORAGE_H
#define I2PD_PRIVSEP_DATA_STORAGE_H

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <stop_token>

#include "../libi2pd/PersistentStorage.h"
#include "Protocol.h"

namespace i2p::privsep
{
/** True for the artifact kinds this worker is allowed to own. */
bool IsKnownArtifactKind (uint8_t kind);
/** Fixed per-kind root directory name ("netDb", "peerProfiles", "destinations"). */
const char * ArtifactDirectory (i2p::data::StorageArtifact kind);

/**
 * Canonical artifact name validation.
 *
 * `RouterInfoCache` and `PeerProfile` accept exactly a canonical 44-character
 * I2P-base64 identity hash; `DestinationKeys` accepts a canonical lower-case
 * 52-character base32 identity, '.', and a canonical decimal key type in
 * 0..65535 (0 is ElGamal, which the core still persists). Every kind rejects
 * path separators, ':', NUL, "..", an upper-case/invalid alphabet,
 * non-canonical padding or trailing bits. The check is a decode plus
 * re-encode equality, so a name that would map to a different file than the
 * core's read path is refused rather than repaired.
 */
bool IsCanonicalArtifactName (i2p::data::StorageArtifact kind, std::string_view name);

/**
 * Relative path of one artifact inside the delegated root. The layout matches
 * the core's existing readers exactly: `netDb/r&lt;first&gt;/routerInfo-&lt;name&gt;.dat`,
 * `peerProfiles/p&lt;first&gt;/profile-&lt;name&gt;.txt` and
 * `destinations/&lt;name&gt;.dat`. Returns an empty string for an unknown kind or a
 * non-canonical name.
 */
std::string ArtifactRelativePath (i2p::data::StorageArtifact kind, std::string_view name);

/**
 * Target metadata gate used by both the write and the remove path of the
 * worker. A target may be replaced or deleted only when it is a regular file,
 * owned by the worker's effective uid and carrying exactly one hard link.
 * A directory, symlink, foreign-owned file or hardlink is refused, never
 * repaired.
 */
bool AcceptsArtifactTarget (bool isRegularFile, uint32_t targetUid, uint32_t currentUid, uint32_t linkCount);

/**
 * Exponential restart backoff for a storage client whose worker died.
 *
 * The policy is shared by the router-storage and data-storage clients so both
 * fail closed at the same rate: 250 ms doubling to a 30 s cap, at most 8
 * attempts inside a 60 s window, then terminal failure until the owner calls
 * Start() again. No member blocks; callers ask CanAttempt() and schedule a new
 * attempt when the clock passes NextAttempt().
 *
 * Every member takes the caller's `now` and touches no clock, thread or
 * descriptor, so it is also usable from a unit test with an injected time.
 */
class StorageClientRestart
{
public:
	static constexpr unsigned MAX_ATTEMPTS = 8;
	static constexpr std::chrono::seconds WINDOW { 60 };
	static constexpr std::chrono::milliseconds MIN_BACKOFF { 250 };
	static constexpr std::chrono::seconds MAX_BACKOFF { 30 };

	/** Forget every attempt and pending delay. */
	void Reset (std::chrono::steady_clock::time_point now) noexcept;
	/**
	 * Record one failed attempt and schedule the next one. Returns false when
	 * the attempt budget inside the window is exhausted; the caller must then
	 * stop retrying.
	 */
	bool RecordFailure (std::chrono::steady_clock::time_point now) noexcept;
	/** True when the window allows another attempt at @a now. */
	bool CanAttempt (std::chrono::steady_clock::time_point now) noexcept;
	std::chrono::steady_clock::time_point NextAttempt () const noexcept { return m_NextAttempt; }
	unsigned Attempts () const noexcept { return m_Attempts; }
	std::chrono::milliseconds Backoff () const noexcept { return m_Backoff; }

private:
	unsigned m_Attempts = 0;
	std::chrono::milliseconds m_Backoff { MIN_BACKOFF };
	std::chrono::steady_clock::time_point m_WindowStart { };
	std::chrono::steady_clock::time_point m_NextAttempt { };
};

/**
 * True when a reply belongs to the current worker generation and to the
 * request the client is still waiting for. A reply from a replaced worker, or
 * one for an already abandoned request, must be dropped instead of being
 * applied to a different operation.
 */
bool IsCurrentDataStorageReply (
	uint32_t replyGeneration, uint32_t replyRequestId, uint32_t currentGeneration, uint32_t inFlightRequestId);

/**
 * Generation-fenced client for the data-storage worker.
 *
 * Single-flight: one request at a time under a mutex, with monotonic request
 * ids and a 5 s per-request deadline. A send/receive failure closes the
 * channel and enters Restarting; the next Write/Remove past the backoff
 * deadline respawns the worker, reconfigures it and bumps the generation.
 * An ambiguous in-flight write is never replayed: the call that hit the
 * failure returns false. Request-id exhaustion is a local Busy refusal that
 * leaves the channel usable.
 *
 * Concurrency: Start, Stop, Write and Remove serialize on that mutex, so a
 * second caller waits for the in-flight request instead of interleaving two
 * frames on the channel. The diagnostic accessors read atomics and may be
 * called from any thread. The client owns its root descriptor and child
 * process; Stop (also run by the destructor) closes both.
 */
class DataStorageClient
{
public:
	DataStorageClient ();
	~DataStorageClient ();
	DataStorageClient (const DataStorageClient &) = delete;
	DataStorageClient & operator= (const DataStorageClient &) = delete;

	bool Start (const std::filesystem::path & root, std::string & error);
	void Stop ();
	/**
	 * Write or remove one artifact. A stopped token cancels the operation
	 * cooperatively: before the send nothing happens, after the send the
	 * worker is told to abandon the request if it has not run yet, the reply
	 * is drained and the call returns false. An ambiguous write is never
	 * replayed, so a cancelled request may still have been applied.
	 */
	bool Write (i2p::data::StorageArtifact kind, std::string_view name, std::span<const uint8_t> bytes,
		std::stop_token token = { });
	bool Remove (i2p::data::StorageArtifact kind, std::string_view name, std::stop_token token = { });

	/** Typed outcome of the most recent Write/Remove (diagnostics and tests). */
	DataStorageStatus LastStatus () const;
	/** Generation of the configured worker; 0 before the first successful Start. */
	uint32_t Generation () const;
	/** Worker pid, or -1 when this client owns no child (diagnostics). */
	long WorkerPid () const;
	/** Injected clock so restart scheduling can be tested without sleeping. */
	void SetClockForTesting (std::function<std::chrono::steady_clock::time_point ()> clock);
	/**
	 * Move the request-id counter. Exists so the exhaustion contract can be
	 * exercised deterministically; the counter is otherwise monotonic and is
	 * never reset by a restart or a Stop/Start cycle.
	 */
	void SetRequestIdForTesting (uint32_t nextRequestId);

private:
	struct Impl;
	std::unique_ptr<Impl> m_Impl;
};

/**
 * Data-storage worker entry point. Validates the delegated root (identity,
 * owner, mode), creates the three fixed 0700 directories, applies the
 * storage sandbox of the platform and then serves artifact operations until
 * Shutdown, a protocol violation or channel EOF.
 */
int RunDataStorageWorker (int fd, int inheritedRoot = -1);
}

#endif
