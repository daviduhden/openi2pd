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
#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <string_view>

#include "HTTP.h"
#include "I2PTunnelHTTP.h"

extern "C" int LLVMFuzzerTestOneInput (const uint8_t * data, size_t size)
{
	std::string_view input (reinterpret_cast<const char *> (data), size);
	i2p::http::HTTPReq request;
	(void)request.parse (input);
	i2p::http::HTTPRes response;
	(void)response.parse (input);
	(void)i2p::http::UrlDecode (input);

	std::istringstream in { std::string (input) };
	std::ostringstream out;
	(void)i2p::http::MergeChunkedResponse (in, out);

	// Compare the production tunnel transformer under whole-buffer and
	// fragmented delivery. After the first header, production forwards body
	// bytes directly, so include that untouched tail in the comparison.
	using Parser = i2p::client::I2PTunnelHTTPHeader;
	const auto tunnelInput = input.substr (0, i2p::client::I2P_TUNNEL_HTTP_MAX_HEADER_SIZE + 4096);
	for (auto mode : { Parser::Mode::ClientRequest, Parser::Mode::ServerRequest, Parser::Mode::ServerResponse })
	{
		Parser whole (mode, "local", "X-I2P-DestHash: trusted\r\n");
		Parser fragmented (mode, "local", "X-I2P-DestHash: trusted\r\n");
		const auto expected = whole.Feed (tunnelInput);
		size_t offset = 0;
		while (offset < tunnelInput.size () && fragmented.State () == Parser::Result::Incomplete)
		{
			const size_t chunk = std::min (
				tunnelInput.size () - offset, size_t { 1 } + static_cast<unsigned char> (tunnelInput[offset]) % 64);
			(void)fragmented.Feed (tunnelInput.substr (offset, chunk));
			offset += chunk;
		}
		if (fragmented.State () != expected) std::abort ();
		if (expected == Parser::Result::Complete)
		{
			auto output = fragmented.Output ();
			output.append (tunnelInput.substr (offset));
			if (output != whole.Output ()) std::abort ();
		}
	}
	return 0;
}
