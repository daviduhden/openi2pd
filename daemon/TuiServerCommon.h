/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TuiServerCommon.
 *
 * Declares the transport-independent TUI request dispatcher and its result
 * type. Both the UNIX socket and Windows named-pipe servers must pass every
 * request line through this entry point, including embedded torrent import.
 *
 */

#ifndef TUI_SERVER_COMMON_H__
#define TUI_SERVER_COMMON_H__

#include <string>
#include <string_view>
#include <span>
#include <cstddef>
#include <cstdint>
#include <chrono>

namespace i2p
{
namespace tui
{
	/**
	 * Closed set of router-wide administrative operations.
	 *
	 * Every variant carries only bounded, typed data. There is deliberately no
	 * generic option name, path, URL or command string: adding an operation is
	 * a source change reviewed at the local-IPC trust boundary.
	 */
	enum class AdminCommandKind : uint8_t
	{
		Reseed = 0,
		ShutdownGraceful,
		ShutdownImmediate,
		ShutdownCancel,
		ReloadTunnels,
		Bandwidth,
		Share,
		Transit,
		Online,
		LogReopen,
		LogLevel,
		AddressAdd,
		AddressRemove,
		AddressBlock,
		AddressUnblock,
		// One configured client or server tunnel, addressed by its unique
		// service name. Both are idempotent; the executor reports whether the
		// tunnel changed state.
		TunnelStart,
		TunnelStop,
		// In-memory router ban (until restart) and address-book subscription
		// source administration. Both carry bounded text only.
		NetDbBan,
		NetDbUnban,
		AddressSubAdd,
		AddressSubRemove
	};

	/**
	 * Discrete outcome of a verb that can report more than success/failure.
	 *
	 * Only the operations listed below publish one; every other verb reports
	 * through AdminResultCode alone. The spellings are protocol tokens and are
	 * never translated.
	 */
	enum class AdminOutcome : uint8_t
	{
		None = 0,
		Started,
		Stopped,
		AlreadyRunning,
		AlreadyStopped,
		Unknown, // no configured tunnel with that name
		Disabled, // reseed is disabled by configuration
		Exists // ADDRESS-ADD found the name without a replace request
	};

	inline std::string_view AdminOutcomeName (AdminOutcome outcome)
	{
		switch (outcome)
		{
		case AdminOutcome::None: return "";
		case AdminOutcome::Started: return "started";
		case AdminOutcome::Stopped: return "stopped";
		case AdminOutcome::AlreadyRunning: return "already-running";
		case AdminOutcome::AlreadyStopped: return "already-stopped";
		case AdminOutcome::Unknown: return "unknown";
		case AdminOutcome::Disabled: return "disabled";
		case AdminOutcome::Exists: return "exists";
		default: return "";
		}
	}

	struct AdminCommand
	{
		AdminCommandKind kind = AdminCommandKind::Reseed;
		// Bandwidth: ASCII class letter. Share: 0..100.
		// LogLevel: zero-based index into the fixed level table.
		int32_t value = 0;
		// Transit: requested acceptance state.
		// AddressAdd: true re-points an existing name (ADDRESS-REPLACE); false
		// reports `exists` instead of overwriting.
		bool enabled = false;
		// Address add/remove: validated, NUL-terminated fixed storage. NetDb
		// ban/unban store the base64 router hash in `name`; subscription
		// add/remove store the validated URL in `destination` (bounded by the
		// address book to MAX_SUBSCRIPTION_URL_LENGTH); tunnel start/stop
		// store the service name in `name`. The queue copies this struct and
		// never allocates for a submission.
		char name[256] = { };
		char destination[1025] = { };
	};

	enum class AdminResultCode : uint8_t
	{
		Ok = 0,
		Accepted, // queued/executing; the IPC wait expired first
		QueueFull,
		Unavailable, // daemon not started or queue stopped
		Invalid,
		Failed
	};

	inline std::string_view AdminResultCodeName (AdminResultCode code)
	{
		switch (code)
		{
		case AdminResultCode::Ok: return "ok";
		case AdminResultCode::Accepted: return "accepted";
		case AdminResultCode::QueueFull: return "queue-full";
		case AdminResultCode::Unavailable: return "unavailable";
		case AdminResultCode::Invalid: return "invalid";
		case AdminResultCode::Failed: return "failed";
		default: return "failed";
		}
	}

	struct AdminCommandResult
	{
		AdminResultCode code = AdminResultCode::Failed;
		// Set only by the verbs with a discrete outcome; None means the verb
		// reports through `code` alone.
		AdminOutcome outcome = AdminOutcome::None;
		int32_t value = 0;
		// Transit: applied acceptance state. Tunnel start/stop: running state
		// after the command.
		bool enabled = false;
		// Fixed storage: the IPC thread only copies, never allocates, this.
		// messageLength makes the valid prefix explicit so serialization never
		// performs an unbounded strlen on the fixed array.
		char message[96] = { };
		size_t messageLength = 0;
	};

	/**
	 * Executes one command on the daemon-owned administrative thread. The
	 * callback must not block the IPC transport and must fill a typed result.
	 */
	using AdminCommandExecutor = void (*) (const AdminCommand &, AdminCommandResult &);

	inline constexpr size_t ADMIN_COMMAND_QUEUE_CAPACITY = 8;
	inline constexpr auto ADMIN_COMMAND_TIMEOUT = std::chrono::milliseconds (2000);

	// Start/stop the single daemon-owned executor. Start fails if it is
	// already running or the executor is null. The queue is fixed-capacity and
	// Submit never allocates.
	bool StartAdminCommandQueue (AdminCommandExecutor executor);
	void StopAdminCommandQueue ();

	/**
	 * Enqueue one typed command and wait up to timeout for its result.
	 *
	 * A timeout does not cancel an accepted command: it returns Accepted while
	 * the daemon-owned thread continues. QueueFull and Unavailable are reported
	 * without waiting. This call is made by the local-IPC thread.
	 */
	AdminCommandResult SubmitAdminCommand (const AdminCommand & command, std::chrono::milliseconds timeout);

	/**
	 * Result of handling one request line.
	 *
	 * The response is already framed and length-checked by the protocol
	 * helpers; transports only have to write it verbatim. A true
	 * closeConnection means the peer asked to end the session (QUIT).
	 */
	struct DispatchResult
	{
		std::string response;
		bool closeConnection = false;
	};

	/**
	 * Validate and execute one newline-free request line.
	 *
	 * This is the only protocol entry point. Both the UNIX-domain socket
	 * and the Windows named-pipe transports must call it; neither may build
	 * snapshots or execute commands itself.
	 *
	 * All exceptions are translated into an Internal error frame here. The
	 * transports must nevertheless wrap their own per-client buffering and
	 * send loop in catch(...): a bad_alloc in transport state must close one
	 * client, not unwind through std::thread and terminate the router.
	 */
	DispatchResult Dispatch (std::string_view line);
	// Embedded administration only: caller must establish the same local
	// application authority as Dispatch. Bytes are metainfo, never a path.
	DispatchResult ImportTorrent (uint32_t tunnelIndex, std::span<const uint8_t> content);
}
}

#endif // TUI_SERVER_COMMON_H__
