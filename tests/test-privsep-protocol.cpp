#include <cassert>
#include <string>
#include <string_view>

#include "privsep/Channel.h"
#include "privsep/Protocol.h"

using namespace i2p::privsep;

int main ()
{
	Message message{MessageType::LogRecord, 42, EncodeLogRecord (4, 123456789, "hello\nworld")};
	auto frame = Encode (message);
	assert (frame.has_value ());
	auto decoded = Decode (*frame);
	assert (decoded.has_value ());
	assert (decoded->type == MessageType::LogRecord);
	assert (decoded->requestId == 42);
	auto record = DecodeLogRecord (decoded->payload);
	assert (record.has_value ());
	assert (std::get<0> (*record) == 4);
	assert (std::get<1> (*record) == 123456789);
	assert (std::get<2> (*record) == "hello\nworld");

	// Corrupt length, magic and version are rejected before dispatch.
	auto badLength = *frame;
	badLength[12] = 0x7f;
	assert (!Decode (badLength).has_value ());
	auto badMagic = *frame;
	badMagic[0] = 0;
	assert (!Decode (badMagic).has_value ());
	auto badVersion = *frame;
	badVersion[5] = 0;
	assert (!Decode (badVersion).has_value ());
	assert (!Decode ("short").has_value ());
	assert (!Decode (std::string (MAX_MESSAGE_SIZE + HEADER_SIZE, 'x')).has_value ());

	// Bounds on string lengths prevent allocation from an untrusted field.
	PayloadWriter writer;
	writer.U32 (MAX_TEXT_SIZE + 1);
	auto malformed = DecodeLogRecord (writer.Data ());
	assert (!malformed.has_value ());

	// Channel::CreatePair uses AF_UNIX/SOCK_SEQPACKET and preserves messages.
	auto pair = Channel::CreatePair ();
	if (!pair)
	{
		// A platform without SOCK_SEQPACKET must not silently use a weaker
		// transport; the test fails rather than accepting a fallback.
		return 2;
	}
	auto sent = pair->first.Send (message);
	assert (sent.has_value ());
	auto received = pair->second.Receive (1000);
	assert (received.has_value ());
	assert (received->has_value ());
	assert ((*received)->type == MessageType::LogRecord);
	assert ((*received)->requestId == 42);

	return 0;
}
