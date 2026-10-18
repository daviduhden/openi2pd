/*
* Copyright (c) 2013-2026, The PurpleI2P Project
* Copyright (c) 2026, David Uhden Collado
* SPDX-License-Identifier: BSD-3-Clause
*
* This file is part of openi2pd and licensed under BSD-3-Clause
*
* See full license text in LICENSE file at top of project tree
*/

#include "TuiClient.h"

#ifndef _WIN32

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <utility>

#include "TuiProtocol.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace i2p
{
namespace tui
{
	namespace
	{
		std::string Trim (std::string_view value)
		{
			while (!value.empty () && (value.front () == ' ' || value.front () == '\t')) value.remove_prefix (1);
			while (!value.empty () && (value.back () == ' ' || value.back () == '\t' || value.back () == '\r'))
				value.remove_suffix (1);
			return std::string (value);
		}

		i2p::expected<std::pair<std::string, std::string>, std::string> ReadConfigValues (const std::string & path)
		{
			std::ifstream config (path);
			if (!config) return i2p::unexpected ("can't open " + path);

			std::string datadir;
			std::string socket;
			bool inTui = false;
			std::string line;
			while (std::getline (config, line))
			{
				auto comment = line.find ('#');
				if (comment != std::string::npos) line.resize (comment);
				line = Trim (line);
				if (line.empty ()) continue;
				if (line.front () == '[' && line.back () == ']')
				{
					inTui = Trim (std::string_view (line).substr (1, line.size () - 2)) == "tui";
					continue;
				}
				auto equals = line.find ('=');
				if (equals == std::string::npos) continue;
				auto key = Trim (std::string_view (line).substr (0, equals));
				auto value = Trim (std::string_view (line).substr (equals + 1));
				if (key == "datadir" && datadir.empty ())
					datadir = value;
				else if (inTui && key == "socket")
					socket = value;
			}
			if (config.bad ()) return i2p::unexpected ("can't read " + path);
			return std::pair { datadir, socket };
		}

		std::string DefaultDataDir (bool service)
		{
			if (service)
			{
#if defined(__OpenBSD__)
				return "/var/i2pd";
#else
				return "/var/lib/i2pd";
#endif
			}
			const char * home = std::getenv ("HOME");
			if (home && *home) return std::string (home) + "/.i2pd";
			return ".i2pd";
		}

		std::string AbsolutePath (const std::string & path)
		{
			if (!path.empty () && path.front () == '/') return path;
			char cwd[4096];
			if (getcwd (cwd, sizeof (cwd))) return std::string (cwd) + "/" + path;
			return path;
		}
	}

	i2p::expected<ClientOptions, std::string> ParseOptions (int argc, char * argv[])
	{
		ClientOptions options;
		bool service = false;
		for (int i = 1; i < argc; ++i)
		{
			std::string_view arg = argv[i];
			auto takeValue = [&] (std::string & target) -> bool
			{
				if (i + 1 >= argc) return false;
				target = argv[++i];
				return true;
			};
			if (arg == "--help" || arg == "-h")
				options.showHelp = true;
			else if (arg == "--version")
				options.showVersion = true;
			else if (arg == "--service")
				service = true;
			else if (arg == "--socket")
			{
				if (!takeValue (options.socketPath)) return i2p::unexpected ("--socket requires a path");
			}
			else if (arg.starts_with ("--socket="))
				options.socketPath = std::string (arg.substr (9));
			else if (arg == "--datadir")
			{
				if (!takeValue (options.dataDir)) return i2p::unexpected ("--datadir requires a path");
			}
			else if (arg.starts_with ("--datadir="))
				options.dataDir = std::string (arg.substr (10));
			else if (arg == "--conf" || arg == "--config")
			{
				if (!takeValue (options.configPath)) return i2p::unexpected ("--conf requires a path");
			}
			else if (arg.starts_with ("--conf="))
				options.configPath = std::string (arg.substr (7));
			else
				return i2p::unexpected ("unknown option: " + std::string (arg));
		}
		if (options.dataDir.empty ()) options.dataDir = DefaultDataDir (service);
		options.dataDir = AbsolutePath (options.dataDir);
		return options;
	}

	i2p::expected<ClientOptions, std::string> ResolveSocket (ClientOptions options)
	{
		if (!options.socketPath.empty ())
		{
			options.socketPath = AbsolutePath (options.socketPath);
			return options;
		}

		if (options.configPath.empty ())
			options.configPath = options.dataDir + "/i2pd.conf";
		else
			options.configPath = AbsolutePath (options.configPath);

		std::string configSocket;
		struct stat st { };
		if (stat (options.configPath.c_str (), &st) == 0)
		{
			auto values = ReadConfigValues (options.configPath);
			if (!values) return i2p::unexpected (values.error ());
			if (!values->first.empty ()) options.dataDir = AbsolutePath (values->first);
			configSocket = values->second;
		}

		if (!configSocket.empty ())
			options.socketPath = configSocket.front () == '/' ? configSocket : options.dataDir + "/" + configSocket;
		else
			options.socketPath = options.dataDir + "/i2pd-tui.sock";
		return options;
	}

	Client::Client (std::string socketPath)
		: m_Path (std::move (socketPath))
	{
	}

	Client::~Client ()
	{
		Disconnect ();
	}

	i2p::expected<void, std::string> Client::Connect ()
	{
		Disconnect ();
		if (m_Path.size () >= sizeof (sockaddr_un { }.sun_path)) return i2p::unexpected ("socket path is too long");

		int fd = ::socket (AF_UNIX, SOCK_STREAM, 0);
		if (fd < 0) return i2p::unexpected (std::string ("socket: ") + strerror (errno));
		int flags = fcntl (fd, F_GETFD);
		if (flags >= 0) fcntl (fd, F_SETFD, flags | FD_CLOEXEC);

		sockaddr_un address { };
		address.sun_family = AF_UNIX;
		std::memcpy (address.sun_path, m_Path.c_str (), m_Path.size () + 1);
		if (::connect (fd, reinterpret_cast<sockaddr *> (&address), sizeof (address)) != 0)
		{
			int error = errno;
			close (fd);
			return i2p::unexpected (std::string ("connect: ") + strerror (error));
		}

		timeval timeout { 3, 0 };
		setsockopt (fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof (timeout));
		setsockopt (fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof (timeout));
		m_Fd = fd;
		return { };
	}

	void Client::Disconnect ()
	{
		if (m_Fd >= 0)
		{
			close (m_Fd);
			m_Fd = -1;
		}
	}

	i2p::expected<std::string, std::string> Client::ReadLine (size_t limit)
	{
		std::string line;
		line.reserve (std::min (limit, size_t (128)));
		while (line.size () < limit)
		{
			char ch = 0;
			auto received = ::recv (m_Fd, &ch, 1, 0);
			if (received < 0)
			{
				if (errno == EINTR) continue;
				return i2p::unexpected (std::string ("recv: ") + strerror (errno));
			}
			if (received == 0) return i2p::unexpected ("connection closed");
			if (ch == '\n') return line;
			line.push_back (ch);
		}
		return i2p::unexpected ("response header too long");
	}

	i2p::expected<std::string, std::string> Client::ReadExact (size_t size)
	{
		std::string data (size, '\0');
		size_t offset = 0;
		while (offset < size)
		{
			auto received = ::recv (m_Fd, data.data () + offset, size - offset, 0);
			if (received < 0)
			{
				if (errno == EINTR) continue;
				return i2p::unexpected (std::string ("recv: ") + strerror (errno));
			}
			if (received == 0) return i2p::unexpected ("connection closed during response");
			offset += static_cast<size_t> (received);
		}
		return data;
	}

	i2p::expected<std::string, RequestError> Client::Request (std::string_view command)
	{
		if (m_Fd < 0) return i2p::unexpected (RequestError { RequestErrorKind::NotConnected, "not connected" });
		if (command.find ('\n') != std::string_view::npos || command.find ('\r') != std::string_view::npos)
			return i2p::unexpected (RequestError { RequestErrorKind::Protocol, "command contains a line break" });

		std::string request;
		request.reserve (command.size () + 10);
		request.append (PROTOCOL_NAME);
		request.push_back ('/');
		request.append (std::to_string (PROTOCOL_VERSION));
		request.push_back (' ');
		request.append (command);
		request.push_back ('\n');

		size_t offset = 0;
		while (offset < request.size ())
		{
			auto sent = ::send (m_Fd, request.data () + offset, request.size () - offset, MSG_NOSIGNAL);
			if (sent < 0)
			{
				if (errno == EINTR) continue;
				int error = errno;
				Disconnect ();
				return i2p::unexpected (
					RequestError { RequestErrorKind::Transport, std::string ("send: ") + strerror (error) });
			}
			if (!sent)
			{
				Disconnect ();
				return i2p::unexpected (
					RequestError { RequestErrorKind::Transport, "connection closed during request" });
			}
			offset += static_cast<size_t> (sent);
		}

		auto headerLine = ReadLine (128);
		if (!headerLine)
		{
			Disconnect ();
			auto kind = headerLine.error ().find ("too long") != std::string::npos ? RequestErrorKind::Protocol
																				   : RequestErrorKind::Transport;
			return i2p::unexpected (RequestError { kind, headerLine.error () });
		}
		auto header = ParseResponseHeader (*headerLine);
		if (!header)
		{
			Disconnect ();
			return i2p::unexpected (RequestError {
				RequestErrorKind::Protocol, std::string ("protocol error: ") + std::string (header.error ().message) });
		}
		auto payload = ReadExact (header->payloadSize);
		if (!payload)
		{
			Disconnect ();
			return i2p::unexpected (RequestError { RequestErrorKind::Transport, payload.error () });
		}
		if (header->status == ResponseStatus::Error)
			return i2p::unexpected (RequestError { RequestErrorKind::Remote, std::move (*payload) });
		return std::move (*payload);
	}
}
}

#endif // _WIN32
