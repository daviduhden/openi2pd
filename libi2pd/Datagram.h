/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Datagram.
 *
 * Declares the I2P datagram endpoint for I2CP protocols 17, 18, 19 and 20 and
 * its per-peer garlic sessions. Sessions manage routing paths, LeaseSet
 * refresh, bounded send queues and idle cleanup, while port-keyed receivers
 * dispatch Datagram, raw and Datagram2/3 payloads.
 *
 */

#ifndef DATAGRAM_H__
#define DATAGRAM_H__

#include <inttypes.h>
#include <memory>
#include <functional>
#include <unordered_map>
#include <vector>
#include "Base.h"
#include "Gzip.h"
#include "Identity.h"
#include "LeaseSet.h"
#include "I2NPProtocol.h"
#include "Garlic.h"
#include "util.h"
#include "ECIESX25519AEADRatchetSession.h"

namespace i2p
{
namespace client
{
	class ClientDestination;
}
namespace datagram
{
	// milliseconds for max session idle time
	const uint64_t DATAGRAM_SESSION_MAX_IDLE = 10 * 60 * 1000;
	// milliseconds interval a routing path is used before switching
	const uint64_t DATAGRAM_SESSION_PATH_SWITCH_INTERVAL = 20 * 60 * 1000;
	// milliseconds before lease expire should we try switching leases
	const uint64_t DATAGRAM_SESSION_LEASE_HANDOVER_WINDOW = 30 * 1000;
	// milliseconds fudge factor for leases handover
	const uint64_t DATAGRAM_SESSION_LEASE_HANDOVER_FUDGE = 1000;
	// milliseconds minimum time between path switches
	const uint64_t DATAGRAM_SESSION_PATH_MIN_LIFETIME = 5 * 1000;
	// max 64 messages buffered in send queue for each datagram session
	const size_t DATAGRAM_SEND_QUEUE_MAX_SIZE = 64;
	const uint64_t DATAGRAM_MAX_FLUSH_INTERVAL = 5; // in milliseconds
	const int DATAGRAM_SESSION_ACK_REQUEST_INTERVAL = 5500; // in milliseconds
	// Datagram3 first contact can name an arbitrary sender hash, so the session map
	// must not grow without bound. Java keeps these in the router's session table
	// with a lifetime; this is the local equivalent.
	const size_t DATAGRAM_SESSIONS_MAX_SIZE = 1024;

	enum DatagramVersion
	{
		eDatagramV1 = 1,
		eDatagramV2 = 2,
		eDatagramV3 = 3,
	};

	constexpr uint8_t DATAGRAM2_FLAG_OPTIONS = 0x10;
	constexpr uint8_t DATAGRAM2_FLAG_OFFLINE_SIGNATURE = 0x20;
	constexpr uint8_t DATAGRAM3_FLAG_OPTIONS = 0x10;
	// I2CP PROTOCOL_TYPE_RAW. Datagram.h is included by Destination.h, so the
	// constant is duplicated here instead of creating an include cycle.
	constexpr uint8_t DATAGRAM_RAW_PROTOCOL = 18;

	class DatagramSession : public std::enable_shared_from_this<DatagramSession>
	{

	public:
		DatagramSession (
			std::shared_ptr<i2p::client::ClientDestination> localDestination, const i2p::data::IdentHash & remoteIdent);

		void Start ();
		void Stop ();

		/** @brief ack the garlic routing path */
		void Ack ();

		/** send an i2np message to remote endpoint for this session */
		void SendMsg (std::shared_ptr<I2NPMessage> msg);
		void FlushSendQueue (bool waitForLeaseSet = true);
		/** get the last time in milliseconds for when we used this datagram session */
		uint64_t LastActivity () const { return m_LastUse; }

		bool IsRatchets () const { return m_RoutingSession && m_RoutingSession->IsRatchets (); }
		void SetRemoteLeaseSet (std::shared_ptr<const i2p::data::LeaseSet> ls)
		{
			m_RemoteLeaseSet = ls;
			m_LoggedIncompatibleLeaseSet = false;
		}
		std::shared_ptr<const i2p::data::LeaseSet> GetRemoteLeaseSet () const { return m_RemoteLeaseSet; }
		const i2p::data::IdentHash & GetRemoteIdent () const { return m_RemoteIdent; }

		DatagramVersion GetVersion () const { return m_Version; }
		void SetVersion (DatagramVersion version) { m_Version = version; }

		void DropSharedRoutingPath ()
		{
			if (m_RoutingSession)
			{
				m_RoutingSession->SetSharedRoutingPath (nullptr);
				m_NumPathDrops++;
			}
		}
		// request LeaseSet update from floodfill in case our copy contains dead leases
		void RequestUpdatedLeaseSet ();

		uint32_t GetNumPathDrops () const { return m_NumPathDrops; }
		uint32_t GetNumDroppedNoPath () const { return m_NumDroppedNoPath; }

		struct Info
		{
			std::shared_ptr<const i2p::data::IdentHash> IBGW;
			std::shared_ptr<const i2p::data::IdentHash> OBEP;
			const uint64_t activity;

			Info ()
				: IBGW (nullptr)
				, OBEP (nullptr)
				, activity (0)
			{
			}
			Info (const uint8_t * ibgw, const uint8_t * obep, const uint64_t a)
				: activity (a)
			{
				if (ibgw)
					IBGW = std::make_shared<i2p::data::IdentHash> (ibgw);
				else
					IBGW = nullptr;
				if (obep)
					OBEP = std::make_shared<i2p::data::IdentHash> (obep);
				else
					OBEP = nullptr;
			}
		};

		Info GetSessionInfo () const;

	private:
		std::shared_ptr<i2p::garlic::GarlicRoutingPath> GetSharedRoutingPath ();

		void HandleLeaseSetUpdated (std::shared_ptr<i2p::data::LeaseSet> ls);

	private:
		std::shared_ptr<i2p::client::ClientDestination> m_LocalDestination;
		i2p::data::IdentHash m_RemoteIdent;
		std::shared_ptr<const i2p::data::LeaseSet> m_RemoteLeaseSet;
		std::shared_ptr<i2p::garlic::GarlicRoutingSession> m_RoutingSession;
		std::vector<std::weak_ptr<i2p::garlic::GarlicRoutingSession>> m_PendingRoutingSessions;
		std::vector<std::shared_ptr<const I2NPMessage>> m_SendQueue;
		uint64_t m_LastUse, m_LastFlush; // milliseconds
		bool m_RequestingLS;
		bool m_LoggedIncompatibleLeaseSet = false;
		DatagramVersion m_Version;
		uint32_t m_NumPathDrops = 0, m_NumDroppedNoPath = 0;
	};

	typedef std::shared_ptr<DatagramSession> DatagramSession_ptr;

	const size_t MAX_DATAGRAM_SIZE = 32768;

	// Parse the optional Datagram2/3 options block (16-bit length followed by a
	// Mapping). A zero-length Mapping is valid. Exposed for regression tests.
	bool ParseDatagramOptions (i2p::util::Mapping & options, const uint8_t * buf, size_t len, size_t & offset);

	class DatagramDestination
	{
		typedef std::function<void (const i2p::data::IdentityEx & from, uint16_t fromPort, uint16_t toPort,
			const uint8_t * buf, size_t len, const i2p::util::Mapping * options)>
			Receiver;
		// Datagram3 carries only the sender hash. A receiver that can accept that
		// form must not force a full LeaseSet or identity lookup on first contact.
		typedef std::function<void (const i2p::data::IdentHash & from, uint16_t fromPort, uint16_t toPort,
			const uint8_t * buf, size_t len, const i2p::util::Mapping * options)>
			HashReceiver;
		// Includes the I2CP protocol number for SAM 3.2 RAW RECEIVED headers.
		typedef std::function<void (
			uint8_t protocol, uint16_t fromPort, uint16_t toPort, const uint8_t * buf, size_t len)>
			RawReceiver;

	public:
		DatagramDestination (std::shared_ptr<i2p::client::ClientDestination> owner, bool gzip, DatagramVersion version);
		~DatagramDestination ();

		void SendDatagramTo (const uint8_t * payload, size_t len, const i2p::data::IdentHash & ident,
			uint16_t fromPort = 0, uint16_t toPort = 0);
		void SendRawDatagramTo (const uint8_t * payload, size_t len, const i2p::data::IdentHash & ident,
			uint16_t fromPort = 0, uint16_t toPort = 0, uint8_t protocol = DATAGRAM_RAW_PROTOCOL);
		// TODO: implement calls from other thread from SAM

		std::shared_ptr<DatagramSession> GetSession (const i2p::data::IdentHash & ident);
		void SendDatagram (std::shared_ptr<DatagramSession> session, const uint8_t * payload, size_t len,
			uint16_t fromPort, uint16_t toPort, const i2p::util::Mapping * options = nullptr);
		void SendRawDatagram (std::shared_ptr<DatagramSession> session, const uint8_t * payload, size_t len,
			uint16_t fromPort, uint16_t toPort, uint8_t protocol = DATAGRAM_RAW_PROTOCOL);
		void FlushSendQueue (std::shared_ptr<DatagramSession> session);

		void HandleDataMessagePayload (uint16_t fromPort, uint16_t toPort, const uint8_t * buf, size_t len,
			uint8_t protocolType, i2p::garlic::ECIESX25519AEADRatchetSession * from);

		void SetReceiver (const Receiver & receiver, uint16_t port = 0);
		void ResetReceiver (uint16_t port = 0);

		void SetHashReceiver (const HashReceiver & receiver, uint16_t port = 0);
		void ResetHashReceiver (uint16_t port = 0);

		void SetRawReceiver (const RawReceiver & receiver, uint16_t port = 0);
		void ResetRawReceiver (uint16_t port = 0);

		std::shared_ptr<DatagramSession::Info> GetInfoForRemote (const i2p::data::IdentHash & remote);

		// clean up stale sessions
		void CleanUp ();

	private:
		std::shared_ptr<DatagramSession> ObtainSession (const i2p::data::IdentHash & ident);
		std::shared_ptr<DatagramSession> FindSession (const i2p::data::IdentHash & ident) const;

		std::shared_ptr<I2NPMessage> CreateDataMessage (
			const std::vector<std::pair<const uint8_t *, size_t>> & payloads, uint16_t fromPort, uint16_t toPort,
			uint8_t protocolType, bool checksum = true);

		void HandleDatagram (uint16_t fromPort, uint16_t toPort, const uint8_t * buf, size_t len,
			i2p::garlic::ECIESX25519AEADRatchetSession * from);
		void HandleRawDatagram (uint8_t protocol, uint16_t fromPort, uint16_t toPort, const uint8_t * buf, size_t len);
		void HandleDatagram2 (uint16_t fromPort, uint16_t toPort, const uint8_t * buf, size_t len,
			i2p::garlic::ECIESX25519AEADRatchetSession * from);
		void HandleDatagram3 (uint16_t fromPort, uint16_t toPort, const uint8_t * buf, size_t len,
			i2p::garlic::ECIESX25519AEADRatchetSession * from);

		Receiver FindReceiver (uint16_t port);
		HashReceiver FindHashReceiver (uint16_t port);
		RawReceiver FindRawReceiver (uint16_t port);

	private:
		std::shared_ptr<i2p::client::ClientDestination> m_Owner;

		mutable std::mutex m_SessionsMutex;
		std::unordered_map<i2p::data::IdentHash, DatagramSession_ptr> m_Sessions;

		Receiver m_DefaultReceiver;
		HashReceiver m_DefaultHashReceiver;
		RawReceiver m_DefaultRawReceiver;
		uint16_t m_DefaultReceiverPort;
		uint16_t m_DefaultHashReceiverPort;
		uint16_t m_DefaultRawReceiverPort;
		std::mutex m_ReceiversMutex;
		std::mutex m_HashReceiversMutex;
		std::mutex m_RawReceiversMutex;
		std::unordered_map<uint16_t, Receiver> m_ReceiversByPorts;
		std::unordered_map<uint16_t, HashReceiver> m_HashReceiversByPorts;
		std::unordered_map<uint16_t, RawReceiver> m_RawReceiversByPorts;

		bool m_Gzip; // gzip compression of data messages
		DatagramVersion m_Version; // default for destination
		i2p::data::GzipInflator m_Inflator;
		std::unique_ptr<i2p::data::GzipDeflator> m_Deflator;
		i2p::util::Mapping m_Options;
		std::vector<uint8_t> m_From;
		i2p::util::MemoryPool<I2NPMessageBuffer<I2NP_MAX_MESSAGE_SIZE>> m_I2NPMsgsPool;
	};
}
}

#endif
