/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TuiClient.
 *
 * Implements the Unix local-IPC client: resolves the socket from
 * options or i2pd.conf, connects with bounded send and receive
 * timeouts, and exchanges versioned, length-framed TUI protocol
 * requests. Transport and protocol failures close the connection, while
 * a well-formed remote error leaves it usable.
 *
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

		struct ConfigValues
		{
			std::string datadir;
			std::string socket;
			std::string language;
		};

		// Minimal reader for the shared i2pd.conf: only "datadir" (any
		// section, first value wins) and the [tui] "socket"/"language" keys,
		// because the console links neither the router nor its configuration
		// parser. Unknown keys and sections are ignored rather than rejected,
		// and a file that does not exist is skipped by the caller before this
		// reader runs, so its defaults survive. '#' always starts a comment,
		// so a value containing '#' (a path or a URL fragment) is truncated
		// at the first one.
		i2p::expected<ConfigValues, std::string> ReadConfigValues (const std::string & path)
		{
			std::ifstream config (path);
			if (!config) return i2p::unexpected ("can't open " + path);

			ConfigValues values;
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
				if (key == "datadir" && values.datadir.empty ())
					values.datadir = value;
				else if (inTui && key == "socket")
					values.socket = value;
				else if (inTui && (key == "language" || key == "lang"))
					values.language = value;
			}
			if (config.bad ()) return i2p::unexpected ("can't read " + path);
			return values;
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
			else if (arg == "--lang")
			{
				if (!takeValue (options.language)) return i2p::unexpected ("--lang requires a language");
			}
			else if (arg.starts_with ("--lang="))
				options.language = std::string (arg.substr (7));
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
			if (options.language.empty ())
			{
				auto configPath
					= options.configPath.empty () ? options.dataDir + "/i2pd.conf" : AbsolutePath (options.configPath);
				struct stat st { };
				if (stat (configPath.c_str (), &st) == 0)
				{
					auto values = ReadConfigValues (configPath);
					if (values) options.language = values->language;
				}
			}
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
			if (!values->datadir.empty ()) options.dataDir = AbsolutePath (values->datadir);
			configSocket = values->socket;
			if (options.language.empty ()) options.language = values->language;
		}

		// A relative [tui] socket is resolved against the effective data
		// directory, never against the process working directory, so console
		// and daemon agree even when started from different directories.
		// --socket still reads the file, but only to pick up a missing
		// language.
		if (!configSocket.empty ())
			options.socketPath = configSocket.front () == '/' ? configSocket : options.dataDir + "/" + configSocket;
		else
			options.socketPath = options.dataDir + "/i2pd-tui.sock";
		return options;
	}

	class Client::Impl
	{
	public:
		explicit Impl (std::string socketPath)
			: m_Path (std::move (socketPath))
		{
		}

		~Impl () { Disconnect (); }

		Impl (const Impl &) = delete;
		Impl & operator= (const Impl &) = delete;

		bool IsConnected () const { return m_Fd >= 0; }
		i2p::expected<void, std::string> Connect ();
		void Disconnect ();

		i2p::expected<std::string, RequestError> Request (std::string_view command);

	private:
		i2p::expected<std::string, std::string> ReadLine (size_t limit);
		i2p::expected<std::string, std::string> ReadExact (size_t size);

		std::string m_Path;
		int m_Fd { -1 };
	};

	i2p::expected<void, std::string> Client::Impl::Connect ()
	{
		// One attempt, no retry loop: App::ConnectIfNeeded owns the reconnect
		// cadence (two seconds while disconnected) and keeps the failure text
		// on screen, so retrying here would hide a missing daemon behind a
		// blocked event loop. AF_UNIX connect() is blocking and has no
		// timeout of its own, so a daemon that stops accepting can stall this
		// call until the address queue drains; nothing here can interrupt it.
		//
		// A path that does not fit sun_path (including the terminating NUL)
		// is rejected before the socket exists rather than truncated.
		// FD_CLOEXEC keeps the descriptor out of any child the console might
		// start.
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
		// Per-recv/send deadlines, installed only after a successful connect.
		// They bound every later call individually, so a stalled daemon turns
		// into an EAGAIN that the request path reports as a transport failure
		// (and disconnects, because the stream position is then unknown)
		// rather than a console that never returns.
		setsockopt (fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof (timeout));
		setsockopt (fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof (timeout));
		m_Fd = fd;
		return { };
	}

	void Client::Impl::Disconnect ()
	{
		if (m_Fd >= 0)
		{
			close (m_Fd);
			m_Fd = -1;
		}
	}

	i2p::expected<std::string, std::string> Client::Impl::ReadLine (size_t limit)
	{
		// One byte at a time so the payload that follows the header stays in
		// the socket buffer: over-reading would consume the next frame and
		// leave the connection unusable even after a well-formed remote
		// error.
		// The loop counts characters before LF, while the codec treats 128
		// bytes as an inclusive response-header bound, so the client's
		// effective limit is one byte smaller than the protocol allows.
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

	i2p::expected<std::string, std::string> Client::Impl::ReadExact (size_t size)
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

	i2p::expected<std::string, RequestError> Client::Impl::Request (std::string_view command)
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
		// MSG_NOSIGNAL keeps a vanished daemon from raising SIGPIPE; the
		// partial-write loop exists because a UNIX stream socket may accept
		// fewer bytes than requested even for a short request line.
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
			return i2p::unexpected (RequestError { RequestErrorKind::Remote, std::move (*payload), header->errorCode });
		return std::move (*payload);
	}

	Client::Client (std::string socketPath)
		: m_Impl (std::make_unique<Impl> (std::move (socketPath)))
	{
	}

	Client::~Client () = default;

	bool Client::IsConnected () const
	{
		return m_Impl && m_Impl->IsConnected ();
	}

	i2p::expected<void, std::string> Client::Connect ()
	{
		return m_Impl ? m_Impl->Connect () : i2p::unexpected ("client is not initialized");
	}

	void Client::Disconnect ()
	{
		if (m_Impl) m_Impl->Disconnect ();
	}

	i2p::expected<std::string, RequestError> Client::Request (std::string_view command)
	{
		if (!m_Impl) return i2p::unexpected (RequestError { RequestErrorKind::NotConnected, "not connected" });
		return m_Impl->Request (command);
	}
}
}

#endif // _WIN32
