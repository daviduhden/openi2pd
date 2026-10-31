/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: fuzz I2NP/garlic dispatch policy.
 *
 * Exercises the pure predicates that decide whether an untrusted
 * message may reach a handler: expiration, router-wide duplicate
 * suppression, clove type admission, clove count, DataMessage and
 * DeliveryStatus lengths. It never creates a router, socket or thread,
 * so a finding is a policy/framing bug rather than a networking
 * artifact.
 *
 */

#include <cstddef>
#include <cstdint>

#include "Destination.h"
#include "Garlic.h"
#include "I2NPProtocol.h"

namespace
{
uint32_t ReadU32 (const uint8_t * data, size_t size, size_t offset)
{
	uint32_t value = 0;
	for (size_t i = 0; i < 4 && offset + i < size; ++i) value |= static_cast<uint32_t> (data[offset + i]) << (8 * i);
	return value;
}
} // namespace

extern "C" int LLVMFuzzerTestOneInput (const uint8_t * data, size_t size)
{
	if (!data || size < 16) return 0;
	const uint64_t now = 1700000000000ULL + (data[0] % 16) * 60000ULL;
	const uint64_t expiration = now + static_cast<int64_t> (static_cast<int8_t> (data[1])) * 1000LL;
	const uint32_t msgID = ReadU32 (data, size, 2);
	const uint32_t salt = ReadU32 (data, size, 6);
	const bool fromTunnel = (data[10] & 1) != 0;
	const bool fromClove = (data[10] & 2) != 0;
	const uint8_t type = data[11];
	const unsigned int cloves = data[12];
	const uint32_t declared = ReadU32 (data, size, 13);

	(void)i2p::IsFreshI2NPExpiration (expiration, now);
	(void)i2p::GetI2NPDuplicateKey (msgID, expiration, salt);
	(void)i2p::IsValidDeliveryStatusPayload (size);
	(void)i2p::garlic::IsValidCloveCount (cloves);
	(void)i2p::garlic::IsValidCloveCount (cloves & 0x1F);
	(void)i2p::client::IsValidDataMessageLength (declared, size);
	(void)i2p::IsAllowedCloveMessageType (type, fromTunnel);
	(void)i2p::IsAllowedCloveMessageType (type);

	// Exercise the bounded filter itself, including generation rotation
	// and eviction, with a tiny capacity so the input can reach both
	// branches.
	i2p::I2NPDuplicateFilter filter (size % 64 + 1, 1 + (size % 4));
	(void)filter.CheckAndInsert (i2p::GetI2NPDuplicateKey (msgID, expiration, salt), now / 1000);
	(void)filter.CheckAndInsert (i2p::GetI2NPDuplicateKey (msgID, expiration, salt), now / 1000 + 1);
	(void)filter.CheckAndInsert (i2p::GetI2NPDuplicateKey (msgID ^ 0xFFFFFFFFU, expiration, salt), now / 1000 + 8);
	(void)filter.GetSize ();
	(void)fromClove; // clove origin is handled by the destination-specific filter
	return 0;
}
