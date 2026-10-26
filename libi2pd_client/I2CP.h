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

	// Read deadlines for the control plane. The handshake deadline covers the
	// protocol byte and the first message; the body deadline covers a declared
	// payload and a partially received header. An established connection that
	// sends nothing is idle (clients keep the socket open between messages), so
	// waiting for the first header byte of a later message is not bounded here;
	// the connection and session caps bound how many such sockets exist.
	const int I2CP_HANDSHAKE_TIMEOUT = 30; // in seconds
	const int I2CP_BODY_TIMEOUT = 30; // in seconds

	// Admission bounds for the I2CP control plane. The reference router admits
	// 50 concurrent I2CP client connections; a configured value is clamped to a
	// hard ceiling so a hostile or mistaken configuration cannot exhaust the
	// process' descriptor and session budget.
	const size_t I2CP_MAX_CONNECTIONS_DEFAULT = 50;
	const size_t I2CP_MAX_CONNECTIONS_LIMIT = 2048;
	const size_t I2CP_MAX_SESSIONS_DEFAULT = 512;
	const size_t I2CP_MAX_SESSIONS_LIMIT = 4096;

	// Pure, socket-free admission policy. A configuration value of 0 means
	// "use the default"; anything above the hard limit is clamped. Pinned by
	// the I2CP tests so the counters and the policy cannot drift apart.
	struct I2CPAdmissionPolicy
	{
		size_t maxConnections = I2CP_MAX_CONNECTIONS_DEFAULT;
		size_t maxSessions = I2CP_MAX_SESSIONS_DEFAULT;

		bool AcceptConnection (size_t currentConnections) const { return currentConnections < maxConnections; }
		bool AcceptSession (size_t currentSessions) const { return currentSessions < maxSessions; }
	};
	I2CPAdmissionPolicy MakeI2CPAdmissionPolicy (size_t configuredConnections, size_t configuredSessions);

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
		// Java MessageStatusMessage codes (MessageStatusMessage.java).
		eI2CPMessageStatusAvailable = 0,
		eI2CPMessageStatusAccepted = 1,
		eI2CPMessageStatusBestEffortSuccess = 2,
		eI2CPMessageStatusBestEffortFailure = 3,
		eI2CPMessageStatusGuaranteedSuccess = 4,
		eI2CPMessageStatusGuaranteedFailure = 5,
		eI2CPMessageStatusLocalSuccess = 6,
		eI2CPMessageStatusLocalFailure = 7,
		eI2CPMessageStatusRouterFailure = 8,
		eI2CPMessageStatusNetworkFailure = 9,
		eI2CPMessageStatusBadSession = 10,
		eI2CPMessageStatusBadMessage = 11,
		eI2CPMessageStatusBadPayload = 12,
		eI2CPMessageStatusSendFailureExpired = 14,
		eI2CPMessageStatusNoLocalTunnels = 16,
		eI2CPMessageStatusNoLeaseSet = 21,
		eI2CPMessageStatusNoTunnels = 22
	};

	// HostReplyMessage result codes (HostReplyMessage.java, proposal 167).
	// 2-4 tell the client which client-auth material the blinded destination
	// needs before the encrypted LeaseSet2 can even be decrypted.
	enum I2CPHostReplyResult : uint8_t
	{
		eI2CPHostReplySuccess = 0,
		eI2CPHostReplyFailure = 1,
		eI2CPHostReplySecretRequired = 2,
		eI2CPHostReplyKeyRequired = 3,
		eI2CPHostReplySecretAndKeyRequired = 4,
		eI2CPHostReplyDecryptionFailure = 5,
		eI2CPHostReplyLeaseSetLookupFailure = 6
	};

	// Maps the client-auth scheme of a stored BlindingInfo record onto the
	// HostReply result the requesting session receives. authType follows
	// BlindingInfoMessage: 0 none, 1 DH (private key), 2 PSK (secret),
	// 3 DH+PSK. A usable secret is the 32 bytes LeaseSet2 client auth consumes.
	uint8_t GetI2CPHostReplyResultForAuth (uint8_t authType, bool hasUsableSecret);

	// Builds a complete HOST REPLY payload (without the I2CP header). A result
	// other than success never carries an identity, and an identity without a
	// result reports the generic failure: both forms are the 7-byte failure
	// layout. The success layout is the identity followed by the optional
	// options mapping, byte-identical to the pre-existing encoder.
	std::vector<uint8_t> BuildI2CPHostReplyPayload (uint32_t requestID, uint16_t sessionID,
		std::shared_ptr<const i2p::data::IdentityEx> identity, const std::vector<uint8_t> & options = { },
		uint8_t result = eI2CPHostReplySuccess);

	// Decoded SEND MESSAGE / SEND MESSAGE EXPIRES request. `flags` and
	// `expiration` only exist in the expires form; the expiration is a
	// 48-bit milliseconds-since-epoch value, 0 meaning "does not expire".
	struct I2CPSendMessageRequest
	{
		uint16_t sessionID = 0xFFFF;
		bool hasNonce = false;
		uint32_t nonce = 0;
		const uint8_t * identity = nullptr;
		size_t identityLen = 0;
		const uint8_t * payload = nullptr;
		size_t payloadLen = 0;
		uint16_t flags = 0;
		uint64_t expiration = 0;
	};

	// Pure wire parse. Returns false for a malformed request; sessionID is set
	// whenever the buffer held it and `hasNonce` records whether the trailing
	// nonce could still be located, so the caller can answer with a status
	// instead of dropping the message silently.
	bool ParseI2CPSendMessage (const uint8_t * buf, size_t len, bool withExpiration, I2CPSendMessageRequest & request);

	enum class I2CPSendMessageCheck : uint8_t
	{
		Ok = 0,
		BadSession, // no session object for this connection / foreign session id
		BadMessage, // malformed identity, payload or nonce
		Expired // SEND MESSAGE EXPIRES carried an expiration in the past
	};
	I2CPSendMessageCheck CheckI2CPSendMessage (
		const I2CPSendMessageRequest & request, uint16_t ownSessionID, bool hasDestination, uint64_t nowMilliseconds);

	// BlindingInfo (message type 42) endpoint and client-auth values.
	enum I2CPBlindingEndpointType : uint8_t
	{
		eI2CPBlindingEndpointHash = 0,
		eI2CPBlindingEndpointHost = 1,
		eI2CPBlindingEndpointDestination = 2,
		eI2CPBlindingEndpointKey = 3
	};
	enum I2CPBlindingAuthType : uint8_t
	{
		eI2CPBlindingAuthNone = 0,
		eI2CPBlindingAuthDH = 1,
		eI2CPBlindingAuthPSK = 2,
		eI2CPBlindingAuthDHAndPSK = 3
	};

	struct I2CPBlindingInfo
	{
		uint8_t endpointType = eI2CPBlindingEndpointDestination;
		std::shared_ptr<const i2p::data::IdentityEx> identity; // destination endpoint only
		i2p::data::SigningKeyType blindType = i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519;
		i2p::data::SigningKeyType signingKeyType = i2p::data::SIGNING_KEY_TYPE_DSA_SHA1;
		// Unblinded signing public key: the registry key. Empty for hash/host
		// endpoints, which carry no signing key.
		std::vector<uint8_t> signingKey;
		uint8_t authType = eI2CPBlindingAuthNone;
		std::vector<uint8_t> authPrivateKey; // DH / DH+PSK: 32 bytes
		std::string secret; // PSK / DH+PSK: the per-client secret bytes
		uint64_t expiration = 0; // milliseconds since epoch, 0 = never
		std::string host; // host endpoint only
	};

	enum I2CPBlindingInfoResult : uint8_t
	{
		eI2CPBlindingInfoOk = 0,
		eI2CPBlindingInfoMalformed = 1,
		eI2CPBlindingInfoUnsupported = 2
	};

	// Pure wire parse of a BLINDING INFO payload. `error` receives a short,
	// non-secret diagnostic; key and secret bytes are never logged or echoed.
	I2CPBlindingInfoResult ParseI2CPBlindingInfoMessage (
		const uint8_t * buf, size_t len, I2CPBlindingInfo & info, std::string & error);

	// Bounded, mutex-guarded registry. Records are keyed by the unblinded
	// signing key (ClientMessageEventListener.handleBlindingInfo keeps the same
	// association in the router's netDb) and expire at the announced time.
	// Every read prunes expired records.
	void StoreI2CPBlindingInfo (I2CPBlindingInfo info);
	bool FindI2CPBlindingInfoBySigningKey (
		i2p::data::SigningKeyType keyType, const uint8_t * key, size_t keyLen, I2CPBlindingInfo & info);
	// Blinding is one-way and the b33 address never carries the unblinded key,
	// so a b33 lookup matches a stored destination identity by re-deriving the
	// current day's blinded key. The scan is bounded by the registry size.
	bool FindI2CPBlindingInfoByBlindedKey (const i2p::data::BlindedPublicKey & blinded, I2CPBlindingInfo & info);
	// The 32-byte value LeaseSet2 client auth consumes: the DH private key for
	// auth types 1/3, otherwise the PSK secret for types 2/3. Empty when the
	// record carries no usable material.
	std::vector<uint8_t> GetI2CPBlindingAuthSecret (const I2CPBlindingInfo & info);
	size_t GetI2CPBlindingInfoCount ();
	void ClearI2CPBlindingInfos (); // tests and shutdown

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

		void SetEncryptionPrivateKey (i2p::data::CryptoKeyType keyType, const uint8_t * key, size_t keyLen = 0);
		void SetECIESx25519EncryptionPrivateKey (
			i2p::data::CryptoKeyType keyType, const uint8_t * key, size_t keyLen = 0);
		// Derives the public key of a staged private key without installing it
		// and reports whether the LeaseSet2 advertises exactly that key. Types
		// whose public key cannot be re-derived from the stored scalar (ElGamal,
		// P-256) return "not verifiable" so the caller can keep the install-time
		// check for them instead of rejecting a valid set.
		bool IsAdvertisedEncryptionKey (i2p::data::CryptoKeyType keyType, const uint8_t * key, size_t keyLen,
			const i2p::data::LeaseSet2 & ls, bool & verifiable) const;
		void LeaseSetCreated (const uint8_t * buf, size_t len); // called from I2CPSession
		void LeaseSet2Created (uint8_t storeType, const uint8_t * buf, size_t len); // called from I2CPSession
		void SendMsgTo (const uint8_t * payload, size_t len, const i2p::data::IdentHash & ident,
			uint32_t nonce); // called from I2CPSession
		bool SendMsg (
			const uint8_t * payload, size_t len, std::shared_ptr<i2p::garlic::GarlicRoutingSession> remoteSession);

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
		// First selectable type of i2cp.leaseSetEncType (Java publication order).
		i2p::data::CryptoKeyType m_ConfiguredPreferredCryptoType = 0;
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
		I2CPSession (
			I2CPServer & owner, std::shared_ptr<boost::asio::ip::tcp::socket> socket, bool connectionAdmitted = false);

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
		// `sessionID` defaults to this session's identifier; a BAD_SESSION status
		// echoes the identifier the client used instead.
		void SendMessageStatusMessage (uint32_t nonce, I2CPMessageStatus status, uint16_t sessionID = 0xFFFF);
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
		void HandleReceivedHeader ();
		void ReceivePayload ();
		void HandleReceivedPayload (const boost::system::error_code & ecode, std::size_t bytes_transferred);
		void HandleMessage ();
		// Cancellable read deadline. Every arm replaces the previous one; the
		// completion paths cancel it so an idle connection is never killed by a
		// stale timer.
		void ArmReadTimeout (int seconds);
		void CancelReadTimeout ();
		void HandleReadTimeout (const boost::system::error_code & ecode);
		// Terminate always runs on the I2CP server's service thread, which owns
		// the socket and the send queue. Callers on another thread only post it.
		void Terminate ();
		void TerminateInOwnerThread ();

		void QueueI2CPMessage (std::shared_ptr<i2p::stream::SendBuffer> && sendBuf, size_t len);
		void StartNextI2CPWrite (size_t len);
		void HandleI2CPMessageSent (const boost::system::error_code & ecode, std::size_t bytes_transferred);

		void SendSessionStatusMessage (I2CPSessionStatus status);
		// Shared SEND MESSAGE / SEND MESSAGE EXPIRES path.
		void SendMessageRequest (const uint8_t * buf, size_t len, bool withExpiration);
		void SendHostReplyMessage (uint32_t requestID, uint16_t sessionID,
			std::shared_ptr<const i2p::data::IdentityEx> identity, bool withOptions = false,
			std::shared_ptr<const i2p::data::LeaseSet> leaseSet = nullptr, uint8_t result = eI2CPHostReplySuccess);

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

		// Read-deadline and admission state. Declared last so the initialiser
		// order in the constructor follows the declaration order.
		// Bytes of the fixed-size header already received. The header is read
		// incrementally so a client that stalls mid-header is bounded by the
		// read deadline instead of holding a blocking read.
		size_t m_HeaderReceived = 0;
		// The first fully handled message ends the handshake; afterwards an
		// idle connection is legal and header waits are not timed out.
		bool m_HandshakeComplete = false;
		boost::asio::steady_timer m_ReadTimer;
		std::atomic<bool> m_ConnectionReleased { false };
		// True only when the server charged this session against the
		// concurrent-connection budget (HandleAccept). A session built directly
		// by a test or another owner never releases an uncharged slot.
		bool m_ConnectionAdmitted = false;
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

		// Concurrent-connection admission. The counter is incremented once per
		// accepted socket and released exactly once, from the session's
		// owner-thread teardown, so a refused or failed connection cannot leak a
		// slot.
		bool TryAcquireConnection ();
		void ReleaseConnection ();
		size_t GetNumConnections () const { return m_NumConnections.load (); };
		const I2CPAdmissionPolicy & GetAdmissionPolicy () const { return m_AdmissionPolicy; };

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
		I2CPAdmissionPolicy m_AdmissionPolicy;
		std::atomic<size_t> m_NumConnections { 0 };

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
