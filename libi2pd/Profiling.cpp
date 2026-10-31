/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Profiling.
 *
 * Implements peer profiles that record tunnel participation, decline,
 * non-reply and usage history and derive bad, unreachable and useful
 * classifications. Profiles are loaded from and saved to per-peer INI
 * files in hashed storage through asynchronous persistence, with
 * expiry-based cleanup.
 *
 */

#include <sys/stat.h>
#include <unordered_map>
#include <list>
#include <thread>
#include <random>
#include <sstream>
#include <span>
#include <string_view>
#include <stdexcept>
#include <iomanip>
#include <algorithm>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include "Base.h"
#include "FS.h"
#include "Log.h"
#include "Timestamp.h"
#include "util.h"
#include "NetDb.hpp"
#include "Tunnel.h"
#include "Profiling.h"
#include "Blocklist.h"
#include "PersistentStorage.h"

namespace i2p
{
namespace data
{
	static i2p::fs::HashedStorage g_ProfilesStorage ("peerProfiles", "p", "profile-", "txt");
	// Storage lock is outermost for filesystem work; no map/state lock is
	// held while performing file IO. Profile methods acquire their IO lock
	// first. A RouterProfile can be read from several threads while it is
	// written by the tunnel or transport that observed the peer. m_Mutex
	// guards every field (including endpoints and the dirty flag);
	// m_IoMutex serialises Save()/Load() per object so one profile has at
	// most one file operation in flight. The lock order is m_IoMutex
	// before m_Mutex. Callers must hold a shared_ptr for as long as they
	// touch the profile: the caches only evict an entry whose shared count
	// is one. The storage root is fixed for the process lifetime
	// (InitProfilesStorage); g_ProfilesStorageMutex makes "initialized +
	// root" a single immutable fact so a writer never observes a
	// half-configured store.
	static std::mutex g_ProfilesStorageMutex;
	static bool g_ProfilesStorageInitialized = false;
	static std::string g_ProfilesStoragePlace;
	static std::unordered_map<i2p::data::IdentHash, std::shared_ptr<RouterProfile>> g_Profiles;
	static std::mutex g_ProfilesMutex;
	static std::list<std::pair<i2p::data::IdentHash, std::function<void (std::shared_ptr<RouterProfile>)>>>
		g_PostponedUpdates;
	static std::mutex g_PostponedUpdatesMutex;
	static std::mt19937 m_ProfilesRng (i2p::util::GetRngSeed ());
	static std::mutex m_ProfilesRngMutex;
	static std::mutex g_ProfileLoadObserverMutex;
	static std::function<void (const i2p::data::IdentHash &)> g_ProfileLoadObserver;

	RouterProfile::RouterProfile ()
		: m_IsUpdated (false)
		, m_LastDeclineTime (0)
		, m_LastUnreachableTime (0)
		, m_LastUpdateTime (i2p::util::GetSecondsSinceEpoch ())
		, m_LastAccessTime (0)
		, m_LastPersistTime (0)
		, m_NumTunnelsAgreed (0)
		, m_NumTunnelsDeclined (0)
		, m_NumTunnelsNonReplied (0)
		, m_NumTimesTaken (0)
		, m_NumTimesRejected (0)
		, m_HasConnected (false)
		, m_IsDuplicated (false)
	{
	}

	void RouterProfile::UpdateTime ()
	{
		m_LastUpdateTime = i2p::util::GetSecondsSinceEpoch ();
		m_IsUpdated = true;
	}

	bool RouterProfile::Save (const IdentHash & identHash)
	{
		std::lock_guard ioLock (m_IoMutex);
		uint64_t lastUpdateTime = 0;
		uint64_t lastUnreachableTime = 0;
		uint32_t numTunnelsAgreed = 0;
		uint32_t numTunnelsDeclined = 0;
		float numTunnelsNonReplied = 0;
		uint32_t numTimesTaken = 0;
		uint32_t numTimesRejected = 0;
		bool hasConnected = false;
		bool isDuplicated = false;
		{
			std::lock_guard lock (m_Mutex);
			lastUpdateTime = m_LastUpdateTime;
			lastUnreachableTime = m_LastUnreachableTime;
			numTunnelsAgreed = m_NumTunnelsAgreed;
			numTunnelsDeclined = m_NumTunnelsDeclined;
			numTunnelsNonReplied = m_NumTunnelsNonReplied;
			numTimesTaken = m_NumTimesTaken;
			numTimesRejected = m_NumTimesRejected;
			hasConnected = m_HasConnected;
			isDuplicated = m_IsDuplicated;
		}
		// fill sections
		boost::property_tree::ptree participation;
		participation.put (PEER_PROFILE_PARTICIPATION_AGREED, numTunnelsAgreed);
		participation.put (PEER_PROFILE_PARTICIPATION_DECLINED, numTunnelsDeclined);
		participation.put (PEER_PROFILE_PARTICIPATION_NON_REPLIED, numTunnelsNonReplied);
		boost::property_tree::ptree usage;
		usage.put (PEER_PROFILE_USAGE_TAKEN, numTimesTaken);
		usage.put (PEER_PROFILE_USAGE_REJECTED, numTimesRejected);
		usage.put (PEER_PROFILE_USAGE_CONNECTED, hasConnected);
		if (isDuplicated) usage.put (PEER_PROFILE_USAGE_DUPLICATED, true);
		// fill property tree
		boost::property_tree::ptree pt;
		pt.put (PEER_PROFILE_LAST_UPDATE_TIMESTAMP, lastUpdateTime);
		if (lastUnreachableTime) pt.put (PEER_PROFILE_LAST_UNREACHABLE_TIME, lastUnreachableTime);
		pt.put_child (PEER_PROFILE_SECTION_PARTICIPATION, participation);
		pt.put_child (PEER_PROFILE_SECTION_USAGE, usage);

		// save to file
		try
		{
			std::ostringstream serialized;
			serialized.imbue (std::locale::classic ());
			boost::property_tree::write_ini (serialized, pt);
			std::lock_guard storageLock (g_ProfilesStorageMutex);
			if (!g_ProfilesStorageInitialized)
			{
				LogPrint (eLogError, "Profiling: Storage is not initialized; refusing profile write");
				return false;
			}
			// The peer identity is the artifact name and the INI bytes are
			// unchanged, so a worker-written profile stays readable by Load().
			// A failed write keeps the previous file and returns false, so the
			// caller retains the dirty state and retries later.
			const auto ident = identHash.ToBase64 ();
			const auto text = serialized.str ();
			const std::span<const uint8_t> bytes (reinterpret_cast<const uint8_t *> (text.data ()), text.size ());
			if (!i2p::data::WriteArtifact (i2p::data::StorageArtifact::PeerProfile, ident, bytes)) return false;
		}
		catch (std::exception & ex)
		{
			/* boost exception verbose enough */
			LogPrint (eLogError, "Profiling: ", ex.what ());
			return false;
		}
		return true;
	}

	void RouterProfile::Load (const IdentHash & identHash)
	{
		std::lock_guard ioLock (m_IoMutex);
		const auto ident = identHash.ToBase64 ();
		boost::property_tree::ptree pt;

		try
		{
			std::lock_guard storageLock (g_ProfilesStorageMutex);
			if (!g_ProfilesStorageInitialized)
			{
				LogPrint (eLogWarning, "Profiling: Storage is not initialized; skipping profile load");
				return;
			}
			const auto path = g_ProfilesStorage.Path (ident);
			if (!i2p::fs::Exists (path))
			{
				LogPrint (eLogWarning, "Profiling: No profile yet for ", ident);
				return;
			}
			boost::property_tree::read_ini (path, pt, std::locale::classic ());
		}
		catch (std::exception & ex)
		{
			/* boost exception verbose enough */
			LogPrint (eLogError, "Profiling: ", ex.what ());
			return;
		}

		uint64_t lastUpdateTime = i2p::util::GetSecondsSinceEpoch ();
		uint64_t lastUnreachableTime = 0;
		uint32_t numTunnelsAgreed = 0;
		uint32_t numTunnelsDeclined = 0;
		float numTunnelsNonReplied = 0;
		uint32_t numTimesTaken = 0;
		uint32_t numTimesRejected = 0;
		bool hasConnected = false;
		bool isDuplicated = false;
		try
		{
			auto ts = pt.get (PEER_PROFILE_LAST_UPDATE_TIMESTAMP, 0);
			if (ts)
				lastUpdateTime = ts;
			else
			{
				// try old lastupdatetime
				auto ut = pt.get (PEER_PROFILE_LAST_UPDATE_TIME, "");
				if (ut.length () > 0)
				{
					std::istringstream ss (ut);
					std::tm t { };
					ss >> std::get_time (&t, "%Y-%b-%d %H:%M:%S");
					if (!ss.fail ()) lastUpdateTime = mktime (&t); // t is local time
				}
			}
			if (i2p::util::GetSecondsSinceEpoch () - lastUpdateTime < PEER_PROFILE_EXPIRATION_TIMEOUT)
			{
				lastUnreachableTime = pt.get (PEER_PROFILE_LAST_UNREACHABLE_TIME, 0);
				try
				{
					// read participations
					auto participations = pt.get_child (PEER_PROFILE_SECTION_PARTICIPATION);
					numTunnelsAgreed = participations.get (PEER_PROFILE_PARTICIPATION_AGREED, 0);
					numTunnelsDeclined = participations.get (PEER_PROFILE_PARTICIPATION_DECLINED, 0);
					numTunnelsNonReplied = participations.get (PEER_PROFILE_PARTICIPATION_NON_REPLIED, 0.0f);
				}
				catch (boost::property_tree::ptree_bad_path &)
				{
					LogPrint (eLogWarning, "Profiling: Missing section ", PEER_PROFILE_SECTION_PARTICIPATION,
						" in profile for ", ident);
				}
				try
				{
					// read usage
					auto usage = pt.get_child (PEER_PROFILE_SECTION_USAGE);
					numTimesTaken = usage.get (PEER_PROFILE_USAGE_TAKEN, 0);
					numTimesRejected = usage.get (PEER_PROFILE_USAGE_REJECTED, 0);
					hasConnected = usage.get (PEER_PROFILE_USAGE_CONNECTED, false);
					isDuplicated = usage.get (PEER_PROFILE_USAGE_DUPLICATED, false);
				}
				catch (boost::property_tree::ptree_bad_path &)
				{
					LogPrint (eLogWarning, "Profiling: Missing section ", PEER_PROFILE_SECTION_USAGE,
						" in profile for ", ident);
				}
			}
			else
			{
				lastUpdateTime = i2p::util::GetSecondsSinceEpoch ();
				lastUnreachableTime = 0;
				numTunnelsAgreed = 0;
				numTunnelsDeclined = 0;
				numTunnelsNonReplied = 0;
				numTimesTaken = 0;
				numTimesRejected = 0;
				hasConnected = false;
				isDuplicated = false;
			}
		}
		catch (std::exception & ex)
		{
			LogPrint (eLogError, "Profiling: Can't read profile ", ident, " :", ex.what ());
			return;
		}

		{
			std::lock_guard lock (m_Mutex);
			m_LastDeclineTime = 0;
			m_LastUnreachableTime = lastUnreachableTime;
			m_LastUpdateTime = lastUpdateTime;
			m_LastAccessTime = 0;
			m_LastPersistTime = 0;
			m_IsUpdated = false;
			m_NumTunnelsAgreed = numTunnelsAgreed;
			m_NumTunnelsDeclined = numTunnelsDeclined;
			m_NumTunnelsNonReplied = numTunnelsNonReplied;
			m_NumTimesTaken = numTimesTaken;
			m_NumTimesRejected = numTimesRejected;
			m_HasConnected = hasConnected;
			m_IsDuplicated = isDuplicated;
			m_LastEndpoint = { };
		}
	}

	void RouterProfile::TunnelBuildResponse (uint8_t ret)
	{
		std::lock_guard lock (m_Mutex);
		UpdateTime ();
		if (ret > 0)
		{
			m_NumTunnelsDeclined++;
			m_LastDeclineTime = i2p::util::GetSecondsSinceEpoch ();
		}
		else
		{
			m_NumTunnelsAgreed++;
			m_LastDeclineTime = 0;
		}
	}

	void RouterProfile::TunnelNonReplied (int tunnelLength)
	{
		if (!tunnelLength) return;
		std::lock_guard lock (m_Mutex);
		if (tunnelLength == 1)
			m_NumTunnelsNonReplied++;
		else
			m_NumTunnelsNonReplied += 1.0f / tunnelLength;
		UpdateTime ();
		if (m_NumTunnelsNonReplied > m_NumTunnelsAgreed + m_NumTunnelsDeclined && m_NumTunnelsNonReplied > 3)
		{
			m_LastDeclineTime = i2p::util::GetSecondsSinceEpoch ();
		}
	}

	void RouterProfile::Unreachable (bool unreachable)
	{
		std::lock_guard lock (m_Mutex);
		m_LastUnreachableTime = unreachable ? i2p::util::GetSecondsSinceEpoch () : 0;
		UpdateTime ();
	}

	void RouterProfile::Connected ()
	{
		std::lock_guard lock (m_Mutex);
		m_HasConnected = true;
		UpdateTime ();
	}

	void RouterProfile::Duplicated ()
	{
		std::lock_guard lock (m_Mutex);
		m_IsDuplicated = true;
	}

	bool RouterProfile::IsDeclinedRecentlyLocked (uint64_t ts)
	{
		if (!m_LastDeclineTime) return false;
		if (ts > m_LastDeclineTime + PEER_PROFILE_DECLINED_RECENTLY_INTERVAL
			|| ts + PEER_PROFILE_DECLINED_RECENTLY_INTERVAL < m_LastDeclineTime)
			m_LastDeclineTime = 0;
		return (bool)m_LastDeclineTime;
	}

	bool RouterProfile::IsBad ()
	{
		uint32_t randomValues[3];
		{
			std::lock_guard lock (m_ProfilesRngMutex);
			for (auto & value : randomValues) value = m_ProfilesRng ();
		}
		const auto successRate = i2p::tunnel::tunnels.GetPreciseTunnelCreationSuccessRate ();
		std::lock_guard lock (m_Mutex);
		const auto ts = i2p::util::GetSecondsSinceEpoch ();
		if (IsUnreachableLocked (ts) || m_IsDuplicated) return true;
		if (IsDeclinedRecentlyLocked (ts)) return true;
		bool checkIsKnown = successRate < (randomValues[0] % 100);
		if (checkIsKnown)
		{
			if (!m_NumTunnelsAgreed && !m_NumTunnelsDeclined) return true;
		}
		bool isBad = false;
		auto total = m_NumTunnelsAgreed + m_NumTunnelsDeclined;
		if (total)
			isBad = m_NumTunnelsAgreed < randomValues[1] % total;
		else if ((int)m_NumTunnelsNonReplied)
			isBad = randomValues[2] % (int)m_NumTunnelsNonReplied;
		if (isBad)
			m_NumTimesRejected++;
		else
			m_NumTimesTaken++;
		return isBad;
	}

	bool RouterProfile::IsUnreachable ()
	{
		std::lock_guard lock (m_Mutex);
		return IsUnreachableLocked (i2p::util::GetSecondsSinceEpoch ());
	}

	bool RouterProfile::IsUnreachableLocked (uint64_t ts)
	{
		if (!m_LastUnreachableTime) return false;
		if (ts > m_LastUnreachableTime + PEER_PROFILE_UNREACHABLE_INTERVAL
			|| ts + PEER_PROFILE_UNREACHABLE_INTERVAL < m_LastUnreachableTime)
			m_LastUnreachableTime = 0;
		return (bool)m_LastUnreachableTime;
	}

	bool RouterProfile::IsUseful () const
	{
		std::lock_guard lock (m_Mutex);
		return IsRealLocked () || m_NumTunnelsNonReplied >= PEER_PROFILE_USEFUL_THRESHOLD;
	}

	bool RouterProfile::IsReal () const
	{
		std::lock_guard lock (m_Mutex);
		return IsRealLocked ();
	}

	bool RouterProfile::IsRealLocked () const
	{
		return m_HasConnected || m_NumTunnelsAgreed > 0 || m_NumTunnelsDeclined > 0;
	}

	uint64_t RouterProfile::GetLastUpdateTime () const
	{
		std::lock_guard lock (m_Mutex);
		return m_LastUpdateTime;
	}

	bool RouterProfile::IsUpdated () const
	{
		std::lock_guard lock (m_Mutex);
		return m_IsUpdated;
	}

	void RouterProfile::SetUpdated (bool updated)
	{
		std::lock_guard lock (m_Mutex);
		m_IsUpdated = updated;
	}

	bool RouterProfile::MarkPersistenceDue (uint64_t ts)
	{
		std::lock_guard lock (m_Mutex);
		if (!m_IsUpdated || ts <= m_LastPersistTime + PEER_PROFILE_PERSIST_INTERVAL) return false;
		m_LastPersistTime = ts;
		m_IsUpdated = false;
		return true;
	}

	void RouterProfile::MarkPersistenceFailed ()
	{
		std::lock_guard lock (m_Mutex);
		m_IsUpdated = true;
		m_LastPersistTime = 0;
	}

	bool RouterProfile::IsExpiredFromCache (uint64_t ts) const
	{
		std::lock_guard lock (m_Mutex);
		return !m_IsUpdated && ts > std::max (m_LastUpdateTime, m_LastAccessTime) + PEER_PROFILE_PERSIST_INTERVAL;
	}

	bool RouterProfile::IsExpiredByLifetime (uint64_t ts) const
	{
		std::lock_guard lock (m_Mutex);
		return !m_IsUpdated && ts - m_LastUpdateTime >= PEER_PROFILE_EXPIRATION_TIMEOUT;
	}

	bool RouterProfile::ShouldSaveOnShutdown (uint64_t ts) const
	{
		std::lock_guard lock (m_Mutex);
		return (IsRealLocked () || m_NumTunnelsNonReplied >= PEER_PROFILE_USEFUL_THRESHOLD)
			&& (m_IsUpdated || ts - m_LastUpdateTime < PEER_PROFILE_EXPIRATION_TIMEOUT);
	}

	uint64_t RouterProfile::GetLastAccessTime () const
	{
		std::lock_guard lock (m_Mutex);
		return m_LastAccessTime;
	}

	void RouterProfile::SetLastAccessTime (uint64_t ts)
	{
		std::lock_guard lock (m_Mutex);
		m_LastAccessTime = ts;
	}

	uint64_t RouterProfile::GetLastPersistTime () const
	{
		std::lock_guard lock (m_Mutex);
		return m_LastPersistTime;
	}

	void RouterProfile::SetLastPersistTime (uint64_t ts)
	{
		std::lock_guard lock (m_Mutex);
		m_LastPersistTime = ts;
	}

	bool RouterProfile::IsDuplicated () const
	{
		std::lock_guard lock (m_Mutex);
		return m_IsDuplicated;
	}

	boost::asio::ip::udp::endpoint RouterProfile::GetLastEndpoint () const
	{
		std::lock_guard lock (m_Mutex);
		return m_LastEndpoint;
	}

	void RouterProfile::SetLastEndpoint (const boost::asio::ip::udp::endpoint & ep)
	{
		std::lock_guard lock (m_Mutex);
		m_LastEndpoint = ep;
	}

	bool RouterProfile::HasLastEndpoint (bool v4) const
	{
		std::lock_guard lock (m_Mutex);
		return !m_LastEndpoint.address ().is_unspecified () && m_LastEndpoint.port ()
			&& ((v4 && m_LastEndpoint.address ().is_v4 ()) || (!v4 && m_LastEndpoint.address ().is_v6 ()));
	}

	/**
	 * @brief Return the canonical profile for a peer, creating and loading
	 *        it once.
	 *
	 * The cache lookup is done under g_ProfilesMutex; a miss then loads
	 * from disk outside every lock and inserts under the lock again. Two
	 * threads that race on the same new peer may therefore both parse the
	 * file, but emplace keeps the first instance and both callers receive
	 * that same canonical object, so accounting is never split across two
	 * profiles. The returned shared_ptr extends the object's lifetime past
	 * map eviction; callers must keep it for the whole time they read or
	 * mutate the profile.
	 *
	 * The optional load observer runs on the miss path only (used by tests
	 * and by the deferred-update flush), outside the map lock, so it may
	 * allocate and perform IO.
	 */
	std::shared_ptr<RouterProfile> GetRouterProfile (const IdentHash & identHash)
	{
		{
			std::unique_lock<std::mutex> l (g_ProfilesMutex);
			auto it = g_Profiles.find (identHash);
			if (it != g_Profiles.end ())
			{
				it->second->SetLastAccessTime (i2p::util::GetSecondsSinceEpoch ());
				return it->second;
			}
		}
		{
			std::function<void (const IdentHash &)> observer;
			{
				std::lock_guard<std::mutex> l (g_ProfileLoadObserverMutex);
				observer = g_ProfileLoadObserver;
			}
			if (observer) observer (identHash);
		}
		auto profile = netdb.NewRouterProfile ();
		profile->Load (identHash); // if possible
		{
			std::lock_guard<std::mutex> l (g_ProfilesMutex);
			auto it = g_Profiles.emplace (identHash, profile).first;
			profile = it->second;
		}
		profile->SetLastAccessTime (i2p::util::GetSecondsSinceEpoch ());
		return profile;
	}

	std::shared_ptr<RouterProfile> GetRouterProfileIfExists (const IdentHash & identHash)
	{
		std::lock_guard<std::mutex> l (g_ProfilesMutex);
		auto it = g_Profiles.find (identHash);
		if (it == g_Profiles.end ()) return nullptr;
		it->second->SetLastAccessTime (i2p::util::GetSecondsSinceEpoch ());
		return it->second;
	}

	void SetRouterProfileLoadObserverForTesting (std::function<void (const IdentHash &)> observer)
	{
		std::lock_guard<std::mutex> l (g_ProfileLoadObserverMutex);
		g_ProfileLoadObserver = std::move (observer);
	}

	bool IsRouterBanned (const IdentHash & identHash)
	{
		// Operator policy comes first: a blocklisted or runtime-banned router
		// is never a valid peer, regardless of its historical profile. Both
		// checks are cheap and the runtime-ban path is lock-free while the
		// set is empty.
		if (IsOperatorBlockedRouter (identHash) || IsRuntimeRouterBanned (identHash)) return true;
		std::lock_guard<std::mutex> l (g_ProfilesMutex);
		auto it = g_Profiles.find (identHash);
		if (it != g_Profiles.end ()) return it->second->IsUnreachable ();
		return false;
	}

	bool IsRouterDuplicated (const IdentHash & identHash)
	{
		std::lock_guard<std::mutex> l (g_ProfilesMutex);
		auto it = g_Profiles.find (identHash);
		if (it != g_Profiles.end ()) return it->second->IsDuplicated ();
		return false;
	}

	void InitProfilesStorage ()
	{
		InitProfilesStorage (i2p::fs::GetDataDir ());
	}

	void InitProfilesStorage (const std::string & place)
	{
		if (place.empty ()) throw std::runtime_error ("Profiling: Refusing empty storage root");
		const auto requested = std::filesystem::weakly_canonical (place).string ();
		std::lock_guard storageLock (g_ProfilesStorageMutex);
		if (g_ProfilesStorageInitialized)
		{
			if (requested != g_ProfilesStoragePlace)
				throw std::runtime_error ("Profiling: Storage root is immutable after initialization");
			return;
		}
		g_ProfilesStorage.SetPlace (requested);
		if (!g_ProfilesStorage.Init (i2p::data::GetBase64SubstitutionTable (), 64))
			throw std::runtime_error ("Profiling: Failed to initialize storage root");
		g_ProfilesStoragePlace = requested;
		g_ProfilesStorageInitialized = true;
	}

	static void SaveProfilesToDisk (
		std::list<std::pair<i2p::data::IdentHash, std::shared_ptr<RouterProfile>>> && profiles)
	{
		for (auto & it : profiles)
			if (it.second && !it.second->Save (it.first)) it.second->MarkPersistenceFailed ();
	}

	/**
	 * @brief Snapshot the profiles that are due and start an asynchronous
	 *        save.
	 * @return future of the save; an invalid future when nothing is due.
	 *
	 * MarkPersistenceDue() both selects a profile and clears its dirty
	 * flag, so the snapshot cannot contain the same update twice; a failed
	 * write is reported back through MarkPersistenceFailed() and the flag
	 * is restored. Eviction requires use_count() == 1: any thread still
	 * holding the profile may mutate it outside the map lock, so dropping
	 * the canonical reference early would let a later lookup create a
	 * second object for the same peer.
	 *
	 * The future is not detached: destroying it blocks until the save
	 * finished, which is what keeps the storage root and the worker
	 * channel alive across shutdown. Callers that start a save must
	 * therefore wait for the future before reconfiguring or releasing the
	 * storage backend.
	 */
	std::future<void> PersistProfiles ()
	{
		{
			std::lock_guard storageLock (g_ProfilesStorageMutex);
			if (!g_ProfilesStorageInitialized)
			{
				LogPrint (eLogWarning, "Profiling: Storage is not initialized; skipping persistence");
				return std::future<void> ();
			}
		}
		auto ts = i2p::util::GetSecondsSinceEpoch ();
		std::list<std::pair<i2p::data::IdentHash, std::shared_ptr<RouterProfile>>> tmp;
		{
			std::lock_guard<std::mutex> l (g_ProfilesMutex);
			for (auto it = g_Profiles.begin (); it != g_Profiles.end ();)
			{
				if (it->second->MarkPersistenceDue (ts))
				{
					tmp.push_back (*it);
					++it;
					continue;
				}
				// A shared owner may still mutate this profile outside the map
				// lock. Keep it canonical until the map is its only remaining
				// owner.
				if (it->second.use_count () == 1 && it->second->IsExpiredFromCache (ts))
					it = g_Profiles.erase (it);
				else
					it++;
			}
		}
		if (!tmp.empty ()) return std::async (std::launch::async, SaveProfilesToDisk, std::move (tmp));
		return std::future<void> ();
	}

	void SaveProfiles ()
	{
		std::unordered_map<i2p::data::IdentHash, std::shared_ptr<RouterProfile>> tmp;
		{
			std::lock_guard<std::mutex> l (g_ProfilesMutex);
			std::swap (tmp, g_Profiles);
		}
		auto ts = i2p::util::GetSecondsSinceEpoch ();
		for (auto & it : tmp)
			if (it.second->ShouldSaveOnShutdown (ts))
				if (!it.second->Save (it.first)) it.second->MarkPersistenceFailed ();
	}

	/**
	 * @brief Remove expired profile files from the shared hashed storage.
	 *
	 * Traverse() yields every regular file below the store root, including
	 * files that belong to a different store or to a future format. Each
	 * candidate is therefore re-encoded and compared with the canonical
	 * path for that hash, which accepts only a
	 * "profile-&lt;base64&gt;.txt" leaf in its own shard; staging files,
	 * backups, symlink leaves and unrelated regular files are skipped.
	 * Deletion goes through the installed artifact authority, so a
	 * delegated worker owns the removal (direct unlink is only used when
	 * no worker is installed). The whole sweep runs under the storage lock
	 * and is executed on a caller-owned async task.
	 */
	static void DeleteFilesFromDisk ()
	{
		std::lock_guard storageLock (g_ProfilesStorageMutex);
		if (!g_ProfilesStorageInitialized)
		{
			LogPrint (eLogWarning, "Profiling: Storage is not initialized; skipping cleanup");
			return;
		}
		std::vector<std::string> files;
		g_ProfilesStorage.Traverse (files);

		struct stat st;
		std::time_t now = std::time (nullptr);
		for (const auto & path : files)
		{
			// Traverse is shared with other hashed stores and includes unrelated
			// regular files. Expiry owns only canonical profile leaves in their
			// expected shard, never staging files, backups or symlink leaves.
			const std::filesystem::path candidate (path);
			const auto filename = candidate.filename ().string ();
			constexpr std::string_view prefix = "profile-", suffix = ".txt";
			constexpr size_t encodedHashLength = 4 * ((IdentHash::len + 2) / 3);
			if (filename.size () != prefix.size () + encodedHashLength + suffix.size ()
				|| !filename.starts_with (prefix) || !filename.ends_with (suffix))
				continue;
			const auto encoded = std::string_view (filename).substr (prefix.size (), encodedHashLength);
			IdentHash hash;
			if (hash.FromBase64 (encoded) != IdentHash::len || hash.ToBase64 () != encoded
				|| candidate != std::filesystem::path (g_ProfilesStorage.Path (std::string (encoded))))
				continue;
			std::error_code error;
			if (!std::filesystem::is_regular_file (std::filesystem::symlink_status (candidate, error)) || error)
				continue;
			if (stat (path.c_str (), &st) != 0)
			{
				LogPrint (eLogWarning, "Profiling: Can't stat(): ", path);
				continue;
			}
			if (now - st.st_mtime >= PEER_PROFILE_EXPIRATION_TIMEOUT)
			{
				LogPrint (eLogDebug, "Profiling: Removing expired peer profile: ", path);
				// A delegated profile is removed through the same authority that
				// writes it. Falling back to a direct deletion would touch a file
				// the installed backend owns.
				if (i2p::data::IsArtifactStorageInstalled ())
				{
					if (!i2p::data::RemoveArtifact (i2p::data::StorageArtifact::PeerProfile, encoded))
						LogPrint (eLogWarning, "Profiling: Can't remove expired peer profile ", encoded);
				}
				else
					i2p::fs::Remove (path);
			}
		}
	}

	std::future<void> DeleteObsoleteProfiles ()
	{
		{
			std::lock_guard storageLock (g_ProfilesStorageMutex);
			if (!g_ProfilesStorageInitialized)
			{
				LogPrint (eLogWarning, "Profiling: Storage is not initialized; skipping cleanup");
				return std::future<void> ();
			}
		}
		{
			auto ts = i2p::util::GetSecondsSinceEpoch ();
			std::lock_guard<std::mutex> l (g_ProfilesMutex);
			for (auto it = g_Profiles.begin (); it != g_Profiles.end ();)
			{
				// The map lock prevents new lookups; an existing owner keeps the
				// profile canonical until it can no longer be mutated externally.
				if (it->second.use_count () == 1 && it->second->IsExpiredByLifetime (ts))
					it = g_Profiles.erase (it);
				else
					it++;
			}
		}

		return std::async (std::launch::async, DeleteFilesFromDisk);
	}

	bool UpdateRouterProfile (const IdentHash & identHash, std::function<void (std::shared_ptr<RouterProfile>)> update)
	{
		if (!update) return true;
		std::shared_ptr<RouterProfile> profile;
		{
			std::lock_guard<std::mutex> l (g_ProfilesMutex);
			auto it = g_Profiles.find (identHash);
			if (it != g_Profiles.end ()) profile = it->second;
		}
		if (profile)
		{
			update (profile);
			return true;
		}
		// postpone
		std::lock_guard<std::mutex> l (g_PostponedUpdatesMutex);
		g_PostponedUpdates.emplace_back (identHash, update);
		return false;
	}

	static void ApplyPostponedUpdates (
		std::list<std::pair<i2p::data::IdentHash, std::function<void (std::shared_ptr<RouterProfile>)>>> && updates)
	{
		for (const auto & [ident, update] : updates)
		{
			auto profile = GetRouterProfile (ident);
			update (profile);
		}
	}

	/**
	 * @brief Apply and clear all postponed profile updates,
	 *        asynchronously.
	 * @return future of the flush; invalid when the queue was empty.
	 *
	 * The queue is swapped out under its own mutex before any work starts,
	 * so new submissions are accepted while the flush runs and are handled
	 * by the next call. The future must be waited for before the profile
	 * store is released, because the task creates profiles and therefore
	 * touches the storage root.
	 */
	std::future<void> FlushPostponedRouterProfileUpdates ()
	{
		std::list<std::pair<i2p::data::IdentHash, std::function<void (std::shared_ptr<RouterProfile>)>>> updates;
		{
			std::lock_guard<std::mutex> l (g_PostponedUpdatesMutex);
			if (g_PostponedUpdates.empty ()) return std::future<void> ();
			g_PostponedUpdates.swap (updates);
		}
		return std::async (std::launch::async, ApplyPostponedUpdates, std::move (updates));
	}

	void DiscardPostponedRouterProfileUpdates ()
	{
		std::list<std::pair<i2p::data::IdentHash, std::function<void (std::shared_ptr<RouterProfile>)>>> discarded;
		{
			std::lock_guard lock (g_PostponedUpdatesMutex);
			discarded.swap (g_PostponedUpdates);
		}
	}
}
}
