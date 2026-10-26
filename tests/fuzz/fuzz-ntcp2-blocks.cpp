/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: fuzz NTCP2 blocks.
 *
 * Exercises the pure SessionConfirmed payload validator and the handshake
 * padding table. No socket, session or global router state is touched, so a
 * finding is a framing/padding bug rather than a networking artifact.
 *
 */

#include <cstddef>
#include <cstdint>

#include "NTCP2.h"

extern "C" int LLVMFuzzerTestOneInput (const uint8_t * data, size_t size)
{
	if (!data || size < 2) return 0;
	size_t routerInfoDataSize = 0;
	(void)i2p::transport::ValidateNTCP2SessionConfirmedPayload (data + 1, size - 1, routerInfoDataSize);
	const int version = data[0] % 5;
	for (bool sessionCreated : { false, true })
	{
		(void)i2p::transport::NTCP2IsValidHandshakePadding (version, sessionCreated, size);
		(void)i2p::transport::NTCP2IsValidHandshakePadding (version, sessionCreated, size + 1);
	}
	return 0;
}
