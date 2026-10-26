/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TunnelPool.
 *
 * Implements tunnel creation, selection and expiration, peer selection including
 * custom selectors and peer ordering, tunnel testing, garlic and delivery-status
 * handling, and pool reconfiguration.
 *
 */

#include <algorithm>
#include "I2PEndian.h"
#include "Blocklist.h"
#include "Crypto.h"
#include "Tunnel.h"
#include "NetDb.hpp"
#include "Timestamp.h"
#include "util.h"
#include "Garlic.h"
#include "ECIESX25519AEADRatchetSession.h"
#include "Transports.h"
#include "Log.h"
#include "Tunnel.h"
#include "TunnelPool.h"
#include "Destination.h"

namespace i2p
{
namespace tunnel
{
	void Path::Add (std::shared_ptr<const i2p::data::RouterInfo> r)
	{
		if (r) peers.push_back (r->GetRouterIdentity ());
	}

	void Path::Reverse ()
	{
		std::reverse (peers.begin (), peers.end ());
	}

	void ClampTunnelLength (int & hops, int & variance, bool inbound)
	{
		// One record is reserved for the phony record of an inbound tunnel.
		const int maxHops = inbound ? MAX_INBOUND_HOPS : MAX_NUM_RECORDS;
		if (hops < 0) hops = 0;
		if (hops > maxHops) hops = maxHops;
		// A negative variance may shorten a path but never to zero hops: an
		// empty path is not a tunnel. A positive variance must not push the
		// longest path past the record table.
		if (variance < 0)
		{
			if (hops + variance <= 0) variance = hops ? -hops + 1 : 0;
		}
		else if (variance > 0 && hops + variance > maxHops)
			variance = (hops < maxHops) ? maxHops - hops : 0;
	}

	int ComputeBuildRequestCount (int configured, int established, int pending, int consecutiveFailures)
	{
		if (configured <= 0) return 0;
		// Builds already waiting for a reply count against the target, so a
		// slow build round cannot be oversubscribed by the next tick.
		int requested = configured - established - pending;
		if (requested <= 0) return 0;
		// Java-like ladder: the first failure threshold halves the round, the
		// second reduces it to a single request. The count never reaches zero,
		// so a recovering network is always given another chance.
		int maximum = TUNNEL_POOL_MAX_NUM_BUILD_REQUESTS;
		if (consecutiveFailures >= 2 * TUNNEL_POOL_BUILD_FAILURES_TO_REDUCE)
			maximum = 1;
		else if (consecutiveFailures >= TUNNEL_POOL_BUILD_FAILURES_TO_REDUCE)
			maximum = 2;
		return std::min (requested, maximum);
	}

	int GetTunnelBuildBackoff (int consecutiveFailures)
	{
		if (consecutiveFailures < TUNNEL_POOL_BUILD_FAILURES_TO_REDUCE) return TUNNEL_POOL_BUILD_BACKOFF_BASE;
		int steps = consecutiveFailures / TUNNEL_POOL_BUILD_FAILURES_TO_REDUCE;
		// Five doublings from the 10 s base reach the cap; clamping the step
		// count there keeps the ladder monotonic and bounded.
		if (steps > 5) steps = 5;
		int delay = TUNNEL_POOL_BUILD_BACKOFF_BASE << steps;
		return std::min (delay, TUNNEL_POOL_BUILD_BACKOFF_MAX);
	}

	// Per-path test deadline. A test leaves through the outbound tunnel and
	// returns through the inbound one, so the budget is twice the one-way
	// estimate: TUNNEL_POOL_TEST_TIMEOUT_PER_HOP (2.5 s) per hop plus the locally
	// measured transport delay. The result is clamped to 7.5..15 s so a zero-hop
	// pair still gets a usable deadline and a long path cannot hold a test entry
	// open for minutes. Units: milliseconds.
	uint64_t GetTunnelTestTimeout (int numHops, int localDelayMs)
	{
		if (numHops < 0) numHops = 0;
		if (localDelayMs < 0) localDelayMs = 0;
		const int64_t perHop = static_cast<int64_t> (numHops) * TUNNEL_POOL_TEST_TIMEOUT_PER_HOP;
		int64_t timeout = 2 * (perHop + localDelayMs);
		if (timeout < TUNNEL_POOL_TEST_TIMEOUT_MIN) timeout = TUNNEL_POOL_TEST_TIMEOUT_MIN;
		if (timeout > TUNNEL_POOL_TEST_TIMEOUT_MAX) timeout = TUNNEL_POOL_TEST_TIMEOUT_MAX;
		return static_cast<uint64_t> (timeout);
	}

	bool ShouldSkipTunnelTest (uint64_t nowMs, uint64_t lastReceiveMs)
	{
		if (!lastReceiveMs || nowMs < lastReceiveMs) return false;
		return nowMs - lastReceiveMs < static_cast<uint64_t> (TUNNEL_POOL_INBOUND_ACTIVITY_SKIP_TEST) * 1000;
	}

	TunnelPeerRole GetTunnelPeerRole (int index, int numPeers, bool inbound)
	{
		if (index <= 0 && inbound) return TunnelPeerRole::Gateway;
		if (index == numPeers - 1) return TunnelPeerRole::Endpoint;
		return TunnelPeerRole::Middle;
	}

	bool IsRouterUsableForTunnel (const i2p::data::RouterInfo & router, const i2p::data::RouterInfo * compatibleWith,
		TunnelPeerRole role, bool inbound, bool clientTunnel)
	{
		const auto & hash = router.GetIdentHash ();
		if (router.IsHidden ()) return false;
		if (!router.IsECIES ()) return false;
		// Operator blocklist and in-memory runtime bans: a reused identity must
		// not bypass an entry that arrived after the tunnel was built.
		if (i2p::data::IsOperatorBlockedRouter (hash) || i2p::data::IsRuntimeRouterBanned (hash)) return false;
		if (router.IsUnreachable ()) return false;
		// A descriptor with no usable transport can never carry a tunnel hop.
		if (!router.HasValidAddresses ()) return false;
		// Never create or load a profile from the selection path. The bad-peer
		// rule matches SelectNextHop(); profile-level unreachability is a short
		// lived transport signal and is deliberately left to the connection
		// layer so a transient outage does not empty every candidate set.
		if (auto profile = router.GetProfileIfExists ())
		{
			if (profile->IsBad ()) return false;
		}
		// General version floor identical to NetDb random selection; a
		// high-bandwidth peer is exempt, exactly as in NetDb::ManageRouterInfos.
		if (router.GetVersion () < i2p::data::NETDB_MIN_ALLOWED_VERSION && !router.IsHighBandwidth ()) return false;
		if (router.IsHighCongestion (clientTunnel)) return false;
		switch (role)
		{
		case TunnelPeerRole::Endpoint:
			// The endpoint of an outbound tunnel must be reachable by IPv4,
			// matching the NetDb endpoint filter.
			if (!router.IsV4 ()) return false;
			break;
		case TunnelPeerRole::Gateway:
			// An inbound gateway is reached directly and must publish an IPv4
			// transport, matching the first-hop rule of StandardSelectPeers.
			if (inbound && !(router.IsV4 () && router.IsPublished (true))) return false;
			break;
		default: break;
		}
		if (compatibleWith)
		{
			// The local RouterInfo exists before its identity does, so never
			// dereference a missing identity while comparing peers.
			auto compatibleIdentity = compatibleWith->GetIdentity ();
			if (compatibleIdentity && router.GetIdentHash () == compatibleIdentity->GetIdentHash ()) return false;
			if (router.IsNAT2NATOnly (*compatibleWith)) return false;
			if (inbound ? (!compatibleWith->IsReachableFrom (router) || !router.GetCompatibleTransports (true))
						: !router.IsReachableFrom (*compatibleWith))
				return false;
			if (i2p::transport::transports.IsCheckReserved ()
				&& (router.IsSameFamily (*compatibleWith) || router.IsSameSubnet (*compatibleWith)))
				return false;
		}
		return true;
	}

	TunnelPeerSelectorHandle::TunnelPeerSelectorHandle (ITunnelPeerSelector * selector, std::shared_ptr<void> owner)
		: m_Selector (selector)
		, m_Owner (owner)
		, m_HasOwner (static_cast<bool> (owner))
	{
	}

	TunnelPeerSelectorHandle::Result TunnelPeerSelectorHandle::TrySelectPeers (
		Path & path, int hops, bool inbound) const
	{
		if (!m_Selector) return Result::NoSelector;
		if (!m_HasOwner) return m_Selector->SelectPeers (path, hops, inbound) ? Result::Accepted : Result::Rejected;
		// Pin the owner for the whole call. A plain expired()/lock() test before
		// the call would still race with the last owner release and leave the
		// selector dangling while SelectPeers() runs.
		auto owner = m_Owner.lock ();
		if (!owner) return Result::OwnerGone;
		return m_Selector->SelectPeers (path, hops, inbound) ? Result::Accepted : Result::Rejected;
	}

	TunnelPool::TunnelPool (int numInboundHops, int numOutboundHops, int numInboundTunnels, int numOutboundTunnels,
		int inboundVariance, int outboundVariance, bool isHighBandwidth)
		: m_NumInboundHops (numInboundHops)
		, m_NumOutboundHops (numOutboundHops)
		, m_NumInboundTunnels (numInboundTunnels)
		, m_NumOutboundTunnels (numOutboundTunnels)
		, m_InboundVariance (inboundVariance)
		, m_OutboundVariance (outboundVariance)
		, m_IsActive (true)
		, m_IsHighBandwidth (isHighBandwidth)
		, m_ConsecutiveBuildFailures (0)
		, m_NextBuildTime (0)
		, m_Rng (i2p::util::GetRngSeed ())
	{
		int inbound = m_NumInboundHops.load ();
		int inboundVarianceValue = m_InboundVariance.load ();
		ClampTunnelLength (inbound, inboundVarianceValue, true);
		m_NumInboundHops = inbound;
		m_InboundVariance = inboundVarianceValue;
		int outbound = m_NumOutboundHops.load ();
		int outboundVarianceValue = m_OutboundVariance.load ();
		ClampTunnelLength (outbound, outboundVarianceValue, false);
		m_NumOutboundHops = outbound;
		m_OutboundVariance = outboundVarianceValue;
		if (m_NumInboundTunnels > TUNNEL_POOL_MAX_INBOUND_TUNNELS_QUANTITY)
			m_NumInboundTunnels = TUNNEL_POOL_MAX_INBOUND_TUNNELS_QUANTITY;
		if (m_NumOutboundTunnels > TUNNEL_POOL_MAX_OUTBOUND_TUNNELS_QUANTITY)
			m_NumOutboundTunnels = TUNNEL_POOL_MAX_OUTBOUND_TUNNELS_QUANTITY;
		m_NextManageTime = i2p::util::GetSecondsSinceEpoch () + m_Rng () % TUNNEL_POOL_MANAGE_INTERVAL;
	}

	TunnelPool::~TunnelPool ()
	{
		DetachTunnels ();
	}

	std::shared_ptr<i2p::garlic::GarlicDestination> TunnelPool::GetLocalDestination () const
	{
		std::lock_guard<std::mutex> l (m_LocalDestinationMutex);
		return m_LocalDestination;
	}

	void TunnelPool::SetLocalDestination (std::shared_ptr<i2p::garlic::GarlicDestination> destination)
	{
		std::lock_guard<std::mutex> l (m_LocalDestinationMutex);
		m_LocalDestination = std::move (destination);
	}

	uint32_t TunnelPool::NextRng ()
	{
		std::lock_guard<std::mutex> l (m_RngMutex);
		return m_Rng ();
	}

	void TunnelPool::SetExplicitPeers (std::vector<i2p::data::IdentHash> explicitPeers)
	{
		std::lock_guard<std::mutex> lock (m_PeersConfigMutex);
		m_ExplicitPeers.swap (explicitPeers);
		int size = static_cast<int> (m_ExplicitPeers.size ());
		if (size > 0)
		{
			if (m_NumInboundHops > size)
			{
				m_NumInboundHops = size;
				LogPrint (
					eLogInfo, "Tunnels: Inbound tunnel length has been adjusted to ", size, " for explicit peers");
			}
			if (m_NumOutboundHops > size)
			{
				m_NumOutboundHops = size;
				LogPrint (
					eLogInfo, "Tunnels: Outbound tunnel length has been adjusted to ", size, " for explicit peers");
			}
			m_NumInboundTunnels = 1;
			m_NumOutboundTunnels = 1;
		}
	}

	void TunnelPool::SetTrustedRouters (std::vector<i2p::data::IdentHash> routers)
	{
		std::lock_guard<std::mutex> lock (m_PeersConfigMutex);
		m_TrustedRouters.swap (routers);
	}

	void TunnelPool::SetInboundPeerOrderingKey (const uint8_t * key)
	{
		m_InboundPeerOrdering.SetKey (key);
	}

	void TunnelPool::SetOutboundPeerOrderingKey (const uint8_t * key)
	{
		m_OutboundPeerOrdering.SetKey (key);
	}

	void TunnelPool::DetachTunnels ()
	{
		{
			std::unique_lock<std::mutex> l (m_InboundTunnelsMutex);
			for (auto & it : m_InboundTunnels) it->SetTunnelPool (nullptr);
			m_InboundTunnels.clear ();
		}
		{
			std::unique_lock<std::mutex> l (m_OutboundTunnelsMutex);
			for (auto & it : m_OutboundTunnels) it->SetTunnelPool (nullptr);
			m_OutboundTunnels.clear ();
		}
		{
			std::unique_lock<std::mutex> l (m_TestsMutex);
			m_Tests.clear ();
		}
		// A lifecycle detach starts from a clean build state: a pool that was
		// backing off when the worker stopped must not inherit the delay (or
		// the failure ladder) on the next Start().
		m_ConsecutiveBuildFailures.store (0, std::memory_order_relaxed);
		m_NextBuildTime.store (0, std::memory_order_relaxed);
	}

	bool TunnelPool::Reconfigure (int inHops, int outHops, int inQuant, int outQuant)
	{
		if (inHops >= 0 && outHops >= 0 && inQuant > 0 && outQuant > 0)
		{
			// Re-clamp the stored variance too: a length change must not let the
			// next selection exceed the record table (inbound also reserves the
			// phony record slot).
			int inboundVariance = m_InboundVariance.load (std::memory_order_relaxed);
			int outboundVariance = m_OutboundVariance.load (std::memory_order_relaxed);
			ClampTunnelLength (inHops, inboundVariance, true);
			ClampTunnelLength (outHops, outboundVariance, false);
			m_NumInboundHops = inHops;
			m_NumOutboundHops = outHops;
			m_InboundVariance = inboundVariance;
			m_OutboundVariance = outboundVariance;
			m_NumInboundTunnels = inQuant > TUNNEL_POOL_MAX_INBOUND_TUNNELS_QUANTITY
				? TUNNEL_POOL_MAX_INBOUND_TUNNELS_QUANTITY
				: inQuant;
			m_NumOutboundTunnels = outQuant > TUNNEL_POOL_MAX_OUTBOUND_TUNNELS_QUANTITY
				? TUNNEL_POOL_MAX_OUTBOUND_TUNNELS_QUANTITY
				: outQuant;
			return true;
		}
		return false;
	}

	void TunnelPool::TunnelCreated (std::shared_ptr<InboundTunnel> createdTunnel)
	{
		if (!m_IsActive) return;
		{
			std::unique_lock<std::mutex> l (m_InboundTunnelsMutex);
			if (createdTunnel->IsRecreated ())
			{
				// A recreated tunnel replaces the path it was built from.
				// RecreateInboundTunnel() marks the new tunnel, and creating it
				// clears the flag here while the first still-marked member with
				// the same next hop moves to Expiring instead of being erased:
				// the expiry path owns removal, so in-flight traffic is not cut
				// at the moment the replacement is published. Only one
				// predecessor is matched per creation, so a pool full of
				// recreated tunnels converges instead of expiring everything at
				// once.
				createdTunnel->SetRecreated (false);
				for (auto & it : m_InboundTunnels)
					if (it->IsRecreated () && it->GetNextIdentHash () == createdTunnel->GetNextIdentHash ())
					{
						it->SetState (eTunnelStateExpiring);
						break;
					}
			}
			m_InboundTunnels.insert (createdTunnel);
		}
		if (auto destination = GetLocalDestination ()) destination->SetLeaseSetUpdated (true);
	}

	void TunnelPool::TunnelExpired (std::shared_ptr<InboundTunnel> expiredTunnel)
	{
		if (expiredTunnel)
		{
			expiredTunnel->SetTunnelPool (std::shared_ptr<TunnelPool> { });
			{
				std::unique_lock<std::mutex> l (m_TestsMutex);
				for (auto & it : m_Tests)
					if (it.second.inbound == expiredTunnel) it.second.inbound = nullptr;
			}

			std::unique_lock<std::mutex> l (m_InboundTunnelsMutex);
			m_InboundTunnels.erase (expiredTunnel);
		}
	}

	void TunnelPool::TunnelCreated (std::shared_ptr<OutboundTunnel> createdTunnel)
	{
		if (!m_IsActive) return;
		{
			std::unique_lock<std::mutex> l (m_OutboundTunnelsMutex);
			m_OutboundTunnels.insert (createdTunnel);
		}
	}

	void TunnelPool::TunnelExpired (std::shared_ptr<OutboundTunnel> expiredTunnel)
	{
		if (expiredTunnel)
		{
			expiredTunnel->SetTunnelPool (nullptr);
			{
				std::unique_lock<std::mutex> l (m_TestsMutex);
				for (auto & it : m_Tests)
					if (it.second.outbound == expiredTunnel) it.second.outbound = nullptr;
			}

			std::unique_lock<std::mutex> l (m_OutboundTunnelsMutex);
			m_OutboundTunnels.erase (expiredTunnel);
		}
	}

	std::vector<std::shared_ptr<InboundTunnel>> TunnelPool::GetInboundTunnels (int num) const
	{
		std::vector<std::shared_ptr<InboundTunnel>> v;
		int i = 0;
		std::shared_ptr<InboundTunnel> slowTunnel;
		std::unique_lock<std::mutex> l (m_InboundTunnelsMutex);
		for (const auto & it : m_InboundTunnels)
		{
			if (i >= num) break;
			// A tunnel the pool marked test-failed must never be advertised in
			// a LeaseSet: leases are a commitment that the path works.
			if (it->GetState () == eTunnelStateTestFailed) continue;
			if (it->IsEstablished ())
			{
				if (it->IsSlow () && !slowTunnel)
					slowTunnel = it;
				else
				{
					v.push_back (it);
					i++;
				}
			}
		}
		if (slowTunnel && (int)v.size () < (num / 2 + 1)) v.push_back (slowTunnel);
		return v;
	}

	std::shared_ptr<OutboundTunnel> TunnelPool::GetNextOutboundTunnel (
		std::shared_ptr<OutboundTunnel> excluded, i2p::data::RouterInfo::CompatibleTransports compatible)
	{
		std::unique_lock<std::mutex> l (m_OutboundTunnelsMutex);
		return GetNextTunnel (m_OutboundTunnels, excluded, compatible);
	}

	std::shared_ptr<InboundTunnel> TunnelPool::GetNextInboundTunnel (
		std::shared_ptr<InboundTunnel> excluded, i2p::data::RouterInfo::CompatibleTransports compatible)
	{
		std::unique_lock<std::mutex> l (m_InboundTunnelsMutex);
		return GetNextTunnel (m_InboundTunnels, excluded, compatible);
	}

	template <class TTunnels>
	typename TTunnels::value_type TunnelPool::GetNextTunnel (TTunnels & tunnels, typename TTunnels::value_type excluded,
		i2p::data::RouterInfo::CompatibleTransports compatible)
	{
		if (tunnels.empty ()) return nullptr;
		uint32_t ind = static_cast<uint32_t> (NextRng () % (tunnels.size () / 2 + 1)), i = 0;
		bool skipped = false;
		typename TTunnels::value_type tunnel = nullptr;
		for (const auto & it : tunnels)
		{
			if (it->IsEstablished () && it != excluded && (compatible & it->GetFarEndTransports ()))
			{
				if (it->IsSlow ()
					|| (HasLatencyRequirement () && it->LatencyIsKnown ()
						&& !it->LatencyFitsRange (m_MinLatency, m_MaxLatency)))
				{
					i++;
					skipped = true;
					continue;
				}
				tunnel = it;
				i++;
			}
			if (i > ind && tunnel) break;
		}
		if (!tunnel && skipped)
		{
			ind = NextRng () % (tunnels.size () / 2 + 1), i = 0;
			for (const auto & it : tunnels)
			{
				if (it->IsEstablished () && it != excluded)
				{
					tunnel = it;
					i++;
				}
				if (i > ind && tunnel) break;
			}
		}
		if (!tunnel && excluded && excluded->IsEstablished ()) tunnel = excluded;
		return tunnel;
	}

	std::pair<std::shared_ptr<OutboundTunnel>, bool> TunnelPool::GetNewOutboundTunnel (
		std::shared_ptr<OutboundTunnel> old)
	{
		if (old && old->IsEstablished ()) return std::make_pair (old, false);
		std::shared_ptr<OutboundTunnel> tunnel;
		bool freshTunnel = false;
		if (old)
		{
			std::unique_lock<std::mutex> l (m_OutboundTunnelsMutex);
			for (const auto & it : m_OutboundTunnels)
				if (it->IsEstablished () && old->GetEndpointIdentHash () == it->GetEndpointIdentHash ())
				{
					tunnel = it;
					break;
				}
		}

		if (!tunnel)
		{
			tunnel = GetNextOutboundTunnel ();
			freshTunnel = true;
		}
		return std::make_pair (tunnel, freshTunnel);
	}

	void TunnelPool::CreateTunnels ()
	{
		CreateTunnels (i2p::util::GetSecondsSinceEpoch ());
	}

	void TunnelPool::CreateTunnels (uint64_t ts)
	{
		if (!IsActive ()) return;
		const int failures = m_ConsecutiveBuildFailures.load (std::memory_order_relaxed);
		// The ladder is also the minimum delay between build rounds: a pool
		// that keeps failing must not start a new round on every tick.
		if (ts < m_NextBuildTime.load (std::memory_order_relaxed)) return;
		bool attempted = false;
		int num = 0;
		{
			std::unique_lock<std::mutex> l (m_OutboundTunnelsMutex);
			for (const auto & it : m_OutboundTunnels)
				if (it->IsEstablished ()) num++;
		}
		const int pendingOutbound = static_cast<int> (tunnels.GetNumPendingTunnels (this, false));
		num = ComputeBuildRequestCount (
			m_NumOutboundTunnels.load (std::memory_order_relaxed), num, pendingOutbound, failures);
		if (num > 0)
		{
			// CreateOutboundTunnel() must run without any pool mutex: it can
			// publish a zero-hop tunnel synchronously through TunnelCreated().
			attempted = true;
			for (int i = 0; i < num; i++) CreateOutboundTunnel (ts);
		}

		num = 0;
		{
			std::unique_lock<std::mutex> l (m_InboundTunnelsMutex);
			for (const auto & it : m_InboundTunnels)
				if (it->IsEstablished ()) num++;
		}
		const int pendingInbound = static_cast<int> (tunnels.GetNumPendingTunnels (this, true));
		num = ComputeBuildRequestCount (
			m_NumInboundTunnels.load (std::memory_order_relaxed), num, pendingInbound, failures);
		bool createdInbound = false;
		if (num > 0)
		{
			attempted = true;
			createdInbound = true;
			for (int i = 0; i < num; i++) CreateInboundTunnel (ts);
		}

		if (attempted) m_NextBuildTime.store (ts + GetTunnelBuildBackoff (failures), std::memory_order_relaxed);

		if (createdInbound && m_NumInboundHops.load (std::memory_order_relaxed) <= 0)
		{
			if (auto destination = GetLocalDestination ()) // zero hops IB
				destination->SetLeaseSetUpdated (true); // update LeaseSet immediately
		}
	}

	void TunnelPool::TestTunnels (uint64_t ts)
	{
		const uint64_t nowMs = i2p::util::GetMillisecondsSinceEpoch ();
		// Tests are kept across ticks: a reply may legitimately arrive after
		// this call. Only a test whose own deadline has passed counts as a
		// failed test.
		std::vector<TunnelTest> expiredTests;
		{
			std::unique_lock<std::mutex> l (m_TestsMutex);
			for (auto it = m_Tests.begin (); it != m_Tests.end ();)
			{
				if (it->second.expiration <= nowMs)
				{
					expiredTests.push_back (it->second);
					it = m_Tests.erase (it);
				}
				else
					++it;
			}
		}

		// Phase one: decide under the pool locks, without ever creating a
		// tunnel. CreateInboundTunnel()/CreateOutboundTunnel() publish zero-hop
		// tunnels synchronously through TunnelCreated(), which takes the same
		// container mutex, so calling them here would self-deadlock.
		bool createOutbound = false, createInbound = false, inboundRemoved = false;
		for (auto & test : expiredTests)
		{
			if (test.outbound && !test.outbound->IsFailed () && test.outbound->GetState () != eTunnelStateExpiring)
			{
				const int strikes = test.outbound->AddTestStrike ();
				LogPrint (eLogWarning, "Tunnels: Test of outbound tunnel ", test.outbound->GetTunnelID (),
					" failed, strike ", strikes, " of ", TUNNEL_POOL_TEST_STRIKES);
				if (strikes >= TUNNEL_POOL_TEST_STRIKES)
				{
					test.outbound->SetState (eTunnelStateFailed);
					std::unique_lock<std::mutex> l (m_OutboundTunnelsMutex);
					if (m_OutboundTunnels.erase (test.outbound))
					{
						createOutbound = true;
						LogPrint (
							eLogDebug, "Tunnels: Outbound tunnel ", test.outbound->GetTunnelID (), " failed its tests");
					}
				}
			}
			if (test.inbound && !test.inbound->IsFailed () && test.inbound->GetState () != eTunnelStateExpiring)
			{
				const int strikes = test.inbound->AddTestStrike ();
				LogPrint (eLogWarning, "Tunnels: Test of inbound tunnel ", test.inbound->GetTunnelID (),
					" failed, strike ", strikes, " of ", TUNNEL_POOL_TEST_STRIKES);
				if (strikes >= TUNNEL_POOL_TEST_STRIKES)
				{
					test.inbound->SetState (eTunnelStateFailed);
					{
						std::unique_lock<std::mutex> l (m_InboundTunnelsMutex);
						if (m_InboundTunnels.erase (test.inbound))
						{
							createInbound = true;
							inboundRemoved = true;
							LogPrint (eLogDebug, "Tunnels: Inbound tunnel ", test.inbound->GetTunnelID (),
								" failed its tests");
						}
					}
				}
			}
		}

		// A tunnel that the build path already failed while a test was in
		// flight is dead too; keep the pool consistent with the container.
		{
			std::unique_lock<std::mutex> l (m_OutboundTunnelsMutex);
			for (auto it = m_OutboundTunnels.begin (); it != m_OutboundTunnels.end ();)
			{
				if ((*it)->IsFailed ())
				{
					it = m_OutboundTunnels.erase (it);
					createOutbound = true;
				}
				else
					++it;
			}
		}
		{
			std::unique_lock<std::mutex> l (m_InboundTunnelsMutex);
			for (auto it = m_InboundTunnels.begin (); it != m_InboundTunnels.end ();)
			{
				if ((*it)->IsFailed ())
				{
					it = m_InboundTunnels.erase (it);
					createInbound = true;
					inboundRemoved = true;
				}
				else
					++it;
			}
		}

		// Phase two: create and publish outside every pool lock.
		if (createOutbound) CreateOutboundTunnel (ts);
		if (createInbound) CreateInboundTunnel (ts);
		if (inboundRemoved || createInbound)
		{
			if (auto destination = GetLocalDestination ()) destination->SetLeaseSetUpdated (true);
		}

		// new tests
		auto destination = GetLocalDestination ();
		if (!destination) return;
		std::vector<std::pair<std::shared_ptr<OutboundTunnel>, std::shared_ptr<InboundTunnel>>> newTests;
		// Snapshots: the containers can be mutated by tunnel build replies and
		// by DetachTunnels() while this thread works.
		std::vector<std::shared_ptr<OutboundTunnel>> outboundTunnels;
		{
			std::unique_lock<std::mutex> l (m_OutboundTunnelsMutex);
			for (auto & it : m_OutboundTunnels)
				if (it->IsEstablished ()) outboundTunnels.push_back (it);
		}
		if (outboundTunnels.empty ()) return;
		{
			std::lock_guard<std::mutex> l (m_RngMutex);
			std::shuffle (outboundTunnels.begin (), outboundTunnels.end (), m_Rng);
		}
		std::vector<std::shared_ptr<InboundTunnel>> inboundTunnels;
		{
			std::unique_lock<std::mutex> l (m_InboundTunnelsMutex);
			for (auto & it : m_InboundTunnels)
				// Recent traffic is proof of life; testing again would only add
				// load to a working tunnel and delay its teardown on failure.
				if (it->IsEstablished () && !ShouldSkipTunnelTest (nowMs, it->GetLastReceiveTimeMs ()))
					inboundTunnels.push_back (it);
		}
		if (inboundTunnels.empty ()) return;
		{
			std::lock_guard<std::mutex> l (m_RngMutex);
			std::shuffle (inboundTunnels.begin (), inboundTunnels.end (), m_Rng);
		}
		auto it1 = outboundTunnels.begin ();
		auto it2 = inboundTunnels.begin ();
		while (it1 != outboundTunnels.end () && it2 != inboundTunnels.end ())
		{
			newTests.push_back (std::make_pair (*it1, *it2));
			++it1;
			++it2;
		}
		for (auto & it : newTests)
		{
			uint32_t msgID;
			RAND_bytes ((uint8_t *)&msgID, 4);
			TunnelTest test;
			test.outbound = it.first;
			test.inbound = it.second;
			test.sentTime = nowMs;
			test.expiration = nowMs
				+ GetTunnelTestTimeout (
					it.first->GetNumHops () + it.second->GetNumHops (), i2p::transport::transports.GetLocalDelay ());
			{
				std::unique_lock<std::mutex> l (m_TestsMutex);
				m_Tests[msgID] = test;
			}
			auto msg = CreateTunnelTestMsg (msgID);
			auto outbound = it.first;
			auto s = shared_from_this ();
			msg->onDrop = [msgID, outbound, s] ()
			{
				// if test msg dropped locally it's outbound tunnel to blame
				outbound->SetState (eTunnelStateFailed);
				{
					std::unique_lock<std::mutex> l (s->m_TestsMutex);
					s->m_Tests.erase (msgID);
				}
				{
					std::unique_lock<std::mutex> l (s->m_OutboundTunnelsMutex);
					s->m_OutboundTunnels.erase (outbound);
				}
			};
			// encrypt
			uint8_t key[32];
			RAND_bytes (key, 32);
			uint64_t tag;
			RAND_bytes ((uint8_t *)&tag, 8);
			destination->SubmitECIESx25519Key (key, tag);
			msg = i2p::garlic::WrapECIESX25519Message (msg, key, tag);
			// send
			outbound->SendTunnelDataMsgTo (it.second->GetNextIdentHash (), it.second->GetNextTunnelID (), msg);
		}
	}

	void TunnelPool::ManageTunnels (uint64_t ts)
	{
		if (!IsActive ()) return;
		if (ts > m_NextManageTime
			|| ts + 2 * TUNNEL_POOL_MANAGE_INTERVAL < m_NextManageTime) // in case if clock was adjusted
		{
			auto destination = GetLocalDestination ();
			if (!destination || !destination->IsIdling ())
			{
				CreateTunnels (ts);
				TestTunnels (ts);
				m_InboundPeerOrdering.CleanUp (ts);
				m_OutboundPeerOrdering.CleanUp (ts);
			}
			m_NextManageTime = ts + TUNNEL_POOL_MANAGE_INTERVAL + (tunnels.GetRng () % TUNNEL_POOL_MANAGE_INTERVAL) / 2;
		}
	}

	void TunnelPool::ProcessGarlicMessage (std::shared_ptr<I2NPMessage> msg)
	{
		auto destination = GetLocalDestination ();
		if (destination)
			destination->ProcessGarlicMessage (msg);
		else
			LogPrint (eLogWarning, "Tunnels: Local destination doesn't exist, dropped");
	}

	void TunnelPool::ProcessDeliveryStatus (std::shared_ptr<I2NPMessage> msg)
	{
		auto destination = GetLocalDestination ();
		if (destination)
			destination->ProcessDeliveryStatusMessage (msg);
		else
			LogPrint (eLogWarning, "Tunnels: Local destination doesn't exist, dropped");
	}

	void TunnelPool::ProcessTunnelTest (std::shared_ptr<I2NPMessage> msg)
	{
		const uint8_t * buf = msg->GetPayload ();
		uint32_t msgID = bufbe32toh (buf);
		buf += 4;
		uint64_t timestamp = bufbe64toh (buf);

		ProcessTunnelTest (msgID, timestamp);
	}

	bool TunnelPool::ProcessTunnelTest (uint32_t msgID, uint64_t timestamp)
	{
		TunnelTest test;
		bool found = false;
		{
			std::unique_lock<std::mutex> l (m_TestsMutex);
			auto it = m_Tests.find (msgID);
			if (it != m_Tests.end ())
			{
				found = true;
				test = it->second;
				m_Tests.erase (it);
			}
		}
		if (found)
		{
			int dlt = static_cast<int> ((uint64_t)i2p::util::GetMonotonicMicroseconds () - (int64_t)timestamp);
			LogPrint (eLogDebug, "Tunnels: Test of ", msgID, " successful. ", dlt, " microseconds");
			if (dlt < 0) dlt = 0; // should not happen
			int numHops = 0;
			if (test.outbound) numHops += test.outbound->GetNumHops ();
			if (test.inbound) numHops += test.inbound->GetNumHops ();
			// The round trip covers both paths, so each tunnel is charged only its
			// share of the measured time (hops of that tunnel over the total hops).
			// A zero-hop tunnel has no share, hence the dlt/2 fallback, and the
			// sample is added to the tunnel's mean rather than replacing it.
			// A successful test clears the strike ladder and restores a tunnel
			// that an earlier test had demoted.
			if (test.outbound)
			{
				test.outbound->ResetTestStrikes ();
				if (test.outbound->GetState () != eTunnelStateExpiring)
					test.outbound->SetState (eTunnelStateEstablished);
				// update latency
				int latency = 0;
				if (numHops) latency = dlt * test.outbound->GetNumHops () / numHops;
				if (!latency) latency = dlt / 2;
				test.outbound->AddLatencySample (latency);
			}
			if (test.inbound)
			{
				test.inbound->ResetTestStrikes ();
				if (test.inbound->GetState () != eTunnelStateExpiring) test.inbound->SetState (eTunnelStateEstablished);
				// update latency
				int latency = 0;
				if (numHops) latency = dlt * test.inbound->GetNumHops () / numHops;
				if (!latency) latency = dlt / 2;
				test.inbound->AddLatencySample (latency);
			}
		}
		return found;
	}

	bool TunnelPool::IsExploratory () const
	{
		return i2p::tunnel::tunnels.GetExploratoryPool () == shared_from_this ();
	}

	// Hop selection first tries the client-tunnel candidate set (the
	// high-bandwidth variant when this pool serves a high-bandwidth router), then
	// falls back once to the general random set, so one exhausted filter cannot
	// fail an entire path build. The pool's peer ordering key keeps the
	// per-pool peer order stable across builds without pinning the same peers
	// forever. When the previous hop is this router - the first hop of the path -
	// a peer whose subnet already has too many established connections is
	// refused; only the transport layer can decide that.
	std::shared_ptr<const i2p::data::RouterInfo> TunnelPool::SelectNextHop (
		std::shared_ptr<const i2p::data::RouterInfo> prevHop, bool reverse, bool endpoint)
	{
		bool tryClient = !IsExploratory () && !i2p::context.IsLimitedConnectivity ();
		std::shared_ptr<const i2p::data::RouterInfo> hop;
		for (int i = 0; i < TUNNEL_POOL_MAX_HOP_SELECTION_ATTEMPTS; i++)
		{
			hop = tryClient
				? (m_IsHighBandwidth ? i2p::data::netdb.GetHighBandwidthRandomRouter (prevHop, reverse, endpoint,
										   reverse ? &m_InboundPeerOrdering : &m_OutboundPeerOrdering)
									 : i2p::data::netdb.GetRandomRouter (prevHop, reverse, endpoint, true,
										   reverse ? &m_InboundPeerOrdering : &m_OutboundPeerOrdering))
				: i2p::data::netdb.GetRandomRouter (prevHop, reverse, endpoint, false);
			if (hop)
			{
				if ((!hop->HasProfile () || !hop->GetProfile ()->IsBad ())
					&& (prevHop != i2p::context.GetSharedRouterInfo ()
						|| !i2p::transport::transports.IsTooManyConnectionsFromSubnet (hop)))
					break;
			}
			else if (tryClient)
				tryClient = false;
			else
				return nullptr;
		}
		return hop;
	}

	bool TunnelPool::StandardSelectPeers (Path & path, int numHops, bool inbound, SelectHopFunc nextHop)
	{
		int start = 0;
		std::shared_ptr<const i2p::data::RouterInfo> prevHop = i2p::context.GetSharedRouterInfo ();
		bool hasTrustedRouters;
		{
			std::lock_guard<std::mutex> lock (m_PeersConfigMutex);
			hasTrustedRouters = !m_TrustedRouters.empty ();
		}
		if (i2p::transport::transports.RoutesRestricted () || hasTrustedRouters)
		{
			/** if routes are restricted or trusted prepend trusted first hop */
			auto hop
				= hasTrustedRouters ? SelectTrustedRouter (inbound) : i2p::transport::transports.GetRestrictedPeer ();
			if (!hop) return false;
			path.Add (hop);
			prevHop = hop;
			start++;
		}
		else if (i2p::transport::transports.GetNumPeers () > 100
			|| (inbound
				&& (i2p::transport::transports.GetNumPeers () > 25
					|| (i2p::context.IsLimitedConnectivity () && i2p::transport::transports.GetNumPeers () > 0))))
		{
			auto r
				= i2p::transport::transports.GetRandomPeer (m_IsHighBandwidth && !i2p::context.IsLimitedConnectivity (),
					inbound ? &m_InboundPeerOrdering : &m_OutboundPeerOrdering);
			if (r && r->IsECIES () && (!r->HasProfile () || !r->GetProfile ()->IsBad ())
				&& (numHops > 1
					|| (r->IsV4 () && (!inbound || r->IsPublished (true))))) // first inbound must be published ipv4
			{
				prevHop = r;
				path.Add (r);
				start++;
			}
		}

		auto isDuplicate = [&path] (const std::shared_ptr<const i2p::data::RouterInfo> & hop) -> bool
		{
			if (!hop) return false;
			for (const auto & peer : path.peers)
				if (peer && peer->GetIdentHash () == hop->GetIdentHash ()) return true;
			return false;
		};
		const bool clientTunnel = !IsExploratory ();
		for (int i = start; i < numHops; i++)
		{
			const auto role = GetTunnelPeerRole (i, numHops, inbound);
			std::shared_ptr<const i2p::data::RouterInfo> hop;
			for (int attempt = 0; attempt < TUNNEL_POOL_MAX_HOP_SELECTION_ATTEMPTS; ++attempt)
			{
				hop = nextHop (prevHop, inbound, i == numHops - 1);
				if (!hop) break;
				// The same admission predicate the reuse path uses; NetDb has
				// usually applied it already, this keeps the two paths equal.
				if (!IsRouterUsableForTunnel (*hop, prevHop.get (), role, inbound, clientTunnel))
				{
					LogPrint (eLogDebug, "Tunnels: Rejecting peer ",
						i2p::data::GetIdentHashAbbreviation (hop->GetIdentHash ()), " for the tunnel path");
					hop = nullptr;
					continue;
				}
				if (!isDuplicate (hop)) break;
				LogPrint (eLogDebug, "Tunnels: Rejecting duplicate peer in tunnel path");
				hop = nullptr;
			}
			if (!hop && i == start) // if no suitable peer found for the first hop, try already connected
			{
				LogPrint (eLogInfo, "Tunnels: Can't select next hop for a tunnel. Trying already connected");
				hop = i2p::transport::transports.GetRandomPeer (
					false, inbound ? &m_InboundPeerOrdering : &m_OutboundPeerOrdering);
				// Compare against the hop this peer would actually connect to,
				// not against our own RouterInfo, which is only a stand-in at
				// the start of a path.
				if (hop
					&& (!hop->IsECIES ()
						|| !IsRouterUsableForTunnel (*hop, prevHop.get (), role, inbound, clientTunnel)))
					hop = nullptr;
			}
			if (hop && isDuplicate (hop)) hop = nullptr;
			if (!hop)
			{
				// The local RouterInfo has no identity until the router is
				// initialised, so never dereference it just to log.
				auto prevIdentity = prevHop ? prevHop->GetIdentity () : nullptr;
				LogPrint (eLogError, "Tunnels: Can't select next hop for ",
					prevIdentity ? prevIdentity->GetIdentHash ().ToBase64 () : std::string ("an unknown router"));
				return false;
			}
			prevHop = hop;
			path.Add (hop);
		}
		path.farEndTransports = prevHop->GetCompatibleTransports (inbound); // last hop
		return true;
	}

	bool TunnelPool::SelectPeers (Path & path, bool isInbound)
	{
		// explicit peers in use
		bool hasExplicitPeers;
		{
			std::lock_guard<std::mutex> lock (m_PeersConfigMutex);
			hasExplicitPeers = !m_ExplicitPeers.empty ();
		}
		if (hasExplicitPeers) return SelectExplicitPeers (path, isInbound);
		// calculate num hops
		int numHops;
		if (isInbound)
		{
			numHops = m_NumInboundHops.load (std::memory_order_relaxed);
			if (m_InboundVariance.load (std::memory_order_relaxed))
			{
				int offset = tunnels.GetRng () % (std::abs (m_InboundVariance.load (std::memory_order_relaxed)) + 1);
				if (m_InboundVariance.load (std::memory_order_relaxed) < 0) offset = -offset;
				numHops += offset;
			}
		}
		else
		{
			numHops = m_NumOutboundHops.load (std::memory_order_relaxed);
			if (m_OutboundVariance.load (std::memory_order_relaxed))
			{
				int offset = tunnels.GetRng () % (std::abs (m_OutboundVariance.load (std::memory_order_relaxed)) + 1);
				if (m_OutboundVariance.load (std::memory_order_relaxed) < 0) offset = -offset;
				numHops += offset;
			}
		}
		// Defense in depth: even a stale or misconfigured variance must never
		// produce a path longer than the build record table can describe.
		const int maxHops = isInbound ? MAX_INBOUND_HOPS : MAX_NUM_RECORDS;
		if (numHops > maxHops) numHops = maxHops;
		// peers is empty
		if (numHops <= 0) return true;
		// custom peer selector in use? Copy the registration under the lock and
		// call it outside: a selector is allowed to call back into the pool
		// without self-deadlocking on m_CustomPeerSelectorMutex, and the handle
		// pins its owner for the duration of the call.
		const auto selector = GetCustomPeerSelector ();
		switch (selector.TrySelectPeers (path, numHops, isInbound))
		{
		case TunnelPeerSelectorHandle::Result::Accepted: return true;
		case TunnelPeerSelectorHandle::Result::Rejected: return false;
		case TunnelPeerSelectorHandle::Result::OwnerGone:
			// The selector's owner was released: drop any partial path and fall
			// back to the standard algorithm instead of using half a tunnel.
			path.peers.clear ();
			path.farEndTransports = i2p::data::RouterInfo::eAllTransports;
			break;
		case TunnelPeerSelectorHandle::Result::NoSelector:
		default: break;
		}
		return StandardSelectPeers (path, numHops, isInbound,
			std::bind (
				&TunnelPool::SelectNextHop, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
	}

	bool TunnelPool::SelectExplicitPeers (Path & path, bool isInbound)
	{
		std::vector<i2p::data::IdentHash> explicitPeers;
		{
			std::lock_guard<std::mutex> lock (m_PeersConfigMutex);
			explicitPeers = m_ExplicitPeers;
		}
		if (explicitPeers.empty ()) return false;
		int numHops = isInbound ? m_NumInboundHops.load (std::memory_order_relaxed)
								: m_NumOutboundHops.load (std::memory_order_relaxed);
		const int maxHops = isInbound ? MAX_INBOUND_HOPS : MAX_NUM_RECORDS;
		if (numHops > maxHops) numHops = maxHops;
		if (numHops > (int)explicitPeers.size ()) numHops = static_cast<int> (explicitPeers.size ());
		for (int i = 0; i < numHops; i++)
		{
			auto & ident = explicitPeers[i];
			auto r = i2p::data::netdb.FindRouter (ident);
			if (r)
			{
				// Explicit peers are still subject to the admission predicate:
				// the operator names routers, not a licence to ignore a ban or
				// an unreachable peer.
				const auto role = GetTunnelPeerRole (i, numHops, isInbound);
				if (IsRouterUsableForTunnel (*r, nullptr, role, isInbound, m_IsHighBandwidth))
				{
					path.Add (r);
					if (i == numHops - 1) path.farEndTransports = r->GetCompatibleTransports (isInbound);
				}
				else
				{
					LogPrint (eLogError, "Tunnels: Explicit router ", ident.ToBase64 (), " is not usable for a tunnel");
					return false;
				}
			}
			else
			{
				LogPrint (eLogInfo, "Tunnels: Can't find router for ", ident.ToBase64 ());
				i2p::data::netdb.RequestDestination (ident);
				return false;
			}
		}
		return true;
	}

	std::shared_ptr<const i2p::data::RouterInfo> TunnelPool::SelectTrustedRouter (bool inbound) const
	{
		std::vector<i2p::data::IdentHash> trustedRouters;
		{
			std::lock_guard<std::mutex> lock (m_PeersConfigMutex);
			trustedRouters = m_TrustedRouters;
		}
		size_t count = trustedRouters.size ();
		if (!count) return nullptr;
		std::shared_ptr<const i2p::data::RouterInfo> r;
		int ind = tunnels.GetRng () % count;
		for (size_t i = 0; i < count; i++)
		{
			auto & ident = trustedRouters[(ind + i) % count];
			if (inbound)
			{
				if (i2p::transport::transports.IsConnected (ident))
				{
					r = i2p::data::netdb.FindRouter (ident);
					break;
				}
			}
			else
			{
				auto r1 = i2p::data::netdb.FindRouter (ident);
				if (!r1) i2p::data::netdb.RequestDestination (ident, nullptr); // request one if not in NetDB
				if (r1 && !r1->IsUnreachable () && r1->IsReachableFrom (i2p::context.GetRouterInfo ()))
				{
					r = r1;
					break;
				}
			}
		}
		return r;
	}

	void TunnelPool::CreateInboundTunnel (uint64_t ts)
	{
		LogPrint (eLogDebug, "Tunnels: Creating destination inbound tunnel...");
		Path path;
		if (SelectPeers (path, true))
		{
			// if we are out of inbound tunnels and last outbound was not create recently most like it's dead
			bool hasInbound = false, recentOutbound = false;
			{
				std::unique_lock<std::mutex> l (m_InboundTunnelsMutex);
				hasInbound = !m_InboundTunnels.empty ();
			}
			if (!hasInbound)
			{
				std::unique_lock<std::mutex> l (m_OutboundTunnelsMutex);
				recentOutbound = !m_OutboundTunnels.empty ()
					&& ts < (*m_OutboundTunnels.begin ())->GetCreationTime ()
							+ TUNNEL_POOL_TUNNEL_CREATED_RECENTLY_INTERVAL;
			}
			auto outboundTunnel
				= (hasInbound || recentOutbound) ? GetNextOutboundTunnel (nullptr, path.farEndTransports) : nullptr;
			if (!outboundTunnel) outboundTunnel = tunnels.GetNextOutboundTunnel ();
			std::shared_ptr<TunnelConfig> config;
			if (!path.peers.empty ())
			{
				path.Reverse ();
				config = std::make_shared<TunnelConfig> (path.peers, path.farEndTransports);
			}
			auto tunnel = tunnels.CreateInboundTunnel (config, shared_from_this (), outboundTunnel);
			if (tunnel->IsEstablished ()) // zero hops
				TunnelCreated (tunnel);
		}
		else
			LogPrint (eLogError, "Tunnels: Can't create inbound tunnel, no peers available");
	}

	void TunnelPool::RecreateInboundTunnel (std::shared_ptr<InboundTunnel> tunnel)
	{
		if (IsExploratory () || tunnel->IsSlow () || !CanReuseTunnelPeers (*tunnel)) // create a fresh path
		{
			CreateInboundTunnel (i2p::util::GetSecondsSinceEpoch ());
			return;
		}
		auto outboundTunnel = GetNextOutboundTunnel (nullptr, tunnel->GetFarEndTransports ());
		if (!outboundTunnel) outboundTunnel = tunnels.GetNextOutboundTunnel ();
		LogPrint (eLogDebug, "Tunnels: Re-creating destination inbound tunnel...");
		std::shared_ptr<TunnelConfig> config;
		auto peers = tunnel->GetPeers ();
		if (!peers.empty () && !ValidatePeers (peers, true))
		{
			CreateInboundTunnel (i2p::util::GetSecondsSinceEpoch ());
			return;
		}
		if (!peers.empty ()) config = std::make_shared<TunnelConfig> (peers, tunnel->GetFarEndTransports ());
		if (peers.empty () || config)
		{
			auto newTunnel = tunnels.CreateInboundTunnel (config, shared_from_this (), outboundTunnel);
			if (newTunnel->IsEstablished ()) // zero hops
				TunnelCreated (newTunnel);
			else
				newTunnel->SetRecreated (true);
		}
	}

	void TunnelPool::CreateOutboundTunnel (uint64_t ts)
	{
		LogPrint (eLogDebug, "Tunnels: Creating destination outbound tunnel...");
		Path path;
		if (SelectPeers (path, false))
		{
			// if we are out of outbound tunnels and last inbound was not create recently most like it's dead
			bool hasOutbound = false, recentInbound = false;
			{
				std::unique_lock<std::mutex> l (m_OutboundTunnelsMutex);
				hasOutbound = !m_OutboundTunnels.empty ();
			}
			if (!hasOutbound)
			{
				std::unique_lock<std::mutex> l (m_InboundTunnelsMutex);
				recentInbound = !m_InboundTunnels.empty ()
					&& ts < (*m_InboundTunnels.begin ())->GetCreationTime ()
							+ TUNNEL_POOL_TUNNEL_CREATED_RECENTLY_INTERVAL;
			}
			auto inboundTunnel
				= (hasOutbound || recentInbound) ? GetNextInboundTunnel (nullptr, path.farEndTransports) : nullptr;
			if (!inboundTunnel) inboundTunnel = tunnels.GetNextInboundTunnel ();
			if (!inboundTunnel)
			{
				LogPrint (eLogError, "Tunnels: Can't create outbound tunnel, no inbound tunnels found");
				return;
			}

			std::shared_ptr<TunnelConfig> config;
			if (!path.peers.empty ())
				config = std::make_shared<TunnelConfig> (path.peers, inboundTunnel->GetNextTunnelID (),
					inboundTunnel->GetNextIdentHash (), path.farEndTransports);

			std::shared_ptr<OutboundTunnel> tunnel;
			// TODO: implement it better
			tunnel = tunnels.CreateOutboundTunnel (config, inboundTunnel->GetTunnelPool ());
			if (tunnel)
			{
				tunnel->SetTunnelPool (shared_from_this ());
				if (tunnel->IsEstablished ()) // zero hops
					TunnelCreated (tunnel);
			}
		}
		else
			LogPrint (eLogError, "Tunnels: Can't create outbound tunnel, no peers available");
	}

	void TunnelPool::RecreateOutboundTunnel (std::shared_ptr<OutboundTunnel> tunnel)
	{
		if (IsExploratory () || tunnel->IsSlow () || !CanReuseTunnelPeers (*tunnel)) // create a fresh path
		{
			CreateOutboundTunnel (i2p::util::GetSecondsSinceEpoch ());
			return;
		}
		auto inboundTunnel = GetNextInboundTunnel (nullptr, tunnel->GetFarEndTransports ());
		if (!inboundTunnel) inboundTunnel = tunnels.GetNextInboundTunnel ();
		if (inboundTunnel)
		{
			LogPrint (eLogDebug, "Tunnels: Re-creating destination outbound tunnel...");
			std::shared_ptr<TunnelConfig> config;
			auto peers = tunnel->GetPeers ();
			if (!peers.empty () && !ValidatePeers (peers, false))
			{
				CreateOutboundTunnel (i2p::util::GetSecondsSinceEpoch ());
				return;
			}
			if (!peers.empty ())
				config = std::make_shared<TunnelConfig> (peers, inboundTunnel->GetNextTunnelID (),
					inboundTunnel->GetNextIdentHash (), tunnel->GetFarEndTransports ());
			if (peers.empty () || config)
			{
				auto newTunnel = tunnels.CreateOutboundTunnel (config, inboundTunnel->GetTunnelPool ());
				if (newTunnel)
				{
					newTunnel->SetTunnelPool (shared_from_this ());
					if (newTunnel->IsEstablished ()) // zero hops
						TunnelCreated (newTunnel);
				}
			}
		}
		else
			LogPrint (eLogDebug, "Tunnels: Can't re-create outbound tunnel, no inbound tunnels found");
	}

	void TunnelPool::SetCustomPeerSelector (ITunnelPeerSelector * selector, std::shared_ptr<void> owner)
	{
		TunnelPeerSelectorHandle handle (selector, std::move (owner));
		std::lock_guard<std::mutex> lock (m_CustomPeerSelectorMutex);
		m_CustomPeerSelector = handle;
	}

	void TunnelPool::UnsetCustomPeerSelector ()
	{
		SetCustomPeerSelector (nullptr);
	}

	bool TunnelPool::HasCustomPeerSelector ()
	{
		std::lock_guard<std::mutex> lock (m_CustomPeerSelectorMutex);
		return m_CustomPeerSelector.HasSelector ();
	}

	bool TunnelPool::CanReuseTunnelPeers (const Tunnel & tunnel) const
	{
		// Reusing the peers of a tunnel that already failed would rebuild the
		// same broken path; the test strikes are evidence that the path does
		// not pass traffic even though the build succeeded.
		if (tunnel.IsFailed () || tunnel.GetTestStrikes () > 0) return false;
		return true;
	}

	bool TunnelPool::ValidatePeers (
		std::vector<std::shared_ptr<const i2p::data::IdentityEx>> & peers, bool inbound) const
	{
		const bool clientTunnel = !IsExploratory ();
		const auto ourRouter = i2p::context.GetSharedRouterInfo ();
		const int numPeers = static_cast<int> (peers.size ());
		for (int i = 0; i < numPeers; i++)
		{
			auto & it = peers[static_cast<size_t> (i)];
			auto r = i2p::data::netdb.FindRouter (it->GetIdentHash ());
			if (!r) return false;
			// Reuse must pass exactly the same admission rules as fresh
			// selection: blocklist, runtime ban, reachability, version floor,
			// congestion, ECIES requirement and endpoint families.
			const auto role = GetTunnelPeerRole (i, numPeers, inbound);
			if (!IsRouterUsableForTunnel (*r, ourRouter.get (), role, inbound, clientTunnel)) return false;
			it = r->GetIdentity (); // use identity from updated RouterInfo
		}
		return true;
	}

	std::shared_ptr<InboundTunnel> TunnelPool::GetLowestLatencyInboundTunnel (
		std::shared_ptr<InboundTunnel> exclude) const
	{
		std::shared_ptr<InboundTunnel> tun = nullptr;
		std::unique_lock<std::mutex> lock (m_InboundTunnelsMutex);
		int min = 1000000;
		for (const auto & itr : m_InboundTunnels)
		{
			// The excluded tunnel takes no part in the comparison: selecting it
			// as the "lowest latency alternative" is exactly what the caller
			// asked to avoid.
			if (itr == exclude || !itr->LatencyIsKnown ()) continue;
			auto l = itr->GetMeanLatency ();
			if (l >= min) continue;
			min = l;
			tun = itr;
		}
		return tun;
	}

	std::shared_ptr<OutboundTunnel> TunnelPool::GetLowestLatencyOutboundTunnel (
		std::shared_ptr<OutboundTunnel> exclude) const
	{
		std::shared_ptr<OutboundTunnel> tun = nullptr;
		std::unique_lock<std::mutex> lock (m_OutboundTunnelsMutex);
		int min = 1000000;
		for (const auto & itr : m_OutboundTunnels)
		{
			if (itr == exclude || !itr->LatencyIsKnown ()) continue;
			auto l = itr->GetMeanLatency ();
			if (l >= min) continue;
			min = l;
			tun = itr;
		}
		return tun;
	}
}
}
