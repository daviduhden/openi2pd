/*
* Copyright (c) 2026, The PurpleI2P Project
*
* This file is part of Open I2Pd and licensed under BSD3
*/

#ifndef I2PD_PRIVSEP_PROTOCOL_H__
#define I2PD_PRIVSEP_PROTOCOL_H__

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace i2p::privsep
{
	inline constexpr uint32_t PROTOCOL_MAGIC = 0x49503250; // "IP2P"
	inline constexpr uint16_t PROTOCOL_VERSION = 1;
	inline constexpr size_t HEADER_SIZE = 16;
	inline constexpr size_t MAX_MESSAGE_SIZE = 64 * 1024;
	inline constexpr size_t MAX_TEXT_SIZE = 8 * 1024;

	enum class MessageType : uint16_t
	{
		ConfigureLog = 1,
		LogRecord = 2,
		Shutdown = 3,
		Ping = 4,
		Pong = 5
	};

	struct Message
	{
		MessageType type = MessageType::Ping;
		uint32_t requestId = 0;
		std::string payload;
	};

	enum class ErrorCode : uint8_t
	{
		None = 0,
		Malformed = 1,
		TooLarge = 2,
		UnsupportedVersion = 3,
		UnknownType = 4,
		BadField = 5,
		NotAuthorized = 6
	};

	struct ProtocolError
	{
		ErrorCode code;
		std::string_view text;
	};

	class PayloadWriter
	{
		public:
			void U8 (uint8_t value) { m_Data.push_back (static_cast<char> (value)); }
			void U16 (uint16_t value)
			{
				U8 (value >> 8);
				U8 (value & 0xff);
			}
			void U32 (uint32_t value)
			{
				U16 (static_cast<uint16_t> (value >> 16));
				U16 (static_cast<uint16_t> (value & 0xffff));
			}
			void U64 (uint64_t value)
			{
				U32 (static_cast<uint32_t> (value >> 32));
				U32 (static_cast<uint32_t> (value & 0xffffffff));
			}
			// Bounded by MAX_TEXT_SIZE. Log records therefore truncate rather
			// than copy unbounded text supplied by a router thread.
			void String (std::string_view value)
			{
				const auto size = std::min (value.size (), MAX_TEXT_SIZE);
				U32 (static_cast<uint32_t> (size));
				m_Data.append (value.substr (0, size));
			}
			const std::string& Data () const { return m_Data; }

		private:
			std::string m_Data;
	};

	class PayloadReader
	{
		public:
			explicit PayloadReader (std::string_view data): m_Data (data) {}

			std::expected<uint8_t, ProtocolError> U8 ()
			{
				if (m_Pos >= m_Data.size ()) return Bad ("truncated u8");
				return static_cast<uint8_t> (m_Data[m_Pos++]);
			}
			std::expected<uint16_t, ProtocolError> U16 ()
			{
				auto hi = U8 (); if (!hi) return std::unexpected (hi.error ());
				auto lo = U8 (); if (!lo) return std::unexpected (lo.error ());
				return static_cast<uint16_t> ((*hi << 8) | *lo);
			}
			std::expected<uint32_t, ProtocolError> U32 ()
			{
				auto hi = U16 (); if (!hi) return std::unexpected (hi.error ());
				auto lo = U16 (); if (!lo) return std::unexpected (lo.error ());
				return (static_cast<uint32_t> (*hi) << 16) | *lo;
			}
			std::expected<uint64_t, ProtocolError> U64 ()
			{
				auto hi = U32 (); if (!hi) return std::unexpected (hi.error ());
				auto lo = U32 (); if (!lo) return std::unexpected (lo.error ());
				return (static_cast<uint64_t> (*hi) << 32) | *lo;
			}
			std::expected<std::string_view, ProtocolError> String ()
			{
				auto size = U32 (); if (!size) return std::unexpected (size.error ());
				if (*size > MAX_TEXT_SIZE || *size > m_Data.size () - m_Pos)
					return Bad ("invalid string length");
				auto value = m_Data.substr (m_Pos, *size);
				m_Pos += *size;
				return value;
			}
			bool Done () const { return m_Pos == m_Data.size (); }

		private:
			static std::unexpected<ProtocolError> Bad (std::string_view text)
			{
				return std::unexpected (ProtocolError{ErrorCode::BadField, text});
			}
			std::string_view m_Data;
			size_t m_Pos = 0;
	};

	inline std::expected<std::string, ProtocolError> Encode (const Message& message)
	{
		if (message.payload.size () > MAX_MESSAGE_SIZE - HEADER_SIZE)
			return std::unexpected (ProtocolError{ErrorCode::TooLarge, "message too large"});

		PayloadWriter header;
		header.U32 (PROTOCOL_MAGIC);
		header.U16 (PROTOCOL_VERSION);
		header.U16 (static_cast<uint16_t> (message.type));
		header.U32 (message.requestId);
		header.U32 (static_cast<uint32_t> (message.payload.size ()));
		std::string out = header.Data ();
		out.append (message.payload);
		return out;
	}

	inline std::expected<Message, ProtocolError> Decode (std::string_view frame)
	{
		if (frame.size () < HEADER_SIZE)
			return std::unexpected (ProtocolError{ErrorCode::Malformed, "short frame"});
		PayloadReader reader (frame.substr (0, HEADER_SIZE));
		auto magic = reader.U32 (); if (!magic) return std::unexpected (magic.error ());
		auto version = reader.U16 (); if (!version) return std::unexpected (version.error ());
		auto type = reader.U16 (); if (!type) return std::unexpected (type.error ());
		auto request = reader.U32 (); if (!request) return std::unexpected (request.error ());
		auto length = reader.U32 (); if (!length) return std::unexpected (length.error ());
		if (*magic != PROTOCOL_MAGIC)
			return std::unexpected (ProtocolError{ErrorCode::Malformed, "bad magic"});
		if (*version != PROTOCOL_VERSION)
			return std::unexpected (ProtocolError{ErrorCode::UnsupportedVersion, "bad version"});
		if (*type < static_cast<uint16_t> (MessageType::ConfigureLog) ||
			*type > static_cast<uint16_t> (MessageType::Pong))
			return std::unexpected (ProtocolError{ErrorCode::UnknownType, "unknown type"});
		if (*length > MAX_MESSAGE_SIZE - HEADER_SIZE)
			return std::unexpected (ProtocolError{ErrorCode::TooLarge, "payload too large"});
		// One SOCK_SEQPACKET record is one frame. A short/long frame is a
		// framing error, not a size limit violation.
		if (*length != frame.size () - HEADER_SIZE)
			return std::unexpected (ProtocolError{ErrorCode::Malformed, "length mismatch"});
		return Message{static_cast<MessageType> (*type), *request,
			std::string (frame.substr (HEADER_SIZE, *length))};
	}

	inline std::string EncodeLogRecord (uint8_t level, int64_t timestamp, std::string_view text)
	{
		PayloadWriter writer;
		writer.U8 (level);
		writer.U64 (static_cast<uint64_t> (timestamp));
		writer.String (text);
		return writer.Data ();
	}

	inline std::expected<std::tuple<uint8_t, int64_t, std::string>, ProtocolError>
	DecodeLogRecord (std::string_view payload)
	{
		PayloadReader reader (payload);
		auto level = reader.U8 (); if (!level) return std::unexpected (level.error ());
		auto timestamp = reader.U64 (); if (!timestamp) return std::unexpected (timestamp.error ());
		auto text = reader.String (); if (!text) return std::unexpected (text.error ());
		if (!reader.Done ()) return std::unexpected (ProtocolError{ErrorCode::BadField, "trailing log data"});
		return std::tuple{*level, static_cast<int64_t> (*timestamp), std::string (*text)};
	}

	inline std::string EncodeLogConfig (bool useSyslog, std::string_view path, std::string_view tag)
	{
		PayloadWriter writer;
		writer.U8 (useSyslog ? 1 : 0);
		writer.String (path);
		writer.String (tag);
		return writer.Data ();
	}

	inline std::expected<std::tuple<bool, std::string, std::string>, ProtocolError>
	DecodeLogConfig (std::string_view payload)
	{
		PayloadReader reader (payload);
		auto syslog = reader.U8 (); if (!syslog) return std::unexpected (syslog.error ());
		auto path = reader.String (); if (!path) return std::unexpected (path.error ());
		auto tag = reader.String (); if (!tag) return std::unexpected (tag.error ());
		if (!reader.Done () || *syslog > 1)
			return std::unexpected (ProtocolError{ErrorCode::BadField, "invalid log config"});
		// These become null-terminated path/ident arguments. An embedded NUL
		// would make the worker act on a different path than the parent meant.
		if (path->find ('\0') != std::string_view::npos ||
			tag->find ('\0') != std::string_view::npos)
			return std::unexpected (ProtocolError{ErrorCode::BadField, "NUL in log config"});
		return std::tuple{*syslog != 0, std::string (*path), std::string (*tag)};
	}
}

#endif
