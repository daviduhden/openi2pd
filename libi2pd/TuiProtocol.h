/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TuiProtocol.
 *
 * Defines the I2PDTUI v1 request/response codec: typed request fields, error
 * codes, and bounded parsing/serialization helpers shared by the daemon server
 * and the terminal client. These limits are part of the local-IPC trust
 * boundary.
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
 * Versioned, line-framed protocol used between the router and i2pd-tui over
 * a local UNIX stream socket.
 *
 * Request:   I2PDTUI/1 <COMMAND> [arguments]\n
 * Response:  I2PDTUI/1 OK <payload-length>\n<payload>
 *            I2PDTUI/1 ERR <error-code> <message-length>\n<message>
 *
 * The payload is a sequence of "key=escaped-value\n" fields. Every length is
 * explicit and bounded; the receiver must never allocate based on a value it
 * has not validated. Torrent commands are validated here and then mediated by
 * the torrents tunnel's Asio service thread; the IPC thread never touches
 * mutable engine state. Configuration snapshots are read-only and limited to
 * a server-side allowlist; version 1 has no configuration writer. Router-wide
 * writes are restricted to a closed set of explicitly typed ADMIN operations:
 * reseed, shutdown/cancel, tunnel reload, bandwidth class, share percentage,
 * transit acceptance and daemon log level. The dispatcher converts each into a
 * bounded daemon command; no request carries a generic option name, path, URL
 * or shell string. ADMIN replies carry typed fields: admin=<action>,
 * result=ok|accepted|error, code=<reason>, value=<integer>, message=<text>.
 */
namespace i2p
{
namespace tui
{
	inline constexpr std::string_view PROTOCOL_NAME = "I2PDTUI";
	inline constexpr uint32_t PROTOCOL_VERSION = 1;

	// Requests are single lines. A malicious client cannot make the daemon
	// buffer without bound: the accumulator is bounded by MAX_REQUEST_LINE.
	inline constexpr size_t MAX_REQUEST_LINE = 4096;
	inline constexpr size_t MAX_RESPONSE_PAYLOAD = 256 * 1024;
	inline constexpr size_t MAX_FIELD_NAME = 64;
	inline constexpr size_t MAX_LOG_RECORDS_PER_REQUEST = 200;
	inline constexpr size_t MAX_PEER_RECORDS_PER_REQUEST = 200;
	inline constexpr size_t MAX_CONNECTION_RECORDS_PER_REQUEST = 200;
	inline constexpr size_t MAX_NETDB_RECORDS_PER_REQUEST = 200;
	inline constexpr size_t MAX_TUNNEL_RECORDS_PER_REQUEST = 200;
	inline constexpr size_t MAX_TORRENT_RECORDS_PER_REQUEST = 200;
	inline constexpr size_t MAX_TORRENT_FILE_RECORDS_PER_REQUEST = 1000;
	inline constexpr size_t MAX_TORRENT_PEER_RECORDS_PER_REQUEST = 200;
	inline constexpr size_t MAX_TORRENT_TRACKER_RECORDS_PER_REQUEST = 100;
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
		Logs,
		Config, // bounded, typed snapshot of allowlisted options; no writer
		AdminTransit,
		AdminLogLevel,
		AdminReseed,
		AdminShutdown,
		AdminReloadTunnels,
		AdminBandwidth,
		AdminShare,
		Ping,
		Quit,
		Torrents,
		TorrentDetail,
		TorrentStart,
		TorrentStop,
		TorrentPause,
		TorrentResume,
		TorrentReannounce,
		TorrentRemove,
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
		TorrentFilePriority,
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
		uint32_t limit = 0; // LOGS/PEERS/CONNECTIONS/NETDB-*/TORRENTS: record count
		uint32_t tunnelIndex = 0; // TORRENT-*: index into the sorted tunnel snapshot
		int torrentId = 0; // TORRENT-*: engine-assigned torrent id
		bool deleteFiles = false; // TORRENT-REMOVE: explicit files flag
		bool enabled = false; // ADMIN TRANSIT: requested acceptance state
		AdminShutdownMode shutdownMode = AdminShutdownMode::Graceful; // ADMIN SHUTDOWN
		// ADMIN BANDWIDTH: ASCII class; ADMIN SHARE: percent;
		// ADMIN LOGLEVEL: zero-based index into the fixed level table.
		uint32_t adminValue = 0;
		std::string argument; // TORRENT-ADD-*/ADMIN LOGLEVEL: bounded argument
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
			|| command == "TORRENT-REMOVE" || command == "TORRENT-START-ALL" || command == "TORRENT-STOP-ALL"
			|| command == "TORRENT-REANNOUNCE-ALL" || command == "TORRENT-ADD-MAGNET"
			|| command == "TORRENT-TRACKER-ADD" || command == "TORRENT-TRACKER-REMOVE" || command == "TORRENT-COMMENTS"
			|| command == "TORRENT-COMMENT-ADD" || command == "TORRENT-COMMENT-HIDE"
			|| command == "TORRENT-FILE-PRIORITY" || command == "TORRENT-CREATE-V1"
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
			else if (command == "TORRENT-REMOVE")
				request.type = RequestType::TorrentRemove;
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
			else if (command == "TORRENT-FILE-PRIORITY")
				request.type = RequestType::TorrentFilePriority;
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

			if (request.type == RequestType::TorrentCreateV1)
			{
				auto size = detail::ParseUnsigned (detail::NextToken (rest));
				auto privacy = detail::NextToken (rest);
				auto count = detail::ParseUnsigned (detail::NextToken (rest));
				if (!size || *size > 256 * 1024 * 1024 || (*size && (*size < 16384 || (*size & (*size - 1))))
					|| (privacy != "public" && privacy != "private") || !count || *count > MAX_TORRENT_TRACKER_RECORDS_PER_REQUEST)
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
						return i2p::unexpected (ProtocolError { ErrorCode::BadArgument, "invalid creation tracker tier" });
					if (*tier == request.createTrackers.size ()) request.createTrackers.emplace_back ();
					request.createTrackers[static_cast<size_t> (*tier)].emplace_back (token.substr (split + 1));
				}
				auto source = detail::ParseRestOfLine (rest);
				if (!source) return i2p::unexpected (source.error ());
				request.argument = std::move (*source);
				rest = {};
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
				|| request.type == RequestType::TorrentRemove || request.type == RequestType::TorrentAddTracker
				|| request.type == RequestType::TorrentRemoveTracker
				|| request.type == RequestType::TorrentCommentsEnable || request.type == RequestType::TorrentCommentAdd
				|| request.type == RequestType::TorrentCommentHide || request.type == RequestType::TorrentFilePriority)
			{
				if (rest.empty ())
					return i2p::unexpected (ProtocolError { ErrorCode::BadArgument, "missing torrent id" });
				auto id = detail::ParseTorrentId (detail::NextToken (rest));
				if (!id) return i2p::unexpected (id.error ());
				request.torrentId = *id;

				if (request.type == RequestType::TorrentAddTracker || request.type == RequestType::TorrentRemoveTracker
					|| request.type == RequestType::TorrentCommentsEnable
					|| request.type == RequestType::TorrentCommentAdd || request.type == RequestType::TorrentCommentHide
					|| request.type == RequestType::TorrentFilePriority)
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
