/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: NetDb.
 *
 * Network database: stores RouterInfo and LeaseSet entries in memory
 * and in hashed storage, validates their signatures, timestamps and
 * expiry, and selects random, closest-floodfill or peer-ordered routers
 * for tunnels. Maintains the floodfill DHT, handles DatabaseStore and
 * DatabaseLookup messages, bounds administrative snapshots, persists
 * descriptors and profiles asynchronously, and bootstraps through the
 * Reseeder.
 *
 */

#ifndef NETDB_H__
#define NETDB_H__
// this file is called NetDb.hpp to resolve conflict with libc's netdb.h
// on case insensitive fs
#include <inttypes.h>
#include <array>
#include <atomic>
#include <unordered_set>
#include <unordered_map>
#include <string>
#include <map>
#include <thread>
#include <mutex>
#include <future>
#include <condition_variable>
#include <random>
#include <memory>
#include <utility>
#include <vector>
#include <functional>
#include <list>

#include "Base.h"
#include "Gzip.h"
#include "FS.h"
#include "Queue.h"
#include "I2NPProtocol.h"
#include "RouterInfo.h"
#include "LeaseSet.h"
#include "Tunnel.h"
#include "TunnelPool.h"
#include "Reseed.h"
#include "NetDbRequests.h"
#include "Family.h"
#include "version.h"
#include "util.h"
#include "KadDHT.h"
#include "IdentMetrics.h"

namespace i2p
{
namespace data
{
	const int NETDB_MIN_ROUTERS = 90;
	const int NETDB_MIN_FLOODFILLS = 5;
	const int NETDB_MIN_TRANSPORTS = 10; // otherwise assume offline
	const int NETDB_NUM_FLOODFILLS_THRESHOLD = 2000;
	const int NETDB_NUM_ROUTERS_THRESHOLD = 4 * NETDB_NUM_FLOODFILLS_THRESHOLD;
	const int NETDB_TUNNEL_CREATION_RATE_THRESHOLD = 10; // in %
	const int NETDB_CHECK_FOR_EXPIRATION_UPTIME = 600; // 10 minutes, in seconds
	const int NETDB_FLOODFILL_EXPIRATION_TIMEOUT = 60 * 60; // 1 hour, in seconds
	const int NETDB_MIN_EXPIRATION_TIMEOUT = 90 * 60; // 1.5 hours
	const int NETDB_MAX_EXPIRATION_TIMEOUT = 27 * 60 * 60; // 27 hours
	const int NETDB_MAX_OFFLINE_EXPIRATION_TIMEOUT = 180; // in days
	const int NETDB_EXPIRATION_TIMEOUT_THRESHOLD = 2 * 60; // 2 minutes
	const int NETDB_MIN_HIGHBANDWIDTH_VERSION = MAKE_VERSION_NUMBER (0, 9, 62); // 0.9.62
	const int NETDB_MIN_FLOODFILL_VERSION = MAKE_VERSION_NUMBER (0, 9, 62); // 0.9.62
	const int NETDB_MIN_SHORT_TUNNEL_BUILD_VERSION = MAKE_VERSION_NUMBER (0, 9, 51); // 0.9.51
	const int NETDB_MIN_PEER_TEST_VERSION = MAKE_VERSION_NUMBER (0, 9, 62); // 0.9.62
	const int NETDB_MIN_ALLOWED_VERSION = MAKE_VERSION_NUMBER (0, 9, 57); // 0.9.57 (Debian 12 compatibility)
	const size_t NETDB_MAX_NUM_SEARCH_REPLY_PEER_HASHES = 16;
	const size_t NETDB_MAX_EXPLORATORY_SELECTION_SIZE = 500;
	const int NETDB_EXPLORATORY_SELECTION_UPDATE_INTERVAL = 82; // in seconds. for floodfill
	const int NETDB_NEXT_DAY_ROUTER_INFO_THRESHOLD = 45; // in minutes
	const int NETDB_NEXT_DAY_LEASESET_THRESHOLD = 10; // in minutes
	const int NETDB_MIN_PERSIST_INTERVAL = 15; // 15 secs in seconds
	const int NETDB_MAX_PERSIST_INTERVAL = 1800; // 30 minutes in seconds
	// Java LookupThrottler: a floodfill serves more lookups than an
	// ordinary router. Both limits share the same 120-second
	// per-reply-tunnel window.
	const uint32_t NETDB_LOOKUP_THROTTLE_FLOODFILL = 14;
	const uint32_t NETDB_LOOKUP_THROTTLE_NON_FLOODFILL = 3;
	// Hard caps for entries that need local work. The queue admission cap
	// keeps a burst of DatabaseStore/DatabaseLookup messages from growing
	// the netDb backlog without bound; the store caps bound the in-memory
	// RouterInfo map for new, unsolicited entries (an update of an
	// existing key is always allowed, and the reseed insert path bypasses
	// the store cap).
	const size_t NETDB_MAX_QUEUED_MESSAGES = 1024;
	const size_t NETDB_MAX_ROUTER_INFOS = 1000;
	const size_t NETDB_MAX_ROUTER_INFOS_FLOODFILL = 4000;
	// Java validate() applies its short-freshness predicates only after an
	// hour of uptime, when the local clock and peer view can be trusted.
	const uint32_t NETDB_JAVA_UP_LONG_ENOUGH = 60 * 60; // in seconds

	/** function for visiting a leaseset stored in a floodfill */
	typedef std::function<void (const IdentHash, std::shared_ptr<LeaseSet>)> LeaseSetVisitor;

	/** function for visiting a router info we have locally */
	typedef std::function<void (std::shared_ptr<const i2p::data::RouterInfo>)> RouterInfoVisitor;

	/**
	 * function for visiting a router info and determining if we want to
	 * use it
	 */
	typedef std::function<bool (std::shared_ptr<const i2p::data::RouterInfo>)> RouterInfoFilter;

	/**
	 * Decoded DatabaseLookup reply-key block (I2NP DatabaseLookup, bit
	 * 1/bit 4).
	 *
	 * The block starts after the excluded-peer hashes. `mode == None`
	 * means no key block is present. `tag` holds 32 bytes for ElGamal/AES
	 * and 8 bytes for ECIES; the remaining bytes are zero.
	 */
	struct DatabaseLookupReplyKey
	{
		enum class Mode : uint8_t
		{
			None = 0,
			ElGamalAES, // 0x02
			ECIESAEAD // 0x10
		};

		Mode mode = Mode::None;
		std::array<uint8_t, 32> key { };
		std::array<uint8_t, 32> tag { };
	};

	/**
	 * Validate and decode a DatabaseLookup reply-key block.
	 *
	 * Returns false for truncated blocks, an invalid tag count, or the
	 * unimplemented derived-key mode (bits 4 and 1 both set, 0x12). This
	 * function never selects a cleartext fallback. It is public so the
	 * wire bounds can be regression-tested without starting the netDb
	 * thread.
	 */
	bool ParseDatabaseLookupReplyKey (
		const uint8_t * block, size_t len, uint8_t flags, DatabaseLookupReplyKey & replyKey);

	/**
	 * True when the parsed reply key can encrypt the reply. A lookup that
	 * requested an encrypted reply (bit 1 or bit 4) must never be answered
	 * in cleartext, so callers drop the message when this returns false.
	 *
	 * Parity note: Java 2.14.0 parses the derived-key flag 0x12 (bits 4
	 * and 1) but has no sender/KDF for it, so a Java floodfill answers
	 * such a lookup in cleartext. This implementation is deliberately
	 * fail-closed and drops it instead. No KDF is invented, and the wire
	 * format is unchanged.
	 */
	inline bool CanEncryptDatabaseLookupReply (uint8_t flags, const DatabaseLookupReplyKey & replyKey)
	{
		const bool requested = (flags & (DATABASE_LOOKUP_ENCRYPTION_FLAG | DATABASE_LOOKUP_RATCHET_FLAG)) != 0;
		return !requested || replyKey.mode != DatabaseLookupReplyKey::Mode::None;
	}

	/**
	 * Delivery path of a DatabaseStore/DatabaseLookup.
	 *
	 * Java's InboundMessageDistributor treats a message that crossed an
	 * I2NP tunnel differently from one received directly from a peer. A
	 * garlic delivered directly (HandleGarlicMessageJob) counts as direct;
	 * a clove of a tunnel-delivered garlic does not.
	 */
	enum class DatabaseStoreOrigin : uint8_t
	{
		Direct = 0, // direct transport, not inside garlic
		DirectGarlic, // clove of a garlic received directly from a peer
		Tunnel // I2NP tunnel, directly or via a tunnel-delivered garlic
	};

	inline DatabaseStoreOrigin GetDatabaseStoreOrigin (const I2NPMessage & msg)
	{
		if (msg.fromTunnel || msg.from) return DatabaseStoreOrigin::Tunnel;
		return msg.fromClove ? DatabaseStoreOrigin::DirectGarlic : DatabaseStoreOrigin::Direct;
	}

	inline bool IsDirectDatabaseStoreOrigin (DatabaseStoreOrigin origin)
	{
		return origin != DatabaseStoreOrigin::Tunnel;
	}

	/**
	 * Java InboundMessageDistributor/HandleDatabaseStoreMessageJob: only a
	 * direct store may carry a reply token and be acknowledged/relayed. A
	 * tunnel-origin store still keeps its entry, but the token, reply
	 * tunnel and gateway are cleared, no DeliveryStatus is sent and
	 * nothing is flooded, so a last hop cannot reflect a DeliveryStatus to
	 * an arbitrary reply hash/tunnel.
	 */
	inline bool KeepsDatabaseStoreReplyToken (DatabaseStoreOrigin origin, uint32_t replyToken)
	{
		return IsDirectDatabaseStoreOrigin (origin) && replyToken != 0;
	}

	// A LeaseSet is stored only when the DatabaseStore itself was direct;
	// a tunnel-delivered one is answered by the tunnel's originator, not
	// us.
	inline bool AcceptsUnsolicitedDatabaseStore (DatabaseStoreOrigin /* origin */)
	{
		// Java InboundMessageDistributor stores a tunnel-delivered
		// DatabaseStore after clearing its reply fields; only the
		// acknowledgement depends on the origin. Keep this explicit so a
		// future change cannot reintroduce the old "drop every tunnel store"
		// rule, which broke LeaseSet publication and floodfill propagation.
		return true;
	}

	/**
	 * Java PeerState.gotRI/PeerState2.gotRI admission for the
	 * request-flood bit: only a floodfill forwards a received RouterInfo,
	 * and never for an operator-blocked or runtime-banned key. The per-key
	 * FloodThrottler is charged afterwards by
	 * NetDb::ShouldThrottleFlood().
	 */
	bool IsRouterInfoFloodAdmitted (bool floodfillEnabled, const IdentHash & ident);

	/**
	 * Classification of a file found under the netDb hashed storage root.
	 */
	enum class RouterInfoFileKind : uint8_t
	{
		Load = 0, // exact routerInfo-<base64 identity hash>.dat
		Staging, // interrupted atomic write (routerInfo-*.dat.tmp-<pid>-<seq>)
		Foreign // not written by this storage; left untouched
	};

	/**
	 * Classify a netDb storage path and, for Load, return the expected
	 * base64 identity hash. A cached RouterInfo is only accepted when its
	 * file name still matches the identity hash of the parsed descriptor.
	 */
	RouterInfoFileKind ClassifyRouterInfoFile (const std::string & path, std::string & identBase64);

	/// Owns the router and LeaseSet stores, lookup admission and reseed
	/// job. Container locks protect membership; returned shared pointers
	/// keep entries alive after a lock is released and are not a frozen
	/// database transaction.
	/// @see https://i2p.net/en/docs/specs/i2np/
	class NetDb
	{
	public:
		NetDb ();
		~NetDb ();

		void Start ();
		void Stop ();

		std::shared_ptr<const RouterInfo> AddRouterInfo (const uint8_t * buf, int len);
		// The updated flag lets a transport distinguish a newly accepted or
		// replaced RouterInfo from a stale replay before requesting a flood.
		std::shared_ptr<const RouterInfo> AddRouterInfo (const uint8_t * buf, int len, bool & updated);
		bool AddRouterInfo (const IdentHash & ident, const uint8_t * buf, int len);
		// Forward an already validated RouterInfo through the bounded
		// closest-floodfill selection. Used by the SSU2 request-flood flag;
		// the netDb remains the owner of the actual flood routing.
		bool FloodRouterInfo (std::shared_ptr<const RouterInfo> router);
		// Transport-initiated flood (NTCP2/SSU2 request-flood bit). Applies
		// the Java floodfill/shouldThrottleFlood admission before flooding;
		// returns false when the request was refused (not a floodfill, banned
		// key, or throttled). The DatabaseStore path keeps its own
		// accounting.
		bool FloodRouterInfoConditional (std::shared_ptr<const RouterInfo> router);
		bool AddLeaseSet (const IdentHash & ident, const uint8_t * buf, int len);
		/// Store a LeaseSet2 from an untrusted DatabaseStore. The entry is
		/// accepted only while updates are open, with one of the three known
		/// store types, a valid signature and a store key that matches
		/// `ident` (the destination hash, or the blinded destination for an
		/// encrypted set). An encrypted LS2 is additionally bounded by its
		/// outer published/expiry window because a floodfill cannot read the
		/// inner set.
		/// @return False when the entry was rejected; the caller must not
		/// acknowledge or flood a rejected store.
		bool AddLeaseSet2 (const IdentHash & ident, const uint8_t * buf, int len, uint8_t storeType);
		/// Look up a router by identity hash.
		/// @return A snapshot under shared ownership, or null when
		///         absent. The object may be replaced by a later
		///         AddRouterInfo, so callers must keep the pointer for as
		///         long as they use it and must not treat it as a stable view
		///         of the map.
		std::shared_ptr<RouterInfo> FindRouter (const IdentHash & ident) const;
		/// Look up a destination's LeaseSet.
		/// @return A shared pointer to the currently cached object,
		///         or null. A later store normally installs a freshly
		///         validated object rather than mutating this one, but
		///         locally built LeaseSets do update in place, so the pointer
		///         must not be treated as an immutable snapshot of the cache.
		std::shared_ptr<LeaseSet> FindLeaseSet (const IdentHash & destination) const;
		/// Look up the profile attached to a stored router.
		/// @return Null when the router is unknown or profiles are disabled.
		/// @warning This uses RouterInfo::GetProfile(), which may
		///          create and load a profile from disk; callers already
		///          holding a netDb container lock must use
		///          RouterInfo::GetProfileIfExists() instead.
		std::shared_ptr<RouterProfile> FindRouterProfile (const IdentHash & ident) const;

		void RequestDestination (const IdentHash & destination,
			RequestedDestination::RequestComplete requestComplete = nullptr, bool direct = true);

		std::shared_ptr<const RouterInfo> GetRandomRouter () const;
		/// Pick a random router usable next to `compatibleWith`. The
		/// candidate must be ECIES, at least the minimum accepted version,
		/// not hidden, not a high-congestion peer for a client tunnel, and
		/// not the same family/subnet when reserved-range checking is
		/// enabled.
		/// @param compatibleWith Local or previous-hop
		///                       router the candidate has to interoperate
		///                       with; its transport mask is the filter
		///                       anchor.
		/// @param reverse Inbound direction: require the
		///                candidate to reach us rather than the other way
		///                round (and, for an endpoint, a published IPv4
		///                address).
		/// @param endpoint Candidate must belong to the last-hop ordering
		///                 group when peer ordering is active.
		/// @param clientTunnel Selects the client-tunnel
		///                     congestion policy and may add the
		///                     precise-build-rate "is real" profile check.
		/// @param peerOrdering Per-lookup ordering state
		///                     used to keep first and last hops in different
		///                     groups; optional and borrowed for the call.
		/// @warning Selection filters run under the RouterInfo container lock
		///          and must use the non-creating profile lookup.
		std::shared_ptr<const RouterInfo> GetRandomRouter (std::shared_ptr<const RouterInfo> compatibleWith,
			bool reverse, bool endpoint, bool clientTunnel, PeerOrdering * peerOrdering = nullptr) const;
		std::shared_ptr<const RouterInfo> GetHighBandwidthRandomRouter (
			std::shared_ptr<const RouterInfo> compatibleWith, bool reverse, bool endpoint,
			PeerOrdering * peerOrdering = nullptr) const;
		std::shared_ptr<const RouterInfo> GetRandomSSU2PeerTestRouter (
			bool v4, const std::unordered_set<IdentHash> & excluded) const;
		std::shared_ptr<const RouterInfo> GetRandomSSU2Introducer (
			bool v4, const std::unordered_set<IdentHash> & excluded) const;
		std::shared_ptr<const RouterInfo> GetClosestFloodfill (
			const IdentHash & destination, const std::unordered_set<IdentHash> & excluded, bool nextDay = false) const;
		std::vector<IdentHash> GetClosestFloodfills (const IdentHash & destination, size_t num,
			std::unordered_set<IdentHash> & excluded, bool closeThanUsOnly = false) const;
		std::vector<IdentHash> GetExploratoryNonFloodfill (
			const IdentHash & destination, size_t num, const std::unordered_set<IdentHash> & excluded);
		std::shared_ptr<const RouterInfo> GetRandomRouterInFamily (FamilyID fam) const;
		void SetUnreachable (const IdentHash & ident, bool unreachable);
		void ExcludeReachableTransports (const IdentHash & ident, RouterInfo::CompatibleTransports transports);

		/// Submit a message to the bounded worker queue. Null messages,
		/// requests after admission closes, and messages over the queue cap
		/// are dropped. The shared pointer keeps an admitted message alive
		/// until it is handled.
		void PostI2NPMsg (std::shared_ptr<const I2NPMessage> msg);
		void PostDatabaseSearchReplyMsg (std::shared_ptr<const I2NPMessage> msg); // to NetdbReq thread

		void Reseed ();
		// Non-blocking reseed request for administrative callers (TUI,
		// Android, I2PControl). Returns false when reseed is disabled or when
		// a job is already running; the caller is never blocked. The owned
		// job is cancelled and joined by Stop().
		bool RequestReseed ();
		// Bounded, non-secret administrative view. Never waits for the job.
		ReseedState GetReseedState () const;
		// Test seam: replaces the transport, clock and source decisions so a
		// regression never needs a network or a configuration file.
		void SetReseedTestHooks (const ReseedTestHooks & hooks);
		Families & GetFamilies () { return m_Families; };

		// Counts for administrative interfaces. The containers are
		// mutated by several NetDb threads, so every read takes the
		// corresponding mutex instead of racing with insert/erase.
		int GetNumRouters () const
		{
			std::lock_guard<std::mutex> l (m_RouterInfosMutex);
			return static_cast<int> (m_RouterInfos.size ());
		}
		int GetNumFloodfills () const
		{
			std::lock_guard<std::mutex> l (m_FloodfillsMutex);
			return static_cast<int> (m_Floodfills.GetSize ());
		}
		int GetNumLeaseSets () const
		{
			std::lock_guard<std::mutex> l (m_LeaseSetsMutex);
			return static_cast<int> (m_LeaseSets.size ());
		}
		bool IsReseeding () const { return m_IsReseeding.load (std::memory_order_relaxed); }

		/// Bounded snapshots of container membership for administrative
		/// views. Returned shared pointers keep objects alive after the
		/// container lock is released; LeaseSet objects are not immutable
		/// snapshots of their state.
		/// @param max Maximum entries to return; zero returns an empty
		///            vector.
		std::vector<std::shared_ptr<const RouterInfo>> GetRouterInfosSnapshot (size_t max) const;
		/// Like GetRouterInfosSnapshot(), but also returns each LeaseSet key.
		std::vector<std::pair<IdentHash, std::shared_ptr<LeaseSet>>> GetLeaseSetsSnapshot (size_t max) const;

		/// Snapshot membership under the container lock, then invoke v
		/// unlocked.
		void VisitLeaseSets (LeaseSetVisitor v);
		/// Visit RouterInfos read from a snapshot of stored paths; disk I/O
		/// can be expensive.
		void VisitStoredRouterInfos (RouterInfoVisitor v);
		/// Snapshot in-memory membership under the container lock, then
		/// invoke v unlocked.
		void VisitRouterInfos (RouterInfoVisitor v);
		/// Select up to n matching routers and visit the selected entries
		/// unlocked.
		/// @warning The filter runs while the RouterInfo container
		///          mutex is held; it must not reenter operations that
		///          acquire the same mutex.
		/// @return Number visited, which may be less than n after bounded
		///         retries.
		size_t VisitRandomRouterInfos (RouterInfoFilter f, RouterInfoVisitor v, size_t n);

		void ClearRouterInfos ()
		{
			// Called by Stop() while the netDb/transport threads may still look
			// up or insert RouterInfos. Keep the same router -> floodfill lock
			// order as AddRouterInfo(), and do not leave floodfill entries that
			// reference routers removed from the map.
			std::lock_guard<std::mutex> l (m_RouterInfosMutex);
			m_RouterInfos.clear ();
			std::lock_guard<std::mutex> lf (m_FloodfillsMutex);
			m_Floodfills.Clear ();
		};
		template <typename... TArgs> std::shared_ptr<RouterInfo::Buffer> NewRouterInfoBuffer (TArgs &&... args)
		{
			return m_RouterInfoBuffersPool.AcquireSharedMt (std::forward<TArgs> (args)...);
		}
		bool PopulateRouterInfoBuffer (std::shared_ptr<RouterInfo> r);
		std::shared_ptr<RouterInfo::Address> NewRouterInfoAddress ()
		{
			return m_RouterInfoAddressesPool.AcquireSharedMt ();
		};
		RouterInfo::AddressesPtr NewRouterInfoAddresses ()
		{
			return RouterInfo::AddressesPtr (m_RouterInfoAddressVectorsPool.AcquireMt (),
				i2p::util::MemoryPoolMt<RouterInfo::Addresses>::Deleter { &m_RouterInfoAddressVectorsPool });
		};
		std::shared_ptr<Lease> NewLease (const Lease & lease) { return m_LeasesPool.AcquireSharedMt (lease); };
		std::shared_ptr<IdentityEx> NewIdentity (const uint8_t * buf, size_t len)
		{
			return m_IdentitiesPool.AcquireSharedMt (buf, len);
		};
		std::shared_ptr<RouterProfile> NewRouterProfile () { return m_RouterProfilesPool.AcquireSharedMt (); };

	private:
		// Keep the request manager alive for the duration of a call even if
		// Stop() swaps m_Requests out concurrently.
		std::shared_ptr<NetDbRequests> GetRequests () const
		{
			std::lock_guard<std::mutex> l (m_RequestsMutex);
			return m_Requests;
		}

		void Load ();
		bool LoadRouterInfo (const std::string & path, uint64_t ts);
		void SaveUpdated ();
		void PersistRouters (std::list<std::pair<std::string, std::shared_ptr<RouterInfo::Buffer>>> && update,
			std::list<std::string> && remove);
		void Run ();
		void Flood (const IdentHash & ident, std::shared_ptr<I2NPMessage> floodMsg, bool andNextDay = false);
		void ManageRouterInfos ();
		void ManageLeaseSets ();

		void ReseedFromFloodfill (const RouterInfo & ri, int numRouters = 40, int numFloodfills = 20);

		// Reseed internals. StartReseedLocked() requires m_ReseedMutex and
		// returns false when reseed is disabled or already running.
		bool StartReseedLocked ();
		void RunReseedJob (Reseeder * reseeder, std::stop_token token);
		void PublishReseedCompletion (const ReseedResult & result, bool cancelled);
		void CancelReseedJob ();

		std::shared_ptr<const RouterInfo> AddRouterInfo (
			const IdentHash & ident, const uint8_t * buf, int len, bool & updated);

		template <typename Filter> std::shared_ptr<const RouterInfo> GetRandomRouter (Filter filter) const;

		void HandleDatabaseStoreMsg (std::shared_ptr<const I2NPMessage> msg);
		void HandleDatabaseLookupMsg (std::shared_ptr<const I2NPMessage> msg);
		// Java LookupThrottler/FloodThrottler: bound lookup replies and
		// repeated floods independently of the stored-entry deduplication.
		bool ShouldThrottleLookup (const IdentHash & from, uint32_t tunnelID);
		bool ShouldThrottleFlood (const IdentHash & ident);

	private:
		// Test fixture opens queue admission without starting storage/network
		// workers.
		friend struct NetDbLifecycleTestAccess;
		i2p::util::MemoryPoolMt<RouterInfo::Buffer> m_RouterInfoBuffersPool;
		i2p::util::MemoryPoolMt<RouterInfo::Address> m_RouterInfoAddressesPool;
		i2p::util::MemoryPoolMt<RouterInfo::Addresses> m_RouterInfoAddressVectorsPool;
		i2p::util::MemoryPoolMt<Lease> m_LeasesPool;
		i2p::util::MemoryPoolMt<IdentityEx> m_IdentitiesPool;
		i2p::util::MemoryPoolMt<RouterProfile> m_RouterProfilesPool;

		mutable std::mutex m_LeaseSetsMutex;
		std::unordered_map<IdentHash, std::shared_ptr<LeaseSet>> m_LeaseSets;
		mutable std::mutex m_RouterInfosMutex;
		std::unordered_map<IdentHash, std::shared_ptr<RouterInfo>> m_RouterInfos;
		mutable std::mutex m_FloodfillsMutex;
		DHTTable m_Floodfills;

		std::atomic<bool> m_IsRunning;
		// False between Stop() and the next Start(). Guards every public
		// insertion path, including reseed and DatabaseStore handlers.
		std::atomic<bool> m_AcceptUpdates;
		std::unique_ptr<std::thread> m_Thread;
		i2p::util::Queue<std::shared_ptr<const I2NPMessage>> m_Queue; // of I2NPDatabaseStoreMsg
		// Admission and enqueue share one lock; Stop closes before
		// joining/draining.
		std::mutex m_QueueAdmissionMutex;
		bool m_AcceptMessages = false;
		// Cumulative count of records rejected by the bounded admission
		// above.
		std::atomic<uint64_t> m_DroppedQueueMessages { 0 };
		// Cumulative count of new RouterInfos rejected by the store cap.
		std::atomic<uint64_t> m_RejectedStoreCap { 0 };

		GzipInflator m_Inflator;
		// Reseed ownership: the job is single-flight and owns the reseeder it
		// uses. m_ReseedMutex serializes start/stop and the test hooks; the
		// job itself only publishes through m_ReseedStateMutex, so it never
		// needs m_ReseedMutex and Stop()/Start() can join it while holding
		// the lock.
		std::unique_ptr<Reseeder> m_Reseeder;
		std::unique_ptr<std::jthread> m_ReseedThread;
		std::mutex m_ReseedMutex;
		ReseedTestHooks m_ReseedHooks;
		std::atomic<bool> m_IsReseeding { false };
		mutable std::mutex m_ReseedStateMutex;
		ReseedState m_ReseedState;
		std::mutex m_ReseedDoneMutex;
		std::condition_variable m_ReseedDone;
		Families m_Families;
		i2p::fs::HashedStorage m_Storage;
		// HashedStorage is not internally synchronized. Serialize Init(),
		// Traverse()/Iterate(), Path() and Remove() against TUI/IPC readers
		// and against Start()/Stop().
		mutable std::mutex m_StorageMutex;

		mutable std::mutex m_RequestsMutex;
		std::shared_ptr<NetDbRequests> m_Requests;

		// Start()/Stop() may run on different threads (daemon and
		// I2PControl).
		std::mutex m_LifecycleMutex;
		bool m_PersistProfiles;
		std::future<void> m_SavingProfiles, m_DeletingProfiles, m_ApplyingProfileUpdates, m_PersistingRouters;
		uint64_t m_NetDbPersistInterval; // in milliseconds

		std::mutex m_ExploratorySelectionMutex;
		std::vector<std::shared_ptr<const RouterInfo>> m_ExploratorySelection;
		uint64_t m_LastExploratorySelectionUpdateTime; // in monotonic seconds
		std::mutex m_RngMutex;
		std::mt19937 m_Rng;
		// Bounded per-(from, reply tunnel) and per-key throttles. These are
		// deliberately in-memory only; restart clears them like Java.
		std::mutex m_ThrottleMutex;
		std::map<std::pair<IdentHash, uint32_t>, std::pair<uint64_t, uint32_t>> m_LookupThrottle;
		std::map<IdentHash, std::pair<uint64_t, uint32_t>> m_FloodThrottle;
	};

	extern NetDb netdb;
}
}

#endif
