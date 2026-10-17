#include <cassert>
#include <string>
#include <string_view>

#include "TuiProtocol.h"

using namespace i2p::tui;

static void TestRequests ()
{
	auto state = ParseRequest ("I2PDTUI/1 STATE");
	assert (state.has_value ());
	assert (state->type == RequestType::State);

	auto ping = ParseRequest ("I2PDTUI/1 PING");
	assert (ping.has_value ());
	assert (ping->type == RequestType::Ping);

	auto logs = ParseRequest ("I2PDTUI/1 LOGS 42 10");
	assert (logs.has_value ());
	assert (logs->type == RequestType::Logs);
	assert (logs->after == 42);
	assert (logs->limit == 10);

	auto peers = ParseRequest ("I2PDTUI/1 PEERS");
	assert (peers.has_value ());
	assert (peers->limit == MAX_PEER_RECORDS_PER_REQUEST);

	// Bounds, versions and surface syntax are validated before dispatch.
	assert (!ParseRequest ("I2PDTUI/2 STATE").has_value ());
	assert (!ParseRequest ("I2PDTUI/1").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 UNKNOWN").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 LOGS x").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 LOGS 0 999999").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 PEERS 0").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 STATE extra").has_value ());
	// Repeated separators are tolerated, leading/trailing whitespace is not.
	assert (ParseRequest ("I2PDTUI/1  STATE").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 STATE ").has_value ());
	assert (!ParseRequest (std::string (MAX_REQUEST_LINE + 1, 'A')).has_value ());
}

static void TestResponseHeader ()
{
	auto ok = ParseResponseHeader ("I2PDTUI/1 OK 3");
	assert (ok.has_value ());
	assert (ok->status == ResponseStatus::Ok);
	assert (ok->payloadSize == 3);

	auto error = ParseResponseHeader ("I2PDTUI/1 ERR 3 7");
	assert (error.has_value ());
	assert (error->status == ResponseStatus::Error);
	assert (error->errorCode == ErrorCode::UnknownCommand);
	assert (error->payloadSize == 7);

	assert (!ParseResponseHeader ("I2PDTUI/1 OK 999999999").has_value ());
	assert (!ParseResponseHeader ("I2PDTUI/1 ERR 999 1").has_value ());
	assert (!ParseResponseHeader ("I2PDTUI/1 OK -1").has_value ());
	assert (!ParseResponseHeader ("I2PDTUI/1 OK 1 extra").has_value ());
}

static void TestFields ()
{
	std::string payload;
	AppendField (payload, "key", "line\nwith\\slash");
	assert (payload == "key=line\\nwith\\\\slash\n");

	auto position = payload.find ('=');
	auto decoded = UnescapeValue (std::string_view (payload).substr (position + 1,
		payload.size () - position - 2));
	assert (decoded == "line\nwith\\slash");

	// A response built by the daemon must round-trip through the parser.
	auto frame = MakeResponse (ResponseStatus::Ok, payload);
	auto newline = frame.find ('\n');
	assert (newline != std::string::npos);
	auto header = ParseResponseHeader (std::string_view (frame).substr (0, newline));
	assert (header.has_value ());
	assert (header->payloadSize == payload.size ());
	assert (frame.substr (newline + 1) == payload);
}

int main ()
{
	TestRequests ();
	TestResponseHeader ();
	TestFields ();
	return 0;
}
