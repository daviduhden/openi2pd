/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test http proxy limits.
 *
 * Exercises the HTTP proxy receive-header ceiling at and above its boundary and handler accounting when CreateStream() completes with a null stream, using no listener or peer.
 *
 */

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "HTTP.h"
#include "I2PService.h"
#include "HTTPProxy.h"

namespace
{
using i2p::proxy::HTTP_PROXY_MAX_HEADER_SIZE;
using i2p::proxy::HTTPProxyHeaderLimitExceeded;

/** Build a syntactically valid request whose header is exactly `total` bytes. */
std::string MakeRequestWithHeaderSize (std::size_t total)
{
	const std::string prefix = "GET / HTTP/1.1\r\nHost: example.i2p\r\nX-Fill: ";
	const std::string suffix = "\r\n\r\n";
	assert (total > prefix.size () + suffix.size ());

	std::string request = prefix;
	request.append (total - prefix.size () - suffix.size (), 'a');
	request.append (suffix);
	assert (request.size () == total);
	return request;
}

void TestHeaderLimitBoundary ()
{
	static_assert (HTTP_PROXY_MAX_HEADER_SIZE == 1024 * 1024, "the documented header ceiling is 1 MiB");

	assert (!HTTPProxyHeaderLimitExceeded (0, 0));
	assert (!HTTPProxyHeaderLimitExceeded (0, HTTP_PROXY_MAX_HEADER_SIZE));
	assert (HTTPProxyHeaderLimitExceeded (0, HTTP_PROXY_MAX_HEADER_SIZE + 1));
	assert (!HTTPProxyHeaderLimitExceeded (HTTP_PROXY_MAX_HEADER_SIZE - 1, 1));
	assert (HTTPProxyHeaderLimitExceeded (HTTP_PROXY_MAX_HEADER_SIZE - 1, 2));
	assert (HTTPProxyHeaderLimitExceeded (HTTP_PROXY_MAX_HEADER_SIZE, 1));
	assert (HTTPProxyHeaderLimitExceeded (HTTP_PROXY_MAX_HEADER_SIZE + 1, 0));
}

/**
 * The parser must stay incomplete while the header is being accumulated and
 * must succeed once the end-of-headers marker arrives at exactly 1 MiB.
 */
void TestIncrementalParserAtHeaderLimit ()
{
	const std::string request = MakeRequestWithHeaderSize (HTTP_PROXY_MAX_HEADER_SIZE);
	constexpr std::size_t CHUNK = 8192;

	i2p::http::HTTPReq parsed;
	std::string buffered;
	bool sawIncomplete = false;
	int result = -1;

	for (std::size_t offset = 0; offset < request.size (); offset += CHUNK)
	{
		const std::size_t count = std::min (CHUNK, request.size () - offset);
		const bool complete = buffered.find ("\r\n\r\n") != std::string::npos;
		if (!complete)
		{
			// The handler checks the ceiling before appending.
			assert (!HTTPProxyHeaderLimitExceeded (buffered.size (), count));
		}

		buffered.append (request.data () + offset, count);
		result = parsed.parse (buffered);
		if (result == 0)
			sawIncomplete = true;
		else
			assert (result == static_cast<int> (buffered.size ()));
	}

	assert (sawIncomplete);
	assert (result == static_cast<int> (HTTP_PROXY_MAX_HEADER_SIZE));
	assert (parsed.method == "GET");
	assert (parsed.uri == "/");
	assert (parsed.GetHeader ("Host") == "example.i2p");
}

/**
 * A peer that never terminates its header must be rejected before the buffer
 * grows past the ceiling. This mirrors HTTPReqHandler::HandleSockRecv(), which
 * calls HTTPProxyHeaderLimitExceeded() before every append.
 */
void TestOversizedHeaderRejected ()
{
	const std::string sender = std::string (HTTP_PROXY_MAX_HEADER_SIZE + 8192, 'b');
	constexpr std::size_t CHUNK = 8192;

	std::size_t buffered = 0;
	bool rejected = false;
	for (std::size_t offset = 0; offset < sender.size () && !rejected; offset += CHUNK)
	{
		const std::size_t count = std::min (CHUNK, sender.size () - offset);
		if (HTTPProxyHeaderLimitExceeded (buffered, count))
		{
			rejected = true;
			break;
		}
		buffered += count;
	}

	assert (rejected);
	assert (buffered <= HTTP_PROXY_MAX_HEADER_SIZE);
}

/**
 * Mirrors HTTPReqHandler's finalisation contract: a null stream closes the
 * handler state, leaves the owner's handler set and is safe to repeat.
 */
class NullStreamHandler final : public i2p::client::I2PServiceHandler,
								public std::enable_shared_from_this<NullStreamHandler>
{
public:
	explicit NullStreamHandler (std::shared_ptr<i2p::client::I2PService> owner)
		: I2PServiceHandler (std::move (owner))
	{
	}

	void Handle () override
	{
		auto owner = GetOwner ();
		if (!owner) return;

		owner->CreateStream ([self = shared_from_this ()] (std::shared_ptr<i2p::stream::Stream> stream)
			{ self->HandleStreamComplete (stream); },
			// Not .i2p, not .b32.i2p and not base64: AddressBook::GetAddress()
			// returns nullptr without starting a lookup.
			"nonexistent.invalid");
	}

	unsigned int CloseCount () const { return m_CloseCount; }

private:
	void HandleStreamComplete (std::shared_ptr<i2p::stream::Stream> stream)
	{
		if (stream) stream->AsyncClose ();

		const bool alreadyDead = Kill ();
		Close ();
		if (!alreadyDead) Done (shared_from_this ());
	}

	void Close ()
	{
		if (m_SocketClosed) return;
		m_SocketClosed = true;
		++m_CloseCount;
	}

	bool m_SocketClosed = false;
	unsigned int m_CloseCount = 0;
};

void TestNullStreamHandlerAccounting ()
{
	// I2PService allocates a transient local destination (no network I/O and
	// no listener) so CreateStream() can return through its normal callback.
	auto owner = std::make_shared<i2p::client::I2PService> ();
	auto handler = std::make_shared<NullStreamHandler> (owner);
	std::weak_ptr<NullStreamHandler> weak = handler;

	owner->AddHandler (handler);
	handler->Handle ();
	assert (handler->CloseCount () == 1);

	// A second completion must not close twice or resurrect the handler.
	handler->Handle ();
	assert (handler->CloseCount () == 1);

	handler.reset ();
	assert (weak.expired ()); // the owner's set no longer accounts for it

	owner->ClearHandlers (); // safe and idempotent with an empty set
}
}

int main ()
{
	TestHeaderLimitBoundary ();
	TestIncrementalParserAtHeaderLimit ();
	TestOversizedHeaderRejected ();
	TestNullStreamHandlerAccounting ();
	return 0;
}
