/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test torrents dht interop.
 *
 * Checks the I2P torrent DHT codec: compact node layout and secure node-ID binding, opaque token bytes, nearest-node selection across buckets, seed maintenance and KRPC error envelopes.
 *
 */

#include <cassert>
#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include "TorrentsDHT.h"

#ifndef NO_TORRENTS
using namespace i2p::torrents;

int main ()
{
	std::array<uint8_t, 20> exact { };
	assert (ParseByteArray (CreateByteString (std::string (20, 'x')), exact).second);
	assert (!ParseByteArray (CreateByteString (std::string (21, 'x')), exact).second);
	assert (!ParseByteArray (CreateByteString (std::string (19, 'x')), exact).second);

	// Independent vector for the documented I2P compact node layout and
	// secure-NID binding (Java 2.13.0 NodeInfo, not the public UDP DHT).
	i2p::data::IdentHash peer;
	std::memset (peer (), 0x42, peer.len);
	NodeID id { };
	std::memcpy (id.data (), peer (), 6);
	const uint16_t port = 0x1234;
	id[4] ^= 0x12;
	id[5] ^= 0x34;
	auto node = std::make_shared<Node> (id, peer, port);
	assert (node->VerifyID ());
	auto compact = node->GetNodeInfo ();
	static_assert (NodeInfo { }.size () == 54);
	assert (compact[52] == 0x12 && compact[53] == 0x34);
	Node restored (compact);
	assert (restored.id == id && restored.peer == peer && restored.port == port);
	assert (restored.VerifyID ());
	assert (!Node (id, peer, 0).VerifyID ());
	NodeID maxPortID = id;
	maxPortID[4] = 0x42 ^ 0xff;
	maxPortID[5] = 0x42 ^ 0xff;
	assert (!Node (maxPortID, peer, 65535).VerifyID ());
	id[0] ^= 1;
	assert (!Node (id, peer, port).VerifyID ());

	// Opaque tokens must retain every byte, including NULs and bytes past
	// offset eight, when queued for a later announce. No integer conversion.
	auto torrent = std::make_shared<Torrent> (Torrent::InfoHash { });
	RequestInfo request (torrent, 1);
	GetPeersToken token (64, '\0');
	token[0] = '\x81';
	token[63] = '\x7f';
	assert (request.AddNodeToken (node, token));
	assert (std::get<2> (*request.tokens.begin ()) == token);
	RequestInfo oversized (torrent, 1);
	assert (!oversized.AddNodeToken (node, GetPeersToken (65, 'x')));
	RequestInfo emptyToken (torrent, 1);
	assert (emptyToken.AddNodeToken (node, GetPeersToken { }));
	assert (std::get<2> (*emptyToken.tokens.begin ()).empty ());

	// Find the globally nearest nodes even when the result crosses bucket
	// boundaries; a target's single bucket is not a complete Kademlia answer.
	NodeID local { };
	local[0] = 0x80;
	RoutingTable routing (local);
	std::vector<std::shared_ptr<Node>> inserted;
	for (unsigned i = 0; i < 16; ++i)
	{
		NodeID candidate { };
		candidate[0] = 1 + i * 16;
		auto entry = std::make_shared<Node> (candidate, peer, port);
		assert (routing.AddNode (entry));
		inserted.push_back (entry);
	}
	assert (routing.GetNumBuckets () > 1);
	NodeID target { };
	target[0] = 0x7f;
	std::sort (inserted.begin (), inserted.end (),
		[&target] (const auto & a, const auto & b) { return (a->id ^ target) < (b->id ^ target); });
	auto nearest = routing.GetClosestNodes (target, 10);
	assert (nearest.size () == 10);
	for (size_t i = 0; i < nearest.size (); ++i) assert (nearest[i] == inserted[i]);
	auto excluded = routing.GetClosestNodes (target, 10, &inserted.front ()->id);
	assert (excluded.size () == 10 && excluded.front () == inserted[1]);
	assert (routing.GetClosestNodes (target, 0).empty ());
	RequestInfo budget (nullptr, 1);
	assert (budget.AddNode (inserted[0]) && budget.AddNode (inserted[1]));
	assert (budget.GetNextNode () && !budget.GetNextNode ());

	// Seed announcements are refreshed in place, and noseed excludes seeds
	// without excluding leechers or returning the requester to itself.
	DHTTorrent swarm;
	i2p::data::IdentHash leecher = peer;
	leecher ()[31] ^= 1;
	assert (swarm.AddPeer (peer, true));
	assert (swarm.HasPeers () && !swarm.HasPeers (true));
	assert (swarm.GetBEncodedPeers (true) == "le");
	assert (swarm.AddPeer (leecher, false));
	// Keep the encoded buffer alive while inspecting string views.
	auto encoded = swarm.GetBEncodedPeers (true);
	auto parsedPeers = ParseStringList (encoded);
	assert (parsedPeers.second == encoded.size () && parsedPeers.first.size () == 1);
	assert (!std::memcmp (parsedPeers.first.front ().data (), leecher.data (), leecher.len));
	assert (swarm.GetBEncodedPeers (true, &leecher) == "le");
	assert (!swarm.AddPeer (peer, false));
	assert (swarm.HasPeers (true));
	assert (swarm.CleanUp (i2p::util::GetMonotonicSeconds () + DHT_TORRENT_PEER_EXPIRATION_TIME + 1));

	DHTQueryLimiter limiter;
	for (size_t i = 0; i < MAX_DHT_QUERIES_PER_ORIGIN; ++i) assert (limiter.Allow (peer, 100));
	assert (!limiter.Allow (peer, 100));
	assert (!limiter.Allow (peer, 99)); // clock regression cannot reset the quota
	assert (limiter.Allow (leecher, 100));
	assert (limiter.Allow (peer, 100 + DHT_QUERY_RATE_WINDOW));
	DHTQueryLimiter bounded;
	for (size_t i = 0; i < MAX_DHT_QUERY_ORIGINS; ++i)
	{
		i2p::data::IdentHash origin = peer;
		origin ()[0] = i >> 8;
		origin ()[1] = i & 0xff;
		assert (bounded.Allow (origin, 100));
	}
	assert (!bounded.Allow (peer, 100));
	assert (bounded.Allow (peer, 100 + DHT_QUERY_RATE_WINDOW));

	// KRPC errors omit r.id: decode their own envelope before ordinary
	// successful replies. Transaction bytes are opaque, including NULs.
	const std::string transaction ("q\0reply!", 8);
	const auto errorList = CreateList ({ CreateInteger (203), CreateByteString ("protocol error") });
	const auto errorFrame = [&] (std::string_view error, std::string_view tx = { })
	{
		return CreateDictionary (
			{ { "e", error }, { "t", CreateByteString (tx.empty () ? std::string_view (transaction) : tx) },
				{ "y", CreateByteString ("e") } });
	};
	const auto encodedError = errorFrame (errorList);
	auto decodedError = DecodeDHTErrorReply (encodedError);
	assert (decodedError && decodedError->code == 203 && decodedError->transactionID == transaction);
	for (size_t length = 0; length < encodedError.size (); ++length)
		assert (!DecodeDHTErrorReply (std::string_view (encodedError).substr (0, length)));
	assert (!DecodeDHTErrorReply (encodedError + "e"));
	assert (!DecodeDHTErrorReply (errorFrame (CreateByteString ("not a list"))));
	assert (!DecodeDHTErrorReply (errorFrame (CreateList ({ CreateInteger (203) }))));
	assert (!DecodeDHTErrorReply (
		errorFrame (CreateList ({ CreateInteger (203), CreateByteString ("x"), CreateInteger (1) }))));
	assert (!DecodeDHTErrorReply (errorFrame (CreateList ({ CreateByteString ("203"), CreateByteString ("x") }))));
	assert (!DecodeDHTErrorReply (errorFrame (CreateList ({ CreateInteger (203), CreateInteger (1) }))));
	assert (!DecodeDHTErrorReply (
		errorFrame (CreateList ({ CreateInteger (int64_t { 1 } << 32), CreateByteString ("x") }))));
	assert (!DecodeDHTErrorReply (
		errorFrame (CreateList ({ CreateInteger (203), CreateByteString (std::string (1025, 'x')) }))));
	assert (!DecodeDHTErrorReply (errorFrame (errorList, std::string (17, 't'))));
	assert (!DecodeDHTErrorReply (
		CreateDictionary ({ { "e", errorList }, { "t", CreateByteString ("") }, { "y", CreateByteString ("e") } })));
	assert (!DecodeDHTErrorReply (CreateDictionary ({ { "e", errorList }, { "t", CreateByteString (transaction) } })));
	assert (!DecodeDHTErrorReply (CreateDictionary (
		{ { "e", errorList }, { "t", CreateByteString (transaction) }, { "y", CreateByteString ("r") } })));
	assert (!DecodeDHTErrorReply (CreateDictionary ({ { "e", errorList }, { "t", CreateByteString (transaction) },
		{ "r", "de" }, { "y", CreateByteString ("e") } })));
	return 0;
}
#else
int main ()
{
	return 0;
}
#endif
