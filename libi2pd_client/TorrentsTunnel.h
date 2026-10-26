/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TorrentsTunnel.
 *
 * Declares the torrent tunnel service that owns torrents, trackers, peer
 * connections and the optional DHT, plus its bounded control snapshot/command
 * interface and background timers. It also coordinates a separate disk I/O
 * service and enforces root-confined paths.
 *
 */

#ifndef NO_TORRENTS

#ifndef TORRENTS_TUNNEL_H__
#define TORRENTS_TUNNEL_H__

#include <string>
#include <string_view>
#include <memory>
#include <filesystem>
#include <array>
#include <vector>
#include <list>
#include <map>
#include <set>
#include <unordered_map>
#include <utility>
#include <unordered_set>
#include <mutex>
#include <future>
#include <chrono>
#include <atomic>
#include <optional>
#include <functional>
#include <algorithm>
#include <compare>
#include <cstdint>
#include <tuple>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include "HTTP.h"
#include "Datagram.h"
#include "I2PService.h"
#include "util.h"
#include "BoostStream.h"
#include "Torrents.h"
#include "TorrentsControl.h"
#include "TorrentsDHT.h"
#include "TorrentsState.h"

namespace i2p
{
namespace torrents
{
	// Administrative registration before service startup. A selected but unavailable
	// backend must return an erroring capability, never nullptr/native fallback.
	using TorrentPayloadStorageFactory = std::function<std::shared_ptr<TorrentPayloadStorage> (
		const std::string & tunnelName, const std::string & infoHash)>;
	void SetTorrentPayloadStorageFactory (TorrentPayloadStorageFactory factory);

	constexpr int TRACKER_RESPONSE_TIMEOUT = 8; // in seconds
	// Hard cap on announces that may be airborne at the same time across every
	// torrent of this tunnel. A hostile or silent tracker must never accumulate
	// parallel requests from the retry ladder.
	constexpr size_t MAX_INFLIGHT_TRACKER_REQUESTS = 16;
	constexpr uint16_t DATAGRAM_TRACKER_DEFAULT_PORT = 6969; // BEP 15
	constexpr int DATAGRAM_TRACKER_TRANSACTION_TIMEOUT = 10000; // in milliseconds
	constexpr int DATAGRAM_TRACKER_CONNECTION_EXPIRATION = 60000; // in milliseconds
	constexpr int TRACKER_REQUESTS_CHECK_TIMEOUT = 1900; // in milliseconds
	constexpr int RECONNECT_INTERVAL = 45; // in seconds
	constexpr int RECONNECT_INTERVAL_VARIANCE = 30; // in seconds
	constexpr int RECONNECT_CHECK_INTERVAL = 30; // in seconds
	constexpr int TRACKER_REQUESTS_INTERVAL_VARIANCE = 3000; // in milliseconds
	constexpr int TRACKER_INITIAL_REQUEST_INTERVAL_VARIANCE = 31000; // in milliseconds
	constexpr int PEER_KEEP_ALIVE_CHECK_INTERVAL = 15; // in seconds
	constexpr int TORRENTS_STATUS_UPDATE_INTERVAL = 9; // in seconds
	constexpr int TORRENTS_STATUS_UPDATE_INTERVAL_VARIANCE = 7; // in seconds
	constexpr int TORRENTS_STATUS_UPDATE_CHECK_INTERVAL = 8; // in seconds
	constexpr int TRACKER_MAX_NUM_WANT = 25;
	// Upper bound for marshalling the shutdown sequence to the tunnel service
	// thread. A timed-out teardown is left on the service thread and logged;
	// the object is kept alive by the posted closure, so it is never destroyed
	// while that closure is pending.
	constexpr std::chrono::milliseconds TORRENTS_STOP_TIMEOUT { 5000 };

	enum DatagramTrackerAction
	{
		eDatagramTrackerActionConnect = 0,
		eDatagramTrackerActionAnnounce = 1,
		eDatagramTrackerActionError = 3
	};

	enum TrackerAnnounceEvent
	{
		eTrackerAnnounceEventNone = 0,
		eTrackerAnnounceEventCompleted = 1,
		eTrackerAnnounceEventStarted = 2,
		eTrackerAnnounceEventStopped = 3,
		eTrackerNumAnnounceEvents
	};

	constexpr std::array<std::string_view, eTrackerNumAnnounceEvents> TrackerAnnounceEventStr { "", "completed",
		"started", "stopped" };

	// Identity of one outstanding tracker announce. Torrent-side tracker
	// statistics are keyed by registry slot, so (torrent, slot) is the
	// single-flight key: two torrents may legitimately announce to one slot at
	// the same time, but one torrent must not have two announces in flight for
	// the same slot.
	struct TrackerRequestKey
	{
		Torrent::InfoHash infoHash { };
		size_t trackerID = 0;
		auto operator<=> (const TrackerRequestKey &) const = default;
	};

	// Bounded, service-thread-only single-flight admission for tracker
	// announces. The payload is opaque, so the policy can be exercised without
	// a service, a socket or a timer. `TryAcquire` never evicts an admitted
	// request: a duplicate is refused and the cap is enforced exactly.
	template <typename Payload> class TrackerInflightRegistry
	{
	public:
		enum class Admission
		{
			Accepted = 0,
			Duplicate,
			Full
		};

		Admission TryAcquire (const TrackerRequestKey & key, Payload payload = { })
		{
			if (m_Entries.contains (key)) return Admission::Duplicate;
			if (m_Entries.size () >= MAX_INFLIGHT_TRACKER_REQUESTS) return Admission::Full;
			m_Entries.emplace (key, std::move (payload));
			return Admission::Accepted;
		}
		bool Release (const TrackerRequestKey & key) { return m_Entries.erase (key) > 0; }
		bool Contains (const TrackerRequestKey & key) const { return m_Entries.contains (key); }
		Payload * Find (const TrackerRequestKey & key)
		{
			auto it = m_Entries.find (key);
			return it == m_Entries.end () ? nullptr : &it->second;
		}
		// Drops every outstanding announce of one torrent and returns the
		// removed keys so the caller can retire its own per-request state. The
		// map is ordered by (info hash, slot), so all keys of one torrent are
		// contiguous.
		std::vector<TrackerRequestKey> ReleaseTorrent (const Torrent::InfoHash & infoHash)
		{
			std::vector<TrackerRequestKey> released;
			for (auto it = m_Entries.lower_bound (TrackerRequestKey { infoHash, 0 });
				it != m_Entries.end () && it->first.infoHash == infoHash; it = m_Entries.erase (it))
				released.push_back (it->first);
			return released;
		}
		std::vector<TrackerRequestKey> ReleaseAll ()
		{
			std::vector<TrackerRequestKey> released;
			released.reserve (m_Entries.size ());
			for (const auto & entry : m_Entries) released.push_back (entry.first);
			m_Entries.clear ();
			return released;
		}
		// Read-only traversal used to retire per-request state (a stream handle)
		// before the entries are dropped. The visitor must not insert or erase.
		template <typename Visitor> void ForEachTorrent (const Torrent::InfoHash & infoHash, Visitor visitor)
		{
			for (auto it = m_Entries.lower_bound (TrackerRequestKey { infoHash, 0 });
				it != m_Entries.end () && it->first.infoHash == infoHash; ++it)
				visitor (it->first, it->second);
		}
		template <typename Visitor> void ForEach (Visitor visitor)
		{
			for (auto & entry : m_Entries) visitor (entry.first, entry.second);
		}
		size_t Size () const { return m_Entries.size (); }

	private:
		std::map<TrackerRequestKey, Payload> m_Entries;
	};

	// Registry slot state, parallel to the announce URL table. Torrent-side
	// tracker statistics are index-keyed, so a slot keeps its index for the
	// lifetime of the tunnel process. A slot is reusable only when no torrent
	// references it and it was not configured as a common tracker.
	//
	// Reuse does not reset Torrent::TrackerStats for that index: the next
	// response overwrites them, so a snapshot taken in between can still show
	// the previous announce's last result for the new URL. Clearing them would
	// need a per-slot reset on the Torrent side, which this registry deliberately
	// does not reach into.
	struct TrackerSlotState
	{
		uint32_t references = 0;
		bool isCommon = false;
	};
	using TrackerSlots = std::vector<TrackerSlotState>;

	// A common tracker slot (or one still referenced by a torrent) is live and
	// may be matched; a released, non-common slot is free and must be ignored by
	// every membership, snapshot and announce path.
	inline bool IsTrackerSlotLive (const TrackerSlots & slots, size_t trackerID)
	{
		return trackerID < slots.size () && (slots[trackerID].isCommon || slots[trackerID].references > 0);
	}

	// Lowest slot a *new* announce URL may take. Returns nullopt when every slot
	// is live, so the caller must append or refuse at MAX_NUM_TRACKERS.
	inline std::optional<size_t> FindReusableTrackerSlot (const TrackerSlots & slots)
	{
		for (size_t i = 0; i < slots.size (); i++)
			if (!slots[i].isCommon && !slots[i].references) return i;
		return std::nullopt;
	}

	// Pure admission decision for one announce URL. `existing` is the slot that
	// already holds the same URL (the caller compares normalized URLs because it
	// owns URL parsing); `trackerID` is the slot to use or nullopt when the table
	// is full. `existing` distinguishes a plain reference increment from a slot
	// that must be initialised first.
	struct TrackerSlotAdmission
	{
		std::optional<size_t> trackerID;
		bool existing = false;
	};

	inline TrackerSlotAdmission AdmitTrackerSlot (const TrackerSlots & slots, std::optional<size_t> existing)
	{
		// A live registration is never duplicated and never evicted.
		if (existing) return { *existing, true };
		// Reuse the lowest free slot so the slot count stays as small as
		// possible without ever renumbering a live entry.
		if (const auto reusable = FindReusableTrackerSlot (slots)) return { reusable, false };
		if (slots.size () < MAX_NUM_TRACKERS) return { slots.size (), false };
		return { std::nullopt, false };
	}

	inline void AcquireTrackerSlotReference (TrackerSlots & slots, size_t trackerID)
	{
		if (trackerID < slots.size ()) slots[trackerID].references++;
	}

	enum class TrackerSlotRelease
	{
		Invalid = 0, // no such slot, or its count is already zero
		Retained, // another holder (or the common flag) keeps the slot live
		Freed // the caller must clear the slot's announce URL
	};

	inline TrackerSlotRelease ReleaseTrackerSlotReference (TrackerSlots & slots, size_t trackerID)
	{
		if (trackerID >= slots.size ()) return TrackerSlotRelease::Invalid;
		auto & slot = slots[trackerID];
		if (!slot.references) return TrackerSlotRelease::Invalid;
		--slot.references;
		return (!slot.isCommon && !slot.references) ? TrackerSlotRelease::Freed : TrackerSlotRelease::Retained;
	}

	// Next announce time in monotonic milliseconds for one (torrent, tracker).
	//
	// Dispatching a request is not a success: while `failurePending` is set the
	// bounded failure ladder (written by Torrent::ReportTrackerFailure) owns the
	// next attempt and must never be overwritten by the tracker-advertised
	// interval, which is floored at MIN_TRACKER_ANNOUNCE_INTERVAL (15 minutes).
	// `scheduledFailureTime` is the retry time Torrent already holds; it is
	// honoured while it is still ahead of `now` and the ladder floor applies
	// otherwise. Jitter is bounded by the failure cap, so no result exceeds
	// MAX_TRACKER_FAILURE_BACKOFF (or a larger advertised minimum).
	inline uint64_t NextTrackerDispatchTime (uint64_t now, bool failurePending, uint64_t scheduledFailureTime,
		int advertisedInterval, int minInterval, uint32_t jitter)
	{
		if (failurePending)
		{
			const int64_t floorMs = TrackerSchedule::FailureInterval (0, minInterval);
			const int64_t capMs = std::max<int64_t> (MAX_TRACKER_FAILURE_BACKOFF * 1000LL, minInterval);
			const int64_t remaining = scheduledFailureTime > now
				? static_cast<int64_t> (std::min<uint64_t> (scheduledFailureTime - now, static_cast<uint64_t> (capMs)))
				: floorMs;
			const int64_t wait = std::clamp<int64_t> (remaining + static_cast<int64_t> (jitter), floorMs, capMs);
			return now + static_cast<uint64_t> (wait);
		}
		const int64_t interval = std::max<int64_t> (advertisedInterval, MIN_TRACKER_ANNOUNCE_INTERVAL * 1000LL);
		return now + static_cast<uint64_t> (interval) + jitter;
	}

	class TorrentsTunnel final : public i2p::client::I2PService
	{
	private:
		class DiskIOService : private i2p::util::RunnableServiceWithWork
		{
		public:
			DiskIOService ()
				: RunnableServiceWithWork ("TDiskIO")
			{
			}
			auto & GetService () { return GetIOService (); }
			void Start () { StartIOService (); }
			void Stop () { StopWorkAndFinishTasks (); }
		};

		using TrackerInfo = std::tuple<i2p::http::URL, bool, uint64_t, uint64_t, uint16_t>;
		// (announce url, common, connection_id, connection expiration time in monotonic milliseconds, connection from_port)

	public:
		TorrentsTunnel (std::string_view name, std::shared_ptr<i2p::client::ClientDestination> localDestination,
			std::string_view torrentsDir, std::string_view trackers = "", bool dht = true,
			std::string_view dhtFileName = "", bool isolatedStorage = false);
		~TorrentsTunnel () override;

		void Start () override;
		// Marshals the service-thread teardown with a bounded wait. When it
		// times out the posted closure owns a reference and completes the
		// teardown on the service thread; Stop() never mutates DHT, timers,
		// accepted streams or torrent state from the caller's thread.
		void Stop () override;
		auto & GetDiskIOService () { return m_DiskIOService.GetService (); };

		// Control snapshots and commands are executed on this tunnel's Asio
		// service thread. Callers on other threads wait for at most timeout.
		std::optional<TorrentsSnapshot> GetControlSnapshot (std::chrono::milliseconds timeout);
		TorrentCommandResult ExecuteControlCommand (const TorrentCommand & command, std::chrono::milliseconds timeout);

		const std::filesystem::path & GetTorrentsDir () const { return m_TorrentsDir; }
		// A released, non-common slot reports an empty announce so snapshots and
		// RPC listings never expose a stale URL for a reusable index. The registry
		// mutex is required because the RPC thread reads these slots while the
		// service thread creates, recycles and releases them.
		std::string GetTrackerAnnounce (size_t id) const
		{
			std::lock_guard<std::mutex> lock (m_TrackerRegistryMutex);
			return (IsTrackerSlotLive (m_TrackerSlots, id)) ? std::get<0> (m_Trackers[id]).to_string () : "";
		}
		// Registry slots are never renumbered, so this is the high-water mark and
		// may include free slots; callers must skip ids without an announce.
		size_t GetNumTrackers () const
		{
			std::lock_guard<std::mutex> lock (m_TrackerRegistryMutex);
			return m_Trackers.size ();
		}
		// Resolves/creates the slot and increments its reference count; returns
		// the tracker id or -1 if not inserted.
		int AddTracker (std::string_view announce, bool isCommon);
		std::shared_ptr<Torrent> FindTorrent (const Torrent::InfoHash & infoHash) const;
		std::shared_ptr<Torrent> FindTorrentByID (int id) const;
		std::vector<int> GetTorrentIDs () const;
		std::list<std::shared_ptr<Torrent>> GetTorrents () const;
		std::pair<std::shared_ptr<Torrent>, int> AddTorrent (
			std::string_view torrentFileContent); // retrun (torrent, id)
		std::pair<std::shared_ptr<Torrent>, int> AddMagnet (std::string_view magnet); // return (torrent, id)
		// RPC entry points marshal to the tunnel service thread; they never
		// touch m_Torrents from an HTTP I/O thread. Return {nullptr, 0} on timeout.
		std::pair<std::shared_ptr<Torrent>, int> AddTorrentFromRpc (std::string_view torrentFileContent);
		std::pair<std::shared_ptr<Torrent>, int> AddMagnetFromRpc (std::string_view magnet);
		void UpdateTorrentInfo (std::shared_ptr<Torrent> torrent, std::string_view info); // magnet
		bool RemoveTorrent (int id, bool deleteFiles);
		void RemoveAllTorrents (bool deleteFiles);
		bool StopTorrent (int id);
		void StopAllTorrents ();
		bool StartTorrent (int id);
		void StartAllTorrents ();
		bool ReannounceTorrent (int id);
		void ReannounceAllTorrents ();
		void ConnectToNewPeers (std::shared_ptr<Torrent> torrent, std::unordered_set<i2p::data::IdentHash> & newPeers,
			PeerConnectionOrigin origin);
		// Dial candidates admitted by PEX/DHT. Service-thread only: the table
		// owns dedupe/expiry/backoff, so a hostile peer cannot restart a dial
		// storm by re-advertising the same destinations.
		void DrainPeerCandidates (std::shared_ptr<Torrent> torrent, uint64_t now);
		// Network-thread entry point: copy a bounded raw datagram and post it to
		// the service thread. The service variant is the only one that mutates
		// DHT/tracker state.
		void HandleRecvFromI2PRawOnService (
			uint8_t protocol, uint16_t fromPort, uint16_t toPort, const uint8_t * buf, size_t len);
		void SendDHTPingQuery (const i2p::data::IdentHash & toIdent, uint16_t toPort);
		std::pair<uint16_t, uint16_t> GetDHTPorts () const
		{
			return { m_DHT ? m_DHT->GetPort () : 0, m_DHT ? m_DHT->GetRPort () : 0 };
		}
		bool SupportsDHT () const { return (bool)m_DHT; }
		const std::unique_ptr<TorrentsDHT> & GetDHT () const { return m_DHT; }

		const char * GetName () const override { return m_Name.c_str (); }

	private:
		void StopOnService ();
		std::optional<TorrentsSnapshot> BuildControlSnapshot () const;
		TorrentCommandResult ExecuteControlCommandOnService (const TorrentCommand & command);
		TorrentCommandResult QueueTorrentCreation (const TorrentCommand & command);
		void LoadTorrentAdminState (const std::shared_ptr<Torrent> & torrent);
		void SaveTorrentAdminState (const std::shared_ptr<Torrent> & torrent, bool final = false);

		// True when path is lexically inside m_TorrentsDir and neither the
		// root nor any component in between is a symlink. Existing leaves
		// must be regular files, or directories when allowDirectoryLeaf is
		// set. Missing components are allowed for files that will be
		// created, but are rechecked before use.
		bool IsContainedPath (const std::filesystem::path & path, bool allowDirectoryLeaf) const;
		// Set the download root from the torrent's sanitized name only after
		// proving that it remains under m_TorrentsDir. This rejects a
		// malicious/legacy name before it can enter the session map.
		bool SetTorrentPathWithinRoot (const std::shared_ptr<Torrent> & torrent) const;
		std::string GetDisplayFilePath (const std::filesystem::path & path) const;

		void Accept ();
		// Enumerate and read the configured torrent directory on the storage
		// service. Parsing/insertion is posted back to the network service;
		// the network thread never walks the directory or opens a file.
		void LoadInitialTorrents ();
		bool ReadTorrentFileForControl (
			const std::filesystem::path & path, std::vector<uint8_t> & content, std::string & error);
		void SaveTorrentFile (std::shared_ptr<Torrent> torrent, const std::string & content);
		void InitTorrentFiles (std::shared_ptr<Torrent> torrent);
		std::pair<int, bool> InsertTorrent (
			std::shared_ptr<Torrent> torrent); // returns id > 0 if success and 0 if failed, true if innserted
		void RemoveTorrent (std::shared_ptr<Torrent> torrent, bool deleteFiles);
		int FindTorrentID (std::shared_ptr<Torrent> torrent) const; // 0 if not found
		bool CreateAndReserveFile (const std::filesystem::path & filePath, size_t reserve);
		void CompleteTorrent (std::shared_ptr<Torrent> torrent);
		void StopTorrent (std::shared_ptr<Torrent> torrent);
		bool IsTorrentTracker (size_t trackerID, const Torrent & torrent) const;
		bool RequestTracker (size_t trackerID, std::shared_ptr<Torrent> torrent, TrackerAnnounceEvent event);
		void RequestTorrentTrackers (std::shared_ptr<Torrent> torrent, TrackerAnnounceEvent event);
		void TrackerRequestSent (const boost::beast::error_code & ecode, size_t bytes_transferred,
			std::shared_ptr<i2p::client::BoostAsyncStream> httpStream, std::shared_ptr<Torrent> torrent,
			std::shared_ptr<boost::beast::http::request<boost::beast::http::string_body>> req,
			const TrackerRequestKey & key);

		// Tracker registry and single-flight bookkeeping. Every helper below is
		// service-thread only; the announce URL table, the slot table, the
		// membership map and the in-flight registry move together. The first
		// three also take m_TrackerRegistryMutex because the RPC-facing getters
		// read the same slot table from another thread.
		// Validates an announce URL and resolves it to its slot. `create` may
		// admit the URL into the lowest free slot (or append) without changing
		// any reference count; returned indices are stable for the process.
		std::optional<size_t> ResolveTrackerSlot (std::string_view announce, bool isCommon, bool create);
		void AcquireTrackerSlot (size_t trackerID);
		// Decrements one membership. A non-common slot whose count reaches zero
		// becomes free (its announce is cleared) and may be reused later.
		bool ReleaseTrackerSlot (size_t trackerID);
		// Releases every slot this torrent's membership holds, exactly once.
		void ReleaseTorrentTrackers (const Torrent::InfoHash & infoHash);
		// Recomputes the membership of one torrent from its current announce and
		// tier list. Retained slots keep their index, so torrent-side tracker
		// statistics stay bound to the same URL; dropped slots are released.
		void RegisterTorrentTrackers (const Torrent & torrent);

		// Admit one HTTP announce. Returns false when the same (torrent, slot)
		// already has a request in flight or the tunnel cap is reached.
		bool AcquireTrackerRequest (const TrackerRequestKey & key, const std::shared_ptr<Torrent> & torrent);
		void ReleaseTrackerRequest (const TrackerRequestKey & key);
		void CancelTrackerRequests (const Torrent::InfoHash & infoHash);
		void CancelAllTrackerRequests ();
		void ScheduleTrackerRequestDeadline (const TrackerRequestKey & key);
		void HandleTrackerRequestTimeout (const TrackerRequestKey & key);

		void ScheduleTrackerRequestsCheck ();
		void HandleTrackerRequestsCheckTimer (const boost::system::error_code & ecode);

		void ScheduleKeepAliveCheck ();
		void HandleKeepAliveCheckTimer (const boost::system::error_code & ecode);

		void ScheduleReconnectCheck ();
		void HandleReconnectCheckTimer (const boost::system::error_code & ecode);

		void ScheduleStatusUpdate ();
		void HandleTorrentsStatusUpdateTimer (const boost::system::error_code & ecode);

		void ConnectToPeer (
			std::shared_ptr<Torrent> torrent, const i2p::data::IdentHash & peer, PeerConnectionOrigin origin);
		size_t ConnectToPeers (std::shared_ptr<Torrent> torrent);
		size_t ConnectToPeers (std::shared_ptr<Torrent> torrent, size_t trackerID);
		void UpdatePeersPerPiece (std::shared_ptr<Torrent> torrent);

		// Peer accounting on the service thread. CountPeerHandlers() covers
		// every PeerConnection, including inbound streams that have not
		// completed a handshake yet, so the global bound cannot be bypassed by
		// opening streams without announcing a torrent.
		size_t CountPeerHandlers ();
		bool CanDial (Torrent & torrent);
		void FinishDial (uint64_t dial);
		void ExpireDials (uint64_t now);
		// One piece per storage-service job, chained through the service
		// thread so tracker, control and peer work keeps running in between.
		void ScheduleLocalVerification (const std::shared_ptr<Torrent> & torrent);
		void VerifyNextLocalPiece (const std::shared_ptr<Torrent> & torrent);

		void HandleRecvFromI2PRaw (
			uint8_t protocol, uint16_t fromPort, uint16_t toPort, const uint8_t * buf, size_t len);
		void ConnectToDatagramTracker (size_t trackerID, std::string_view dest, uint16_t port);
		void HandleConnectResponse (const uint8_t * buf, size_t len, uint16_t fromPort, uint16_t toPort);
		void HandleErrorResponse (const uint8_t * buf, size_t len, uint16_t fromPort, uint16_t toPort);
		void HandleAnnounceResponse (const uint8_t * buf, size_t len, uint16_t fromPort, uint16_t toPort);
		void SendAnnounceToDatagramTracker (size_t trackerID, uint64_t connectionID, std::shared_ptr<Torrent> torrent,
			std::string_view dest, uint16_t port, uint16_t fromPort, TrackerAnnounceEvent event);

	private:
		std::string m_Name;
		std::filesystem::path m_TorrentsDir;
		std::vector<TrackerInfo> m_Trackers;
		// Slot state parallel to m_Trackers; both vectors always have the same
		// size and index i in one describes index i in the other. The mutex
		// serializes the public RPC-facing getters against service-thread slot
		// creation, recycling and release; it is never held across a stream,
		// timer or torrent operation.
		mutable std::mutex m_TrackerRegistryMutex;
		TrackerSlots m_TrackerSlots;
		// Registry slots referenced by one torrent membership. Indexed by info
		// hash so removal releases exactly one reference per membership even when
		// the torrent's tier list or instance is already gone.
		std::map<Torrent::InfoHash, std::vector<size_t>> m_TrackerMemberships;
		std::map<Torrent::InfoHash, std::shared_ptr<Torrent>> m_Torrents;
		std::map<int, std::weak_ptr<Torrent>> m_TorrentsByID;
		mutable std::mutex m_TorrentsMutex;
		// Protected by m_TorrentsMutex; re-add is rejected until serialized
		// cleanup succeeds. Failures retain the reservation until restart.
		std::set<Torrent::InfoHash> m_RemovingTorrents;
		boost::asio::steady_timer m_TrackerRequestsCheckTimer, m_KeepAliveCheckTimer, m_ReconnectCheckTimer,
			m_TorrentsStatusUpdateTimer;
		DiskIOService m_DiskIOService;
		bool m_UseIsolatedStorage = false;
		std::shared_ptr<TorrentStorageWorker> m_StorageWorker;
		std::shared_ptr<TorrentStorageWorker> m_DHTStorageWorker;
		i2p::datagram::DatagramDestination::ReceiverRegistration m_RawDatagramReceiver;
		std::unordered_map<uint32_t, std::tuple<size_t, uint16_t, std::weak_ptr<Torrent>, uint64_t>>
			m_DatragramTrackerTransactions;
		// transactionID->(trackerID, from_port, torrent, timestamp monotonic milliseconds)
		// Per-request state of an HTTP announce that is still airborne. The
		// deadline timer is owned here; the stream is weak because the tunnel
		// service thread owns it until the write/read completes.
		struct TrackerRequestState
		{
			std::shared_ptr<boost::asio::steady_timer> deadline;
			std::weak_ptr<i2p::stream::Stream> stream;
			std::weak_ptr<Torrent> torrent;
			uint64_t started = 0; // monotonic milliseconds
		};
		TrackerInflightRegistry<TrackerRequestState> m_TrackerRequests;
		std::unique_ptr<TorrentsDHT> m_DHT;
		std::atomic<bool> m_Stopping { false };
		// Outbound dials whose stream is still being established. Each entry
		// expires so a lost completion callback cannot leak a slot, and the
		// map is bounded by MAX_TOTAL_DIALS before a dial starts.
		struct PendingDial
		{
			std::weak_ptr<Torrent> torrent;
			uint64_t started = 0; // monotonic seconds
		};
		std::unordered_map<uint64_t, PendingDial> m_PendingDials;
		uint64_t m_NextDialID = 1;
		struct AdminStateStatus
		{
			bool loaded = false;
			bool pending = false;
			bool dirty = false;
			uint64_t generation = 0;
			uint64_t commentRevision = 0;
			std::string error;
		};
		std::map<Torrent::InfoHash, AdminStateStatus> m_AdminState;

		struct CreationProgress
		{
			std::atomic<bool> cancelled { false };
			std::atomic<uint64_t> bytesDone { 0 }, totalBytes { 0 }, filesDone { 0 }, totalFiles { 0 };
		};
		std::shared_ptr<CreationProgress> m_CreationProgress;

		TorrentCreationInfo m_Creation;
	};

}
}

#endif

#endif // NO_TORRENTS
