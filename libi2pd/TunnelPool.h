/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TunnelPool.
 *
 * Declares per-destination tunnel pools: peer selection (explicit, trusted or
 * exploratory), inbound/outbound tunnel replacement, latency requirements,
 * tunnel tests and I2CP reconfiguration. Numeric configuration and pool state
 * are synchronized for concurrent readers.
 *
 */

#ifndef TUNNEL_POOL__
#define TUNNEL_POOL__

#include <inttypes.h>
#include <atomic>
#include <algorithm>
#include <set>
#include <vector>
#include <utility>
#include <mutex>
#include <memory>
#include <random>
#include <functional>
#include <map>
#include "Identity.h"
#include "IdentMetrics.h"
#include "LeaseSet.h"
#include "RouterInfo.h"
#include "I2NPProtocol.h"
#include "TunnelBase.h"
#include "RouterContext.h"
#include "Garlic.h"

namespace i2p
{
namespace tunnel
{
	const int TUNNEL_POOL_MANAGE_INTERVAL = 10; // in seconds
	const int TUNNEL_POOL_TUNNEL_CREATED_RECENTLY_INTERVAL = 120; // in seconds
	const int TUNNEL_POOL_MAX_INBOUND_TUNNELS_QUANTITY = 16;
	const int TUNNEL_POOL_MAX_OUTBOUND_TUNNELS_QUANTITY = 16;
	const int TUNNEL_POOL_MAX_NUM_BUILD_REQUESTS = 3;
	const int TUNNEL_POOL_MAX_HOP_SELECTION_ATTEMPTS = 3;
	// A tunnel is failed only after this many tunnel tests missed their
	// deadline in a row. One lost test is ordinary packet loss; three in a row
	// is a dead tunnel. The counter lives in Tunnel, never in the state, so
	// inbound traffic cannot erase it (Java Tunnel.testJob).
	const int TUNNEL_POOL_TEST_STRIKES = 3;
	// A test reply is expected within a per-hop budget plus the local
	// transport delay, bounded so a dead tunnel is still replaced promptly.
	const int TUNNEL_POOL_TEST_TIMEOUT_PER_HOP = 2500; // in milliseconds
	const int TUNNEL_POOL_TEST_TIMEOUT_MIN = 7500; // in milliseconds
	const int TUNNEL_POOL_TEST_TIMEOUT_MAX = 15000; // in milliseconds
	// An inbound tunnel that delivered a message this recently is proven alive
	// and is not tested again: the test would only duplicate recent traffic.
	const int TUNNEL_POOL_INBOUND_ACTIVITY_SKIP_TEST = 40; // in seconds
	// Java-like build backoff ladder. After 4 consecutive failed builds fewer
	// requests are issued; 8 and 12 reduce the quantity further and raise the
	// delay between build rounds. The quantity never drops to zero and the
	// delay is capped so a recovering network is noticed quickly.
	const int TUNNEL_POOL_BUILD_FAILURES_TO_REDUCE = 4;
	const int TUNNEL_POOL_BUILD_BACKOFF_BASE = 10; // in seconds
	const int TUNNEL_POOL_BUILD_BACKOFF_MAX = 240; // in seconds

	class Tunnel;
	class InboundTunnel;
	class OutboundTunnel;

	typedef std::shared_ptr<const i2p::data::IdentityEx> Peer;
	struct Path
	{
		std::vector<Peer> peers;
		i2p::data::RouterInfo::CompatibleTransports farEndTransports = i2p::data::RouterInfo::eAllTransports;

		void Add (std::shared_ptr<const i2p::data::RouterInfo> r);
		void Reverse ();
	};

	/** interface for custom tunnel peer selection algorithm */
	struct ITunnelPeerSelector
	{
		virtual ~ITunnelPeerSelector () { };
		virtual bool SelectPeers (Path & peers, int hops, bool isInbound) = 0;
	};

	/** Which hop of a path a router is being admitted for. */
	enum class TunnelPeerRole : uint8_t
	{
		Gateway = 0,
		Middle,
		Endpoint
	};

	/**
	 * Peer admission rules shared by fresh path selection and tunnel reuse.
	 *
	 * NetDb random selection applies most of these centrally, but a recreated
	 * tunnel reuses identities chosen earlier and would otherwise bypass the
	 * operator blocklist, runtime bans, revocation, congestion, version floor
	 * and endpoint reachability rules. `compatibleWith` is the router the peer
	 * will connect to: the previous hop during selection, our own RouterInfo
	 * when a recreated path is validated. It may be null when no local identity
	 * is available (unit tests); the reachability and family checks are then
	 * skipped rather than guessed. `clientTunnel` selects the NetDb congestion
	 * rule: a client tunnel may not use a peer that is congested for client
	 * traffic, an exploratory tunnel is more tolerant.
	 */
	bool IsRouterUsableForTunnel (const i2p::data::RouterInfo & router, const i2p::data::RouterInfo * compatibleWith,
		TunnelPeerRole role, bool inbound, bool clientTunnel);

	/**
	 * Role of peer @a index in a path of @a numPeers.
	 *
	 * Matches the flags StandardSelectPeers passes to NetDb: the last hop is
	 * requested with `endpoint = true` in both directions, and the first hop of
	 * an inbound tunnel is the gateway a peer connects to.
	 */
	TunnelPeerRole GetTunnelPeerRole (int index, int numPeers, bool inbound);

	/** Clamp a configured hop count and variance to the build record table. */
	void ClampTunnelLength (int & hops, int & variance, bool inbound);

	/** Build requests this round may start, given the pool shape and failures. */
	int ComputeBuildRequestCount (int configured, int established, int pending, int consecutiveFailures);

	/** Delay before the next build round for @a consecutiveFailures. */
	int GetTunnelBuildBackoff (int consecutiveFailures);

	/** Deadline for a tunnel test reply, in milliseconds. */
	uint64_t GetTunnelTestTimeout (int numHops, int localDelayMs);

	/** True when recent inbound traffic makes a test redundant. */
	bool ShouldSkipTunnelTest (uint64_t nowMs, uint64_t lastReceiveMs);

	/**
	 * Lifetime-safe registration of a custom tunnel peer selector.
	 *
	 * The pool never owns the selector. A registration may carry an owner,
	 * normally the std::shared_ptr of the object that also implements
	 * ITunnelPeerSelector. TrySelectPeers() pins that owner with a strong
	 * reference for the whole call, so the selector cannot be destroyed while
	 * it runs even if the owner is released concurrently. The stored reference
	 * stays weak, which keeps destination/pool cycles out of the object graph.
	 */
	class TunnelPeerSelectorHandle final
	{
	public:
		enum class Result : uint8_t
		{
			NoSelector = 0, // nothing registered
			OwnerGone, // registered, but its owner was already released
			Rejected, // the selector ran and declined
			Accepted // the selector ran and filled the path
		};

		TunnelPeerSelectorHandle () = default;
		TunnelPeerSelectorHandle (ITunnelPeerSelector * selector, std::shared_ptr<void> owner);

		bool HasSelector () const noexcept { return m_Selector != nullptr; }
		bool HasOwner () const noexcept { return m_HasOwner; }

		Result TrySelectPeers (Path & path, int hops, bool inbound) const;

	private:
		ITunnelPeerSelector * m_Selector = nullptr;
		std::weak_ptr<void> m_Owner;
		bool m_HasOwner = false;
	};

	class TunnelPool : public std::enable_shared_from_this<TunnelPool> // per local destination
	{
		typedef std::function<std::shared_ptr<const i2p::data::RouterInfo> (
			std::shared_ptr<const i2p::data::RouterInfo>, bool, bool)>
			SelectHopFunc;

	public:
		TunnelPool (int numInboundHops, int numOutboundHops, int numInboundTunnels, int numOutboundTunnels,
			int inboundVariance, int outboundVariance, bool isHighBandwidth);
		~TunnelPool ();

		std::shared_ptr<i2p::garlic::GarlicDestination> GetLocalDestination () const;
		void SetLocalDestination (std::shared_ptr<i2p::garlic::GarlicDestination> destination);
		void SetExplicitPeers (std::vector<i2p::data::IdentHash> explicitPeers);
		void SetTrustedRouters (std::vector<i2p::data::IdentHash> routers);
		void SetInboundPeerOrderingKey (const uint8_t * key);
		void SetOutboundPeerOrderingKey (const uint8_t * key);

		void CreateTunnels ();
		void TunnelCreated (std::shared_ptr<InboundTunnel> createdTunnel);
		void TunnelExpired (std::shared_ptr<InboundTunnel> expiredTunnel);
		void TunnelCreated (std::shared_ptr<OutboundTunnel> createdTunnel);
		void TunnelExpired (std::shared_ptr<OutboundTunnel> expiredTunnel);
		void RecreateInboundTunnel (std::shared_ptr<InboundTunnel> tunnel);
		void RecreateOutboundTunnel (std::shared_ptr<OutboundTunnel> tunnel);
		std::vector<std::shared_ptr<InboundTunnel>> GetInboundTunnels (int num) const;
		std::shared_ptr<OutboundTunnel> GetNextOutboundTunnel (std::shared_ptr<OutboundTunnel> excluded = nullptr,
			i2p::data::RouterInfo::CompatibleTransports compatible = i2p::data::RouterInfo::eAllTransports);
		std::shared_ptr<InboundTunnel> GetNextInboundTunnel (std::shared_ptr<InboundTunnel> excluded = nullptr,
			i2p::data::RouterInfo::CompatibleTransports compatible = i2p::data::RouterInfo::eAllTransports);
		std::pair<std::shared_ptr<OutboundTunnel>, bool> GetNewOutboundTunnel (std::shared_ptr<OutboundTunnel> old);
		void ManageTunnels (uint64_t ts);
		void ProcessGarlicMessage (std::shared_ptr<I2NPMessage> msg);
		void ProcessDeliveryStatus (std::shared_ptr<I2NPMessage> msg);
		void ProcessTunnelTest (std::shared_ptr<I2NPMessage> msg);
		bool ProcessTunnelTest (uint32_t msgID, uint64_t timestamp);

		bool IsExploratory () const;
		bool IsActive () const { return m_IsActive.load (std::memory_order_relaxed); };
		void SetActive (bool isActive) { m_IsActive.store (isActive, std::memory_order_relaxed); };
		void DetachTunnels ();

		int GetNumInboundTunnels () const { return m_NumInboundTunnels.load (std::memory_order_relaxed); };
		int GetNumOutboundTunnels () const { return m_NumOutboundTunnels.load (std::memory_order_relaxed); };
		int GetNumInboundHops () const { return m_NumInboundHops.load (std::memory_order_relaxed); };
		int GetNumOutboundHops () const { return m_NumOutboundHops.load (std::memory_order_relaxed); };
		bool AcceptsInboundLength (int hops) const
		{
			const int base = m_NumInboundHops.load (std::memory_order_relaxed);
			const int variance = m_InboundVariance.load (std::memory_order_relaxed);
			return hops >= base + std::min (0, variance) && hops <= base + std::max (0, variance);
		}
		bool AcceptsOutboundLength (int hops) const
		{
			const int base = m_NumOutboundHops.load (std::memory_order_relaxed);
			const int variance = m_OutboundVariance.load (std::memory_order_relaxed);
			return hops >= base + std::min (0, variance) && hops <= base + std::max (0, variance);
		}

		/** i2cp reconfigure */
		bool Reconfigure (int inboundHops, int outboundHops, int inboundQuant, int outboundQuant);

		void SetCustomPeerSelector (ITunnelPeerSelector * selector, std::shared_ptr<void> owner = nullptr);
		void UnsetCustomPeerSelector ();
		bool HasCustomPeerSelector ();
		/** Copy the current registration; holds no strong owner reference. */
		TunnelPeerSelectorHandle GetCustomPeerSelector () const
		{
			std::lock_guard<std::mutex> lock (m_CustomPeerSelectorMutex);
			return m_CustomPeerSelector;
		}

		/** record the outcome of a tunnel build owned by this pool */
		void TunnelBuildFailed () { m_ConsecutiveBuildFailures.fetch_add (1, std::memory_order_relaxed); };
		void TunnelBuildSucceeded () { m_ConsecutiveBuildFailures.store (0, std::memory_order_relaxed); };
		int GetConsecutiveBuildFailures () const { return m_ConsecutiveBuildFailures.load (std::memory_order_relaxed); }
		uint64_t GetNextBuildTime () const { return m_NextBuildTime.load (std::memory_order_relaxed); }
		/** outstanding tunnel tests, for diagnostics and regression tests */
		size_t GetNumTests () const
		{
			std::lock_guard<std::mutex> l (m_TestsMutex);
			return m_Tests.size ();
		}

		/** @brief make this tunnel pool yield tunnels that fit latency range [min, max] */
		void RequireLatency (int min, int max)
		{
			m_MinLatency.store (min, std::memory_order_relaxed);
			m_MaxLatency.store (max, std::memory_order_relaxed);
		}

		/** @brief return true if this tunnel pool has a latency requirement */
		bool HasLatencyRequirement () const
		{
			return m_MinLatency.load (std::memory_order_relaxed) > 0
				&& m_MaxLatency.load (std::memory_order_relaxed) > 0;
		}

		/** @brief get the lowest latency tunnel in this tunnel pool regardless of latency requirements */
		std::shared_ptr<InboundTunnel> GetLowestLatencyInboundTunnel (
			std::shared_ptr<InboundTunnel> exclude = nullptr) const;
		std::shared_ptr<OutboundTunnel> GetLowestLatencyOutboundTunnel (
			std::shared_ptr<OutboundTunnel> exclude = nullptr) const;

		// for overriding tunnel peer selection
		std::shared_ptr<const i2p::data::RouterInfo> SelectNextHop (
			std::shared_ptr<const i2p::data::RouterInfo> prevHop, bool reverse, bool endpoint);
		bool StandardSelectPeers (Path & path, int numHops, bool inbound, SelectHopFunc nextHop);

	private:
		/** One outstanding tunnel test message. */
		struct TunnelTest
		{
			std::shared_ptr<OutboundTunnel> outbound;
			std::shared_ptr<InboundTunnel> inbound;
			uint64_t sentTime = 0; // milliseconds since epoch
			uint64_t expiration = 0; // milliseconds since epoch
		};

	private:
		void TestTunnels (uint64_t ts);
		void CreateTunnels (uint64_t ts);
		void CreateInboundTunnel (uint64_t ts);
		void CreateOutboundTunnel (uint64_t ts);
		template <class TTunnels>
		typename TTunnels::value_type GetNextTunnel (TTunnels & tunnels, typename TTunnels::value_type excluded,
			i2p::data::RouterInfo::CompatibleTransports compatible);
		bool SelectPeers (Path & path, bool isInbound);
		bool SelectExplicitPeers (Path & path, bool isInbound);
		bool ValidatePeers (std::vector<std::shared_ptr<const i2p::data::IdentityEx>> & peers, bool inbound) const;
		/** True when a recreation may reuse the peers of @a tunnel. */
		bool CanReuseTunnelPeers (const Tunnel & tunnel) const;
		std::shared_ptr<const i2p::data::RouterInfo> SelectTrustedRouter (bool inbound) const;

	private:
		// Test-only accessor; production code must use the public API.
		friend struct TunnelPoolTestAccess;

	private:
		std::shared_ptr<i2p::garlic::GarlicDestination> m_LocalDestination;
		mutable std::mutex m_LocalDestinationMutex;
		// Reconfigure() can run from an I2CP thread while the tunnel thread
		// reads the pool shape, so the numeric configuration is atomic too.
		std::atomic<int> m_NumInboundHops, m_NumOutboundHops, m_NumInboundTunnels, m_NumOutboundTunnels,
			m_InboundVariance, m_OutboundVariance;
		mutable std::mutex m_PeersConfigMutex;
		std::vector<i2p::data::IdentHash> m_ExplicitPeers, m_TrustedRouters;
		mutable std::mutex m_InboundTunnelsMutex;
		std::set<std::shared_ptr<InboundTunnel>, TunnelCreationTimeCmp> m_InboundTunnels; // recent tunnel appears first
		mutable std::mutex m_OutboundTunnelsMutex;
		std::set<std::shared_ptr<OutboundTunnel>, TunnelCreationTimeCmp> m_OutboundTunnels;
		mutable std::mutex m_TestsMutex;
		std::map<uint32_t, TunnelTest> m_Tests;
		std::atomic<bool> m_IsActive;
		bool m_IsHighBandwidth;
		uint64_t m_NextManageTime; // in seconds
		// Consecutive failed builds and the earliest next build round. Jitter is
		// deliberately absent for pooled tunnels: the ladder itself spreads the
		// load when the network is unhealthy.
		std::atomic<int> m_ConsecutiveBuildFailures;
		std::atomic<uint64_t> m_NextBuildTime; // in seconds
		mutable std::mutex m_CustomPeerSelectorMutex;
		TunnelPeerSelectorHandle m_CustomPeerSelector;
		mutable std::mutex m_RngMutex;
		std::mt19937 m_Rng; // for tunnel selection
		i2p::data::PeerOrdering m_InboundPeerOrdering, m_OutboundPeerOrdering;

		std::atomic<int> m_MinLatency {
			0
		}; // if > 0 this tunnel pool will try building tunnels with minimum latency by ms
		std::atomic<int> m_MaxLatency {
			0
		}; // if > 0 this tunnel pool will try building tunnels with maximum latency by ms

	private:
		uint32_t NextRng ();

	public:
		bool HasOutboundTunnels () const
		{
			std::unique_lock<std::mutex> l (m_OutboundTunnelsMutex);
			return !m_OutboundTunnels.empty ();
		}
		bool HasInboundTunnels () const
		{
			std::unique_lock<std::mutex> l (m_InboundTunnelsMutex);
			return !m_InboundTunnels.empty ();
		}

		// for administrative views; thread-safe snapshots, safe to iterate without holding any lock
		std::vector<std::shared_ptr<OutboundTunnel>> GetOutboundTunnelsList () const
		{
			std::unique_lock<std::mutex> l (m_OutboundTunnelsMutex);
			return std::vector<std::shared_ptr<OutboundTunnel>> (m_OutboundTunnels.begin (), m_OutboundTunnels.end ());
		}
		std::vector<std::shared_ptr<InboundTunnel>> GetInboundTunnelsList () const
		{
			std::unique_lock<std::mutex> l (m_InboundTunnelsMutex);
			return std::vector<std::shared_ptr<InboundTunnel>> (m_InboundTunnels.begin (), m_InboundTunnels.end ());
		}
	};
}
}

#endif
