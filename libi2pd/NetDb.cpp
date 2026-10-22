/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: NetDb.
 *
 * Implements the router's network database: validated RouterInfo and LeaseSet
 * storage, lookup, expiry and replacement policy, floodfill DatabaseStore/
 * DatabaseLookup handling, reseed fallback and asynchronous persistence. The
 * in-memory maps are mutex-protected, and Java-compatible clock-skew,
 * lease-age and store-type rules decide freshness and publication.
 *
 */

#include <string.h>
#include <algorithm>
#include <fstream>
#include <vector>
#include <map>
#include <utility>
#include <boost/asio.hpp>
#include <stdexcept>

#include "I2PEndian.h"
#include "Base.h"
#include "Crypto.h"
#include "Log.h"
#include "Timestamp.h"
#include "I2NPProtocol.h"
#include "Tunnel.h"
#include "Transports.h"
#include "NTCP2.h"
#include "RouterContext.h"
#include "Garlic.h"
#include "ECIESX25519AEADRatchetSession.h"
#include "Config.h"
#include "util.h"
#include "IdentMetrics.h"
#include "NetDb.hpp"

using namespace i2p::transport;

namespace i2p
{
namespace data
{
	NetDb netdb;

	namespace
	{
		// KademliaNetworkDatabaseFacade.validate(): earliest <= now - 10m is
		// rejected for LeaseSet1. Router.CLOCK_FUDGE_FACTOR is 60 seconds.
		constexpr uint64_t LEASESET1_MAX_EARLIEST_AGE = 10 * 60 * 1000LL;

		bool IsRouterInfoStoreValid (const RouterInfo & router, uint64_t now)
		{
			if (router.IsUnreachable ()) return false;
			if (!router.HasValidAddresses ())
			{
				// KademliaNetworkDatabaseFacade rejects a zero-address RI only
				// when it is no longer current (54 minutes). A fresh hidden RI
				// or one with only an unknown address style remains storable,
				// but selection paths still require valid addresses.
				constexpr uint64_t JAVA_NO_ADDRESS_GRACE = 54 * 60 * 1000LL;
				if (now >= router.GetTimestamp () + JAVA_NO_ADDRESS_GRACE) return false;
			}
			if (router.IsFloodfill ())
			{
				auto profile = router.GetProfile ();
				if (profile && profile->IsUnreachable ()) return false;
			}
			// Same bounds as the historical insert path: reject entries more
			// than two minutes in the future, and entries older than 27 hours
			// once we have been up long enough to make that judgement.
			if (now + NETDB_EXPIRATION_TIMEOUT_THRESHOLD * 1000LL <= router.GetTimestamp ()) return false;
			if (now >= router.GetTimestamp () + NETDB_MAX_EXPIRATION_TIMEOUT * 1000LL
				&& i2p::context.GetUptime () >= NETDB_CHECK_FOR_EXPIRATION_UPTIME / 10)
				return false;
			return true;
		}

		// LeaseSet1 has no signed published timestamp. Java uses the earliest
		// and latest lease dates for validate() and isNewer(); reproduce those
		// bounds from the validated wire buffer.
		// KademliaNetworkDatabaseFacade.isNewer(): use the signed published
		// timestamp only when both entries are LS2; for any LS1/LS2 mix use the
		// earliest lease date. Encrypted and Meta LS2 have no leases and are
		// represented by 0, matching Java's -1 sentinel for ordering purposes.
		bool IsLeaseSetNewer (const LeaseSet & candidate, const LeaseSet & stored)
		{
			if (candidate.GetStoreType () != NETDB_STORE_TYPE_LEASESET
				&& stored.GetStoreType () != NETDB_STORE_TYPE_LEASESET)
				return candidate.GetPublishedTimestamp () > stored.GetPublishedTimestamp ();
			return candidate.GetEarliestLeaseDate () > stored.GetEarliestLeaseDate ();
		}

		bool IsLeaseSet1Fresh (const LeaseSet & leaseSet, uint64_t now)
		{
			auto identity = leaseSet.GetIdentity ();
			auto buffer = leaseSet.GetBuffer ();
			if (!identity || !buffer) return false;
			size_t offset = identity->GetFullLen () + 256 + identity->GetSigningPublicKeyLen ();
			size_t len = leaseSet.GetBufferLen ();
			if (offset >= len) return false;
			uint8_t numLeases = buffer[offset++];
			if (!numLeases || numLeases > MAX_NUM_LEASES || offset + static_cast<size_t> (numLeases) * LEASE_SIZE > len)
				return false;
			uint64_t earliest = UINT64_MAX, latest = 0;
			for (uint8_t i = 0; i < numLeases; i++)
			{
				auto endDate = bufbe64toh (buffer + offset + static_cast<size_t> (i) * LEASE_SIZE + 36);
				if (endDate < earliest) earliest = endDate;
				if (endDate > latest) latest = endDate;
			}
			// LeaseSet::IsValid() already rejects an empty, all-expired or
			// far-future set. These are the Java-specific store bounds.
			if (now > LEASESET2_CLOCK_FUDGE && latest <= now - LEASESET2_CLOCK_FUDGE) return false;
			if (now > LEASESET1_MAX_EARLIEST_AGE && earliest <= now - LEASESET1_MAX_EARLIEST_AGE) return false;
			if (latest > now + LEASESET_MAX_LEASE_FUTURE + LEASESET2_CLOCK_FUDGE) return false;
			return true;
		}
	}

	bool ParseDatabaseLookupReplyKey (
		const uint8_t * block, size_t len, uint8_t flags, DatabaseLookupReplyKey & replyKey)
	{
		replyKey = DatabaseLookupReplyKey { };
		bool encryptReply = flags & DATABASE_LOOKUP_ENCRYPTION_FLAG;
		bool ratchetReply = flags & DATABASE_LOOKUP_RATCHET_FLAG;
		if (!encryptReply && !ratchetReply) return true;
		if (!block) return false;
		// Proposal 154 / I2NP: bits 4+1 (0x12) select the ECIES derived-key
		// reply. The KDF is TBD, Java 2.14.0 does not implement it, and the
		// block has no tag or count. There is no specified way to encrypt this
		// reply, so fail closed instead of guessing or replying in cleartext.
		if (encryptReply && ratchetReply) return false;
		if (len < 33) return false;
		uint8_t numTags = block[32];
		size_t tagLen = ratchetReply ? 8 : 32;
		if (numTags < 1 || numTags > DATABASE_LOOKUP_MAX_NUM_TAGS || len < 33 + static_cast<size_t> (numTags) * tagLen)
			return false;
		memcpy (replyKey.key.data (), block, replyKey.key.size ());
		memcpy (replyKey.tag.data (), block + 33, tagLen);
		replyKey.mode
			= ratchetReply ? DatabaseLookupReplyKey::Mode::ECIESAEAD : DatabaseLookupReplyKey::Mode::ElGamalAES;
		return true;
	}

	NetDb::NetDb ()
		: m_IsRunning (false)
		, m_AcceptUpdates (true)
		, m_Thread (nullptr)
		, m_Reseeder (nullptr)
		, m_Storage ("netDb", "r", "routerInfo-", "dat")
		, m_PersistProfiles (true)
		, m_NetDbPersistInterval (NETDB_MIN_PERSIST_INTERVAL * 1000LL)
		, m_LastExploratorySelectionUpdateTime (0)
		, m_Rng (i2p::util::GetRngSeed ())
	{
	}

	NetDb::~NetDb ()
	{
		Stop ();
	}

	void NetDb::Start ()
	{
		// A second Start() would destroy a joinable std::thread (and therefore
		// call std::terminate), so treat repeated starts as a no-op. Serialize
		// with Stop() because shutdown may come from another thread.
		std::lock_guard<std::mutex> lifecycle (m_LifecycleMutex);
		if (m_IsRunning.load (std::memory_order_relaxed) || m_Thread) return;

		m_AcceptUpdates.store (false, std::memory_order_relaxed);
		{
			std::lock_guard<std::mutex> storage (m_StorageMutex);
			m_Storage.SetPlace (i2p::fs::GetDataDir ());
			m_Storage.Init (i2p::data::GetBase64SubstitutionTable (), 64);
		}
		InitProfilesStorage ();
		m_Families.LoadCertificates ();
		Load ();

		// Loading is complete; reseed and the request thread may now insert.
		m_AcceptUpdates.store (true, std::memory_order_relaxed);

		if (!GetRequests ())
		{
			auto requests = std::make_shared<NetDbRequests> ();
			{
				std::lock_guard<std::mutex> l (m_RequestsMutex);
				m_Requests = requests;
			}
			requests->Start ();
		}

		uint16_t threshold;
		i2p::config::GetOption ("reseed.threshold", threshold);
		if (GetNumRouters () < threshold
			|| GetNumFloodfills ()
				< NETDB_MIN_FLOODFILLS) // reseed if # of router less than threshold or too few floodfiils
		{
			Reseed ();
		}
		else if (!GetRandomRouter (i2p::context.GetSharedRouterInfo (), false, false, false))
			Reseed (); // we don't have a router we can connect to. Trying to reseed

		// The request thread may already be running. Mutate the maps under the
		// same locks used by lookups and ClearRouterInfos; keep the established
		// router-info -> floodfill lock order.
		{
			auto ownRouter = i2p::context.GetSharedRouterInfo ();
			std::lock_guard<std::mutex> l (m_RouterInfosMutex);
			auto it = m_RouterInfos.find (i2p::context.GetIdentHash ());
			if (it != m_RouterInfos.end ())
			{
				{
					std::lock_guard<std::mutex> lf (m_FloodfillsMutex);
					m_Floodfills.Remove (it->second->GetIdentHash ());
				}
				m_RouterInfos.erase (it);
			}
			m_RouterInfos.emplace (i2p::context.GetIdentHash (), ownRouter);
			if (i2p::context.IsFloodfill ())
			{
				std::lock_guard<std::mutex> lf (m_FloodfillsMutex);
				m_Floodfills.Insert (ownRouter);
			}
		}

		i2p::config::GetOption ("persist.profiles", m_PersistProfiles);

		int persistInterval = 0;
		i2p::config::GetOption ("persist.netdbinterval", persistInterval);
		if (persistInterval < NETDB_MIN_PERSIST_INTERVAL) persistInterval = NETDB_MIN_PERSIST_INTERVAL;
		if (persistInterval > NETDB_MAX_PERSIST_INTERVAL) persistInterval = NETDB_MAX_PERSIST_INTERVAL;
		m_NetDbPersistInterval = persistInterval * 1000LL;

		m_IsRunning.store (true, std::memory_order_relaxed);
		m_Thread = std::make_unique<std::thread> (std::bind (&NetDb::Run, this));
	}

	void NetDb::Stop ()
	{
		std::lock_guard<std::mutex> lifecycle (m_LifecycleMutex);
		// Reject new RouterInfo/LeaseSet insertions before stopping the worker
		// so a handler racing with shutdown cannot populate the maps again.
		m_AcceptUpdates.store (false, std::memory_order_relaxed);
		// Stop and join the worker before touching the request manager or the
		// containers it reads. The old order stopped requests and cleared
		// m_RouterInfos while Run() could still be using them.
		const bool wasRunning = m_IsRunning.exchange (false, std::memory_order_relaxed);
		m_Queue.WakeUp ();
		if (m_Thread)
		{
			if (m_Thread->joinable ()) m_Thread->join ();
			m_Thread.reset ();
		}
		// Messages queued while the worker was stopping cannot be handled any
		// more. Drop them so a later Start() does not process stale stores.
		std::list<std::shared_ptr<const I2NPMessage>> stale;
		m_Queue.GetWholeQueue (stale);

		std::shared_ptr<NetDbRequests> requests;
		{
			std::lock_guard<std::mutex> l (m_RequestsMutex);
			requests.swap (m_Requests);
		}
		if (requests) requests->Stop ();

		// The profile and RouterInfo persistence helpers run on std::async
		// threads. Wait for them before clearing the maps or returning, so a
		// restart cannot Load() while an old PersistRouters() still owns the
		// same buffers and storage paths.
		auto waitFor = [] (std::future<void> & future, const char * name)
		{
			if (!future.valid ()) return;
			try
			{
				future.get ();
			}
			catch (const std::exception & ex)
			{
				LogPrint (eLogError, "NetDb: ", name, " failed: ", ex.what ());
			}
		};
		waitFor (m_SavingProfiles, "profile save");
		waitFor (m_DeletingProfiles, "profile cleanup");
		waitFor (m_ApplyingProfileUpdates, "profile update");
		waitFor (m_PersistingRouters, "RouterInfo persistence");

		if (wasRunning)
		{
			if (m_PersistProfiles) SaveProfiles ();
			auto cleanup = DeleteObsoleteProfiles ();
			waitFor (cleanup, "profile cleanup");
		}
		// Clear even if the worker never reached the running state: Start()
		// may have loaded RouterInfos before a failure or stop request.
		ClearRouterInfos ();
		{
			std::lock_guard<std::mutex> l (m_LeaseSetsMutex);
			m_LeaseSets.clear ();
		}
		{
			// Keep this separate from ClearRouterInfos(): the exploratory path
			// takes m_ExploratorySelectionMutex before m_RouterInfosMutex, so
			// clearing here preserves that lock order.
			std::lock_guard<std::mutex> l (m_ExploratorySelectionMutex);
			m_ExploratorySelection.clear ();
			m_LastExploratorySelectionUpdateTime = 0;
		}
	}

	void NetDb::ResetRemoteRouterInfos ()
	{
		if (!m_AcceptUpdates.load (std::memory_order_relaxed)) return;
		auto own = i2p::context.GetSharedRouterInfo ();
		std::lock_guard<std::mutex> l (m_RouterInfosMutex);
		// Stop() may have flipped the flag while this reseeder waited for the
		// map lock; never repopulate a stopped netDb.
		if (!m_AcceptUpdates.load (std::memory_order_relaxed)) return;
		for (auto it = m_RouterInfos.begin (); it != m_RouterInfos.end ();)
		{
			if (own && it->second == own) ++it;
			else it = m_RouterInfos.erase (it);
		}
		{
			std::lock_guard<std::mutex> lf (m_FloodfillsMutex);
			m_Floodfills.Clear ();
			if (own && i2p::context.IsFloodfill ()
				&& m_RouterInfos.find (own->GetIdentHash ()) != m_RouterInfos.end ())
				m_Floodfills.Insert (own);
		}
	}

	void NetDb::Run ()
	{
		i2p::util::SetThreadName ("NetDB");

		uint64_t lastManage = 0;
		uint64_t lastProfilesCleanup = i2p::util::GetMonotonicMilliseconds (),
				 lastObsoleteProfilesCleanup = lastProfilesCleanup, lastApplyingProfileUpdates = lastProfilesCleanup;
		int16_t profilesCleanupVariance = 0, obsoleteProfilesCleanVariance = 0, applyingProfileUpdatesVariance = 0;

		std::list<std::shared_ptr<const I2NPMessage>> msgs;
		while (m_IsRunning.load (std::memory_order_relaxed))
		{
			try
			{
				if (m_Queue.Wait (1, 0)) // 1 sec
				{
					m_Queue.GetWholeQueue (msgs);
					while (!msgs.empty ())
					{
						auto msg = msgs.front ();
						msgs.pop_front ();
						if (!msg) continue;
						LogPrint (eLogDebug, "NetDb: Got request with type ", (int)msg->GetTypeID ());
						switch (msg->GetTypeID ())
						{
						case eI2NPDatabaseStore: HandleDatabaseStoreMsg (msg); break;
						case eI2NPDatabaseLookup: HandleDatabaseLookupMsg (msg); break;
						default: // WTF?
							LogPrint (eLogError, "NetDb: Unexpected message type ", (int)msg->GetTypeID ());
							//i2p::HandleI2NPMessage (msg);
						}
					}
				}
				if (!m_IsRunning.load (std::memory_order_relaxed)) break;
				if (!i2p::transport::transports.IsOnline () || !i2p::transport::transports.IsRunning ())
					continue; // don't manage netdb when offline or transports are not running

				uint64_t mts = i2p::util::GetMonotonicMilliseconds ();
				if (mts
					>= lastManage + m_NetDbPersistInterval) // manage routers and leasesets every persist.netdbinterval
				{
					if (lastManage)
					{
						ManageRouterInfos ();
						ManageLeaseSets ();
					}
					lastManage = mts;
				}

				if (mts >= lastProfilesCleanup
						+ (uint64_t)(i2p::data::PEER_PROFILE_AUTOCLEAN_TIMEOUT + profilesCleanupVariance) * 1000)
				{
					m_RouterProfilesPool.CleanUpMt ();
					if (m_PersistProfiles)
					{
						bool isSaving = m_SavingProfiles.valid ();
						if (isSaving
							&& m_SavingProfiles.wait_for (std::chrono::seconds (0))
								== std::future_status::ready) // still active?
						{
							m_SavingProfiles.get ();
							isSaving = false;
						}
						if (!isSaving)
							m_SavingProfiles = PersistProfiles ();
						else
							LogPrint (eLogWarning, "NetDb: Can't persist profiles. Profiles are being saved to disk");
					}
					lastProfilesCleanup = mts;
					{
						std::lock_guard<std::mutex> l (m_RngMutex);
						profilesCleanupVariance = m_Rng () % i2p::data::PEER_PROFILE_AUTOCLEAN_VARIANCE;
					}
				}

				if (mts >= lastObsoleteProfilesCleanup
						+ (uint64_t)(i2p::data::PEER_PROFILE_OBSOLETE_PROFILES_CLEAN_TIMEOUT
							  + obsoleteProfilesCleanVariance)
							* 1000)
				{
					bool isDeleting = m_DeletingProfiles.valid ();
					if (isDeleting
						&& m_DeletingProfiles.wait_for (std::chrono::seconds (0))
							== std::future_status::ready) // still active?
					{
						m_DeletingProfiles.get ();
						isDeleting = false;
					}
					if (!isDeleting)
						m_DeletingProfiles = DeleteObsoleteProfiles ();
					else
						LogPrint (eLogWarning, "NetDb: Can't delete profiles. Profiles are being deleted from disk");
					lastObsoleteProfilesCleanup = mts;
					{
						std::lock_guard<std::mutex> l (m_RngMutex);
						obsoleteProfilesCleanVariance
							= m_Rng () % i2p::data::PEER_PROFILE_OBSOLETE_PROFILES_CLEAN_VARIANCE;
					}
				}
				if (mts >= lastApplyingProfileUpdates + i2p::data::PEER_PROFILE_APPLY_POSTPONED_TIMEOUT
						+ applyingProfileUpdatesVariance)
				{
					bool isApplying = m_ApplyingProfileUpdates.valid ();
					if (isApplying
						&& m_ApplyingProfileUpdates.wait_for (std::chrono::seconds (0))
							== std::future_status::ready) // still active?
					{
						m_ApplyingProfileUpdates.get ();
						isApplying = false;
					}
					if (!isApplying) m_ApplyingProfileUpdates = i2p::data::FlushPostponedRouterProfileUpdates ();
					lastApplyingProfileUpdates = mts;
					{
						std::lock_guard<std::mutex> l (m_RngMutex);
						applyingProfileUpdatesVariance
							= m_Rng () % i2p::data::PEER_PROFILE_APPLY_POSTPONED_TIMEOUT_VARIANCE;
					}
				}
			}
			catch (std::exception & ex)
			{
				LogPrint (eLogError, "NetDb: Runtime exception: ", ex.what ());
			}
		}
	}

	std::shared_ptr<const RouterInfo> NetDb::AddRouterInfo (const uint8_t * buf, int len)
	{
		bool updated;
		return AddRouterInfo (buf, len, updated);
	}

	bool NetDb::FloodRouterInfo (std::shared_ptr<const RouterInfo> router)
	{
		if (!router || !m_AcceptUpdates.load (std::memory_order_relaxed)) return false;
		auto ident = router->GetIdentHash ();
		auto msg = CreateDatabaseStoreMsg (std::move (router));
		if (!msg) return false;
		Flood (ident, std::move (msg));
		return true;
	}

	std::shared_ptr<const RouterInfo> NetDb::AddRouterInfo (const uint8_t * buf, int len, bool & updated)
	{
		if (!buf || len <= 0)
		{
			updated = false;
			return nullptr;
		}
		IdentityEx identity;
		if (identity.FromBuffer (buf, len)) return AddRouterInfo (identity.GetIdentHash (), buf, len, updated);
		updated = false;
		return nullptr;
	}

	bool NetDb::AddRouterInfo (const IdentHash & ident, const uint8_t * buf, int len)
	{
		bool updated;
		if (!AddRouterInfo (ident, buf, len, updated)) updated = false;
		return updated;
	}

	std::shared_ptr<const RouterInfo> NetDb::AddRouterInfo (
		const IdentHash & ident, const uint8_t * buf, int len, bool & updated)
	{
		updated = false;
		if (!m_AcceptUpdates.load (std::memory_order_relaxed))
		{
			LogPrint (eLogWarning, "NetDb: RouterInfo rejected because NetDb is stopped: ", ident.ToBase64 ());
			return nullptr;
		}
		if (!buf || len <= 0) return nullptr;
		// The DatabaseStore key must be the hash of the RouterInfo identity.
		// Verify the binding before accepting or updating a cached entry.
		IdentityEx announcedIdentity;
		if (!announcedIdentity.FromBuffer (buf, len) || announcedIdentity.GetIdentHash () != ident)
		{
			LogPrint (eLogWarning, "NetDb: RouterInfo identity does not match store key ", ident.ToBase64 ());
			return nullptr;
		}

		auto now = i2p::util::GetMillisecondsSinceEpoch ();
		auto r = FindRouter (ident);
		if (r && !r->IsNewer (buf, len))
		{
			r->CancelBufferToDelete (); // since an update received
			if (CheckLogLevel (eLogDebug)) LogPrint (eLogDebug, "NetDb: RouterInfo is older: ", ident.ToBase64 ());
			auto requests = GetRequests ();
			if (requests) requests->RequestComplete (ident, r);
			return r;
		}

		// Validate the replacement before changing the stored object. A newer
		// but malformed, unreachable or far-future RouterInfo must not erase
		// the valid entry that is already in the map (Java store(): validate()
		// throws before _ds.put()).
		auto candidate = std::make_shared<RouterInfo> (buf, len);
		if (candidate->GetIdentHash () != ident || !IsRouterInfoStoreValid (*candidate, now))
		{
			LogPrint (eLogWarning, "NetDb: RouterInfo can't be updated ", ident.ToBase64 ());
			if (r)
			{
				auto requests = GetRequests ();
				if (requests) requests->RequestComplete (ident, r);
				return r;
			}
			auto requests = GetRequests ();
			if (requests) requests->RequestComplete (ident, nullptr);
			return nullptr;
		}

		std::shared_ptr<RouterInfo> stored;
		bool wasFloodfill = false;
		{
			std::lock_guard<std::mutex> l (m_RouterInfosMutex);
			auto it = m_RouterInfos.find (ident);
			if (it == m_RouterInfos.end ())
			{
				// The entry was removed while the candidate was being parsed.
				stored = candidate;
				m_RouterInfos.emplace (ident, stored);
				updated = true;
			}
			else if (!it->second)
			{
				stored = candidate;
				it->second = stored;
				updated = true;
			}
			else
			{
				stored = it->second;
				wasFloodfill = stored->IsFloodfill ();
				if (stored->IsNewer (buf, len))
				{
					if (!stored->Update (buf, len))
					{
						// The candidate parsed and verified, so Update() should
						// not fail. If it does, install the validated candidate
						// rather than leaving a half-updated object in the map.
						stored = candidate;
						it->second = stored;
					}
					updated = true;
				}
				else // another thread stored the same or a newer RI first
					stored->CancelBufferToDelete ();
			}
		}

		if (updated)
		{
			if (CheckLogLevel (eLogInfo)) LogPrint (eLogInfo, "NetDb: RouterInfo updated: ", ident.ToBase64 ());
			bool isFloodfill = stored->IsFloodfill ();
			if (wasFloodfill || isFloodfill)
			{
				// Rebuild floodfill membership from the new RI. In particular,
				// a router that still declares 'f' but is no longer eligible
				// must be removed instead of staying selectable forever.
				std::lock_guard<std::mutex> l (m_FloodfillsMutex);
				if (wasFloodfill) m_Floodfills.Remove (ident);
				if (isFloodfill)
				{
					if (stored->IsEligibleFloodfill ())
					{
						if (m_Floodfills.GetSize () < NETDB_NUM_FLOODFILLS_THRESHOLD
							|| stored->GetProfile ()->IsReal ())
							m_Floodfills.Insert (stored);
						else
							stored->ResetFloodfill ();
					}
					else
						stored->ResetFloodfill (); // declared floodfill but not eligible
				}
			}
		}
		// take care about requested destination
		auto requests = GetRequests ();
		if (requests) requests->RequestComplete (ident, stored);
		return stored;
	}

	bool NetDb::AddLeaseSet (const IdentHash & ident, const uint8_t * buf, int len)
	{
		if (!m_AcceptUpdates.load (std::memory_order_relaxed))
		{
			LogPrint (eLogWarning, "NetDb: LeaseSet rejected because NetDb is stopped: ", ident.ToBase32 ());
			return false;
		}
		if (!buf || len <= 0) return false;
		// Build and verify a candidate before touching the cached object. The
		// old path called Update() on the stored LeaseSet and could leave that
		// object invalid (and later erase it) if the replacement was rejected.
		auto leaseSet = std::make_shared<LeaseSet> (buf, len, false); // we don't need leases in netdb
		auto now = i2p::util::GetMillisecondsSinceEpoch ();
		if (!leaseSet->IsValid () || !leaseSet->GetIdentity () || leaseSet->GetIdentity ()->GetIdentHash () != ident
			|| !IsLeaseSet1Fresh (*leaseSet, now))
		{
			LogPrint (eLogError, "NetDb: LeaseSet is invalid or does not match store key: ", ident.ToBase32 ());
			return false;
		}

		std::lock_guard<std::mutex> lock (m_LeaseSetsMutex);
		auto it = m_LeaseSets.find (ident);
		if (it != m_LeaseSets.end () && (!it->second || !IsLeaseSetNewer (*leaseSet, *it->second)))
		{
			// Java isNewer(): LS1 uses the earliest lease date; a valid stored
			// entry is never evicted by an older or replayed replacement.
			if (CheckLogLevel (eLogDebug)) LogPrint (eLogDebug, "NetDb: LeaseSet is older: ", ident.ToBase32 ());
			return false;
		}
		if (CheckLogLevel (eLogInfo)) LogPrint (eLogInfo, "NetDb: LeaseSet added: ", ident.ToBase32 ());
		m_LeaseSets[ident] = leaseSet;
		return true;
	}

	bool NetDb::AddLeaseSet2 (const IdentHash & ident, const uint8_t * buf, int len, uint8_t storeType)
	{
		if (!m_AcceptUpdates.load (std::memory_order_relaxed))
		{
			LogPrint (eLogWarning, "NetDb: LeaseSet2 rejected because NetDb is stopped: ", ident.ToBase32 ());
			return false;
		}
		if (!buf || len <= 0) return false;
		if (storeType != NETDB_STORE_TYPE_STANDARD_LEASESET2 && storeType != NETDB_STORE_TYPE_ENCRYPTED_LEASESET2
			&& storeType != NETDB_STORE_TYPE_META_LEASESET2)
		{
			LogPrint (eLogWarning, "NetDb: Unsupported LeaseSet2 store type ", (int)storeType);
			return false;
		}
		auto leaseSet = std::make_shared<LeaseSet2> (storeType, buf, len, false); // we don't need leases in netdb
		if (leaseSet->IsValid ())
		{
			// Standard LS2 is indexed by the destination hash; encrypted LS2 by
			// SHA256(blinded sig type || blinded public key). Never accept a
			// valid entry under the wrong store key.
			if (!leaseSet->IsStoreKey (ident))
			{
				LogPrint (eLogError, "NetDb: LeaseSet2 does not match store key: ", ident.ToBase32 ());
				return false;
			}
			if (storeType == NETDB_STORE_TYPE_ENCRYPTED_LEASESET2)
			{
				// A floodfill cannot decrypt type 5, so apply the outer-header
				// freshness bounds from KademliaNetworkDatabaseFacade.validate():
				// earliest = published, latest = expires. Without this an
				// expired/replayed encrypted LS2 with a fresh published timestamp
				// or a far-future expiry could remain stored.
				auto now = i2p::util::GetMillisecondsSinceEpoch ();
				auto published = leaseSet->GetPublishedTimestamp () * 1000LL;
				auto expires = leaseSet->GetExpirationTime ();
				if (published + LEASESET2_META_MAX_PUBLISHED_AGE <= now || expires <= now - LEASESET2_CLOCK_FUDGE
					|| expires > now + LEASESET_MAX_LEASE_FUTURE + LEASESET2_CLOCK_FUDGE)
				{
					LogPrint (eLogWarning,
						"NetDb: Encrypted LeaseSet2 is expired, stale or too far in the future: ", ident.ToBase32 ());
					return false;
				}
			}
			std::lock_guard<std::mutex> lock (m_LeaseSetsMutex);
			auto it = m_LeaseSets.find (ident);
			bool replace = it == m_LeaseSets.end () || !it->second;
			if (!replace)
			{
				// Java isNewer(): two LS2 entries compare the signed published
				// timestamp; an LS1/LS2 mix compares the earliest lease date.
				replace = IsLeaseSetNewer (*leaseSet, *it->second);
			}
			if (replace)
			{
				if (leaseSet->IsPublic () && !leaseSet->IsExpired ())
				{
					// TODO: implement actual update
					if (CheckLogLevel (eLogInfo)) LogPrint (eLogInfo, "NetDb: LeaseSet2 updated: ", ident.ToBase32 ());
					m_LeaseSets[ident] = leaseSet;
					return true;
				}
				else
				{
					// Do not evict a currently valid entry because a replay or an
					// expired replacement arrived.
					LogPrint (
						eLogWarning, "NetDb: Unpublished or expired or future LeaseSet2 received: ", ident.ToBase32 ());
				}
			}
		}
		else
			LogPrint (eLogError, "NetDb: New LeaseSet2 validation failed: ", ident.ToBase32 ());
		return false;
	}

	std::shared_ptr<RouterInfo> NetDb::FindRouter (const IdentHash & ident) const
	{
		std::lock_guard<std::mutex> l (m_RouterInfosMutex);
		auto it = m_RouterInfos.find (ident);
		if (it != m_RouterInfos.end ())
			return it->second;
		else
			return nullptr;
	}

	std::shared_ptr<LeaseSet> NetDb::FindLeaseSet (const IdentHash & destination) const
	{
		std::lock_guard<std::mutex> lock (m_LeaseSetsMutex);
		auto it = m_LeaseSets.find (destination);
		if (it != m_LeaseSets.end ())
			return it->second;
		else
			return nullptr;
	}

	std::shared_ptr<RouterProfile> NetDb::FindRouterProfile (const IdentHash & ident) const
	{
		if (!m_PersistProfiles) return nullptr;

		auto router = FindRouter (ident);
		return router ? router->GetProfile () : nullptr;
	}

	void NetDb::SetUnreachable (const IdentHash & ident, bool unreachable)
	{
		auto r = FindRouter (ident);
		if (r)
		{
			r->SetUnreachable (unreachable);
			auto profile = r->GetProfile ();
			if (profile)
			{
				profile->Unreachable (unreachable);
				if (!unreachable && r->IsDeclaredFloodfill () && !r->IsFloodfill () && r->IsEligibleFloodfill ()
					&& profile->IsReal ())
				{
					// enable previously disabled floodfill
					r->SetFloodfill ();
					std::lock_guard<std::mutex> l (m_FloodfillsMutex);
					m_Floodfills.Insert (r);
				}
			}
		}
	}

	void NetDb::ExcludeReachableTransports (const IdentHash & ident, RouterInfo::CompatibleTransports compatibleTransports)
	{
		auto r = FindRouter (ident);
		if (r)
		{
			std::lock_guard<std::mutex> l (m_RouterInfosMutex);
			r->ExcludeReachableTransports (compatibleTransports);
		}
	}

	void NetDb::Reseed ()
	{
		// Reseed can be requested by the NetDb thread and by I2PControl at
		// the same time. Never build or use m_Reseeder concurrently; a
		// second request is simply ignored while one is in progress.
		std::unique_lock<std::mutex> lock (m_ReseedMutex, std::try_to_lock);
		if (!lock.owns_lock ()) return;
		if (m_IsReseeding.exchange (true)) return;

		struct ReseedGuard
		{
			std::atomic<bool> & flag;
			~ReseedGuard () { flag.store (false); }
		} guard { m_IsReseeding };

		if (!m_Reseeder)
		{
			m_Reseeder = std::make_unique<Reseeder> ();
			m_Reseeder->LoadCertificates (); // we need certificates for SU3 verification
		}

		m_Reseeder->Bootstrap ();
	}

	void NetDb::ReseedFromFloodfill (const RouterInfo & ri, int numRouters, int numFloodfills)
	{
		LogPrint (eLogInfo, "NetDB: Reseeding from floodfill ", ri.GetIdentHashBase64 ());
		std::list<std::shared_ptr<i2p::I2NPMessage>> requests;

		i2p::data::IdentHash ourIdent = i2p::context.GetIdentHash ();
		i2p::data::IdentHash ih = ri.GetIdentHash ();
		i2p::data::IdentHash randomIdent;

		// make floodfill lookups
		while (numFloodfills > 0)
		{
			randomIdent.Randomize ();
			auto msg = i2p::CreateRouterInfoDatabaseLookupMsg (randomIdent, ourIdent, 0, false);
			requests.push_back (msg);
			numFloodfills--;
		}

		// make regular router lookups
		while (numRouters > 0)
		{
			randomIdent.Randomize ();
			auto msg = i2p::CreateRouterInfoDatabaseLookupMsg (randomIdent, ourIdent, 0, true);
			requests.push_back (msg);
			numRouters--;
		}

		// send them off
		i2p::transport::transports.SendMessages (ih, std::move (requests));
	}

	bool NetDb::LoadRouterInfo (const std::string & path, uint64_t ts)
	{
		auto r = std::make_shared<RouterInfo> (path);
		constexpr uint64_t JAVA_NO_ADDRESS_GRACE = 54 * 60 * 1000LL;
		const bool addressesOk = r->HasValidAddresses () || ts < r->GetTimestamp () + JAVA_NO_ADDRESS_GRACE;
		if (r->GetRouterIdentity () && !r->IsUnreachable () && addressesOk
			&& ts < r->GetTimestamp () + 24 * 60 * 60 * NETDB_MAX_OFFLINE_EXPIRATION_TIMEOUT * 1000LL // too old
			&& ts + NETDB_EXPIRATION_TIMEOUT_THRESHOLD * 1000LL > r->GetTimestamp () // from future
			&& (r->GetVersion () >= NETDB_MIN_ALLOWED_VERSION || r->IsHighBandwidth ())) // old version
		{
			r->DeleteBuffer ();
			bool inserted = false;
			{
				std::lock_guard<std::mutex> l (m_RouterInfosMutex);
				inserted = m_RouterInfos.emplace (r->GetIdentHash (), r).second;
			}
			if (inserted)
			{
				if (r->IsFloodfill () && r->IsEligibleFloodfill ())
				{
					std::lock_guard<std::mutex> l (m_FloodfillsMutex);
					m_Floodfills.Insert (r);
				}
			}
		}
		else
		{
			LogPrint (eLogWarning, "NetDb: RI from ", path, " is invalid, from the future or too old. Delete");
			i2p::fs::Remove (path);
		}
		return true;
	}

	std::vector<std::shared_ptr<const RouterInfo>> NetDb::GetRouterInfosSnapshot (size_t max) const
	{
		std::vector<std::shared_ptr<const RouterInfo>> routers;
		if (!max) return routers;
		std::lock_guard<std::mutex> lock (m_RouterInfosMutex);
		routers.reserve (std::min (max, m_RouterInfos.size ()));
		for (const auto & [ident, router] : m_RouterInfos)
		{
			(void)ident;
			if (routers.size () >= max) break;
			if (router) routers.push_back (router);
		}
		return routers;
	}

	std::vector<std::pair<IdentHash, std::shared_ptr<LeaseSet>>> NetDb::GetLeaseSetsSnapshot (size_t max) const
	{
		std::vector<std::pair<IdentHash, std::shared_ptr<LeaseSet>>> leaseSets;
		if (!max) return leaseSets;
		std::lock_guard<std::mutex> lock (m_LeaseSetsMutex);
		leaseSets.reserve (std::min (max, m_LeaseSets.size ()));
		for (const auto & [ident, leaseSet] : m_LeaseSets)
		{
			if (leaseSets.size () >= max) break;
			if (leaseSet) leaseSets.emplace_back (ident, leaseSet);
		}
		return leaseSets;
	}

	void NetDb::VisitLeaseSets (LeaseSetVisitor v)
	{
		// Callbacks may call back into NetDb. Snapshot under the lock and run
		// the visitor without holding a container mutex.
		std::vector<std::pair<IdentHash, std::shared_ptr<LeaseSet>>> leaseSets;
		{
			std::lock_guard<std::mutex> lock (m_LeaseSetsMutex);
			leaseSets.reserve (m_LeaseSets.size ());
			for (const auto & entry : m_LeaseSets) leaseSets.emplace_back (entry.first, entry.second);
		}
		for (const auto & entry : leaseSets) v (entry.first, entry.second);
	}

	void NetDb::VisitStoredRouterInfos (RouterInfoVisitor v)
	{
		// HashedStorage::Iterate is not synchronized and invokes the visitor
		// synchronously. Snapshot the file list under m_StorageMutex and run
		// the (potentially re-entrant) visitor outside it.
		std::vector<std::string> files;
		{
			std::lock_guard<std::mutex> storage (m_StorageMutex);
			m_Storage.Traverse (files);
		}
		for (const auto & filename : files)
		{
			auto ri = std::make_shared<i2p::data::RouterInfo> (filename);
			v (ri);
		}
	}

	void NetDb::VisitRouterInfos (RouterInfoVisitor v)
	{
		// As VisitLeaseSets(): do not hold m_RouterInfosMutex across arbitrary
		// visitor code, which may insert, erase or call FindRouter().
		std::vector<std::shared_ptr<const RouterInfo>> routers;
		{
			std::lock_guard<std::mutex> lock (m_RouterInfosMutex);
			routers.reserve (m_RouterInfos.size ());
			for (const auto & item : m_RouterInfos) routers.push_back (item.second);
		}
		for (const auto & router : routers) v (router);
	}

	size_t NetDb::VisitRandomRouterInfos (RouterInfoFilter filter, RouterInfoVisitor v, size_t n)
	{
		std::vector<std::shared_ptr<const RouterInfo>> found;
		const size_t max_iters_per_cyle = 3;
		size_t iters = max_iters_per_cyle;
		while (n > 0)
		{
			uint32_t randomValue;
			{
				std::lock_guard<std::mutex> rng (m_RngMutex);
				randomValue = m_Rng ();
			}
			std::lock_guard<std::mutex> lock (m_RouterInfosMutex);
			if (m_RouterInfos.empty ()) break;
			uint32_t idx = randomValue % m_RouterInfos.size ();
			uint32_t i = 0;
			for (const auto & it : m_RouterInfos)
			{
				if (i >= idx) // are we at the random start point?
				{
					// yes, check if we want this one
					if (filter (it.second))
					{
						// we have a match
						--n;
						found.push_back (it.second);
						// reset max iterations per cycle
						iters = max_iters_per_cyle;
						break;
					}
				}
				else // not there yet
					++i;
			}
			// we have enough
			if (n == 0) break;
			--iters;
			// have we tried enough this cycle ?
			if (!iters)
			{
				// yes let's try the next cycle
				--n;
				iters = max_iters_per_cyle;
			}
		}
		// visit the ones we found
		size_t visited = 0;
		for (const auto & ri : found)
		{
			v (ri);
			++visited;
		}
		return visited;
	}

	void NetDb::Load ()
	{
		// make sure we cleanup netDb from previous attempts
		{
			std::lock_guard<std::mutex> l (m_RouterInfosMutex);
			m_RouterInfos.clear ();
		}
		{
			std::lock_guard<std::mutex> l (m_FloodfillsMutex);
			m_Floodfills.Clear ();
		}

		uint64_t ts = i2p::util::GetMillisecondsSinceEpoch ();
		std::vector<std::string> files;
		{
			std::lock_guard<std::mutex> storage (m_StorageMutex);
			m_Storage.Traverse (files);
		}
		for (const auto & path : files) LoadRouterInfo (path, ts);

		LogPrint (eLogInfo, "NetDb: ", GetNumRouters (), " routers loaded (", GetNumFloodfills (), " floodfils)");
	}

	void NetDb::SaveUpdated ()
	{
		if (m_PersistingRouters.valid ())
		{
			if (m_PersistingRouters.wait_for (std::chrono::seconds (0)) == std::future_status::ready)
				m_PersistingRouters.get ();
			else
			{
				LogPrint (eLogWarning, "NetDb: Can't save updated routers. Routers are being saved to disk");
				return;
			}
		}

		int updatedCount = 0, deletedCount = 0, deletedFloodfillsCount = 0;
		// SaveUpdated runs outside the transport threads. Snapshot the map under
		// the same mutex used by insert/erase so iteration cannot be invalidated.
		std::vector<std::pair<IdentHash, std::shared_ptr<RouterInfo>>> routers;
		{
			std::lock_guard<std::mutex> l (m_RouterInfosMutex);
			routers.reserve (m_RouterInfos.size ());
			for (const auto & [ident, router] : m_RouterInfos) routers.emplace_back (ident, router);
		}
		auto total = routers.size ();
		size_t totalFloodfills = 0;
		{
			std::lock_guard<std::mutex> l (m_FloodfillsMutex);
			totalFloodfills = m_Floodfills.GetSize ();
		}
		uint64_t expirationTimeout = NETDB_MAX_EXPIRATION_TIMEOUT * 1000LL;
		uint64_t ts = i2p::util::GetMillisecondsSinceEpoch ();
		auto uptime = i2p::context.GetUptime ();
		double minTunnelCreationSuccessRate;
		i2p::config::GetOption ("limits.zombies", minTunnelCreationSuccessRate);
		bool isLowRate = i2p::tunnel::tunnels.GetPreciseTunnelCreationSuccessRate () < minTunnelCreationSuccessRate;
		// routers don't expire if less than 90 or uptime is less than 1 hour
		bool checkForExpiration = total > NETDB_MIN_ROUTERS && uptime > NETDB_CHECK_FOR_EXPIRATION_UPTIME; // 10 minutes
		if (checkForExpiration && uptime > i2p::transport::SSU2_TO_INTRODUCER_SESSION_DURATION) // 1 hour
			expirationTimeout = i2p::context.IsFloodfill () ? NETDB_FLOODFILL_EXPIRATION_TIMEOUT * 1000LL
															: NETDB_MIN_EXPIRATION_TIMEOUT * 1000LL
					+ (NETDB_MAX_EXPIRATION_TIMEOUT - NETDB_MIN_EXPIRATION_TIMEOUT) * 1000LL * NETDB_MIN_ROUTERS
						/ total;
		bool isOffline = checkForExpiration
			&& i2p::transport::transports.GetNumPeers ()
				< NETDB_MIN_TRANSPORTS; // enough routers and uptime, but no transports

		std::list<std::pair<std::string, std::shared_ptr<RouterInfo::Buffer>>> saveToDisk;
		std::list<std::string> removeFromDisk;

		auto own = i2p::context.GetSharedRouterInfo ();
		for (const auto & [ident, r] : routers)
		{
			if (!r || r == own) continue; // skip own
			std::shared_ptr<RouterInfo::Buffer> buffer;
			bool deleted = false;
			const bool updated = r->TakeUpdatedBuffer (buffer, deleted);
			if (updated)
			{
				if (buffer && !r->IsUnreachable ())
				{
					// we have something to save
					if (!i2p::transport::transports.IsConnected (ident)) r->ScheduleBufferToDelete ();
					saveToDisk.emplace_back (ident.ToBase64 (), buffer);
				}
				updatedCount++;
				continue;
			}
			else if (r->GetBuffer () && ts > r->GetTimestamp () + NETDB_MIN_EXPIRATION_TIMEOUT * 1000LL)
				// since update was long time ago we assume that router is not connected anymore
				r->ScheduleBufferToDelete ();

			if (r->HasProfile () && r->GetProfile ()->IsUnreachable ()) r->SetUnreachable (true);
			// make router reachable back if too few routers or floodfills
			if (r->IsUnreachable ()
				&& (total - deletedCount < NETDB_MIN_ROUTERS || isLowRate || isOffline
					|| (r->IsFloodfill () && totalFloodfills - deletedFloodfillsCount < NETDB_MIN_FLOODFILLS)))
				r->SetUnreachable (false);
			if (!r->IsUnreachable ())
			{
				// find & mark expired routers
				if (!r->GetCompatibleTransports (true)) // non reachable by any transport
					r->SetUnreachable (true);
				else if (r->GetVersion () < NETDB_MIN_ALLOWED_VERSION && !r->IsHighBandwidth ()) // too old
					r->SetUnreachable (true);
				else if (ts + NETDB_EXPIRATION_TIMEOUT_THRESHOLD * 1000LL < r->GetTimestamp ())
				{
					LogPrint (eLogWarning, "NetDb: RouterInfo is from future for ", (r->GetTimestamp () - ts) / 1000LL,
						" seconds");
					r->SetUnreachable (true);
				}
				else if (checkForExpiration)
				{
					if (ts > r->GetTimestamp () + expirationTimeout)
						r->SetUnreachable (true);
					else if ((ts > r->GetTimestamp () + expirationTimeout / 2) && // more than half of expiration
						total > NETDB_NUM_ROUTERS_THRESHOLD && !r->IsHighBandwidth () && // low bandwidth
						!r->IsFloodfill ()
						&& (!i2p::context.IsFloodfill () || // non floodfill
							(CreateRoutingKey (ident) ^ i2p::context.GetIdentHash ()).metric[0]
								>= 0x02)) // different first 7 bits
						r->SetUnreachable (true);
				}
			}
			// make router reachable back if connected now or trusted router
			if (r->IsUnreachable ()
				&& (i2p::transport::transports.IsConnected (ident)
					|| i2p::transport::transports.IsTrustedRouter (ident)))
				r->SetUnreachable (false);

			if (r->IsUnreachable ())
			{
				if (r->IsFloodfill ()) deletedFloodfillsCount++;
				// delete RI file
				removeFromDisk.emplace_back (ident.ToBase64 ());
				deletedCount++;
				if (total - deletedCount < NETDB_MIN_ROUTERS) checkForExpiration = false;
			}
		} // m_RouterInfos iteration

		if (!saveToDisk.empty () || !removeFromDisk.empty ())
		{
			m_PersistingRouters = std::async (
				std::launch::async, &NetDb::PersistRouters, this, std::move (saveToDisk), std::move (removeFromDisk));
		}

		m_RouterInfoBuffersPool.CleanUpMt ();
		m_RouterInfoAddressesPool.CleanUpMt ();
		m_RouterInfoAddressVectorsPool.CleanUpMt ();
		m_IdentitiesPool.CleanUpMt ();

		if (updatedCount > 0) LogPrint (eLogInfo, "NetDb: Saved ", updatedCount, " new/updated routers");
		if (deletedCount > 0)
		{
			LogPrint (eLogInfo, "NetDb: Deleting ", deletedCount, " unreachable routers");
			// clean up RouterInfos table
			{
				std::lock_guard<std::mutex> l (m_RouterInfosMutex);
				for (auto it = m_RouterInfos.begin (); it != m_RouterInfos.end ();)
				{
					if (!it->second || it->second->IsUnreachable ())
						it = m_RouterInfos.erase (it);
					else
					{
						it->second->DropProfile ();
						it++;
					}
				}
			}
			// clean up expired floodfills or not floodfills anymore
			{
				std::lock_guard<std::mutex> l (m_FloodfillsMutex);
				m_Floodfills.Cleanup ([] (const std::shared_ptr<RouterInfo> & r) -> bool
					{ return r && r->IsFloodfill () && !r->IsUnreachable (); });
			}
		}
	}

	void NetDb::PersistRouters (std::list<std::pair<std::string, std::shared_ptr<RouterInfo::Buffer>>> && update,
		std::list<std::string> && remove)
	{
		std::vector<std::pair<std::string, std::shared_ptr<RouterInfo::Buffer>>> saves;
		std::vector<std::string> removals;
		{
			std::lock_guard<std::mutex> storage (m_StorageMutex);
			saves.reserve (update.size ());
			for (const auto & it : update) saves.emplace_back (m_Storage.Path (it.first), it.second);
			removals.reserve (remove.size ());
			for (const auto & it : remove) removals.push_back (it);
		}
		for (const auto & it : saves)
			if (!RouterInfo::SaveToFile (it.first, it.second))
				LogPrint (eLogError, "NetDb: Failed to persist RouterInfo ", it.first);
		for (const auto & it : removals)
		{
			std::lock_guard<std::mutex> storage (m_StorageMutex);
			m_Storage.Remove (it);
		}
	}

	void NetDb::RequestDestination (
		const IdentHash & destination, RequestedDestination::RequestComplete requestComplete, bool direct)
	{
		if (direct && (i2p::transport::transports.RoutesRestricted () || i2p::context.IsLimitedConnectivity ()))
			direct = false; // always use tunnels for restricted routes or limited connectivity
		auto requests = GetRequests ();
		if (requests)
			requests->PostRequestDestination (destination, requestComplete, direct);
		else
			LogPrint (eLogError, "NetDb: Requests is null");
	}

	void NetDb::HandleDatabaseStoreMsg (std::shared_ptr<const I2NPMessage> m)
	{
		const uint8_t * buf = m->GetPayload ();
		size_t len = m->GetPayloadLength ();
		if (len < DATABASE_STORE_HEADER_SIZE)
		{
			LogPrint (eLogError, "NetDb: Database store msg is too short ", len, ". Dropped");
			return;
		}
		IdentHash ident (buf + DATABASE_STORE_KEY_OFFSET);
		if (ident.IsZero ())
		{
			LogPrint (eLogDebug, "NetDb: Database store with zero ident, dropped");
			return;
		}
		uint32_t replyToken = bufbe32toh (buf + DATABASE_STORE_REPLY_TOKEN_OFFSET);
		if (!IsTunnelDatabaseStoreAccepted (m->from != nullptr || m->fromClove, replyToken))
		{
			LogPrint (eLogDebug, "NetDb: Database store with reply token arrived through a tunnel, dropped");
			return;
		}
		size_t offset = DATABASE_STORE_HEADER_SIZE;
		if (replyToken)
		{
			if (len < offset + 36) // 32 + 4
			{
				LogPrint (eLogError, "NetDb: Database store msg with reply token is too short ", len, ". Dropped");
				return;
			}
			uint32_t tunnelID = bufbe32toh (buf + offset);
			offset += 4;
			if (replyToken != 0xFFFFFFFFU) // if not caught on OBEP or IBGW
			{
				IdentHash replyIdent (buf + offset);
				if (replyIdent.IsZero ())
				{
					LogPrint (eLogWarning, "NetDb: Database store reply hash is zero. Dropped");
					return;
				}
				auto deliveryStatus = CreateDeliveryStatusMsg (replyToken);
				if (!tunnelID) // send response directly
					transports.SendMessage (replyIdent, deliveryStatus);
				else
				{
					bool direct = true;
					if (!i2p::transport::transports.IsConnected (replyIdent))
					{
						auto r = FindRouter (replyIdent);
						if (r && !r->IsReachableFrom (i2p::context.GetRouterInfo ())) direct = false;
					}
					if (direct) // send response directly to IBGW
						transports.SendMessage (replyIdent, i2p::CreateTunnelGatewayMsg (tunnelID, deliveryStatus));
					else
					{
						// send response through exploratory tunnel
						auto pool = i2p::tunnel::tunnels.GetExploratoryPool ();
						auto outbound = pool ? pool->GetNextOutboundTunnel () : nullptr;
						if (outbound)
							outbound->SendTunnelDataMsgTo (replyIdent, tunnelID, deliveryStatus);
						else
							LogPrint (eLogWarning, "NetDb: No outbound tunnels for DatabaseStore reply found");
					}
				}
			}
			offset += 32;
		}
		// we must send reply back before this check
		if (ident == i2p::context.GetIdentHash ())
		{
			LogPrint (eLogDebug, "NetDb: Database store with own RouterInfo received, dropped");
			return;
		}
		size_t payloadOffset = offset;

		bool updated = false;
		// Java ignores the upper four bits of the DatabaseStore type byte.
		// Use the masked type for dispatch, entry construction and the
		// Flood copy so LS2 signature verification remains bound to the
		// canonical type (3/5/7).
		uint8_t storeType = buf[DATABASE_STORE_TYPE_OFFSET] & DATABASE_STORE_TYPE_MASK;
		if (storeType) // LeaseSet or LeaseSet2
		{
			// len is capped by MAX_LS_BUFFER_SIZE here, so the fits-in-int
			// lengths below cannot overflow.
			if (len > MAX_LS_BUFFER_SIZE + offset)
			{
				LogPrint (eLogError, "NetDb: Database store message is too long ", len);
				return;
			}
			if (!context.IsFloodfill ())
			{
				LogPrint (eLogInfo, "NetDb: Not Floodfill, LeaseSet store request ignored for ", ident.ToBase32 ());
				return;
			}
			else if (!m->from) // unsolicited LS must be received directly
			{
				if (storeType == NETDB_STORE_TYPE_LEASESET) // 1
				{
					if (CheckLogLevel (eLogDebug))
						LogPrint (eLogDebug, "NetDb: Store request: LeaseSet for ", ident.ToBase32 ());
					updated = AddLeaseSet (ident, buf + offset, static_cast<int> (len - offset));
				}
				else // all others are considered as LeaseSet2
				{
					if (CheckLogLevel (eLogDebug))
						LogPrint (eLogDebug, "NetDb: Store request: LeaseSet2 of type ", int (storeType), " for ",
							ident.ToBase32 ());
					updated = AddLeaseSet2 (
						ident, buf + offset, static_cast<int> (len - offset), storeType);
				}
			}
		}
		else // RouterInfo
		{
			if (CheckLogLevel (eLogDebug)) LogPrint (eLogDebug, "NetDb: Store request: RouterInfo ", ident.ToBase64 ());
			if (offset + 2 > len)
			{
				LogPrint (eLogError, "NetDb: Database store msg is too short for RouterInfo size ", len);
				return;
			}
			size_t size = bufbe16toh (buf + offset);
			offset += 2;
			if (size > MAX_RI_BUFFER_SIZE || size > len - offset)
			{
				LogPrint (eLogError, "NetDb: Invalid RouterInfo length ", (int)size);
				return;
			}
			uint8_t uncompressed[MAX_RI_BUFFER_SIZE];
			size_t uncompressedSize = m_Inflator.Inflate (buf + offset, size, uncompressed, MAX_RI_BUFFER_SIZE);
			if (uncompressedSize && uncompressedSize <= MAX_RI_BUFFER_SIZE)
				updated = AddRouterInfo (ident, uncompressed, static_cast<int> (uncompressedSize));
			else
			{
				LogPrint (eLogInfo, "NetDb: Decompression failed ", uncompressedSize);
				return;
			}
		}

		if (replyToken && context.IsFloodfill () && updated && ShouldThrottleFlood (ident))
		{
			LogPrint (eLogDebug, "NetDb: DatabaseStore flood throttled for ", ident.ToBase64 ());
		}
		else if (replyToken && context.IsFloodfill () && updated)
		{
			// flood updated. Size the message for the actual entry: a valid
			// Meta/LS2 may be close to MAX_LS_BUFFER_SIZE and not fit the
			// fixed short-message buffer together with the DSM header.
			size_t msgLen = len - payloadOffset;
			auto floodMsg = NewI2NPMessage (DATABASE_STORE_HEADER_SIZE + msgLen);
			uint8_t * payload = floodMsg->GetPayload ();
			memcpy (payload, buf, 33); // key + type
			payload[DATABASE_STORE_TYPE_OFFSET] = storeType; // drop future type bits
			htobe32buf (payload + DATABASE_STORE_REPLY_TOKEN_OFFSET, 0); // zero reply token
			floodMsg->len += DATABASE_STORE_HEADER_SIZE + msgLen;
			if (floodMsg->len <= floodMsg->maxLen)
			{
				memcpy (payload + DATABASE_STORE_HEADER_SIZE, buf + payloadOffset, msgLen);
				floodMsg->FillI2NPMessageHeader (eI2NPDatabaseStore);
				int minutesBeforeMidnight = 24 * 60 - i2p::util::GetMinutesSinceEpoch () % (24 * 60);
				bool andNextDay = storeType ? minutesBeforeMidnight < NETDB_NEXT_DAY_LEASESET_THRESHOLD
											: minutesBeforeMidnight < NETDB_NEXT_DAY_ROUTER_INFO_THRESHOLD;
				Flood (ident, floodMsg, andNextDay);
			}
			else
				LogPrint (eLogError, "NetDb: Database store message is too long ", floodMsg->len);
		}
	}

	bool NetDb::ShouldThrottleLookup (const IdentHash & from, uint32_t tunnelID)
	{
		constexpr uint64_t window = 120; // seconds
		constexpr uint32_t limit = 14; // Java floodfill limit
		constexpr size_t maximum = 4096;
		const auto now = i2p::util::GetSecondsSinceEpoch ();
		std::lock_guard<std::mutex> l (m_ThrottleMutex);
		auto key = std::make_pair (from, tunnelID);
		auto it = m_LookupThrottle.find (key);
		if (it == m_LookupThrottle.end ())
		{
			if (m_LookupThrottle.size () >= maximum) m_LookupThrottle.erase (m_LookupThrottle.begin ());
			m_LookupThrottle.emplace (key, std::make_pair (now, 1));
			return false;
		}
		if (it->second.first + window <= now)
		{
			it->second = std::make_pair (now, 1);
			return false;
		}
		if (it->second.second >= limit) return true;
		++it->second.second;
		return false;
	}

	bool NetDb::ShouldThrottleFlood (const IdentHash & ident)
	{
		constexpr uint64_t window = 90; // seconds
		constexpr uint32_t limit = 3;
		constexpr size_t maximum = 4096;
		const auto now = i2p::util::GetSecondsSinceEpoch ();
		std::lock_guard<std::mutex> l (m_ThrottleMutex);
		auto it = m_FloodThrottle.find (ident);
		if (it == m_FloodThrottle.end ())
		{
			if (m_FloodThrottle.size () >= maximum) m_FloodThrottle.erase (m_FloodThrottle.begin ());
			m_FloodThrottle.emplace (ident, std::make_pair (now, 1));
			return false;
		}
		if (it->second.first + window <= now)
		{
			it->second = std::make_pair (now, 1);
			return false;
		}
		if (it->second.second >= limit) return true;
		++it->second.second;
		return false;
	}

	void NetDb::HandleDatabaseLookupMsg (std::shared_ptr<const I2NPMessage> msg)
	{
		const uint8_t * buf = msg->GetPayload ();
		if (msg->GetPayloadLength () < 65)
		{
			LogPrint (eLogWarning, "NetDb: DatabaseLookup message is too short ", msg->GetPayloadLength ());
			return;
		}
		IdentHash ident (buf);
		if (ident.IsZero ())
		{
			LogPrint (eLogError, "NetDb: DatabaseLookup for zero ident. Ignored");
			return;
		}
		std::string key;
		if (CheckLogLevel (eLogInfo)) key = i2p::data::ByteStreamToBase64 (buf, 32);

		IdentHash replyIdent (buf + 32);
		uint8_t flag = buf[64];

		LogPrint (eLogDebug, "NetDb: DatabaseLookup for ", key, " received flags=", (int)flag);
		uint8_t lookupType = flag & DATABASE_LOOKUP_TYPE_FLAGS_MASK;
		size_t minLen = 65 + ((flag & DATABASE_LOOKUP_DELIVERY_FLAG) ? 4 : 0) + 2;
		if (msg->GetPayloadLength () < minLen)
		{
			LogPrint (eLogWarning, "NetDb: DatabaseLookup message is too short for its flags");
			return;
		}
		const uint8_t * excluded = buf + 65;
		uint32_t replyTunnelID = 0;
		if (flag & DATABASE_LOOKUP_DELIVERY_FLAG) //reply to tunnel
		{
			replyTunnelID = bufbe32toh (excluded);
			excluded += 4;
		}
		uint16_t numExcluded = bufbe16toh (excluded);
		excluded += 2;
		if (numExcluded > DATABASE_LOOKUP_MAX_NUM_PEERS
			|| (excluded - buf) + numExcluded * 32 > (int)msg->GetPayloadLength ())
		{
			LogPrint (eLogWarning, "NetDb: Number of excluded peers", numExcluded, " is too much");
			return;
		}
		if (replyIdent.IsZero ())
		{
			LogPrint (eLogWarning, "NetDb: DatabaseLookup has a zero reply hash");
			return;
		}
		// Hidden routers do not answer unsolicited lookups (Java
		// HandleDatabaseLookupMessageJob.isHidden except for our own key).
		if (i2p::context.IsHidden () && ident != i2p::context.GetIdentHash ())
		{
			LogPrint (eLogDebug, "NetDb: DatabaseLookup ignored in hidden mode");
			return;
		}
		// A known floodfill must not send direct exploratory/normal lookups;
		// Java drops them before dispatch (FloodfillDatabaseLookupMessageHandler).
		if (!replyTunnelID && replyIdent != i2p::context.GetIdentHash ()
			&& (lookupType == DATABASE_LOOKUP_TYPE_EXPLORATORY_LOOKUP
				|| lookupType == DATABASE_LOOKUP_TYPE_NORMAL_LOOKUP))
		{
			auto sender = FindRouter (replyIdent);
			if (sender && sender->IsFloodfill ())
			{
				LogPrint (eLogDebug, "NetDb: Direct lookup from floodfill dropped");
				return;
			}
		}
		// HandleDatabaseLookupMessageJob: drop a direct reply addressed to us.
		// Sending it would loop the DSM/DSRM back through the transport layer.
		if (!replyTunnelID && replyIdent == i2p::context.GetIdentHash ())
		{
			LogPrint (eLogWarning, "NetDb: DatabaseLookup for ", key, " has replies going to us");
			return;
		}
		// Encrypted replies are only meaningful through a tunnel. Do not fall
		// back to cleartext when a peer sets bit 1 or bit 4 without one.
		bool encryptReply = flag & DATABASE_LOOKUP_ENCRYPTION_FLAG;
		bool ratchetReply = flag & DATABASE_LOOKUP_RATCHET_FLAG;
		if ((encryptReply || ratchetReply) && !replyTunnelID)
		{
			LogPrint (eLogWarning, "NetDb: DatabaseLookup requests an encrypted reply without a reply tunnel");
			return;
		}
		const uint8_t * replyKeyBlock = excluded + numExcluded * 32;
		size_t replyKeyAvailable = msg->GetPayloadLength () - (replyKeyBlock - buf);
		DatabaseLookupReplyKey replyKey;
		if (!ParseDatabaseLookupReplyKey (replyKeyBlock, replyKeyAvailable, flag, replyKey))
		{
			if (encryptReply && ratchetReply)
				LogPrint (eLogWarning, "NetDb: DatabaseLookup derived-key ratchet reply is not implemented");
			else
				LogPrint (eLogWarning, "NetDb: DatabaseLookup reply key block is invalid or truncated");
			return;
		}
		if (ident != i2p::context.GetIdentHash () && ShouldThrottleLookup (replyIdent, replyTunnelID))
		{
			LogPrint (eLogDebug, "NetDb: DatabaseLookup throttled for ", replyIdent.ToBase64 ());
			return;
		}

		std::shared_ptr<I2NPMessage> replyMsg;
		if (lookupType == DATABASE_LOOKUP_TYPE_EXPLORATORY_LOOKUP)
		{
			if (!context.IsFloodfill ())
			{
				LogPrint (eLogWarning, "NetDb: Exploratory lookup to non-floodfill dropped");
				return;
			}
			LogPrint (eLogInfo, "NetDb: Exploratory close to ", key, " ", numExcluded, " excluded");
			std::unordered_set<IdentHash> excludedRouters;
			const uint8_t * excluded_ident = excluded;
			for (int i = 0; i < numExcluded; i++)
			{
				excludedRouters.insert (excluded_ident);
				excluded_ident += 32;
			}
			// Java getNearestRouters() adds us; never advertise our own hash in
			// a search reply (also when we do not declare floodfill).
			excludedRouters.insert (i2p::context.GetIdentHash ());
			replyMsg = CreateDatabaseSearchReply (
				ident, GetExploratoryNonFloodfill (ident, NETDB_MAX_NUM_SEARCH_REPLY_PEER_HASHES, excludedRouters));
		}
		else
		{
			if (lookupType == DATABASE_LOOKUP_TYPE_ROUTERINFO_LOOKUP
				|| lookupType == DATABASE_LOOKUP_TYPE_NORMAL_LOOKUP)
			{
				// try to find router
				auto router = FindRouter (ident);
				if (router && !router->IsUnreachable () && !router->IsHidden ())
				{
					// HandleDatabaseLookupMessageJob.EXPIRE_DELAY: do not send
					// RouterInfos older than one hour, except our own. Apply
					// the rule only here, not to LeaseSet or Meta LS replies.
					// Allow the two-minute rejection threshold for clock skew.
					bool isSelf = ident == i2p::context.GetIdentHash ();
					uint64_t maxAge = (uint64_t)NETDB_FLOODFILL_EXPIRATION_TIMEOUT * 1000LL
						+ (uint64_t)NETDB_EXPIRATION_TIMEOUT_THRESHOLD * 1000LL;
					if (isSelf || router->GetTimestamp () + maxAge >= i2p::util::GetMillisecondsSinceEpoch ())
					{
						LogPrint (eLogDebug, "NetDb: Requested RouterInfo ", key, " found");
						if (PopulateRouterInfoBuffer (router)) replyMsg = CreateDatabaseStoreMsg (router);
					}
					else
						LogPrint (eLogDebug, "NetDb: Requested RouterInfo ", key, " is too old, not sent");
				}
			}

			if (!replyMsg
				&& (lookupType == DATABASE_LOOKUP_TYPE_LEASESET_LOOKUP
					|| lookupType == DATABASE_LOOKUP_TYPE_NORMAL_LOOKUP))
			{
				// try to find leaseset
				if (context.IsFloodfill ())
				{
					auto leaseSet = FindLeaseSet (ident);
					if (!leaseSet)
					{
						// no leaseset found
						LogPrint (eLogDebug, "NetDb: Requested LeaseSet not found for ", ident.ToBase32 ());
					}
					else if (leaseSet->IsValid () && !leaseSet->IsExpired ()) // don't send invalid or expired sets
					{
						LogPrint (eLogDebug, "NetDb: Requested LeaseSet ", key, " found");
						replyMsg = CreateDatabaseStoreMsg (ident, leaseSet);
					}
				}
				else if (lookupType == DATABASE_LOOKUP_TYPE_LEASESET_LOOKUP)
				{
					LogPrint (eLogWarning, "NetDb: Explicit LeaseSet lookup to non-floodfill dropped");
					return;
				}
			}

			if (!replyMsg)
			{
				std::unordered_set<IdentHash> excludedRouters;
				const uint8_t * exclude_ident = excluded;
				for (int i = 0; i < numExcluded; i++)
				{
					excludedRouters.insert (exclude_ident);
					exclude_ident += 32;
				}
				excludedRouters.insert (i2p::context.GetIdentHash ());
				auto closestFloodfills = GetClosestFloodfills (ident, 3, excludedRouters, false);
				if (closestFloodfills.empty ())
					LogPrint (
						eLogWarning, "NetDb: No more floodfills for ", key, " found. ", numExcluded, " peers excluded");
				replyMsg = CreateDatabaseSearchReply (ident, closestFloodfills);
			}
		}
		if (replyMsg)
		{
			if (replyTunnelID)
			{
				if (replyKey.mode == DatabaseLookupReplyKey::Mode::ECIESAEAD)
				{
					uint64_t tag;
					memcpy (&tag, replyKey.tag.data (), sizeof (tag)); // first 8-byte tag, n = 0
					replyMsg = i2p::garlic::WrapECIESX25519Message (replyMsg, replyKey.key.data (), tag);
				}
				else if (replyKey.mode == DatabaseLookupReplyKey::Mode::ElGamalAES)
				{
					const i2p::garlic::SessionTag sessionTag (replyKey.tag.data ()); // first 32-byte tag
					i2p::garlic::ElGamalAESSession garlic (replyKey.key.data (), sessionTag);
					replyMsg = garlic.WrapSingleMessage (replyMsg);
				}
				if (!replyMsg)
				{
					// Never send an unencrypted reply after the requester
					// selected an encrypted mode.
					LogPrint (eLogError, "NetDb: Failed to wrap DatabaseLookup reply");
					return;
				}
				bool direct = true;
				if (!i2p::transport::transports.IsConnected (replyIdent))
				{
					auto r = FindRouter (replyIdent);
					if (r && !r->IsReachableFrom (i2p::context.GetRouterInfo ())) direct = false;
				}
				if (direct)
					transports.SendMessage (replyIdent, i2p::CreateTunnelGatewayMsg (replyTunnelID, replyMsg));
				else
				{
					auto exploratoryPool = i2p::tunnel::tunnels.GetExploratoryPool ();
					auto outbound = exploratoryPool ? exploratoryPool->GetNextOutboundTunnel () : nullptr;
					if (outbound)
						outbound->SendTunnelDataMsgTo (replyIdent, replyTunnelID, replyMsg);
					else
						LogPrint (eLogWarning, "NetDb: Can't send lookup reply to ", replyIdent.ToBase64 (),
							". Non reachable and no outbound tunnels");
				}
			}
			else
				transports.SendMessage (replyIdent, replyMsg);
		}
	}

	void NetDb::Flood (const IdentHash & ident, std::shared_ptr<I2NPMessage> floodMsg, bool andNextDay)
	{
		std::unordered_set<IdentHash> excluded;
		excluded.insert (i2p::context.GetIdentHash ()); // don't flood to itself
		excluded.insert (ident); // don't flood back
		for (int i = 0; i < 3; i++)
		{
			auto floodfill = GetClosestFloodfill (ident, excluded, false); // current day
			if (floodfill)
			{
				const auto & h = floodfill->GetIdentHash ();
				transports.SendMessage (h, CopyI2NPMessage (floodMsg));
				excluded.insert (h);
			}
			else
				return; // no more floodfills
		}
		if (andNextDay)
		{
			// flood to two more closest flodfills for next day
			std::unordered_set<IdentHash> excluded1;
			excluded1.insert (i2p::context.GetIdentHash ()); // don't flood to itself
			excluded1.insert (ident); // don't flood back
			for (int i = 0; i < 2; i++)
			{
				auto floodfill = GetClosestFloodfill (ident, excluded1, true); // next day
				if (floodfill)
				{
					const auto & h = floodfill->GetIdentHash ();
					if (!excluded.count (h)) // we didn't send for current day, otherwise skip
						transports.SendMessage (h, CopyI2NPMessage (floodMsg));
					excluded1.insert (h);
				}
				else
					return;
			}
		}
	}

	std::shared_ptr<const RouterInfo> NetDb::GetRandomRouter () const
	{
		return GetRandomRouter ([] (std::shared_ptr<const RouterInfo> router) -> bool { return !router->IsHidden (); });
	}

	std::shared_ptr<const RouterInfo> NetDb::GetRandomRouter (std::shared_ptr<const RouterInfo> compatibleWith,
		bool reverse, bool endpoint, bool clientTunnel, PeerOrdering * peerOrdering) const
	{
		bool checkIsReal = clientTunnel
			&& i2p::tunnel::tunnels.GetPreciseTunnelCreationSuccessRate () < NETDB_TUNNEL_CREATION_RATE_THRESHOLD
			&& // too low rate
			context.GetUptime () > NETDB_CHECK_FOR_EXPIRATION_UPTIME; // after 10 minutes uptime
		return GetRandomRouter (
			[compatibleWith, reverse, endpoint, clientTunnel, checkIsReal, peerOrdering] (
				std::shared_ptr<const RouterInfo> router) -> bool
			{
				if (peerOrdering)
				{
					bool lastHop = peerOrdering->IsLastHop (router->GetIdentHash ());
					if (endpoint && !lastHop) return false;
					if (lastHop && !endpoint && router->IsV4 ())
						return false; // if router is eligible for endpoint but it's not
				}
				return !router->IsHidden () && router != compatibleWith
					&& (reverse ? (compatibleWith->IsReachableFrom (*router) && router->GetCompatibleTransports (true))
								: router->IsReachableFrom (*compatibleWith))
					&& !router->IsNAT2NATOnly (*compatibleWith) && router->GetVersion () >= NETDB_MIN_ALLOWED_VERSION
					&& router->IsECIES () && !router->IsHighCongestion (clientTunnel)
					&& (!i2p::transport::transports.IsCheckReserved ()
						|| (!router->IsSameFamily (*compatibleWith) && !router->IsSameSubnet (*compatibleWith)))
					&& (!checkIsReal || router->GetProfile ()->IsReal ())
					&& (!endpoint
						|| (router->IsV4 ()
							&& (!reverse
								|| router->IsPublished (
									true)))); // endpoint must be ipv4 and published if inbound(reverse)
			});
	}

	std::shared_ptr<const RouterInfo> NetDb::GetRandomSSU2PeerTestRouter (
		bool v4, const std::unordered_set<IdentHash> & excluded) const
	{
		return GetRandomRouter (
			[v4, &excluded] (std::shared_ptr<const RouterInfo> router) -> bool
			{
				return !router->IsHidden () && router->IsECIES () && router->IsSSU2PeerTesting (v4)
					&& !excluded.count (router->GetIdentHash ());
			});
	}

	std::shared_ptr<const RouterInfo> NetDb::GetRandomSSU2Introducer (
		bool v4, const std::unordered_set<IdentHash> & excluded) const
	{
		return GetRandomRouter (
			[v4, &excluded] (std::shared_ptr<const RouterInfo> router) -> bool
			{
				return !router->IsHidden () && router->IsSSU2Introducer (v4)
					&& !excluded.count (router->GetIdentHash ());
			});
	}

	std::shared_ptr<const RouterInfo> NetDb::GetHighBandwidthRandomRouter (
		std::shared_ptr<const RouterInfo> compatibleWith, bool reverse, bool endpoint,
		PeerOrdering * peerOrdering) const
	{
		bool checkIsReal
			= i2p::tunnel::tunnels.GetPreciseTunnelCreationSuccessRate () < NETDB_TUNNEL_CREATION_RATE_THRESHOLD
			&& // too low rate
			context.GetUptime () > NETDB_CHECK_FOR_EXPIRATION_UPTIME; // after 10 minutes uptime
		return GetRandomRouter (
			[compatibleWith, reverse, endpoint, checkIsReal, peerOrdering] (
				std::shared_ptr<const RouterInfo> router) -> bool
			{
				if (peerOrdering)
				{
					bool lastHop = peerOrdering->IsLastHop (router->GetIdentHash ());
					if (endpoint && !lastHop) return false;
					if (lastHop && !endpoint && router->IsV4 ())
						return false; // if router is eligible for endpoint but it's not
				}
				return !router->IsHidden () && router != compatibleWith
					&& (reverse ? (compatibleWith->IsReachableFrom (*router) && router->GetCompatibleTransports (true))
								: router->IsReachableFrom (*compatibleWith))
					&& !router->IsNAT2NATOnly (*compatibleWith) && (router->GetCaps () & RouterInfo::eHighBandwidth)
					&& router->GetVersion () >= NETDB_MIN_HIGHBANDWIDTH_VERSION && router->IsECIES ()
					&& !router->IsHighCongestion (true)
					&& (!i2p::transport::transports.IsCheckReserved ()
						|| (!router->IsSameFamily (*compatibleWith) && !router->IsSameSubnet (*compatibleWith)))
					&& (!checkIsReal || router->GetProfile ()->IsReal ())
					&& (!endpoint
						|| (router->IsV4 ()
							&& (!reverse
								|| router->IsPublished (
									true)))); // endpoint must be ipv4 and published if inbound(reverse)
			});
	}

	template <typename Filter> std::shared_ptr<const RouterInfo> NetDb::GetRandomRouter (Filter filter) const
	{
		uint16_t inds[3];
		RAND_bytes ((uint8_t *)inds, sizeof (inds));
		std::lock_guard<std::mutex> l (m_RouterInfosMutex);
		auto count = m_RouterInfos.size ();
		if (count == 0) return nullptr;
		inds[0] %= count;
		auto it = m_RouterInfos.begin ();
		std::advance (it, inds[0]);
		// try random router
		if (it != m_RouterInfos.end () && !it->second->IsUnreachable () && filter (it->second)) return it->second;
		// try some routers around
		auto it1 = m_RouterInfos.begin ();
		if (inds[0])
		{
			// before
			inds[1] %= inds[0];
			std::advance (it1, (inds[1] + inds[0]) / 2);
		}
		else
			it1 = it;
		auto it2 = it;
		if (inds[0] < m_RouterInfos.size () - 1)
		{
			// after
			inds[2] %= (m_RouterInfos.size () - 1 - inds[0]);
			inds[2] /= 2;
			std::advance (it2, inds[2]);
		}
		// it1 - from, it2 - to
		it = it1;
		while (it != it2 && it != m_RouterInfos.end ())
		{
			if (!it->second->IsUnreachable () && filter (it->second)) return it->second;
			it++;
		}
		// still not found, try from the beginning
		it = m_RouterInfos.begin ();
		while (it != it1 && it != m_RouterInfos.end ())
		{
			if (!it->second->IsUnreachable () && filter (it->second)) return it->second;
			it++;
		}
		// still not found, try to the beginning
		it = it2;
		while (it != m_RouterInfos.end ())
		{
			if (!it->second->IsUnreachable () && filter (it->second)) return it->second;
			it++;
		}
		return nullptr; // seems we have too few routers
	}

	void NetDb::PostI2NPMsg (std::shared_ptr<const I2NPMessage> msg)
	{
		if (msg) m_Queue.Put (msg);
	}

	void NetDb::PostDatabaseSearchReplyMsg (std::shared_ptr<const I2NPMessage> msg)
	{
		if (!msg) return;
		auto requests = GetRequests ();
		if (requests) requests->PostDatabaseSearchReplyMsg (msg);
	}

	std::shared_ptr<const RouterInfo> NetDb::GetClosestFloodfill (
		const IdentHash & destination, const std::unordered_set<IdentHash> & excluded, bool nextDay) const
	{
		IdentHash destKey = CreateRoutingKey (destination, nextDay);
		std::lock_guard<std::mutex> l (m_FloodfillsMutex);
		return m_Floodfills.FindClosest (destKey,
			[&excluded] (const std::shared_ptr<RouterInfo> & r) -> bool
			{
				return r && !r->IsUnreachable () && !r->GetProfile ()->IsUnreachable ()
					&& !excluded.count (r->GetIdentHash ());
			});
	}

	std::vector<IdentHash> NetDb::GetClosestFloodfills (
		const IdentHash & destination, size_t num, std::unordered_set<IdentHash> & excluded, bool closeThanUsOnly) const
	{
		std::vector<IdentHash> res;
		IdentHash destKey = CreateRoutingKey (destination);
		std::vector<std::shared_ptr<RouterInfo>> v;
		{
			std::lock_guard<std::mutex> l (m_FloodfillsMutex);
			v = m_Floodfills.FindClosest (destKey, num,
				[&excluded] (const std::shared_ptr<RouterInfo> & r) -> bool
				{
					return r && !r->IsUnreachable () && !r->GetProfile ()->IsUnreachable ()
						&& !excluded.count (r->GetIdentHash ());
				});
		}
		if (v.empty ()) return res;

		XORMetric ourMetric;
		if (closeThanUsOnly) ourMetric = destKey ^ i2p::context.GetIdentHash ();
		for (auto & it : v)
		{
			if (closeThanUsOnly && ourMetric < (destKey ^ it->GetIdentHash ())) break;
			res.push_back (it->GetIdentHash ());
		}
		return res;
	}

	std::shared_ptr<const RouterInfo> NetDb::GetRandomRouterInFamily (FamilyID fam) const
	{
		return GetRandomRouter (
			[fam] (std::shared_ptr<const RouterInfo> router) -> bool { return router->IsFamily (fam); });
	}

	std::vector<IdentHash> NetDb::GetExploratoryNonFloodfill (
		const IdentHash & destination, size_t num, const std::unordered_set<IdentHash> & excluded)
	{
		std::vector<IdentHash> ret;
		if (!num) return ret; // empty list
		// m_ExploratorySelection and m_LastExploratorySelectionUpdateTime are
		// also reachable from administrative calls; do not rebuild them while
		// another caller is iterating.
		std::lock_guard<std::mutex> selectionLock (m_ExploratorySelectionMutex);
		auto ts = i2p::util::GetMonotonicSeconds ();
		if (ts > m_LastExploratorySelectionUpdateTime + NETDB_EXPLORATORY_SELECTION_UPDATE_INTERVAL)
		{
			// update selection
			m_ExploratorySelection.clear ();
			std::vector<std::shared_ptr<const RouterInfo>> eligible;
			{
				// collect eligible from current netdb
				bool checkIsReal = i2p::tunnel::tunnels.GetPreciseTunnelCreationSuccessRate ()
					< NETDB_TUNNEL_CREATION_RATE_THRESHOLD; // too low rate
				std::lock_guard<std::mutex> l (m_RouterInfosMutex);
				eligible.reserve (m_RouterInfos.size ());
				for (const auto & it : m_RouterInfos)
					if (!it.second->IsDeclaredFloodfill () && !it.second->IsUnreachable ()
						&& (!checkIsReal || (it.second->HasProfile () && it.second->GetProfile ()->IsReal ())))
						eligible.push_back (it.second);
			}
			if (eligible.size () > NETDB_MAX_EXPLORATORY_SELECTION_SIZE)
			{
				std::lock_guard<std::mutex> rng (m_RngMutex);
				std::sample (eligible.begin (), eligible.end (), std::back_inserter (m_ExploratorySelection),
					NETDB_MAX_EXPLORATORY_SELECTION_SIZE, m_Rng);
			}
			else
				std::swap (m_ExploratorySelection, eligible);
			m_LastExploratorySelectionUpdateTime = ts;
		}

		// sort by distance
		IdentHash destKey = CreateRoutingKey (destination);
		std::map<XORMetric, std::shared_ptr<const RouterInfo>> sorted;
		for (const auto & it : m_ExploratorySelection)
			if (!it->IsUnreachable () && !excluded.count (it->GetIdentHash ()))
				sorted.emplace (destKey ^ it->GetIdentHash (), it);
		// return first num closest routers
		for (const auto & it : sorted)
		{
			ret.push_back (it.second->GetIdentHash ());
			if (ret.size () >= num) break;
		}
		return ret;
	}

	void NetDb::ManageRouterInfos ()
	{
		auto ts = i2p::util::GetSecondsSinceEpoch ();
		{
			std::lock_guard<std::mutex> l (m_RouterInfosMutex);
			for (auto & it : m_RouterInfos) it.second->UpdateIntroducers (ts);
		}
		SaveUpdated ();
	}

	void NetDb::ManageLeaseSets ()
	{
		auto ts = i2p::util::GetMillisecondsSinceEpoch ();
		std::lock_guard<std::mutex> lock (m_LeaseSetsMutex);
		for (auto it = m_LeaseSets.begin (); it != m_LeaseSets.end ();)
		{
			if (!it->second->IsValid () || ts > it->second->GetExpirationTime () - LEASE_ENDDATE_THRESHOLD)
			{
				LogPrint (eLogInfo, "NetDb: LeaseSet ", it->first.ToBase64 (), " expired or invalid");
				it = m_LeaseSets.erase (it);
			}
			else
				++it;
		}
		m_LeasesPool.CleanUpMt ();
	}

	bool NetDb::PopulateRouterInfoBuffer (std::shared_ptr<RouterInfo> r)
	{
		if (!r) return false;
		if (r->GetBuffer ()) return true;
		std::string path;
		{
			std::lock_guard<std::mutex> storage (m_StorageMutex);
			path = m_Storage.Path (r->GetIdentHashBase64 ());
		}
		return r->LoadBuffer (path);
	}
}
}
