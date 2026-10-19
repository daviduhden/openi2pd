/*
* Copyright (c) 2026, David Uhden Collado
* SPDX-License-Identifier: BSD-3-Clause
*
* This file is part of openi2pd and licensed under BSD-3-Clause
*
* See full license text in LICENSE file at top of project tree
*/

#ifndef I2PD_TUI_CLIENT_H__
#define I2PD_TUI_CLIENT_H__

#include "Expected.h"
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
