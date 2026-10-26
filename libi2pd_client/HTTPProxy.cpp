/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: HTTPProxy.
 *
 * Implements an HTTP forward proxy that resolves .i2p hosts through the
 * address book, handles addresshelper jumps, sanitizes proxy-revealing
 * headers and rotates between configured outproxies. CONNECT, upstream
 * HTTP/SOCKS proxying and request parsing use a bounded header buffer, and
 * optional local Basic or Digest authentication answers 407 before any
 * address-book or outproxy side effect. Every generated page escapes the
 * client-supplied host and path, only HTTP(S) request targets are admitted,
 * hop-by-hop headers never cross the proxy in either direction and the
 * upstream/response phases are bounded by cancellable timers.
 *
 */

#include <algorithm>
#include <array>
#include <chrono>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <string>
#include <string_view>
#include <atomic>
#include <memory>
#include <set>
#include <utility>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <mutex>

#include "I2PService.h"
#include "Destination.h"
#include "HTTPProxy.h"
#include "util.h"
#include "Identity.h"
#include "Streaming.h"
#include "Destination.h"
#include "ClientContext.h"
#include "I2PEndian.h"
#include "I2PTunnel.h"
#include "I2PTunnelHTTP.h"
#include "Config.h"
#include "HTTP.h"
#include "I18N.h"
#include "Socks5.h"

namespace i2p
{
namespace proxy
{
	// Status constants live in HTTPProxy.h so regressions can pin the
	// proxy-generated error mapping without duplicating numeric literals.

	static constexpr std::array jumporder = { "reg.i2p", "stats.i2p", "identiguy.i2p", "notbob.i2p" };

	static const std::map<std::string, std::string> jumpservices = {
		{ "reg.i2p", "http://shx5vqsw7usdaunyzr2qmes2fq37oumybpudrd4jjj4e4vk4uusa.b32.i2p/jump/" },
		{ "identiguy.i2p",
			"http://3mzmrus2oron5fxptw7hw2puho3bnqmw2hqy7nw64dsrrjwdilva.b32.i2p/cgi-bin/query?hostname=" },
		{ "stats.i2p", "http://kqypgjpjwrphnzebod5ev3ts2vtii6e5tntrg4rnfijqc7rypldq.b32.i2p/cgi-bin/jump.cgi?a=" },
		{ "notbob.i2p", "http://nytzrhrjjfsutowojvxi7hphesskpqqr65wpistz6wa7cpajhp7a.b32.i2p/cgi-bin/jump.cgi?q=" }
	};

	static constexpr std::string_view pageHead
		= "<head>\r\n"
		  "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\r\n"
		  "  <title>openi2pd HTTP proxy</title>\r\n"
		  "  <style type=\"text/css\">\r\n"
		  "    body { font: 100%/1.5em sans-serif; margin: 0; padding: 1.5em; background: #F7FAFC; color: #162737; "
		  "}\r\n"
		  "    h1 { font-size: 1.7em; color: #007C7A; }\r\n"
		  "    .warning { color: #B3261E; font-weight: bold; }\r\n"
		  "    @media screen and (max-width: 980px) { h1 { font-size: 1.7em; text-align: center; color: #007C7A; }}\r\n"
		  "    @media (prefers-color-scheme: dark) { body { background: #0B141C; color: #E4EDF4; } h1 { color: "
		  "#59D8D4; } .warning { color: #FFB4AB; } }\r\n"
		  "  </style>\r\n"
		  "</head>\r\n";

	static bool IsBlockedClearnetTarget (std::string_view host)
	{
		std::string value (host);
		if (value.size () >= 2 && value.front () == '[' && value.back () == ']')
			value = value.substr (1, value.size () - 2);
		if (boost::iequals (value, "localhost") || i2p::client::HasSuffixIgnoreCase (value, ".localhost")) return true;
		boost::system::error_code ec;
		auto address = boost::asio::ip::make_address (value, ec);
		if (ec) return false;
		if (address.is_v4 ())
		{
			auto b = address.to_v4 ().to_bytes ();
			if (b[0] == 0 || b[0] == 10 || b[0] == 127 || (b[0] == 169 && b[1] == 254)
				|| (b[0] == 172 && b[1] >= 16 && b[1] <= 31) || (b[0] == 192 && b[1] == 168)
				|| address.to_v4 ().is_multicast ())
				return true;
		}
		else
		{
			auto b = address.to_v6 ().to_bytes ();
			if (address.to_v6 ().is_loopback () || address.to_v6 ().is_unspecified ()
				|| address.to_v6 ().is_multicast () || (b[0] == 0xfe && (b[1] & 0xc0) == 0x80) || (b[0] & 0xfe) == 0xfc)
				return true;
		}
		return false;
	}

	/*
	 * Local proxy authentication.
	 *
	 * See HTTPProxy.h for the contract. The helpers below never log, echo or
	 * retain the supplied credential: the decoded buffer is cleansed before it
	 * is released and only the configured username/password live in memory.
	 */
	namespace
	{
		/*
		 * RFC 4648 standard alphabet. `i2p::data::Base64ToByteStream` uses the
		 * I2P URL-safe alphabet ('-'/'~'), so a browser's '+'/'/' payload would
		 * be rejected there; Basic credentials need the standard table.
		 */
		constexpr std::array<std::int8_t, 256> BuildStandardBase64Table () noexcept
		{
			std::array<std::int8_t, 256> table { };
			table.fill (-1);
			for (int i = 0; i < 26; ++i)
			{
				table[static_cast<std::size_t> ('A' + i)] = static_cast<std::int8_t> (i);
				table[static_cast<std::size_t> ('a' + i)] = static_cast<std::int8_t> (26 + i);
			}
			for (int i = 0; i < 10; ++i) table[static_cast<std::size_t> ('0' + i)] = static_cast<std::int8_t> (52 + i);
			table[static_cast<std::size_t> ('+')] = 62;
			table[static_cast<std::size_t> ('/')] = 63;
			table[static_cast<std::size_t> ('=')] = -2; // padding marker, never a symbol
			return table;
		}

		constexpr std::array<std::int8_t, 256> STANDARD_BASE64 = BuildStandardBase64Table ();

		void CleanseString (std::string & value) noexcept
		{
			if (!value.empty ()) OPENSSL_cleanse (value.data (), value.size ());
			value.clear ();
		}

		/**
		 * Strict standard-alphabet base64 decoder: four-character framing,
		 * terminal-only padding, no whitespace, no non-canonical trailing bits
		 * and a hard output bound of PROXY_AUTH_MAX_FIELD_SIZE * 2 + 1 bytes.
		 */
		bool DecodeStandardBase64 (std::string_view encoded, std::string & output)
		{
			output.clear ();
			if (encoded.empty () || encoded.size () % 4 != 0) return false;
			if (encoded.size () > PROXY_AUTH_MAX_ENCODED_SIZE) return false;

			std::size_t padding = 0;
			if (encoded.ends_with ("=="))
				padding = 2;
			else if (encoded.ends_with ('='))
				padding = 1;

			const std::size_t symbols = encoded.size () - padding;
			if (symbols == 0) return false;
			for (std::size_t i = 0; i < symbols; ++i)
				if (STANDARD_BASE64[static_cast<std::uint8_t> (encoded[i])] < 0) return false;
			for (std::size_t i = symbols; i < encoded.size (); ++i)
				if (encoded[i] != '=') return false;

			output.reserve ((symbols / 4) * 3 + 3);
			std::uint32_t accumulator = 0;
			unsigned int bits = 0;
			for (std::size_t i = 0; i < symbols; ++i)
			{
				accumulator = (accumulator << 6)
					| static_cast<std::uint32_t> (STANDARD_BASE64[static_cast<std::uint8_t> (encoded[i])]);
				bits += 6;
				if (bits >= 8)
				{
					bits -= 8;
					output.push_back (static_cast<char> ((accumulator >> bits) & 0xff));
				}
			}
			// A canonical encoding leaves only zero bits in the final quantum.
			if (bits != 0 && (accumulator & ((1u << bits) - 1u)) != 0)
			{
				CleanseString (output);
				return false;
			}
			if (output.size () > PROXY_AUTH_MAX_FIELD_SIZE * 2 + 1)
			{
				CleanseString (output);
				return false;
			}
			return true;
		}

		/** Split `user ":" password`; the password may itself contain ':'. */
		bool SplitBasicCredentials (std::string_view decoded, std::string & user, std::string & password)
		{
			const std::size_t colon = decoded.find (':');
			if (colon == std::string_view::npos) return false;
			if (colon == 0 || colon > PROXY_AUTH_MAX_FIELD_SIZE) return false;
			const std::string_view password_view = decoded.substr (colon + 1);
			if (password_view.empty () || password_view.size () > PROXY_AUTH_MAX_FIELD_SIZE) return false;
			user.assign (decoded.substr (0, colon));
			password.assign (password_view);
			return true;
		}

		char LowerASCII (char ch) noexcept
		{
			return ch >= 'A' && ch <= 'Z' ? static_cast<char> (ch + ('a' - 'A')) : ch;
		}

		bool EqualsIgnoreCase (std::string_view left, std::string_view right) noexcept
		{
			if (left.size () != right.size ()) return false;
			for (std::size_t i = 0; i < left.size (); ++i)
				if (LowerASCII (left[i]) != LowerASCII (right[i])) return false;
			return true;
		}

		bool StartsWithIgnoreCase (std::string_view text, std::string_view prefix) noexcept
		{
			if (text.size () < prefix.size ()) return false;
			for (std::size_t i = 0; i < prefix.size (); ++i)
				if (LowerASCII (text[i]) != LowerASCII (prefix[i])) return false;
			return true;
		}

		/** Strict 1..65535 decimal port. */
		bool ParseDecimalPort (std::string_view text, std::uint16_t & port) noexcept
		{
			if (text.empty () || text.size () > 5) return false;
			unsigned int value = 0;
			for (const char ch : text)
			{
				if (ch < '0' || ch > '9') return false;
				value = value * 10 + static_cast<unsigned int> (ch - '0');
			}
			if (value == 0 || value > 65535) return false;
			port = static_cast<std::uint16_t> (value);
			return true;
		}

		bool IsHostNameCharacter (char ch) noexcept
		{
			return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '.'
				|| ch == '-' || ch == '_';
		}

		/** A single DNS label sequence; no IPv6 colons and no URL syntax. */
		bool IsValidHostName (std::string_view host) noexcept
		{
			if (host.empty () || host.size () > 255) return false;
			for (const char ch : host)
				if (!IsHostNameCharacter (ch)) return false;
			return true;
		}

		bool IsValidIpv6Literal (std::string_view value) noexcept
		{
			if (value.size () < 2 || value.size () > 45) return false;
			for (const char ch : value)
			{
				const bool hex = (ch >= '0' && ch <= '9') || (LowerASCII (ch) >= 'a' && LowerASCII (ch) <= 'f');
				if (!hex && ch != ':' && ch != '.') return false;
			}
			return value.find (':') != std::string_view::npos;
		}

		/**
		 * Parse `host[:port]` or `[ipv6][:port]`.
		 * @return false and sets @a error on a syntax failure.
		 */
		bool ParseAuthority (std::string_view authority, bool requirePort, std::string & host, std::uint16_t & port,
			ProxyTargetError & error)
		{
			error = ProxyTargetError::Malformed;
			if (authority.empty ()) return false;

			std::string_view portText;
			if (authority.front () == '[')
			{
				const auto close = authority.find (']');
				if (close == std::string_view::npos) return false;
				host.assign (authority.substr (1, close - 1));
				if (!IsValidIpv6Literal (host)) return false;
				const auto rest = authority.substr (close + 1);
				if (rest.empty ())
				{
					if (requirePort)
					{
						error = ProxyTargetError::BadPort;
						return false;
					}
					port = 0;
					return true;
				}
				if (rest.front () != ':') return false;
				portText = rest.substr (1);
			}
			else
			{
				const auto colon = authority.find (':');
				if (colon == std::string_view::npos)
				{
					if (requirePort)
					{
						error = ProxyTargetError::BadPort;
						return false;
					}
					host.assign (authority);
				}
				else
				{
					if (authority.find (':', colon + 1) != std::string_view::npos) return false;
					host.assign (authority.substr (0, colon));
					portText = authority.substr (colon + 1);
				}
				if (!IsValidHostName (host)) return false;
			}

			if (portText.empty ())
			{
				error = ProxyTargetError::BadPort;
				return false;
			}
			if (!ParseDecimalPort (portText, port))
			{
				error = ProxyTargetError::BadPort;
				return false;
			}
			error = ProxyTargetError::None;
			return true;
		}

		/** Append the UTF-8 sequence starting at @a index, or a replacement char. */
		bool AppendUtf8 (std::string & out, std::string_view input, std::size_t & index)
		{
			const auto lead = static_cast<unsigned char> (input[index]);
			std::size_t length = 0;
			if (lead >= 0xc2 && lead <= 0xdf)
				length = 2;
			else if (lead >= 0xe0 && lead <= 0xef)
				length = 3;
			else if (lead >= 0xf0 && lead <= 0xf4)
				length = 4;
			else
				return false;
			if (index + length > input.size ()) return false;
			for (std::size_t i = 1; i < length; ++i)
				if ((static_cast<unsigned char> (input[index + i]) & 0xc0) != 0x80) return false;
			out.append (input.substr (index, length));
			index += length;
			return true;
		}
	} // namespace

	std::string EscapeHTML (std::string_view value)
	{
		std::string out;
		const std::size_t limit = std::min (value.size (), HTTP_PROXY_MAX_ESCAPED_INPUT);
		out.reserve (limit);
		for (std::size_t i = 0; i < limit;)
		{
			const auto ch = static_cast<unsigned char> (value[i]);
			switch (ch)
			{
			case '&':
				out += "&amp;";
				++i;
				break;
			case '<':
				out += "&lt;";
				++i;
				break;
			case '>':
				out += "&gt;";
				++i;
				break;
			case '"':
				out += "&quot;";
				++i;
				break;
			case '\'':
				out += "&#39;";
				++i;
				break;
			default:
				if (ch < 0x20 || ch == 0x7f)
					++i; // C0 controls and DEL cannot be represented safely
				else if (ch < 0x80)
				{
					out.push_back (static_cast<char> (ch));
					++i;
				}
				else if (!AppendUtf8 (out, value, i))
				{
					out += "&#xFFFD;";
					++i;
				}
				break;
			}
		}
		return out;
	}

	std::string RedactURLUserInfo (std::string_view url)
	{
		// A configured URL is bounded before it reaches a log line, and the
		// query string is dropped as well because a credential can also be
		// carried there (`?password=`).
		constexpr std::size_t MAX_LOGGED_URL = 512;
		std::string_view value = url.substr (0, std::min (url.size (), MAX_LOGGED_URL));
		const auto scheme = value.find ("://");
		if (scheme == std::string_view::npos) return std::string (value);

		std::string_view authority = value.substr (scheme + 3);
		const auto slash = authority.find_first_of ("/?#");
		if (slash != std::string_view::npos) authority = authority.substr (0, slash);
		const auto at = authority.find ('@');
		if (at != std::string_view::npos) authority = authority.substr (at + 1);

		std::string out (value.substr (0, scheme + 3));
		out.append (authority);
		return out;
	}

	namespace
	{
		constexpr std::array<std::string_view, 7> HOP_BY_HOP_HEADERS = { "connection", "keep-alive",
			"proxy-authenticate", "proxy-authorization", "proxy-connection", "te", "trailer" };

		constexpr std::array<std::string_view, 4> PROXY_DISCLOSURE_HEADERS
			= { "x-real-ip", "client-ip", "x-originating-ip", "forwarded" };
	} // namespace

	bool IsHopByHopHeaderName (std::string_view name) noexcept
	{
		for (const auto header : HOP_BY_HOP_HEADERS)
			if (EqualsIgnoreCase (name, header)) return true;
		return false;
	}

	bool IsProxyDisclosureHeaderName (std::string_view name) noexcept
	{
		if (StartsWithIgnoreCase (name, "x-forwarded-")) return true;
		for (const auto header : PROXY_DISCLOSURE_HEADERS)
			if (EqualsIgnoreCase (name, header)) return true;
		return false;
	}

	std::size_t StripHopByHopHeaders (i2p::http::HTTPReq & req)
	{
		const std::size_t before = req.GetNumHeaders ();
		for (auto it = req.headers.begin (); it != req.headers.end ();)
		{
			if (IsHopByHopHeaderName (it->first) || IsProxyDisclosureHeaderName (it->first))
				it = req.headers.erase (it);
			else
				++it;
		}
		return before - req.GetNumHeaders ();
	}

	bool IsNTLMAuthorizationValue (std::string_view value) noexcept
	{
		std::size_t begin = 0;
		while (begin < value.size () && (value[begin] == ' ' || value[begin] == '\t')) ++begin;
		value = value.substr (begin);
		if (!StartsWithIgnoreCase (value, "ntlm")) return false;
		return value.size () == 4 || value[4] == ' ' || value[4] == '\t';
	}

	ProxyRequestTarget ClassifyRequestTarget (
		std::string_view method, std::string_view target, std::string_view host_header)
	{
		ProxyRequestTarget result;
		if (target.empty ())
		{
			result.error = ProxyTargetError::Malformed;
			return result;
		}

		if (method == "CONNECT")
		{
			result.useConnect = true;
			result.scheme = "https";
			if (!ParseAuthority (target, true, result.host, result.port, result.error)) return result;
			result.path.clear ();
			return result;
		}

		// Detect a URI scheme. `http` is the only scheme a forward proxy can
		// relay as a request; `https` would require an opaque CONNECT tunnel.
		std::string_view scheme;
		if (const auto pos = target.find ("://"); pos != std::string_view::npos)
			scheme = target.substr (0, pos);
		else if (target.front () != '/' && target != "*")
		{
			// `mailto:user@example.com` and `data:...` carry no "//", but the
			// prefix is still a scheme rather than an HTTP authority. A
			// `host:port` target is not a scheme: it is left to the legacy
			// scheme-less parse below.
			const auto colon = target.find (':');
			if (colon != std::string_view::npos && colon > 0 && colon <= 32)
			{
				bool scheme_like = true;
				for (std::size_t i = 0; i < colon; ++i)
				{
					const char ch = target[i];
					if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '+'
							|| ch == '-' || ch == '.'))
					{
						scheme_like = false;
						break;
					}
				}
				// A dotted prefix is a host name (`example.i2p:8080/path`).
				if (scheme_like && target.substr (0, colon).find ('.') == std::string_view::npos)
				{
					const auto rest = target.substr (colon + 1);
					const auto slash = rest.find ('/');
					const auto restPort = rest.substr (0, slash);
					bool port_like = !restPort.empty () && restPort.size () <= 5;
					for (const char ch : restPort)
						if (ch < '0' || ch > '9') port_like = false;
					if (!port_like) scheme = target.substr (0, colon);
				}
			}
		}

		if (!scheme.empty ())
		{
			if (!EqualsIgnoreCase (scheme, "http"))
			{
				result.error = ProxyTargetError::UnsupportedScheme;
				return result;
			}
			i2p::http::URL url;
			if (!url.parse (target) || url.host.empty ())
			{
				result.error = ProxyTargetError::Malformed;
				return result;
			}
			if (!url.ipv6 && !IsValidHostName (url.host))
			{
				result.error = ProxyTargetError::Malformed;
				return result;
			}
			result.absolute = true;
			result.scheme = "http";
			result.host = url.host;
			result.port = url.port ? url.port : 80;
			result.path = url.path.empty () ? std::string ("/") : url.path;
			if (url.hasquery) result.path += "?" + url.query;
			return result;
		}

		if (target.front () == '/' || target == "*")
		{
			result.scheme = "http";
			result.path = std::string (target);
			if (host_header.empty ())
			{
				result.error = ProxyTargetError::MissingHost;
				return result;
			}
			if (!ParseAuthority (host_header, false, result.host, result.port, result.error)) return result;
			if (result.host.empty ())
			{
				result.error = ProxyTargetError::MissingHost;
				return result;
			}
			if (!result.port) result.port = 80;
			return result;
		}

		// Scheme-less absolute-ish target (`example.i2p/path`), accepted for
		// compatibility with the previous URL parser; the scheme is http.
		i2p::http::URL url;
		if (!url.parse (target) || url.host.empty ())
		{
			result.error = ProxyTargetError::Malformed;
			return result;
		}
		if (!url.ipv6 && !IsValidHostName (url.host))
		{
			result.error = ProxyTargetError::Malformed;
			return result;
		}
		result.absolute = true;
		result.scheme = "http";
		result.host = url.host;
		result.port = url.port ? url.port : 80;
		result.path = url.path.empty () ? std::string ("/") : url.path;
		if (url.hasquery) result.path += "?" + url.query;
		return result;
	}

	ProxyAuthResult ProxyBasicAuthDecode (std::string_view value, std::string & user, std::string & password)
	{
		user.clear ();
		password.clear ();
		if (value.size () > PROXY_AUTH_MAX_ENCODED_SIZE) return ProxyAuthResult::TooLarge;

		// The header parser keeps the value verbatim; tolerate optional
		// whitespace around the scheme/credentials pair only.
		std::size_t begin = 0;
		std::size_t end = value.size ();
		while (begin < end && (value[begin] == ' ' || value[begin] == '\t')) ++begin;
		while (end > begin && (value[end - 1] == ' ' || value[end - 1] == '\t')) --end;
		value = value.substr (begin, end - begin);
		if (value.empty ()) return ProxyAuthResult::Missing;

		const std::size_t scheme_end = value.find_first_of (" \t");
		if (scheme_end == std::string_view::npos) return ProxyAuthResult::Malformed;
		const std::string_view scheme = value.substr (0, scheme_end);
		const std::size_t payload_begin = value.find_first_not_of (" \t", scheme_end);
		if (payload_begin == std::string_view::npos) return ProxyAuthResult::Malformed;
		const std::string_view payload = value.substr (payload_begin);
		// Exactly one credential token: embedded whitespace is never valid.
		if (payload.find_first_of (" \t") != std::string_view::npos) return ProxyAuthResult::Malformed;
		if (payload.size () > PROXY_AUTH_MAX_ENCODED_SIZE) return ProxyAuthResult::TooLarge;

		// Only Basic is implemented; Digest and every other scheme are rejected
		// rather than treated as an absent header.
		if (!boost::iequals (scheme, "Basic")) return ProxyAuthResult::UnsupportedScheme;

		std::string decoded;
		if (!DecodeStandardBase64 (payload, decoded)) return ProxyAuthResult::Malformed;
		const bool split = SplitBasicCredentials (decoded, user, password);
		CleanseString (decoded);
		if (!split)
		{
			CleanseString (user);
			CleanseString (password);
			return ProxyAuthResult::Malformed;
		}
		return ProxyAuthResult::Accepted;
	}

	ProxyAuthResult ProxyBasicAuth::Verify (std::string_view value, std::size_t occurrences) const
	{
		if (!enabled) return ProxyAuthResult::NotRequired;
		if (occurrences == 0) return ProxyAuthResult::Missing;
		if (occurrences > 1) return ProxyAuthResult::Duplicate;

		std::string provided_user;
		std::string provided_password;
		const ProxyAuthResult parsed = ProxyBasicAuthDecode (value, provided_user, provided_password);
		if (parsed != ProxyAuthResult::Accepted)
		{
			CleanseString (provided_user);
			CleanseString (provided_password);
			return parsed;
		}

		// An enabled but unusable configuration denies every request instead of
		// falling back to an unauthenticated proxy. Both fields are always
		// compared so the timing does not depend on which one mismatches.
		const bool user_ok = ProxySecretEquals (username, provided_user);
		const bool password_ok = ProxySecretEquals (password, provided_password);
		const bool usable = HasUsableCredentials ();
		CleanseString (provided_user);
		CleanseString (provided_password);
		unsigned int mismatches = usable ? 0u : 1u;
		if (!user_ok) ++mismatches;
		if (!password_ok) ++mismatches;
		return mismatches == 0 ? ProxyAuthResult::Accepted : ProxyAuthResult::BadCredentials;
	}

	std::size_t HTTPProxyStripCredentials (i2p::http::HTTPReq & req)
	{
		const std::size_t before = req.GetNumHeaders ();
		// Case-insensitive prefix match: Proxy-Authorization, Proxy-Connection
		// and every other proxy-scoped header stay local.
		req.RemoveHeader ("Proxy-");
		return before - req.GetNumHeaders ();
	}

	std::string HTTPProxyAuthChallengeResponse ()
	{
		return HTTPProxyAuthChallengeResponse (HTTP_PROXY_AUTH_REALM);
	}

	std::string HTTPProxyAuthChallengeResponse (std::string_view realm)
	{
		i2p::http::HTTPRes res;
		res.code = HTTP_PROXY_STATUS_PROXY_AUTH_REQUIRED;
		res.status = "Proxy Authentication Required";
		res.add_header ("Proxy-Authenticate", "Basic realm=\"" + EscapeHTML (realm) + "\"");
		res.add_header ("Connection", "close");
		res.add_header ("Content-Type", "text/html; charset=UTF-8");
		std::stringstream ss;
		ss << "<html>\r\n"
		   << pageHead << "<body><h1>" << tr ("Proxy authentication required") << "</h1>\r\n"
		   << "<p>" << tr ("This proxy requires local authentication") << "</p>\r\n"
		   << "</body>\r\n"
		   << "</html>\r\n";
		res.body = ss.str ();
		return res.to_string ();
	}

	std::string_view ProxyAuthSchemeName (ProxyAuthScheme scheme) noexcept
	{
		switch (scheme)
		{
		case ProxyAuthScheme::None: return "none";
		case ProxyAuthScheme::Basic: return "basic";
		case ProxyAuthScheme::Digest: return "digest";
		}
		return "none";
	}

	namespace
	{
		/** RFC 7230 token, used for a Digest parameter name. */
		bool IsTokenCharacter (char ch) noexcept
		{
			if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) return true;
			switch (ch)
			{
			case '!':
			case '#':
			case '$':
			case '%':
			case '&':
			case '\'':
			case '*':
			case '+':
			case '-':
			case '.':
			case '^':
			case '_':
			case '`':
			case '|':
			case '~': return true;
			default: return false;
			}
		}

		bool IsHexCharacter (char ch) noexcept
		{
			return (ch >= '0' && ch <= '9') || (LowerASCII (ch) >= 'a' && LowerASCII (ch) <= 'f');
		}

		bool IsHexString (std::string_view value) noexcept
		{
			if (value.empty ()) return false;
			for (const char ch : value)
				if (!IsHexCharacter (ch)) return false;
			return true;
		}

		std::string ToLowerASCII (std::string_view value)
		{
			std::string out (value);
			for (auto & ch : out) ch = LowerASCII (ch);
			return out;
		}
	} // namespace

	std::string ProxyDigestHashHex (std::string_view data, ProxyDigestAlgorithm algorithm)
	{
		const EVP_MD * md = nullptr;
		switch (algorithm)
		{
		case ProxyDigestAlgorithm::Md5: md = EVP_md5 (); break;
		case ProxyDigestAlgorithm::Sha256: md = EVP_sha256 (); break;
		case ProxyDigestAlgorithm::Unsupported: return { };
		}
		if (!md) return { };
		unsigned char digest[EVP_MAX_MD_SIZE] { };
		unsigned int length = 0;
		if (EVP_Digest (data.data (), data.size (), digest, &length, md, nullptr) != 1) return { };
		static constexpr char HEX[] = "0123456789abcdef";
		std::string out;
		out.reserve (length * 2);
		for (unsigned int i = 0; i < length; ++i)
		{
			out.push_back (HEX[digest[i] >> 4]);
			out.push_back (HEX[digest[i] & 0x0f]);
		}
		OPENSSL_cleanse (digest, sizeof (digest));
		return out;
	}

	bool ParseProxyDigestCredentials (std::string_view value, ProxyDigestCredentials & credentials)
	{
		credentials = { };
		if (value.size () > PROXY_DIGEST_MAX_PARAM_SIZE) return false;

		std::size_t begin = 0;
		while (begin < value.size () && (value[begin] == ' ' || value[begin] == '\t')) ++begin;
		value = value.substr (begin);
		if (!StartsWithIgnoreCase (value, "digest")) return false;
		value = value.substr (6);
		if (value.empty () || (value.front () != ' ' && value.front () != '\t')) return false;
		const auto firstToken = value.find_first_not_of (" \t");
		if (firstToken == std::string_view::npos) return false;
		value = value.substr (firstToken);
		if (value.empty ()) return false;

		std::uint32_t seen = 0;
		std::size_t position = 0;
		while (position < value.size ())
		{
			while (position < value.size ()
				&& (value[position] == ' ' || value[position] == '\t' || value[position] == ','))
				++position;
			if (position >= value.size ()) break;

			const std::size_t name_begin = position;
			while (position < value.size () && IsTokenCharacter (value[position])) ++position;
			if (position == name_begin) return false;
			const std::string name = ToLowerASCII (value.substr (name_begin, position - name_begin));

			while (position < value.size () && (value[position] == ' ' || value[position] == '\t')) ++position;
			if (position >= value.size () || value[position] != '=') return false;
			++position;
			while (position < value.size () && (value[position] == ' ' || value[position] == '\t')) ++position;
			if (position >= value.size ()) return false;

			std::string parameter;
			if (value[position] == '"')
			{
				++position;
				bool terminated = false;
				while (position < value.size ())
				{
					const char ch = value[position++];
					if (ch == '\\')
					{
						if (position >= value.size ()) return false;
						parameter.push_back (value[position++]);
					}
					else if (ch == '"')
					{
						terminated = true;
						break;
					}
					else
						parameter.push_back (ch);
				}
				if (!terminated) return false;
			}
			else
			{
				const std::size_t token_begin = position;
				while (position < value.size () && IsTokenCharacter (value[position])) ++position;
				if (position == token_begin) return false;
				parameter.assign (value.substr (token_begin, position - token_begin));
			}

			std::string * slot = nullptr;
			int index = -1;
			if (name == "username")
			{
				slot = &credentials.username;
				index = 0;
			}
			else if (name == "realm")
			{
				slot = &credentials.realm;
				index = 1;
			}
			else if (name == "nonce")
			{
				slot = &credentials.nonce;
				index = 2;
			}
			else if (name == "uri")
			{
				slot = &credentials.uri;
				index = 3;
			}
			else if (name == "qop")
			{
				slot = &credentials.qop;
				index = 4;
			}
			else if (name == "nc")
			{
				slot = &credentials.nc;
				index = 5;
			}
			else if (name == "cnonce")
			{
				slot = &credentials.cnonce;
				index = 6;
			}
			else if (name == "response")
			{
				slot = &credentials.response;
				index = 7;
			}
			else if (name == "opaque")
			{
				slot = &credentials.opaque;
				index = 8;
			}
			else if (name == "algorithm")
			{
				credentials.hasAlgorithm = true;
				slot = &credentials.algorithm;
				index = 9;
			}
			// Unknown parameters are ignored, as RFC 7616 requires.
			if (slot)
			{
				// A duplicated parameter is ambiguous and always rejected.
				const auto bit = 1u << static_cast<unsigned int> (index);
				if (seen & bit) return false;
				seen |= bit;
				*slot = std::move (parameter);
			}

			while (position < value.size () && (value[position] == ' ' || value[position] == '\t')) ++position;
			if (position < value.size () && value[position] != ',') return false;
		}

		return !credentials.username.empty () && !credentials.realm.empty () && !credentials.nonce.empty ()
			&& !credentials.uri.empty () && !credentials.qop.empty () && !credentials.nc.empty ()
			&& !credentials.cnonce.empty () && !credentials.response.empty ();
	}

	std::string_view ProxyDigestResultName (ProxyDigestResult result) noexcept
	{
		switch (result)
		{
		case ProxyDigestResult::Accepted: return "accepted";
		case ProxyDigestResult::Missing: return "missing header";
		case ProxyDigestResult::Duplicate: return "duplicate header";
		case ProxyDigestResult::Malformed: return "malformed value";
		case ProxyDigestResult::UnsupportedScheme: return "unsupported scheme";
		case ProxyDigestResult::UnsupportedAlgorithm: return "unsupported algorithm";
		case ProxyDigestResult::BadRealm: return "wrong realm";
		case ProxyDigestResult::BadQop: return "unsupported qop";
		case ProxyDigestResult::StaleNonce: return "stale nonce";
		case ProxyDigestResult::BadNonce: return "wrong opaque";
		case ProxyDigestResult::Replayed: return "replayed nonce count";
		case ProxyDigestResult::BadCredentials: return "bad credentials";
		}
		return "unknown";
	}

	bool ProxyDigestAuth::Prepare ()
	{
		m_Ha1Md5.clear ();
		m_Ha1Sha256.clear ();
		m_Prepared = false;
		if (!enabled) return false;
		if (username.empty () || password.empty ()) return false;
		if (username.size () > PROXY_AUTH_MAX_FIELD_SIZE || password.size () > PROXY_AUTH_MAX_FIELD_SIZE) return false;
		// A realm is interpolated verbatim into a quoted-string, so it must not
		// contain a quote, a backslash, a comma or a line break.
		if (realm.empty () || realm.size () > 64) return false;
		for (const char ch : realm)
			if (ch == '"' || ch == '\\' || ch == ',' || static_cast<unsigned char> (ch) < 0x20
				|| static_cast<unsigned char> (ch) == 0x7f)
				return false;

		const std::string a1 = username + ":" + realm + ":" + password;
		m_Ha1Md5 = ProxyDigestHashHex (a1, ProxyDigestAlgorithm::Md5);
		m_Ha1Sha256 = ProxyDigestHashHex (a1, ProxyDigestAlgorithm::Sha256);
		std::string scrub (a1);
		CleanseString (scrub);
		m_Prepared = !m_Ha1Md5.empty () && !m_Ha1Sha256.empty ();
		return m_Prepared;
	}

	bool ProxyDigestAuth::NewChallenge (std::int64_t nowMs, std::string & nonce, std::string & opaque)
	{
		nonce.clear ();
		opaque.clear ();
		if (!m_Prepared) return false;

		unsigned char raw[16] { };
		if (RAND_bytes (raw, static_cast<int> (sizeof (raw))) != 1)
		{
			LogPrint (eLogCritical, "HTTPProxy: CSPRNG unavailable for a Digest nonce");
			return false;
		}
		unsigned char tag[16] { };
		if (RAND_bytes (tag, static_cast<int> (sizeof (tag))) != 1)
		{
			OPENSSL_cleanse (raw, sizeof (raw));
			LogPrint (eLogCritical, "HTTPProxy: CSPRNG unavailable for a Digest opaque value");
			return false;
		}
		static constexpr char HEX[] = "0123456789abcdef";
		auto hex = [&] (const unsigned char * data, std::size_t size)
		{
			std::string out;
			out.reserve (size * 2);
			for (std::size_t i = 0; i < size; ++i)
			{
				out.push_back (HEX[data[i] >> 4]);
				out.push_back (HEX[data[i] & 0x0f]);
			}
			return out;
		};
		nonce = hex (raw, sizeof (raw));
		opaque = hex (tag, 8);
		OPENSSL_cleanse (raw, sizeof (raw));
		OPENSSL_cleanse (tag, sizeof (tag));

		SeedNonce (nonce, opaque, nowMs);
		return true;
	}

	void ProxyDigestAuth::SeedNonce (std::string nonce, std::string opaque, std::int64_t nowMs)
	{
		if (nonce.empty () || nonce.size () > 64 || opaque.size () > 64) return;
		std::lock_guard<std::mutex> lock (m_Mutex);
		PurgeExpiredLocked (nowMs);
		if (m_Nonces.size () >= PROXY_DIGEST_MAX_NONCES)
		{
			// Every entry is live, so evict the one that expires first. The
			// cache stays bounded instead of growing with client requests.
			auto oldest = m_Nonces.begin ();
			for (auto it = m_Nonces.begin (); it != m_Nonces.end (); ++it)
				if (it->second.expiresMs < oldest->second.expiresMs) oldest = it;
			m_Nonces.erase (oldest);
		}
		NonceState state;
		state.expiresMs = nowMs + PROXY_DIGEST_NONCE_LIFETIME_MS;
		state.lastNc = 0;
		state.opaque = std::move (opaque);
		m_Nonces[std::move (nonce)] = std::move (state);
	}

	void ProxyDigestAuth::PurgeExpiredLocked (std::int64_t nowMs)
	{
		for (auto it = m_Nonces.begin (); it != m_Nonces.end ();)
		{
			if (it->second.expiresMs <= nowMs)
				it = m_Nonces.erase (it);
			else
				++it;
		}
	}

	std::size_t ProxyDigestAuth::NonceCount () const
	{
		std::lock_guard<std::mutex> lock (m_Mutex);
		return m_Nonces.size ();
	}

	ProxyDigestResult ProxyDigestAuth::Verify (std::string_view value, std::string_view method, std::int64_t nowMs)
	{
		if (!m_Prepared) return ProxyDigestResult::BadCredentials;

		ProxyDigestCredentials credentials;
		if (!ParseProxyDigestCredentials (value, credentials))
			return StartsWithIgnoreCase (value, "digest") ? ProxyDigestResult::Malformed
														  : ProxyDigestResult::UnsupportedScheme;

		ProxyDigestAlgorithm algorithm = ProxyDigestAlgorithm::Md5;
		if (credentials.hasAlgorithm)
		{
			if (EqualsIgnoreCase (credentials.algorithm, "MD5"))
				algorithm = ProxyDigestAlgorithm::Md5;
			else if (EqualsIgnoreCase (credentials.algorithm, "SHA-256"))
				algorithm = ProxyDigestAlgorithm::Sha256;
			else
				return ProxyDigestResult::UnsupportedAlgorithm;
		}
		if (!ProxySecretEquals (realm, credentials.realm)) return ProxyDigestResult::BadRealm;
		if (!EqualsIgnoreCase (credentials.qop, "auth")) return ProxyDigestResult::BadQop;
		if (credentials.nc.size () != PROXY_DIGEST_NC_LENGTH || !IsHexString (credentials.nc))
			return ProxyDigestResult::Malformed;
		const std::size_t expected_length = algorithm == ProxyDigestAlgorithm::Sha256 ? 64 : 32;
		if (credentials.response.size () != expected_length || !IsHexString (credentials.response))
			return ProxyDigestResult::Malformed;

		std::uint32_t nc = 0;
		for (const char ch : credentials.nc)
		{
			nc = (nc << 4)
				| static_cast<std::uint32_t> (ch >= '0' && ch <= '9' ? ch - '0' : LowerASCII (ch) - 'a' + 10);
		}
		if (nc == 0) return ProxyDigestResult::Malformed;

		std::lock_guard<std::mutex> lock (m_Mutex);
		auto nonce = m_Nonces.find (credentials.nonce);
		if (nonce == m_Nonces.end ()) return ProxyDigestResult::StaleNonce;
		if (nonce->second.expiresMs <= nowMs)
		{
			m_Nonces.erase (nonce);
			return ProxyDigestResult::StaleNonce;
		}
		if (!ProxySecretEquals (nonce->second.opaque, credentials.opaque)) return ProxyDigestResult::BadNonce;
		// The nonce count must strictly increase: a replay and an out-of-order
		// retry are both rejected without touching the recorded value.
		if (nc <= nonce->second.lastNc) return ProxyDigestResult::Replayed;

		const std::string & ha1 = algorithm == ProxyDigestAlgorithm::Sha256 ? m_Ha1Sha256 : m_Ha1Md5;
		const std::string ha2 = ProxyDigestHashHex (std::string (method) + ":" + credentials.uri, algorithm);
		const std::string kd = ha1 + ":" + credentials.nonce + ":" + credentials.nc + ":" + credentials.cnonce + ":"
			+ credentials.qop + ":" + ha2;
		const std::string expected = ProxyDigestHashHex (kd, algorithm);
		if (expected.empty ()) return ProxyDigestResult::BadCredentials;

		const bool response_ok = ProxySecretEquals (expected, ToLowerASCII (credentials.response));
		const bool user_ok = ProxySecretEquals (username, credentials.username);
		if (!response_ok || !user_ok) return ProxyDigestResult::BadCredentials;

		nonce->second.lastNc = nc;
		return ProxyDigestResult::Accepted;
	}

	std::string HTTPProxyDigestChallenge (std::string_view realm, std::string_view nonce, std::string_view opaque,
		ProxyDigestAlgorithm algorithm, bool stale)
	{
		std::string value = "Digest realm=\"";
		value += EscapeHTML (realm);
		value += "\", nonce=\"";
		value += EscapeHTML (nonce);
		value += "\", opaque=\"";
		value += EscapeHTML (opaque);
		value += "\", algorithm=";
		value += algorithm == ProxyDigestAlgorithm::Sha256 ? "SHA-256" : "MD5";
		value += ", charset=UTF-8, qop=\"auth\"";
		if (stale) value += ", stale=true";
		return value;
	}

	std::string HTTPProxyDigestChallengeResponse (
		std::string_view realm, std::string_view nonce, std::string_view opaque, bool stale)
	{
		std::stringstream body;
		body << "<html>\r\n"
			 << pageHead << "<body><h1>" << EscapeHTML (tr ("Proxy authentication required")) << "</h1>\r\n"
			 << "<p>" << EscapeHTML (tr ("This proxy requires local authentication")) << "</p>\r\n"
			 << "</body>\r\n"
			 << "</html>\r\n";
		const std::string content = body.str ();

		// Two `Proxy-Authenticate` headers cannot be represented by the
		// HTTPRes header map (it is keyed by name), so the response is built
		// explicitly. RFC 7616 clients accept the first algorithm they support,
		// so the stronger one is offered first; an MD5-only client reads the
		// second challenge.
		std::string response = "HTTP/1.1 407 Proxy Authentication Required\r\n";
		response += "Proxy-Authenticate: "
			+ HTTPProxyDigestChallenge (realm, nonce, opaque, ProxyDigestAlgorithm::Sha256, stale) + "\r\n";
		response += "Proxy-Authenticate: "
			+ HTTPProxyDigestChallenge (realm, nonce, opaque, ProxyDigestAlgorithm::Md5, stale) + "\r\n";
		response += "Content-Type: text/html; charset=UTF-8\r\n";
		response += "Content-Length: " + std::to_string (content.size ()) + "\r\n";
		response += "Connection: close\r\n\r\n";
		response += content;
		return response;
	}

	class HTTPReqHandler : public i2p::client::I2PServiceHandler, public std::enable_shared_from_this<HTTPReqHandler>
	{
	private:
		bool HandleRequest ();
		void HandleSockRecv (const boost::system::error_code & ecode, std::size_t bytes_transfered);
		void Terminate ();
		/** Close both sockets without calling Done (safe in the destructor). */
		void CloseSockets () noexcept;
		void AsyncSockRead ();
		static bool ExtractAddressHelper (i2p::http::URL & url, std::string & jump, bool & confirm);
		static bool VerifyAddressHelper (std::string_view jump);
		void SanitizeHTTPRequest (i2p::http::HTTPReq & req);
		void SentHTTPFailed (const boost::system::error_code & ecode);
		void ArmHeaderDeadline ();
		void HandleHeaderDeadline (const boost::system::error_code & ecode);
		void HandleStreamRequestComplete (std::shared_ptr<i2p::stream::Stream> stream);
		/* error helpers */
		void GenericProxyError (
			std::string_view title, std::string_view description, int code = HTTP_PROXY_STATUS_INTERNAL_ERROR);
		/** Trusted HTML fragment with an escaped title. */
		void SendProxyInfoHTML (
			std::string_view title, std::string_view html, int code = HTTP_PROXY_STATUS_INTERNAL_ERROR);
		void HostNotFound (std::string_view host);
		void SendProxyError (std::string_view content, int code = HTTP_PROXY_STATUS_INTERNAL_ERROR);
		void SendRedirect (const std::string & address);
		/** Local proxy authentication; also strips every Proxy-* header. */
		bool AuthorizeClient ();
		/** @param stale true when a Digest nonce expired and the client may retry. */
		void SendProxyAuthRequired (bool stale = false);

		void ForwardToUpstreamProxy ();
		void HandleUpstreamHTTPProxyConnect (const boost::system::error_code & ec);
		void HandleUpstreamSocksProxyConnect (const boost::system::error_code & ec);
		void HTTPConnect (std::string_view host, uint16_t port);
		void HandleHTTPConnectStreamRequestComplete (std::shared_ptr<i2p::stream::Stream> stream);

		typedef std::function<void (boost::asio::ip::tcp::endpoint)> ProxyResolvedHandler;

		void HandleUpstreamProxyResolved (const boost::system::error_code & ecode,
			boost::asio::ip::tcp::resolver::results_type endpoints, ProxyResolvedHandler handler);

		void SocksProxySuccess ();
		void HandoverToUpstreamProxy ();

		/**
		 * Cancellable deadline for one upstream phase (resolve, connect or
		 * SOCKS5 handshake). The phase name is a fixed internal label, never
		 * client input, and never contains a URL or a credential.
		 */
		void ArmUpstreamDeadline (std::string_view phase);
		void CancelUpstreamDeadline ();
		void HandleUpstreamDeadline ();
		/** 504 + close, shared by every upstream failure path. */
		void UpstreamTimedOut ();

		uint8_t m_recv_chunk[8192];
		std::string m_recv_buf; // from client
		std::string m_send_buf; // to upstream
		std::shared_ptr<boost::asio::ip::tcp::socket> m_sock;
		std::shared_ptr<boost::asio::ip::tcp::socket> m_proxysock;
		boost::asio::ip::tcp::resolver m_proxy_resolver;
		boost::asio::steady_timer m_HeaderTimer;
		boost::asio::steady_timer m_UpstreamTimer;
		boost::asio::steady_timer m_AuthTimer;
		std::string m_UpstreamPhase;
		/** Set once an upstream phase expired: late callbacks must not reply. */
		bool m_UpstreamAbandoned = false;
		std::string m_OutproxyUrl, m_Response;
		bool m_Addresshelper, m_SendUserAgent;
		// Snapshot of the owner's local credential at accept time. Later
		// configuration changes apply to new connections only.
		ProxyBasicAuth m_Auth;
		ProxyAuthScheme m_AuthScheme = ProxyAuthScheme::None;
		std::shared_ptr<ProxyDigestAuth> m_DigestAuth;
		std::string m_AuthRealm = std::string (HTTP_PROXY_AUTH_REALM);
		std::uint32_t m_AuthFailureDelayMs = 0;
		i2p::http::URL m_ProxyURL;
		i2p::http::URL m_RequestURL;
		/** Validated destination of the current request. */
		ProxyRequestTarget m_Target;
		int m_req_len = 0;
		i2p::http::URL m_ClientRequestURL;
		i2p::http::HTTPReq m_ClientRequest;
		i2p::http::HTTPRes m_ClientResponse;
		std::stringstream m_ClientRequestBuffer;

	public:
		HTTPReqHandler (
			std::shared_ptr<i2p::client::I2PService> parent, std::shared_ptr<boost::asio::ip::tcp::socket> sock)
			: I2PServiceHandler (parent)
			, m_sock (sock)
			, m_proxysock (std::make_shared<boost::asio::ip::tcp::socket> (parent->GetService ()))
			, m_proxy_resolver (parent->GetService ())
			, m_HeaderTimer (parent->GetService ())
			, m_UpstreamTimer (parent->GetService ())
			, m_AuthTimer (parent->GetService ())
			, m_OutproxyUrl (std::static_pointer_cast<HTTPProxy> (parent)->GetOutproxyURL ())
			, m_Addresshelper (std::static_pointer_cast<HTTPProxy> (parent)->GetHelperSupport ())
			, m_SendUserAgent (std::static_pointer_cast<HTTPProxy> (parent)->GetSendUserAgent ())
			, m_Auth (std::static_pointer_cast<HTTPProxy> (parent)->GetBasicAuth ())
			, m_AuthScheme (std::static_pointer_cast<HTTPProxy> (parent)->GetAuthScheme ())
			, m_DigestAuth (std::static_pointer_cast<HTTPProxy> (parent)->GetDigestAuth ())
			, m_AuthRealm (std::static_pointer_cast<HTTPProxy> (parent)->GetAuthRealm ())
			, m_AuthFailureDelayMs (std::static_pointer_cast<HTTPProxy> (parent)->GetAuthFailureDelayMs ())
		{
		}
		// A handler can time out and be destroyed by the owner. Done() must
		// not be called here because shared_from_this() is invalid once the
		// last reference is gone; closing the sockets is still required.
		~HTTPReqHandler ()
		{
			m_HeaderTimer.cancel ();
			m_UpstreamTimer.cancel ();
			m_AuthTimer.cancel ();
			CloseSockets ();
		}
		void Handle ()
		{
			ArmHeaderDeadline ();
			AsyncSockRead ();
		} /* overload */
	};

	void HTTPReqHandler::ArmHeaderDeadline ()
	{
		auto weak = weak_from_this ();
		m_HeaderTimer.expires_after (std::chrono::seconds (HTTP_PROXY_HEADER_TIMEOUT));
		m_HeaderTimer.async_wait (
			[weak] (const boost::system::error_code & ecode)
			{
				if (ecode) return;
				auto self = weak.lock ();
				if (!self) return;
				self->HandleHeaderDeadline (boost::system::error_code ());
			});
	}

	void HTTPReqHandler::HandleHeaderDeadline (const boost::system::error_code &)
	{
		if (Kill ()) return;
		LogPrint (eLogWarning, "HTTPProxy: request header was not completed in ", HTTP_PROXY_HEADER_TIMEOUT,
			" seconds, closing connection");
		CloseSockets ();
		Done (shared_from_this ());
	}

	void HTTPReqHandler::AsyncSockRead ()
	{
		LogPrint (eLogDebug, "HTTPProxy: Async sock read");
		if (!m_sock)
		{
			LogPrint (eLogError, "HTTPProxy: No socket for read");
			return;
		}
		m_sock->async_read_some (boost::asio::buffer (m_recv_chunk, sizeof (m_recv_chunk)),
			std::bind (
				&HTTPReqHandler::HandleSockRecv, shared_from_this (), std::placeholders::_1, std::placeholders::_2));
	}

	void HTTPReqHandler::CloseSockets () noexcept
	{
		// Move the references out first so an exception from logging or from a
		// socket operation cannot leave a half-closed member behind. The
		// remaining shared_ptr destructors close the descriptors.
		auto sock = std::move (m_sock);
		auto proxysock = std::move (m_proxysock);
		try
		{
			if (sock)
			{
				LogPrint (eLogDebug, "HTTPProxy: Close sock");
				boost::system::error_code ec;
				sock->close (ec);
			}
			if (proxysock)
			{
				LogPrint (eLogDebug, "HTTPProxy: Close proxysock");
				boost::system::error_code ec;
				proxysock->close (ec);
			}
		}
		catch (...)
		{
			// Closing is best-effort; the destructor must not throw.
		}
	}

	void HTTPReqHandler::Terminate ()
	{
		m_HeaderTimer.cancel ();
		m_UpstreamTimer.cancel ();
		m_AuthTimer.cancel ();
		// Idempotent: Kill() may already have been called by another callback,
		// in which case the owner's reference was already released (or the
		// owner is clearing its handler set). Sockets are always closed.
		const bool alreadyDead = Kill ();
		CloseSockets ();
		if (!alreadyDead) Done (shared_from_this ());
	}

	void HTTPReqHandler::GenericProxyError (std::string_view title, std::string_view description, int code)
	{
		// Both arguments are plain text: every untrusted value must already be
		// escaped by the caller, and the escaping happens here as well so a
		// future call site cannot interpolate markup.
		std::stringstream ss;
		ss << "<h1>" << EscapeHTML (tr ("Proxy error")) << ": " << EscapeHTML (title) << "</h1>\r\n";
		ss << "<p>" << EscapeHTML (description) << "</p>\r\n";
		SendProxyError (ss.str (), code);
	}

	void HTTPReqHandler::SendProxyInfoHTML (std::string_view title, std::string_view html, int code)
	{
		// The fragment is built only from escaped values and fixed markup.
		std::stringstream ss;
		ss << "<h1>" << EscapeHTML (tr ("Proxy info")) << ": " << EscapeHTML (title) << "</h1>\r\n";
		ss << html;
		SendProxyError (ss.str (), code);
	}

	void HTTPReqHandler::HostNotFound (std::string_view host)
	{
		const std::string escapedHost = EscapeHTML (host);
		std::stringstream ss;
		ss << "<h1>" << EscapeHTML (tr ("Proxy error: Host not found")) << "</h1>\r\n"
		   << "<p>" << EscapeHTML (tr ("Remote host not found in router's addressbook")) << "</p>\r\n"
		   << "<p>" << EscapeHTML (tr ("You may try to find this host on jump services below")) << ":</p>\r\n"
		   << "<ul>\r\n";
		for (const auto & jump : jumporder)
		{
			auto js = jumpservices.find (jump);
			if (js != jumpservices.end ())
				ss << "  <li><a href=\"" << js->second << escapedHost << "\">" << js->first << "</a></li>\r\n";
		}
		ss << "</ul>\r\n";
		// 502: the proxy is a gateway and could not resolve a valid destination.
		SendProxyError (ss.str (), HTTP_PROXY_STATUS_BAD_GATEWAY);
	}

	void HTTPReqHandler::SendProxyError (std::string_view content, int code)
	{
		if (!m_sock)
		{
			Terminate ();
			return;
		}
		i2p::http::HTTPRes res;
		res.code = code;
		res.add_header ("Content-Type", "text/html; charset=UTF-8");
		res.add_header ("Connection", "close");
		std::stringstream ss;
		ss << "<html>\r\n"
		   << pageHead << "<body>" << content << "</body>\r\n"
		   << "</html>\r\n";
		res.body = ss.str ();
		m_Response = res.to_string ();
		boost::asio::async_write (*m_sock, boost::asio::buffer (m_Response), boost::asio::transfer_all (),
			std::bind (&HTTPReqHandler::SentHTTPFailed, shared_from_this (), std::placeholders::_1));
	}

	void HTTPReqHandler::SendRedirect (const std::string & address)
	{
		if (!m_sock)
		{
			Terminate ();
			return;
		}
		// A header value must never carry a line break. The address comes from
		// the parsed request, so this is defence in depth only.
		if (address.find ('\r') != std::string::npos || address.find ('\n') != std::string::npos
			|| address.size () > HTTP_PROXY_MAX_HEADER_SIZE)
		{
			GenericProxyError (tr ("Invalid request"), tr ("Invalid request URI"), HTTP_PROXY_STATUS_BAD_REQUEST);
			return;
		}
		i2p::http::HTTPRes res;
		res.code = 302;
		res.add_header ("Location", address);
		res.add_header ("Connection", "close");
		m_Response = res.to_string ();
		boost::asio::async_write (*m_sock, boost::asio::buffer (m_Response), boost::asio::transfer_all (),
			std::bind (&HTTPReqHandler::SentHTTPFailed, shared_from_this (), std::placeholders::_1));
	}

	void HTTPReqHandler::SendProxyAuthRequired (bool stale)
	{
		if (!m_sock)
		{
			Terminate ();
			return;
		}
		// Fixed-size page with a bounded header block: no client input is
		// echoed, so the response size cannot depend on the request. A Digest
		// challenge carries a fresh nonce; when the CSPRNG is unavailable the
		// proxy refuses rather than advertising an unverifiable nonce.
		if (m_AuthScheme == ProxyAuthScheme::Digest)
		{
			std::string nonce, opaque;
			if (!m_DigestAuth
				|| !m_DigestAuth->NewChallenge (
					static_cast<std::int64_t> (i2p::util::GetMillisecondsSinceEpoch ()), nonce, opaque))
			{
				GenericProxyError (tr ("Internal error"), tr ("The proxy cannot issue an authentication challenge"),
					HTTP_PROXY_STATUS_INTERNAL_ERROR);
				return;
			}
			m_Response = HTTPProxyDigestChallengeResponse (m_AuthRealm, nonce, opaque, stale);
		}
		else
			m_Response = HTTPProxyAuthChallengeResponse (m_AuthRealm);
		boost::asio::async_write (*m_sock, boost::asio::buffer (m_Response), boost::asio::transfer_all (),
			std::bind (&HTTPReqHandler::SentHTTPFailed, shared_from_this (), std::placeholders::_1));
	}

	bool HTTPReqHandler::AuthorizeClient ()
	{
		if (m_AuthScheme == ProxyAuthScheme::None)
		{
			// Always strip: no later branch, including CONNECT, the outproxy
			// path or an addresshelper form, can forward a credential.
			HTTPProxyStripCredentials (m_ClientRequest);
			return true;
		}

		const std::size_t occurrences = m_ClientRequest.GetNumHeaders ("Proxy-Authorization");
		std::string provided = occurrences == 1 ? m_ClientRequest.GetHeader ("Proxy-Authorization") : std::string ();
		HTTPProxyStripCredentials (m_ClientRequest);

		bool rejected = false;
		bool stale = false;
		std::string_view reason = "unknown";
		if (m_AuthScheme == ProxyAuthScheme::Digest)
		{
			if (!m_DigestAuth || !m_DigestAuth->HasUsableCredentials ())
			{
				CleanseString (provided);
				// An enabled but unusable configuration denies every request
				// instead of falling back to an unauthenticated proxy.
				LogPrint (eLogCritical, "HTTPProxy: Digest authentication is enabled without usable credentials");
				SendProxyAuthRequired ();
				return false;
			}
			ProxyDigestResult result = ProxyDigestResult::Missing;
			if (occurrences == 0)
				result = ProxyDigestResult::Missing;
			else if (occurrences > 1)
				result = ProxyDigestResult::Duplicate;
			else
				result = m_DigestAuth->Verify (provided, m_ClientRequest.method,
					static_cast<std::int64_t> (i2p::util::GetMillisecondsSinceEpoch ()));
			CleanseString (provided);
			if (ProxyDigestAllowed (result)) return true;
			rejected = true;
			stale = ProxyDigestStale (result);
			reason = ProxyDigestResultName (result);
		}
		else
		{
			const ProxyAuthResult result = m_Auth.Verify (provided, occurrences);
			CleanseString (provided);
			if (ProxyAuthAllowed (result)) return true;
			rejected = true;
			reason = ProxyAuthResultName (result);
		}

		if (rejected)
		{
			// Log only the stable reason: never the scheme payload, the
			// username, the password, the nonce, the nonce count or the digest
			// response, and never the raw header value.
			LogPrint (eLogWarning, "HTTPProxy: Local authentication rejected request (", reason, ")");
			if (m_AuthFailureDelayMs == 0 || stale)
			{
				SendProxyAuthRequired (stale);
				return false;
			}
			// A configured failure delay is applied as a timer so the service
			// thread keeps serving other connections; it is not a sleep.
			auto weak = weak_from_this ();
			m_AuthTimer.expires_after (std::chrono::milliseconds (m_AuthFailureDelayMs));
			m_AuthTimer.async_wait (
				[weak] (const boost::system::error_code & ecode)
				{
					if (ecode) return;
					auto self = weak.lock ();
					if (!self) return;
					self->SendProxyAuthRequired (false);
				});
		}
		return false;
	}

	bool HTTPReqHandler::ExtractAddressHelper (i2p::http::URL & url, std::string & jump, bool & confirm)
	{
		confirm = false;
		const char * param = "i2paddresshelper=";
		std::size_t pos = url.query.find (param);
		std::size_t len = std::strlen (param);
		std::map<std::string, std::string> params;

		if (pos == std::string::npos) return false; /* not found */
		if (!url.parse_query (params)) return false;

		std::string value = params["i2paddresshelper"];
		len += value.length ();
		jump = i2p::http::UrlDecode (value);
		if (!VerifyAddressHelper (jump))
		{
			LogPrint (eLogError, "HTTPProxy: Malformed jump link ", jump);
			return false;
		}

		// if we need update exists, request formed with update param
		if (params["update"] == "true")
		{
			len += std::strlen ("&update=true");
			confirm = true;
		}

		// if helper is not only one query option and it placed after user's query
		if (pos != 0 && url.query[pos - 1] == '&')
		{
			pos--;
			len++;
		}
		// if helper is not only one query option and it placed before user's query
		else if (pos == 0 && url.query.length () > len && url.query[len] == '&')
		{
			// we don't touch the '?' but remove the trailing '&'
			len++;
		}
		else
		{
			// there is no more query options, resetting hasquery flag
			url.hasquery = false;
		}

		// reset hasquery flag and remove addresshelper from URL
		url.query.replace (pos, len, "");
		return true;
	}

	bool HTTPReqHandler::VerifyAddressHelper (std::string_view jump)
	{
		auto pos = jump.find (".b32.i2p");
		if (pos != std::string::npos)
		{
			auto b32 = jump.substr (0, pos);
			for (auto & ch : b32)
				if (!i2p::data::IsBase32 (ch)) return false;
			return true;
		}
		else
		{
			bool padding = false;
			for (auto & ch : jump)
			{
				if (ch == '=')
					padding = true;
				else
				{
					if (padding) return false; // other chars after padding
					if (!i2p::data::IsBase64 (ch)) return false;
				}
			}
			return true;
		}
		return false;
	}

	void HTTPReqHandler::SanitizeHTTPRequest (i2p::http::HTTPReq & req)
	{
		/* drop common headers */
		req.RemoveHeader ("Via");
		req.RemoveHeader ("From");
		req.RemoveHeader ("Forwarded");
		req.RemoveHeader ("DNT"); // Useless DoNotTrack flag
		req.RemoveHeader ("Accept", "Accept-Encoding"); // Accept*, but Accept-Encoding
		/* drop proxy-disclosing headers */
		req.RemoveHeader ("X-Forwarded");
		req.RemoveHeader ("Proxy-"); // Proxy-*

		/*
		 * Hop-by-hop headers and the X-Forwarded-*/ *-IP family are never * forwarded
				.The websocket upgrade decision is taken first because a * legitimate `Connection
			: Upgrade` must survive in a normalised form.* / const std::string connection
			= req.GetHeader ("Connection");
		const auto upgrade = connection.find ("pgrade");
		const bool preserveUpgrade = upgrade != std::string::npos && upgrade
			&& std::tolower (static_cast<unsigned char> (connection[upgrade - 1])) == 'u';
		StripHopByHopHeaders (req);

		// NTLM authenticates the upstream connection, not the request, so a
		// forwarded value would bind the destination connection to us.
		if (IsNTLMAuthorizationValue (req.GetHeader ("Authorization"))) req.RemoveHeader ("Authorization");

		/* replace headers */
		if (!m_SendUserAgent) req.UpdateHeader ("User-Agent", "MYOB/6.66 (AN/ON)");

		/**
		 * i2pd PR #1816:
		 * Android Webview send this with the value set to the application ID, so we drop it,
		 * but only if it does not belong to an AJAX request (*HttpRequest, like XMLHttpRequest).
		 */
		if (req.GetHeader ("X-Requested-With") != "")
		{
			auto h = req.GetHeader ("X-Requested-With");
			auto x = h.find ("HttpRequest");
			if (x == std::string::npos) // not found
				req.RemoveHeader ("X-Requested-With");
		}

		/**
		 * according to i2p ticket #1862:
		 * leave Referer if requested URL with same schema, host and port,
		 * otherwise, drop it.
		 */
		if (req.GetHeader ("Referer") != "")
		{
			i2p::http::URL reqURL;
			reqURL.parse (req.uri);
			i2p::http::URL refURL;
			refURL.parse (req.GetHeader ("Referer"));
			if (!boost::iequals (reqURL.schema, refURL.schema) || !boost::iequals (reqURL.host, refURL.host)
				|| reqURL.port != refURL.port)
				req.RemoveHeader ("Referer");
		}

		/* add headers */
		/* close connection, if not Connection: (U|u)pgrade (for websocket) */
		if (preserveUpgrade)
			req.UpdateHeader ("Connection", "Upgrade");
		else
			req.UpdateHeader ("Connection", "close");
	}

	/**
	 * @brief Try to parse request from @a m_recv_buf
	 *   If parsing success, rebuild request and store to @a m_send_buf
	 * with remaining data tail
	 * @return true on processed request or false if more data needed
	 */
	bool HTTPReqHandler::HandleRequest ()
	{
		/*
		 * Refuse an unsupported request scheme before the full parser can turn
		 * it into a different error and before any address-book, resolver or
		 * outproxy side effect. `ftp://`, `mailto:` and every other non-HTTP
		 * scheme must be a 403, matching I2P Java
		 * `I2PTunnelHTTPClient.isSupportedAddress()`.
		 */
		if (const auto firstLine = m_recv_buf.find ("\r\n"); firstLine != std::string::npos)
		{
			const std::string_view line (m_recv_buf.data (), firstLine);
			const auto firstSpace = line.find (' ');
			const auto secondSpace
				= firstSpace == std::string_view::npos ? std::string_view::npos : line.find (' ', firstSpace + 1);
			if (firstSpace != std::string_view::npos && secondSpace != std::string_view::npos)
			{
				const auto method = line.substr (0, firstSpace);
				const auto target = line.substr (firstSpace + 1, secondSpace - firstSpace - 1);
				const auto early = ClassifyRequestTarget (method, target, { });
				if (early.error == ProxyTargetError::UnsupportedScheme)
				{
					m_HeaderTimer.cancel ();
					LogPrint (eLogWarning, "HTTPProxy: Rejected a request with an unsupported scheme");
					CleanseString (m_recv_buf);
					GenericProxyError (tr ("Unsupported request"),
						tr ("Only http:// requests and HTTPS CONNECT tunnels are supported"),
						HTTP_PROXY_STATUS_FORBIDDEN);
					return true;
				}
			}
		}

		m_req_len = m_ClientRequest.parse (m_recv_buf);

		if (!m_req_len) return false; /* need more data */
		// Header complete (or rejected): the handshake deadline no longer applies.
		m_HeaderTimer.cancel ();

		if (m_req_len < 0)
		{
			LogPrint (eLogError, "HTTPProxy: Unable to parse request");
			GenericProxyError (
				tr ("Invalid request"), tr ("Proxy unable to parse your request"), HTTP_PROXY_STATUS_BAD_REQUEST);
			return true; /* parse error */
		}

		// The incremental buffer cap is enforced before appending, but keep a
		// second bound on the parsed header length so a future change to the
		// read path cannot feed an oversized header to the request parser.
		if (static_cast<size_t> (m_req_len) > HTTP_PROXY_MAX_HEADER_SIZE)
		{
			LogPrint (eLogWarning, "HTTPProxy: Request header exceeds ", HTTP_PROXY_MAX_HEADER_SIZE, " bytes");
			CleanseString (m_recv_buf);
			GenericProxyError (
				tr ("Request too large"), tr ("HTTP request header is too large"), HTTP_PROXY_STATUS_HEADER_TOO_LARGE);
			return true;
		}

		// Local authentication runs before any address-book mutation, redirect
		// or stream creation. It also rewrites the parsed request so the local
		// credential is gone whatever happens below.
		if (!AuthorizeClient ()) return true;

		// Only the method and the validated destination host are logged: the
		// request target may carry a query string, a session token or userinfo.
		ProxyRequestTarget requestTarget
			= ClassifyRequestTarget (m_ClientRequest.method, m_ClientRequest.uri, m_ClientRequest.GetHeader ("Host"));
		if (!requestTarget.IsValid ())
		{
			LogPrint (eLogWarning, "HTTPProxy: Rejected ", m_ClientRequest.method, " request (",
				static_cast<int> (requestTarget.error), ")");
			CleanseString (m_recv_buf);
			if (requestTarget.error == ProxyTargetError::UnsupportedScheme)
				GenericProxyError (tr ("Unsupported request"),
					tr ("Only http:// requests and HTTPS CONNECT tunnels are supported"), HTTP_PROXY_STATUS_FORBIDDEN);
			else
				GenericProxyError (tr ("Invalid request"), tr ("Invalid request URI"), HTTP_PROXY_STATUS_BAD_REQUEST);
			return true;
		}
		LogPrint (eLogDebug, "HTTPProxy: ", m_ClientRequest.method, " for host ", requestTarget.host, ":",
			requestTarget.port);
		m_Target = requestTarget;

		m_RequestURL.parse (m_ClientRequest.uri);
		bool m_Confirm;

		std::string jump;
		if (ExtractAddressHelper (m_RequestURL, jump, m_Confirm))
		{
			if (!m_Addresshelper || !i2p::client::context.GetAddressBook ().IsEnabled ())
			{
				LogPrint (eLogWarning, "HTTPProxy: Addresshelper request rejected");
				GenericProxyError (
					tr ("Invalid request"), tr ("Addresshelper is not supported"), HTTP_PROXY_STATUS_BAD_REQUEST);
				return true;
			}

			if (m_RequestURL.host.empty ())
			{
				// The helper rewrites the host of an absolute URL. A relative
				// target has no host to bind the destination to, so never let
				// it reach the address book with an empty name.
				LogPrint (eLogWarning, "HTTPProxy: Addresshelper request without a destination host rejected");
				GenericProxyError (
					tr ("Invalid request"), tr ("Addresshelper is not supported"), HTTP_PROXY_STATUS_BAD_REQUEST);
				return true;
			}

			if (i2p::client::context.GetAddressBook ().RecordExists (m_RequestURL.host, jump))
			{
				std::string full_url = m_RequestURL.to_string ();
				SendRedirect (full_url);
				return true;
			}
			else if (!i2p::client::context.GetAddressBook ().FindAddress (m_RequestURL.host) || m_Confirm)
			{
				const std::string referer_raw = m_ClientRequest.GetHeader ("Referer");
				i2p::http::URL referer_url;
				if (!referer_raw.empty ())
				{
					referer_url.parse (referer_raw);
				}
				if (m_RequestURL.host != referer_url.host)
				{
					const std::string escapedHost = EscapeHTML (m_RequestURL.host);
					const std::string escapedJump = EscapeHTML (jump);
					if (m_Confirm) // Attempt to forced overwriting by link with "&update=true" from harmful URL
					{
						LogPrint (eLogWarning, "HTTPProxy: Address update from addresshelper rejected for ",
							m_RequestURL.host, " (referer is ", m_RequestURL.host.empty () ? "empty" : "harmful", ")");
						std::string full_url = EscapeHTML (m_RequestURL.to_string ());
						std::stringstream ss;
						ss << tr (
							"Host %s is <span class=\"warning\">already in router's addressbook</span>. <b>Be careful: "
							"source of this URL may be harmful!</b> Click here to update record: <a "
							"href=\"%s%s%s&update=true\">Continue</a>.",
							escapedHost.c_str (), full_url.c_str (),
							(full_url.find ('?') != std::string::npos ? "&amp;i2paddresshelper="
																	  : "?i2paddresshelper="),
							escapedJump.c_str ());
						SendProxyInfoHTML (tr ("Addresshelper forced update rejected"), ss.str ());
					}
					else // Preventing unauthorized additions to the address book
					{
						LogPrint (eLogDebug, "HTTPProxy: Adding address from addresshelper for ", m_RequestURL.host,
							" (generate refer-base page)");
						std::string full_url = EscapeHTML (m_RequestURL.to_string ());
						std::stringstream ss;
						ss << tr ("To add host <b>%s</b> in router's addressbook, click here: <a "
								  "href=\"%s%s%s\">Continue</a>.",
							escapedHost.c_str (), full_url.c_str (),
							(full_url.find ('?') != std::string::npos ? "&amp;i2paddresshelper="
																	  : "?i2paddresshelper="),
							escapedJump.c_str ());
						SendProxyInfoHTML (tr ("Addresshelper request"), ss.str ());
					}
					return true; /* request processed */
				}

				i2p::client::context.GetAddressBook ().InsertAddress (m_RequestURL.host, jump);
				LogPrint (eLogInfo, "HTTPProxy: Added address from addresshelper for ", m_RequestURL.host);
				std::string full_url = EscapeHTML (m_RequestURL.to_string ());
				std::stringstream ss;
				ss << tr ("Host %s added to router's addressbook from helper. Click here to proceed: <a "
						  "href=\"%s\">Continue</a>.",
					EscapeHTML (m_RequestURL.host).c_str (), full_url.c_str ());
				SendProxyInfoHTML (tr ("Addresshelper adding"), ss.str ());
				return true; /* request processed */
			}
			else
			{
				std::string full_url = EscapeHTML (m_RequestURL.to_string ());
				std::stringstream ss;
				ss << tr (
					"Host %s is <span class=\"warning\">already in router's addressbook</span>. Click here to update "
					"record: <a href=\"%s%s%s&update=true\">Continue</a>.",
					EscapeHTML (m_RequestURL.host).c_str (), full_url.c_str (),
					(full_url.find ('?') != std::string::npos ? "&amp;i2paddresshelper=" : "?i2paddresshelper="),
					EscapeHTML (jump).c_str ());
				SendProxyInfoHTML (tr ("Addresshelper update"), ss.str ());
				return true; /* request processed */
			}
		}

		// The classifier already resolved the authority (including an IPv6
		// CONNECT target and the origin-form default port), so no branch below
		// can act on an unvalidated host.
		const std::string dest_host = requestTarget.host;
		const uint16_t dest_port = requestTarget.port;
		const bool useConnect = requestTarget.useConnect;
		if (!useConnect)
		{
			SanitizeHTTPRequest (m_ClientRequest);
			/* absolute url: set the proper 'Host' header in the upstream request */
			if (requestTarget.absolute)
			{
				std::string h (dest_host);
				if (dest_port != 80) h += ":" + std::to_string (dest_port);
				m_ClientRequest.UpdateHeader ("Host", h);
			}
		}
		/* Never relay loopback/private clearnet targets through an outproxy. */
		if (!i2p::client::IsI2PHost (dest_host) && IsBlockedClearnetTarget (dest_host))
		{
			GenericProxyError (tr ("Blocked target"), tr ("Loopback and private clearnet targets are not allowed"),
				HTTP_PROXY_STATUS_FORBIDDEN);
			return true;
		}
		/* check dest_host really exists and inside I2P network */
		if (i2p::client::IsI2PHost (dest_host))
		{
			auto addr = i2p::client::context.GetAddressBook ().GetAddress (dest_host);
			if (addr)
			{
				if (!i2p::client::HasSuffixIgnoreCase (dest_host, ".b32.i2p"))
				{
					auto b32Host = addr->ToBase32 ();
					if (!b32Host.empty ())
					{
						b32Host += ".b32.i2p";
						if (m_RequestURL.port) b32Host += ":" + std::to_string (m_RequestURL.port);
						m_ClientRequest.UpdateHeader ("Host", b32Host); // replace our name to .b32.i2p address
					}
					else
					{
						// invalid address
						HostNotFound (dest_host);
						return true;
					}
				}
			}
			else
			{
				HostNotFound (dest_host);
				return true; /* request processed */
			}
		}
		else
		{
			if (m_OutproxyUrl.size ())
			{
				// Only the redacted form is logged: a configured outproxy URL
				// may carry `user:password@`, which must never reach a log.
				LogPrint (eLogDebug, "HTTPProxy: Using outproxy ", RedactURLUserInfo (m_OutproxyUrl));
				if (m_ProxyURL.parse (m_OutproxyUrl))
					ForwardToUpstreamProxy ();
				else
					GenericProxyError (
						tr ("Outproxy failure"), tr ("Bad outproxy settings"), HTTP_PROXY_STATUS_BAD_GATEWAY);
			}
			else
			{
				LogPrint (eLogWarning, "HTTPProxy: Outproxy failure for ", dest_host, ": no outproxy enabled");
				std::stringstream ss;
				ss << tr ("Host %s is not inside I2P network, but outproxy is not enabled", dest_host.c_str ());
				GenericProxyError (tr ("Outproxy failure"), ss.str (), HTTP_PROXY_STATUS_BAD_GATEWAY);
			}
			return true;
		}
		if (useConnect)
		{
			HTTPConnect (dest_host, dest_port);
			return true;
		}

		/* make relative url */
		m_RequestURL.schema = "";
		m_RequestURL.host = "";
		m_ClientRequest.uri = m_RequestURL.to_string ();

		/* drop original request from recv buffer */
		m_recv_buf.erase (0, m_req_len);
		/* build new buffer from modified request and data from original request */
		m_send_buf = m_ClientRequest.to_string ();
		m_send_buf.append (m_recv_buf);
		/* connect to destination */
		LogPrint (eLogDebug, "HTTPProxy: Connecting to host ", dest_host, ":", dest_port);
		auto owner = GetOwner ();
		if (!owner) return true;
		owner->UpdateLastActivityTime ();
		owner->CreateStream (
			std::bind (&HTTPReqHandler::HandleStreamRequestComplete, shared_from_this (), std::placeholders::_1),
			dest_host, dest_port);
		return true;
	}

	void HTTPReqHandler::ForwardToUpstreamProxy ()
	{
		LogPrint (eLogDebug, "HTTPProxy: Forwarded to upstream");

		/* build http request */
		m_ClientRequestURL = m_RequestURL;
		LogPrint (eLogDebug, "HTTPProxy: ", m_ClientRequestURL.host);
		m_ClientRequestURL.schema = "";
		m_ClientRequestURL.host = "";
		std::string origURI = m_ClientRequest.uri; // TODO: what do we need to change uri for?
		m_ClientRequest.uri = m_ClientRequestURL.to_string ();

		/* update User-Agent to ESR version of Firefox, same as Tor Browser below version 13, for non-HTTPS connections */
		if (m_ClientRequest.method != "CONNECT" && !m_SendUserAgent)
			m_ClientRequest.UpdateHeader (
				"User-Agent", "Mozilla/5.0 (Windows NT 10.0; rv:109.0) Gecko/20100101 Firefox/115.0");

		m_ClientRequest.write (m_ClientRequestBuffer);
		m_ClientRequestBuffer << m_recv_buf.substr (m_req_len);

		/* assume http if empty schema */
		if (m_ProxyURL.schema == "" || m_ProxyURL.schema == "http")
		{
			/* handle upstream http proxy */
			if (!m_ProxyURL.port) m_ProxyURL.port = 80;
			if (m_ProxyURL.is_i2p ())
			{
				m_ClientRequest.uri = origURI;
				auto auth = i2p::http::CreateBasicAuthorizationString (m_ProxyURL.user, m_ProxyURL.pass);
				if (!auth.empty ())
				{
					/* remove existing authorization if any */
					m_ClientRequest.RemoveHeader ("Proxy-");
					/* add own http proxy authorization */
					m_ClientRequest.AddHeader ("Proxy-Authorization", auth);
				}
				m_send_buf = m_ClientRequest.to_string ();
				m_recv_buf.erase (0, m_req_len);
				m_send_buf.append (m_recv_buf);
				auto owner = GetOwner ();
				if (owner)
				{
					ArmUpstreamDeadline ("connect");
					owner->CreateStream (std::bind (&HTTPReqHandler::HandleStreamRequestComplete, shared_from_this (),
											 std::placeholders::_1),
						m_ProxyURL.host, m_ProxyURL.port);
				}
			}
			else
			{
				auto self = shared_from_this ();
				ArmUpstreamDeadline ("resolve");
				m_proxy_resolver.async_resolve (m_ProxyURL.host, std::to_string (m_ProxyURL.port),
					std::bind (&HTTPReqHandler::HandleUpstreamProxyResolved, self, std::placeholders::_1,
						std::placeholders::_2,
						[self] (boost::asio::ip::tcp::endpoint ep)
						{
							if (!self->m_proxysock) return;
							self->m_proxysock->async_connect (ep,
								std::bind (
									&HTTPReqHandler::HandleUpstreamHTTPProxyConnect, self, std::placeholders::_1));
						}));
			}
		}
		else if (m_ProxyURL.schema == "socks")
		{
			/* handle upstream socks proxy */
			if (!m_ProxyURL.port) m_ProxyURL.port = 9050; // default to tor default if not specified
			auto self = shared_from_this ();
			ArmUpstreamDeadline ("resolve");
			m_proxy_resolver.async_resolve (m_ProxyURL.host, std::to_string (m_ProxyURL.port),
				std::bind (&HTTPReqHandler::HandleUpstreamProxyResolved, self, std::placeholders::_1,
					std::placeholders::_2,
					[self] (boost::asio::ip::tcp::endpoint ep)
					{
						if (!self->m_proxysock) return;
						self->m_proxysock->async_connect (ep,
							std::bind (&HTTPReqHandler::HandleUpstreamSocksProxyConnect, self, std::placeholders::_1));
					}));
		}
		else
		{
			/* unknown type: report a fixed reason, never the configured URL */
			GenericProxyError (tr ("Unknown outproxy URL"), tr ("The configured outproxy scheme is not supported"),
				HTTP_PROXY_STATUS_BAD_GATEWAY);
		}
	}

	void HTTPReqHandler::ArmUpstreamDeadline (std::string_view phase)
	{
		// The phase label is a fixed internal string: it never contains a URL,
		// a credential, a query string or any client input.
		m_UpstreamPhase.assign (phase);
		auto weak = weak_from_this ();
		m_UpstreamTimer.expires_after (std::chrono::seconds (HTTP_PROXY_UPSTREAM_TIMEOUT));
		m_UpstreamTimer.async_wait (
			[weak] (const boost::system::error_code & ecode)
			{
				if (ecode) return;
				auto self = weak.lock ();
				if (!self) return;
				self->HandleUpstreamDeadline ();
			});
	}

	void HTTPReqHandler::CancelUpstreamDeadline ()
	{
		m_UpstreamTimer.cancel ();
	}

	void HTTPReqHandler::HandleUpstreamDeadline ()
	{
		if (m_UpstreamAbandoned) return;
		m_UpstreamAbandoned = true;
		// Closing the upstream socket cancels every pending resolve, connect
		// and SOCKS5 handshake operation started from this handler.
		CancelUpstreamDeadline ();
		if (m_proxysock)
		{
			boost::system::error_code ec;
			m_proxysock->close (ec);
		}
		LogPrint (eLogWarning, "HTTPProxy: upstream ", m_UpstreamPhase, " exceeded ", HTTP_PROXY_UPSTREAM_TIMEOUT,
			" seconds");
		UpstreamTimedOut ();
	}

	void HTTPReqHandler::UpstreamTimedOut ()
	{
		if (!m_sock)
		{
			Terminate ();
			return;
		}
		GenericProxyError (tr ("Upstream timeout"), tr ("The upstream proxy did not respond in time"),
			HTTP_PROXY_STATUS_GATEWAY_TIMEOUT);
	}

	void HTTPReqHandler::HandleUpstreamProxyResolved (const boost::system::error_code & ec,
		boost::asio::ip::tcp::resolver::results_type endpoints, ProxyResolvedHandler handler)
	{
		if (m_UpstreamAbandoned) return;
		if (ec)
		{
			CancelUpstreamDeadline ();
			GenericProxyError (tr ("Cannot resolve upstream proxy"), ec.message (), HTTP_PROXY_STATUS_BAD_GATEWAY);
			return;
		}
		if (endpoints.begin () == endpoints.end () || !handler)
		{
			CancelUpstreamDeadline ();
			GenericProxyError (
				tr ("Cannot resolve upstream proxy"), tr ("No address was returned"), HTTP_PROXY_STATUS_BAD_GATEWAY);
			return;
		}
		// The resolve phase finished: the same deadline now covers connect.
		ArmUpstreamDeadline ("connect");
		handler (*endpoints.begin ());
	}

	void HTTPReqHandler::HandleUpstreamSocksProxyConnect (const boost::system::error_code & ec)
	{
		if (m_UpstreamAbandoned) return;
		if (!ec)
		{
			if (!m_proxysock)
			{
				Terminate ();
				return;
			}
			if (m_Target.host.size () > 255)
			{
				CancelUpstreamDeadline ();
				// Fixed reason: echoing the host would let a request reflect
				// content into a page.
				GenericProxyError (tr ("Hostname is too long"), tr ("The destination hostname is too long"),
					HTTP_PROXY_STATUS_BAD_REQUEST);
				return;
			}
			uint16_t port = m_Target.port;
			if (!port) port = 80;
			LogPrint (eLogDebug, "HTTPProxy: Connected to SOCKS upstream");
			std::string host = m_Target.host;
			auto s = shared_from_this ();
			// The handshake has no internal deadline of its own, so the
			// handler keeps the upstream deadline armed around it.
			ArmUpstreamDeadline ("handshake");
			i2p::transport::Socks5Handshake (*m_proxysock, std::make_pair (host, port),
				[s] (const boost::system::error_code & ec)
				{
					if (s->m_UpstreamAbandoned) return;
					if (!ec)
						s->SocksProxySuccess ();
					else
					{
						s->CancelUpstreamDeadline ();
						s->GenericProxyError (tr ("SOCKS proxy error"), ec.message (), HTTP_PROXY_STATUS_BAD_GATEWAY);
					}
				});
		}
		else
		{
			CancelUpstreamDeadline ();
			GenericProxyError (
				tr ("Cannot connect to upstream SOCKS proxy"), ec.message (), HTTP_PROXY_STATUS_BAD_GATEWAY);
		}
	}

	void HTTPReqHandler::HandoverToUpstreamProxy ()
	{
		LogPrint (eLogDebug, "HTTPProxy: Handover to SOCKS proxy");
		CancelUpstreamDeadline ();
		auto owner = GetOwner ();
		if (!owner)
		{
			Terminate ();
			return;
		}
		if (!m_proxysock || !m_sock)
		{
			LogPrint (eLogError, "HTTPProxy: Cannot hand over, a socket is already closed");
			GenericProxyError (
				tr ("SOCKS proxy error"), tr ("Connection closed during handover"), HTTP_PROXY_STATUS_BAD_GATEWAY);
			return;
		}
		auto connection = CreateSocketsPipe (owner, m_proxysock, m_sock);
		m_sock = nullptr;
		m_proxysock = nullptr;
		owner->AddHandler (connection);
		connection->Start ();
		Terminate ();
	}

	void HTTPReqHandler::HTTPConnect (std::string_view host, uint16_t port)
	{
		LogPrint (eLogDebug, "HTTPProxy: CONNECT ", host, ":", port);
		if (!i2p::client::IsI2PHost (host) && IsBlockedClearnetTarget (host))
		{
			GenericProxyError (tr ("Blocked target"), tr ("Loopback and private clearnet targets are not allowed"),
				HTTP_PROXY_STATUS_FORBIDDEN);
			return;
		}
		auto owner = GetOwner ();
		if (i2p::client::IsI2PHost (host))
		{
			if (owner)
				owner->CreateStream (std::bind (&HTTPReqHandler::HandleHTTPConnectStreamRequestComplete,
										 shared_from_this (), std::placeholders::_1),
					host, port);
		}
		else
			ForwardToUpstreamProxy ();
	}

	void HTTPReqHandler::HandleHTTPConnectStreamRequestComplete (std::shared_ptr<i2p::stream::Stream> stream)
	{
		if (stream)
		{
			if (m_sock && m_sock->is_open ())
			{
				m_ClientResponse.code = 200;
				m_ClientResponse.status = "OK";
				m_send_buf = m_ClientResponse.to_string ();
				m_sock->send (boost::asio::buffer (m_send_buf));
				auto owner = GetOwner ();
				if (!owner)
				{
					stream->AsyncClose ();
					Terminate ();
					return;
				}
				auto connection = std::make_shared<i2p::client::I2PTunnelConnection> (owner, m_sock, stream);
				owner->AddHandler (connection);
				connection->I2PConnect ();
			}
			else
				stream->AsyncClose ();
			m_sock = nullptr;
			Terminate ();
		}
		else
		{
			GenericProxyError (tr ("CONNECT error"), tr ("Failed to connect"), HTTP_PROXY_STATUS_BAD_GATEWAY);
			// The error response keeps its own handler reference; release the
			// owner's accounting entry now instead of waiting for the write.
			Done (shared_from_this ());
		}
	}

	void HTTPReqHandler::SocksProxySuccess ()
	{
		// Capture a strong reference instead of binding [&]: the asynchronous
		// write may complete after this stack frame is gone.
		auto self = shared_from_this ();
		// The SOCKS negotiation is complete in both branches; only the local
		// write/handover remains and it is bounded by the socket itself.
		CancelUpstreamDeadline ();
		if (m_ClientRequest.method == "CONNECT")
		{
			if (!m_sock)
			{
				self->Terminate ();
				return;
			}
			m_ClientResponse.code = 200;
			m_send_buf = m_ClientResponse.to_string ();
			boost::asio::async_write (*m_sock, boost::asio::buffer (m_send_buf), boost::asio::transfer_all (),
				[self] (const boost::system::error_code & ec, std::size_t)
				{
					if (ec)
						self->GenericProxyError (
							tr ("SOCKS proxy error"), ec.message (), HTTP_PROXY_STATUS_BAD_GATEWAY);
					else
						self->HandoverToUpstreamProxy ();
				});
		}
		else
		{
			if (!m_proxysock)
			{
				self->Terminate ();
				return;
			}
			m_send_buf = m_ClientRequestBuffer.str ();
			LogPrint (eLogDebug, "HTTPProxy: Send ", m_send_buf.size (), " bytes");
			boost::asio::async_write (*m_proxysock, boost::asio::buffer (m_send_buf), boost::asio::transfer_all (),
				[self] (const boost::system::error_code & ec, std::size_t)
				{
					if (ec)
						self->GenericProxyError (
							tr ("Failed to send request to upstream"), ec.message (), HTTP_PROXY_STATUS_BAD_GATEWAY);
					else
						self->HandoverToUpstreamProxy ();
				});
		}
	}

	void HTTPReqHandler::HandleUpstreamHTTPProxyConnect (const boost::system::error_code & ec)
	{
		if (m_UpstreamAbandoned) return;
		CancelUpstreamDeadline ();
		if (!ec)
		{
			LogPrint (eLogDebug, "HTTPProxy: Connected to http upstream");
			GenericProxyError (
				tr ("Cannot connect"), tr ("HTTP out proxy not implemented"), HTTP_PROXY_STATUS_BAD_GATEWAY);
		}
		else
			GenericProxyError (
				tr ("Cannot connect to upstream HTTP proxy"), ec.message (), HTTP_PROXY_STATUS_BAD_GATEWAY);
	}

	/* will be called after some data received from client */
	void HTTPReqHandler::HandleSockRecv (const boost::system::error_code & ecode, std::size_t len)
	{
		LogPrint (eLogDebug, "HTTPProxy: Sock recv: ", len, " bytes, recv buf: ", m_recv_buf.length (),
			", send buf: ", m_send_buf.length ());
		if (ecode)
		{
			LogPrint (eLogWarning, "HTTPProxy: Sock recv got error: ", ecode);
			Terminate ();
			return;
		}

		// Invariant: a previous HandleRequest() that saw the end of headers
		// either consumed the request (m_recv_buf cleared) or stopped reading.
		// Whenever this callback runs, m_recv_buf therefore holds at most an
		// incomplete header, and the ceiling is checked before every append.
		if (HTTPProxyHeaderLimitExceeded (m_recv_buf.size (), len))
		{
			LogPrint (eLogWarning, "HTTPProxy: Request header exceeds ", HTTP_PROXY_MAX_HEADER_SIZE,
				" bytes, dropping connection");
			CleanseString (m_recv_buf);
			GenericProxyError (
				tr ("Request too large"), tr ("HTTP request header is too large"), HTTP_PROXY_STATUS_HEADER_TOO_LARGE);
			return;
		}

		m_recv_buf.append (reinterpret_cast<const char *> (m_recv_chunk), len);
		if (HandleRequest ())
		{
			CleanseString (m_recv_buf);
			return;
		}
		AsyncSockRead ();
	}

	void HTTPReqHandler::SentHTTPFailed (const boost::system::error_code & ecode)
	{
		if (ecode) LogPrint (eLogError, "HTTPProxy: Closing socket after sending failure because: ", ecode.message ());
		Terminate ();
	}

	/**
	 * A proxied connection towards an I2P destination.
	 *
	 * The request direction keeps the existing ClientRequest normalisation.
	 * The response direction is parsed with Mode::ClientResponse, so
	 * hop-by-hop and proxy-only headers from the destination never reach the
	 * browser, and the first response header must arrive within
	 * HTTP_PROXY_RESPONSE_HEADER_TIMEOUT; an expiry answers 504 and closes.
	 * `I2PClientTunnelConnectionHTTP` cannot be reused because it applies the
	 * *request* transform to the response direction.
	 */
	class HTTPProxyClientConnection final : public i2p::client::I2PTunnelConnection
	{
	public:
		HTTPProxyClientConnection (std::shared_ptr<i2p::client::I2PService> owner,
			std::shared_ptr<boost::asio::ip::tcp::socket> socket, std::shared_ptr<i2p::stream::Stream> stream)
			: I2PTunnelConnection (owner, std::move (socket), std::move (stream))
			, m_ResponseTimer (owner->GetService ())
		{
		}

		~HTTPProxyClientConnection () { m_ResponseTimer.cancel (); }

		/** Arm the response-header deadline, then start the connection. */
		void Start (const uint8_t * msg, size_t len)
		{
			ArmResponseDeadline ();
			I2PConnect (msg, len);
		}

	protected:
		void Write (const uint8_t * buf, size_t len) override
		{
			if (m_ResponseHeader.State () == I2PTunnelHTTPHeader::Result::Complete)
			{
				I2PTunnelConnection::Write (buf, len);
				return;
			}
			switch (m_ResponseHeader.Feed ({ reinterpret_cast<const char *> (buf), len }))
			{
			case I2PTunnelHTTPHeader::Result::Complete:
			{
				m_ResponseTimer.cancel ();
				const auto & output = m_ResponseHeader.Output ();
				I2PTunnelConnection::Write (reinterpret_cast<const uint8_t *> (output.data ()), output.size ());
				break;
			}
			case I2PTunnelHTTPHeader::Result::Incomplete: StreamReceive (); break;
			case I2PTunnelHTTPHeader::Result::Rejected:
				// Fail closed: a header the sanitizer cannot parse must not be
				// forwarded verbatim to the client.
				LogPrint (eLogError, "HTTPProxy: response header exceeds bounds");
				Terminate ();
				break;
			}
		}

	private:
		void ArmResponseDeadline ()
		{
			auto weak = weak_from_this ();
			m_ResponseTimer.expires_after (std::chrono::seconds (HTTP_PROXY_RESPONSE_HEADER_TIMEOUT));
			m_ResponseTimer.async_wait (
				[weak] (const boost::system::error_code & ecode)
				{
					if (ecode) return;
					auto self = std::static_pointer_cast<HTTPProxyClientConnection> (weak.lock ());
					if (!self) return;
					self->HandleResponseDeadline ();
				});
		}

		void HandleResponseDeadline ()
		{
			LogPrint (eLogWarning, "HTTPProxy: destination response header exceeded ",
				HTTP_PROXY_RESPONSE_HEADER_TIMEOUT, " seconds");
			auto socket = GetSocket ();
			auto stream = GetStream ();
			if (stream) stream->AsyncClose ();
			if (!socket) return;
			// Fixed page: no client or destination input is echoed.
			i2p::http::HTTPRes res;
			res.code = HTTP_PROXY_STATUS_GATEWAY_TIMEOUT;
			res.status = "Gateway Timeout";
			res.add_header ("Content-Type", "text/html; charset=UTF-8");
			res.add_header ("Connection", "close");
			std::stringstream ss;
			ss << "<html><body><h1>" << EscapeHTML (tr ("Upstream timeout")) << "</h1><p>"
			   << EscapeHTML (tr ("The destination did not answer in time")) << "</p></body></html>\r\n";
			res.body = ss.str ();
			auto response = std::make_shared<std::string> (res.to_string ());
			auto self = std::static_pointer_cast<HTTPProxyClientConnection> (shared_from_this ());
			boost::asio::async_write (*socket, boost::asio::buffer (*response), boost::asio::transfer_all (),
				[self, response] (const boost::system::error_code &, std::size_t) { self->Terminate (); });
		}

		I2PTunnelHTTPHeader m_ResponseHeader { I2PTunnelHTTPHeader::Mode::ClientResponse };
		boost::asio::steady_timer m_ResponseTimer;
	};

	void HTTPReqHandler::HandleStreamRequestComplete (std::shared_ptr<i2p::stream::Stream> stream)
	{
		// Any upstream phase (an I2P outproxy stream included) ends here.
		CancelUpstreamDeadline ();
		if (!stream)
		{
			LogPrint (
				eLogError, "HTTPProxy: Error when creating the stream, check the previous warnings for more info");
			if (Kill ())
			{
				// Another completion already killed the handler (the owner
				// either removed it or is clearing its set). Still close the
				// sockets instead of depending on an error write that may
				// never run for an already-gone client.
				CloseSockets ();
				return;
			}
			GenericProxyError (tr ("Host is down"),
				tr ("Can't create connection to requested host, it may be down. Please try again later."),
				HTTP_PROXY_STATUS_GATEWAY_TIMEOUT);
			// The error response keeps its own reference to this handler.
			// Release the owner's reference now so request accounting does
			// not depend on the async write ever completing.
			Done (shared_from_this ());
			return;
		}
		if (Kill ())
		{
			stream->AsyncClose ();
			CloseSockets ();
			return;
		}
		LogPrint (eLogDebug, "HTTPProxy: Created new I2PTunnel stream, sSID=", stream->GetSendStreamID (),
			", rSID=", stream->GetRecvStreamID ());
		auto owner = GetOwner ();
		if (!owner)
		{
			stream->AsyncClose ();
			CloseSockets ();
			Done (shared_from_this ());
			return;
		}
		if (!m_sock)
		{
			stream->AsyncClose ();
			CloseSockets ();
			Done (shared_from_this ());
			return;
		}
		auto connection = std::make_shared<HTTPProxyClientConnection> (owner, m_sock, stream);
		owner->AddHandler (connection);
		connection->Start (reinterpret_cast<const uint8_t *> (m_send_buf.data ()), m_send_buf.length ());
		// Ownership of the client socket is handed to the tunnel connection.
		// Do not close it from this handler's destructor.
		m_sock = nullptr;
		Done (shared_from_this ());
	}

	HTTPProxy::HTTPProxy (const std::string & name, const std::string & address, uint16_t port,
		const std::string & outproxy, bool addresshelper, bool senduseragent,
		std::shared_ptr<i2p::client::ClientDestination> localDestination)
		: TCPIPAcceptor (
			  address, port, localDestination ? localDestination : i2p::client::context.GetSharedLocalDestination ())
		, m_Name (name)
		, m_NextOutproxy (0)
		, m_Addresshelper (addresshelper)
		, m_SendUserAgent (senduseragent)
	{
		// outproxy is a comma separated list, the same way a client tunnel takes several destinations
		boost::split (m_OutproxyUrls, outproxy, boost::is_any_of (","), boost::token_compress_on);
		for (auto & it : m_OutproxyUrls) boost::trim (it);
		m_OutproxyUrls.erase (std::remove (m_OutproxyUrls.begin (), m_OutproxyUrls.end (), ""), m_OutproxyUrls.end ());

		// Local HTTP proxy authentication. Keys (see libi2pd/Config.cpp):
		//   httpproxy.auth.enabled     - require credentials on this listener
		//   httpproxy.auth.type        - `basic` (default) or `digest`
		//   httpproxy.auth.username    - accepted username, 1..255 bytes
		//   httpproxy.auth.password    - accepted password, 1..255 bytes
		//   httpproxy.auth.realm       - Digest realm, default "openi2pd"
		//   httpproxy.auth.failuredelay- non-blocking delay before a 407, ms
		// An enabled listener with an empty or oversized field, an unknown
		// scheme or an invalid realm denies every request (fail closed) instead
		// of exposing an open proxy.
		bool authEnabled = false;
		i2p::config::GetOption ("httpproxy.auth.enabled", authEnabled);
		std::string authType = "basic";
		std::string authUsername;
		std::string authPassword;
		std::string authRealm = std::string (HTTP_PROXY_AUTH_REALM);
		std::string authFailureDelay = "0";
		i2p::config::GetOption ("httpproxy.auth.type", authType);
		i2p::config::GetOption ("httpproxy.auth.username", authUsername);
		i2p::config::GetOption ("httpproxy.auth.password", authPassword);
		i2p::config::GetOption ("httpproxy.auth.realm", authRealm);
		i2p::config::GetOption ("httpproxy.auth.failuredelay", authFailureDelay);
		m_Auth.enabled = authEnabled;
		m_Auth.username = std::move (authUsername);
		m_Auth.password = std::move (authPassword);
		m_AuthRealm = std::move (authRealm);

		// A failure delay is bounded and parsed strictly; a malformed value
		// never disables authentication, it only disables the delay.
		{
			const auto trimmed = boost::trim_copy (authFailureDelay);
			std::uint32_t milliseconds = 0;
			const auto parsed = std::from_chars (trimmed.data (), trimmed.data () + trimmed.size (), milliseconds);
			if (!trimmed.empty () && parsed.ec == std::errc () && parsed.ptr == trimmed.data () + trimmed.size ()
				&& milliseconds <= HTTP_PROXY_AUTH_MAX_FAILURE_DELAY_MS)
				m_AuthFailureDelayMs = milliseconds;
			else if (trimmed != "0")
				LogPrint (eLogCritical, "HTTPProxy: httpproxy.auth.failuredelay is invalid (expected 0..",
					HTTP_PROXY_AUTH_MAX_FAILURE_DELAY_MS, " milliseconds); using 0");
		}

		if (!authEnabled)
		{
			m_AuthScheme = ProxyAuthScheme::None;
			return;
		}

		if (boost::iequals (authType, "digest"))
		{
			auto digest = std::make_shared<ProxyDigestAuth> ();
			digest->enabled = true;
			digest->username = m_Auth.username;
			digest->password = m_Auth.password;
			digest->realm = m_AuthRealm;
			if (!digest->Prepare ())
			{
				// Fail closed: every request is rejected instead of falling
				// back to an unauthenticated proxy. The realm or a credential
				// is unusable, which is a configuration error.
				LogPrint (eLogCritical,
					"HTTPProxy: httpproxy.auth.type=digest is set but "
					"httpproxy.auth.username/password/realm are unusable; every request will be "
					"rejected");
			}
			m_DigestAuth = std::move (digest);
			m_AuthScheme = ProxyAuthScheme::Digest;
			LogPrint (eLogInfo, "HTTPProxy: Local Digest authentication enabled for ", m_Name);
			return;
		}

		if (!boost::iequals (authType, "basic"))
		{
			// Unknown scheme: deny everything, never fall back to an open
			// proxy. This matches I2P Java's AUTH_BAD_REQ for an unknown
			// `proxyAuth` value.
			m_AuthScheme = ProxyAuthScheme::Digest;
			m_DigestAuth = nullptr;
			LogPrint (eLogCritical,
				"HTTPProxy: httpproxy.auth.type is not basic or digest; every request will be "
				"rejected");
			return;
		}

		m_AuthScheme = ProxyAuthScheme::Basic;
		if (m_Auth.HasUsableCredentials ())
			LogPrint (eLogInfo, "HTTPProxy: Local Basic authentication enabled for ", m_Name);
		else
			LogPrint (eLogCritical,
				"HTTPProxy: httpproxy.auth.enabled is set but httpproxy.auth.username/password are empty "
				"or longer than ",
				PROXY_AUTH_MAX_FIELD_SIZE, " bytes; every request will be rejected");
	}

	void HTTPProxy::SetBasicAuth (std::string username, std::string password)
	{
		m_Auth.enabled = true;
		m_Auth.username = std::move (username);
		m_Auth.password = std::move (password);
		// An explicit programmatic credential selects Basic; a Digest
		// configuration is installed only through the constructor.
		m_AuthScheme = ProxyAuthScheme::Basic;
		if (!m_Auth.HasUsableCredentials ())
			LogPrint (eLogCritical,
				"HTTPProxy: Local authentication configured without usable credentials; every request will be "
				"rejected");
	}

	std::string HTTPProxy::GetOutproxyURL ()
	{
		if (m_OutproxyUrls.empty ()) return "";
		auto url = m_OutproxyUrls[m_NextOutproxy];
		m_NextOutproxy = (m_NextOutproxy + 1) % m_OutproxyUrls.size ();
		return url;
	}

	std::shared_ptr<i2p::client::I2PServiceHandler> HTTPProxy::CreateHandler (
		std::shared_ptr<boost::asio::ip::tcp::socket> socket)
	{
		return std::make_shared<HTTPReqHandler> (shared_from_this (), socket);
	}
} // http
} // i2p
