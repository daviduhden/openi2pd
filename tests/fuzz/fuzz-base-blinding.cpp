/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: fuzz base blinding.
 *
 * Fuzzes the Base32/Base64 decoders and the proposal 149 b33 parser
 * using fixed output buffers, so malformed or overlong addresses must
 * fail closed without out-of-bounds reads.
 *
 */

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "Base.h"
#include "Blinding.h"

extern "C" int LLVMFuzzerTestOneInput (const uint8_t * data, size_t size)
{
	const std::string_view input (reinterpret_cast<const char *> (data), size);
	uint8_t output[128] = { };

	(void)i2p::data::Base64ToByteStream (input, output, sizeof (output));
	(void)i2p::data::Base32ToByteStream (input, output, sizeof (output));

	const i2p::data::BlindedPublicKey key (input);
	(void)key.IsValid ();
	(void)key.ToB33 ();
	return 0;
}
