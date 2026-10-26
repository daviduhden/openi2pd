/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: fuzz SAM control.
 *
 * Exercises the bounded SAM control framing, parameter parser and HELLO
 * version negotiation on untrusted text. The harness never opens a socket or
 * starts a destination.
 *
 */

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "SAM.h"

extern "C" int LLVMFuzzerTestOneInput (const uint8_t * data, size_t size)
{
	if (!data) return 0;
	const std::string_view input (reinterpret_cast<const char *> (data), size);
	int version = 0;
	(void) i2p::client::NegotiateSAMHandshake (input, version);
	(void) i2p::client::ParseSAMParams (input);
	i2p::client::SAMFrame frame;
	(void) i2p::client::GetSAMNextFrame (input, 32768, frame);
	return 0;
}
