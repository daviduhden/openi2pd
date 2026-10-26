/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Profiling.
 *
 * Peer profiles used for tunnel and routing decisions: participation, usage, reachability and
 * last-endpoint history with decline and usefulness thresholds. Profiles persist to disk,
 * expire after 1.5 days, and are updated through a coalesced path so the router thread is not
 * blocked by storage.
 *
 */

#ifndef PROFILING_H__
#define PROFILING_H__

#include <memory>
#include <future>
#include <functional>
#include <mutex>
#include <string>
#include <boost/asio.hpp>
#include "Identity.h"

namespace i2p
{
namespace data
{
	// sections
	const char PEER_PROFILE_SECTION_PARTICIPATION[] = "participation";
	const char PEER_PROFILE_SECTION_USAGE[] = "usage";
	// params
	const char PEER_PROFILE_LAST_UPDATE_TIME[] = "lastupdatetime"; // deprecated
	const char PEER_PROFILE_LAST_UPDATE_TIMESTAMP[] = "lastupdatetimestamp";
	const char PEER_PROFILE_LAST_UNREACHABLE_TIME[] = "lastunreachabletime";
	const char PEER_PROFILE_PARTICIPATION_AGREED[] = "agreed";
	const char PEER_PROFILE_PARTICIPATION_DECLINED[] = "declined";
	const char PEER_PROFILE_PARTICIPATION_NON_REPLIED[] = "nonreplied";
	const char PEER_PROFILE_USAGE_TAKEN[] = "taken";
	const char PEER_PROFILE_USAGE_REJECTED[] = "rejected";
	const char PEER_PROFILE_USAGE_CONNECTED[] = "connected";
	const char PEER_PROFILE_USAGE_DUPLICATED[] = "duplicated";

	const int PEER_PROFILE_EXPIRATION_TIMEOUT = 36 * 60 * 60; // in seconds (1.5 days)
	const int PEER_PROFILE_AUTOCLEAN_TIMEOUT = 1500; // in seconds (25 minutes)
	const int PEER_PROFILE_AUTOCLEAN_VARIANCE = 900; // in seconds (15 minutes)
	const int PEER_PROFILE_OBSOLETE_PROFILES_CLEAN_TIMEOUT = 5400; // in seconds (1.5 hours)
	const int PEER_PROFILE_OBSOLETE_PROFILES_CLEAN_VARIANCE = 2400; // in seconds (40 minutes)
	const int PEER_PROFILE_DECLINED_RECENTLY_INTERVAL = 330; // in seconds (5.5 minutes)
	const int PEER_PROFILE_MAX_DECLINED_INTERVAL = 4400; // in second (1.5 hours)
	const int PEER_PROFILE_PERSIST_INTERVAL = 1320; // in seconds (22 minutes)
	const int PEER_PROFILE_UNREACHABLE_INTERVAL = 480; // in seconds (8 minutes)
	const int PEER_PROFILE_USEFUL_THRESHOLD = 3;
	const int PEER_PROFILE_ALWAYS_DECLINING_NUM = 5; // num declines in row to consider always declined
	const int PEER_PROFILE_APPLY_POSTPONED_TIMEOUT = 2100; // in milliseconds
	const int PEER_PROFILE_APPLY_POSTPONED_TIMEOUT_VARIANCE = 500; // in milliseconds

	class RouterProfile
	{
	public:
		RouterProfile ();

		bool Save (const IdentHash & identHash);
		// Load before publication; do not replace state while callers mutate it.
		void Load (const IdentHash & identHash);

		bool IsBad ();
		bool IsUnreachable ();
		bool IsReal () const;

		void TunnelBuildResponse (uint8_t ret);
		void TunnelNonReplied (int tunnelLength);

		void Unreachable (bool unreachable);
		void Connected ();
		void Duplicated ();

		uint64_t GetLastUpdateTime () const;
		bool IsUpdated () const;
		void SetUpdated (bool updated);
		bool MarkPersistenceDue (uint64_t ts);
		void MarkPersistenceFailed ();
		bool IsExpiredFromCache (uint64_t ts) const;
		bool IsExpiredByLifetime (uint64_t ts) const;
		bool ShouldSaveOnShutdown (uint64_t ts) const;
		uint64_t GetLastAccessTime () const;
		void SetLastAccessTime (uint64_t ts);
		uint64_t GetLastPersistTime () const;
		void SetLastPersistTime (uint64_t ts);

		bool IsUseful () const;
		bool IsDuplicated () const;

		boost::asio::ip::udp::endpoint GetLastEndpoint () const;
		void SetLastEndpoint (const boost::asio::ip::udp::endpoint & ep);
		bool HasLastEndpoint (bool v4) const;

	private:
		void UpdateTime ();

		bool IsRealLocked () const;
		bool IsUnreachableLocked (uint64_t ts);
		bool IsDeclinedRecentlyLocked (uint64_t ts);

	private:
		mutable std::mutex m_Mutex;
		mutable std::mutex m_IoMutex;
		bool m_IsUpdated;
		uint64_t m_LastDeclineTime, m_LastUnreachableTime, m_LastUpdateTime, m_LastAccessTime,
			m_LastPersistTime; // in seconds
		// participation
		uint32_t m_NumTunnelsAgreed;
		uint32_t m_NumTunnelsDeclined;
		float m_NumTunnelsNonReplied; // per one hop tunnel, 2 hops tunnel adds 0.5
		// usage
		uint32_t m_NumTimesTaken;
		uint32_t m_NumTimesRejected;
		bool m_HasConnected; // successful trusted(incoming or NTCP2) connection
		bool m_IsDuplicated;
		// connectivity
		boost::asio::ip::udp::endpoint m_LastEndpoint; // SSU2 for non-published addresses
	};

	std::shared_ptr<RouterProfile> GetRouterProfile (const IdentHash & identHash);
	// Non-creating lookup: returns an existing in-memory profile, or null. It
	// never creates a profile and never reads from disk, so it is safe for
	// callers that hold a netDb container lock (selection filters).
	std::shared_ptr<RouterProfile> GetRouterProfileIfExists (const IdentHash & identHash);
	// Deterministic test seam: observes every profile created by
	// GetRouterProfile() that has to load its persisted state. The observer runs
	// before the load; nullptr restores the default (no observer).
	void SetRouterProfileLoadObserverForTesting (std::function<void (const IdentHash &)> observer);
	// Operator blocklist or runtime ban first, then the existing profile-tier
	// unreachable check. Cheap and lock-free while the blocklist/runtime sets
	// are empty; callers may use it on the transport hot path.
	bool IsRouterBanned (const IdentHash & identHash);
	bool IsRouterDuplicated (const IdentHash & identHash); // check only existing profiles
	void InitProfilesStorage ();
	// The selected root is immutable for the process lifetime; changing datadir
	// requires a process restart. Calls are coordinated within this process and
	// require exclusive ownership of the data directory. The overload accepts a test root.
	void InitProfilesStorage (const std::string & place);
	std::future<void> DeleteObsoleteProfiles ();
	// Caller must stop profile producers and drain NetDb-owned async profile work first.
	void SaveProfiles ();
	std::future<void> PersistProfiles ();
	bool UpdateRouterProfile (const IdentHash & identHash,
		std::function<void (std::shared_ptr<RouterProfile>)>
			update); // return true if updated immediately, and false if postponed
	std::future<void> FlushPostponedRouterProfileUpdates ();
	void DiscardPostponedRouterProfileUpdates ();
}
}

#endif
