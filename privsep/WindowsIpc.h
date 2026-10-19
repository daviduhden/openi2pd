/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: WindowsIpc.
 *
 * Declares the Windows confinement and IPC layer: restricted worker tokens,
 * Job Objects, policy checks and probing, plus the message-mode named-pipe
 * server and channel that carry the shared Protocol frames. The worker
 * inherits no handles and reconnects by pipe name.
 *
 */

#ifndef I2PD_PRIVSEP_WINDOWS_IPC_H__
#define I2PD_PRIVSEP_WINDOWS_IPC_H__

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "Expected.h"
#include "Protocol.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace i2p::privsep
{
struct WindowsPeer
{
	DWORD processId = 0;
	DWORD sessionId = 0;
	std::wstring userSid;
	bool elevated = false;
};

enum class WindowsWorkerRole : uint8_t
{
	Supervisor,
	Log,
	Storage,
	Network,
	Crypto,
	Admin
};

struct WindowsWorkerPolicy
{
	bool allowNetwork = false;
	bool allowPersistentWrites = false;
	bool useRestrictedToken = true;
	bool useAppContainer = false;
	bool useLowPrivilegeAppContainer = false;
	bool useJobObject = true;
	bool useMitigationPolicy = true;
	uint64_t memoryLimit = 0;
	uint32_t processLimit = 1;
};

WindowsWorkerPolicy GetWindowsWorkerPolicy (WindowsWorkerRole role);
bool WindowsAllowsOperation (WindowsWorkerRole role, MessageType type);

struct WindowsIsolationSupport
{
	bool restrictedTokens = false;
	bool jobObjects = false;
	bool appContainer = false;
	bool lowPrivilegeAppContainer = false;
	bool mitigationPolicies = false;
	bool messageModePipes = true;
};

WindowsIsolationSupport ProbeWindowsIsolation ();

/** Owns one Win32 handle; closes it exactly once. */
class WindowsHandle
{
public:
	WindowsHandle () = default;
	explicit WindowsHandle (HANDLE handle)
		: m_Handle (handle)
	{
	}
	~WindowsHandle ();
	WindowsHandle (WindowsHandle && other) noexcept;
	WindowsHandle & operator= (WindowsHandle && other) noexcept;
	WindowsHandle (const WindowsHandle &) = delete;
	WindowsHandle & operator= (const WindowsHandle &) = delete;

	HANDLE Native () const { return m_Handle; }
	bool IsValid () const { return m_Handle && m_Handle != INVALID_HANDLE_VALUE; }
	void Reset (HANDLE handle = INVALID_HANDLE_VALUE);

private:
	HANDLE m_Handle = INVALID_HANDLE_VALUE;
};

i2p::expected<WindowsHandle, std::string> CreateRestrictedWorkerToken (const WindowsWorkerPolicy & policy);
i2p::expected<WindowsHandle, std::string> CreateWorkerJobObject (const WindowsWorkerPolicy & policy);

struct WindowsWorkerProcess
{
	WindowsHandle process;
	WindowsHandle thread;
	WindowsHandle job;
	DWORD processId = 0;
};

/**
 * Spawn a worker with a restricted token and an optional Job Object. The
 * worker inherits no handles: it connects to the named pipe by name. The
 * process is created suspended, assigned to the Job Object and only then
 * resumed. AppContainer/LPAC policy is probed but not silently substituted by
 * a weaker token.
 */
i2p::expected<WindowsWorkerProcess, std::string> SpawnWindowsWorker (const std::wstring & executable,
	WindowsWorkerRole role, const std::wstring & pipeName, const WindowsWorkerPolicy & policy);

/**
 * Windows local transport for the same versioned Message frames used by the
 * UNIX backend. It is a message-mode named pipe, never TCP or an undocumented
 * ALPC interface. No handle is inherited by the worker: the child reconnects
 * by name to a pipe whose protected DACL contains only the service user and
 * SYSTEM.
 */
class WindowsServer
{
public:
	WindowsServer () = default;
	~WindowsServer ();
	WindowsServer (WindowsServer && other) noexcept;
	WindowsServer & operator= (WindowsServer && other) noexcept;
	WindowsServer (const WindowsServer &) = delete;
	WindowsServer & operator= (const WindowsServer &) = delete;

	static i2p::expected<WindowsServer, std::string> Create (const std::wstring & pipeName);
	i2p::expected<class WindowsChannel, std::string> Accept (DWORD timeoutMs);
	HANDLE Native () const { return m_Pipe; }
	const std::wstring & Name () const { return m_Name; }

private:
	explicit WindowsServer (HANDLE pipe, std::wstring name, std::wstring userSid, PSECURITY_DESCRIPTOR security)
		: m_Pipe (pipe)
		, m_Name (std::move (name))
		, m_UserSid (std::move (userSid))
		, m_Security (security)
	{
	}

	HANDLE m_Pipe = INVALID_HANDLE_VALUE;
	std::wstring m_Name;
	std::wstring m_UserSid;
	PSECURITY_DESCRIPTOR m_Security = nullptr;
};

class WindowsChannel
{
public:
	WindowsChannel () = default;
	~WindowsChannel ();
	WindowsChannel (WindowsChannel && other) noexcept;
	WindowsChannel & operator= (WindowsChannel && other) noexcept;
	WindowsChannel (const WindowsChannel &) = delete;
	WindowsChannel & operator= (const WindowsChannel &) = delete;

	static i2p::expected<WindowsChannel, std::string> Connect (const std::wstring & pipeName, DWORD timeoutMs);

	bool IsOpen () const { return m_Pipe != INVALID_HANDLE_VALUE; }
	HANDLE Native () const { return m_Pipe; }
	void Close ();

	i2p::expected<void, std::string> Send (const Message & message, DWORD timeoutMs = 5000);
	i2p::expected<std::optional<Message>, std::string> Receive (DWORD timeoutMs = 1000);
	i2p::expected<WindowsPeer, std::string> Peer () const;

private:
	friend class WindowsServer;
	explicit WindowsChannel (HANDLE pipe, bool serverSide = false)
		: m_Pipe (pipe)
		, m_ServerSide (serverSide)
	{
	}

	HANDLE m_Pipe = INVALID_HANDLE_VALUE;
	bool m_ServerSide = false;
};

/** Build a local pipe name from an operator-configured endpoint. */
i2p::expected<std::wstring, std::string> MakePrivatePipeName (std::string_view endpoint);
}

#endif // _WIN32

#endif
