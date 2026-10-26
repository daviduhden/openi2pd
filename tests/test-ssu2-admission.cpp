/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test ssu2 admission.
 *
 * Checks the inbound handshake admission cap, the state set that it counts,
 * the connection-limit (reason 19) decision at the cap and the separate
 * PeerTest pseudo-session cap.
 *
 */

#include <cassert>
#include <cstdint>

#include "SSU2Session.h"

namespace
{
using namespace i2p::transport;

void TestInboundAdmissionCap ()
{
	// Named constant, deliberately not configurable.
	assert (SSU2_MAX_INBOUND_SESSIONS == 40);

	for (size_t n = 0; n <= SSU2_MAX_INBOUND_SESSIONS; ++n)
		assert (IsInboundSessionAdmissionAllowed (n) == (n < SSU2_MAX_INBOUND_SESSIONS));
	assert (!IsInboundSessionAdmissionAllowed (SSU2_MAX_INBOUND_SESSIONS + 1));
	assert (!IsInboundSessionAdmissionAllowed (1000));
}

void TestCountedStates ()
{
	// States an unauthenticated inbound packet can create before the
	// handshake completes are counted against the cap.
	assert (IsSSU2InboundHandshakeState (eSSU2SessionStateTokenRequestReceived));
	assert (IsSSU2InboundHandshakeState (eSSU2SessionStateSessionRequestReceived));
	assert (IsSSU2InboundHandshakeState (eSSU2SessionStateSessionCreatedSent));

	// Everything else is either pre-created (Unknown), authenticated or a
	// bounded pseudo-session with its own cap.
	for (auto state : { eSSU2SessionStateUnknown, eSSU2SessionStateTokenReceived, eSSU2SessionStateSessionRequestSent,
			 eSSU2SessionStateSessionCreatedReceived, eSSU2SessionStateSessionConfirmedSent,
			 eSSU2SessionStateEstablished, eSSU2SessionStateClosing, eSSU2SessionStateClosingConfirmed,
			 eSSU2SessionStateTerminated, eSSU2SessionStateFailed, eSSU2SessionStateIntroduced,
			 eSSU2SessionStateHolePunch, eSSU2SessionStatePeerTest })
		assert (!IsSSU2InboundHandshakeState (state));

	// The admission decision is a pure comparison of the counted value.
	size_t counted = 0;
	for (auto state : { eSSU2SessionStateSessionRequestReceived, eSSU2SessionStateSessionCreatedSent,
			 eSSU2SessionStateEstablished, eSSU2SessionStatePeerTest, eSSU2SessionStateSessionRequestReceived })
	{
		if (IsSSU2InboundHandshakeState (state)) counted++;
	}
	assert (counted == 3);
}

void TestConnectionLimitDecision ()
{
	// At the cap, a handshake context is answered with reason 19 and
	// everything else is dropped.
	assert (CanRejectInboundHandshakeWithRetry (static_cast<uint8_t> (eSSU2SessionRequest)));
	assert (CanRejectInboundHandshakeWithRetry (static_cast<uint8_t> (eSSU2TokenRequest)));
	assert (!CanRejectInboundHandshakeWithRetry (static_cast<uint8_t> (eSSU2SessionCreated)));
	assert (!CanRejectInboundHandshakeWithRetry (static_cast<uint8_t> (eSSU2SessionConfirmed)));
	assert (!CanRejectInboundHandshakeWithRetry (static_cast<uint8_t> (eSSU2Data)));
	assert (!CanRejectInboundHandshakeWithRetry (static_cast<uint8_t> (eSSU2PeerTest)));
	assert (!CanRejectInboundHandshakeWithRetry (static_cast<uint8_t> (eSSU2Retry)));
	assert (!CanRejectInboundHandshakeWithRetry (static_cast<uint8_t> (eSSU2HolePunch)));
	assert (!CanRejectInboundHandshakeWithRetry (0xFF));

	// The reason code itself is the SSU2 connection-limits value.
	assert (eSSU2TerminationReasonConnectionLimits == 19);
}

void TestPeerTestSessionCap ()
{
	assert (SSU2_MAX_PEER_TEST_SESSIONS == 64);
	for (size_t n = 0; n <= SSU2_MAX_PEER_TEST_SESSIONS; ++n)
		assert (IsSSU2PeerTestSessionAdmissionAllowed (n) == (n < SSU2_MAX_PEER_TEST_SESSIONS));
	assert (!IsSSU2PeerTestSessionAdmissionAllowed (SSU2_MAX_PEER_TEST_SESSIONS + 1));
}
} // namespace

int main ()
{
	TestInboundAdmissionCap ();
	TestCountedStates ();
	TestConnectionLimitDecision ();
	TestPeerTestSessionCap ();
	return 0;
}
