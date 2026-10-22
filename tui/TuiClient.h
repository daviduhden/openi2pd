/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TuiClient.
 *
 * Declares the local IPC client used by i2pd-tui, including command-line
 * parsing, endpoint resolution and a framed request API. Requests return
 * either a payload or a typed transport, protocol or remote error so callers
 * can distinguish recoverable daemon errors from a broken connection.
 *
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

	/** Parse command line options without depending on the router's config code. */
	i2p::expected<ClientOptions, std::string> ParseOptions (int argc, char * argv[]);

	/** Resolve the local IPC endpoint from --socket, the config file, or the data dir. */
	i2p::expected<ClientOptions, std::string> ResolveSocket (ClientOptions options);

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
