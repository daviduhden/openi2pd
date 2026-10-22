/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: HTTPProxy.
 *
 * Implements an HTTP forward proxy that resolves .i2p hosts through the
 * address book, handles addresshelper jumps, sanitizes proxy-revealing
 * headers and rotates between configured outproxies. CONNECT, upstream
 * HTTP/SOCKS proxying and request parsing use a bounded header buffer.
 *
 */

#include <algorithm>
#include <charconv>
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
		  "    @media (prefers-color-scheme: dark) { body { background: #0B141C; color: #E4EDF4; } h1 { color: #59D8D4; } .warning { color: #FFB4AB; } }\r\n"
		  "  </style>\r\n"
		  "</head>\r\n";

	static bool IsBlockedClearnetTarget (std::string_view host)
	{
		std::string value (host);
		if (value.size () >= 2 && value.front () == '[' && value.back () == ']')
			value = value.substr (1, value.size () - 2);
		if (boost::iequals (value, "localhost")
			|| i2p::client::HasSuffixIgnoreCase (value, ".localhost"))
			return true;
		boost::system::error_code ec;
		auto address = boost::asio::ip::make_address (value, ec);
		if (ec) return false;
		if (address.is_v4 ())
		{
			auto b = address.to_v4 ().to_bytes ();
			if (b[0] == 0 || b[0] == 10 || b[0] == 127
				|| (b[0] == 169 && b[1] == 254)
				|| (b[0] == 172 && b[1] >= 16 && b[1] <= 31)
				|| (b[0] == 192 && b[1] == 168) || address.to_v4 ().is_multicast ())
				return true;
		}
		else
		{
			auto b = address.to_v6 ().to_bytes ();
			if (address.to_v6 ().is_loopback () || address.to_v6 ().is_unspecified ()
				|| address.to_v6 ().is_multicast () || (b[0] == 0xfe && (b[1] & 0xc0) == 0x80)
				|| (b[0] & 0xfe) == 0xfc)
				return true;
		}
		return false;
	}

	static bool str_rmatch (std::string_view str, std::string_view suffix)
	{
		auto pos = str.rfind (suffix);
		if (pos == std::string_view::npos) return false; /* not found */
		if (str.length () == pos + suffix.length ()) return true; /* match */
		return false;
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
		void HandleStreamRequestComplete (std::shared_ptr<i2p::stream::Stream> stream);
		/* error helpers */
		void GenericProxyError (
			std::string_view title, std::string_view description, int code = HTTP_PROXY_STATUS_INTERNAL_ERROR);
		void GenericProxyInfo (std::string_view title, std::string_view description);
		void HostNotFound (std::string_view host);
		void SendProxyError (std::string_view content, int code = HTTP_PROXY_STATUS_INTERNAL_ERROR);
		void SendRedirect (const std::string & address);

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

		uint8_t m_recv_chunk[8192];
		std::string m_recv_buf; // from client
		std::string m_send_buf; // to upstream
		std::shared_ptr<boost::asio::ip::tcp::socket> m_sock;
		std::shared_ptr<boost::asio::ip::tcp::socket> m_proxysock;
		boost::asio::ip::tcp::resolver m_proxy_resolver;
		std::string m_OutproxyUrl, m_Response;
		bool m_Addresshelper, m_SendUserAgent;
		i2p::http::URL m_ProxyURL;
		i2p::http::URL m_RequestURL;
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
			, m_OutproxyUrl (std::static_pointer_cast<HTTPProxy> (parent)->GetOutproxyURL ())
			, m_Addresshelper (std::static_pointer_cast<HTTPProxy> (parent)->GetHelperSupport ())
			, m_SendUserAgent (std::static_pointer_cast<HTTPProxy> (parent)->GetSendUserAgent ())
		{
		}
		// A handler can time out and be destroyed by the owner. Done() must
		// not be called here because shared_from_this() is invalid once the
		// last reference is gone; closing the sockets is still required.
		~HTTPReqHandler () { CloseSockets (); }
		void Handle () { AsyncSockRead (); } /* overload */
	};

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
		// Idempotent: Kill() may already have been called by another callback,
		// in which case the owner's reference was already released (or the
		// owner is clearing its handler set). Sockets are always closed.
		const bool alreadyDead = Kill ();
		CloseSockets ();
		if (!alreadyDead) Done (shared_from_this ());
	}

	void HTTPReqHandler::GenericProxyError (
		std::string_view title, std::string_view description, int code)
	{
		std::stringstream ss;
		ss << "<h1>" << tr ("Proxy error") << ": " << title << "</h1>\r\n";
		ss << "<p>" << description << "</p>\r\n";
		SendProxyError (ss.str (), code);
	}

	void HTTPReqHandler::GenericProxyInfo (std::string_view title, std::string_view description)
	{
		std::stringstream ss;
		ss << "<h1>" << tr ("Proxy info") << ": " << title << "</h1>\r\n";
		ss << "<p>" << description << "</p>\r\n";
		SendProxyError (ss.str ());
	}

	void HTTPReqHandler::HostNotFound (std::string_view host)
	{
		std::stringstream ss;
		ss << "<h1>" << tr ("Proxy error: Host not found") << "</h1>\r\n"
		   << "<p>" << tr ("Remote host not found in router's addressbook") << "</p>\r\n"
		   << "<p>" << tr ("You may try to find this host on jump services below") << ":</p>\r\n"
		   << "<ul>\r\n";
		for (const auto & jump : jumporder)
		{
			auto js = jumpservices.find (jump);
			if (js != jumpservices.end ())
				ss << "  <li><a href=\"" << js->second << host << "\">" << js->first << "</a></li>\r\n";
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
		i2p::http::HTTPRes res;
		res.code = 302;
		res.add_header ("Location", address);
		res.add_header ("Connection", "close");
		m_Response = res.to_string ();
		boost::asio::async_write (*m_sock, boost::asio::buffer (m_Response), boost::asio::transfer_all (),
			std::bind (&HTTPReqHandler::SentHTTPFailed, shared_from_this (), std::placeholders::_1));
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
		auto h = req.GetHeader ("Connection");
		auto x = h.find ("pgrade");
		if (x == std::string::npos || !x || std::tolower (h[x - 1]) != 'u') req.UpdateHeader ("Connection", "close");
	}

	/**
	 * @brief Try to parse request from @a m_recv_buf
	 *   If parsing success, rebuild request and store to @a m_send_buf
	 * with remaining data tail
	 * @return true on processed request or false if more data needed
	 */
	bool HTTPReqHandler::HandleRequest ()
	{
		m_req_len = m_ClientRequest.parse (m_recv_buf);

		if (m_req_len == 0) return false; /* need more data */

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
			m_recv_buf.clear ();
			GenericProxyError (tr ("Request too large"), tr ("HTTP request header is too large"),
				HTTP_PROXY_STATUS_HEADER_TOO_LARGE);
			return true;
		}

		/* parsing success, now let's look inside request */
		LogPrint (eLogDebug, "HTTPProxy: Requested: ", m_ClientRequest.uri);
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
					if (m_Confirm) // Attempt to forced overwriting by link with "&update=true" from harmful URL
					{
						LogPrint (eLogWarning, "HTTPProxy: Address update from addresshelper rejected for ",
							m_RequestURL.host, " (referer is ", m_RequestURL.host.empty () ? "empty" : "harmful", ")");
						std::string full_url = m_RequestURL.to_string ();
						std::stringstream ss;
						ss << tr ("Host %s is <span class=\"warning\">already in router's addressbook</span>. <b>Be careful: "
								  "source of this URL may be harmful!</b> Click here to update record: <a "
								  "href=\"%s%s%s&update=true\">Continue</a>.",
							m_RequestURL.host.c_str (), full_url.c_str (),
							(full_url.find ('?') != std::string::npos ? "&i2paddresshelper=" : "?i2paddresshelper="),
							jump.c_str ());
						GenericProxyInfo (tr ("Addresshelper forced update rejected"), ss.str ());
					}
					else // Preventing unauthorized additions to the address book
					{
						LogPrint (eLogDebug, "HTTPProxy: Adding address from addresshelper for ", m_RequestURL.host,
							" (generate refer-base page)");
						std::string full_url = m_RequestURL.to_string ();
						std::stringstream ss;
						ss << tr ("To add host <b>%s</b> in router's addressbook, click here: <a "
								  "href=\"%s%s%s\">Continue</a>.",
							m_RequestURL.host.c_str (), full_url.c_str (),
							(full_url.find ('?') != std::string::npos ? "&i2paddresshelper=" : "?i2paddresshelper="),
							jump.c_str ());
						GenericProxyInfo (tr ("Addresshelper request"), ss.str ());
					}
					return true; /* request processed */
				}

				i2p::client::context.GetAddressBook ().InsertAddress (m_RequestURL.host, jump);
				LogPrint (eLogInfo, "HTTPProxy: Added address from addresshelper for ", m_RequestURL.host);
				std::string full_url = m_RequestURL.to_string ();
				std::stringstream ss;
				ss << tr ("Host %s added to router's addressbook from helper. Click here to proceed: <a "
						  "href=\"%s\">Continue</a>.",
					m_RequestURL.host.c_str (), full_url.c_str ());
				GenericProxyInfo (tr ("Addresshelper adding"), ss.str ());
				return true; /* request processed */
			}
			else
			{
				std::string full_url = m_RequestURL.to_string ();
				std::stringstream ss;
				ss << tr ("Host %s is <span class=\"warning\">already in router's addressbook</span>. Click here to update "
						  "record: <a href=\"%s%s%s&update=true\">Continue</a>.",
					m_RequestURL.host.c_str (), full_url.c_str (),
					(full_url.find ('?') != std::string::npos ? "&i2paddresshelper=" : "?i2paddresshelper="),
					jump.c_str ());
				GenericProxyInfo (tr ("Addresshelper update"), ss.str ());
				return true; /* request processed */
			}
		}
		std::string dest_host;
		uint16_t dest_port;
		bool useConnect = false;
		if (m_ClientRequest.method == "CONNECT")
		{
			const std::string & uri = m_ClientRequest.uri;
			auto pos = uri.find (":");
			if (pos == std::string::npos || pos == uri.size () - 1)
			{
				GenericProxyError (tr ("Invalid request"), tr ("Invalid request URI"), HTTP_PROXY_STATUS_BAD_REQUEST);
				return true;
			}
			else
			{
				useConnect = true;
				// stoi throws on a port out of int range and nothing catches it here
				std::string_view portStr = std::string_view (uri).substr (pos + 1);
				auto res = std::from_chars (portStr.data (), portStr.data () + portStr.size (), dest_port);
				if (res.ec != std::errc () || res.ptr != portStr.data () + portStr.size ())
				{
					GenericProxyError (tr ("Invalid request"), tr ("Invalid request URI"), HTTP_PROXY_STATUS_BAD_REQUEST);
					return true;
				}
				dest_host = uri.substr (0, pos);
			}
		}
		else
		{
			SanitizeHTTPRequest (m_ClientRequest);

			dest_host = m_RequestURL.host;
			dest_port = m_RequestURL.port;
			/* always set port, even if missing in request */
			if (!dest_port) dest_port = (m_RequestURL.schema == "https") ? 443 : 80;
			/* detect dest_host, set proper 'Host' header in upstream request */
			if (dest_host != "")
			{
				/* absolute url, replace 'Host' header */
				std::string h (dest_host);
				if (dest_port != 0 && dest_port != 80) h += ":" + std::to_string (dest_port);
				m_ClientRequest.UpdateHeader ("Host", h);
			}
			else
			{
				auto h = m_ClientRequest.GetHeader ("Host");
				if (h.length () > 0)
				{
					/* relative url and 'Host' header provided. transparent proxy mode? */
					i2p::http::URL u;
					std::string t = "http://" + h;
					u.parse (t);
					dest_host = u.host;
					dest_port = u.port;
				}
				else
				{
					/* relative url and missing 'Host' header */
					GenericProxyError (tr ("Invalid request"), tr ("Can't detect destination host from request"),
						HTTP_PROXY_STATUS_BAD_REQUEST);
					return true;
				}
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
				LogPrint (eLogDebug, "HTTPProxy: Using outproxy ", m_OutproxyUrl);
				if (m_ProxyURL.parse (m_OutproxyUrl))
					ForwardToUpstreamProxy ();
				else
					GenericProxyError (tr ("Outproxy failure"), tr ("Bad outproxy settings"), HTTP_PROXY_STATUS_BAD_GATEWAY);
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
					owner->CreateStream (std::bind (&HTTPReqHandler::HandleStreamRequestComplete, shared_from_this (),
											 std::placeholders::_1),
						m_ProxyURL.host, m_ProxyURL.port);
			}
			else
			{
				auto self = shared_from_this ();
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
			/* unknown type, complain */
			GenericProxyError (tr ("Unknown outproxy URL"), m_ProxyURL.to_string (), HTTP_PROXY_STATUS_BAD_GATEWAY);
		}
	}

	void HTTPReqHandler::HandleUpstreamProxyResolved (const boost::system::error_code & ec,
		boost::asio::ip::tcp::resolver::results_type endpoints, ProxyResolvedHandler handler)
	{
		if (ec)
			GenericProxyError (tr ("Cannot resolve upstream proxy"), ec.message (), HTTP_PROXY_STATUS_BAD_GATEWAY);
		else if (endpoints.begin () != endpoints.end () && handler)
			handler (*endpoints.begin ());
	}

	void HTTPReqHandler::HandleUpstreamSocksProxyConnect (const boost::system::error_code & ec)
	{
		if (!ec)
		{
			if (!m_proxysock)
			{
				Terminate ();
				return;
			}
			if (m_RequestURL.host.size () > 255)
			{
				GenericProxyError (tr ("Hostname is too long"), m_RequestURL.host, HTTP_PROXY_STATUS_BAD_REQUEST);
				return;
			}
			uint16_t port = m_RequestURL.port;
			if (!port) port = 80;
			LogPrint (eLogDebug, "HTTPProxy: Connected to SOCKS upstream");
			std::string host = m_RequestURL.host;
			auto s = shared_from_this ();
			i2p::transport::Socks5Handshake (*m_proxysock, std::make_pair (host, port),
				[s] (const boost::system::error_code & ec)
				{
					if (!ec)
						s->SocksProxySuccess ();
					else
						s->GenericProxyError (tr ("SOCKS proxy error"), ec.message (), HTTP_PROXY_STATUS_BAD_GATEWAY);
				});
		}
		else
			GenericProxyError (
				tr ("Cannot connect to upstream SOCKS proxy"), ec.message (), HTTP_PROXY_STATUS_BAD_GATEWAY);
	}

	void HTTPReqHandler::HandoverToUpstreamProxy ()
	{
		LogPrint (eLogDebug, "HTTPProxy: Handover to SOCKS proxy");
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
			m_recv_buf.clear ();
			GenericProxyError (tr ("Request too large"), tr ("HTTP request header is too large"),
				HTTP_PROXY_STATUS_HEADER_TOO_LARGE);
			return;
		}

		m_recv_buf.append (reinterpret_cast<const char *> (m_recv_chunk), len);
		if (HandleRequest ())
		{
			m_recv_buf.clear ();
			return;
		}
		AsyncSockRead ();
	}

	void HTTPReqHandler::SentHTTPFailed (const boost::system::error_code & ecode)
	{
		if (ecode) LogPrint (eLogError, "HTTPProxy: Closing socket after sending failure because: ", ecode.message ());
		Terminate ();
	}

	void HTTPReqHandler::HandleStreamRequestComplete (std::shared_ptr<i2p::stream::Stream> stream)
	{
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
		auto connection = std::make_shared<i2p::client::I2PClientTunnelConnectionHTTP> (owner, m_sock, stream);
		owner->AddHandler (connection);
		connection->I2PConnect (reinterpret_cast<const uint8_t *> (m_send_buf.data ()), m_send_buf.length ());
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
