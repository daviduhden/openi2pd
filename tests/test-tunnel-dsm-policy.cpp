/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test tunnel DatabaseStore policy.
 *
 * Offline policy check for Java-compatible rejection of token-bearing
 * DatabaseStore messages that arrive through an exploratory tunnel.
 */

#include <cassert>

#include "NetDb.hpp"

int main ()
{
	using i2p::data::IsTunnelDatabaseStoreAccepted;

	// Direct stores, including publish confirmations, remain valid.
	assert (IsTunnelDatabaseStoreAccepted (false, 0));
	assert (IsTunnelDatabaseStoreAccepted (false, 1));
	assert (IsTunnelDatabaseStoreAccepted (false, 0xFFFFFFFFU));

	// Tunnel/clove-delivered stores are accepted only without any reply token,
	// including the legacy 0xFFFFFFFF marker, which Java also drops from a
	// tunnel. A token would create a DeliveryStatus relay.
	assert (IsTunnelDatabaseStoreAccepted (true, 0));
	assert (!IsTunnelDatabaseStoreAccepted (true, 0xFFFFFFFFU));
	assert (!IsTunnelDatabaseStoreAccepted (true, 1));
	assert (!IsTunnelDatabaseStoreAccepted (true, 0x12345678U));

	return 0;
}
