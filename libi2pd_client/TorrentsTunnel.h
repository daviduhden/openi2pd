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
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <future>
#include <chrono>
#include <atomic>
#include <optional>
#include <functional>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include "HTTP.h"
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
	using TorrentPayloadStorageFactory = std::function<std::shared_ptr<TorrentPayloadStorage>
		(const std::string & tunnelName, const std::string & infoHash)>;
	void SetTorrentPayloadStorageFactory (TorrentPayloadStorageFactory factory);

	constexpr int TRACKER_RESPONSE_TIMEOUT = 8; // in seconds
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
		std::string GetTrackerAnnounce (size_t id) const
		{
			return (id < m_Trackers.size ()) ? std::get<0> (m_Trackers[id]).to_string () : "";
		}
		size_t GetNumTrackers () const { return m_Trackers.size (); }
		int AddTracker (std::string_view announce, bool isCommon); // tarckerID or -1 if not inserted
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
			std::shared_ptr<boost::beast::http::request<boost::beast::http::string_body>> req, size_t trackerID);

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
		void HandleConnectResponse (const uint8_t * buf, size_t len);
		void HandleErrorResponse (const uint8_t * buf, size_t len);
		void HandleAnnounceResponse (const uint8_t * buf, size_t len);
		void SendAnnounceToDatagramTracker (size_t trackerID, uint64_t connectionID, std::shared_ptr<Torrent> torrent,
			std::string_view dest, uint16_t port, uint16_t fromPort, TrackerAnnounceEvent event);

	private:
		std::string m_Name;
		std::filesystem::path m_TorrentsDir;
		std::vector<TrackerInfo> m_Trackers;
		std::map<Torrent::InfoHash, std::shared_ptr<Torrent>> m_Torrents;
		std::map<int, std::weak_ptr<Torrent>> m_TorrentsByID;
		mutable std::mutex m_TorrentsMutex;
		boost::asio::steady_timer m_TrackerRequestsCheckTimer, m_KeepAliveCheckTimer, m_ReconnectCheckTimer,
			m_TorrentsStatusUpdateTimer;
		DiskIOService m_DiskIOService;
		bool m_UseIsolatedStorage = false;
		std::shared_ptr<TorrentStorageWorker> m_StorageWorker;
		std::shared_ptr<TorrentStorageWorker> m_DHTStorageWorker;
		std::unordered_map<uint32_t, std::tuple<size_t, uint16_t, std::weak_ptr<Torrent>, uint64_t>>
			m_DatragramTrackerTransactions;
		// transactionID->(trackerID, from_port, torrent, timestamp monotonic milliseconds)
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
