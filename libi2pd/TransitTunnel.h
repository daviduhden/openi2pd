/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TransitTunnel.
 *
 * Declares transit tunnel roles (participant, gateway and endpoint) that
 * decrypt, forward or terminate tunnel data messages, and TransitTunnels, which
 * accepts build requests and tracks active transit tunnels on a worker thread.
 *
 */

#ifndef TRANSIT_TUNNEL_H__
#define TRANSIT_TUNNEL_H__

#include <inttypes.h>
#include <atomic>
#include <deque>
#include <list>
#include <unordered_set>
#include <vector>
#include <mutex>
#include <memory>
#include <string_view>
#include <thread>
#include "Crypto.h"
#include "Queue.h"
#include "Tag.h"
#include "I2NPProtocol.h"
#include "TunnelEndpoint.h"
#include "TunnelGateway.h"
#include "TunnelBase.h"

namespace i2p
{
namespace tunnel
{
	class TransitTunnel : public TunnelBase
	{
	public:
		TransitTunnel (uint32_t receiveTunnelID, const i2p::data::IdentHash & nextIdent, uint32_t nextTunnelID,
			const i2p::crypto::AESKey & layerKey, const i2p::crypto::AESKey & ivKey);

		virtual size_t GetNumTransmittedBytes () const { return 0; };
		virtual std::string GetNextPeerName () const;

		// implements TunnelBase
		void SendTunnelDataMsg (std::shared_ptr<i2p::I2NPMessage> msg) override;
		void HandleTunnelDataMsg (std::shared_ptr<i2p::I2NPMessage> && tunnelMsg) override;
		void EncryptTunnelMsg (std::shared_ptr<const I2NPMessage> in, std::shared_ptr<I2NPMessage> out) override;

	private:
		i2p::crypto::AESKey m_LayerKey, m_IVKey;
		std::unique_ptr<i2p::crypto::TunnelEncryption> m_Encryption;
	};

	class TransitTunnelParticipant : public TransitTunnel
	{
	public:
		TransitTunnelParticipant (uint32_t receiveTunnelID, const i2p::data::IdentHash & nextIdent,
			uint32_t nextTunnelID, const i2p::crypto::AESKey & layerKey, const i2p::crypto::AESKey & ivKey)
			: TransitTunnel (receiveTunnelID, nextIdent, nextTunnelID, layerKey, ivKey)
			, m_NumTransmittedBytes (0) { };
		~TransitTunnelParticipant ();

		size_t GetNumTransmittedBytes () const override { return m_NumTransmittedBytes; };
		std::string GetNextPeerName () const override;
		void HandleTunnelDataMsg (std::shared_ptr<i2p::I2NPMessage> && tunnelMsg) override;
		void FlushTunnelDataMsgs () override;

	private:
		size_t m_NumTransmittedBytes;
		std::list<std::shared_ptr<i2p::I2NPMessage>> m_TunnelDataMsgs;
		std::unique_ptr<TunnelTransportSender> m_Sender;
	};

	class TransitTunnelGateway : public TransitTunnel
	{
	public:
		TransitTunnelGateway (uint32_t receiveTunnelID, const i2p::data::IdentHash & nextIdent, uint32_t nextTunnelID,
			const i2p::crypto::AESKey & layerKey, const i2p::crypto::AESKey & ivKey)
			: TransitTunnel (receiveTunnelID, nextIdent, nextTunnelID, layerKey, ivKey)
			, m_Gateway (*this) { };

		void SendTunnelDataMsg (std::shared_ptr<i2p::I2NPMessage> msg) override;
		void FlushTunnelDataMsgs () override;
		size_t GetNumTransmittedBytes () const override { return m_Gateway.GetNumSentBytes (); };
		std::string GetNextPeerName () const override;

	private:
		std::mutex m_SendMutex;
		TunnelGateway m_Gateway;
	};

	class TransitTunnelEndpoint : public TransitTunnel
	{
	public:
		TransitTunnelEndpoint (uint32_t receiveTunnelID, const i2p::data::IdentHash & nextIdent, uint32_t nextTunnelID,
			const i2p::crypto::AESKey & layerKey, const i2p::crypto::AESKey & ivKey)
			: TransitTunnel (receiveTunnelID, nextIdent, nextTunnelID, layerKey, ivKey) { };

		void Cleanup () override;

		void HandleTunnelDataMsg (std::shared_ptr<i2p::I2NPMessage> && tunnelMsg) override;
		void FlushTunnelDataMsgs () override;
		size_t GetNumTransmittedBytes () const override { return m_Endpoint ? m_Endpoint->GetNumReceivedBytes () : 0; }
		std::string GetNextPeerName () const override;

	private:
		std::mutex m_HandleMutex;
		std::unique_ptr<TunnelEndpoint> m_Endpoint;
	};

	std::shared_ptr<TransitTunnel> CreateTransitTunnel (uint32_t receiveTunnelID,
		const i2p::data::IdentHash & nextIdent, uint32_t nextTunnelID, const i2p::crypto::AESKey & layerKey,
		const i2p::crypto::AESKey & ivKey, bool isGateway, bool isEndpoint);

	const int TRANSIT_TUNNELS_QUEUE_WAIT_INTERVAL = 10; // in seconds
	const int TRANSIT_TUNNELS_BUILD_MSG_QUEUE_MAX_SIZE = 100;

	// A transit build request is replayed when the same derived reply key is
	// seen again inside this window. The key is derived from the request's DH,
	// so it is unique per legitimate request; only a replay matches.
	const uint64_t TRANSIT_TUNNEL_BUILD_REPLAY_WINDOW_MS = 8 * 60 * 1000; // 8 minutes
	const size_t TRANSIT_TUNNEL_BUILD_REPLAY_MAX_ENTRIES = 4096;
	// Java BuildHandler: a build reply we send for a tunnel we just created may
	// take a full round trip, a build request we merely relay is forwarded with
	// a much shorter lifetime.
	const uint64_t TUNNEL_BUILD_REPLY_EXPIRATION_MS = 60 * 1000; // 60 seconds
	const uint64_t TUNNEL_BUILD_RELAY_EXPIRATION_MS = 25 * 1000; // 25 seconds

	/** Exact ShortTunnelBuild payload size for @a numRecords records. */
	constexpr size_t GetShortTunnelBuildMessageSize (int numRecords)
	{
		return 1 + static_cast<size_t> (numRecords) * SHORT_TUNNEL_BUILD_RECORD_SIZE;
	}
	/** Exact VariableTunnelBuild payload size for @a numRecords records. */
	constexpr size_t GetVariableTunnelBuildMessageSize (int numRecords)
	{
		return 1 + static_cast<size_t> (numRecords) * TUNNEL_BUILD_RECORD_SIZE;
	}
	/** True when a build payload is exactly the declared size (no trailing bytes). */
	inline bool IsExactTunnelBuildMessageSize (size_t payloadLength, int numRecords, bool shortRecords)
	{
		if (numRecords <= 0 || numRecords > MAX_NUM_RECORDS) return false;
		const size_t expected = shortRecords ? GetShortTunnelBuildMessageSize (numRecords)
											 : GetVariableTunnelBuildMessageSize (numRecords);
		return payloadLength == expected;
	}

	/**
	 * Bounded, decaying replay filter for transit build requests.
	 *
	 * Entries expire after the replay window and the table is capped, so a
	 * hostile peer cannot grow it without limit. Keys are never logged: only
	 * the record index and the outcome are.
	 */
	class TransitBuildReplayFilter final
	{
	public:
		explicit TransitBuildReplayFilter (uint64_t windowMs = TRANSIT_TUNNEL_BUILD_REPLAY_WINDOW_MS,
			size_t maximumEntries = TRANSIT_TUNNEL_BUILD_REPLAY_MAX_ENTRIES)
			: m_WindowMs (windowMs)
			, m_MaximumEntries (maximumEntries)
		{
		}

		/** @return true when @a key is new (and remembers it), false on replay */
		bool CheckAndStore (const uint8_t * key, uint64_t nowMs);
		size_t GetSize () const { return m_Entries.size (); }
		void Clear ();

	private:
		struct Entry
		{
			i2p::data::Tag<32> key;
			uint64_t time;
		};

	private:
		const uint64_t m_WindowMs;
		const size_t m_MaximumEntries;
		std::deque<Entry> m_Entries; // insertion order, oldest first
		std::unordered_set<i2p::data::Tag<32>> m_Keys;
	};

	constexpr std::string_view TRANSIT_TUNNEL_MINIMUM_BANDWIDTH { "m" };
	constexpr std::string_view TRANSIT_TUNNEL_REQUESTED_BANDWIDTH { "r" };
	constexpr std::string_view TRANSIT_TUNNEL_LIMIT_BANDWIDTH { "l" };
	constexpr std::string_view TRANSIT_TUNNEL_AVAILABLE_BANDWIDTH { "b" };

	class TransitTunnels
	{
	public:
		TransitTunnels ();
		~TransitTunnels ();

		// Stop() clears the transit tunnel list and joins the worker. A later
		// Start() is supported for an orderly restart; build requests queued
		// for the old instance are discarded.
		void Start ();
		void Stop ();
		void PostTransitTunnelBuildMsg (std::shared_ptr<I2NPMessage> && msg);

		size_t GetNumTransitTunnels () const
		{
			std::lock_guard<std::mutex> l (m_TransitTunnelsMutex);
			return m_TransitTunnels.size ();
		}
		int GetTransitTunnelsExpirationTimeout ();

	private:
		bool AddTransitTunnel (std::shared_ptr<TransitTunnel> tunnel);
		void ManageTransitTunnels (uint64_t ts);

		void HandleShortTransitTunnelBuildMsg (std::shared_ptr<I2NPMessage> && msg);
		void HandleVariableTransitTunnelBuildMsg (std::shared_ptr<I2NPMessage> && msg);
		bool HandleBuildRequestRecords (int num, uint8_t * records, uint8_t * clearText);

		void Run ();

	private:
		std::atomic<bool> m_IsRunning;
		std::unique_ptr<std::thread> m_Thread;
		mutable std::mutex m_TransitTunnelsMutex;
		std::list<std::shared_ptr<TransitTunnel>> m_TransitTunnels;
		i2p::util::Queue<std::shared_ptr<I2NPMessage>> m_TunnelBuildMsgQueue;
		// The manager thread is the only producer, so no lock is needed.
		TransitBuildReplayFilter m_BuildReplayFilter;
		std::mt19937 m_Rng;

	public:
		// for administrative views and I2PControl; thread-safe snapshot, safe to iterate without holding any lock
		std::vector<std::shared_ptr<TransitTunnel>> GetTransitTunnelsList () const
		{
			std::lock_guard<std::mutex> l (m_TransitTunnelsMutex);
			return std::vector<std::shared_ptr<TransitTunnel>> (m_TransitTunnels.begin (), m_TransitTunnels.end ());
		}
		size_t GetTunnelBuildMsgQueueSize () const { return m_TunnelBuildMsgQueue.GetSize (); };
	};
}
}

#endif
