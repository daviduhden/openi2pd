/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: HTTP.
 *
 * Implements HTTP/1.0 and 1.1 request/response parsing and serialisation with
 * strict token, header and version validation, URL and query-string handling,
 * percent encoding, chunked-response merging, gzip detection and Basic
 * authentication. Content lengths are capped before allocation, and malformed
 * headers or body framing are rejected.
 *
 */

#include <algorithm>
#include <optional>
#include <utility>
#include <stdio.h>
#include <ctime>
#include <charconv>
#include <boost/beast/core/detail/base64.hpp>
#include <boost/algorithm/string.hpp>
#include "util.h"
#include "Base.h"
#include "HTTP.h"

namespace i2p
{
namespace http
{
	// list of valid HTTP methods
	static constexpr std::array<std::string_view, 16> HTTP_METHODS = {
		"GET", "HEAD", "POST", "PUT", "PATCH", "DELETE", "OPTIONS", "CONNECT", // HTTP basic methods
		"COPY", "LOCK", "MKCOL", "MOVE", "PROPFIND", "PROPPATCH", "UNLOCK",
		"SEARCH" // WebDAV methods, for SEARCH see rfc5323
	};

	// list of valid HTTP versions
	static constexpr std::array<std::string_view, 2> HTTP_VERSIONS = { "HTTP/1.0", "HTTP/1.1" };

	static constexpr std::array<const char *, 7> weekdays = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };

	static constexpr std::array<const char *, 12> months
		= { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

	static inline bool is_http_version (std::string_view str)
	{
		return std::find (HTTP_VERSIONS.begin (), HTTP_VERSIONS.end (), str) != std::end (HTTP_VERSIONS);
	}

	static inline bool is_http_method (std::string_view str)
	{
		return std::find (HTTP_METHODS.begin (), HTTP_METHODS.end (), str) != std::end (HTTP_METHODS);
	}

	static void strsplit (
		std::string_view line, std::vector<std::string_view> & tokens, char delim, std::size_t limit = 0)
	{
		size_t count = 1, pos;
		while ((pos = line.find (delim)) != line.npos)
		{
			count++;
			if (limit > 0 && count >= limit) delim = '\n'; // reset delimiter
			tokens.push_back (line.substr (0, pos));
			line = line.substr (pos + 1);
		}
		if (!line.empty ()) tokens.push_back (line);
	}

	static bool is_header_name (std::string_view name)
	{
		// RFC 9110 token: no controls, separators or whitespace.
		static constexpr std::string_view separators = "()<>@,;:\\\"/[]?={} \t";
		if (name.empty ()) return false;
		for (unsigned char ch : name)
		{
			if (ch <= 0x20 || ch >= 0x7f) return false;
			if (separators.find (static_cast<char> (ch)) != std::string_view::npos) return false;
		}
		return true;
	}

	static std::optional<std::pair<std::string, std::string>> parse_header_line (std::string_view line)
	{
		const auto pos = line.find (':');
		if (pos == std::string_view::npos) return std::nullopt;

		const auto name = line.substr (0, pos);
		if (!is_header_name (name)) return std::nullopt;

		// RFC 9110 allows zero or more optional whitespace characters after
		// the colon. Requiring a space here rejected valid peers such as
		// "Host:example.i2p".
		auto value = line.substr (pos + 1);
		while (!value.empty () && (value.front () == ' ' || value.front () == '\t')) value.remove_prefix (1);
		while (!value.empty () && (value.back () == ' ' || value.back () == '\t')) value.remove_suffix (1);
		return std::pair { std::string (name), std::string (value) };
	}

	void gen_rfc7231_date (std::string & out)
	{
		std::time_t now = std::time (nullptr);
		struct tm tm;
#ifdef _WIN32
		if (gmtime_s (&tm, &now) != 0)
		{
			out.clear ();
			return;
		}
#else
		if (!gmtime_r (&now, &tm))
		{
			out.clear ();
			return;
		}
#endif
		char buf[128];
		snprintf (buf, sizeof (buf), "%s, %02d %s %d %02d:%02d:%02d GMT", weekdays[tm.tm_wday], tm.tm_mday,
			months[tm.tm_mon], tm.tm_year + 1900, tm.tm_hour, tm.tm_min, tm.tm_sec);
		out = buf;
	}

	bool URL::parse (const char * str, std::size_t len)
	{
		return parse ({ str, len ? len : strlen (str) });
	}

	bool URL::parse (std::string_view url)
	{
		if (url.empty ()) return false;
		std::size_t pos_p = 0; /* < current parse position */
		std::size_t pos_c = 0; /* < work position */
		if (url.at (0) != '/' || pos_p > 0)
		{
			std::size_t pos_s = 0;

			/* schema */
			pos_c = url.find ("://");
			if (pos_c != std::string::npos)
			{
				schema = url.substr (0, pos_c);
				pos_p = pos_c + 3;
			}

			/* user[:pass] */
			pos_s = url.find ('/', pos_p); /* find first slash */
			pos_c = url.find ('@', pos_p); /* find end of 'user' or 'user:pass' part */

			if (pos_c != std::string::npos && (pos_s == std::string::npos || pos_s > pos_c))
			{
				std::size_t delim = url.find (':', pos_p);
				if (delim && delim != std::string::npos && delim < pos_c)
				{
					user = url.substr (pos_p, delim - pos_p);
					delim += 1;
					pass = url.substr (delim, pos_c - delim);
				}
				else if (delim)
				{
					user = url.substr (pos_p, pos_c - pos_p);
				}
				pos_p = pos_c + 1;
			}

			/* hostname[:port][/path] */
			if (pos_p >= url.length ()) return false; /* nothing left after schema or user part */
			if (url.at (pos_p) == '[') // ipv6
			{
				auto pos_b = url.find (']', pos_p);
				if (pos_b == std::string::npos) return false;
				ipv6 = true;
				pos_c = url.find_first_of (":/", pos_b);
			}
			else
				pos_c = url.find_first_of (":/", pos_p);

			if (pos_c == std::string::npos)
			{
				/* only hostname, without post and path */
				host = ipv6 ? url.substr (pos_p + 1, url.length () - 1) : url.substr (pos_p, std::string::npos);
				return true;
			}
			else if (url.at (pos_c) == ':')
			{
				host = ipv6 ? url.substr (pos_p + 1, pos_c - pos_p - 2) : url.substr (pos_p, pos_c - pos_p);
				/* port[/path] */
				pos_p = pos_c + 1;
				pos_c = url.find ('/', pos_p);
				std::string_view port_str = (pos_c == std::string::npos) ? url.substr (pos_p, std::string::npos)
																		 : url.substr (pos_p, pos_c - pos_p);

				port = 0;
				auto res = std::from_chars (port_str.data (), port_str.data () + port_str.size (), port);
				if (res.ec != std::errc () || res.ptr != port_str.data () + port_str.size () || port == 0) return false;

				if (pos_c == std::string::npos) return true; /* no path part */
				pos_p = pos_c;
			}
			else
			{
				/* start of path part found */
				host = ipv6 ? url.substr (pos_p + 1, pos_c - pos_p - 2) : url.substr (pos_p, pos_c - pos_p);
				pos_p = pos_c;
			}
		}

		/* pos_p now at start of path part */
		pos_c = url.find_first_of ("?#", pos_p);
		if (pos_c == std::string::npos)
		{
			/* only path, without fragment and query */
			path = url.substr (pos_p, std::string::npos);
			return true;
		}
		else if (url.at (pos_c) == '?')
		{
			/* found query part */
			hasquery = true;
			path = url.substr (pos_p, pos_c - pos_p);
			pos_p = pos_c + 1;
			pos_c = url.find ('#', pos_p);
			if (pos_c == std::string::npos)
			{
				/* no fragment */
				query = url.substr (pos_p, std::string::npos);
				return true;
			}
			else
			{
				query = url.substr (pos_p, pos_c - pos_p);
				pos_p = pos_c + 1;
			}
		}
		else
		{
			/* found fragment part */
			path = url.substr (pos_p, pos_c - pos_p);
			pos_p = pos_c + 1;
		}

		/* pos_p now at start of fragment part */
		frag = url.substr (pos_p, std::string::npos);
		return true;
	}

	bool URL::parse_query (std::map<std::string, std::string> & params) const
	{
		std::vector<std::string_view> tokens;
		strsplit (query, tokens, '&');

		params.clear ();
		for (const auto & it : tokens)
		{
			if (!it.length ()) // empty
				continue;
			std::size_t eq = it.find ('=');
			if (eq != std::string::npos)
			{
				auto e = std::pair<std::string, std::string> (it.substr (0, eq), it.substr (eq + 1));
				params.insert (e);
			}
			else
			{
				auto e = std::pair<std::string, std::string> (it, "");
				params.insert (e);
			}
		}
		return true;
	}

	void URL::create_query (const std::map<std::string, std::string> & params)
	{
		if (params.empty ()) return;
		hasquery = true;
		std::stringstream s;
		bool isFirst = true;
		for (const auto & [param, value] : params)
		{
			if (isFirst)
				isFirst = false;
			else
				s << '&';
			s << param << '=' << value;
		}
		query = s.str ();
	}

	std::string URL::to_string (bool requestTargetOnly) const
	{
		std::string out = "";
		if (!requestTargetOnly)
		{
			if (schema != "")
			{
				out = schema + "://";
				if (user != "" && pass != "")
				{
					out += user + ":" + pass + "@";
				}
				else if (user != "")
				{
					out += user + "@";
				}
				if (ipv6)
				{
					if (port)
					{
						out += "[" + host + "]:" + std::to_string (port);
					}
					else
					{
						out += "[" + host + "]";
					}
				}
				else
				{
					if (port)
					{
						out += host + ":" + std::to_string (port);
					}
					else
					{
						out += host;
					}
				}
			}
		}
		out += path;
		if (out.empty ()) out = '/';
		if (hasquery) // add query even if it was empty
			out += "?";
		if (query != "") out += query;
		if (frag != "") out += "#" + frag;
		return out;
	}

	bool URL::is_i2p () const
	{
		return host.ends_with (".i2p");
	}

	void HTTPMsg::add_header (const char * name, const std::string & value, bool replace)
	{
		add_header (name, value.c_str (), replace);
	}

	void HTTPMsg::add_header (const char * name, const char * value, bool replace)
	{
		std::size_t count = headers.count (name);
		if (count && !replace) return;
		if (count)
		{
			headers[name] = value;
			return;
		}
		headers.insert (std::pair<std::string, std::string> (name, value));
	}

	void HTTPMsg::del_header (const char * name)
	{
		headers.erase (name);
	}

	std::string HTTPMsg::get_header (const std::string & name) const
	{
		auto it = headers.find (name);
		if (it == headers.end ())
			return "";
		else
			return it->second;
	}

	int HTTPReq::parse (const char * buf, size_t len)
	{
		return parse ({ buf, len });
	}

	int HTTPReq::parse (std::string_view str)
	{
		enum
		{
			REQ_LINE,
			HEADER_LINE
		} expect = REQ_LINE;
		std::size_t eoh = str.find (HTTP_EOH); /* request head size */
		std::size_t eol = 0, pos = 0;
		URL url;

		if (eoh == std::string::npos) return 0; /* str not contains complete request */

		while ((eol = str.find (CRLF, pos)) != std::string::npos)
		{
			if (expect == REQ_LINE)
			{
				std::string_view line = str.substr (pos, eol - pos);
				std::vector<std::string_view> tokens;
				strsplit (line, tokens, ' ');

				if (tokens.size () != 3) return -1;
				if (!is_http_method (tokens[0])) return -1;
				if (!is_http_version (tokens[2])) return -1;
				if (!url.parse (tokens[1])) return -1;
				/* all ok */
				method = tokens[0];
				uri = tokens[1];
				version = tokens[2];
				expect = HEADER_LINE;
			}
			else
			{
				std::string_view line = str.substr (pos, eol - pos);
				auto p = parse_header_line (line);
				if (!p) return -1;
				headers.push_back (std::move (*p));
			}
			pos = eol + CRLF.length ();
			if (pos >= eoh) break;
		}
		return static_cast<int> (eoh + HTTP_EOH.length ());
	}

	void HTTPReq::write (std::ostream & o) const
	{
		o << method << " " << uri << " " << version << CRLF;
		for (auto & h : headers) o << h.first << ": " << h.second << CRLF;
		o << CRLF;
	}

	std::string HTTPReq::to_string () const
	{
		std::stringstream ss;
		write (ss);
		return ss.str ();
	}

	void HTTPReq::AddHeader (const std::string & name, const std::string & value)
	{
		headers.push_back (std::make_pair (name, value));
	}

	void HTTPReq::UpdateHeader (const std::string & name, const std::string & value)
	{
		for (auto & it : headers)
			if (boost::iequals (it.first, name))
			{
				it.second = value;
				break;
			}
	}

	void HTTPReq::RemoveHeader (const std::string & name, const std::string & exempt)
	{
		for (auto it = headers.begin (); it != headers.end ();)
		{
			if (boost::istarts_with (it->first, name) && !boost::iequals (it->first, exempt))
				it = headers.erase (it);
			else
				it++;
		}
	}

	std::string HTTPReq::GetHeader (std::string_view name) const
	{
		for (auto & it : headers)
			if (boost::iequals (it.first, name)) return it.second;
		return "";
	}

	size_t HTTPReq::GetNumHeaders (std::string_view name) const
	{
		size_t num = 0;
		for (auto & it : headers)
			if (boost::iequals (it.first, name)) num++;
		return num;
	}

	bool HTTPRes::is_chunked () const
	{
		auto it = headers.find ("Transfer-Encoding");
		if (it == headers.end ()) return false;
		if (it->second.find ("chunked") != std::string::npos) return true;
		return false;
	}

	bool HTTPRes::is_gzipped (bool includingI2PGzip) const
	{
		auto it = headers.find ("Content-Encoding");
		if (it == headers.end ()) return false; /* no header */

		std::vector<std::string> encodingDirectives;
		boost::split (encodingDirectives, it->second, boost::is_any_of (","), boost::token_compress_on);
		for (auto & directive : encodingDirectives)
		{
			boost::algorithm::trim (directive);
			if (boost::iequals (directive, "gzip")) return true; /* gotcha! */
			if (includingI2PGzip && boost::iequals (directive, "x-i2p-gzip")) return true;
		}
		return false;
	}

	long int HTTPMsg::content_length () const
	{
		auto it = headers.find ("Content-Length");
		if (it == headers.end ()) return -1;

		// strtoul accepted signs, whitespace and trailing garbage, so a
		// malformed value could be interpreted as a different length by our
		// parser than by the next hop. Parse the complete decimal value or
		// reject it. The upper bound also keeps a hostile header from being
		// turned into a huge reservation by callers.
		static constexpr unsigned long MAX_CONTENT_LENGTH = 64UL * 1024 * 1024;
		unsigned long length = 0;
		const auto * begin = it->second.data ();
		const auto * end = begin + it->second.size ();
		auto result = std::from_chars (begin, end, length);
		if (result.ec != std::errc () || result.ptr != end || length > MAX_CONTENT_LENGTH) return -1;
		return static_cast<long int> (length);
	}

	int HTTPRes::parse (const char * buf, size_t len)
	{
		return parse ({ buf, len });
	}

	int HTTPRes::parse (std::string_view str)
	{
		enum
		{
			RES_LINE,
			HEADER_LINE
		} expect = RES_LINE;
		std::size_t eoh = str.find (HTTP_EOH); /* request head size */
		std::size_t eol = 0, pos = 0;

		if (eoh == std::string::npos) return 0; /* str not contains complete request */

		while ((eol = str.find (CRLF, pos)) != std::string::npos)
		{
			if (expect == RES_LINE)
			{
				std::string_view line = str.substr (pos, eol - pos);
				std::vector<std::string_view> tokens;
				strsplit (line, tokens, ' ', 3);
				if (tokens.size () != 3) return -1;
				if (!is_http_version (tokens[0])) return -1;
				auto res = std::from_chars (tokens[1].data (), tokens[1].data () + tokens[1].size (), code);
				if (res.ec != std::errc ()) return -1;
				if (code < 100 || code >= 600) return -1;
				/* all ok */
				version = tokens[0];
				status = tokens[2];
				expect = HEADER_LINE;
			}
			else
			{
				std::string_view line = str.substr (pos, eol - pos);
				auto p = parse_header_line (line);
				if (!p) return -1;
				headers.insert (std::move (*p));
			}
			pos = eol + CRLF.length ();
			if (pos >= eoh) break;
		}
		return static_cast<int> (eoh + HTTP_EOH.length ());
	}

	std::string HTTPRes::to_string ()
	{
		if (version == "HTTP/1.1" && headers.count ("Date") == 0)
		{
			std::string date;
			gen_rfc7231_date (date);
			add_header ("Date", date.c_str ());
		}
		if (status == "OK" && code != 200) status = HTTPCodeToStatus (code); // update
		if (body.length () > 0 && headers.count ("Content-Length") == 0)
			add_header ("Content-Length", std::to_string (body.length ()).c_str ());
		/* build response */
		std::stringstream ss;
		ss << version << " " << code << " " << status << CRLF;
		for (auto & h : headers)
		{
			ss << h.first << ": " << h.second << CRLF;
		}
		ss << CRLF;
		if (body.length () > 0) ss << body;
		return ss.str ();
	}

	std::string_view HTTPCodeToStatus (int code)
	{
		std::string_view ptr;
		switch (code)
		{
		case 105: ptr = "Name Not Resolved"; break;
		/* success */
		case 200: ptr = "OK"; break;
		case 206: ptr = "Partial Content"; break;
		/* redirect */
		case 301: ptr = "Moved Permanently"; break;
		case 302: ptr = "Found"; break;
		case 304: ptr = "Not Modified"; break;
		case 307: ptr = "Temporary Redirect"; break;
		/* client error */
		case 400: ptr = "Bad Request"; break;
		case 401: ptr = "Unauthorized"; break;
		case 403: ptr = "Forbidden"; break;
		case 404: ptr = "Not Found"; break;
		case 407: ptr = "Proxy Authentication Required"; break;
		case 408: ptr = "Request Timeout"; break;
		/* server error */
		case 500: ptr = "Internal Server Error"; break;
		case 502: ptr = "Bad Gateway"; break;
		case 503: ptr = "Not Implemented"; break;
		case 504: ptr = "Gateway Timeout"; break;
		default: ptr = "Unknown Status"; break;
		}
		return ptr;
	}

	std::string UrlDecode (std::string_view url, bool allow_null)
	{
		std::string decoded;
		decoded.reserve (url.length ());
		size_t start = 0;
		for (size_t i = 0; i < url.length (); i++)
		{
			auto c = url[i];
			if (c == '%')
			{
				decoded.append (url, start, i - start);
				if (i + 3 <= url.length ())
				{
					unsigned char ch;
					auto res = std::from_chars (url.data () + i + 1, url.data () + i + 3, ch, 16);
					if (res.ec == std::errc () && (ch || allow_null))
						decoded += ch;
					else
						decoded.append (url, i, 3);
					i += 2;
					start = i + 1;
				}
				else
					break;
			}
		}
		if (start < url.length ()) decoded.append (url, start);
		return decoded;
	}

	bool MergeChunkedResponse (std::istream & in, std::ostream & out)
	{
		static constexpr size_t MAX_CHUNK_SIZE = 10 * 1024 * 1024;
		std::string hexLen;
		std::vector<char> buf;
		while (!in.eof ())
		{
			if (!std::getline (in, hexLen)) return false;
			// Chunk extensions are separated by ';'.
			if (auto semicolon = hexLen.find (';'); semicolon != std::string::npos) hexLen.resize (semicolon);
			while (!hexLen.empty () && (hexLen.back () == '\r' || hexLen.back () == ' ' || hexLen.back () == '\t'))
				hexLen.pop_back ();
			if (hexLen.empty ()) return false;

			size_t len = 0;
			auto result = std::from_chars (hexLen.data (), hexLen.data () + hexLen.size (), len, 16);
			if (result.ec != std::errc () || result.ptr != hexLen.data () + hexLen.size ()) return false;
			if (!len) return true; // end of stream
			if (len > MAX_CHUNK_SIZE) return false; /* too large chunk */

			buf.resize (len);
			in.read (buf.data (), static_cast<std::streamsize> (len));
			if (in.gcount () != static_cast<std::streamsize> (len)) /* chunk is shorter than announced */
				return false;
			out.write (buf.data (), static_cast<std::streamsize> (len));
			if (!out) return false;
			std::getline (in, hexLen); // read \r\n after chunk
		}
		return true;
	}

	std::string CreateBasicAuthorizationString (const std::string & user, const std::string & pass)
	{
		if (user.empty () && pass.empty ()) return "";
		auto credentials = user + ":" + pass;
		std::string encoded (boost::beast::detail::base64::encoded_size (credentials.length ()), 0);
		encoded.resize (
			boost::beast::detail::base64::encode (encoded.data (), credentials.data (), credentials.length ()));
		return "Basic " + encoded;
	}

} // http
} // i2p
