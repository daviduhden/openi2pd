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
#include <stdexcept>
#include <string>
#include <string_view>
#include <sstream>
#include <map>
#include <set>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/property_tree/ptree.hpp>
#if defined(BOOST_ASIO_HAS_LOCAL_SOCKETS) && !defined(_WIN32)
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif
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

	/**
	 * A method handler failure that must reach the client as a JSON-RPC error
	 * object instead of an empty successful result.
	 *
	 * Handlers throw before writing any member, so the dispatcher can discard
	 * the partial result and publish the exact code and message. The message is
	 * never built from a password or token.
	 */
	class I2PControlError : public std::runtime_error
	{
	public:
		I2PControlError (int code, const std::string & message)
			: std::runtime_error (message)
			, m_Code (code)
		{
		}
		int GetCode () const { return m_Code; }

	private:
		int m_Code;
	};

#if defined(BOOST_ASIO_HAS_LOCAL_SOCKETS) && !defined(_WIN32)
	/*
	 * AF_UNIX listener path hygiene.
	 *
	 * These helpers are header-only so the compile-free socket regression can
	 * exercise the real policy without linking the daemon: only an owned stale
	 * socket is replaced, the path is restricted to 0600 after bind, the path
	 * is removed only while it is still the socket this process created, and a
	 * connected peer must belong to this user.
	 */
	namespace localcontrol
	{
		struct SocketIdentity
		{
			dev_t device { 0 };
			ino_t inode { 0 };
			bool valid { false };
		};

		inline bool PrepareSocketPath (const std::string & path, std::string & error)
		{
			struct stat status { };
			if (lstat (path.c_str (), &status) != 0)
			{
				if (errno == ENOENT) return true;
				error = std::strerror (errno);
				return false;
			}
			if (!S_ISSOCK (status.st_mode) || status.st_uid != geteuid ())
			{
				error = "not a socket owned by this user";
				return false;
			}
			if (unlink (path.c_str ()) != 0 && errno != ENOENT)
			{
				error = std::strerror (errno);
				return false;
			}
			return true;
		}

		inline SocketIdentity IdentifySocketPath (const std::string & path)
		{
			SocketIdentity identity;
			struct stat status { };
			if (lstat (path.c_str (), &status) == 0)
			{
				identity.device = status.st_dev;
				identity.inode = status.st_ino;
				identity.valid = true;
			}
			return identity;
		}

		inline bool RestrictSocketPath (const std::string & path, std::string & error)
		{
			if (chmod (path.c_str (), S_IRUSR | S_IWUSR) != 0)
			{
				error = std::strerror (errno);
				return false;
			}
			return true;
		}

		inline bool RemoveSocketPath (const std::string & path, const SocketIdentity & identity)
		{
			if (!identity.valid) return false;
			struct stat status { };
			if (lstat (path.c_str (), &status) != 0) return false;
			if (!S_ISSOCK (status.st_mode) || status.st_dev != identity.device || status.st_ino != identity.inode)
				return false;
			return unlink (path.c_str ()) == 0;
		}

		inline bool IsSameUserPeer (int fd)
		{
#if defined(__linux__)
			struct ucred credentials { };
			socklen_t length = sizeof (credentials);
			if (getsockopt (fd, SOL_SOCKET, SO_PEERCRED, &credentials, &length) != 0) return false;
			return credentials.uid == geteuid ();
#elif defined(__OpenBSD__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__APPLE__)
			uid_t uid = 0;
			gid_t gid = 0;
			if (getpeereid (fd, &uid, &gid) != 0) return false;
			return uid == geteuid ();
#else
			(void)fd;
			return false; // no peer-credential call: fail closed
#endif
		}

#if defined(__linux__) || defined(__OpenBSD__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__APPLE__)
		inline constexpr bool HAS_PEER_CREDENTIALS = true;
#else
		inline constexpr bool HAS_PEER_CREDENTIALS = false;
#endif
	}
#endif

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
			std::map<uint64_t, std::weak_ptr<boost::asio::steady_timer>> deadlines;
			std::atomic_size_t active { 0 };
			uint64_t nextID { 0 };
		};

		// Outcome of presenting a session token. Missing, unknown and expired
		// are distinct protocol errors, so the distinction is kept here.
		enum class TokenCheck
		{
			Valid = 0,
			Missing,
			Unknown,
			Expired
		};

		void Run ();
		void Accept ();
		template <typename ssl_socket>
		void HandleAccepted (const boost::system::error_code & ecode, std::shared_ptr<ssl_socket> newSocket);
		template <typename ssl_socket>
		void Handshake (std::shared_ptr<ssl_socket> socket, std::shared_ptr<boost::asio::steady_timer> deadline,
			std::shared_ptr<void> connection);
		template <typename ssl_socket>
		void ReadRequest (std::shared_ptr<ssl_socket> socket, std::shared_ptr<boost::asio::steady_timer> deadline,
			std::shared_ptr<void> connection);
		template <typename ssl_socket>
		void HandleRequestReceived (const boost::system::error_code & ecode, size_t bytes_transferred,
			std::shared_ptr<ssl_socket> socket, std::shared_ptr<I2PControlBuffer> buf,
			std::shared_ptr<boost::asio::steady_timer> deadline, std::shared_ptr<void> connection);
		template <typename ssl_socket>
		void ProcessRequest (std::shared_ptr<ssl_socket> socket, std::shared_ptr<std::string> request, bool isHtml,
			std::shared_ptr<boost::asio::steady_timer> deadline, std::shared_ptr<void> connection);
		template <typename ssl_socket>
		void HandleHttpRequest (std::shared_ptr<ssl_socket> socket, std::shared_ptr<std::string> request,
			std::shared_ptr<boost::asio::steady_timer> deadline, std::shared_ptr<void> connection);
		template <typename ssl_socket>
		void SendResponse (std::shared_ptr<ssl_socket> socket, std::ostringstream & response, bool isHtml,
			std::shared_ptr<boost::asio::steady_timer> deadline, std::shared_ptr<void> connection);

		void CreateCertificate (const char * crt_path, const char * key_path);
#if defined(BOOST_ASIO_HAS_LOCAL_SOCKETS) && !defined(_WIN32)
		// Removes the configured path only while it is still the socket this
		// process created.
		void RemoveLocalControlSocket (const std::string & path);
#endif

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
		TokenCheck CheckToken (const std::string & token);
#if defined(BOOST_ASIO_HAS_LOCAL_SOCKETS) && !defined(_WIN32)
		// Identity of the AF_UNIX socket this process created. Stop() removes
		// the path only while it still matches, so a replaced path survives.
		dev_t m_LocalSocketDevice { 0 };
		ino_t m_LocalSocketInode { 0 };
		bool m_LocalSocketIdentity { false };
#endif

		std::map<std::string, MethodHandler> m_MethodHandlers;
		std::map<std::string, I2PControlRequestHandler> m_I2PControlHandlers;
		std::map<std::string, RouterManagerRequestHandler> m_RouterManagerHandlers;
	};
}
}

#endif
