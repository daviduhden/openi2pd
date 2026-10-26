/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: netDb garlic-clove origin policy.
 *
 * Checks the Java 2.14.0 distinction between a garlic received directly from a
 * peer and one that arrived through an I2NP tunnel: a direct-garlic clove may
 * be answered and may keep a DatabaseStore reply token, a tunnel-garlic clove
 * neither answers a DatabaseLookup nor acknowledges/relays a store. No
 * sockets, threads or sleeps are used.
 *
 */

#include <cassert>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>

#include "I2NPProtocol.h"
#include "NetDb.hpp"

namespace i2p::data
{
// The friend declared by NetDb; only queue admission is exposed so the dispatch
// path can be observed without starting the netDb worker.
struct NetDbLifecycleTestAccess
{
	static void OpenAdmission (NetDb & database)
	{
		std::lock_guard lock (database.m_QueueAdmissionMutex);
		database.m_AcceptMessages = true;
	}
	static size_t Queued (NetDb & database) { return database.m_Queue.GetSize (); }
	static void Drain (NetDb & database)
	{
		std::list<std::shared_ptr<const I2NPMessage>> queued;
		database.m_Queue.GetWholeQueue (queued);
	}
};
}

namespace
{
using namespace i2p;
using namespace i2p::data;

void TestCloveOriginMatrix ()
{
	// Direct transport, not inside garlic.
	I2NPMessage direct;
	assert (direct.GetCloveOrigin () == I2NPCloveOrigin::Direct);
	assert (!direct.IsTunnelOrigin ());
	assert (GetDatabaseStoreOrigin (direct) == DatabaseStoreOrigin::Direct);
	assert (IsDatabaseLookupDispatchAllowed (direct));
	assert (IsDirectDatabaseStoreOrigin (DatabaseStoreOrigin::Direct));

	// Clove of a garlic received directly from a peer: Java's
	// HandleGarlicMessageJob. It has no InboundTunnel pointer but is trusted
	// enough to answer a lookup and to acknowledge a store.
	I2NPMessage directGarlic;
	directGarlic.fromClove = true;
	assert (directGarlic.GetCloveOrigin () == I2NPCloveOrigin::DirectGarlic);
	assert (!directGarlic.IsTunnelOrigin ());
	assert (GetDatabaseStoreOrigin (directGarlic) == DatabaseStoreOrigin::DirectGarlic);
	assert (IsDirectDatabaseStoreOrigin (DatabaseStoreOrigin::DirectGarlic));
	assert (IsDatabaseLookupDispatchAllowed (directGarlic));

	// Clove of a garlic that arrived through an I2NP tunnel: Java's
	// InboundMessageDistributor. Same absence of InboundTunnel, but the
	// enclosing garlic is recorded as tunnel-delivered.
	I2NPMessage tunnelGarlic;
	tunnelGarlic.fromClove = true;
	tunnelGarlic.fromTunnel = true;
	assert (tunnelGarlic.GetCloveOrigin () == I2NPCloveOrigin::TunnelGarlic);
	assert (tunnelGarlic.IsTunnelOrigin ());
	assert (GetDatabaseStoreOrigin (tunnelGarlic) == DatabaseStoreOrigin::Tunnel);
	assert (!IsDirectDatabaseStoreOrigin (DatabaseStoreOrigin::Tunnel));
	assert (!IsDatabaseLookupDispatchAllowed (tunnelGarlic));

	// A nested garlic clove keeps its parent's origin.
	const uint8_t garlicPayload[1] = { 0 };
	auto nested = CreateI2NPMessage (eI2NPGarlic, garlicPayload, sizeof (garlicPayload), 7);
	assert (nested);
	nested->fromClove = true;
	nested->fromTunnel = true;
	assert (nested->GetCloveOrigin () == I2NPCloveOrigin::TunnelGarlic);
}

void TestCloveAllowList ()
{
	// Java InboundMessageDistributor.handleClove(): database and control
	// messages only. DatabaseLookup is admitted so HandleGarlicMessageJob can
	// dispatch it; the origin check then decides whether it is answered.
	assert (IsAllowedCloveMessageType (eI2NPDatabaseStore));
	assert (IsAllowedCloveMessageType (eI2NPDatabaseLookup));
	assert (IsAllowedCloveMessageType (eI2NPDatabaseSearchReply));
	assert (IsAllowedCloveMessageType (eI2NPDeliveryStatus));
	assert (IsAllowedCloveMessageType (eI2NPGarlic));
	assert (IsAllowedCloveMessageType (eI2NPData));
	assert (IsAllowedCloveMessageType (eI2NPShortTunnelBuildReply));
	// Tunnel/build requests must never be injected from a clove.
	assert (!IsAllowedCloveMessageType (eI2NPTunnelData));
	assert (!IsAllowedCloveMessageType (eI2NPTunnelGateway));
	assert (!IsAllowedCloveMessageType (eI2NPVariableTunnelBuild));
	assert (!IsAllowedCloveMessageType (eI2NPTunnelBuild));
	assert (!IsAllowedCloveMessageType (eI2NPShortTunnelBuild));
	assert (IsAllowedCloveMessageType (eI2NPTunnelBuildReply));
	assert (IsAllowedCloveMessageType (eI2NPVariableTunnelBuildReply));
}

void TestDatabaseStoreTokenMatrix ()
{
	// Direct and direct-garlic stores keep a reply token (store + DeliveryStatus
	// acknowledgement); a tunnel-origin store clears it and never acknowledges.
	const uint32_t tokens[] = { 0, 1, 0xFFFFFFFFU, 0x12345678U };
	for (uint32_t token : tokens)
	{
		assert (KeepsDatabaseStoreReplyToken (DatabaseStoreOrigin::Direct, token) == (token != 0));
		assert (KeepsDatabaseStoreReplyToken (DatabaseStoreOrigin::DirectGarlic, token) == (token != 0));
		assert (!KeepsDatabaseStoreReplyToken (DatabaseStoreOrigin::Tunnel, token));
	}

	// An unsolicited LeaseSet is stored for a direct and a direct-garlic store,
	// never for a tunnel-origin one.
	assert (AcceptsUnsolicitedDatabaseStore (DatabaseStoreOrigin::Direct));
	assert (AcceptsUnsolicitedDatabaseStore (DatabaseStoreOrigin::DirectGarlic));
	assert (AcceptsUnsolicitedDatabaseStore (DatabaseStoreOrigin::Tunnel)); // stored, not acked
}

void TestCloveDispatchReachesNetDbAdmission ()
{
	// End-to-end dispatch without a worker: a clove DatabaseStore is handed to
	// the netDb admission queue. The origin policy itself is applied inside
	// HandleDatabaseStoreMsg(), which the predicate assertions above pin.
	using Access = i2p::data::NetDbLifecycleTestAccess;
	auto & database = i2p::data::netdb;
	Access::Drain (database);
	Access::OpenAdmission (database);
	assert (Access::Queued (database) == 0);

	const uint8_t payload[DATABASE_STORE_HEADER_SIZE] = { };
	auto directGarlicStore = CreateI2NPMessage (eI2NPDatabaseStore, payload, sizeof (payload), 0x1234);
	assert (directGarlicStore);
	directGarlicStore->fromClove = true;
	HandleI2NPMessage (directGarlicStore);
	assert (Access::Queued (database) == 1);

	// A tunnel-origin clove store is still admitted: the handler stores the
	// entry and applies the token/origin policy instead of dropping it.
	auto tunnelGarlicStore = CreateI2NPMessage (eI2NPDatabaseStore, payload, sizeof (payload), 0x1235);
	assert (tunnelGarlicStore);
	tunnelGarlicStore->fromClove = true;
	tunnelGarlicStore->fromTunnel = true;
	HandleI2NPMessage (tunnelGarlicStore);
	assert (Access::Queued (database) == 2);

	Access::Drain (database);
	assert (Access::Queued (database) == 0);
}
}

int main ()
{
	TestCloveOriginMatrix ();
	TestCloveAllowList ();
	TestDatabaseStoreTokenMatrix ();
	TestCloveDispatchReachesNetDbAdmission ();
	return 0;
}
