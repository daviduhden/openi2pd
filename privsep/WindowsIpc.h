/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: WindowsIpc.
 *
 * Declares the Windows confinement and IPC layer: restricted worker tokens,
 * AppContainer/LPAC delegation, Job Objects with a mitigation policy, the
 * message-mode named-pipe server and channel that carry the shared Protocol
 * frames, and the exec'ed worker entry point. A worker inherits no handle: it
 * reconnects by name, proves a per-spawn CSPRNG nonce with a WorkerHello frame
 * and is bound to the spawned PID, session and AppContainer SID before a
 * single request frame is served.
 *
 */

#ifndef I2PD_PRIVSEP_WINDOWS_IPC_H__
#define I2PD_PRIVSEP_WINDOWS_IPC_H__

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
/**
 * AppContainer profile and SID owner, declared by WindowsIsolation.h. It is
 * only forward declared here so this header stays independent of the isolation
 * model that includes it; callers that spawn an isolated worker include
 * WindowsIsolation.h themselves.
 */
class WindowsAppContainer;

/**
 * Identity of one pipe peer. `appContainer` and `appContainerSid` are filled
 * from the client impersonation token on the accepting side, so an accepted
 * connection is bound to a container SID rather than to a PID alone.
 */
struct WindowsPeer
{
	DWORD processId = 0;
	DWORD sessionId = 0;
	std::wstring userSid;
	bool elevated = false;
	bool appContainer = false;
	bool lowPrivilegeAppContainer = false;
	std::wstring appContainerSid;
	/** Integrity level RID (SECURITY_MANDATORY_*), 0 when unavailable. */
	DWORD integrityLevel = 0;
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

/**
 * Spawn policy for one worker. The isolation mechanism itself (AppContainer
 * versus restricted token, capability set, Internet access) is decided by
 * `GetWindowsRoleIsolation()` in WindowsIsolation.h; this structure only
 * carries the operator-visible delegation and the Job Object limits.
 */
struct WindowsWorkerPolicy
{
	bool allowNetwork = false;
	/** Secondary (LAN) reach; never implied by allowNetwork. */
	bool allowLan = false;
	bool allowPersistentWrites = false;
	/**
	 * Absolute directory granted to the worker's AppContainer SID before the
	 * spawn. A role whose isolation table demands persistent writes is
	 * refused when this is empty; there is no implicit current directory.
	 */
	std::wstring persistentWriteRoot;
	bool useRestrictedToken = true;
	bool useAppContainer = false;
	bool useLowPrivilegeAppContainer = false;
	bool useJobObject = true;
	bool useMitigationPolicy = true;
	uint64_t memoryLimit = 0;
	uint32_t processLimit = 1;
};

/**
 * Job Object, token and mitigation policy of one role. These are the
 * operator-visible limits; the isolation mechanism itself (AppContainer versus
 * restricted token, Internet capability, delegated root) is owned by
 * `GetWindowsRoleIsolation()` and must not be widened here.
 */
WindowsWorkerPolicy GetWindowsWorkerPolicy (WindowsWorkerRole role);
/**
 * Whether one Windows worker role may receive @a type.
 *
 * The shared message registry is the policy; this function adds only the
 * limitations of this transport (no descriptor channel, so descriptor-bearing
 * types are refused) and the fact that a role without a data-plane backend
 * accepts control frames only. It is not an authorization substitute: the
 * receiving channel still enforces the registry and answers a wrong-role frame
 * with a bounded typed refusal.
 */
bool WindowsAllowsOperation (WindowsWorkerRole role, MessageType type);

/** Nonce size of the WorkerHello frame, in bytes. */
inline constexpr size_t WINDOWS_WORKER_NONCE_SIZE = 32;

/** Lowercase hex encoding of one nonce; empty for a wrong-sized nonce. */
std::wstring EncodeWindowsWorkerNonce (std::string_view nonce);
/** Decode 64 hex characters into exactly WINDOWS_WORKER_NONCE_SIZE bytes. */
i2p::expected<std::string, std::string> DecodeWindowsWorkerNonce (std::string_view hex);
/** Fresh CSPRNG nonce, hex encoded; fails closed when the provider fails. */
i2p::expected<std::wstring, std::string> GenerateWindowsWorkerNonce ();

/**
 * Build the WorkerHello payload: `u8 role | u8 nonce length (32) | nonce`.
 * Rejects an unknown role, a wrong-sized nonce and an all-zero nonce.
 */
i2p::expected<std::string, std::string> EncodeWorkerHelloFrame (WindowsWorkerRole role, std::string_view nonce);
/**
 * Validate one received WorkerHello frame. The nonce is returned as raw bytes
 * and is never included in an error message.
 */
i2p::expected<std::pair<WindowsWorkerRole, std::string>, std::string> DecodeWorkerHelloFrame (const Message & message);

struct WindowsIsolationSupport
{
	bool restrictedTokens = false;
	bool jobObjects = false;
	bool appContainer = false;
	bool lowPrivilegeAppContainer = false;
	bool mitigationPolicies = false;
	bool messageModePipes = true;
};

/// Report which mechanisms this build and host can actually enforce. It never
/// enables anything: a mechanism the probe cannot confirm (for example LPAC,
/// which needs a verified mitigation profile, not just userenv exports) stays
/// reported as unavailable.
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

/**
 * Restricted token for a non-AppContainer worker role.
 *
 * Every privilege the parent holds is deleted (not merely disabled) and LUA_TOKEN
 * is added when the parent is elevated, so the worker cannot re-enable one. An
 * AppContainer or LPAC policy is refused here: those roles must be created
 * through their real container identity, never by silently degrading to a
 * restricted token.
 */
i2p::expected<WindowsHandle, std::string> CreateRestrictedWorkerToken (const WindowsWorkerPolicy & policy);
/**
 * Job Object for one worker. KILL_ON_JOB_CLOSE is the backstop for any stop
 * path that cannot wait, DIE_ON_UNHANDLED_EXCEPTION turns a crash into a
 * diagnosable exit, and the configured process/memory limits and desktop/clipboard
 * UI restrictions are applied before the worker is assigned. A role that is
 * allowed to spawn a child (the supervisor) must not set a process limit of one.
 */
i2p::expected<WindowsHandle, std::string> CreateWorkerJobObject (const WindowsWorkerPolicy & policy);

struct WindowsWorkerProcess
{
	WindowsHandle process;
	WindowsHandle thread;
	WindowsHandle job;
	DWORD processId = 0;
	/**
	 * Hex nonce that was passed on the worker command line: the supervisor must
	 * hand the same value to WindowsServer::AcceptBound().
	 */
	std::wstring nonceHex;

	/** Bounded wait for the worker; false on timeout or an invalid handle. */
	bool Wait (DWORD timeoutMs) const;
	DWORD ExitCode () const;
	/** Terminate the job (when present) and the process, then wait for exit. */
	bool TerminateAndWait (DWORD timeoutMs, UINT exitCode = ERROR_PROCESS_ABORTED);
	/** Wait for a graceful exit within the budget, then terminate. */
	bool Stop (DWORD timeoutMs = 2000);
};

/**
 * Spawn a worker bound to one supervisor.
 *
 * The child receives
 * `--privsep-worker <role> <pipe> <parentPid> <parentSession> <nonceHex>` and
 * inherits no handle: it reconnects to the named pipe by name. The process is
 * created suspended, placed in a Job Object with per-role limits and then
 * resumed. A role that the isolation table marks as AppContainer-based is
 * spawned with PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES; a role that needs
 * persistent writes additionally requires `policy.persistentWriteRoot` and an
 * explicit AppContainer grant before the spawn. There is no silent fallback to
 * an unseparated token, and every failure terminates the child and closes its
 * handles.
 *
 * An empty `nonceHex` makes the helper generate one (returned in
 * `WindowsWorkerProcess::nonceHex`); a supplied value must be 64 hex
 * characters.
 */
i2p::expected<WindowsWorkerProcess, std::string> SpawnWindowsWorker (const std::wstring & executable,
	WindowsWorkerRole role, const std::wstring & pipeName, DWORD parentProcessId, DWORD parentSessionId,
	const std::wstring & nonceHex, const WindowsAppContainer * container, const WindowsWorkerPolicy & policy);

/**
 * Windows local transport for the same versioned Message frames used by the
 * UNIX backend. It is a message-mode named pipe, never TCP or an undocumented
 * ALPC interface. No handle is inherited by the worker: the child reconnects
 * by name to a pipe whose protected DACL contains the service user, SYSTEM and
 * optionally one explicitly supplied AppContainer SID.
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

	/**
	 * Create the single pipe instance. `extraSid` is a container SID string
	 * (`S-1-...`) granted full access in addition to the service user and
	 * SYSTEM; an empty value keeps the two-ACE DACL. The name must be free:
	 * FILE_FLAG_FIRST_PIPE_INSTANCE fails instead of joining a squatted name.
	 */
	static i2p::expected<WindowsServer, std::string> Create (
		const std::wstring & pipeName, const std::wstring & extraSid = { });

	/**
	 * Test-only legacy accept. It binds the same user SID but verifies no spawn
	 * nonce, PID, session or AppContainer identity; production callers must use
	 * AcceptBound().
	 */
	i2p::expected<class WindowsChannel, std::string> Accept (DWORD timeoutMs);

	/**
	 * Accept only the expected spawned worker.
	 *
	 * Both the PID and the session are mandatory (a zero value is a programming
	 * error, never "disabled"). The client identity is read from the pipe
	 * impersonation token, so the connection must come from the same user, the
	 * expected PID/session and, when @a container is given, the exact
	 * AppContainer SID; an AppContainer peer is refused when no container is
	 * expected, and a non-AppContainer peer is refused when one is. The first
	 * frame must then be a WorkerHello carrying the expected role and nonce;
	 * it is consumed before the channel is returned. Rejected connections are
	 * closed, re-armed with a short backoff and bounded to 16 per call.
	 */
	i2p::expected<class WindowsChannel, std::string> AcceptBound (DWORD timeoutMs, DWORD expectedProcessId,
		DWORD expectedSessionId, WindowsWorkerRole role, const std::wstring & nonceHex,
		const WindowsAppContainer * container);

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

	/** One bounded ConnectNamedPipe() on the armed instance. */
	i2p::expected<bool, std::string> WaitForConnection (DWORD timeoutMs);

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
	/** Connect and authenticate the expected server process/session. */
	static i2p::expected<WindowsChannel, std::string> ConnectBound (
		const std::wstring & pipeName, DWORD expectedServerProcessId, DWORD expectedSessionId, DWORD timeoutMs);

	bool IsOpen () const { return m_Pipe != INVALID_HANDLE_VALUE; }
	HANDLE Native () const { return m_Pipe; }
	/** Detach the handle without closing it. */
	HANDLE Release ()
	{
		HANDLE h = m_Pipe;
		m_Pipe = INVALID_HANDLE_VALUE;
		return h;
	}
	void Close ();

	/**
	 * Bind this endpoint to one worker role. Once set, Receive() authorizes
	 * every decoded frame against the shared registry plus the Windows
	 * descriptor limitation: an unauthorized type is answered with one typed
	 * Error(NotAuthorized) and closes the channel after the bounded violation
	 * budget. A WorkerHello handshake must be consumed before the role is set,
	 * because the registry lets only the core receive it.
	 */
	void SetRole (WindowsWorkerRole role)
	{
		m_Role = role;
		m_RoleSet = true;
		m_CoreSide = false;
	}
	/**
	 * Bind this endpoint as the request-issuing side. It then receives only
	 * reply-direction types (Pong/Error/storage replies) and the pre-dispatch
	 * WorkerHello, never a worker request.
	 */
	void SetCoreRole ()
	{
		m_RoleSet = true;
		m_CoreSide = true;
	}
	WindowsWorkerRole Role () const { return m_Role; }
	bool RoleSet () const { return m_RoleSet; }
	bool IsCoreSide () const { return m_CoreSide; }

	i2p::expected<void, std::string> Send (const Message & message, DWORD timeoutMs = 5000);
	/**
	 * Receive one frame. A deadline expiry that cancels a read is reported as
	 * an empty optional *only* when no record completed: a record that finished
	 * while the cancellation was in flight is delivered rather than discarded.
	 * Receive() must not be called concurrently on the same channel.
	 */
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
	WindowsWorkerRole m_Role = WindowsWorkerRole::Admin;
	bool m_RoleSet = false;
	bool m_CoreSide = false;
	/** Reusable overlapped receive state; guarded by single-threaded Receive(). */
	WindowsHandle m_ReadEvent;
	std::vector<char> m_ReadBuffer;
	/** Bounded tolerance for attributable frame faults (mirrors Channel). */
	static constexpr unsigned MAX_WINDOWS_PROTOCOL_VIOLATIONS = 8;
	unsigned m_ProtocolViolations = 0;
};

/** Build a deterministic local pipe name from an operator-configured endpoint (tests). */
i2p::expected<std::wstring, std::string> MakePrivatePipeName (std::string_view endpoint);
/** Build a per-spawn, CSPRNG-scoped local pipe name. */
i2p::expected<std::wstring, std::string> MakeRandomPipeName ();

/**
 * Entry point of an exec'ed Windows worker. It pins standard descriptors to
 * NUL, connects to the supervisor pipe bound to the given parent PID/session,
 * sends the WorkerHello frame with the spawn nonce, binds the role and serves
 * only control frames. It never initialises or runs the router: a role without
 * a Windows data-plane backend answers a data frame with a typed Error and
 * exits instead of silently ignoring it.
 */
int RunWindowsWorker (std::string_view role, std::string_view pipeName, std::string_view parentPid,
	std::string_view parentSession, std::string_view nonceHex);
}

#endif // _WIN32

#endif
