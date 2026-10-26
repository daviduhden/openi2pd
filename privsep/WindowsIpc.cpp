/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: WindowsIpc.
 *
 * Implements Windows IPC and confinement: private-DACL named-pipe creation,
 * identity-bound accept, message-mode framed send and receive with correct
 * cancellation reporting, peer identity lookup, and worker spawning with a
 * restricted token or an AppContainer plus a Job Object. An accept is bound to
 * one spawn by PID, session, AppContainer SID and a CSPRNG nonce that the
 * worker proves with a WorkerHello frame before any request is served.
 *
 */

#include "WindowsIpc.h"

#ifdef _WIN32

// The isolation model declares the role table and the AppContainer object this
// translation unit consumes. It includes this header itself, so the cycle is
// resolved by the include guard.
#include "WindowsIsolation.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <aclapi.h>
#include <bcrypt.h>
#include <sddl.h>

#ifndef LUA_TOKEN
#define LUA_TOKEN 0x00000004
#endif
#ifndef JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION
#define JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION 0x00000400
#endif

// Mitigation bits and process-creation attributes that older SDK headers do
// not name. They are optional hardening: the OS ignores a value it does not
// implement, so a fallback definition never weakens another protection.
#ifndef PROCESS_CREATION_MITIGATION_POLICY_HEAP_TERMINATE_ALWAYS_ON
#define PROCESS_CREATION_MITIGATION_POLICY_HEAP_TERMINATE_ALWAYS_ON (0x00000001ULL << 12)
#endif
#ifndef PROCESS_CREATION_MITIGATION_POLICY_BOTTOM_UP_ASLR_ALWAYS_ON
#define PROCESS_CREATION_MITIGATION_POLICY_BOTTOM_UP_ASLR_ALWAYS_ON (0x00000001ULL << 16)
#endif
#ifndef PROCESS_CREATION_MITIGATION_POLICY_HIGH_ENTROPY_ASLR_ALWAYS_ON
#define PROCESS_CREATION_MITIGATION_POLICY_HIGH_ENTROPY_ASLR_ALWAYS_ON (0x00000001ULL << 20)
#endif
#ifndef PROCESS_CREATION_MITIGATION_POLICY_STRICT_HANDLE_CHECKS_ALWAYS_ON
#define PROCESS_CREATION_MITIGATION_POLICY_STRICT_HANDLE_CHECKS_ALWAYS_ON (0x00000001ULL << 24)
#endif
#ifndef PROCESS_CREATION_MITIGATION_POLICY_EXTENSION_POINT_DISABLE_ALWAYS_ON
#define PROCESS_CREATION_MITIGATION_POLICY_EXTENSION_POINT_DISABLE_ALWAYS_ON (0x00000001ULL << 32)
#endif
#ifndef PROCESS_CREATION_MITIGATION_POLICY_PROHIBIT_DYNAMIC_CODE_ALWAYS_ON
#define PROCESS_CREATION_MITIGATION_POLICY_PROHIBIT_DYNAMIC_CODE_ALWAYS_ON (0x00000001ULL << 36)
#endif
#ifndef PROCESS_CREATION_MITIGATION_POLICY_DEP_ENABLE
#define PROCESS_CREATION_MITIGATION_POLICY_DEP_ENABLE 0x00000001ULL
#endif
#ifndef PROCESS_CREATION_MITIGATION_POLICY_SEHOP_ENABLE
#define PROCESS_CREATION_MITIGATION_POLICY_SEHOP_ENABLE 0x00000004ULL
#endif
#ifndef PROCESS_CREATION_MITIGATION_POLICY_FORCE_RELOCATE_IMAGES_ALWAYS_ON
#define PROCESS_CREATION_MITIGATION_POLICY_FORCE_RELOCATE_IMAGES_ALWAYS_ON (0x00000001ULL << 8)
#endif
#ifndef PROCESS_CREATION_MITIGATION_POLICY_CONTROL_FLOW_GUARD_ALWAYS_ON
#define PROCESS_CREATION_MITIGATION_POLICY_CONTROL_FLOW_GUARD_ALWAYS_ON (0x00000001ULL << 40)
#endif
#ifndef PROC_THREAD_ATTRIBUTE_JOB_LIST
#define PROC_THREAD_ATTRIBUTE_JOB_LIST ProcThreadAttributeValue (13, FALSE, TRUE, FALSE)
#endif
#ifndef PROC_THREAD_ATTRIBUTE_HANDLE_LIST
#define PROC_THREAD_ATTRIBUTE_HANDLE_LIST ProcThreadAttributeValue (2, FALSE, TRUE, FALSE)
#endif
#ifndef PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES
#define PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES ProcThreadAttributeValue (9, FALSE, TRUE, FALSE)
#endif
#ifndef PROC_THREAD_ATTRIBUTE_ALL_APPLICATION_PACKAGES_POLICY
#define PROC_THREAD_ATTRIBUTE_ALL_APPLICATION_PACKAGES_POLICY ProcThreadAttributeValue (15, FALSE, TRUE, FALSE)
#endif
#ifndef PROCESS_CREATION_ALL_APPLICATION_PACKAGES_OPT_OUT
#define PROCESS_CREATION_ALL_APPLICATION_PACKAGES_OPT_OUT 0x00000001
#endif
#if defined(NTDDI_WIN10_RS5) && defined(NTDDI_VERSION) && NTDDI_VERSION >= NTDDI_WIN10_RS5
#define I2PD_TOKEN_IS_LESS_PRIVILEGED_APPCONTAINER TokenIsLessPrivilegedAppContainer
#else
// TokenIsLessPrivilegedAppContainer was added in the Windows 10 1809 SDK. The
// information class value is stable ABI, and a failed query is treated as
// "not a low-privilege container" rather than as a fatal error.
#define I2PD_TOKEN_IS_LESS_PRIVILEGED_APPCONTAINER static_cast<TOKEN_INFORMATION_CLASS> (46)
#endif

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
	/** A hostile same-user connect must never exhaust the accept loop. */
	constexpr unsigned MAX_REJECTED_CONNECTIONS = 16;
	constexpr DWORD REJECTED_CONNECTION_BACKOFF_MS = 25;
	constexpr DWORD WORKER_HELLO_TIMEOUT_MS = 5000;
	constexpr DWORD WORKER_CONTROL_TIMEOUT_MS = 5000;
	constexpr DWORD WORKER_CONNECT_TIMEOUT_MS = 10000;

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

	/**
	 * True when a value can be interpolated into a single command-line token.
	 * A quote would break the quoting of the executable and whitespace would
	 * split the token, so a pipe name or role containing either is refused
	 * before a command line is built.
	 */
	bool IsSafeCommandLineToken (const std::wstring & token)
	{
		if (token.empty ()) return false;
		for (wchar_t ch : token)
		{
			if (ch <= 0x20 || ch == 0x7f) return false;
			if (ch == L'"' || ch == L'\'') return false;
		}
		return true;
	}

	i2p::expected<std::string, std::string> NarrowAscii (const std::wstring & text)
	{
		std::string out;
		out.reserve (text.size ());
		for (wchar_t ch : text)
		{
			if (ch <= 0 || ch > 0x7f) return i2p::unexpected ("value is not ASCII");
			out.push_back (static_cast<char> (ch));
		}
		return out;
	}

	i2p::expected<std::wstring, std::string> WidenAscii (std::string_view text)
	{
		std::wstring out;
		out.reserve (text.size ());
		for (char ch : text)
		{
			const auto value = static_cast<unsigned char> (ch);
			if (value == 0 || value > 0x7f) return i2p::unexpected ("value is not ASCII");
			out.push_back (static_cast<wchar_t> (value));
		}
		return out;
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

	bool IsKnownRole (WindowsWorkerRole role)
	{
		switch (role)
		{
		case WindowsWorkerRole::Supervisor:
		case WindowsWorkerRole::Log:
		case WindowsWorkerRole::Storage:
		case WindowsWorkerRole::Network:
		case WindowsWorkerRole::Crypto:
		case WindowsWorkerRole::Admin: return true;
		}
		return false;
	}

	std::optional<WindowsWorkerRole> RoleFromName (std::string_view name)
	{
		static constexpr std::pair<std::string_view, WindowsWorkerRole> roles[] = {
			{ "supervisor", WindowsWorkerRole::Supervisor },
			{ "log", WindowsWorkerRole::Log },
			{ "storage", WindowsWorkerRole::Storage },
			{ "network", WindowsWorkerRole::Network },
			{ "crypto", WindowsWorkerRole::Crypto },
			{ "admin", WindowsWorkerRole::Admin },
		};
		for (const auto & entry : roles)
			if (entry.first == name) return entry.second;
		return std::nullopt;
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

	/**
	 * Validate one SID string before it is interpolated into an SDDL DACL.
	 * The SDDL grammar is text, so a `)` or `;` in the value would close the
	 * ACE and let a caller inject unrelated access.
	 */
	bool IsValidSidString (const std::wstring & sid)
	{
		if (sid.size () < 4 || sid.size () > 184) return false;
		if (sid.compare (0, 4, L"S-1-") != 0) return false;
		for (wchar_t ch : sid)
		{
			if (ch == L'(' || ch == L')' || ch == L';' || ch == L'"' || ch == L'\'' || ch <= 0x20 || ch == 0x7f)
				return false;
		}
		return true;
	}

	bool BuildPipeSecurity (const std::wstring & userSid, const std::wstring & extraSid,
		PSECURITY_DESCRIPTOR & descriptor, std::string & error)
	{
		if (userSid.empty ())
		{
			error = "empty process user SID";
			return false;
		}
		// Protected DACL: only the service user and SYSTEM. Administrators are
		// not added implicitly; a deployment that needs an administrator must
		// grant that explicitly through the service account model. An
		// AppContainer worker gets exactly one additional ACE for its own SID.
		// There is no Everyone, Authenticated Users or anonymous ACE.
		std::wstring sddl = L"D:P(A;;GA;;;" + userSid + L")(A;;GA;;;SY)";
		if (!extraSid.empty ())
		{
			if (!IsValidSidString (extraSid))
			{
				error = "invalid extra pipe DACL SID";
				return false;
			}
			sddl += L"(A;;GA;;;" + extraSid + L")";
		}
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

	/**
	 * Outcome of one overlapped operation.
	 *
	 * `completed` means the operation finished successfully with `transferred`
	 * bytes and the record must be delivered. `cancelled` means the deadline
	 * expired and the operation was reaped without data. Anything else sets
	 * `error`.
	 */
	struct OverlappedOutcome
	{
		bool completed = false;
		bool cancelled = false;
		DWORD transferred = 0;
		DWORD error = 0;
	};

	/**
	 * Wait for one overlapped pipe operation and reap it before returning.
	 *
	 * A record that completed in the same instant as its deadline must not be
	 * discarded: message-mode pipes transfer whole records, so a completion
	 * observed after CancelIoEx is real data rather than a partial write. Only
	 * ERROR_OPERATION_ABORTED, ERROR_NOT_FOUND or a successful completion with
	 * zero bytes map to `cancelled`, and `emptyCompletionIsSuccess` marks the
	 * operations (ConnectNamedPipe) whose successful completion transfers no
	 * bytes at all.
	 *
	 * The OVERLAPPED and the caller's buffer stay owned by the kernel until the
	 * operation is reaped, so the post-cancel wait is unconditional: returning
	 * earlier would hand the kernel a dangling stack object.
	 */
	OverlappedOutcome WaitOverlapped (
		HANDLE pipe, OVERLAPPED & overlapped, DWORD timeoutMs, bool emptyCompletionIsSuccess = false)
	{
		OverlappedOutcome outcome;
		const DWORD wait = WaitForSingleObject (overlapped.hEvent, timeoutMs);
		if (wait == WAIT_OBJECT_0)
		{
			if (GetOverlappedResult (pipe, &overlapped, &outcome.transferred, FALSE))
			{
				outcome.completed = true;
				return outcome;
			}
			outcome.error = GetLastError ();
			return outcome;
		}
		if (wait == WAIT_FAILED)
		{
			outcome.error = GetLastError ();
			return outcome;
		}
		// Deadline expired. CancelIoEx is asynchronous and races with a
		// completion that is already queued, so the result is inspected
		// afterwards instead of being thrown away.
		(void)CancelIoEx (pipe, &overlapped);
		(void)WaitForSingleObject (overlapped.hEvent, INFINITE);
		DWORD transferred = 0;
		if (GetOverlappedResult (pipe, &overlapped, &transferred, FALSE))
		{
			if (transferred > 0 || emptyCompletionIsSuccess)
			{
				outcome.completed = true;
				outcome.transferred = transferred;
			}
			else
				outcome.cancelled = true;
			return outcome;
		}
		const DWORD error = GetLastError ();
		if (error == ERROR_OPERATION_ABORTED || error == ERROR_NOT_FOUND)
		{
			outcome.cancelled = true;
			return outcome;
		}
		outcome.error = error;
		return outcome;
	}

	/**
	 * The Windows pipe transport has no descriptor channel, so a type whose
	 * registry row requires a descriptor can never be satisfied here.
	 */
	bool RequiresDescriptorChannel (MessageType type)
	{
		switch (DescriptorPolicyFor (type))
		{
		case DescriptorPolicy::RequiredFile:
		case DescriptorPolicy::RequiredDirectory:
		case DescriptorPolicy::RequiredSocket: return true;
		case DescriptorPolicy::None:
		case DescriptorPolicy::OptionalFile:
		case DescriptorPolicy::OptionalDirectory:
		case DescriptorPolicy::OptionalSocket: return false;
		}
		return false;
	}

	/** Compare two nonces without an early exit on the first differing byte. */
	bool ConstantTimeEqual (std::string_view left, std::string_view right)
	{
		if (left.size () != right.size ()) return false;
		unsigned char difference = 0;
		for (size_t i = 0; i < left.size (); ++i)
			difference |= static_cast<unsigned char> (left[i]) ^ static_cast<unsigned char> (right[i]);
		return difference == 0;
	}

	bool SidToString (PSID sid, std::wstring & out)
	{
		if (!sid) return false;
		LPWSTR text = nullptr;
		if (!ConvertSidToStringSidW (sid, &text)) return false;
		LocalPtr guard (text);
		out.assign (text);
		return true;
	}

	bool TokenUserSid (HANDLE token, std::wstring & out)
	{
		DWORD size = 0;
		GetTokenInformation (token, TokenUser, nullptr, 0, &size);
		if (GetLastError () != ERROR_INSUFFICIENT_BUFFER || size == 0) return false;
		std::vector<BYTE> buffer (size);
		if (!GetTokenInformation (token, TokenUser, buffer.data (), size, &size)) return false;
		auto * user = reinterpret_cast<TOKEN_USER *> (buffer.data ());
		return SidToString (user->User.Sid, out);
	}

	bool TokenDword (HANDLE token, TOKEN_INFORMATION_CLASS infoClass, DWORD & value)
	{
		DWORD size = sizeof (value);
		return GetTokenInformation (token, infoClass, &value, size, &size) != FALSE;
	}

	bool TokenAppContainerSid (HANDLE token, std::wstring & out)
	{
		DWORD size = 0;
		GetTokenInformation (token, ::TokenAppContainerSid, nullptr, 0, &size);
		if (GetLastError () != ERROR_INSUFFICIENT_BUFFER || size == 0) return false;
		std::vector<BYTE> buffer (size);
		if (!GetTokenInformation (token, ::TokenAppContainerSid, buffer.data (), size, &size)) return false;
		auto * info = reinterpret_cast<TOKEN_APPCONTAINER_INFORMATION *> (buffer.data ());
		return SidToString (info->TokenAppContainer, out);
	}

	bool TokenIntegrityLevel (HANDLE token, DWORD & rid)
	{
		DWORD size = 0;
		GetTokenInformation (token, ::TokenIntegrityLevel, nullptr, 0, &size);
		if (GetLastError () != ERROR_INSUFFICIENT_BUFFER || size == 0) return false;
		std::vector<BYTE> buffer (size);
		if (!GetTokenInformation (token, ::TokenIntegrityLevel, buffer.data (), size, &size)) return false;
		auto * label = reinterpret_cast<TOKEN_MANDATORY_LABEL *> (buffer.data ());
		if (!label->Label.Sid) return false;
		const auto * count = GetSidSubAuthorityCount (label->Label.Sid);
		if (!count || *count == 0) return false;
		const auto * subAuthority = GetSidSubAuthority (label->Label.Sid, *count - 1);
		if (!subAuthority) return false;
		rid = *subAuthority;
		return true;
	}

	/**
	 * Read the identity of the client end of a connected message-mode pipe.
	 *
	 * Impersonation is limited to the level the client granted (the Windows
	 * channel connects with SECURITY_IDENTIFICATION), which is enough to read
	 * the client token but not to act on its behalf. RevertToSelf always runs
	 * before this function returns; a failure to revert is reported as fatal
	 * because the calling thread would otherwise keep the client identity.
	 */
	i2p::expected<WindowsPeer, std::string> QueryPipeClient (HANDLE pipe, bool & fatal)
	{
		fatal = false;
		WindowsPeer peer;
		if (!GetNamedPipeClientProcessId (pipe, &peer.processId))
			return i2p::unexpected ("GetNamedPipeClientProcessId: " + Win32ErrorText (GetLastError ()));
		if (!GetNamedPipeClientSessionId (pipe, &peer.sessionId))
			return i2p::unexpected ("GetNamedPipeClientSessionId: " + Win32ErrorText (GetLastError ()));
		if (!ImpersonateNamedPipeClient (pipe))
			return i2p::unexpected ("ImpersonateNamedPipeClient: " + Win32ErrorText (GetLastError ()));
		HANDLE token = nullptr;
		const BOOL opened = OpenThreadToken (GetCurrentThread (), TOKEN_QUERY, FALSE, &token);
		const DWORD openError = opened ? 0 : GetLastError ();
		if (!RevertToSelf ())
		{
			if (opened && token) CloseHandle (token);
			fatal = true;
			return i2p::unexpected ("RevertToSelf: " + Win32ErrorText (GetLastError ()));
		}
		if (!opened || !token) return i2p::unexpected ("OpenThreadToken(worker peer): " + Win32ErrorText (openError));
		ScopedHandle tokenGuard (token);

		if (!TokenUserSid (token, peer.userSid))
			return i2p::unexpected ("GetTokenInformation(peer user): " + Win32ErrorText (GetLastError ()));
		DWORD flag = 0;
		peer.appContainer = TokenDword (token, TokenIsAppContainer, flag) && flag != 0;
		peer.lowPrivilegeAppContainer
			= TokenDword (token, I2PD_TOKEN_IS_LESS_PRIVILEGED_APPCONTAINER, flag) && flag != 0;
		if (peer.appContainer && !TokenAppContainerSid (token, peer.appContainerSid))
			return i2p::unexpected ("GetTokenInformation(peer AppContainer SID): " + Win32ErrorText (GetLastError ()));
		// An unreadable integrity level is not a rejection on its own: the
		// value is recorded for the supervisor's policy, and the AppContainer
		// SID comparison above is the identity boundary.
		(void)TokenIntegrityLevel (token, peer.integrityLevel);
		TOKEN_ELEVATION elevation { };
		DWORD elevationSize = sizeof (elevation);
		peer.elevated = GetTokenInformation (token, TokenElevation, &elevation, elevationSize, &elevationSize)
			&& elevation.TokenIsElevated != 0;
		return peer;
	}

	/** Consume and validate the first frame of a freshly accepted connection. */
	bool ReceiveWorkerHello (
		WindowsChannel & channel, WindowsWorkerRole expectedRole, std::string_view nonce, std::string & error)
	{
		auto received = channel.Receive (WORKER_HELLO_TIMEOUT_MS);
		if (!received)
		{
			error = received.error ();
			return false;
		}
		if (!received->has_value ())
		{
			error = "worker did not send an identity handshake";
			return false;
		}
		auto decoded = DecodeWorkerHelloFrame (**received);
		if (!decoded)
		{
			error = "worker identity handshake: " + decoded.error ();
			return false;
		}
		if (decoded->first != expectedRole)
		{
			error = "worker identity role does not match the spawned role";
			return false;
		}
		if (!ConstantTimeEqual (decoded->second, nonce))
		{
			error = "worker identity nonce does not match the spawn nonce";
			return false;
		}
		return true;
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

std::wstring EncodeWindowsWorkerNonce (std::string_view nonce)
{
	if (nonce.size () != WINDOWS_WORKER_NONCE_SIZE) return { };
	std::wstring out;
	out.reserve (nonce.size () * 2);
	const wchar_t digits[] = L"0123456789abcdef";
	for (char raw : nonce)
	{
		const auto byte = static_cast<unsigned char> (raw);
		out.push_back (digits[byte >> 4]);
		out.push_back (digits[byte & 0x0f]);
	}
	return out;
}

i2p::expected<std::string, std::string> DecodeWindowsWorkerNonce (std::string_view hex)
{
	if (hex.size () != WINDOWS_WORKER_NONCE_SIZE * 2)
		return i2p::unexpected ("worker nonce must be 32 hex-encoded bytes");
	auto nibble = [] (char ch) -> int
	{
		if (ch >= '0' && ch <= '9') return ch - '0';
		if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
		if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
		return -1;
	};
	std::string out;
	out.reserve (WINDOWS_WORKER_NONCE_SIZE);
	bool nonZero = false;
	for (size_t i = 0; i < hex.size (); i += 2)
	{
		const int high = nibble (hex[i]);
		const int low = nibble (hex[i + 1]);
		if (high < 0 || low < 0) return i2p::unexpected ("worker nonce is not hexadecimal");
		const auto byte = static_cast<unsigned char> ((high << 4) | low);
		nonZero = nonZero || byte != 0;
		out.push_back (static_cast<char> (byte));
	}
	// An all-zero nonce is predictable and would make the handshake worthless.
	if (!nonZero) return i2p::unexpected ("worker nonce must not be all zero");
	return out;
}

i2p::expected<std::wstring, std::string> GenerateWindowsWorkerNonce ()
{
	std::string nonce (WINDOWS_WORKER_NONCE_SIZE, '\0');
	const NTSTATUS status = BCryptGenRandom (nullptr, reinterpret_cast<PUCHAR> (nonce.data ()),
		static_cast<ULONG> (nonce.size ()), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
	if (status < 0) return i2p::unexpected ("BCryptGenRandom failed");
	// Reject an all-zero draw instead of shipping a predictable nonce.
	bool nonZero = false;
	for (char byte : nonce) nonZero = nonZero || byte != 0;
	if (!nonZero) return i2p::unexpected ("random worker nonce was all zero");
	const std::wstring hex = EncodeWindowsWorkerNonce (nonce);
	if (hex.empty ()) return i2p::unexpected ("cannot encode worker nonce");
	return hex;
}

i2p::expected<std::string, std::string> EncodeWorkerHelloFrame (WindowsWorkerRole role, std::string_view nonce)
{
	if (!IsKnownRole (role)) return i2p::unexpected ("unknown worker role");
	if (nonce.size () != WINDOWS_WORKER_NONCE_SIZE) return i2p::unexpected ("worker hello nonce must be 32 bytes");
	bool nonZero = false;
	for (char byte : nonce) nonZero = nonZero || byte != 0;
	if (!nonZero) return i2p::unexpected ("worker hello nonce must not be all zero");
	// The payload is built here rather than by a shared Protocol helper because
	// only the Windows pipe transport carries it; the frame bound itself comes
	// from the message registry.
	PayloadWriter writer;
	writer.WriteU8 (static_cast<uint8_t> (role));
	writer.WriteU8 (static_cast<uint8_t> (nonce.size ()));
	writer.WriteRaw (nonce);
	return writer.Data ();
}

i2p::expected<std::pair<WindowsWorkerRole, std::string>, std::string> DecodeWorkerHelloFrame (const Message & message)
{
	if (message.type != MessageType::WorkerHello) return i2p::unexpected ("frame is not a worker hello");
	PayloadReader reader (message.payload);
	auto role = reader.ReadU8 ();
	if (!role) return i2p::unexpected ("worker hello is truncated");
	auto length = reader.ReadU8 ();
	if (!length) return i2p::unexpected ("worker hello is truncated");
	if (*length != WINDOWS_WORKER_NONCE_SIZE) return i2p::unexpected ("worker hello nonce length is invalid");
	auto nonce = reader.ReadRaw (*length);
	if (!nonce) return i2p::unexpected ("worker hello nonce is truncated");
	if (!reader.Done ()) return i2p::unexpected ("worker hello has trailing data");
	if (!IsKnownRole (static_cast<WindowsWorkerRole> (*role))) return i2p::unexpected ("worker hello role is unknown");
	bool nonZero = false;
	for (char byte : *nonce) nonZero = nonZero || byte != 0;
	if (!nonZero) return i2p::unexpected ("worker hello nonce is all zero");
	return std::pair { static_cast<WindowsWorkerRole> (*role), std::string (*nonce) };
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

i2p::expected<WindowsServer, std::string> WindowsServer::Create (
	const std::wstring & pipeName, const std::wstring & extraSid)
{
	if (!IsLocalPipeName (pipeName)) return i2p::unexpected ("worker pipe name is not local \\\\.\\pipe\\...");
	if (!extraSid.empty () && !IsValidSidString (extraSid))
		return i2p::unexpected ("worker pipe DACL SID is not a valid SID string");
	std::string error;
	const std::wstring userSid = CurrentUserSid (error);
	if (userSid.empty ()) return i2p::unexpected (error);
	PSECURITY_DESCRIPTOR descriptor = nullptr;
	if (!BuildPipeSecurity (userSid, extraSid, descriptor, error)) return i2p::unexpected (error);

	SECURITY_ATTRIBUTES attributes { };
	attributes.nLength = sizeof (attributes);
	attributes.lpSecurityDescriptor = descriptor;
	attributes.bInheritHandle = FALSE;

	const DWORD openMode = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE;
	const DWORD pipeMode = PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS;
	// nOutBufferSize is the buffer for records the client sends. One byte above
	// the frame maximum lets the receiver distinguish "exactly the maximum
	// frame" from "one byte too large" without a dead zone.
	const HANDLE pipe = CreateNamedPipeW (pipeName.c_str (), openMode, pipeMode, PIPE_MAX_INSTANCES, PIPE_BUFFER_SIZE,
		PIPE_BUFFER_SIZE + 1, 0, &attributes);
	if (pipe == INVALID_HANDLE_VALUE)
	{
		LocalFree (descriptor);
		return i2p::unexpected ("CreateNamedPipe: " + Win32ErrorText (GetLastError ()));
	}
	return WindowsServer (pipe, pipeName, userSid, descriptor);
}

i2p::expected<bool, std::string> WindowsServer::WaitForConnection (DWORD timeoutMs)
{
	ScopedHandle event (CreateEventW (nullptr, TRUE, FALSE, nullptr));
	if (!event) return i2p::unexpected ("CreateEvent(pipe accept): " + Win32ErrorText (GetLastError ()));

	OVERLAPPED overlapped { };
	overlapped.hEvent = event.get ();
	BOOL connected = ConnectNamedPipe (m_Pipe, &overlapped);
	if (!connected)
	{
		const DWORD error = GetLastError ();
		if (error == ERROR_PIPE_CONNECTED) return true;
		if (error != ERROR_IO_PENDING) return i2p::unexpected ("ConnectNamedPipe: " + Win32ErrorText (error));
		const auto outcome = WaitOverlapped (m_Pipe, overlapped, timeoutMs, true /* a connect transfers no bytes */);
		if (!outcome.completed)
		{
			DisconnectNamedPipe (m_Pipe);
			if (outcome.cancelled) return false;
			return i2p::unexpected ("worker pipe accept: " + Win32ErrorText (outcome.error));
		}
	}
	return true;
}

i2p::expected<WindowsChannel, std::string> WindowsServer::Accept (DWORD timeoutMs)
{
	// Test-only legacy path: it binds the peer user SID and nothing else. Every
	// production caller uses AcceptBound(), which also proves the spawn nonce,
	// the PID/session and the AppContainer identity.
	if (m_Pipe == INVALID_HANDLE_VALUE) return i2p::unexpected ("worker pipe is closed");
	const auto deadline = std::chrono::steady_clock::now () + std::chrono::milliseconds (timeoutMs);
	unsigned rejected = 0;
	while (true)
	{
		const auto now = std::chrono::steady_clock::now ();
		if (now >= deadline) return i2p::unexpected ("worker pipe accept timeout");
		const DWORD remaining
			= static_cast<DWORD> (std::chrono::duration_cast<std::chrono::milliseconds> (deadline - now).count ());
		auto connected = WaitForConnection (remaining);
		if (!connected) return i2p::unexpected (connected.error ());
		if (!*connected) return i2p::unexpected ("worker pipe accept timeout");

		WindowsChannel candidate (m_Pipe, true);
		auto peer = candidate.Peer ();
		if (!peer)
		{
			candidate.Release ();
			DisconnectNamedPipe (m_Pipe);
			return i2p::unexpected (peer.error ());
		}
		if (peer->userSid != m_UserSid)
		{
			// Reject this connection but keep the listener usable for the real
			// worker; a hostile same-user connect must not be a DoS, and the
			// rejection budget bounds the work it can cause.
			candidate.Release ();
			DisconnectNamedPipe (m_Pipe);
			if (++rejected >= MAX_REJECTED_CONNECTIONS) return i2p::unexpected ("too many rejected worker connections");
			Sleep (REJECTED_CONNECTION_BACKOFF_MS);
			continue;
		}
		m_Pipe = INVALID_HANDLE_VALUE;
		return candidate;
	}
}

// Identity is checked in a fixed order, and each step is an independent
// boundary: the pipe-reported PID/session name the connecting process, the
// impersonated client token proves the user SID and the AppContainer identity,
// and only then does the WorkerHello nonce prove that this is the child the
// supervisor spawned for this generation. A rejected connection is closed and
// the listener is re-armed, because a same-user squatter must not be able to
// deny the real worker; the rejection budget bounds how much work that costs.
i2p::expected<WindowsChannel, std::string> WindowsServer::AcceptBound (DWORD timeoutMs, DWORD expectedProcessId,
	DWORD expectedSessionId, WindowsWorkerRole role, const std::wstring & nonceHex,
	const WindowsAppContainer * container)
{
	if (m_Pipe == INVALID_HANDLE_VALUE) return i2p::unexpected ("worker pipe is closed");
	// Both identifiers are mandatory: a zero value would silently accept any
	// same-user process that guessed the pipe name.
	if (expectedProcessId == 0 || expectedSessionId == 0)
		return i2p::unexpected ("worker accept requires the spawn PID and session");
	if (!IsKnownRole (role)) return i2p::unexpected ("worker accept requires a known worker role");
	const auto nonceText = NarrowAscii (nonceHex);
	if (!nonceText) return i2p::unexpected ("worker accept nonce is not ASCII hex");
	auto nonce = DecodeWindowsWorkerNonce (*nonceText);
	if (!nonce) return i2p::unexpected ("worker accept nonce: " + nonce.error ());
	if (container && !container->IsValid ()) return i2p::unexpected ("expected worker AppContainer is not valid");
	const bool containerExpected = container != nullptr;

	const auto deadline = std::chrono::steady_clock::now () + std::chrono::milliseconds (timeoutMs);
	unsigned rejected = 0;
	while (true)
	{
		const auto now = std::chrono::steady_clock::now ();
		if (now >= deadline) return i2p::unexpected ("worker pipe accept timeout");
		const DWORD remaining
			= static_cast<DWORD> (std::chrono::duration_cast<std::chrono::milliseconds> (deadline - now).count ());
		auto connected = WaitForConnection (remaining);
		if (!connected) return i2p::unexpected (connected.error ());
		if (!*connected) return i2p::unexpected ("worker pipe accept timeout");

		WindowsChannel candidate (m_Pipe, true);
		bool fatal = false;
		auto peer = QueryPipeClient (m_Pipe, fatal);
		std::string rejection;
		if (!peer)
		{
			if (fatal)
			{
				// The thread could not leave the client identity, so this
				// listener cannot continue safely.
				candidate.Release ();
				DisconnectNamedPipe (m_Pipe);
				return i2p::unexpected (peer.error ());
			}
			rejection = peer.error ();
		}
		else if (peer->userSid != m_UserSid)
			rejection = "worker pipe peer user does not match the service user";
		else if (peer->processId != expectedProcessId)
			rejection = "worker pipe peer PID does not match the spawned worker";
		else if (peer->sessionId != expectedSessionId)
			rejection = "worker pipe peer session does not match the spawned worker";
		else if (peer->appContainer != containerExpected)
			rejection = "worker pipe peer AppContainer state does not match the spawn policy";
		else if (containerExpected && peer->appContainerSid != container->SidString ())
			rejection = "worker pipe peer AppContainer SID does not match";
		if (rejection.empty ())
		{
			std::string helloError;
			if (!ReceiveWorkerHello (candidate, role, *nonce, helloError)) rejection = helloError;
		}
		if (!rejection.empty ())
		{
			candidate.Release ();
			DisconnectNamedPipe (m_Pipe);
			if (++rejected >= MAX_REJECTED_CONNECTIONS) return i2p::unexpected (rejection);
			Sleep (REJECTED_CONNECTION_BACKOFF_MS);
			continue;
		}
		// Record the spawned role for diagnostics, but authorize this side as
		// the request-issuing endpoint: it receives replies (Pong/Error), not
		// the worker's request types.
		candidate.SetRole (role);
		candidate.SetCoreRole ();
		m_Pipe = INVALID_HANDLE_VALUE;
		return candidate;
	}
}

WindowsChannel::~WindowsChannel ()
{
	Close ();
}

WindowsChannel::WindowsChannel (WindowsChannel && other) noexcept
	: m_Pipe (other.m_Pipe)
	, m_ServerSide (other.m_ServerSide)
	, m_Role (other.m_Role)
	, m_RoleSet (other.m_RoleSet)
	, m_CoreSide (other.m_CoreSide)
	, m_ReadEvent (std::move (other.m_ReadEvent))
	, m_ReadBuffer (std::move (other.m_ReadBuffer))
	, m_ProtocolViolations (other.m_ProtocolViolations)
{
	other.m_Pipe = INVALID_HANDLE_VALUE;
	other.m_ServerSide = false;
	other.m_RoleSet = false;
	other.m_CoreSide = false;
	other.m_ProtocolViolations = 0;
}

WindowsChannel & WindowsChannel::operator= (WindowsChannel && other) noexcept
{
	if (this != &other)
	{
		Close ();
		m_Pipe = other.m_Pipe;
		m_ServerSide = other.m_ServerSide;
		m_ReadEvent = std::move (other.m_ReadEvent);
		m_ReadBuffer = std::move (other.m_ReadBuffer);
		m_Role = other.m_Role;
		m_RoleSet = other.m_RoleSet;
		m_CoreSide = other.m_CoreSide;
		m_ProtocolViolations = other.m_ProtocolViolations;
		other.m_Pipe = INVALID_HANDLE_VALUE;
		other.m_ServerSide = false;
		other.m_RoleSet = false;
		other.m_CoreSide = false;
		other.m_ProtocolViolations = 0;
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
	m_ReadEvent.Reset ();
	m_ReadBuffer.clear ();
}

i2p::expected<WindowsChannel, std::string> WindowsChannel::Connect (const std::wstring & pipeName, DWORD timeoutMs)
{
	return ConnectBound (pipeName, 0, 0, timeoutMs);
}

// A zero PID or session disables that half of the binding, which is the test
// seam used by Connect()/Accept(); RunWindowsWorker always passes both. The
// retry loop keeps the same absolute deadline across WaitNamedPipe and
// CreateFile, so a busy or not-yet-created pipe cannot extend the caller's
// budget, and any other error is terminal instead of retried.
i2p::expected<WindowsChannel, std::string> WindowsChannel::ConnectBound (
	const std::wstring & pipeName, DWORD expectedServerProcessId, DWORD expectedSessionId, DWORD timeoutMs)
{
	if (!IsLocalPipeName (pipeName)) return i2p::unexpected ("worker pipe name is not local");
	const auto deadline = std::chrono::steady_clock::now () + std::chrono::milliseconds (timeoutMs);
	HANDLE pipe = INVALID_HANDLE_VALUE;
	DWORD lastError = ERROR_FILE_NOT_FOUND;
	while (true)
	{
		// SECURITY_IDENTIFICATION lets the server identify this process (and
		// read its token) but never act on its behalf.
		pipe = CreateFileW (pipeName.c_str (), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
			FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
		if (pipe != INVALID_HANDLE_VALUE) break;
		lastError = GetLastError ();
		const auto now = std::chrono::steady_clock::now ();
		if (now >= deadline) break;
		if (lastError != ERROR_PIPE_BUSY && lastError != ERROR_FILE_NOT_FOUND)
			return i2p::unexpected ("CreateFile(worker pipe): " + Win32ErrorText (lastError));
		const DWORD remaining
			= static_cast<DWORD> (std::chrono::duration_cast<std::chrono::milliseconds> (deadline - now).count ());
		WaitNamedPipeW (pipeName.c_str (), std::min<DWORD> (remaining, 100));
		Sleep (10);
	}
	if (pipe == INVALID_HANDLE_VALUE)
		return i2p::unexpected ("WaitNamedPipe/CreateFile: " + Win32ErrorText (lastError));
	WindowsChannel channel (pipe);
	auto mode = SetPipeMessageMode (pipe);
	if (!mode)
	{
		channel.Close ();
		return i2p::unexpected (mode.error ());
	}
	if (expectedServerProcessId != 0 || expectedSessionId != 0)
	{
		auto peer = channel.Peer ();
		if (!peer)
		{
			channel.Close ();
			return i2p::unexpected (peer.error ());
		}
		if ((expectedServerProcessId != 0 && peer->processId != expectedServerProcessId)
			|| (expectedSessionId != 0 && peer->sessionId != expectedSessionId))
		{
			channel.Close ();
			return i2p::unexpected ("worker pipe server PID/session does not match the expected supervisor");
		}
	}
	return channel;
}

i2p::expected<void, std::string> WindowsChannel::Send (const Message & message, DWORD timeoutMs)
{
	if (!IsOpen ()) return i2p::unexpected ("worker pipe is closed");
	// Direction is part of the role contract: the request-issuing side sends
	// requests, a bound worker sends replies. WorkerHello is the one request a
	// worker sends, and it is written before the role is bound.
	if (m_RoleSet)
	{
		const bool allowed = m_CoreSide ? IsRequestType (message.type) : IsReplyType (message.type);
		if (!allowed) return i2p::unexpected ("message direction is not allowed on this endpoint");
	}
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
		const auto outcome = WaitOverlapped (m_Pipe, overlapped, timeoutMs);
		if (outcome.completed)
			written = outcome.transferred;
		else if (outcome.cancelled)
		{
			// A cancelled message-mode write is never retried on a fresh
			// channel: the record may already have been delivered, and a
			// second copy would desynchronize the request/reply correlation.
			Close ();
			return i2p::unexpected ("worker pipe send timeout");
		}
		else
		{
			Close ();
			return i2p::unexpected ("worker pipe send: " + Win32ErrorText (outcome.error));
		}
	}
	if (static_cast<size_t> (written) != encoded->size ())
	{
		Close ();
		return i2p::unexpected ("worker pipe short message write");
	}
	return { };
}

i2p::expected<std::optional<Message>, std::string> WindowsChannel::Receive (DWORD timeoutMs)
{
	if (!IsOpen ()) return i2p::unexpected ("worker pipe is closed");
	// One byte over the maximum legal frame: every larger message-mode record
	// raises ERROR_MORE_DATA and is terminal, with no 16-byte dead zone.
	if (m_ReadBuffer.size () != MAX_MESSAGE_SIZE + 1) m_ReadBuffer.assign (MAX_MESSAGE_SIZE + 1, '\0');
	if (!m_ReadEvent.IsValid ())
	{
		m_ReadEvent.Reset (CreateEventW (nullptr, TRUE, FALSE, nullptr));
		if (!m_ReadEvent.IsValid ())
			return i2p::unexpected ("CreateEvent(receive): " + Win32ErrorText (GetLastError ()));
	}
	if (!ResetEvent (m_ReadEvent.Native ()))
		return i2p::unexpected ("ResetEvent(receive): " + Win32ErrorText (GetLastError ()));

	OVERLAPPED overlapped { };
	overlapped.hEvent = m_ReadEvent.Native ();
	DWORD read = 0;
	BOOL result
		= ReadFile (m_Pipe, m_ReadBuffer.data (), static_cast<DWORD> (m_ReadBuffer.size ()), &read, &overlapped);
	if (!result)
	{
		const DWORD error = GetLastError ();
		if (error == ERROR_MORE_DATA)
		{
			Close ();
			return i2p::unexpected ("worker pipe message is too large");
		}
		if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED)
		{
			Close ();
			return i2p::unexpected ("worker pipe closed");
		}
		if (error != ERROR_IO_PENDING) return i2p::unexpected ("ReadFile(worker pipe): " + Win32ErrorText (error));
		const auto outcome = WaitOverlapped (m_Pipe, overlapped, timeoutMs);
		if (outcome.completed)
			read = outcome.transferred;
		else if (outcome.cancelled)
		{
			// An idle pipe is not a broken pipe. Every worker polls with a
			// finite timeout, so closing here would terminate it after one
			// quiet period. A record that completed during the cancellation is
			// reported as `completed` above and was not discarded.
			return std::optional<Message> { };
		}
		else if (outcome.error == ERROR_MORE_DATA)
		{
			Close ();
			return i2p::unexpected ("worker pipe message is too large");
		}
		else if (outcome.error == ERROR_BROKEN_PIPE || outcome.error == ERROR_PIPE_NOT_CONNECTED)
		{
			Close ();
			return i2p::unexpected ("worker pipe closed");
		}
		else
		{
			Close ();
			return i2p::unexpected ("worker pipe receive: " + Win32ErrorText (outcome.error));
		}
	}
	if (read == 0)
	{
		Close ();
		return i2p::unexpected ("worker pipe closed");
	}
	const std::string_view frame (m_ReadBuffer.data (), read);
	auto decoded = Decode (frame);
	if (!decoded)
	{
		// Match the POSIX endpoint contract: an attributable frame fault is
		// answered once with a typed Error and the channel stays usable, up
		// to a bounded budget. A corrupt header or a credential failure is
		// still terminal.
		const auto inspection = InspectFrame (frame);
		const bool attributable = inspection.headerPresent && inspection.magicValid
			&& inspection.version >= MIN_PROTOCOL_VERSION && inspection.version <= PROTOCOL_VERSION
			&& inspection.lengthConsistent;
		if (attributable && IsRecoverableFrameFault (decoded.error ())
			&& ++m_ProtocolViolations <= MAX_WINDOWS_PROTOCOL_VIOLATIONS)
		{
			const auto offending = static_cast<MessageType> (inspection.rawType);
			(void)Send ({ MessageType::Error, inspection.requestId,
							EncodeError (offending, decoded.error ().code, decoded.error ().text) },
				250);
			return std::optional<Message> { };
		}
		Close ();
		return i2p::unexpected ("protocol: " + std::string (decoded.error ().text));
	}
	// Authorization happens after framing: a type this endpoint may not
	// receive, or one that requires a descriptor channel the Windows transport
	// does not have, is a bounded typed refusal rather than a silent drop.
	std::string_view refusal;
	const bool unauthorized = m_RoleSet
		&& !(m_CoreSide ? AllowsOperation (EndpointRole::Core, decoded->type)
						: WindowsAllowsOperation (m_Role, decoded->type));
	if (RequiresDescriptorChannel (decoded->type))
		refusal = "descriptor-carrying messages are not supported on the Windows pipe transport";
	else if (unauthorized)
		refusal = "operation is not allowed on this endpoint";
	if (!refusal.empty ())
	{
		if (++m_ProtocolViolations <= MAX_WINDOWS_PROTOCOL_VIOLATIONS)
		{
			(void)Send ({ MessageType::Error, decoded->requestId,
							EncodeError (decoded->type, ErrorCode::NotAuthorized, refusal) },
				250);
			return std::optional<Message> { };
		}
		Close ();
		return i2p::unexpected ("worker pipe authorization: " + std::string (refusal));
	}
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

	WindowsPeer peer;
	peer.processId = processId;
	peer.sessionId = sessionOk ? sessionId : 0;
	if (!TokenUserSid (token, peer.userSid))
		return i2p::unexpected ("GetTokenInformation(peer user): " + Win32ErrorText (GetLastError ()));
	DWORD flag = 0;
	peer.appContainer = TokenDword (token, TokenIsAppContainer, flag) && flag != 0;
	peer.lowPrivilegeAppContainer = TokenDword (token, I2PD_TOKEN_IS_LESS_PRIVILEGED_APPCONTAINER, flag) && flag != 0;
	if (peer.appContainer) (void)TokenAppContainerSid (token, peer.appContainerSid);
	(void)TokenIntegrityLevel (token, peer.integrityLevel);
	TOKEN_ELEVATION elevation { };
	DWORD elevationSize = sizeof (elevation);
	peer.elevated = GetTokenInformation (token, TokenElevation, &elevation, elevationSize, &elevationSize)
		&& elevation.TokenIsElevated != 0;
	return peer;
}

bool WindowsAllowsOperation (WindowsWorkerRole role, MessageType type)
{
	// The shared registry is the single policy: keep only the Windows-specific
	// limitations here instead of a second hand-maintained switch.
	switch (role)
	{
	case WindowsWorkerRole::Supervisor: return AllowsOperation (EndpointRole::Supervisor, type);
	case WindowsWorkerRole::Log:
		// ConfigureLogFd promises one descriptor. The Windows pipe transport
		// has no descriptor channel, so accepting it would silently drop the
		// capability; only path configuration is valid on this backend.
		if (type == MessageType::ConfigureLogFd) return false;
		return AllowsOperation (EndpointRole::Log, type);
	case WindowsWorkerRole::Storage:
		// The OpenBSD broker's inherited-root configure is never valid here:
		// Windows has no inherited-directory descriptor channel.
		if (type == MessageType::ConfigureTorrentStorageSupervised) return false;
		return AllowsOperation (EndpointRole::RouterStorage, type)
			|| AllowsOperation (EndpointRole::TorrentStorage, type)
			|| AllowsOperation (EndpointRole::DataStorage, type);
	case WindowsWorkerRole::Network:
	case WindowsWorkerRole::Crypto:
	case WindowsWorkerRole::Admin:
		// No typed Windows backend exists yet; control-plane messages only.
		return type == MessageType::Ping || type == MessageType::Shutdown;
	}
	return false;
}

WindowsWorkerPolicy GetWindowsWorkerPolicy (WindowsWorkerRole role)
{
	WindowsWorkerPolicy policy;
	// A native worker has no use for more than 64 MiB of private memory. The
	// limit is a bound, not a target: exceeding it terminates the job instead
	// of letting one role exhaust the service host.
	policy.memoryLimit = 64ull * 1024ull * 1024ull;
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
		// One process: a log worker never spawns a child.
		policy.processLimit = 1;
		break;
	case WindowsWorkerRole::Storage:
		// Storage may write only through an explicitly granted directory ACL;
		// it must not have Internet capability.
		policy.allowNetwork = false;
		policy.allowPersistentWrites = true;
		policy.useJobObject = true;
		policy.processLimit = 1;
		break;
	case WindowsWorkerRole::Network:
		// Networking needs external sockets but no persistent file capability.
		policy.allowNetwork = true;
		policy.allowPersistentWrites = false;
		policy.useJobObject = true;
		policy.processLimit = 1;
		break;
	case WindowsWorkerRole::Crypto:
	case WindowsWorkerRole::Admin:
		policy.allowNetwork = false;
		policy.allowPersistentWrites = false;
		policy.useJobObject = true;
		policy.processLimit = 1;
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
	// DISABLE_MAX_PRIVILEGE only disables privileges that were already
	// enabled. Delete every privilege so the worker cannot re-enable one.
	DWORD privilegeBytes = 0;
	GetTokenInformation (token, TokenPrivileges, nullptr, 0, &privilegeBytes);
	std::vector<LUID_AND_ATTRIBUTES> privileges;
	if (privilegeBytes >= sizeof (TOKEN_PRIVILEGES))
	{
		std::vector<unsigned char> buffer (privilegeBytes);
		if (!GetTokenInformation (token, TokenPrivileges, buffer.data (), privilegeBytes, &privilegeBytes))
			return i2p::unexpected ("GetTokenInformation(TokenPrivileges): " + Win32ErrorText (GetLastError ()));
		auto * tokenPrivileges = reinterpret_cast<TOKEN_PRIVILEGES *> (buffer.data ());
		privileges.assign (tokenPrivileges->Privileges, tokenPrivileges->Privileges + tokenPrivileges->PrivilegeCount);
	}
	DWORD flags = DISABLE_MAX_PRIVILEGE;
	TOKEN_ELEVATION elevation { };
	DWORD elevationLength = sizeof (elevation);
	if (GetTokenInformation (token, TokenElevation, &elevation, elevationLength, &elevationLength)
		&& elevation.TokenIsElevated)
		flags |= LUA_TOKEN;
	HANDLE restricted = nullptr;
	if (!CreateRestrictedToken (token, flags, 0, nullptr, static_cast<DWORD> (privileges.size ()),
			privileges.empty () ? nullptr : privileges.data (), 0, nullptr, &restricted))
		return i2p::unexpected ("CreateRestrictedToken: " + Win32ErrorText (GetLastError ()));
	if (!restricted) return i2p::unexpected ("CreateRestrictedToken returned no token");
	return WindowsHandle (restricted);
}

// Creation fails closed: a job that cannot carry the limits or the UI
// restrictions is closed and reported instead of being used as a bare
// container. KILL_ON_JOB_CLOSE is the stop backstop (closing the last job
// handle terminates the tree), DIE_ON_UNHANDLED_EXCEPTION turns a worker crash
// into a diagnosable exit, and the active-process limit is what makes
// "this role never spawns a child" enforceable rather than conventional.
i2p::expected<WindowsHandle, std::string> CreateWorkerJobObject (const WindowsWorkerPolicy & policy)
{
	HANDLE job = CreateJobObjectW (nullptr, nullptr);
	if (!job) return i2p::unexpected ("CreateJobObject: " + Win32ErrorText (GetLastError ()));
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits { };
	limits.BasicLimitInformation.LimitFlags
		= JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION;
	if (policy.processLimit > 0)
	{
		// With a process limit of one, a child process creation inside the
		// worker fails: storage, log, crypto and admin workers have no reason
		// to spawn anything.
		limits.BasicLimitInformation.ActiveProcessLimit = policy.processLimit;
		limits.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
	}
	if (policy.memoryLimit > 0)
	{
		limits.ProcessMemoryLimit = static_cast<SIZE_T> (policy.memoryLimit);
		limits.JobMemoryLimit = static_cast<SIZE_T> (policy.memoryLimit);
		limits.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_PROCESS_MEMORY | JOB_OBJECT_LIMIT_JOB_MEMORY;
	}
	if (!SetInformationJobObject (job, JobObjectExtendedLimitInformation, &limits, sizeof (limits)))
	{
		const DWORD error = GetLastError ();
		CloseHandle (job);
		return i2p::unexpected ("SetInformationJobObject: " + Win32ErrorText (error));
	}
	JOBOBJECT_BASIC_UI_RESTRICTIONS ui { };
	ui.UIRestrictionsClass = JOB_OBJECT_UILIMIT_DESKTOP | JOB_OBJECT_UILIMIT_DISPLAYSETTINGS
		| JOB_OBJECT_UILIMIT_EXITWINDOWS | JOB_OBJECT_UILIMIT_GLOBALATOMS | JOB_OBJECT_UILIMIT_HANDLES
		| JOB_OBJECT_UILIMIT_READCLIPBOARD | JOB_OBJECT_UILIMIT_SYSTEMPARAMETERS | JOB_OBJECT_UILIMIT_WRITECLIPBOARD;
	if (!SetInformationJobObject (job, JobObjectBasicUIRestrictions, &ui, sizeof (ui)))
	{
		const DWORD error = GetLastError ();
		CloseHandle (job);
		return i2p::unexpected ("SetInformationJobObject(UI): " + Win32ErrorText (error));
	}
	return WindowsHandle (job);
}

// Failure handling: every handle here is RAII-owned, so a rejected attribute or
// a failed CreateProcess leaves no process and no handle behind, while a
// failure after creation terminates the job, terminates the process and waits
// before returning. The child is created suspended and resumed only after the
// Job Object attribute or its fallback assignment succeeded, so no worker
// instruction runs outside the job.
i2p::expected<WindowsWorkerProcess, std::string> SpawnWindowsWorker (const std::wstring & executable,
	WindowsWorkerRole role, const std::wstring & pipeName, DWORD parentProcessId, DWORD parentSessionId,
	const std::wstring & nonceHex, const WindowsAppContainer * container, const WindowsWorkerPolicy & policy)
{
	if (executable.empty ()) return i2p::unexpected ("empty worker executable");
	if (executable.find (L'"') != std::wstring::npos)
		return i2p::unexpected ("worker executable path must not contain a quote");
	if (!IsLocalPipeName (pipeName)) return i2p::unexpected ("worker pipe name is not local");
	if (!IsSafeCommandLineToken (pipeName))
		return i2p::unexpected ("worker pipe name must not contain quotes or whitespace");
	if (!IsKnownRole (role) || !IsSafeCommandLineToken (RoleName (role)))
		return i2p::unexpected ("unknown or unusable worker role");
	if (parentProcessId == 0 || parentSessionId == 0)
		return i2p::unexpected ("worker spawn requires the supervisor PID and session");

	std::wstring nonce = nonceHex;
	if (nonce.empty ())
	{
		auto generated = GenerateWindowsWorkerNonce ();
		if (!generated) return i2p::unexpected (generated.error ());
		nonce = std::move (*generated);
	}
	const auto nonceText = NarrowAscii (nonce);
	if (!nonceText) return i2p::unexpected ("worker nonce is not ASCII hex");
	auto nonceBytes = DecodeWindowsWorkerNonce (*nonceText);
	if (!nonceBytes) return i2p::unexpected ("worker nonce: " + nonceBytes.error ());

	// The isolation table, not the caller, decides which mechanism a role uses
	// and whether it may reach the network or the filesystem. A mismatch fails
	// closed instead of silently degrading to a weaker boundary.
	const WindowsRoleIsolation isolation = GetWindowsRoleIsolation (role);
	const bool useContainer = container != nullptr;
	if (isolation.appContainer || isolation.lowPrivilegeAppContainer)
	{
		if (!useContainer)
			return i2p::unexpected ("worker role requires an AppContainer process; refusing to spawn without one");
	}
	else if (useContainer)
		return i2p::unexpected ("worker role does not accept an AppContainer process");
	if (useContainer && !container->IsValid ()) return i2p::unexpected ("worker AppContainer is not valid");
	if (isolation.internetClient && !useContainer)
		return i2p::unexpected ("network worker requires an AppContainer with an Internet capability");
	if (isolation.persistentWrites)
	{
		if (!useContainer) return i2p::unexpected ("persistent-write worker requires an AppContainer");
		if (policy.persistentWriteRoot.empty ())
			return i2p::unexpected ("persistent-write worker has no delegated root");
		auto granted = GrantAppContainerPath (*container, policy.persistentWriteRoot, true);
		if (!granted) return i2p::unexpected ("AppContainer root grant: " + granted.error ());
	}
	const bool lowPrivilege = useContainer && (container->LowPrivilege () || isolation.lowPrivilegeAppContainer);

	auto job = policy.useJobObject ? CreateWorkerJobObject (policy)
								   : i2p::expected<WindowsHandle, std::string> (WindowsHandle ());
	if (!job) return i2p::unexpected (job.error ());
	WindowsHandle token;
	if (!useContainer)
	{
		auto created = CreateRestrictedWorkerToken (policy);
		if (!created) return i2p::unexpected (created.error ());
		token = std::move (*created);
	}

	const std::wstring commandLine = L"\"" + executable + L"\" --privsep-worker " + RoleName (role) + L" " + pipeName
		+ L" " + std::to_wstring (parentProcessId) + L" " + std::to_wstring (parentSessionId) + L" " + nonce;
	std::vector<wchar_t> mutableCommandLine (commandLine.begin (), commandLine.end ());
	mutableCommandLine.push_back (L'\0');

	// Standard handles are pinned to NUL so a service manager's console or log
	// pipe is never handed to a worker. bInheritHandles stays FALSE: the values
	// document intent, and inheritance itself is already impossible.
	ScopedHandle nullInput (
		CreateFileW (L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
	ScopedHandle nullOutput (
		CreateFileW (L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
	ScopedHandle nullError (
		CreateFileW (L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
	if (!nullInput || !nullOutput || !nullError)
		return i2p::unexpected ("CreateFile(NUL): " + Win32ErrorText (GetLastError ()));

	// One attribute for the empty handle list, plus every mechanism actually
	// requested. The count is computed, never hard-coded.
	DWORD attributeCount = 1;
	if (policy.useMitigationPolicy) ++attributeCount;
	if (job->IsValid ()) ++attributeCount;
	if (useContainer) ++attributeCount;
	if (lowPrivilege) ++attributeCount;

	STARTUPINFOEXW startup { };
	startup.StartupInfo.cb = sizeof (startup);
	startup.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
	startup.StartupInfo.hStdInput = nullInput.get ();
	startup.StartupInfo.hStdOutput = nullOutput.get ();
	startup.StartupInfo.hStdError = nullError.get ();

	std::vector<BYTE> attributeBuffer;
	struct AttributeListReleaser
	{
		LPPROC_THREAD_ATTRIBUTE_LIST list = nullptr;
		~AttributeListReleaser ()
		{
			if (list) DeleteProcThreadAttributeList (list);
		}
	} attributeGuard;
	SIZE_T attributeSize = 0;
	// The first call is expected to fail with ERROR_INSUFFICIENT_BUFFER and
	// reports the required size; only a zero size is an error.
	(void)InitializeProcThreadAttributeList (nullptr, attributeCount, 0, &attributeSize);
	if (attributeSize == 0)
		return i2p::unexpected ("InitializeProcThreadAttributeList: " + Win32ErrorText (GetLastError ()));
	attributeBuffer.resize (attributeSize);
	startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST> (attributeBuffer.data ());
	if (!InitializeProcThreadAttributeList (startup.lpAttributeList, attributeCount, 0, &attributeSize))
		return i2p::unexpected ("InitializeProcThreadAttributeList: " + Win32ErrorText (GetLastError ()));
	attributeGuard.list = startup.lpAttributeList;

	// Inheriting is already disabled, so the empty handle list is explicit
	// defence in depth. If the kernel rejects a zero-length list, the spawn
	// continues with bInheritHandles=FALSE rather than failing a worker for an
	// attribute that adds no restriction.
	(void)UpdateProcThreadAttribute (
		startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, nullptr, 0, nullptr, nullptr);

	if (policy.useMitigationPolicy)
	{
		// Keep DEP, SEHOP, mandatory ASLR, heap termination and strict handle
		// checks enabled, disable legacy extension points and dynamic code
		// (every Windows worker is a native image, none of them is Python or a
		// JIT host), and require CFG for the images that support it. The DEP-ATL
		// compatibility emulation bit is deliberately not set; it would weaken
		// DEP and is not defined by the MinGW headers.
		DWORD64 mitigation = PROCESS_CREATION_MITIGATION_POLICY_DEP_ENABLE
			| PROCESS_CREATION_MITIGATION_POLICY_SEHOP_ENABLE
			| PROCESS_CREATION_MITIGATION_POLICY_FORCE_RELOCATE_IMAGES_ALWAYS_ON
			| PROCESS_CREATION_MITIGATION_POLICY_HEAP_TERMINATE_ALWAYS_ON
			| PROCESS_CREATION_MITIGATION_POLICY_BOTTOM_UP_ASLR_ALWAYS_ON
			| PROCESS_CREATION_MITIGATION_POLICY_HIGH_ENTROPY_ASLR_ALWAYS_ON
			| PROCESS_CREATION_MITIGATION_POLICY_STRICT_HANDLE_CHECKS_ALWAYS_ON
			| PROCESS_CREATION_MITIGATION_POLICY_EXTENSION_POINT_DISABLE_ALWAYS_ON
			| PROCESS_CREATION_MITIGATION_POLICY_PROHIBIT_DYNAMIC_CODE_ALWAYS_ON
			| PROCESS_CREATION_MITIGATION_POLICY_CONTROL_FLOW_GUARD_ALWAYS_ON;
		if (!UpdateProcThreadAttribute (startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY,
				&mitigation, sizeof (mitigation), nullptr, nullptr))
			return i2p::unexpected ("UpdateProcThreadAttribute(mitigation): " + Win32ErrorText (GetLastError ()));
	}

	bool jobInAttributeList = false;
	if (job->IsValid ())
	{
		HANDLE jobHandle = job->Native ();
		// The process is created suspended either way, so the fallback
		// assignment below also happens before the first instruction runs.
		jobInAttributeList = UpdateProcThreadAttribute (startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST,
								 &jobHandle, sizeof (jobHandle), nullptr, nullptr)
			!= FALSE;
	}

	if (useContainer)
	{
		if (!container->Sid ()) return i2p::unexpected ("worker AppContainer has no SID");
		SECURITY_CAPABILITIES capabilities { };
		capabilities.AppContainerSid = container->Sid ();
		capabilities.Capabilities = const_cast<SID_AND_ATTRIBUTES *> (container->Capabilities ().data ());
		capabilities.CapabilityCount = static_cast<DWORD> (container->Capabilities ().size ());
		capabilities.Reserved = 0;
		if (!UpdateProcThreadAttribute (startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES,
				&capabilities, sizeof (capabilities), nullptr, nullptr))
			return i2p::unexpected (
				"UpdateProcThreadAttribute(security capabilities): " + Win32ErrorText (GetLastError ()));

		if (lowPrivilege)
		{
			// A low-privilege AppContainer is the same profile without the
			// implicit all-application-packages capability.
			DWORD allPackages = PROCESS_CREATION_ALL_APPLICATION_PACKAGES_OPT_OUT;
			if (!UpdateProcThreadAttribute (startup.lpAttributeList, 0,
					PROC_THREAD_ATTRIBUTE_ALL_APPLICATION_PACKAGES_POLICY, &allPackages, sizeof (allPackages), nullptr,
					nullptr))
				return i2p::unexpected (
					"UpdateProcThreadAttribute(low privilege): " + Win32ErrorText (GetLastError ()));
		}
	}

	const DWORD creationFlags
		= CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT;
	PROCESS_INFORMATION processInfo { };
	BOOL created = FALSE;
	if (useContainer)
		created = CreateProcessW (executable.c_str (), mutableCommandLine.data (), nullptr, nullptr, FALSE,
			creationFlags, nullptr, nullptr, &startup.StartupInfo, &processInfo);
	else
		created = CreateProcessAsUserW (token.Native (), executable.c_str (), mutableCommandLine.data (), nullptr,
			nullptr, FALSE, creationFlags, nullptr, nullptr, &startup.StartupInfo, &processInfo);
	if (!created)
	{
		const DWORD error = GetLastError ();
		return i2p::unexpected ("CreateProcess(worker): " + Win32ErrorText (error));
	}

	WindowsWorkerProcess worker;
	worker.process.Reset (processInfo.hProcess);
	worker.thread.Reset (processInfo.hThread);
	worker.job = std::move (*job);
	worker.processId = processInfo.dwProcessId;
	worker.nonceHex = nonce;
	// PROC_THREAD_ATTRIBUTE_JOB_LIST already placed the suspended process in
	// the job; the fallback assigns it before the first instruction runs.
	if (!jobInAttributeList && worker.job.IsValid ()
		&& !AssignProcessToJobObject (worker.job.Native (), worker.process.Native ()))
	{
		const std::string error = "AssignProcessToJobObject: " + Win32ErrorText (GetLastError ());
		(void)worker.TerminateAndWait (2000, ERROR_ACCESS_DENIED);
		return i2p::unexpected (error);
	}
	if (ResumeThread (worker.thread.Native ()) == static_cast<DWORD> (-1))
	{
		const std::string error = "ResumeThread: " + Win32ErrorText (GetLastError ());
		(void)worker.TerminateAndWait (2000, ERROR_ACCESS_DENIED);
		return i2p::unexpected (error);
	}
	return worker;
}

bool WindowsWorkerProcess::Wait (DWORD timeoutMs) const
{
	return process.IsValid () && WaitForSingleObject (process.Native (), timeoutMs) == WAIT_OBJECT_0;
}

DWORD WindowsWorkerProcess::ExitCode () const
{
	DWORD code = 0;
	if (!process.IsValid () || !GetExitCodeProcess (process.Native (), &code)) return ERROR_PROCESS_ABORTED;
	return code;
}

// Termination order is job first, process second, wait last: the job covers
// every process the worker created, and the wait keeps the process handle alive
// until the kernel is done with it. A timeout returns false and leaves the
// KILL_ON_JOB_CLOSE backstop to finish the teardown when the handles go away.
bool WindowsWorkerProcess::TerminateAndWait (DWORD timeoutMs, UINT exitCode)
{
	// The job owns every process the worker created; closing the job first
	// prevents an orphaned child from outliving its parent.
	if (job.IsValid ()) (void)TerminateJobObject (job.Native (), exitCode);
	if (!process.IsValid ()) return true;
	(void)TerminateProcess (process.Native (), exitCode);
	return Wait (timeoutMs);
}

bool WindowsWorkerProcess::Stop (DWORD timeoutMs)
{
	if (!process.IsValid ()) return true;
	if (WaitForSingleObject (process.Native (), timeoutMs) == WAIT_OBJECT_0) return true;
	return TerminateAndWait (timeoutMs);
}

// Deterministic name for tests and local tools: it is derived from the
// configured endpoint, so it is guessable by any local process. Authorization
// is the protected DACL plus the PID/session/token/nonce accept, never the
// name; a production supervisor uses MakeRandomPipeName() so two generations
// cannot collide or be anticipated.
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

i2p::expected<std::wstring, std::string> MakeRandomPipeName ()
{
	unsigned char random[16] { };
	const NTSTATUS status = BCryptGenRandom (nullptr, random, sizeof (random), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
	if (status < 0) return i2p::unexpected ("BCryptGenRandom failed");
	std::wostringstream name;
	name << L"\\\\.\\pipe\\openi2pd-worker-" << std::hex << std::nouppercase << std::setfill (L'0');
	for (unsigned char byte : random) name << std::setw (2) << static_cast<unsigned int> (byte);
	return name.str ();
}

namespace
{
	/** A worker has no log sink before its channel is configured. */
	void WorkerFault (const char * text)
	{
		OutputDebugStringA (text);
		OutputDebugStringA ("\n");
	}

	bool PinWorkerStandardHandles ()
	{
		// A directly invoked worker must not inherit the operator's terminal or
		// a service-manager log pipe. There is no descriptor channel on Windows,
		// so stdio is a pure liability for every role.
		SECURITY_ATTRIBUTES attributes { };
		attributes.nLength = sizeof (attributes);
		attributes.bInheritHandle = FALSE;
		HANDLE input = CreateFileW (
			L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes, OPEN_EXISTING, 0, nullptr);
		if (input == INVALID_HANDLE_VALUE) return false;
		HANDLE output = CreateFileW (
			L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes, OPEN_EXISTING, 0, nullptr);
		if (output == INVALID_HANDLE_VALUE)
		{
			CloseHandle (input);
			return false;
		}
		if (!SetStdHandle (STD_INPUT_HANDLE, input) || !SetStdHandle (STD_OUTPUT_HANDLE, output)
			|| !SetStdHandle (STD_ERROR_HANDLE, output))
		{
			CloseHandle (input);
			CloseHandle (output);
			return false;
		}
		return true;
	}

	i2p::expected<DWORD, std::string> ParseDword (std::string_view text)
	{
		if (text.empty () || text.size () > 10) return i2p::unexpected ("invalid numeric worker argument");
		DWORD value = 0;
		for (char ch : text)
		{
			if (ch < '0' || ch > '9') return i2p::unexpected ("invalid numeric worker argument");
			const DWORD digit = static_cast<DWORD> (ch - '0');
			if (value > (std::numeric_limits<DWORD>::max () - digit) / 10)
				return i2p::unexpected ("numeric worker argument is out of range");
			value = value * 10 + digit;
		}
		if (value == 0) return i2p::unexpected ("numeric worker argument must not be zero");
		return value;
	}
}

int RunWindowsWorker (std::string_view roleArgument, std::string_view pipeName, std::string_view parentPid,
	std::string_view parentSession, std::string_view nonceHex)
{
	if (!PinWorkerStandardHandles ())
	{
		WorkerFault ("privsep worker: cannot pin standard handles");
		return 1;
	}
	// The command line is only a claim: the pipe connection is bound to the
	// supervisor PID/session and the nonce below, so a hand-started worker
	// cannot serve a role it was not spawned for.
	const auto role = RoleFromName (roleArgument);
	if (!role)
	{
		WorkerFault ("privsep worker: unknown role");
		return 1;
	}
	const auto pid = ParseDword (parentPid);
	const auto session = ParseDword (parentSession);
	const auto name = WidenAscii (pipeName);
	if (!pid || !session || !name || !IsLocalPipeName (*name))
	{
		WorkerFault ("privsep worker: invalid supervisor pipe or parent identity");
		return 1;
	}
	const auto nonce = DecodeWindowsWorkerNonce (nonceHex);
	if (!nonce)
	{
		WorkerFault ("privsep worker: invalid spawn nonce");
		return 1;
	}
	auto connected = WindowsChannel::ConnectBound (*name, *pid, *session, WORKER_CONNECT_TIMEOUT_MS);
	if (!connected)
	{
		WorkerFault ("privsep worker: cannot connect to the supervisor pipe");
		return 1;
	}
	auto & channel = *connected;
	auto hello = EncodeWorkerHelloFrame (*role, *nonce);
	if (!hello || !channel.Send ({ MessageType::WorkerHello, 0, std::move (*hello) }, WORKER_HELLO_TIMEOUT_MS))
	{
		WorkerFault ("privsep worker: cannot send the identity handshake");
		return 1;
	}
	// The handshake is consumed and accepted before the role applies: the
	// registry lets only the core receive WorkerHello.
	channel.SetRole (*role);
	while (true)
	{
		auto received = channel.Receive (1000);
		if (!received)
		{
			WorkerFault ("privsep worker: supervisor channel closed");
			return 1;
		}
		if (!received->has_value ()) continue; // idle poll or a refused frame
		const Message & message = **received;
		switch (message.type)
		{
		case MessageType::Ping:
			if (!message.payload.empty ())
			{
				WorkerFault ("privsep worker: invalid ping");
				return 1;
			}
			if (!channel.Send ({ MessageType::Pong, message.requestId, { } }, WORKER_CONTROL_TIMEOUT_MS))
			{
				WorkerFault ("privsep worker: cannot send pong");
				return 1;
			}
			break;
		case MessageType::Shutdown: return 0;
		case MessageType::Error: WorkerFault ("privsep worker: supervisor refused a frame"); return 1;
		default:
			// No Windows data-plane backend is implemented for any role yet. A
			// data frame is answered with a typed refusal and the worker exits
			// instead of silently ignoring it or starting the router here.
			(void)channel.Send ({ MessageType::Error, message.requestId,
									EncodeError (message.type, ErrorCode::NotAuthorized,
										"no Windows worker backend is implemented for this role") },
				WORKER_CONTROL_TIMEOUT_MS);
			WorkerFault ("privsep worker: no Windows worker backend for this role");
			return 1;
		}
	}
}

}

#endif
