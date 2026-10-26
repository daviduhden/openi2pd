/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test privsep windows ipc.
 *
 * Windows-only checks for the worker pipe transport: role authorization,
 * private DACL derivation with an optional AppContainer SID, spawn-nonce and
 * WorkerHello binding, identity-bound accept with rejection and re-arm, frame
 * bounds, descriptor refusal, cancellation delivery and reconnect behavior.
 *
 */

#ifndef _WIN32
#error "test-privsep-windows-ipc is a Windows-only test"
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
// The CMake target already supplies these as compile definitions; only
// provide them when the test is compiled standalone.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <aclapi.h>
#include <sddl.h>

#include <cassert>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "privsep/WindowsIpc.h"

using namespace i2p::privsep;

namespace
{
/** A nonce-shaped 32-byte pattern; the value never has to be secret here. */
std::string TestNonce ()
{
	std::string nonce (WINDOWS_WORKER_NONCE_SIZE, '\0');
	for (size_t i = 0; i < nonce.size (); ++i) nonce[i] = static_cast<char> (0x40 + i);
	return nonce;
}

std::wstring TestNonceHexA ()
{
	return EncodeWindowsWorkerNonce (TestNonce ());
}

std::wstring OtherNonceHex ()
{
	std::string nonce (WINDOWS_WORKER_NONCE_SIZE, '\0');
	for (size_t i = 0; i < nonce.size (); ++i) nonce[i] = static_cast<char> (0x80 + i);
	return EncodeWindowsWorkerNonce (nonce);
}

/** Narrow the ASCII hex accepted by DecodeWindowsWorkerNonce. */
std::string NarrowHex (const std::wstring & hex)
{
	std::string narrow;
	narrow.reserve (hex.size ());
	for (const wchar_t c : hex) narrow.push_back (static_cast<char> (c));
	return narrow;
}

DWORD CurrentSessionId ()
{
	DWORD session = 0;
	// The query has a side effect, so it must never live inside the assertion.
	const BOOL resolved = ProcessIdToSessionId (GetCurrentProcessId (), &session);
	(void)resolved;
	assert (resolved);
	return session;
}

std::wstring MakeTestPipeName (const wchar_t * suffix)
{
	static std::atomic<unsigned> counter { 0 };
	std::wstring name = L"\\\\.\\pipe\\openi2pd-worker-test-";
	name += std::to_wstring (GetCurrentProcessId ());
	name += L"-";
	name += std::to_wstring (GetTickCount64 ());
	name += L"-";
	name += std::to_wstring (counter.fetch_add (1));
	name += L"-";
	name += suffix;
	return name;
}

bool IsWellKnown (const void * sid, WELL_KNOWN_SID_TYPE type)
{
	BYTE buffer[SECURITY_MAX_SID_SIZE] { };
	DWORD size = sizeof (buffer);
	return CreateWellKnownSid (type, nullptr, buffer, &size) && EqualSid (const_cast<void *> (sid), buffer);
}

/**
 * The DACL must stay protected, must not contain a world/authenticated/anonymous
 * ACE and must contain exactly the expected number of allow ACEs, with
 * `expectedSid` present when it is not empty.
 */
void CheckDacl (HANDLE pipe, const std::wstring & expectedSid, DWORD expectedAces)
{
	PSECURITY_DESCRIPTOR descriptor = nullptr;
	PACL dacl = nullptr;
	const DWORD result = GetSecurityInfo (
		pipe, SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &dacl, nullptr, &descriptor);
	assert (result == ERROR_SUCCESS);
	assert (descriptor && dacl);

	SECURITY_DESCRIPTOR_CONTROL control = 0;
	DWORD revision = 0;
	assert (GetSecurityDescriptorControl (descriptor, &control, &revision));
	assert ((control & SE_DACL_PROTECTED) != 0);

	ACL_SIZE_INFORMATION sizeInfo { };
	assert (GetAclInformation (dacl, &sizeInfo, sizeof (sizeInfo), AclSizeInformation));
	assert (sizeInfo.AceCount == expectedAces);
	bool extraFound = false;
	for (DWORD i = 0; i < sizeInfo.AceCount; ++i)
	{
		void * raw = nullptr;
		assert (GetAce (dacl, i, &raw));
		auto * ace = static_cast<ACCESS_ALLOWED_ACE *> (raw);
		assert (ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE);
		assert (!IsWellKnown (&ace->SidStart, WinWorldSid));
		assert (!IsWellKnown (&ace->SidStart, WinAuthenticatedUserSid));
		assert (!IsWellKnown (&ace->SidStart, WinAnonymousSid));
		if (!expectedSid.empty ())
		{
			LPWSTR text = nullptr;
			if (ConvertSidToStringSidW (&ace->SidStart, &text))
			{
				if (expectedSid == text) extraFound = true;
				LocalFree (text);
			}
		}
	}
	if (!expectedSid.empty ()) assert (extraFound);
	LocalFree (descriptor);
}

/** Connect as the (trusted) test process and run `handshake` on the channel. */
std::thread StartTestWorker (const std::wstring & pipeName, std::function<void (WindowsChannel &)> handshake,
	std::unique_ptr<WindowsChannel> & channel)
{
	return std::thread (
		[&pipeName, handshake = std::move (handshake), &channel]
		{
			auto connected = WindowsChannel::ConnectBound (pipeName, GetCurrentProcessId (), CurrentSessionId (), 5000);
			if (!connected) return;
			channel = std::make_unique<WindowsChannel> (std::move (*connected));
			handshake (*channel);
		});
}

void SendHello (WindowsChannel & channel, WindowsWorkerRole role, const std::string & nonce, uint32_t requestId = 1)
{
	auto payload = EncodeWorkerHelloFrame (role, nonce);
	assert (payload.has_value ());
	assert (channel.Send ({ MessageType::WorkerHello, requestId, *payload }, 5000).has_value ());
}

/** Write one raw message-mode record, bypassing the frame encoders. */
void WriteRawRecord (HANDLE pipe, const std::string & record)
{
	DWORD written = 0;
	HANDLE event = CreateEventW (nullptr, TRUE, FALSE, nullptr);
	assert (event != nullptr);
	OVERLAPPED overlapped { };
	overlapped.hEvent = event;
	BOOL result = WriteFile (pipe, record.data (), static_cast<DWORD> (record.size ()), &written, &overlapped);
	if (!result && GetLastError () == ERROR_IO_PENDING)
		result = GetOverlappedResult (pipe, &overlapped, &written, TRUE);
	assert (result);
	assert (written == record.size ());
	CloseHandle (event);
}

void TestPolicyAndRoles ()
{
	static constexpr WindowsWorkerRole kRoles[] = { WindowsWorkerRole::Supervisor, WindowsWorkerRole::Log,
		WindowsWorkerRole::Storage, WindowsWorkerRole::Network, WindowsWorkerRole::Crypto, WindowsWorkerRole::Admin };

	for (auto role : kRoles)
	{
		const auto policy = GetWindowsWorkerPolicy (role);
		assert (policy.memoryLimit == 64ull * 1024ull * 1024ull);
		assert (policy.useJobObject);
		assert (policy.useRestrictedToken);
		assert (!policy.allowLan);
		assert (policy.persistentWriteRoot.empty ());
		assert (policy.allowNetwork == (role == WindowsWorkerRole::Network));
		assert (policy.allowPersistentWrites == (role == WindowsWorkerRole::Storage));
		assert (policy.processLimit == (role == WindowsWorkerRole::Supervisor ? 2u : 1u));

		// Every worker accepts the control plane only; a handshake and a core
		// reply are never receivable by a worker endpoint.
		assert (WindowsAllowsOperation (role, MessageType::Ping));
		assert (WindowsAllowsOperation (role, MessageType::Shutdown));
		assert (!WindowsAllowsOperation (role, MessageType::WorkerHello));
		assert (!WindowsAllowsOperation (role, MessageType::Pong));
	}

	// Storage owns the three storage request families and nothing else.
	assert (WindowsAllowsOperation (WindowsWorkerRole::Storage, MessageType::RouterStorageRequest));
	assert (WindowsAllowsOperation (WindowsWorkerRole::Storage, MessageType::TorrentStorageRequest));
	assert (WindowsAllowsOperation (WindowsWorkerRole::Storage, MessageType::DataStorageRequest));
	assert (!WindowsAllowsOperation (WindowsWorkerRole::Storage, MessageType::LogRecord));
	assert (!WindowsAllowsOperation (WindowsWorkerRole::Storage, MessageType::DataStorageReply));
	assert (!WindowsAllowsOperation (WindowsWorkerRole::Storage, MessageType::RouterStorageReply));
	assert (!WindowsAllowsOperation (WindowsWorkerRole::Storage, MessageType::ConfigureLog));

	// The log worker never receives the descriptor variant.
	assert (WindowsAllowsOperation (WindowsWorkerRole::Log, MessageType::LogRecord));
	assert (WindowsAllowsOperation (WindowsWorkerRole::Log, MessageType::ConfigureLog));
	assert (!WindowsAllowsOperation (WindowsWorkerRole::Log, MessageType::ConfigureLogFd));
	assert (!WindowsAllowsOperation (WindowsWorkerRole::Log, MessageType::TorrentStorageRequest));

	// The supervisor configures the log worker but owns no storage operation.
	assert (WindowsAllowsOperation (WindowsWorkerRole::Supervisor, MessageType::ConfigureLog));
	assert (!WindowsAllowsOperation (WindowsWorkerRole::Supervisor, MessageType::ConfigureLogFd));
	assert (!WindowsAllowsOperation (WindowsWorkerRole::Supervisor, MessageType::DataStorageRequest));

	// A role without a Windows data plane is control-only.
	assert (!WindowsAllowsOperation (WindowsWorkerRole::Network, MessageType::LogRecord));
	assert (!WindowsAllowsOperation (WindowsWorkerRole::Crypto, MessageType::ConfigureDataStorage));
	assert (!WindowsAllowsOperation (WindowsWorkerRole::Admin, MessageType::DataStorageRequest));

	const auto support = ProbeWindowsIsolation ();
	assert (support.messageModePipes);
	assert (support.restrictedTokens);
	assert (support.jobObjects);

	auto token = CreateRestrictedWorkerToken (GetWindowsWorkerPolicy (WindowsWorkerRole::Storage));
	assert (token.has_value ());
	assert (token->IsValid ());
	auto job = CreateWorkerJobObject (GetWindowsWorkerPolicy (WindowsWorkerRole::Storage));
	assert (job.has_value ());
	assert (job->IsValid ());
}

void TestNonceCodec ()
{
	const std::string nonce = TestNonce ();
	const std::wstring hex = EncodeWindowsWorkerNonce (nonce);
	assert (hex.size () == WINDOWS_WORKER_NONCE_SIZE * 2);
	const std::string text (hex.begin (), hex.end ());
	auto decoded = DecodeWindowsWorkerNonce (text);
	assert (decoded.has_value ());
	assert (*decoded == nonce);

	auto generated = GenerateWindowsWorkerNonce ();
	assert (generated.has_value ());
	assert (generated->size () == WINDOWS_WORKER_NONCE_SIZE * 2);
	const std::string generatedText (generated->begin (), generated->end ());
	auto generatedBytes = DecodeWindowsWorkerNonce (generatedText);
	assert (generatedBytes.has_value ());
	assert (generatedBytes->size () == WINDOWS_WORKER_NONCE_SIZE);

	assert (!DecodeWindowsWorkerNonce ("").has_value ());
	assert (!DecodeWindowsWorkerNonce (std::string (32, 'a')).has_value ());
	assert (!DecodeWindowsWorkerNonce (std::string (WINDOWS_WORKER_NONCE_SIZE * 2, '0')).has_value ());
	assert (!DecodeWindowsWorkerNonce (std::string (WINDOWS_WORKER_NONCE_SIZE * 2, 'z')).has_value ());
	// A nonce-shaped value with a trailing byte is refused, not truncated.
	assert (!DecodeWindowsWorkerNonce (text + "a").has_value ());
	assert (EncodeWindowsWorkerNonce (std::string (8, 'a')).empty ());
}

void TestWorkerHelloCodec ()
{
	const std::string nonce = TestNonce ();
	auto payload = EncodeWorkerHelloFrame (WindowsWorkerRole::Storage, nonce);
	assert (payload.has_value ());
	assert (payload->size () == 1 + 1 + WINDOWS_WORKER_NONCE_SIZE);
	assert (payload->size () <= MaxPayloadFor (MessageType::WorkerHello));

	Message hello { MessageType::WorkerHello, 5, *payload };
	auto decoded = DecodeWorkerHelloFrame (hello);
	assert (decoded.has_value ());
	assert (decoded->first == WindowsWorkerRole::Storage);
	assert (decoded->second == nonce);

	// A frame of the wrong type is not a handshake.
	assert (!DecodeWorkerHelloFrame ({ MessageType::Ping, 5, *payload }).has_value ());
	// An unknown role is refused by the encoder and the decoder.
	assert (!EncodeWorkerHelloFrame (static_cast<WindowsWorkerRole> (9), nonce).has_value ());
	{
		PayloadWriter writer;
		writer.WriteU8 (9);
		writer.WriteU8 (WINDOWS_WORKER_NONCE_SIZE);
		writer.WriteRaw (nonce);
		assert (!DecodeWorkerHelloFrame ({ MessageType::WorkerHello, 6, writer.Data () }).has_value ());
	}
	// Wrong nonce length, truncated nonce, trailing data and an all-zero nonce.
	{
		PayloadWriter writer;
		writer.WriteU8 (static_cast<uint8_t> (WindowsWorkerRole::Storage));
		writer.WriteU8 (16);
		writer.WriteRaw (std::string (16, 'a'));
		assert (!DecodeWorkerHelloFrame ({ MessageType::WorkerHello, 6, writer.Data () }).has_value ());
	}
	{
		PayloadWriter writer;
		writer.WriteU8 (static_cast<uint8_t> (WindowsWorkerRole::Storage));
		writer.WriteU8 (WINDOWS_WORKER_NONCE_SIZE);
		writer.WriteRaw (nonce.substr (0, 16));
		assert (!DecodeWorkerHelloFrame ({ MessageType::WorkerHello, 6, writer.Data () }).has_value ());
	}
	{
		PayloadWriter writer;
		writer.WriteU8 (static_cast<uint8_t> (WindowsWorkerRole::Storage));
		writer.WriteU8 (WINDOWS_WORKER_NONCE_SIZE);
		writer.WriteRaw (nonce);
		writer.WriteU8 (0);
		assert (!DecodeWorkerHelloFrame ({ MessageType::WorkerHello, 6, writer.Data () }).has_value ());
	}
	{
		PayloadWriter writer;
		writer.WriteU8 (static_cast<uint8_t> (WindowsWorkerRole::Storage));
		writer.WriteU8 (WINDOWS_WORKER_NONCE_SIZE);
		writer.WriteRaw (std::string (WINDOWS_WORKER_NONCE_SIZE, '\0'));
		assert (!DecodeWorkerHelloFrame ({ MessageType::WorkerHello, 6, writer.Data () }).has_value ());
	}
	// The whole frame is bounded by the registry, not by the 64 KiB maximum.
	assert (Encode (Message {
						MessageType::WorkerHello, 1, std::string (MaxPayloadFor (MessageType::WorkerHello) + 1, 'x') })
				.error ()
				.code
		== ErrorCode::TooLarge);
}

void TestPrivatePipeNameAndSquatting ()
{
	auto name = MakePrivatePipeName ("C:\\ProgramData\\openi2pd");
	assert (name.has_value ());
	assert (name->rfind (L"\\\\.\\pipe\\openi2pd-worker-", 0) == 0);
	auto remote = WindowsServer::Create (L"\\\\remote\\pipe\\openi2pd");
	assert (!remote.has_value ());
	auto randomOne = MakeRandomPipeName ();
	auto randomTwo = MakeRandomPipeName ();
	assert (randomOne.has_value () && randomTwo.has_value ());
	assert (*randomOne != *randomTwo);
	assert (randomOne->rfind (L"\\\\.\\pipe\\openi2pd-worker-", 0) == 0);

	// A name that is already listening must not be joined: the worker would
	// otherwise connect to a squatted listener instead of its supervisor.
	const std::wstring squatted = MakeTestPipeName (L"squatted");
	HANDLE squatter = CreateNamedPipeW (squatted.c_str (), PIPE_ACCESS_DUPLEX,
		PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 4096, 4096, 0, nullptr);
	assert (squatter != INVALID_HANDLE_VALUE);
	auto server = WindowsServer::Create (squatted);
	assert (!server.has_value ());
	CloseHandle (squatter);
}

void TestDaclWithExtraSid ()
{
	const std::wstring extraSid = L"S-1-5-21-1111111111-2222222222-3333333333-1001";
	const std::wstring name = MakeTestPipeName (L"dacl");
	auto server = WindowsServer::Create (name, extraSid);
	assert (server.has_value ());
	CheckDacl (server->Native (), extraSid, 3);

	// The default DACL stays at the service user and SYSTEM.
	const std::wstring plainName = MakeTestPipeName (L"dacl-plain");
	auto plain = WindowsServer::Create (plainName);
	assert (plain.has_value ());
	CheckDacl (plain->Native (), std::wstring { }, 2);

	// An SDDL injection must never reach ConvertStringSecurityDescriptor and a
	// value that is not a SID string is refused outright.
	auto injected = WindowsServer::Create (MakeTestPipeName (L"dacl-inject"), L"S-1-5-21-1)(A;;GA;;;WD");
	assert (!injected.has_value ());
	auto notSid = WindowsServer::Create (MakeTestPipeName (L"dacl-notsid"), L"WD");
	assert (!notSid.has_value ());
	auto withSemicolon = WindowsServer::Create (MakeTestPipeName (L"dacl-semicolon"), L"S-1-5-21-1;A");
	assert (!withSemicolon.has_value ());
}

void TestBindRequiresExplicitIdentity ()
{
	const std::wstring name = MakeTestPipeName (L"bind-identity");
	auto server = WindowsServer::Create (name);
	assert (server.has_value ());
	const std::wstring nonce = TestNonceHexA ();
	// A zero PID or session is a programming error, never "any peer".
	assert (
		!server->AcceptBound (100, 0, CurrentSessionId (), WindowsWorkerRole::Storage, nonce, nullptr).has_value ());
	assert (
		!server->AcceptBound (100, GetCurrentProcessId (), 0, WindowsWorkerRole::Storage, nonce, nullptr).has_value ());
	// A malformed nonce is refused before the listener is touched.
	assert (!server
			->AcceptBound (
				100, GetCurrentProcessId (), CurrentSessionId (), WindowsWorkerRole::Storage, L"abcd", nullptr)
			.has_value ());
	assert (!server
			->AcceptBound (100, GetCurrentProcessId (), CurrentSessionId (), WindowsWorkerRole::Storage,
				std::wstring (WINDOWS_WORKER_NONCE_SIZE * 2, '0'), nullptr)
			.has_value ());
}

void TestAcceptBoundHappyPath ()
{
	const std::wstring name = MakeTestPipeName (L"bound");
	auto server = WindowsServer::Create (name);
	assert (server.has_value ());
	const std::string nonce = TestNonce ();
	const std::wstring nonceHex = TestNonceHexA ();

	std::unique_ptr<WindowsChannel> client;
	std::thread worker (StartTestWorker (
		name, [&nonce] (WindowsChannel & channel) { SendHello (channel, WindowsWorkerRole::Storage, nonce, 7); },
		client));
	auto accepted = server->AcceptBound (
		5000, GetCurrentProcessId (), CurrentSessionId (), WindowsWorkerRole::Storage, nonceHex, nullptr);
	worker.join ();
	assert (accepted.has_value ());
	assert (client);
	assert (accepted->IsOpen ());
	assert (accepted->RoleSet ());
	assert (accepted->Role () == WindowsWorkerRole::Storage);

	// The handshake was consumed: the first request the worker sends is
	// dispatched normally.
	assert (client->Send ({ MessageType::Ping, 11, { } }).has_value ());
	auto ping = accepted->Receive (2000);
	assert (ping.has_value ());
	assert (ping->has_value ());
	assert ((*ping)->type == MessageType::Ping);
	assert ((*ping)->requestId == 11);
	assert (accepted->Send ({ MessageType::Pong, 11, { } }).has_value ());
	auto pong = client->Receive (2000);
	assert (pong.has_value () && pong->has_value ());
	assert ((*pong)->type == MessageType::Pong);

	accepted->Close ();
	client->Close ();
}

void TestHelloRejectionsAndRearm ()
{
	const std::string nonce = TestNonce ();
	const std::wstring nonceHex = TestNonceHexA ();
	const std::wstring otherNonce = OtherNonceHex ();

	// A handshake with the wrong nonce is rejected and the listener re-arms for
	// the real worker, so an impostor can never starve the spawn.
	{
		const std::wstring name = MakeTestPipeName (L"bad-nonce");
		auto server = WindowsServer::Create (name);
		assert (server.has_value ());
		std::unique_ptr<WindowsChannel> impostor;
		std::thread bad (StartTestWorker (
			name,
			[&] (WindowsChannel & channel)
			{
				auto bytes = DecodeWindowsWorkerNonce (NarrowHex (otherNonce));
				assert (bytes.has_value ());
				SendHello (channel, WindowsWorkerRole::Storage, *bytes, 1);
			},
			impostor));
		std::this_thread::sleep_for (std::chrono::milliseconds (200));

		std::unique_ptr<WindowsChannel> client;
		std::thread good (StartTestWorker (
			name, [&nonce] (WindowsChannel & channel) { SendHello (channel, WindowsWorkerRole::Storage, nonce); },
			client));
		auto accepted = server->AcceptBound (
			5000, GetCurrentProcessId (), CurrentSessionId (), WindowsWorkerRole::Storage, nonceHex, nullptr);
		bad.join ();
		good.join ();
		assert (accepted.has_value ());
		assert (client);
		if (impostor) impostor->Close ();
		accepted->Close ();
		client->Close ();
	}

	// The same re-arm applies to a short handshake and to a wrong role.
	{
		const std::wstring name = MakeTestPipeName (L"short-hello");
		auto server = WindowsServer::Create (name);
		assert (server.has_value ());
		std::unique_ptr<WindowsChannel> impostor;
		std::thread bad (StartTestWorker (
			name,
			[] (WindowsChannel & channel)
			{
				PayloadWriter writer;
				writer.WriteU8 (static_cast<uint8_t> (WindowsWorkerRole::Storage));
				writer.WriteU8 (16);
				writer.WriteRaw (std::string (16, 'a'));
				(void)channel.Send ({ MessageType::WorkerHello, 1, writer.Data () }, 5000);
			},
			impostor));
		std::this_thread::sleep_for (std::chrono::milliseconds (200));

		std::unique_ptr<WindowsChannel> client;
		std::thread good (StartTestWorker (
			name, [&nonce] (WindowsChannel & channel) { SendHello (channel, WindowsWorkerRole::Storage, nonce); },
			client));
		auto accepted = server->AcceptBound (
			5000, GetCurrentProcessId (), CurrentSessionId (), WindowsWorkerRole::Storage, nonceHex, nullptr);
		bad.join ();
		good.join ();
		assert (accepted.has_value ());
		assert (client);
		if (impostor) impostor->Close ();
		accepted->Close ();
		client->Close ();
	}
	{
		const std::wstring name = MakeTestPipeName (L"wrong-role");
		auto server = WindowsServer::Create (name);
		assert (server.has_value ());
		std::unique_ptr<WindowsChannel> impostor;
		std::thread bad (StartTestWorker (
			name, [&nonce] (WindowsChannel & channel) { SendHello (channel, WindowsWorkerRole::Network, nonce); },
			impostor));
		std::this_thread::sleep_for (std::chrono::milliseconds (200));

		std::unique_ptr<WindowsChannel> client;
		std::thread good (StartTestWorker (
			name, [&nonce] (WindowsChannel & channel) { SendHello (channel, WindowsWorkerRole::Storage, nonce); },
			client));
		auto accepted = server->AcceptBound (
			5000, GetCurrentProcessId (), CurrentSessionId (), WindowsWorkerRole::Storage, nonceHex, nullptr);
		bad.join ();
		good.join ();
		assert (accepted.has_value ());
		assert (client);
		if (impostor) impostor->Close ();
		accepted->Close ();
		client->Close ();
	}

	// A wrong expected PID is rejected without accepting the connection.
	{
		const std::wstring name = MakeTestPipeName (L"wrong-pid");
		auto server = WindowsServer::Create (name);
		assert (server.has_value ());
		std::unique_ptr<WindowsChannel> client;
		std::thread worker (StartTestWorker (
			name, [&nonce] (WindowsChannel & channel) { SendHello (channel, WindowsWorkerRole::Storage, nonce); },
			client));
		auto accepted = server->AcceptBound (
			500, GetCurrentProcessId () + 1, CurrentSessionId (), WindowsWorkerRole::Storage, nonceHex, nullptr);
		worker.join ();
		assert (!accepted.has_value ());
		if (client) client->Close ();
	}
	// A wrong expected session is rejected on the same grounds.
	{
		const std::wstring name = MakeTestPipeName (L"wrong-session");
		auto server = WindowsServer::Create (name);
		assert (server.has_value ());
		std::unique_ptr<WindowsChannel> client;
		std::thread worker (StartTestWorker (
			name, [&nonce] (WindowsChannel & channel) { SendHello (channel, WindowsWorkerRole::Storage, nonce); },
			client));
		auto accepted = server->AcceptBound (
			500, GetCurrentProcessId (), CurrentSessionId () + 1, WindowsWorkerRole::Storage, nonceHex, nullptr);
		worker.join ();
		assert (!accepted.has_value ());
		if (client) client->Close ();
	}
}

void TestDuplicateHelloIsRefused ()
{
	const std::wstring name = MakeTestPipeName (L"duplicate-hello");
	auto server = WindowsServer::Create (name);
	assert (server.has_value ());
	const std::string nonce = TestNonce ();
	const std::wstring nonceHex = TestNonceHexA ();

	std::unique_ptr<WindowsChannel> client;
	std::thread worker (StartTestWorker (
		name,
		[&nonce] (WindowsChannel & channel)
		{
			SendHello (channel, WindowsWorkerRole::Storage, nonce, 1);
			// A second handshake must never be mistaken for a request.
			SendHello (channel, WindowsWorkerRole::Storage, nonce, 2);
		},
		client));
	auto accepted = server->AcceptBound (
		5000, GetCurrentProcessId (), CurrentSessionId (), WindowsWorkerRole::Storage, nonceHex, nullptr);
	worker.join ();
	assert (accepted.has_value ());
	assert (client);

	// The duplicate is not dispatchable: it is answered with one typed refusal
	// and the channel stays open.
	auto duplicate = accepted->Receive (2000);
	assert (duplicate.has_value ());
	assert (!duplicate->has_value ());
	auto refusal = client->Receive (2000);
	assert (refusal.has_value () && refusal->has_value ());
	assert ((*refusal)->type == MessageType::Error);
	auto decoded = DecodeError ((*refusal)->payload);
	assert (decoded.has_value ());
	assert (std::get<0> (*decoded) == MessageType::WorkerHello);
	assert (std::get<1> (*decoded) == ErrorCode::NotAuthorized);

	assert (client->Send ({ MessageType::Ping, 21, { } }).has_value ());
	auto ping = accepted->Receive (2000);
	assert (ping.has_value () && ping->has_value ());
	assert ((*ping)->type == MessageType::Ping);

	accepted->Close ();
	client->Close ();
}

void TestAuthorizationAndDescriptorRefusal ()
{
	const std::wstring name = MakeTestPipeName (L"authorization");
	auto server = WindowsServer::Create (name);
	assert (server.has_value ());
	const std::string nonce = TestNonce ();
	const std::wstring nonceHex = TestNonceHexA ();

	std::unique_ptr<WindowsChannel> client;
	std::thread worker (StartTestWorker (
		name, [&nonce] (WindowsChannel & channel) { SendHello (channel, WindowsWorkerRole::Storage, nonce); }, client));
	auto accepted = server->AcceptBound (
		5000, GetCurrentProcessId (), CurrentSessionId (), WindowsWorkerRole::Storage, nonceHex, nullptr);
	worker.join ();
	assert (accepted.has_value ());
	assert (client);

	// A log record is not a storage operation.
	const std::string logRecord = EncodeLogRecord (1, 2, "secret-free");
	assert (client->Send ({ MessageType::LogRecord, 31, logRecord }).has_value ());
	auto refused = accepted->Receive (2000);
	assert (refused.has_value () && !refused->has_value ());
	auto refusal = client->Receive (2000);
	assert (refusal.has_value () && refusal->has_value ());
	assert ((*refusal)->requestId == 31);
	auto decoded = DecodeError ((*refusal)->payload);
	assert (decoded.has_value ());
	assert (std::get<0> (*decoded) == MessageType::LogRecord);
	assert (std::get<1> (*decoded) == ErrorCode::NotAuthorized);

	// A reply type is not receivable by a worker endpoint either.
	assert (client->Send ({ MessageType::DataStorageReply, 32, EncodeDataStorageReply (0, 1, 9) }).has_value ());
	auto replyRefused = accepted->Receive (2000);
	assert (replyRefused.has_value () && !replyRefused->has_value ());
	auto replyRefusal = client->Receive (2000);
	assert (replyRefusal.has_value () && replyRefusal->has_value ());
	assert ((*replyRefusal)->requestId == 32);
	auto replyDecoded = DecodeError ((*replyRefusal)->payload);
	assert (replyDecoded.has_value ());
	assert (std::get<0> (*replyDecoded) == MessageType::DataStorageReply);
	assert (std::get<1> (*replyDecoded) == ErrorCode::NotAuthorized);

	// ConfigureLogFd requires a descriptor channel, which the pipe transport
	// does not have, so it is refused instead of silently dropping the sink.
	auto logConfig = EncodeLogConfig (false, "C:\\openi2pd\\router.log", "openi2pd");
	assert (logConfig.has_value ());
	assert (client->Send ({ MessageType::ConfigureLogFd, 33, *logConfig }).has_value ());
	auto fdRefused = accepted->Receive (2000);
	assert (fdRefused.has_value () && !fdRefused->has_value ());
	auto fdRefusal = client->Receive (2000);
	assert (fdRefusal.has_value () && fdRefusal->has_value ());
	assert ((*fdRefusal)->requestId == 33);
	auto fdDecoded = DecodeError ((*fdRefusal)->payload);
	assert (fdDecoded.has_value ());
	assert (std::get<0> (*fdDecoded) == MessageType::ConfigureLogFd);
	assert (std::get<1> (*fdDecoded) == ErrorCode::NotAuthorized);

	// The channel is still usable after two bounded refusals.
	assert (client->Send ({ MessageType::RouterStorageRequest, 34, "abc" }).has_value ());
	auto stored = accepted->Receive (2000);
	assert (stored.has_value () && stored->has_value ());
	assert ((*stored)->type == MessageType::RouterStorageRequest);

	accepted->Close ();
	client->Close ();
}

void TestFrameBounds ()
{
	const std::wstring name = MakeTestPipeName (L"frame-bounds");
	auto server = WindowsServer::Create (name);
	assert (server.has_value ());
	const std::string nonce = TestNonce ();
	const std::wstring nonceHex = TestNonceHexA ();

	std::unique_ptr<WindowsChannel> client;
	std::thread worker (StartTestWorker (
		name, [&nonce] (WindowsChannel & channel) { SendHello (channel, WindowsWorkerRole::Storage, nonce); }, client));
	auto accepted = server->AcceptBound (
		5000, GetCurrentProcessId (), CurrentSessionId (), WindowsWorkerRole::Storage, nonceHex, nullptr);
	worker.join ();
	assert (accepted.has_value ());
	assert (client);

	// One frame exactly at the per-type ceiling is delivered intact.
	const size_t ceiling = MaxPayloadFor (MessageType::RouterStorageRequest);
	Message maximum { MessageType::RouterStorageRequest, 41, std::string (ceiling, 'z') };
	assert (client->Send (maximum).has_value ());
	auto delivered = accepted->Receive (2000);
	assert (delivered.has_value () && delivered->has_value ());
	assert ((*delivered)->payload.size () == ceiling);

	// One byte above the per-type ceiling is a size fault: it is answered with
	// one typed Error and the channel survives.
	PayloadWriter oversized;
	oversized.WriteU32 (PROTOCOL_MAGIC);
	oversized.WriteU16 (PROTOCOL_VERSION);
	oversized.WriteU16 (static_cast<uint16_t> (MessageType::RouterStorageRequest));
	oversized.WriteU32 (42);
	oversized.WriteU32 (static_cast<uint32_t> (ceiling + 1));
	oversized.WriteRaw (std::string (ceiling + 1, 'y'));
	const std::string oversizedFrame = oversized.Data ();
	assert (oversizedFrame.size () < MAX_MESSAGE_SIZE);
	WriteRawRecord (client->Native (), oversizedFrame);

	auto tooLarge = accepted->Receive (2000);
	assert (tooLarge.has_value () && !tooLarge->has_value ());
	auto sizeRefusal = client->Receive (2000);
	assert (sizeRefusal.has_value () && sizeRefusal->has_value ());
	assert ((*sizeRefusal)->requestId == 42);
	auto sizeDecoded = DecodeError ((*sizeRefusal)->payload);
	assert (sizeDecoded.has_value ());
	assert (std::get<1> (*sizeDecoded) == ErrorCode::TooLarge);

	// One record larger than any legal frame is terminal.
	std::vector<char> beyond (HEADER_SIZE + MAX_MESSAGE_SIZE + 64, 'x');
	std::thread writer (
		[&]
		{
			HANDLE writerEvent = CreateEventW (nullptr, TRUE, FALSE, nullptr);
			assert (writerEvent != nullptr);
			OVERLAPPED overlapped { };
			overlapped.hEvent = writerEvent;
			DWORD count = 0;
			BOOL result = WriteFile (
				client->Native (), beyond.data (), static_cast<DWORD> (beyond.size ()), &count, &overlapped);
			if (!result && GetLastError () == ERROR_IO_PENDING)
				GetOverlappedResult (client->Native (), &overlapped, &count, TRUE);
			CloseHandle (writerEvent);
		});
	auto terminal = accepted->Receive (2000);
	assert (!terminal.has_value ());
	assert (!accepted->IsOpen ());
	writer.join ();
	client->Close ();
}

void TestLegacyAcceptRoundTripAndTimeout ()
{
	const std::wstring name = MakeTestPipeName (L"legacy");
	auto server = WindowsServer::Create (name);
	assert (server.has_value ());
	std::unique_ptr<WindowsChannel> client;
	std::thread connector (
		[&name, &client]
		{
			auto connected = WindowsChannel::ConnectBound (name, GetCurrentProcessId (), CurrentSessionId (), 5000);
			if (connected) client = std::make_unique<WindowsChannel> (std::move (*connected));
		});
	auto accepted = server->Accept (5000);
	connector.join ();
	assert (accepted.has_value ());
	assert (client);

	// A quiet pipe is a timeout, not a failure, and the same channel is reused
	// for the next record.
	auto idle = accepted->Receive (25);
	assert (idle.has_value ());
	assert (!idle->has_value ());
	assert (accepted->IsOpen ());

	assert (client->Send ({ MessageType::Ping, 51, { } }).has_value ());
	auto received = accepted->Receive (2000);
	assert (received.has_value () && received->has_value ());
	assert ((*received)->type == MessageType::Ping);
	assert ((*received)->requestId == 51);

	const auto peer = accepted->Peer ();
	assert (peer.has_value ());
	assert (peer->processId == GetCurrentProcessId ());
	assert (peer->sessionId == CurrentSessionId ());
	assert (!peer->userSid.empty ());
	assert (!peer->appContainer);

	// A frame with an unattributable header (bad magic) is terminal: it cannot
	// even be answered, because a corrupt header must not choose a request id.
	PayloadWriter bogus;
	bogus.WriteU32 (0xdeadbeef);
	bogus.WriteU16 (PROTOCOL_VERSION);
	bogus.WriteU16 (static_cast<uint16_t> (MessageType::Ping));
	bogus.WriteU32 (52);
	bogus.WriteU32 (0);
	WriteRawRecord (client->Native (), bogus.Data ());
	auto malformed = accepted->Receive (2000);
	assert (!malformed.has_value ());
	assert (!accepted->IsOpen ());

	accepted->Close ();
	client->Close ();
}

void TestCompletedRecordDuringCancel ()
{
	const std::wstring name = MakeTestPipeName (L"cancel");
	auto server = WindowsServer::Create (name);
	assert (server.has_value ());
	std::unique_ptr<WindowsChannel> client;
	std::thread connector (
		[&name, &client]
		{
			auto connected = WindowsChannel::ConnectBound (name, GetCurrentProcessId (), CurrentSessionId (), 5000);
			if (connected) client = std::make_unique<WindowsChannel> (std::move (*connected));
		});
	auto accepted = server->Accept (5000);
	connector.join ();
	assert (accepted.has_value ());
	assert (client);

	// A record that completes while the deadline is cancelling the read must be
	// delivered: the cancellation only wins when no bytes arrived. The window is
	// swept because it is inherently a race.
	bool delivered = false;
	bool failed = false;
	for (int attempt = 0; attempt < 64 && !delivered; ++attempt)
	{
		std::thread reader (
			[&]
			{
				auto received = accepted->Receive (1);
				if (!received)
				{
					failed = true;
					return;
				}
				if (received->has_value () && (*received)->type == MessageType::Ping) delivered = true;
			});
		std::this_thread::sleep_for (std::chrono::microseconds (100 * (attempt % 16)));
		const auto sent = client->Send ({ MessageType::Ping, static_cast<uint32_t> (100 + attempt), { } }, 2000);
		assert (sent.has_value ());
		reader.join ();
	}
	assert (!failed);
	assert (delivered);
	accepted->Close ();
	client->Close ();
}

void TestReconnectAfterClose ()
{
	const std::wstring name = MakeTestPipeName (L"recovery");
	auto server = WindowsServer::Create (name);
	assert (server.has_value ());
	std::unique_ptr<WindowsChannel> client;
	std::thread connector (
		[&name, &client]
		{
			auto connected = WindowsChannel::ConnectBound (name, GetCurrentProcessId (), CurrentSessionId (), 5000);
			if (connected) client = std::make_unique<WindowsChannel> (std::move (*connected));
		});
	auto accepted = server->Accept (5000);
	connector.join ();
	assert (accepted.has_value ());
	assert (client);
	accepted->Close ();
	client->Close ();

	// The closed pair is unusable and the old name accepts no new worker.
	assert (!accepted->IsOpen ());
	assert (!client->IsOpen ());
	assert (!accepted->Send ({ MessageType::Ping, 1, { } }).has_value ());
	assert (!WindowsChannel::Connect (name, 100).has_value ());

	// A fresh listener on a fresh name is independent of the closed pair.
	const std::wstring replacementName = MakeTestPipeName (L"replacement");
	auto replacement = WindowsServer::Create (replacementName);
	assert (replacement.has_value ());
	std::unique_ptr<WindowsChannel> replacementClient;
	std::thread replacementConnector (
		[&replacementName, &replacementClient]
		{
			auto connected
				= WindowsChannel::ConnectBound (replacementName, GetCurrentProcessId (), CurrentSessionId (), 5000);
			if (connected) replacementClient = std::make_unique<WindowsChannel> (std::move (*connected));
		});
	auto replacementAccepted = replacement->Accept (5000);
	replacementConnector.join ();
	assert (replacementAccepted.has_value ());
	assert (replacementClient);
	replacementAccepted->Close ();
	replacementClient->Close ();
}
}

int main ()
{
	TestPolicyAndRoles ();
	TestNonceCodec ();
	TestWorkerHelloCodec ();
	TestPrivatePipeNameAndSquatting ();
	TestDaclWithExtraSid ();
	TestBindRequiresExplicitIdentity ();
	TestAcceptBoundHappyPath ();
	TestHelloRejectionsAndRearm ();
	TestDuplicateHelloIsRefused ();
	TestAuthorizationAndDescriptorRefusal ();
	TestFrameBounds ();
	TestLegacyAcceptRoundTripAndTimeout ();
	TestCompletedRecordDuringCancel ();
	TestReconnectAfterClose ();
	return 0;
}
