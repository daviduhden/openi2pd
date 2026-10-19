/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: fuzz http parser.
 *
 * Feeds arbitrary bytes to HTTP request/response parsing, URL decoding and chunked-response merging, exercising header syntax, framing and chunk-length validation without sockets.
 *
 */

#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string_view>

#include "HTTP.h"

extern "C" int LLVMFuzzerTestOneInput (const uint8_t * data, size_t size)
{
	std::string_view input (reinterpret_cast<const char *> (data), size);
	i2p::http::HTTPReq request;
	(void)request.parse (input);
	i2p::http::HTTPRes response;
	(void)response.parse (input);
	(void)i2p::http::UrlDecode (input);

	std::istringstream in {std::string (input)};
	std::ostringstream out;
	(void)i2p::http::MergeChunkedResponse (in, out);
	return 0;
}
