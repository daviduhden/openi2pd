/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: NetDb.
 *
 * Network database: stores RouterInfo and LeaseSet entries in memory and in hashed storage,
 * validates their signatures, timestamps and expiry, and selects random, closest-floodfill or
 * peer-ordered routers for tunnels. Maintains the floodfill DHT, handles DatabaseStore and
 * DatabaseLookup messages, bounds administrative snapshots, persists descriptors and profiles
 * asynchronously, and bootstraps through the Reseeder.
 *
 */

#ifndef NETDB_H__
#define NETDB_H__
// this file is called NetDb.hpp to resolve conflict with libc's netdb.h on case insensitive fs
#include <inttypes.h>
#include <array>
#include <atomic>
#include <unordered_set>
#include <unordered_map>
#include <string>
#include <thread>
#include <mutex>
#include <future>
#include <random>
#include <memory>
#include <utility>
#include <vector>

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
	const int NETDB_MIN_ALLOWED_VERSION = MAKE_VERSION_NUMBER (0, 9, 58); // 0.9.58
	const size_t NETDB_MAX_NUM_SEARCH_REPLY_PEER_HASHES = 16;
	const size_t NETDB_MAX_EXPLORATORY_SELECTION_SIZE = 500;
	const int NETDB_EXPLORATORY_SELECTION_UPDATE_INTERVAL = 82; // in seconds. for floodfill
	const int NETDB_NEXT_DAY_ROUTER_INFO_THRESHOLD = 45; // in minutes
	const int NETDB_NEXT_DAY_LEASESET_THRESHOLD = 10; // in minutes
	const int NETDB_MIN_PERSIST_INTERVAL = 15; // 15 secs in seconds
	const int NETDB_MAX_PERSIST_INTERVAL = 1800; // 30 minutes in seconds

	/** function for visiting a leaseset stored in a floodfill */
	typedef std::function<void (const IdentHash, std::shared_ptr<LeaseSet>)> LeaseSetVisitor;

	/** function for visiting a router info we have locally */
	typedef std::function<void (std::shared_ptr<const i2p::data::RouterInfo>)> RouterInfoVisitor;

	/** function for visiting a router info and determining if we want to use it */
	typedef std::function<bool (std::shared_ptr<const i2p::data::RouterInfo>)> RouterInfoFilter;

	/**
	 * Decoded DatabaseLookup reply-key block (I2NP DatabaseLookup, bit 1/bit 4).
	 *
	 * The block starts after the excluded-peer hashes. `mode == None` means no
	 * key block is present. `tag` holds 32 bytes for ElGamal/AES and 8 bytes
	 * for ECIES; the remaining bytes are zero.
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
	 * function never selects a cleartext fallback. It is public so the wire
	 * bounds can be regression-tested without starting the netDb thread.
	 */
	bool ParseDatabaseLookupReplyKey (
		const uint8_t * block, size_t len, uint8_t flags, DatabaseLookupReplyKey & replyKey);

	class NetDb
	{
	public:
		NetDb ();
		~NetDb ();

		void Start ();
		void Stop ();

		std::shared_ptr<const RouterInfo> AddRouterInfo (const uint8_t * buf, int len);
		bool AddRouterInfo (const IdentHash & ident, const uint8_t * buf, int len);
		bool AddLeaseSet (const IdentHash & ident, const uint8_t * buf, int len);
		bool AddLeaseSet2 (const IdentHash & ident, const uint8_t * buf, int len, uint8_t storeType);
		std::shared_ptr<RouterInfo> FindRouter (const IdentHash & ident) const;
		std::shared_ptr<LeaseSet> FindLeaseSet (const IdentHash & destination) const;
		std::shared_ptr<RouterProfile> FindRouterProfile (const IdentHash & ident) const;

		void RequestDestination (const IdentHash & destination,
			RequestedDestination::RequestComplete requestComplete = nullptr, bool direct = true);

		std::shared_ptr<const RouterInfo> GetRandomRouter () const;
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

		void PostI2NPMsg (std::shared_ptr<const I2NPMessage> msg);
		void PostDatabaseSearchReplyMsg (std::shared_ptr<const I2NPMessage> msg); // to NetdbReq thread

		void Reseed ();
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

		/**
		 * Bounded immutable snapshots for administrative interfaces.
		 * The returned RouterInfo/LeaseSet shared pointers keep each object
		 * alive while the caller formats it; container iteration itself is
		 * serialized by the corresponding NetDb mutex.
		 */
		std::vector<std::shared_ptr<const RouterInfo>> GetRouterInfosSnapshot (size_t max) const;
		std::vector<std::pair<IdentHash, std::shared_ptr<LeaseSet>>> GetLeaseSetsSnapshot (size_t max) const;

		/** visit all lease sets we currently store */
		void VisitLeaseSets (LeaseSetVisitor v);
		/** visit all router infos we have currently on disk, usually insanely expensive, does not access in memory RI */
		void VisitStoredRouterInfos (RouterInfoVisitor v);
		/** visit all router infos we have loaded in memory, cheaper than VisitLocalRouterInfos but locks access while visiting */
		void VisitRouterInfos (RouterInfoVisitor v);
		/** visit N random router that match using filter, then visit them with a visitor, return number of RouterInfos that were visited */
		size_t VisitRandomRouterInfos (RouterInfoFilter f, RouterInfoVisitor v, size_t n);

		void ClearRouterInfos ()
		{
			// Called by Reseeder while the netDb/transport threads may still
			// look up or insert RouterInfos. Keep the same router -> floodfill
			// lock order as AddRouterInfo(), and do not leave floodfill entries
			// that reference routers removed from the map.
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
			return RouterInfo::AddressesPtr { m_RouterInfoAddressVectorsPool.AcquireMt (),
				std::bind<void (i2p::util::MemoryPoolMt<RouterInfo::Addresses>::*) (RouterInfo::Addresses *)> (
					&i2p::util::MemoryPoolMt<RouterInfo::Addresses>::ReleaseMt, &m_RouterInfoAddressVectorsPool,
					std::placeholders::_1) };
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
		void ManageRequests ();

		void ReseedFromFloodfill (const RouterInfo & ri, int numRouters = 40, int numFloodfills = 20);

		std::shared_ptr<const RouterInfo> AddRouterInfo (const uint8_t * buf, int len, bool & updated);
		std::shared_ptr<const RouterInfo> AddRouterInfo (
			const IdentHash & ident, const uint8_t * buf, int len, bool & updated);

		template <typename Filter> std::shared_ptr<const RouterInfo> GetRandomRouter (Filter filter) const;

		void HandleDatabaseStoreMsg (std::shared_ptr<const I2NPMessage> msg);
		void HandleDatabaseLookupMsg (std::shared_ptr<const I2NPMessage> msg);

	private:
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

		GzipInflator m_Inflator;
		std::unique_ptr<Reseeder> m_Reseeder;
		std::mutex m_ReseedMutex;
		std::atomic<bool> m_IsReseeding { false };
		Families m_Families;
		i2p::fs::HashedStorage m_Storage;
		// HashedStorage is not internally synchronized. Serialize Init(),
		// Traverse()/Iterate(), Path() and Remove() against TUI/IPC readers and
		// against Start()/Stop().
		mutable std::mutex m_StorageMutex;

		mutable std::mutex m_RequestsMutex;
		std::shared_ptr<NetDbRequests> m_Requests;

		// Start()/Stop() may run on different threads (daemon and I2PControl).
		std::mutex m_LifecycleMutex;
		bool m_PersistProfiles;
		std::future<void> m_SavingProfiles, m_DeletingProfiles, m_ApplyingProfileUpdates, m_PersistingRouters;
		uint64_t m_NetDbPersistInterval; // in milliseconds

		std::mutex m_ExploratorySelectionMutex;
		std::vector<std::shared_ptr<const RouterInfo>> m_ExploratorySelection;
		uint64_t m_LastExploratorySelectionUpdateTime; // in monotonic seconds
		std::mutex m_RngMutex;
		std::mt19937 m_Rng;
	};

	extern NetDb netdb;
}
}

#endif
