/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test tunnel DatabaseStore policy.
 *
 * Offline policy check for Java-compatible DatabaseStore origin
 * handling: a direct or direct-garlic store keeps its reply token and
 * is acknowledged, while a tunnel-origin store clears it and is neither
 * acknowledged nor relayed. The entry itself is stored in every case.
 */

#include <cassert>
#include <cstdint>

#include "NetDb.hpp"

int main ()
{
	using i2p::data::AcceptsUnsolicitedDatabaseStore;
	using i2p::data::DatabaseStoreOrigin;
	using i2p::data::IsDirectDatabaseStoreOrigin;
	using i2p::data::KeepsDatabaseStoreReplyToken;

	const uint32_t tokens[] = { 0, 1, 0xFFFFFFFFU, 0x12345678U };

	// Direct stores, including publish confirmations, keep the reply token
	// and are answered with a DeliveryStatus.
	for (uint32_t token : tokens)
	{
		assert (KeepsDatabaseStoreReplyToken (DatabaseStoreOrigin::Direct, token) == (token != 0));
		// A clove of a garlic received directly from a peer behaves like a
		// direct store (Java HandleGarlicMessageJob). Dropping it here broke
		// a local destination publishing its LeaseSet to a local floodfill.
		assert (KeepsDatabaseStoreReplyToken (DatabaseStoreOrigin::DirectGarlic, token) == (token != 0));
		// A tunnel-origin store keeps the entry but never keeps the token,
		// including the legacy 0xFFFFFFFF marker: a token would create a
		// DeliveryStatus relay to an arbitrary reply hash/tunnel.
		assert (!KeepsDatabaseStoreReplyToken (DatabaseStoreOrigin::Tunnel, token));
	}

	// An unsolicited LeaseSet is stored only when the store itself was
	// direct (plain direct or direct garlic); a tunnel-delivered one is
	// not ours.
	assert (AcceptsUnsolicitedDatabaseStore (DatabaseStoreOrigin::Direct));
	assert (AcceptsUnsolicitedDatabaseStore (DatabaseStoreOrigin::DirectGarlic));
	assert (AcceptsUnsolicitedDatabaseStore (DatabaseStoreOrigin::Tunnel)); // stored, not acked

	assert (IsDirectDatabaseStoreOrigin (DatabaseStoreOrigin::Direct));
	assert (IsDirectDatabaseStoreOrigin (DatabaseStoreOrigin::DirectGarlic));
	assert (!IsDirectDatabaseStoreOrigin (DatabaseStoreOrigin::Tunnel));

	return 0;
}
