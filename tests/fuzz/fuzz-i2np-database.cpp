/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: fuzz I2NP database messages.
 *
 * Exercises the pure DatabaseSearchReply parser on untrusted payload bytes.
 * The harness does not create a router, perform a lookup or contact a peer.
 *
 */

#include <cstddef>
#include <cstdint>

#include "I2NPProtocol.h"

extern "C" int LLVMFuzzerTestOneInput (const uint8_t * data, size_t size)
{
	if (!data || size < 65) return 0;
	i2p::DatabaseSearchReply reply;
	(void) i2p::ParseDatabaseSearchReply (data, size, reply);
	return 0;
}
