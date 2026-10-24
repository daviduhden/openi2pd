/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: I2PControl.
 *
 * Declares the TLS I2PControl service: an asynchronous JSON-RPC dispatcher
 * over TCP or a local stream socket, with bounded request buffers, expiring
 * tokens and the RouterManager method tables.
 *
 */

#ifndef I2P_CONTROL_H__
#define I2P_CONTROL_H__

#include <inttypes.h>
#include <atomic>
#include <thread>
#include <memory>
#include <array>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <sstream>
#include <map>
#include <set>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/property_tree/ptree.hpp>
#include "I2PControlHandlers.h"

namespace i2p
{
namespace client
{
	const size_t I2P_CONTROL_MAX_REQUEST_SIZE = 1024;
	typedef std::array<char, I2P_CONTROL_MAX_REQUEST_SIZE> I2PControlBuffer;
	const size_t I2P_CONTROL_MAX_CONTENT_LENGTH = 65536; // sanity limit on declared HTTP Content-Length
	const size_t I2P_CONTROL_MAX_CONNECTIONS = 64;
	const size_t I2P_CONTROL_MAX_TOKENS = 128;
	const size_t I2P_CONTROL_MAX_AUTHENTICATIONS_PER_MINUTE = 32;
	const std::chrono::seconds I2P_CONTROL_OPERATION_TIMEOUT { 15 };
	constexpr std::string_view I2P_CONTROL_DEFAULT_PASSWORD { "itoopie" };

	const uint64_t I2P_CONTROL_TOKEN_LIFETIME = 60 * 60; // 1 hour, in seconds
	const long I2P_CONTROL_CERTIFICATE_VALIDITY = 365 * 10; // 10 years
	const char I2P_CONTROL_CERTIFICATE_COMMON_NAME[] = "i2pd.i2pcontrol";
	const char I2P_CONTROL_CERTIFICATE_ORGANIZATION[] = "openi2pd";

	class I2PControlService : public I2PControlHandlers
	{
	public:
		I2PControlService (const std::string & address, int port);
		~I2PControlService ();

		void Start ();
		void Stop ();

	private:
		struct ConnectionRegistry
		{
			std::mutex mutex;
			std::map<uint64_t, std::function<void ()>> close;
			std::atomic_size_t active { 0 };
			uint64_t nextID { 0 };
		};

		void Run ();
		void Accept ();
		template <typename ssl_socket>
		void HandleAccepted (const boost::system::error_code & ecode, std::shared_ptr<ssl_socket> newSocket);
		template <typename ssl_socket> void Handshake (std::shared_ptr<ssl_socket> socket,
			std::shared_ptr<boost::asio::steady_timer> deadline, std::shared_ptr<void> connection);
		template <typename ssl_socket> void ReadRequest (std::shared_ptr<ssl_socket> socket,
			std::shared_ptr<boost::asio::steady_timer> deadline, std::shared_ptr<void> connection);
		template <typename ssl_socket>
		void HandleRequestReceived (const boost::system::error_code & ecode, size_t bytes_transferred,
			std::shared_ptr<ssl_socket> socket, std::shared_ptr<I2PControlBuffer> buf,
			std::shared_ptr<boost::asio::steady_timer> deadline, std::shared_ptr<void> connection);
		template <typename ssl_socket> void ProcessRequest (std::shared_ptr<ssl_socket> socket,
			std::shared_ptr<std::string> request, bool isHtml, std::shared_ptr<boost::asio::steady_timer> deadline,
			std::shared_ptr<void> connection);
		template <typename ssl_socket> void HandleHttpRequest (std::shared_ptr<ssl_socket> socket,
			std::shared_ptr<std::string> request, std::shared_ptr<boost::asio::steady_timer> deadline,
			std::shared_ptr<void> connection);
		template <typename ssl_socket>
		void SendResponse (std::shared_ptr<ssl_socket> socket, std::ostringstream & response, bool isHtml,
			std::shared_ptr<boost::asio::steady_timer> deadline, std::shared_ptr<void> connection);

		void CreateCertificate (const char * crt_path, const char * key_path);

	private:
		// methods
		typedef void (I2PControlService::*MethodHandler) (
			const boost::property_tree::ptree & params, std::ostringstream & results);

		void AuthenticateHandler (const boost::property_tree::ptree & params, std::ostringstream & results);
		void EchoHandler (const boost::property_tree::ptree & params, std::ostringstream & results);
		void I2PControlHandler (const boost::property_tree::ptree & params, std::ostringstream & results);
		void RouterManagerHandler (const boost::property_tree::ptree & params, std::ostringstream & results);

		// I2PControl
		typedef void (I2PControlService::*I2PControlRequestHandler) (const std::string & value);
		void PasswordHandler (const std::string & value);

		// RouterManager
		typedef void (I2PControlService::*RouterManagerRequestHandler) (std::ostringstream & results);
		void ShutdownHandler (std::ostringstream & results);
		void ShutdownGracefulHandler (std::ostringstream & results);
		void ReseedHandler (std::ostringstream & results);

	private:
		std::string m_Password;
		std::atomic<bool> m_IsRunning;
		std::unique_ptr<std::thread> m_Thread;

		boost::asio::io_context m_Service;
		std::unique_ptr<boost::asio::ip::tcp::acceptor> m_Acceptor;
#if defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)
		std::unique_ptr<boost::asio::local::stream_protocol::acceptor> m_LocalAcceptor;
#endif
		boost::asio::ssl::context m_SSLContext;
		boost::asio::steady_timer m_ShutdownTimer;
		std::map<std::string, uint64_t> m_Tokens; // token -> expiration time in seconds since epoch
		uint64_t m_AuthenticationWindowStart { 0 };
		size_t m_AuthenticationsInWindow { 0 };
		std::shared_ptr<ConnectionRegistry> m_Connections { std::make_shared<ConnectionRegistry> () };
		void PurgeExpiredTokens (uint64_t now);
		bool IsValidToken (const std::string & token);

		std::map<std::string, MethodHandler> m_MethodHandlers;
		std::map<std::string, I2PControlRequestHandler> m_I2PControlHandlers;
		std::map<std::string, RouterManagerRequestHandler> m_RouterManagerHandlers;
	};
}
}

#endif
