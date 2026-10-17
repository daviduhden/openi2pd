/*
* Copyright (c) 2013-2026, The PurpleI2P Project
*
* This file is part of Open I2Pd and licensed under BSD3
*
* See full license text in LICENSE file at top of project tree
*/

#ifndef I2PD_TUI_CLIENT_H__
#define I2PD_TUI_CLIENT_H__

#ifndef _WIN32

#include <expected>
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
	std::expected<ClientOptions, std::string> ParseOptions (int argc, char* argv[]);

	/** Resolve the socket path from --socket, the config file, or the data dir. */
	std::expected<ClientOptions, std::string> ResolveSocket (ClientOptions options);

	class Client
	{
		public:

			explicit Client (std::string socketPath);
			~Client ();

			Client (const Client&) = delete;
			Client& operator= (const Client&) = delete;

			bool IsConnected () const { return m_Fd >= 0; }
			std::expected<void, std::string> Connect ();
			void Disconnect ();

			/**
			 * Send one command and return its response payload. On protocol or
			 * transport errors the connection is closed and an error is
			 * returned; the caller may reconnect and retry.
			 */
			std::expected<std::string, std::string> Request (std::string_view command);

		private:

			std::expected<std::string, std::string> ReadLine (size_t limit);
			std::expected<std::string, std::string> ReadExact (size_t size);

			std::string m_Path;
			int m_Fd{-1};
	};
}
}

#endif // _WIN32

#endif // I2PD_TUI_CLIENT_H__
