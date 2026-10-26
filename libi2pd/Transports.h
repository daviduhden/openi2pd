/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Transports.
 *
 * Declares the transport manager, which selects peers, opens NTCP2/SSU2
 * sessions, queues and retries messages, samples bandwidth and maintains the IP
 * ban list, peer tests and traffic statistics. Also provides a bounded
 * pre-generated X25519 key pool and immutable peer snapshots for admin views.
 *
 */

#ifndef TRANSPORTS_H__
#define TRANSPORTS_H__

#include <thread>
#include <stop_token>
#include <mutex>
#include <future>
#include <condition_variable>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <list>
#include <string>
#include <string_view>
#include <map>
#include <memory>
#include <atomic>
#include <random>
#include <utility>
#include <boost/asio.hpp>
#include "TransportSession.h"
#include "SSU2.h"
#include "NTCP2.h"
#include "RouterInfo.h"
#include "I2NPProtocol.h"
#include "Identity.h"
#include "IdentMetrics.h"
#include "util.h"

namespace i2p
{
namespace transport
{
	/// Bounded pool of pre-generated X25519 handshake key pairs.
	///
	/// Key generation is the slowest step of an outbound handshake, so a
	/// background jthread keeps a small queue ready and `Acquire()` only
	/// generates on the calling thread when the queue ran dry. A pair whose
	/// private key never left the process is recycled through `Return()`. A
	/// provider failure is retried a few times and then returned keyless, so
	/// callers must check `HasPrivateKey()` and fail that handshake closed
	/// instead of publishing an all-zero key.
	class X25519KeysPairSupplier
	{
	public:
		X25519KeysPairSupplier (int size);
		~X25519KeysPairSupplier ();
		void Start ();
		void Stop ();
		std::shared_ptr<i2p::crypto::X25519Keys> Acquire ();
		void Return (std::shared_ptr<i2p::crypto::X25519Keys> pair);

	private:
		void Run (std::stop_token token);
		size_t CreateEphemeralKeys (int num); // return new queue size

	private:
		const int m_QueueSize;
		i2p::util::MemoryPoolMt<i2p::crypto::X25519Keys> m_KeysPool;
		std::list<std::shared_ptr<i2p::crypto::X25519Keys>> m_Queue;

		std::jthread m_Thread;
		std::condition_variable_any m_Acquired;
		std::mutex m_AcquiredMutex;
	};

	const int PEER_ROUTER_INFO_UPDATE_INTERVAL = 31 * 60; // in seconds
	const int PEER_ROUTER_INFO_UPDATE_INTERVAL_VARIANCE = 7 * 60; // in seconds
	const size_t PEER_ROUTER_INFO_OVERLOAD_QUEUE_SIZE = 25;
	const int PEER_SELECTION_MIN_INTERVAL = 20; // in seconds
	/// One router in the transport peer table.
	///
	/// `router` is the last known RouterInfo, `sessions` the live transport
	/// sessions for it, and `delayedMessages` messages waiting for a session.
	/// `priority` is recomputed by `Transports::SetPriority()` from the mutually
	/// supported transports (a random SSU2/NTCP2 preference decides the order)
	/// and is the list `ConnectToPeer()` walks. `isEligible` means the peer is
	/// reachable, accepts tunnels, is not congested, is ECIES-capable and new
	/// enough; `isDone` marks a discarded entry and makes `Done()` safe to call
	/// once, dropping sessions and undeliverable delayed messages.
	struct Peer
	{
		std::atomic<int> numAttempts;
		std::shared_ptr<const i2p::data::RouterInfo> router;
		std::list<std::shared_ptr<TransportSession>> sessions;
		uint64_t creationTime, nextRouterInfoUpdateTime, lastSelectionTime;
		std::list<std::shared_ptr<i2p::I2NPMessage>> delayedMessages;
		std::vector<i2p::data::RouterInfo::SupportedTransports> priority;
		std::atomic<bool> isHighBandwidth, isEligible, isDone;

		Peer (std::shared_ptr<const i2p::data::RouterInfo> r, uint64_t ts)
			: numAttempts (0)
			, router (r)
			, creationTime (ts)
			, nextRouterInfoUpdateTime (ts + PEER_ROUTER_INFO_UPDATE_INTERVAL)
			, lastSelectionTime (0)
			, isHighBandwidth (false)
			, isEligible (false)
			, isDone (false)
		{
			UpdateParams (router);
		}

		void Done ()
		{
			isDone = true;
			if (!sessions.empty ())
			{
				for (auto & it : sessions) it->Done ();
				decltype (sessions) tmp;
				sessions.swap (tmp);
			}
			// drop not sent delayed messages
			if (!delayedMessages.empty ())
			{
				for (auto & it : delayedMessages) it->Drop ();
				decltype (delayedMessages) tmp;
				delayedMessages.swap (tmp);
			}
		}

		void SetRouter (std::shared_ptr<const i2p::data::RouterInfo> r)
		{
			router = r;
			UpdateParams (router);
		}

		bool IsConnected () const { return !sessions.empty (); }
		void UpdateParams (std::shared_ptr<const i2p::data::RouterInfo> router);
	};

	const uint64_t SESSION_CREATION_TIMEOUT = 15; // in seconds
	/**
	 * Immutable copy of the transport peer table for administrative views.
	 * It intentionally contains no RouterInfo pointers or session objects:
	 * the caller cannot retain router state or race with peer cleanup.
	 */
	struct PeerSnapshot
	{
		i2p::data::IdentHash ident;
		int numAttempts;
		size_t numSessions;
		uint8_t transports; // bitmask of active TransportSession transports
		bool isHighBandwidth;
	};

	const int PEER_TEST_INTERVAL = 68 * 60; // in seconds
	const int PEER_TEST_INTERVAL_VARIANCE = 3 * 60; // in seconds
	const int PEER_TEST_DELAY_INTERVAL = 20; // in milliseconds
	const int PEER_TEST_DELAY_INTERVAL_VARIANCE = 30; // in milliseconds
	const int MAX_NUM_DELAYED_MESSAGES = 150;
	const int CHECK_PROFILE_NUM_DELAYED_MESSAGES = 15; // check profile after
	const int NUM_X25519_PRE_GENERATED_KEYS = 25; // pre-generated x25519 keys pairs
	const int MAX_NUM_CONNECTIONS_FROM_SUBNET_FOR_PEER = 3; // for first hop selection

	const int IP_BAN_TIME = 1800; // in seconds
	const int IP_BAN_TIME_VARIANCE = 540; // in seconds
	const int BAN_LIST_CLEANUP_INTERVAL = 66; // in seconds
	const int BAN_LIST_CLEANUP_INTERVAL_VARIANCE = 20; // in seconds

	const int TRAFFIC_SAMPLE_COUNT = 301; // seconds
	struct TrafficSample
	{
		uint64_t Timestamp;
		uint64_t TotalReceivedBytes;
		uint64_t TotalSentBytes;
		uint64_t TotalTransitTransmittedBytes;
	};

	/// Transport manager: owns the NTCP2 and SSU2 servers, the peer table, the
	/// ban list and the traffic samples, and runs one service thread.
	///
	/// `SendMessage()`/`SendMessages()` post onto that service and complete once
	/// the messages were handed to a session (or the attempt gave up); the
	/// returned future is the only completion signal, and it is invalid when the
	/// transport is offline. The peer map is guarded by `m_PeersMutex`, the
	/// connected-subnet accounting by `m_ConnectedNetworksMutex`, and the
	/// administrative views publish copies so a caller never holds transport
	/// locks while rendering.
	class Transports
	{
	public:
		Transports ();
		~Transports ();

		void Start (bool enableNTCP2 = true, bool enableSSU2 = true);
		void Stop ();
		bool IsRunning () const { return m_IsRunning.load (std::memory_order_acquire); }

		bool IsBoundSSU2 () const { return m_HasSSU2.load (std::memory_order_acquire); }
		bool IsBoundNTCP2 () const { return m_HasNTCP2.load (std::memory_order_acquire); }

		bool IsOnline () const { return m_IsOnline.load (std::memory_order_acquire); };
		void SetOnline (bool online);

		int GetLocalDelay () const; // in milliseconds

		auto & GetService () { return *m_Service; };
		std::shared_ptr<i2p::crypto::X25519Keys> GetNextX25519KeysPair ();
		void ReuseX25519KeysPair (std::shared_ptr<i2p::crypto::X25519Keys> pair);

		std::future<std::shared_ptr<TransportSession>> SendMessage (
			const i2p::data::IdentHash & ident, std::shared_ptr<i2p::I2NPMessage> msg);
		std::future<std::shared_ptr<TransportSession>> SendMessages (
			const i2p::data::IdentHash & ident, std::list<std::shared_ptr<i2p::I2NPMessage>> && msgs);

		void PeerConnected (std::weak_ptr<TransportSession> session);
		void PeerDisconnected (std::weak_ptr<TransportSession> session);
		bool IsConnected (const i2p::data::IdentHash & ident) const;
		void UpdatePeerParams (std::shared_ptr<const i2p::data::RouterInfo> r);

		void UpdateSentBytes (uint64_t numBytes) { m_TotalSentBytes += numBytes; };
		void UpdateReceivedBytes (uint64_t numBytes) { m_TotalReceivedBytes += numBytes; };
		uint64_t GetTotalSentBytes () const { return m_TotalSentBytes; };
		uint64_t GetTotalReceivedBytes () const { return m_TotalReceivedBytes; };
		uint64_t GetTotalTransitTransmittedBytes () const { return m_TotalTransitTransmittedBytes; }
		void UpdateTotalTransitTransmittedBytes (uint32_t add) { m_TotalTransitTransmittedBytes += add; };
		uint32_t GetInBandwidth () const { return m_InBandwidth; };
		uint32_t GetOutBandwidth () const { return m_OutBandwidth; };
		uint32_t GetTransitBandwidth () const { return m_TransitBandwidth; };
		uint32_t GetInBandwidth15s () const { return m_InBandwidth15s; };
		uint32_t GetOutBandwidth15s () const { return m_OutBandwidth15s; };
		uint32_t GetTransitBandwidth15s () const { return m_TransitBandwidth15s; };
		int GetCongestionLevel (bool longTerm) const;
		size_t GetNumPeers () const;
		size_t GetNumConnectedPeers () const;
		std::vector<PeerSnapshot> GetPeersSnapshot (size_t max) const;
		/** bounded snapshot containing only peers with at least one live session */
		std::vector<PeerSnapshot> GetConnectedPeersSnapshot (size_t max) const;
		/// Pick one connected peer that passes every session check, or null.
		/// Candidates must have a live session that is not overloaded, slow or
		/// over its bandwidth allowance; @a peerOrdering is an optional first-hop
		/// policy consulted per candidate, and null accepts any eligible peer.
		/// @param isHighBandwidth require a high-bandwidth peer
		/// @param peerOrdering optional first-hop policy, may be null
		/// @return RouterInfo resolved from netDb, or null when nothing qualified
		std::shared_ptr<const i2p::data::RouterInfo> GetRandomPeer (
			bool isHighBandwidth, i2p::data::PeerOrdering * peerOrdering = nullptr) const;

		/** get a trusted first hop for restricted routes */
		std::shared_ptr<const i2p::data::RouterInfo> GetRestrictedPeer ();
		/** do we want to use restricted routes? */
		bool RoutesRestricted () const;
		/** restrict routes to use only these router families for first hops */
		void RestrictRoutesToFamilies (const std::vector<std::string_view> & families);
		/** restrict routes to use only these routers for first hops */
		void RestrictRoutesToRouters (const std::vector<i2p::data::IdentHash> & routers);

		bool IsTrustedRouter (const i2p::data::IdentHash & ih) const;
		bool IsRestrictedPeer (const i2p::data::IdentHash & ih) const;

		void PeerTest (bool ipv4 = true, bool ipv6 = true);

		void SetCheckReserved (bool check) { m_CheckReserved = check; };
		bool IsCheckReserved () const { return m_CheckReserved; };
		bool IsInReservedRange (const boost::asio::ip::address & host) const;

		bool IsBanned (const boost::asio::ip::address & addr);
		bool AddBan (const boost::asio::ip::address & addr);

		bool IsTooManyConnectionsFromSubnet (std::shared_ptr<const i2p::data::RouterInfo> r) const;

	private:
		void Run ();
		void RequestComplete (std::shared_ptr<const i2p::data::RouterInfo> r, const i2p::data::IdentHash & ident);
		void HandleRequestComplete (std::shared_ptr<const i2p::data::RouterInfo> r, i2p::data::IdentHash ident);
		std::shared_ptr<TransportSession> PostMessages (
			const i2p::data::IdentHash & ident, std::list<std::shared_ptr<i2p::I2NPMessage>> & msgs);
		bool ConnectToPeer (const i2p::data::IdentHash & ident, std::shared_ptr<Peer> peer);
		void SetPriority (std::shared_ptr<Peer> peer);
		void HandlePeerCleanupTimer (const boost::system::error_code & ecode);
		void HandlePeerTestTimer (const boost::system::error_code & ecode);
		void HandleUpdateBandwidthTimer (const boost::system::error_code & ecode);
		void HandleBanListCleanupTimer (const boost::system::error_code & ecode);
		void UpdateBandwidthValues (int interval, uint32_t & in, uint32_t & out, uint32_t & transit);

		void DetectExternalIP ();

		template <typename Filter>
		std::shared_ptr<const i2p::data::RouterInfo> GetRandomPeer (
			Filter filter, i2p::data::PeerOrdering * peerOrdering) const;
		boost::asio::ip::address GetNetworkAddress (std::shared_ptr<TransportSession> session) const;
		boost::asio::ip::address GetNetworkAddress (const boost::asio::ip::address & addr) const;

	private:
		std::atomic<bool> m_IsOnline { true };
		std::atomic<bool> m_IsRunning { false };
		std::atomic<bool> m_HasSSU2 { false };
		std::atomic<bool> m_HasNTCP2 { false };
		bool m_IsNAT, m_CheckReserved;
		std::unique_ptr<std::thread> m_Thread;
		std::unique_ptr<boost::asio::io_context> m_Service;
		std::unique_ptr<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> m_Work;
		std::unique_ptr<boost::asio::steady_timer> m_PeerCleanupTimer, m_PeerTestTimer, m_UpdateBandwidthTimer;

		std::unique_ptr<SSU2Server> m_SSU2Server;
		std::unique_ptr<NTCP2Server> m_NTCP2Server;
		mutable std::mutex m_PeersMutex;
		std::unordered_map<i2p::data::IdentHash, std::shared_ptr<Peer>> m_Peers;
		mutable std::mutex m_ConnectedNetworksMutex;
		std::map<boost::asio::ip::address, int> m_ConnectedNetworks; // /24 or /56 address -> count

		X25519KeysPairSupplier m_X25519KeysPairSupplier;

		std::atomic<uint64_t> m_TotalSentBytes, m_TotalReceivedBytes, m_TotalTransitTransmittedBytes;

		TrafficSample m_TrafficSamples[TRAFFIC_SAMPLE_COUNT];
		int m_TrafficSamplePtr;

		// Bandwidth per second
		uint32_t m_InBandwidth, m_OutBandwidth, m_TransitBandwidth;
		// Bandwidth during last 15 seconds
		uint32_t m_InBandwidth15s, m_OutBandwidth15s, m_TransitBandwidth15s;
		// Bandwidth during last 5 minutes
		uint32_t m_InBandwidth5m, m_OutBandwidth5m, m_TransitBandwidth5m;

		/** which router families to trust for first hops */
		std::vector<i2p::data::FamilyID> m_TrustedFamilies;
		mutable std::mutex m_FamilyMutex;

		/** which routers for first hop to trust */
		std::unordered_set<i2p::data::IdentHash> m_TrustedRouters;
		mutable std::mutex m_TrustedRoutersMutex;

		i2p::I2NPMessagesHandler m_LoopbackHandler;
		std::mt19937 m_Rng;

		std::map<boost::asio::ip::address, uint64_t> m_BanList; // ip->expiration time in seconds
		mutable std::mutex m_BanListMutex;
		std::unique_ptr<boost::asio::steady_timer> m_BanListCleanupTimer;

	public:
		// for administrative views
		const NTCP2Server * GetNTCP2Server () const { return m_NTCP2Server.get (); };
		SSU2Server * GetSSU2Server () const { return m_SSU2Server.get (); };
		const decltype (m_Peers) & GetPeers () const { return m_Peers; };
	};

	extern Transports transports;

	void InitAddressFromIface ();
	void InitTransports ();
}
}

#endif
