/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TuiClient.
 *
 * Declares the local IPC client used by i2pd-tui, including
 * command-line parsing, endpoint resolution and a framed request API.
 * Requests return either a payload or a typed transport, protocol or
 * remote error so callers can distinguish recoverable daemon errors
 * from a broken connection.
 */

#ifndef I2PD_TUI_CLIENT_H__
#define I2PD_TUI_CLIENT_H__

#include "Expected.h"
#include "TuiProtocol.h"
#include <memory>
#include <string>
#include <string_view>

namespace i2p
{
namespace tui
{
	struct ClientOptions
	{
		std::string socketPath;
		std::string dataDir;
		std::string configPath;
		std::string language;
		bool showHelp = false;
		bool showVersion = false;
	};

	/**
	 * Parse command line options without depending on the router's
	 * config code. */
	i2p::expected<ClientOptions, std::string> ParseOptions (int argc, char * argv[]);

	/**
	 * Resolve the local IPC endpoint from --socket, the config file, or
	 * the data dir. */
	i2p::expected<ClientOptions, std::string> ResolveSocket (ClientOptions options);

	/// @brief Failure classification for one request.
	///
	/// The distinction matters to callers: Remote means the daemon
	/// understood the verb and rejected it, so the connection is still
	/// usable and the UI can show a status message.
	/// Transport/Protocol/NotConnected mean the channel itself is unusable
	/// and the caller must reconnect before the next request.
	enum class RequestErrorKind
	{
		NotConnected,
		Transport,
		Protocol,
		Remote
	};

	struct RequestError
	{
		RequestErrorKind kind;
		std::string message;
		ErrorCode code = ErrorCode::None;
	};

	/// @brief Single-threaded synchronous client for the local TUI
	/// endpoint.
	/// One request may be in flight at a time: the wire protocol has no
	/// request ids, so a caller must not issue a second command before the
	/// previous reply was consumed. The TUI event loop is the only caller,
	/// which is why no internal locking is needed.
	///
	/// Connect resolves a Unix socket (POSIX) or the derived local named
	/// pipe (Windows). Both platforms apply a bounded connect and
	/// per-request deadline; on Windows the server process identity is
	/// additionally verified before the first command.
	///
	/// @warning Not copyable and not safe to share across threads.
	class Client
	{
	public:
		explicit Client (std::string socketPath);
		~Client ();

		Client (const Client &) = delete;
		Client & operator= (const Client &) = delete;

		bool IsConnected () const;
		i2p::expected<void, std::string> Connect ();
		void Disconnect ();

		/**
			 * Send one command and return its response payload. Transport and
			 * protocol failures close the connection and report the matching
			 * kind. A well-formed remote error response keeps the connection
			 * usable and is reported as RequestErrorKind::Remote.
			 */
		i2p::expected<std::string, RequestError> Request (std::string_view command);

	private:
		class Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}
}

#endif // I2PD_TUI_CLIENT_H__
