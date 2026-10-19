/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: SSU2Session.
 *
 * Declares SSU2Session and its handshake state machine, packet and block codecs,
 * ACK/retransmission state, fragment reassembly, relay handling and peer-test
 * hooks. Protocol limits mirror the SSU2 specification and Java router
 * behaviour (RTO, windows, fragment counts and resend budgets).
 *
 */

#ifndef SSU2_SESSION_H__
#define SSU2_SESSION_H__

#include <memory>
#include <functional>
#include <atomic>
#include <mutex>
#include <utility>
#include <cstddef>
#include <vector>
#include <map>
#include <set>
#include <list>
#include <boost/asio.hpp>
#include "version.h"
#include "Crypto.h"
#include "I2PEndian.h"
#include "PostQuantum.h"
#include "RouterInfo.h"
#include "RouterContext.h"
#include "TransportSession.h"

namespace i2p
{
namespace transport
{
	// Overall outbound establishment budget. Java uses MAX_OB_ESTABLISH_TIME
	// (EstablishmentManager.java:153) and per-message OB_MESSAGE_TIMEOUT = 15 s.
	const int SSU2_CONNECT_TIMEOUT = 25; // in seconds
	const int SSU2_TERMINATION_TIMEOUT = 165; // in seconds, Java UDPTransport.MIN_EXPIRE_TIMEOUT
	const int SSU2_CLOCK_SKEW = 60; // in seconds
	const int SSU2_CLOCK_THRESHOLD = 15; // in seconds, if more we should adjust
	const int SSU2_TOKEN_EXPIRATION_TIMEOUT = 9; // for Retry message, in seconds
	const int SSU2_NEXT_TOKEN_EXPIRATION_TIMEOUT = 52 * 60; // for next token block, in seconds
	const int SSU2_TOKEN_EXPIRATION_THRESHOLD = 2; // in seconds
	const int SSU2_RELAY_NONCE_EXPIRATION_TIMEOUT = 10; // in seconds
	// IntroductionManager.MAX_SKEW = 2*60*1000 in Java 2.13.0
	const int SSU2_RELAY_MAX_SKEW = 120; // in seconds
	// Bounds for relay state/replay caches. Java uses an LHMCache of 32
	// recent relay nonces and keeps at most MAX_INBOUND introductions.
	const size_t SSU2_MAX_RELAY_SESSIONS = 32;
	const size_t SSU2_MAX_RECENT_RELAY_REQUESTS = 32;
	const int SSU2_PEER_TEST_EXPIRATION_TIMEOUT = 60; // 60 seconds
	const size_t SSU2_MAX_PACKET_SIZE = 1500;
	const size_t SSU2_MIN_PACKET_SIZE = 1280;
	const size_t SSU2_MLKEM768_MIN_PAYLOAD_SIZE = 1258;
	// SSU2 spec recommends 1.25, 2.5 and 5 seconds for SessionRequest and
	// SessionCreated retransmission; Java OutboundEstablishState.RETRANSMIT_DELAY
	// is 1250 ms.
	const int SSU2_HANDSHAKE_RESEND_INTERVAL = 1250; // in milliseconds
	const int SSU2_MAX_NUM_RESENDS = 5;
	const int SSU2_RESEND_ATTEMPT_MIN_INTERVAL = 3; // in milliseconds
	const int SSU2_INCOMPLETE_MESSAGES_CLEANUP_TIMEOUT = 30; // in seconds
	const size_t SSU2_MAX_NUM_INCOMPLETE_MESSAGES = 128; // per session, bounds fragment reassembly memory
	const int SSU2_MAX_NUM_RECEIVED_I2NP_MSGIDS = 5000; // how many msgID we store for duplicates check
	const int SSU2_RECEIVED_I2NP_MSGIDS_CLEANUP_TIMEOUT = 10; // in seconds
	const int SSU2_DECAY_INTERVAL = 20; // in seconds
	const int SSU2_ROUTERINFO_RESEND_INTERVAL = 20 * 60 * 1000; // in milliseconds
	const int SSU2_ROUTERINFO_RESEND_INTERVAL_VARIANCE = 30 * 60 * 1000; // in milliseconds
	const size_t SSU2_MIN_WINDOW_SIZE = 16; // in packets
	const size_t SSU2_MAX_WINDOW_SIZE = 256; // in packets
	// RFC 6298 / Java PeerState (MIN_RTO, INIT_RTO, MAX_RTO). The initial
	// 540 ms value retransmitted data far too eagerly on a normal WAN path.
	const size_t SSU2_MIN_RTO = 1000; // in milliseconds
	const size_t SSU2_INITIAL_RTO = 1000; // in milliseconds
	const size_t SSU2_MAX_RTO = 60 * 1000; // in milliseconds
	const double SSU2_UNKNOWN_RTT = -1;
	const double SSU2_RTT_EWMA_ALPHA = 0.125;
	const int SSU2_MAX_NUM_ACNT = 255; // acnt, acks or nacks
	const int SSU2_MAX_NUM_ACK_PACKETS = 511; // ackthrough + acnt + 1 range
	const int SSU2_MAX_NUM_ACK_RANGES = 32; // to send
	// Per-session bound for reordered packet numbers. Java's receive window is
	// also far below this; a peer more than one full ACK window ahead is either
	// broken or trying to grow the set between cleanup passes.
	const size_t SSU2_MAX_OUT_OF_SEQUENCE_PACKETS = SSU2_MAX_NUM_ACK_PACKETS;
	const uint8_t SSU2_MAX_NUM_FRAGMENTS = 64;
	const uint8_t SSU2_MAX_SESSION_CONFIRMED_FRAGMENTS = 15; // 4-bit frag/count field
	const int SSU2_SEND_DATETIME_NUM_PACKETS = 256;
	const int SSU2_MIN_RELAY_RESPONSE_RESEND_VERSION = MAKE_VERSION_NUMBER (0, 9, 64); // 0.9.64

	// flags
	const uint8_t SSU2_FLAG_IMMEDIATE_ACK_REQUESTED = 0x01;

	enum SSU2MessageType
	{
		eSSU2SessionRequest = 0,
		eSSU2SessionCreated = 1,
		eSSU2SessionConfirmed = 2,
		eSSU2Data = 6,
		eSSU2PeerTest = 7,
		eSSU2Retry = 9,
		eSSU2TokenRequest = 10,
		eSSU2HolePunch = 11
	};

	enum SSU2BlockType
	{
		eSSU2BlkDateTime = 0,
		eSSU2BlkOptions, // 1
		eSSU2BlkRouterInfo, // 2
		eSSU2BlkI2NPMessage, // 3
		eSSU2BlkFirstFragment, // 4
		eSSU2BlkFollowOnFragment, // 5
		eSSU2BlkTermination, // 6
		eSSU2BlkRelayRequest, // 7
		eSSU2BlkRelayResponse, // 8
		eSSU2BlkRelayIntro, // 9
		eSSU2BlkPeerTest, // 10
		eSSU2BlkNextNonce, // 11
		eSSU2BlkAck, // 12
		eSSU2BlkAddress, // 13
		eSSU2BlkIntroKey, // 14
		eSSU2BlkRelayTagRequest, // 15
		eSSU2BlkRelayTag, // 16
		eSSU2BlkNewToken, // 17
		eSSU2BlkPathChallenge, // 18
		eSSU2BlkPathResponse, // 19
		eSSU2BlkFirstPacketNumber, // 20
		eSSU2BlkPadding = 254
	};

	// Out-of-session PeerTest messages (5/6/7) are decrypted with the public
	// introduction key, so they must never reuse the generic data-phase
	// dispatcher. Only the blocks used by PeerTest are valid there.
	inline bool IsPeerTestPayloadBlockAllowed (uint8_t block)
	{
		switch (block)
		{
		case eSSU2BlkDateTime:
		case eSSU2BlkPeerTest:
		case eSSU2BlkAddress:
		case eSSU2BlkPadding: return true;
		default: return false;
		}
	}

	enum SSU2SessionState
	{
		eSSU2SessionStateUnknown,
		eSSU2SessionStateTokenReceived,
		eSSU2SessionStateSessionRequestSent,
		eSSU2SessionStateSessionRequestReceived,
		eSSU2SessionStateSessionCreatedSent,
		eSSU2SessionStateSessionCreatedReceived,
		eSSU2SessionStateSessionConfirmedSent,
		eSSU2SessionStateEstablished,
		eSSU2SessionStateClosing,
		eSSU2SessionStateClosingConfirmed,
		eSSU2SessionStateTerminated,
		eSSU2SessionStateFailed,
		eSSU2SessionStateIntroduced,
		eSSU2SessionStateHolePunch,
		eSSU2SessionStatePeerTest,
		eSSU2SessionStateTokenRequestReceived
	};

	/**
	 * Payload block whitelist for each SSU2 message context.
	 *
	 * Java SSU2Payload.processPayload() is called with an explicit message type
	 * and rejects a block that does not belong to that message. Applying the
	 * same rule here prevents a handshake payload from reaching data-phase
	 * handlers (I2NP injection, address mutation, relay blocks) before the peer
	 * is authenticated. Data-phase states deliberately keep the historical
	 * full block set; PeerTest keeps its own narrower override.
	 */
	inline bool IsSSU2PayloadBlockAllowedForState (SSU2SessionState state, uint8_t block)
	{
		switch (state)
		{
		case eSSU2SessionStateTokenRequestReceived:
			// DateTime is mandatory; Termination is allowed so a responder can
			// reject the request without building a Retry.
			return block == eSSU2BlkDateTime || block == eSSU2BlkPadding || block == eSSU2BlkTermination;
		case eSSU2SessionStateSessionRequestReceived:
			return block == eSSU2BlkDateTime || block == eSSU2BlkPadding || block == eSSU2BlkRelayTagRequest
				|| block == eSSU2BlkTermination;
		case eSSU2SessionStateSessionCreatedReceived:
			// NewToken and RelayTag are produced by Bob in SessionCreated.
			return block == eSSU2BlkDateTime || block == eSSU2BlkAddress || block == eSSU2BlkNewToken
				|| block == eSSU2BlkRelayTag || block == eSSU2BlkPadding;
		case eSSU2SessionStateTokenReceived: // Retry payload
			return block == eSSU2BlkDateTime || block == eSSU2BlkAddress || block == eSSU2BlkPadding
				|| block == eSSU2BlkTermination;
		case eSSU2SessionStateSessionCreatedSent: // SessionConfirmed payload after RouterInfo
			// The leading RouterInfo block is parsed by ProcessSessionConfirmed();
			// FIRST/FOLLOW-ON fragments have their own validation and are not
			// valid in this context.
			return block == eSSU2BlkRouterInfo || block == eSSU2BlkOptions || block == eSSU2BlkPadding;
		default: return true;
		}
	}

	/**
	 * Whether an Address block may update RouterContext from this session.
	 *
	 * Address blocks are expected in SessionCreated, Retry and the established
	 * data phase. Everything else is pre-authentication: accepting it would let
	 * an unauthenticated packet rewrite our published external address.
	 */
	inline bool IsSSU2AddressBlockContextAllowed (SSU2SessionState state, bool hasRemoteIdentity)
	{
		if (!hasRemoteIdentity) return false;
		switch (state)
		{
		case eSSU2SessionStateEstablished:
		case eSSU2SessionStateSessionCreatedReceived:
		case eSSU2SessionStateTokenReceived: return true;
		default: return false;
		}
	}

	/**
	 * PeerTest (msgs 1-4), I2NP messages and other data-phase handlers require
	 * an established session with a known peer identity.
	 */
	inline bool IsSSU2EstablishedPayloadContext (SSU2SessionState state, bool hasRemoteIdentity)
	{
		return state == eSSU2SessionStateEstablished && hasRemoteIdentity;
	}

	/**
	 * Pure framing and block-whitelist validator for decrypted SSU2 payloads.
	 * It never touches sockets, sessions or global router state, so fuzzers and
	 * unit tests can exercise the state matrix directly.
	 *
	 * The caller's handshake-specific rules (DateTime must be first) remain in
	 * ProcessSessionRequest/ProcessSessionCreated/ProcessTokenRequest/ProcessRetry.
	 */
	inline bool ValidateSSU2PayloadBlocks (SSU2SessionState state, const uint8_t * buf, size_t len)
	{
		if (!buf && len) return false;
		bool gotPadding = false, gotTermination = false;
		size_t offset = 0;
		while (offset < len)
		{
			if (offset + 3 > len) return false;
			const uint8_t block = buf[offset++];
			const size_t size = ((size_t)buf[offset] << 8) | buf[offset + 1];
			offset += 2;
			if (!IsSSU2PayloadBlockAllowedForState (state, block)) return false;
			// SSU2Payload.processPayload() rejects everything after padding and
			// everything except padding after a termination block.
			if (gotPadding || (gotTermination && block != eSSU2BlkPadding)) return false;
			if (offset + size > len) return false;
			if (block == eSSU2BlkDateTime && size != 4) return false;
			if (block == eSSU2BlkTermination)
			{
				if (size < 9) return false;
				gotTermination = true;
			}
			if (block == eSSU2BlkPadding) gotPadding = true;
			offset += size;
		}
		return offset == len;
	}

	enum SSU2PeerTestCode
	{
		eSSU2PeerTestCodeAccept = 0,
		eSSU2PeerTestCodeBobReasonUnspecified = 1,
		eSSU2PeerTestCodeBobNoCharlieAvailable = 2,
		eSSU2PeerTestCodeBobLimitExceeded = 3,
		eSSU2PeerTestCodeBobSignatureFailure = 4,
		eSSU2PeerTestCodeCharlieReasonUnspecified = 64,
		eSSU2PeerTestCodeCharlieUnsupportedAddress = 65,
		eSSU2PeerTestCodeCharlieLimitExceeded = 66,
		eSSU2PeerTestCodeCharlieSignatureFailure = 67,
		eSSU2PeerTestCodeCharlieAliceIsAlreadyConnected = 68,
		eSSU2PeerTestCodeCharlieAliceIsBanned = 69,
		eSSU2PeerTestCodeCharlieAliceIsUnknown = 70,
		eSSU2PeerTestCodeUnspecified = 128
	};

	enum SSU2RelayResponseCode
	{
		eSSU2RelayResponseCodeAccept = 0,
		eSSU2RelayResponseCodeBobUnspecified = 1,
		eSSU2RelayResponseCodeBobLimit = 3,
		eSSU2RelayResponseCodeBobSignatureFailure = 4,
		eSSU2RelayResponseCodeBobRelayTagNotFound = 5,
		eSSU2RelayResponseCodeBobAliceIsUnknown = 6,
		eSSU2RelayResponseCodeCharlieUnsupportedAddress = 65,
		eSSU2RelayResponseCodeCharlieSignatureFailure = 67,
		eSSU2RelayResponseCodeCharlieAliceIsUnknown = 70
	};

	enum SSU2TerminationReason
	{
		eSSU2TerminationReasonNormalClose = 0,
		eSSU2TerminationReasonTerminationReceived = 1,
		eSSU2TerminationReasonIdleTimeout = 2,
		eSSU2TerminationReasonRouterShutdown = 3,
		eSSU2TerminationReasonDataPhaseAEADFailure = 4,
		eSSU2TerminationReasonIncompatibleOptions = 5,
		eSSU2TerminationReasonTncompatibleSignatureType = 6,
		eSSU2TerminationReasonClockSkew = 7,
		eSSU2TerminationPaddingViolation = 8,
		eSSU2TerminationReasonAEADFramingError = 9,
		eSSU2TerminationReasonPayloadFormatError = 10,
		eSSU2TerminationReasonSessionRequestError = 11,
		eSSU2TerminationReasonSessionCreatedError = 12,
		eSSU2TerminationReasonSessionConfirmedError = 13,
		eSSU2TerminationReasonTimeout = 14,
		eSSU2TerminationReasonRouterInfoSignatureVerificationFail = 15,
		eSSU2TerminationReasonInvalidS = 16,
		eSSU2TerminationReasonBanned = 17,
		eSSU2TerminationReasonBadToken = 18,
		eSSU2TerminationReasonConnectionLimits = 19,
		eSSU2TerminationReasonIncompatibleVersion = 20,
		eSSU2TerminationReasonWrongNetID = 21,
		eSSU2TerminationReasonReplacedByNewSession = 22
	};

	struct SSU2IncompleteMessage
	{
		struct Fragment
		{
			uint8_t buf[SSU2_MAX_PACKET_SIZE];
			size_t len;
			int fragmentNum;
			bool isLast;
			std::shared_ptr<Fragment> next;

			Fragment (const uint8_t * b, size_t l, int fn, bool last)
				: len (l)
				, fragmentNum (fn)
				, isLast (last)
			{
				if (len > SSU2_MAX_PACKET_SIZE) len = SSU2_MAX_PACKET_SIZE;
				memcpy (buf, b, len);
			}
		};

		std::shared_ptr<I2NPMessage> msg;
		int nextFragmentNum;
		int lastFragmentNum = -1; // highest fragment explicitly marked isLast
		uint32_t lastFragmentInsertTime; // in seconds
		std::shared_ptr<Fragment> outOfSequenceFragments; // #1 and more

		bool AttachNextFragment (const uint8_t * fragment, size_t fragmentSize); // false on size overflow
		bool ConcatOutOfSequenceFragments (); // true if message complete
		void AddOutOfSequenceFragment (std::shared_ptr<Fragment> fragment);
	};

	struct SSU2SentPacket
	{
		uint8_t payload[SSU2_MAX_PACKET_SIZE];
		size_t payloadSize = 0;
		uint64_t sendTime; // in milliseconds
		int numResends = 0;
	};

	// RouterInfo flags
	const uint8_t SSU2_ROUTER_INFO_FLAG_REQUEST_FLOOD = 0x01;
	const uint8_t SSU2_ROUTER_INFO_FLAG_GZIP = 0x02;

	class SSU2Server;
	class SSU2Session : public TransportSession, public std::enable_shared_from_this<SSU2Session>
	{
	protected:
		union Header
		{
			uint64_t ll[2];
			uint8_t buf[16];
			struct
			{
				uint64_t connID;
				uint32_t packetNum;
				uint8_t type;
				uint8_t flags[3];
			} h;
		};

	private:
		struct HandshakePacket
		{
			Header header;
			uint8_t headerX[48]; // part1 for SessionConfirmed
			uint8_t payload[SSU2_MAX_PACKET_SIZE * 2];
			size_t payloadSize = 0;
			uint64_t sendTime = 0; // in milliseconds
			int numResends = 0; // handshake retransmissions, used for backoff
			bool isSecondFragment = false; // for SessionConfirmed
		};

		typedef std::function<void ()> OnEstablished;

	public:
		SSU2Session (SSU2Server & server, std::shared_ptr<const i2p::data::RouterInfo> in_RemoteRouter = nullptr,
			std::shared_ptr<const i2p::data::RouterInfo::Address> addr = nullptr);
		virtual ~SSU2Session ();

		bool SetVersion (uint8_t version);
		void SetRemoteEndpoint (const boost::asio::ip::udp::endpoint & ep) { m_RemoteEndpoint = ep; };
		const boost::asio::ip::udp::endpoint & GetRemoteEndpoint () const { return m_RemoteEndpoint; };
		void AdjustMaxPayloadSize (size_t maxMtu = SSU2_MAX_PACKET_SIZE);
		i2p::data::RouterInfo::CompatibleTransports GetRemoteTransports () const { return m_RemoteTransports; };
		i2p::data::RouterInfo::CompatibleTransports GetRemotePeerTestTransports () const
		{
			return m_RemotePeerTestTransports;
		};
		int GetRemoteVersion () const { return m_RemoteVersion; };
		std::shared_ptr<const i2p::data::RouterInfo::Address> GetAddress () const { return m_Address; };
		void SetOnEstablished (OnEstablished e)
		{
			std::lock_guard<std::mutex> l (m_OnEstablishedMutex);
			m_OnEstablished = std::move (e);
		};
		// Append instead of replacing, so a peer-test or introduction callback
		// registered while another callback is pending is not lost.
		void AddOnEstablished (OnEstablished e)
		{
			if (!e) return;
			std::lock_guard<std::mutex> l (m_OnEstablishedMutex);
			if (!m_OnEstablished)
			{
				m_OnEstablished = std::move (e);
				return;
			}
			auto previous = std::move (m_OnEstablished);
			m_OnEstablished = [previous = std::move (previous), e = std::move (e)] () mutable
			{
				previous ();
				e ();
			};
		};
		OnEstablished GetOnEstablished () const
		{
			std::lock_guard<std::mutex> l (m_OnEstablishedMutex);
			return m_OnEstablished;
		};

		virtual void Connect ();
		bool Introduce (std::shared_ptr<SSU2Session> session, uint32_t relayTag);
		void WaitForIntroduction ();
		void SendPeerTest (); // Alice, Data message
		void SendKeepAlive ();
		void RequestTermination (SSU2TerminationReason reason);
		void CleanUp (uint64_t ts);
		void FlushData ();
		void Done () override;
		void SendLocalRouterInfo (bool update) override;
		void SendI2NPMessages (std::list<std::shared_ptr<I2NPMessage>> & msgs) override;
		void MoveSendQueue (std::shared_ptr<SSU2Session> other);
		uint32_t GetRelayTag () const override { return m_RelayTag; };
		size_t Resend (uint64_t ts); // return number of resent packets
		uint64_t GetLastResendTime () const { return m_LastResendTime; };
		bool IsEstablished () const override { return m_State == eSSU2SessionStateEstablished; };
		i2p::data::RouterInfo::SupportedTransports GetTransportType () const override;
		boost::asio::ip::address GetRemoteAddress () const override { return m_RemoteEndpoint.address (); };
		uint64_t GetConnID () const { return m_SourceConnID; };
		SSU2SessionState GetState () const { return m_State; };
		void SetState (SSU2SessionState state) { m_State = state; };

		virtual bool ProcessFirstIncomingMessage (uint64_t connID, uint8_t * buf, size_t len);
		bool ProcessSessionCreated (uint8_t * buf, size_t len);
		bool ProcessSessionConfirmed (uint8_t * buf, size_t len);
		bool ProcessRetry (uint8_t * buf, size_t len);
		bool ProcessHolePunch (uint8_t * buf, size_t len);
		virtual bool ProcessPeerTest (uint8_t * buf, size_t len);
		void ProcessData (uint8_t * buf, size_t len, const boost::asio::ip::udp::endpoint & from);

	protected:
		SSU2Server & GetServer () { return m_Server; }
		RouterStatus GetRouterStatus () const;
		void SetRouterStatus (RouterStatus status) const;
		size_t GetMaxPayloadSize () const { return m_MaxPayloadSize; }
		void SetIsDataReceived (bool dataReceived) { m_IsDataReceived = dataReceived; };

		uint64_t GetSourceConnID () const { return m_SourceConnID; }
		void SetSourceConnID (uint64_t sourceConnID) { m_SourceConnID = sourceConnID; }
		uint64_t GetDestConnID () const { return m_DestConnID; }
		void SetDestConnID (uint64_t destConnID) { m_DestConnID = destConnID; }

		void SetAddress (std::shared_ptr<const i2p::data::RouterInfo::Address> addr) { m_Address = addr; }
		void HandlePayload (const uint8_t * buf, size_t len);
		bool HasInvalidPayload () const { return m_IsInvalidMessage; }
		virtual bool IsPayloadBlockAllowed (uint8_t block) const { return true; }

		size_t CreateAddressBlock (uint8_t * buf, size_t len, const boost::asio::ip::udp::endpoint & ep);
		size_t CreatePaddingBlock (uint8_t * buf, size_t len, size_t minSize = 0);
		size_t CreatePeerTestBlock (uint8_t * buf, size_t len, uint8_t msg, SSU2PeerTestCode code,
			const uint8_t * routerHash, const uint8_t * signedData, size_t signedDataLen);

		bool ExtractEndpoint (const uint8_t * buf, size_t size, boost::asio::ip::udp::endpoint & ep);

	private:
		void Terminate ();
		void Established ();
		OnEstablished TakeOnEstablished ();
		void ClearOnEstablished ();
		void ScheduleConnectTimer ();
		void HandleConnectTimer (const boost::system::error_code & ecode);
		void PostI2NPMessages ();
		bool SendQueue (); // returns true if ack block was sent
		bool SendFragmentedMessage (std::shared_ptr<I2NPMessage> msg);
		void ResendHandshakePacket ();
		void ConnectAfterIntroduction ();

		bool ProcessSessionRequest (Header & header, uint8_t * buf, size_t len);
		bool ProcessTokenRequest (Header & header, uint8_t * buf, size_t len);

		void SelectOutgoingVersion ();
		bool SendSessionRequest (uint64_t token = 0);
		bool SendSessionCreated (const uint8_t * X);
		bool SendSessionConfirmed (const uint8_t * Y);
		void KDFDataPhase (uint8_t * keydata_ab, uint8_t * keydata_ba);
		void SendTokenRequest ();
		void SendRetry ();
		// Returns false when the packet could not be built or sent. Packet
		// number 0 is a valid data packet number, so it is never used as an
		// error sentinel.
		bool SendData (const uint8_t * buf, size_t len, uint8_t flags = 0, uint32_t * packetNum = nullptr);
		void SendQuickAck ();
		void SendTermination ();
		void SendPathResponse (const uint8_t * data, size_t len);
		void SendPathChallenge (const boost::asio::ip::udp::endpoint & to);

		void HandleDateTime (const uint8_t * buf, size_t len);
		void HandleRouterInfo (const uint8_t * buf, size_t len);
		void HandleAck (const uint8_t * buf, size_t len);
		void HandleAckRange (uint32_t firstPacketNum, uint32_t lastPacketNum, uint64_t ts);
		virtual void HandleAddress (const uint8_t * buf, size_t len);
		size_t CreateEndpoint (uint8_t * buf, size_t len, const boost::asio::ip::udp::endpoint & ep);
		std::shared_ptr<const i2p::data::RouterInfo::Address> FindLocalAddress () const;
		bool GetTestingState () const;
		void SetTestingState (bool testing) const;
		std::shared_ptr<const i2p::data::RouterInfo> ExtractRouterInfo (const uint8_t * buf, size_t size);
		bool UpdateReceivePacketNum (uint32_t packetNum); // for Ack, returns false if duplicate
		uint32_t GetHighestReceivedPacketNum () const
		{
			return m_OutOfSequencePackets.empty () ? m_ReceivePacketNum : *m_OutOfSequencePackets.rbegin ();
		}
		void HandleFirstFragment (const uint8_t * buf, size_t len);
		void HandleFollowOnFragment (const uint8_t * buf, size_t len);
		void HandleRelayRequest (const uint8_t * buf, size_t len);
		void HandleRelayIntro (const uint8_t * buf, size_t len, int attempts = 0);
		void HandleRelayResponse (const uint8_t * buf, size_t len);
		virtual void HandlePeerTest (const uint8_t * buf, size_t len);
		void HandleI2NPMsg (std::shared_ptr<I2NPMessage> && msg);

		size_t CreateRouterInfoBlock (uint8_t * buf, size_t len, std::shared_ptr<const i2p::data::RouterInfo> r);
		size_t CreateRouterInfoBlock (
			uint8_t * buf, size_t len, std::shared_ptr<const i2p::data::RouterInfo::Buffer> riBuffer);
		size_t CreateAckBlock (uint8_t * buf, size_t len);
		size_t CreateI2NPBlock (uint8_t * buf, size_t len, std::shared_ptr<I2NPMessage> && msg);
		size_t CreateFirstFragmentBlock (uint8_t * buf, size_t len, std::shared_ptr<I2NPMessage> msg);
		size_t CreateFollowOnFragmentBlock (
			uint8_t * buf, size_t len, std::shared_ptr<I2NPMessage> msg, uint8_t & fragmentNum, uint32_t msgID);
		size_t CreateRelayIntroBlock (uint8_t * buf, size_t len, const uint8_t * introData, size_t introDataLen);
		size_t CreateRelayResponseBlock (
			uint8_t * buf, size_t len, SSU2RelayResponseCode code, uint32_t nonce, uint64_t token, bool v4);

		size_t CreatePeerTestBlock (uint8_t * buf, size_t len, uint32_t nonce); // Alice
		size_t CreateTerminationBlock (uint8_t * buf, size_t len);

	private:
		SSU2Server & m_Server;
		std::shared_ptr<i2p::crypto::X25519Keys> m_EphemeralKeys;
		std::unique_ptr<i2p::crypto::NoiseSymmetricState> m_NoiseState;
		// SessionConfirmed fragments 1..n-1 that Alice must retransmit with
		// fragment 0. Fragment 0 is m_SentHandshakePacket.
		std::vector<std::unique_ptr<HandshakePacket>> m_SessionConfirmedFragmentsOut;
		std::map<uint8_t, std::vector<uint8_t>> m_SessionConfirmedFragments;
		uint8_t m_SessionConfirmedNumFragments = 0;
		Header m_SessionConfirmedHeader { };
		bool m_IsVersionFallback = false;
		bool m_IsFirstReceivePacket = true;
		bool m_ReceivedPacketZero = false;
		std::unique_ptr<HandshakePacket> m_SentHandshakePacket; // SessionRequest, SessionCreated or SessionConfirmed
#if OPENSSL_PQ
		std::unique_ptr<i2p::crypto::MLKEMKeys> m_PQKeys;
#endif
		std::shared_ptr<const i2p::data::RouterInfo::Address> m_Address;
		boost::asio::ip::udp::endpoint m_RemoteEndpoint;
		i2p::data::RouterInfo::CompatibleTransports m_RemoteTransports, m_RemotePeerTestTransports;
		int m_RemoteVersion;
		uint64_t m_DestConnID, m_SourceConnID;
		std::atomic<SSU2SessionState> m_State;
		uint8_t m_KeyDataSend[64] = { }, m_KeyDataReceive[64] = { };
		uint32_t m_SendPacketNum, m_ReceivePacketNum, m_LastDatetimeSentPacketNum;
		std::set<uint32_t> m_OutOfSequencePackets; // packet nums > receive packet num
		std::map<uint32_t, std::shared_ptr<SSU2SentPacket>> m_SentPackets; // packetNum -> packet
		std::unordered_map<uint32_t, std::shared_ptr<SSU2IncompleteMessage>> m_IncompleteMessages; // msgID -> I2NP
		std::unordered_map<uint32_t, std::pair<std::shared_ptr<SSU2Session>, uint64_t>>
			m_RelaySessions; // nonce->(Alice, timestamp) for Bob or nonce->(Charlie, timestamp) for Alice
		std::unordered_map<uint32_t, uint32_t> m_RecentRelayRequests; // nonce->timestamp, Bob replay cache (bounded)
		std::list<std::shared_ptr<I2NPMessage>> m_SendQueue;
		i2p::I2NPMessagesHandler m_Handler;
		std::list<std::shared_ptr<I2NPMessage>> m_IntermediateQueue; // from transports
		mutable std::mutex m_IntermediateQueueMutex;
		bool m_IsDataReceived, m_IsInvalidMessage;
		bool m_ReceivedTermination;
		double m_RTT;
		double m_RTTDeviation; // RFC 6298 variation, in milliseconds
		int m_MsgLocalExpirationTimeout;
		int m_MsgLocalSemiExpirationTimeout;
		size_t m_WindowSize, m_RTO;
		uint32_t m_RelayTag; // between Bob and Charlie
		mutable std::mutex m_OnEstablishedMutex;
		OnEstablished m_OnEstablished; // callback from Established, guarded by m_OnEstablishedMutex
		boost::asio::steady_timer m_ConnectTimer;
		SSU2TerminationReason m_TerminationReason;
		size_t m_MaxPayloadSize;
		std::unique_ptr<std::pair<uint64_t, boost::asio::ip::udp::endpoint>> m_PathChallenge;
		std::unordered_map<uint32_t, uint32_t> m_ReceivedI2NPMsgIDs; // msgID -> timestamp in seconds
		uint64_t m_LastResendTime, m_LastResendAttemptTime, m_NextRouterInfoResendTime; // in milliseconds
		int m_NumRanges;
		uint8_t m_Ranges[(SSU2_MAX_NUM_ACK_RANGES + 1) * 2]; // ranges sent with previous Ack if any
	};

	inline uint64_t CreateHeaderMask (const uint8_t * kh, const uint8_t * nonce)
	{
		uint64_t data = 0;
		i2p::crypto::ChaCha20 ((uint8_t *)&data, 8, kh, nonce, (uint8_t *)&data);
		return data;
	}

	inline void CreateNonce (uint64_t seqn, uint8_t * nonce)
	{
		memset (nonce, 0, 4);
		htole64buf (nonce + 4, seqn);
	}
}
}

#endif
