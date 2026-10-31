/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test transit tunnel build replay filter and message sizes.
 *
 * The transit build handlers must reject a replayed request and a
 * payload that carries trailing bytes. Deterministic: the filter is
 * driven with explicit clocks and the size predicates are pure
 * functions.
 *
 */

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>

#include "I2NPProtocol.h"
#include "TransitTunnel.h"

namespace
{
using namespace i2p::tunnel;

std::array<uint8_t, 32> MakeKey (uint8_t fill)
{
	std::array<uint8_t, 32> key { };
	key.fill (fill);
	return key;
}

void TestExactBuildMessageSizes ()
{
	assert (GetShortTunnelBuildMessageSize (1) == 1 + SHORT_TUNNEL_BUILD_RECORD_SIZE);
	assert (GetShortTunnelBuildMessageSize (STANDARD_NUM_RECORDS) == 1 + 4 * SHORT_TUNNEL_BUILD_RECORD_SIZE);
	assert (GetVariableTunnelBuildMessageSize (MAX_NUM_RECORDS) == 1 + 8 * TUNNEL_BUILD_RECORD_SIZE);

	assert (IsExactTunnelBuildMessageSize (1 + 4 * SHORT_TUNNEL_BUILD_RECORD_SIZE, 4, true));
	assert (!IsExactTunnelBuildMessageSize (1 + 4 * SHORT_TUNNEL_BUILD_RECORD_SIZE + 1, 4, true));
	assert (!IsExactTunnelBuildMessageSize (1 + 4 * SHORT_TUNNEL_BUILD_RECORD_SIZE - 1, 4, true));
	assert (IsExactTunnelBuildMessageSize (1 + 8 * TUNNEL_BUILD_RECORD_SIZE, 8, false));
	assert (!IsExactTunnelBuildMessageSize (1 + 8 * TUNNEL_BUILD_RECORD_SIZE + 1, 8, false));
	// A zero or out-of-range record count is never a valid size.
	assert (!IsExactTunnelBuildMessageSize (1, 0, true));
	assert (!IsExactTunnelBuildMessageSize (1, -1, false));
	assert (!IsExactTunnelBuildMessageSize (1 + 9 * TUNNEL_BUILD_RECORD_SIZE, MAX_NUM_RECORDS + 1, false));
}

void TestReplayWindow ()
{
	TransitBuildReplayFilter filter;
	const uint64_t windowMs = TRANSIT_TUNNEL_BUILD_REPLAY_WINDOW_MS;
	const auto keyA = MakeKey (0xA1);
	const auto keyB = MakeKey (0xB2);
	const uint64_t t0 = 1000000;

	assert (!filter.CheckAndStore (nullptr, t0));
	assert (filter.CheckAndStore (keyA.data (), t0));
	assert (!filter.CheckAndStore (keyA.data (), t0 + 1)); // replay
	assert (filter.GetSize () == 1);
	assert (filter.CheckAndStore (keyB.data (), t0 + 2));
	assert (filter.GetSize () == 2);

	// Inside the window the key is still a replay.
	assert (!filter.CheckAndStore (keyA.data (), t0 + windowMs - 1));
	// Once the window passed the same key is a fresh request again.
	assert (filter.CheckAndStore (keyA.data (), t0 + windowMs));
	assert (!filter.CheckAndStore (keyA.data (), t0 + windowMs + 1));

	filter.Clear ();
	assert (filter.GetSize () == 0);
	assert (filter.CheckAndStore (keyA.data (), t0));
}

void TestReplayTableIsBounded ()
{
	// A hostile peer must not be able to grow the table without limit; the
	// oldest entry is evicted, never the newest.
	TransitBuildReplayFilter filter (TRANSIT_TUNNEL_BUILD_REPLAY_WINDOW_MS, 2);
	const auto key1 = MakeKey (1);
	const auto key2 = MakeKey (2);
	const auto key3 = MakeKey (3);
	assert (filter.CheckAndStore (key1.data (), 10));
	assert (filter.CheckAndStore (key2.data (), 11));
	assert (filter.CheckAndStore (key3.data (), 12));
	assert (filter.GetSize () == 2);
	assert (filter.CheckAndStore (key1.data (), 13)); // evicted, accepted again
	assert (filter.GetSize () == 2);
}

void TestBuildExpirations ()
{
	// Java BuildHandler: a reply for a tunnel we created may take a full
	// round trip, a relayed request only has to reach the next hop. Both
	// are longer than the generic 8 s I2NP expiration.
	assert (TUNNEL_BUILD_REPLY_EXPIRATION_MS == 60 * 1000);
	assert (TUNNEL_BUILD_RELAY_EXPIRATION_MS == 25 * 1000);
	assert (TUNNEL_BUILD_REPLY_EXPIRATION_MS > i2p::I2NP_MESSAGE_EXPIRATION_TIMEOUT);
	assert (TUNNEL_BUILD_RELAY_EXPIRATION_MS > i2p::I2NP_MESSAGE_EXPIRATION_TIMEOUT);
	assert (TUNNEL_BUILD_RELAY_EXPIRATION_MS < TUNNEL_BUILD_REPLY_EXPIRATION_MS);
}
}

int main ()
{
	TestExactBuildMessageSizes ();
	TestReplayWindow ();
	TestReplayTableIsBounded ();
	TestBuildExpirations ();
	return 0;
}
