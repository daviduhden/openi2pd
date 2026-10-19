/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: fuzz privsep protocol.
 *
 * Feeds arbitrary input to the privilege-separation frame decoder and both log payload decoders, covering magic, version, type, declared-length, string-length and trailing-byte checks without I/O.
 *
 */

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "privsep/Protocol.h"

extern "C" int LLVMFuzzerTestOneInput (const uint8_t * data, size_t size)
{
	std::string_view input (reinterpret_cast<const char *> (data), size);
	(void)i2p::privsep::Decode (input);
	(void)i2p::privsep::DecodeLogRecord (input);
	(void)i2p::privsep::DecodeLogConfig (input);
	return 0;
}
