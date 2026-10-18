/*
 * Copyright (c) 2013-2026, The PurpleI2P Project
 * Copyright (c) 2026, David Uhden Collado
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <cassert>
#include <cstdint>
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

	// Every declared error code is accepted; anything above Internal is not.
	for (uint32_t code = 0; code <= static_cast<uint32_t> (ErrorCode::Internal); ++code)
	{
		auto header = ParseResponseHeader ("I2PDTUI/1 ERR " + std::to_string (code) + " 1");
		assert (header.has_value ());
		assert (header->errorCode == static_cast<ErrorCode> (code));
	}
	assert (!ParseResponseHeader ("I2PDTUI/1 ERR 7 1").has_value ());

	assert (!ParseResponseHeader ("I2PDTUI/1 OK 999999999").has_value ());
	assert (!ParseResponseHeader ("I2PDTUI/1 ERR 999 1").has_value ());
	assert (!ParseResponseHeader ("I2PDTUI/1 OK -1").has_value ());
	assert (!ParseResponseHeader ("I2PDTUI/1 OK 1 extra").has_value ());

	// The payload limit is inclusive; one byte above it is rejected before
	// any allocation takes place.
	auto maximum = "I2PDTUI/1 OK " + std::to_string (MAX_RESPONSE_PAYLOAD);
	auto maximumHeader = ParseResponseHeader (maximum);
	assert (maximumHeader.has_value ());
	assert (maximumHeader->payloadSize == MAX_RESPONSE_PAYLOAD);
	auto tooLarge = "I2PDTUI/1 OK " + std::to_string (MAX_RESPONSE_PAYLOAD + 1);
	assert (!ParseResponseHeader (tooLarge).has_value ());

	assert (!ParseResponseHeader (std::string (129, 'A')).has_value ());
}

static void TestFraming ()
{
	std::string payload;
	AppendField (payload, "count", 42);
	AppendField (payload, "enabled", true);
	AppendField (payload, "disabled", false);
	assert (payload == "count=42\nenabled=1\ndisabled=0\n");

	// Field names are silently ignored when empty or over the limit; a noisy
	// snapshot producer must not be able to grow the frame through them.
	std::string names;
	std::string validName (MAX_FIELD_NAME, 'n');
	AppendField (names, validName, "ok");
	assert (!names.empty ());
	names.clear ();
	AppendField (names, "", "ignored");
	AppendField (names, std::string (MAX_FIELD_NAME + 1, 'n'), "ignored");
	assert (names.empty ());

	auto frame = MakeErrorResponse (ErrorCode::ResponseTooLarge, "too big");
	auto newline = frame.find ('\n');
	assert (newline != std::string::npos);
	auto header = ParseResponseHeader (std::string_view (frame).substr (0, newline));
	assert (header.has_value ());
	assert (header->status == ResponseStatus::Error);
	assert (header->errorCode == ErrorCode::ResponseTooLarge);
	assert (frame.substr (newline + 1) == "too big");

	// Exactly the request-line limit is accepted; one byte more is not.
	std::string atLimit = "I2PDTUI/1 PEERS";
	atLimit.append (MAX_REQUEST_LINE - atLimit.size () - 1, ' ');
	atLimit.push_back ('1');
	assert (atLimit.size () == MAX_REQUEST_LINE);
	assert (ParseRequest (atLimit).has_value ());
	atLimit.push_back ('1');
	assert (atLimit.size () == MAX_REQUEST_LINE + 1);
	assert (!ParseRequest (atLimit).has_value ());

	// Escaping is symmetric and preserves unknown/truncated escapes verbatim.
	assert (EscapeValue ("a\\b\nc\rd") == "a\\\\b\\nc\\rd");
	assert (UnescapeValue ("a\\\\b\\nc\\rd") == "a\\b\nc\rd");
	assert (UnescapeValue ("tail\\") == "tail\\");
	assert (UnescapeValue ("\\q") == "\\q");
}

static void TestFields ()
{
	std::string payload;
	AppendField (payload, "key", "line\nwith\\slash");
	assert (payload == "key=line\\nwith\\\\slash\n");

	auto position = payload.find ('=');
	auto decoded = UnescapeValue (std::string_view (payload).substr (position + 1, payload.size () - position - 2));
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

static void TestTorrentRequests ()
{
	auto tunnels = ParseRequest ("I2PDTUI/1 TORRENT-TUNNELS");
	assert (tunnels.has_value () && tunnels->type == RequestType::TorrentTunnels);

	auto list = ParseRequest ("I2PDTUI/1 TORRENTS 0 200");
	assert (list.has_value () && list->type == RequestType::Torrents);
	assert (list->tunnelIndex == 0 && list->limit == 200);
	assert (!ParseRequest ("I2PDTUI/1 TORRENTS").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 TORRENTS 0 0").has_value ());

	auto detail = ParseRequest ("I2PDTUI/1 TORRENT-DETAIL 1 7");
	assert (detail.has_value () && detail->type == RequestType::TorrentDetail);
	assert (detail->tunnelIndex == 1 && detail->torrentId == 7);
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-DETAIL 1 0").has_value ());

	auto remove = ParseRequest ("I2PDTUI/1 TORRENT-REMOVE 0 3 files");
	assert (remove.has_value () && remove->type == RequestType::TorrentRemove);
	assert (remove->deleteFiles);
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-REMOVE 0 3 all").has_value ());

	auto add = ParseRequest ("I2PDTUI/1 TORRENT-ADD-MAGNET 0 magnet:?xt=urn:btih:abc");
	assert (add.has_value () && add->type == RequestType::TorrentAddMagnet);
	assert (add->argument == "magnet:?xt=urn:btih:abc");

	auto file = ParseRequest ("I2PDTUI/1 TORRENT-ADD-FILE 0 /tmp/a.torrent");
	assert (file.has_value () && file->type == RequestType::TorrentAddFile);
	assert (file->argument == "/tmp/a.torrent");

	auto tooLong = std::string ("I2PDTUI/1 TORRENT-ADD-MAGNET 0 ");
	tooLong.append (MAX_TORRENT_ARGUMENT_LENGTH + 1, 'x');
	assert (!ParseRequest (tooLong).has_value ());
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-START 0").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-START 0 1 extra").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-ADD-FILE 0 bad\nvalue").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-ADD-FILE 0").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-REMOVE 0 1 files extra").has_value ());

	auto session = ParseRequest ("I2PDTUI/1 TORRENT-SESSION 2");
	assert (session.has_value () && session->type == RequestType::TorrentSession);
	auto dht = ParseRequest ("I2PDTUI/1 TORRENT-DHT 2");
	assert (dht.has_value () && dht->type == RequestType::TorrentDht);
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-SESSION").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-DHT 2 extra").has_value ());

	auto startAll = ParseRequest ("I2PDTUI/1 TORRENT-START-ALL 3");
	assert (startAll.has_value () && startAll->type == RequestType::TorrentStartAll);
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-STOP-ALL").has_value ());
}

int main ()
{
	TestRequests ();
	TestTorrentRequests ();
	TestResponseHeader ();
	TestFraming ();
	TestFields ();
	return 0;
}
