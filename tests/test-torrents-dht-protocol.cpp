/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: torrents DHT protocol regressions.
 *
 * Pins the side-effect free KRPC frame decoder, the bounded issued-token store
 * with its per-node quota, the compact node/peer decoders, the per-peer routing
 * cap, the anonymous raw-frame budget, the port-safety helpers and the flat
 * state-record validation. No socket, timer or live node is involved.
 */

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "TorrentsDHT.h"
#include "TorrentsTunnel.h"

#ifndef NO_TORRENTS

namespace
{
using namespace i2p::torrents;

std::string_view AsView (const void * data, size_t len)
{
	return std::string_view (reinterpret_cast<const char *> (data), len);
}

std::string_view AsView (const NodeID & id)
{
	return AsView (id.data (), id.size ());
}
std::string_view AsView (const Torrent::InfoHash & hash)
{
	return AsView (hash.data (), hash.size ());
}
std::string_view AsView (const i2p::data::IdentHash & ident)
{
	return AsView (ident.data (), ident.len);
}

i2p::data::IdentHash MakePeer (uint8_t salt)
{
	i2p::data::IdentHash peer;
	std::memset (peer (), salt, peer.len);
	return peer;
}

// Only the first six node-ID bytes are bound to the peer and port, so a
// distinct tail is what makes a second entry look like another node.
NodeID MakeNodeID (const i2p::data::IdentHash & peer, uint16_t port, uint8_t salt)
{
	NodeID id { };
	std::memcpy (id.data (), peer (), 6);
	id[4] ^= static_cast<uint8_t> (port >> 8);
	id[5] ^= static_cast<uint8_t> (port & 0xFF);
	std::memset (id.data () + 6, salt, id.size () - 6);
	return id;
}

Torrent::InfoHash MakeInfoHash (uint8_t salt)
{
	Torrent::InfoHash hash { };
	std::memset (hash.data (), salt, hash.size ());
	return hash;
}

// Deterministic and unique per (fill, node, port). The wire token itself
// stays opaque eight-byte material.
DHTIncomingToken MakeToken (uint8_t fill, const NodeID & nodeID, const i2p::data::IdentHash & peer,
	const Torrent::InfoHash & infoHash, uint16_t port, uint64_t issued)
{
	const std::array<uint8_t, 8> raw { fill, nodeID[0], nodeID[6], nodeID[7], static_cast<uint8_t> (port >> 8),
		static_cast<uint8_t> (port), infoHash[0], peer.data ()[0] };
	DHTIncomingToken entry;
	entry.token.assign (reinterpret_cast<const char *> (raw.data ()), raw.size ());
	entry.nodeID = nodeID;
	entry.peer = peer;
	entry.infoHash = infoHash;
	entry.port = port;
	entry.issued = issued;
	return entry;
}

std::string PingQuery (const NodeID & nodeID, std::string_view transaction)
{
	return CreateDictionary ({ { "a", CreateDictionary ({ { "id", CreateByteString (AsView (nodeID)) } }) },
		{ "q", CreateByteString ("ping") }, { "t", CreateByteString (transaction) }, { "y", CreateByteString ("q") } });
}

void TestFrameQueries ()
{
	NodeID nodeID { };
	std::memset (nodeID.data (), 0x11, nodeID.size ());
	const auto infoHash = MakeInfoHash (0x22);

	// ping: y=q, q=ping, t, a.id.
	const auto ping = PingQuery (nodeID, "tt01");
	const auto pingFrame = DecodeDHTFrame (ping);
	assert (pingFrame.kind == DHTFrameKind::Query);
	assert (pingFrame.transaction == "tt01");
	assert (pingFrame.query == "ping");
	assert (pingFrame.arguments == CreateDictionary ({ { "id", CreateByteString (AsView (nodeID)) } }));
	assert (pingFrame.response.empty () && pingFrame.errorText.empty () && pingFrame.errorCode == 0);
	assert (GetKRPCQueryKind (pingFrame.query) == KRPCQuery::eKRPCQueryPing);

	// find_node: a.id + a.target.
	const auto findNode = CreateDictionary ({ { "a",
												  CreateDictionary ({ { "id", CreateByteString (AsView (nodeID)) },
													  { "target", CreateByteString (AsView (infoHash)) } }) },
		{ "q", CreateByteString ("find_node") }, { "t", CreateByteString ("tt02") }, { "y", CreateByteString ("q") } });
	const auto findNodeFrame = DecodeDHTFrame (findNode);
	assert (findNodeFrame.kind == DHTFrameKind::Query);
	assert (findNodeFrame.transaction == "tt02");
	assert (findNodeFrame.query == "find_node");
	assert (GetKRPCQueryKind (findNodeFrame.query) == KRPCQuery::eKRPCQueryFindNode);

	// get_peers: a.id + a.info_hash + a.noseed.
	const auto getPeers = CreateDictionary (
		{ { "a",
			  CreateDictionary ({ { "id", CreateByteString (AsView (nodeID)) },
				  { "info_hash", CreateByteString (AsView (infoHash)) }, { "noseed", CreateInteger (1) } }) },
			{ "q", CreateByteString ("get_peers") }, { "t", CreateByteString ("tt03") },
			{ "y", CreateByteString ("q") } });
	const auto getPeersFrame = DecodeDHTFrame (getPeers);
	assert (getPeersFrame.kind == DHTFrameKind::Query);
	assert (getPeersFrame.query == "get_peers");
	assert (GetKRPCQueryKind (getPeersFrame.query) == KRPCQuery::eKRPCQueryGetPeers);

	// announce_peer: a.id + a.info_hash + a.seed + a.token, sent raw.
	const std::string token (8, 'k');
	const auto announce
		= CreateDictionary ({ { "a",
								  CreateDictionary ({ { "id", CreateByteString (AsView (nodeID)) },
									  { "info_hash", CreateByteString (AsView (infoHash)) },
									  { "seed", CreateInteger (1) }, { "token", CreateByteString (token) } }) },
			{ "q", CreateByteString ("announce_peer") }, { "t", CreateByteString ("tt04") },
			{ "y", CreateByteString ("q") } });
	const auto announceFrame = DecodeDHTFrame (announce);
	assert (announceFrame.kind == DHTFrameKind::Query);
	assert (announceFrame.query == "announce_peer");
	assert (GetKRPCQueryKind (announceFrame.query) == KRPCQuery::eKRPCQueryAnnouncePeer);

	// An unknown but well-formed query name is not a framing error.
	const auto unknown = CreateDictionary ({ { "a",
												 CreateDictionary ({ { "id", CreateByteString (AsView (nodeID)) } }) },
		{ "q", CreateByteString ("dht_bogus") }, { "t", CreateByteString ("tt05") }, { "y", CreateByteString ("q") } });
	const auto unknownFrame = DecodeDHTFrame (unknown);
	assert (unknownFrame.kind == DHTFrameKind::Query);
	assert (unknownFrame.query == "dht_bogus");
	assert (!GetKRPCQueryKind (unknownFrame.query).has_value ());
	assert (!GetKRPCQueryKind ("").has_value ());

	// BEP 5's optional `v` client version is the one tolerated extension
	// key and is never read; any other top-level key is rejected.
	const auto versioned
		= CreateDictionary ({ { "a", CreateDictionary ({ { "id", CreateByteString (AsView (nodeID)) } }) },
			{ "q", CreateByteString ("ping") }, { "t", CreateByteString ("tt06") }, { "v", CreateByteString ("UT35") },
			{ "y", CreateByteString ("q") } });
	const auto versionedFrame = DecodeDHTFrame (versioned);
	assert (versionedFrame.kind == DHTFrameKind::Query && versionedFrame.transaction == "tt06");
}

void TestFrameResponses ()
{
	NodeID nodeID { };
	std::memset (nodeID.data (), 0x33, nodeID.size ());
	const auto peer = MakePeer (0x44);
	const std::string token (8, 't');

	const auto values = CreateDictionary (
		{ { "r",
			  CreateDictionary ({ { "id", CreateByteString (AsView (nodeID)) }, { "token", CreateByteString (token) },
				  { "values", CreateList ({ CreateByteString (AsView (peer)) }) } }) },
			{ "t", CreateByteString ("tt06") }, { "y", CreateByteString ("r") } });
	const auto valuesFrame = DecodeDHTFrame (values);
	assert (valuesFrame.kind == DHTFrameKind::Response);
	assert (valuesFrame.transaction == "tt06");
	assert (valuesFrame.query.empty () && valuesFrame.arguments.empty ());
	assert (valuesFrame.response.front () == 'd' && valuesFrame.response.back () == 'e');

	// Compact referral nodes keep the 54-byte record layout.
	std::vector<uint8_t> records;
	for (uint8_t i = 0; i < 2; i++)
	{
		const auto record = Node (MakeNodeID (peer, 0x1234, i), peer, 0x1234).GetNodeInfo ();
		assert (record.size () == DHT_COMPACT_NODE_SIZE);
		records.insert (records.end (), record.begin (), record.end ());
	}
	const auto nodes
		= CreateDictionary ({ { "r",
								  CreateDictionary ({ { "id", CreateByteString (AsView (nodeID)) },
									  { "nodes", CreateByteString (AsView (records.data (), records.size ())) } }) },
			{ "t", CreateByteString ("tt07") }, { "y", CreateByteString ("r") } });
	const auto nodesFrame = DecodeDHTFrame (nodes);
	assert (nodesFrame.kind == DHTFrameKind::Response);
	assert (nodesFrame.response.size () > records.size ());

	// A reply may carry peers and referral nodes at once; the decoder
	// returns the whole dictionary and the handler uses each key it needs.
	const auto both
		= CreateDictionary ({ { "r",
								  CreateDictionary ({ { "id", CreateByteString (AsView (nodeID)) },
									  { "nodes", CreateByteString (AsView (records.data (), records.size ())) },
									  { "token", CreateByteString (token) },
									  { "values", CreateList ({ CreateByteString (AsView (peer)) }) } }) },
			{ "t", CreateByteString ("tt08") }, { "y", CreateByteString ("r") } });
	const auto bothFrame = DecodeDHTFrame (both);
	assert (bothFrame.kind == DHTFrameKind::Response);
	assert (bothFrame.response.find ("5:nodes") != std::string_view::npos);
	assert (bothFrame.response.find ("6:values") != std::string_view::npos);
}

void TestFrameErrors ()
{
	const auto error = std::string ("d1:eli201e3:msge1:t1:a1:y1:ee");
	const auto frame = DecodeDHTFrame (error);
	assert (frame.kind == DHTFrameKind::Error);
	assert (frame.transaction == "a");
	assert (frame.errorCode == 201);
	assert (frame.errorText == "msg");
	assert (frame.arguments.empty () && frame.response.empty () && frame.query.empty ());

	const auto reply = DecodeDHTErrorReply (error);
	assert (reply.has_value ());
	assert (reply->transactionID == "a" && reply->code == 201);

	// Negative and out-of-range codes.
	assert (DecodeDHTFrame ("d1:eli-1e0:e1:t1:a1:y1:ee").errorCode == -1);
	assert (DecodeDHTFrame ("d1:eli2147483648e0:e1:t1:a1:y1:ee").kind == DHTFrameKind::Malformed);
	// A three-entry list, a missing text and an oversized text.
	assert (DecodeDHTFrame ("d1:eli201e3:msgi1ee1:t1:a1:y1:ee").kind == DHTFrameKind::Malformed);
	assert (DecodeDHTFrame ("d1:eli201ee1:t1:a1:y1:ee").kind == DHTFrameKind::Malformed);
	const std::string oversized = "d1:eli201e" + std::to_string (MAX_DHT_ERROR_TEXT + 1) + ":"
		+ std::string (MAX_DHT_ERROR_TEXT + 1, 'x') + "e1:t1:a1:y1:ee";
	assert (DecodeDHTFrame (oversized).kind == DHTFrameKind::Malformed);
	// An error reply must not also carry a query, arguments or a response.
	assert (DecodeDHTFrame ("d1:eli201e0:e1:q4:ping1:t1:a1:y1:ee").kind == DHTFrameKind::Malformed);
	assert (DecodeDHTFrame ("d1:ade1:eli201e0:e1:t1:a1:y1:ee").kind == DHTFrameKind::Malformed);
	assert (DecodeDHTFrame ("d1:eli201e0:e1:rde1:t1:a1:y1:ee").kind == DHTFrameKind::Malformed);
	// A well-formed response is not an error reply.
	assert (!DecodeDHTErrorReply ("d1:y1:r1:t1:a1:rde").has_value ());
}

void TestFrameMalformed ()
{
	const NodeID nodeID { };
	const auto ping = PingQuery (nodeID, "tt01");

	// Every strict prefix must fail: a truncated frame never decodes and
	// cannot be mistaken for a complete query.
	for (size_t len = 1; len < ping.size (); len++)
		assert (DecodeDHTFrame (std::string_view (ping).substr (0, len)).kind == DHTFrameKind::Malformed);
	assert (DecodeDHTFrame (ping).kind == DHTFrameKind::Query);

	assert (DecodeDHTFrame ("").kind == DHTFrameKind::Malformed);
	assert (DecodeDHTFrame ("e").kind == DHTFrameKind::Malformed);
	assert (DecodeDHTFrame ("de").kind == DHTFrameKind::Malformed); // no y/t
	assert (DecodeDHTFrame (std::string (MAX_DHT_KRPC_FRAME + 1, 'd')).kind == DHTFrameKind::Malformed);

	// Duplicate keys are rejected even when both values are well formed.
	assert (DecodeDHTFrame ("d1:y1:q1:y1:qe").kind == DHTFrameKind::Malformed);
	assert (DecodeDHTFrame ("d1:t1:a1:t1:b1:y1:qe").kind == DHTFrameKind::Malformed);
	assert (DecodeDHTFrame ("d1:ade1:ade1:q4:ping1:t1:a1:y1:qe").kind == DHTFrameKind::Malformed);

	// Wrong, missing, mixed or oversized framing fields.
	assert (DecodeDHTFrame ("d1:t1:a1:y1:xe").kind == DHTFrameKind::Malformed); // unknown y
	assert (DecodeDHTFrame ("d1:t1:a1:y12:qqe").kind == DHTFrameKind::Malformed); // y is two bytes
	assert (DecodeDHTFrame ("d1:t1:a1:y1:q1:z1:ze").kind == DHTFrameKind::Malformed); // unknown key
	assert (DecodeDHTFrame ("d1:ade1:q4:ping1:t1:a1:v1:x1:z1:ze").kind == DHTFrameKind::Malformed);
	assert (DecodeDHTFrame ("d1:q4:ping1:t1:a1:y1:qe").kind == DHTFrameKind::Malformed); // missing a
	assert (DecodeDHTFrame ("d1:ae1:q4:ping1:t1:a1:y1:qe").kind == DHTFrameKind::Malformed); // a not a dict
	assert (DecodeDHTFrame ("d1:ade1:t1:a1:y1:qe").kind == DHTFrameKind::Malformed); // missing q
	assert (DecodeDHTFrame ("d1:q4:ping1:t1:a1:y1:re").kind == DHTFrameKind::Malformed); // missing r
	assert (DecodeDHTFrame ("d1:ade1:q4:ping1:rde1:t1:a1:y1:qe").kind == DHTFrameKind::Malformed); // a and r
	assert (DecodeDHTFrame ("d1:ade1:q4:ping1:t1:a1:y1:re").kind == DHTFrameKind::Malformed); // q with r

	// An empty transaction ID is invalid; more than 16 bytes is too.
	assert (DecodeDHTFrame ("d1:ade1:q4:ping1:t0:1:y1:qe").kind == DHTFrameKind::Malformed);
	const std::string longTransaction (MAX_DHT_TRANSACTION_ID + 1, 'x');
	assert (DecodeDHTFrame (
				"d1:ade1:q4:ping1:t" + std::to_string (longTransaction.size ()) + ":" + longTransaction + "1:y1:qe")
				.kind
		== DHTFrameKind::Malformed);
	// The maximum accepted transaction length still decodes.
	const std::string maxTransaction (MAX_DHT_TRANSACTION_ID, 'x');
	const auto maxFrame = DecodeDHTFrame (
		"d1:ade1:q4:ping1:t" + std::to_string (maxTransaction.size ()) + ":" + maxTransaction + "1:y1:qe");
	assert (maxFrame.kind == DHTFrameKind::Query && maxFrame.transaction == maxTransaction);

	// Trailing or leading bytes are not part of the frame.
	assert (DecodeDHTFrame (ping + "x").kind == DHTFrameKind::Malformed);
	assert (DecodeDHTFrame ("x" + ping).kind == DHTFrameKind::Malformed);
}

void TestCompactDecoders ()
{
	const auto peer = MakePeer (0x51);
	std::vector<uint8_t> records;
	for (uint8_t i = 0; i < 4; i++)
	{
		const auto record = Node (MakeNodeID (peer, 0x2000 + i, i), peer, 0x2000 + i).GetNodeInfo ();
		records.insert (records.end (), record.begin (), record.end ());
	}
	// A record whose node ID does not bind its destination and port is
	// dropped, never repaired.
	NodeInfo broken { };
	std::memset (broken.data (), 0x7e, broken.size ());
	records.insert (records.end (), broken.begin (), broken.end ());

	std::vector<Node> nodes;
	assert (DecodeCompactNodes (AsView (records.data (), records.size ()), nodes) == 4);
	assert (nodes.front ().port == 0x2000 && nodes.back ().port == 0x2003);
	// Work is bounded by the requested count as well as by the input.
	assert (DecodeCompactNodes (AsView (records.data (), records.size ()), nodes, 2) == 2);
	assert (nodes.size () == 2);
	// A trailing partial record is ignored.
	assert (DecodeCompactNodes (AsView (records.data (), records.size () - 3), nodes) == 4);
	assert (DecodeCompactNodes ("", nodes) == 0 && nodes.empty ());

	i2p::data::IdentHash zero;
	std::memset (zero (), 0, zero.len);
	std::vector<std::string_view> values;
	values.push_back (AsView (peer));
	values.push_back ("short");
	values.push_back (AsView (zero));
	values.push_back (AsView (peer));
	std::vector<i2p::data::IdentHash> peers;
	assert (DecodeCompactPeers (values, peers) == 2);
	assert (peers.size () == 2 && peers[0] == peer && peers[1] == peer);
	// The cap counts entries, including the ones it drops, so padding the
	// list with junk cannot stretch the scan.
	assert (DecodeCompactPeers (values, peers, 2) == 1);
	const auto encoded = CreateList ({ CreateByteString (AsView (peer)), CreateByteString ("short") });
	assert (DecodeCompactPeers (encoded, peers) == 1);
	assert (peers.size () == 1 && peers[0] == peer);
	assert (DecodeCompactPeers ("not-a-list", peers) == 0 && peers.empty ());
	assert (DecodeCompactPeers (std::string (encoded) + "x", peers) == 0 && peers.empty ());
}

void TestTokenStoreBinding ()
{
	DHTTokenStore store;
	const auto peer = MakePeer (0x61);
	const auto nodeID = MakeNodeID (peer, 0x3000, 0x01);
	const auto infoHash = MakeInfoHash (0x62);
	const uint16_t port = 0x3000;
	const uint64_t issued = 1000;

	const auto entry = MakeToken (0x01, nodeID, peer, infoHash, port, issued);
	assert (entry.token.size () == 8);
	assert (store.Issue (entry) == DHTTokenStore::Admission::Issued);
	assert (store.Size () == 1 && store.SizeForNode (nodeID) == 1);
	assert (store.Contains (entry.token));

	DHTIncomingToken out;
	assert (store.Validate (entry.token, nodeID, infoHash, port, issued, out) == DHTTokenStore::Validation::Valid);
	assert (out.peer == peer && out.port == port && out.infoHash == infoHash);
	assert (out.token == entry.token);

	// The store does not consume a token on validation: bounded replay
	// accounting stays in DHTQueryLimiter, exactly as before.
	assert (store.Validate (entry.token, nodeID, infoHash, port, issued + 1, out) == DHTTokenStore::Validation::Valid);

	// Wrong node, wrong port and wrong hash are rejected without dropping
	// the entry, so the legitimate announce still succeeds afterwards.
	assert (store.Validate (entry.token, MakeNodeID (peer, port, 0x09), infoHash, port, issued, out)
		== DHTTokenStore::Validation::WrongNode);
	assert (
		store.Validate (entry.token, nodeID, infoHash, port + 1, issued, out) == DHTTokenStore::Validation::WrongPort);
	assert (store.Validate (entry.token, nodeID, MakeInfoHash (0x63), port, issued, out)
		== DHTTokenStore::Validation::WrongInfoHash);
	assert (store.Validate (entry.token, nodeID, infoHash, port, issued, out) == DHTTokenStore::Validation::Valid);

	// Malformed token/port and an unknown token are rejected.
	assert (store.Validate ("short", nodeID, infoHash, port, issued, out) == DHTTokenStore::Validation::Invalid);
	assert (store.Validate (entry.token, nodeID, infoHash, 0, issued, out) == DHTTokenStore::Validation::Invalid);
	assert (store.Validate (entry.token, nodeID, infoHash, 65535, issued, out) == DHTTokenStore::Validation::Invalid);
	assert (store.Validate (std::string (8, 'z'), nodeID, infoHash, port, issued, out)
		== DHTTokenStore::Validation::Unknown);
	assert (store.Issue (MakeToken (0x02, nodeID, peer, infoHash, 0, issued)) == DHTTokenStore::Admission::Rejected);

	// Re-issuing the same eight-byte token asks the caller for a new one.
	assert (store.Issue (entry) == DHTTokenStore::Admission::AlreadyPresent);

	// Refresh extends the lifetime, and expiry removes the entry.
	assert (store.Refresh (entry.token, issued + 500));
	assert (
		store.Validate (entry.token, nodeID, infoHash, port, issued + 1099, out) == DHTTokenStore::Validation::Valid);
	assert (
		store.Validate (entry.token, nodeID, infoHash, port, issued + 1101, out) == DHTTokenStore::Validation::Expired);
	assert (store.Size () == 0 && !store.Contains (entry.token));
	assert (store.Validate (entry.token, nodeID, infoHash, port, issued, out) == DHTTokenStore::Validation::Unknown);
	assert (!store.Refresh (entry.token, issued + 10));
}

void TestTokenStoreQuotaAndCapacity ()
{
	DHTTokenStore store;
	const auto peer = MakePeer (0x71);
	const auto infoHash = MakeInfoHash (0x72);
	std::vector<NodeID> nodes;
	for (uint8_t i = 0; i < 3; i++) nodes.push_back (MakeNodeID (peer, 0x4000 + i, i));

	// Per-node quota: 16 live tokens per node, independent of each other.
	for (uint8_t i = 0; i < DHTTokenStore::MaxTokensPerNode; i++)
		assert (
			store.Issue (MakeToken (i, nodes[0], peer, infoHash, 0x4000, 100 + i)) == DHTTokenStore::Admission::Issued);
	for (uint8_t i = 0; i < DHTTokenStore::MaxTokensPerNode; i++)
		assert (
			store.Issue (MakeToken (i, nodes[1], peer, infoHash, 0x4001, 200 + i)) == DHTTokenStore::Admission::Issued);
	assert (store.Size () == 2 * DHTTokenStore::MaxTokensPerNode);
	assert (store.SizeForNode (nodes[0]) == DHTTokenStore::MaxTokensPerNode);

	// Node 0 issues one more: only its own oldest token is reclaimed, so
	// node 1 keeps every token it holds.
	assert (store.Issue (MakeToken (0x20, nodes[0], peer, infoHash, 0x4000, 300))
		== DHTTokenStore::Admission::ReplacedOwnOldest);
	assert (store.SizeForNode (nodes[0]) == DHTTokenStore::MaxTokensPerNode);
	assert (store.SizeForNode (nodes[1]) == DHTTokenStore::MaxTokensPerNode);
	const auto node0Oldest = MakeToken (0x00, nodes[0], peer, infoHash, 0x4000, 0);
	const auto node1Oldest = MakeToken (0x00, nodes[1], peer, infoHash, 0x4001, 0);
	assert (!store.Contains (node0Oldest.token));
	assert (store.Contains (node1Oldest.token));

	// Global capacity: the oldest remaining token is evicted and the store
	// never grows past MaxTokens.
	DHTTokenStore full;
	const size_t nodesToFill = DHTTokenStore::MaxTokens / DHTTokenStore::MaxTokensPerNode;
	size_t issued = 0;
	for (size_t n = 0; n < nodesToFill; n++)
	{
		const auto node = MakeNodeID (peer, static_cast<uint16_t> (n + 1), static_cast<uint8_t> (n));
		for (size_t i = 0; i < DHTTokenStore::MaxTokensPerNode; i++)
		{
			assert (full.Issue (MakeToken (
						static_cast<uint8_t> (i), node, peer, infoHash, static_cast<uint16_t> (n + 1), 500 + issued))
				== DHTTokenStore::Admission::Issued);
			++issued;
		}
	}
	assert (full.Size () == DHTTokenStore::MaxTokens);
	const auto oldestNode = MakeNodeID (peer, 1, 0);
	const auto oldest = MakeToken (0x00, oldestNode, peer, infoHash, 1, 500);
	assert (full.Contains (oldest.token));
	assert (full.Issue (MakeToken (0x30, nodes[2], peer, infoHash, 0x4002, 9000))
		== DHTTokenStore::Admission::EvictedOldest);
	assert (full.Size () == DHTTokenStore::MaxTokens);
	assert (!full.Contains (oldest.token));
	assert (full.SizeForNode (oldestNode) == DHTTokenStore::MaxTokensPerNode - 1);

	// The sweep ages out everything older than the lifetime, keeps the
	// newest entry and leaves both indexes consistent.
	assert (full.Expire (9000) == DHTTokenStore::MaxTokens - 1);
	assert (full.Size () == 1);
	assert (full.SizeForNode (nodes[2]) == 1);
	full.Clear ();
	assert (full.Size () == 0 && full.SizeForNode (nodes[2]) == 0);
}

void TestTokenStoreRemoveInfoHash ()
{
	DHTTokenStore store;
	const auto peer = MakePeer (0x81);
	const auto node = MakeNodeID (peer, 0x5000, 0x01);
	const auto kept = MakeInfoHash (0x82);
	const auto dropped = MakeInfoHash (0x83);
	for (uint8_t i = 0; i < 4; i++)
		assert (store.Issue (MakeToken (i, node, peer, i < 2 ? dropped : kept, 0x5000, 100 + i))
			== DHTTokenStore::Admission::Issued);
	assert (store.Size () == 4);
	store.RemoveInfoHash (dropped);
	assert (store.Size () == 2 && store.SizeForNode (node) == 2);
	DHTIncomingToken out;
	const auto droppedToken = MakeToken (0, node, peer, dropped, 0x5000, 0);
	const auto keptToken = MakeToken (2, node, peer, kept, 0x5000, 0);
	assert (store.Validate (droppedToken.token, node, dropped, 0x5000, 100, out) == DHTTokenStore::Validation::Unknown);
	assert (store.Validate (keptToken.token, node, kept, 0x5000, 100, out) == DHTTokenStore::Validation::Valid);
	store.RemoveInfoHash (MakeInfoHash (0x84)); // absent hash is a no-op
	assert (store.Size () == 2);
}

void TestRawFrameLimiter ()
{
	DHTRawFrameLimiter limiter;
	assert (!limiter.HasWindow ());
	const uint64_t start = 1000;
	for (size_t i = 0; i < DHTRawFrameLimiter::MaxFrames; i++) assert (limiter.Allow (start));
	assert (limiter.GetCount () == DHTRawFrameLimiter::MaxFrames);
	assert (!limiter.Allow (start)); // the boundary itself is refused
	assert (!limiter.Allow (start + DHTRawFrameLimiter::Window - 1));
	assert (limiter.GetCount () == DHTRawFrameLimiter::MaxFrames);

	// A monotonic-clock regression must not refill an exhausted window.
	assert (!limiter.Allow (start - 500));
	assert (!limiter.Allow (0));
	assert (limiter.GetWindowStart () == start);
	assert (limiter.GetCount () == DHTRawFrameLimiter::MaxFrames);

	// A full window later starts a fresh budget.
	assert (limiter.Allow (start + DHTRawFrameLimiter::Window));
	assert (limiter.GetWindowStart () == start + DHTRawFrameLimiter::Window);
	assert (limiter.GetCount () == 1);
}

void TestRoutingPerPeerCap ()
{
	const auto ourNode = MakeNodeID (MakePeer (0x91), 0x6000, 0x00);
	RoutingTable table (ourNode);
	const auto peer = MakePeer (0x91);
	const auto other = MakePeer (0x92);

	std::vector<std::shared_ptr<Node>> accepted;
	for (uint8_t i = 0; i < MAX_DHT_NODES_PER_PEER; i++)
	{
		auto node = std::make_shared<Node> (MakeNodeID (peer, 0x6001, i), peer, 0x6001);
		assert (node->VerifyID ());
		accepted.push_back (table.AddNode (node));
		assert (accepted.back () == node);
	}
	assert (table.GetNumNodes () == MAX_DHT_NODES_PER_PEER);
	assert (table.CountPeerNodes (peer) == MAX_DHT_NODES_PER_PEER);

	// The newcomer is rejected; an existing entry is never evicted for it.
	auto extra = std::make_shared<Node> (MakeNodeID (peer, 0x6001, 0x0f), peer, 0x6001);
	assert (extra->VerifyID ());
	assert (table.AddNode (extra) == nullptr);
	assert (table.GetNumNodes () == MAX_DHT_NODES_PER_PEER);
	assert (table.CountPeerNodes (peer) == MAX_DHT_NODES_PER_PEER);
	assert (table.GetNodes ().size () == MAX_DHT_NODES_PER_PEER);
	assert (table.FindNodeByPeer (peer) != nullptr);

	// Another destination still gets its own entries.
	auto second = std::make_shared<Node> (MakeNodeID (other, 0x6002, 0x00), other, 0x6002);
	assert (second->VerifyID ());
	assert (table.AddNode (second) == second);
	assert (table.CountPeerNodes (other) == 1);
	assert (table.GetNumNodes () == MAX_DHT_NODES_PER_PEER + 1);

	// Our own node ID is never stored.
	assert (table.AddNode (std::make_shared<Node> (ourNode, MakePeer (0x93), 0x6000)) == nullptr);

	auto found = table.FindNodeByPeer (other);
	assert (found && found->id == second->id);
	table.RemoveNodeByPeer (other);
	assert (table.CountPeerNodes (other) == 0);
	// The rejected peer is still capped until one of its own entries goes.
	assert (table.AddNode (extra) == nullptr);
	table.RemoveNodeByPeer (peer);
	assert (table.CountPeerNodes (peer) == 0);
	assert (table.AddNode (extra) == extra);
	assert (table.CountPeerNodes (peer) == 1);
}

void TestClosestNodesOrdering ()
{
	// Global XOR selection is unchanged: ascending distance order, capped
	// by the requested count, never returning our own ID.
	const auto ourNode = MakeNodeID (MakePeer (0xa1), 0x7000, 0x00);
	RoutingTable table (ourNode);
	std::vector<std::shared_ptr<Node>> nodes;
	for (uint8_t i = 0; i < 5; i++)
	{
		const auto peer = MakePeer (static_cast<uint8_t> (0xa2 + i));
		auto node = std::make_shared<Node> (MakeNodeID (peer, 0x7001 + i, i), peer, 0x7001 + i);
		assert (node->VerifyID ());
		assert (table.AddNode (node) == node);
		nodes.push_back (node);
	}
	const auto target = MakeInfoHash (0xb0);
	auto closest = table.GetClosestNodes (target, nodes.size ());
	assert (closest.size () == nodes.size ());
	for (size_t i = 1; i < closest.size (); i++) assert ((closest[i - 1]->id ^ target) <= (closest[i]->id ^ target));
	auto limited = table.GetClosestNodes (target, 2);
	assert (limited.size () == 2 && limited[0] == closest[0] && limited[1] == closest[1]);
	// The excluded node is skipped.
	auto excluded = table.GetClosestNodes (target, 1, &closest[0]->id);
	assert (excluded.size () == 1 && excluded[0] == closest[1]);
}

void TestPortSafety ()
{
	assert (IsValidDHTPort (DHT_MIN_PORT));
	assert (IsValidDHTPort (DHT_MAX_PORT));
	assert (!IsValidDHTPort (0));
	assert (!IsValidDHTPort (65535));
	assert (!IsValidDHTPort (70000));
	assert (ClampDHTPort (0) == DHT_MIN_PORT);
	assert (ClampDHTPort (65535) == DHT_MAX_PORT);
	assert (ClampDHTPort (1234) == 1234);
	// Every accepted port keeps the raw port in 1..65535.
	for (uint32_t port = 0; port <= 65535; port++)
	{
		const uint32_t clamped = ClampDHTPort (static_cast<uint16_t> (port));
		assert (clamped >= 1 && clamped <= 65534);
		assert (clamped + 1 >= 1 && clamped + 1 <= 65535);
	}

	TorrentsTunnel tunnel ("dht-protocol-test", nullptr, "/unused-authority", std::string_view { }, false);
	TorrentsDHT refused (tunnel, 65535, std::string_view { });
	assert (refused.GetPort () == DHT_MAX_PORT);
	assert (refused.GetRPort () == 65535);
	TorrentsDHT zero (tunnel, 0, std::string_view { });
	assert (zero.GetPort () == DHT_MIN_PORT && zero.GetRPort () == 2);
	TorrentsDHT normal (tunnel, 1234, std::string_view { });
	assert (normal.GetPort () == 1234 && normal.GetRPort () == 1235);
	assert (normal.GetNumQueuedDatagrams () == 0);
	assert (normal.GetNumIncomingTokens () == 0);
}

void TestStateRecords ()
{
	const auto peer = MakePeer (0xc1);
	const auto record = Node (MakeNodeID (peer, 0x8001, 0x00), peer, 0x8001).GetNodeInfo ();
	std::vector<uint8_t> bytes (record.begin (), record.end ());
	const auto single = DecodeDHTStateRecords (bytes);
	assert (single.has_value () && *single == 1);
	bytes.insert (bytes.end (), record.begin (), record.end ());
	const auto pair = DecodeDHTStateRecords (bytes);
	assert (pair.has_value () && *pair == 2);
	assert (!DecodeDHTStateRecords (bytes, 1).has_value ());
	// An empty, misaligned, oversized or zero-bounded buffer is rejected
	// before anything walks it.
	assert (!DecodeDHTStateRecords (std::span<const uint8_t> { }).has_value ());
	bytes.push_back (0);
	assert (!DecodeDHTStateRecords (bytes).has_value ());
	bytes.resize (record.size ());
	const auto restored = DecodeDHTStateRecords (bytes);
	assert (restored.has_value () && *restored == 1);
	assert (!DecodeDHTStateRecords (bytes, 0).has_value ());
	const std::vector<uint8_t> oversized (TorrentsDHT::MaxStateBytes + NodeInfo { }.size (), 0);
	assert (!DecodeDHTStateRecords (oversized).has_value ());
}
} // namespace

int main ()
{
	TestFrameQueries ();
	TestFrameResponses ();
	TestFrameErrors ();
	TestFrameMalformed ();
	TestCompactDecoders ();
	TestTokenStoreBinding ();
	TestTokenStoreQuotaAndCapacity ();
	TestTokenStoreRemoveInfoHash ();
	TestRawFrameLimiter ();
	TestRoutingPerPeerCap ();
	TestClosestNodesOrdering ();
	TestPortSafety ();
	TestStateRecords ();
	return 0;
}

#else

int main ()
{
	return 0;
}

#endif
