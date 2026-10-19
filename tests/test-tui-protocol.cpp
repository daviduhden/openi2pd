/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test tui protocol.
 *
 * Checks TUI request/response parsing, field escaping, framing and payload limits, config snapshot/write restrictions and torrent command bounds.
 *
 */

#include <cassert>
#include <cstdint>
#include <string>
#include <string_view>

#include "TuiProtocol.h"

using namespace i2p::tui;

static void TestRequests ()
{
	auto create = ParseRequest ("I2PDTUI/1 TORRENT-CREATE 0 payload");
	assert (create && create->type == RequestType::TorrentCreate && create->argument == "payload");
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-CREATE 0"));

	auto tracker = ParseRequest ("I2PDTUI/1 TORRENT-TRACKER-ADD 0 12 http://tracker.i2p/a");
	assert (tracker && tracker->type == RequestType::TorrentAddTracker && tracker->torrentId == 12);
	assert (tracker->argument == "http://tracker.i2p/a");
	auto removeTracker = ParseRequest ("I2PDTUI/1 TORRENT-TRACKER-REMOVE 1 3 http://tracker.i2p/a");
	assert (removeTracker && removeTracker->type == RequestType::TorrentRemoveTracker);
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-TRACKER-ADD 0 1"));
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-TRACKER-REMOVE 0 -1 http://tracker.i2p/a"));
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-TRACKER-ADD 0 1 " + std::string (MAX_TORRENT_ARGUMENT_LENGTH + 1, 'a')));

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

	auto connections = ParseRequest ("I2PDTUI/1 CONNECTIONS 25");
	assert (connections.has_value ());
	assert (connections->type == RequestType::Connections);
	assert (connections->limit == 25);
	assert (!ParseRequest ("I2PDTUI/1 CONNECTIONS 0").has_value ());
	auto clampedConnections = ParseRequest ("I2PDTUI/1 CONNECTIONS 201");
	assert (clampedConnections.has_value ());
	assert (clampedConnections->limit == MAX_CONNECTION_RECORDS_PER_REQUEST);

	auto netdb = ParseRequest ("I2PDTUI/1 NETDB");
	assert (netdb.has_value ());
	assert (netdb->type == RequestType::NetDb);

	auto netdbRouters = ParseRequest ("I2PDTUI/1 NETDB-ROUTERS");
	assert (netdbRouters.has_value ());
	assert (netdbRouters->type == RequestType::NetDbRouters);
	assert (netdbRouters->limit == MAX_NETDB_RECORDS_PER_REQUEST);
	assert (!ParseRequest ("I2PDTUI/1 NETDB-ROUTERS 0").has_value ());
	auto netdbLeaseSets = ParseRequest ("I2PDTUI/1 NETDB-LEASESETS 10");
	assert (netdbLeaseSets.has_value ());
	assert (netdbLeaseSets->type == RequestType::NetDbLeaseSets);
	assert (netdbLeaseSets->limit == 10);

	auto transitOn = ParseRequest ("I2PDTUI/1 ADMIN TRANSIT ON");
	assert (transitOn.has_value ());
	assert (transitOn->type == RequestType::AdminTransit);
	assert (transitOn->enabled);
	auto transitOff = ParseRequest ("I2PDTUI/1 ADMIN TRANSIT OFF");
	assert (transitOff.has_value ());
	assert (transitOff->type == RequestType::AdminTransit);
	assert (!transitOff->enabled);
	auto logLevel = ParseRequest ("I2PDTUI/1 ADMIN LOGLEVEL warn");
	assert (logLevel.has_value ());
	assert (logLevel->type == RequestType::AdminLogLevel);
	assert (logLevel->argument == "warn");
	assert (!ParseRequest ("I2PDTUI/1 ADMIN").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 ADMIN TRANSIT").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 ADMIN TRANSIT MAYBE").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 ADMIN LOGLEVEL verbose").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 ADMIN LOGLEVEL INFO").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 ADMIN TRANSIT ON extra").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 ADMIN NOPE ON").has_value ());

	auto destinations = ParseRequest ("I2PDTUI/1 DESTINATIONS");
	assert (destinations.has_value ());
	assert (destinations->type == RequestType::Destinations);

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

static void TestConfigRequests ()
{
	auto config = ParseRequest ("I2PDTUI/1 CONFIG");
	assert (config.has_value ());
	assert (config->type == RequestType::Config);

	// The snapshot command takes no section selector, option name or value.
	auto extra = ParseRequest ("I2PDTUI/1 CONFIG 1");
	assert (!extra.has_value ());
	assert (extra.error ().code == ErrorCode::BadArgument);
	assert (!ParseRequest ("I2PDTUI/1 CONFIG bandwidth").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 CONFIG ").has_value ());
	assert (!ParseRequest ("I2PDTUI/2 CONFIG").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 config").has_value ());

	// Version 1 has no configuration writer. It must stay an unknown
	// command so a client cannot mistake a rejection for a partial update.
	auto write = ParseRequest ("I2PDTUI/1 CONFIG-SET bandwidth L");
	assert (!write.has_value ());
	assert (write.error ().code == ErrorCode::UnknownCommand);

	// A typed record must survive framing and escaping unchanged.
	std::string payload;
	AppendField (payload, "config.schema", 1);
	AppendField (payload, "config.readonly", true);
	AppendField (payload, "config.count", 1);
	AppendField (payload, "config.option.0.name", "share");
	AppendField (payload, "config.option.0.type", "int");
	AppendField (payload, "config.option.0.value", 100);
	auto frame = MakeResponse (ResponseStatus::Ok, payload);
	auto newline = frame.find ('\n');
	assert (newline != std::string::npos);
	auto header = ParseResponseHeader (std::string_view (frame).substr (0, newline));
	assert (header.has_value ());
	assert (header->payloadSize == payload.size ());
	assert (frame.substr (newline + 1) == payload);

	// The worst-case indexed field name must fit the wire field-name limit.
	const auto largest
		= std::string ("config.option.") + std::to_string (MAX_CONFIG_RECORDS_PER_REQUEST - 1) + ".value";
	assert (largest.size () <= MAX_FIELD_NAME);
	assert (MAX_CONFIG_VALUE_LENGTH > 0);
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

	auto pause = ParseRequest ("I2PDTUI/1 TORRENT-PAUSE 0 3");
	assert (pause.has_value () && pause->type == RequestType::TorrentPause);
	assert (pause->tunnelIndex == 0 && pause->torrentId == 3);
	auto resume = ParseRequest ("I2PDTUI/1 TORRENT-RESUME 0 3");
	assert (resume.has_value () && resume->type == RequestType::TorrentResume);
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-PAUSE 0").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-RESUME 0 3 extra").has_value ());

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
	TestConfigRequests ();
	TestTorrentRequests ();
	TestResponseHeader ();
	TestFraming ();
	TestFields ();
	return 0;
}
