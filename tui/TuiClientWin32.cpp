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

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "TuiProtocol.h"
#include "TuiWin32Ipc.h"

namespace i2p
{
namespace tui
{
	namespace
	{
		constexpr DWORD CLIENT_IO_TIMEOUT_MS = 3000;
		constexpr DWORD CLIENT_CONNECT_TIMEOUT_MS = 3000;

		struct LocalFreeDeleter
		{
			void operator() (void * pointer) const
			{
				if (pointer) LocalFree (pointer);
			}
		};
		using LocalPtr = std::unique_ptr<void, LocalFreeDeleter>;

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
				const char * programData = std::getenv ("PROGRAMDATA");
				if (programData && *programData) return std::string (programData) + "\\i2pd";
				return "C:\\ProgramData\\i2pd";
			}

			// Match the router's FS::DetectDataDir(): an i2pd.conf next to
			// the executable selects the executable directory; otherwise the
			// per-user roaming AppData directory is used.  In particular, do
			// not use LOCALAPPDATA here: the daemon uses CSIDL_APPDATA.
			std::wstring moduleName (32768, L'\0');
			DWORD moduleLength
				= GetModuleFileNameW (nullptr, moduleName.data (), static_cast<DWORD> (moduleName.size ()));
			if (moduleLength && moduleLength < moduleName.size ())
			{
				moduleName.resize (moduleLength);
				std::filesystem::path executableDirectory = std::filesystem::path (moduleName).parent_path ();
				std::error_code error;
				if (std::filesystem::exists (executableDirectory / L"i2pd.conf", error) && !error)
					return executableDirectory.string ();
			}

			const char * appData = std::getenv ("APPDATA");
			if (appData && *appData) return std::string (appData) + "\\i2pd";

			const char * profile = std::getenv ("USERPROFILE");
			if (profile && *profile) return std::string (profile) + "\\AppData\\Roaming\\i2pd";
			return "i2pd";
		}

		std::string AbsolutePath (const std::string & path)
		{
			std::filesystem::path value (path);
			if (value.is_absolute ()) return value.lexically_normal ().string ();
			std::error_code error;
			auto absolute = std::filesystem::absolute (value, error);
			if (error) return path;
			return absolute.lexically_normal ().string ();
		}

		std::string JoinPath (const std::string & directory, const std::string & leaf)
		{
			if (leaf.empty ()) return directory;
			if (std::filesystem::path (leaf).is_absolute ()) return leaf;
			if (directory.empty ()) return leaf;
			const char last = directory.back ();
			if (last == '\\' || last == '/') return directory + leaf;
			return directory + "\\" + leaf;
		}

		std::string Win32ErrorText (DWORD error)
		{
			char * message = nullptr;
			DWORD length = FormatMessageA (
				FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
				error, MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPSTR> (&message), 0, nullptr);
			if (!length || !message) return "Windows error " + std::to_string (error);

			std::string text (message, length);
			LocalFree (message);
			while (!text.empty () && (text.back () == '\r' || text.back () == '\n' || text.back () == ' '))
				text.pop_back ();
			return text;
		}

		std::string ToUtf8 (const std::wstring & text)
		{
			if (text.empty ()) return { };
			int size = WideCharToMultiByte (
				CP_UTF8, 0, text.data (), static_cast<int> (text.size ()), nullptr, 0, nullptr, nullptr);
			if (size <= 0) return { };
			std::string result (static_cast<size_t> (size), '\0');
			WideCharToMultiByte (
				CP_UTF8, 0, text.data (), static_cast<int> (text.size ()), result.data (), size, nullptr, nullptr);
			return result;
		}

		bool GetTokenUserSid (HANDLE token, LocalPtr & sid)
		{
			DWORD size = 0;
			GetTokenInformation (token, TokenUser, nullptr, 0, &size);
			if (size == 0) return false;

			LocalPtr buffer (LocalAlloc (LPTR, size));
			if (!buffer) return false;
			if (!GetTokenInformation (token, TokenUser, buffer.get (), size, &size)) return false;

			PSID tokenSid = static_cast<TOKEN_USER *> (buffer.get ())->User.Sid;
			const DWORD sidSize = GetLengthSid (tokenSid);
			LocalPtr copy (LocalAlloc (LPTR, sidSize));
			if (!copy || !CopySid (sidSize, copy.get (), tokenSid)) return false;
			sid = std::move (copy);
			return true;
		}

		/**
		 * Verify the connected pipe server as far as Win32 makes practical.
		 *
		 * GetNamedPipeServerProcessId reports the process that owns the
		 * connected instance, not a name supplied by the peer. We require
		 * that process image to be `i2pd.exe` and its token to be the
		 * current user, SYSTEM, or a member of Administrators, matching the
		 * pipe DACL policy. This is defence in depth against a same-name
		 * squatter; the DACL remains the authorization boundary. It does
		 * not prove binary provenance, so an attacker already running as an
		 * authorized user can still provide a matching executable name.
		 */
		bool VerifyServer (HANDLE pipe, std::string & error)
		{
			ULONG serverPid = 0;
			if (!GetNamedPipeServerProcessId (pipe, &serverPid))
			{
				error = "can't identify the pipe server process: " + Win32ErrorText (GetLastError ());
				return false;
			}

			HANDLE process = OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, FALSE, serverPid);
			if (!process)
			{
				error = "can't open the pipe server process: " + Win32ErrorText (GetLastError ());
				return false;
			}

			std::wstring imagePath (32768, L'\0');
			DWORD imageSize = static_cast<DWORD> (imagePath.size ());
			if (!QueryFullProcessImageNameW (process, 0, imagePath.data (), &imageSize))
			{
				error = "can't query the pipe server image: " + Win32ErrorText (GetLastError ());
				CloseHandle (process);
				return false;
			}
			imagePath.resize (imageSize);
			auto separator = imagePath.find_last_of (L"\\/");
			std::wstring image = separator == std::wstring::npos ? imagePath : imagePath.substr (separator + 1);
			for (auto & character : image)
				if (character >= L'A' && character <= L'Z') character = static_cast<wchar_t> (character - L'A' + L'a');
			if (image != L"i2pd.exe")
			{
				error = "unexpected pipe server image: " + ToUtf8 (imagePath);
				CloseHandle (process);
				return false;
			}

			HANDLE serverToken = nullptr;
			HANDLE ownToken = nullptr;
			bool allowed = false;
			LocalPtr serverSid;
			LocalPtr ownSid;
			if (!OpenProcessToken (process, TOKEN_QUERY, &serverToken))
				error = "can't open the pipe server token: " + Win32ErrorText (GetLastError ());
			else if (!OpenProcessToken (GetCurrentProcess (), TOKEN_QUERY, &ownToken))
				error = "can't open the local process token: " + Win32ErrorText (GetLastError ());
			else if (!GetTokenUserSid (serverToken, serverSid) || !GetTokenUserSid (ownToken, ownSid))
				error = "can't read the pipe server user SID";
			else
			{
				allowed = EqualSid (serverSid.get (), ownSid.get ()) == TRUE;

				if (!allowed)
				{
					std::array<BYTE, SECURITY_MAX_SID_SIZE> systemSid { };
					DWORD systemSidSize = static_cast<DWORD> (systemSid.size ());
					if (CreateWellKnownSid (WinLocalSystemSid, nullptr, systemSid.data (), &systemSidSize)
						&& EqualSid (serverSid.get (), systemSid.data ()))
						allowed = true;
				}

				if (!allowed)
				{
					std::array<BYTE, SECURITY_MAX_SID_SIZE> administratorsSid { };
					DWORD administratorsSidSize = static_cast<DWORD> (administratorsSid.size ());
					if (!CreateWellKnownSid (
							WinBuiltinAdministratorsSid, nullptr, administratorsSid.data (), &administratorsSidSize))
						error = "can't create the Administrators SID: " + Win32ErrorText (GetLastError ());
					else
					{
						BOOL serverAdministrator = FALSE;
						BOOL localAdministrator = FALSE;
						if (!CheckTokenMembership (serverToken, administratorsSid.data (), &serverAdministrator))
							error = "can't check the pipe server group membership: " + Win32ErrorText (GetLastError ());
						else if (!CheckTokenMembership (ownToken, administratorsSid.data (), &localAdministrator))
							error = "can't check the local group membership: " + Win32ErrorText (GetLastError ());
						else if (serverAdministrator || localAdministrator)
							allowed = true;
					}
				}

				if (!allowed && error.empty ()) error = "the named pipe server runs as a different user";
			}

			if (serverToken) CloseHandle (serverToken);
			if (ownToken) CloseHandle (ownToken);
			CloseHandle (process);
			return allowed;
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
				if (!takeValue (options.socketPath)) return i2p::unexpected ("--socket requires an endpoint");
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
			else if (arg.starts_with ("--config="))
				options.configPath = std::string (arg.substr (9));
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
			options.configPath = JoinPath (options.dataDir, "i2pd.conf");
		else
			options.configPath = AbsolutePath (options.configPath);

		std::string configSocket;
		std::error_code error;
		if (std::filesystem::is_regular_file (options.configPath, error))
		{
			auto values = ReadConfigValues (options.configPath);
			if (!values) return i2p::unexpected (values.error ());
			if (!values->first.empty ()) options.dataDir = AbsolutePath (values->first);
			configSocket = values->second;
		}

		if (!configSocket.empty ())
			options.socketPath = JoinPath (options.dataDir, configSocket);
		else
			options.socketPath = JoinPath (options.dataDir, "i2pd-tui.sock");
		return options;
	}

	class Client::Impl
	{
	public:
		explicit Impl (std::string endpoint)
			: m_Endpoint (std::move (endpoint))
			, m_PipeName (MakeWin32PipeName (m_Endpoint))
		{
		}

		~Impl () { Disconnect (); }

		Impl (const Impl &) = delete;
		Impl & operator= (const Impl &) = delete;

		bool IsConnected () const { return m_Handle != INVALID_HANDLE_VALUE; }
		i2p::expected<void, std::string> Connect ();
		void Disconnect ();

		i2p::expected<std::string, RequestError> Request (std::string_view command);

	private:
		bool WaitForIo (OVERLAPPED & overlapped, DWORD timeoutMs, DWORD & transferred, std::string & error);
		bool WriteAll (std::string_view data, std::string & error);
		i2p::expected<std::string, std::string> ReadLine (size_t limit);
		i2p::expected<std::string, std::string> ReadExact (size_t size);

		std::string m_Endpoint;
		std::wstring m_PipeName;
		HANDLE m_Handle { INVALID_HANDLE_VALUE };
		HANDLE m_IoEvent { nullptr };
	};

	i2p::expected<void, std::string> Client::Impl::Connect ()
	{
		Disconnect ();
		if (m_Endpoint.empty ()) return i2p::unexpected ("local IPC endpoint is empty");
		if (m_PipeName.empty ()) m_PipeName = MakeWin32PipeName (m_Endpoint);

		const auto deadline = std::chrono::steady_clock::now () + std::chrono::milliseconds (CLIENT_CONNECT_TIMEOUT_MS);
		HANDLE handle = INVALID_HANDLE_VALUE;
		for (;;)
		{
			auto remaining
				= std::chrono::duration_cast<std::chrono::milliseconds> (deadline - std::chrono::steady_clock::now ())
					  .count ();
			if (remaining <= 0) return i2p::unexpected ("connect: timed out waiting for the local IPC endpoint");
			if (!WaitNamedPipeW (m_PipeName.c_str (), static_cast<DWORD> (remaining)))
			{
				DWORD error = GetLastError ();
				// The daemon closes its one instance after each client and
				// creates the next one immediately. A reconnect can observe
				// that short gap, so keep waiting within the deadline
				// instead of treating it as a permanent failure.
				if (error == ERROR_FILE_NOT_FOUND || error == ERROR_SEM_TIMEOUT || error == ERROR_PIPE_BUSY)
				{
					Sleep (50);
					continue;
				}
				return i2p::unexpected (std::string ("connect: ") + Win32ErrorText (error));
			}

			handle = CreateFileW (m_PipeName.c_str (), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
				FILE_FLAG_OVERLAPPED, nullptr);
			if (handle != INVALID_HANDLE_VALUE) break;

			DWORD error = GetLastError ();
			if (error != ERROR_PIPE_BUSY && error != ERROR_FILE_NOT_FOUND && error != ERROR_SEM_TIMEOUT)
				return i2p::unexpected (std::string ("connect: ") + Win32ErrorText (error));
			Sleep (50);
		}

		std::string verifyError;
		if (!VerifyServer (handle, verifyError))
		{
			CloseHandle (handle);
			return i2p::unexpected ("refusing local IPC server: " + verifyError);
		}

		HANDLE ioEvent = CreateEventW (nullptr, TRUE, FALSE, nullptr);
		if (!ioEvent)
		{
			DWORD error = GetLastError ();
			CloseHandle (handle);
			return i2p::unexpected ("CreateEvent: " + Win32ErrorText (error));
		}

		m_Handle = handle;
		m_IoEvent = ioEvent;
		return { };
	}

	void Client::Impl::Disconnect ()
	{
		if (m_Handle != INVALID_HANDLE_VALUE)
		{
			CancelIoEx (m_Handle, nullptr);
			CloseHandle (m_Handle);
			m_Handle = INVALID_HANDLE_VALUE;
		}
		if (m_IoEvent)
		{
			CloseHandle (m_IoEvent);
			m_IoEvent = nullptr;
		}
	}

	bool Client::Impl::WaitForIo (OVERLAPPED & overlapped, DWORD timeoutMs, DWORD & transferred, std::string & error)
	{
		DWORD wait = WaitForSingleObject (overlapped.hEvent, timeoutMs);
		if (wait == WAIT_OBJECT_0)
		{
			if (GetOverlappedResult (m_Handle, &overlapped, &transferred, FALSE)) return true;
			error = "I/O: " + Win32ErrorText (GetLastError ());
			return false;
		}

		if (wait == WAIT_TIMEOUT)
			error = "I/O timed out";
		else if (wait == WAIT_FAILED)
			error = "WaitForSingleObject: " + Win32ErrorText (GetLastError ());
		else
			error = "unexpected overlapped wait result";

		CancelIoEx (m_Handle, &overlapped);
		GetOverlappedResult (m_Handle, &overlapped, &transferred, TRUE);
		return false;
	}

	bool Client::Impl::WriteAll (std::string_view data, std::string & error)
	{
		while (!data.empty ())
		{
			OVERLAPPED overlapped { };
			overlapped.hEvent = m_IoEvent;
			ResetEvent (m_IoEvent);

			DWORD written = 0;
			BOOL result = WriteFile (m_Handle, data.data (), static_cast<DWORD> (data.size ()), &written, &overlapped);
			if (!result)
			{
				DWORD writeError = GetLastError ();
				if (writeError != ERROR_IO_PENDING)
				{
					error = "send: " + Win32ErrorText (writeError);
					return false;
				}
				if (!WaitForIo (overlapped, CLIENT_IO_TIMEOUT_MS, written, error))
				{
					error = "send: " + error;
					return false;
				}
			}
			if (!written)
			{
				error = "connection closed during request";
				return false;
			}
			data.remove_prefix (static_cast<size_t> (written));
		}
		return true;
	}

	i2p::expected<std::string, std::string> Client::Impl::ReadLine (size_t limit)
	{
		std::string line;
		line.reserve (std::min (limit, size_t (128)));
		while (line.size () < limit)
		{
			char ch = 0;
			OVERLAPPED overlapped { };
			overlapped.hEvent = m_IoEvent;
			ResetEvent (m_IoEvent);

			DWORD received = 0;
			BOOL result = ReadFile (m_Handle, &ch, 1, &received, &overlapped);
			if (!result)
			{
				DWORD readError = GetLastError ();
				if (readError != ERROR_IO_PENDING)
					return i2p::unexpected (std::string ("recv: ") + Win32ErrorText (readError));
				std::string error;
				if (!WaitForIo (overlapped, CLIENT_IO_TIMEOUT_MS, received, error))
					return i2p::unexpected (std::string ("recv: ") + error);
			}
			if (!received) return i2p::unexpected ("connection closed");
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
			OVERLAPPED overlapped { };
			overlapped.hEvent = m_IoEvent;
			ResetEvent (m_IoEvent);

			DWORD received = 0;
			BOOL result = ReadFile (
				m_Handle, data.data () + offset, static_cast<DWORD> (size - offset), &received, &overlapped);
			if (!result)
			{
				DWORD readError = GetLastError ();
				if (readError != ERROR_IO_PENDING)
					return i2p::unexpected (std::string ("recv: ") + Win32ErrorText (readError));
				std::string error;
				if (!WaitForIo (overlapped, CLIENT_IO_TIMEOUT_MS, received, error))
					return i2p::unexpected (std::string ("recv: ") + error);
			}
			if (!received) return i2p::unexpected ("connection closed during response");
			offset += static_cast<size_t> (received);
		}
		return data;
	}

	i2p::expected<std::string, RequestError> Client::Impl::Request (std::string_view command)
	{
		if (m_Handle == INVALID_HANDLE_VALUE)
			return i2p::unexpected (RequestError { RequestErrorKind::NotConnected, "not connected" });
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

		std::string error;
		if (!WriteAll (request, error))
		{
			Disconnect ();
			return i2p::unexpected (RequestError { RequestErrorKind::Transport, error });
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
