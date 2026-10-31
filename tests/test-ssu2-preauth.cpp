/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test ssu2 preauth.
 *
 * Enforces SSU2 pre-authentication boundaries: handshake states accept
 * only whitelisted blocks, PeerTest/I2NP/Address handlers require an
 * established session with a remote identity, PathChallenge is answered
 * only from an established session, data-phase blocks are rejected
 * before authentication, and an unauthenticated Retry cannot downgrade
 * the negotiated PQ version.
 *
 */

#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <vector>

#include "SSU2Session.h"

namespace
{
using namespace i2p::transport;

bool IsBlockIn (uint8_t block, std::initializer_list<uint8_t> allowed)
{
	for (auto value : allowed)
		if (value == block) return true;
	return false;
}

void ExpectWhitelist (SSU2SessionState state, std::initializer_list<uint8_t> allowed)
{
	for (int block = 0; block < 256; block++)
		assert (IsSSU2PayloadBlockAllowedForState (state, static_cast<uint8_t> (block))
			== IsBlockIn (static_cast<uint8_t> (block), allowed));
}

std::vector<uint8_t> MakeBlock (uint8_t type, size_t size, uint8_t fill = 0)
{
	std::vector<uint8_t> block;
	block.push_back (type);
	block.push_back (static_cast<uint8_t> (size >> 8));
	block.push_back (static_cast<uint8_t> (size));
	block.insert (block.end (), size, fill);
	return block;
}

std::vector<uint8_t> Concat (std::initializer_list<std::vector<uint8_t>> parts)
{
	std::vector<uint8_t> out;
	for (const auto & part : parts) out.insert (out.end (), part.begin (), part.end ());
	return out;
}

void TestStateBlockMatrix ()
{
	ExpectWhitelist (eSSU2SessionStateTokenRequestReceived, { eSSU2BlkDateTime, eSSU2BlkPadding, eSSU2BlkTermination });
	ExpectWhitelist (eSSU2SessionStateSessionRequestReceived,
		{ eSSU2BlkDateTime, eSSU2BlkPadding, eSSU2BlkRelayTagRequest, eSSU2BlkTermination });
	ExpectWhitelist (eSSU2SessionStateSessionCreatedReceived,
		{ eSSU2BlkDateTime, eSSU2BlkAddress, eSSU2BlkNewToken, eSSU2BlkRelayTag, eSSU2BlkPadding });
	ExpectWhitelist (
		eSSU2SessionStateTokenReceived, { eSSU2BlkDateTime, eSSU2BlkAddress, eSSU2BlkPadding, eSSU2BlkTermination });
	ExpectWhitelist (eSSU2SessionStateSessionCreatedSent, { eSSU2BlkRouterInfo, eSSU2BlkOptions, eSSU2BlkPadding });

	// Established data phase keeps the complete block set.
	for (int block = 0; block < 256; block++)
		assert (IsSSU2PayloadBlockAllowedForState (eSSU2SessionStateEstablished, static_cast<uint8_t> (block)));
}

void TestPreAuthHandlerGuards ()
{
	// HandlePeerTest (msgs 1-4) and HandleI2NPMsg only run in the
	// established data phase and only with a known peer identity.
	assert (IsSSU2EstablishedPayloadContext (eSSU2SessionStateEstablished, true));
	assert (!IsSSU2EstablishedPayloadContext (eSSU2SessionStateEstablished, false));
	for (auto state :
		{ eSSU2SessionStateUnknown, eSSU2SessionStateTokenReceived, eSSU2SessionStateSessionRequestSent,
			eSSU2SessionStateSessionRequestReceived, eSSU2SessionStateSessionCreatedSent,
			eSSU2SessionStateSessionCreatedReceived, eSSU2SessionStateSessionConfirmedSent, eSSU2SessionStateIntroduced,
			eSSU2SessionStateHolePunch, eSSU2SessionStatePeerTest, eSSU2SessionStateTokenRequestReceived })
	{
		assert (!IsSSU2EstablishedPayloadContext (state, true));
		assert (!IsSSU2EstablishedPayloadContext (state, false));
	}

	// HandleAddress also accepts the two authenticated pre-data contexts
	// that legitimately carry an Address block, but never without an
	// identity.
	assert (IsSSU2AddressBlockContextAllowed (eSSU2SessionStateEstablished, true));
	assert (IsSSU2AddressBlockContextAllowed (eSSU2SessionStateSessionCreatedReceived, true));
	assert (IsSSU2AddressBlockContextAllowed (eSSU2SessionStateTokenReceived, true));
	assert (!IsSSU2AddressBlockContextAllowed (eSSU2SessionStateEstablished, false));
	assert (!IsSSU2AddressBlockContextAllowed (eSSU2SessionStateSessionCreatedReceived, false));
	assert (!IsSSU2AddressBlockContextAllowed (eSSU2SessionStateTokenReceived, false));
	for (auto state :
		{ eSSU2SessionStateUnknown, eSSU2SessionStateSessionRequestSent, eSSU2SessionStateSessionRequestReceived,
			eSSU2SessionStateSessionCreatedSent, eSSU2SessionStateSessionConfirmedSent, eSSU2SessionStateIntroduced,
			eSSU2SessionStateHolePunch, eSSU2SessionStatePeerTest, eSSU2SessionStateTokenRequestReceived })
		assert (!IsSSU2AddressBlockContextAllowed (state, true));
}

void TestPathChallengeStateMatrix ()
{
	// PathChallenge may only be answered from an established data session.
	assert (IsSSU2PathChallengeResponseAllowed (eSSU2SessionStateEstablished));
	for (auto state : { eSSU2SessionStateUnknown, eSSU2SessionStateTokenReceived, eSSU2SessionStateSessionRequestSent,
			 eSSU2SessionStateSessionRequestReceived, eSSU2SessionStateSessionCreatedSent,
			 eSSU2SessionStateSessionCreatedReceived, eSSU2SessionStateSessionConfirmedSent,
			 eSSU2SessionStateClosingConfirmed, eSSU2SessionStateTerminated, eSSU2SessionStateFailed,
			 eSSU2SessionStateIntroduced, eSSU2SessionStateHolePunch, eSSU2SessionStatePeerTest,
			 eSSU2SessionStateTokenRequestReceived })
		assert (!IsSSU2PathChallengeResponseAllowed (state));

	// Closing is not a data phase: the established-only guard must not be
	// widened to match the termination whitelist.
	assert (!IsSSU2PathChallengeResponseAllowed (eSSU2SessionStateClosing));
}

void TestPathChallengeRetransmissionSchedule ()
{
	// Java PeerState2: initial send, then retries after 5 s, 10 s, 20 s
	// and a 30 s overall deadline. The delay doubles after every send.
	assert (SSU2_MAX_PATH_CHALLENGE_SENDS == 4);
	assert (SSU2_PATH_CHALLENGE_DELAY == 5000);
	assert (SSU2_MAX_PATH_CHALLENGE_TIME == 30000);
	assert (GetSSU2PathChallengeRetryDelay (1) == 5000);
	assert (GetSSU2PathChallengeRetryDelay (2) == 10000);
	assert (GetSSU2PathChallengeRetryDelay (3) == 20000);
	assert (GetSSU2PathChallengeRetryDelay (4) == 40000);
	// A corrupt counter must not shift out of range.
	assert (GetSSU2PathChallengeRetryDelay (0) == 5000);
	assert (GetSSU2PathChallengeRetryDelay (99) == 80000);

	// The deadline is strict and an exhausted send count also expires the
	// migration. Neither outcome terminates the session, so this helper is
	// the only decision involved in keeping the old address.
	const uint64_t started = 1000000;
	assert (!IsSSU2PathChallengeExpired (started, started, 1));
	assert (!IsSSU2PathChallengeExpired (started + SSU2_MAX_PATH_CHALLENGE_TIME, started, 1));
	assert (IsSSU2PathChallengeExpired (started + SSU2_MAX_PATH_CHALLENGE_TIME + 1, started, 1));
	// Exactly four sends are still allowed; only a fifth exceeds the
	// bound.
	assert (!IsSSU2PathChallengeExpired (started + 1, started, SSU2_MAX_PATH_CHALLENGE_SENDS));
	assert (IsSSU2PathChallengeExpired (started + 1, started, SSU2_MAX_PATH_CHALLENGE_SENDS + 1));

	// The 5/10/20 schedule cannot reach a fourth retry before the 30 s
	// deadline, so the expiry keeps the old endpoint without a fifth send.
	uint64_t next = started + GetSSU2PathChallengeRetryDelay (1);
	unsigned int sends = 1;
	while (!IsSSU2PathChallengeExpired (next, started, sends) && sends < SSU2_MAX_PATH_CHALLENGE_SENDS)
	{
		sends++;
		next += GetSSU2PathChallengeRetryDelay (sends);
	}
	assert (sends <= SSU2_MAX_PATH_CHALLENGE_SENDS);
	assert (next > started + SSU2_MAX_PATH_CHALLENGE_TIME);
}

void TestPayloadValidatorRejectsPreAuthInjection ()
{
	const auto dateTime = MakeBlock (eSSU2BlkDateTime, 4, 1);
	const auto padding = MakeBlock (eSSU2BlkPadding, 0);
	const auto address = MakeBlock (eSSU2BlkAddress, 6);
	const auto relayTagRequest = MakeBlock (eSSU2BlkRelayTagRequest, 0);
	const auto newToken = MakeBlock (eSSU2BlkNewToken, 12);
	const auto relayTag = MakeBlock (eSSU2BlkRelayTag, 4);
	const auto routerInfo = MakeBlock (eSSU2BlkRouterInfo, 2);
	const auto options = MakeBlock (eSSU2BlkOptions, 0);
	const auto i2np = MakeBlock (eSSU2BlkI2NPMessage, 9);
	const auto firstFragment = MakeBlock (eSSU2BlkFirstFragment, 10);
	const auto followOn = MakeBlock (eSSU2BlkFollowOnFragment, 6);
	const auto termination = MakeBlock (eSSU2BlkTermination, 9);

	const auto tokenRequest = Concat ({ dateTime, padding });
	assert (
		ValidateSSU2PayloadBlocks (eSSU2SessionStateTokenRequestReceived, tokenRequest.data (), tokenRequest.size ()));
	for (const auto * forbidden : { &address, &i2np, &firstFragment, &routerInfo })
		assert (
			!ValidateSSU2PayloadBlocks (eSSU2SessionStateTokenRequestReceived, forbidden->data (), forbidden->size ()));

	const auto sessionRequest = Concat ({ dateTime, relayTagRequest, padding });
	assert (ValidateSSU2PayloadBlocks (
		eSSU2SessionStateSessionRequestReceived, sessionRequest.data (), sessionRequest.size ()));
	for (const auto * forbidden : { &address, &i2np, &firstFragment, &newToken })
		assert (!ValidateSSU2PayloadBlocks (
			eSSU2SessionStateSessionRequestReceived, forbidden->data (), forbidden->size ()));

	const auto sessionCreated = Concat ({ dateTime, address, newToken, relayTag, padding });
	assert (ValidateSSU2PayloadBlocks (
		eSSU2SessionStateSessionCreatedReceived, sessionCreated.data (), sessionCreated.size ()));
	for (const auto * forbidden : { &i2np, &firstFragment, &followOn, &termination })
		assert (!ValidateSSU2PayloadBlocks (
			eSSU2SessionStateSessionCreatedReceived, forbidden->data (), forbidden->size ()));

	const auto retry = Concat ({ dateTime, address, padding });
	assert (ValidateSSU2PayloadBlocks (eSSU2SessionStateTokenReceived, retry.data (), retry.size ()));
	for (const auto * forbidden : { &i2np, &relayTagRequest, &newToken })
		assert (!ValidateSSU2PayloadBlocks (eSSU2SessionStateTokenReceived, forbidden->data (), forbidden->size ()));

	const auto sessionConfirmed = Concat ({ routerInfo, options, padding });
	assert (ValidateSSU2PayloadBlocks (
		eSSU2SessionStateSessionCreatedSent, sessionConfirmed.data (), sessionConfirmed.size ()));
	for (const auto * forbidden : { &i2np, &firstFragment, &followOn, &address })
		assert (
			!ValidateSSU2PayloadBlocks (eSSU2SessionStateSessionCreatedSent, forbidden->data (), forbidden->size ()));

	// In the data phase the same blocks are accepted.
	const auto dataPhase = Concat ({ dateTime, i2np, firstFragment, followOn, padding });
	assert (ValidateSSU2PayloadBlocks (eSSU2SessionStateEstablished, dataPhase.data (), dataPhase.size ()));

	// Framing and ordering rules are independent of the state whitelist.
	const std::vector<uint8_t> truncated { eSSU2BlkPadding, 0 };
	assert (!ValidateSSU2PayloadBlocks (eSSU2SessionStateTokenRequestReceived, truncated.data (), truncated.size ()));
	assert (!ValidateSSU2PayloadBlocks (eSSU2SessionStateTokenRequestReceived, nullptr, 1));
	const auto afterPadding = Concat ({ padding, padding });
	assert (
		!ValidateSSU2PayloadBlocks (eSSU2SessionStateTokenRequestReceived, afterPadding.data (), afterPadding.size ()));
	const auto afterTermination = Concat ({ termination, dateTime });
	assert (!ValidateSSU2PayloadBlocks (
		eSSU2SessionStateTokenRequestReceived, afterTermination.data (), afterTermination.size ()));
	const auto paddingAfterTermination = Concat ({ termination, padding });
	assert (ValidateSSU2PayloadBlocks (
		eSSU2SessionStateTokenRequestReceived, paddingAfterTermination.data (), paddingAfterTermination.size ()));
}

void TestRetryVersionAuthorisation ()
{
	// The Retry header "ver" byte is authenticated only with the peer's
	// published intro key, so a forged lower value must never be adopted.
	// The
	// signed RouterInfo "pq" option is the only authority.
	const uint8_t v2Only = GetSSU2AdvertisedVersionMask (0);
	const uint8_t v2v3 = GetSSU2AdvertisedVersionMask (1u << 3);
	const uint8_t v2v4 = GetSSU2AdvertisedVersionMask (1u << 4);
	const uint8_t v2v3v4 = GetSSU2AdvertisedVersionMask ((1u << 3) | (1u << 4));

	// Version 2 is the base version and is always advertised.
	assert (v2Only == (1u << 2));
	for (auto mask : { v2Only, v2v3, v2v4, v2v3v4 }) assert (mask & (1u << 2));
	assert (GetHighestSSU2Version (v2Only) == 2);
	assert (GetHighestSSU2Version (v2v3) == 3);
	assert (GetHighestSSU2Version (v2v4) == 4);
	assert (GetHighestSSU2Version (v2v3v4) == 4);

	// Adopting only the peer's advertised maximum is allowed.
	assert (IsSSU2RetryVersionAccepted (2, v2Only));
	assert (IsSSU2RetryVersionAccepted (3, v2v3));
	assert (IsSSU2RetryVersionAccepted (4, v2v3v4));
	assert (IsSSU2RetryVersionAccepted (3, v2v3v4) == false); // not the maximum
	assert (IsSSU2RetryVersionAccepted (2, v2v3v4) == false); // PQ downgrade
	assert (IsSSU2RetryVersionAccepted (2, v2v3) == false); // PQ downgrade
	assert (IsSSU2RetryVersionAccepted (3, v2v4) == false); // not advertised
	assert (IsSSU2RetryVersionAccepted (4, v2v3) == false); // not advertised
	for (int version = 5; version < 256; ++version) assert (!IsSSU2RetryVersionAccepted (version, v2v3v4));
	assert (!IsSSU2RetryVersionAccepted (0, v2Only));
	assert (!IsSSU2RetryVersionAccepted (1, v2Only));

	// The authenticated IncompatibleVersion fallback may only select a
	// version the peer advertised, and only ever downwards.
	assert (GetSSU2FallbackVersion (4, v2v3v4) == 3);
	assert (GetSSU2FallbackVersion (4, v2v4) == 2); // 3 was not advertised
	assert (GetSSU2FallbackVersion (3, v2v3) == 2);
	assert (GetSSU2FallbackVersion (3, v2v3v4) == 2);
	// Nothing left to fall back to: the attempt must fail, not force
	// version 2
	// for a peer that never advertised a lower one.
	assert (GetSSU2FallbackVersion (2, v2Only) == 0);
	assert (GetSSU2FallbackVersion (2, v2v3v4) == 0);
	assert (GetSSU2FallbackVersion (0, v2Only) == 0);
}

void TestAckHandshakeCoverage ()
{
	// The ACK covers [ackThrough - acnt, ackThrough] and packet 0 is the
	// SessionConfirmed. Only an ACK that reaches packet 0 may call
	// Established(), and the ACK ranges are processed first so packet 0 is
	// retired from the sent-packet map before the data phase starts.
	assert (IsSSU2AckCoveringPacketZero (0, 0));
	assert (IsSSU2AckCoveringPacketZero (5, 5));
	assert (!IsSSU2AckCoveringPacketZero (5, 4));
	assert (!IsSSU2AckCoveringPacketZero (5, 0));
	assert (!IsSSU2AckCoveringPacketZero (1, 2));
}

} // namespace

int main ()
{
	TestStateBlockMatrix ();
	TestPreAuthHandlerGuards ();
	TestPathChallengeStateMatrix ();
	TestPathChallengeRetransmissionSchedule ();
	TestPayloadValidatorRejectsPreAuthInjection ();
	TestRetryVersionAuthorisation ();
	TestAckHandshakeCoverage ();
	return 0;
}
