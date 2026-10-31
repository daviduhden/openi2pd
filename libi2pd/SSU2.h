/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: SSU2.
 *
 * Declares SSU2Server, the UDP transport manager: socket receive queue,
 * session and pending-handshake tables, bounded token, relay and
 * peer-test caches, and timers for termination, cleanup, resends and
 * introducer updates. It also owns ChaCha20-Poly1305 helpers,
 * MTU/firewall bookkeeping and the SOCKS5 UDP-associate proxy path.
 *
 */

#ifndef SSU2_H__
#define SSU2_H__

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <list>
#include <map>
#include <array>
#include <functional>
#include <mutex>
#include <random>
#include <string>
#include <future>
#include <memory>
#include "util.h"
#include "SSU2Session.h"
#include "SSU2OutOfSession.h"
#include "Socks5.h"

namespace i2p
{
namespace transport
{
	const int SSU2_TERMINATION_CHECK_TIMEOUT = 23; // in seconds
	const int SSU2_TERMINATION_CHECK_TIMEOUT_VARIANCE = 5; // in seconds
	const int SSU2_CLEANUP_INTERVAL = 72; // in seconds
	const int SSU2_RESEND_CHECK_TIMEOUT = 40; // in milliseconds
	const int SSU2_RESEND_CHECK_TIMEOUT_VARIANCE = 10; // in milliseconds
	const int SSU2_RESEND_CHECK_MORE_TIMEOUT = 4; // in milliseconds
	const int SSU2_RESEND_CHECK_MORE_TIMEOUT_VARIANCE = 9; // in milliseconds
	const size_t SSU2_MAX_RESEND_PACKETS = 128; // packets to resend at the time
	const uint64_t SSU2_SOCKET_MIN_BUFFER_SIZE = 128 * 1024;
#if defined(__OpenBSD__)
	const uint64_t SSU2_SOCKET_MAX_BUFFER_SIZE = 2 * 1024 * 1024;
#else
	const uint64_t SSU2_SOCKET_MAX_BUFFER_SIZE = 4 * 1024 * 1024;
#endif
	const size_t SSU2_MAX_NUM_INTRODUCERS = 3;
	const size_t SSU2_MIN_RECEIVED_PACKET_SIZE = 40; // 16 byte short header + 8 byte minimum payload + 16 byte MAC
	const size_t SSU2_MAX_RECEIVED_QUEUE_SIZE = 2500; // in packets
	const size_t SSU2_STOP_ACCEPTING_NEW_SESSIONS_QUEUE_SIZE = 500; // in packets
	const int SSU2_TO_INTRODUCER_SESSION_DURATION = 3600; // 1 hour
	const int SSU2_TO_INTRODUCER_SESSION_EXPIRATION = 4800; // 80 minutes
	const int SSU2_KEEP_ALIVE_INTERVAL = 15; // in seconds
	const int SSU2_KEEP_ALIVE_INTERVAL_VARIANCE = 4; // in seconds
	const int SSU2_PROXY_CONNECT_RETRY_TIMEOUT = 30; // in seconds
	const int SSU2_MIN_HOLE_PUNCH_EXPIRATION = 30; // in seconds
	const int SSU2_MAX_HOLE_PUNCH_EXPIRATION = 160; // in seconds
	const size_t SSU2_MAX_NUM_PACKETS_PER_BATCH = 64;
	// Untrusted endpoints can create token cache entries before the
	// handshake authenticates them. Java keeps these caches bounded; do
	// the same so a spoofed SessionRequest/TokenRequest flood cannot grow
	// the maps until the periodic cleanup timer runs.
	const size_t SSU2_MAX_INCOMING_TOKENS = 4096;
	const size_t SSU2_MAX_OUTGOING_TOKENS = 4096;
	// Recently-connected endpoints are used for hole punching. They are
	// also populated when a session terminates, so keep that cache bounded
	// too.
	const size_t SSU2_MAX_CONNECTED_RECENTLY = 4096;
	// Peer-test nonces are also provided by the remote side (msg 1). Keep
	// the bookkeeping bounded like the Java establishment manager.
	const size_t SSU2_MAX_PEER_TESTS = 64;
	const size_t SSU2_MAX_REQUESTED_PEER_TESTS = 64;

	/// UDP transport manager and socket owner for SSU2.
	///
	/// It owns the v4/v6 sockets, the session and pending-handshake maps,
	/// the token/relay/peer-test caches and the periodic termination,
	/// cleanup, resend and introducer-update timers. Unless a method
	/// documents otherwise, entries in `m_Sessions` are created, mutated
	/// and removed on the SSU2 service thread only; administrative readers
	/// must use `GetSSU2Sessions()`, which posts the copy onto that
	/// thread. The receive service only parses and queues packets.
	class SSU2Server : private i2p::util::RunnableServiceWithWork
	{
		struct Packet
		{
			uint8_t buf[SSU2_MAX_PACKET_SIZE];
			size_t len;
			boost::asio::ip::udp::endpoint from;
		};

		class ReceiveService : public i2p::util::RunnableService
		{
		public:
			ReceiveService (const std::string & name)
				: RunnableService (name) { };
			auto & GetService () { return GetIOService (); };
			void Start () { StartIOService (); };
			void Stop () { StopIOService (); };
		};

	public:
		SSU2Server ();
		~SSU2Server () { };

		void Start ();
		void Stop ();
		auto & GetService () { return GetIOService (); };
		void SetLocalAddress (const boost::asio::ip::address & localAddress);
		bool SetProxy (const std::string & address, uint16_t port);
		bool UsesProxy () const { return m_IsThroughProxy; };
		bool IsSupported (const boost::asio::ip::address & addr) const;
		uint16_t GetPort (bool v4) const;
		bool IsForcedFirewalled (bool v4) const { return v4 ? m_IsForcedFirewalled4 : m_IsForcedFirewalled6; }
		bool IsConnectedRecently (const boost::asio::ip::udp::endpoint & ep, bool max = true);
		void AddConnectedRecently (const boost::asio::ip::udp::endpoint & ep, uint64_t ts);
		std::mt19937 & GetRng () { return m_Rng; }
		bool AEADChaCha20Poly1305Encrypt (const uint8_t * msg, size_t msgLen, const uint8_t * ad, size_t adLen,
			const uint8_t * key, const uint8_t * nonce, uint8_t * buf, size_t len);
		bool AEADChaCha20Poly1305Decrypt (const uint8_t * msg, size_t msgLen, const uint8_t * ad, size_t adLen,
			const uint8_t * key, const uint8_t * nonce, uint8_t * buf, size_t len);
		void ChaCha20 (const uint8_t * msg, size_t msgLen, const uint8_t * key, const uint8_t * nonce, uint8_t * out);
		bool IsMaxNumIntroducers (bool v4) const
		{
			return (v4 ? m_Introducers.size () : m_IntroducersV6.size ()) >= SSU2_MAX_NUM_INTRODUCERS;
		}
		bool IsSyncClockFromPeers () const { return m_IsSyncClockFromPeers; };
		void AdjustTimeOffset (int64_t offset, std::shared_ptr<const i2p::data::IdentityEx> from);

		using SSU2Sessions = std::unordered_map<uint64_t, std::shared_ptr<SSU2Session>>;

		bool AddSession (std::shared_ptr<SSU2Session> session);
		// Removal is identity-checked when an owner is supplied so a losing
		// duplicate handshake cannot unregister the live session for a shared
		// connection ID or endpoint. Null owner preserves the legacy internal
		// calls that already hold the exact map entry.
		void RemoveSession (uint64_t connID, const std::shared_ptr<SSU2Session> & owner = nullptr);
		void RequestRemoveSession (uint64_t connID, const std::shared_ptr<SSU2Session> & owner = nullptr);
		void AddSessionByRouterHash (std::shared_ptr<SSU2Session> session);
		bool AddPendingOutgoingSession (std::shared_ptr<SSU2Session> session);
		void RemovePendingOutgoingSession (
			const boost::asio::ip::udp::endpoint & ep, const std::shared_ptr<SSU2Session> & owner = nullptr);
		std::shared_ptr<SSU2Session> FindSession (const i2p::data::IdentHash & ident);
		std::shared_ptr<SSU2Session> FindPendingOutgoingSession (const boost::asio::ip::udp::endpoint & ep) const;
		std::shared_ptr<SSU2Session> GetRandomPeerTestSession (
			i2p::data::RouterInfo::CompatibleTransports remoteTransports, const i2p::data::IdentHash & excluded);

		void AddRelay (uint32_t tag, std::shared_ptr<SSU2Session> relay);
		void RemoveRelay (uint32_t tag);
		std::shared_ptr<SSU2Session> FindRelaySession (uint32_t tag);

		SSU2PeerTestAdmission AddPeerTest (uint32_t nonce, std::shared_ptr<SSU2Session> aliceSession, uint64_t ts);
		std::shared_ptr<SSU2Session> GetPeerTest (uint32_t nonce);
		// Per-source PeerTest msg 1/2 rate limit (IPv4 /32, IPv6 /64; 12 per
		// 10 minutes like Java). Bounded map; expired entries are evicted on
		// insertion.
		bool AllowPeerTestMessage (const boost::asio::ip::address & source, uint64_t now);
		// Admission for out-of-session PeerTest pseudo-sessions, counted from
		// m_Sessions. Alice's test start and Charlie's msg 2 handler must
		// check it before creating a session. Service thread only.
		size_t GetNumPeerTestSessions () const;
		bool IsPeerTestSessionAdmissionAllowed () const;
		// Charlie-side RelayIntro anti-amplification: one HolePunch per
		// target within a five-second window and a bounded number of targets.
		bool AllowRelayHolePunch (const boost::asio::ip::udp::endpoint & ep);

		bool AddRequestedPeerTest (uint32_t nonce, std::shared_ptr<SSU2PeerTestSession> session, uint64_t ts);
		std::shared_ptr<SSU2PeerTestSession> GetRequestedPeerTest (uint32_t nonce);

		void Send (const uint8_t * header, size_t headerLen, const uint8_t * payload, size_t payloadLen,
			const boost::asio::ip::udp::endpoint & to);
		void Send (const uint8_t * header, size_t headerLen, const uint8_t * headerX, size_t headerXLen,
			const uint8_t * payload, size_t payloadLen, const boost::asio::ip::udp::endpoint & to);

		bool CreateSession (std::shared_ptr<const i2p::data::RouterInfo> router,
			std::shared_ptr<const i2p::data::RouterInfo::Address> address, bool peerTest = false);
		bool StartPeerTest (std::shared_ptr<const i2p::data::RouterInfo> router, bool v4);

		void UpdateOutgoingToken (const boost::asio::ip::udp::endpoint & ep, uint64_t token, uint32_t exp);
		uint64_t FindOutgoingToken (const boost::asio::ip::udp::endpoint & ep);
		uint64_t GetIncomingToken (const boost::asio::ip::udp::endpoint & ep);
		void RemoveIncomingToken (const boost::asio::ip::udp::endpoint & ep);
		std::pair<uint64_t, uint32_t> NewIncomingToken (const boost::asio::ip::udp::endpoint & ep);

		void RescheduleIntroducersUpdateTimer ();
		void RescheduleIntroducersUpdateTimerV6 ();

		i2p::util::MemoryPool<SSU2SentPacket> & GetSentPacketsPool () { return m_SentPacketsPool; };
		i2p::util::MemoryPool<SSU2IncompleteMessage> & GetIncompleteMessagesPool ()
		{
			return m_IncompleteMessagesPool;
		};
		i2p::util::MemoryPool<SSU2IncompleteMessage::Fragment> & GetFragmentsPool () { return m_FragmentsPool; };

		void SetVersion (int version);
		int GetVersion () const { return m_Version; }

	private:
		boost::asio::ip::udp::socket & OpenSocket (const boost::asio::ip::udp::endpoint & localEndpoint);
		void Receive (boost::asio::ip::udp::socket & socket);
		void HandleReceivedFrom (const boost::system::error_code & ecode, size_t bytes_transferred, Packet * packet,
			boost::asio::ip::udp::socket & socket);
		void HandleReceivedPackets (std::list<Packet *> && packets);
		void ProcessNextPacket (uint8_t * buf, size_t len, const boost::asio::ip::udp::endpoint & senderEndpoint);
		void InsertToReceivedPacketsQueue (Packet * packet);
		void InsertToReceivedPacketsQueue (std::list<Packet *> & packets);
		void HandleReceivedPacketsQueue ();

		void ScheduleTermination ();
		void HandleTerminationTimer (const boost::system::error_code & ecode);

		void ScheduleCleanup ();
		void HandleCleanupTimer (const boost::system::error_code & ecode);

		void ScheduleResend (bool more);
		void HandleResendTimer (const boost::system::error_code & ecode);

		bool CheckPendingOutgoingSession (const boost::asio::ip::udp::endpoint & ep, bool peerTest);
		// Count of non-established inbound handshakes in m_Sessions. Service
		// thread only.
		size_t GetNumInboundHandshakeSessions () const;
		bool IsInboundAdmissionAllowed () const;
		// Answer a rejected handshake with a Retry carrying
		// eSSU2TerminationReasonConnectionLimits (19) instead of creating
		// unbounded session state. Packets without a handshake context are
		// dropped; the temporary session is never inserted into m_Sessions.
		void RejectInboundHandshake (
			uint64_t connID, uint8_t * buf, size_t len, const boost::asio::ip::udp::endpoint & senderEndpoint);
		void ConnectThroughIntroducer (std::shared_ptr<SSU2Session> session);
		std::vector<std::shared_ptr<SSU2Session>> FindIntroducers (
			int maxNumIntroducers, bool v4, const std::unordered_set<i2p::data::IdentHash> & excluded);
		void UpdateIntroducers (bool v4);
		void ScheduleIntroducersUpdateTimer ();
		void HandleIntroducersUpdateTimer (const boost::system::error_code & ecode, bool v4);
		void ScheduleIntroducersUpdateTimerV6 ();

		void EvictIncomingToken (uint64_t now);
		void EvictOutgoingToken (uint64_t now);

		void SendThroughProxy (const uint8_t * header, size_t headerLen, const uint8_t * headerX, size_t headerXLen,
			const uint8_t * payload, size_t payloadLen, const boost::asio::ip::udp::endpoint & to);
		void ProcessNextPacketFromProxy (uint8_t * buf, size_t len);
		void ConnectToProxy ();
		void ReconnectToProxy ();
		void HandshakeWithProxy ();
		void ReadHandshakeWithProxyReply ();
		void SendUDPAssociateRequest ();
		void ReadUDPAssociateReply ();
		void ReadUDPAssociateSocket (); // handle if closed by peer

	private:
		// The receive service may drain handlers that use pool-backed data.
		// Declare pools first so they outlive the service during destruction.
		i2p::util::MemoryPoolMt<Packet> m_PacketsPool;
		i2p::util::MemoryPool<SSU2SentPacket> m_SentPacketsPool;
		i2p::util::MemoryPool<SSU2IncompleteMessage> m_IncompleteMessagesPool;
		i2p::util::MemoryPool<SSU2IncompleteMessage::Fragment> m_FragmentsPool;
		ReceiveService m_ReceiveService;
		// Residual threading hazard (not fixed here): these sockets are
		// shared by the SSU2 service (synchronous send_to in
		// Send()/SendThroughProxy()) and the receive service
		// (async_receive_from and the error path that calls OpenSocket()). A
		// socket error on the receive service can close()/open() the object
		// while the other thread is inside send_to(). Removing it requires a
		// dedicated send socket, or posting every send onto the receive
		// service and changing Send() to an asynchronous API; neither is safe
		// as a local patch because senders currently rely on the synchronous
		// result and buffer lifetime.
		boost::asio::ip::udp::socket m_SocketV4, m_SocketV6;
		boost::asio::ip::address m_AddressV4, m_AddressV6;
		SSU2Sessions m_Sessions;
		std::unordered_map<i2p::data::IdentHash, std::weak_ptr<SSU2Session>> m_SessionsByRouterHash;
		mutable std::mutex m_SessionsByRouterHashMutex;
		std::map<boost::asio::ip::udp::endpoint, std::shared_ptr<SSU2Session>> m_PendingOutgoingSessions;
		mutable std::mutex m_PendingOutgoingSessionsMutex;
		std::map<boost::asio::ip::udp::endpoint, std::pair<uint64_t, uint32_t>> m_IncomingTokens,
			m_OutgoingTokens; // remote endpoint -> (token, expires in seconds)
		std::unordered_map<uint32_t, std::weak_ptr<SSU2Session>> m_Relays; // we are introducer, relay tag -> session
		std::unordered_map<uint32_t, std::pair<std::weak_ptr<SSU2Session>, uint64_t>>
			m_PeerTests; // nonce->(Alice, timestamp). We are Bob
		std::list<std::pair<i2p::data::IdentHash, uint32_t>> m_Introducers,
			m_IntroducersV6; // introducers we are connected to
		boost::asio::steady_timer m_TerminationTimer, m_CleanupTimer, m_ResendTimer, m_IntroducersUpdateTimer,
			m_IntroducersUpdateTimerV6;
		std::shared_ptr<SSU2Session> m_LastSession;
		bool m_IsPublished; // if we maintain introducers
		bool m_IsSyncClockFromPeers;
		int64_t m_PendingTimeOffset; // during peer test
		std::shared_ptr<const i2p::data::IdentityEx> m_PendingTimeOffsetFrom;
		std::mt19937 m_Rng;
		std::map<boost::asio::ip::udp::endpoint, uint64_t>
			m_ConnectedRecently; // endpoint -> last activity time in seconds
		mutable std::mutex m_ConnectedRecentlyMutex;
		std::map<boost::asio::ip::udp::endpoint, uint64_t>
			m_RecentHolePunches; // endpoint -> last HolePunch time in seconds
		mutable std::mutex m_RecentHolePunchesMutex;
		// Per-source PeerTest msg 1/2 limiter keyed by IPv4 /32 or IPv6 /64.
		// std::map keeps this independent of std::hash specialisations for
		// boost::asio::ip::address; the table is capped at
		// SSU2_MAX_PEER_TEST_RATE_ENTRIES so a linear/logarithmic lookup is
		// irrelevant.
		std::map<boost::asio::ip::address, SSU2PeerTestRateState> m_PeerTestRate;
		mutable std::mutex m_PeerTestRateMutex;
		std::unordered_map<uint32_t, std::pair<std::weak_ptr<SSU2PeerTestSession>, uint64_t>>
			m_RequestedPeerTests; // nonce->(Alice, timestamp)
		std::list<Packet *> m_ReceivedPacketsQueue;
		mutable std::mutex m_ReceivedPacketsQueueMutex;
		i2p::crypto::AEADChaCha20Poly1305Encryptor m_Encryptor;
		i2p::crypto::AEADChaCha20Poly1305Decryptor m_Decryptor;
		i2p::crypto::ChaCha20Context m_ChaCha20;
		bool m_IsForcedFirewalled4, m_IsForcedFirewalled6;
		int m_Version;

		// proxy
		bool m_IsThroughProxy;
		// SOCKS5 handshake/associate scratch buffer. It is owned by the
		// receive service (ConnectToProxy..ReadUDPAssociateReply) and is
		// never used to compose outgoing UDP datagrams; SendThroughProxy
		// builds its request header on its own stack frame.
		uint8_t m_ProxyHandshakeBuffer[SOCKS5_UDP_IPV6_REQUEST_HEADER_SIZE];
		// Draining the SOCKS UDP-associate socket must not reuse any
		// handshake buffer: the receive service has a pending read there.
		uint8_t m_ProxyUDPAssociateBuffer[1];
		// m_ProxyRelayEndpoint is published by the receive service and
		// consumed by SendThroughProxy on the SSU2 service. Copy it under
		// this lock before dereferencing so a reconnect cannot free it
		// mid-send.
		std::mutex m_ProxyRelayEndpointMutex;
		std::unique_ptr<boost::asio::ip::tcp::endpoint> m_ProxyEndpoint;
		std::unique_ptr<boost::asio::ip::tcp::socket> m_UDPAssociateSocket;
		std::unique_ptr<boost::asio::ip::udp::endpoint> m_ProxyRelayEndpoint;
		std::unique_ptr<boost::asio::steady_timer> m_ProxyConnectRetryTimer;

	public:
		// for HTTP/I2PControl Safe form: the caller owns the returned copy
		// and does not need to keep a reference alive until the future
		// completes.
		std::future<SSU2Sessions> GetSSU2Sessions ()
		{
			return boost::asio::post (GetService (), boost::asio::use_future ([this] () { return m_Sessions; }));
		};

		// Legacy reference form. The caller must keep `sessions` alive until
		// the future is waited on; prefer the no-argument overload above.
		std::future<void> GetSSU2Sessions (SSU2Sessions & sessions)
		{
			return boost::asio::post (
				GetService (), boost::asio::use_future ([this, &sessions] () { sessions = m_Sessions; }));
		};
	};
}
}

#endif
