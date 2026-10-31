/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: I2NP dispatcher admission policy.
 *
 * Checks the Java 2.14.0 MessageValidator window applied before an I2NP
 * message reaches a handler, the bounded decaying duplicate filter
 * keyed by message ID and expiration second (DecayingHashSet TIMER_MASK
 * form), the origin-dependent clove allow-list, the strict
 * DeliveryStatus and DataMessage payload lengths, the bounded garlic
 * queue predicate and the CreateI2NPMessage overflow behaviour. No
 * sockets, threads or sleeps are used.
 *
 */

#include <cassert>
#include <cstdint>
#include <cstring>
#include <list>
#include <memory>
#include <mutex>

#include "I2NPProtocol.h"
#include "NetDb.hpp"
#include "Timestamp.h"

namespace i2p::data
{
// The friend declared by NetDb; only queue admission is exposed so the
// dispatch path can be observed without starting the netDb worker.
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
} // namespace i2p::data

namespace
{
using namespace i2p;

void ResetDispatchState ()
{
	GetI2NPDispatchCounters ().Reset ();
	GetI2NPRouterDuplicateFilter ().Clear ();
}

void TestDispatcherExpirationWindow ()
{
	// Java MessageValidator: 90 s in the past and 240 s in the future are
	// the boundaries and both are inclusive.
	const uint64_t now = 1'700'000'000'000ULL;
	assert (IsFreshI2NPExpiration (now, now));
	assert (IsFreshI2NPExpiration (now - I2NP_DISPATCH_MAX_AGE, now)); // exactly 90 s old
	assert (!IsFreshI2NPExpiration (now - I2NP_DISPATCH_MAX_AGE - 1, now));
	assert (IsFreshI2NPExpiration (now + I2NP_DISPATCH_MAX_FUTURE, now)); // exactly 240 s ahead
	assert (!IsFreshI2NPExpiration (now + I2NP_DISPATCH_MAX_FUTURE + 1, now));
	// A hostile 64-bit date must be rejected, not wrap into the window.
	assert (!IsFreshI2NPExpiration (UINT64_MAX, now));
	assert (!IsFreshI2NPExpiration (0, now));
}

void TestDispatchExpiryExemptsTunnelData ()
{
	// A TunnelData cell has no meaningful expiration; the endpoint
	// validates
	// the inner message after decryption.
	const uint8_t empty = 0;
	auto tunnelData = CreateI2NPMessage (eI2NPTunnelData, &empty, 0, 0x01020304);
	assert (tunnelData);
	tunnelData->SetExpiration (0);
	assert (!IsI2NPDispatchExpired (*tunnelData, 1'700'000'000'000ULL));

	// Every other type is subject to the window.
	const uint8_t payload[DELIVERY_STATUS_SIZE] = { };
	auto deliveryStatus = CreateI2NPMessage (eI2NPDeliveryStatus, payload, sizeof (payload), 1);
	assert (deliveryStatus);
	const uint64_t now = i2p::util::GetMillisecondsSinceEpoch ();
	assert (!IsI2NPDispatchExpired (*deliveryStatus, now));
	deliveryStatus->SetExpiration (now - I2NP_DISPATCH_MAX_AGE - 1);
	assert (IsI2NPDispatchExpired (*deliveryStatus, now));
	deliveryStatus->SetExpiration (now + I2NP_DISPATCH_MAX_FUTURE + 1);
	assert (IsI2NPDispatchExpired (*deliveryStatus, now));
}

void TestDuplicateKeyMatrix ()
{
	const uint32_t msgID = 0x12345678;
	const uint64_t expiration = 1'700'000'000'000ULL;

	// Same ID and same expiration second: same key.
	assert (GetI2NPDuplicateKey (msgID, expiration, I2NP_DUPLICATE_SALT_ROUTER)
		== GetI2NPDuplicateKey (msgID, expiration + 999, I2NP_DUPLICATE_SALT_ROUTER));
	// A different expiration second is a different key, so a peer cannot
	// keep
	// one ID alive by resending it with a later date.
	assert (GetI2NPDuplicateKey (msgID, expiration, I2NP_DUPLICATE_SALT_ROUTER)
		!= GetI2NPDuplicateKey (msgID, expiration + 1000, I2NP_DUPLICATE_SALT_ROUTER));
	// A different message ID is a different key.
	assert (GetI2NPDuplicateKey (msgID, expiration, I2NP_DUPLICATE_SALT_ROUTER)
		!= GetI2NPDuplicateKey (msgID + 1, expiration, I2NP_DUPLICATE_SALT_ROUTER));
	// The per-context salt separates the router-wide filter from a
	// destination-scoped clove filter.
	assert (GetI2NPDuplicateKey (msgID, expiration, I2NP_DUPLICATE_SALT_ROUTER)
		!= GetI2NPDuplicateKey (msgID, expiration, I2NP_DUPLICATE_SALT_DESTINATION));
	// TIME_MASK drops the high bits of the expiration second, so dates one
	// 2^31-second cycle apart share a key (Java DecayingHashSet
	// semantics).
	const uint64_t wrapped = expiration + (static_cast<uint64_t> (I2NP_DUPLICATE_TIME_MASK) + 1) * 1000;
	assert (GetI2NPDuplicateKey (msgID, expiration, I2NP_DUPLICATE_SALT_ROUTER)
		== GetI2NPDuplicateKey (msgID, wrapped, I2NP_DUPLICATE_SALT_ROUTER));
}

void TestDuplicateFilterDecayAndBound ()
{
	I2NPDuplicateFilter filter (2, 10); // two entries, ten-second generations
	assert (!filter.CheckAndInsert (1, 1000)); // first sighting
	assert (filter.CheckAndInsert (1, 1000)); // duplicate
	assert (!filter.CheckAndInsert (2, 1000));
	assert (filter.GetSize () == 2);

	// Rotation moves the current generation to the previous one, so an
	// entry stays visible for one more window.
	assert (!filter.CheckAndInsert (3, 1011));
	assert (filter.CheckAndInsert (1, 1011));
	assert (filter.CheckAndInsert (2, 1011));

	// Second rotation drops the oldest generation entirely.
	assert (!filter.CheckAndInsert (4, 1022));
	assert (!filter.CheckAndInsert (1, 1022));

	// The hard bound evicts instead of growing.
	I2NPDuplicateFilter small (2, 3600);
	assert (!small.CheckAndInsert (10, 1));
	assert (!small.CheckAndInsert (11, 1));
	assert (!small.CheckAndInsert (12, 1));
	assert (small.GetSize () == 2);
	assert (!small.CheckAndInsert (13, 1)); // still bounded
	assert (small.GetSize () == 2);

	// Clear resets the generations and the rotation clock.
	small.Clear ();
	assert (small.GetSize () == 0);
	assert (!small.CheckAndInsert (10, 2));
}

void TestCloveAllowListByOrigin ()
{
	// Java InboundMessageDistributor.handleClove(): a tunnel-delivered
	// garlic
	// may carry data and control replies only.
	assert (IsAllowedCloveMessageType (eI2NPData, true));
	assert (IsAllowedCloveMessageType (eI2NPDeliveryStatus, true));
	assert (IsAllowedCloveMessageType (eI2NPDatabaseStore, true));
	assert (IsAllowedCloveMessageType (eI2NPDatabaseLookup, true));
	assert (IsAllowedCloveMessageType (eI2NPShortTunnelBuildReply, true));
	assert (!IsAllowedCloveMessageType (eI2NPTunnelTest, true));
	assert (!IsAllowedCloveMessageType (eI2NPTunnelData, true));
	assert (!IsAllowedCloveMessageType (eI2NPTunnelGateway, true));
	assert (!IsAllowedCloveMessageType (eI2NPShortTunnelBuild, true));
	assert (!IsAllowedCloveMessageType (eI2NPVariableTunnelBuild, true));

	// A garlic received directly from a peer is HandleGarlicMessageJob: it
	// may additionally carry a TunnelTest, but still no tunnel/build
	// request.
	assert (IsAllowedCloveMessageType (eI2NPData, false));
	assert (IsAllowedCloveMessageType (eI2NPTunnelTest, false));
	assert (!IsAllowedCloveMessageType (eI2NPTunnelData, false));
	assert (!IsAllowedCloveMessageType (eI2NPShortTunnelBuild, false));
}

void TestDispatcherDropsExpiredAndDuplicate ()
{
	using Access = i2p::data::NetDbLifecycleTestAccess;
	auto & database = i2p::data::netdb;
	Access::Drain (database);
	Access::OpenAdmission (database);
	ResetDispatchState ();
	assert (Access::Queued (database) == 0);

	const uint8_t payload[DATABASE_STORE_HEADER_SIZE] = { };
	auto store = CreateI2NPMessage (eI2NPDatabaseStore, payload, sizeof (payload), 0xAABBCCDD);
	assert (store);
	HandleI2NPMessage (store);
	assert (Access::Queued (database) == 1);
	assert (GetI2NPDispatchCounters ().duplicate.load () == 0);

	// The same message ID and expiration second is a duplicate.
	auto copy = CreateI2NPMessage (eI2NPDatabaseStore, payload, sizeof (payload), 0xAABBCCDD);
	assert (copy);
	copy->SetExpiration (store->GetExpiration ());
	HandleI2NPMessage (copy);
	assert (Access::Queued (database) == 1);
	assert (GetI2NPDispatchCounters ().duplicate.load () == 1);

	// A different message ID still reaches the handler.
	auto other = CreateI2NPMessage (eI2NPDatabaseStore, payload, sizeof (payload), 0xAABBCCDE);
	assert (other);
	HandleI2NPMessage (other);
	assert (Access::Queued (database) == 2);

	// An expired message is dropped before the handler, even with a fresh
	// ID.
	auto expired = CreateI2NPMessage (eI2NPDatabaseStore, payload, sizeof (payload), 0xAABBCCDF);
	assert (expired);
	expired->SetExpiration (i2p::util::GetMillisecondsSinceEpoch () - I2NP_DISPATCH_MAX_AGE - 1);
	HandleI2NPMessage (expired);
	assert (Access::Queued (database) == 2);
	assert (GetI2NPDispatchCounters ().expired.load () == 1);

	// A message dated far in the future is dropped as well.
	auto future = CreateI2NPMessage (eI2NPDatabaseStore, payload, sizeof (payload), 0xAABBCCE0);
	assert (future);
	future->SetExpiration (i2p::util::GetMillisecondsSinceEpoch () + I2NP_DISPATCH_MAX_FUTURE + 1);
	HandleI2NPMessage (future);
	assert (Access::Queued (database) == 2);
	assert (GetI2NPDispatchCounters ().expired.load () == 2);

	// A clove-delivered type outside the allow-list never reaches the
	// netDb.
	auto cloveLookup = CreateI2NPMessage (eI2NPTunnelData, payload, 0, 0xAABBCCE1);
	assert (cloveLookup);
	cloveLookup->fromClove = true;
	HandleI2NPMessage (cloveLookup);
	assert (GetI2NPDispatchCounters ().disallowedCloveType.load () == 1);

	Access::Drain (database);
	assert (Access::Queued (database) == 0);
	ResetDispatchState ();
}

void TestCreateI2NPMessageBounds ()
{
	std::unique_ptr<uint8_t[]> payload (new uint8_t[I2NP_MAX_MESSAGE_SIZE + 1]);
	memset (payload.get (), 0, I2NP_MAX_MESSAGE_SIZE + 1);

	// The largest payload that fits the largest buffer is accepted...
	const size_t maxPayload = I2NP_MAX_MESSAGE_SIZE - I2NP_HEADER_SIZE - 2;
	auto fits = CreateI2NPMessage (eI2NPData, payload.get (), maxPayload, 1);
	assert (fits);
	assert (fits->GetPayloadLength () == maxPayload);

	// ... one more byte is refused instead of returning a truncated
	// message.
	auto tooLong = CreateI2NPMessage (eI2NPData, payload.get (), maxPayload + 1, 2);
	assert (!tooLong);
	assert (GetI2NPDispatchCounters ().oversizedMessage.load () >= 1);

	// A null payload with a non-zero length is refused as well.
	assert (!CreateI2NPMessage (eI2NPData, nullptr, 4, 3));

	// The tunnel-delivered factory rejects a payload that does not fit
	// rather
	// than handing back an uninitialised message.
	auto fromTunnel = CreateI2NPMessage (payload.get (), I2NP_MAX_MESSAGE_SIZE + 1, nullptr);
	assert (!fromTunnel);
	auto exact = CreateI2NPMessage (payload.get (), maxPayload, nullptr);
	assert (exact);
	assert (exact->GetLength () == maxPayload);

	ResetDispatchState ();
}

void TestStrictPayloadLengths ()
{
	// DeliveryStatus is exactly msgID + timestamp.
	assert (IsValidDeliveryStatusPayload (DELIVERY_STATUS_SIZE));
	assert (!IsValidDeliveryStatusPayload (0));
	assert (!IsValidDeliveryStatusPayload (DELIVERY_STATUS_SIZE - 1));
	assert (!IsValidDeliveryStatusPayload (DELIVERY_STATUS_SIZE + 1));

	auto deliveryStatus = CreateDeliveryStatusMsg (0x11223344);
	assert (deliveryStatus);
	assert (deliveryStatus->GetPayloadLength () == DELIVERY_STATUS_SIZE);
	assert (IsValidDeliveryStatusPayload (deliveryStatus->GetPayloadLength ()));

	// DataMessage: the declared length is the whole payload, no trailing
	// bytes.
	assert (IsValidDataMessageLength (0, 4));
	assert (IsValidDataMessageLength (10, 14));
	assert (!IsValidDataMessageLength (11, 14));
	assert (!IsValidDataMessageLength (9, 14));
	assert (!IsValidDataMessageLength (0, 3)); // cannot even hold the length
}

void TestGarlicQueueBound ()
{
	assert (MAX_INCOMING_GARLIC_QUEUE >= 64);
	for (size_t size = 0; size < MAX_INCOMING_GARLIC_QUEUE; size++) assert (!IsIncomingGarlicQueueFull (size));
	assert (IsIncomingGarlicQueueFull (MAX_INCOMING_GARLIC_QUEUE));
	assert (IsIncomingGarlicQueueFull (MAX_INCOMING_GARLIC_QUEUE + 1000));
}
} // namespace

int main ()
{
	TestDispatcherExpirationWindow ();
	TestDispatchExpiryExemptsTunnelData ();
	TestDuplicateKeyMatrix ();
	TestDuplicateFilterDecayAndBound ();
	TestCloveAllowListByOrigin ();
	TestDispatcherDropsExpiredAndDuplicate ();
	TestCreateI2NPMessageBounds ();
	TestStrictPayloadLengths ();
	TestGarlicQueueBound ();
	return 0;
}
