/*
* Copyright (c) 2026, David Uhden Collado
* SPDX-License-Identifier: BSD-3-Clause
*
* This file is part of openi2pd and licensed under BSD-3-Clause
*
* See full license text in LICENSE file at top of project tree
*/

#include "TuiServer.h"

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
#include <aclapi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "FS.h"
#include "Log.h"
#include "TuiProtocol.h"
#include "TuiServerCommon.h"
#include "TuiWin32Ipc.h"
#include "util.h"

namespace i2p
{
namespace tui
{
	namespace
	{
		// The UNIX transport uses 5 second receive/send timeouts. Keep the
		// same bound for one overlapped read or write; Stop() is checked
		// every kPollMs even while an operation is outstanding.
		constexpr DWORD PIPE_IO_TIMEOUT_MS = 5000;
		constexpr DWORD PIPE_POLL_MS = 250;
		constexpr DWORD PIPE_BUFFER_SIZE = 64 * 1024;
		constexpr DWORD PIPE_MAX_INSTANCES = 1;
		// Per-connection budgets mirror the UNIX transport: a client cannot
		// hold the single IPC instance forever or issue unlimited requests.
		constexpr size_t PIPE_MAX_REQUESTS_PER_CONNECTION = 4096;
		constexpr auto PIPE_MAX_CONNECTION_DURATION = std::chrono::minutes (5);

		struct LocalFreeDeleter
		{
			void operator() (void * pointer) const
			{
				if (pointer) LocalFree (pointer);
			}
		};
		using LocalPtr = std::unique_ptr<void, LocalFreeDeleter>;

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

		bool IsAbsoluteEndpoint (const std::string & path)
		{
			if (path.size () >= 2 && path[1] == ':'
				&& ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')))
				return true; // drive-qualified, e.g. C:\...
			if (path.size () >= 2 && (path[0] == '\\' || path[0] == '/') && (path[1] == '\\' || path[1] == '/'))
				return true; // UNC, e.g. \\server\share
			return false;
		}

		bool CopyTokenSid (HANDLE token, TOKEN_INFORMATION_CLASS informationClass, LocalPtr & sid)
		{
			DWORD size = 0;
			GetTokenInformation (token, informationClass, nullptr, 0, &size);
			if (size == 0) return false;

			LocalPtr buffer (LocalAlloc (LPTR, size));
			if (!buffer) return false;
			if (!GetTokenInformation (token, informationClass, buffer.get (), size, &size)) return false;

			PSID tokenSid = nullptr;
			if (informationClass == TokenUser)
				tokenSid = static_cast<TOKEN_USER *> (buffer.get ())->User.Sid;
			else if (informationClass == TokenPrimaryGroup)
				tokenSid = static_cast<TOKEN_PRIMARY_GROUP *> (buffer.get ())->PrimaryGroup;
			if (!tokenSid) return false;

			const DWORD sidSize = GetLengthSid (tokenSid);
			LocalPtr copy (LocalAlloc (LPTR, sidSize));
			if (!copy || !CopySid (sidSize, copy.get (), tokenSid)) return false;
			sid = std::move (copy);
			return true;
		}

		/**
		 * Build a self-relative security descriptor for the pipe.
		 *
		 * Policy (documented in TuiServer.h and the design report):
		 *   - owner: the daemon process user, from the process token;
		 *   - group: the process token primary group;
		 *   - DACL: full control for that user, SYSTEM and the built-in
		 *     Administrators group only;
		 *   - no Everyone, no Authenticated Users, no anonymous ACE, and
		 *     SE_DACL_PROTECTED so parent inheritance cannot broaden it.
		 *
		 * The descriptor is self-relative so CreateNamedPipeW keeps its
		 * ACL/SID data after the temporary absolute structures are freed.
		 */
		bool BuildPipeSecurity (LocalPtr & descriptor, std::string & error)
		{
			HANDLE token = nullptr;
			if (!OpenProcessToken (GetCurrentProcess (), TOKEN_QUERY, &token))
			{
				error = "OpenProcessToken: " + Win32ErrorText (GetLastError ());
				return false;
			}

			LocalPtr userSid;
			LocalPtr groupSid;
			bool tokenOk
				= CopyTokenSid (token, TokenUser, userSid) && CopyTokenSid (token, TokenPrimaryGroup, groupSid);
			CloseHandle (token);
			if (!tokenOk)
			{
				error = "can't read the process token user/group";
				return false;
			}

			std::array<BYTE, SECURITY_MAX_SID_SIZE> systemSid { };
			DWORD systemSidSize = static_cast<DWORD> (systemSid.size ());
			if (!CreateWellKnownSid (WinLocalSystemSid, nullptr, systemSid.data (), &systemSidSize))
			{
				error = "CreateWellKnownSid(SYSTEM): " + Win32ErrorText (GetLastError ());
				return false;
			}

			std::array<BYTE, SECURITY_MAX_SID_SIZE> administratorsSid { };
			DWORD administratorsSidSize = static_cast<DWORD> (administratorsSid.size ());
			if (!CreateWellKnownSid (
					WinBuiltinAdministratorsSid, nullptr, administratorsSid.data (), &administratorsSidSize))
			{
				error = "CreateWellKnownSid(Administrators): " + Win32ErrorText (GetLastError ());
				return false;
			}

			EXPLICIT_ACCESS_W entries[3] { };
			entries[0].grfAccessPermissions = FILE_ALL_ACCESS;
			entries[0].grfAccessMode = GRANT_ACCESS;
			entries[0].grfInheritance = NO_INHERITANCE;
			entries[0].Trustee.TrusteeForm = TRUSTEE_IS_SID;
			entries[0].Trustee.TrusteeType = TRUSTEE_IS_USER;
			entries[0].Trustee.ptstrName = static_cast<LPWSTR> (userSid.get ());

			entries[1].grfAccessPermissions = FILE_ALL_ACCESS;
			entries[1].grfAccessMode = GRANT_ACCESS;
			entries[1].grfInheritance = NO_INHERITANCE;
			entries[1].Trustee.TrusteeForm = TRUSTEE_IS_SID;
			entries[1].Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
			entries[1].Trustee.ptstrName = reinterpret_cast<LPWSTR> (systemSid.data ());

			entries[2].grfAccessPermissions = FILE_ALL_ACCESS;
			entries[2].grfAccessMode = GRANT_ACCESS;
			entries[2].grfInheritance = NO_INHERITANCE;
			entries[2].Trustee.TrusteeForm = TRUSTEE_IS_SID;
			entries[2].Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
			entries[2].Trustee.ptstrName = reinterpret_cast<LPWSTR> (administratorsSid.data ());

			PACL rawAcl = nullptr;
			DWORD aclResult = SetEntriesInAclW (3, entries, nullptr, &rawAcl);
			LocalPtr acl (rawAcl);
			if (aclResult != ERROR_SUCCESS)
			{
				error = "SetEntriesInAcl: " + Win32ErrorText (aclResult);
				return false;
			}

			SECURITY_DESCRIPTOR absolute { };
			if (!InitializeSecurityDescriptor (&absolute, SECURITY_DESCRIPTOR_REVISION))
			{
				error = "InitializeSecurityDescriptor: " + Win32ErrorText (GetLastError ());
				return false;
			}
			if (!SetSecurityDescriptorOwner (&absolute, userSid.get (), FALSE))
			{
				error = "SetSecurityDescriptorOwner: " + Win32ErrorText (GetLastError ());
				return false;
			}
			if (!SetSecurityDescriptorGroup (&absolute, groupSid.get (), FALSE))
			{
				error = "SetSecurityDescriptorGroup: " + Win32ErrorText (GetLastError ());
				return false;
			}
			if (!SetSecurityDescriptorDacl (&absolute, TRUE, static_cast<PACL> (acl.get ()), FALSE))
			{
				error = "SetSecurityDescriptorDacl: " + Win32ErrorText (GetLastError ());
				return false;
			}
			if (!SetSecurityDescriptorControl (&absolute, SE_DACL_PROTECTED, SE_DACL_PROTECTED))
			{
				error = "SetSecurityDescriptorControl: " + Win32ErrorText (GetLastError ());
				return false;
			}

			DWORD descriptorSize = 0;
			MakeSelfRelativeSD (&absolute, nullptr, &descriptorSize);
			if (GetLastError () != ERROR_INSUFFICIENT_BUFFER || descriptorSize == 0)
			{
				error = "MakeSelfRelativeSD(size): " + Win32ErrorText (GetLastError ());
				return false;
			}

			LocalPtr selfRelative (LocalAlloc (LPTR, descriptorSize));
			if (!selfRelative)
			{
				error = "can't allocate the pipe security descriptor";
				return false;
			}
			if (!MakeSelfRelativeSD (&absolute, selfRelative.get (), &descriptorSize))
			{
				error = "MakeSelfRelativeSD: " + Win32ErrorText (GetLastError ());
				return false;
			}

			descriptor = std::move (selfRelative);
			return true;
		}
	}

	class Server::Impl
	{
	public:
		explicit Impl (std::string endpoint)
			: m_Endpoint (std::move (endpoint))
		{
		}

		~Impl () { Stop (); }

		Impl (const Impl &) = delete;
		Impl & operator= (const Impl &) = delete;

		bool Start ();
		void Stop ();

	private:
		HANDLE CreatePipeInstance (std::string & error);
		bool ConnectClient (HANDLE pipe);
		void HandleClient (HANDLE pipe);
		bool WaitForIo (HANDLE pipe, OVERLAPPED & overlapped, DWORD timeoutMs, DWORD & transferred);
		bool SendAll (HANDLE pipe, HANDLE ioEvent, std::string_view data,
			std::chrono::steady_clock::time_point deadline);
		void Run (HANDLE firstPipe);

		std::string m_Endpoint;
		std::wstring m_PipeName;
		LocalPtr m_Security;
		HANDLE m_StopEvent { nullptr };
		std::thread m_Thread;
		std::atomic<bool> m_Stop { false };
	};

	HANDLE Server::Impl::CreatePipeInstance (std::string & error)
	{
		if (!m_Security)
		{
			error = "pipe security descriptor is not initialized";
			return INVALID_HANDLE_VALUE;
		}

		SECURITY_ATTRIBUTES security { };
		security.nLength = sizeof (security);
		security.lpSecurityDescriptor = m_Security.get ();
		security.bInheritHandle = FALSE;

		// FILE_FLAG_FIRST_PIPE_INSTANCE makes creation fail if any process
		// already owns the name. We only ever have one instance, so applying
		// it to each recreated instance also prevents a squatter from taking
		// over between clients; the daemon never attaches to an existing
		// arbitrary pipe. PIPE_REJECT_REMOTE_CLIENTS refuses SMB clients.
		HANDLE pipe = CreateNamedPipeW (m_PipeName.c_str (),
			PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
			PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, PIPE_MAX_INSTANCES,
			PIPE_BUFFER_SIZE, PIPE_BUFFER_SIZE, 0, &security);
		if (pipe == INVALID_HANDLE_VALUE) error = "CreateNamedPipe: " + Win32ErrorText (GetLastError ());
		return pipe;
	}

	bool Server::Impl::Start ()
	{
		if (m_StopEvent) return true;
		if (m_Endpoint.empty ())
		{
			LogPrint (eLogError, "TUI: Empty local IPC endpoint");
			return false;
		}

		// Match the client's config resolution: a relative tui.socket is
		// relative to the data directory, not to the service's working
		// directory (which is often System32).
		std::string endpoint = m_Endpoint;
		if (!IsAbsoluteEndpoint (endpoint)) endpoint = i2p::fs::DataDirPath (endpoint);
		m_PipeName = MakeWin32PipeName (endpoint);
		if (m_PipeName.size () >= 256)
		{
			LogPrint (eLogError, "TUI: Named pipe name is too long");
			return false;
		}

		std::string error;
		if (!BuildPipeSecurity (m_Security, error))
		{
			LogPrint (eLogError, "TUI: Can't build named pipe DACL: ", error);
			return false;
		}

		m_StopEvent = CreateEventW (nullptr, TRUE, FALSE, nullptr);
		if (!m_StopEvent)
		{
			LogPrint (eLogError, "TUI: Can't create stop event: ", Win32ErrorText (GetLastError ()));
			m_Security.reset ();
			return false;
		}

		m_Stop.store (false, std::memory_order_relaxed);
		HANDLE firstPipe = CreatePipeInstance (error);
		if (firstPipe == INVALID_HANDLE_VALUE)
		{
			LogPrint (eLogError, "TUI: Can't create named pipe ", ToUtf8 (m_PipeName), ": ", error);
			CloseHandle (m_StopEvent);
			m_StopEvent = nullptr;
			m_Security.reset ();
			return false;
		}

		try
		{
			m_Thread = std::thread ([this, firstPipe] { Run (firstPipe); });
		}
		catch (...)
		{
			CloseHandle (firstPipe);
			CloseHandle (m_StopEvent);
			m_StopEvent = nullptr;
			m_Security.reset ();
			LogPrint (eLogError, "TUI: Can't start the named pipe thread");
			return false;
		}

		LogPrint (eLogInfo, "TUI: Listening on named pipe ", ToUtf8 (m_PipeName));
		return true;
	}

	void Server::Impl::Stop ()
	{
		m_Stop.store (true, std::memory_order_relaxed);
		if (m_StopEvent) SetEvent (m_StopEvent);
		if (m_Thread.joinable ()) m_Thread.join ();
		if (m_StopEvent)
		{
			CloseHandle (m_StopEvent);
			m_StopEvent = nullptr;
		}
		m_Security.reset ();
	}

	void Server::Impl::Run (HANDLE firstPipe)
	{
		i2p::util::SetThreadName ("i2pd-tui-ipc");
		HANDLE pipe = firstPipe;
		while (!m_Stop.load (std::memory_order_relaxed))
		{
			if (pipe == INVALID_HANDLE_VALUE)
			{
				std::string error;
				pipe = CreatePipeInstance (error);
				if (pipe == INVALID_HANDLE_VALUE)
				{
					if (m_Stop.load (std::memory_order_relaxed)) break;
					LogPrint (eLogWarning, "TUI: Can't recreate named pipe: ", error);
					WaitForSingleObject (m_StopEvent, 1000);
					continue;
				}
			}

			bool connected = ConnectClient (pipe);
			if (connected)
			{
				HandleClient (pipe);
				// Reuse the instance for the next client. Closing and
				// recreating it would leave the pipe name unowned for a
				// short interval, during which another local user could
				// squat the name and deny the TUI service. The name stays
				// continuously owned on the normal path.
				if (!DisconnectNamedPipe (pipe))
				{
					LogPrint (eLogWarning, "TUI: Can't reset named pipe instance: ", Win32ErrorText (GetLastError ()));
					CloseHandle (pipe);
					pipe = INVALID_HANDLE_VALUE;
				}
			}
			else
			{
				// The instance was aborted before a client connected (or
				// ConnectNamedPipe failed). Do not reuse a partially
				// completed overlapped state.
				CloseHandle (pipe);
				pipe = INVALID_HANDLE_VALUE;
				if (!m_Stop.load (std::memory_order_relaxed)) WaitForSingleObject (m_StopEvent, 100);
			}
		}
		if (pipe != INVALID_HANDLE_VALUE) CloseHandle (pipe);
	}

	bool Server::Impl::ConnectClient (HANDLE pipe)
	{
		OVERLAPPED overlapped { };
		overlapped.hEvent = CreateEventW (nullptr, TRUE, FALSE, nullptr);
		if (!overlapped.hEvent)
		{
			LogPrint (eLogWarning, "TUI: Can't create connect event: ", Win32ErrorText (GetLastError ()));
			return false;
		}

		BOOL result = ConnectNamedPipe (pipe, &overlapped);
		if (result)
		{
			CloseHandle (overlapped.hEvent);
			return true;
		}

		DWORD error = GetLastError ();
		if (error == ERROR_PIPE_CONNECTED)
		{
			CloseHandle (overlapped.hEvent);
			return true;
		}
		if (error != ERROR_IO_PENDING)
		{
			LogPrint (eLogWarning, "TUI: ConnectNamedPipe: ", Win32ErrorText (error));
			CloseHandle (overlapped.hEvent);
			return false;
		}

		bool connected = false;
		for (;;)
		{
			HANDLE handles[2] = { overlapped.hEvent, m_StopEvent };
			DWORD wait = WaitForMultipleObjects (2, handles, FALSE, PIPE_POLL_MS);
			if (wait == WAIT_OBJECT_0)
			{
				DWORD transferred = 0;
				if (GetOverlappedResult (pipe, &overlapped, &transferred, FALSE))
					connected = true;
				else
				{
					error = GetLastError ();
					if (error != ERROR_OPERATION_ABORTED && error != ERROR_PIPE_NOT_CONNECTED)
						LogPrint (eLogWarning, "TUI: ConnectNamedPipe completion: ", Win32ErrorText (error));
				}
				break;
			}
			if (wait == WAIT_OBJECT_0 + 1)
			{
				CancelIoEx (pipe, &overlapped);
				DWORD transferred = 0;
				GetOverlappedResult (pipe, &overlapped, &transferred, TRUE);
				break;
			}
			if (wait == WAIT_FAILED)
			{
				LogPrint (eLogWarning, "TUI: WaitForMultipleObjects(connect): ", Win32ErrorText (GetLastError ()));
				CancelIoEx (pipe, &overlapped);
				DWORD transferred = 0;
				GetOverlappedResult (pipe, &overlapped, &transferred, TRUE);
				break;
			}
		}

		CloseHandle (overlapped.hEvent);
		return connected;
	}

	bool Server::Impl::WaitForIo (HANDLE pipe, OVERLAPPED & overlapped, DWORD timeoutMs, DWORD & transferred)
	{
		const auto deadline = std::chrono::steady_clock::now () + std::chrono::milliseconds (timeoutMs);
		for (;;)
		{
			if (m_Stop.load (std::memory_order_relaxed))
			{
				CancelIoEx (pipe, &overlapped);
				GetOverlappedResult (pipe, &overlapped, &transferred, TRUE);
				return false;
			}

			auto remaining
				= std::chrono::duration_cast<std::chrono::milliseconds> (deadline - std::chrono::steady_clock::now ())
					  .count ();
			if (remaining <= 0)
			{
				CancelIoEx (pipe, &overlapped);
				GetOverlappedResult (pipe, &overlapped, &transferred, TRUE);
				LogPrint (eLogWarning, "TUI: Named pipe I/O timed out");
				return false;
			}

			DWORD waitMs = static_cast<DWORD> (std::min<int64_t> (remaining, PIPE_POLL_MS));
			HANDLE handles[2] = { overlapped.hEvent, m_StopEvent };
			DWORD wait = WaitForMultipleObjects (2, handles, FALSE, waitMs);
			if (wait == WAIT_OBJECT_0)
			{
				if (GetOverlappedResult (pipe, &overlapped, &transferred, FALSE)) return true;
				DWORD error = GetLastError ();
				if (error != ERROR_OPERATION_ABORTED && error != ERROR_BROKEN_PIPE && error != ERROR_PIPE_NOT_CONNECTED
					&& error != ERROR_NO_DATA)
					LogPrint (eLogWarning, "TUI: Named pipe I/O: ", Win32ErrorText (error));
				return false;
			}
			if (wait == WAIT_OBJECT_0 + 1)
			{
				CancelIoEx (pipe, &overlapped);
				GetOverlappedResult (pipe, &overlapped, &transferred, TRUE);
				return false;
			}
			if (wait == WAIT_FAILED)
			{
				LogPrint (eLogWarning, "TUI: WaitForMultipleObjects(I/O): ", Win32ErrorText (GetLastError ()));
				CancelIoEx (pipe, &overlapped);
				GetOverlappedResult (pipe, &overlapped, &transferred, TRUE);
				return false;
			}
		}
	}

	bool Server::Impl::SendAll (HANDLE pipe, HANDLE ioEvent, std::string_view data,
		std::chrono::steady_clock::time_point deadline)
	{
		while (!data.empty ())
		{
			if (m_Stop.load (std::memory_order_relaxed)) return false;
			const auto now = std::chrono::steady_clock::now ();
			if (now >= deadline) return false;
			const auto remaining
				= std::chrono::duration_cast<std::chrono::milliseconds> (deadline - now).count ();
			if (remaining <= 0) return false;
			const DWORD ioTimeout = static_cast<DWORD> (
				std::min<int64_t> (std::max<int64_t> (remaining, 1), static_cast<int64_t> (PIPE_IO_TIMEOUT_MS)));

			OVERLAPPED overlapped { };
			overlapped.hEvent = ioEvent;
			ResetEvent (ioEvent);

			DWORD written = 0;
			BOOL result = WriteFile (pipe, data.data (), static_cast<DWORD> (data.size ()), &written, &overlapped);
			if (!result)
			{
				DWORD error = GetLastError ();
				if (error != ERROR_IO_PENDING)
				{
					if (error != ERROR_BROKEN_PIPE && error != ERROR_PIPE_NOT_CONNECTED && error != ERROR_NO_DATA
						&& error != ERROR_OPERATION_ABORTED)
						LogPrint (eLogWarning, "TUI: Named pipe write: ", Win32ErrorText (error));
					return false;
				}
				if (!WaitForIo (pipe, overlapped, ioTimeout, written)) return false;
			}
			if (!written) return false;
			data.remove_prefix (static_cast<size_t> (written));
		}
		return true;
	}

	void Server::Impl::HandleClient (HANDLE pipe)
	{
		HANDLE ioEvent = CreateEventW (nullptr, TRUE, FALSE, nullptr);
		if (!ioEvent)
		{
			LogPrint (eLogWarning, "TUI: Can't create client I/O event: ", Win32ErrorText (GetLastError ()));
			return;
		}

		// A per-client allocation failure must not escape into std::thread
		// and terminate the daemon. Dispatch() already contains subsystem
		// exceptions; this guard covers buffering and response framing.
		try
		{
			const auto connectionDeadline = std::chrono::steady_clock::now () + PIPE_MAX_CONNECTION_DURATION;
			size_t requests = 0;
			std::string pending;
			pending.reserve (MAX_REQUEST_LINE);
			std::array<char, 1024> buffer { };
			while (!m_Stop.load (std::memory_order_relaxed))
			{
				const auto now = std::chrono::steady_clock::now ();
				if (now >= connectionDeadline) break;
				const auto remaining
					= std::chrono::duration_cast<std::chrono::milliseconds> (connectionDeadline - now).count ();
				const DWORD readTimeout = static_cast<DWORD> (
					std::min<int64_t> (std::max<int64_t> (remaining, 1), static_cast<int64_t> (PIPE_IO_TIMEOUT_MS)));

				OVERLAPPED overlapped { };
				overlapped.hEvent = ioEvent;
				ResetEvent (ioEvent);

				DWORD received = 0;
				BOOL result = ReadFile (
					pipe, buffer.data (), static_cast<DWORD> (buffer.size ()), &received, &overlapped);
				if (!result)
				{
					DWORD error = GetLastError ();
					if (error != ERROR_IO_PENDING)
					{
						if (error != ERROR_BROKEN_PIPE && error != ERROR_PIPE_NOT_CONNECTED && error != ERROR_NO_DATA
							&& error != ERROR_OPERATION_ABORTED)
							LogPrint (eLogWarning, "TUI: Named pipe read: ", Win32ErrorText (error));
						break;
					}
					if (!WaitForIo (pipe, overlapped, readTimeout, received)) break;
				}
				if (!received) break;

				pending.append (buffer.data (), static_cast<size_t> (received));
				if (pending.size () > MAX_REQUEST_LINE + 1)
				{
					SendAll (pipe, ioEvent, MakeErrorResponse (ErrorCode::MalformedRequest, "request too long"),
						connectionDeadline);
					break;
				}

				bool closeConnection = false;
				size_t newline = 0;
				while ((newline = pending.find ('\n')) != std::string::npos)
				{
					if (m_Stop.load (std::memory_order_relaxed))
					{
						closeConnection = true;
						break;
					}
					if (std::chrono::steady_clock::now () >= connectionDeadline)
					{
						closeConnection = true;
						break;
					}
					if (++requests > PIPE_MAX_REQUESTS_PER_CONNECTION)
					{
						SendAll (pipe, ioEvent,
							MakeErrorResponse (ErrorCode::Internal, "connection request limit reached"),
							connectionDeadline);
						closeConnection = true;
						break;
					}
					std::string_view line (pending.data (), newline);
					if (!line.empty () && line.back () == '\r') line.remove_suffix (1);
					auto response = Dispatch (line);
					if (!SendAll (pipe, ioEvent, response.response, connectionDeadline))
					{
						closeConnection = true;
						break;
					}
					pending.erase (0, newline + 1);
					if (response.closeConnection)
					{
						closeConnection = true;
						break;
					}
				}
				if (closeConnection) break;
			}
		}
		catch (...)
		{
			// The pipe is disconnected by Run(). Do not log from here: a
			// second allocation failure while formatting would terminate.
		}

		CloseHandle (ioEvent);
	}

	Server::Server (std::string endpoint)
		: m_Impl (std::make_unique<Impl> (std::move (endpoint)))
	{
	}

	Server::~Server ()
	{
		Stop ();
	}

	bool Server::Start ()
	{
		return m_Impl && m_Impl->Start ();
	}

	void Server::Stop ()
	{
		if (m_Impl) m_Impl->Stop ();
	}
}
}

#endif // _WIN32
