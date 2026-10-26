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
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>
#include <sstream>
#include <iomanip>
#include <openssl/x509.h>
#include <openssl/bn.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

#ifndef _WIN32
#include <cerrno>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <boost/property_tree/json_parser.hpp>
#include <boost/algorithm/string.hpp>

#include "FS.h"
#include "Base.h"
#include "Crypto.h"
#include "Log.h"
#include "Config.h"
#include "NetDb.hpp"
#include "Tunnel.h"
#include "Daemon.h"
#include "I2PControl.h"

namespace
{
template <typename Writer> bool WritePEMAtomically (std::string_view path, Writer && writer)
{
	std::unique_ptr<BIO, decltype (&BIO_free)> output (BIO_new (BIO_s_mem ()), BIO_free);
	if (!output || writer (output.get ()) != 1) return false;
	char * bytes = nullptr;
	const long length = BIO_get_mem_data (output.get (), &bytes);
	if (length <= 0 || !bytes) return false;
	const bool written = i2p::fs::AtomicWritePrivate (path, std::string_view (bytes, static_cast<size_t> (length)));
	// This helper carries the private-key PEM as well as the certificate.
	OPENSSL_cleanse (bytes, static_cast<size_t> (length));
	return written;
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

#if defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)
// The TCP path is unchanged; a local stream peer must belong to this process'
// user where the platform provides a peer-credential call for local sockets.
// Elsewhere the constructor requires a non-default password instead, so the
// listener is not left with file mode as its only gate.
template <typename Socket> bool AcceptLocalControlPeer (const Socket &)
{
	return true;
}
#if !defined(_WIN32)
constexpr bool I2P_CONTROL_LOCAL_PEER_AUTH = i2p::client::localcontrol::HAS_PEER_CREDENTIALS;
[[maybe_unused]] bool AcceptLocalControlPeer (boost::asio::local::stream_protocol::socket & socket)
{
	return i2p::client::localcontrol::IsSameUserPeer (socket.native_handle ());
}
#else
// No per-user credential call exists for a local socket on this platform.
constexpr bool I2P_CONTROL_LOCAL_PEER_AUTH = false;
[[maybe_unused]] bool AcceptLocalControlPeer (const boost::asio::local::stream_protocol::socket &)
{
	return true;
}
#endif
#else
// No local listener is available, so the credential policy is not consulted.
[[maybe_unused]] constexpr bool I2P_CONTROL_LOCAL_PEER_AUTH = false;
#endif

template <typename Socket>
void ArmControlDeadline (
	const std::shared_ptr<Socket> & socket, const std::shared_ptr<boost::asio::steady_timer> & deadline)
{
	deadline->expires_after (i2p::client::I2P_CONTROL_OPERATION_TIMEOUT);
	deadline->async_wait (
		[weakSocket = std::weak_ptr<Socket> (socket)] (const boost::system::error_code & ec)
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
		// Validate the credentials before binding anything: a rejected startup
		// must not leave a socket behind.
		i2p::config::GetOption ("i2pcontrol.password", m_Password);
		if (m_Password.empty ()) throw std::invalid_argument ("I2PControl password must not be empty");
		if (port && m_Password == I2P_CONTROL_DEFAULT_PASSWORD)
		{
			auto listenAddress = boost::asio::ip::make_address (address);
			if (!listenAddress.is_loopback ())
				throw std::invalid_argument ("I2PControl refuses a non-loopback listener with the default password");
		}
		// A local socket is protected by its mode and its peer-user check. If
		// the platform provides neither, the password is the only gate. This is
		// only enforced when a local listener can actually be created.
#if defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)
		if (!port && !I2P_CONTROL_LOCAL_PEER_AUTH && m_Password == I2P_CONTROL_DEFAULT_PASSWORD)
			throw std::invalid_argument (
				"I2PControl refuses a local socket without peer credentials and the default password");
#endif

		if (port)
			m_Acceptor = std::make_unique<boost::asio::ip::tcp::acceptor> (m_Service,
				boost::asio::ip::tcp::endpoint (
					boost::asio::ip::make_address (address), static_cast<unsigned short> (port)));
		else
#if defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)
		{
#if !defined(_WIN32)
			std::string pathError;
			if (!localcontrol::PrepareSocketPath (address, pathError))
			{
				LogPrint (eLogError, "I2PControl: Refusing to replace ", address, ": ", pathError);
				throw std::invalid_argument ("I2PControl refuses to replace the configured local socket path");
			}
#else
			std::remove (address.c_str ()); // just in case
#endif
			m_LocalAcceptor = std::make_unique<boost::asio::local::stream_protocol::acceptor> (
				m_Service, boost::asio::local::stream_protocol::endpoint (address));
#if !defined(_WIN32)
			// Record what bind() created before any later step can fail, so
			// cleanup removes this socket and never a replacement path.
			const auto identity = localcontrol::IdentifySocketPath (address);
			if (identity.valid)
			{
				m_LocalSocketDevice = identity.device;
				m_LocalSocketInode = identity.inode;
				m_LocalSocketIdentity = true;
			}
			// Do not touch the process-wide umask. The reject-after-accept peer
			// check below does not depend on the mode, but the mode keeps the
			// path out of reach of other users in the first place.
			if (!localcontrol::RestrictSocketPath (address, pathError))
			{
				LogPrint (eLogError, "I2PControl: Can't restrict local socket mode: ", pathError);
				RemoveLocalControlSocket (address);
				throw std::invalid_argument ("I2PControl can't restrict the configured local socket path");
			}
#endif
		}
#else
			LogPrint (eLogError, "I2PControl: Local sockets are not supported");
#endif

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
		// Make the protocol floor explicit rather than relying only on the
		// deprecated Asio option bits. The control listener is local-only.
		if (SSL_CTX_set_min_proto_version (m_SSLContext.native_handle (), TLS1_2_VERSION) != 1)
			LogPrint (eLogWarning, "I2PControl: Unable to set the TLS 1.2 minimum version");
		boost::system::error_code ec;
		m_SSLContext.use_certificate_file (i2pcp_crt, boost::asio::ssl::context::pem, ec);
		if (!ec) m_SSLContext.use_private_key_file (i2pcp_key, boost::asio::ssl::context::pem, ec);
		if (!ec && SSL_CTX_check_private_key (m_SSLContext.native_handle ()) != 1)
		{
			LogPrint (eLogWarning, "I2PControl: certificate and private key do not match; recreating");
			ec = boost::system::error_code (boost::system::errc::invalid_argument, boost::system::generic_category ());
		}
		if (ec)
		{
			LogPrint (eLogInfo, "I2PControl: Failed to load ceritifcate: ", ec.message (), ". Recreating");
			CreateCertificate (i2pcp_crt.c_str (), i2pcp_key.c_str ());
			m_SSLContext.use_certificate_file (i2pcp_crt, boost::asio::ssl::context::pem, ec);
			if (!ec) m_SSLContext.use_private_key_file (i2pcp_key, boost::asio::ssl::context::pem, ec);
			if (!ec && SSL_CTX_check_private_key (m_SSLContext.native_handle ()) != 1)
			{
				LogPrint (eLogError, "I2PControl: regenerated certificate and key still do not match");
				ec = boost::system::error_code (
					boost::system::errc::invalid_argument, boost::system::generic_category ());
			}
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
			std::vector<std::function<void ()>> closeConnections;
			std::vector<std::shared_ptr<boost::asio::steady_timer>> deadlines;
			// Stop the io_context and join first: Asio shared objects must not be
			// closed while a handler is running on them. The queued cancellation
			// handlers are drained after the sockets are closed.
			m_Service.stop ();
			// A shutdown request can originate in a control handler, i.e. on the
			// io_context thread itself. Joining there would self-deadlock; detach
			// instead, because Run() observes m_IsRunning == false as soon as the
			// current handler returns.
			const bool onServiceThread = m_Service.get_executor ().running_in_this_thread ();
			if (m_Thread)
			{
				if (onServiceThread)
					m_Thread->detach ();
				else
					m_Thread->join ();
				m_Thread = nullptr;
			}
			if (m_Acceptor) m_Acceptor->cancel ();
#if defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)
			if (m_LocalAcceptor)
			{
				auto path = m_LocalAcceptor->local_endpoint ().path ();
				m_LocalAcceptor->cancel ();
#if !defined(_WIN32)
				RemoveLocalControlSocket (path);
#else
				std::remove (path.c_str ());
#endif
			}
#endif
			{
				std::lock_guard lock (m_Connections->mutex);
				for (const auto & [id, close] : m_Connections->close)
				{
					(void)id;
					closeConnections.push_back (close);
				}
				for (const auto & [id, deadline] : m_Connections->deadlines)
				{
					(void)id;
					if (auto timer = deadline.lock ()) deadlines.push_back (timer);
				}
			}
			for (const auto & deadline : deadlines) deadline->cancel ();
			for (const auto & close : closeConnections) close ();
			m_ShutdownTimer.cancel ();
			// Do not re-enter run() from a handler that is still on the stack.
			// In that path cancellation handlers remain queued on the stopped
			// context until it is restarted and run, or the context is destroyed.
			if (!onServiceThread)
			{
				m_Service.restart ();
				m_Service.run ();
				m_Service.stop ();
			}
		}
	}

#if defined(BOOST_ASIO_HAS_LOCAL_SOCKETS) && !defined(_WIN32)
	void I2PControlService::RemoveLocalControlSocket (const std::string & path)
	{
		if (!m_LocalSocketIdentity) return;
		localcontrol::SocketIdentity identity;
		identity.device = m_LocalSocketDevice;
		identity.inode = m_LocalSocketInode;
		identity.valid = true;
		if (!localcontrol::RemoveSocketPath (path, identity))
		{
			// The path was replaced after bind(); it is not ours to delete.
			LogPrint (eLogDebug, "I2PControl: Leaving a replaced local socket path ", path);
			return;
		}
		m_LocalSocketIdentity = false;
	}
#endif

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
			if (ecode != boost::asio::error::operation_aborted)
				LogPrint (eLogError, "I2PControl: Accept error: ", ecode.message ());
			return;
		}
#if defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)
		// The TCP path keeps its existing behaviour (the password and the
		// listener's loopback restriction are its gates). A local stream peer
		// must additionally belong to this user.
		if (!AcceptLocalControlPeer (newSocket->lowest_layer ()))
		{
			LogPrint (eLogWarning, "I2PControl: Rejected a local connection from another user");
			boost::system::error_code ignored;
			newSocket->lowest_layer ().close (ignored);
			return;
		}
#endif
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
			m_Connections->close.emplace (connectionID,
				[weak = std::weak_ptr<ssl_socket> (newSocket)]
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
					registry->deadlines.erase (connectionID);
				}
				registry->active.fetch_sub (1, std::memory_order_acq_rel);
			});
		auto deadline = std::make_shared<boost::asio::steady_timer> (m_Service);
		{
			std::lock_guard lock (m_Connections->mutex);
			m_Connections->deadlines[connectionID] = deadline;
		}
		// The same handler serves TCP and local stream sockets; remote_endpoint()
		// throwing on a disconnected peer must not skip the handshake deadline.
		LogPrint (eLogDebug, "I2PControl: New request accepted");
		Handshake (newSocket, deadline, connection);
	}

	template <typename ssl_socket>
	void I2PControlService::Handshake (std::shared_ptr<ssl_socket> socket,
		std::shared_ptr<boost::asio::steady_timer> deadline, std::shared_ptr<void> connection)
	{
		ArmControlDeadline (socket, deadline);
		socket->async_handshake (boost::asio::ssl::stream_base::server,
			[this, socket, deadline, connection] (const boost::system::error_code & ecode)
			{
				if (ecode)
				{
					// A cancelled handshake during Stop() is expected, not an error.
					if (ecode != boost::asio::error::operation_aborted)
						LogPrint (eLogError, "I2PControl: Handshake error: ", ecode.message ());
					return;
				}
				if (!m_IsRunning.load (std::memory_order_acquire))
				{
					LogPrint (eLogDebug, "I2PControl: Handshake completed while stopping");
					return;
				}
				ReadRequest (socket, deadline, connection);
			});
	}

	template <typename ssl_socket>
	void I2PControlService::ReadRequest (std::shared_ptr<ssl_socket> socket,
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
			bool isHtml = bytes_transferred == 4 && !std::memcmp (buf->data (), "POST", 4);
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

	template <typename ssl_socket>
	void I2PControlService::HandleHttpRequest (std::shared_ptr<ssl_socket> socket, std::shared_ptr<std::string> request,
		std::shared_ptr<boost::asio::steady_timer> deadline, std::shared_ptr<void> connection)
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
					if (readError || !m_IsRunning.load (std::memory_order_acquire) || transferred != body->size ())
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

	template <typename ssl_socket>
	void I2PControlService::ProcessRequest (std::shared_ptr<ssl_socket> socket, std::shared_ptr<std::string> request,
		bool isHtml, std::shared_ptr<boost::asio::steady_timer> deadline, std::shared_ptr<void> connection)
	{
		bool responseStarted = false;
		// Kept outside the try block so an error reply can echo the same id.
		std::string idText;
		try
		{
			// The JSON body starts after the HTTP headers, and the request id
			// must be recovered from that body because property_tree does not
			// preserve the JSON type of a scalar.
			std::string_view body (*request);
			if (isHtml)
			{
				const auto separator = request->find ("\r\n\r\n");
				if (separator == std::string::npos) throw std::runtime_error ("Malformed HTTP headers");
				body.remove_prefix (separator + 4);
			}

			std::ostringstream response;
			boost::property_tree::ptree pt;
			{
				std::istringstream stream { std::string (body) };
				boost::property_tree::read_json (stream, pt);
			}

			const auto requestId = json::ParseRequestId (body);
			switch (requestId.kind)
			{
			case json::JSONIdKind::Integer: idText = requestId.value; break;
			case json::JSONIdKind::String:
			{
				// A string id is a caller-supplied value: it is escaped and
				// never concatenated verbatim.
				std::string quoted;
				if (json::AppendQuoted (quoted, requestId.value)) idText = std::move (quoted);
				break;
			}
			default: break;
			}
			auto makeError = [&idText] (int code, std::string_view message)
			{
				std::ostringstream out;
				out << "{\"id\":" << (idText.empty () ? "null" : idText) << ",\"error\":{\"code\":" << code
					<< ",\"message\":" << json::Quote (message) << "},\"jsonrpc\":\"2.0\"}\n";
				return out;
			};
			if (idText.empty ())
			{
				responseStarted = true;
				auto errorResponse = makeError (I2P_CONTROL_ERROR_INVALID_REQUEST, "Invalid or missing request id");
				SendResponse (socket, errorResponse, isHtml, deadline, connection);
				return;
			}

			const auto method = pt.get_optional<std::string> ("method");
			if (!method || method->empty ())
			{
				responseStarted = true;
				auto errorResponse = makeError (I2P_CONTROL_ERROR_INVALID_REQUEST, "Invalid request");
				SendResponse (socket, errorResponse, isHtml, deadline, connection);
				return;
			}
			auto it = m_MethodHandlers.find (*method);
			if (it == m_MethodHandlers.end ())
			{
				LogPrint (eLogWarning, "I2PControl: Unknown method ", *method);
				responseStarted = true;
				auto errorResponse = makeError (I2P_CONTROL_ERROR_METHOD_NOT_FOUND, "Method not found");
				SendResponse (socket, errorResponse, isHtml, deadline, connection);
				return;
			}

			const auto params = pt.get_child_optional ("params");
			boost::property_tree::ptree emptyParams;
			const auto & requestParams = params ? *params : emptyParams;
			if (*method != "Authenticate")
			{
				switch (CheckToken (requestParams.get<std::string> ("Token", "")))
				{
				case TokenCheck::Valid: break;
				case TokenCheck::Missing:
					LogPrint (eLogWarning, "I2PControl: No authentication token presented for method ", *method);
					responseStarted = true;
					{
						auto errorResponse
							= makeError (I2P_CONTROL_ERROR_NO_TOKEN, "No authentication token presented");
						SendResponse (socket, errorResponse, isHtml, deadline, connection);
					}
					return;
				case TokenCheck::Unknown:
					LogPrint (eLogWarning, "I2PControl: Unknown authentication token for method ", *method);
					responseStarted = true;
					{
						auto errorResponse
							= makeError (I2P_CONTROL_ERROR_UNKNOWN_TOKEN, "Authentication token does not exist");
						SendResponse (socket, errorResponse, isHtml, deadline, connection);
					}
					return;
				case TokenCheck::Expired:
					LogPrint (eLogWarning, "I2PControl: Expired authentication token for method ", *method);
					responseStarted = true;
					{
						auto errorResponse
							= makeError (I2P_CONTROL_ERROR_EXPIRED_TOKEN, "Authentication token has expired");
						SendResponse (socket, errorResponse, isHtml, deadline, connection);
					}
					return;
				}
			}

			std::ostringstream results;
			(this->*(it->second)) (requestParams, results);
			response << "{\"id\":" << idText << ",\"result\":{" << results.str () << "},\"jsonrpc\":\"2.0\"}\n";
			responseStarted = true;
			SendResponse (socket, response, isHtml, deadline, connection);
		}
		catch (const I2PControlError & ex)
		{
			if (responseStarted)
			{
				LogPrint (eLogError, "I2PControl: Response failure: ", ex.what ());
				return;
			}
			LogPrint (eLogError, "I2PControl: Request failed: ", ex.what ());
			std::ostringstream response;
			response << "{\"id\":" << (idText.empty () ? "null" : idText) << ",\"error\":{\"code\":" << ex.GetCode ()
					 << ",\"message\":" << json::Quote (ex.what ()) << "},\"jsonrpc\":\"2.0\"}\n";
			SendResponse (socket, response, isHtml, deadline, connection);
		}
		catch (std::exception & ex)
		{
			if (responseStarted)
			{
				// Do not send a second JSON-RPC body after a write failure.
				LogPrint (eLogError, "I2PControl: Response failure: ", ex.what ());
				return;
			}
			LogPrint (eLogError, "I2PControl: Exception when handle request: ", ex.what ());
			std::ostringstream response;
			response << "{\"id\":null,\"error\":";
			response << "{\"code\":-32700,\"message\":\"Malformed request\"},";
			response << "\"jsonrpc\":\"2.0\"}\n";
			SendResponse (socket, response, isHtml, deadline, connection);
		}
		catch (...)
		{
			if (!responseStarted) LogPrint (eLogError, "I2PControl: Handle request unknown exception");
		}
	}

	template <typename ssl_socket>
	void I2PControlService::SendResponse (std::shared_ptr<ssl_socket> socket, std::ostringstream & response,
		bool isHtml, std::shared_ptr<boost::asio::steady_timer> deadline, std::shared_ptr<void> connection)
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
				deadline->cancel ();
				if (ecode) LogPrint (eLogError, "I2PControl: Write error: ", ecode.message ());
			});
	}

	// handlers

	void I2PControlService::AuthenticateHandler (
		const boost::property_tree::ptree & params, std::ostringstream & results)
	{
		// The API version is required by Java 2.14.0: a missing or unknown
		// value is a protocol error, not a silently successful handshake.
		const auto apiValue = params.get_optional<std::string> ("API");
		if (!apiValue)
			throw I2PControlError (I2P_CONTROL_ERROR_API_UNSPECIFIED, "I2PControl API version not specified");
		unsigned api = 0;
		if (!ParseUnsignedDecimal (*apiValue, api) || api != I2P_CONTROL_SUPPORTED_API)
			throw I2PControlError (I2P_CONTROL_ERROR_API_UNSUPPORTED, "I2PControl API version not supported");
		// The password is never logged or formatted anywhere in this handler.
		const auto password = params.get<std::string> ("Password", "");
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
			throw I2PControlError (I2P_CONTROL_ERROR_RATE_LIMITED, "Too many authentication attempts");
		}
		++m_AuthenticationsInWindow;
		const bool isValid = password.size () == m_Password.size ()
			&& CRYPTO_memcmp (password.data (), m_Password.data (), m_Password.size ()) == 0;
		if (!isValid)
		{
			LogPrint (eLogError, "I2PControl: Authenticate - Invalid password");
			throw I2PControlError (I2P_CONTROL_ERROR_INVALID_PASSWORD, "Invalid password");
		}
		uint8_t tokenBytes[32];
		if (RAND_bytes (tokenBytes, sizeof (tokenBytes)) != 1)
		{
			LogPrint (eLogError, "I2PControl: Can't generate authentication token: ", GetOpenSSLError ());
			throw I2PControlError (I2P_CONTROL_ERROR_INTERNAL, "Unable to generate authentication token");
		}
		std::string token = i2p::data::ByteStreamToBase64 (tokenBytes, sizeof (tokenBytes));
		if (m_Tokens.size () >= I2P_CONTROL_MAX_TOKENS)
		{
			auto oldest = std::min_element (m_Tokens.begin (), m_Tokens.end (),
				[] (const auto & left, const auto & right) { return left.second < right.second; });
			if (oldest != m_Tokens.end ()) m_Tokens.erase (oldest);
		}
		m_Tokens[token] = now + I2P_CONTROL_TOKEN_LIFETIME;
		InsertParam (results, "API", static_cast<int> (api));
		results << ",";
		InsertParam (results, "Token", token);
	}

	void I2PControlService::PurgeExpiredTokens (uint64_t now)
	{
		std::erase_if (m_Tokens, [now] (const auto & token) { return now >= token.second; });
	}

	I2PControlService::TokenCheck I2PControlService::CheckToken (const std::string & token)
	{
		if (token.empty ()) return TokenCheck::Missing;
		const auto now = i2p::util::GetSecondsSinceEpoch ();
		bool found = false;
		bool expired = false;
		for (const auto & it : m_Tokens)
		{
			if (it.first.size () == token.size ()
				&& CRYPTO_memcmp (it.first.data (), token.data (), token.size ()) == 0)
			{
				// don't short-circuit; keep timing independent of match position
				found = true;
				expired = now >= it.second;
			}
		}
		if (!found) return TokenCheck::Unknown;
		if (!expired) return TokenCheck::Valid;
		// The token existed and is now expired: drop it and report the
		// dedicated code instead of "does not exist".
		m_Tokens.erase (token);
		return TokenCheck::Expired;
	}

	void I2PControlService::EchoHandler (const boost::property_tree::ptree & params, std::ostringstream & results)
	{
		// The echo value is caller-supplied and may carry a token: it is
		// returned but never logged.
		auto echo = params.get<std::string> ("Echo", "");
		LogPrint (eLogDebug, "I2PControl: Echo request");
		InsertParam (results, "Result", echo);
	}

	// I2PControl

	void I2PControlService::I2PControlHandler (const boost::property_tree::ptree & params, std::ostringstream & results)
	{
		bool first = true;
		for (auto & it : params)
		{
			LogPrint (eLogDebug, "I2PControl: I2PControl request: ", it.first);
			auto it1 = m_I2PControlHandlers.find (it.first);
			if (it1 != m_I2PControlHandlers.end ())
			{
				(this->*(it1->second)) (it.second.data ());
				if (!first) results << ",";
				first = false;
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
		bool first = true;
		for (auto it = params.begin (); it != params.end (); it++)
		{
			LogPrint (eLogDebug, "I2PControl: RouterManager request: ", it->first);
			auto it1 = m_RouterManagerHandlers.find (it->first);
			if (it1 != m_RouterManagerHandlers.end ())
			{
				// Separate members by what was emitted, not by the position of
				// the key: an unknown key must not leave a leading comma.
				if (!first) results << ",";
				first = false;
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
		m_ShutdownTimer.async_wait (
			[] (const boost::system::error_code & ecode)
			{
				if (!ecode) Daemon.running = 0;
			});
	}

	void I2PControlService::ShutdownGracefulHandler (std::ostringstream & results)
	{
		i2p::context.SetAcceptsTunnels (false);
		int timeout = i2p::tunnel::tunnels.GetTransitTunnelsExpirationTimeout ();
		LogPrint (eLogInfo, "I2PControl: Graceful shutdown requested, ", timeout, " seconds remains");
		InsertParam (results, "ShutdownGraceful", "");
		m_ShutdownTimer.expires_after (std::chrono::seconds (timeout + 1)); // + 1 second
		m_ShutdownTimer.async_wait (
			[] (const boost::system::error_code & ecode)
			{
				if (!ecode) Daemon.running = 0;
			});
	}

	void I2PControlService::ReseedHandler (std::ostringstream & results)
	{
		// RequestReseed() starts the owned reseed job and returns immediately.
		// Calling the blocking NetDb::Reseed() here would stall the control
		// thread (and every other control client) for the whole transfer.
		const bool accepted = i2p::data::netdb.RequestReseed ();
		const auto state = i2p::data::netdb.GetReseedState ();
		LogPrint (
			eLogInfo, "I2PControl: Reseed requested, accepted=", accepted, " phase=", static_cast<int> (state.phase));
		InsertParam (results, "Reseed", "");
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
			if (!WritePEMAtomically (crt_path,
					[certificate = x509.get ()] (BIO * output) { return PEM_write_bio_X509 (output, certificate); }))
				LogPrint (eLogError, "I2PControl: Can't atomically write cert: ", GetOpenSSLError ());
		}

		LogPrint (eLogInfo, "I2PControl: Saving cert key to ", key_path);
		if (!WritePEMAtomically (key_path, [key = pkey.get ()] (BIO * output)
				{ return PEM_write_bio_PrivateKey (output, key, nullptr, nullptr, 0, nullptr, nullptr); }))
			LogPrint (eLogError, "I2PControl: Can't atomically write key: ", GetOpenSSLError ());
	}
}
}
