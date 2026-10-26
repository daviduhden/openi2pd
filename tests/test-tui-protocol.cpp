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
// The daemon-side header is self-contained and declares only enums plus
// inline name tables, so the token vocabulary can be pinned by this test
// without linking the daemon dispatcher.
#include "daemon/TuiServerCommon.h"
#ifndef NO_TORRENTS
// Pure comment-page slicing lives in the snapshot header; the torrents build
// pins its page size against the wire bound.
#include "TorrentsControl.h"
#endif

using namespace i2p::tui;

static void TestRequests ()
{
	auto advanced = ParseRequest (
		"I2PDTUI/1 TORRENT-CREATE-V1 0 0 private 3 0:http://a.i2p/a 0:http://b.i2p/a 1:udp://c.i2p/a payload");
	assert (advanced && advanced->type == RequestType::TorrentCreateV1 && advanced->createPrivate);
	assert (!advanced->createPieceLength && advanced->argument == "payload");
	assert (advanced->createTrackers.size () == 2 && advanced->createTrackers.front ().size () == 2);
	assert (ParseRequest ("I2PDTUI/1 TORRENT-CREATE-CANCEL 0")->type == RequestType::TorrentCreateCancel);
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-CREATE-V1 0 16385 public 0 payload"));
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-CREATE-V1 0 0 private 1 1:http://a.i2p/a payload"));
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-CREATE-V1 0 0 private 101 payload"));
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-CREATE-CANCEL 0 trailing"));

	auto create = ParseRequest ("I2PDTUI/1 TORRENT-CREATE 0 payload");
	assert (create && create->type == RequestType::TorrentCreate && create->argument == "payload");
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-CREATE 0"));
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-CREATE 0 bad\x01path"));
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-CREATE 0 bad\x7fpath"));

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
	auto onlineOn = ParseRequest ("I2PDTUI/1 ADMIN ONLINE ON");
	assert (onlineOn.has_value ());
	assert (onlineOn->type == RequestType::AdminOnline && onlineOn->enabled);
	auto onlineOff = ParseRequest ("I2PDTUI/1 ADMIN ONLINE OFF");
	assert (onlineOff.has_value ());
	assert (onlineOff->type == RequestType::AdminOnline && !onlineOff->enabled);
	assert (!ParseRequest ("I2PDTUI/1 ADMIN ONLINE").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 ADMIN ONLINE MAYBE").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 ADMIN ONLINE ON extra").has_value ());
	auto logReopen = ParseRequest ("I2PDTUI/1 ADMIN LOG-REOPEN");
	assert (logReopen.has_value ());
	assert (logReopen->type == RequestType::AdminLogReopen);
	assert (!ParseRequest ("I2PDTUI/1 ADMIN LOG-REOPEN now").has_value ());
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

	auto services = ParseRequest ("I2PDTUI/1 SERVICES");
	assert (services.has_value ());
	assert (services->type == RequestType::Services);
	assert (services->limit == MAX_SERVICE_RECORDS_PER_REQUEST);
	auto limitedServices = ParseRequest ("I2PDTUI/1 SERVICES 25");
	assert (limitedServices.has_value () && limitedServices->limit == 25);
	assert (!ParseRequest ("I2PDTUI/1 SERVICES 0").has_value ());
	auto clampedServices = ParseRequest ("I2PDTUI/1 SERVICES 201");
	assert (clampedServices.has_value () && clampedServices->limit == MAX_SERVICE_RECORDS_PER_REQUEST);
	assert (!ParseRequest ("I2PDTUI/1 SERVICES 1 extra").has_value ());

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
	assert (ErrorCodeName (error->errorCode) == "unknown-command");
	assert (ErrorCodeName (ErrorCode::BadArgument) == "bad-argument");

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

static void TestAdminCommands ()
{
	auto reseed = ParseRequest ("I2PDTUI/1 ADMIN RESEED");
	assert (reseed.has_value () && reseed->type == RequestType::AdminReseed);
	assert (!ParseRequest ("I2PDTUI/1 ADMIN RESEED now").has_value ());

	auto graceful = ParseRequest ("I2PDTUI/1 ADMIN SHUTDOWN GRACEFUL");
	assert (graceful.has_value () && graceful->type == RequestType::AdminShutdown);
	assert (graceful->shutdownMode == AdminShutdownMode::Graceful);
	auto immediate = ParseRequest ("I2PDTUI/1 ADMIN SHUTDOWN IMMEDIATE");
	assert (immediate.has_value () && immediate->shutdownMode == AdminShutdownMode::Immediate);
	auto cancel = ParseRequest ("I2PDTUI/1 ADMIN SHUTDOWN CANCEL");
	assert (cancel.has_value () && cancel->shutdownMode == AdminShutdownMode::Cancel);
	assert (!ParseRequest ("I2PDTUI/1 ADMIN SHUTDOWN").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 ADMIN SHUTDOWN NOW").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 ADMIN SHUTDOWN graceful").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 ADMIN SHUTDOWN CANCEL now").has_value ());

	auto reload = ParseRequest ("I2PDTUI/1 ADMIN RELOAD TUNNELS");
	assert (reload.has_value () && reload->type == RequestType::AdminReloadTunnels);
	assert (!ParseRequest ("I2PDTUI/1 ADMIN RELOAD").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 ADMIN RELOAD CONFIG").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 ADMIN RELOAD /etc/i2pd.conf").has_value ());

	auto bandwidth = ParseRequest ("I2PDTUI/1 ADMIN BANDWIDTH L");
	assert (bandwidth.has_value () && bandwidth->type == RequestType::AdminBandwidth);
	assert (bandwidth->adminValue == static_cast<uint32_t> ('L'));
	for (auto token : { "K", "L", "M", "N", "O", "P", "X" })
		assert (ParseRequest (std::string ("I2PDTUI/1 ADMIN BANDWIDTH ") + token).has_value ());
	for (auto token : { "k", "Q", "LL", "1", "1000", "L/s", "" })
		assert (!ParseRequest (std::string ("I2PDTUI/1 ADMIN BANDWIDTH ") + token).has_value ());
	assert (!ParseRequest ("I2PDTUI/1 ADMIN BANDWIDTH L extra").has_value ());

	for (int percent = 0; percent <= 100; ++percent)
	{
		auto share = ParseRequest ("I2PDTUI/1 ADMIN SHARE " + std::to_string (percent));
		assert (share.has_value () && share->type == RequestType::AdminShare);
		assert (share->adminValue == static_cast<uint32_t> (percent));
	}
	assert (!ParseRequest ("I2PDTUI/1 ADMIN SHARE 101").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 ADMIN SHARE -1").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 ADMIN SHARE 50%").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 ADMIN SHARE").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 ADMIN SHARE 10 extra").has_value ());

	auto addressBook = ParseRequest ("I2PDTUI/1 ADDRESSBOOK 20");
	assert (addressBook.has_value () && addressBook->type == RequestType::AddressBook);
	assert (addressBook->limit == 20);
	assert (!ParseRequest ("I2PDTUI/1 ADDRESSBOOK 0").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 ADDRESSBOOK 20 extra").has_value ());

	auto addressAdd = ParseRequest ("I2PDTUI/1 ADMIN ADDRESS-ADD site.i2p AAAA");
	assert (addressAdd.has_value () && addressAdd->type == RequestType::AdminAddressAdd);
	assert (addressAdd->argument == "site.i2p" && addressAdd->argument2 == "AAAA");
	auto addressRemove = ParseRequest ("I2PDTUI/1 ADMIN ADDRESS-REMOVE site.i2p");
	assert (addressRemove.has_value () && addressRemove->type == RequestType::AdminAddressRemove);
	assert (!ParseRequest ("I2PDTUI/1 ADMIN ADDRESS-ADD site.i2p").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 ADMIN ADDRESS-ADD site.i2p AAAA extra").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 ADMIN ADDRESS-ADD site.i2p AAAA\nQUIT").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 ADMIN ADDRESS-REMOVE site.i2p extra").has_value ());
	auto addressBlock = ParseRequest ("I2PDTUI/1 ADMIN ADDRESS-BLOCK *spam*.i2p");
	assert (addressBlock.has_value () && addressBlock->type == RequestType::AdminAddressBlock);
	assert (addressBlock->argument == "*spam*.i2p");
	auto addressUnblock = ParseRequest ("I2PDTUI/1 ADMIN ADDRESS-UNBLOCK *spam*.i2p");
	assert (addressUnblock.has_value () && addressUnblock->type == RequestType::AdminAddressUnblock);
	auto blocked = ParseRequest ("I2PDTUI/1 ADDRESSBOOK-BLOCKED 12");
	assert (blocked.has_value () && blocked->type == RequestType::AddressBookBlocked);
	assert (blocked->limit == 12);

	// Address-book subscription sources are read as a bounded list and edited
	// through typed ADMIN verbs; a URL is one whitespace-free token.
	auto subscriptions = ParseRequest ("I2PDTUI/1 ADDRESSBOOK-SUBSCRIPTIONS 16");
	assert (subscriptions.has_value () && subscriptions->type == RequestType::AddressBookSubscriptions);
	assert (subscriptions->limit == 16);
	auto clampedSubscriptions = ParseRequest ("I2PDTUI/1 ADDRESSBOOK-SUBSCRIPTIONS 17");
	assert (clampedSubscriptions.has_value ());
	assert (clampedSubscriptions->limit == i2p::tui::MAX_SUBSCRIPTION_RECORDS_PER_REQUEST);
	assert (!ParseRequest ("I2PDTUI/1 ADDRESSBOOK-SUBSCRIPTIONS 0").has_value ());
	auto subAdd = ParseRequest ("I2PDTUI/1 ADMIN ADDRESS-SUB-ADD http://reg.i2p/hosts.txt");
	assert (subAdd.has_value () && subAdd->type == RequestType::AdminAddressSubAdd);
	assert (subAdd->argument == "http://reg.i2p/hosts.txt");
	auto subRemove = ParseRequest ("I2PDTUI/1 ADMIN ADDRESS-SUB-REMOVE http://reg.i2p/hosts.txt");
	assert (subRemove.has_value () && subRemove->type == RequestType::AdminAddressSubRemove);
	assert (!ParseRequest ("I2PDTUI/1 ADMIN ADDRESS-SUB-ADD").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 ADMIN ADDRESS-SUB-ADD http://a/ http://b/").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 ADMIN ADDRESS-SUB-ADD bad\nurl").has_value ());

	// Operator blocklist: counters plus the in-memory runtime ban list. The
	// router hash is a bounded base64 token.
	auto blocklist = ParseRequest ("I2PDTUI/1 BLOCKLIST 200");
	assert (blocklist.has_value () && blocklist->type == RequestType::Blocklist);
	assert (blocklist->limit == 200);
	auto clampedBlocklist = ParseRequest ("I2PDTUI/1 BLOCKLIST 201");
	assert (clampedBlocklist.has_value ());
	assert (clampedBlocklist->limit == i2p::tui::MAX_BLOCKLIST_RECORDS_PER_REQUEST);
	auto netDbBan = ParseRequest ("I2PDTUI/1 ADMIN NETDB-BAN AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=");
	assert (netDbBan.has_value () && netDbBan->type == RequestType::AdminNetDbBan);
	assert (netDbBan->argument == "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=");
	auto netDbUnban = ParseRequest ("I2PDTUI/1 ADMIN NETDB-UNBAN AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=");
	assert (netDbUnban.has_value () && netDbUnban->type == RequestType::AdminNetDbUnban);
	assert (!ParseRequest ("I2PDTUI/1 ADMIN NETDB-BAN").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 ADMIN NETDB-BAN hash extra").has_value ());
	std::string longHash (i2p::tui::MAX_ROUTER_HASH_TEXT_LENGTH + 1, 'A');
	assert (!ParseRequest ("I2PDTUI/1 ADMIN NETDB-BAN " + longHash).has_value ());

	auto commentsEnable = ParseRequest ("I2PDTUI/1 TORRENT-COMMENTS 0 3 ON");
	assert (commentsEnable && commentsEnable->type == RequestType::TorrentCommentsEnable);
	assert (commentsEnable->argument == "ON");
	auto commentAdd = ParseRequest ("I2PDTUI/1 TORRENT-COMMENT-ADD 0 3 4 author|text");
	assert (commentAdd && commentAdd->type == RequestType::TorrentCommentAdd);
	assert (commentAdd->argument == "4 author|text");
	auto commentHide = ParseRequest ("I2PDTUI/1 TORRENT-COMMENT-HIDE 0 3 42");
	assert (commentHide && commentHide->type == RequestType::TorrentCommentHide);
	assert (commentHide->argument == "42");
	auto commentUnhide = ParseRequest ("I2PDTUI/1 TORRENT-COMMENT-UNHIDE 0 3 42");
	assert (commentUnhide && commentUnhide->type == RequestType::TorrentCommentUnhide);
	assert (commentUnhide->argument == "42");
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-COMMENT-ADD 0 3").has_value ());
	auto metaComment = ParseRequest ("I2PDTUI/1 TORRENT-META-COMMENT 0 3 hello world");
	assert (metaComment.has_value () && metaComment->type == RequestType::TorrentSetComment);
	assert (metaComment->torrentId == 3 && metaComment->argument == "hello world");
	auto metaCreated = ParseRequest ("I2PDTUI/1 TORRENT-META-CREATED-BY 0 3 openi2pd");
	assert (metaCreated.has_value () && metaCreated->type == RequestType::TorrentSetCreatedBy);
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-META-COMMENT 0 0 text").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-META-CREATED-BY 0 3 bad\nvalue").has_value ());

	// Existing typed admin operations now carry a canonical level index.
	auto logLevel = ParseRequest ("I2PDTUI/1 ADMIN LOGLEVEL warn");
	assert (logLevel.has_value () && logLevel->type == RequestType::AdminLogLevel);
	assert (logLevel->adminValue == 3 && logLevel->argument == "warn");

	// No generic option or file/URL carrying forms exist.
	assert (!ParseRequest ("I2PDTUI/1 ADMIN SET bandwidth L").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 ADMIN BANDWIDTH").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 ADMIN RELOAD https://example.invalid/").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 ADMIN SHUTDOWN GRACEFUL;reboot").has_value ());
}

static void TestTunnelAndReseedAdminVerbs ()
{
	// One configured tunnel, addressed by its bounded, whitespace-free service
	// name. Both verbs accept exactly one token and no trailing data.
	auto start = ParseRequest ("I2PDTUI/1 ADMIN TUNNEL-START web");
	assert (start.has_value () && start->type == RequestType::AdminTunnelStart);
	assert (start->argument == "web");
	auto stop = ParseRequest ("I2PDTUI/1 ADMIN TUNNEL-STOP web");
	assert (stop.has_value () && stop->type == RequestType::AdminTunnelStop);
	assert (stop->argument == "web");
	auto longest = std::string ("I2PDTUI/1 ADMIN TUNNEL-START ")
		+ std::string (MAX_TUNNEL_SERVICE_NAME_LENGTH, 'a');
	assert (ParseRequest (longest).has_value ());
	assert (!ParseRequest (longest + "a").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 ADMIN TUNNEL-START").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 ADMIN TUNNEL-START web extra").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 ADMIN TUNNEL-START ").has_value ());
	// A name is one line: no whitespace, control characters, NUL or a hidden
	// second request can ride along.
	assert (!ParseRequest ("I2PDTUI/1 ADMIN TUNNEL-START two words").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 ADMIN TUNNEL-START web\tname").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 ADMIN TUNNEL-START web\nQUIT").has_value ());
	assert (!ParseRequest (std::string ("I2PDTUI/1 ADMIN TUNNEL-START web") + '\0' + "QUIT").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 ADMIN TUNNEL-START web\x01name").has_value ());
	// Only the closed verb set exists: no generic tunnel writer. The argument is
	// a lookup key and never a file path or URL the daemon would open, so a
	// path-shaped name simply resolves to "unknown".
	assert (!ParseRequest ("I2PDTUI/1 ADMIN TUNNEL-RESTART web").has_value ());
	assert (ParseRequest ("I2PDTUI/1 ADMIN TUNNEL-START /etc/i2pd/tunnels.conf").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 TUNNEL-START web").has_value ());

	// Reseed never carries an argument: the daemon reports the typed outcome.
	auto reseed = ParseRequest ("I2PDTUI/1 ADMIN RESEED");
	assert (reseed.has_value () && reseed->type == RequestType::AdminReseed);
	assert (!ParseRequest ("I2PDTUI/1 ADMIN RESEED now").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 ADMIN RESEED ON").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 RESEED").has_value ());

	// The typed outcome tokens are the protocol vocabulary; the reseed and
	// tunnel verbs are the only producers of these spellings.
	using i2p::tui::AdminOutcome;
	assert (AdminOutcomeName (AdminOutcome::None).empty ());
	assert (AdminOutcomeName (AdminOutcome::Started) == "started");
	assert (AdminOutcomeName (AdminOutcome::Stopped) == "stopped");
	assert (AdminOutcomeName (AdminOutcome::AlreadyRunning) == "already-running");
	assert (AdminOutcomeName (AdminOutcome::AlreadyStopped) == "already-stopped");
	assert (AdminOutcomeName (AdminOutcome::Unknown) == "unknown");
	assert (AdminOutcomeName (AdminOutcome::Disabled) == "disabled");
	assert (AdminOutcomeName (AdminOutcome::Exists) == "exists");
	// Every token is one lowercase word, so a client never has to translate
	// or guess a spelling and a token can never carry whitespace.
	for (auto outcome : { AdminOutcome::Started, AdminOutcome::Stopped, AdminOutcome::AlreadyRunning,
			 AdminOutcome::AlreadyStopped, AdminOutcome::Unknown, AdminOutcome::Disabled,
			 AdminOutcome::Exists })
	{
		const auto token = AdminOutcomeName (outcome);
		assert (!token.empty () && token.size () <= MAX_FIELD_NAME);
		for (char ch : token)
			assert ((ch >= 'a' && ch <= 'z') || ch == '-');
	}

	// The reseed status and tunnel list field names must fit the wire limit,
	// including the largest indexed variant a bounded snapshot can produce.
	for (auto name : { "netdb.reseed.phase", "netdb.reseed.enabled", "netdb.reseed.attempts",
			 "netdb.reseed.succeeded", "netdb.reseed.last_attempt_ms", "netdb.reseed.last_success_ms",
			 "netdb.reseed.last_error", "tunnel.service", "tunnel.service.count", "outcome", "running" })
		assert (std::string_view (name).size () <= MAX_FIELD_NAME);

	// A tunnel row and an admin reply must survive framing and escaping.
	std::string payload;
	AppendField (payload, "tunnel.service", "web|client|1|127.0.0.1:4444|site.i2p");
	AppendField (payload, "tunnel.service.count", 1);
	AppendField (payload, "outcome", "already-running");
	AppendField (payload, "running", true);
	auto frame = MakeResponse (ResponseStatus::Ok, payload);
	auto newline = frame.find ('\n');
	assert (newline != std::string::npos);
	auto header = ParseResponseHeader (std::string_view (frame).substr (0, newline));
	assert (header.has_value ());
	assert (header->payloadSize == payload.size ());
	assert (frame.substr (newline + 1) == payload);
}

static void TestAddressReplaceVerbs ()
{
	// ADDRESS-ADD never re-points an existing name; ADDRESS-REPLACE is the
	// explicit overwrite verb. Both carry the same bounded pair.
	auto add = ParseRequest ("I2PDTUI/1 ADMIN ADDRESS-ADD site.i2p AAAA");
	assert (add.has_value () && add->type == RequestType::AdminAddressAdd);
	assert (add->argument == "site.i2p" && add->argument2 == "AAAA");
	auto replace = ParseRequest ("I2PDTUI/1 ADMIN ADDRESS-REPLACE site.i2p BBBB");
	assert (replace.has_value () && replace->type == RequestType::AdminAddressReplace);
	assert (replace->argument == "site.i2p" && replace->argument2 == "BBBB");
	assert (RequestType::AdminAddressAdd != RequestType::AdminAddressReplace);

	assert (!ParseRequest ("I2PDTUI/1 ADMIN ADDRESS-REPLACE site.i2p").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 ADMIN ADDRESS-REPLACE").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 ADMIN ADDRESS-REPLACE site.i2p AAAA extra").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 ADMIN ADDRESS-REPLACE site.i2p AAAA\nQUIT").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 ADMIN ADDRESS-REPLACE " + std::string (MAX_ADDRESS_ADD_NAME_LENGTH + 1, 'a')
						   + " AAAA")
				   .has_value ());
	std::string longDestination (MAX_ADDRESS_DESTINATION_LENGTH + 1, 'A');
	assert (!ParseRequest ("I2PDTUI/1 ADMIN ADDRESS-ADD site.i2p " + longDestination).has_value ());
	assert (!ParseRequest ("I2PDTUI/1 ADMIN ADDRESS-REPLACE site.i2p " + longDestination).has_value ());
	// Exactly the declared destination bound is accepted; the address book
	// still validates that it is a full base64 destination.
	std::string atBound (MAX_ADDRESS_DESTINATION_LENGTH, 'A');
	assert (ParseRequest ("I2PDTUI/1 ADMIN ADDRESS-ADD site.i2p " + atBound).has_value ());
	// The stored-name verbs keep the wider name bound.
	std::string longName (MAX_ADDRESS_NAME_LENGTH, 'a');
	assert (ParseRequest ("I2PDTUI/1 ADMIN ADDRESS-REMOVE " + longName).has_value ());
	assert (!ParseRequest ("I2PDTUI/1 ADMIN ADDRESS-REMOVE " + longName + "a").has_value ());

	// The add/replace result codes are a closed set too.
	using i2p::tui::AdminResultCode;
	assert (AdminResultCodeName (AdminResultCode::Ok) == "ok");
	assert (AdminResultCodeName (AdminResultCode::Accepted) == "accepted");
	assert (AdminResultCodeName (AdminResultCode::Invalid) == "invalid");
	assert (AdminResultCodeName (AdminResultCode::Unavailable) == "unavailable");
	assert (AdminResultCodeName (AdminResultCode::QueueFull) == "queue-full");
	assert (AdminResultCodeName (AdminResultCode::Failed) == "failed");
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
	// Without page arguments the historic request is preserved exactly: page
	// one, newest records first.
	assert (detail->commentOffset == 0);
	assert (detail->commentLimit == MAX_TORRENT_COMMENT_RECORDS_PER_REQUEST);
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-DETAIL 1 0").has_value ());

	// TORRENT-DETAIL accepts an optional trailing comment page
	// `[offset] [limit]`: offset 0..255, limit 1..16.
	auto detailPage = ParseRequest ("I2PDTUI/1 TORRENT-DETAIL 0 7 16 8");
	assert (detailPage.has_value () && detailPage->type == RequestType::TorrentDetail);
	assert (detailPage->tunnelIndex == 0 && detailPage->torrentId == 7);
	assert (detailPage->commentOffset == 16 && detailPage->commentLimit == 8);
	auto detailOffsetOnly = ParseRequest ("I2PDTUI/1 TORRENT-DETAIL 0 7 16");
	assert (detailOffsetOnly.has_value () && detailOffsetOnly->commentOffset == 16);
	assert (detailOffsetOnly->commentLimit == MAX_TORRENT_COMMENT_RECORDS_PER_REQUEST);
	auto detailFullPage = ParseRequest ("I2PDTUI/1 TORRENT-DETAIL 0 7 240 16");
	assert (detailFullPage.has_value () && detailFullPage->commentOffset == 240);
	assert (detailFullPage->commentLimit == MAX_TORRENT_COMMENT_RECORDS_PER_REQUEST);
	auto detailSingle = ParseRequest ("I2PDTUI/1 TORRENT-DETAIL 0 7 0 1");
	assert (detailSingle.has_value () && detailSingle->commentOffset == 0
		&& detailSingle->commentLimit == 1);
	auto detailLastPage = ParseRequest ("I2PDTUI/1 TORRENT-DETAIL 0 7 255 16");
	assert (detailLastPage.has_value () && detailLastPage->commentOffset == MAX_TORRENT_COMMENT_OFFSET);
	// Out-of-range, negative, non-numeric and extra tokens are rejected rather
	// than clamped: a client must learn that its page request was invalid.
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-DETAIL 0 7 0 0").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-DETAIL 0 7 0 17").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-DETAIL 0 7 256").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-DETAIL 0 7 256 16").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-DETAIL 0 7 -1").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-DETAIL 0 7 0 -1").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-DETAIL 0 7 1x").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-DETAIL 0 7 0 x").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-DETAIL 0 7 0 8 extra").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-DETAIL 0 7 8 8 8").has_value ());
	// The page arguments belong to TORRENT-DETAIL alone.
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-PAUSE 0 7 8").has_value ());

	auto pause = ParseRequest ("I2PDTUI/1 TORRENT-PAUSE 0 3");
	assert (pause.has_value () && pause->type == RequestType::TorrentPause);
	assert (pause->tunnelIndex == 0 && pause->torrentId == 3);
	auto resume = ParseRequest ("I2PDTUI/1 TORRENT-RESUME 0 3");
	assert (resume.has_value () && resume->type == RequestType::TorrentResume);
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-PAUSE 0").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-RESUME 0 3 extra").has_value ());

	auto recheck = ParseRequest ("I2PDTUI/1 TORRENT-RECHECK 0 3");
	assert (recheck.has_value () && recheck->type == RequestType::TorrentRecheck);
	assert (recheck->tunnelIndex == 0 && recheck->torrentId == 3);
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-RECHECK 0").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-RECHECK 0 3 extra").has_value ());

	auto remove = ParseRequest ("I2PDTUI/1 TORRENT-REMOVE 0 3 files");
	assert (remove.has_value () && remove->type == RequestType::TorrentRemove);
	assert (remove->deleteFiles);
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-REMOVE 0 3 all").has_value ());

	// TORRENT-REMOVE-ALL is addressed by the tunnel index alone. The only
	// accepted option is the exact token "files"; no torrent id is parsed.
	auto removeAll = ParseRequest ("I2PDTUI/1 TORRENT-REMOVE-ALL 2");
	assert (removeAll.has_value () && removeAll->type == RequestType::TorrentRemoveAll);
	assert (removeAll->tunnelIndex == 2 && !removeAll->deleteFiles && removeAll->torrentId == 0);
	auto removeAllFiles = ParseRequest ("I2PDTUI/1 TORRENT-REMOVE-ALL 2 files");
	assert (removeAllFiles.has_value () && removeAllFiles->type == RequestType::TorrentRemoveAll);
	assert (removeAllFiles->tunnelIndex == 2 && removeAllFiles->deleteFiles);
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-REMOVE-ALL").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-REMOVE-ALL 2 Files").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-REMOVE-ALL 2 files extra").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-REMOVE-ALL 2 5").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-REMOVE-ALL -1").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-REMOVE-ALL 4294967296").has_value ());

	// Comment hide/unhide carry the bounded local comment id as their argument.
	auto commentHide = ParseRequest ("I2PDTUI/1 TORRENT-COMMENT-HIDE 0 3 42");
	assert (commentHide.has_value () && commentHide->type == RequestType::TorrentCommentHide);
	assert (commentHide->tunnelIndex == 0 && commentHide->torrentId == 3 && commentHide->argument == "42");
	auto commentUnhide = ParseRequest ("I2PDTUI/1 TORRENT-COMMENT-UNHIDE 0 3 42");
	assert (commentUnhide.has_value () && commentUnhide->type == RequestType::TorrentCommentUnhide);
	assert (commentUnhide->tunnelIndex == 0 && commentUnhide->torrentId == 3
		&& commentUnhide->argument == "42");
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-COMMENT-UNHIDE 0 3").has_value ());

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

	// Single-torrent lifecycle verbs used by the TUI and the Android detail
	// actions; each binds a tunnel index and a positive engine id.
	auto startOne = ParseRequest ("I2PDTUI/1 TORRENT-START 1 9");
	assert (startOne.has_value () && startOne->type == RequestType::TorrentStart);
	assert (startOne->tunnelIndex == 1 && startOne->torrentId == 9);
	auto stopOne = ParseRequest ("I2PDTUI/1 TORRENT-STOP 1 9");
	assert (stopOne.has_value () && stopOne->type == RequestType::TorrentStop);
	assert (stopOne->tunnelIndex == 1 && stopOne->torrentId == 9);
	auto reannounceOne = ParseRequest ("I2PDTUI/1 TORRENT-REANNOUNCE 1 9");
	assert (reannounceOne.has_value () && reannounceOne->type == RequestType::TorrentReannounce);
	assert (reannounceOne->tunnelIndex == 1 && reannounceOne->torrentId == 9);
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-START 0 0").has_value ());
	assert (!ParseRequest ("I2PDTUI/1 TORRENT-REANNOUNCE 0 3 files").has_value ());
}

static void TestCommentPageBounds ()
{
#ifndef NO_TORRENTS
	using i2p::torrents::SliceCommentPage;
	// The snapshot retains up to 256 records while one response emits at most
	// 16; the pure slice is what both the daemon and the clients rely on.
	// The per-torrent comments.visible/hidden/local counters are produced by
	// TorrentsTunnel::GetControlSnapshot and need a live torrent fixture, so
	// they are not exercised by this protocol test.
	assert (i2p::torrents::MAX_COMMENT_PAGE == MAX_TORRENT_COMMENT_RECORDS_PER_REQUEST);

	auto first = SliceCommentPage (40, 0, MAX_TORRENT_COMMENT_RECORDS_PER_REQUEST);
	assert (first.offset == 0u && first.limit == 16u && first.count == 16u && first.total == 40u);
	assert (first.truncated);

	auto middle = SliceCommentPage (40, 16, 16);
	assert (middle.offset == 16u && middle.count == 16u && middle.truncated);

	auto last = SliceCommentPage (40, 32, 16);
	assert (last.offset == 32u && last.count == 8u && !last.truncated);

	auto exact = SliceCommentPage (16, 0, 16);
	assert (exact.count == 16u && !exact.truncated);

	auto single = SliceCommentPage (256, 255, 16);
	assert (single.offset == 255u && single.count == 1u && !single.truncated);

	auto empty = SliceCommentPage (0, 0, 16);
	assert (empty.count == 0u && empty.truncated == false);

	// A page size outside 1..16 is clamped, never honoured as given.
	auto clamped = SliceCommentPage (40, 0, 4096);
	assert (clamped.limit == 16u && clamped.count == 16u && clamped.truncated);
	auto zeroLimit = SliceCommentPage (40, 0, 0);
	assert (zeroLimit.limit == 1u && zeroLimit.count == 1u && zeroLimit.truncated);

	// An offset past the retained records is an empty page with a truthful
	// total; it never reads out of range and never fails.
	auto beyond = SliceCommentPage (5, 255, 16);
	assert (beyond.total == 5u && beyond.offset == 5u && beyond.count == 0u);
	assert (!beyond.truncated);
#endif
}

int main ()
{
	TestRequests ();
	TestAdminCommands ();
	TestTunnelAndReseedAdminVerbs ();
	TestAddressReplaceVerbs ();
	TestConfigRequests ();
	TestTorrentRequests ();
	TestCommentPageBounds ();
	TestResponseHeader ();
	TestFraming ();
	TestFields ();
	return 0;
}
