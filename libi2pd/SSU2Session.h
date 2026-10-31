/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: SSU2Session.
 *
 * Declares SSU2Session and its handshake state machine, packet and
 * block codecs, ACK/retransmission state, fragment reassembly, relay
 * handling and peer-test hooks. Protocol limits mirror the SSU2
 * specification and Java router behaviour (RTO, windows, fragment
 * counts and resend budgets).
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
#include <cstring>
#include <algorithm>
#include <vector>
#include <map>
#include <set>
#include <list>
#include <unordered_map>
#include <boost/asio.hpp>
#include "version.h"
#include "Crypto.h"
#include "I2PEndian.h"
#include "I2NPProtocol.h"
#include "PostQuantum.h"
#include "RouterInfo.h"
#include "RouterContext.h"
#include "TransportSession.h"

namespace i2p
{
namespace transport
{
	// Overall outbound establishment budget. Java uses
	// MAX_OB_ESTABLISH_TIME (EstablishmentManager.java:153) and
	// per-message OB_MESSAGE_TIMEOUT = 15 s.
	const int SSU2_CONNECT_TIMEOUT = 25; // in seconds
	const int SSU2_TERMINATION_TIMEOUT = 165; // in seconds, Java UDPTransport.MIN_EXPIRE_TIMEOUT
	// Java EstablishmentManager compares the peer DateTime against a two
	// minute window (and, for the handshake, additionally tolerates half
	// the measured RTT). A one minute window dropped peers with a normally
	// imperfect clock, so keep the same 120 s bound as the relay/peer-test
	// paths.
	const int SSU2_CLOCK_SKEW = 120; // in seconds
	const int SSU2_CLOCK_THRESHOLD = 15; // in seconds, if more we should adjust
	const int SSU2_TOKEN_EXPIRATION_TIMEOUT = 9; // for Retry message, in seconds
	const int SSU2_NEXT_TOKEN_EXPIRATION_TIMEOUT = 52 * 60; // for next token block, in seconds
	const int SSU2_TOKEN_EXPIRATION_THRESHOLD = 2; // in seconds
	const int SSU2_RELAY_NONCE_EXPIRATION_TIMEOUT = 10; // in seconds
	// Bob answers a RelayRequest immediately, so its pending nonce is
	// short-lived. Alice waits for Charlie's HolePunch (and Bob's
	// RelayResponse when the introduction is direct), so it keeps a longer
	// budget.
	const int SSU2_RELAY_NONCE_EXPIRATION_BOB = SSU2_RELAY_NONCE_EXPIRATION_TIMEOUT; // 10 seconds
	const int SSU2_RELAY_NONCE_EXPIRATION_ALICE = 30; // 30 seconds
	const uint32_t SSU2_RECENT_RELAY_REQUEST_EXPIRATION = 10; // 10 seconds, Bob replay cache
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
	// SessionCreated retransmission; Java
	// OutboundEstablishState.RETRANSMIT_DELAY is 1250 ms.
	const int SSU2_HANDSHAKE_RESEND_INTERVAL = 1250; // in milliseconds
	const int SSU2_MAX_NUM_RESENDS = 5;
	const int SSU2_RESEND_ATTEMPT_MIN_INTERVAL = 3; // in milliseconds
	const int SSU2_INCOMPLETE_MESSAGES_CLEANUP_TIMEOUT = 10; // in seconds, Java MAX_RECEIVE_TIME
	// Per-session incomplete-message cap. Each message can queue at most
	// SSU2_MAX_FRAGMENTS_PER_MESSAGE fixed slabs, so 32 messages bound the
	// reassembly working set to roughly 3 MiB instead of the previous 12
	// MiB.
	const size_t SSU2_MAX_NUM_INCOMPLETE_MESSAGES = 32;
	const size_t SSU2_MAX_OUT_OF_SEQUENCE_FRAGMENTS = 512; // per incomplete message
	const int SSU2_MAX_NUM_RECEIVED_I2NP_MSGIDS = 5000; // how many msgID we store for duplicates check
	const int SSU2_RECEIVED_I2NP_MSGIDS_CLEANUP_TIMEOUT = 10; // in seconds
	const int SSU2_DECAY_INTERVAL = 20; // in seconds
	const int SSU2_ROUTERINFO_RESEND_INTERVAL = 20 * 60 * 1000; // in milliseconds
	const int SSU2_ROUTERINFO_RESEND_INTERVAL_VARIANCE = 30 * 60 * 1000; // in milliseconds
	const size_t SSU2_MIN_WINDOW_SIZE = 16; // in packets
	const size_t SSU2_MAX_WINDOW_SIZE = 256; // in packets
	// RFC 6298 / Java PeerState (MIN_RTO, INIT_RTO, MAX_RTO). The initial
	// 540 ms value retransmitted data far too eagerly on a normal WAN
	// path.
	const size_t SSU2_MIN_RTO = 1000; // in milliseconds
	const size_t SSU2_INITIAL_RTO = 1000; // in milliseconds
	const size_t SSU2_MAX_RTO = 60 * 1000; // in milliseconds
	const double SSU2_UNKNOWN_RTT = -1;
	const double SSU2_RTT_EWMA_ALPHA = 0.125;
	const int SSU2_MAX_NUM_ACNT = 255; // acnt, acks or nacks
	const int SSU2_MAX_NUM_ACK_PACKETS = 511; // ackthrough + acnt + 1 range
	const int SSU2_MAX_NUM_ACK_RANGES = 32; // to send
	// Per-session bound for reordered packet numbers. Java's receive
	// window is also far below this; a peer more than one full ACK window
	// ahead is either broken or trying to grow the set between cleanup
	// passes.
	const size_t SSU2_MAX_OUT_OF_SEQUENCE_PACKETS = SSU2_MAX_NUM_ACK_PACKETS;
	const uint8_t SSU2_MAX_NUM_FRAGMENTS = 64;
	const uint8_t SSU2_MAX_SESSION_CONFIRMED_FRAGMENTS = 15; // 4-bit frag/count field
	// A follow-on fragment that is queued while out of sequence
	// contributes at least one byte to the reassembled I2NP message, whose
	// size is bounded by I2NP_MAX_MESSAGE_SIZE. More fragments than this
	// can never be needed, so the per-message chain is capped
	// independently of the 7-bit field.
	const size_t SSU2_MAX_FRAGMENTS_PER_MESSAGE = SSU2_MAX_NUM_FRAGMENTS < I2NP_MAX_MESSAGE_SIZE
		? static_cast<size_t> (SSU2_MAX_NUM_FRAGMENTS)
		: I2NP_MAX_MESSAGE_SIZE;
	// Fixed-slab byte budget for one incomplete message. A full chain of
	// SSU2_MAX_FRAGMENTS_PER_MESSAGE slabs must fit, so this only guards
	// against accounting mistakes; the fragment count is the real cap.
	const size_t SSU2_MAX_OUT_OF_SEQUENCE_BYTES = 128 * 1024;
	// The published/effective IPv6 MTU is clamped to the SSU2 IPv6 ceiling
	// (Java PeerState2.MAX_IPV6_MTU) while keeping the 1280 byte floor.
	const int SSU2_MAX_IPV6_MTU = 1488;
	const int SSU2_SEND_DATETIME_NUM_PACKETS = 256;
	const int SSU2_MIN_RELAY_RESPONSE_RESEND_VERSION = MAKE_VERSION_NUMBER (0, 9, 64); // 0.9.64
	// Java PeerState2 connection migration: at most four PathChallenge
	// sends and a 30 s overall budget, with the retry delay doubling from
	// 5 s. The session keeps using the old endpoint when the challenge
	// expires.
	const unsigned int SSU2_MAX_PATH_CHALLENGE_SENDS = 4;
	const uint64_t SSU2_PATH_CHALLENGE_DELAY = 5 * 1000; // in milliseconds
	const uint64_t SSU2_MAX_PATH_CHALLENGE_TIME = 30 * 1000; // in milliseconds

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

	// Out-of-session PeerTest messages (5/6/7) are decrypted with the
	// public introduction key, so they must never reuse the generic
	// data-phase dispatcher. Only the blocks used by PeerTest are valid
	// there.
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

	/**
	 * HolePunch/Introduced payload whitelist.
	 *
	 * SSU2 spec, Hole Punch: the payload carries DateTime, Address,
	 * RelayResponse, Padding and an optional Termination. It is decrypted
	 * with the public introduction key, so everything else (RouterInfo,
	 * NewToken, fragments/I2NP, RelayRequest/Intro, PeerTest,
	 * PathChallenge, Ack) must be rejected before a data-phase handler can
	 * act on it.
	 */
	inline bool IsHolePunchPayloadBlockAllowed (uint8_t block)
	{
		switch (block)
		{
		case eSSU2BlkDateTime:
		case eSSU2BlkAddress:
		case eSSU2BlkRelayResponse:
		case eSSU2BlkPadding:
		case eSSU2BlkTermination: return true;
		default: return false;
		}
	}

	/**
	 * Out-of-session message versions accepted on receive.
	 *
	 * PeerTest and HolePunch always send version 2 (Java PacketBuilder2),
	 * but a peer that already negotiated a post-quantum version may put it
	 * in the long header. Accept 2..4 on receive and keep sending 2.
	 */
	constexpr bool IsSSU2OutOfSessionVersionAccepted (uint8_t version)
	{
		return version >= 2 && version <= 4;
	}

	/**
	 * Advertised SSU2 version mask from a signed RouterInfo address.
	 *
	 * Version 2 is the base "v" option and is always usable. Bits 3 and 4
	 * are only set when the publisher listed them in the comma-separated
	 * "pq" address option, which is what
	 * RouterInfo::Address::SupportsSSU2Version() checks.
	 */
	constexpr uint8_t GetSSU2AdvertisedVersionMask (uint8_t pqVersions)
	{
		return static_cast<uint8_t> ((1u << 2) | (pqVersions & ((1u << 3) | (1u << 4))));
	}

	/** Highest advertised version, always at least 2. */
	constexpr int GetHighestSSU2Version (uint8_t advertisedMask)
	{
		for (int version = 4; version >= 3; --version)
			if (advertisedMask & (1u << version)) return version;
		return 2;
	}

	/**
	 * Whether an unauthenticated Retry header may change Alice's version.
	 *
	 * The Retry header is authenticated only with the peer's published
	 * intro key, which any on-path attacker knows, so its "ver" byte is
	 * not authority. A change is adopted only when the peer's signed
	 * RouterInfo advertises that exact version and it is not below the
	 * highest advertised version; the only accepted change is therefore
	 * the peer's own maximum. Everything else fails the attempt instead of
	 * silently downgrading PQ.
	 */
	constexpr bool IsSSU2RetryVersionAccepted (int retryVersion, uint8_t advertisedMask)
	{
		if (retryVersion < 2 || retryVersion > 4) return false;
		if (!(advertisedMask & (1u << retryVersion))) return false;
		return retryVersion >= GetHighestSSU2Version (advertisedMask);
	}

	/**
	 * Version to retry after an authenticated IncompatibleVersion
	 * termination.
	 *
	 * Returns the highest version strictly below `rejectedVersion` that
	 * the peer advertised, or 0 when there is none (the attempt must then
	 * fail). The termination block travels inside the AEAD payload, so
	 * this hint is authentic, but it still may only select an advertised
	 * version: version 2 is always advertised, hybrid versions only when
	 * "pq" named them.
	 */
	constexpr int GetSSU2FallbackVersion (int rejectedVersion, uint8_t advertisedMask)
	{
		for (int version = rejectedVersion - 1; version >= 2; --version)
			if (advertisedMask & (1u << version)) return version;
		return 0;
	}

	/**
	 * Whether an ACK block actually acknowledges packet 0.
	 *
	 * The ACK block covers [ackThrough - acnt, ackThrough] and no First
	 * Packet Number is negotiated, so packet 0 is covered only when acnt
	 * counts back to it. Alice must not treat an ACK for a later packet as
	 * the SessionConfirmed confirmation and must keep retransmitting
	 * instead.
	 */
	constexpr bool IsSSU2AckCoveringPacketZero (uint32_t ackThrough, uint32_t acnt)
	{
		return acnt == ackThrough;
	}

	/// Handshake, data, closing and auxiliary introduction/peer-test
	/// states. Payload block admission depends on this state; it is not a
	/// linear counter.
	/// @see https://i2p.net/en/docs/specs/ssu2/
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
	 * Java SSU2Payload.processPayload() is called with an explicit message
	 * type and rejects a block that does not belong to that message.
	 * Applying the same rule here prevents a handshake payload from
	 * reaching data-phase handlers (I2NP injection, address mutation,
	 * relay blocks) before the peer is authenticated. Data-phase states
	 * deliberately keep the historical full block set; PeerTest keeps its
	 * own narrower override.
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
			// The leading RouterInfo block is parsed by
			// ProcessSessionConfirmed(); FIRST/FOLLOW-ON fragments have their
			// own validation and are not valid in this context.
			return block == eSSU2BlkRouterInfo || block == eSSU2BlkOptions || block == eSSU2BlkPadding;
		case eSSU2SessionStateHolePunch: // Charlie waiting for Alice's SessionRequest
		case eSSU2SessionStateIntroduced: // Alice in an out-of-session HolePunch
			// HolePunch payloads are encrypted with the public introduction key
			// and may only carry DateTime/Address/RelayResponse/Padding.
			return IsHolePunchPayloadBlockAllowed (block);
		default: return true;
		}
	}

	/**
	 * Whether an Address block may update RouterContext from this session.
	 *
	 * Address blocks are expected in SessionCreated, Retry and the
	 * established data phase. Everything else is pre-authentication:
	 * accepting it would let an unauthenticated packet rewrite our
	 * published external address.
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
	 * PeerTest (msgs 1-4), I2NP messages and other data-phase handlers
	 * require an established session with a known peer identity.
	 */
	inline bool IsSSU2EstablishedPayloadContext (SSU2SessionState state, bool hasRemoteIdentity)
	{
		return state == eSSU2SessionStateEstablished && hasRemoteIdentity;
	}

	// Inbound admission and PeerTest caps. An unauthenticated peer can
	// create a non-established inbound session with one SessionRequest, so
	// bound both the number of such sessions and the number of PeerTest
	// pseudo-sessions. Java uses MAX_INBOUND_SESSIONS and limits PeerTest
	// msgs 1/2 to 12 per source and 10 minutes. Deliberately not
	// configurable: no new configuration keys are introduced for these
	// internal bounds.
	const size_t SSU2_MAX_INBOUND_SESSIONS = 40;
	const size_t SSU2_MAX_PEER_TEST_SESSIONS = 64;
	const size_t SSU2_PEER_TEST_RATE_LIMIT = 12;
	const uint64_t SSU2_PEER_TEST_RATE_WINDOW = 10 * 60; // in seconds
	const size_t SSU2_MAX_PEER_TEST_RATE_ENTRIES = 256;

	/** Whether one more non-established inbound session may be created. */
	constexpr bool IsInboundSessionAdmissionAllowed (size_t numNonEstablishedInboundSessions)
	{
		return numNonEstablishedInboundSessions < SSU2_MAX_INBOUND_SESSIONS;
	}

	/**
	 * Reaction to an inbound packet while SSU2_MAX_INBOUND_SESSIONS is
	 * reached. Only a SessionRequest or TokenRequest still has a handshake
	 * context that a Retry can answer with
	 * eSSU2TerminationReasonConnectionLimits; every other message type is
	 * dropped without a reply.
	 */
	constexpr bool CanRejectInboundHandshakeWithRetry (uint8_t messageType)
	{
		return messageType == static_cast<uint8_t> (eSSU2SessionRequest)
			|| messageType == static_cast<uint8_t> (eSSU2TokenRequest);
	}

	/** Whether one more out-of-session PeerTest session may be created. */
	constexpr bool IsSSU2PeerTestSessionAdmissionAllowed (size_t numPeerTestSessions)
	{
		return numPeerTestSessions < SSU2_MAX_PEER_TEST_SESSIONS;
	}

	/**
	 * Whether a peer test in the given family may proceed despite an
	 * existing session to Alice. Java only blocks when the existing
	 * session has the same address family (`aps.isIPv6() == isIPv6`), so
	 * an IPv4 session must not suppress an IPv6 test (or vice versa).
	 */
	template <typename SessionPtr> bool IsSSU2PeerTestFamilyAvailable (const SessionPtr & existing, bool testV4)
	{
		if (!existing || !existing->IsEstablished ()) return true;
		return existing->GetRemoteEndpoint ().address ().is_v4 () != testV4;
	}

	/**
	 * Whether the Bob-side PeerTest nonce table is full (never evict live
	 * state).
	 */
	constexpr bool IsSSU2PeerTestTableFull (size_t numEntries, size_t maximum)
	{
		return numEntries >= maximum;
	}

	/** Result of admitting a Bob-side PeerTest nonce. */
	enum class SSU2PeerTestAdmission : uint8_t
	{
		Accepted = 0,
		Duplicate,
		LimitExceeded
	};

	/**
	 * RouterInfo block frag byte for one complete (unfragmented)
	 * RouterInfo. The SSU2 spec encodes `index << 4 | count` for
	 * fragments, but only this value is valid in the SessionConfirmed
	 * leading block and in a data-phase RouterInfo block, so a fragment is
	 * never parsed as a whole RouterInfo.
	 */
	constexpr uint8_t SSU2_ROUTER_INFO_FRAG_SINGLE = 0x01;

	constexpr bool IsSSU2RouterInfoFragmentAcceptable (uint8_t frag)
	{
		return frag == SSU2_ROUTER_INFO_FRAG_SINGLE;
	}

	/**
	 * Mint a relay tag. Zero means "no tag" in this implementation, so
	 * zero is never published: at most `attempts` draws are made and 0 is
	 * returned when every draw produced zero (the caller then publishes
	 * nothing).
	 */
	template <typename NextValue> uint32_t MintSSU2RelayTag (NextValue && next, unsigned int attempts = 4)
	{
		while (attempts--)
		{
			const uint32_t tag = static_cast<uint32_t> (next ());
			if (tag) return tag;
		}
		return 0;
	}

	/**
	 * Whether a received RelayTag block may set our tag. The first
	 * non-zero tag wins; a later block is ignored so the tag already
	 * published to our introducers never changes underneath them.
	 */
	constexpr bool AcceptSSU2RelayTag (uint32_t current, uint32_t received)
	{
		return !current && received != 0;
	}

	/**
	 * States that an unauthenticated inbound packet can put a session in
	 * before the handshake completes. They are the entries counted against
	 * SSU2_MAX_INBOUND_SESSIONS.
	 */
	constexpr bool IsSSU2InboundHandshakeState (SSU2SessionState state)
	{
		switch (state)
		{
		case eSSU2SessionStateTokenRequestReceived:
		case eSSU2SessionStateSessionRequestReceived:
		case eSSU2SessionStateSessionCreatedSent: return true;
		default: return false;
		}
	}

	/**
	 * Fixed-window state for the per-source PeerTest msg 1/2 rate limit.
	 */
	struct SSU2PeerTestRateState
	{
		uint64_t windowStarted = 0; // in seconds
		unsigned int count = 0;
	};

	/**
	 * Charge one PeerTest msg 1/2 against a per-source window.
	 *
	 * Returns true when the message may be processed. The window is fixed:
	 * the first message opens it, every admitted message increments the
	 * counter, and the first message after the window expires opens a new
	 * one. A backwards clock jump also restarts the window instead of
	 * blocking the source until it catches up.
	 */
	constexpr bool AllowSSU2PeerTestMessage (SSU2PeerTestRateState & state, uint64_t now,
		size_t limit = SSU2_PEER_TEST_RATE_LIMIT, uint64_t window = SSU2_PEER_TEST_RATE_WINDOW)
	{
		if (!state.windowStarted || now < state.windowStarted || now - state.windowStarted >= window)
		{
			state.windowStarted = now;
			state.count = 1;
			return true;
		}
		if (state.count >= limit) return false;
		state.count++;
		return true;
	}

	/**
	 * Key for the PeerTest per-source limiter: IPv4 /32 and IPv6 /64.
	 *
	 * Java PeerTestManager keys the limiter on the source prefix, so a
	 * peer cannot bypass it by rotating the interface identifier inside
	 * one IPv6 /64. The same prefix rule is used by SamePeerTestAddress().
	 */
	inline boost::asio::ip::address GetSSU2PeerTestRateKey (const boost::asio::ip::address & address)
	{
		if (!address.is_v6 ()) return address;
		auto bytes = address.to_v6 ().to_bytes ();
		std::fill (bytes.begin () + 8, bytes.end (), 0);
		return boost::asio::ip::address_v6 (bytes);
	}

	/**
	 * Whether PeerTest msg 4 may open msg 6 toward the Charlie endpoint
	 * that the msg 4 signed data reports. The endpoint must be routable
	 * (Java isValidPeerTestEndpoint), supported by this router and not
	 * operator-blocked.
	 */
	constexpr bool AllowSSU2PeerTestTarget (bool endpointValid, bool addressSupported, bool operatorBlocked)
	{
		return endpointValid && addressSupported && !operatorBlocked;
	}

	/**
	 * PathChallenge is only meaningful inside an established data session.
	 *
	 * Responding earlier would let a pre-authentication or closing packet
	 * make us emit a PathResponse (with an Address block) toward an
	 * endpoint that has not completed the handshake. Upstream 472ef88b
	 * applies the same guard at dispatch; SendPathResponse() keeps its own
	 * check as defence in depth.
	 */
	inline bool IsSSU2PathChallengeResponseAllowed (SSU2SessionState state)
	{
		return state == eSSU2SessionStateEstablished;
	}

	/**
	 * Delay before the next PathChallenge retransmission.
	 *
	 * `sent` is the number of challenges already sent, including the
	 * initial one, so the schedule is 5 s, 10 s, 20 s. PeerState2 uses the
	 * same `PATH_CHALLENGE_DELAY << count` schedule and stops at the
	 * overall deadline before the 40 s step is reached.
	 */
	constexpr uint64_t GetSSU2PathChallengeRetryDelay (unsigned int sent)
	{
		// Clamp the shift so a corrupted counter cannot invoke undefined
		// behaviour. The send/deadline bounds end the migration long before
		// this matters.
		const unsigned int shift = sent > 4 ? 4 : (sent ? sent - 1 : 0);
		return SSU2_PATH_CHALLENGE_DELAY << shift;
	}

	/**
	 * Whether pending connection migration must be abandoned. Java checks
	 * the strict `now > started + MAX_PATH_CHALLENGE_TIME` and send-count
	 * bounds. Expiry never terminates the SSU2 session; it keeps the old
	 * endpoint.
	 */
	constexpr bool IsSSU2PathChallengeExpired (uint64_t now, uint64_t started, unsigned int sent)
	{
		return now > started + SSU2_MAX_PATH_CHALLENGE_TIME || sent > SSU2_MAX_PATH_CHALLENGE_SENDS;
	}

	/**
	 * Pending connection migration state, matching PeerState2's
	 * MIGRATION_STATE_PENDING fields. A single challenge value is reused
	 * for every retransmission, and only a PathResponse from the pending
	 * endpoint carrying that value may move the session.
	 */
	struct SSU2PathChallengeState
	{
		uint64_t challenge;
		boost::asio::ip::udp::endpoint endpoint;
		uint64_t startedTime; // in milliseconds
		uint64_t nextSendTime; // in milliseconds
		unsigned int numSends;
		size_t previousWindowSize;
		size_t previousMaxPayloadSize;
	};

	/**
	 * Pure framing and block-whitelist validator for decrypted SSU2
	 * payloads. It never touches sockets, sessions or global router state,
	 * so fuzzers and unit tests can exercise the state matrix directly.
	 *
	 * Handshake-specific rules (DateTime must be first) remain in
	 * ProcessSessionRequest, ProcessSessionCreated, ProcessTokenRequest,
	 * and ProcessRetry.
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
		eSSU2PeerTestCodeBobAddress = 5,
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
		eSSU2RelayResponseCodeCharlieLimit = 66,
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
		// A reordered peer can otherwise queue an unbounded chain for one
		// reassembly entry. The byte budget charges the fixed fragment slab
		// (each Fragment owns a full SSU2_MAX_PACKET_SIZE buffer) and is
		// capped at one maximum-size I2NP message.
		size_t outOfSequenceBytes = 0;
		size_t numOutOfSequenceFragments = 0;

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
	/// UDP transport session owning SSU2 handshake and data-phase state.
	/// Packet acknowledgements, retransmissions and fragment reassembly
	/// are session-specific; the server must outlive the session that
	/// refers to it.
	/// @see https://i2p.net/en/docs/specs/ssu2/
	class SSU2Session : public TransportSession, public std::enable_shared_from_this<SSU2Session>
	{
		friend class SSU2Server; // service-thread shutdown/expiry calls Terminate()
	protected:
		/// First 16 bytes of an SSU2 long header, in wire order. `connID` is
		/// the destination connection ID, `packetNum`/`type` follow, then
		/// version, netID and the flag byte pack into `flags[0..2]`. The
		/// whole block is XOR-masked with CreateHeaderMask() before it is
		/// sent, so a parsed value is only meaningful once the mask is
		/// removed.
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

		/**
		 * Which side of an introduction a pending relay nonce belongs to.
		 *
		 * The role used to be inferred from the stored session's
		 * IsEstablished() state, which can mis-forward a RelayResponse when
		 * the stored session establishes or closes between the request and
		 * the response. Store it with the pending entry instead.
		 */
		enum SSU2RelayRole : uint8_t
		{
			eSSU2RelayRoleBob = 0, // we are Bob: forward Charlie's response to Alice
			eSSU2RelayRoleAlice = 1 // we are Alice: wait for and process the response
		};

		struct RelaySession
		{
			std::shared_ptr<SSU2Session> session;
			uint64_t timestamp; // in seconds
			SSU2RelayRole role;
		};

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
		// `m_Address` is written by the service thread (SessionConfirmed,
		// RouterInfo update, peer test) and read from other threads through
		// GetAddress(), so the accessors copy under the mutex. Internal
		// service-thread code may still read the member directly.
		std::shared_ptr<const i2p::data::RouterInfo::Address> GetAddress () const
		{
			std::lock_guard<std::mutex> l (m_AddressMutex);
			return m_Address;
		};
		void SetOnEstablished (OnEstablished e)
		{
			std::lock_guard<std::mutex> l (m_OnEstablishedMutex);
			m_OnEstablished = std::move (e);
		};
		// Append instead of replacing, so a peer-test or introduction
		// callback registered while another callback is pending is not lost.
		void AddOnEstablished (OnEstablished e);
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
		/// Ask for an orderly close. In an established/closing session this
		/// sends an ACK+Termination frame and moves to Closing; in every
		/// other state there is no authenticated channel to notify, so it
		/// drops the session immediately. It never blocks and never waits for
		/// the peer's reply.
		void RequestTermination (SSU2TerminationReason reason);
		void CleanUp (uint64_t ts);
		void FlushData ();
		void Done () override;
		void SendLocalRouterInfo (bool update) override;
		void SendI2NPMessages (std::list<std::shared_ptr<I2NPMessage>> & msgs) override;
		/// Re-offer this session's unsent queue to a replacement session for
		/// the same peer. Only messages whose expiration has not passed are
		/// moved; the rest are dropped (their `onDrop` runs) so a timed-out
		/// message is never delivered later. The source queue is emptied
		/// either way.
		void MoveSendQueue (std::shared_ptr<SSU2Session> other);
		uint32_t GetRelayTag () const override { return m_RelayTag; };
		size_t Resend (uint64_t ts); // return number of resent packets
		uint64_t GetLastResendTime () const { return m_LastResendTime; };
		bool IsEstablished () const override { return m_State == eSSU2SessionStateEstablished; };
		bool IsTerminated () const { return m_Terminated.load (std::memory_order_acquire); }
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
		/// Handle one data packet; the payload is authenticated by the AEAD
		/// tag before any handler runs.
		/// @param from Sender endpoint of this datagram. It is
		///             the only source accepted for the PathResponse that
		///             completes a pending connection migration, so a
		///             replayed or forwarded packet cannot move the session.
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

		void SetAddress (std::shared_ptr<const i2p::data::RouterInfo::Address> addr)
		{
			std::lock_guard<std::mutex> l (m_AddressMutex);
			m_Address = std::move (addr);
		}
		// `from` is the authenticated source of a data-phase packet. It is
		// required before accepting a PathResponse for pending migration.
		void HandlePayload (const uint8_t * buf, size_t len, const boost::asio::ip::udp::endpoint * from = nullptr);
		bool HasInvalidPayload () const { return m_IsInvalidMessage; }
		virtual bool IsPayloadBlockAllowed (uint8_t /* block */) const { return true; }

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
		// Drops handshake crypto state that is bound to one negotiated
		// version (Noise state, ephemeral X25519 pair, ML-KEM keys) before a
		// Retry starts a new attempt.
		void ResetHandshakeCryptoState ();
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
		bool SendPathChallengePacket (const SSU2PathChallengeState & state);
		void RetransmitPathChallenge (uint64_t now);
		void CancelPathChallenge (bool migrated);

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
		mutable std::mutex m_AddressMutex; // guards m_Address for GetAddress()/SetAddress()
		boost::asio::ip::udp::endpoint m_RemoteEndpoint;
		i2p::data::RouterInfo::CompatibleTransports m_RemoteTransports, m_RemotePeerTestTransports;
		int m_RemoteVersion;
		uint64_t m_DestConnID, m_SourceConnID;
		std::atomic<SSU2SessionState> m_State;
		std::atomic<bool> m_Terminated { false };
		uint8_t m_KeyDataSend[64] = { }, m_KeyDataReceive[64] = { };
		uint32_t m_SendPacketNum, m_ReceivePacketNum, m_LastDatetimeSentPacketNum;
		std::set<uint32_t> m_OutOfSequencePackets; // packet nums > receive packet num
		std::map<uint32_t, std::shared_ptr<SSU2SentPacket>> m_SentPackets; // packetNum -> packet
		std::unordered_map<uint32_t, std::shared_ptr<SSU2IncompleteMessage>> m_IncompleteMessages; // msgID -> I2NP
		std::unordered_map<uint32_t, RelaySession>
			m_RelaySessions; // nonce -> pending relay introduction (role, target, timestamp)
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
		std::unique_ptr<SSU2PathChallengeState> m_PathChallenge;
		std::unordered_map<uint32_t, uint32_t> m_ReceivedI2NPMsgIDs; // msgID -> timestamp in seconds
		uint64_t m_LastResendTime, m_LastResendAttemptTime, m_NextRouterInfoResendTime; // in milliseconds
		int m_NumRanges;
		uint8_t m_Ranges[(SSU2_MAX_NUM_ACK_RANGES + 1) * 2]; // ranges sent with previous Ack if any
	};

	/// Obfuscation mask for one 8-byte half of an SSU2 header.
	///
	/// Eight zero bytes are encrypted with ChaCha20 under @a kh (the
	/// peer's introduction key, or the per-session header key) and the
	/// 12-byte
	/// @a nonce taken from the tail of the encrypted payload. XOR-ing the
	/// result into `header.ll[0]`/`header.ll[1]` both hides and restores
	/// the field, and an observer cannot recompute the mask without the
	/// key.
	inline uint64_t CreateHeaderMask (const uint8_t * kh, const uint8_t * nonce)
	{
		uint64_t data = 0;
		i2p::crypto::ChaCha20 ((uint8_t *)&data, 8, kh, nonce, (uint8_t *)&data);
		return data;
	}

	/// Build the 12-byte AEAD nonce for a data packet: four zero bytes
	/// followed by the packet number in little-endian order. The packet
	/// number is unique per key direction, so nonces never repeat inside
	/// one session.
	inline void CreateNonce (uint64_t seqn, uint8_t * nonce)
	{
		memset (nonce, 0, 4);
		htole64buf (nonce + 4, seqn);
	}
}
}

#endif
