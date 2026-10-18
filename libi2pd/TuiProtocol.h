/*
* Copyright (c) 2013-2026, The PurpleI2P Project
*
* This file is part of Open I2Pd and licensed under BSD3
*
* See full license text in LICENSE file at top of project tree
*/

#ifndef TUI_PROTOCOL_H__
#define TUI_PROTOCOL_H__

#include <charconv>
#include <concepts>
#include <cstdint>
#include <algorithm>
#include <expected>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

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
 * mutable engine state.
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
	inline constexpr size_t MAX_TUNNEL_RECORDS_PER_REQUEST = 200;
	inline constexpr size_t MAX_TORRENT_RECORDS_PER_REQUEST = 200;
	inline constexpr size_t MAX_TORRENT_FILE_RECORDS_PER_REQUEST = 1000;
	inline constexpr size_t MAX_TORRENT_PEER_RECORDS_PER_REQUEST = 200;
	inline constexpr size_t MAX_TORRENT_TRACKER_RECORDS_PER_REQUEST = 100;
	inline constexpr size_t MAX_TORRENT_ARGUMENT_LENGTH = 2048;

	enum class RequestType
	{
		State,
		Peers,
		Tunnels,
		NetDb,
		Destinations,
		Logs,
		Ping,
		Quit,
		Torrents,
		TorrentDetail,
		TorrentStart,
		TorrentStop,
		TorrentReannounce,
		TorrentRemove,
		TorrentStartAll,
		TorrentStopAll,
		TorrentReannounceAll,
		TorrentAddMagnet,
		TorrentAddFile,
		TorrentSession,
		TorrentDht,
		TorrentTunnels
	};

	struct Request
	{
		RequestType type;
		uint64_t after = 0;   // LOGS: last sequence seen by the client
		uint32_t limit = 0;   // LOGS/PEERS/TORRENTS: requested record count
		uint32_t tunnelIndex = 0; // TORRENT-*: index into the sorted tunnel snapshot
		int torrentId = 0;    // TORRENT-*: engine-assigned torrent id
		bool deleteFiles = false; // TORRENT-REMOVE: explicit files flag
		std::string argument; // TORRENT-ADD-MAGNET/FILE: bounded rest of line
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
		inline std::expected<uint64_t, ProtocolError> ParseUnsigned (std::string_view text)
		{
			if (text.empty ()) return std::unexpected (ProtocolError{ErrorCode::BadArgument, "empty numeric argument"});
			uint64_t value = 0;
			auto result = std::from_chars (text.data (), text.data () + text.size (), value);
			if (result.ec != std::errc () || result.ptr != text.data () + text.size ())
				return std::unexpected (ProtocolError{ErrorCode::BadArgument, "invalid numeric argument"});
			return value;
		}

		inline std::expected<uint32_t, ProtocolError> ParseVersion (std::string_view& rest)
		{
			const auto space = rest.find (' ');
			if (space == std::string_view::npos)
				return std::unexpected (ProtocolError{ErrorCode::MalformedRequest, "missing command"});
			auto version = ParseUnsigned (rest.substr (0, space));
			if (!version)
				return std::unexpected (ProtocolError{ErrorCode::MalformedRequest, "invalid protocol version"});
			rest.remove_prefix (space + 1);
			if (*version != PROTOCOL_VERSION)
				return std::unexpected (ProtocolError{ErrorCode::UnsupportedVersion, "unsupported protocol version"});
			return static_cast<uint32_t> (*version);
		}

		inline std::string_view NextToken (std::string_view& rest)
		{
			while (!rest.empty () && rest.front () == ' ') rest.remove_prefix (1);
			const auto space = rest.find (' ');
			if (space == std::string_view::npos)
			{
				auto token = rest;
				rest = {};
				return token;
			}
			auto token = rest.substr (0, space);
			rest.remove_prefix (space + 1);
			return token;
		}

		inline std::expected<uint32_t, ProtocolError> ParseIndex (std::string_view text)
		{
			auto value = ParseUnsigned (text);
			if (!value) return std::unexpected (value.error ());
			if (*value > std::numeric_limits<uint32_t>::max ())
				return std::unexpected (ProtocolError{ErrorCode::BadArgument, "index out of range"});
			return static_cast<uint32_t> (*value);
		}

		inline std::expected<int, ProtocolError> ParseTorrentId (std::string_view text)
		{
			auto value = ParseUnsigned (text);
			if (!value) return std::unexpected (value.error ());
			if (!*value || *value > static_cast<uint64_t> (std::numeric_limits<int>::max ()))
				return std::unexpected (ProtocolError{ErrorCode::BadArgument, "torrent id out of range"});
			return static_cast<int> (*value);
		}

		inline std::expected<uint32_t, ProtocolError> ParseLimit (std::string_view text, uint32_t maximum)
		{
			auto value = ParseUnsigned (text);
			if (!value) return std::unexpected (value.error ());
			if (!*value) return std::unexpected (ProtocolError{ErrorCode::BadArgument, "limit must be positive"});
			return static_cast<uint32_t> (std::min<uint64_t> (*value, maximum));
		}

		// Consumes the rest of the request as one bounded argument. This is
		// used for magnets and paths, where spaces are part of the value.
		inline std::expected<std::string, ProtocolError> ParseRestOfLine (std::string_view rest)
		{
			while (!rest.empty () && rest.front () == ' ') rest.remove_prefix (1);
			if (rest.empty ())
				return std::unexpected (ProtocolError{ErrorCode::BadArgument, "missing argument"});
			if (rest.size () > MAX_TORRENT_ARGUMENT_LENGTH)
				return std::unexpected (ProtocolError{ErrorCode::BadArgument, "argument too long"});
			if (rest.find ('\0') != std::string_view::npos ||
				rest.find ('\r') != std::string_view::npos ||
				rest.find ('\n') != std::string_view::npos)
				return std::unexpected (ProtocolError{ErrorCode::BadArgument, "invalid argument character"});
			return std::string (rest);
		}

		inline std::optional<ProtocolError> RejectTrailing (std::string_view rest)
		{
			while (!rest.empty () && rest.front () == ' ') rest.remove_prefix (1);
			if (!rest.empty ())
				return ProtocolError{ErrorCode::BadArgument, "unexpected trailing arguments"};
			return std::nullopt;
		}
	}

	/**
	 * Parse one request line (without the trailing newline).
	 */
	inline std::expected<Request, ProtocolError> ParseRequest (std::string_view line)
	{
		if (line.empty ()) return std::unexpected (ProtocolError{ErrorCode::MalformedRequest, "empty request"});
		if (line.size () > MAX_REQUEST_LINE)
			return std::unexpected (ProtocolError{ErrorCode::MalformedRequest, "request line too long"});
		if (line.front () == ' ' || line.back () == ' ')
			return std::unexpected (ProtocolError{ErrorCode::MalformedRequest, "surrounding whitespace"});

		const auto separator = line.find ('/');
		if (separator == std::string_view::npos || line.substr (0, separator) != PROTOCOL_NAME)
			return std::unexpected (ProtocolError{ErrorCode::MalformedRequest, "bad protocol name"});

		std::string_view rest = line.substr (separator + 1);
		auto version = detail::ParseVersion (rest);
		if (!version) return std::unexpected (version.error ());

		const auto command = detail::NextToken (rest);
		if (command.empty ()) return std::unexpected (ProtocolError{ErrorCode::MalformedRequest, "missing command"});

		Request request{};
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
					return std::unexpected (ProtocolError{ErrorCode::BadArgument, "peer limit out of range"});
				request.limit = static_cast<uint32_t> (*limit);
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
		else if (command == "DESTINATIONS")
		{
			request.type = RequestType::Destinations;
		}
		else if (command == "LOGS")
		{
			request.type = RequestType::Logs;
			if (rest.empty ())
				return std::unexpected (ProtocolError{ErrorCode::BadArgument, "missing log sequence"});
			auto after = detail::ParseUnsigned (detail::NextToken (rest));
			if (!after) return std::unexpected (after.error ());
			request.after = *after;
			request.limit = MAX_LOG_RECORDS_PER_REQUEST;
			if (!rest.empty ())
			{
				auto limit = detail::ParseUnsigned (detail::NextToken (rest));
				if (!limit || *limit == 0 || *limit > MAX_LOG_RECORDS_PER_REQUEST)
					return std::unexpected (ProtocolError{ErrorCode::BadArgument, "log limit out of range"});
				request.limit = static_cast<uint32_t> (*limit);
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
				return std::unexpected (ProtocolError{ErrorCode::BadArgument, "missing tunnel index"});
			auto index = detail::ParseIndex (detail::NextToken (rest));
			if (!index) return std::unexpected (index.error ());
			request.tunnelIndex = *index;
			request.limit = MAX_TORRENT_RECORDS_PER_REQUEST;
			if (!rest.empty ())
			{
				auto limit = detail::ParseLimit (detail::NextToken (rest), MAX_TORRENT_RECORDS_PER_REQUEST);
				if (!limit) return std::unexpected (limit.error ());
				request.limit = *limit;
			}
		}
		else if (command == "TORRENT-DETAIL" || command == "TORRENT-START" ||
			command == "TORRENT-STOP" || command == "TORRENT-REANNOUNCE" ||
			command == "TORRENT-REMOVE" || command == "TORRENT-START-ALL" ||
			command == "TORRENT-STOP-ALL" || command == "TORRENT-REANNOUNCE-ALL" ||
			command == "TORRENT-ADD-MAGNET" || command == "TORRENT-ADD-FILE" ||
			command == "TORRENT-SESSION" || command == "TORRENT-DHT")
		{
			if (rest.empty ())
				return std::unexpected (ProtocolError{ErrorCode::BadArgument, "missing tunnel index"});
			auto index = detail::ParseIndex (detail::NextToken (rest));
			if (!index) return std::unexpected (index.error ());
			request.tunnelIndex = *index;

			if (command == "TORRENT-DETAIL")
				request.type = RequestType::TorrentDetail;
			else if (command == "TORRENT-START")
				request.type = RequestType::TorrentStart;
			else if (command == "TORRENT-STOP")
				request.type = RequestType::TorrentStop;
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
			else if (command == "TORRENT-ADD-MAGNET")
				request.type = RequestType::TorrentAddMagnet;
			else if (command == "TORRENT-ADD-FILE")
				request.type = RequestType::TorrentAddFile;
			else if (command == "TORRENT-SESSION")
				request.type = RequestType::TorrentSession;
			else
				request.type = RequestType::TorrentDht;

			if (request.type == RequestType::TorrentAddMagnet || request.type == RequestType::TorrentAddFile)
			{
				auto argument = detail::ParseRestOfLine (rest);
				if (!argument) return std::unexpected (argument.error ());
				request.argument = std::move (*argument);
				rest = {};
			}
			else if (request.type == RequestType::TorrentDetail || request.type == RequestType::TorrentStart ||
				request.type == RequestType::TorrentStop || request.type == RequestType::TorrentReannounce ||
				request.type == RequestType::TorrentRemove)
			{
				if (rest.empty ())
					return std::unexpected (ProtocolError{ErrorCode::BadArgument, "missing torrent id"});
				auto id = detail::ParseTorrentId (detail::NextToken (rest));
				if (!id) return std::unexpected (id.error ());
				request.torrentId = *id;

				if (request.type == RequestType::TorrentRemove && !rest.empty ())
				{
					auto option = detail::NextToken (rest);
					if (option != "files")
						return std::unexpected (ProtocolError{ErrorCode::BadArgument, "unknown remove option"});
					request.deleteFiles = true;
				}
			}
		}
		else
		{
			return std::unexpected (ProtocolError{ErrorCode::UnknownCommand, "unknown command"});
		}

		if (auto trailing = detail::RejectTrailing (rest))
			return std::unexpected (*trailing);
		return request;
	}

	inline std::expected<ResponseHeader, ProtocolError> ParseResponseHeader (std::string_view line)
	{
		if (line.size () > 128)
			return std::unexpected (ProtocolError{ErrorCode::MalformedRequest, "response header too long"});
		const auto separator = line.find ('/');
		if (separator == std::string_view::npos || line.substr (0, separator) != PROTOCOL_NAME)
			return std::unexpected (ProtocolError{ErrorCode::MalformedRequest, "bad response protocol"});

		std::string_view rest = line.substr (separator + 1);
		auto version = detail::ParseVersion (rest);
		if (!version) return std::unexpected (version.error ());

		const auto status = detail::NextToken (rest);
		ResponseHeader header{};
		if (status == "OK")
		{
			auto length = detail::ParseUnsigned (detail::NextToken (rest));
			if (!length || *length > MAX_RESPONSE_PAYLOAD)
				return std::unexpected (ProtocolError{ErrorCode::ResponseTooLarge, "invalid payload length"});
			header.status = ResponseStatus::Ok;
			header.payloadSize = static_cast<uint32_t> (*length);
			header.errorCode = ErrorCode::None;
		}
		else if (status == "ERR")
		{
			auto code = detail::ParseUnsigned (detail::NextToken (rest));
			auto length = detail::ParseUnsigned (detail::NextToken (rest));
			if (!code || *code > static_cast<uint64_t> (ErrorCode::Internal) || !length || *length > MAX_RESPONSE_PAYLOAD)
				return std::unexpected (ProtocolError{ErrorCode::MalformedRequest, "invalid error response"});
			header.status = ResponseStatus::Error;
			header.errorCode = static_cast<ErrorCode> (*code);
			header.payloadSize = static_cast<uint32_t> (*length);
		}
		else
		{
			return std::unexpected (ProtocolError{ErrorCode::MalformedRequest, "unknown response status"});
		}

		while (!rest.empty () && rest.front () == ' ') rest.remove_prefix (1);
		if (!rest.empty ())
			return std::unexpected (ProtocolError{ErrorCode::MalformedRequest, "trailing response data"});
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

	inline void AppendField (std::string& payload, std::string_view name, std::string_view value)
	{
		if (name.empty () || name.size () > MAX_FIELD_NAME) return;
		payload.append (name);
		payload.push_back ('=');
		payload.append (EscapeValue (value));
		payload.push_back ('\n');
	}

	template<typename T>
		requires std::integral<T> || std::same_as<T, bool>
	inline void AppendField (std::string& payload, std::string_view name, T value)
	{
		if constexpr (std::same_as<T, bool>)
			AppendField (payload, name, value ? "1" : "0");
		else
			AppendField (payload, name, std::to_string (value));
	}

	inline std::string MakeResponse (ResponseStatus status, std::string_view payload,
		ErrorCode errorCode = ErrorCode::None)
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
