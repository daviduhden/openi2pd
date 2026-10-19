/*
* Copyright (c) 2013-2026, The PurpleI2P Project
* Copyright (c) 2026, David Uhden Collado
* SPDX-License-Identifier: BSD-3-Clause
*
* This file is part of openi2pd and licensed under BSD-3-Clause
*
* See full license text in LICENSE file at top of project tree
*/

#include "Log.h"
#include "I2NPProtocol.h"
#include "Transports.h"
#include "NetDb.hpp"
#include "ECIESX25519AEADRatchetSession.h"
#include "RouterContext.h"
#include "Timestamp.h"
#include "util.h"
#include "NetDbRequests.h"

namespace i2p
{
namespace data
{
	RequestedDestination::RequestedDestination (const IdentHash & destination, bool isExploratory, bool direct)
		: m_Destination (destination)
		, m_IsExploratory (isExploratory)
		, m_IsDirect (direct)
		, m_IsActive (true)
		, m_IsSentDirectly (false)
		, m_CreationTime (i2p::util::GetMillisecondsSinceEpoch ())
		, m_LastRequestTime (0)
		, m_NumAttempts (0)
	{
		if (i2p::context.IsFloodfill ())
			m_ExcludedPeers.insert (i2p::context.GetIdentHash ()); // exclude self if floodfill
	}

	RequestedDestination::~RequestedDestination ()
	{
		InvokeRequestComplete (nullptr);
	}

	std::shared_ptr<I2NPMessage> RequestedDestination::CreateRequestMessage (
		std::shared_ptr<const RouterInfo> router, std::shared_ptr<const i2p::tunnel::InboundTunnel> replyTunnel)
	{
		std::shared_ptr<I2NPMessage> msg;
		if (replyTunnel)
			msg = i2p::CreateRouterInfoDatabaseLookupMsg (m_Destination, replyTunnel->GetNextIdentHash (),
				replyTunnel->GetNextTunnelID (), m_IsExploratory, &m_ExcludedPeers);
		else
			msg = i2p::CreateRouterInfoDatabaseLookupMsg (
				m_Destination, i2p::context.GetIdentHash (), 0, m_IsExploratory, &m_ExcludedPeers);
		if (router)
		{
			m_ExcludedPeers.insert (router->GetIdentHash ());
			m_RequestedPeers.insert (router->GetIdentHash ());
		}
		m_LastRequestTime = i2p::util::GetMillisecondsSinceEpoch ();
		m_NumAttempts++;
		m_IsSentDirectly = false;
		return msg;
	}

	std::shared_ptr<I2NPMessage> RequestedDestination::CreateRequestMessage (const IdentHash & floodfill)
	{
		// The direct form must keep the lookup type too. Java ExploreJob sets
		// Type.EXPL for a real exploration regardless of whether the lookup is
		// sent directly or through tunnels (I2NP flags bits 3-2: 11 =
		// exploration, 10 = RouterInfo).
		auto msg = i2p::CreateRouterInfoDatabaseLookupMsg (
			m_Destination, i2p::context.GetRouterInfo ().GetIdentHash (), 0, m_IsExploratory, &m_ExcludedPeers);
		m_ExcludedPeers.insert (floodfill);
		m_RequestedPeers.insert (floodfill);
		m_NumAttempts++;
		m_LastRequestTime = i2p::util::GetMillisecondsSinceEpoch ();
		m_IsSentDirectly = true;
		return msg;
	}

	bool RequestedDestination::IsExcluded (const IdentHash & ident) const
	{
		return m_ExcludedPeers.count (ident);
	}

	void RequestedDestination::ClearExcludedPeers ()
	{
		m_ExcludedPeers.clear ();
	}

	void RequestedDestination::InvokeRequestComplete (std::shared_ptr<RouterInfo> r)
	{
		if (!m_RequestComplete.empty ())
		{
			for (auto it : m_RequestComplete)
				if (it != nullptr) it (r);
			m_RequestComplete.clear ();
		}
	}

	void RequestedDestination::Success (std::shared_ptr<RouterInfo> r)
	{
		if (m_IsActive)
		{
			m_IsActive = false;
			InvokeRequestComplete (r);
		}
	}

	void RequestedDestination::Fail ()
	{
		if (m_IsActive)
		{
			m_IsActive = false;
			InvokeRequestComplete (nullptr);
		}
	}

	NetDbRequests::NetDbRequests ()
		: RunnableServiceWithWork ("NetDbReq")
		, m_ManageRequestsTimer (GetIOService ())
		, m_ExploratoryTimer (GetIOService ())
		, m_CleanupTimer (GetIOService ())
		, m_DiscoveredRoutersTimer (GetIOService ())
		, m_Rng (i2p::util::GetRngSeed ())
	{
	}

	NetDbRequests::~NetDbRequests ()
	{
		Stop ();
	}

	void NetDbRequests::Start ()
	{
		if (!IsRunning ())
		{
			StartIOService ();
			ScheduleManageRequests ();
			ScheduleCleanup ();
			if (!i2p::context.IsHidden ()) ScheduleExploratory (EXPLORATORY_REQUEST_INTERVAL);
		}
	}

	void NetDbRequests::Stop ()
	{
		if (IsRunning ())
		{
			m_ManageRequestsTimer.cancel ();
			m_ExploratoryTimer.cancel ();
			m_CleanupTimer.cancel ();
			StopIOService ();

			m_RequestedDestinations.clear ();
			m_RequestedDestinationsPool.CleanUpMt ();
		}
	}

	void NetDbRequests::ScheduleCleanup ()
	{
		m_CleanupTimer.expires_after (std::chrono::seconds (REQUESTED_DESTINATIONS_POOL_CLEANUP_INTERVAL));
		m_CleanupTimer.async_wait (std::bind (&NetDbRequests::HandleCleanupTimer, this, std::placeholders::_1));
	}

	void NetDbRequests::HandleCleanupTimer (const boost::system::error_code & ecode)
	{
		if (ecode != boost::asio::error::operation_aborted)
		{
			m_RequestedDestinationsPool.CleanUpMt ();
			ScheduleCleanup ();
		}
	}

	std::shared_ptr<RequestedDestination> NetDbRequests::CreateRequest (const IdentHash & destination,
		bool isExploratory, bool direct, RequestedDestination::RequestComplete requestComplete)
	{
		// Reuse a pending (or still cached) request before enforcing the cap:
		// a duplicate must attach its callback to the existing request.
		auto it = m_RequestedDestinations.find (destination);
		if (it != m_RequestedDestinations.end ())
		{
			auto dest = it->second;
			if (requestComplete)
			{
				if (dest->IsActive ())
					dest->AddRequestComplete (requestComplete);
				else
					requestComplete (nullptr);
			}
			return nullptr;
		}
		if (m_RequestedDestinations.size () >= MAX_NUM_PENDING_REQUESTS)
		{
			LogPrint (eLogWarning, "NetDbReq: Too many pending requests, dropping lookup for ",
				destination.ToBase64 ());
			if (requestComplete) requestComplete (nullptr);
			return nullptr;
		}
		// request RouterInfo directly
		auto dest = m_RequestedDestinationsPool.AcquireSharedMt (destination, isExploratory, direct);
		if (requestComplete) dest->AddRequestComplete (requestComplete);
		m_RequestedDestinations.emplace (destination, dest);
		return dest;
	}

	void NetDbRequests::RequestComplete (const IdentHash & ident, std::shared_ptr<RouterInfo> r)
	{
		boost::asio::post (GetIOService (),
			[this, ident, r] ()
			{
				std::shared_ptr<RequestedDestination> request;
				auto it = m_RequestedDestinations.find (ident);
				if (it != m_RequestedDestinations.end ())
				{
					request = it->second;
					if (request->IsExploratory ()) m_RequestedDestinations.erase (it);
					// otherwise cache for a while
				}
				if (request)
				{
					if (r)
						request->Success (r);
					else
						request->Fail ();
				}
			});
	}

	std::shared_ptr<RequestedDestination> NetDbRequests::FindRequest (const IdentHash & ident) const
	{
		auto it = m_RequestedDestinations.find (ident);
		if (it != m_RequestedDestinations.end ()) return it->second;
		return nullptr;
	}

	void NetDbRequests::ManageRequests ()
	{
		uint64_t ts = i2p::util::GetMillisecondsSinceEpoch ();
		for (auto it = m_RequestedDestinations.begin (); it != m_RequestedDestinations.end ();)
		{
			auto & dest = it->second;
			if (dest->IsActive () || ts < dest->GetCreationTime () + REQUEST_CACHE_TIME)
			{
				if (!dest->IsExploratory ())
				{
					// regular request
					bool done = false;
					if (ts < dest->GetCreationTime () + MAX_REQUEST_TIME)
					{
						if (ts > dest->GetLastRequestTime ()
								+ (dest->IsSentDirectly () ? MIN_DIRECT_REQUEST_TIME : MIN_REQUEST_TIME))
							// try next floodfill if no response after min interval
							done = !SendNextRequest (dest);
					}
					else // request is expired
						done = true;
					if (done) dest->Fail ();
					it++;
				}
				else
				{
					// exploratory
					if (ts >= dest->GetCreationTime () + MAX_EXPLORATORY_REQUEST_TIME)
					{
						dest->Fail ();
						it = m_RequestedDestinations.erase (it); // delete expired exploratory request right a way
					}
					else
						it++;
				}
			}
			else
				it = m_RequestedDestinations.erase (it);
		}
	}

	bool NetDbRequests::SendNextRequest (std::shared_ptr<RequestedDestination> dest)
	{
		if (!dest || !dest->IsActive ()) return false;
		bool ret = true;
		auto count = dest->GetNumAttempts ();
		if (!dest->IsExploratory () && count < MAX_NUM_REQUEST_ATTEMPTS)
		{
			auto nextFloodfill = netdb.GetClosestFloodfill (dest->GetDestination (), dest->GetExcludedPeers ());
			if (nextFloodfill)
			{
				bool direct = dest->IsDirect ();
				if (direct && !nextFloodfill->IsReachableFrom (i2p::context.GetRouterInfo ())
					&& !i2p::transport::transports.IsConnected (nextFloodfill->GetIdentHash ()))
					direct = false; // floodfill can't be reached directly
				auto s = shared_from_this ();
				// I2NPMessage::Drop() may run on a transport or tunnel thread.
				// Do not read RequestedDestination state there; re-check on the
				// NetDbRequests strand so all request state stays single-threaded.
				auto onDrop = [s, dest] ()
				{
					boost::asio::post (s->GetIOService (),
						[s, dest] ()
						{
							if (dest->IsActive ()) s->SendNextRequest (dest);
						});
				};
				if (direct)
				{
					if (CheckLogLevel (eLogDebug))
						LogPrint (eLogDebug, "NetDbReq: Try ", dest->GetDestination ().ToBase64 (), " at ", count,
							" floodfill ", nextFloodfill->GetIdentHash ().ToBase64 (), " directly");
					auto msg = dest->CreateRequestMessage (nextFloodfill->GetIdentHash ());
					msg->onDrop = onDrop;
					i2p::transport::transports.SendMessage (nextFloodfill->GetIdentHash (), msg);
				}
				else
				{
					auto pool = i2p::tunnel::tunnels.GetExploratoryPool ();
					if (pool)
					{
						auto outbound = pool->GetNextOutboundTunnel ();
						auto inbound = pool->GetNextInboundTunnel ();
						if (nextFloodfill && outbound && inbound)
						{
							if (CheckLogLevel (eLogDebug))
								LogPrint (eLogDebug, "NetDbReq: Try ", dest->GetDestination ().ToBase64 (), " at ",
									count, " floodfill ", nextFloodfill->GetIdentHash ().ToBase64 (),
									" through tunnels");
							auto msg = dest->CreateRequestMessage (nextFloodfill, inbound);
							msg->onDrop = onDrop;
							outbound->SendTunnelDataMsgTo (nextFloodfill->GetIdentHash (), 0,
								i2p::garlic::WrapECIESX25519MessageForRouter (
									msg, nextFloodfill->GetIdentity ()->GetEncryptionPublicKey ()));
						}
						else
						{
							ret = false;
							if (!inbound) LogPrint (eLogWarning, "NetDbReq: No inbound tunnels");
							if (!outbound) LogPrint (eLogWarning, "NetDbReq: No outbound tunnels");
						}
					}
					else
					{
						ret = false;
						LogPrint (eLogWarning, "NetDbReq: Exploratory pool is not ready");
					}
				}
			}
			else
			{
				ret = false;
				LogPrint (eLogWarning, "NetDbReq: No more floodfills for ", dest->GetDestination ().ToBase64 (),
					" after ", count, "attempts");
			}
		}
		else
		{
			if (!dest->IsExploratory ())
				LogPrint (eLogWarning, "NetDbReq: ", dest->GetDestination ().ToBase64 (), " not found after ",
					MAX_NUM_REQUEST_ATTEMPTS, " attempts");
			ret = false;
		}
		return ret;
	}

	void NetDbRequests::ScheduleManageRequests ()
	{
		m_ManageRequestsTimer.expires_after (
			std::chrono::milliseconds (MANAGE_REQUESTS_INTERVAL + m_Rng () % MANAGE_REQUESTS_INTERVAL_VARIANCE));
		m_ManageRequestsTimer.async_wait (
			std::bind (&NetDbRequests::HandleManageRequestsTimer, this, std::placeholders::_1));
	}

	void NetDbRequests::HandleManageRequestsTimer (const boost::system::error_code & ecode)
	{
		if (ecode != boost::asio::error::operation_aborted)
		{
			if (i2p::tunnel::tunnels.GetExploratoryPool ()) // expolratory pool is ready?
				ManageRequests ();
			ScheduleManageRequests ();
		}
	}

	void NetDbRequests::PostDatabaseSearchReplyMsg (std::shared_ptr<const I2NPMessage> msg)
	{
		boost::asio::post (GetIOService (), [this, msg] () { HandleDatabaseSearchReplyMsg (msg); });
	}

	void NetDbRequests::HandleDatabaseSearchReplyMsg (std::shared_ptr<const I2NPMessage> msg)
	{
		if (!msg) return;
		DatabaseSearchReply reply;
		if (!ParseDatabaseSearchReply (msg->GetPayload (), msg->GetPayloadLength (), reply))
		{
			// Truncated key/count/peer list/from, or a declared count that
			// does not fit the payload. Never treat trailing bytes as peers.
			LogPrint (eLogWarning, "NetDbReq: Malformed database search reply, ", msg->GetPayloadLength (), " bytes");
			return;
		}
		LogPrint (eLogDebug, "NetDbReq: DatabaseSearchReply for ", reply.key.ToBase64 (), " num=", reply.peers.size ());
		if (reply.from.IsZero ())
		{
			LogPrint (eLogWarning, "NetDbReq: Database search reply has zero sender hash");
			return;
		}

		auto dest = FindRequest (reply.key);
		if (!dest || !dest->IsActive ())
		{
			LogPrint (eLogInfo, "NetDbReq: Unsolicited or late database search reply for ", reply.key.ToBase64 ());
			return;
		}
		// The DatabaseSearchReply "from" hash is unauthenticated (I2NP spec
		// note), so bind it to the peers this lookup was actually sent to.
		// A request that has not been sent yet has no valid responder.
		if (!dest->IsRequestedPeer (reply.from))
		{
			LogPrint (eLogWarning, "NetDbReq: Database search reply for ", reply.key.ToBase64 (), " from unexpected sender ",
				reply.from.ToBase64 ());
			return;
		}
		// One queried peer gets one answer per request. Replayed or duplicate
		// replies must not consume the remaining retry slots.
		if (!dest->MarkReplyFromPeer (reply.from))
		{
			LogPrint (eLogWarning, "NetDbReq: Duplicate database search reply for ", reply.key.ToBase64 (), " from ",
				reply.from.ToBase64 ());
			return;
		}

		bool isExploratory = dest->IsExploratory ();
		if (!isExploratory
			&& (!reply.peers.empty () || dest->GetNumAttempts () < 3)) // before 3-rd attempt might be just bad luck
		{
			// try to send next requests
			if (!SendNextRequest (dest)) RequestComplete (reply.key, nullptr);
		}
		else
			// no more requests for destination possible. delete it
			RequestComplete (reply.key, nullptr);

		// try responses
		// I2NP recommends at most 16 returned hashes; follow that receiver
		// policy even though the wire count is 0-255.
		size_t num = std::min (reply.peers.size (), NETDB_MAX_NUM_SEARCH_REPLY_PEER_HASHES);
		if (reply.peers.size () > num)
			LogPrint (eLogWarning, "NetDbReq: Too many peer hashes ", reply.peers.size (),
				" in database search reply, reduced to ", num);
		if (isExploratory && !m_DiscoveredRouterHashes.empty ())
		{
			// request outstanding routers
			for (auto it : m_DiscoveredRouterHashes) RequestRouter (it);
			m_DiscoveredRouterHashes.clear ();
			m_DiscoveredRoutersTimer.cancel ();
		}
		const auto & ourIdent = i2p::context.GetIdentHash ();
		std::unordered_set<IdentHash> seenPeers;
		seenPeers.reserve (num);
		for (size_t i = 0; i < num; i++)
		{
			const auto & router = reply.peers[i];
			// Java SingleLookupJob skips our own hash and the sender; a zero
			// hash is not a router identity. Deduplicate within one reply so
			// repeated entries cannot multiply lookup work.
			if (router.IsZero () || router == reply.from || router == ourIdent) continue;
			if (!seenPeers.insert (router).second) continue;
			if (CheckLogLevel (eLogDebug)) LogPrint (eLogDebug, "NetDbReq: ", i, ": ", router.ToBase64 ());

			if (isExploratory)
				// postpone request
				m_DiscoveredRouterHashes.push_back (router);
			else
				// send request right a way
				RequestRouter (router);
		}
		if (isExploratory && !m_DiscoveredRouterHashes.empty ()) ScheduleDiscoveredRoutersRequest ();
	}

	void NetDbRequests::RequestRouter (const IdentHash & router)
	{
		auto r = netdb.FindRouter (router);
		if (!r || i2p::util::GetMillisecondsSinceEpoch () > r->GetTimestamp () + 3600 * 1000LL)
		{
			// router with ident not found or too old (1 hour)
			LogPrint (eLogDebug, "NetDbReq: Found new/outdated router. Requesting RouterInfo...");
			if (!IsRouterBanned (router))
				RequestDestination (router, nullptr, true);
			else
				LogPrint (eLogDebug, "NetDbReq: Router ", router.ToBase64 (), " is banned. Skipped");
		}
		else
			LogPrint (eLogDebug, "NetDbReq: [:|||:]");
	}

	void NetDbRequests::PostRequestDestination (
		const IdentHash & destination, const RequestedDestination::RequestComplete & requestComplete, bool direct)
	{
		boost::asio::post (GetIOService (), [this, destination, requestComplete, direct] ()
			{ RequestDestination (destination, requestComplete, direct); });
	}

	void NetDbRequests::RequestDestination (
		const IdentHash & destination, const RequestedDestination::RequestComplete & requestComplete, bool direct)
	{
		auto dest = CreateRequest (destination, false, direct, requestComplete); // non-exploratory
		if (dest)
		{
			if (!SendNextRequest (dest)) RequestComplete (destination, nullptr);
		}
		else
			LogPrint (
				eLogWarning, "NetDbReq: Destination ", destination.ToBase64 (), " is requested already or cached");
	}

	void NetDbRequests::Explore (int numDestinations)
	{
		// new requests
		auto exploratoryPool = i2p::tunnel::tunnels.GetExploratoryPool ();
		auto outbound = exploratoryPool ? exploratoryPool->GetNextOutboundTunnel () : nullptr;
		auto inbound = exploratoryPool ? exploratoryPool->GetNextInboundTunnel () : nullptr;
		bool throughTunnels = outbound && inbound;

		uint8_t randomHash[32];
		std::vector<i2p::tunnel::TunnelMessageBlock> msgs;
		LogPrint (eLogInfo, "NetDbReq: Exploring new ", numDestinations, " routers ...");
		for (int i = 0; i < numDestinations; i++)
		{
			RAND_bytes (randomHash, 32);
			auto dest = CreateRequest (randomHash, true, !throughTunnels); // exploratory
			if (!dest)
			{
				LogPrint (eLogWarning, "NetDbReq: Exploratory destination is requested already");
				return;
			}
			auto floodfill = netdb.GetClosestFloodfill (randomHash, dest->GetExcludedPeers ());
			if (floodfill)
			{
				if (i2p::transport::transports.IsConnected (floodfill->GetIdentHash ())) throughTunnels = false;
				if (throughTunnels)
				{
					msgs.push_back (i2p::tunnel::TunnelMessageBlock {
						i2p::tunnel::eDeliveryTypeRouter, floodfill->GetIdentHash (), 0,
						CreateDatabaseStoreMsg () // tell floodfill about us
					});
					msgs.push_back (i2p::tunnel::TunnelMessageBlock {
						i2p::tunnel::eDeliveryTypeRouter, floodfill->GetIdentHash (), 0,
						dest->CreateRequestMessage (floodfill, inbound) // explore
					});
				}
				else
					i2p::transport::transports.SendMessage (
						floodfill->GetIdentHash (), dest->CreateRequestMessage (floodfill->GetIdentHash ()));
			}
			else
				RequestComplete (randomHash, nullptr);
		}
		if (throughTunnels && msgs.size () > 0) outbound->SendTunnelDataMsgs (msgs);
	}

	void NetDbRequests::ScheduleExploratory (uint64_t interval)
	{
		m_ExploratoryTimer.expires_after (std::chrono::seconds (interval));
		m_ExploratoryTimer.async_wait (std::bind (&NetDbRequests::HandleExploratoryTimer, this, std::placeholders::_1));
	}

	void NetDbRequests::HandleExploratoryTimer (const boost::system::error_code & ecode)
	{
		if (ecode != boost::asio::error::operation_aborted)
		{
			auto numRouters = netdb.GetNumRouters ();
			auto nextExploratoryInterval = numRouters < 2500
				? (EXPLORATORY_REQUEST_INTERVAL + m_Rng () % EXPLORATORY_REQUEST_INTERVAL) / 2
				: EXPLORATORY_REQUEST_INTERVAL + m_Rng () % EXPLORATORY_REQUEST_INTERVAL_VARIANCE;
			if (numRouters)
			{
				if (i2p::transport::transports.IsOnline () && i2p::transport::transports.IsRunning ())
				{
					// explore only if online
					numRouters = 800 / numRouters;
					if (numRouters < 1) numRouters = 1;
					if (numRouters > 9) numRouters = 9;
					Explore (numRouters);
				}
			}
			else
				LogPrint (eLogError, "NetDbReq: No known routers, reseed seems to be totally failed");
			ScheduleExploratory (nextExploratoryInterval);
		}
	}

	void NetDbRequests::ScheduleDiscoveredRoutersRequest ()
	{
		m_DiscoveredRoutersTimer.expires_after (
			std::chrono::milliseconds (DISCOVERED_REQUEST_INTERVAL + m_Rng () % DISCOVERED_REQUEST_INTERVAL_VARIANCE));
		m_DiscoveredRoutersTimer.async_wait (
			std::bind (&NetDbRequests::HandleDiscoveredRoutersTimer, this, std::placeholders::_1));
	}

	void NetDbRequests::HandleDiscoveredRoutersTimer (const boost::system::error_code & ecode)
	{
		if (ecode != boost::asio::error::operation_aborted)
		{
			if (!m_DiscoveredRouterHashes.empty ())
			{
				RequestRouter (m_DiscoveredRouterHashes.front ());
				m_DiscoveredRouterHashes.pop_front ();
				if (!m_DiscoveredRouterHashes.empty ()) // more hashes to request
					ScheduleDiscoveredRoutersRequest ();
			}
		}
	}
}
}
