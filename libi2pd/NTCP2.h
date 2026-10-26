/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: NTCP2.
 *
 * NTCP2 transport: Noise XK handshake with optional ML-KEM hybrid key exchange,
 * SessionRequest/Created/Confirmed processing, AEAD-ChaCha20-Poly1305 data framing, SipHash
 * obfuscation and padding policy. The server bounds handshake allocation, enforces replay
 * caches and per-address pending-session limits, and uses Java-compatible termination timers.
 *
 */

#ifndef NTCP2_H__
#define NTCP2_H__

#include <inttypes.h>
#include <memory>
#include <list>
#include <map>
#include <array>
#include <random>
#include <string>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <vector>
#include <boost/asio.hpp>
#include "Crypto.h"
#include "PostQuantum.h"
#include "util.h"
#include "RouterInfo.h"
#include "TransportSession.h"

namespace i2p
{
namespace transport
{

	constexpr size_t NTCP2_UNENCRYPTED_FRAME_MAX_SIZE = 65519;
	constexpr size_t NTCP2_SEND_AFTER_FRAME_SIZE = 16386; // send frame when exceeds this size
	constexpr size_t NTCP2_SESSION_HANDSHAKE_MAX_SIZE = 287; // SessionRequest/SessionCreated without ML-KEM frame
	constexpr size_t NTCP2_SESSION_HANDSHAKE_LONG_MAX_SIZE = 944; // for non-PQ
	// Java 2.14.0 InboundEstablishState: BUFFER_SIZE (4096) - MSG3P1_SIZE
	// (48) = 4048. MSG3P2_MIN is 459 (minimum RI block plus MAC/block
	// overhead). Keep both directions on the reference bounds.
	constexpr size_t NTCP2_SESSION_CONFIRMED_PART2_MIN_SIZE = 459;
	constexpr size_t NTCP2_SESSION_CONFIRMED_PART2_MAX_SIZE = 4048;
	const int NTCP2_MAX_PADDING_RATIO = 6; // in %

	// Bob must reject a SessionRequest whose encrypted ephemeral key was seen
	// before. Java uses a 10 minute decaying set (DecayingHashSet in
	// NTCPTransport); the spec requires a cache lifetime of at least 2*D.
	const int NTCP2_HANDSHAKE_REPLAY_TIMEOUT = 10 * 60; // in seconds
	const size_t NTCP2_MAX_HANDSHAKE_REPLAY_ENTRIES = 8192;
	// Bound the number of distinct source addresses that can occupy a pending
	// inbound handshake slot at once. One pending handshake per address is
	// already enforced by NTCP2Server::m_PendingIncomingSessions.
	const size_t NTCP2_MAX_PENDING_INCOMING_SESSIONS = 512;

	// spec/ntcp2-hybrid, table "Maximum Padding": the maximum cleartext padding
	// a peer may send in SessionRequest (message 1) and SessionCreated
	// (message 2). ML-KEM-512 uses the non-PQ limits; ML-KEM-768 and
	// ML-KEM-1024 have their own row. Only the SessionCreated column of
	// ML-KEM-512/768 is smaller than its SessionRequest column, and the
	// ML-KEM-1024 row is 1648 for both messages. A larger padLen must be
	// rejected (see NTCP2IsValidHandshakePadding below), so do not collapse the
	// two columns into one value: 1136/1616 for SessionCreated was wrong.
	constexpr size_t NTCP2GetMaxHandshakePadding (int version, bool sessionCreated)
	{
		switch (version)
		{
		case 3: return sessionCreated ? 848 : 880; // ML-KEM-512
		case 4: return sessionCreated ? 1168 : 1264; // ML-KEM-768
		case 5: return sessionCreated ? 1648 : 1648; // ML-KEM-1024
		default: return sessionCreated ? 848 : 880; // x25519 and unknown versions
		}
	}

	// ML-KEM-1024 SessionRequest/SessionCreated size before padding: encrypted
	// X/Y (32) + ML-KEM frame (1568 + 16) + encrypted options (32).
	constexpr size_t NTCP2_MLKEM1024_HANDSHAKE_BASE_SIZE = 1648;

	// The only accepted padLen rule for both handshake messages, so the table
	// above can be regression-tested without constructing a handshake.
	constexpr bool NTCP2IsValidHandshakePadding (int version, bool sessionCreated, size_t paddingLen)
	{
		return paddingLen <= NTCP2GetMaxHandshakePadding (version, sessionCreated);
	}

	// Outbound NTCP2 uses the version negotiated from the peer's RouterInfo
	// "v" option for the whole connection. An earlier build randomly forced
	// x25519 for one in four hybrid peers; that is removed because Java 2.14.0
	// never downgrades and a passive observer could otherwise expect/force the
	// weaker handshake half the time. Kept as a pure function so the policy is
	// pinned by a test without sockets.
	constexpr int NTCP2SelectOutboundVersion (int negotiatedVersion)
	{
		return negotiatedVersion;
	}

	const int NTCP2_CONNECT_TIMEOUT = 10; // in seconds, Java NTCPTransport.ESTABLISH_TIMEOUT
	const int NTCP2_ESTABLISH_TIMEOUT = 10; // 10 seconds
	// Java EventPumper.MIN_EXPIRE_IDLE_TIME is 120 s; closing earlier can race
	// the peer's keepalive/expiration decision.
	const int NTCP2_TERMINATION_TIMEOUT = 120; // in seconds
	const int NTCP2_TERMINATION_TIMEOUT_VARIANCE = 10; // 10 seconds
	const int NTCP2_TERMINATION_CHECK_TIMEOUT = 28; // 28 seconds
	const int NTCP2_TERMINATION_CHECK_TIMEOUT_VARIANCE = 5; // 5 seconds
	const int NTCP2_RECEIVE_BUFFER_DELETION_TIMEOUT = 3; // 3 seconds
	const int NTCP2_ROUTERINFO_RESEND_INTERVAL = 25 * 60; // 25 minutes in seconds
	const int NTCP2_ROUTERINFO_RESEND_INTERVAL_VARIANCE = 25 * 60; // 25 minutes

	const int NTCP2_CLOCK_SKEW = 60; // in seconds
	const int NTCP2_MAX_OUTGOING_QUEUE_SIZE = 500; // how many messages we can queue up

	// spec/ntcp2 "AEAD Error Handling" ("decryption oracle"): a data-phase tag
	// failure must not be answered immediately, because an immediate
	// Termination tells an attacker that the ciphertext was processed with the
	// session keys. Bob/Alice instead read and discard a random number of bytes
	// bounded by the largest possible frame and only then send Termination with
	// reason 4. The drain is uniform in [18, 1041] and costs one fixed-size
	// buffer, so it cannot be used to force allocation.
	constexpr size_t NTCP2_AEAD_FAILURE_DRAIN_MIN_SIZE = 18;
	constexpr size_t NTCP2_AEAD_FAILURE_DRAIN_MAX_SIZE = 1041; // 18 + 1023
	constexpr int NTCP2_AEAD_FAILURE_DRAIN_TIMEOUT = 10; // in seconds
	// An established session that stops in the middle of a frame is closed with
	// reason 14. The 2-byte obfuscated length read is not covered: an idle
	// session legitimately waits there, and the 120 s idle termination handles
	// a stalled peer that never completes it.
	constexpr int NTCP2_INTRA_FRAME_READ_TIMEOUT = 30; // in seconds

	// Uniform drain length for one data-phase AEAD failure, in bytes.
	constexpr size_t NTCP2GetAEADFailureDrainSize (uint32_t randomValue)
	{
		return NTCP2_AEAD_FAILURE_DRAIN_MIN_SIZE
			+ (randomValue % (NTCP2_AEAD_FAILURE_DRAIN_MAX_SIZE - NTCP2_AEAD_FAILURE_DRAIN_MIN_SIZE + 1));
	}

	// Pure model of the data-phase failure sequence. It encodes the ordering
	// rule that is otherwise buried in socket handlers: after an authentication
	// failure nothing may be written until the drain finished (completion or
	// deadline), and the drain itself is a single bounded step. NTCP2Session
	// owns one of these; tests exercise it without sockets.
	class NTCP2FailureDrain
	{
	public:
		// Stops a drain. Used by Terminate()/Close(), which close the socket
		// instead of sending a polite Termination; the dead session, not this
		// flag, blocks further writes there.
		constexpr void Reset ()
		{
			m_IsDraining = false;
			m_Remaining = 0;
		}

		// Starts a drain. Returns false for an out-of-range size or when a drain
		// is already in progress, so duplicate failures cannot restart it.
		constexpr bool Begin (size_t drainSize)
		{
			if (m_IsDraining || drainSize < NTCP2_AEAD_FAILURE_DRAIN_MIN_SIZE
				|| drainSize > NTCP2_AEAD_FAILURE_DRAIN_MAX_SIZE)
				return false;
			m_IsDraining = true;
			m_Remaining = drainSize;
			return true;
		}

		// Accounts received bytes during the drain; returns true once the drain
		// is satisfied and Termination may be sent.
		constexpr bool Consume (size_t received)
		{
			if (!m_IsDraining) return false;
			m_Remaining = received >= m_Remaining ? 0 : m_Remaining - received;
			if (!m_Remaining) m_IsDraining = false;
			return !m_IsDraining;
		}

		// The drain deadline expired. Termination is allowed afterwards, exactly
		// as for a completed drain.
		constexpr bool Expire ()
		{
			if (!m_IsDraining) return false;
			m_IsDraining = false;
			m_Remaining = 0;
			return true;
		}

		// Nothing may be written while the drain is still outstanding.
		constexpr bool CanWrite () const { return !m_IsDraining; }
		constexpr bool IsDraining () const { return m_IsDraining; }
		constexpr size_t GetRemaining () const { return m_Remaining; }

	private:
		bool m_IsDraining = false;
		size_t m_Remaining = 0;
	};

	// Java NTCP2Payload honors the RouterInfo request-flood bit only for a
	// RouterInfo block smaller than 3 KiB, so a large RI is stored but never
	// forwarded.
	constexpr size_t NTCP2_ROUTER_INFO_MAX_FLOOD_SIZE = 3 * 1024;

	enum NTCP2BlockType
	{
		eNTCP2BlkDateTime = 0,
		eNTCP2BlkOptions, // 1
		eNTCP2BlkRouterInfo, // 2
		eNTCP2BlkI2NPMessage, // 3
		eNTCP2BlkTermination, // 4
		eNTCP2BlkPadding = 254
	};

	enum NTCP2TerminationReason
	{
		eNTCP2NormalClose = 0,
		eNTCP2TerminationReceived, // 1
		eNTCP2IdleTimeout, // 2
		eNTCP2RouterShutdown, // 3
		eNTCP2DataPhaseAEADFailure, // 4
		eNTCP2IncompatibleOptions, // 5
		eNTCP2IncompatibleSignatureType, // 6
		eNTCP2ClockSkew, // 7
		eNTCP2PaddingViolation, // 8
		eNTCP2AEADFramingError, // 9
		eNTCP2PayloadFormatError, // 10
		eNTCP2Message1Error, // 11
		eNTCP2Message2Error, // 12
		eNTCP2Message3Error, // 13
		eNTCP2IntraFrameReadTimeout, // 14
		eNTCP2RouterInfoSignatureVerificationFail, // 15
		eNTCP2IncorrectSParameter, // 16
		eNTCP2Banned, // 17
	};

	// RouterInfo flags
	const uint8_t NTCP2_ROUTER_INFO_FLAG_REQUEST_FLOOD = 0x01;

	// Validates the decrypted payload of SessionConfirmed part 2 (message 3
	// part 2, with the 16-byte MAC removed). The only permitted layout is:
	// RouterInfo block, optional Options block, optional Padding block last
	// (spec/ntcp2 "Block Ordering Rules"). No other block type is allowed in
	// the handshake. Returns false on any malformed or out-of-order block and
	// stores the RouterInfo block data size (flag + RouterInfo) on success.
	bool ValidateNTCP2SessionConfirmedPayload (const uint8_t * payload, size_t len, size_t & routerInfoDataSize);

	struct NTCP2Establisher : private i2p::crypto::NoiseSymmetricState
	{
		NTCP2Establisher ();

		void SetVersion (int version);

		// A null or zeroed ephemeral pair means the provider failed to generate
		// a key. Every handshake message that embeds X or Y must then abort
		// instead of sending an all-zero public key.
		bool HasUsableEphemeralKey () const
		{
			if (!m_EphemeralKeys || !m_EphemeralKeys->HasPrivateKey ()) return false;
			const uint8_t * pub = m_EphemeralKeys->GetPublicKey ();
			for (size_t i = 0; i < 32; ++i)
				if (pub[i]) return true;
			return false;
		}

		const uint8_t * GetPub () const { return m_EphemeralKeys->GetPublicKey (); };
		const uint8_t * GetRemotePub () const { return m_RemoteEphemeralPublicKey; }; // Y for Alice
		uint8_t * GetRemotePub () { return m_RemoteEphemeralPublicKey; }; // to set or X for Bob

		const uint8_t * GetCK () const { return m_CK; };
		const uint8_t * GetH () const { return m_H; };
		int GetVersion () const { return (int)m_CryptoType - (int)i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD + 2; }

		bool KDF1Alice ();
		bool KDF1Bob ();
		bool KDF2Alice ();
		bool KDF2Bob ();
		bool KDF3Alice (); // for SessionConfirmed part 2
		bool KDF3Bob ();

		bool KeyDerivationFunction1 (const uint8_t * pub, i2p::crypto::X25519Keys & priv, const uint8_t * rs,
			const uint8_t * epub); // for SessionRequest, (pub, priv) for DH
		bool KeyDerivationFunction2 (const uint8_t * epub); // for SessionCreate
		void CreateEphemeralKey ();
		void ApplyPadding (uint8_t * padding, size_t paddingLength);

		bool CreateSessionRequestMessage (std::mt19937 & rng);
		bool CreateSessionCreatedMessage (std::mt19937 & rng);
		bool CreateSessionConfirmedMessagePart1 ();
		bool CreateSessionConfirmedMessagePart2 ();

		bool ProcessSessionRequestMessage (
			uint16_t & paddingLen, bool & clockSkew, bool & netIdMismatch, bool & pq, bool decryptX = true);
		bool ProcessSessionCreatedMessage (uint16_t & paddingLen);
		bool ProcessSessionConfirmedMessagePart1 ();
		bool ProcessSessionConfirmedMessagePart2 (uint8_t * m3p2Buf);

		std::shared_ptr<i2p::crypto::X25519Keys> m_EphemeralKeys;
		i2p::data::CryptoKeyType m_CryptoType;
		uint8_t m_RemoteEphemeralPublicKey[32]; // x25519
		uint8_t m_RemoteStaticKey[32], m_IV[16];
		i2p::data::IdentHash m_RemoteIdentHash;
		uint16_t m3p2Len;

#if OPENSSL_PQ
		std::unique_ptr<i2p::crypto::MLKEMKeys> m_PQKeys;
		static constexpr size_t m_MaxMsgSize = 2 * i2p::crypto::MLKEM1024_KEY_LENGTH + 160;
		// spec/ntcp2-hybrid: the largest handshake frame is ML-KEM-1024
		// SessionRequest/SessionCreated (1648 bytes of base data) with the v5
		// padding limit (1648 bytes). Both must fit the shared buffer exactly;
		// reducing it or "fixing" the padding table to 1136/1616 breaks this.
		static_assert (NTCP2_MLKEM1024_HANDSHAKE_BASE_SIZE + NTCP2GetMaxHandshakePadding (5, false) <= m_MaxMsgSize,
			"NTCP2 handshake buffer must hold the v5 SessionRequest maximum");
		static_assert (NTCP2_MLKEM1024_HANDSHAKE_BASE_SIZE + NTCP2GetMaxHandshakePadding (5, true) <= m_MaxMsgSize,
			"NTCP2 handshake buffer must hold the v5 SessionCreated maximum");
		static_assert (m_MaxMsgSize == NTCP2_MLKEM1024_HANDSHAKE_BASE_SIZE + NTCP2GetMaxHandshakePadding (5, true),
			"the ML-KEM-1024 buffer must be exactly one maximum v5 handshake frame");
#else
		static constexpr size_t m_MaxMsgSize = NTCP2_SESSION_HANDSHAKE_LONG_MAX_SIZE;
#endif
		uint8_t m_Buffer[m_MaxMsgSize]; // for SessionRequest and SessionCreated
		// SessionConfirmed, m3p1 (48 bytes) followed by the encrypted m3p2
		std::unique_ptr<uint8_t[]> m_SessionConfirmedBuffer;
		size_t m_BufferLen;
		bool m_IsLongPadding;
	};

	class NTCP2Server;
	class NTCP2Session : public TransportSession, public std::enable_shared_from_this<NTCP2Session>
	{
	public:
		NTCP2Session (NTCP2Server & server, std::shared_ptr<const i2p::data::RouterInfo> in_RemoteRouter = nullptr,
			std::shared_ptr<const i2p::data::RouterInfo::Address> addr = nullptr);
		~NTCP2Session ();
		void Terminate ();
		void TerminateByTimeout ();
		void Done () override;
		void Close (); // for accept
		void DeleteNextReceiveBuffer (uint64_t ts);

		boost::asio::ip::tcp::socket & GetSocket () { return m_Socket; };
		const boost::asio::ip::tcp::endpoint & GetRemoteEndpoint () { return m_RemoteEndpoint; };
		void SetRemoteEndpoint (const boost::asio::ip::tcp::endpoint & ep) { m_RemoteEndpoint = ep; };

		bool IsEstablished () const override { return m_IsEstablished; };
		i2p::data::RouterInfo::SupportedTransports GetTransportType () const override;
		boost::asio::ip::address GetRemoteAddress () const override { return m_RemoteEndpoint.address (); };
		bool IsTerminated () const { return m_IsTerminated; };

		void ClientLogin (); // Alice
		void ServerLogin (int version); // Bob

		void SendLocalRouterInfo (bool update) override; // after handshake or by update
		void SendI2NPMessages (std::list<std::shared_ptr<I2NPMessage>> & msgs) override;
		void MoveSendQueue (std::shared_ptr<NTCP2Session> other);

	private:
		void Established ();

		void CreateNonce (uint64_t seqn, uint8_t * nonce);
		void CreateNextReceivedBuffer (size_t size);
		void KeyDerivationFunctionDataPhase ();
		bool SetSipKeys (const uint8_t * sendSipKey, const uint8_t * receiveSipKey);

		// establish
		void SendSessionRequest ();
		void SendSessionCreated ();
		void SendSessionConfirmed ();

		void HandleSessionRequestSent (const boost::system::error_code & ecode, std::size_t bytes_transferred);
		void HandleSessionRequestReceived (const boost::system::error_code & ecode, std::size_t bytes_transferred);
		void ProcessSessionRequest (size_t len, bool first = true);
		void HandleSessionRequestPaddingReceived (
			const boost::system::error_code & ecode, std::size_t bytes_transferred);
#if OPENSSL_PQ
		void HandleSessionRequestMLKEMReceived (const boost::system::error_code & ecode, std::size_t bytes_transferred);
#endif
		void HandleSessionCreatedSent (const boost::system::error_code & ecode, std::size_t bytes_transferred);
		void HandleSessionCreatedReceived (const boost::system::error_code & ecode, std::size_t bytes_transferred);
		void ProcessSessionCreated (size_t len);
		void HandleSessionCreatedPaddingReceived (
			const boost::system::error_code & ecode, std::size_t bytes_transferred);
		void HandleSessionConfirmedSent (const boost::system::error_code & ecode, std::size_t bytes_transferred);
		void HandleSessionConfirmedReceived (const boost::system::error_code & ecode, std::size_t bytes_transferred);
		void ProcessSessionConfirmed ();
		void EstablishSessionAfterSessionConfirmed (std::shared_ptr<std::vector<uint8_t>> buf, size_t size);

		// data
		void ReceiveLength ();
		void HandleReceivedLength (const boost::system::error_code & ecode, std::size_t bytes_transferred);
		void Receive ();
		void HandleReceived (const boost::system::error_code & ecode, std::size_t bytes_transferred);
		void ProcessNextFrame (const uint8_t * frame, size_t len);

		// data-phase failure handling (spec/ntcp2 "AEAD Error Handling")
		void ScheduleFrameReadDeadline ();
		void HandleReadDeadlineTimer (const boost::system::error_code & ecode);
		void EnterFailureDrain (); // never writes before the drain finishes
		void HandleFailureDrainReceived (const boost::system::error_code & ecode, std::size_t bytes_transferred);
		void FinishFailureDrain (); // sends reason 4 and closes after the write

		void SetNextSentFrameLength (size_t frameLen, uint8_t * lengthBuf);
		void SendI2NPMsgs (std::vector<std::shared_ptr<I2NPMessage>> & msgs);
		void HandleI2NPMsgsSent (const boost::system::error_code & ecode, std::size_t bytes_transferred,
			std::vector<std::shared_ptr<I2NPMessage>> msgs, std::unique_ptr<uint8_t[]> sendBuffer);
		bool EncryptAndSendNextBuffer (size_t payloadLen);
		void HandleNextFrameSent (const boost::system::error_code & ecode, std::size_t bytes_transferred,
			std::unique_ptr<uint8_t[]> sendBuffer);
		size_t CreatePaddingBlock (size_t msgLen, uint8_t * buf, size_t len);
		void SendQueue ();
		void SendRouterInfo ();
		bool SendTermination (NTCP2TerminationReason reason);
		void SendTerminationAndTerminate (NTCP2TerminationReason reason);
		void ReadSomethingAndTerminate ();
		void PostI2NPMessages ();
		bool HasPendingIncomingData ();

	private:
		NTCP2Server & m_Server;
		boost::asio::ip::tcp::socket m_Socket;
		// One timer for the data phase: the per-frame read deadline and, after a
		// failure, the bounded drain deadline. Both are cancelled on close/Stop.
		boost::asio::steady_timer m_ReadTimer;
		boost::asio::ip::tcp::endpoint m_RemoteEndpoint;
		std::atomic<bool> m_IsEstablished, m_IsTerminated;
		// SessionRequest carried a tsA outside ±NTCP2_CLOCK_SKEW. Bob still sends
		// SessionCreated so Alice can see our time, but terminates with reason 7
		// once SessionConfirmed arrived and the message 2 write has completed.
		std::atomic<bool> m_IsClockSkew;
		NTCP2FailureDrain m_FailureDrain;
		// Fixed drain buffer: the drain size is bounded by
		// NTCP2_AEAD_FAILURE_DRAIN_MAX_SIZE, so no allocation depends on input.
		uint8_t m_FailureDrainBuffer[NTCP2_AEAD_FAILURE_DRAIN_MAX_SIZE];

		std::unique_ptr<NTCP2Establisher> m_Establisher;
		// data phase
		uint8_t m_Kab[32], m_Kba[32], m_Sipkeysab[32], m_Sipkeysba[32];
		const uint8_t *m_SendKey, *m_ReceiveKey;
#if OPENSSL_SIPHASH
		EVP_MD_CTX *m_SendMDCtx, *m_ReceiveMDCtx;
#else
		const uint8_t *m_SendSipKey, *m_ReceiveSipKey;
#endif
		uint16_t m_NextReceivedLen;
		std::unique_ptr<uint8_t[]> m_NextReceivedBuffer;
		// staging buffer for the next outgoing frame; ownership is moved into the
		// write completion handler before async_write
		std::unique_ptr<uint8_t[]> m_NextSendBuffer;
		size_t m_NextReceivedBufferSize;
		union
		{
			uint8_t buf[8];
			uint16_t key;
		} m_ReceiveIV, m_SendIV;
		uint64_t m_ReceiveSequenceNumber, m_SendSequenceNumber;

		i2p::I2NPMessagesHandler m_Handler;

		bool m_IsSending, m_IsReceiving;
		// A control frame (RouterInfo/termination) must not be started while a
		// data frame write is in flight. Remember the request and act on it in
		// HandleNextFrameSent() instead of dropping it.
		bool m_IsTerminating;
		int m_PendingTerminationReason; // -1 when none
		bool m_PendingRouterInfo;
		std::list<std::shared_ptr<I2NPMessage>> m_SendQueue;
		uint64_t m_NextRouterInfoResendTime; // seconds since epoch

		std::list<std::shared_ptr<I2NPMessage>> m_IntermediateQueue; // from transports
		mutable std::mutex m_IntermediateQueueMutex;

		uint16_t m_PaddingSizes[16];
		int m_NextPaddingSize;
	};

	class NTCP2Server : private i2p::util::RunnableServiceWithWork
	{
	private:
		class EstablisherService : public i2p::util::RunnableServiceWithWork
		{
		public:
			EstablisherService (uint32_t seed)
				: RunnableServiceWithWork ("NTCP2e")
				, m_Rng (seed) { };
			auto & GetService () { return GetIOService (); };
			std::mt19937 & GetRng () { return m_Rng; };
			void Start () { StartIOService (); };
			void Stop () { StopIOService (); };

		private:
			std::mt19937 m_Rng;
		};

	public:
		enum ProxyType
		{
			eNoProxy,
			eSocksProxy,
			eHTTPProxy
		};

		NTCP2Server ();
		~NTCP2Server ();

		void Start ();
		void Stop ();
		auto & GetService () { return GetIOService (); };
		auto & GetEstablisherService () { return m_EstablisherService.GetService (); };
		std::mt19937 & GetRng () { return m_Rng; };
		std::mt19937 & GetEstablisherRng () { return m_EstablisherService.GetRng (); };
		bool AEADChaCha20Poly1305Encrypt (const std::vector<std::pair<uint8_t *, size_t>> & bufs, const uint8_t * key,
			const uint8_t * nonce, uint8_t * mac);
		bool AEADChaCha20Poly1305Decrypt (const uint8_t * msg, size_t msgLen, const uint8_t * ad, size_t adLen,
			const uint8_t * key, const uint8_t * nonce, uint8_t * buf, size_t len);

		bool AddNTCP2Session (std::shared_ptr<NTCP2Session> session, bool incoming = false);
		void RemoveNTCP2Session (std::shared_ptr<NTCP2Session> session);
		std::shared_ptr<NTCP2Session> FindNTCP2Session (const i2p::data::IdentHash & ident);
		// Returns true if the encrypted X value (first 8 bytes, as in Java)
		// was already seen within NTCP2_HANDSHAKE_REPLAY_TIMEOUT. Inserts the
		// value atomically so concurrent handshakes cannot both pass.
		bool IsHandshakeReplay (const uint8_t * encryptedX);
		// Alice-side equivalent for the encrypted Y in SessionCreated
		// (spec/ntcp2 message 2 replay note). Kept in a separate cache so an
		// X prefix can never alias a Y prefix.
		bool IsHandshakeReplayOutgoing (const uint8_t * encryptedY);

		void ConnectWithProxy (std::shared_ptr<NTCP2Session> conn);
		void Connect (std::shared_ptr<NTCP2Session> conn);

		bool UsingProxy () const { return m_ProxyType != eNoProxy; };
		void UseProxy (ProxyType proxy, const std::string & address, uint16_t port, const std::string & user,
			const std::string & pass);

		void SetLocalAddress (const boost::asio::ip::address & localAddress);
		void SetVersion (int version);
		int GetVersion () const { return m_Version; }

	private:
		void HandleAccept (std::shared_ptr<NTCP2Session> conn, const boost::system::error_code & error);
		void HandleAcceptV6 (std::shared_ptr<NTCP2Session> conn, const boost::system::error_code & error);

		void HandleConnect (const boost::system::error_code & ecode, std::shared_ptr<NTCP2Session> conn,
			std::shared_ptr<boost::asio::steady_timer> timer);
		void HandleProxyConnect (const boost::system::error_code & ecode, std::shared_ptr<NTCP2Session> conn,
			std::shared_ptr<boost::asio::steady_timer> timer);
		bool IsHandshakeReplay (const uint8_t * encrypted, std::unordered_map<uint64_t, uint64_t> & cache);

		// timer
		void ScheduleTermination ();
		void HandleTerminationTimer (const boost::system::error_code & ecode);

	private:
		boost::asio::steady_timer m_TerminationTimer;
		std::unique_ptr<boost::asio::ip::tcp::acceptor> m_NTCP2Acceptor, m_NTCP2V6Acceptor;
		std::map<i2p::data::IdentHash, std::shared_ptr<NTCP2Session>> m_NTCP2Sessions;
		std::map<boost::asio::ip::address, std::shared_ptr<NTCP2Session>> m_PendingIncomingSessions;

		std::mutex m_HandshakeReplayMutex;
		std::unordered_map<uint64_t, uint64_t> m_HandshakeReplayCache; // incoming X -> expiry (seconds)
		std::unordered_map<uint64_t, uint64_t> m_HandshakeReplayCacheOutgoing; // outgoing Y -> expiry

		ProxyType m_ProxyType;
		std::string m_ProxyAddress, m_ProxyAuthorization;
		uint16_t m_ProxyPort;
		boost::asio::ip::tcp::resolver m_Resolver;
		std::unique_ptr<boost::asio::ip::tcp::endpoint> m_ProxyEndpoint;

		std::shared_ptr<boost::asio::ip::tcp::endpoint> m_Address4, m_Address6, m_YggdrasilAddress;
		std::mt19937 m_Rng;
		EstablisherService m_EstablisherService;
		i2p::crypto::AEADChaCha20Poly1305Encryptor m_Encryptor;
		i2p::crypto::AEADChaCha20Poly1305Decryptor m_Decryptor;

		int m_Version;
		std::atomic<bool> m_IsReady;

	public:
		// for HTTP/I2PControl
		const decltype (m_NTCP2Sessions) & GetNTCP2Sessions () const { return m_NTCP2Sessions; };
	};
}
}

#endif
