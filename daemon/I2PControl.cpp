/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: I2PControl.
 *
 * Implements the asynchronous Boost.Asio TLS control service, accepting TCP
 * or UNIX stream connections and parsing bounded HTTP/JSON-RPC requests.
 * Passwords are compared in constant time, issued tokens expire after one
 * hour and every non-Authenticate method requires a valid token. A
 * self-signed certificate is generated when the configured files are missing
 * or unreadable.
 *
 */

#include <algorithm>
#include <cstring>
#include <ctime>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>
#include <sstream>
#include <iomanip>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>
#include <openssl/err.h>

#include <boost/property_tree/json_parser.hpp>
#include <boost/algorithm/string.hpp>

#include "FS.h"
#include "Base.h"
#include "Log.h"
#include "Config.h"
#include "NetDb.hpp"
#include "Tunnel.h"
#include "Daemon.h"
#include "I2PControl.h"

namespace
{
template <typename Writer>
bool WritePEMAtomically (std::string_view path, Writer && writer)
{
	std::unique_ptr<BIO, decltype (&BIO_free)> output (BIO_new (BIO_s_mem ()), BIO_free);
	if (!output || writer (output.get ()) != 1) return false;
	char * bytes = nullptr;
	const long length = BIO_get_mem_data (output.get (), &bytes);
	return length > 0 && bytes
		&& i2p::fs::AtomicWritePrivate (path, std::string_view (bytes, static_cast<size_t> (length)));
}

// Return the first queued OpenSSL error and drain the queue, so a later
// unrelated failure is not reported with a stale reason.
std::string GetOpenSSLError ()
{
	unsigned long error = ERR_get_error ();
	if (!error) return "unknown error";
	char buffer[256];
	ERR_error_string_n (error, buffer, sizeof (buffer));
	while (ERR_get_error () != 0)
	{
	}
	return buffer;
}

template <typename Socket>
void ArmControlDeadline (const std::shared_ptr<Socket> & socket,
	const std::shared_ptr<boost::asio::steady_timer> & deadline)
{
	deadline->expires_after (i2p::client::I2P_CONTROL_OPERATION_TIMEOUT);
	deadline->async_wait ([weakSocket = std::weak_ptr<Socket> (socket)] (const boost::system::error_code & ec)
		{
			if (ec) return;
			if (auto locked = weakSocket.lock ())
			{
				boost::system::error_code ignored;
				locked->lowest_layer ().close (ignored);
			}
		});
}
}

namespace i2p
{
namespace client
{
	I2PControlService::I2PControlService (const std::string & address, int port)
		: m_IsRunning (false)
		, m_SSLContext (boost::asio::ssl::context::tls_server)
		, m_ShutdownTimer (m_Service)
	{
		if (port)
			m_Acceptor = std::make_unique<boost::asio::ip::tcp::acceptor> (m_Service,
				boost::asio::ip::tcp::endpoint (
					boost::asio::ip::make_address (address), static_cast<boost::asio::ip::port_type> (port)));
		else
#if defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)
		{
			std::remove (address.c_str ()); // just in case
			m_LocalAcceptor = std::make_unique<boost::asio::local::stream_protocol::acceptor> (
				m_Service, boost::asio::local::stream_protocol::endpoint (address));
		}
#else
			LogPrint (eLogError, "I2PControl: Local sockets are not supported");
#endif

		i2p::config::GetOption ("i2pcontrol.password", m_Password);
		if (m_Password.empty ()) throw std::invalid_argument ("I2PControl password must not be empty");
		if (port && m_Password == I2P_CONTROL_DEFAULT_PASSWORD)
		{
			auto listenAddress = boost::asio::ip::make_address (address);
			if (!listenAddress.is_loopback ())
				throw std::invalid_argument (
					"I2PControl refuses a non-loopback listener with the default password");
		}

		// certificate / keys
		std::string i2pcp_crt;
		i2p::config::GetOption ("i2pcontrol.cert", i2pcp_crt);
		std::string i2pcp_key;
		i2p::config::GetOption ("i2pcontrol.key", i2pcp_key);

		if (!i2pcp_crt.empty () && i2pcp_crt[0] != '/') i2pcp_crt = i2p::fs::DataDirPath (i2pcp_crt);
		if (!i2pcp_key.empty () && i2pcp_key[0] != '/') i2pcp_key = i2p::fs::DataDirPath (i2pcp_key);
		if (!i2p::fs::Exists (i2pcp_crt) || !i2p::fs::Exists (i2pcp_key))
		{
			LogPrint (eLogInfo, "I2PControl: Creating new certificate for control connection ", i2pcp_crt, " key ",
				i2pcp_key);
			CreateCertificate (i2pcp_crt.c_str (), i2pcp_key.c_str ());
		}
		else
			LogPrint (eLogDebug, "I2PControl: Using cert from ", i2pcp_crt, " key ", i2pcp_key);
		m_SSLContext.set_options (boost::asio::ssl::context::default_workarounds | boost::asio::ssl::context::no_sslv2
			| boost::asio::ssl::context::no_sslv3 | boost::asio::ssl::context::no_tlsv1
			| boost::asio::ssl::context::no_tlsv1_1 | boost::asio::ssl::context::single_dh_use);
		boost::system::error_code ec;
		m_SSLContext.use_certificate_file (i2pcp_crt, boost::asio::ssl::context::pem, ec);
		if (!ec) m_SSLContext.use_private_key_file (i2pcp_key, boost::asio::ssl::context::pem, ec);
		if (ec)
		{
			LogPrint (eLogInfo, "I2PControl: Failed to load ceritifcate: ", ec.message (), ". Recreating");
			CreateCertificate (i2pcp_crt.c_str (), i2pcp_key.c_str ());
			m_SSLContext.use_certificate_file (i2pcp_crt, boost::asio::ssl::context::pem, ec);
			if (!ec) m_SSLContext.use_private_key_file (i2pcp_key, boost::asio::ssl::context::pem, ec);
			if (ec)
				// give up
				LogPrint (eLogError, "I2PControl: Can't load certificates");
		}

		// handlers
		m_MethodHandlers["Authenticate"] = &I2PControlService::AuthenticateHandler;
		m_MethodHandlers["Echo"] = &I2PControlService::EchoHandler;
		m_MethodHandlers["I2PControl"] = &I2PControlService::I2PControlHandler;
		m_MethodHandlers["RouterInfo"] = &I2PControlHandlers::RouterInfoHandler;
		m_MethodHandlers["RouterManager"] = &I2PControlService::RouterManagerHandler;
		m_MethodHandlers["NetworkSetting"] = &I2PControlHandlers::NetworkSettingHandler;
		m_MethodHandlers["ClientServicesInfo"] = &I2PControlHandlers::ClientServicesInfoHandler;
		m_MethodHandlers["LocalDestinationInfo"] = &I2PControlHandlers::LocalDestinationInfoHandler;

		// I2PControl
		m_I2PControlHandlers["i2pcontrol.password"] = &I2PControlService::PasswordHandler;

		// RouterManager
		m_RouterManagerHandlers["Reseed"] = &I2PControlService::ReseedHandler;
		m_RouterManagerHandlers["Shutdown"] = &I2PControlService::ShutdownHandler;
		m_RouterManagerHandlers["ShutdownGraceful"] = &I2PControlService::ShutdownGracefulHandler;
	}

	I2PControlService::~I2PControlService ()
	{
		Stop ();
	}

	void I2PControlService::Start ()
	{
		bool expected = false;
		if (!m_IsRunning.compare_exchange_strong (expected, true, std::memory_order_acq_rel)) return;
		// Stop() leaves the io_context stopped; a restart must restart it
		// before registering the accept operation.
		m_Service.restart ();
		Accept ();
		m_Thread = std::make_unique<std::thread> (std::bind (&I2PControlService::Run, this));
	}

	void I2PControlService::Stop ()
	{
		bool expected = true;
		if (m_IsRunning.compare_exchange_strong (expected, false, std::memory_order_acq_rel))
		{
			if (m_Acceptor) m_Acceptor->cancel ();
#if defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)
			if (m_LocalAcceptor)
			{
				auto path = m_LocalAcceptor->local_endpoint ().path ();
				m_LocalAcceptor->cancel ();
				std::remove (path.c_str ());
			}
#endif
			std::vector<std::function<void ()>> closeConnections;
			{
				std::lock_guard lock (m_Connections->mutex);
				for (const auto & [id, close] : m_Connections->close)
				{
					(void)id;
					closeConnections.push_back (close);
				}
			}
			for (const auto & close : closeConnections) close ();
			boost::system::error_code ignored;
			m_ShutdownTimer.cancel (ignored);
			m_Service.stop ();
			if (m_Thread)
			{
				m_Thread->join ();
				m_Thread = nullptr;
			}
			m_Service.restart ();
			m_Service.run ();
			m_Service.stop ();
		}
	}

	void I2PControlService::Run ()
	{
		i2p::util::SetThreadName ("I2PC");

		while (m_IsRunning.load (std::memory_order_acquire))
		{
			try
			{
				m_Service.run ();
			}
			catch (std::exception & ex)
			{
				LogPrint (eLogError, "I2PControl: Runtime exception: ", ex.what ());
			}
		}
	}

	void I2PControlService::Accept ()
	{
		if (!m_IsRunning.load (std::memory_order_acquire)) return;
		if (m_Acceptor)
		{
			auto newSocket
				= std::make_shared<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> (m_Service, m_SSLContext);
			m_Acceptor->async_accept (newSocket->lowest_layer (),
				[this, newSocket] (const boost::system::error_code & ecode) { HandleAccepted (ecode, newSocket); });
		}
#if defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)
		else if (m_LocalAcceptor)
		{
			auto newSocket = std::make_shared<boost::asio::ssl::stream<boost::asio::local::stream_protocol::socket>> (
				m_Service, m_SSLContext);
			m_LocalAcceptor->async_accept (newSocket->lowest_layer (),
				[this, newSocket] (const boost::system::error_code & ecode) { HandleAccepted (ecode, newSocket); });
		}
#endif
	}

	template <typename ssl_socket>
	void I2PControlService::HandleAccepted (
		const boost::system::error_code & ecode, std::shared_ptr<ssl_socket> newSocket)
	{
		if (ecode != boost::asio::error::operation_aborted) Accept ();

		if (ecode)
		{
			LogPrint (eLogError, "I2PControl: Accept error: ", ecode.message ());
			return;
		}
		if (!m_IsRunning.load (std::memory_order_acquire))
		{
			boost::system::error_code ignored;
			newSocket->lowest_layer ().close (ignored);
			return;
		}
		if (m_Connections->active.fetch_add (1, std::memory_order_acq_rel) >= I2P_CONTROL_MAX_CONNECTIONS)
		{
			m_Connections->active.fetch_sub (1, std::memory_order_acq_rel);
			boost::system::error_code ignored;
			newSocket->lowest_layer ().close (ignored);
			LogPrint (eLogWarning, "I2PControl: Active connection limit reached");
			return;
		}
		uint64_t connectionID = 0;
		{
			std::lock_guard lock (m_Connections->mutex);
			if (!m_IsRunning.load (std::memory_order_acquire))
			{
				m_Connections->active.fetch_sub (1, std::memory_order_acq_rel);
				boost::system::error_code ignored;
				newSocket->lowest_layer ().close (ignored);
				return;
			}
			connectionID = ++m_Connections->nextID;
			m_Connections->close.emplace (connectionID, [weak = std::weak_ptr<ssl_socket> (newSocket)]
				{
					if (auto socket = weak.lock ())
					{
						boost::system::error_code ignored;
						socket->lowest_layer ().close (ignored);
					}
				});
		}
		auto connection = std::shared_ptr<void> (new int (0),
			[registry = m_Connections, connectionID] (void * value)
			{
				delete static_cast<int *> (value);
				{
					std::lock_guard lock (registry->mutex);
					registry->close.erase (connectionID);
				}
				registry->active.fetch_sub (1, std::memory_order_acq_rel);
			});
		auto deadline = std::make_shared<boost::asio::steady_timer> (m_Service);
		LogPrint (eLogDebug, "I2PControl: New request from ", newSocket->lowest_layer ().remote_endpoint ());
		Handshake (newSocket, deadline, connection);
	}

	template <typename ssl_socket> void I2PControlService::Handshake (std::shared_ptr<ssl_socket> socket,
		std::shared_ptr<boost::asio::steady_timer> deadline, std::shared_ptr<void> connection)
	{
		ArmControlDeadline (socket, deadline);
		socket->async_handshake (boost::asio::ssl::stream_base::server,
			[this, socket, deadline, connection] (const boost::system::error_code & ecode)
			{
				if (ecode || !m_IsRunning.load (std::memory_order_acquire))
				{
					LogPrint (eLogError, "I2PControl: Handshake error: ", ecode.message ());
					return;
				}
				ReadRequest (socket, deadline, connection);
			});
	}

	template <typename ssl_socket> void I2PControlService::ReadRequest (std::shared_ptr<ssl_socket> socket,
		std::shared_ptr<boost::asio::steady_timer> deadline, std::shared_ptr<void> connection)
	{
		ArmControlDeadline (socket, deadline);
		auto request = std::make_shared<I2PControlBuffer> ();
		boost::asio::async_read (*socket, boost::asio::buffer (request->data (), 4), boost::asio::transfer_exactly (4),
			[this, socket, request, deadline, connection] (
				const boost::system::error_code & ecode, size_t bytes_transferred)
			{ HandleRequestReceived (ecode, bytes_transferred, socket, request, deadline, connection); });
	}

	template <typename ssl_socket>
	void I2PControlService::HandleRequestReceived (const boost::system::error_code & ecode, size_t bytes_transferred,
		std::shared_ptr<ssl_socket> socket, std::shared_ptr<I2PControlBuffer> buf,
		std::shared_ptr<boost::asio::steady_timer> deadline, std::shared_ptr<void> connection)
	{
		if (ecode)
		{
			LogPrint (eLogError, "I2PControl: Read error: ", ecode.message ());
			return;
		}
		else
		{
			if (!m_IsRunning.load (std::memory_order_acquire)) return;
			bool isHtml = bytes_transferred == 4 && !memcmp (buf->data (), "POST", 4);
			try
			{
				auto request = std::make_shared<std::string> (buf->data (), bytes_transferred);
				if (isHtml)
				{
					if (request->find ("\r\n\r\n") == std::string::npos)
					{
						ArmControlDeadline (socket, deadline);
						boost::asio::async_read_until (*socket,
							boost::asio::dynamic_buffer (*request, I2P_CONTROL_MAX_REQUEST_SIZE), "\r\n\r\n",
							[this, socket, request, deadline, connection] (
								const boost::system::error_code & readError, size_t)
							{
								if (readError || !m_IsRunning.load (std::memory_order_acquire))
								{
									LogPrint (eLogError, "I2PControl: Incomplete or oversized HTTP headers");
									return;
								}
								HandleHttpRequest (socket, request, deadline, connection);
							});
						return;
					}
					HandleHttpRequest (socket, request, deadline, connection);
					return;
				}
				ArmControlDeadline (socket, deadline);
				buf->fill (0);
				socket->async_read_some (boost::asio::buffer (*buf),
					[this, socket, request, buf, deadline, connection] (
						const boost::system::error_code & readError, size_t transferred)
					{
						if (readError || !m_IsRunning.load (std::memory_order_acquire)) return;
						request->append (buf->data (), transferred);
						ProcessRequest (socket, request, false, deadline, connection);
					});
			}
			catch (std::exception & ex)
			{
				LogPrint (eLogError, "I2PControl: Exception while reading request: ", ex.what ());
			}
		}
	}

	template <typename ssl_socket> void I2PControlService::HandleHttpRequest (std::shared_ptr<ssl_socket> socket,
		std::shared_ptr<std::string> request, std::shared_ptr<boost::asio::steady_timer> deadline,
		std::shared_ptr<void> connection)
	{
		try
		{
			const auto separator = request->find ("\r\n\r\n");
			if (separator == std::string::npos || separator + 4 > I2P_CONTROL_MAX_REQUEST_SIZE)
				throw std::runtime_error ("Malformed or oversized HTTP headers");
			std::stringstream headers (request->substr (0, separator + 2));
			std::string header;
			std::optional<size_t> contentLength;
			while (std::getline (headers, header))
			{
				auto colon = header.find (':');
				if (colon != std::string::npos && boost::iequals (header.substr (0, colon), "Content-Length"))
				{
					if (contentLength) throw std::runtime_error ("Duplicate Content-Length");
					const auto value = boost::algorithm::trim_copy (header.substr (colon + 1));
					size_t parsed = 0;
					const auto length = std::stoull (value, &parsed);
					if (parsed != value.size () || length > I2P_CONTROL_MAX_CONTENT_LENGTH)
						throw std::runtime_error ("Invalid Content-Length");
					contentLength = static_cast<size_t> (length);
				}
			}
			if (!contentLength) throw std::runtime_error ("Missing Content-Length");
			const size_t bodyOffset = separator + 4;
			const size_t received = request->size () - bodyOffset;
			if (received > *contentLength) throw std::runtime_error ("Contradictory Content-Length");
			const size_t remaining = *contentLength - received;
			if (!remaining)
			{
				ProcessRequest (socket, request, true, deadline, connection);
				return;
			}
			auto body = std::make_shared<std::vector<char>> (remaining);
			ArmControlDeadline (socket, deadline);
			boost::asio::async_read (*socket, boost::asio::buffer (*body), boost::asio::transfer_exactly (remaining),
				[this, socket, request, body, deadline, connection] (
					const boost::system::error_code & readError, size_t transferred)
				{
					if (readError || !m_IsRunning.load (std::memory_order_acquire)
						|| transferred != body->size ())
					{
						LogPrint (eLogError, "I2PControl: Incomplete HTTP request body");
						return;
					}
					request->append (body->data (), body->size ());
					ProcessRequest (socket, request, true, deadline, connection);
				});
		}
		catch (const std::exception & ex)
		{
			LogPrint (eLogError, "I2PControl: Invalid HTTP request: ", ex.what ());
		}
	}

	template <typename ssl_socket> void I2PControlService::ProcessRequest (std::shared_ptr<ssl_socket> socket,
		std::shared_ptr<std::string> request, bool isHtml, std::shared_ptr<boost::asio::steady_timer> deadline,
		std::shared_ptr<void> connection)
	{
		try
		{
			std::stringstream ss (*request);
			if (isHtml)
			{
				std::string header;
				do std::getline (ss, header); while (ss && header != "\r");
				if (!ss) throw std::runtime_error ("Malformed HTTP headers");
			}
				std::ostringstream response;
				boost::property_tree::ptree pt;
				boost::property_tree::read_json (ss, pt);

				std::string id = pt.get<std::string> ("id");
				std::string method = pt.get<std::string> ("method");
				auto it = m_MethodHandlers.find (method);
				if (it != m_MethodHandlers.end ())
				{
					auto params = pt.get_child ("params");
					if (method != "Authenticate" && !IsValidToken (params.get<std::string> ("Token", "")))
					{
						LogPrint (eLogWarning, "I2PControl: Invalid or missing token for method ", method);
						response << "{\"id\":null,\"error\":";
						response << "{\"code\":-32602,\"message\":\"Invalid or missing token\"},";
						response << "\"jsonrpc\":\"2.0\"}\n";
					}
					else
					{
						response << "{\"id\":" << id << ",\"result\":{";
						(this->*(it->second)) (params, response);
						response << "},\"jsonrpc\":\"2.0\"}\n";
					}
				}
				else
				{
					LogPrint (eLogWarning, "I2PControl: Unknown method ", method);
					response << "{\"id\":null,\"error\":";
					response << "{\"code\":-32601,\"message\":\"Method not found\"},";
					response << "\"jsonrpc\":\"2.0\"}\n";
				}
				SendResponse (socket, response, isHtml, deadline, connection);
			}
			catch (std::exception & ex)
			{
				LogPrint (eLogError, "I2PControl: Exception when handle request: ", ex.what ());
				std::ostringstream response;
				response << "{\"id\":null,\"error\":";
				response << "{\"code\":-32700,\"message\":\"Malformed request\"},";
				response << "\"jsonrpc\":\"2.0\"}\n";
				SendResponse (socket, response, isHtml, deadline, connection);
			}
			catch (...)
			{
				LogPrint (eLogError, "I2PControl: Handle request unknown exception");
			}
		}
	}

	template <typename ssl_socket>
	void I2PControlService::SendResponse (
		std::shared_ptr<ssl_socket> socket, std::ostringstream & response, bool isHtml,
		std::shared_ptr<boost::asio::steady_timer> deadline, std::shared_ptr<void> connection)
	{
		auto message = std::make_shared<std::string> ();
		if (isHtml)
		{
			std::ostringstream header;
			header << "HTTP/1.1 200 OK\r\n";
			header << "Connection: close\r\n";
			header << "Content-Length: " << std::to_string (response.str ().length ()) << "\r\n";
			header << "Content-Type: application/json\r\n";
			header << "Date: ";
			std::time_t t = std::time (nullptr);
			std::tm tm { };
#ifdef _WIN32
			const bool haveTime = gmtime_s (&tm, &t) == 0;
#else
			const bool haveTime = gmtime_r (&t, &tm) != nullptr;
#endif
			if (haveTime) header << std::put_time (&tm, "%a, %d %b %Y %T GMT");
			header << "\r\n";
			header << "\r\n";
			*message = header.str ();
		}
		*message += response.str ();
		ArmControlDeadline (socket, deadline);
		boost::asio::async_write (*socket, boost::asio::buffer (*message), boost::asio::transfer_all (),
			[socket, message, deadline, connection] (
				const boost::system::error_code & ecode, std::size_t /*bytes_transferred*/)
			{
				boost::system::error_code ignored;
				deadline->cancel (ignored);
				if (ecode) LogPrint (eLogError, "I2PControl: Write error: ", ecode.message ());
			});
	}

	// handlers

	void I2PControlService::AuthenticateHandler (
		const boost::property_tree::ptree & params, std::ostringstream & results)
	{
		int api = params.get<int> ("API");
		auto password = params.get<std::string> ("Password");
		LogPrint (eLogDebug, "I2PControl: Authenticate API=", api);
		const auto now = i2p::util::GetSecondsSinceEpoch ();
		PurgeExpiredTokens (now);
		if (!m_AuthenticationWindowStart || now >= m_AuthenticationWindowStart + 60)
		{
			m_AuthenticationWindowStart = now;
			m_AuthenticationsInWindow = 0;
		}
		if (m_AuthenticationsInWindow >= I2P_CONTROL_MAX_AUTHENTICATIONS_PER_MINUTE)
		{
			LogPrint (eLogWarning, "I2PControl: Authentication rate limit reached");
			return;
		}
		++m_AuthenticationsInWindow;
		bool isValid = password.size () == m_Password.size ()
			&& CRYPTO_memcmp (password.data (), m_Password.data (), m_Password.size ()) == 0;
		if (!isValid)
		{
			LogPrint (eLogError, "I2PControl: Authenticate - Invalid password");
			return;
		}
		InsertParam (results, "API", api);
		results << ",";
		uint8_t tokenBytes[32];
		if (RAND_bytes (tokenBytes, sizeof (tokenBytes)) != 1)
		{
			LogPrint (eLogError, "I2PControl: Can't generate authentication token: ", GetOpenSSLError ());
			return;
		}
		std::string token = i2p::data::ByteStreamToBase64 (tokenBytes, sizeof (tokenBytes));
		if (m_Tokens.size () >= I2P_CONTROL_MAX_TOKENS)
		{
			auto oldest = std::min_element (m_Tokens.begin (), m_Tokens.end (),
				[] (const auto & left, const auto & right) { return left.second < right.second; });
			if (oldest != m_Tokens.end ()) m_Tokens.erase (oldest);
		}
		m_Tokens[token] = now + I2P_CONTROL_TOKEN_LIFETIME;
		InsertParam (results, "Token", token);
	}

	void I2PControlService::PurgeExpiredTokens (uint64_t now)
	{
		std::erase_if (m_Tokens, [now] (const auto & token) { return now >= token.second; });
	}

	bool I2PControlService::IsValidToken (const std::string & token)
	{
		uint64_t ts = i2p::util::GetSecondsSinceEpoch ();
		PurgeExpiredTokens (ts);
		bool found = false;
		for (const auto & it : m_Tokens)
		{
			if (it.first.size () == token.size ()
				&& CRYPTO_memcmp (it.first.data (), token.data (), token.size ()) == 0)
				found = true; // don't short-circuit; keep timing independent of match position
		}
		return found;
	}

	void I2PControlService::EchoHandler (const boost::property_tree::ptree & params, std::ostringstream & results)
	{
		auto echo = params.get<std::string> ("Echo");
		LogPrint (eLogDebug, "I2PControl Echo Echo=", echo);
		InsertParam (results, "Result", echo);
	}

	// I2PControl

	void I2PControlService::I2PControlHandler (const boost::property_tree::ptree & params, std::ostringstream & results)
	{
		for (auto & it : params)
		{
			LogPrint (eLogDebug, "I2PControl: I2PControl request: ", it.first);
			auto it1 = m_I2PControlHandlers.find (it.first);
			if (it1 != m_I2PControlHandlers.end ())
			{
				(this->*(it1->second)) (it.second.data ());
				InsertParam (results, it.first, "");
			}
			else
				LogPrint (eLogError, "I2PControl: I2PControl unknown request: ", it.first);
		}
	}

	void I2PControlService::PasswordHandler (const std::string & value)
	{
		if (value.empty () || value == I2P_CONTROL_DEFAULT_PASSWORD)
		{
			LogPrint (eLogWarning, "I2PControl: Refusing an empty or default administrative password");
			return;
		}
		LogPrint (eLogWarning, "I2PControl: Administrative password changed; update the configuration to persist it");
		m_Password = value;
		m_Tokens.clear ();
	}

	// RouterManager

	void I2PControlService::RouterManagerHandler (
		const boost::property_tree::ptree & params, std::ostringstream & results)
	{
		for (auto it = params.begin (); it != params.end (); it++)
		{
			LogPrint (eLogDebug, "I2PControl: RouterManager request: ", it->first);
			auto it1 = m_RouterManagerHandlers.find (it->first);
			if (it1 != m_RouterManagerHandlers.end ())
			{
				if (it != params.begin ()) results << ",";
				(this->*(it1->second)) (results);
			}
			else
				LogPrint (eLogError, "I2PControl: RouterManager unknown request: ", it->first);
		}
	}

	void I2PControlService::ShutdownHandler (std::ostringstream & results)
	{
		LogPrint (eLogInfo, "I2PControl: Shutdown requested");
		InsertParam (results, "Shutdown", "");
		m_ShutdownTimer.expires_after (std::chrono::seconds (1)); // 1 second to make sure response has been sent
		m_ShutdownTimer.async_wait ([] (const boost::system::error_code &) { Daemon.running = 0; });
	}

	void I2PControlService::ShutdownGracefulHandler (std::ostringstream & results)
	{
		i2p::context.SetAcceptsTunnels (false);
		int timeout = i2p::tunnel::tunnels.GetTransitTunnelsExpirationTimeout ();
		LogPrint (eLogInfo, "I2PControl: Graceful shutdown requested, ", timeout, " seconds remains");
		InsertParam (results, "ShutdownGraceful", "");
		m_ShutdownTimer.expires_after (std::chrono::seconds (timeout + 1)); // + 1 second
		m_ShutdownTimer.async_wait ([] (const boost::system::error_code &) { Daemon.running = 0; });
	}

	void I2PControlService::ReseedHandler (std::ostringstream & results)
	{
		LogPrint (eLogInfo, "I2PControl: Reseed requested");
		InsertParam (results, "Reseed", "");
		i2p::data::netdb.Reseed ();
	}

	// certificate
	void I2PControlService::CreateCertificate (const char * crt_path, const char * key_path)
	{
		std::unique_ptr<EVP_PKEY, decltype (&EVP_PKEY_free)> pkey (nullptr, EVP_PKEY_free);
#ifndef LIBRESSL_VERSION_NUMBER // OpenSSL 3.0.2 is the minimum
		pkey.reset (EVP_RSA_gen (4096)); // e = 65537
		if (!pkey)
		{
			LogPrint (eLogError, "I2PControl: Can't create RSA key for certificate: ", GetOpenSSLError ());
			return;
		}
#else
		pkey.reset (EVP_PKEY_new ());
		std::unique_ptr<RSA, decltype (&RSA_free)> rsa (RSA_new (), RSA_free);
		std::unique_ptr<BIGNUM, decltype (&BN_free)> e (BN_dup (i2p::crypto::GetRSAE ()), BN_free);
		if (!pkey || !rsa || !e || RSA_generate_key_ex (rsa.get (), 4096, e.get (), nullptr) != 1)
		{
			LogPrint (eLogError, "I2PControl: Can't create RSA key for certificate: ", GetOpenSSLError ());
			return;
		}
		if (EVP_PKEY_assign_RSA (pkey.get (), rsa.get ()) != 1)
		{
			LogPrint (eLogError, "I2PControl: Can't assign RSA key for certificate: ", GetOpenSSLError ());
			return;
		}
		rsa.release (); // pkey owns the RSA key now
#endif

		// The certificate is released after it has been written and before the
		// private key is saved, matching the previous explicit free order.
		{
			std::unique_ptr<X509, decltype (&X509_free)> x509 (X509_new (), X509_free);
			if (!x509)
			{
				LogPrint (eLogError, "I2PControl: Can't create certificate: ", GetOpenSSLError ());
				return;
			}
			if (ASN1_INTEGER_set (X509_get_serialNumber (x509.get ()), 1) != 1
				|| !X509_gmtime_adj (X509_getm_notBefore (x509.get ()), 0)
				|| !X509_gmtime_adj (X509_getm_notAfter (x509.get ()),
					I2P_CONTROL_CERTIFICATE_VALIDITY * 24 * 60 * 60) // expiration
				|| X509_set_pubkey (x509.get (), pkey.get ()) != 1) // public key
			{
				LogPrint (eLogError, "I2PControl: Can't initialize certificate: ", GetOpenSSLError ());
				return;
			}

			// The subject is a private copy; it is released right after signing.
			{
				std::unique_ptr<X509_NAME, decltype (&X509_NAME_free)> name (
					X509_NAME_dup (X509_get_subject_name (x509.get ())), X509_NAME_free);
				if (!name)
				{
					LogPrint (eLogError, "I2PControl: Can't duplicate certificate subject: ", GetOpenSSLError ());
					return;
				}
				if (X509_NAME_add_entry_by_txt (name.get (), "C", MBSTRING_ASC, (unsigned char *)"A1", -1, -1, 0)
						!= 1 // country (Anonymous proxy)
					|| X509_NAME_add_entry_by_txt (name.get (), "O", MBSTRING_ASC,
						   (unsigned char *)I2P_CONTROL_CERTIFICATE_ORGANIZATION, -1, -1, 0)
						!= 1 // organization
					|| X509_NAME_add_entry_by_txt (name.get (), "CN", MBSTRING_ASC,
						   (unsigned char *)I2P_CONTROL_CERTIFICATE_COMMON_NAME, -1, -1, 0)
						!= 1) // common name
				{
					LogPrint (eLogError, "I2PControl: Can't set certificate subject: ", GetOpenSSLError ());
					return;
				}
				if (X509_set_subject_name (x509.get (), name.get ()) != 1
					|| X509_set_issuer_name (x509.get (), name.get ()) != 1) // set issuer to ourselves
				{
					LogPrint (eLogError, "I2PControl: Can't set certificate names: ", GetOpenSSLError ());
					return;
				}
				if (X509_sign (x509.get (), pkey.get (), EVP_sha256 ()) == 0) // last param must be NULL for EdDSA
				{
					LogPrint (eLogError, "I2PControl: Can't sign certificate: ", GetOpenSSLError ());
					return;
				}
			}

			LogPrint (eLogInfo, "I2PControl: Saving new cert to ", crt_path);
			if (!WritePEMAtomically (crt_path, [certificate = x509.get ()] (BIO * output)
				{ return PEM_write_bio_X509 (output, certificate); }))
				LogPrint (eLogError, "I2PControl: Can't atomically write cert: ", GetOpenSSLError ());
		}

		LogPrint (eLogInfo, "I2PControl: Saving cert key to ", key_path);
		if (!WritePEMAtomically (key_path, [key = pkey.get ()] (BIO * output)
			{ return PEM_write_bio_PrivateKey (output, key, nullptr, nullptr, 0, nullptr, nullptr); }))
			LogPrint (eLogError, "I2PControl: Can't atomically write key: ", GetOpenSSLError ());
	}
}
}
