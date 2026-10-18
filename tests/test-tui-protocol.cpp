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
	TestFields ();
	return 0;
}
