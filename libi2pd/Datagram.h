/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Datagram.
 *
 * Declares the I2P datagram endpoint for I2CP protocols 17, 18, 19 and 20 and
 * its per-peer garlic sessions. Sessions manage routing paths, LeaseSet
 * refresh, bounded send queues and idle cleanup. Identity and hash receivers
 * are keyed by (protocol, port) so Datagram1/2/3 subsessions can share a port,
 * and raw receivers use the same keying (SAM 3.3 LISTEN_PROTOCOL). The framing
 * version is per send, not only per destination.
 *
 */

#ifndef DATAGRAM_H__
#define DATAGRAM_H__

#include <inttypes.h>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <utility>
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
	namespace detail
	{
		struct ReceiverInvocationFrame
		{
			const void * slot;
			ReceiverInvocationFrame * previous;
		};

		inline thread_local ReceiverInvocationFrame * g_ReceiverInvocationFrame = nullptr;

		inline bool IsInReceiverCallback ()
		{
			return g_ReceiverInvocationFrame != nullptr;
		}

		template <typename Callback> class ReceiverSlot
		{
		public:
			explicit ReceiverSlot (Callback callback)
				: m_Callback (std::move (callback))
			{
			}
			bool HasCallback () const { return static_cast<bool> (m_Callback); }

			template <typename... Args> bool Invoke (Args &&... args)
			{
				Callback callback;
				{
					std::lock_guard<std::mutex> lock (m_Mutex);
					if (!m_Active || !m_Callback) return false;
					// Preserve the previous dispatcher semantics: every dispatch gets
					// its own function-target snapshot. Copy before incrementing so an
					// exception from a target copy cannot leak the in-flight count.
					callback = m_Callback;
					++m_InFlight;
				}
				struct Invocation
				{
					ReceiverSlot & slot;
					ReceiverInvocationFrame frame;
					Invocation (ReceiverSlot & s)
						: slot (s)
						, frame { &s, g_ReceiverInvocationFrame }
					{
						g_ReceiverInvocationFrame = &frame;
					}
					~Invocation ()
					{
						g_ReceiverInvocationFrame = frame.previous;
						std::lock_guard<std::mutex> lock (slot.m_Mutex);
						--slot.m_InFlight;
						slot.m_Drained.notify_all ();
					}
				} invocation (*this);
				callback (std::forward<Args> (args)...);
				return true;
			}

			// Returns false when called from any receiver callback. In that
			// context waiting could deadlock through same-slot or cross-slot
			// callback cycles; deactivation still prevents future dispatch.
			// Owners that need a lifetime barrier must reset from outside callbacks.
			bool DeactivateAndWait ()
			{
				std::unique_lock<std::mutex> lock (m_Mutex);
				m_Active = false;
				if (IsInReceiverCallback ()) return false;
				m_Drained.wait (lock, [this] { return m_InFlight == 0; });
				return true;
			}

		private:
			Callback m_Callback;
			std::mutex m_Mutex;
			std::condition_variable m_Drained;
			size_t m_InFlight = 0;
			bool m_Active = true;
		};
	}

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
		// Unset: keep the session's framing (destination default or the style
		// learned from a received Datagram1/Datagram2/Datagram3).
		eDatagramDefault = 0,
		eDatagramV1 = 1,
		eDatagramV2 = 2,
		eDatagramV3 = 3,
	};

	// Resolve the framing version of one send. An explicit request wins, because
	// a SAM DATAGRAM3 subsession shares the DatagramDestination that a
	// DATAGRAM/DATAGRAM2 session created with its own version, and Java selects
	// the framing per message from the subsession style. eDatagramDefault keeps
	// the session's version.
	constexpr DatagramVersion ResolveDatagramVersion (DatagramVersion requested, DatagramVersion session)
	{
		return requested == eDatagramDefault ? session : requested;
	}

	constexpr bool IsDatagramVersionSupported (DatagramVersion version)
	{
		return version == eDatagramV1 || version == eDatagramV2 || version == eDatagramV3;
	}

	constexpr uint8_t DATAGRAM2_FLAG_OPTIONS = 0x10;
	constexpr uint8_t DATAGRAM2_FLAG_OFFLINE_SIGNATURE = 0x20;
	constexpr uint8_t DATAGRAM3_FLAG_OPTIONS = 0x10;
	// I2CP PROTOCOL_TYPE_RAW. Datagram.h is included by Destination.h, so the
	// constant is duplicated here instead of creating an include cycle.
	constexpr uint8_t DATAGRAM_RAW_PROTOCOL = 18;

	// I2CP protocols that a typed handler consumes (PROTOCOL_TYPE_DATAGRAM 17,
	// PROTOCOL_TYPE_DATAGRAM2 19, PROTOCOL_TYPE_DATAGRAM3 20). Duplicated here
	// for the same include-cycle reason: HandleDataMessagePayload dispatches
	// them before the raw path, so a raw receiver registered for one of them
	// could never receive anything.
	constexpr bool IsTypedDatagramProtocol (uint8_t protocol)
	{
		return protocol == 17 || protocol == 19 || protocol == 20;
	}

	// Streaming (6) and the typed datagram protocols are never forwarded to a
	// raw receiver. SAM 3.3 refuses the same set on LISTEN_PROTOCOL.
	constexpr bool IsReservedRawProtocol (uint8_t protocol)
	{
		return protocol == 6 || IsTypedDatagramProtocol (protocol);
	}

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
		uint64_t LastActivity () const { return m_LastUse.load (std::memory_order_relaxed); }

		bool IsRatchets () const;
		void SetRemoteLeaseSet (std::shared_ptr<const i2p::data::LeaseSet> ls);
		std::shared_ptr<const i2p::data::LeaseSet> GetRemoteLeaseSet () const;
		const i2p::data::IdentHash & GetRemoteIdent () const { return m_RemoteIdent; }

		DatagramVersion GetVersion () const { return m_Version.load (std::memory_order_relaxed); }
		void SetVersion (DatagramVersion version) { m_Version.store (version, std::memory_order_relaxed); }

		void DropSharedRoutingPath ();

		// Deterministic test seam: while an observer is installed, SendMsg hands
		// the built I2NP Data message to it instead of queueing it, so the
		// per-send framing can be verified without a routing path or a peer. The
		// observer is invoked without the session lock and must not call back
		// into this session. Never installed by the daemon.
		void SetSendObserverForTesting (std::function<void (const I2NPMessage &)> observer);
		// request LeaseSet update from floodfill in case our copy contains dead leases
		void RequestUpdatedLeaseSet ();

		uint32_t GetNumPathDrops () const { return m_NumPathDrops.load (std::memory_order_relaxed); }
		uint32_t GetNumDroppedNoPath () const { return m_NumDroppedNoPath.load (std::memory_order_relaxed); }

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
		// Caller holds m_Mutex. GetSharedRoutingPath may re-enter FlushSendQueue
		// through the LeaseSet request callback only after the lock is released.
		std::shared_ptr<i2p::garlic::GarlicRoutingPath> GetSharedRoutingPath ();
		void SendMsgLocked (std::shared_ptr<I2NPMessage> msg);
		void FlushSendQueueLocked (bool waitForLeaseSet);
		bool IsRatchetsLocked () const { return m_RoutingSession && m_RoutingSession->IsRatchets (); }

		void HandleLeaseSetUpdated (std::shared_ptr<i2p::data::LeaseSet> ls);

	private:
		std::shared_ptr<i2p::client::ClientDestination> m_LocalDestination;
		const i2p::data::IdentHash m_RemoteIdent;
		// m_Mutex serializes the send queue, the routing-session state and the
		// remote LeaseSet. SAM calls SendDatagramTo/SendDatagram from its own
		// socket thread while inbound datagrams Ack() on the destination
		// service, so none of these fields may be touched without it. Lock order
		// is DatagramDestination::m_SessionsMutex -> m_Mutex; a session method
		// never acquires m_SessionsMutex.
		mutable std::mutex m_Mutex;
		std::shared_ptr<const i2p::data::LeaseSet> m_RemoteLeaseSet;
		std::shared_ptr<i2p::garlic::GarlicRoutingSession> m_RoutingSession;
		std::vector<std::weak_ptr<i2p::garlic::GarlicRoutingSession>> m_PendingRoutingSessions;
		std::vector<std::shared_ptr<const I2NPMessage>> m_SendQueue;
		// Relaxed atomics: liveness/counters read from CleanUp and the
		// administrative views without taking m_Mutex.
		std::atomic<uint64_t> m_LastUse { 0 }; // milliseconds
		std::atomic<uint32_t> m_NumPathDrops { 0 }, m_NumDroppedNoPath { 0 };
		uint64_t m_LastFlush = 0; // milliseconds, guarded by m_Mutex
		bool m_RequestingLS = false;
		bool m_LoggedIncompatibleLeaseSet = false;
		std::atomic<DatagramVersion> m_Version { eDatagramV1 };
		std::function<void (const I2NPMessage &)> m_SendObserver; // guarded by m_Mutex
	};

	typedef std::shared_ptr<DatagramSession> DatagramSession_ptr;

	// Local inbound decompression bound. Java's DataMessage limit is 61 KiB, so
	// this is a deliberate divergence (a larger Datagram3 would be dropped
	// here); it is tracked for the coordinator instead of being changed with
	// the protocol-version work.
	const size_t MAX_DATAGRAM_SIZE = 32768;

	// Parse the optional Datagram2/3 options block (16-bit length followed by a
	// Mapping). A zero-length Mapping is valid. Exposed for regression tests.
	bool ParseDatagramOptions (i2p::util::Mapping & options, const uint8_t * buf, size_t len, size_t & offset);

	class DatagramDestination : public std::enable_shared_from_this<DatagramDestination>
	{
		typedef std::function<void (const i2p::data::IdentityEx & from, uint16_t fromPort, uint16_t toPort,
			const uint8_t * buf, size_t len, const i2p::util::Mapping * options)>
			Receiver;
		// Datagram3 carries only the sender hash. A receiver that can accept that
		// form must not force a full LeaseSet or identity lookup on first contact.
		typedef std::function<void (const i2p::data::IdentHash & from, uint16_t fromPort, uint16_t toPort,
			const uint8_t * buf, size_t len, const i2p::util::Mapping * options)>
			HashReceiver;
		// Includes the I2CP protocol number for SAM 3.2 RAW RECEIVED headers and
		// for SAM 3.3 LISTEN_PROTOCOL dispatch.
		typedef std::function<void (
			uint8_t protocol, uint16_t fromPort, uint16_t toPort, const uint8_t * buf, size_t len)>
			RawReceiver;
		typedef std::shared_ptr<detail::ReceiverSlot<Receiver>> ReceiverSlotPtr;
		typedef std::shared_ptr<detail::ReceiverSlot<HashReceiver>> HashReceiverSlotPtr;
		typedef std::shared_ptr<detail::ReceiverSlot<RawReceiver>> RawReceiverSlotPtr;

	public:
		class ReceiverRegistration
		{
		public:
			ReceiverRegistration () = default;
			// true means all invocations have drained. Reset from any receiver
			// callback only deactivates; it returns false to avoid callback cycles.
			// Such a reset cannot safely release callback-captured owner state until
			// the callback has returned.
			bool Reset () const;
			explicit operator bool () const { return !m_Slot.expired () && !m_Destination.expired (); }

		private:
			enum class Kind : uint8_t
			{
				Identity,
				Hash,
				Raw
			};
			friend class DatagramDestination;
			ReceiverRegistration (std::weak_ptr<DatagramDestination> destination, std::weak_ptr<void> slot,
				uint16_t port, ReceiverRegistration::Kind kind, uint8_t protocol = 0)
				: m_Destination (std::move (destination))
				, m_Slot (std::move (slot))
				, m_Port (port)
				, m_Kind (kind)
				, m_Protocol (protocol)
			{
			}
			std::weak_ptr<DatagramDestination> m_Destination;
			std::weak_ptr<void> m_Slot;
			uint16_t m_Port = 0;
			Kind m_Kind = Kind::Identity;
			// Receiving protocol: SAM 3.3 LISTEN_PROTOCOL for a RAW
			// registration and the I2CP style protocol (17/19/20) for an
			// identity/hash registration. 0 is the catch-all.
			uint8_t m_Protocol = 0;
		};

		DatagramDestination (std::shared_ptr<i2p::client::ClientDestination> owner, bool gzip, DatagramVersion version);
		~DatagramDestination ();

		// `version` selects the framing of this message. eDatagramDefault keeps
		// the session's version; an explicit V1/V2/V3 overrides it so a SAM
		// subsession style is not frozen by the destination's default.
		void SendDatagramTo (const uint8_t * payload, size_t len, const i2p::data::IdentHash & ident,
			uint16_t fromPort = 0, uint16_t toPort = 0, DatagramVersion version = eDatagramDefault);
		void SendRawDatagramTo (const uint8_t * payload, size_t len, const i2p::data::IdentHash & ident,
			uint16_t fromPort = 0, uint16_t toPort = 0, uint8_t protocol = DATAGRAM_RAW_PROTOCOL);
		// Send-time entry points may be called from SAM socket threads. The
		// session send queue and routing state are mutex-guarded; the message
		// pool is thread-safe and the gzip deflator is serialized.

		std::shared_ptr<DatagramSession> GetSession (const i2p::data::IdentHash & ident);
		void SendDatagram (std::shared_ptr<DatagramSession> session, const uint8_t * payload, size_t len,
			uint16_t fromPort, uint16_t toPort, const i2p::util::Mapping * options = nullptr,
			DatagramVersion version = eDatagramDefault);
		void SendRawDatagram (std::shared_ptr<DatagramSession> session, const uint8_t * payload, size_t len,
			uint16_t fromPort, uint16_t toPort, uint8_t protocol = DATAGRAM_RAW_PROTOCOL);
		void FlushSendQueue (std::shared_ptr<DatagramSession> session);

		void HandleDataMessagePayload (uint16_t fromPort, uint16_t toPort, const uint8_t * buf, size_t len,
			uint8_t protocolType, i2p::garlic::ECIESX25519AEADRatchetSession * from);

		// Identity receiver for a (protocol, port) pair. The two-argument form
		// registers the protocol-0 catch-all; a port-specific registration is
		// never promoted to the destination-wide fallback, so it cannot capture
		// another subsession's traffic. Only an explicit (0, 0) registration
		// becomes the fallback (Java: PROTO_ANY, PORT_ANY).
		ReceiverRegistration SetReceiver (const Receiver & receiver, uint16_t port = 0, uint8_t protocol = 0);

		// Sender-hash receiver with the same keying. DATAGRAM3 registers
		// protocol 20.
		ReceiverRegistration SetHashReceiver (const HashReceiver & receiver, uint16_t port = 0, uint8_t protocol = 0);

		// Port-keyed RAW receiver. Protocol 0 is the catch-all, so the legacy
		// two-argument form keeps receiving every raw protocol as before.
		ReceiverRegistration SetRawReceiver (const RawReceiver & receiver, uint16_t port = 0);

		// SAM 3.3 LISTEN_PROTOCOL. Selects the raw receiver for an exact I2CP
		// payload protocol with protocol 0 as the catch-all fallback. Streaming
		// (6) and the typed datagram protocols (17/19/20) are consumed by their
		// own handlers and are refused.
		ReceiverRegistration SetRawReceiver (const RawReceiver & receiver, uint16_t port, uint8_t protocol);

		std::shared_ptr<DatagramSession::Info> GetInfoForRemote (const i2p::data::IdentHash & remote);

		// Rebuild the cached Datagram1/2 sender identity after SetPrivateKeys or
		// an offline-signature refresh.
		void RefreshIdentity ();

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

		ReceiverSlotPtr FindReceiver (uint16_t port, uint8_t protocol);
		HashReceiverSlotPtr FindHashReceiver (uint16_t port, uint8_t protocol);
		RawReceiverSlotPtr FindRawReceiver (uint16_t port, uint8_t protocol);
		bool ResetRegistration (const ReceiverRegistration & registration);

	private:
		std::shared_ptr<i2p::client::ClientDestination> m_Owner;

		mutable std::mutex m_SessionsMutex;
		std::unordered_map<i2p::data::IdentHash, DatagramSession_ptr> m_Sessions;

		ReceiverSlotPtr m_DefaultReceiver;
		HashReceiverSlotPtr m_DefaultHashReceiver;
		RawReceiverSlotPtr m_DefaultRawReceiver;
		std::mutex m_ReceiversMutex;
		std::mutex m_HashReceiversMutex;
		std::mutex m_RawReceiversMutex;
		// (protocol << 16) | port for all three receiver kinds. Keying identity
		// and hash receivers by protocol keeps a DATAGRAM2 subsession from
		// receiving Datagram1 payloads (and vice versa) on a shared port.
		std::unordered_map<uint32_t, ReceiverSlotPtr> m_ReceiversByProtocolPorts;
		std::unordered_map<uint32_t, HashReceiverSlotPtr> m_HashReceiversByProtocolPorts;
		std::unordered_map<uint32_t, RawReceiverSlotPtr> m_RawReceiversByProtocolPorts;

		const bool m_Gzip; // gzip compression of data messages
		const DatagramVersion m_Version; // default for destination
		// The zlib streams keep per-datagram state, so they are serialized:
		// SAM sends on its socket thread while inbound datagrams inflate on the
		// destination service.
		std::mutex m_InflatorMutex;
		i2p::data::GzipInflator m_Inflator;
		std::mutex m_DeflatorMutex;
		std::unique_ptr<i2p::data::GzipDeflator> m_Deflator;
		std::mutex m_FromMutex;
		std::vector<uint8_t> m_From;
		// Thread-safe pool: messages are acquired on SAM threads and released
		// from whichever thread drops the last reference.
		i2p::util::MemoryPoolMt<I2NPMessageBuffer<I2NP_MAX_MESSAGE_SIZE>> m_I2NPMsgsPool;
	};
}
}

#endif
