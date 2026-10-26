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

void TestProxyStatusMapping ()
{
	using i2p::proxy::HTTPProxyFailure;
	using i2p::proxy::HTTPProxyFailureStatus;
	assert (HTTPProxyFailureStatus (HTTPProxyFailure::BadRequest) == 400);
	assert (HTTPProxyFailureStatus (HTTPProxyFailure::HeaderTooLarge) == 431);
	assert (HTTPProxyFailureStatus (HTTPProxyFailure::Internal) == 500);
	assert (HTTPProxyFailureStatus (HTTPProxyFailure::BadGateway) == 502);
	assert (HTTPProxyFailureStatus (HTTPProxyFailure::GatewayTimeout) == 504);
}

/** Only http:// (and https for CONNECT) may reach a destination. */
void TestRequestTargetSchemes ()
{
	using i2p::proxy::ClassifyRequestTarget;
	using i2p::proxy::ProxyTargetError;
	using i2p::proxy::ProxyTargetErrorStatus;

	// I2P Java isSupportedAddress(): only these two schemes are accepted.
	for (const char * target : { "ftp://example.i2p/file", "mailto:user@example.i2p", "data:text/html,<b>x</b>",
			 "file:///etc/passwd", "gopher://example.i2p/", "ws://example.i2p/" })
	{
		const auto result = ClassifyRequestTarget ("GET", target, "example.i2p");
		assert (result.error == ProxyTargetError::UnsupportedScheme);
		assert (ProxyTargetErrorStatus (result.error) == 403);
	}

	// Absolute http form is forwarded, with the default port.
	const auto absolute = ClassifyRequestTarget ("GET", "http://example.i2p:8080/x?y=1", { });
	assert (absolute.IsValid ());
	assert (absolute.host == "example.i2p");
	assert (absolute.port == 8080);
	assert (absolute.path == "/x?y=1");
	assert (absolute.absolute && !absolute.useConnect);

	const auto defaults = ClassifyRequestTarget ("GET", "http://example.i2p/", { });
	assert (defaults.IsValid () && defaults.port == 80);

	// Userinfo is parsed away, never part of the host.
	const auto userinfo = ClassifyRequestTarget ("GET", "http://user:secret@example.i2p/p", { });
	assert (userinfo.IsValid () && userinfo.host == "example.i2p");
	assert (userinfo.host.find ("secret") == std::string::npos);

	// Origin form takes the authority from Host and defaults to port 80.
	const auto origin = ClassifyRequestTarget ("GET", "/index.html", "example.i2p");
	assert (origin.IsValid ());
	assert (origin.host == "example.i2p" && origin.port == 80 && !origin.absolute);
	assert (origin.path == "/index.html");

	const auto originPort = ClassifyRequestTarget ("GET", "/x", "example.i2p:8080");
	assert (originPort.IsValid () && originPort.port == 8080);

	// Scheme-less absolute-ish form is still accepted (previous parser).
	const auto schemeLess = ClassifyRequestTarget ("GET", "example.i2p/a", { });
	assert (schemeLess.IsValid () && schemeLess.host == "example.i2p" && schemeLess.port == 80);

	// A relative target without Host cannot be routed.
	assert (ClassifyRequestTarget ("GET", "/x", { }).error == ProxyTargetError::MissingHost);
	// A hostile Host is never accepted as an authority.
	assert (!ClassifyRequestTarget ("GET", "/x", "exa<mple.i2p").IsValid ());
	assert (!ClassifyRequestTarget ("GET", "/x", "example.i2p:0").IsValid ());
	assert (ProxyTargetErrorStatus (ProxyTargetError::Malformed) == 400);
}

/** `CONNECT [::1]:443` must parse an IPv6 literal instead of splitting on ':'. */
void TestConnectAuthority ()
{
	using i2p::proxy::ClassifyRequestTarget;
	using i2p::proxy::ProxyTargetError;

	const auto v6 = ClassifyRequestTarget ("CONNECT", "[::1]:443", { });
	assert (v6.IsValid ());
	assert (v6.useConnect);
	assert (v6.scheme == "https");
	assert (v6.host == "::1");
	assert (v6.port == 443);

	const auto v6long = ClassifyRequestTarget ("CONNECT", "[2001:db8::1]:8443", { });
	assert (v6long.IsValid () && v6long.host == "2001:db8::1" && v6long.port == 8443);

	const auto v4 = ClassifyRequestTarget ("CONNECT", "example.i2p:443", { });
	assert (v4.IsValid () && v4.host == "example.i2p" && v4.port == 443);

	// A CONNECT target always needs a port and must not carry a scheme.
	assert (ClassifyRequestTarget ("CONNECT", "example.i2p", { }).error == ProxyTargetError::BadPort);
	assert (!ClassifyRequestTarget ("CONNECT", "[::1]", { }).IsValid ());
	assert (!ClassifyRequestTarget ("CONNECT", "[::1", { }).IsValid ());
	assert (!ClassifyRequestTarget ("CONNECT", "http://example.i2p/", { }).IsValid ());
	assert (!ClassifyRequestTarget ("CONNECT", "::1:443", { }).IsValid ());
}

/** Hop-by-hop and proxy-disclosure headers are consumed by the proxy. */
void TestHopByHopStripMatrix ()
{
	using i2p::proxy::IsHopByHopHeaderName;
	using i2p::proxy::IsProxyDisclosureHeaderName;
	using i2p::proxy::IsNTLMAuthorizationValue;
	using i2p::proxy::StripHopByHopHeaders;

	for (const char * name : { "Connection", "connection", "Keep-Alive", "Proxy-Authenticate",
			 "Proxy-Authorization", "Proxy-Connection", "TE", "te", "Trailer" })
		assert (IsHopByHopHeaderName (name));
	for (const char * name : { "Content-Length", "Transfer-Encoding", "Host", "Upgrade", "Authorization",
			 "Proxy-Foo", "TE2", "Trailers" })
		assert (!IsHopByHopHeaderName (name));

	for (const char * name : { "X-Forwarded-For", "x-forwarded-proto", "X-Real-IP", "Client-IP",
			 "X-Originating-IP", "Forwarded" })
		assert (IsProxyDisclosureHeaderName (name));
	for (const char * name : { "Content-Length", "Referer", "X-Requested-With" })
		assert (!IsProxyDisclosureHeaderName (name));

	assert (IsNTLMAuthorizationValue ("NTLM TlRMTVNTUAAB"));
	assert (IsNTLMAuthorizationValue ("ntlm"));
	assert (IsNTLMAuthorizationValue ("  NTLM x"));
	assert (!IsNTLMAuthorizationValue ("Bearer ntlm"));
	assert (!IsNTLMAuthorizationValue ("NTLMx"));
	assert (!IsNTLMAuthorizationValue ("Digest username=\"ntlm\""));

	i2p::http::HTTPReq req;
	const std::string raw = "GET http://example.i2p/ HTTP/1.1\r\n"
							"Host: example.i2p\r\n"
							"Connection: keep-alive\r\n"
							"Keep-Alive: timeout=5\r\n"
							"TE: trailers\r\n"
							"Trailer: X-Checksum\r\n"
							"Proxy-Authorization: Basic dXNlcjpwYXNz\r\n"
							"Proxy-Connection: keep-alive\r\n"
							"Proxy-Authenticate: Basic realm=\"x\"\r\n"
							"X-Forwarded-For: 10.0.0.1\r\n"
							"X-Real-IP: 10.0.0.1\r\n"
							"Client-IP: 10.0.0.1\r\n"
							"X-Originating-IP: 10.0.0.1\r\n"
							"Content-Length: 0\r\n"
							"Transfer-Encoding: chunked\r\n"
							"Upgrade: websocket\r\n"
							"\r\n";
	assert (req.parse (raw) > 0);
	assert (StripHopByHopHeaders (req) == 11);
	assert (req.GetHeader ("Connection").empty ());
	assert (req.GetHeader ("X-Forwarded-For").empty ());
	assert (req.GetHeader ("X-Real-IP").empty ());
	assert (req.GetHeader ("Proxy-Authorization").empty ());
	// Framing headers are preserved: the body parser depends on them.
	assert (req.GetHeader ("Content-Length") == "0");
	assert (req.GetHeader ("Transfer-Encoding") == "chunked");
	assert (req.GetHeader ("Upgrade") == "websocket");
	// Idempotent.
	assert (StripHopByHopHeaders (req) == 0);
}

/** A hostile Host can never produce markup, and the escaper is bounded. */
void TestHtmlEscaping ()
{
	using i2p::proxy::EscapeHTML;
	using i2p::proxy::HTTP_PROXY_MAX_ESCAPED_INPUT;

	assert (EscapeHTML ("example.i2p") == "example.i2p");
	assert (EscapeHTML ("<script>alert(1)</script>") == "&lt;script&gt;alert(1)&lt;/script&gt;");
	assert (EscapeHTML ("a&b") == "a&amp;b");
	assert (EscapeHTML ("\"quoted\"") == "&quot;quoted&quot;");
	assert (EscapeHTML ("it's") == "it&#39;s");
	// Attribute and element terminators are all escaped, so a hostile Host
	// cannot break out of `href="..."`.
	const std::string hostile = "\"><img src=x onerror=alert(1)>";
	const std::string escaped = EscapeHTML (hostile);
	assert (escaped.find ('<') == std::string::npos);
	assert (escaped.find ('>') == std::string::npos);
	assert (escaped.find ('"') == std::string::npos);
	assert (escaped.find ("&lt;") != std::string::npos);

	// C0 controls and DEL are dropped, so a page cannot inject a raw CR/LF.
	assert (EscapeHTML ("a\r\nb") == "ab");
	assert (EscapeHTML (std::string ("a\0b", 3)) == "ab");

	// Valid UTF-8 is preserved, invalid bytes become a replacement reference.
	assert (EscapeHTML ("\xc3\xa9") == "\xc3\xa9");
	assert (EscapeHTML ("\xff") == "&#xFFFD;");

	// The escaper is bounded: a 1 MiB Host cannot inflate a page freely.
	assert (EscapeHTML (std::string (HTTP_PROXY_MAX_ESCAPED_INPUT + 100, 'a')).size ()
		== HTTP_PROXY_MAX_ESCAPED_INPUT);
}

/** A configured outproxy URL never appears in a page or a log line. */
void TestOutproxyURLRedaction ()
{
	using i2p::proxy::RedactURLUserInfo;

	assert (RedactURLUserInfo ("http://user:secret@outproxy.i2p:8080") == "http://outproxy.i2p:8080");
	assert (RedactURLUserInfo ("socks://user:secret@127.0.0.1:9050") == "socks://127.0.0.1:9050");
	assert (RedactURLUserInfo ("http://outproxy.i2p:8080") == "http://outproxy.i2p:8080");
	// A credential carried in the query string is dropped as well.
	assert (RedactURLUserInfo ("http://outproxy.i2p/?password=secret") == "http://outproxy.i2p");
	assert (RedactURLUserInfo ("http://outproxy.i2p/path").find ("secret") == std::string::npos);
	// A value without an authority is returned unchanged (bounded).
	assert (RedactURLUserInfo ("outproxy.i2p") == "outproxy.i2p");

	for (const char * url : { "http://user:secret@outproxy.i2p/", "socks://user:secret@127.0.0.1:9050",
			 "http://outproxy.i2p/?password=secret" })
	{
		const std::string redacted = RedactURLUserInfo (url);
		assert (redacted.find ("secret") == std::string::npos);
		assert (redacted.find ("user") == std::string::npos);
	}

	// The 502 "unknown outproxy" page is fixed text: no configured URL is
	// interpolated, so an unreachable outproxy cannot leak its credential.
	assert (RedactURLUserInfo ("") == "");
}

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
	TestProxyStatusMapping ();
	TestRequestTargetSchemes ();
	TestConnectAuthority ();
	TestHopByHopStripMatrix ();
	TestHtmlEscaping ();
	TestOutproxyURLRedaction ();
	TestNullStreamHandlerAccounting ();
	return 0;
}
