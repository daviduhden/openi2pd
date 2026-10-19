/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test privsep windows ipc.
 *
 * Windows-only checks for worker pipe policy, private pipe-name derivation, restricted DACLs, versioned frame round trips and recovery after closing a connection.
 *
 */

#ifndef _WIN32
#error "test-privsep-windows-ipc is a Windows-only test"
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <aclapi.h>
#include <sddl.h>

#include <cassert>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "privsep/WindowsIpc.h"

using namespace i2p::privsep;

namespace
{
std::wstring MakeTestPipeName (const wchar_t * suffix)
{
	std::wstring name = L"\\\\.\\pipe\\openi2pd-worker-test-";
	name += std::to_wstring (GetCurrentProcessId ());
	name += L"-";
	name += std::to_wstring (GetTickCount64 ());
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

void CheckRestrictedDacl (HANDLE pipe)
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
	for (DWORD i = 0; i < sizeInfo.AceCount; ++i)
	{
		void * raw = nullptr;
		assert (GetAce (dacl, i, &raw));
		auto * ace = static_cast<ACCESS_ALLOWED_ACE *> (raw);
		assert (ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE);
		assert (!IsWellKnown (&ace->SidStart, WinWorldSid));
		assert (!IsWellKnown (&ace->SidStart, WinAuthenticatedUserSid));
		assert (!IsWellKnown (&ace->SidStart, WinAnonymousSid));
	}
	LocalFree (descriptor);
}

void TestPolicyAndPolicyPrimitives ()
{
	const auto storage = GetWindowsWorkerPolicy (WindowsWorkerRole::Storage);
	assert (!storage.allowNetwork);
	assert (storage.allowPersistentWrites);
	assert (storage.useRestrictedToken);
	assert (storage.useJobObject);

	const auto network = GetWindowsWorkerPolicy (WindowsWorkerRole::Network);
	assert (network.allowNetwork);
	assert (!network.allowPersistentWrites);
	assert (network.useRestrictedToken);
	assert (network.useJobObject);

	const auto log = GetWindowsWorkerPolicy (WindowsWorkerRole::Log);
	assert (!log.allowNetwork);
	assert (!log.allowPersistentWrites);
	assert (WindowsAllowsOperation (WindowsWorkerRole::Log, MessageType::LogRecord));
	assert (WindowsAllowsOperation (WindowsWorkerRole::Log, MessageType::Ping));
	assert (!WindowsAllowsOperation (WindowsWorkerRole::Storage, MessageType::LogRecord));

	const auto support = ProbeWindowsIsolation ();
	assert (support.messageModePipes);
	assert (support.restrictedTokens);
	assert (support.jobObjects);
	// AppContainer/LPAC is capability-probed; it is not silently enabled when
	// the host does not expose the documented userenv APIs.

	auto token = CreateRestrictedWorkerToken (storage);
	assert (token.has_value ());
	assert (token->IsValid ());
	auto job = CreateWorkerJobObject (storage);
	assert (job.has_value ());
	assert (job->IsValid ());
}

void TestPrivatePipeName ()
{
	auto name = MakePrivatePipeName ("C:\\ProgramData\\openi2pd");
	assert (name.has_value ());
	assert (name->rfind (L"\\\\.\\pipe\\openi2pd-worker-", 0) == 0);
	auto remote = WindowsServer::Create (L"\\\\remote\\pipe\\openi2pd");
	assert (!remote.has_value ());
}

void TestRoundTripAndPeer ()
{
	const std::wstring name = MakeTestPipeName (L"roundtrip");
	auto server = WindowsServer::Create (name);
	assert (server.has_value ());
	CheckRestrictedDacl (server->Native ());

	std::unique_ptr<WindowsChannel> client;
	std::thread connector (
		[&name, &client]
		{
			auto connected = WindowsChannel::Connect (name, 5000);
			if (connected) client = std::make_unique<WindowsChannel> (std::move (*connected));
		});
	auto accepted = server->Accept (5000);
	connector.join ();
	assert (accepted.has_value ());
	assert (client);
	auto & channel = *accepted;

	assert (channel.Send (Message { MessageType::Ping, 42, { } }).has_value ());
	auto received = client->Receive (1000);
	assert (received.has_value ());
	assert (received->has_value ());
	assert ((*received)->type == MessageType::Ping);
	assert ((*received)->requestId == 42);

	const auto peer = channel.Peer ();
	assert (peer.has_value ());
	assert (peer->processId == GetCurrentProcessId ());
	assert (!peer->userSid.empty ());
	assert (peer->sessionId != 0);

	// The versioned frame rejects an unknown type before dispatch.
	assert (channel.Send (Message { static_cast<MessageType> (0xffff), 7, { } }).has_value ());
	auto malformed = client->Receive (1000);
	assert (!malformed.has_value ());

	channel.Close ();
	client->Close ();
}

void TestRecoveryAfterClose ()
{
	const std::wstring name = MakeTestPipeName (L"recovery");
	auto server = WindowsServer::Create (name);
	assert (server.has_value ());
	std::unique_ptr<WindowsChannel> client;
	std::thread connector (
		[&name, &client]
		{
			auto connected = WindowsChannel::Connect (name, 5000);
			if (connected) client = std::make_unique<WindowsChannel> (std::move (*connected));
		});
	auto accepted = server->Accept (5000);
	connector.join ();
	assert (accepted.has_value ());
	assert (client);
	accepted->Close ();
	client->Close ();

	// A new listener with a fresh name is independent of the closed pair.
	const std::wstring replacementName = MakeTestPipeName (L"replacement");
	auto replacement = WindowsServer::Create (replacementName);
	assert (replacement.has_value ());
	// A fresh listener is independent from the closed pair and accepts a new
	// client; it is the old name that must no longer connect.
	auto replacementClient = WindowsChannel::Connect (replacementName, 1000);
	assert (replacementClient.has_value ());
	replacementClient->Close ();
}
}

int main ()
{
	TestPolicyAndPolicyPrimitives ();
	TestPrivatePipeName ();
	TestRoundTripAndPeer ();
	TestRecoveryAfterClose ();
	return 0;
}
