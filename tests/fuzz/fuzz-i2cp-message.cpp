/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: fuzz I2CP messages.
 *
 * Exercises the pure I2CP SEND MESSAGE parser, including the optional
 * expiration field. The harness never opens a listener or a session.
 *
 */

#include <cstddef>
#include <cstdint>

#include "I2CP.h"

extern "C" int LLVMFuzzerTestOneInput (const uint8_t * data, size_t size)
{
	if (!data || size < 2) return 0;
	i2p::client::I2CPSendMessageRequest request;
	(void) i2p::client::ParseI2CPSendMessage (data + 1, size - 1, (data[0] & 1) != 0, request);
	return 0;
}
