/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test ssu2 peertest policy.
 *
 * Checks the pure PeerTest policy helpers: the per-source msg 1/2 rate
 * limit,
 * the Bob nonce-table cap that never evicts live state, the msg 4
 * target
 * validation, the msg 5/7 external-address rule, nonzero first-wins
 * relay
 * tags, the ACK-0 predicate and the accepted out-of-session versions.
 *
 */

#include <cassert>
#include <cstdint>

#include <boost/asio/ip/address.hpp>

#include "SSU2OutOfSession.h"
#include "SSU2Session.h"

namespace
{
using namespace i2p::transport;

void TestPeerTestRateLimit ()
{
	assert (SSU2_PEER_TEST_RATE_LIMIT == 12);
	assert (SSU2_PEER_TEST_RATE_WINDOW == 600);

	// Java admits 12 PeerTest msgs 1/2 per source prefix and 10 minutes.
	SSU2PeerTestRateState state;
	const uint64_t start = 1000000;
	for (unsigned int i = 0; i < SSU2_PEER_TEST_RATE_LIMIT; ++i) assert (AllowSSU2PeerTestMessage (state, start));
	assert (state.count == SSU2_PEER_TEST_RATE_LIMIT);
	assert (!AllowSSU2PeerTestMessage (state, start));
	// The window is still open one second before it expires.
	assert (!AllowSSU2PeerTestMessage (state, start + SSU2_PEER_TEST_RATE_WINDOW - 1));
	// The first message at/after the window boundary opens a fresh window.
	assert (AllowSSU2PeerTestMessage (state, start + SSU2_PEER_TEST_RATE_WINDOW));
	assert (state.count == 1);
	assert (state.windowStarted == start + SSU2_PEER_TEST_RATE_WINDOW);

	// A backwards clock jump restarts the window instead of blocking the
	// source until the wall clock catches up again.
	SSU2PeerTestRateState jumped;
	for (unsigned int i = 0; i < SSU2_PEER_TEST_RATE_LIMIT; ++i) assert (AllowSSU2PeerTestMessage (jumped, start));
	assert (!AllowSSU2PeerTestMessage (jumped, start));
	assert (AllowSSU2PeerTestMessage (jumped, start - 10));
	assert (jumped.count == 1);

	// An explicit limit/window pair is honoured (used by future tuning).
	SSU2PeerTestRateState custom;
	assert (AllowSSU2PeerTestMessage (custom, 1000, 1, 10));
	assert (custom.windowStarted == 1000);
	assert (!AllowSSU2PeerTestMessage (custom, 1005, 1, 10));
	assert (AllowSSU2PeerTestMessage (custom, 1010, 1, 10));
}

void TestPeerTestRateKey ()
{
	const auto v4a = boost::asio::ip::make_address ("1.2.3.4");
	const auto v4b = boost::asio::ip::make_address ("1.2.3.5");
	assert (GetSSU2PeerTestRateKey (v4a) == v4a);
	assert (GetSSU2PeerTestRateKey (v4a) != GetSSU2PeerTestRateKey (v4b));

	// IPv6 is keyed on the /64 prefix, so rotating the interface
	// identifier
	// inside one prefix cannot reset the limiter.
	const auto v6a = boost::asio::ip::make_address ("2001:db8:1:2::1");
	const auto v6b = boost::asio::ip::make_address ("2001:db8:1:2:ffff:ffff:ffff:ffff");
	const auto v6c = boost::asio::ip::make_address ("2001:db8:1:3::1");
	assert (GetSSU2PeerTestRateKey (v6a) == GetSSU2PeerTestRateKey (v6b));
	assert (GetSSU2PeerTestRateKey (v6a) != GetSSU2PeerTestRateKey (v6c));
	assert (GetSSU2PeerTestRateKey (v6a).is_v6 ());
	assert (GetSSU2PeerTestRateKey (v4a).is_v4 ());
}

void TestBobNonceTableCap ()
{
	// Bob must reject at the cap and never evict another test's live
	// state.
	assert (!IsSSU2PeerTestTableFull (0, 64));
	assert (!IsSSU2PeerTestTableFull (63, 64));
	assert (IsSSU2PeerTestTableFull (64, 64));
	assert (IsSSU2PeerTestTableFull (65, 64));

	// The admission result order used by the msg 1 handler: a duplicate is
	// reported without touching the table, a full table is a limit error.
	assert (static_cast<uint8_t> (SSU2PeerTestAdmission::Accepted) == 0);
	assert (SSU2PeerTestAdmission::Duplicate != SSU2PeerTestAdmission::LimitExceeded);
}

void TestMsg4TargetValidation ()
{
	// The Charlie endpoint from msg 4 must be routable, supported by this
	// router and not operator-blocked before msg 6 is opened.
	assert (AllowSSU2PeerTestTarget (true, true, false));
	assert (!AllowSSU2PeerTestTarget (false, true, false)); // invalid endpoint
	assert (!AllowSSU2PeerTestTarget (true, false, false)); // unsupported family
	assert (!AllowSSU2PeerTestTarget (true, true, true)); // operator-blocked
	assert (!AllowSSU2PeerTestTarget (false, false, false));
}

void TestExternalAddressRule ()
{
	// Java never publishes an address from msg 5, and msg 7 only counts
	// when
	// it comes from the Charlie learned in msg 4.
	assert (!AllowPeerTestExternalAddressUpdate (5, false));
	assert (!AllowPeerTestExternalAddressUpdate (5, true));
	assert (!AllowPeerTestExternalAddressUpdate (6, true));
	assert (!AllowPeerTestExternalAddressUpdate (7, false));
	assert (AllowPeerTestExternalAddressUpdate (7, true));
	// Messages outside the 5..7 range never update the address.
	assert (!AllowPeerTestExternalAddressUpdate (4, true));
	assert (!AllowPeerTestExternalAddressUpdate (8, true));
}

void TestRelayTagMinting ()
{
	// Zero means "no tag", so zero draws are skipped up to the attempt
	// bound.
	uint32_t draws[] = { 0, 0, 0x1234 };
	size_t index = 0;
	assert (MintSSU2RelayTag ([&] () { return draws[index++]; }) == 0x1234);
	assert (index == 3);

	uint32_t zeros[] = { 0, 0, 0, 0, 0x55 };
	index = 0;
	assert (MintSSU2RelayTag ([&] () { return zeros[index++]; }) == 0);
	assert (index == 4); // the attempt bound is respected

	index = 0;
	assert (MintSSU2RelayTag ([&] () { return 0xABCD; }) == 0xABCD);
	assert (index == 0); // returned on the first draw

	// First tag wins: a later block never replaces a published tag.
	assert (AcceptSSU2RelayTag (0, 7));
	assert (!AcceptSSU2RelayTag (7, 8));
	assert (!AcceptSSU2RelayTag (0, 0));
	assert (!AcceptSSU2RelayTag (7, 7));
}

void TestAckZeroPredicate ()
{
	// Only an ACK that counts back to packet 0 confirms SessionConfirmed.
	assert (IsSSU2AckCoveringPacketZero (0, 0));
	assert (IsSSU2AckCoveringPacketZero (1, 1));
	assert (IsSSU2AckCoveringPacketZero (3, 3));
	assert (!IsSSU2AckCoveringPacketZero (3, 2));
	assert (!IsSSU2AckCoveringPacketZero (3, 0));
	assert (!IsSSU2AckCoveringPacketZero (300, 255)); // acnt is one byte on the wire
}

void TestOutOfSessionVersion ()
{
	// PeerTest/HolePunch always send 2 but accept 2..4 so a PQ peer is not
	// rejected for advertising its negotiated version.
	assert (!IsSSU2OutOfSessionVersionAccepted (0));
	assert (!IsSSU2OutOfSessionVersionAccepted (1));
	assert (IsSSU2OutOfSessionVersionAccepted (2));
	assert (IsSSU2OutOfSessionVersionAccepted (3));
	assert (IsSSU2OutOfSessionVersionAccepted (4));
	assert (!IsSSU2OutOfSessionVersionAccepted (5));
	assert (!IsSSU2OutOfSessionVersionAccepted (255));
}
} // namespace

int main ()
{
	TestPeerTestRateLimit ();
	TestPeerTestRateKey ();
	TestBobNonceTableCap ();
	TestMsg4TargetValidation ();
	TestExternalAddressRule ();
	TestRelayTagMinting ();
	TestAckZeroPredicate ();
	TestOutOfSessionVersion ();
	return 0;
}
