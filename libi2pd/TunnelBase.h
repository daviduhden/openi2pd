/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TunnelBase.
 *
 * Defines the tunnel base interface (IDs, next hop, creation time, message
 * handling and encryption), tunnel message block types, and
 * TunnelTransportSender, which resolves and caches the transport session used
 * to reach the next peer.
 *
 */

#ifndef TUNNEL_BASE_H__
#define TUNNEL_BASE_H__

#include <inttypes.h>
#include <atomic>
#include <memory>
#include <future>
#include <list>
#include <deque>
#include <mutex>
#include <unordered_set>
#include <utility>
#include "Timestamp.h"
#include "I2NPProtocol.h"
#include "Identity.h"
#include "Tag.h"

namespace i2p
{
namespace transport
{
	class TransportSession;
}

namespace tunnel
{
	const size_t TUNNEL_DATA_MSG_SIZE = 1028;
	const size_t TUNNEL_DATA_ENCRYPTED_SIZE = 1008;
	const size_t TUNNEL_DATA_MAX_PAYLOAD_SIZE = 1003;

	enum TunnelDeliveryType
	{
		eDeliveryTypeLocal = 0,
		eDeliveryTypeTunnel = 1,
		eDeliveryTypeRouter = 2
	};
	struct TunnelMessageBlock
	{
		TunnelDeliveryType deliveryType;
		i2p::data::IdentHash hash;
		uint32_t tunnelID;
		std::shared_ptr<I2NPMessage> data;
	};

	class TunnelBase
	{
	public:
		TunnelBase (uint32_t tunnelID, uint32_t nextTunnelID, const i2p::data::IdentHash & nextIdent)
			: m_TunnelID (tunnelID)
			, m_NextTunnelID (nextTunnelID)
			, m_NextIdent (nextIdent)
			, m_CreationTime (static_cast<uint32_t> (i2p::util::GetSecondsSinceEpoch ())) { };
		virtual ~TunnelBase () { };
		virtual void Cleanup () { };

		virtual void HandleTunnelDataMsg (std::shared_ptr<i2p::I2NPMessage> && tunnelMsg) = 0;
		virtual void SendTunnelDataMsg (std::shared_ptr<i2p::I2NPMessage> msg) = 0;
		virtual void FlushTunnelDataMsgs () { };
		virtual void EncryptTunnelMsg (std::shared_ptr<const I2NPMessage> in, std::shared_ptr<I2NPMessage> out) = 0;
		uint32_t GetNextTunnelID () const { return m_NextTunnelID; };
		const i2p::data::IdentHash & GetNextIdentHash () const { return m_NextIdent; };
		virtual uint32_t GetTunnelID () const { return m_TunnelID; }; // as known at our side

		uint32_t GetCreationTime () const { return m_CreationTime.load (std::memory_order_relaxed); };
		void SetCreationTime (uint32_t t) { m_CreationTime.store (t, std::memory_order_relaxed); };

		// Bounded per-tunnel IV replay filter (Java BloomFilterIVValidator).
		// The tunnel worker is the only producer for a given receive tunnel,
		// but the mutex also protects administrative/diagnostic callers.
		bool CheckAndStoreIV (const uint8_t * iv)
		{
			if (!iv) return false;
			constexpr uint64_t window = 10 * 60 * 1000LL; // Java 10-minute half-life
			constexpr size_t maximum = 512;
			const auto now = i2p::util::GetMillisecondsSinceEpoch ();
			std::lock_guard<std::mutex> l (m_IVMutex);
			while (!m_RecentIVs.empty () && m_RecentIVs.front ().second + window <= now)
			{
				m_RecentIVSet.erase (m_RecentIVs.front ().first);
				m_RecentIVs.pop_front ();
			}
			i2p::data::Tag<16> tag (iv);
			if (m_RecentIVSet.count (tag)) return false;
			if (m_RecentIVs.size () >= maximum)
			{
				m_RecentIVSet.erase (m_RecentIVs.front ().first);
				m_RecentIVs.pop_front ();
			}
			m_RecentIVs.emplace_back (tag, now);
			m_RecentIVSet.insert (tag);
			return true;
		}

	private:
		uint32_t m_TunnelID, m_NextTunnelID;
		i2p::data::IdentHash m_NextIdent;
		std::atomic<uint32_t> m_CreationTime; // seconds since epoch, read by TUI/IPC
		std::mutex m_IVMutex;
		std::deque<std::pair<i2p::data::Tag<16>, uint64_t>> m_RecentIVs;
		std::unordered_set<i2p::data::Tag<16>> m_RecentIVSet;
	};

	struct TunnelCreationTimeCmp
	{
		template <typename T> bool operator() (const std::shared_ptr<T> & t1, const std::shared_ptr<T> & t2) const
		{
			if (t1->GetCreationTime () != t2->GetCreationTime ())
				return t1->GetCreationTime () > t2->GetCreationTime ();
			else
				return t1 < t2;
		}
	};

	class TunnelTransportSender final
	{
	public:
		TunnelTransportSender () = default;
		~TunnelTransportSender () = default;

		void SendMessagesTo (const i2p::data::IdentHash & to, std::list<std::shared_ptr<I2NPMessage>> && msgs);
		void SendMessagesTo (
			const i2p::data::IdentHash & to, std::list<std::shared_ptr<I2NPMessage>> & msgs); // send and clear

		std::shared_ptr<const i2p::transport::TransportSession> GetCurrentTransport () const
		{
			return m_CurrentTransport.lock ();
		}
		void Reset ();

	private:
		std::weak_ptr<i2p::transport::TransportSession> m_CurrentTransport;
		std::future<std::shared_ptr<i2p::transport::TransportSession>> m_PendingTransport;
	};
}
}

#endif
