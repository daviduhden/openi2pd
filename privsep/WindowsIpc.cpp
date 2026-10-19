/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: WindowsIpc.
 *
 * Implements Windows IPC and confinement: private-DACL named-pipe creation
 * and accept, message-mode framed send and receive, peer identity lookup, and
 * worker spawning with a restricted token and Job Object. AppContainer and
 * LPAC support is probed but never silently replaced by a weaker token.
 *
 */

#include "WindowsIpc.h"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <aclapi.h>
#include <sddl.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <vector>

namespace i2p::privsep
{
namespace
{
	constexpr DWORD PIPE_BUFFER_SIZE = 64 * 1024;
	constexpr DWORD PIPE_MAX_INSTANCES = 1;

	struct LocalFreeDeleter
	{
		void operator() (void * pointer) const
		{
			if (pointer) LocalFree (pointer);
		}
	};
	using LocalPtr = std::unique_ptr<void, LocalFreeDeleter>;

	struct HandleCloser
	{
		void operator() (void * handle) const
		{
			if (handle && handle != INVALID_HANDLE_VALUE) CloseHandle (static_cast<HANDLE> (handle));
		}
	};
	using ScopedHandle = std::unique_ptr<void, HandleCloser>;

	std::string Win32ErrorText (DWORD error)
	{
		char * message = nullptr;
		const DWORD length = FormatMessageA (
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error,
			MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPSTR> (&message), 0, nullptr);
		if (!length || !message) return "Windows error " + std::to_string (error);
		std::string text (message, length);
		LocalFree (message);
		while (!text.empty () && (text.back () == '\r' || text.back () == '\n' || text.back () == ' '))
			text.pop_back ();
		return text;
	}

	bool IsLocalPipeName (const std::wstring & name)
	{
		const wchar_t prefix[] = L"\\\\.\\pipe\\";
		if (name.size () < sizeof (prefix) / sizeof (prefix[0]) - 1) return false;
		return CompareStringOrdinal (name.c_str (), static_cast<int> (sizeof (prefix) / sizeof (prefix[0]) - 1), prefix,
				   static_cast<int> (sizeof (prefix) / sizeof (prefix[0]) - 1), TRUE)
			== CSTR_EQUAL;
	}

	std::wstring CurrentUserSid (std::string & error)
	{
		HANDLE token = nullptr;
		if (!OpenProcessToken (GetCurrentProcess (), TOKEN_QUERY, &token))
		{
			error = "OpenProcessToken: " + Win32ErrorText (GetLastError ());
			return { };
		}
		ScopedHandle tokenGuard (token);
		DWORD size = 0;
		GetTokenInformation (token, TokenUser, nullptr, 0, &size);
		if (GetLastError () != ERROR_INSUFFICIENT_BUFFER || size == 0)
		{
			error = "GetTokenInformation(TokenUser,size): " + Win32ErrorText (GetLastError ());
			return { };
		}
		std::vector<BYTE> buffer (size);
		if (!GetTokenInformation (token, TokenUser, buffer.data (), size, &size))
		{
			error = "GetTokenInformation(TokenUser): " + Win32ErrorText (GetLastError ());
			return { };
		}
		auto * user = reinterpret_cast<TOKEN_USER *> (buffer.data ());
		LPWSTR sidString = nullptr;
		if (!ConvertSidToStringSidW (user->User.Sid, &sidString))
		{
			error = "ConvertSidToStringSid: " + Win32ErrorText (GetLastError ());
			return { };
		}
		LocalPtr sidGuard (sidString);
		return std::wstring (sidString);
	}

	bool BuildPipeSecurity (const std::wstring & userSid, PSECURITY_DESCRIPTOR & descriptor, std::string & error)
	{
		if (userSid.empty ())
		{
			error = "empty process user SID";
			return false;
		}
		// Protected DACL: only the service user and SYSTEM. Administrators are
		// not added implicitly; a deployment that needs an administrator must
		// grant that explicitly through the service account model. There is no
		// Everyone, Authenticated Users or anonymous ACE.
		const std::wstring sddl = L"D:P(A;;GA;;;" + userSid + L")(A;;GA;;;SY)";
		if (!ConvertStringSecurityDescriptorToSecurityDescriptorW (
				sddl.c_str (), SDDL_REVISION_1, &descriptor, nullptr))
		{
			error = "ConvertStringSecurityDescriptorToSecurityDescriptor: " + Win32ErrorText (GetLastError ());
			return false;
		}
		BOOL present = FALSE;
		BOOL defaulted = FALSE;
		PACL dacl = nullptr;
		if (!GetSecurityDescriptorDacl (descriptor, &present, &dacl, &defaulted) || !present || !dacl)
		{
			error = "pipe security descriptor has no DACL";
			LocalFree (descriptor);
			descriptor = nullptr;
			return false;
		}
		// The SDDL "D:P" prefix has already marked the DACL protected. Do not
		// call SetSecurityDescriptorControl on a self-relative descriptor; the
		// API expects an absolute descriptor.
		SECURITY_DESCRIPTOR_CONTROL control = 0;
		DWORD revision = 0;
		if (!GetSecurityDescriptorControl (descriptor, &control, &revision) || !(control & SE_DACL_PROTECTED))
		{
			error = "pipe security descriptor DACL is not protected";
			LocalFree (descriptor);
			descriptor = nullptr;
			return false;
		}
		return true;
	}

	const wchar_t * RoleName (WindowsWorkerRole role)
	{
		switch (role)
		{
		case WindowsWorkerRole::Supervisor: return L"supervisor";
		case WindowsWorkerRole::Log: return L"log";
		case WindowsWorkerRole::Storage: return L"storage";
		case WindowsWorkerRole::Network: return L"network";
		case WindowsWorkerRole::Crypto: return L"crypto";
		case WindowsWorkerRole::Admin: return L"admin";
		}
		return L"unknown";
	}

	std::wstring NormalizeEndpoint (std::string_view endpoint)
	{
		while (!endpoint.empty () && (endpoint.front () == ' ' || endpoint.front () == '\t'))
			endpoint.remove_prefix (1);
		while (!endpoint.empty () && (endpoint.back () == ' ' || endpoint.back () == '\t')) endpoint.remove_suffix (1);
		std::wstring result;
		result.reserve (endpoint.size ());
		for (unsigned char ch : endpoint)
		{
			if (ch == '/') ch = '\\';
			if (ch >= 'A' && ch <= 'Z') ch = static_cast<unsigned char> (ch - 'A' + 'a');
			result.push_back (static_cast<wchar_t> (ch));
		}
		while (!result.empty () && result.back () == L'\\') result.pop_back ();
		return result;
	}

	i2p::expected<void, std::string> SetPipeMessageMode (HANDLE pipe)
	{
		DWORD mode = PIPE_READMODE_MESSAGE;
		if (!SetNamedPipeHandleState (pipe, &mode, nullptr, nullptr))
			return i2p::unexpected ("SetNamedPipeHandleState: " + Win32ErrorText (GetLastError ()));
		return { };
	}

	bool WaitOverlapped (HANDLE pipe, OVERLAPPED & overlapped, DWORD timeoutMs, DWORD & transferred)
	{
		if (WaitForSingleObject (overlapped.hEvent, timeoutMs) != WAIT_OBJECT_0)
		{
			CancelIoEx (pipe, &overlapped);
			return false;
		}
		return GetOverlappedResult (pipe, &overlapped, &transferred, FALSE) != 0;
	}
}

WindowsHandle::~WindowsHandle ()
{
	Reset ();
}

WindowsHandle::WindowsHandle (WindowsHandle && other) noexcept
	: m_Handle (other.m_Handle)
{
	other.m_Handle = INVALID_HANDLE_VALUE;
}

WindowsHandle & WindowsHandle::operator= (WindowsHandle && other) noexcept
{
	if (this != &other)
	{
		Reset ();
		m_Handle = other.m_Handle;
		other.m_Handle = INVALID_HANDLE_VALUE;
	}
	return *this;
}

void WindowsHandle::Reset (HANDLE handle)
{
	if (IsValid ()) CloseHandle (m_Handle);
	m_Handle = handle;
}

WindowsServer::~WindowsServer ()
{
	if (m_Pipe != INVALID_HANDLE_VALUE)
	{
		DisconnectNamedPipe (m_Pipe);
		CloseHandle (m_Pipe);
	}
	if (m_Security) LocalFree (m_Security);
}

WindowsServer::WindowsServer (WindowsServer && other) noexcept
	: m_Pipe (other.m_Pipe)
	, m_Name (std::move (other.m_Name))
	, m_UserSid (std::move (other.m_UserSid))
	, m_Security (other.m_Security)
{
	other.m_Pipe = INVALID_HANDLE_VALUE;
	other.m_Security = nullptr;
}

WindowsServer & WindowsServer::operator= (WindowsServer && other) noexcept
{
	if (this != &other)
	{
		if (m_Pipe != INVALID_HANDLE_VALUE)
		{
			DisconnectNamedPipe (m_Pipe);
			CloseHandle (m_Pipe);
		}
		if (m_Security) LocalFree (m_Security);
		m_Pipe = other.m_Pipe;
		m_Name = std::move (other.m_Name);
		m_UserSid = std::move (other.m_UserSid);
		m_Security = other.m_Security;
		other.m_Pipe = INVALID_HANDLE_VALUE;
		other.m_Security = nullptr;
	}
	return *this;
}

i2p::expected<WindowsServer, std::string> WindowsServer::Create (const std::wstring & pipeName)
{
	if (!IsLocalPipeName (pipeName)) return i2p::unexpected ("worker pipe name is not local \\\\.\\pipe\\...");
	std::string error;
	const std::wstring userSid = CurrentUserSid (error);
	if (userSid.empty ()) return i2p::unexpected (error);
	PSECURITY_DESCRIPTOR descriptor = nullptr;
	if (!BuildPipeSecurity (userSid, descriptor, error)) return i2p::unexpected (error);

	SECURITY_ATTRIBUTES attributes { };
	attributes.nLength = sizeof (attributes);
	attributes.lpSecurityDescriptor = descriptor;
	attributes.bInheritHandle = FALSE;

	const DWORD openMode = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE;
	const DWORD pipeMode = PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS;
	const HANDLE pipe = CreateNamedPipeW (
		pipeName.c_str (), openMode, pipeMode, PIPE_MAX_INSTANCES, PIPE_BUFFER_SIZE, PIPE_BUFFER_SIZE, 0, &attributes);
	if (pipe == INVALID_HANDLE_VALUE)
	{
		LocalFree (descriptor);
		return i2p::unexpected ("CreateNamedPipe: " + Win32ErrorText (GetLastError ()));
	}
	return WindowsServer (pipe, pipeName, userSid, descriptor);
}

i2p::expected<WindowsChannel, std::string> WindowsServer::Accept (DWORD timeoutMs)
{
	if (m_Pipe == INVALID_HANDLE_VALUE) return i2p::unexpected ("worker pipe is closed");
	ScopedHandle event (CreateEventW (nullptr, TRUE, FALSE, nullptr));
	if (!event) return i2p::unexpected ("CreateEvent(pipe accept): " + Win32ErrorText (GetLastError ()));

	OVERLAPPED overlapped { };
	overlapped.hEvent = event.get ();
	DWORD transferred = 0;
	BOOL connected = ConnectNamedPipe (m_Pipe, &overlapped);
	if (!connected)
	{
		const DWORD error = GetLastError ();
		if (error == ERROR_PIPE_CONNECTED)
			connected = TRUE;
		else if (error == ERROR_IO_PENDING)
		{
			if (WaitForSingleObject (event.get (), timeoutMs) != WAIT_OBJECT_0)
			{
				CancelIoEx (m_Pipe, &overlapped);
				return i2p::unexpected ("worker pipe accept timeout");
			}
			connected = GetOverlappedResult (m_Pipe, &overlapped, &transferred, FALSE);
		}
		else
			return i2p::unexpected ("ConnectNamedPipe: " + Win32ErrorText (error));
	}
	if (!connected) return i2p::unexpected ("worker pipe accept failed");

	WindowsChannel channel (m_Pipe, true);
	m_Pipe = INVALID_HANDLE_VALUE;
	auto peer = channel.Peer ();
	if (!peer)
	{
		channel.Close ();
		return i2p::unexpected (peer.error ());
	}
	if (peer->userSid != m_UserSid)
	{
		channel.Close ();
		return i2p::unexpected ("worker pipe peer has an unexpected user SID");
	}
	return channel;
}

WindowsChannel::~WindowsChannel ()
{
	Close ();
}

WindowsChannel::WindowsChannel (WindowsChannel && other) noexcept
	: m_Pipe (other.m_Pipe)
	, m_ServerSide (other.m_ServerSide)
{
	other.m_Pipe = INVALID_HANDLE_VALUE;
	other.m_ServerSide = false;
}

WindowsChannel & WindowsChannel::operator= (WindowsChannel && other) noexcept
{
	if (this != &other)
	{
		Close ();
		m_Pipe = other.m_Pipe;
		m_ServerSide = other.m_ServerSide;
		other.m_Pipe = INVALID_HANDLE_VALUE;
		other.m_ServerSide = false;
	}
	return *this;
}

void WindowsChannel::Close ()
{
	if (m_Pipe != INVALID_HANDLE_VALUE)
	{
		// Do not call FlushFileBuffers() here: it can block indefinitely if a
		// peer stops reading. The channel is bounded by its I/O deadlines and
		// is closed directly; DisconnectNamedPipe is a no-op on the client end.
		DisconnectNamedPipe (m_Pipe);
		CloseHandle (m_Pipe);
		m_Pipe = INVALID_HANDLE_VALUE;
	}
}

i2p::expected<WindowsChannel, std::string> WindowsChannel::Connect (const std::wstring & pipeName, DWORD timeoutMs)
{
	if (!IsLocalPipeName (pipeName)) return i2p::unexpected ("worker pipe name is not local");
	if (!WaitNamedPipeW (pipeName.c_str (), timeoutMs))
		return i2p::unexpected ("WaitNamedPipe: " + Win32ErrorText (GetLastError ()));
	const HANDLE pipe = CreateFileW (pipeName.c_str (), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
		FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
	if (pipe == INVALID_HANDLE_VALUE)
		return i2p::unexpected ("CreateFile(worker pipe): " + Win32ErrorText (GetLastError ()));
	WindowsChannel channel (pipe);
	auto mode = SetPipeMessageMode (pipe);
	if (!mode)
	{
		channel.Close ();
		return i2p::unexpected (mode.error ());
	}
	return channel;
}

i2p::expected<void, std::string> WindowsChannel::Send (const Message & message, DWORD timeoutMs)
{
	if (!IsOpen ()) return i2p::unexpected ("worker pipe is closed");
	auto encoded = Encode (message);
	if (!encoded) return i2p::unexpected (encoded.error ().text);
	ScopedHandle event (CreateEventW (nullptr, TRUE, FALSE, nullptr));
	if (!event) return i2p::unexpected ("CreateEvent(send): " + Win32ErrorText (GetLastError ()));
	OVERLAPPED overlapped { };
	overlapped.hEvent = event.get ();
	DWORD written = 0;
	BOOL result = WriteFile (m_Pipe, encoded->data (), static_cast<DWORD> (encoded->size ()), &written, &overlapped);
	if (!result)
	{
		const DWORD error = GetLastError ();
		if (error != ERROR_IO_PENDING) return i2p::unexpected ("WriteFile(worker pipe): " + Win32ErrorText (error));
		if (!WaitOverlapped (m_Pipe, overlapped, timeoutMs, written))
			return i2p::unexpected ("worker pipe send timeout");
	}
	if (static_cast<size_t> (written) != encoded->size ()) return i2p::unexpected ("worker pipe short message write");
	return { };
}

i2p::expected<std::optional<Message>, std::string> WindowsChannel::Receive (DWORD timeoutMs)
{
	if (!IsOpen ()) return i2p::unexpected ("worker pipe is closed");
	ScopedHandle event (CreateEventW (nullptr, TRUE, FALSE, nullptr));
	if (!event) return i2p::unexpected ("CreateEvent(receive): " + Win32ErrorText (GetLastError ()));
	std::vector<char> buffer (MAX_MESSAGE_SIZE);
	OVERLAPPED overlapped { };
	overlapped.hEvent = event.get ();
	DWORD read = 0;
	BOOL result = ReadFile (m_Pipe, buffer.data (), static_cast<DWORD> (buffer.size ()), &read, &overlapped);
	if (!result)
	{
		const DWORD error = GetLastError ();
		if (error == ERROR_MORE_DATA) return i2p::unexpected ("worker pipe message is too large");
		if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED)
			return i2p::unexpected ("worker pipe closed");
		if (error != ERROR_IO_PENDING) return i2p::unexpected ("ReadFile(worker pipe): " + Win32ErrorText (error));
		if (!WaitOverlapped (m_Pipe, overlapped, timeoutMs, read))
			return i2p::unexpected ("worker pipe receive timeout");
	}
	if (read == 0) return i2p::unexpected ("worker pipe closed");
	auto decoded = Decode (std::string_view (buffer.data (), read));
	if (!decoded) return i2p::unexpected ("protocol: " + std::string (decoded.error ().text));
	return std::optional<Message> (std::move (*decoded));
}

i2p::expected<WindowsPeer, std::string> WindowsChannel::Peer () const
{
	if (!IsOpen ()) return i2p::unexpected ("worker pipe is closed");
	DWORD processId = 0;
	DWORD sessionId = 0;
	const BOOL processOk = m_ServerSide ? GetNamedPipeClientProcessId (m_Pipe, &processId)
										: GetNamedPipeServerProcessId (m_Pipe, &processId);
	const BOOL sessionOk = m_ServerSide ? GetNamedPipeClientSessionId (m_Pipe, &sessionId)
										: GetNamedPipeServerSessionId (m_Pipe, &sessionId);
	if (!processOk) return i2p::unexpected ("GetNamedPipePeerProcessId: " + Win32ErrorText (GetLastError ()));
	HANDLE process = OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
	if (!process) return i2p::unexpected ("OpenProcess(peer): " + Win32ErrorText (GetLastError ()));
	ScopedHandle processGuard (process);
	HANDLE token = nullptr;
	if (!OpenProcessToken (process, TOKEN_QUERY, &token))
		return i2p::unexpected ("OpenProcessToken(peer): " + Win32ErrorText (GetLastError ()));
	ScopedHandle tokenGuard (token);

	DWORD userSize = 0;
	GetTokenInformation (token, TokenUser, nullptr, 0, &userSize);
	if (GetLastError () != ERROR_INSUFFICIENT_BUFFER || userSize == 0)
		return i2p::unexpected ("GetTokenInformation(peer user,size): " + Win32ErrorText (GetLastError ()));
	std::vector<BYTE> userBuffer (userSize);
	if (!GetTokenInformation (token, TokenUser, userBuffer.data (), userSize, &userSize))
		return i2p::unexpected ("GetTokenInformation(peer user): " + Win32ErrorText (GetLastError ()));
	auto * user = reinterpret_cast<TOKEN_USER *> (userBuffer.data ());
	LPWSTR sidString = nullptr;
	if (!ConvertSidToStringSidW (user->User.Sid, &sidString))
		return i2p::unexpected ("ConvertSidToStringSid(peer): " + Win32ErrorText (GetLastError ()));
	LocalPtr sidGuard (sidString);

	TOKEN_ELEVATION elevation { };
	DWORD elevationSize = sizeof (elevation);
	const bool elevated = GetTokenInformation (token, TokenElevation, &elevation, elevationSize, &elevationSize)
		&& elevation.TokenIsElevated != 0;
	return WindowsPeer { processId, sessionOk ? sessionId : 0, std::wstring (sidString), elevated };
}

bool WindowsAllowsOperation (WindowsWorkerRole role, MessageType type)
{
	switch (type)
	{
	case MessageType::Ping:
	case MessageType::Pong:
	case MessageType::Shutdown: return true;
	case MessageType::ConfigureLog:
	case MessageType::ConfigureLogFd:
	case MessageType::LogRecord: return role == WindowsWorkerRole::Log || role == WindowsWorkerRole::Supervisor;
	default: return false;
	}
}

WindowsWorkerPolicy GetWindowsWorkerPolicy (WindowsWorkerRole role)
{
	WindowsWorkerPolicy policy;
	switch (role)
	{
	case WindowsWorkerRole::Supervisor:
		policy.allowNetwork = false;
		policy.allowPersistentWrites = false;
		policy.useJobObject = true;
		// Supervisor may own a log child; do not set ActiveProcessLimit=1.
		policy.processLimit = 2;
		break;
	case WindowsWorkerRole::Log:
		policy.allowNetwork = false;
		policy.allowPersistentWrites = false;
		policy.useJobObject = true;
		break;
	case WindowsWorkerRole::Storage:
		// Storage may write only through an explicitly granted directory ACL;
		// it must not have Internet capability.
		policy.allowNetwork = false;
		policy.allowPersistentWrites = true;
		policy.useJobObject = true;
		break;
	case WindowsWorkerRole::Network:
		// Networking needs external sockets but no persistent file capability.
		policy.allowNetwork = true;
		policy.allowPersistentWrites = false;
		policy.useJobObject = true;
		break;
	case WindowsWorkerRole::Crypto:
	case WindowsWorkerRole::Admin:
		policy.allowNetwork = false;
		policy.allowPersistentWrites = false;
		policy.useJobObject = true;
		break;
	}
	return policy;
}

WindowsIsolationSupport ProbeWindowsIsolation ()
{
	WindowsIsolationSupport support;
	const HMODULE kernel = GetModuleHandleW (L"kernel32.dll");
	const HMODULE advapi = GetModuleHandleW (L"advapi32.dll");
	const HMODULE userenv = LoadLibraryW (L"userenv.dll");
	support.restrictedTokens = advapi && GetProcAddress (advapi, "CreateRestrictedToken");
	support.jobObjects = kernel && GetProcAddress (kernel, "CreateJobObjectW");
	support.appContainer = userenv && GetProcAddress (userenv, "CreateAppContainerProfile")
		&& GetProcAddress (userenv, "DeriveAppContainerSidFromAppContainerName");
	// LPAC requires a capability-free AppContainer plus a host-verified
	// mitigation profile. The presence of userenv exports is necessary but not
	// sufficient, so do not report LPAC as available until it is exercised.
	support.lowPrivilegeAppContainer = false;
	support.mitigationPolicies = kernel && GetProcAddress (kernel, "InitializeProcThreadAttributeList")
		&& GetProcAddress (kernel, "UpdateProcThreadAttribute");
	if (userenv) FreeLibrary (userenv);
	support.messageModePipes = true;
	return support;
}

i2p::expected<WindowsHandle, std::string> CreateRestrictedWorkerToken (const WindowsWorkerPolicy & policy)
{
	if (!policy.useRestrictedToken) return i2p::unexpected ("worker token policy requires a restricted token");
	if (policy.useAppContainer || policy.useLowPrivilegeAppContainer)
		return i2p::unexpected (
			"AppContainer/LPAC worker token creation is not implemented; refusing to weaken the policy");
	HANDLE token = nullptr;
	if (!OpenProcessToken (GetCurrentProcess (), TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY | TOKEN_QUERY, &token))
		return i2p::unexpected ("OpenProcessToken: " + Win32ErrorText (GetLastError ()));
	ScopedHandle original (token);
	HANDLE restricted = nullptr;
	if (!CreateRestrictedToken (token, DISABLE_MAX_PRIVILEGE, 0, nullptr, 0, nullptr, 0, nullptr, &restricted))
		return i2p::unexpected ("CreateRestrictedToken: " + Win32ErrorText (GetLastError ()));
	if (!restricted) return i2p::unexpected ("CreateRestrictedToken returned no token");
	return WindowsHandle (restricted);
}

i2p::expected<WindowsHandle, std::string> CreateWorkerJobObject (const WindowsWorkerPolicy & policy)
{
	HANDLE job = CreateJobObjectW (nullptr, nullptr);
	if (!job) return i2p::unexpected ("CreateJobObject: " + Win32ErrorText (GetLastError ()));
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits { };
	limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
	if (policy.processLimit > 0)
	{
		limits.BasicLimitInformation.ActiveProcessLimit = policy.processLimit;
		limits.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
	}
	if (policy.memoryLimit > 0)
	{
		limits.ProcessMemoryLimit = static_cast<SIZE_T> (policy.memoryLimit);
		limits.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_PROCESS_MEMORY;
	}
	if (!SetInformationJobObject (job, JobObjectExtendedLimitInformation, &limits, sizeof (limits)))
	{
		const DWORD error = GetLastError ();
		CloseHandle (job);
		return i2p::unexpected ("SetInformationJobObject: " + Win32ErrorText (error));
	}
	return WindowsHandle (job);
}

i2p::expected<WindowsWorkerProcess, std::string> SpawnWindowsWorker (const std::wstring & executable,
	WindowsWorkerRole role, const std::wstring & pipeName, const WindowsWorkerPolicy & policy)
{
	if (executable.empty ()) return i2p::unexpected ("empty worker executable");
	if (!IsLocalPipeName (pipeName)) return i2p::unexpected ("worker pipe name is not local");
	auto token = CreateRestrictedWorkerToken (policy);
	if (!token) return i2p::unexpected (token.error ());
	auto job = policy.useJobObject ? CreateWorkerJobObject (policy)
								   : i2p::expected<WindowsHandle, std::string> (WindowsHandle ());
	if (!job) return i2p::unexpected (job.error ());

	std::wstring commandLine = L"\"" + executable + L"\" --privsep-worker " + RoleName (role) + L" " + pipeName;
	std::vector<wchar_t> mutableCommandLine (commandLine.begin (), commandLine.end ());
	mutableCommandLine.push_back (L'\0');

	STARTUPINFOEXW startup { };
	startup.StartupInfo.cb = sizeof (startup);
	std::vector<BYTE> attributeBuffer;
	bool attributesInitialized = false;
	if (policy.useMitigationPolicy)
	{
		SIZE_T attributeSize = 0;
		InitializeProcThreadAttributeList (nullptr, 1, 0, &attributeSize);
		attributeBuffer.resize (attributeSize);
		startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST> (attributeBuffer.data ());
		if (!attributeSize || !InitializeProcThreadAttributeList (startup.lpAttributeList, 1, 0, &attributeSize))
			return i2p::unexpected ("InitializeProcThreadAttributeList: " + Win32ErrorText (GetLastError ()));
		attributesInitialized = true;
		// Keep DEP, SEHOP and mandatory ASLR enabled for the child. The
		// DEP-ATL compatibility emulation bit is deliberately not set; it
		// would weaken DEP and is not defined by the MinGW headers.
#ifndef PROCESS_CREATION_MITIGATION_POLICY_DEP_ENABLE
#define PROCESS_CREATION_MITIGATION_POLICY_DEP_ENABLE 0x00000001ULL
#endif
#ifndef PROCESS_CREATION_MITIGATION_POLICY_SEHOP_ENABLE
#define PROCESS_CREATION_MITIGATION_POLICY_SEHOP_ENABLE 0x00000004ULL
#endif
#ifndef PROCESS_CREATION_MITIGATION_POLICY_FORCE_RELOCATE_IMAGES_ALWAYS_ON
#define PROCESS_CREATION_MITIGATION_POLICY_FORCE_RELOCATE_IMAGES_ALWAYS_ON (0x00000001ULL << 8)
#endif
		DWORD64 mitigation = PROCESS_CREATION_MITIGATION_POLICY_DEP_ENABLE
			| PROCESS_CREATION_MITIGATION_POLICY_SEHOP_ENABLE
			| PROCESS_CREATION_MITIGATION_POLICY_FORCE_RELOCATE_IMAGES_ALWAYS_ON;
		if (!UpdateProcThreadAttribute (startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY,
				&mitigation, sizeof (mitigation), nullptr, nullptr))
		{
			DeleteProcThreadAttributeList (startup.lpAttributeList);
			return i2p::unexpected ("UpdateProcThreadAttribute(mitigation): " + Win32ErrorText (GetLastError ()));
		}
	}

	PROCESS_INFORMATION processInfo { };
	const BOOL created = CreateProcessAsUserW (token->Native (), executable.c_str (), mutableCommandLine.data (),
		nullptr, nullptr, FALSE, CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, nullptr,
		nullptr, &startup.StartupInfo, &processInfo);
	if (attributesInitialized) DeleteProcThreadAttributeList (startup.lpAttributeList);
	if (!created) return i2p::unexpected ("CreateProcessAsUser: " + Win32ErrorText (GetLastError ()));

	WindowsWorkerProcess worker;
	worker.process.Reset (processInfo.hProcess);
	worker.thread.Reset (processInfo.hThread);
	worker.job = std::move (*job);
	worker.processId = processInfo.dwProcessId;
	if (policy.useJobObject && !AssignProcessToJobObject (worker.job.Native (), worker.process.Native ()))
	{
		TerminateProcess (worker.process.Native (), ERROR_ACCESS_DENIED);
		return i2p::unexpected ("AssignProcessToJobObject: " + Win32ErrorText (GetLastError ()));
	}
	if (ResumeThread (worker.thread.Native ()) == static_cast<DWORD> (-1))
	{
		TerminateProcess (worker.process.Native (), ERROR_ACCESS_DENIED);
		return i2p::unexpected ("ResumeThread: " + Win32ErrorText (GetLastError ()));
	}
	return worker;
}

i2p::expected<std::wstring, std::string> MakePrivatePipeName (std::string_view endpoint)
{
	const std::wstring normalized = NormalizeEndpoint (endpoint);
	if (normalized.empty ()) return i2p::unexpected ("empty worker endpoint");
	uint64_t hash = 14695981039346656037ULL;
	for (wchar_t ch : normalized)
	{
		hash ^= static_cast<uint64_t> (ch);
		hash *= 1099511628211ULL;
	}
	std::wostringstream name;
	name << L"\\\\.\\pipe\\openi2pd-worker-" << std::hex << std::nouppercase << std::setw (16) << std::setfill (L'0')
		 << hash;
	return name.str ();
}
}

#endif
