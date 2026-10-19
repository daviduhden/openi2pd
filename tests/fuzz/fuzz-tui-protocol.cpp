/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: fuzz tui protocol.
 *
 * Parses arbitrary TUI request and response headers, then re-frames accepted requests and escaped fields, exercising line/payload limits and symmetric escaping without a daemon.
 *
 */

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "TuiProtocol.h"

extern "C" int LLVMFuzzerTestOneInput (const uint8_t * data, size_t size)
{
	std::string_view input (reinterpret_cast<const char *> (data), size);
	auto request = i2p::tui::ParseRequest (input);
	if (request)
	{
		// Exercise the same bounded formatting paths used by the daemon.
		std::string payload;
		i2p::tui::AppendField (payload, "fuzz", input);
		auto response = i2p::tui::MakeResponse (i2p::tui::ResponseStatus::Ok, payload);
		auto newline = response.find ('\n');
		if (newline != std::string::npos)
			(void)i2p::tui::ParseResponseHeader (std::string_view (response).substr (0, newline));
	}
	(void)i2p::tui::ParseResponseHeader (input);
	return 0;
}
