/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test tunnel build record table.
 *
 * Checks the short-build record plan, the hard hop-count guard in Tunnel::Build
 * (a path longer than the record table must fail without sending) and the
 * fail-closed record writer plus the exact build-response count/length checks.
 * Deterministic: no sockets, threads or sleeps.
 *
 */

#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "Identity.h"
#include "Tunnel.h"
#include "TunnelConfig.h"

namespace
{
using i2p::SHORT_TUNNEL_BUILD_RECORD_SIZE;
using namespace i2p::data;
using namespace i2p::tunnel;

std::shared_ptr<const IdentityEx> MakePeer ()
{
	return PrivateKeys::CreateRandomKeys (SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD)
		.GetPublic ();
}

std::vector<std::shared_ptr<const IdentityEx>> MakePeers (int count)
{
	std::vector<std::shared_ptr<const IdentityEx>> peers;
	for (int i = 0; i < count; ++i) peers.push_back (MakePeer ());
	return peers;
}

void TestRecordPlan ()
{
	for (int hops = -1; hops <= 12; ++hops)
	{
		for (bool inbound : { false, true })
		{
			const auto plan = PlanTunnelBuildRecords (hops, inbound);
			if (hops <= 0 || hops > MAX_NUM_RECORDS)
			{
				// Anything the table cannot describe must be rejected before a
				// byte is written; the hop loop indexes the shuffled record map.
				assert (!plan.valid);
				continue;
			}
			assert (plan.valid);
			assert (plan.numHops == hops);
			if (inbound)
				assert (plan.insertPhonyRecord == (hops < MAX_NUM_RECORDS));
			else
				assert (!plan.insertPhonyRecord);
			const int hopsIncludingPhony = hops + (plan.insertPhonyRecord ? 1 : 0);
			assert (plan.numRecords
				== (hopsIncludingPhony <= STANDARD_NUM_RECORDS ? STANDARD_NUM_RECORDS : MAX_NUM_RECORDS));
			assert (plan.numRecords == STANDARD_NUM_RECORDS || plan.numRecords == MAX_NUM_RECORDS);
			// The invariant that was violated before the guard existed.
			assert (hopsIncludingPhony <= plan.numRecords);
			assert (plan.numRecords <= MAX_NUM_RECORDS);
		}
	}

	// An inbound path at the record limit needs no phony record, one below it
	// does, and the phony hop still fits.
	assert (!PlanTunnelBuildRecords (MAX_NUM_RECORDS, true).insertPhonyRecord);
	assert (PlanTunnelBuildRecords (MAX_INBOUND_HOPS, true).insertPhonyRecord);
	assert (PlanTunnelBuildRecords (MAX_INBOUND_HOPS, true).numRecords == MAX_NUM_RECORDS);
}

void TestOversizedBuildFailsClosed ()
{
	// Nine hops cannot be indexed by an eight-entry record map. The build must
	// fail before touching the message and must not send anything.
	auto config = std::make_shared<TunnelConfig> (MakePeers (MAX_NUM_RECORDS + 1), 1, IdentHash { });
	assert (config->GetNumHops () == MAX_NUM_RECORDS + 1);
	auto tunnel = std::make_shared<OutboundTunnel> (config);
	tunnel->Build (1);
	assert (tunnel->GetState () == eTunnelStateBuildFailed);
	assert (tunnel->GetNumBuildRecords () == 0);
	assert (!tunnel->IsEstablished ());
}

void TestValidBuildStoresRecordCount ()
{
	auto config = std::make_shared<TunnelConfig> (MakePeers (2), 7, IdentHash { });
	auto tunnel = std::make_shared<OutboundTunnel> (config);
	tunnel->Build (0x5A5A5A5AU);
	assert (tunnel->GetState () == eTunnelStatePending);
	assert (tunnel->GetNumBuildRecords () == STANDARD_NUM_RECORDS);

	std::vector<uint8_t> wrongCount (1 + MAX_NUM_RECORDS * SHORT_TUNNEL_BUILD_RECORD_SIZE, 0);
	wrongCount[0] = static_cast<uint8_t> (MAX_NUM_RECORDS);
	assert (!tunnel->HandleTunnelBuildResponse (wrongCount.data (), wrongCount.size ()));

	// Trailing bytes are rejected: the reply must be exactly the advertised
	// record table, otherwise the nonce/record mapping would be ambiguous.
	std::vector<uint8_t> trailing (1 + STANDARD_NUM_RECORDS * SHORT_TUNNEL_BUILD_RECORD_SIZE + 1, 0);
	trailing[0] = static_cast<uint8_t> (STANDARD_NUM_RECORDS);
	assert (!tunnel->HandleTunnelBuildResponse (trailing.data (), trailing.size ()));

	// Truncated by one byte is rejected too.
	assert (!tunnel->HandleTunnelBuildResponse (trailing.data (), trailing.size () - 2));

	// Correct shape but unauthenticatable records: rejected, not established.
	std::vector<uint8_t> exact (1 + STANDARD_NUM_RECORDS * SHORT_TUNNEL_BUILD_RECORD_SIZE, 0);
	exact[0] = static_cast<uint8_t> (STANDARD_NUM_RECORDS);
	assert (!tunnel->HandleTunnelBuildResponse (exact.data (), exact.size ()));
	assert (!tunnel->IsEstablished ());
}

void TestRecordWriterBounds ()
{
	auto config = std::make_shared<TunnelConfig> (MakePeers (2), 1, IdentHash { });
	auto * hop = config->GetFirstHop ();
	assert (hop);

	std::vector<uint8_t> records (STANDARD_NUM_RECORDS * SHORT_TUNNEL_BUILD_RECORD_SIZE, 0);
	hop->recordIndex = 0;
	assert (hop->CreateBuildRequestRecord (records.data (), STANDARD_NUM_RECORDS, 0x11223344U));

	const std::vector<uint8_t> before = records;
	// An index outside the table is refused and the buffer is left untouched.
	hop->recordIndex = STANDARD_NUM_RECORDS;
	assert (!hop->CreateBuildRequestRecord (records.data (), STANDARD_NUM_RECORDS, 1));
	assert (std::memcmp (before.data (), records.data (), records.size ()) == 0);
	hop->recordIndex = -1;
	assert (!hop->CreateBuildRequestRecord (records.data (), STANDARD_NUM_RECORDS, 1));
	assert (std::memcmp (before.data (), records.data (), records.size ()) == 0);

	// A phony record fails closed while the router has no identity: it must be
	// zeroed instead of naming a null identity.
	ShortPhonyTunnelHopConfig phony;
	phony.recordIndex = 0;
	std::vector<uint8_t> phonyRecords (STANDARD_NUM_RECORDS * SHORT_TUNNEL_BUILD_RECORD_SIZE, 0xAA);
	assert (!phony.CreateBuildRequestRecord (phonyRecords.data (), STANDARD_NUM_RECORDS, 0));
	for (size_t i = 0; i < SHORT_TUNNEL_BUILD_RECORD_SIZE; ++i) assert (phonyRecords[i] == 0);
	// The untouched rest of the table keeps its contents.
	assert (phonyRecords[SHORT_TUNNEL_BUILD_RECORD_SIZE] == 0xAA);
}

void TestTooManyRecordsRejected ()
{
	auto config = std::make_shared<TunnelConfig> (MakePeers (1), 1, IdentHash { });
	auto tunnel = std::make_shared<OutboundTunnel> (config);
	tunnel->Build (1);
	assert (tunnel->GetNumBuildRecords () == STANDARD_NUM_RECORDS);

	std::vector<uint8_t> reply (1 + (MAX_NUM_RECORDS + 1) * SHORT_TUNNEL_BUILD_RECORD_SIZE, 0);
	reply[0] = static_cast<uint8_t> (MAX_NUM_RECORDS + 1);
	assert (!tunnel->HandleTunnelBuildResponse (reply.data (), reply.size ()));
}
}

int main ()
{
	TestRecordPlan ();
	TestOversizedBuildFailsClosed ();
	TestValidBuildStoresRecordCount ();
	TestRecordWriterBounds ();
	TestTooManyRecordsRejected ();
	return 0;
}
