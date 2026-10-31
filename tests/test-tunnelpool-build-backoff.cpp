/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test tunnel pool build accounting and backoff.
 *
 * Checks the hop/variance clamp, the Java-like build backoff ladder,
 * the per-tunnel test deadline and the in-progress build accounting
 * that stops a pool from oversubscribing a slow build round.
 * Deterministic: the pending map is inspected directly and no
 * transport, socket or thread runs.
 *
 */

#include <cassert>
#include <cstdint>
#include <memory>

#include "Identity.h"
#include "Tunnel.h"
#include "TunnelPool.h"

namespace i2p
{
namespace tunnel
{
	struct TunnelPoolTestAccess
	{
		static void RunCreateTunnels (TunnelPool & pool, uint64_t ts) { pool.CreateTunnels (ts); }
	};
}
}

namespace
{
using namespace i2p::data;
using namespace i2p::tunnel;

std::shared_ptr<const IdentityEx> MakePeer ()
{
	return PrivateKeys::CreateRandomKeys (SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD)
		.GetPublic ();
}

void TestClampTunnelLength ()
{
	int hops = MAX_NUM_RECORDS + 4, variance = 3;
	ClampTunnelLength (hops, variance, true);
	assert (hops == MAX_INBOUND_HOPS); // phony record slot reserved
	assert (hops + variance <= MAX_NUM_RECORDS);

	hops = MAX_NUM_RECORDS + 4;
	variance = 3;
	ClampTunnelLength (hops, variance, false);
	assert (hops == MAX_NUM_RECORDS);
	assert (variance == 0); // no room left for a longer path

	// A negative variance may shorten a path but never to zero hops.
	hops = 2;
	variance = -5;
	ClampTunnelLength (hops, variance, true);
	assert (hops == 2);
	assert (hops + variance >= 1);

	hops = 0;
	variance = -5;
	ClampTunnelLength (hops, variance, false);
	assert (hops == 0);
	assert (variance == 0);

	hops = -3;
	variance = 0;
	ClampTunnelLength (hops, variance, false);
	assert (hops == 0);
}

void TestReconfigureClamp ()
{
	// A pool length change must keep the variance inside the record table:
	// inbound reserves the phony record slot, outbound may use all
	// records.
	auto pool = std::make_shared<TunnelPool> (MAX_NUM_RECORDS, MAX_NUM_RECORDS, 1, 1, 3, 3, false);
	assert (pool->GetNumInboundHops () == MAX_INBOUND_HOPS);
	assert (pool->GetNumOutboundHops () == MAX_NUM_RECORDS);
	assert (pool->AcceptsInboundLength (MAX_INBOUND_HOPS));
	assert (!pool->AcceptsInboundLength (MAX_NUM_RECORDS));
	assert (pool->AcceptsOutboundLength (MAX_NUM_RECORDS));
	assert (!pool->AcceptsOutboundLength (MAX_NUM_RECORDS + 1));

	assert (pool->Reconfigure (MAX_NUM_RECORDS + 5, MAX_NUM_RECORDS + 5, 2, 2));
	assert (pool->GetNumInboundHops () == MAX_INBOUND_HOPS);
	assert (pool->GetNumOutboundHops () == MAX_NUM_RECORDS);
	// The stored variance was re-clamped with the new length.
	assert (pool->AcceptsInboundLength (MAX_INBOUND_HOPS));
	assert (!pool->AcceptsInboundLength (MAX_INBOUND_HOPS + 1));
	assert (pool->AcceptsOutboundLength (MAX_NUM_RECORDS));

	tunnels.DeleteTunnelPool (pool);
}

void TestBuildRequestLadder ()
{
	// No work when the target is reached, even if one request is in
	// flight.
	assert (ComputeBuildRequestCount (0, 0, 0, 0) == 0);
	assert (ComputeBuildRequestCount (2, 2, 0, 0) == 0);
	assert (ComputeBuildRequestCount (3, 0, 3, 0) == 0);
	assert (ComputeBuildRequestCount (3, 1, 1, 0) == 1);

	// Healthy pool: the round is capped at the configured maximum.
	assert (ComputeBuildRequestCount (16, 0, 0, 0) == TUNNEL_POOL_MAX_NUM_BUILD_REQUESTS);
	// In-progress builds count against the target, so a slow build round
	// cannot
	// be oversubscribed by the next management tick.
	assert (ComputeBuildRequestCount (3, 0, 1, 0) == 2);
	assert (ComputeBuildRequestCount (3, 0, 3, 0) == 0);
	assert (ComputeBuildRequestCount (5, 3, 1, 0) == 1);
	assert (ComputeBuildRequestCount (2, 0, 2, 0) == 0);

	// Java-like ladder: 4/8/12 consecutive failures reduce the quantity.
	assert (ComputeBuildRequestCount (16, 0, 0, TUNNEL_POOL_BUILD_FAILURES_TO_REDUCE - 1)
		== TUNNEL_POOL_MAX_NUM_BUILD_REQUESTS);
	assert (ComputeBuildRequestCount (16, 0, 0, TUNNEL_POOL_BUILD_FAILURES_TO_REDUCE) == 2);
	assert (ComputeBuildRequestCount (16, 0, 0, 2 * TUNNEL_POOL_BUILD_FAILURES_TO_REDUCE) == 1);
	assert (ComputeBuildRequestCount (16, 0, 0, 3 * TUNNEL_POOL_BUILD_FAILURES_TO_REDUCE) == 1);
	assert (ComputeBuildRequestCount (16, 0, 0, 100) == 1); // never stops building
}

void TestBuildBackoff ()
{
	const int base = GetTunnelBuildBackoff (0);
	assert (base == TUNNEL_POOL_BUILD_BACKOFF_BASE);
	assert (GetTunnelBuildBackoff (TUNNEL_POOL_BUILD_FAILURES_TO_REDUCE - 1) == base);
	int previous = base;
	for (int failures = 0; failures <= 64; ++failures)
	{
		const int delay = GetTunnelBuildBackoff (failures);
		assert (delay >= base);
		assert (delay <= TUNNEL_POOL_BUILD_BACKOFF_MAX);
		assert (delay >= previous); // never shortens while failures grow
		previous = delay;
	}
	assert (GetTunnelBuildBackoff (TUNNEL_POOL_BUILD_FAILURES_TO_REDUCE) > base);
	assert (GetTunnelBuildBackoff (1000) == TUNNEL_POOL_BUILD_BACKOFF_MAX);
}

void TestTestTimeout ()
{
	assert (GetTunnelTestTimeout (0, 0) == TUNNEL_POOL_TEST_TIMEOUT_MIN);
	assert (GetTunnelTestTimeout (1, 0) >= TUNNEL_POOL_TEST_TIMEOUT_MIN);
	assert (GetTunnelTestTimeout (0, 1000000) == TUNNEL_POOL_TEST_TIMEOUT_MAX);
	assert (GetTunnelTestTimeout (MAX_NUM_RECORDS, 1000000) == TUNNEL_POOL_TEST_TIMEOUT_MAX);
	// More hops (or a slower local link) must never shorten the deadline.
	uint64_t previous = 0;
	for (int hops = 0; hops <= MAX_NUM_RECORDS; ++hops)
	{
		const uint64_t timeout = GetTunnelTestTimeout (hops, 20);
		assert (timeout >= previous);
		assert (timeout >= TUNNEL_POOL_TEST_TIMEOUT_MIN);
		assert (timeout <= TUNNEL_POOL_TEST_TIMEOUT_MAX);
		previous = timeout;
	}
}

void TestInboundActivitySkip ()
{
	const uint64_t now = 1000000000ULL;
	assert (!ShouldSkipTunnelTest (now, 0)); // no traffic yet
	assert (ShouldSkipTunnelTest (now, now));
	assert (ShouldSkipTunnelTest (now, now - (TUNNEL_POOL_INBOUND_ACTIVITY_SKIP_TEST * 1000ULL - 1)));
	assert (
		!ShouldSkipTunnelTest (now, now - (static_cast<uint64_t> (TUNNEL_POOL_INBOUND_ACTIVITY_SKIP_TEST) * 1000ULL)));
	// A clock that moved backwards is not evidence of recent traffic.
	assert (!ShouldSkipTunnelTest (now, now + 1));
}

void TestPoolAccounting ()
{
	auto pool = std::make_shared<TunnelPool> (1, 1, 1, 1, 0, 0, false);
	assert (pool->GetConsecutiveBuildFailures () == 0);

	pool->TunnelBuildFailed ();
	pool->TunnelBuildFailed ();
	assert (pool->GetConsecutiveBuildFailures () == 2);
	pool->TunnelBuildSucceeded ();
	assert (pool->GetConsecutiveBuildFailures () == 0); // reset on success

	auto config = std::make_shared<TunnelConfig> (
		std::vector<std::shared_ptr<const IdentityEx>> { MakePeer () }, 1, IdentHash { });
	auto pending = tunnels.CreateOutboundTunnel (config, pool);
	assert (pending);
	assert (pending->GetState () == eTunnelStatePending);
	assert (tunnels.GetNumPendingTunnels (pool.get (), false) == 1);
	assert (tunnels.GetNumPendingTunnels (pool.get (), true) == 0);
	assert (tunnels.GetNumPendingTunnels (nullptr, false) == 0);

	// The pool target is one tunnel and one build is already in flight:
	// the next round must not start another request.
	const uint64_t ts = 1000;
	TunnelPoolTestAccess::RunCreateTunnels (*pool, ts);
	assert (tunnels.GetNumPendingTunnels (pool.get (), false) == 1);
	const uint64_t nextBuildTime = pool->GetNextBuildTime ();
	assert (nextBuildTime == ts + GetTunnelBuildBackoff (0));

	// A second tick inside the build delay does nothing at all.
	TunnelPoolTestAccess::RunCreateTunnels (*pool, ts + 1);
	assert (pool->GetNextBuildTime () == nextBuildTime);
	assert (tunnels.GetNumPendingTunnels (pool.get (), false) == 1);

	pending->SetState (eTunnelStateFailed);
	tunnels.DeleteTunnelPool (pool);
}
}

int main ()
{
	TestClampTunnelLength ();
	TestReconfigureClamp ();
	TestBuildRequestLadder ();
	TestBuildBackoff ();
	TestTestTimeout ();
	TestInboundActivitySkip ();
	TestPoolAccounting ();
	return 0;
}
