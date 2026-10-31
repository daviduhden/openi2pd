/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Tunnel.
 *
 * Declares Tunnel, OutboundTunnel and InboundTunnel together with their
 * state machine, build-response handling, layered encryption, latency
 * tracking and gateway sending, plus the Tunnels manager thread, pools
 * and pending builds.
 *
 */

#ifndef TUNNEL_H__
#define TUNNEL_H__

#include <inttypes.h>
#include <atomic>
#include <algorithm>
#include <map>
#include <unordered_map>
#include <list>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <memory>
#include <random>
#include <functional>
#include "util.h"
#include "AtomicSharedPtr.h"
#include "Queue.h"
#include "Crypto.h"
#include "TunnelConfig.h"
#include "TunnelPool.h"
#include "TransitTunnel.h"
#include "TunnelEndpoint.h"
#include "TunnelGateway.h"
#include "TunnelBase.h"
#include "I2NPProtocol.h"

namespace i2p
{
namespace tunnel
{
	const int TUNNEL_EXPIRATION_TIMEOUT = 660; // 11 minutes
	const int TUNNEL_EXPIRATION_THRESHOLD = 60; // 1 minute
	const int TUNNEL_RECREATION_THRESHOLD = 90; // 1.5 minutes
	const int TUNNEL_CREATION_TIMEOUT = 30; // 30 seconds
	const int UNKNOWN_LATENCY = -1;
	const int HIGH_LATENCY_PER_HOP = 250000; // in microseconds
	const int MAX_TUNNEL_MSGS_BATCH_SIZE = 100; // handle messages without interrupt
	const uint16_t DEFAULT_MAX_NUM_TRANSIT_TUNNELS = 5000;
	const int TUNNEL_MANAGE_INTERVAL = 15; // in seconds
	const int TUNNEL_POOLS_MANAGE_INTERVAL = 5; // in seconds
	const int TUNNEL_MEMORY_POOL_MANAGE_INTERVAL = 120; // in seconds

	const size_t I2NP_TUNNEL_MESSAGE_SIZE
		= TUNNEL_DATA_MSG_SIZE + I2NP_HEADER_SIZE + 34; // reserved for alignment and NTCP 16 + 6 + 12
	const size_t I2NP_TUNNEL_ENPOINT_MESSAGE_SIZE = 2 * TUNNEL_DATA_MSG_SIZE + I2NP_HEADER_SIZE
		+ TUNNEL_GATEWAY_HEADER_SIZE + 28; // reserved for alignment and NTCP 16 + 6 + 6

	const double TCSR_SMOOTHING_CONSTANT = 0.0005; // smoothing constant in exponentially weighted moving average
	const double TCSR_START_VALUE = 0.1; // start value of tunnel creation success rate

	/**
	 * Record table planned for one short tunnel build message.
	 *
	 * `numHops` are the real hops of the configured path,
	 * `insertPhonyRecord` adds the trailing phony record for an inbound
	 * tunnel and `numRecords` is the record count advertised in
	 * ShortTunnelBuild. The hop loop indexes the shuffled record map once
	 * per real hop, so `valid == false` means the build must fail before
	 * any record buffer is touched. `PlanTunnelBuildRecords` is the single
	 * authority for that decision.
	 */
	struct TunnelBuildRecordPlan
	{
		int numHops = 0;
		int numRecords = 0;
		bool insertPhonyRecord = false;
		bool valid = false;
	};

	TunnelBuildRecordPlan PlanTunnelBuildRecords (int numHops, bool isInbound);

	enum TunnelState
	{
		eTunnelStatePending,
		eTunnelStateBuildReplyReceived,
		eTunnelStateBuildFailed,
		eTunnelStateEstablished,
		eTunnelStateTestFailed,
		eTunnelStateFailed,
		eTunnelStateExpiring
	};

	class OutboundTunnel;
	class InboundTunnel;
	/// Locally built tunnel with a configuration, build state and hop
	/// ciphers. Successful build-response processing installs the ciphers
	/// and releases the configuration; the pool association is a
	/// shared-ownership snapshot.
	class Tunnel : public TunnelBase, public std::enable_shared_from_this<Tunnel>
	{
		struct TunnelHop
		{
			std::shared_ptr<const i2p::data::IdentityEx> ident;
			i2p::crypto::TunnelDecryption decryption;
		};

	public:
		/** function for visiting a hops stored in a tunnel */
		typedef std::function<void (std::shared_ptr<const i2p::data::IdentityEx>)> TunnelHopVisitor;

		Tunnel (std::shared_ptr<TunnelConfig> config);
		~Tunnel ();

		void Build (uint32_t replyMsgID, std::shared_ptr<OutboundTunnel> outboundTunnel = nullptr);

		std::shared_ptr<TunnelConfig> GetTunnelConfig () const { return m_Config; }
		std::vector<std::shared_ptr<const i2p::data::IdentityEx>> GetPeers () const;
		std::vector<std::shared_ptr<const i2p::data::IdentityEx>> GetInvertedPeers () const;
		i2p::data::RouterInfo::CompatibleTransports GetFarEndTransports () const { return m_FarEndTransports; };
		TunnelState GetState () const { return m_State; };
		void SetState (TunnelState state);
		bool IsEstablished () const;
		bool IsFailed () const { return m_State == eTunnelStateFailed; };
		bool IsRecreated () const { return m_IsRecreated.load (std::memory_order_relaxed); };
		void SetRecreated (bool recreated) { m_IsRecreated.store (recreated, std::memory_order_relaxed); };
		int GetNumHops () const { return static_cast<int> (m_Hops.size ()); };
		/**
		 * record count advertised by the last build request, 0 if none was
		 * sent
		 */
		int GetNumBuildRecords () const { return m_NumBuildRecords.load (std::memory_order_relaxed); };

		/**
		 * @name Tunnel test strikes
		 *
		 * How many tunnel tests this tunnel has failed in a row. The counter
		 * is deliberately independent of the state machine: inbound traffic
		 * may promote a pending tunnel to established, but it must not erase
		 * the evidence that the tunnel already missed its test deadlines. The
		 * pool fails the tunnel at TUNNEL_POOL_TEST_STRIKES and clears the
		 * counter when a test succeeds.
		 */
		///@{
		int GetTestStrikes () const { return m_TestStrikes.load (std::memory_order_relaxed); };
		int AddTestStrike () { return m_TestStrikes.fetch_add (1, std::memory_order_relaxed) + 1; };
		void ResetTestStrikes () { m_TestStrikes.store (0, std::memory_order_relaxed); };
		///@}

		virtual bool IsInbound () const = 0;
		virtual bool Recreate () = 0;

		std::shared_ptr<TunnelPool> GetTunnelPool () const { return m_Pool.Load (std::memory_order_acquire); };
		void SetTunnelPool (std::shared_ptr<TunnelPool> pool)
		{
			m_Pool.Store (std::move (pool), std::memory_order_release);
		};

		/// Decrypt and evaluate the response for the outstanding build.
		/// @param msg Mutable response payload, including its record-count
		///            byte.
		/// @param len Exact payload length; it must match the sent record
		///            count.
		/// @return True only when every hop accepted the build.
		/// @warning Records are decrypted in place; a false result
		///          can still leave the buffer modified and it must not be
		///          reused as the original response.
		bool HandleTunnelBuildResponse (uint8_t * msg, size_t len);

		// implements TunnelBase
		void SendTunnelDataMsg (std::shared_ptr<i2p::I2NPMessage> /* msg */) override;
		void EncryptTunnelMsg (std::shared_ptr<const I2NPMessage> in, std::shared_ptr<I2NPMessage> out) override;

		/** @brief add latency sample */
		void AddLatencySample (const int us)
		{
			// Read-modify-write from several transport / tunnel threads. A
			// compare-exchange loop keeps the moving average consistent without
			// introducing a tunnel-wide mutex on the data path.
			int old = m_Latency.load (std::memory_order_relaxed);
			for (;;)
			{
				int updated = old != UNKNOWN_LATENCY ? (old + us) >> 1 : us;
				if (m_Latency.compare_exchange_weak (old, updated, std::memory_order_relaxed)) break;
			}
		}
		/** @brief get this tunnel's estimated latency */
		int GetMeanLatency () const { return (m_Latency.load (std::memory_order_relaxed) + 500) / 1000; }
		/** Return true when latency is within [lowerbound, upperbound]. */
		bool LatencyFitsRange (int lowerbound, int upperbound) const;

		bool LatencyIsKnown () const { return m_Latency.load (std::memory_order_relaxed) != UNKNOWN_LATENCY; }
		bool IsSlow () const;

		/** visit all hops we currently store */
		void VisitTunnelHops (TunnelHopVisitor v);

	private:
		std::shared_ptr<TunnelConfig> m_Config;
		std::vector<TunnelHop> m_Hops;
		// Portable atomic shared_ptr storage with a libc++ fallback.
		mutable i2p::util::AtomicSharedPtr<TunnelPool> m_Pool; // pool, tunnel belongs to, or null
		std::atomic<TunnelState> m_State;
		i2p::data::RouterInfo::CompatibleTransports m_FarEndTransports;
		std::atomic<bool> m_IsRecreated; // if tunnel is replaced by new, or new tunnel requested to replace
		std::atomic<int> m_Latency; // in microseconds
		std::atomic<int> m_NumBuildRecords; // record count sent with the build request
		std::atomic<int> m_TestStrikes; // failed tunnel tests in a row
	};

	/// Locally built outbound tunnel: this router is the gateway, so
	/// locally originated cells are encrypted hop by hop and handed to the
	/// first peer. `SendTunnelDataMsg...` are producer entry points and
	/// serialize through `m_SendMutex`; `HandleTunnelDataMsg()` cannot
	/// legitimately be reached in this direction and only logs an error.
	class OutboundTunnel : public Tunnel
	{
	public:
		OutboundTunnel (std::shared_ptr<TunnelConfig> config)
			: Tunnel (config)
			, m_Gateway (*this)
			, m_EndpointIdentHash (config->GetLastIdentHash ()) { };

		void SendTunnelDataMsgTo (const uint8_t * gwHash, uint32_t gwTunnel, std::shared_ptr<i2p::I2NPMessage> msg);
		virtual void SendTunnelDataMsgsTo (
			const uint8_t * gwHash, uint32_t gwTunnel, const std::vector<std::shared_ptr<i2p::I2NPMessage>> & msgs);
		virtual void SendTunnelDataMsgs (const std::vector<TunnelMessageBlock> & msgs); // multiple messages
		const i2p::data::IdentHash & GetEndpointIdentHash () const { return m_EndpointIdentHash; };
		virtual size_t GetNumSentBytes () const { return m_Gateway.GetNumSentBytes (); };

		// implements TunnelBase
		void HandleTunnelDataMsg (std::shared_ptr<i2p::I2NPMessage> && /* tunnelMsg */) override;

		bool IsInbound () const override { return false; }
		bool Recreate () override;

	protected:
		static TunnelMessageBlock CreateTunnelMessageBlock (const uint8_t * gwHash, uint32_t gwTunnel);

	private:
		std::mutex m_SendMutex;
		TunnelGateway m_Gateway;
		i2p::data::IdentHash m_EndpointIdentHash;
	};

	/// Locally built inbound tunnel: the last hop delivers to this router,
	/// which decrypts every layer and reassembles the I2NP message in
	/// `m_Endpoint`. `HandleTunnelDataMsg()` runs on the tunnel worker and
	/// consumes the cell; fragments and the per-tunnel IV replay filter
	/// are owned by the endpoint.
	class InboundTunnel : public Tunnel
	{
	public:
		InboundTunnel (std::shared_ptr<TunnelConfig> config)
			: Tunnel (config)
			, m_Endpoint (true) { };
		void HandleTunnelDataMsg (std::shared_ptr<I2NPMessage> && msg) override;
		virtual size_t GetNumReceivedBytes () const { return m_Endpoint.GetNumReceivedBytes (); };
		/** last tunnel data message arrival in milliseconds, 0 if none yet */
		uint64_t GetLastReceiveTimeMs () const { return m_Endpoint.GetLastReceiveTimeMs (); };
		bool IsInbound () const override { return true; }
		bool Recreate () override;

		// override TunnelBase
		void Cleanup () override { m_Endpoint.Cleanup (); };

	protected:
		std::shared_ptr<InboundTunnel> GetSharedFromThis ()
		{
			return std::static_pointer_cast<InboundTunnel> (shared_from_this ());
		}

	private:
		TunnelEndpoint m_Endpoint;
	};

	/// Inbound shortcut for the local (zero-hop) case: no peer decrypts
	/// the cell, so `SendTunnelDataMsg()` delivers it straight to the
	/// local endpoint and the counter only exists for statistics.
	class ZeroHopsInboundTunnel : public InboundTunnel
	{
	public:
		ZeroHopsInboundTunnel ();
		void SendTunnelDataMsg (std::shared_ptr<i2p::I2NPMessage> msg) override;
		size_t GetNumReceivedBytes () const override { return m_NumReceivedBytes; };

	private:
		size_t m_NumReceivedBytes;
	};

	/// Outbound shortcut for the local (zero-hop) case: a cell destined
	/// for another router or tunnel is sent as a plain I2NP message, and a
	/// local delivery is dispatched directly, so nothing is
	/// layered-encrypted.
	class ZeroHopsOutboundTunnel : public OutboundTunnel
	{
	public:
		ZeroHopsOutboundTunnel ();
		void SendTunnelDataMsgsTo (const uint8_t * gwHash, uint32_t gwTunnel,
			const std::vector<std::shared_ptr<i2p::I2NPMessage>> & msgs) override;
		void SendTunnelDataMsgs (const std::vector<TunnelMessageBlock> & msgs) override;
		size_t GetNumSentBytes () const override { return m_NumSentBytes; };

	private:
		size_t m_NumSentBytes;
	};

	/**
	 * One-hop tunnel configurations used by the fallback builders.
	 *
	 * The far-end transport mask is the peer's real transport set in the
	 * tunnel's direction. Passing a zero mask (a `false` boolean in the
	 * old call sites) makes the tunnel incompatible with every paired
	 * tunnel and disables the pairing checks in the pool, so these helpers
	 * exist to keep the mask correct and testable.
	 */
	std::shared_ptr<TunnelConfig> CreateOneHopInboundTunnelConfig (const i2p::data::RouterInfo & router);
	std::shared_ptr<TunnelConfig> CreateOneHopOutboundTunnelConfig (
		const i2p::data::RouterInfo & router, uint32_t replyTunnelID, const i2p::data::IdentHash & replyIdent);
	/** Outbound mirror of @a inbound: same peers, same far-end mask. */
	std::shared_ptr<TunnelConfig> CreatePairedOutboundTunnelConfig (const InboundTunnel & inbound);

	/// Tunnel manager: owns the tunnel worker thread, the registered
	/// tunnel maps, the pending builds keyed by reply message ID, the pool
	/// list and the transit-tunnel worker.
	///
	/// Tunnel and pool objects are shared, so administrative views publish
	/// copies under `m_TunnelsMutex`. `m_PendingTunnelsMutex` guards the
	/// reply-ID maps alone, while `m_LifecycleMutex` serializes Start/Stop
	/// transitions and `m_QueueAdmissionMutex` keeps queue admission short
	/// so a producer rejects promptly during a stop. The shared
	/// `std::mt19937` is not thread-safe, so it is only reached through
	/// `GetRng()`/`Shuffle()` under `m_RngMutex`.
	class Tunnels
	{
	public:
		Tunnels ();
		~Tunnels ();
		// Stop() tears down the worker, the transit worker and all runtime
		// tunnel maps. Start()/Stop() cycles are supported for an orderly
		// restart with no in-flight traffic; wire messages queued for the old
		// instance are dropped and must be retransmitted by peers. Producers
		// must be quiesced across restart; posts carry no instance
		// generation.
		void Start ();
		void Stop ();

		std::shared_ptr<InboundTunnel> GetPendingInboundTunnel (uint32_t replyMsgID);
		std::shared_ptr<OutboundTunnel> GetPendingOutboundTunnel (uint32_t replyMsgID);
		std::shared_ptr<InboundTunnel> GetNextInboundTunnel ();
		std::shared_ptr<OutboundTunnel> GetNextOutboundTunnel ();
		std::shared_ptr<TunnelPool> GetExploratoryPool () const { return m_ExploratoryPool.Load (); };
		std::shared_ptr<TunnelBase> GetTunnel (uint32_t tunnelID);
		bool AddTunnel (std::shared_ptr<TunnelBase> tunnel);
		void RemoveTunnel (uint32_t tunnelID);
		int GetTransitTunnelsExpirationTimeout ();
		void AddOutboundTunnel (std::shared_ptr<OutboundTunnel> newTunnel);
		void AddInboundTunnel (std::shared_ptr<InboundTunnel> newTunnel);
		std::shared_ptr<InboundTunnel> CreateInboundTunnel (std::shared_ptr<TunnelConfig> config,
			std::shared_ptr<TunnelPool> pool, std::shared_ptr<OutboundTunnel> outboundTunnel);
		std::shared_ptr<OutboundTunnel> CreateOutboundTunnel (
			std::shared_ptr<TunnelConfig> config, std::shared_ptr<TunnelPool> pool);
		// Returns false for a null message or when the service is stopped.
		// The list overload consumes its input on both accepted and rejected
		// posts.
		bool PostTunnelData (std::shared_ptr<I2NPMessage> msg);
		bool PostTunnelData (std::list<std::shared_ptr<I2NPMessage>> & msgs);
		void AddPendingTunnel (uint32_t replyMsgID, std::shared_ptr<InboundTunnel> tunnel);
		void AddPendingTunnel (uint32_t replyMsgID, std::shared_ptr<OutboundTunnel> tunnel);
		std::shared_ptr<TunnelPool> CreateTunnelPool (int numInboundHops, int numOuboundHops, int numInboundTunnels,
			int numOutboundTunnels, int inboundVariance, int outboundVariance, bool isHighBandwidth);
		void DeleteTunnelPool (std::shared_ptr<TunnelPool> pool);
		void StopTunnelPool (std::shared_ptr<TunnelPool> pool);

		std::shared_ptr<I2NPMessage> NewI2NPTunnelMessage (bool endpoint);

		/**
		 * Tunnels with an outstanding build request that belong to @a pool.
		 */
		size_t GetNumPendingTunnels (const TunnelPool * pool, bool inbound) const;
		/** Inbound registry messages dropped because the queue was full. */
		uint64_t GetNumDroppedTunnelMessages () const
		{
			return m_NumDroppedTunnelMessages.load (std::memory_order_relaxed);
		}
		/**
		 * Queue size bound used by PostTunnelData(), for diagnostics and
		 * tests.
		 */
		size_t GetTunnelMessageQueueLimit () const { return TUNNEL_MESSAGE_QUEUE_MAX_SIZE; }

		void SetMaxNumTransitTunnels (uint32_t maxNumTransitTunnels);
		uint32_t GetMaxNumTransitTunnels () const { return m_MaxNumTransitTunnels.load (std::memory_order_relaxed); };
		int GetCongestionLevel () const
		{
			auto maxNumTransitTunnels = m_MaxNumTransitTunnels.load (std::memory_order_relaxed);
			return maxNumTransitTunnels ? static_cast<int> (CONGESTION_LEVEL_FULL
											  * m_TransitTunnels.GetNumTransitTunnels () / maxNumTransitTunnels)
										: CONGESTION_LEVEL_FULL;
		}
		/** Returns one value from the shared tunnel RNG. */
		uint32_t GetRng ();
		/** Returns a copy of the shared RNG for read-only algorithms. */
		std::mt19937 GetRngSnapshot () const;
		/**
		 * Shuffle in place while advancing the shared RNG under its mutex.
		 */
		template <typename RandomIt> void Shuffle (RandomIt first, RandomIt last)
		{
			std::lock_guard<std::mutex> l (m_RngMutex);
			std::shuffle (first, last, m_Rng);
		}

	private:
		template <class TTunnel>
		std::shared_ptr<TTunnel> CreateTunnel (std::shared_ptr<TunnelConfig> config, std::shared_ptr<TunnelPool> pool,
			std::shared_ptr<OutboundTunnel> outboundTunnel = nullptr);

		template <class TTunnel>
		std::shared_ptr<TTunnel> GetPendingTunnel (
			uint32_t replyMsgID, const std::map<uint32_t, std::shared_ptr<TTunnel>> & pendingTunnels);

		template <typename TTunnel>
		std::shared_ptr<TTunnel> GetRandomTunnel (const std::list<std::shared_ptr<TTunnel>> & tunnels);

		void HandleTunnelGatewayMsg (std::shared_ptr<TunnelBase> tunnel, std::shared_ptr<I2NPMessage> msg);
		void HandleShortTunnelBuildMsg (std::shared_ptr<I2NPMessage> msg);
		void HandleVariableTunnelBuildMsg (std::shared_ptr<I2NPMessage> msg);
		void HandleTunnelBuildReplyMsg (std::shared_ptr<I2NPMessage> msg, bool /* isShort */);

		void Run ();
		void ManageTunnels (uint64_t ts);
		void ManageOutboundTunnels (uint64_t ts, std::vector<std::shared_ptr<Tunnel>> & toRecreate);
		void ManageInboundTunnels (uint64_t ts, std::vector<std::shared_ptr<Tunnel>> & toRecreate);
		void ManagePendingTunnels (uint64_t ts);
		template <class PendingTunnels> void ManagePendingTunnels (PendingTunnels & pendingTunnels, uint64_t ts);
		void ManageTunnelPools (uint64_t ts);

		std::shared_ptr<ZeroHopsInboundTunnel> CreateZeroHopsInboundTunnel (std::shared_ptr<TunnelPool> pool);
		std::shared_ptr<ZeroHopsOutboundTunnel> CreateZeroHopsOutboundTunnel (std::shared_ptr<TunnelPool> pool);

		// Calculating of tunnel creation success rate
		void SuccesiveTunnelCreation ()
		{
			// total TCSR
			m_TotalNumSuccesiveTunnelCreations.fetch_add (1, std::memory_order_relaxed);
			UpdateTunnelCreationSuccessRate (1.0);
		}
		void FailedTunnelCreation ()
		{
			m_TotalNumFailedTunnelCreations.fetch_add (1, std::memory_order_relaxed);
			UpdateTunnelCreationSuccessRate (0.0);
		}
		void UpdateTunnelCreationSuccessRate (double sample)
		{
			// A modified version of the EWMA algorithm, where alpha is increased
			// at the beginning to accelerate similarity. The success-rate sample
			// is updated with a CAS loop because build replies and expirations
			// arrive from different threads.
			double alpha = TCSR_SMOOTHING_CONSTANT + (1 - TCSR_SMOOTHING_CONSTANT) / ++m_TunnelCreationAttemptsNum;
			double old = m_TunnelCreationSuccessRate.load (std::memory_order_relaxed);
			for (;;)
			{
				double updated = alpha * sample + (1 - alpha) * old;
				if (m_TunnelCreationSuccessRate.compare_exchange_weak (old, updated, std::memory_order_relaxed)) break;
			}
		}

	private:
		std::atomic<bool> m_IsRunning;
		// Serialize whole lifecycle transitions separately from short queue
		// admission sections, so producers reject promptly while Stop joins.
		std::mutex m_LifecycleMutex;
		std::mutex m_QueueAdmissionMutex;
		std::unique_ptr<std::thread> m_Thread;
		i2p::util::MemoryPoolMt<I2NPMessageBuffer<I2NP_TUNNEL_ENPOINT_MESSAGE_SIZE>>
			m_I2NPTunnelEndpointMessagesMemoryPool;
		i2p::util::MemoryPoolMt<I2NPMessageBuffer<I2NP_TUNNEL_MESSAGE_SIZE>> m_I2NPTunnelMessagesMemoryPool;
		std::map<uint32_t, std::shared_ptr<InboundTunnel>> m_PendingInboundTunnels; // by replyMsgID
		std::map<uint32_t, std::shared_ptr<OutboundTunnel>> m_PendingOutboundTunnels; // by replyMsgID
		mutable std::mutex m_PendingTunnelsMutex;
		std::list<std::shared_ptr<InboundTunnel>> m_InboundTunnels;
		std::list<std::shared_ptr<OutboundTunnel>> m_OutboundTunnels;
		mutable std::mutex m_TunnelsMutex;
		std::unordered_map<uint32_t, std::shared_ptr<TunnelBase>> m_Tunnels; // tunnelID->tunnel known by this id
		mutable std::mutex m_PoolsMutex;
		std::list<std::shared_ptr<TunnelPool>> m_Pools;
		mutable i2p::util::AtomicSharedPtr<TunnelPool> m_ExploratoryPool;
		i2p::util::Queue<std::shared_ptr<I2NPMessage>> m_Queue;
		std::atomic<uint64_t> m_NumDroppedTunnelMessages;
		std::atomic<uint32_t> m_MaxNumTransitTunnels;
		// count of tunnels for total TCSR algorithm
		std::atomic<int> m_TotalNumSuccesiveTunnelCreations, m_TotalNumFailedTunnelCreations;
		std::atomic<double> m_TunnelCreationSuccessRate;
		std::atomic<int> m_TunnelCreationAttemptsNum;
		mutable std::mutex m_RngMutex;
		std::mt19937 m_Rng;
		TransitTunnels m_TransitTunnels;

	public:
		// for administrative views; thread-safe snapshots, safe to iterate
		// without holding any lock
		std::vector<std::shared_ptr<OutboundTunnel>> GetOutboundTunnelsList () const
		{
			std::lock_guard<std::mutex> l (m_TunnelsMutex);
			return std::vector<std::shared_ptr<OutboundTunnel>> (m_OutboundTunnels.begin (), m_OutboundTunnels.end ());
		}
		std::vector<std::shared_ptr<InboundTunnel>> GetInboundTunnelsList () const
		{
			std::lock_guard<std::mutex> l (m_TunnelsMutex);
			return std::vector<std::shared_ptr<InboundTunnel>> (m_InboundTunnels.begin (), m_InboundTunnels.end ());
		}
		auto GetTransitTunnelsList () const { return m_TransitTunnels.GetTransitTunnelsList (); };

		size_t CountTransitTunnels () const;
		size_t CountInboundTunnels () const;
		size_t CountOutboundTunnels () const;

		size_t GetQueueSize () const { return m_Queue.GetSize (); };
		size_t GetTBMQueueSize () const { return m_TransitTunnels.GetTunnelBuildMsgQueueSize (); };
		int GetTunnelCreationSuccessRate () const
		{
			return static_cast<int> (std::lround (m_TunnelCreationSuccessRate.load (std::memory_order_relaxed) * 100));
		} // in percents
		double GetPreciseTunnelCreationSuccessRate () const
		{
			return m_TunnelCreationSuccessRate.load (std::memory_order_relaxed) * 100;
		} // in percents
		int GetTotalTunnelCreationSuccessRate () const // in percents
		{
			int totalNum = m_TotalNumSuccesiveTunnelCreations.load (std::memory_order_relaxed)
				+ m_TotalNumFailedTunnelCreations.load (std::memory_order_relaxed);
			return totalNum ? m_TotalNumSuccesiveTunnelCreations.load (std::memory_order_relaxed) * 100 / totalNum : 0;
		}
	};

	extern Tunnels tunnels;
}
}

#endif
