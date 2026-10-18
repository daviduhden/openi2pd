/*
 * Copyright (c) 2013-2026, The PurpleI2P Project
 * Copyright (c) 2026, David Uhden Collado
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <cassert>
#include <cstdint>
#include <string>
#include <string_view>
#include <unistd.h>

#include "privsep/Channel.h"
#include "privsep/Protocol.h"

using namespace i2p::privsep;

namespace
{
std::string Header (uint32_t magic, uint16_t version, uint16_t type, uint32_t requestId, uint32_t length)
{
	PayloadWriter writer;
	writer.U32 (magic);
	writer.U16 (version);
	writer.U16 (type);
	writer.U32 (requestId);
	writer.U32 (length);
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
	ExpectDecodeError (Header (PROTOCOL_MAGIC, PROTOCOL_VERSION, 0, 0, 0), ErrorCode::UnknownType);
	ExpectDecodeError (Header (PROTOCOL_MAGIC, PROTOCOL_VERSION, 6, 0, 0), ErrorCode::UnknownType);
	ExpectDecodeError (Header (PROTOCOL_MAGIC, PROTOCOL_VERSION, 0xffff, 0, 0), ErrorCode::UnknownType);

	// A declared payload above the frame limit is a size error; a mismatch
	// between the declared and actual length is a framing error.
	ExpectDecodeError (
		Header (PROTOCOL_MAGIC, PROTOCOL_VERSION, 2, 0, static_cast<uint32_t> (MAX_MESSAGE_SIZE)), ErrorCode::TooLarge);
	ExpectDecodeError (Header (PROTOCOL_MAGIC, PROTOCOL_VERSION, 2, 0, 1), ErrorCode::Malformed);
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
		writer.U8 (4);
		assert (!DecodeLogRecord (writer.Data ()).has_value ()); // missing timestamp
	}
	{
		PayloadWriter writer;
		writer.U8 (4);
		writer.U64 (1);
		assert (!DecodeLogRecord (writer.Data ()).has_value ()); // missing length
	}
	{
		// String length larger than the remaining buffer.
		PayloadWriter writer;
		writer.U8 (4);
		writer.U64 (1);
		writer.U32 (5);
		writer.U8 ('x');
		assert (!DecodeLogRecord (writer.Data ()).has_value ());
	}
	{
		// String length above MAX_TEXT_SIZE is rejected before allocation.
		PayloadWriter writer;
		writer.U8 (4);
		writer.U64 (1);
		writer.U32 (MAX_TEXT_SIZE + 1);
		auto malformed = DecodeLogRecord (writer.Data ());
		assert (!malformed.has_value ());
		assert (malformed.error ().code == ErrorCode::BadField);
	}
	{
		// Trailing bytes after a complete record are rejected.
		PayloadWriter writer;
		writer.U8 (4);
		writer.U64 (1);
		writer.String ("ok");
		writer.U8 (0);
		auto malformed = DecodeLogRecord (writer.Data ());
		assert (!malformed.has_value ());
		assert (malformed.error ().code == ErrorCode::BadField);
	}

	// Log configuration validation.
	auto goodConfig = EncodeLogConfig (false, "/tmp/router.log", "open-i2pd");
	auto parsedConfig = DecodeLogConfig (goodConfig);
	assert (parsedConfig.has_value ());
	assert (!std::get<0> (*parsedConfig));
	assert (std::get<1> (*parsedConfig) == "/tmp/router.log");
	{
		PayloadWriter writer;
		writer.U8 (2); // invalid bool encoding
		writer.String ("");
		writer.String ("tag");
		auto invalid = DecodeLogConfig (writer.Data ());
		assert (!invalid.has_value ());
		assert (invalid.error ().code == ErrorCode::BadField);
	}
	{
		PayloadWriter writer;
		writer.U8 (1);
		writer.String ("/tmp/router.log");
		writer.String ("tag");
		writer.U8 (0); // trailing byte
		auto invalid = DecodeLogConfig (writer.Data ());
		assert (!invalid.has_value ());
		assert (invalid.error ().code == ErrorCode::BadField);
	}
	{
		// Embedded NUL would truncate the path in the worker's C APIs.
		PayloadWriter writer;
		writer.U8 (0);
		writer.String (std::string ("/tmp/bad\0name", 13));
		writer.String ("tag");
		auto invalid = DecodeLogConfig (writer.Data ());
		assert (!invalid.has_value ());
		assert (invalid.error ().code == ErrorCode::BadField);
	}
	{
		PayloadWriter writer;
		writer.U8 (1);
		writer.String ("");
		writer.String (std::string ("ta\0g", 4));
		auto invalid = DecodeLogConfig (writer.Data ());
		assert (!invalid.has_value ());
		assert (invalid.error ().code == ErrorCode::BadField);
	}

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
