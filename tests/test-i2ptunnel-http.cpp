/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: HTTP tunnel incremental parsing regressions.
 */
#include <cassert>
#include <string>
#include <string_view>
#include "I2PTunnelHTTP.h"

using Parser = i2p::client::I2PTunnelHTTPHeader;
using Result = Parser::Result;

static void EverySplit (Parser::Mode mode, const std::string & message, const std::string & expected,
	std::string host = { }, std::string identity = { })
{
	// Fragment only the header. Once complete, production forwards subsequent
	// body buffers directly rather than passing them back through the parser.
	const auto end = message.find ("\r\n\r\n") + 4;
	for (size_t split = 0; split < end; ++split)
	{
		Parser parser (mode, host, identity);
		assert (parser.Feed (std::string_view (message).substr (0, split)) == Result::Incomplete);
		assert (parser.Feed (std::string_view (message).substr (split)) == Result::Complete);
		assert (parser.Output () == expected);
		const auto * data = parser.Output ().data ();
		assert (parser.Feed ("ignored after completion") == Result::Complete);
		assert (parser.Output () == expected && parser.Output ().data () == data);
	}
	Parser bytes (mode, host, identity);
	for (size_t i = 0; i < end - 1; ++i)
		assert (bytes.Feed (std::string_view (message).substr (i, 1)) == Result::Incomplete);
	assert (bytes.Feed (std::string_view (message).substr (end - 1)) == Result::Complete);
	assert (bytes.Output () == expected);
}

int main ()
{
	const std::string body ("a\0b\r\n", 6);
	EverySplit (Parser::Mode::ClientRequest, "GET / HTTP/1.1\r\nHost: peer\r\nConnection: Upgrade\r\n\r\n" + body,
		"GET / HTTP/1.1\r\nHost: peer\r\nConnection: Upgrade\r\nProxy-Connection: close\r\n\r\n" + body);
	EverySplit (Parser::Mode::ClientRequest,
		"GET / HTTP/1.1\r\nConnection: keep-alive\r\nProxy-Connection: keep-alive\r\n\r\n",
		"GET / HTTP/1.1\r\nConnection: close\r\nProxy-Connection: close\r\n\r\n");
	EverySplit (Parser::Mode::ClientRequest, "GET / HTTP/1.1\r\npRoXy-CoNnEcTiOn: keep-alive\r\n\r\n",
		"GET / HTTP/1.1\r\nProxy-Connection: close\r\n\r\n");
	EverySplit (Parser::Mode::ServerRequest,
		"GET / HTTP/1.1\r\nHost: peer\r\nConnection: Upgrade\r\nKeep-Alive: 10\r\nX-I2P-DestHash: forged\r\n\r\n"
			+ body,
		"GET / HTTP/1.1\r\nHost: local\r\nConnection: Upgrade\r\nX-I2P-DestHash: trusted\r\n\r\n" + body, "local",
		"X-I2P-DestHash: trusted\r\n");
	EverySplit (Parser::Mode::ServerResponse,
		"HTTP/1.1 200 OK\r\nServer: private\r\nDate: now\r\nX-Runtime: 10\r\nX-Powered-By: private\r\nProxy-Foo: "
		"private\r\nContent-Length: 6\r\n\r\n"
			+ body,
		"HTTP/1.1 200 OK\r\nContent-Length: 6\r\n\r\n" + body);
	// Header-name matching is ASCII case-insensitive. The trusted identity
	// header is re-injected after the request headers, never accepted from the
	// untrusted client spelling.
	EverySplit (Parser::Mode::ServerRequest,
		"GET / HTTP/1.1\r\nhOsT: peer\r\nkEeP-aLiVe: 1\r\nx-i2p-desthash: forged\r\ncOnNeCtIoN: uPgRaDe\r\n\r\n" + body,
		"GET / HTTP/1.1\r\nHost: local\r\ncOnNeCtIoN: uPgRaDe\r\nX-I2P-DestHash: trusted\r\n\r\n" + body, "local",
		"X-I2P-DestHash: trusted\r\n");
	EverySplit (Parser::Mode::ServerRequest, "GET / HTTP/1.1\r\nconnection: upgrade\r\n\r\n",
		"GET / HTTP/1.1\r\nconnection: upgrade\r\n\r\n");

	// Request output is borrowed by async_write while independent TCP response
	// fragments arrive. Response state must never overwrite request storage.
	Parser request (Parser::Mode::ServerRequest), response (Parser::Mode::ServerResponse);
	assert (request.Feed ("GET / HTTP/1.1\r\nConnection: Upgrade\r\n\r\n") == Result::Complete);
	const auto saved = request.Output ();
	const auto * borrowed = request.Output ().data ();
	assert (response.Feed ("HTTP/1.1 200 OK\r\n") == Result::Incomplete);
	assert (response.Feed ("Content-Length: 0\r\n\r\n") == Result::Complete);
	assert (response.Output () == "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
	assert (request.Output () == saved && request.Output ().data () == borrowed);

	for (size_t bytes : { 8192u, 8193u })
	{
		Parser parser (Parser::Mode::ServerResponse);
		std::string header = "HTTP/1.1 200 OK\r\nX: ";
		header += std::string (bytes - header.size () - 4, 'a');
		header += "\r\n\r\n";
		const auto expected = bytes == 8192 ? Result::Complete : Result::Rejected;
		assert (parser.Feed (header) == expected);
		if (expected == Result::Rejected) assert (parser.Feed ("\r\n") == Result::Rejected);
	}
	for (size_t lines : { 100u, 101u })
	{
		Parser parser (Parser::Mode::ServerResponse);
		std::string header = "HTTP/1.1 200 OK\r\n";
		for (size_t i = 1; i < lines; ++i) header += "X: a\r\n";
		header += "\r\n";
		assert (parser.Feed (header) == (lines == 100 ? Result::Complete : Result::Rejected));
	}
	for (size_t lines : { 99u, 100u })
	{
		Parser parser (Parser::Mode::ClientRequest);
		std::string header = "GET / HTTP/1.1\r\n";
		for (size_t i = 1; i < lines; ++i) header += "X: a\r\n";
		header += "\r\n";
		assert (parser.Feed (header) == (lines == 99 ? Result::Complete : Result::Rejected));
	}
	Parser unterminated (Parser::Mode::ServerResponse);
	for (size_t i = 0; i < 8192; ++i) assert (unterminated.Feed ("a") == Result::Incomplete);
	assert (unterminated.Feed ("a") == Result::Rejected);
	Parser expansion (Parser::Mode::ServerRequest, { }, std::string (8193, 'a'));
	assert (expansion.Feed ("GET / HTTP/1.1\r\n\r\n") == Result::Rejected);
	Parser folded (Parser::Mode::ServerRequest);
	assert (folded.Feed ("GET / HTTP/1.1\r\n X-I2P-DestHash: forged\r\n\r\n") == Result::Rejected);
	Parser tabbed (Parser::Mode::ClientRequest);
	assert (tabbed.Feed ("GET / HTTP/1.1\r\n\tProxy-Connection: keep-alive\r\n\r\n") == Result::Rejected);
	return 0;
}
