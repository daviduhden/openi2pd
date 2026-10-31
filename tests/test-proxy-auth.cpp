/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test proxy auth.
 *
 * Exercises local HTTP proxy Basic and Digest authentication (parsing,
 * bounds, constant time comparison, 407 challenges, nonce
 * replay/staleness and no credential forwarding) and the SOCKS5 RFC
 * 1929 authenticator with its method-selection state machine. No
 * socket, listener or peer is used.
 *
 */

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "HTTP.h"
#include "I2PService.h"
#include "HTTPProxy.h"
#include "SOCKS.h"

namespace
{
using i2p::proxy::HTTP_PROXY_AUTH_CHALLENGE;
using i2p::proxy::HTTP_PROXY_AUTH_REALM;
using i2p::proxy::HTTP_PROXY_STATUS_PROXY_AUTH_REQUIRED;
using i2p::proxy::HTTPProxyAuthChallengeResponse;
using i2p::proxy::HTTPProxyDigestChallengeResponse;
using i2p::proxy::HTTPProxyStripCredentials;
using i2p::proxy::ParseProxyDigestCredentials;
using i2p::proxy::PROXY_AUTH_MAX_ENCODED_SIZE;
using i2p::proxy::PROXY_AUTH_MAX_FIELD_SIZE;
using i2p::proxy::PROXY_DIGEST_MAX_NONCES;
using i2p::proxy::PROXY_DIGEST_NONCE_LIFETIME_MS;
using i2p::proxy::ProxyAuthAllowed;
using i2p::proxy::ProxyAuthResult;
using i2p::proxy::ProxyBasicAuth;
using i2p::proxy::ProxyBasicAuthDecode;
using i2p::proxy::ProxyDigestAlgorithm;
using i2p::proxy::ProxyDigestAuth;
using i2p::proxy::ProxyDigestCredentials;
using i2p::proxy::ProxyDigestHashHex;
using i2p::proxy::ProxyDigestResult;
using i2p::proxy::ProxyDigestResultName;
using i2p::proxy::ProxySecretEquals;
using i2p::proxy::SOCKS_AUTH_MAX_FIELD_SIZE;
using i2p::proxy::SOCKSAuthMethodChoice;
using i2p::proxy::SOCKSChooseAuthMethod;
using i2p::proxy::SOCKSUserPassAuth;
using i2p::proxy::SOCKSUserPassFieldValid;

/**
 * RFC 4648 standard-alphabet encoder for the test vectors. The
 * production decoder is deliberately independent of the I2P URL-safe
 * codec, so the test must not use i2p::data::ByteStreamToBase64 either.
 */
std::string EncodeStandardBase64 (std::string_view input)
{
	static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	out.reserve ((input.size () + 2) / 3 * 4);
	std::size_t i = 0;
	while (i + 3 <= input.size ())
	{
		const std::uint32_t v = (static_cast<std::uint32_t> (static_cast<unsigned char> (input[i])) << 16)
			| (static_cast<std::uint32_t> (static_cast<unsigned char> (input[i + 1])) << 8)
			| static_cast<std::uint32_t> (static_cast<unsigned char> (input[i + 2]));
		out.push_back (alphabet[(v >> 18) & 0x3f]);
		out.push_back (alphabet[(v >> 12) & 0x3f]);
		out.push_back (alphabet[(v >> 6) & 0x3f]);
		out.push_back (alphabet[v & 0x3f]);
		i += 3;
	}
	const std::size_t rest = input.size () - i;
	if (rest == 1)
	{
		const std::uint32_t v = static_cast<std::uint32_t> (static_cast<unsigned char> (input[i])) << 16;
		out.push_back (alphabet[(v >> 18) & 0x3f]);
		out.push_back (alphabet[(v >> 12) & 0x3f]);
		out.append ("==");
	}
	else if (rest == 2)
	{
		const std::uint32_t v = (static_cast<std::uint32_t> (static_cast<unsigned char> (input[i])) << 16)
			| (static_cast<std::uint32_t> (static_cast<unsigned char> (input[i + 1])) << 8);
		out.push_back (alphabet[(v >> 18) & 0x3f]);
		out.push_back (alphabet[(v >> 12) & 0x3f]);
		out.push_back (alphabet[(v >> 6) & 0x3f]);
		out.push_back ('=');
	}
	return out;
}

std::string BasicHeader (std::string_view user, std::string_view password)
{
	std::string raw (user);
	raw.push_back (':');
	raw.append (password);
	return "Basic " + EncodeStandardBase64 (raw);
}

ProxyBasicAuth MakeAuth (std::string user, std::string password)
{
	ProxyBasicAuth auth;
	auth.enabled = true;
	auth.username = std::move (user);
	auth.password = std::move (password);
	return auth;
}

void TestConstantTimeComparison ()
{
	assert (ProxySecretEquals ("", ""));
	assert (ProxySecretEquals ("user", "user"));
	assert (!ProxySecretEquals ("user", "useR"));
	assert (!ProxySecretEquals ("user", "users"));
	assert (!ProxySecretEquals ("users", "user"));
	assert (!ProxySecretEquals ("", "x"));
	assert (!ProxySecretEquals ("x", ""));

	// Embedded NUL bytes are compared, not treated as terminators.
	assert (ProxySecretEquals (std::string_view ("a\0b", 3), std::string_view ("a\0b", 3)));
	assert (!ProxySecretEquals (std::string_view ("a\0b", 3), std::string_view ("a\0c", 3)));
}

void TestHttpBasicAccepted ()
{
	const ProxyBasicAuth auth = MakeAuth ("user", "pass");

	// Canonical published vector for user:pass.
	assert (auth.Verify ("Basic dXNlcjpwYXNz", 1) == ProxyAuthResult::Accepted);
	// The scheme token is case-insensitive (RFC 7235).
	assert (auth.Verify ("basic dXNlcjpwYXNz", 1) == ProxyAuthResult::Accepted);
	assert (auth.Verify ("BASIC dXNlcjpwYXNz", 1) == ProxyAuthResult::Accepted);
	// Surrounding optional whitespace is tolerated, interior whitespace is
	// not.
	assert (auth.Verify ("  Basic dXNlcjpwYXNz\t", 1) == ProxyAuthResult::Accepted);
	assert (auth.Verify ("Basic\tdXNlcjpwYXNz", 1) == ProxyAuthResult::Accepted);

	// The decoder itself reports the same outcome.
	std::string user;
	std::string password;
	assert (ProxyBasicAuthDecode ("Basic dXNlcjpwYXNz", user, password) == ProxyAuthResult::Accepted);
	assert (user == "user");
	assert (password == "pass");

	// A password may contain the separator; the first ':' splits the
	// fields.
	const ProxyBasicAuth colon_auth = MakeAuth ("alice", "pa:ss:word");
	assert (colon_auth.Verify (BasicHeader ("alice", "pa:ss:word"), 1) == ProxyAuthResult::Accepted);
	assert (colon_auth.Verify (BasicHeader ("alice", "pa:ss:word!"), 1) == ProxyAuthResult::BadCredentials);

	// '+' and '/' must be accepted: the I2P URL-safe alphabet would reject
	// them.
	std::string plus_user;
	plus_user.push_back (static_cast<char> (0xfb)); // base64 quantum "++++"
	plus_user.push_back (static_cast<char> (0xef));
	plus_user.push_back (static_cast<char> (0xbe));
	const ProxyBasicAuth plus_auth = MakeAuth (plus_user, "x");
	const std::string plus_header = BasicHeader (plus_user, "x");
	assert (plus_header.find ('+') != std::string::npos);
	assert (plus_auth.Verify (plus_header, 1) == ProxyAuthResult::Accepted);

	std::string slash_user;
	slash_user.push_back (static_cast<char> (0xff)); // base64 quantum "////"
	slash_user.push_back (static_cast<char> (0xff));
	slash_user.push_back (static_cast<char> (0xff));
	const ProxyBasicAuth slash_auth = MakeAuth (slash_user, "x");
	const std::string slash_header = BasicHeader (slash_user, "x");
	assert (slash_header.find ('/') != std::string::npos);
	assert (slash_auth.Verify (slash_header, 1) == ProxyAuthResult::Accepted);

	// Maximum accepted field length.
	const ProxyBasicAuth long_auth = MakeAuth (std::string (PROXY_AUTH_MAX_FIELD_SIZE, 'u'), "p");
	assert (long_auth.Verify (BasicHeader (std::string (PROXY_AUTH_MAX_FIELD_SIZE, 'u'), "p"), 1)
		== ProxyAuthResult::Accepted);
}

void TestHttpBasicRejected ()
{
	const ProxyBasicAuth auth = MakeAuth ("user", "pass");

	// Absent and duplicated headers.
	assert (auth.Verify ("", 0) == ProxyAuthResult::Missing);
	assert (auth.Verify ("", 1) == ProxyAuthResult::Missing);
	assert (auth.Verify ("Basic dXNlcjpwYXNz", 2) == ProxyAuthResult::Duplicate);
	assert (auth.Verify ("Basic dXNlcjpwYXNz", 3) == ProxyAuthResult::Duplicate);

	// Wrong material is reported separately from malformed syntax.
	assert (auth.Verify ("Basic dXNlcjpwYXNz", 1) == ProxyAuthResult::Accepted);
	assert (auth.Verify (BasicHeader ("user", "wrong"), 1) == ProxyAuthResult::BadCredentials);
	assert (auth.Verify (BasicHeader ("User", "pass"), 1) == ProxyAuthResult::BadCredentials);
	assert (auth.Verify (BasicHeader ("userX", "pass"), 1) == ProxyAuthResult::BadCredentials);

	// Unsupported schemes are rejected, never treated as absent.
	assert (auth.Verify ("Digest username=\"user\"", 1) == ProxyAuthResult::UnsupportedScheme);
	assert (auth.Verify ("Bearer dXNlcjpwYXNz", 1) == ProxyAuthResult::UnsupportedScheme);
	assert (auth.Verify ("Negotiate", 1) == ProxyAuthResult::Malformed);

	// Malformed framing, base64 and field layout.
	assert (auth.Verify ("Basic", 1) == ProxyAuthResult::Malformed);
	assert (auth.Verify ("Basic ", 1) == ProxyAuthResult::Malformed);
	assert (auth.Verify ("Basic dXNlcjpwYXNz extra", 1) == ProxyAuthResult::Malformed);
	assert (auth.Verify ("Basic dXNlcjpwYXN", 1) == ProxyAuthResult::Malformed); // missing padding
	assert (auth.Verify ("Basic dXNlcjpwYXNz!", 1) == ProxyAuthResult::Malformed); // bad alphabet
	assert (auth.Verify ("Basic ~~~~", 1) == ProxyAuthResult::Malformed); // I2P URL-safe alphabet
	assert (auth.Verify ("Basic " + EncodeStandardBase64 ("user"), 1) == ProxyAuthResult::Malformed); // no ':'
	assert (auth.Verify ("Basic " + EncodeStandardBase64 (":pass"), 1) == ProxyAuthResult::Malformed); // empty user
	assert (auth.Verify ("Basic " + EncodeStandardBase64 ("user:"), 1) == ProxyAuthResult::Malformed); // empty password
	assert (auth.Verify ("Basic " + EncodeStandardBase64 (std::string (256, 'u') + ":p"), 1)
		== ProxyAuthResult::Malformed); // user over the field bound
	assert (auth.Verify ("Basic " + EncodeStandardBase64 ("u:" + std::string (256, 'p')), 1)
		== ProxyAuthResult::Malformed); // password over the field bound

	// Bounded header value.
	const std::string oversized = "Basic " + std::string (PROXY_AUTH_MAX_ENCODED_SIZE, 'A');
	assert (auth.Verify (oversized, 1) == ProxyAuthResult::TooLarge);
}

void TestHttpNotConfiguredAndFailClosed ()
{
	ProxyBasicAuth disabled;
	assert (!disabled.IsEnabled ());
	assert (!disabled.HasUsableCredentials ());
	assert (disabled.Verify ("Basic dXNlcjpwYXNz", 1) == ProxyAuthResult::NotRequired);
	assert (disabled.Verify ("", 0) == ProxyAuthResult::NotRequired);
	assert (ProxyAuthAllowed (ProxyAuthResult::NotRequired));
	assert (!ProxyAuthAllowed (ProxyAuthResult::BadCredentials));

	// Enabled without a usable credential must deny instead of opening up.
	const ProxyBasicAuth no_password = MakeAuth ("user", "");
	assert (no_password.IsEnabled ());
	assert (!no_password.HasUsableCredentials ());
	assert (no_password.Verify ("Basic " + EncodeStandardBase64 ("user:"), 1) == ProxyAuthResult::Malformed);
	assert (no_password.Verify ("Basic " + EncodeStandardBase64 ("user:x"), 1) == ProxyAuthResult::BadCredentials);

	const ProxyBasicAuth no_user = MakeAuth ("", "pass");
	assert (!no_user.HasUsableCredentials ());
	assert (no_user.Verify (BasicHeader ("", "pass"), 1) == ProxyAuthResult::Malformed);
	assert (no_user.Verify (BasicHeader ("any", "pass"), 1) == ProxyAuthResult::BadCredentials);

	const ProxyBasicAuth oversized_user = MakeAuth (std::string (PROXY_AUTH_MAX_FIELD_SIZE + 1, 'u'), "pass");
	assert (!oversized_user.HasUsableCredentials ());
}

void TestChallengeResponse ()
{
	assert (HTTP_PROXY_STATUS_PROXY_AUTH_REQUIRED == 407);
	assert (HTTP_PROXY_AUTH_CHALLENGE == "Basic realm=\"openi2pd\"");

	const std::string response = HTTPProxyAuthChallengeResponse ();
	assert (response.rfind ("HTTP/1.1 407 ", 0) == 0);
	assert (response.find ("Proxy Authentication Required\r\n") != std::string::npos);
	assert (response.find ("\r\nProxy-Authenticate: Basic realm=\"openi2pd\"\r\n") != std::string::npos);
	assert (response.find ("\r\nConnection: close\r\n") != std::string::npos);
	// Digest must not be advertised while it is unimplemented.
	assert (response.find ("Digest") == std::string::npos);
	// Bounded, fixed-size response with no client input echoed.
	assert (response.size () < 2048);

	// Repeating the call must be deterministic apart from the generated
	// Date.
	assert (HTTPProxyAuthChallengeResponse ().find ("Proxy-Authenticate: Basic") != std::string::npos);
}

void TestNoCredentialForwarding ()
{
	i2p::http::HTTPReq req;
	const std::string raw = "GET http://example.i2p/ HTTP/1.1\r\n"
							"Host: example.i2p\r\n"
							"Proxy-Authorization: Basic dXNlcjpwYXNz\r\n"
							"proxy-connection: keep-alive\r\n"
							"Proxy-Foo: bar\r\n"
							"Authorization: Bearer end-to-end\r\n"
							"X-Keep-Test: keep\r\n"
							"\r\n";
	assert (req.parse (raw) > 0);
	assert (req.GetNumHeaders ("Proxy-Authorization") == 1);

	assert (HTTPProxyStripCredentials (req) == 3);
	assert (req.GetNumHeaders ("Proxy-Authorization") == 0);
	// End-to-end credentials and unrelated headers stay untouched.
	assert (req.GetHeader ("Authorization") == "Bearer end-to-end");
	assert (req.GetHeader ("X-Keep-Test") == "keep");

	const std::string forwarded = req.to_string ();
	assert (forwarded.find ("Proxy-") == std::string::npos);
	assert (forwarded.find ("proxy-connection") == std::string::npos);
	assert (forwarded.find ("dXNlcjpwYXNz") == std::string::npos);
	assert (forwarded.find ("Authorization: Bearer end-to-end") != std::string::npos);
	assert (forwarded.find ("X-Keep-Test: keep") != std::string::npos);

	// Idempotent on a request without proxy headers.
	assert (HTTPProxyStripCredentials (req) == 0);
}

void TestSocksFieldBounds ()
{
	assert (!SOCKSUserPassFieldValid (0));
	assert (SOCKSUserPassFieldValid (1));
	assert (SOCKSUserPassFieldValid (SOCKS_AUTH_MAX_FIELD_SIZE));
	assert (!SOCKSUserPassFieldValid (SOCKS_AUTH_MAX_FIELD_SIZE + 1));
	assert (SOCKS_AUTH_MAX_FIELD_SIZE == PROXY_AUTH_MAX_FIELD_SIZE);
}

void TestSocksUserPass ()
{
	SOCKSUserPassAuth auth;
	auth.enabled = true;
	auth.username = "user";
	auth.password = "pass";
	assert (auth.IsEnabled ());
	assert (auth.HasUsableCredentials ());

	// RFC 1929 vectors.
	assert (auth.Verify ("user", "pass"));
	assert (!auth.Verify ("user", "Pass"));
	assert (!auth.Verify ("User", "pass"));
	assert (!auth.Verify ("user", "pass "));
	assert (!auth.Verify ("", "pass"));
	assert (!auth.Verify ("user", ""));
	assert (!auth.Verify ("", ""));

	// The full 1..255 range is accepted, one byte beyond is not stored.
	const std::string longest (SOCKS_AUTH_MAX_FIELD_SIZE, 'u');
	SOCKSUserPassAuth long_auth;
	long_auth.enabled = true;
	long_auth.username = longest;
	long_auth.password = std::string (SOCKS_AUTH_MAX_FIELD_SIZE, 'p');
	assert (long_auth.HasUsableCredentials ());
	assert (long_auth.Verify (longest, std::string (SOCKS_AUTH_MAX_FIELD_SIZE, 'p')));
	assert (!long_auth.Verify (longest, std::string (SOCKS_AUTH_MAX_FIELD_SIZE - 1, 'p')));

	SOCKSUserPassAuth oversized;
	oversized.enabled = true;
	oversized.username = std::string (SOCKS_AUTH_MAX_FIELD_SIZE + 1, 'u');
	oversized.password = "pass";
	assert (!oversized.HasUsableCredentials ());
	assert (!oversized.Verify (oversized.username, oversized.password));

	// Authentication disabled never accepts credentials, and never rejects
	// a credential-free method either (see the selection test below).
	SOCKSUserPassAuth disabled;
	assert (!disabled.IsEnabled ());
	assert (!disabled.HasUsableCredentials ());
	assert (!disabled.Verify ("user", "pass"));
	assert (!disabled.Verify ("", ""));
}

void TestSocksMethodSelection ()
{
	// Authentication required: only a verifiable exchange is acceptable.
	assert (SOCKSChooseAuthMethod (true, false, true) == SOCKSAuthMethodChoice::UserPass);
	assert (SOCKSChooseAuthMethod (true, true, true) == SOCKSAuthMethodChoice::UserPass);
	assert (SOCKSChooseAuthMethod (true, true, false) == SOCKSAuthMethodChoice::Unacceptable);
	assert (SOCKSChooseAuthMethod (true, false, false) == SOCKSAuthMethodChoice::Unacceptable);

	// No authentication: method 0x00 only.
	assert (SOCKSChooseAuthMethod (false, true, false) == SOCKSAuthMethodChoice::None);
	assert (SOCKSChooseAuthMethod (false, true, true) == SOCKSAuthMethodChoice::None);

	// A client that offers nothing but 0x02 must not be accepted just
	// because
	// the proxy has no credential: arbitrary credentials are never
	// trusted.
	assert (SOCKSChooseAuthMethod (false, false, true) == SOCKSAuthMethodChoice::Unacceptable);
	assert (SOCKSChooseAuthMethod (false, false, false) == SOCKSAuthMethodChoice::Unacceptable);
}

/*
 * RFC 7616 Digest.
 *
 * The two vectors below are the published examples from RFC 7616
 * sections 3.9.1 (SHA-256) and 3.9.2 (MD5); the response values were
 * re-derived from the referenced
 * username/realm/password/nonce/cnonce/uri/method tuple.
 */
constexpr char RFC7616_REALM[] = "http-auth@example.org";
constexpr char RFC7616_USER[] = "Mufasa";
constexpr char RFC7616_PASSWORD[] = "Circle of Life";
constexpr char RFC7616_URI[] = "/dir/index.html";
constexpr char RFC7616_NONCE[] = "7ypf/xlj9XXwfDPEoM4URrv/xwf94BcCAzFZH4GiTo0v";
constexpr char RFC7616_CNONCE[] = "f2/wE4q74E6zIJEtWaHKaf5wv/H5QzzpXusqGemxURZJ";
constexpr char RFC7616_OPAQUE[] = "FQhe/qaU925kfnzjCev0ciny7QMkPqMAFRtzCUYo5tdS";
constexpr char RFC7616_SHA256_RESPONSE[] = "753927fa0e85d155564e2e272a28d1802ca10daf4496794697cf8db5856cb6c1";
constexpr char RFC7616_MD5_RESPONSE[] = "8ca523f5e9506fed4657c9700eebdbec";
constexpr std::int64_t DIGEST_NOW = 1'700'000'000'000; // fixed millisecond base

std::unique_ptr<ProxyDigestAuth> MakeDigestAuth (std::string user, std::string password, std::string realm)
{
	auto auth = std::make_unique<ProxyDigestAuth> ();
	auth->enabled = true;
	auth->username = std::move (user);
	auth->password = std::move (password);
	auth->realm = std::move (realm);
	assert (auth->Prepare ());
	assert (auth->HasUsableCredentials ());
	return auth;
}

std::string DigestHeader (std::string_view user, std::string_view realm, std::string_view uri,
	std::string_view algorithm, std::string_view nonce, std::string_view nc, std::string_view cnonce,
	std::string_view response, std::string_view opaque)
{
	std::string value = "Digest username=\"";
	value += user;
	value += "\", realm=\"";
	value += realm;
	value += "\", uri=\"";
	value += uri;
	value += "\", algorithm=";
	value += algorithm;
	value += ", nonce=\"";
	value += nonce;
	value += "\", nc=";
	value += nc;
	value += ", cnonce=\"";
	value += cnonce;
	value += "\", qop=auth, response=\"";
	value += response;
	value += "\", opaque=\"";
	value += opaque;
	value += "\"";
	return value;
}

void TestDigestRFC7616Vectors ()
{
	// Both published vectors must be accepted byte-for-byte.
	{
		auto auth = MakeDigestAuth (RFC7616_USER, RFC7616_PASSWORD, RFC7616_REALM);
		auth->SeedNonce (RFC7616_NONCE, RFC7616_OPAQUE, DIGEST_NOW);
		const std::string header = DigestHeader (RFC7616_USER, RFC7616_REALM, RFC7616_URI, "SHA-256", RFC7616_NONCE,
			"00000001", RFC7616_CNONCE, RFC7616_SHA256_RESPONSE, RFC7616_OPAQUE);
		assert (auth->Verify (header, "GET", DIGEST_NOW) == ProxyDigestResult::Accepted);
	}
	{
		auto auth = MakeDigestAuth (RFC7616_USER, RFC7616_PASSWORD, RFC7616_REALM);
		auth->SeedNonce (RFC7616_NONCE, RFC7616_OPAQUE, DIGEST_NOW);
		const std::string header = DigestHeader (RFC7616_USER, RFC7616_REALM, RFC7616_URI, "MD5", RFC7616_NONCE,
			"00000001", RFC7616_CNONCE, RFC7616_MD5_RESPONSE, RFC7616_OPAQUE);
		assert (auth->Verify (header, "GET", DIGEST_NOW) == ProxyDigestResult::Accepted);
	}
	// The hashes themselves must match the published H(A1)/H(A2) inputs.
	assert (ProxyDigestHashHex (
				std::string (RFC7616_USER) + ":" + RFC7616_REALM + ":" + RFC7616_PASSWORD, ProxyDigestAlgorithm::Md5)
		== "3d78807defe7de2157e2b0b6573a855f");
	assert (ProxyDigestHashHex ("GET:" + std::string (RFC7616_URI), ProxyDigestAlgorithm::Sha256)
		== "9a3fdae9a622fe8de177c24fa9c070f2b181ec85e15dcbdc32e10c82ad450b04");
	assert (ProxyDigestHashHex ("x", ProxyDigestAlgorithm::Unsupported).empty ());
}

void TestDigestRejections ()
{
	auto auth = MakeDigestAuth (RFC7616_USER, RFC7616_PASSWORD, RFC7616_REALM);
	auth->SeedNonce (RFC7616_NONCE, RFC7616_OPAQUE, DIGEST_NOW);

	const auto header = [&] (std::string_view user, std::string_view realm, std::string_view nonce, std::string_view nc,
							std::string_view algorithm, std::string_view response, std::string_view opaque,
							std::string_view qop = "auth")
	{
		std::string value = "Digest username=\"";
		value += user;
		value += "\", realm=\"";
		value += realm;
		value += "\", uri=\"";
		value += RFC7616_URI;
		value += "\", algorithm=";
		value += algorithm;
		value += ", nonce=\"";
		value += nonce;
		value += "\", nc=";
		value += nc;
		value += ", cnonce=\"";
		value += RFC7616_CNONCE;
		value += "\", qop=";
		value += qop;
		value += ", response=\"";
		value += response;
		value += "\", opaque=\"";
		value += opaque;
		value += "\"";
		return value;
	};

	// Wrong password (a valid MD5 digest for a different secret).
	assert (auth->Verify (header (RFC7616_USER, RFC7616_REALM, RFC7616_NONCE, "00000001", "MD5",
							  "0123456789abcdef0123456789abcdef", RFC7616_OPAQUE),
				"GET", DIGEST_NOW)
		== ProxyDigestResult::BadCredentials);
	// Wrong username with the correct response for the configured user.
	assert (auth->Verify (header ("mufasa", RFC7616_REALM, RFC7616_NONCE, "00000002", "MD5", RFC7616_MD5_RESPONSE,
							  RFC7616_OPAQUE),
				"GET", DIGEST_NOW)
		== ProxyDigestResult::BadCredentials);
	// A different method changes H(A2).
	assert (auth->Verify (header (RFC7616_USER, RFC7616_REALM, RFC7616_NONCE, "00000003", "MD5", RFC7616_MD5_RESPONSE,
							  RFC7616_OPAQUE),
				"POST", DIGEST_NOW)
		== ProxyDigestResult::BadCredentials);
	// Wrong realm is rejected before the digest is evaluated.
	assert (auth->Verify (header (RFC7616_USER, "other-realm", RFC7616_NONCE, "00000004", "MD5", RFC7616_MD5_RESPONSE,
							  RFC7616_OPAQUE),
				"GET", DIGEST_NOW)
		== ProxyDigestResult::BadRealm);
	// Unknown nonce: the client must retry with the new challenge.
	assert (auth->Verify (header (RFC7616_USER, RFC7616_REALM, "unknown-nonce", "00000001", "MD5", RFC7616_MD5_RESPONSE,
							  RFC7616_OPAQUE),
				"GET", DIGEST_NOW)
		== ProxyDigestResult::StaleNonce);
	// Expired nonce (one hour plus one millisecond).
	const std::string expired
		= header (RFC7616_USER, RFC7616_REALM, RFC7616_NONCE, "00000001", "MD5", RFC7616_MD5_RESPONSE, RFC7616_OPAQUE);
	assert (auth->Verify (expired, "GET", DIGEST_NOW + PROXY_DIGEST_NONCE_LIFETIME_MS + 1)
		== ProxyDigestResult::StaleNonce);
	// Wrong opaque value for a known nonce.
	assert (auth->Verify (header (RFC7616_USER, RFC7616_REALM, RFC7616_NONCE, "00000001", "MD5", RFC7616_MD5_RESPONSE,
							  "wrong-opaque"),
				"GET", DIGEST_NOW)
		== ProxyDigestResult::BadNonce);
	// Algorithms the proxy does not advertise.
	for (const char * algorithm : { "SHA-512-256", "MD5-sess", "SHA-256-sess" })
		assert (auth->Verify (header (RFC7616_USER, RFC7616_REALM, RFC7616_NONCE, "00000001", algorithm,
								  RFC7616_SHA256_RESPONSE, RFC7616_OPAQUE),
					"GET", DIGEST_NOW)
			== ProxyDigestResult::UnsupportedAlgorithm);
	// qop=auth-int is not advertised.
	assert (auth->Verify (header (RFC7616_USER, RFC7616_REALM, RFC7616_NONCE, "00000001", "MD5", RFC7616_MD5_RESPONSE,
							  RFC7616_OPAQUE, "auth-int"),
				"GET", DIGEST_NOW)
		== ProxyDigestResult::BadQop);
	// Nonce count and response framing.
	assert (auth->Verify (
				header (RFC7616_USER, RFC7616_REALM, RFC7616_NONCE, "1", "MD5", RFC7616_MD5_RESPONSE, RFC7616_OPAQUE),
				"GET", DIGEST_NOW)
		== ProxyDigestResult::Malformed);
	assert (auth->Verify (header (RFC7616_USER, RFC7616_REALM, RFC7616_NONCE, "00000000", "MD5", RFC7616_MD5_RESPONSE,
							  RFC7616_OPAQUE),
				"GET", DIGEST_NOW)
		== ProxyDigestResult::Malformed);
	assert (
		auth->Verify (header (RFC7616_USER, RFC7616_REALM, RFC7616_NONCE, "00000001", "MD5", "not-hex", RFC7616_OPAQUE),
			"GET", DIGEST_NOW)
		== ProxyDigestResult::Malformed);
	// Missing parameters and non-Digest schemes.
	assert (auth->Verify ("Digest username=\"Mufasa\"", "GET", DIGEST_NOW) == ProxyDigestResult::Malformed);
	assert (auth->Verify ("Basic dXNlcjpwYXNz", "GET", DIGEST_NOW) == ProxyDigestResult::UnsupportedScheme);
	assert (auth->Verify ("", "GET", DIGEST_NOW) == ProxyDigestResult::UnsupportedScheme);
	// The credentials are never accepted through a rejection path.
	assert (!i2p::proxy::ProxyDigestAllowed (ProxyDigestResult::BadCredentials));
	assert (!i2p::proxy::ProxyDigestAllowed (ProxyDigestResult::Replayed));
	assert (i2p::proxy::ProxyDigestStale (ProxyDigestResult::StaleNonce));
	assert (!i2p::proxy::ProxyDigestStale (ProxyDigestResult::BadCredentials));
}

void TestDigestNonceCount ()
{
	auto auth = MakeDigestAuth (RFC7616_USER, RFC7616_PASSWORD, RFC7616_REALM);
	auth->SeedNonce (RFC7616_NONCE, RFC7616_OPAQUE, DIGEST_NOW);
	assert (auth->NonceCount () == 1);

	const auto header = [&] (std::string_view nc)
	{
		return DigestHeader (RFC7616_USER, RFC7616_REALM, RFC7616_URI, "MD5", RFC7616_NONCE, nc, RFC7616_CNONCE,
			RFC7616_MD5_RESPONSE, RFC7616_OPAQUE);
	};

	assert (auth->Verify (header ("00000001"), "GET", DIGEST_NOW) == ProxyDigestResult::Accepted);
	// Replaying the same nc is rejected even with a valid digest.
	assert (auth->Verify (header ("00000001"), "GET", DIGEST_NOW) == ProxyDigestResult::Replayed);
	// Going backwards is rejected: nc is monotonic per nonce.
	assert (auth->Verify (header ("00000000"), "GET", DIGEST_NOW) == ProxyDigestResult::Malformed);
	// A higher count is accepted.
	assert (auth->Verify (header ("00000002"), "GET", DIGEST_NOW) == ProxyDigestResult::Accepted);
	assert (auth->Verify (header ("00000002"), "GET", DIGEST_NOW) == ProxyDigestResult::Replayed);

	// A rejected digest must not consume an nc value.
	auto fresh = MakeDigestAuth (RFC7616_USER, RFC7616_PASSWORD, RFC7616_REALM);
	fresh->SeedNonce (RFC7616_NONCE, RFC7616_OPAQUE, DIGEST_NOW);
	assert (fresh->Verify (DigestHeader (RFC7616_USER, RFC7616_REALM, RFC7616_URI, "MD5", RFC7616_NONCE, "00000005",
							   RFC7616_CNONCE, "0123456789abcdef0123456789abcdef", RFC7616_OPAQUE),
				"GET", DIGEST_NOW)
		== ProxyDigestResult::BadCredentials);
	assert (fresh->Verify (DigestHeader (RFC7616_USER, RFC7616_REALM, RFC7616_URI, "MD5", RFC7616_NONCE, "00000005",
							   RFC7616_CNONCE, RFC7616_MD5_RESPONSE, RFC7616_OPAQUE),
				"GET", DIGEST_NOW)
		== ProxyDigestResult::Accepted);
}

void TestDigestNonceCacheBounds ()
{
	auto auth = MakeDigestAuth ("user", "pass", std::string (HTTP_PROXY_AUTH_REALM));
	assert (auth->NonceCount () == 0);

	// Issuing far more nonces than the bound must not grow the cache.
	for (std::size_t i = 0; i < PROXY_DIGEST_MAX_NONCES * 2; ++i)
	{
		std::string nonce, opaque;
		assert (auth->NewChallenge (DIGEST_NOW, nonce, opaque));
		assert (!nonce.empty () && !opaque.empty ());
		assert (nonce.find (RFC7616_PASSWORD) == std::string::npos);
	}
	assert (auth->NonceCount () <= PROXY_DIGEST_MAX_NONCES);
	assert (PROXY_DIGEST_NONCE_LIFETIME_MS == 60LL * 60 * 1000);
	assert (PROXY_DIGEST_MAX_NONCES == 1024);

	// Every issued nonce expires: after the lifetime plus one the cache is
	// empty again and an old nonce is stale.
	auth->SeedNonce ("abc", "def", DIGEST_NOW);
	assert (auth->NonceCount () >= 1);

	auto shortlived = MakeDigestAuth ("user", "pass", std::string (HTTP_PROXY_AUTH_REALM));
	shortlived->SeedNonce ("only-nonce", "opaque", DIGEST_NOW);
	assert (shortlived->NonceCount () == 1);
	// An insert at a much later time purges everything that already
	// expired.
	shortlived->SeedNonce ("newer", "opaque2", DIGEST_NOW + PROXY_DIGEST_NONCE_LIFETIME_MS + 1);
	assert (shortlived->NonceCount () == 1);
}

void TestDigestConfiguration ()
{
	// Digest must fail closed on unusable configuration.
	{
		ProxyDigestAuth auth;
		auth.enabled = false;
		assert (!auth.Prepare ());
		assert (!auth.HasUsableCredentials ());
	}
	{
		ProxyDigestAuth auth;
		auth.enabled = true;
		auth.username = "user";
		auth.password = "";
		assert (!auth.Prepare ());
	}
	{
		ProxyDigestAuth auth;
		auth.enabled = true;
		auth.username = std::string (PROXY_AUTH_MAX_FIELD_SIZE + 1, 'u');
		auth.password = "pass";
		assert (!auth.Prepare ());
	}
	{
		// A realm is interpolated into a quoted string: quotes, backslashes,
		// commas and control characters are refused.
		ProxyDigestAuth auth;
		auth.enabled = true;
		auth.username = "user";
		auth.password = "pass";
		auth.realm = "bad\"realm";
		assert (!auth.Prepare ());
		auth.realm = "bad\\realm";
		assert (!auth.Prepare ());
		auth.realm = "bad,realm";
		assert (!auth.Prepare ());
		auth.realm = "bad\r\nrealm";
		assert (!auth.Prepare ());
		auth.realm = std::string (65, 'r');
		assert (!auth.Prepare ());
	}
	// A prepared but disabled object still never issues a challenge.
	{
		ProxyDigestAuth auth;
		assert (!auth.Prepare ());
		std::string nonce, opaque;
		assert (!auth.NewChallenge (DIGEST_NOW, nonce, opaque));
	}
}

void TestDigestChallenges ()
{
	const std::string response
		= HTTPProxyDigestChallengeResponse (HTTP_PROXY_AUTH_REALM, "nonce-value", "opaque-value", false);
	assert (response.rfind ("HTTP/1.1 407 ", 0) == 0);
	assert (response.find ("\r\nProxy-Authenticate: Digest realm=\"openi2pd\"") != std::string::npos);
	assert (response.find ("algorithm=SHA-256") != std::string::npos);
	assert (response.find ("algorithm=MD5") != std::string::npos);
	assert (response.find ("qop=\"auth\"") != std::string::npos);
	assert (response.find ("nonce=\"nonce-value\"") != std::string::npos);
	assert (response.find ("opaque=\"opaque-value\"") != std::string::npos);
	assert (response.find ("charset=UTF-8") != std::string::npos);
	assert (response.find ("stale=true") == std::string::npos);
	assert (response.find ("\r\nConnection: close\r\n") != std::string::npos);
	assert (response.size () < 2048);

	const std::string stale = HTTPProxyDigestChallengeResponse (HTTP_PROXY_AUTH_REALM, "n", "o", true);
	assert (stale.find ("stale=true") != std::string::npos);

	// Neither challenge page nor any log label may contain a password.
	const std::string secret = "s3cretpassword";
	for (const std::string & page : { response, stale, HTTPProxyAuthChallengeResponse () })
	{
		assert (page.find (secret) == std::string::npos);
		assert (page.find (RFC7616_PASSWORD) == std::string::npos);
	}
	for (const auto result : { ProxyDigestResult::Accepted, ProxyDigestResult::Missing, ProxyDigestResult::Malformed,
			 ProxyDigestResult::StaleNonce, ProxyDigestResult::BadCredentials, ProxyDigestResult::Replayed })
	{
		const auto label = ProxyDigestResultName (result);
		assert (!label.empty ());
		assert (label.find (secret) == std::string::npos);
	}
	// The Basic challenge keeps its historical, pinned form.
	assert (HTTP_PROXY_AUTH_CHALLENGE == "Basic realm=\"openi2pd\"");
	assert (HTTPProxyAuthChallengeResponse ().find ("\r\nProxy-Authenticate: Basic realm=\"openi2pd\"\r\n")
		!= std::string::npos);
	// A configured realm is escaped into the Basic challenge.
	const std::string custom = HTTPProxyAuthChallengeResponse ("a\"b");
	assert (custom.find ("Basic realm=\"a&quot;b\"") != std::string::npos);
}

void TestDigestCredentialParser ()
{
	ProxyDigestCredentials credentials;
	assert (ParseProxyDigestCredentials (
		"Digest username=\"u\", realm=\"r\", nonce=\"n\", uri=\"/x\", qop=auth, nc=00000001, "
		"cnonce=\"c\", response=\"abc\", algorithm=SHA-256, opaque=\"o\"",
		credentials));
	assert (credentials.username == "u");
	assert (credentials.realm == "r");
	assert (credentials.nonce == "n");
	assert (credentials.uri == "/x");
	assert (credentials.qop == "auth");
	assert (credentials.nc == "00000001");
	assert (credentials.cnonce == "c");
	assert (credentials.response == "abc");
	assert (credentials.hasAlgorithm && credentials.algorithm == "SHA-256");
	assert (credentials.opaque == "o");

	// A quoted-string may escape a quote; the parameter name is
	// case-insensitive.
	ProxyDigestCredentials escaped;
	assert (ParseProxyDigestCredentials (
		"digest USERNAME=\"a\\\"b\", realm=\"r\", nonce=\"n\", uri=\"/x\", qop=auth, nc=00000001, "
		"cnonce=\"c\", response=\"abc\"",
		escaped));
	assert (escaped.username == "a\"b");

	// Missing mandatory parameters, a duplicate parameter and broken
	// quoting.
	ProxyDigestCredentials invalid;
	assert (!ParseProxyDigestCredentials ("Digest realm=\"r\"", invalid));
	assert (!ParseProxyDigestCredentials (
		"Digest username=\"u\", username=\"u2\", realm=\"r\", nonce=\"n\", uri=\"/x\", qop=auth, nc=00000001, "
		"cnonce=\"c\", response=\"abc\"",
		invalid));
	assert (!ParseProxyDigestCredentials (
		"Digest username=\"u, realm=\"r\", nonce=\"n\", uri=\"/x\", qop=auth, nc=00000001, cnonce=\"c\", "
		"response=\"abc\"",
		invalid));
	assert (!ParseProxyDigestCredentials ("", invalid));
	assert (!ParseProxyDigestCredentials ("Basic abc", invalid));
	assert (!ParseProxyDigestCredentials ("Digest " + std::string (PROXY_AUTH_MAX_ENCODED_SIZE + 1, 'a'), invalid));
}
} // namespace

int main ()
{
	TestConstantTimeComparison ();
	TestHttpBasicAccepted ();
	TestHttpBasicRejected ();
	TestHttpNotConfiguredAndFailClosed ();
	TestChallengeResponse ();
	TestNoCredentialForwarding ();
	TestSocksFieldBounds ();
	TestSocksUserPass ();
	TestSocksMethodSelection ();
	TestDigestRFC7616Vectors ();
	TestDigestRejections ();
	TestDigestNonceCount ();
	TestDigestNonceCacheBounds ();
	TestDigestConfiguration ();
	TestDigestChallenges ();
	TestDigestCredentialParser ();
	return 0;
}
