/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test tui win32 pipe.
 *
 * Windows-only primitive check that the SDK exposes named-pipe and DACL
 * APIs, a protected DACL can be attached and a local byte-mode round
 * trip succeeds; cross-account enforcement is not covered.
 *
 */

#ifndef _WIN32
#error "test-tui-win32-pipe is a Windows-only test"
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sddl.h>

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace
{
std::wstring MakePipeName (const wchar_t * suffix)
{
	std::wstring name = L"\\\\.\\pipe\\openi2pd-tui-test-";
	name += std::to_wstring (GetCurrentProcessId ());
	name += L"-";
	name += std::to_wstring (GetTickCount64 ());
	name += L"-";
	name += suffix;
	return name;
}

void TestPipeNameShape ()
{
	const auto first = MakePipeName (L"a");
	const auto second = MakePipeName (L"b");
	assert (first.rfind (L"\\\\.\\pipe\\", 0) == 0);
	assert (first.find (L"openi2pd-tui") != std::wstring::npos);
	assert (first != second);
}

// The daemon creates every instance with FILE_FLAG_FIRST_PIPE_INSTANCE so a
// same-named squatter cannot be adopted between clients. Pin the primitive:
// a second first-instance of the same name must be refused.
void TestFirstInstanceGuard ()
{
	const auto name = MakePipeName (L"first-instance");
	constexpr DWORD openMode
		= PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE | FILE_FLAG_OVERLAPPED;
	constexpr DWORD pipeMode = PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS;

	HANDLE first = CreateNamedPipeW (name.c_str (), openMode, pipeMode, 1, 4096, 4096, 0, nullptr);
	if (first == INVALID_HANDLE_VALUE) fwprintf (stderr, L"CreateNamedPipeW failed: %lu\n", GetLastError ());
	assert (first != INVALID_HANDLE_VALUE);

	HANDLE second = CreateNamedPipeW (name.c_str (), openMode, pipeMode, 1, 4096, 4096, 0, nullptr);
	assert (second == INVALID_HANDLE_VALUE);
	assert (GetLastError () == ERROR_ACCESS_DENIED);

	assert (CloseHandle (first));
}

// The transport copies every pending read into a caller buffer, cancels it on
// timeout and must drain the completion before that buffer leaves scope. A
// cancel that is not drained leaves the kernel free to write into a dead
// frame, which is exactly what WaitForIo() avoids.
void TestOverlappedCancelDrain ()
{
	const auto name = MakePipeName (L"cancel-drain");
	HANDLE server = CreateNamedPipeW (name.c_str (), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
		PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 4096, 4096, 0, nullptr);
	if (server == INVALID_HANDLE_VALUE) fwprintf (stderr, L"CreateNamedPipeW failed: %lu\n", GetLastError ());
	assert (server != INVALID_HANDLE_VALUE);

	HANDLE client = INVALID_HANDLE_VALUE;
	std::thread connector (
		[&name, &client]
		{ client = CreateFileW (name.c_str (), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr); });

	OVERLAPPED connectOverlapped { };
	connectOverlapped.hEvent = CreateEventW (nullptr, TRUE, FALSE, nullptr);
	assert (connectOverlapped.hEvent != nullptr);
	if (!ConnectNamedPipe (server, &connectOverlapped))
	{
		const DWORD error = GetLastError ();
		assert (error == ERROR_IO_PENDING || error == ERROR_PIPE_CONNECTED);
		if (error == ERROR_IO_PENDING)
		{
			assert (WaitForSingleObject (connectOverlapped.hEvent, 5000) == WAIT_OBJECT_0);
			DWORD transferred = 0;
			assert (GetOverlappedResult (server, &connectOverlapped, &transferred, FALSE));
		}
	}
	connector.join ();
	assert (client != INVALID_HANDLE_VALUE);
	assert (CloseHandle (connectOverlapped.hEvent));

	OVERLAPPED readOverlapped { };
	readOverlapped.hEvent = CreateEventW (nullptr, TRUE, FALSE, nullptr);
	assert (readOverlapped.hEvent != nullptr);
	char byte = 0;
	DWORD received = 0;
	const BOOL started = ReadFile (server, &byte, 1, &received, &readOverlapped);
	assert (!started);
	assert (GetLastError () == ERROR_IO_PENDING);
	assert (WaitForSingleObject (readOverlapped.hEvent, 200) == WAIT_TIMEOUT);

	// Cancel and drain: the completion is observed here, not after the
	// buffer is released.
	assert (CancelIoEx (server, &readOverlapped));
	assert (!GetOverlappedResult (server, &readOverlapped, &received, TRUE));
	assert (GetLastError () == ERROR_OPERATION_ABORTED);

	assert (CloseHandle (readOverlapped.hEvent));
	assert (DisconnectNamedPipe (server));
	assert (CloseHandle (client));
	assert (CloseHandle (server));
}

// A byte-mode pipe preserves the byte stream, not message boundaries: one
// read may return less than the peer wrote. The transport accumulates until
// LF instead of assuming a whole request arrives at once.
void TestByteModeFraming ()
{
	const auto name = MakePipeName (L"byte-mode");
	HANDLE server = CreateNamedPipeW (name.c_str (), PIPE_ACCESS_DUPLEX,
		PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 4096, 4096, 0, nullptr);
	if (server == INVALID_HANDLE_VALUE) fwprintf (stderr, L"CreateNamedPipeW failed: %lu\n", GetLastError ());
	assert (server != INVALID_HANDLE_VALUE);

	HANDLE client = INVALID_HANDLE_VALUE;
	std::thread connector (
		[&name, &client]
		{ client = CreateFileW (name.c_str (), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr); });
	const BOOL connected = ConnectNamedPipe (server, nullptr);
	if (!connected) assert (GetLastError () == ERROR_PIPE_CONNECTED);
	connector.join ();
	assert (client != INVALID_HANDLE_VALUE);

	const std::string request = "I2PDTUI/1 PING\n";
	DWORD written = 0;
	assert (WriteFile (client, request.data (), static_cast<DWORD> (request.size ()), &written, nullptr));
	assert (written == request.size ());

	std::vector<char> buffer (request.size ());
	DWORD received = 0;
	assert (ReadFile (server, buffer.data (), static_cast<DWORD> (buffer.size ()), &received, nullptr));
	assert (received > 0);
	assert (static_cast<size_t> (received) <= request.size ());
	assert (std::memcmp (request.data (), buffer.data (), static_cast<size_t> (received)) == 0);

	assert (FlushFileBuffers (server));
	assert (DisconnectNamedPipe (server));
	assert (CloseHandle (client));
	assert (CloseHandle (server));
}

std::wstring CurrentUserSidString ()
{
	HANDLE token = nullptr;
	assert (OpenProcessToken (GetCurrentProcess (), TOKEN_QUERY, &token));
	DWORD size = 0;
	GetTokenInformation (token, TokenUser, nullptr, 0, &size);
	assert (GetLastError () == ERROR_INSUFFICIENT_BUFFER && size > 0);
	std::vector<BYTE> buffer (size);
	assert (GetTokenInformation (token, TokenUser, buffer.data (), size, &size));
	CloseHandle (token);

	auto * user = reinterpret_cast<TOKEN_USER *> (buffer.data ());
	LPWSTR stringSid = nullptr;
	assert (ConvertSidToStringSidW (user->User.Sid, &stringSid));
	std::wstring result (stringSid);
	LocalFree (stringSid);
	return result;
}

void TestDaclAndRoundTrip ()
{
	PSECURITY_DESCRIPTOR descriptor = nullptr;
	// Mirrors the product policy: protected DACL with full control for the
	// process user, SYSTEM and built-in Administrators only. Use the
	// actual process SID rather than OW: the object owner may be a
	// different principal in service/CI contexts.
	const std::wstring userSid = CurrentUserSidString ();
	assert (!userSid.empty ());
	const std::wstring sddl = L"D:P(A;;GA;;;" + userSid + L")(A;;GA;;;SY)(A;;GA;;;BA)";
	assert (
		ConvertStringSecurityDescriptorToSecurityDescriptorW (sddl.c_str (), SDDL_REVISION_1, &descriptor, nullptr));
	assert (descriptor != nullptr);

	BOOL daclPresent = FALSE;
	BOOL daclDefaulted = FALSE;
	PACL dacl = nullptr;
	assert (GetSecurityDescriptorDacl (descriptor, &daclPresent, &dacl, &daclDefaulted));
	assert (daclPresent == TRUE);
	assert (dacl != nullptr);

	SECURITY_ATTRIBUTES attributes { };
	attributes.nLength = sizeof (attributes);
	attributes.lpSecurityDescriptor = descriptor;
	attributes.bInheritHandle = FALSE;

	const auto name = MakePipeName (L"roundtrip");
	HANDLE server = CreateNamedPipeW (name.c_str (), PIPE_ACCESS_DUPLEX,
		PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 4096, 4096, 0, &attributes);
	if (server == INVALID_HANDLE_VALUE) fwprintf (stderr, L"CreateNamedPipeW failed: %lu\n", GetLastError ());
	assert (server != INVALID_HANDLE_VALUE);

	HANDLE client = INVALID_HANDLE_VALUE;
	std::thread connector ([&name, &client]
		{ client = CreateFileW (name.c_str (), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr); });
	const BOOL connected = ConnectNamedPipe (server, nullptr);
	if (!connected) assert (GetLastError () == ERROR_PIPE_CONNECTED);
	connector.join ();
	assert (client != INVALID_HANDLE_VALUE);

	const char message[] = "openi2pd-tui-pipe";
	constexpr DWORD messageSize = static_cast<DWORD> (sizeof (message));
	DWORD written = 0;
	assert (WriteFile (client, message, messageSize, &written, nullptr));
	assert (written == messageSize);

	char received[sizeof (message)] { };
	DWORD receivedSize = 0;
	assert (ReadFile (server, received, messageSize, &receivedSize, nullptr));
	assert (receivedSize == written);
	assert (std::memcmp (message, received, messageSize) == 0);

	assert (FlushFileBuffers (server));
	assert (DisconnectNamedPipe (server));
	assert (CloseHandle (client));
	assert (CloseHandle (server));
	LocalFree (descriptor);
}
}

int main ()
{
	TestPipeNameShape ();
	TestDaclAndRoundTrip ();
	TestFirstInstanceGuard ();
	TestOverlappedCancelDrain ();
	TestByteModeFraming ();
	return 0;
}
