/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: fuzz torrents KRPC.
 *
 * Feeds arbitrary bytes to the side-effect free KRPC frame decoder, the compact
 * node/peer decoders, the state-record validator, the issued-token store and
 * the anonymous raw-frame budget. The target asserts that every output is
 * bounded and that no decoder throws or reads outside its input; it needs no
 * destination, socket or live DHT node.
 */

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "TorrentsDHT.h"

#ifndef NO_TORRENTS

extern "C" int LLVMFuzzerTestOneInput (const uint8_t * data, size_t size)
{
	using namespace i2p::torrents;
	std::string_view input (reinterpret_cast<const char *> (data), size);

	// Every returned view must point into the input: the decoder never copies
	// and never manufactures bytes.
	const auto inRange = [&input] (std::string_view view)
	{
		if (view.empty ()) return true;
		return view.data () >= input.data () && view.data () + view.size () <= input.data () + input.size ();
	};

	const auto frame = DecodeDHTFrame (input);
	assert (inRange (frame.transaction) && inRange (frame.query) && inRange (frame.arguments)
		&& inRange (frame.response) && inRange (frame.errorText));
	assert (frame.transaction.size () <= MAX_DHT_TRANSACTION_ID);
	assert (frame.query.size () <= MAX_DHT_QUERY_NAME);
	assert (frame.errorText.size () <= MAX_DHT_ERROR_TEXT);
	switch (frame.kind)
	{
	case DHTFrameKind::Malformed:
		assert (!frame.IsValid ());
		break;
	case DHTFrameKind::Query:
		assert (!frame.query.empty ());
		assert (frame.arguments.front () == 'd');
		assert (ParseDictionary (frame.arguments) == frame.arguments.size ());
		break;
	case DHTFrameKind::Response:
		assert (frame.response.front () == 'd');
		assert (ParseDictionary (frame.response) == frame.response.size ());
		break;
	case DHTFrameKind::Error:
	{
		// The compatibility accessor agrees with the frame decoder.
		const auto reply = DecodeDHTErrorReply (input);
		assert (reply.has_value ());
		assert (reply->transactionID == frame.transaction && reply->code == frame.errorCode);
		break;
	}
	}
	assert (!GetKRPCQueryKind (frame.query).has_value () || frame.kind == DHTFrameKind::Query);

	// Compact node decoding is bounded by the input and by the k cap, and every
	// record it returns already passed the node-ID binding check.
	std::vector<Node> nodes;
	assert (DecodeCompactNodes (input, nodes) == nodes.size ());
	assert (nodes.size () <= MAX_DHT_NODES_PER_RESPONSE);
	assert (nodes.size () <= size / DHT_COMPACT_NODE_SIZE);
	for (const auto & node : nodes) assert (node.VerifyID ());

	// Compact peer decoding drops wrong-length and all-zero entries and never
	// exceeds the k cap.
	std::vector<i2p::data::IdentHash> peers;
	assert (DecodeCompactPeers (input, peers) == peers.size ());
	assert (peers.size () <= MAX_DHT_VALUES_PER_RESPONSE);
	for (const auto & peer : peers) assert (!peer.IsZero ());

	// The flat routing-state validator accepts only whole 54-byte records.
	const auto records = DecodeDHTStateRecords (std::span<const uint8_t> (data, size));
	if (records)
	{
		assert (*records <= MAX_DHT_LOADED_NODES);
		assert (size % (NodeID::len + i2p::data::IdentHash::len + 2) == 0);
	}

	// The token store stays bounded whatever the inserted material looks like.
	DHTTokenStore store;
	DHTIncomingToken entry;
	entry.token.assign (8, '\0');
	entry.nodeID = NodeID { };
	entry.peer = i2p::data::IdentHash { };
	entry.infoHash = Torrent::InfoHash { };
	entry.port = 1;
	entry.issued = 2000000000;
	if (size >= 8) std::memcpy (entry.token.data (), data, 8);
	if (size >= 10) entry.port = static_cast<uint16_t> (data[8] | (data[9] << 8));
	if (size >= 42) std::memcpy (entry.infoHash.data (), data + 10, entry.infoHash.size ());
	if (size >= 74) std::memcpy (entry.nodeID.data (), data + 42, entry.nodeID.size ());
	if (size >= 106) std::memcpy (entry.peer (), data + 74, entry.peer.len);

	(void)store.Issue (entry);
	assert (store.Size () <= DHTTokenStore::MaxTokens);
	assert (store.SizeForNode (entry.nodeID) <= DHTTokenStore::MaxTokensPerNode);

	DHTIncomingToken validated;
	const auto validation = store.Validate (
		entry.token, entry.nodeID, entry.infoHash, entry.port, entry.issued, validated);
	assert (validation == DHTTokenStore::Validation::Valid
		|| validation == DHTTokenStore::Validation::Invalid
		|| validation == DHTTokenStore::Validation::Unknown);
	(void)store.Refresh (entry.token, entry.issued);
	store.RemoveInfoHash (entry.infoHash);
	assert (store.Size () == 0);
	(void)store.Expire (entry.issued + DHTTokenStore::TokenLifetime + 1);
	assert (store.Size () == 0);
	store.Clear ();

	// The anonymous raw budget never admits more than its fixed window, and a
	// clock regression cannot refill it.
	DHTRawFrameLimiter limiter;
	uint64_t now = 12345;
	if (size >= 8) std::memcpy (&now, data, sizeof (now));
	size_t allowed = 0;
	for (size_t i = 0; i <= DHTRawFrameLimiter::MaxFrames; i++)
		if (limiter.Allow (now)) ++allowed;
	assert (allowed == DHTRawFrameLimiter::MaxFrames);
	assert (limiter.GetCount () <= DHTRawFrameLimiter::MaxFrames);
	// A smaller monotonic value never opens a new window.
	if (now > 0) assert (!limiter.Allow (now - 1));

	return 0;
}

#else

extern "C" int LLVMFuzzerTestOneInput (const uint8_t *, size_t)
{
	return 0;
}

#endif
