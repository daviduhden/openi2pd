/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: I2CP.
 *
 * Declares the I2CP server, the per-connection session state machine and the
 * destination adapter that exposes a client-managed destination to the
 * streaming and datagram layers.
 *
 */

#ifndef I2CP_H__
#define I2CP_H__

#include <inttypes.h>
#include <string>
#include <string_view>
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>
#include <map>
#include <unordered_map>
#include <vector>
#include <utility>
#include <boost/asio.hpp>
#include "util.h"
#include "Destination.h"
#include "Streaming.h"
#include "CryptoKey.h"

namespace i2p
{
namespace client
{
	const uint8_t I2CP_PROTOCOL_BYTE = 0x2A;
	const size_t I2CP_SESSION_BUFFER_SIZE = 4096;
	const size_t I2CP_MAX_MESSAGE_LENGTH = 65535;
	const size_t I2CP_MAX_SEND_QUEUE_SIZE = 1024 * 1024; // in bytes, 1M
	const int I2CP_LEASESET_CREATION_TIMEOUT = 10; // in seconds
	const int I2CP_DESTINATION_READINESS_CHECK_INTERVAL = 5; // in seconds
	const int I2CP_SESSION_ACK_REQUEST_INTERVAL = 12100; // in milliseconds
	const int I2CP_RATCHETS_RESPONSE_TIMEOUT = 1000; // in milliseconds

	const size_t I2CP_HEADER_LENGTH_OFFSET = 0;
	const size_t I2CP_HEADER_TYPE_OFFSET = I2CP_HEADER_LENGTH_OFFSET + 4;
	const size_t I2CP_HEADER_SIZE = I2CP_HEADER_TYPE_OFFSET + 1;

	const uint8_t I2CP_GET_DATE_MESSAGE = 32;
	const uint8_t I2CP_SET_DATE_MESSAGE = 33;
	const uint8_t I2CP_CREATE_SESSION_MESSAGE = 1;
	const uint8_t I2CP_RECONFIGURE_SESSION_MESSAGE = 2;
	const uint8_t I2CP_SESSION_STATUS_MESSAGE = 20;
	const uint8_t I2CP_DESTROY_SESSION_MESSAGE = 3;
	const uint8_t I2CP_REQUEST_VARIABLE_LEASESET_MESSAGE = 37;
	const uint8_t I2CP_CREATE_LEASESET_MESSAGE = 4;
	const uint8_t I2CP_CREATE_LEASESET2_MESSAGE = 41;
	const uint8_t I2CP_BLINDING_INFO_MESSAGE = 42;
	const uint8_t I2CP_SEND_MESSAGE_MESSAGE = 5;
	const uint8_t I2CP_SEND_MESSAGE_EXPIRES_MESSAGE = 36;
	const uint8_t I2CP_MESSAGE_PAYLOAD_MESSAGE = 31;
	const uint8_t I2CP_MESSAGE_STATUS_MESSAGE = 22;
	const uint8_t I2CP_HOST_LOOKUP_MESSAGE = 38;
	const uint8_t I2CP_HOST_REPLY_MESSAGE = 39;
	const uint8_t I2CP_DEST_LOOKUP_MESSAGE = 34;
	const uint8_t I2CP_DEST_REPLY_MESSAGE = 35;
	const uint8_t I2CP_GET_BANDWIDTH_LIMITS_MESSAGE = 8;
	const uint8_t I2CP_BANDWIDTH_LIMITS_MESSAGE = 23;

	enum I2CPMessageStatus
	{
		eI2CPMessageStatusAccepted = 1,
		eI2CPMessageStatusGuaranteedSuccess = 4,
		eI2CPMessageStatusGuaranteedFailure = 5,
		eI2CPMessageStatusNoLocalTunnels = 16,
		eI2CPMessageStatusNoLeaseSet = 21
	};

	enum I2CPSessionStatus
	{
		eI2CPSessionStatusDestroyed = 0,
		eI2CPSessionStatusCreated = 1,
		eI2CPSessionStatusUpdated = 2,
		eI2CPSessionStatusInvalid = 3,
		eI2CPSessionStatusRefused = 4
	};

	// params
	const char I2CP_PARAM_MESSAGE_RELIABILITY[] = "i2cp.messageReliability";

	// Cache key for a remote garlic routing session. The complete wire identity
	// is hashed, so DSA, ECDSA, EdDSA and offline-signed destinations each key
	// their own entry; no fixed signing-key offset is involved.
	i2p::data::IdentHash GetI2CPRoutingSessionKey (const uint8_t * identity, size_t len);

	class I2CPSession;
	class I2CPDestination : public LeaseSetDestination
	{
	public:
		I2CPDestination (boost::asio::io_context & service, std::shared_ptr<I2CPSession> owner,
			std::shared_ptr<const i2p::data::IdentityEx> identity, bool isPublic, bool isSameThread,
			const i2p::util::Mapping & params);
		~I2CPDestination () { };

		void Stop () override;

		void SetEncryptionPrivateKey (i2p::data::CryptoKeyType keyType, const uint8_t * key);
		void SetECIESx25519EncryptionPrivateKey (i2p::data::CryptoKeyType keyType, const uint8_t * key);
		void LeaseSetCreated (const uint8_t * buf, size_t len); // called from I2CPSession
		void LeaseSet2Created (uint8_t storeType, const uint8_t * buf, size_t len); // called from I2CPSession
		void SendMsgTo (const uint8_t * payload, size_t len, const i2p::data::IdentHash & ident,
			uint32_t nonce); // called from I2CPSession
		bool SendMsg (const uint8_t * payload, size_t len,
			std::shared_ptr<i2p::garlic::GarlicRoutingSession> remoteSession, uint32_t nonce);

		// implements LocalDestination
		bool Decrypt (
			const uint8_t * encrypted, uint8_t * data, i2p::data::CryptoKeyType preferredCrypto) const override;
		bool SupportsEncryptionType (i2p::data::CryptoKeyType keyType) const override;
		const uint8_t * GetEncryptionPublicKey (i2p::data::CryptoKeyType keyType) const override; // for 4 only
		std::shared_ptr<const i2p::data::IdentityEx> GetIdentity () const override { return m_Identity; };

	protected:
		// GarlicDestination
		i2p::data::CryptoKeyType GetRatchetsHighestCryptoType () const override;
		void ScheduleSessionResponseTimer (
			std::shared_ptr<i2p::garlic::ECIESX25519AEADRatchetSession> session) override;
		// LeaseSetDestination
		void CleanupDestination () override;
		i2p::data::CryptoKeyType GetPreferredCryptoType () const override;
		// I2CP
		void HandleDataMessage (
			const uint8_t * buf, size_t len, i2p::garlic::ECIESX25519AEADRatchetSession * from) override;
		void CreateNewLeaseSet (const std::vector<std::shared_ptr<i2p::tunnel::InboundTunnel>> & tunnels) override;

	protected:
		std::shared_ptr<I2CPDestination> GetSharedFromThis ()
		{
			return std::static_pointer_cast<I2CPDestination> (shared_from_this ());
		}

	private:
		bool SendMsg (std::shared_ptr<I2NPMessage> msg, std::shared_ptr<const i2p::data::LeaseSet> remote);
		bool SendMsg (std::shared_ptr<I2NPMessage> garlic, std::shared_ptr<i2p::tunnel::OutboundTunnel> outboundTunnel,
			std::shared_ptr<const i2p::data::Lease> remoteLease);

		void PostCreateNewLeaseSet (std::vector<std::shared_ptr<i2p::tunnel::InboundTunnel>> tunnels);

	private:
		std::shared_ptr<I2CPSession> m_Owner;
		std::shared_ptr<const i2p::data::IdentityEx> m_Identity;
		std::map<i2p::data::CryptoKeyType, std::shared_ptr<i2p::crypto::LocalEncryptionKey>>
			m_EncryptionKeys; // last is most preferable
		uint64_t m_LeaseSetExpirationTime;
		bool m_IsCreatingLeaseSet, m_IsSameThread;
		boost::asio::steady_timer m_LeaseSetCreationTimer, m_ReadinessCheckTimer;
		i2p::util::MemoryPoolMt<I2NPMessageBuffer<I2NP_MAX_MESSAGE_SIZE>> m_I2NPMsgsPool;
	};

	class RunnableI2CPDestination : private i2p::util::RunnableService, public I2CPDestination
	{
	public:
		RunnableI2CPDestination (std::shared_ptr<I2CPSession> owner,
			std::shared_ptr<const i2p::data::IdentityEx> identity, bool isPublic, const i2p::util::Mapping & params);
		~RunnableI2CPDestination ();

		void Start ();
		void Stop ();
		auto & GetService () { return GetIOService (); }

	private:
		std::shared_ptr<RunnableI2CPDestination> GetRunnableSharedFromThis ();
		bool IsInServiceThread () const;

	private:
		mutable std::mutex m_ServiceThreadMutex;
		std::thread::id m_ServiceThreadID;
		std::atomic<bool> m_StopRequested { false };
		// Keeps the destination alive until its own service thread has been
		// joined. Without it the last reference could be released on the
		// service thread, whose destructor would then try to join itself.
		std::shared_ptr<RunnableI2CPDestination> m_Self;
	};

	class I2CPServer;
	class I2CPSession : public std::enable_shared_from_this<I2CPSession>
	{
	public:
		I2CPSession (I2CPServer & owner, std::shared_ptr<boost::asio::ip::tcp::socket> socket);

		~I2CPSession ();

		void Start ();
		void Stop ();
		uint16_t GetSessionID () const { return m_SessionID.load (); };
		// Only I2CPServer inserts a session, and only after the identifier has
		// been checked for collisions. The identifier is never assigned from
		// an unvalidated CREATE SESSION request.
		void AssignSessionID (uint16_t sessionID) { m_SessionID.store (sessionID); };
		std::shared_ptr<const I2CPDestination> GetDestination () const { return m_Destination; };

		// called from I2CPDestination
		void SendI2CPMessage (uint8_t type, const uint8_t * payload, size_t len);
		void SendMessagePayloadMessage (const uint8_t * payload, size_t len);
		void SendMessageStatusMessage (uint32_t nonce, I2CPMessageStatus status);
		void AddRoutingSession (
			const i2p::data::IdentHash & identHash, std::shared_ptr<i2p::garlic::GarlicRoutingSession> remoteSession);
		void CleanupRoutingSessions ();

		// message handlers
		void GetDateMessageHandler (const uint8_t * buf, size_t len);
		void CreateSessionMessageHandler (const uint8_t * buf, size_t len);
		void DestroySessionMessageHandler (const uint8_t * buf, size_t len);
		void ReconfigureSessionMessageHandler (const uint8_t * buf, size_t len);
		void CreateLeaseSetMessageHandler (const uint8_t * buf, size_t len);
		void CreateLeaseSet2MessageHandler (const uint8_t * buf, size_t len);
		void SendMessageMessageHandler (const uint8_t * buf, size_t len);
		void SendMessageExpiresMessageHandler (const uint8_t * buf, size_t len);
		void HostLookupMessageHandler (const uint8_t * buf, size_t len);
		void DestLookupMessageHandler (const uint8_t * buf, size_t len);
		void BlindingInfoMessageHandler (const uint8_t * buf, size_t len);
		void GetBandwidthLimitsMessageHandler (const uint8_t * buf, size_t len);

	private:
		void ReadProtocolByte ();
		void ReceiveHeader ();
		void HandleReceivedHeader (const boost::system::error_code & ecode, std::size_t bytes_transferred);
		void ReceivePayload ();
		void HandleReceivedPayload (const boost::system::error_code & ecode, std::size_t bytes_transferred);
		void HandleMessage ();
		// Terminate always runs on the I2CP server's service thread, which owns
		// the socket and the send queue. Callers on another thread only post it.
		void Terminate ();
		void TerminateInOwnerThread ();

		void QueueI2CPMessage (std::shared_ptr<i2p::stream::SendBuffer> && sendBuf, size_t len);
		void StartNextI2CPWrite (size_t len);
		void HandleI2CPMessageSent (const boost::system::error_code & ecode, std::size_t bytes_transferred);

		void SendSessionStatusMessage (I2CPSessionStatus status);
		void SendHostReplyMessage (uint32_t requestID, uint16_t sessionID,
			std::shared_ptr<const i2p::data::IdentityEx> identity, bool withOptions = false,
			std::shared_ptr<const i2p::data::LeaseSet> leaseSet = nullptr);

	private:
		I2CPServer & m_Owner;
		std::shared_ptr<boost::asio::ip::tcp::socket> m_Socket;
		uint8_t m_Header[I2CP_HEADER_SIZE], m_Payload[I2CP_MAX_MESSAGE_LENGTH];
		size_t m_PayloadLen;

		std::shared_ptr<I2CPDestination> m_Destination;
		std::mutex m_RoutingSessionsMutex;
		std::unordered_map<i2p::data::IdentHash, std::shared_ptr<i2p::garlic::GarlicRoutingSession>>
			m_RoutingSessions; // destination ident hash -> session
		std::atomic<uint16_t> m_SessionID;
		std::atomic<uint32_t> m_MessageID;
		std::atomic<bool> m_IsSendAccepted;
		std::atomic<bool> m_Terminated { false };

		// to client; guarded by m_SendMutex and only started from the service
		// thread. The queue holds complete buffers, so no caller-owned memory
		// outlives a post.
		std::mutex m_SendMutex;
		bool m_IsSending;
		uint8_t m_SendBuffer[I2CP_MAX_MESSAGE_LENGTH];
		i2p::stream::SendBufferQueue m_SendQueue;
	};
	typedef void (I2CPSession::*I2CPMessageHandler) (const uint8_t * buf, size_t len);

	class I2CPServer : private i2p::util::RunnableService
	{
	public:
		I2CPServer (const std::string & interface, uint16_t port, bool isSingleThread);
		~I2CPServer ();

		void Start ();
		void Stop ();
		auto & GetService () { return GetIOService (); };
		bool IsSingleThread () const { return m_IsSingleThread; };

		// Inserts with an explicit, already validated identifier. The session's
		// identifier is assigned only after the insertion succeeds.
		bool InsertSession (std::shared_ptr<I2CPSession> session, uint16_t sessionID);
		void RemoveSession (uint16_t sessionID);
		std::shared_ptr<I2CPSession> FindSessionByIdentHash (const i2p::data::IdentHash & ident) const;

		// The I2CP service runs on one worker thread. This is the local
		// equivalent of RunnableService::IsInServiceThread(), which is not part
		// of the public utility interface.
		bool IsInServiceThread () const
		{
			std::lock_guard<std::mutex> l (m_ServiceThreadMutex);
			return m_ServiceThreadID == std::this_thread::get_id ();
		}

	private:
		void Accept ();
		void StopSessions ();

		void HandleAccept (
			const boost::system::error_code & ecode, std::shared_ptr<boost::asio::ip::tcp::socket> socket);

	private:
		bool m_IsSingleThread;
		I2CPMessageHandler m_MessagesHandlers[256];
		mutable std::mutex m_SessionsMutex;
		std::map<uint16_t, std::shared_ptr<I2CPSession>> m_Sessions;
		mutable std::mutex m_ServiceThreadMutex;
		std::thread::id m_ServiceThreadID;

		boost::asio::ip::tcp::acceptor m_Acceptor;

	public:
		const decltype (m_MessagesHandlers) & GetMessagesHandlers () const { return m_MessagesHandlers; };

		// for HTTP; thread-safe snapshot, safe to iterate without holding any lock
		std::vector<std::pair<uint16_t, std::shared_ptr<I2CPSession>>> GetSessionsList () const
		{
			std::lock_guard<std::mutex> l (m_SessionsMutex);
			return std::vector<std::pair<uint16_t, std::shared_ptr<I2CPSession>>> (
				m_Sessions.begin (), m_Sessions.end ());
		}
		std::shared_ptr<I2CPSession> FindSessionByID (uint16_t sessionID) const
		{
			std::lock_guard<std::mutex> l (m_SessionsMutex);
			auto it = m_Sessions.find (sessionID);
			return it != m_Sessions.end () ? it->second : nullptr;
		}
	};
}
}

#endif
