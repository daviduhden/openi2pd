/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test privsep protocol.
 *
 * Checks privilege-separation framing and field bounds: magic/version/type/length validation, exact maximum sizes, log-record string limits and SOCK_SEQPACKET channel behavior with peer credentials.
 *
 */

#include <cassert>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <unistd.h>

#include "privsep/Channel.h"
#include "privsep/Protocol.h"

using namespace i2p::privsep;

namespace
{
std::string LogConfig (bool useSyslog, std::string_view path, std::string_view tag)
{
	auto encoded = EncodeLogConfig (useSyslog, path, tag);
	assert (encoded.has_value ());
	return *encoded;
}
std::string Header (uint32_t magic, uint16_t version, uint16_t type, uint32_t requestId, uint32_t length)
{
	PayloadWriter writer;
	writer.WriteU32 (magic);
	writer.WriteU16 (version);
	writer.WriteU16 (type);
	writer.WriteU32 (requestId);
	writer.WriteU32 (length);
	return writer.Data ();
}

void ExpectDecodeError (const std::string & frame, ErrorCode expected)
{
	auto decoded = Decode (frame);
	assert (!decoded.has_value ());
	assert (decoded.error ().code == expected);
}
}

int main ()
{
	// Round trip preserves type, request id and the bounded log fields.
	Message message { MessageType::LogRecord, 42, EncodeLogRecord (4, 123456789, "hello\nworld") };
	auto frame = Encode (message);
	assert (frame.has_value ());
	assert (frame->size () <= MAX_MESSAGE_SIZE);
	auto decoded = Decode (*frame);
	assert (decoded.has_value ());
	assert (decoded->type == MessageType::LogRecord);
	assert (decoded->requestId == 42);
	auto record = DecodeLogRecord (decoded->payload);
	assert (record.has_value ());
	assert (std::get<0> (*record) == 4);
	assert (std::get<1> (*record) == 123456789);
	assert (std::get<2> (*record) == "hello\nworld");

	// Header validation: magic, version, type and length are checked before
	// the payload is dispatched.
	ExpectDecodeError (Header (0, PROTOCOL_VERSION, 2, 0, 0), ErrorCode::Malformed);
	ExpectDecodeError (Header (PROTOCOL_MAGIC, PROTOCOL_VERSION + 1, 2, 0, 0), ErrorCode::UnsupportedVersion);
	ExpectDecodeError (Header (PROTOCOL_MAGIC, MIN_PROTOCOL_VERSION - 1, 2, 0, 0), ErrorCode::UnsupportedVersion);
	// Version 1 frames remain readable; new senders emit version 2.
	assert (Decode (Header (PROTOCOL_MAGIC, MIN_PROTOCOL_VERSION, 2, 0, 0)).has_value ());
	ExpectDecodeError (Header (PROTOCOL_MAGIC, PROTOCOL_VERSION, 0, 0, 0), ErrorCode::UnknownType);
	// Error is the last known type; one past it and 0xffff are unknown.
	assert (Decode (Header (PROTOCOL_MAGIC, PROTOCOL_VERSION, 15, 0, 0)).has_value ());
	assert (Decode (Header (PROTOCOL_MAGIC, PROTOCOL_VERSION, 16, 0, 0)).has_value ());
	ExpectDecodeError (Header (PROTOCOL_MAGIC, PROTOCOL_VERSION, 17, 0, 0), ErrorCode::UnknownType);
	ExpectDecodeError (Header (PROTOCOL_MAGIC, PROTOCOL_VERSION, 0xffff, 0, 0), ErrorCode::UnknownType);

	// Configuration and supervisor types are accepted by framing; role-specific
	// payload and descriptor authorization belongs to dispatch. Empty payload is legal for
	// control messages, so the boundary is accepted before dispatch.
	{
		auto fdConfig
			= Encode (Message { MessageType::ConfigureLogFd, 8, LogConfig (false, "/tmp/router.log", "test") });
		assert (fdConfig.has_value ());
		auto decodedConfig = Decode (*fdConfig);
		assert (decodedConfig.has_value ());
		assert (decodedConfig->type == MessageType::ConfigureLogFd);
		assert (decodedConfig->requestId == 8);
		assert (Decode (Header (PROTOCOL_MAGIC, PROTOCOL_VERSION, 6, 0, 0)).has_value ());
		for (uint16_t type = 7; type <= MAX_MESSAGE_TYPE; ++type)
			assert (Decode (Header (PROTOCOL_MAGIC, PROTOCOL_VERSION, type, 1, 0)).has_value ());
		assert (Decode (Header (PROTOCOL_MAGIC, PROTOCOL_VERSION, 3, 0, 0)).has_value ());
	}

	// Every truncated header prefix is malformed, never a partially parsed
	// message with default fields.
	{
		const auto complete = Header (PROTOCOL_MAGIC, PROTOCOL_VERSION, 2, 5, 0);
		for (size_t length = 0; length < HEADER_SIZE; ++length)
		{
			auto truncated = Decode (std::string_view (complete).substr (0, length));
			assert (!truncated.has_value ());
			assert (truncated.error ().code == ErrorCode::Malformed);
		}
	}

	// A declared payload above the frame limit is a size error; a mismatch
	// between the declared and actual length is a framing error.
	ExpectDecodeError (
		Header (PROTOCOL_MAGIC, PROTOCOL_VERSION, 2, 0, static_cast<uint32_t> (MAX_MESSAGE_SIZE)), ErrorCode::TooLarge);
	// Off-by-one on the documented limit: the largest legal declared length
	// is MAX_MESSAGE_SIZE - HEADER_SIZE; one more byte is rejected as a size
	// error even though the frame is short.
	ExpectDecodeError (
		Header (PROTOCOL_MAGIC, PROTOCOL_VERSION, 2, 0, static_cast<uint32_t> (MAX_MESSAGE_SIZE - HEADER_SIZE + 1)),
		ErrorCode::TooLarge);
	ExpectDecodeError (
		Header (PROTOCOL_MAGIC, PROTOCOL_VERSION, 2, 0, std::numeric_limits<uint32_t>::max ()), ErrorCode::TooLarge);
	// A header-only record that declares the maximum payload is a framing
	// error, not a size error: the length is legal but absent.
	ExpectDecodeError (
		Header (PROTOCOL_MAGIC, PROTOCOL_VERSION, 2, 0, static_cast<uint32_t> (MAX_MESSAGE_SIZE - HEADER_SIZE)),
		ErrorCode::Malformed);
	ExpectDecodeError (Header (PROTOCOL_MAGIC, PROTOCOL_VERSION, 2, 0, 1), ErrorCode::Malformed);
	{
		// Declared zero payload with trailing bytes.
		auto zeroLength = Header (PROTOCOL_MAGIC, PROTOCOL_VERSION, 2, 0, 0);
		zeroLength.push_back ('x');
		ExpectDecodeError (zeroLength, ErrorCode::Malformed);
	}
	auto trailing = *frame;
	trailing.push_back ('x');
	ExpectDecodeError (trailing, ErrorCode::Malformed);
	assert (!Decode ("short").has_value ());
	assert (!Decode ("").has_value ());
	assert (!Decode (std::string (MAX_MESSAGE_SIZE + HEADER_SIZE, 'x')).has_value ());

	// A frame exactly at the documented limit is accepted.
	Message maximum { MessageType::LogRecord, 7, std::string (MAX_MESSAGE_SIZE - HEADER_SIZE, 'z') };
	auto maximumFrame = Encode (maximum);
	assert (maximumFrame.has_value ());
	assert (maximumFrame->size () == MAX_MESSAGE_SIZE);
	auto maximumDecoded = Decode (*maximumFrame);
	assert (maximumDecoded.has_value ());
	assert (maximumDecoded->payload.size () == MAX_MESSAGE_SIZE - HEADER_SIZE);

	// Encode refuses a payload that cannot fit the framed size.
	Message tooLarge { MessageType::LogRecord, 0, std::string (MAX_MESSAGE_SIZE, 'x') };
	auto tooLargeFrame = Encode (tooLarge);
	assert (!tooLargeFrame.has_value ());
	assert (tooLargeFrame.error ().code == ErrorCode::TooLarge);

	// Log-record field bounds: truncation on encode and rejection on decode.
	std::string oversized (MAX_TEXT_SIZE + 100, 'a');
	auto oversizedPayload = EncodeLogRecord (5, 1, oversized);
	auto oversizedRecord = DecodeLogRecord (oversizedPayload);
	assert (oversizedRecord.has_value ());
	assert (std::get<2> (*oversizedRecord).size () == MAX_TEXT_SIZE);

	assert (!DecodeLogRecord ("").has_value ()); // missing level
	{
		PayloadWriter writer;
		writer.WriteU8 (4);
		assert (!DecodeLogRecord (writer.Data ()).has_value ()); // missing timestamp
	}
	{
		PayloadWriter writer;
		writer.WriteU8 (4);
		writer.WriteU64BE (1);
		assert (!DecodeLogRecord (writer.Data ()).has_value ()); // missing length
	}
	{
		// String length larger than the remaining buffer.
		PayloadWriter writer;
		writer.WriteU8 (4);
		writer.WriteU64BE (1);
		writer.WriteU32 (5);
		writer.WriteU8 ('x');
		assert (!DecodeLogRecord (writer.Data ()).has_value ());
	}
	{
		// String length above MAX_TEXT_SIZE is rejected before allocation.
		PayloadWriter writer;
		writer.WriteU8 (4);
		writer.WriteU64BE (1);
		writer.WriteU32 (MAX_TEXT_SIZE + 1);
		auto malformed = DecodeLogRecord (writer.Data ());
		assert (!malformed.has_value ());
		assert (malformed.error ().code == ErrorCode::BadField);
	}
	{
		// Trailing bytes after a complete record are rejected.
		PayloadWriter writer;
		writer.WriteU8 (4);
		writer.WriteU64BE (1);
		writer.WriteString ("ok");
		writer.WriteU8 (0);
		auto malformed = DecodeLogRecord (writer.Data ());
		assert (!malformed.has_value ());
		assert (malformed.error ().code == ErrorCode::BadField);
	}

	// Log configuration validation.
	auto goodConfig = LogConfig (false, "/tmp/router.log", "open-i2pd");
	auto parsedConfig = DecodeLogConfig (goodConfig);
	assert (parsedConfig.has_value ());
	assert (!std::get<0> (*parsedConfig));
	assert (std::get<1> (*parsedConfig) == "/tmp/router.log");
	{
		PayloadWriter writer;
		writer.WriteU8 (2); // invalid bool encoding
		writer.WriteString ("");
		writer.WriteString ("tag");
		auto invalid = DecodeLogConfig (writer.Data ());
		assert (!invalid.has_value ());
		assert (invalid.error ().code == ErrorCode::BadField);
	}
	{
		PayloadWriter writer;
		writer.WriteU8 (1);
		writer.WriteString ("/tmp/router.log");
		writer.WriteString ("tag");
		writer.WriteU8 (0); // trailing byte
		auto invalid = DecodeLogConfig (writer.Data ());
		assert (!invalid.has_value ());
		assert (invalid.error ().code == ErrorCode::BadField);
	}
	{
		// Embedded NUL would truncate the path in the worker's C APIs.
		PayloadWriter writer;
		writer.WriteU8 (0);
		writer.WriteString (std::string ("/tmp/bad\0name", 13));
		writer.WriteString ("tag");
		auto invalid = DecodeLogConfig (writer.Data ());
		assert (!invalid.has_value ());
		assert (invalid.error ().code == ErrorCode::BadField);
	}
	{
		PayloadWriter writer;
		writer.WriteU8 (1);
		writer.WriteString ("");
		writer.WriteString (std::string ("ta\0g", 4));
		auto invalid = DecodeLogConfig (writer.Data ());
		assert (!invalid.has_value ());
		assert (invalid.error ().code == ErrorCode::BadField);
	}

	// Encoders reject an over-long path instead of truncating it to a
	// different filesystem object. The maximum is inclusive.
	assert (EncodeLogConfig (false, std::string (MAX_PATH_SIZE + 1, 'a'), "tag").error ().code == ErrorCode::BadField);
	assert (EncodeLogConfig (false, std::string ("/tmp/\0hidden", 12), "tag").error ().code == ErrorCode::BadField);
	assert (EncodeLogConfig (false, std::string (MAX_PATH_SIZE, 'a'), std::string (MAX_TEXT_SIZE, 't')).has_value ());

	// Typed Error replies round-trip and reject unknown/oversized fields.
	{
		const std::string payload = EncodeError (MessageType::TorrentStorageRequest, ErrorCode::Busy, "capacity");
		auto decodedError = DecodeError (payload);
		assert (decodedError.has_value ());
		assert (std::get<0> (*decodedError) == MessageType::TorrentStorageRequest);
		assert (std::get<1> (*decodedError) == ErrorCode::Busy);
		assert (std::get<2> (*decodedError) == "capacity");
		PayloadWriter badType;
		badType.WriteU16 (0xffff);
		badType.WriteU8 (static_cast<uint8_t> (ErrorCode::BadField));
		badType.WriteString ("x");
		assert (!DecodeError (badType.Data ()).has_value ());
		PayloadWriter badCode;
		badCode.WriteU16 (static_cast<uint16_t> (MessageType::Ping));
		badCode.WriteU8 (0xff);
		badCode.WriteString ("x");
		assert (!DecodeError (badCode.Data ()).has_value ());
	}

	// The operation matrix is shared by every transport and endpoint.
	assert (AllowsOperation (EndpointRole::Log, MessageType::LogRecord));
	assert (AllowsOperation (EndpointRole::Supervisor, MessageType::ConfigureLog));
	assert (AllowsOperation (EndpointRole::TorrentStorage, MessageType::TorrentStorageRequest));
	assert (!AllowsOperation (EndpointRole::TorrentStorage, MessageType::ConfigureRouterStorage));
	assert (!AllowsOperation (EndpointRole::RouterStorage, MessageType::LogRecord));
	assert (!AllowsOperation (EndpointRole::StorageBroker, MessageType::LogRecord));

	// Channel behavior: SOCK_SEQPACKET only, peer credentials, timeout versus
	// peer close, and use after close.
	auto pair = Channel::CreatePair ();
	if (!pair)
	{
		// A platform without SOCK_SEQPACKET must not silently use a weaker
		// transport; the test fails rather than accepting a fallback.
		return 2;
	}
	auto uid = pair->first.PeerUid ();
	assert (uid.has_value ());
	assert (*uid == geteuid ());

	auto timeout = pair->second.Receive (50);
	assert (timeout.has_value ());
	assert (!timeout->has_value ());

	auto sent = pair->first.Send (message);
	assert (sent.has_value ());
	auto received = pair->second.Receive (1000);
	assert (received.has_value ());
	assert (received->has_value ());
	assert ((*received)->type == MessageType::LogRecord);
	assert ((*received)->requestId == 42);

	// Closing the peer is reported as an error, never as a timeout: a worker
	// that treats EOF as a timeout would spin forever.
	pair->first.Close ();
	auto closed = pair->second.Receive (1000);
	assert (!closed.has_value ());

	auto sendClosed = pair->first.Send (message);
	assert (!sendClosed.has_value ());
	assert (!pair->first.PeerUid ().has_value ());

	return 0;
}
