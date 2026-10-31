/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TuiProtocol.
 *
 * Defines the I2PDTUI v1 request/response codec: typed request fields,
 * error codes, and bounded parsing/serialization helpers shared by the
 * daemon server and the terminal client. These limits are part of the
 * local-IPC trust boundary.
 *
 */

#ifndef TUI_PROTOCOL_H__
#define TUI_PROTOCOL_H__

#include <charconv>
#include <concepts>
#include <cstdint>
#include <algorithm>
#include "Expected.h"
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

/**
 * Line-framed protocol between the router and i2pd-tui over a
 * local UNIX stream socket.
 *
 * Request:   I2PDTUI/1 &lt;COMMAND&gt; [arguments]\n
 * Response:  I2PDTUI/1 OK &lt;payload-length&gt;\n&lt;payload&gt;
 *            I2PDTUI/1 ERR &lt;error-code&gt; &lt;message-length&gt;\n
 *            &lt;message&gt;
 *
 * Payloads contain "key=escaped-value\n" fields. Lengths are explicit
 * and bounded; the receiver validates each before allocating from it.
 * Torrent commands are validated here, then mediated by the tunnel's
 * service thread. The IPC thread never touches mutable engine state.
 * Configuration snapshots are read-only and restricted to a server-side
 * allowlist; version 1 has no configuration writer. Router-wide writes
 * use typed ADMIN operations: reseed, cancel, reload, and start/stop
 * for one configured client or
 * server tunnel, bandwidth class, share percentage, transit acceptance,
 * daemon log level, bounded address-book edits, and subscription
 * or ban-list edits.
 * Each request maps to a bounded daemon command. Requests cannot carry
 * generic option names, paths, URLs or shell strings. ADMIN
 * replies use typed fields: admin=&lt;action&gt;,
 * result=ok|accepted|error, code=&lt;reason&gt;, value=&lt;integer&gt;,
 * message=&lt;text&gt;, and discrete outcomes use outcome=&lt;token&gt;
 * (started|stopped|already-running|already-stopped|unknown|disabled|
 * exists).
 * Reseed replies immediately with started, already-running or disabled;
 * it never waits for bootstrap completion.
 * TORRENT-DETAIL accepts an optional trailing comment page
 * (`[offset] [limit]`, offset 0..255, limit 1..16). Without arguments,
 * it returns the newest page supported by existing clients.
 */
namespace i2p
{
namespace tui
{
	/**
	 * Framing invariants shared by the daemon and the console.
	 *
	 * A request is exactly one line; a response is a header line followed
	 * by exactly `payload-size` bytes. There are no request ids and no
	 * pipelining, so a receiver must consume the declared payload before
	 * it can read the next header. The header bounds are inclusive (4096
	 * bytes for a request, 128 bytes for a response header), and the 256
	 * KiB payload ceiling is what makes the `uint32_t` conversion of a
	 * parsed length safe in ParseResponseHeader.
	 *
	 * Every constant below is the wire contract, mirrored by
	 * docs/tui-ipc.md. A producer must clamp to them before serializing
	 * because the receiver rejects an oversized frame instead of
	 * truncating it; a limit is a ceiling for one response, never an
	 * invitation to emit a full page of every collection in a single
	 * message.
	 */
	inline constexpr std::string_view PROTOCOL_NAME = "I2PDTUI";
	inline constexpr uint32_t PROTOCOL_VERSION = 1;

	// Requests are single lines. A malicious client cannot make the daemon
	// buffer without bound: the accumulator is bounded by
	// MAX_REQUEST_LINE.
	inline constexpr size_t MAX_REQUEST_LINE = 4096;
	inline constexpr size_t MAX_RESPONSE_PAYLOAD = 256 * 1024;
	inline constexpr size_t MAX_FIELD_NAME = 64;
	inline constexpr size_t MAX_LOG_RECORDS_PER_REQUEST = 200;
	inline constexpr size_t MAX_PEER_RECORDS_PER_REQUEST = 200;
	inline constexpr size_t MAX_CONNECTION_RECORDS_PER_REQUEST = 200;
	inline constexpr size_t MAX_NETDB_RECORDS_PER_REQUEST = 200;
	inline constexpr size_t MAX_ADDRESS_BOOK_RECORDS_PER_REQUEST = 200;
	// Address-book subscription URLs are capped by the address book
	// itself; the protocol refuses to page beyond that fixed bound.
	inline constexpr size_t MAX_SUBSCRIPTION_RECORDS_PER_REQUEST = 16;
	inline constexpr size_t MAX_BLOCKLIST_RECORDS_PER_REQUEST = 200;
	inline constexpr size_t MAX_SUBSCRIPTION_URL_LENGTH = 512;
	inline constexpr size_t MAX_ROUTER_HASH_TEXT_LENGTH = 64; // base64 of a 32-byte hash
	// A configured tunnel service name is printable, whitespace-free text.
	// The bound matches ClientContext::I2P_TUNNEL_SERVICE_NAME_MAX_LENGTH
	// so a name the client context can never hold is also rejected at the
	// wire.
	inline constexpr size_t MAX_TUNNEL_SERVICE_NAME_LENGTH = 64;
	// Address-book administration: a stored name is bounded by the address
	// book, an added/replaced name additionally has to be a host label,
	// and a destination is one full base64 identity.
	inline constexpr size_t MAX_ADDRESS_NAME_LENGTH = 255;
	inline constexpr size_t MAX_ADDRESS_ADD_NAME_LENGTH = 67;
	inline constexpr size_t MAX_ADDRESS_DESTINATION_LENGTH = 1024;
	inline constexpr size_t MAX_TUNNEL_RECORDS_PER_REQUEST = 200;
	inline constexpr size_t MAX_SERVICE_RECORDS_PER_REQUEST = 200;
	inline constexpr size_t MAX_TORRENT_RECORDS_PER_REQUEST = 200;
	inline constexpr size_t MAX_TORRENT_FILE_RECORDS_PER_REQUEST = 1000;
	inline constexpr size_t MAX_TORRENT_PEER_RECORDS_PER_REQUEST = 200;
	inline constexpr size_t MAX_TORRENT_TRACKER_RECORDS_PER_REQUEST = 100;
	// One TORRENT-DETAIL response emits at most this many comment records
	// even though a torrent retains up to MAX_COMMENT_COUNT (256) records,
	// of which at most MAX_COMMENT_HIDDEN_COUNT (64) are hidden. Clients
	// page with the optional `[offset] [limit]` arguments instead of the
	// daemon growing the single-response bound. The offset bound is the
	// highest record index the retained store can have.
	inline constexpr size_t MAX_TORRENT_COMMENT_RECORDS_PER_REQUEST = 16;
	inline constexpr size_t MAX_TORRENT_COMMENT_OFFSET = 255;
	inline constexpr size_t MAX_TORRENT_ARGUMENT_LENGTH = 2048;
	// CONFIG returns an allowlisted, typed snapshot. Both the number of
	// options and each string value are bounded so a misconfigured router
	// cannot turn the snapshot into an unbounded response.
	inline constexpr size_t MAX_CONFIG_RECORDS_PER_REQUEST = 128;
	inline constexpr size_t MAX_CONFIG_VALUE_LENGTH = 512;
	inline constexpr size_t MAX_ADMIN_ARGUMENT_LENGTH = 16;
	inline constexpr size_t MAX_ADMIN_BANDWIDTH_TOKEN_LENGTH = 1;

	enum class AdminShutdownMode : uint8_t
	{
		Graceful = 0,
		Immediate,
		Cancel
	};

	enum class RequestType
	{
		State,
		Peers,
		Connections,
		Tunnels,
		NetDb,
		NetDbRouters,
		NetDbLeaseSets,
		Destinations,
		Services,
		Logs,
		Config, // bounded, typed snapshot of allowlisted options; no writer
		AdminTransit,
		AdminOnline,
		AdminLogReopen,
		AdminLogLevel,
		AdminReseed,
		AdminShutdown,
		AdminReloadTunnels,
		AdminBandwidth,
		AdminShare,
		AdminTunnelStart,
		AdminTunnelStop,
		AdminAddressAdd,
		AdminAddressReplace,
		AdminAddressRemove,
		AdminAddressBlock,
		AdminAddressUnblock,
		AdminNetDbBan,
		AdminNetDbUnban,
		AdminAddressSubAdd,
		AdminAddressSubRemove,
		AddressBook,
		AddressBookBlocked,
		AddressBookSubscriptions,
		Blocklist,
		Ping,
		Quit,
		Torrents,
		TorrentDetail,
		TorrentStart,
		TorrentStop,
		TorrentPause,
		TorrentResume,
		TorrentReannounce,
		TorrentRecheck,
		TorrentRemove,
		TorrentRemoveAll,
		TorrentStartAll,
		TorrentStopAll,
		TorrentReannounceAll,
		TorrentAddTracker,
		TorrentRemoveTracker,
		TorrentCreate,
		TorrentCreateV1,
		TorrentCreateCancel,
		TorrentCommentsEnable,
		TorrentCommentAdd,
		TorrentCommentHide,
		TorrentCommentUnhide,
		TorrentFilePriority,
		TorrentSetComment,
		TorrentSetCreatedBy,
		TorrentAddMagnet,
		TorrentAddFile,
		TorrentSession,
		TorrentDht,
		TorrentTunnels
	};

	struct Request
	{
		RequestType type;
		uint64_t after = 0; // LOGS: last sequence seen by the client
		uint32_t limit = 0; // LOGS/PEERS/CONNECTIONS/NETDB-*/ADDRESSBOOK/TORRENTS: record count
		uint32_t tunnelIndex = 0; // TORRENT-*: index into the sorted tunnel snapshot
		int torrentId = 0; // TORRENT-*: engine-assigned torrent id
		// TORRENT-REMOVE/TORRENT-REMOVE-ALL: explicit exact "files" token
		bool deleteFiles = false;
		bool enabled = false; // ADMIN TRANSIT: requested acceptance state
		AdminShutdownMode shutdownMode = AdminShutdownMode::Graceful; // ADMIN SHUTDOWN
		// ADMIN BANDWIDTH: ASCII class; ADMIN SHARE: percent;
		// ADMIN LOGLEVEL: zero-based index into the fixed level table.
		uint32_t adminValue = 0;
		std::string argument; // TORRENT-ADD-*/ADMIN LOGLEVEL/ADDRESS-*/NETDB-*/TUNNEL-*: bounded argument
		std::string argument2; // ADMIN ADDRESS-ADD/ADDRESS-REPLACE: full base64 identity
		// TORRENT-DETAIL: first retained comment record to emit (0..255) and
		// the page size (1..16). The defaults reproduce the paging-free
		// response every existing client already understands.
		uint32_t commentOffset = 0;
		uint32_t commentLimit = static_cast<uint32_t> (MAX_TORRENT_COMMENT_RECORDS_PER_REQUEST);
		uint32_t createPieceLength = 0;
		bool createPrivate = false;
		std::vector<std::vector<std::string>> createTrackers;
	};

	enum class ErrorCode : uint32_t
	{
		None = 0,
		MalformedRequest = 1,
		UnsupportedVersion = 2,
		UnknownCommand = 3,
		BadArgument = 4,
		ResponseTooLarge = 5,
		Internal = 6
	};

	inline std::string_view ErrorCodeName (ErrorCode code)
	{
		switch (code)
		{
		case ErrorCode::None: return "none";
		case ErrorCode::MalformedRequest: return "malformed-request";
		case ErrorCode::UnsupportedVersion: return "unsupported-version";
		case ErrorCode::UnknownCommand: return "unknown-command";
		case ErrorCode::BadArgument: return "bad-argument";
		case ErrorCode::ResponseTooLarge: return "response-too-large";
		case ErrorCode::Internal: return "internal";
		default: return "unknown";
		}
	}

	struct ProtocolError
	{
		ErrorCode code;
		std::string_view message;
	};

	enum class ResponseStatus
	{
		Ok,
		Error
	};

	struct ResponseHeader
	{
		ResponseStatus status;
		uint32_t payloadSize;
		ErrorCode errorCode;
	};

	namespace detail
	{
		inline i2p::expected<uint64_t, ProtocolError> ParseUnsigned (std::string_view text)
		{
			if (text.empty ())
				return i2p::unexpected (ProtocolError { ErrorCode::BadArgument, "empty numeric argument" });
			uint64_t value = 0;
			auto result = std::from_chars (text.data (), text.data () + text.size (), value);
			if (result.ec != std::errc () || result.ptr != text.data () + text.size ())
				return i2p::unexpected (ProtocolError { ErrorCode::BadArgument, "invalid numeric argument" });
			return value;
		}

		inline i2p::expected<uint32_t, ProtocolError> ParseVersion (std::string_view & rest)
		{
			const auto space = rest.find (' ');
			if (space == std::string_view::npos)
				return i2p::unexpected (ProtocolError { ErrorCode::MalformedRequest, "missing command" });
			auto version = ParseUnsigned (rest.substr (0, space));
			if (!version)
				return i2p::unexpected (ProtocolError { ErrorCode::MalformedRequest, "invalid protocol version" });
			rest.remove_prefix (space + 1);
			if (*version != PROTOCOL_VERSION)
				return i2p::unexpected (
					ProtocolError { ErrorCode::UnsupportedVersion, "unsupported protocol version" });
			return static_cast<uint32_t> (*version);
		}

		inline std::string_view NextToken (std::string_view & rest)
		{
			while (!rest.empty () && rest.front () == ' ') rest.remove_prefix (1);
			const auto space = rest.find (' ');
			if (space == std::string_view::npos)
			{
				auto token = rest;
				rest = { };
				return token;
			}
			auto token = rest.substr (0, space);
			rest.remove_prefix (space + 1);
			return token;
		}

		inline i2p::expected<uint32_t, ProtocolError> ParseIndex (std::string_view text)
		{
			auto value = ParseUnsigned (text);
			if (!value) return i2p::unexpected (value.error ());
			if (*value > std::numeric_limits<uint32_t>::max ())
				return i2p::unexpected (ProtocolError { ErrorCode::BadArgument, "index out of range" });
			return static_cast<uint32_t> (*value);
		}

		inline i2p::expected<int, ProtocolError> ParseTorrentId (std::string_view text)
		{
			auto value = ParseUnsigned (text);
			if (!value) return i2p::unexpected (value.error ());
			if (!*value || *value > static_cast<uint64_t> (std::numeric_limits<int>::max ()))
				return i2p::unexpected (ProtocolError { ErrorCode::BadArgument, "torrent id out of range" });
			return static_cast<int> (*value);
		}

		inline i2p::expected<uint32_t, ProtocolError> ParseLimit (std::string_view text, uint32_t maximum)
		{
			auto value = ParseUnsigned (text);
			if (!value) return i2p::unexpected (value.error ());
			if (!*value) return i2p::unexpected (ProtocolError { ErrorCode::BadArgument, "limit must be positive" });
			return static_cast<uint32_t> (std::min<uint64_t> (*value, maximum));
		}

		// Consumes the rest of the request as one bounded argument. This is
		// used for magnets and paths, where spaces are part of the value.
		inline i2p::expected<std::string, ProtocolError> ParseRestOfLine (std::string_view rest)
		{
			while (!rest.empty () && rest.front () == ' ') rest.remove_prefix (1);
			if (rest.empty ()) return i2p::unexpected (ProtocolError { ErrorCode::BadArgument, "missing argument" });
			if (rest.size () > MAX_TORRENT_ARGUMENT_LENGTH)
				return i2p::unexpected (ProtocolError { ErrorCode::BadArgument, "argument too long" });
			if (rest.find ('\0') != std::string_view::npos || rest.find ('\r') != std::string_view::npos
				|| rest.find ('\n') != std::string_view::npos)
				return i2p::unexpected (ProtocolError { ErrorCode::BadArgument, "invalid argument character" });
			for (const unsigned char c : rest)
				if (c < 0x20 || c == 0x7f)
					return i2p::unexpected (ProtocolError { ErrorCode::BadArgument, "invalid argument character" });
			return std::string (rest);
		}

		inline std::optional<ProtocolError> RejectTrailing (std::string_view rest)
		{
			while (!rest.empty () && rest.front () == ' ') rest.remove_prefix (1);
			if (!rest.empty ()) return ProtocolError { ErrorCode::BadArgument, "unexpected trailing arguments" };
			return std::nullopt;
		}

		inline bool IsAdminLogLevel (std::string_view value)
		{
			return value == "none" || value == "critical" || value == "error" || value == "warn" || value == "info"
				|| value == "debug";
		}

		inline std::optional<uint32_t> AdminLogLevelIndex (std::string_view value)
		{
			if (value == "none") return 0;
			if (value == "critical") return 1;
			if (value == "error") return 2;
			if (value == "warn") return 3;
			if (value == "info") return 4;
			if (value == "debug") return 5;
			return std::nullopt;
		}

		// Only the router's documented bandwidth classes are accepted. The
		// token is an exact one-byte capability letter; ranges, suffixes and
		// free-form configuration values are deliberately not expressible.
		inline bool IsAdminBandwidthToken (std::string_view value)
		{
			if (value.size () != MAX_ADMIN_BANDWIDTH_TOKEN_LENGTH) return false;
			switch (value.front ())
			{
			case 'K':
			case 'L':
			case 'M':
			case 'N':
			case 'O':
			case 'P':
			case 'X': return true;
			default: return false;
			}
		}

		inline std::optional<AdminShutdownMode> ParseAdminShutdownMode (std::string_view value)
		{
			if (value == "GRACEFUL") return AdminShutdownMode::Graceful;
			if (value == "IMMEDIATE") return AdminShutdownMode::Immediate;
			if (value == "CANCEL") return AdminShutdownMode::Cancel;
			return std::nullopt;
		}

		inline bool IsSafeAdminText (std::string_view value, size_t maximum)
		{
			if (value.empty () || value.size () > maximum) return false;
			return std::all_of (value.begin (), value.end (), [] (unsigned char ch) { return ch > 32 && ch != 127; });
		}
	}

	/**
	 * Parse one request line (without the trailing newline).
	 */
	inline i2p::expected<Request, ProtocolError> ParseRequest (std::string_view line)
	{
		if (line.empty ()) return i2p::unexpected (ProtocolError { ErrorCode::MalformedRequest, "empty request" });
		if (line.size () > MAX_REQUEST_LINE)
			return i2p::unexpected (ProtocolError { ErrorCode::MalformedRequest, "request line too long" });
		if (line.front () == ' ' || line.back () == ' ')
			return i2p::unexpected (ProtocolError { ErrorCode::MalformedRequest, "surrounding whitespace" });

		const auto separator = line.find ('/');
		if (separator == std::string_view::npos || line.substr (0, separator) != PROTOCOL_NAME)
			return i2p::unexpected (ProtocolError { ErrorCode::MalformedRequest, "bad protocol name" });

		std::string_view rest = line.substr (separator + 1);
		auto version = detail::ParseVersion (rest);
		if (!version) return i2p::unexpected (version.error ());

		const auto command = detail::NextToken (rest);
		if (command.empty ()) return i2p::unexpected (ProtocolError { ErrorCode::MalformedRequest, "missing command" });

		Request request { };
		if (command == "STATE")
		{
			request.type = RequestType::State;
		}
		else if (command == "PEERS")
		{
			request.type = RequestType::Peers;
			request.limit = MAX_PEER_RECORDS_PER_REQUEST;
			if (!rest.empty ())
			{
				auto limit = detail::ParseUnsigned (detail::NextToken (rest));
				if (!limit || *limit == 0 || *limit > MAX_PEER_RECORDS_PER_REQUEST)
					return i2p::unexpected (ProtocolError { ErrorCode::BadArgument, "peer limit out of range" });
				request.limit = static_cast<uint32_t> (*limit);
			}
		}
		else if (command == "CONNECTIONS")
		{
			request.type = RequestType::Connections;
			request.limit = MAX_CONNECTION_RECORDS_PER_REQUEST;
			if (!rest.empty ())
			{
				auto limit = detail::ParseLimit (detail::NextToken (rest), MAX_CONNECTION_RECORDS_PER_REQUEST);
				if (!limit) return i2p::unexpected (limit.error ());
				request.limit = *limit;
			}
		}
		else if (command == "TUNNELS")
		{
			request.type = RequestType::Tunnels;
		}
		else if (command == "NETDB")
		{
			request.type = RequestType::NetDb;
		}
		else if (command == "NETDB-ROUTERS" || command == "NETDB-LEASESETS")
		{
			request.type = command == "NETDB-ROUTERS" ? RequestType::NetDbRouters : RequestType::NetDbLeaseSets;
			request.limit = MAX_NETDB_RECORDS_PER_REQUEST;
			if (!rest.empty ())
			{
				auto limit = detail::ParseLimit (detail::NextToken (rest), MAX_NETDB_RECORDS_PER_REQUEST);
				if (!limit) return i2p::unexpected (limit.error ());
				request.limit = *limit;
			}
		}
		else if (command == "DESTINATIONS")
		{
			request.type = RequestType::Destinations;
		}
		else if (command == "SERVICES")
		{
			request.type = RequestType::Services;
			request.limit = MAX_SERVICE_RECORDS_PER_REQUEST;
			if (!rest.empty ())
			{
				auto limit = detail::ParseLimit (detail::NextToken (rest), MAX_SERVICE_RECORDS_PER_REQUEST);
				if (!limit) return i2p::unexpected (limit.error ());
				request.limit = *limit;
			}
		}
		else if (command == "LOGS")
		{
			request.type = RequestType::Logs;
			if (rest.empty ())
				return i2p::unexpected (ProtocolError { ErrorCode::BadArgument, "missing log sequence" });
			auto after = detail::ParseUnsigned (detail::NextToken (rest));
			if (!after) return i2p::unexpected (after.error ());
			request.after = *after;
			request.limit = MAX_LOG_RECORDS_PER_REQUEST;
			if (!rest.empty ())
			{
				auto limit = detail::ParseUnsigned (detail::NextToken (rest));
				if (!limit || *limit == 0 || *limit > MAX_LOG_RECORDS_PER_REQUEST)
					return i2p::unexpected (ProtocolError { ErrorCode::BadArgument, "log limit out of range" });
				request.limit = static_cast<uint32_t> (*limit);
			}
		}
		else if (command == "CONFIG")
		{
			// The snapshot has no selectable sections and no arguments.
			// In particular there is no CONFIG-SET command in version 1:
			// configuration writes require a main-loop command queue.
			auto trailing = detail::RejectTrailing (rest);
			if (trailing) return i2p::unexpected (*trailing);
			request.type = RequestType::Config;
		}
		else if (command == "ADDRESSBOOK" || command == "ADDRESSBOOK-BLOCKED")
		{
			request.type = command == "ADDRESSBOOK" ? RequestType::AddressBook : RequestType::AddressBookBlocked;
			request.limit = MAX_ADDRESS_BOOK_RECORDS_PER_REQUEST;
			if (!rest.empty ())
			{
				auto limit = detail::ParseLimit (detail::NextToken (rest), MAX_ADDRESS_BOOK_RECORDS_PER_REQUEST);
				if (!limit) return i2p::unexpected (limit.error ());
				request.limit = *limit;
			}
		}
		else if (command == "ADDRESSBOOK-SUBSCRIPTIONS")
		{
			request.type = RequestType::AddressBookSubscriptions;
			request.limit = MAX_SUBSCRIPTION_RECORDS_PER_REQUEST;
			if (!rest.empty ())
			{
				auto limit = detail::ParseLimit (detail::NextToken (rest), MAX_SUBSCRIPTION_RECORDS_PER_REQUEST);
				if (!limit) return i2p::unexpected (limit.error ());
				request.limit = *limit;
			}
		}
		else if (command == "BLOCKLIST")
		{
			// Bounded read-only view: startup blocklist counters plus the
			// in-memory runtime router bans. There is no file writer.
			request.type = RequestType::Blocklist;
			request.limit = MAX_BLOCKLIST_RECORDS_PER_REQUEST;
			if (!rest.empty ())
			{
				auto limit = detail::ParseLimit (detail::NextToken (rest), MAX_BLOCKLIST_RECORDS_PER_REQUEST);
				if (!limit) return i2p::unexpected (limit.error ());
				request.limit = *limit;
			}
		}
		else if (command == "ADMIN")
		{
			const auto action = detail::NextToken (rest);
			if (action.empty ())
				return i2p::unexpected (ProtocolError { ErrorCode::BadArgument, "missing admin action" });

			if (action == "TRANSIT")
			{
				const auto value = detail::NextToken (rest);
				if (value == "ON")
					request.enabled = true;
				else if (value == "OFF")
					request.enabled = false;
				else
					return i2p::unexpected (
						ProtocolError { ErrorCode::BadArgument, "admin transit expects ON or OFF" });
				request.type = RequestType::AdminTransit;
			}
			else if (action == "ONLINE")
			{
				const auto value = detail::NextToken (rest);
				if (value == "ON")
					request.enabled = true;
				else if (value == "OFF")
					request.enabled = false;
				else
					return i2p::unexpected (ProtocolError { ErrorCode::BadArgument, "admin online expects ON or OFF" });
				request.type = RequestType::AdminOnline;
			}
			else if (action == "LOG-REOPEN")
			{
				request.type = RequestType::AdminLogReopen;
			}
			else if (action == "LOGLEVEL")
			{
				const auto level = detail::NextToken (rest);
				auto index = detail::AdminLogLevelIndex (level);
				if (level.empty () || level.size () > MAX_ADMIN_ARGUMENT_LENGTH || !index)
					return i2p::unexpected (ProtocolError { ErrorCode::BadArgument, "invalid admin log level" });
				request.adminValue = *index;
				request.argument = std::string (level);
				request.type = RequestType::AdminLogLevel;
			}
			else if (action == "RESEED")
			{
				request.type = RequestType::AdminReseed;
			}
			else if (action == "SHUTDOWN")
			{
				const auto mode = detail::ParseAdminShutdownMode (detail::NextToken (rest));
				if (!mode)
					return i2p::unexpected (ProtocolError {
						ErrorCode::BadArgument, "admin shutdown expects GRACEFUL, IMMEDIATE or CANCEL" });
				request.shutdownMode = *mode;
				request.type = RequestType::AdminShutdown;
			}
			else if (action == "RELOAD")
			{
				if (detail::NextToken (rest) != "TUNNELS")
					return i2p::unexpected (ProtocolError { ErrorCode::BadArgument, "admin reload expects TUNNELS" });
				request.type = RequestType::AdminReloadTunnels;
			}
			else if (action == "BANDWIDTH")
			{
				const auto token = detail::NextToken (rest);
				if (!detail::IsAdminBandwidthToken (token))
					return i2p::unexpected (ProtocolError { ErrorCode::BadArgument, "invalid admin bandwidth class" });
				request.adminValue = static_cast<uint32_t> (token.front ());
				request.type = RequestType::AdminBandwidth;
			}
			else if (action == "SHARE")
			{
				auto percent = detail::ParseUnsigned (detail::NextToken (rest));
				if (!percent || *percent > 100)
					return i2p::unexpected (ProtocolError { ErrorCode::BadArgument, "admin share expects 0..100" });
				request.adminValue = static_cast<uint32_t> (*percent);
				request.type = RequestType::AdminShare;
			}
			else if (action == "TUNNEL-START" || action == "TUNNEL-STOP")
			{
				// One configured client or server tunnel, addressed by its
				// unique service name. The name is printable, free of spaces
				// and bounded; the daemon resolves it against its own
				// inventory and rejects anything it does not own.
				const auto name = detail::NextToken (rest);
				if (!detail::IsSafeAdminText (name, MAX_TUNNEL_SERVICE_NAME_LENGTH))
					return i2p::unexpected (ProtocolError { ErrorCode::BadArgument, "invalid tunnel service name" });
				auto trailing = detail::RejectTrailing (rest);
				if (trailing) return i2p::unexpected (*trailing);
				request.argument = std::string (name);
				request.type = action == "TUNNEL-START" ? RequestType::AdminTunnelStart : RequestType::AdminTunnelStop;
			}
			else if (action == "ADDRESS-ADD" || action == "ADDRESS-REPLACE" || action == "ADDRESS-REMOVE"
				|| action == "ADDRESS-BLOCK" || action == "ADDRESS-UNBLOCK")
			{
				const bool changesDestination = action == "ADDRESS-ADD" || action == "ADDRESS-REPLACE";
				const auto name = detail::NextToken (rest);
				if (!detail::IsSafeAdminText (
						name, changesDestination ? MAX_ADDRESS_ADD_NAME_LENGTH : MAX_ADDRESS_NAME_LENGTH))
					return i2p::unexpected (ProtocolError { ErrorCode::BadArgument, "invalid address name" });
				request.argument = std::string (name);
				if (changesDestination)
				{
					const auto destination = detail::NextToken (rest);
					if (!detail::IsSafeAdminText (destination, MAX_ADDRESS_DESTINATION_LENGTH))
						return i2p::unexpected (ProtocolError { ErrorCode::BadArgument, "invalid base64 destination" });
					request.argument2 = std::string (destination);
					// ADDRESS-ADD never re-points an existing name: the daemon
					// answers `exists` and the operator has to ask for
					// ADDRESS-REPLACE explicitly.
					request.type
						= action == "ADDRESS-ADD" ? RequestType::AdminAddressAdd : RequestType::AdminAddressReplace;
				}
				else if (action == "ADDRESS-REMOVE")
					request.type = RequestType::AdminAddressRemove;
				else if (action == "ADDRESS-BLOCK")
					request.type = RequestType::AdminAddressBlock;
				else
					request.type = RequestType::AdminAddressUnblock;
			}
			else if (action == "NETDB-BAN" || action == "NETDB-UNBAN")
			{
				const auto hash = detail::NextToken (rest);
				if (!detail::IsSafeAdminText (hash, MAX_ROUTER_HASH_TEXT_LENGTH))
					return i2p::unexpected (ProtocolError { ErrorCode::BadArgument, "invalid router hash" });
				auto trailing = detail::RejectTrailing (rest);
				if (trailing) return i2p::unexpected (*trailing);
				request.argument = std::string (hash);
				request.type = action == "NETDB-BAN" ? RequestType::AdminNetDbBan : RequestType::AdminNetDbUnban;
			}
			else if (action == "ADDRESS-SUB-ADD" || action == "ADDRESS-SUB-REMOVE")
			{
				// A subscription URL is a single line without whitespace; the
				// address book performs the scheme/host validation.
				const auto url = detail::NextToken (rest);
				if (!detail::IsSafeAdminText (url, MAX_SUBSCRIPTION_URL_LENGTH))
					return i2p::unexpected (ProtocolError { ErrorCode::BadArgument, "invalid subscription URL" });
				auto trailing = detail::RejectTrailing (rest);
				if (trailing) return i2p::unexpected (*trailing);
				request.argument = std::string (url);
				request.type = action == "ADDRESS-SUB-ADD" ? RequestType::AdminAddressSubAdd
														   : RequestType::AdminAddressSubRemove;
			}
			else
			{
				return i2p::unexpected (ProtocolError { ErrorCode::BadArgument, "unknown admin action" });
			}
		}
		else if (command == "PING")
		{
			request.type = RequestType::Ping;
		}
		else if (command == "QUIT")
		{
			request.type = RequestType::Quit;
		}
		else if (command == "TORRENT-TUNNELS")
		{
			request.type = RequestType::TorrentTunnels;
		}
		else if (command == "TORRENTS")
		{
			request.type = RequestType::Torrents;
			if (rest.empty ())
				return i2p::unexpected (ProtocolError { ErrorCode::BadArgument, "missing tunnel index" });
			auto index = detail::ParseIndex (detail::NextToken (rest));
			if (!index) return i2p::unexpected (index.error ());
			request.tunnelIndex = *index;
			request.limit = MAX_TORRENT_RECORDS_PER_REQUEST;
			if (!rest.empty ())
			{
				auto limit = detail::ParseLimit (detail::NextToken (rest), MAX_TORRENT_RECORDS_PER_REQUEST);
				if (!limit) return i2p::unexpected (limit.error ());
				request.limit = *limit;
			}
		}
		else if (command == "TORRENT-DETAIL" || command == "TORRENT-START" || command == "TORRENT-STOP"
			|| command == "TORRENT-PAUSE" || command == "TORRENT-RESUME" || command == "TORRENT-REANNOUNCE"
			|| command == "TORRENT-RECHECK" || command == "TORRENT-REMOVE" || command == "TORRENT-REMOVE-ALL"
			|| command == "TORRENT-START-ALL" || command == "TORRENT-STOP-ALL" || command == "TORRENT-REANNOUNCE-ALL"
			|| command == "TORRENT-ADD-MAGNET" || command == "TORRENT-TRACKER-ADD"
			|| command == "TORRENT-TRACKER-REMOVE" || command == "TORRENT-COMMENTS" || command == "TORRENT-COMMENT-ADD"
			|| command == "TORRENT-COMMENT-HIDE" || command == "TORRENT-COMMENT-UNHIDE"
			|| command == "TORRENT-FILE-PRIORITY" || command == "TORRENT-META-COMMENT"
			|| command == "TORRENT-META-CREATED-BY" || command == "TORRENT-CREATE-V1"
			|| command == "TORRENT-CREATE-CANCEL" || command == "TORRENT-CREATE" || command == "TORRENT-ADD-FILE"
			|| command == "TORRENT-SESSION" || command == "TORRENT-DHT")
		{
			if (rest.empty ())
				return i2p::unexpected (ProtocolError { ErrorCode::BadArgument, "missing tunnel index" });
			auto index = detail::ParseIndex (detail::NextToken (rest));
			if (!index) return i2p::unexpected (index.error ());
			request.tunnelIndex = *index;

			if (command == "TORRENT-DETAIL")
				request.type = RequestType::TorrentDetail;
			else if (command == "TORRENT-START")
				request.type = RequestType::TorrentStart;
			else if (command == "TORRENT-STOP")
				request.type = RequestType::TorrentStop;
			else if (command == "TORRENT-PAUSE")
				request.type = RequestType::TorrentPause;
			else if (command == "TORRENT-RESUME")
				request.type = RequestType::TorrentResume;
			else if (command == "TORRENT-REANNOUNCE")
				request.type = RequestType::TorrentReannounce;
			else if (command == "TORRENT-RECHECK")
				request.type = RequestType::TorrentRecheck;
			else if (command == "TORRENT-REMOVE")
				request.type = RequestType::TorrentRemove;
			else if (command == "TORRENT-REMOVE-ALL")
				// Session-wide removal is addressed by the tunnel index alone;
				// there is no per-torrent id to bind.
				request.type = RequestType::TorrentRemoveAll;
			else if (command == "TORRENT-START-ALL")
				request.type = RequestType::TorrentStartAll;
			else if (command == "TORRENT-STOP-ALL")
				request.type = RequestType::TorrentStopAll;
			else if (command == "TORRENT-REANNOUNCE-ALL")
				request.type = RequestType::TorrentReannounceAll;
			else if (command == "TORRENT-TRACKER-ADD")
				request.type = RequestType::TorrentAddTracker;
			else if (command == "TORRENT-TRACKER-REMOVE")
				request.type = RequestType::TorrentRemoveTracker;
			else if (command == "TORRENT-COMMENTS")
				request.type = RequestType::TorrentCommentsEnable;
			else if (command == "TORRENT-COMMENT-ADD")
				request.type = RequestType::TorrentCommentAdd;
			else if (command == "TORRENT-COMMENT-HIDE")
				request.type = RequestType::TorrentCommentHide;
			else if (command == "TORRENT-COMMENT-UNHIDE")
				request.type = RequestType::TorrentCommentUnhide;
			else if (command == "TORRENT-FILE-PRIORITY")
				request.type = RequestType::TorrentFilePriority;
			else if (command == "TORRENT-META-COMMENT")
				request.type = RequestType::TorrentSetComment;
			else if (command == "TORRENT-META-CREATED-BY")
				request.type = RequestType::TorrentSetCreatedBy;
			else if (command == "TORRENT-CREATE-V1")
				request.type = RequestType::TorrentCreateV1;
			else if (command == "TORRENT-CREATE-CANCEL")
				request.type = RequestType::TorrentCreateCancel;
			else if (command == "TORRENT-CREATE")
				request.type = RequestType::TorrentCreate;
			else if (command == "TORRENT-ADD-MAGNET")
				request.type = RequestType::TorrentAddMagnet;
			else if (command == "TORRENT-ADD-FILE")
				request.type = RequestType::TorrentAddFile;
			else if (command == "TORRENT-SESSION")
				request.type = RequestType::TorrentSession;
			else
				request.type = RequestType::TorrentDht;

			if (request.type == RequestType::TorrentRemoveAll)
			{
				// Bounded exactly like TORRENT-REMOVE: the only accepted
				// option is the exact token "files"; anything else is
				// rejected as an unknown remove option.
				if (!rest.empty ())
				{
					auto option = detail::NextToken (rest);
					if (option != "files")
						return i2p::unexpected (ProtocolError { ErrorCode::BadArgument, "unknown remove option" });
					request.deleteFiles = true;
				}
			}
			else if (request.type == RequestType::TorrentCreateV1)
			{
				auto size = detail::ParseUnsigned (detail::NextToken (rest));
				auto privacy = detail::NextToken (rest);
				auto count = detail::ParseUnsigned (detail::NextToken (rest));
				if (!size || *size > 256 * 1024 * 1024 || (*size && (*size < 16384 || (*size & (*size - 1))))
					|| (privacy != "public" && privacy != "private") || !count
					|| *count > MAX_TORRENT_TRACKER_RECORDS_PER_REQUEST)
					return i2p::unexpected (ProtocolError { ErrorCode::BadArgument, "invalid creation options" });
				request.createPieceLength = static_cast<uint32_t> (*size);
				request.createPrivate = privacy == "private";
				for (uint64_t i = 0; i < *count; ++i)
				{
					auto token = detail::NextToken (rest);
					auto split = token.find (':');
					auto tier = detail::ParseUnsigned (token.substr (0, split));
					if (split == std::string_view::npos || !tier || *tier > request.createTrackers.size ()
						|| token.size () <= split + 1 || token.size () > MAX_TORRENT_ARGUMENT_LENGTH)
						return i2p::unexpected (
							ProtocolError { ErrorCode::BadArgument, "invalid creation tracker tier" });
					if (*tier == request.createTrackers.size ()) request.createTrackers.emplace_back ();
					request.createTrackers[static_cast<size_t> (*tier)].emplace_back (token.substr (split + 1));
				}
				auto source = detail::ParseRestOfLine (rest);
				if (!source) return i2p::unexpected (source.error ());
				request.argument = std::move (*source);
				rest = { };
			}
			else if (request.type == RequestType::TorrentCreate || request.type == RequestType::TorrentAddMagnet
				|| request.type == RequestType::TorrentAddFile)
			{
				auto argument = detail::ParseRestOfLine (rest);
				if (!argument) return i2p::unexpected (argument.error ());
				request.argument = std::move (*argument);
				rest = { };
			}
			else if (request.type == RequestType::TorrentDetail || request.type == RequestType::TorrentStart
				|| request.type == RequestType::TorrentStop || request.type == RequestType::TorrentPause
				|| request.type == RequestType::TorrentResume || request.type == RequestType::TorrentReannounce
				|| request.type == RequestType::TorrentRecheck || request.type == RequestType::TorrentRemove
				|| request.type == RequestType::TorrentAddTracker || request.type == RequestType::TorrentRemoveTracker
				|| request.type == RequestType::TorrentCommentsEnable || request.type == RequestType::TorrentCommentAdd
				|| request.type == RequestType::TorrentCommentHide || request.type == RequestType::TorrentCommentUnhide
				|| request.type == RequestType::TorrentFilePriority || request.type == RequestType::TorrentSetComment
				|| request.type == RequestType::TorrentSetCreatedBy)
			{
				if (rest.empty ())
					return i2p::unexpected (ProtocolError { ErrorCode::BadArgument, "missing torrent id" });
				auto id = detail::ParseTorrentId (detail::NextToken (rest));
				if (!id) return i2p::unexpected (id.error ());
				request.torrentId = *id;

				if (request.type == RequestType::TorrentDetail && !rest.empty ())
				{
					// Optional trailing page: TORRENT-DETAIL &lt;tunnel&gt; &lt;id&gt;
					// [offset] [limit]. Both bounds are explicit so a hostile
					// client cannot ask the daemon to emit an unbounded slice
					// of the retained comment store. Absent arguments keep the
					// historic response (offset 0, limit 16).
					auto offset = detail::ParseUnsigned (detail::NextToken (rest));
					if (!offset || *offset > MAX_TORRENT_COMMENT_OFFSET)
						return i2p::unexpected (
							ProtocolError { ErrorCode::BadArgument, "comment offset out of range" });
					request.commentOffset = static_cast<uint32_t> (*offset);
					if (!rest.empty ())
					{
						auto limit = detail::ParseUnsigned (detail::NextToken (rest));
						if (!limit || *limit == 0 || *limit > MAX_TORRENT_COMMENT_RECORDS_PER_REQUEST)
							return i2p::unexpected (
								ProtocolError { ErrorCode::BadArgument, "comment limit out of range" });
						request.commentLimit = static_cast<uint32_t> (*limit);
					}
				}

				if (request.type == RequestType::TorrentAddTracker || request.type == RequestType::TorrentRemoveTracker
					|| request.type == RequestType::TorrentCommentsEnable
					|| request.type == RequestType::TorrentCommentAdd || request.type == RequestType::TorrentCommentHide
					|| request.type == RequestType::TorrentCommentUnhide
					|| request.type == RequestType::TorrentFilePriority
					|| request.type == RequestType::TorrentSetComment
					|| request.type == RequestType::TorrentSetCreatedBy)
				{
					auto argument = detail::ParseRestOfLine (rest);
					if (!argument) return i2p::unexpected (argument.error ());
					request.argument = std::move (*argument);
					rest = { };
				}
				if (request.type == RequestType::TorrentRemove && !rest.empty ())
				{
					auto option = detail::NextToken (rest);
					if (option != "files")
						return i2p::unexpected (ProtocolError { ErrorCode::BadArgument, "unknown remove option" });
					request.deleteFiles = true;
				}
			}
		}
		else
		{
			return i2p::unexpected (ProtocolError { ErrorCode::UnknownCommand, "unknown command" });
		}

		if (auto trailing = detail::RejectTrailing (rest)) return i2p::unexpected (*trailing);
		return request;
	}

	inline i2p::expected<ResponseHeader, ProtocolError> ParseResponseHeader (std::string_view line)
	{
		if (line.size () > 128)
			return i2p::unexpected (ProtocolError { ErrorCode::MalformedRequest, "response header too long" });
		const auto separator = line.find ('/');
		if (separator == std::string_view::npos || line.substr (0, separator) != PROTOCOL_NAME)
			return i2p::unexpected (ProtocolError { ErrorCode::MalformedRequest, "bad response protocol" });

		std::string_view rest = line.substr (separator + 1);
		auto version = detail::ParseVersion (rest);
		if (!version) return i2p::unexpected (version.error ());

		const auto status = detail::NextToken (rest);
		ResponseHeader header { };
		if (status == "OK")
		{
			auto length = detail::ParseUnsigned (detail::NextToken (rest));
			if (!length || *length > MAX_RESPONSE_PAYLOAD)
				return i2p::unexpected (ProtocolError { ErrorCode::ResponseTooLarge, "invalid payload length" });
			header.status = ResponseStatus::Ok;
			header.payloadSize = static_cast<uint32_t> (*length);
			header.errorCode = ErrorCode::None;
		}
		else if (status == "ERR")
		{
			auto code = detail::ParseUnsigned (detail::NextToken (rest));
			auto length = detail::ParseUnsigned (detail::NextToken (rest));
			if (!code || *code > static_cast<uint64_t> (ErrorCode::Internal) || !length
				|| *length > MAX_RESPONSE_PAYLOAD)
				return i2p::unexpected (ProtocolError { ErrorCode::MalformedRequest, "invalid error response" });
			header.status = ResponseStatus::Error;
			header.errorCode = static_cast<ErrorCode> (*code);
			header.payloadSize = static_cast<uint32_t> (*length);
		}
		else
		{
			return i2p::unexpected (ProtocolError { ErrorCode::MalformedRequest, "unknown response status" });
		}

		while (!rest.empty () && rest.front () == ' ') rest.remove_prefix (1);
		if (!rest.empty ())
			return i2p::unexpected (ProtocolError { ErrorCode::MalformedRequest, "trailing response data" });
		return header;
	}

	// Field values are escaped so a payload always splits on LF and a
	// field on its first '='. Only backslash, LF and CR need escaping, and
	// '=' is left alone because the splitter takes the first one, so a
	// value may contain '=' unchanged. UnescapeValue deliberately keeps an
	// unknown escape as backslash plus the character, which makes it the
	// exact inverse for every string EscapeValue can produce and never
	// drops input.
	inline std::string EscapeValue (std::string_view value)
	{
		std::string escaped;
		escaped.reserve (value.size ());
		for (char ch : value)
		{
			switch (ch)
			{
			case '\\': escaped += "\\\\"; break;
			case '\n': escaped += "\\n"; break;
			case '\r': escaped += "\\r"; break;
			default: escaped.push_back (ch); break;
			}
		}
		return escaped;
	}

	inline std::string UnescapeValue (std::string_view value)
	{
		std::string decoded;
		decoded.reserve (value.size ());
		for (size_t i = 0; i < value.size (); ++i)
		{
			if (value[i] != '\\' || i + 1 >= value.size ())
			{
				decoded.push_back (value[i]);
				continue;
			}
			switch (value[++i])
			{
			case 'n': decoded.push_back ('\n'); break;
			case 'r': decoded.push_back ('\r'); break;
			case '\\': decoded.push_back ('\\'); break;
			default:
				decoded.push_back ('\\');
				decoded.push_back (value[i]);
				break;
			}
		}
		return decoded;
	}

	inline void AppendField (std::string & payload, std::string_view name, std::string_view value)
	{
		if (name.empty () || name.size () > MAX_FIELD_NAME) return;
		payload.append (name);
		payload.push_back ('=');
		payload.append (EscapeValue (value));
		payload.push_back ('\n');
	}

	template <typename T>
		requires std::integral<T> || std::same_as<T, bool>
	inline void AppendField (std::string & payload, std::string_view name, T value)
	{
		if constexpr (std::same_as<T, bool>)
			AppendField (payload, name, value ? "1" : "0");
		else
			AppendField (payload, name, std::to_string (value));
	}

	// Serialise one frame. The payload is not re-checked against
	// MAX_RESPONSE_PAYLOAD here: the daemon verifies the bound while
	// building the payload and substitutes a ResponseTooLarge error frame,
	// and a client only ever parses a frame whose declared length it
	// already accepted.
	inline std::string MakeResponse (
		ResponseStatus status, std::string_view payload, ErrorCode errorCode = ErrorCode::None)
	{
		std::string frame;
		frame.reserve (payload.size () + 64);
		frame.append (PROTOCOL_NAME);
		frame.push_back ('/');
		frame.append (std::to_string (PROTOCOL_VERSION));
		if (status == ResponseStatus::Ok)
		{
			frame.append (" OK ");
			frame.append (std::to_string (payload.size ()));
		}
		else
		{
			frame.append (" ERR ");
			frame.append (std::to_string (static_cast<uint32_t> (errorCode)));
			frame.push_back (' ');
			frame.append (std::to_string (payload.size ()));
		}
		frame.append ("\n");
		frame.append (payload);
		return frame;
	}

	inline std::string MakeErrorResponse (ErrorCode code, std::string_view message)
	{
		return MakeResponse (ResponseStatus::Error, message, code);
	}
}
}

#endif // TUI_PROTOCOL_H__
