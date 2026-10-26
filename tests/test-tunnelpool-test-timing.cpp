/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test tunnel pool test timing and strikes.
 *
 * A tunnel test survives management ticks until its own deadline, one lost
 * test is not a failure, the third strike fails the tunnel, and a tunnel with
 * recent inbound traffic is not tested again. Deterministic: the pending test
 * table is inspected directly and every deadline is injected.
 *
 */

#include <cassert>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "Tunnel.h"
#include "TunnelPool.h"
#include "Timestamp.h"

namespace i2p
{
namespace tunnel
{
	struct TunnelPoolTestAccess
	{
		static void InsertTest (TunnelPool & pool, uint32_t msgID, std::shared_ptr<OutboundTunnel> outbound,
			std::shared_ptr<InboundTunnel> inbound, uint64_t expiration)
		{
			TunnelPool::TunnelTest test;
			test.outbound = std::move (outbound);
			test.inbound = std::move (inbound);
			test.sentTime = i2p::util::GetMillisecondsSinceEpoch ();
			test.expiration = expiration;
			std::lock_guard<std::mutex> l (pool.m_TestsMutex);
			pool.m_Tests[msgID] = test;
		}

		static size_t NumTests (const TunnelPool & pool)
		{
			std::lock_guard<std::mutex> l (pool.m_TestsMutex);
			return pool.m_Tests.size ();
		}

		static void RunTestTunnels (TunnelPool & pool, uint64_t ts) { pool.TestTunnels (ts); }

		static void AddInbound (TunnelPool & pool, std::shared_ptr<InboundTunnel> tunnel)
		{
			std::lock_guard<std::mutex> l (pool.m_InboundTunnelsMutex);
			pool.m_InboundTunnels.insert (std::move (tunnel));
		}

		static void AddOutbound (TunnelPool & pool, std::shared_ptr<OutboundTunnel> tunnel)
		{
			std::lock_guard<std::mutex> l (pool.m_OutboundTunnelsMutex);
			pool.m_OutboundTunnels.insert (std::move (tunnel));
		}

		static size_t InboundSize (const TunnelPool & pool)
		{
			std::lock_guard<std::mutex> l (pool.m_InboundTunnelsMutex);
			return pool.m_InboundTunnels.size ();
		}

		static size_t OutboundSize (const TunnelPool & pool)
		{
			std::lock_guard<std::mutex> l (pool.m_OutboundTunnelsMutex);
			return pool.m_OutboundTunnels.size ();
		}
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

struct EstablishedPair
{
	std::shared_ptr<OutboundTunnel> outbound;
	std::shared_ptr<InboundTunnel> inbound;
};

EstablishedPair MakeEstablishedPair ()
{
	std::vector<std::shared_ptr<const IdentityEx>> peers { MakePeer (), MakePeer () };
	auto outbound = std::make_shared<OutboundTunnel> (std::make_shared<TunnelConfig> (peers, 1, IdentHash { }));
	outbound->SetState (eTunnelStateEstablished);
	auto inboundPeers = peers;
	auto inbound = std::make_shared<InboundTunnel> (std::make_shared<TunnelConfig> (inboundPeers, eAllTransports));
	inbound->SetState (eTunnelStateEstablished);
	return { outbound, inbound };
}

void TestPendingTestsSurviveTicks ()
{
	auto pool = std::make_shared<TunnelPool> (2, 2, 1, 1, 0, 0, false);
	auto pair = MakeEstablishedPair ();
	TunnelPoolTestAccess::AddOutbound (*pool, pair.outbound);
	TunnelPoolTestAccess::AddInbound (*pool, pair.inbound);

	const uint64_t now = i2p::util::GetMillisecondsSinceEpoch ();
	TunnelPoolTestAccess::InsertTest (*pool, 1, pair.outbound, pair.inbound, now + 5000);
	assert (TunnelPoolTestAccess::NumTests (*pool) == 1);

	using Access = TunnelPoolTestAccess;
	Access::RunTestTunnels (*pool, i2p::util::GetSecondsSinceEpoch ());
	// The deadline has not passed: the test is kept and no strike is applied.
	assert (Access::NumTests (*pool) == 1);
	assert (pair.outbound->GetTestStrikes () == 0);
	assert (pair.inbound->GetTestStrikes () == 0);
	assert (pair.outbound->GetState () == eTunnelStateEstablished);

	tunnels.DeleteTunnelPool (pool);
}

void TestStrikeLadder ()
{
	auto pool = std::make_shared<TunnelPool> (2, 2, 1, 1, 0, 0, false);
	auto pair = MakeEstablishedPair ();
	TunnelPoolTestAccess::AddOutbound (*pool, pair.outbound);
	TunnelPoolTestAccess::AddInbound (*pool, pair.inbound);

	const uint64_t past = i2p::util::GetMillisecondsSinceEpoch () - 1;
	for (int strike = 1; strike < TUNNEL_POOL_TEST_STRIKES; ++strike)
	{
		TunnelPoolTestAccess::InsertTest (*pool, static_cast<uint32_t> (strike), pair.outbound, pair.inbound, past);
		TunnelPoolTestAccess::RunTestTunnels (*pool, i2p::util::GetSecondsSinceEpoch ());
		assert (pair.outbound->GetTestStrikes () == strike);
		assert (pair.inbound->GetTestStrikes () == strike);
		// A suspicion is not a failure: the tunnels stay in service.
		assert (pair.outbound->GetState () == eTunnelStateEstablished);
		assert (pair.inbound->GetState () == eTunnelStateEstablished);
		assert (TunnelPoolTestAccess::OutboundSize (*pool) == 1);
		assert (TunnelPoolTestAccess::InboundSize (*pool) == 1);
	}

	// The last strike fails both tunnels and removes them from the pool.
	TunnelPoolTestAccess::InsertTest (*pool, 99, pair.outbound, pair.inbound, past);
	TunnelPoolTestAccess::RunTestTunnels (*pool, i2p::util::GetSecondsSinceEpoch ());
	assert (pair.outbound->GetTestStrikes () == TUNNEL_POOL_TEST_STRIKES);
	assert (pair.outbound->GetState () == eTunnelStateFailed);
	assert (pair.inbound->GetState () == eTunnelStateFailed);
	assert (!pair.outbound->IsEstablished ());
	assert (!pair.inbound->IsEstablished ());
	assert (TunnelPoolTestAccess::OutboundSize (*pool) == 0);
	assert (TunnelPoolTestAccess::InboundSize (*pool) == 0);

	tunnels.DeleteTunnelPool (pool);
}

void TestSuccessfulTestResetsStrikes ()
{
	auto pool = std::make_shared<TunnelPool> (2, 2, 1, 1, 0, 0, false);
	auto pair = MakeEstablishedPair ();
	pair.outbound->AddTestStrike ();
	pair.inbound->AddTestStrike ();
	assert (pair.outbound->GetTestStrikes () == 1);

	const uint64_t now = i2p::util::GetMillisecondsSinceEpoch ();
	TunnelPoolTestAccess::InsertTest (*pool, 7, pair.outbound, pair.inbound, now + 60000);
	assert (pool->ProcessTunnelTest (7, i2p::util::GetMonotonicMicroseconds ()));
	assert (pair.outbound->GetTestStrikes () == 0);
	assert (pair.inbound->GetTestStrikes () == 0);
	assert (pair.outbound->GetState () == eTunnelStateEstablished);
	assert (TunnelPoolTestAccess::NumTests (*pool) == 0);

	tunnels.DeleteTunnelPool (pool);
}

void TestTestFailedTunnelsAreNotPublished ()
{
	// A tunnel the legacy test path demoted must not appear in a LeaseSet.
	auto pool = std::make_shared<TunnelPool> (1, 1, 2, 2, 0, 0, false);
	auto good = MakeEstablishedPair ();
	auto demoted = MakeEstablishedPair ();
	demoted.inbound->SetState (eTunnelStateTestFailed);
	TunnelPoolTestAccess::AddInbound (*pool, demoted.inbound);
	TunnelPoolTestAccess::AddInbound (*pool, good.inbound);

	auto published = pool->GetInboundTunnels (8);
	assert (published.size () == 1);
	assert (published.front () == good.inbound);

	tunnels.DeleteTunnelPool (pool);
}

void TestRecentInboundTrafficSkipsTesting ()
{
	// Recent traffic must make a test redundant; the pure decision is covered
	// here so the pool wiring stays testable without a live destination.
	const uint64_t now = i2p::util::GetMillisecondsSinceEpoch ();
	assert (ShouldSkipTunnelTest (now, now));
	assert (!ShouldSkipTunnelTest (now, 0));
	assert (!ShouldSkipTunnelTest (now, now - 60 * 1000));
}
}

int main ()
{
	TestPendingTestsSurviveTicks ();
	TestStrikeLadder ();
	TestSuccessfulTestResetsStrikes ();
	TestTestFailedTunnelsAreNotPublished ();
	TestRecentInboundTrafficSkipsTesting ();
	return 0;
}
