/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TorrentsDHT.
 *
 * Implements a Kademlia-style routing table and KRPC ping/find_node/get_peers/
 * announce_peer over repliable and raw I2P datagrams, with token-authenticated
 * announces, exploratory lookups and a bounded query rate limiter. The node
 * identity and routing table are persisted and expire on timers.
 *
 */

#ifndef NO_TORRENTS

#include <openssl/rand.h>
#include <string.h>
#include <vector>
#include <fstream>
#include <iterator>
#include <limits>
#include <utility>
#include "I2PEndian.h"
#include "Timestamp.h"
#include "FS.h"
#include "TorrentsTunnel.h"
#include "TorrentsDHT.h"

namespace i2p
{
namespace torrents
{
	Node::Node (const NodeInfo & nodeInfo)
		: lastUpdateTime (i2p::util::GetMonotonicSeconds ())
	{
		memcpy (id.data (), nodeInfo.data (), id.size ());
		memcpy ((uint8_t *)peer, nodeInfo.data () + id.size (), peer.len);
		port = bufbe16toh (nodeInfo.data () + nodeInfo.size () - 2);
	}

	NodeInfo Node::GetNodeInfo () const
	{
		NodeInfo nodeInfo;
		memcpy (nodeInfo.data (), id.data (), id.size ());
		memcpy (nodeInfo.data () + id.size (), peer, peer.len);
		htobe16buf (nodeInfo.data () + nodeInfo.size () - 2, port);
		return nodeInfo;
	}

	bool Node::VerifyID () const
	{
		return port > 0 && port < 65535 && !memcmp (id.data (), peer, 4)
			&& (((uint16_t)(id[4] ^ peer[4]) << 8) | (id[5] ^ peer[5])) == port;
	}

	std::optional<NodeID> Bucket::GetMiddleID () const
	{
		const int bit = std::max (start.FindLowestBit (), next ? next->start.FindLowestBit () : -1) + 1;
		if (bit < 0 || bit >= static_cast<int> (NodeID::len * 8)) return { };
		NodeID middleID = start;
		middleID[bit >> 3] |= (0x80 >> (bit & 0x07));
		return { middleID };
	}

	NodeID Bucket::GetRandomID (std::mt19937 & rng) const
	{
		const int bit = std::max (start.FindLowestBit (), next ? next->start.FindLowestBit () : -1) + 1;
		if (bit < 0 || bit >= static_cast<int> (NodeID::len * 8)) return start;

		NodeID randomID;
		auto d = div (bit, 8);
		memcpy (randomID.data (), start.data (), d.quot);
		randomID[d.quot] = (start[d.quot] & (0xFF00 >> d.rem)) | (rng () & (0xFF >> d.rem));
		for (size_t i = d.quot + 1; i < NodeID::len; i += 4)
		{
			uint32_t r = rng ();
			if (i + 4 <= NodeID::len)
				memcpy (randomID.data () + i, &r, 4);
			else
				memcpy (randomID.data () + i, &r, NodeID::len - i);
		}
		return randomID;
	}

	bool Bucket::Split ()
	{
		auto middleID = GetMiddleID ();
		if (!middleID) return false;
		auto newBucket = std::make_unique<Bucket> (*middleID);
		// move some nodes before publishing the new link, so a failure leaves the chain unchanged
		auto it = nodes.begin ();
		while (it != nodes.end ())
		{
			if (it->first < *middleID)
				it++; // stay in old bucket
			else
				// move to new bucket
				newBucket->nodes.insert (nodes.extract (it++));
		}
		newBucket->next = std::move (next);
		next = std::move (newBucket);
		return true;
	}

	RoutingTable::RoutingTable (const NodeID & ourNode)
		: m_Buckets (std::make_unique<Bucket> ())
		, m_OurNode (ourNode)
	{
	}

	RoutingTable::~RoutingTable () = default;

	void RoutingTable::CleanUp ()
	{
		if (!m_Buckets) return;
		m_Buckets->next.reset (); // recursively destroys the rest of the chain
		m_Buckets->nodes.clear ();
	}

	size_t RoutingTable::GetNumBuckets () const
	{
		size_t num = 0;
		auto bucket = m_Buckets.get ();
		while (bucket)
		{
			num++;
			bucket = bucket->next.get ();
		}
		return num;
	}

	size_t RoutingTable::GetNumNodes () const
	{
		size_t num = 0;
		auto bucket = m_Buckets.get ();
		while (bucket)
		{
			num += bucket->nodes.size ();
			bucket = bucket->next.get ();
		}
		return num;
	}

	std::list<std::shared_ptr<Node>> RoutingTable::GetNodes ()
	{
		std::list<std::shared_ptr<Node>> nodes;
		auto bucket = m_Buckets.get ();
		while (bucket)
		{
			for (auto it : bucket->nodes) nodes.emplace_back (it.second);
			bucket = bucket->next.get ();
		}
		return nodes;
	}

	std::vector<std::shared_ptr<Node>> RoutingTable::GetClosestNodes (
		const Torrent::InfoHash & target, size_t count, const NodeID * exclude) const
	{
		std::vector<std::shared_ptr<Node>> closest;
		count = std::min (count, MAX_DHT_REQUEST_NODES);
		if (!count) return closest;
		const auto nearer
			= [&target] (const auto & left, const auto & right) { return (left->id ^ target) < (right->id ^ target); };
		for (auto bucket = m_Buckets.get (); bucket; bucket = bucket->next.get ())
			for (const auto & [id, node] : bucket->nodes)
			{
				if (id == m_OurNode || (exclude && id == *exclude)) continue;
				auto position = std::lower_bound (closest.begin (), closest.end (), node, nearer);
				if (closest.size () == count && position == closest.end ()) continue;
				closest.insert (position, node);
				if (closest.size () > count) closest.pop_back ();
			}
		return closest;
	}

	Bucket * RoutingTable::FindBucket (const Torrent::InfoHash & id) const
	{
		if (!m_Buckets) return nullptr;
		auto bucket = m_Buckets.get ();
		while (bucket->next)
		{
			if (id < bucket->next->start) break;
			bucket = bucket->next.get ();
		}
		return bucket;
	}

	size_t RoutingTable::DeleteExpiredNodes (uint64_t ts)
	{
		size_t numDeleted = 0;
		Bucket *bucket = m_Buckets.get (), *prev = nullptr;
		while (bucket)
		{
			if (ts > bucket->lastUpdateTime + DHT_BUCKET_EXPIRATION_THRESHOLD)
			{
				auto it = bucket->nodes.begin ();
				while (it != bucket->nodes.end ())
				{
					if (ts > it->second->lastUpdateTime + DHT_NODE_EXPIRATION_TIME)
					{
						numDeleted++;
						it = bucket->nodes.erase (it);
					}
					else
						it++;
				}
			}
			if (bucket->IsEmpty ())
			{
				if (!bucket->next && prev) // last bucket
				{
					LogPrint (eLogDebug, "TorrentsDHT: last bucket deleted");
					prev->next.reset (); // `bucket` is owned by prev and is destroyed here
					break;
				}
				if (!prev && bucket->next) // first bucket
				{
					LogPrint (eLogDebug, "TorrentsDHT: first bucket deleted");
					auto newHead = std::move (bucket->next);
					newHead->start.fill (0);
					m_Buckets = std::move (newHead); // destroys the old head
					bucket = m_Buckets.get ();
					continue;
				}
			}
			prev = bucket;
			bucket = bucket->next.get ();
		}
		return numDeleted;
	}

	std::list<std::shared_ptr<Node>> RoutingTable::GetNodesToPing (uint64_t ts)
	{
		std::list<std::shared_ptr<Node>> toPing;
		auto bucket = m_Buckets.get ();
		while (bucket)
		{
			if (ts > bucket->lastUpdateTime + DHT_BUCKET_EXPIRATION_THRESHOLD)
			{
				for (const auto & it : bucket->nodes)
					if (ts > it.second->lastUpdateTime + DHT_NODE_SEND_PING_TIME) toPing.push_back (it.second);
			}
			bucket = bucket->next.get ();
		}
		return toPing;
	}

	std::shared_ptr<Node> RoutingTable::AddNode (std::shared_ptr<Node> node)
	{
		if (!node) return nullptr;
		if (node->id == m_OurNode) return nullptr;
		auto bucket = FindBucket (node->id);
		if (!bucket) return nullptr;
		auto it = bucket->nodes.find (node->id);
		if (it != bucket->nodes.end ())
		{
			auto ts = i2p::util::GetMonotonicSeconds ();
			it->second->lastUpdateTime = ts;
			bucket->lastUpdateTime = ts;
			return it->second;
		}
		// A destination mints its own node ID, and only six of the 20 ID bytes
		// are bound to it, so one peer can present many valid NIDs and fill
		// several buckets. Enforce the per-peer cap by rejecting the newcomer;
		// an existing entry is never evicted on its behalf.
		if (CountPeerNodes (node->peer) >= MAX_DHT_NODES_PER_PEER) return nullptr;
		if (bucket->IsFull ())
		{
			do
			{
				if (!bucket->IsInBucket (m_OurNode)) return nullptr;
				if (!bucket->Split ()) return nullptr;
				bucket = FindBucket (node->id);
			} while (bucket->IsFull ());
		}
		if (bucket)
		{
			bucket->nodes.emplace (node->id, node);
			bucket->lastUpdateTime = i2p::util::GetMonotonicSeconds ();
		}
		return node;
	}

	size_t RoutingTable::CountPeerNodes (const i2p::data::IdentHash & peer) const
	{
		size_t count = 0;
		for (auto bucket = m_Buckets.get (); bucket; bucket = bucket->next.get ())
			for (const auto & entry : bucket->nodes)
				if (entry.second && entry.second->peer == peer) ++count;
		return count;
	}

	void RoutingTable::RemoveNode (const NodeID & id)
	{
		auto bucket = FindBucket (id);
		if (!bucket) return;
		bucket->nodes.erase (id);
	}

	void RoutingTable::RemoveNodeByPeer (const i2p::data::IdentHash & peer)
	{
		for (auto bucket = m_Buckets.get (); bucket; bucket = bucket->next.get ())
			for (auto it = bucket->nodes.begin (); it != bucket->nodes.end ();)
			{
				if (it->second && it->second->peer == peer)
					it = bucket->nodes.erase (it);
				else
					++it;
			}
	}

	std::shared_ptr<Node> RoutingTable::FindNodeByPeer (const i2p::data::IdentHash & peer) const
	{
		for (auto bucket = m_Buckets.get (); bucket; bucket = bucket->next.get ())
			for (const auto & entry : bucket->nodes)
				if (entry.second && entry.second->peer == peer) return entry.second;
		return nullptr;
	}

	std::list<std::pair<NodeID, Bucket *>> RoutingTable::GetExploratoryTargets (std::mt19937 & rng, uint64_t ts)
	{
		std::list<std::pair<NodeID, Bucket *>> ret;
		auto bucket = m_Buckets.get ();
		while (bucket)
		{
			if (bucket->IsFull ())
			{
				// delete one node about to expire
				for (auto it = bucket->nodes.begin (); it != bucket->nodes.end (); it++)
					if (ts > it->second->lastUpdateTime + DHT_NODE_SEND_PING_TIME)
					{
						bucket->nodes.erase (it);
						break;
					}
			}
			if (!bucket->IsFull () || bucket->IsInBucket (m_OurNode))
				ret.emplace_back (std::make_pair (bucket->GetRandomID (rng), bucket));
			bucket = bucket->next.get ();
		}
		return ret;
	}

	void DHTQueryLimiter::Expire (uint64_t now)
	{
		std::erase_if (m_Origins, [now] (const auto & entry)
			{ return now >= entry.second.first && now - entry.second.first >= DHT_QUERY_RATE_WINDOW; });
	}

	bool DHTQueryLimiter::Allow (const i2p::data::IdentHash & origin, uint64_t now)
	{
		auto it = m_Origins.find (origin);
		if (it == m_Origins.end ())
		{
			if (m_Origins.size () >= MAX_DHT_QUERY_ORIGINS) Expire (now);
			if (m_Origins.size () >= MAX_DHT_QUERY_ORIGINS) return false;
			m_Origins.emplace (origin, std::make_pair (now, size_t { 1 }));
			return true;
		}
		auto & [started, count] = it->second;
		if (now >= started && now - started >= DHT_QUERY_RATE_WINDOW)
		{
			started = now;
			count = 0;
		}
		if (count >= MAX_DHT_QUERIES_PER_ORIGIN) return false;
		++count;
		return true;
	}

	PrivateInfoHashSet::Admission PrivateInfoHashSet::Insert (const Torrent::InfoHash & infoHash)
	{
		if (m_Hashes.contains (infoHash)) return Admission::AlreadyKnown;
		Admission result = Admission::Inserted;
		if (m_Hashes.size () >= MaxHashes && !m_Order.empty ())
		{
			// Evict only the oldest admission; the set must stay bounded even
			// when an operator keeps adding private torrents.
			m_Hashes.erase (m_Order.front ());
			m_Order.pop_front ();
			result = Admission::EvictedOldest;
		}
		m_Hashes.insert (infoHash);
		m_Order.push_back (infoHash);
		return result;
	}

	bool PrivateInfoHashSet::Erase (const Torrent::InfoHash & infoHash)
	{
		if (!m_Hashes.erase (infoHash)) return false;
		std::erase (m_Order, infoHash);
		return true;
	}

	void DHTTokenStore::Erase (Order::iterator it)
	{
		m_Tokens.erase (it->token);
		auto node = m_ByNode.find (it->nodeID);
		if (node != m_ByNode.end ())
		{
			std::erase (node->second, it);
			if (node->second.empty ()) m_ByNode.erase (node);
		}
		m_Order.erase (it);
	}

	DHTTokenStore::Admission DHTTokenStore::Issue (const DHTIncomingToken & entry)
	{
		if (!IsValidToken (entry.token) || !IsValidPort (entry.port)) return Admission::Rejected;
		if (m_Tokens.contains (entry.token)) return Admission::AlreadyPresent;
		Admission result = Admission::Issued;
		// Per-node quota first: reclaim only this node's own oldest token, so a
		// destination that keeps issuing get_peers cannot flush every other
		// destination's tokens. Re-look-up each round because Erase may drop an
		// emptied per-node bucket.
		while (true)
		{
			auto node = m_ByNode.find (entry.nodeID);
			if (node == m_ByNode.end () || node->second.size () < MaxTokensPerNode) break;
			Erase (node->second.front ());
			result = Admission::ReplacedOwnOldest;
		}
		// Then the global capacity: the FIFO front is the oldest issue.
		while (m_Tokens.size () >= MaxTokens && !m_Order.empty ())
		{
			Erase (m_Order.begin ());
			result = Admission::EvictedOldest;
		}
		if (m_Tokens.size () >= MaxTokens) return Admission::Rejected;
		m_Order.push_back (entry);
		const auto last = std::prev (m_Order.end ());
		m_Tokens.emplace (entry.token, last);
		m_ByNode[entry.nodeID].push_back (last);
		return result;
	}

	DHTTokenStore::Validation DHTTokenStore::Validate (std::string_view token, const NodeID & nodeID,
		const Torrent::InfoHash & infoHash, uint16_t port, uint64_t now, DHTIncomingToken & out)
	{
		if (!IsValidToken (token) || !IsValidPort (port)) return Validation::Invalid;
		auto it = m_Tokens.find (GetPeersToken (token));
		if (it == m_Tokens.end ()) return Validation::Unknown;
		const auto entry = it->second;
		// Age is inclusive like the previous table: a token issued exactly
		// TokenLifetime seconds ago is still usable. A clock regression keeps
		// the token usable instead of extending or truncating its window.
		if (now > entry->issued && now - entry->issued > TokenLifetime)
		{
			Erase (entry);
			return Validation::Expired;
		}
		if (entry->infoHash != infoHash) return Validation::WrongInfoHash;
		if (entry->nodeID != nodeID) return Validation::WrongNode;
		if (entry->port != port) return Validation::WrongPort;
		out = *entry;
		return Validation::Valid;
	}

	bool DHTTokenStore::Refresh (std::string_view token, uint64_t now)
	{
		auto it = m_Tokens.find (GetPeersToken (token));
		if (it == m_Tokens.end ()) return false;
		it->second->issued = now;
		return true;
	}

	bool DHTTokenStore::Contains (std::string_view token) const
	{
		return m_Tokens.contains (GetPeersToken (token));
	}

	size_t DHTTokenStore::SizeForNode (const NodeID & nodeID) const
	{
		auto it = m_ByNode.find (nodeID);
		return it == m_ByNode.end () ? 0 : it->second.size ();
	}

	void DHTTokenStore::RemoveInfoHash (const Torrent::InfoHash & infoHash)
	{
		for (auto it = m_Order.begin (); it != m_Order.end ();)
		{
			if (it->infoHash == infoHash)
			{
				const auto next = std::next (it);
				Erase (it);
				it = next;
			}
			else
				++it;
		}
	}

	size_t DHTTokenStore::Expire (uint64_t now)
	{
		size_t expired = 0;
		for (auto it = m_Order.begin (); it != m_Order.end ();)
		{
			if (now > it->issued && now - it->issued > TokenLifetime)
			{
				const auto next = std::next (it);
				Erase (it);
				++expired;
				it = next;
			}
			else
				++it;
		}
		return expired;
	}

	void DHTTokenStore::Clear ()
	{
		m_Tokens.clear ();
		m_ByNode.clear ();
		m_Order.clear ();
	}

	bool DHTRawFrameLimiter::Allow (uint64_t now)
	{
		if (!m_HasWindow)
		{
			m_WindowStart = now;
			m_Count = 0;
			m_HasWindow = true;
		}
		else if (now >= m_WindowStart && now - m_WindowStart >= Window)
		{
			m_WindowStart = now;
			m_Count = 0;
		}
		// A clock regression (now < m_WindowStart) keeps the current window and
		// its count, so a skewed clock cannot refill an exhausted budget.
		if (m_Count >= MaxFrames) return false;
		++m_Count;
		return true;
	}

	std::string DHTTorrent::GetBEncodedPeers (bool noSeeds, const i2p::data::IdentHash * exclude) const
	{
		// Serve only fresh peers, freshest first, and cap the response. A
		// single get_peers query must not return the whole 128-entry map.
		const auto now = i2p::util::GetMonotonicSeconds ();
		std::vector<std::pair<uint64_t, i2p::data::IdentHash>> candidates;
		candidates.reserve (m_Peers.size ());
		for (const auto & [peer, state] : m_Peers)
			if ((!exclude || peer != *exclude) && IsFresh (now, state.first, state.second, noSeeds))
				candidates.emplace_back (state.first, peer);
		std::sort (candidates.begin (), candidates.end (),
			[] (const auto & left, const auto & right) { return left.first > right.first; });
		if (candidates.size () > MAX_DHT_VALUES_PER_RESPONSE) candidates.resize (MAX_DHT_VALUES_PER_RESPONSE);
		std::vector<std::string> peers;
		peers.reserve (candidates.size ());
		for (const auto & [time, peer] : candidates)
			peers.emplace_back (CreateByteString (std::string_view ((const char *)peer.data (), peer.len)));
		return CreateList (peers);
	}

	bool DHTTorrent::HasPeers (bool noSeeds) const
	{
		const auto now = i2p::util::GetMonotonicSeconds ();
		return std::any_of (m_Peers.begin (), m_Peers.end (), [this, now, noSeeds] (const auto & entry)
			{ return IsFresh (now, entry.second.first, entry.second.second, noSeeds); });
	}

	std::unordered_set<i2p::data::IdentHash> DHTTorrent::GetPeers () const
	{
		const auto now = i2p::util::GetMonotonicSeconds ();
		std::unordered_set<i2p::data::IdentHash> peers;
		peers.reserve (m_Peers.size ());
		for (const auto & [peer, state] : m_Peers)
			if (IsFresh (now, state.first, state.second, false)) peers.insert (peer);
		return peers;
	}

	bool DHTTorrent::AddPeer (const i2p::data::IdentHash & peer, bool seed, bool local)
	{
		auto ts = i2p::util::GetMonotonicSeconds ();
		auto it = m_Peers.find (peer);
		if (it != m_Peers.end ())
		{
			it->second = std::make_pair (ts, seed);
			return false;
		}
		const size_t limit = MAX_DHT_PEERS_PER_TORRENT + (local ? 1 : 0);
		if (m_Peers.size () >= limit) return false;
		m_Peers.emplace (peer, std::make_pair (ts, seed));
		return true;
	}

	bool DHTTorrent::RemovePeer (const i2p::data::IdentHash & peer)
	{
		return m_Peers.erase (peer) > 0;
	}

	bool DHTTorrent::CleanUp (uint64_t ts)
	{
		auto it = m_Peers.begin ();
		while (it != m_Peers.end ())
		{
			if (ts > it->second.first + DHT_TORRENT_PEER_EXPIRATION_TIME)
				it = m_Peers.erase (it);
			else
				it++;
		}
		return m_Peers.empty ();
	}

	bool RequestInfo::AddNode (std::shared_ptr<Node> node)
	{
		if (!node || tried.contains (node->id)) return false;
		if (nodesToRequest.size () >= MAX_DHT_REQUEST_NODES) return false;
		return nodesToRequest.emplace (torrent ? node->id ^ torrent->GetInfoHash () : node->id ^ target, node).second;
	}

	bool RequestInfo::AddNodeToken (std::shared_ptr<Node> node, GetPeersToken token)
	{
		if (!node || token.size () > 64 || !torrent) return false;
		if (tokens.size () >= MAX_DHT_REQUEST_TOKENS) return false;
		return tokens.emplace (node->id ^ torrent->GetInfoHash (), node, token).second;
	}

	std::shared_ptr<Node> RequestInfo::GetNextNode ()
	{
		if (IsDone () || nodesToRequest.empty ()) return nullptr;
		auto node = nodesToRequest.begin ()->second;
		nodesToRequest.erase (nodesToRequest.begin ());
		numAttempts++;
		tried.emplace (node->id);
		lastNode = node;
		return node;
	}

	TorrentsDHT::TorrentsDHT (TorrentsTunnel & tunnel, uint16_t port, std::string_view dhtFileName)
		: m_Tunnel (tunnel)
		, m_Port (ClampDHTPort (port))
		, m_DHTFileName (dhtFileName)
		, m_DHTUpdateCheckTimer (tunnel.GetService ())
		, m_DHTExpirationCheckTimer (tunnel.GetService ())
		, m_DHTSendPingCheckTimer (tunnel.GetService ())
		, m_DHTQueryExpirationCheckTimer (tunnel.GetService ())
		, m_DHTIncomingDrainTimer (tunnel.GetService ())
		, m_DHTStateSaveTimer (tunnel.GetService ())
		, m_NextDHTExploratoryTime (i2p::util::GetMonotonicSeconds () + DHT_INITIAL_EXPLORATORY_INTERVAL)
	{
		auto dest = tunnel.GetLocalDestination ();
		if (dest)
		{
			memcpy (m_NodeID.data (), dest->GetIdentHash (), 6);
			m_NodeID[4] ^= (m_Port >> 8);
			m_NodeID[5] ^= (m_Port & 0xFF);
			RAND_bytes (m_NodeID.data () + 6, static_cast<int> (m_NodeID.size () - 6));
			m_RoutingTable = std::make_unique<RoutingTable> (m_NodeID);
		}
		else
			m_NodeID.fill (0);
	}

	TorrentsDHT::~TorrentsDHT ()
	{
		// A timer callback that already reached its turn or a disk-domain save
		// that has not run yet must fail closed instead of entering
		// half-destroyed state.
		m_ServiceState->stopping.store (true, std::memory_order_release);
		m_DHTUpdateCheckTimer.cancel ();
		m_DHTExpirationCheckTimer.cancel ();
		m_DHTSendPingCheckTimer.cancel ();
		m_DHTQueryExpirationCheckTimer.cancel ();
		m_DHTIncomingDrainTimer.cancel ();
		m_DHTStateSaveTimer.cancel ();
		m_DatagramReceiver.Reset ();
		{
			std::lock_guard<std::mutex> lock (m_IncomingMutex);
			m_Incoming.clear ();
		}
	}

	void TorrentsDHT::Start ()
	{
		auto dest = m_Tunnel.GetLocalDestination ();
		if (dest && m_DHTFileName.empty ()) m_DHTFileName = dest->GetIdentHash ().ToBase32 () + ".dht";
		if (m_DHTFileName.empty ()) m_DHTFileName = "nodest.dht";
		if (!m_DHTFileName.empty () && m_DHTFileName[0] && IsValidStateFileName (m_DHTFileName))
		{
			// Load before publishing the receiver: an inbound query must not
			// race replacement of the routing table or identity/port.
			Load (GetDHTFilePath (m_DHTFileName));
		}
		else if (!m_DHTFileName.empty () && m_DHTFileName[0])
			LogPrint (eLogError, "TorrentsDHT: Refusing unsafe DHT state filename");
		m_ServiceState->stopping.store (false, std::memory_order_release);
		// A fresh generation starts with no write in flight; a stale one could
		// not have completed because Stop() marks `stopping` first.
		m_ServiceState->saveInFlight.store (false, std::memory_order_release);
		if (dest)
		{
			auto dgramDest = dest->GetDatagramDestination ();
			if (dgramDest)
			{
				m_DatagramReceiver.Reset ();
				// The receiver runs on the inbound network thread. It only copies
				// the frame into the bounded hand-off queue; every routing, query
				// and torrent mutation happens in DrainIncomingDatagrams() on the
				// service thread. The shared state makes a late dispatch after
				// teardown fail closed.
				auto state = m_ServiceState;
				m_DatagramReceiver = dgramDest->SetReceiver (
					[this, state] (const i2p::data::IdentityEx & from, uint16_t fromPort, uint16_t toPort,
						const uint8_t * buf, size_t len, const i2p::util::Mapping * options)
					{
						if (state->stopping.load (std::memory_order_acquire)) return;
						EnqueueIncomingDatagram (from, fromPort, toPort, buf, len, options);
					});
			}
		}
		ScheduleDHTUpdateCheck ();
		ScheduleDHTExpirationCheck ();
		ScheduleDHTSendPingCheck ();
		ScheduleDHTQueryExpirationCheck ();
		ScheduleDHTIncomingDrain ();
		ScheduleDHTStateSave ();
	}

	void TorrentsDHT::Stop ()
	{
		m_ServiceState->stopping.store (true, std::memory_order_release);
		m_DHTUpdateCheckTimer.cancel ();
		m_DHTExpirationCheckTimer.cancel ();
		m_DHTSendPingCheckTimer.cancel ();
		m_DHTQueryExpirationCheckTimer.cancel ();
		m_DHTIncomingDrainTimer.cancel ();
		m_DHTStateSaveTimer.cancel ();
		// Reset waits for any callback already dispatched before this owner
		// can be destroyed, and its token cannot remove a newer registration.
		m_DatagramReceiver.Reset ();
		// Frames queued but not yet drained belong to the stopped
		// generation and must never be replayed into a later Start().
		{
			std::lock_guard<std::mutex> lock (m_IncomingMutex);
			m_Incoming.clear ();
		}
		// A periodic write that has not started yet sees `stopping` and returns
		// without writing, so the flag must not stay latched across a restart.
		m_ServiceState->saveInFlight.store (false, std::memory_order_release);
		// The final save stays synchronous: the caller stops the delegated
		// storage worker only after this returns, so no periodic save may
		// still be queued when that authority goes away.
		if (!m_DHTFileName.empty () && m_DHTFileName[0] && IsValidStateFileName (m_DHTFileName))
			Save (GetDHTFilePath (m_DHTFileName));
	}

	bool TorrentsDHT::IsValidStateFileName (std::string_view name)
	{
		if (name.empty () || name.size () > 255 || name == "." || name == ".." || name.front () == '.') return false;
		for (unsigned char c : name)
			if (c <= 0x20 || c == 0x7f || c == '/' || c == '\\' || c == ':') return false;
		return true;
	}

	std::filesystem::path TorrentsDHT::GetDHTFilePath (std::string_view filename) const
	{
		if (!IsValidStateFileName (filename)) return { };
		std::filesystem::path dhtFilePath (i2p::fs::GetDataDir ());
		dhtFilePath /= "torrents";
		if (!m_DelegatedPersistence && !std::filesystem::exists (dhtFilePath))
			std::filesystem::create_directories (dhtFilePath);
		dhtFilePath /= filename;
		return dhtFilePath;
	}

	bool TorrentsDHT::BuildStateSnapshot (std::vector<uint8_t> & bytes) const
	{
		bytes.clear ();
		if (!m_RoutingTable) return false;
		auto append = [&bytes] (const NodeInfo & node)
		{
			if (bytes.size () > MaxStateBytes - node.size ()) return false;
			bytes.insert (bytes.end (), node.begin (), node.end ());
			return true;
		};
		auto dest = m_Tunnel.GetLocalDestination ();
		if (dest) append (Node (m_NodeID, dest->GetIdentHash (), m_Port).GetNodeInfo ());
		for (const auto & node : m_RoutingTable->GetNodes ())
			if (!append (node->GetNodeInfo ()))
			{
				LogPrint (eLogError, "TorrentsDHT: State exceeds persistence limit");
				bytes.clear ();
				return false;
			}
		return true;
	}

	void TorrentsDHT::Save (const std::filesystem::path & file)
	{
		if (file.empty () || !IsValidStateFileName (file.filename ().string ())) return;
		std::vector<uint8_t> bytes;
		if (!BuildStateSnapshot (bytes)) return;
		if (m_DelegatedPersistence)
		{
			std::string error;
			if (!m_SaveState || !m_SaveState (file, bytes, error))
				LogPrint (eLogError, "TorrentsDHT: Delegated save failed: ", error);
			return;
		}
		std::string error;
		if (!WriteFileAtomicNoFollow (file.parent_path (), file.filename (), bytes.data (), bytes.size (), error))
			LogPrint (eLogError, "TorrentsDHT: Can't write state file: ", error);
	}

	void TorrentsDHT::Load (const std::filesystem::path & file)
	{
		if (file.empty () || !IsValidStateFileName (file.filename ().string ())) return;
		std::vector<uint8_t> bytes;
		if (m_DelegatedPersistence)
		{
			std::string error;
			if (!m_LoadState || !m_LoadState (file, bytes, error))
			{
				LogPrint (eLogWarning, "TorrentsDHT: Delegated load failed: ", error);
				return;
			}
		}
		else
		{
			std::ifstream f (file, std::ifstream::binary | std::ifstream::ate);
			if (!f.is_open ()) return;
			const auto size = f.tellg ();
			if (size < 0 || static_cast<uint64_t> (size) > MaxStateBytes) return;
			bytes.resize (static_cast<size_t> (size));
			f.seekg (0);
			if (!bytes.empty () && !f.read (reinterpret_cast<char *> (bytes.data ()), bytes.size ())) return;
		}
		// Keep the existing flat 54-byte NodeInfo format; reject partial files
		// before changing identity or routing state.
		if (!DecodeDHTStateRecords (bytes)) return;
		size_t offset = 0;
		NodeInfo first;
		std::copy_n (bytes.data (), first.size (), first.data ());
		auto dest = m_Tunnel.GetLocalDestination ();
		if (dest)
		{
			Node node (first);
			// A persisted self record must carry a usable node ID and a port
			// whose repliable successor is in 1..65535; 0 and 65535 are
			// refused rather than adopted.
			if (node.VerifyID () && node.peer == dest->GetIdentHash () && IsValidDHTPort (node.port))
			{
				m_NodeID = node.id;
				m_Port = node.port;
				offset = first.size ();
			}
		}
		auto table = std::make_unique<RoutingTable> (m_NodeID);
		size_t records = 0;
		for (; offset < bytes.size () && records++ < MAX_DHT_LOADED_NODES; offset += first.size ())
		{
			NodeInfo info;
			std::copy_n (bytes.data () + offset, info.size (), info.data ());
			auto node = std::make_shared<Node> (info);
			if (node->VerifyID ()) table->AddNode (node);
		}
		m_RoutingTable = std::move (table);
		LogPrint (eLogInfo, "TorrentsDHT: ", m_RoutingTable->GetNumNodes (), " DHT nodes loaded to ",
			m_RoutingTable->GetNumBuckets (), " buckets");
	}

	std::optional<KRPCQuery> GetKRPCQueryKind (std::string_view name)
	{
		for (size_t i = 0; i < KRPCQueryStr.size (); i++)
			if (KRPCQueryStr[i] == name) return static_cast<KRPCQuery> (i);
		return std::nullopt;
	}

	namespace
	{
		// Length of the single bencoded value at the front of `value`, or zero
		// when the value is malformed. Used so the envelope handler returns the
		// consumed length instead of asking ParseDictionary to reparse.
		size_t GetEncodedValueLength (std::string_view value)
		{
			if (value.empty ()) return 0;
			switch (value.front ())
			{
			case 'i': return ExtractInteger (value).second;
			case 'l': return ParseList (value);
			case 'd': return ParseDictionary (value);
			default: return ExtractByteString (value).second;
			}
		}

		// KRPC `e` is [integer code, byte-string text]. The text is bounded and
		// informational only; it is never logged.
		bool DecodeKRPCError (std::string_view encoded, int32_t & code, std::string_view & text)
		{
			size_t count = 0;
			bool valid = true;
			const auto length = ParseList (encoded,
				[&] (std::string_view entry) -> size_t
				{
					++count;
					if (count == 1)
					{
						auto [value, size] = ExtractInteger (entry);
						if (!size || value < std::numeric_limits<int32_t>::min ()
							|| value > std::numeric_limits<int32_t>::max ())
							valid = false;
						else
							code = static_cast<int32_t> (value);
						return size;
					}
					if (count == 2)
					{
						auto [message, size] = ExtractByteString (entry);
						if (!size || message.size () > MAX_DHT_ERROR_TEXT)
							valid = false;
						else
							text = message;
						return size;
					}
					valid = false;
					return 0;
				});
			return valid && count == 2 && length && length == encoded.size ();
		}
	}

	DHTFrame DecodeDHTFrame (std::string_view encoded)
	{
		DHTFrame frame;
		if (encoded.empty () || encoded.size () > MAX_DHT_KRPC_FRAME) return frame;

		bool malformed = false, hasType = false, hasTransaction = false;
		std::string_view type, transaction, query, arguments, response, error;
		// ParseDictionary rejects duplicate keys on its own and reports a
		// malformed value as zero, which fails the whole frame below. The
		// envelope is closed: an unknown top-level key cannot be interpreted,
		// so it is rejected instead of silently skipped. BEP 5's optional `v`
		// client version is the only extension key and is accepted unread.
		const auto consumed = ParseDictionary (encoded,
			[&] (std::string_view key, std::string_view value) -> size_t
			{
				const auto length = GetEncodedValueLength (value);
				if (!length)
				{
					malformed = true;
					return 0;
				}
				const auto view = value.substr (0, length);
				if (key == "y")
				{
					hasType = true;
					type = view;
				}
				else if (key == "t")
				{
					hasTransaction = true;
					transaction = view;
				}
				else if (key == "q")
					query = view;
				else if (key == "a")
					arguments = view;
				else if (key == "r")
					response = view;
				else if (key == "e")
					error = view;
				else if (key != "v")
					malformed = true;
				return length;
			});
		if (!consumed || consumed != encoded.size () || malformed || !hasType || !hasTransaction) return frame;

		auto [typeValue, typeLength] = ExtractByteString (type);
		if (!typeLength || typeLength != type.size () || typeValue.size () != 1) return frame;
		auto [transactionValue, transactionLength] = ExtractByteString (transaction);
		if (!transactionLength || transactionLength != transaction.size () || transactionValue.empty ()
			|| transactionValue.size () > MAX_DHT_TRANSACTION_ID)
			return frame;
		frame.transaction = transactionValue;

		switch (typeValue[0])
		{
		case 'q':
		{
			if (query.empty () || !response.empty () || !error.empty ()) return frame;
			auto [queryValue, queryLength] = ExtractByteString (query);
			if (!queryLength || queryLength != query.size () || queryValue.empty ()
				|| queryValue.size () > MAX_DHT_QUERY_NAME)
				return frame;
			// A query must carry exactly one well-formed argument dictionary.
			if (arguments.empty () || arguments.front () != 'd') return frame;
			const auto argumentsLength = ParseDictionary (arguments);
			if (!argumentsLength || argumentsLength != arguments.size ()) return frame;
			frame.kind = DHTFrameKind::Query;
			frame.query = queryValue;
			frame.arguments = arguments;
			return frame;
		}
		case 'r':
		{
			if (response.empty () || !arguments.empty () || !query.empty () || !error.empty ()) return frame;
			if (response.front () != 'd') return frame;
			const auto responseLength = ParseDictionary (response);
			if (!responseLength || responseLength != response.size ()) return frame;
			frame.kind = DHTFrameKind::Response;
			frame.response = response;
			return frame;
		}
		case 'e':
		{
			if (error.empty () || !arguments.empty () || !response.empty () || !query.empty ()) return frame;
			if (!DecodeKRPCError (error, frame.errorCode, frame.errorText)) return frame;
			frame.kind = DHTFrameKind::Error;
			return frame;
		}
		default: return frame;
		}
		return frame; // unreachable: every case above returns
	}

	std::optional<DHTErrorReply> DecodeDHTErrorReply (std::string_view encoded)
	{
		// One authority for the envelope; the error reply view must match the
		// full frame, including the absence of a/r/q and a bounded text.
		const auto frame = DecodeDHTFrame (encoded);
		if (frame.kind != DHTFrameKind::Error) return std::nullopt;
		return DHTErrorReply { GetPeersToken (frame.transaction), frame.errorCode };
	}

	size_t DecodeCompactNodes (std::string_view nodes, std::vector<Node> & out, size_t maxCount)
	{
		out.clear ();
		NodeInfo info;
		size_t processed = 0;
		while (nodes.size () >= info.size () && processed < maxCount)
		{
			memcpy (info.data (), nodes.data (), info.size ());
			Node node (info);
			// A record whose node ID does not bind its destination and port is
			// skipped, never repaired.
			if (node.VerifyID ()) out.emplace_back (node);
			nodes.remove_prefix (info.size ());
			++processed;
		}
		return out.size ();
	}

	size_t DecodeCompactPeers (
		const std::vector<std::string_view> & peers, std::vector<i2p::data::IdentHash> & out, size_t maxCount)
	{
		out.clear ();
		size_t count = 0;
		for (const auto peer : peers)
		{
			if (count >= maxCount) break;
			++count;
			if (peer.size () != i2p::data::IdentHash::len) continue;
			i2p::data::IdentHash hash (reinterpret_cast<const uint8_t *> (peer.data ()));
			if (hash.IsZero ()) continue;
			out.push_back (hash);
		}
		return out.size ();
	}

	size_t DecodeCompactPeers (std::string_view encodedList, std::vector<i2p::data::IdentHash> & out, size_t maxCount)
	{
		auto [peers, length] = ParseStringList (encodedList);
		if (!length || length != encodedList.size ())
		{
			out.clear ();
			return 0;
		}
		return DecodeCompactPeers (peers, out, maxCount);
	}

	std::optional<size_t> DecodeDHTStateRecords (std::span<const uint8_t> bytes, size_t maxRecords)
	{
		if (bytes.empty () || bytes.size () > TorrentsDHT::MaxStateBytes) return std::nullopt;
		if (bytes.size () % NodeInfo { }.size ()) return std::nullopt;
		const auto records = bytes.size () / NodeInfo { }.size ();
		if (!maxRecords || records > maxRecords) return std::nullopt;
		return records;
	}

	void TorrentsDHT::HandleErrorReply (const DHTErrorReply & error, uint16_t fromPort)
	{
		uint64_t transaction = 0;
		if (error.transactionID.size () != sizeof (transaction)) return;
		memcpy (&transaction, error.transactionID.data (), sizeof (transaction));
		auto it = m_Queries.find (transaction);
		if (it == m_Queries.end ()) return;
		const auto query = std::get<2> (it->second);
		const auto expectedPort = std::get<1> (it->second);
		const auto queryPort = query == eKRPCQueryAnnouncePeer && expectedPort > 0
			? static_cast<uint16_t> (expectedPort - 1)
			: expectedPort;
		if (fromPort != queryPort) return;
		// Extract before any continuation can insert a new query and rehash.
		// A protocol error completes the request, but is not a successful
		// ping/announce and must not promote routing or heard-from state.
		auto completed = m_Queries.extract (it);
		const auto & target = std::get<0> (completed.mapped ());
		// An error answer means the peer cannot serve this query right now, so
		// record it in the same bounded failure map a timeout uses before the
		// lookup advances to the next candidate.
		if (m_FailedPeers.contains (target) || m_FailedPeers.size () < MAX_DHT_FAILED_PEERS)
			m_FailedPeers.insert_or_assign (target, i2p::util::GetMonotonicSeconds ());
		auto info = std::get<3> (completed.mapped ());
		LogPrint (eLogDebug, "TorrentsDHT: KRPC error code ", error.code);
		if (query == eKRPCQueryGetPeers)
			SendNextGetPeersQuery (info);
		else if (query == eKRPCQueryFindNode)
			SendNextFindNodeQuery (info);
	}

	void TorrentsDHT::HandleRawDatagram (const uint8_t * buf, size_t len, uint16_t fromPort, uint16_t toPort)
	{
		if (!buf || !len || len > MAX_DHT_KRPC_FRAME || toPort != GetRPort () || !fromPort || fromPort == 65535) return;
		// A raw frame carries no authenticated sender. Charge one global fixed
		// window before parsing so an anonymous flood cannot buy unbounded
		// decode work; the budget never grows back on a clock regression.
		if (!m_RawFrameLimiter.Allow (i2p::util::GetMonotonicSeconds ())) return;

		const auto frame = DecodeDHTFrame (std::string_view (reinterpret_cast<const char *> (buf), len));
		switch (frame.kind)
		{
		case DHTFrameKind::Malformed:
			// Never reply to a frame that could not be decoded, and keep an
			// anonymous flood out of the info log.
			LogPrint (eLogDebug, "TorrentsDHT: Malformed raw datagram received");
			return;
		case DHTFrameKind::Error:
			HandleErrorReply (DHTErrorReply { GetPeersToken (frame.transaction), frame.errorCode }, fromPort);
			return;
		case DHTFrameKind::Response:
		{
			std::optional<GetPeersToken> token;
			bool hasValues = false;
			bool isMalformed = false;
			NodeID id { };
			bool hasID = false;
			std::string_view nodes;
			std::vector<std::string_view> values;
			// A response carries its node ID, an optional token and either
			// compact peer values, compact referral nodes, or both.
			const auto parsed = ParseDictionary (frame.response,
				[&id, &hasID, &values, &hasValues, &token, &isMalformed, &nodes] (
					std::string_view key, std::string_view buf) -> size_t
				{
					if (key == "id")
					{
						auto [l, success] = ParseByteArray (buf, id);
						hasID = success;
						if (!success) isMalformed = true;
						return l;
					}
					else if (key == "values")
					{
						auto [v, l] = ParseStringList (buf);
						if (l)
						{
							values = v;
							hasValues = true;
						}
						else
							isMalformed = true;
						return l;
					}
					else if (key == "nodes")
					{
						auto [value, l] = ExtractByteString (buf);
						if (l)
							nodes = value;
						else
							isMalformed = true;
						return l;
					}
					else if (key == "token")
					{
						auto [value, l] = ExtractByteString (buf);
						if (l && value.size () <= 64)
							token.emplace (value);
						else
							isMalformed = true;
						return l;
					}
					return 0;
				});
			if (isMalformed || !parsed || parsed != frame.response.size () || nodes.size () % DHT_COMPACT_NODE_SIZE != 0
				|| values.size () > MAX_DHT_REQUEST_NODES * 2 || !hasID)
			{
				LogPrint (eLogDebug, "TorrentsDHT: Malformed raw response received");
				return;
			}
			HandleResponse (frame.transaction, id, token, values, hasValues, nodes, fromPort);
			return;
		}
		case DHTFrameKind::Query:
		{
			// Only announce_peer may arrive raw: it is authenticated by its
			// token, and the raw port carries no sender identity.
			if (frame.query != "announce_peer")
			{
				LogPrint (eLogDebug, "TorrentsDHT: Query can't come as raw datagram");
				return;
			}
			bool isMalformed = false, seed = false;
			NodeID id { };
			bool hasID = false;
			Torrent::InfoHash infoHash { };
			bool hasInfoHash = false;
			std::optional<GetPeersToken> token;
			const auto parsed = ParseDictionary (frame.arguments,
				[&id, &hasID, &infoHash, &hasInfoHash, &isMalformed, &seed, &token] (
					std::string_view key, std::string_view buf) -> size_t
				{
					if (key == "id")
					{
						auto [l, success] = ParseByteArray (buf, id);
						hasID = success;
						if (!success) isMalformed = true;
						return l;
					}
					else if (key == "info_hash")
					{
						auto [l, success] = ParseByteArray (buf, infoHash);
						hasInfoHash = success;
						if (!success) isMalformed = true;
						return l;
					}
					else if (key == "seed")
					{
						auto [value, l] = ExtractInteger (buf);
						if (!l) isMalformed = true;
						seed = value == 1;
						return l;
					}
					else if (key == "token")
					{
						auto [value, l] = ExtractByteString (buf);
						if (l && value.size () <= 64)
							token.emplace (value);
						else
							isMalformed = true;
						return l;
					}
					return 0;
				});
			if (isMalformed || !parsed || parsed != frame.arguments.size () || !hasID)
			{
				LogPrint (eLogDebug, "TorrentsDHT: Malformed raw announce received");
				return;
			}
			if (!hasInfoHash || !token)
			{
				LogPrint (eLogDebug, "TorrentsDHT: announce_peer without info_hash");
				return;
			}
			HandleAnnouncePeer (frame.transaction, id, infoHash, *token, seed, fromPort);
			return;
		}
		}
	}

	void TorrentsDHT::HandleDatagram (const i2p::data::IdentityEx & from, uint16_t fromPort, uint16_t toPort,
		const uint8_t * buf, size_t len, const i2p::util::Mapping * /*options*/)
	{
		if (!buf || !len || len > MAX_DHT_KRPC_FRAME || toPort != m_Port) return;
		// Service thread only: the network thread copied this frame into the
		// hand-off queue without parsing it.
		const auto frame = DecodeDHTFrame (std::string_view (reinterpret_cast<const char *> (buf), len));
		if (frame.kind == DHTFrameKind::Malformed)
		{
			LogPrint (eLogDebug, "TorrentsDHT: Malformed datagram received");
			return;
		}
		if (frame.kind != DHTFrameKind::Query)
		{
			// A response or error on the repliable query port is not part of
			// this endpoint's protocol exchange.
			LogPrint (eLogDebug, "TorrentsDHT: Unexpected msg type on the query port");
			return;
		}

		// query
		bool isMalformed = false;
		NodeID id { }, target { };
		bool hasTarget = false, noSeeds = false;
		bool hasID = false;
		Torrent::InfoHash infoHash { };
		bool hasInfoHash = false;
		const auto parsed = ParseDictionary (frame.arguments,
			[&id, &hasID, &infoHash, &hasInfoHash, &isMalformed, &target, &hasTarget, &noSeeds] (
				std::string_view key, std::string_view buf) -> size_t
			{
				if (key == "id")
				{
					auto [l, success] = ParseByteArray (buf, id);
					hasID = success;
					if (!success) isMalformed = true;
					return l;
				}
				else if (key == "info_hash")
				{
					auto [l, success] = ParseByteArray (buf, infoHash);
					hasInfoHash = success;
					if (!success) isMalformed = true;
					return l;
				}
				else if (key == "noseed")
				{
					auto [value, l] = ExtractInteger (buf);
					if (!l) isMalformed = true;
					noSeeds = value == 1;
					return l;
				}
				else if (key == "target")
				{
					auto [l, success] = ParseByteArray (buf, target);
					hasTarget = success;
					if (!success) isMalformed = true;
					return l;
				}
				return 0;
			});
		if (isMalformed || !parsed || parsed != frame.arguments.size () || !hasID)
		{
			LogPrint (eLogDebug, "TorrentsDHT: Malformed datagram received");
			return;
		}
		const auto & query = frame.query;
		auto candidate = std::make_shared<Node> (id, from.GetIdentHash (), fromPort);
		if (!candidate->VerifyID () || !m_QueryLimiter.Allow (from.GetIdentHash (), i2p::util::GetMonotonicSeconds ()))
			return;
		// Routing-table admission is best effort. A valid query from a node
		// whose bucket is full must still receive a response.
		auto stored = UpdateNode (candidate);
		auto node = stored ? stored : candidate;
		UpdateHeardFrom (node->id);
		if (query == "ping")
			HandlePingQuery (from.GetIdentHash (), fromPort, frame.transaction, id);
		else if (query == "get_peers")
		{
			if (hasInfoHash)
				HandleGetPeersQuery (frame.transaction, node, infoHash, noSeeds);
			else
				LogPrint (eLogDebug, "TorrentsDHT: get_peers without info_hash");
		}
		else if (query == "find_node" && hasTarget)
			HandleFindNodeQuery (from.GetIdentHash (), fromPort, frame.transaction, target);
		else
			LogPrint (eLogDebug, "TorrentsDHT: Unexpected query ", query);
	}

	void TorrentsDHT::HandlePingQuery (const i2p::data::IdentHash & fromIdent, uint16_t fromPort,
		std::string_view transactionID, const NodeID & nodeID)
	{
		LogPrint (eLogDebug, "TorrentsDHT: Ping query msg received from ", nodeID.ToBase64 ());
		SendPingResponse (transactionID, fromIdent, fromPort + 1); // to rport
	}

	void TorrentsDHT::HandleGetPeersQuery (
		std::string_view transactionID, std::shared_ptr<Node> from, const Torrent::InfoHash & infoHash, bool noSeeds)
	{
		LogPrint (eLogDebug, "TorrentsDHT: Get peers query msg received from ", from->id.ToBase64 ());

		if (m_PrivateInfoHashes.Contains (infoHash))
		{
			// A locally private swarm must not be confirmed, served or tokenized
			// through the public DHT. Drop the query without a reply: an error or
			// an empty response would still disclose that this node knows it.
			LogPrint (eLogDebug, "TorrentsDHT: Ignoring get_peers for a locally private info hash");
			return;
		}

		// The token stays cryptographic: eight CSPRNG bytes, never derived from
		// the query or the node ID. The bounded store owns capacity, the
		// per-node quota and eviction.
		GetPeersToken token (8, '\0');
		const auto ts = i2p::util::GetMonotonicSeconds ();
		bool issued = false;
		for (int attempt = 0; attempt < 4 && !issued; attempt++)
		{
			if (RAND_bytes (reinterpret_cast<unsigned char *> (token.data ()), static_cast<int> (token.size ())) != 1) return;
			DHTIncomingToken entry;
			entry.token = token;
			entry.nodeID = from->id;
			entry.peer = from->peer;
			entry.infoHash = infoHash;
			entry.port = from->port;
			entry.issued = ts;
			const auto admission = m_IncomingTokens.Issue (entry);
			if (admission == DHTTokenStore::Admission::AlreadyPresent) continue;
			if (admission == DHTTokenStore::Admission::Rejected) return;
			issued = true;
		}
		if (!issued) return;

		auto it = m_Torrents.find (infoHash);
		if (it != m_Torrents.end () && it->second->HasPeers (noSeeds))
			SendGetPeersResponse (transactionID, it->second, token, from->peer, from->port + 1, noSeeds); // to rport
		else if (m_RoutingTable)
		{
			std::vector<uint8_t> nodes;
			for (const auto & node : m_RoutingTable->GetClosestNodes (infoHash, MAX_BUCKET_CAPACITY, &from->id))
			{
				auto compact = node->GetNodeInfo ();
				nodes.insert (nodes.end (), compact.begin (), compact.end ());
			}

			SendGetPeersResponse (transactionID,
				nodes.empty () ? std::string_view { } : std::string_view ((const char *)nodes.data (), nodes.size ()),
				token, from->peer, from->port + 1); // to rport
		}
	}

	void TorrentsDHT::HandleFindNodeQuery (const i2p::data::IdentHash & fromIdent, uint16_t fromPort,
		std::string_view transactionID, const NodeID & target)
	{
		LogPrint (eLogDebug, "TorrentsDHT: Find node query received");
		if (m_RoutingTable)
		{
			std::vector<uint8_t> nodes;
			for (const auto & node : m_RoutingTable->GetClosestNodes (target))
			{
				if (node->peer == fromIdent) continue;
				auto compact = node->GetNodeInfo ();
				nodes.insert (nodes.end (), compact.begin (), compact.end ());
				if (node->id == target) break; // exact known target needs no further referrals
			}
			SendFindNodeResponse (transactionID,
				nodes.empty () ? std::string_view { } : std::string_view ((const char *)nodes.data (), nodes.size ()),
				fromIdent, fromPort + 1); // empty table still receives a valid reply
		}
	}

	void TorrentsDHT::HandleAnnouncePeer (std::string_view transactionID, const NodeID & nodeID,
		const Torrent::InfoHash & infoHash, const GetPeersToken & token, bool seed, uint16_t fromPort)
	{
		LogPrint (eLogDebug, "TorrentsDHT: Announce peer received from ", nodeID.ToBase64 ());
		if (m_PrivateInfoHashes.Contains (infoHash))
		{
			// Members of a private swarm are only reachable through its trackers.
			// Refuse before the token table is consulted so no token, peer or
			// reply confirms local knowledge of the hash.
			LogPrint (eLogDebug, "TorrentsDHT: Ignoring announce_peer for a locally private info hash");
			return;
		}
		DHTIncomingToken entry;
		const auto ts = i2p::util::GetMonotonicSeconds ();
		switch (m_IncomingTokens.Validate (token, nodeID, infoHash, fromPort, ts, entry))
		{
		case DHTTokenStore::Validation::Valid: break;
		case DHTTokenStore::Validation::Expired: LogPrint (eLogInfo, "TorrentsDHT: Token for announce expired"); return;
		case DHTTokenStore::Validation::WrongInfoHash:
			LogPrint (eLogInfo, "TorrentsDHT: Token bound to another info_hash");
			return;
		case DHTTokenStore::Validation::Unknown:
			// An unauthenticated guess at the token; never a reply.
			LogPrint (eLogDebug, "TorrentsDHT: Token for announce not found");
			return;
		default:
			// Wrong node, wrong port or a malformed token: silently dropped.
			return;
		}
		if (!m_QueryLimiter.Allow (entry.peer, ts)) return;
		m_IncomingTokens.Refresh (token, ts); // bounded refresh for legitimate re-announcers
		auto torrentIt = m_Torrents.find (infoHash);
		if (torrentIt == m_Torrents.end ())
		{
			if (m_Torrents.size () >= MAX_DHT_TORRENTS)
			{
				LogPrint (eLogInfo, "TorrentsDHT: Too many announced torrents");
				return;
			}
			torrentIt = m_Torrents.emplace (infoHash, std::make_shared<DHTTorrent> ()).first;
		}
		torrentIt->second->AddPeer (entry.peer, seed);
		SendResponseMsg (
			CreateDictionary ({
				{ "id", CreateByteString (std::string_view ((const char *)m_NodeID.data (), m_NodeID.size ())) },
			}),
			transactionID, entry.peer, entry.port + 1); // to rport
	}

	void TorrentsDHT::HandleResponse (std::string_view transactionID, const NodeID & nodeID,
		std::optional<GetPeersToken> token, const std::vector<std::string_view> & values, bool hasValues,
		std::string_view nodes, uint16_t fromPort)
	{
		uint64_t t = 0;
		if (transactionID.size () != sizeof (t)) return;
		memcpy (&t, transactionID.data (), sizeof (t));
		auto it = m_Queries.find (t);
		if (it != m_Queries.end ())
		{
			const auto & [expectedIdent, expectedPort, expectedQuery, request, sent] = it->second;
			const uint16_t queryPort = expectedQuery == eKRPCQueryAnnouncePeer && expectedPort > 0
				? static_cast<uint16_t> (expectedPort - 1)
				: expectedPort;
			if (fromPort != queryPort || !Node (nodeID, expectedIdent, queryPort).VerifyID ()) return;
			if ((expectedQuery == eKRPCQueryGetPeers || expectedQuery == eKRPCQueryFindNode) && request
				&& request->lastNode && request->lastNode->id != nodeID)
				return;
			UpdateHeardFrom (nodeID);
			m_FailedPeers.erase (expectedIdent);
			// erase the completed transaction before dispatching it; the
			// handlers can send and insert a new query, which may rehash the
			// unordered_map and invalidate this iterator.
			auto node = m_Queries.extract (it);
			if (m_RoutingTable)
			{
				const auto & [ident, port, query, info, time] = node.mapped ();
				if (query != eKRPCQueryAnnouncePeer) // port is rport there
					UpdateNode (std::make_shared<Node> (nodeID, ident, port));
				switch (query)
				{
				case eKRPCQueryPing:
				{
					LogPrint (eLogDebug, "TorrentsDHT: Ping response received from ", nodeID.ToBase64 ());
					break;
				}
				case eKRPCQueryGetPeers:
				{
					if (!info || !info->torrent) break;
					LogPrint (eLogDebug, "TorrentsDHT: get_peers response received from ", nodeID.ToBase64 (),
						" after attempt #", info->numAttempts);
					if (!hasValues)
					{
						if (info && token) info->AddNodeToken (std::make_shared<Node> (nodeID, ident, port), *token);
						HandleGetPeersResponseNodes (info, nodes);
					}
					else
					{
						// A reply may carry peer values and referral nodes at
						// once. Take the referrals too, but let only the peers
						// path advance the lookup.
						if (!nodes.empty ()) ParseGetPeersResponseNodes (info, nodes);
						HandleGetPeersResponsePeersAndAnnounce (info, values, token, ident, port + 1); // to rport
					}
					break;
				}
				case eKRPCQueryFindNode:
				{
					if (!info) break;
					LogPrint (eLogDebug, "TorrentsDHT: find_node response received from ", nodeID.ToBase64 (),
						" after attempt #", info->numAttempts);
					HandleFindNodeResponse (info, nodes);
					break;
				}
				case eKRPCQueryAnnouncePeer:
					LogPrint (eLogDebug, "TorrentsDHT: Announce peer response received from ", nodeID.ToBase64 ());
					HandleAnnouncePeerResponse (info);
					break;
				default: LogPrint (eLogInfo, "TorrentsDHT: Response to unknown KRPC query ", (int)query);
				}
			}
		}
		else
			LogPrint (eLogInfo, "TorrentsDHT: Query not found");
	}

	void TorrentsDHT::ParseGetPeersResponseNodes (std::shared_ptr<RequestInfo> info, std::string_view nodes)
	{
		std::vector<Node> decoded;
		DecodeCompactNodes (nodes, decoded);
		for (const auto & entry : decoded)
		{
			auto node = std::make_shared<Node> (entry);
			if (info && node->id != m_NodeID && info->AddNode (node))
			{
				if (m_HeardFrom.contains (node->id))
					UpdateNode (node);
				else if (!m_FailedPeers.contains (node->peer))
					SendPingQuery (node->peer, node->port);
			}
		}
	}

	void TorrentsDHT::HandleGetPeersResponseNodes (std::shared_ptr<RequestInfo> info, std::string_view nodes)
	{
		ParseGetPeersResponseNodes (info, nodes);
		if (info) SendNextGetPeersQuery (info);
	}

	void TorrentsDHT::HandleGetPeersResponsePeersAndAnnounce (std::shared_ptr<RequestInfo> info,
		const std::vector<std::string_view> & peers, std::optional<GetPeersToken> token,
		const i2p::data::IdentHash & toIdent, uint16_t toPort)
	{
		if (!info || !info->torrent || info->torrent->IsPrivate () || info->torrent->IsStopped ()) return;
		if (IsLocallyPrivate (info->torrent->GetInfoHash ())) return;
		if (!info->torrent->IsComplete ())
		{
			std::vector<i2p::data::IdentHash> decoded;
			DecodeCompactPeers (peers, decoded);
			std::unordered_set<i2p::data::IdentHash> newPeers;
			for (const auto & peer : decoded) newPeers.emplace (peer);
			if (!newPeers.empty ())
			{
				LogPrint (eLogDebug, "TorrentsDHT: ", newPeers.size (), " new peers received");
				m_Tunnel.ConnectToNewPeers (info->torrent, newPeers, ePeerConnectionOriginDHT);
			}
		}
		LogPrint (eLogDebug, "TorrentsDHT: Send announce to ", toIdent.ToBase64 ());
		if (token) SendAnnouncePeerQuery (info, *token, toIdent, toPort);
		// A peers reply (including an empty list after removing the requester)
		// does not end discovery; continue toward the remaining closest nodes.
		SendNextGetPeersQuery (info);
	}

	void TorrentsDHT::HandleFindNodeResponse (std::shared_ptr<RequestInfo> info, std::string_view nodes)
	{
		std::vector<Node> decoded;
		DecodeCompactNodes (nodes, decoded);
		for (const auto & entry : decoded)
		{
			auto node = std::make_shared<Node> (entry);
			if (info && node->id != m_NodeID && info->AddNode (node))
			{
				if (m_HeardFrom.contains (node->id))
					UpdateNode (node);
				else if (!m_FailedPeers.contains (node->peer))
					SendPingQuery (node->peer, node->port);
			}
		}
		if (info) SendNextFindNodeQuery (info);
	}

	void TorrentsDHT::HandleAnnouncePeerResponse (std::shared_ptr<RequestInfo> info)
	{
		if (info && info->torrent)
			info->torrent->SetLastDHTAnnounceTime (i2p::util::GetMonotonicSeconds ());
		else
			LogPrint (eLogError, "TorrentsDHT: Announced torrent not found");
	}

	void TorrentsDHT::SendDatagram (std::string_view msg, const i2p::data::IdentHash & toIdent, uint16_t toPort)
	{
		auto dest = m_Tunnel.GetLocalDestination ();
		if (dest)
		{
			if (toIdent != dest->GetIdentHash ())
			{
				auto dgramDest = dest->GetDatagramDestination ();
				if (dgramDest)
					dgramDest->SendDatagramTo ((const uint8_t *)msg.data (), msg.size (), toIdent, m_Port, toPort);
			}
			else
				LogPrint (eLogWarning, "TorrentsDHT: Can't send datagram to self");
		}
	}

	void TorrentsDHT::SendRawDatagram (std::string_view msg, const i2p::data::IdentHash & toIdent, uint16_t toPort)
	{
		auto dest = m_Tunnel.GetLocalDestination ();
		if (dest)
		{
			if (toIdent != dest->GetIdentHash ())
			{
				auto dgramDest = dest->GetDatagramDestination ();
				if (dgramDest)
					dgramDest->SendRawDatagramTo ((const uint8_t *)msg.data (), msg.size (), toIdent, m_Port, toPort);
			}
			else
				LogPrint (eLogWarning, "TorrentsDHT: Can't send raw datagram to self");
		}
	}

	void TorrentsDHT::SendQueryMsg (KRPCQuery query, std::string_view arguments, const i2p::data::IdentHash & toIdent,
		uint16_t toPort, bool isRaw, std::shared_ptr<RequestInfo> info)
	{
		if (m_Queries.size () >= MAX_DHT_QUERIES) return;
		uint64_t transactionID;
		do
		{
			if (RAND_bytes (reinterpret_cast<unsigned char *> (&transactionID),
				static_cast<int> (sizeof (transactionID)))
				!= 1)
			return;
		} while (m_Queries.contains (transactionID));
		auto msg = CreateDictionary ({ { "a", arguments }, { "q", CreateByteString (KRPCQueryStr[query]) },
			{ "t", CreateByteString (std::string_view ((const char *)&transactionID, sizeof (transactionID))) },
			{ "y", CreateByteString ("q") } });
		m_Queries.insert_or_assign (
			transactionID, std::make_tuple (toIdent, toPort, query, info, i2p::util::GetMonotonicSeconds ()));
		if (isRaw)
			SendRawDatagram (msg, toIdent, toPort);
		else
			SendDatagram (msg, toIdent, toPort);
	}

	void TorrentsDHT::SendResponseMsg (std::string_view response, std::string_view transactionID,
		const i2p::data::IdentHash & toIdent, uint16_t toPort)
	{
		auto msg = CreateDictionary (
			{ { "r", response }, { "t", CreateByteString (transactionID) }, { "y", CreateByteString ("r") } });
		SendRawDatagram (msg, toIdent, toPort);
	}

	void TorrentsDHT::SendPingQuery (const i2p::data::IdentHash & toIdent, uint16_t toPort)
	{
		SendQueryMsg (eKRPCQueryPing,
			CreateDictionary (
				{ { "id", CreateByteString (std::string_view ((const char *)m_NodeID.data (), m_NodeID.size ())) } }),
			toIdent, toPort);
	}

	void TorrentsDHT::SendPingResponse (
		std::string_view transactionID, const i2p::data::IdentHash & toIdent, uint16_t toPort)
	{
		SendResponseMsg (
			CreateDictionary (
				{ { "id", CreateByteString (std::string_view ((const char *)m_NodeID.data (), m_NodeID.size ())) } }),
			transactionID, toIdent, toPort);
	}

	void TorrentsDHT::SendGetPeersQuery (
		std::shared_ptr<RequestInfo> info, const i2p::data::IdentHash & toIdent, uint16_t toPort)
	{
		if (!info || !info->torrent || info->torrent->IsPrivate () || info->torrent->IsStopped ()) return;
		const auto & infoHash = info->torrent->GetInfoHash ();
		SendQueryMsg (eKRPCQueryGetPeers,
			CreateDictionary (
				{ { "id", CreateByteString (std::string_view ((const char *)m_NodeID.data (), m_NodeID.size ())) },
					{ "noseed", CreateInteger (info->torrent->IsComplete () ? 1 : 0) },
					{ "info_hash",
						CreateByteString (std::string_view ((const char *)infoHash.data (), infoHash.size ())) } }),
			toIdent, toPort, false, info);
	}

	void TorrentsDHT::SendNextGetPeersQuery (std::shared_ptr<RequestInfo> info)
	{
		if (!info || !info->torrent || info->torrent->IsPrivate () || info->torrent->IsStopped ()) return;
		bool sent = false;
		if (!info->IsDone () && m_RoutingTable)
		{
			auto nextNode = info->GetNextNode ();
			while (nextNode && m_FailedPeers.contains (nextNode->peer)
				&& !m_HeardFrom.contains (nextNode->id)) // skip failed peers
				nextNode = info->GetNextNode ();
			if (nextNode)
			{
				sent = true;
				SendGetPeersQuery (info, nextNode->peer, nextNode->port);
			}
			else
				LogPrint (eLogDebug, "TorrentsDHT: No more nodes to send get_peers");
		}
		else
			LogPrint (eLogDebug, "TorrentsDHT: Closest node not found after ", DHT_MAX_NUM_GET_PEERS_ATTEMPTS,
				" get_peers attempts");
		if (!sent && !info->announced && !info->tokens.empty () && info->torrent)
		{
			// Announce once per iterative lookup; late responses/timeouts must
			// not replay the same announce set.
			int numAnnounces = 0;
			for (const auto & [distance, node, token] : info->tokens)
			{
				SendAnnouncePeerQuery (info, token, node->peer, node->port + 1); // to rport
				numAnnounces++;
				if (numAnnounces >= DHT_MAX_NUM_CLOSEST_NODES_TO_ANNOUNCE) break;
			}
			if (numAnnounces > 0)
			{
				info->announced = true;
				LogPrint (eLogDebug, "TorrentsDHT: Announce sent to ", numAnnounces, " closest nodes");
			}
		}
	}

	void TorrentsDHT::SendGetPeersResponse (std::string_view transactionID, std::shared_ptr<DHTTorrent> torrent,
		GetPeersToken token, const i2p::data::IdentHash & toIdent, uint16_t toPort, bool noSeeds)
	{
		if (!torrent) return;
		SendResponseMsg (
			CreateDictionary (
				{ { "id", CreateByteString (std::string_view ((const char *)m_NodeID.data (), m_NodeID.size ())) },
					{ "token", CreateByteString (token) },
					{ "values", torrent->GetBEncodedPeers (noSeeds, &toIdent) } }),
			transactionID, toIdent, toPort);
	}

	void TorrentsDHT::SendGetPeersResponse (std::string_view transactionID, std::string_view nodes, GetPeersToken token,
		const i2p::data::IdentHash & toIdent, uint16_t toPort)
	{
		SendResponseMsg (
			CreateDictionary (
				{ { "id", CreateByteString (std::string_view ((const char *)m_NodeID.data (), m_NodeID.size ())) },
					{ "token", CreateByteString (token) }, { "nodes", CreateByteString (nodes) } }),
			transactionID, toIdent, toPort);
	}

	void TorrentsDHT::SendFindNodeQuery (
		std::shared_ptr<RequestInfo> info, const i2p::data::IdentHash & toIdent, uint16_t toPort)
	{
		if (!info) return;
		SendQueryMsg (eKRPCQueryFindNode,
			CreateDictionary (
				{ { "id", CreateByteString (std::string_view ((const char *)m_NodeID.data (), m_NodeID.size ())) },
					{ "target",
						CreateByteString (
							std::string_view ((const char *)info->target.data (), info->target.size ())) } }),
			toIdent, toPort, false, info);
	}

	void TorrentsDHT::SendNextFindNodeQuery (std::shared_ptr<RequestInfo> info)
	{
		if (!info) return;
		if (!info->IsDone () && m_RoutingTable)
		{
			auto nextNode = info->GetNextNode ();
			while (nextNode && m_FailedPeers.contains (nextNode->peer)
				&& !m_HeardFrom.contains (nextNode->id)) // skip failed peers
				nextNode = info->GetNextNode ();
			// GetNextNode accounts for every attempted selection; do not reset
			// the budget when skipping failed nodes or exploring a long chain.
			if (nextNode)
				SendFindNodeQuery (info, nextNode->peer, nextNode->port);
			else
				LogPrint (eLogDebug, "TorrentsDHT: No more nodes to send find_node");
		}
	}

	void TorrentsDHT::SendFindNodeResponse (
		std::string_view transactionID, std::string_view nodes, const i2p::data::IdentHash & toIdent, uint16_t toPort)
	{
		SendResponseMsg (
			CreateDictionary (
				{ { "id", CreateByteString (std::string_view ((const char *)m_NodeID.data (), m_NodeID.size ())) },
					{ "nodes", CreateByteString (nodes) } }),
			transactionID, toIdent, toPort);
	}

	void TorrentsDHT::SendAnnouncePeerQuery (
		std::shared_ptr<RequestInfo> info, GetPeersToken token, const i2p::data::IdentHash & toIdent, uint16_t toPort)
	{
		if (!info || !info->torrent || info->torrent->IsPrivate () || info->torrent->IsStopped () || token.size () > 64)
			return;
		auto infoHash = info->torrent->GetInfoHash ();
		SendQueryMsg (eKRPCQueryAnnouncePeer,
			CreateDictionary (
				{ { "id", CreateByteString (std::string_view ((const char *)m_NodeID.data (), m_NodeID.size ())) },
					{ "seed", CreateInteger (info->torrent->IsComplete () ? 1 : 0) },
					{ "info_hash",
						CreateByteString (std::string_view ((const char *)infoHash.data (), infoHash.size ())) },
					{ "token", CreateByteString (token) } }),
			toIdent, toPort, true, info); // raw
	}

	void TorrentsDHT::Explore ()
	{
		if (m_RoutingTable && m_Tunnel.GetLocalDestination ())
		{
			auto targets = m_RoutingTable->GetExploratoryTargets (
				m_Tunnel.GetLocalDestination ()->GetRng (), i2p::util::GetMonotonicSeconds ());
			for (const auto & entry : targets)
			{
				const auto & target = entry.first;
				auto request = std::make_shared<RequestInfo> (nullptr, DHT_MAX_NUM_FIND_NODE_ATTEMPTS);
				request->target = target;
				for (const auto & node : m_RoutingTable->GetClosestNodes (target)) request->AddNode (node);
				SendNextFindNodeQuery (request);
			}
		}
	}

	std::shared_ptr<Node> TorrentsDHT::UpdateNode (std::shared_ptr<Node> node)
	{
		if (!node) return nullptr;
		if (!node->VerifyID ())
		{
			LogPrint (eLogInfo, "TorrentsDHT: Invalid node ID received ", node->id.ToBase64 ());
			return nullptr;
		}
		if (m_RoutingTable)
		{
			auto addedNode = m_RoutingTable->AddNode (node);
			if (addedNode != node)
				node = addedNode;
			else
				LogPrint (eLogDebug, "TorrentsDHT: Node ", node->id.ToBase64 (), " added");
		}
		else
			LogPrint (eLogError, "TorrentsDHT: No routing table");
		return node;
	}

	void TorrentsDHT::UpdateHeardFrom (const NodeID & nodeID)
	{
		auto it = m_HeardFrom.find (nodeID);
		if (it != m_HeardFrom.end ())
		{
			it->second = i2p::util::GetMonotonicSeconds ();
			return;
		}
		if (m_HeardFrom.size () >= MAX_DHT_HEARD_FROM) return;
		m_HeardFrom.emplace (nodeID, i2p::util::GetMonotonicSeconds ());
	}

	void TorrentsDHT::ScheduleDHTUpdateCheck ()
	{
		m_DHTUpdateCheckTimer.cancel ();
		m_DHTUpdateCheckTimer.expires_after (std::chrono::seconds (DHT_UPDATE_CHECK_INTERVAL));
		m_DHTUpdateCheckTimer.async_wait (
			[this] (const boost::system::error_code & ecode) { HandleDHTUpdateCheckTimer (ecode); });
	}

	void TorrentsDHT::HandleDHTUpdateCheckTimer (const boost::system::error_code & ecode)
	{
		if (ecode != boost::asio::error::operation_aborted)
		{
			auto ts = i2p::util::GetMonotonicSeconds ();
			if (ts > m_NextDHTExploratoryTime)
			{
				Explore ();
				m_NextDHTExploratoryTime = ts + DHT_EXPLORATORY_INTERVAL
					+ (m_Tunnel.GetLocalDestination ()
							? m_Tunnel.GetLocalDestination ()->GetRng () () % DHT_EXPLORATORY_INTERVAL_VARIANCE
							: 0);
			}
			ScheduleDHTUpdateCheck ();
		}
	}

	void TorrentsDHT::ScheduleDHTExpirationCheck ()
	{
		m_DHTExpirationCheckTimer.cancel ();
		m_DHTExpirationCheckTimer.expires_after (std::chrono::seconds (DHT_EXPIRATION_CHECK_INTERVAL));
		m_DHTExpirationCheckTimer.async_wait (
			[this] (const boost::system::error_code & ecode) { HandleDHTExpirationCheckTimer (ecode); });
	}

	void TorrentsDHT::HandleDHTExpirationCheckTimer (const boost::system::error_code & ecode)
	{
		if (ecode != boost::asio::error::operation_aborted)
		{
			auto ts = i2p::util::GetMonotonicSeconds ();
			m_QueryLimiter.Expire (ts);
			if (m_RoutingTable)
			{
				m_RoutingTable->DeleteExpiredNodes (ts);
				LogPrint (eLogDebug, "TorrentsDHT: Stats ", m_RoutingTable->GetNumNodes (), " nodes in ",
					m_RoutingTable->GetNumBuckets (), " buckets ");
			}
			{
				auto it = m_Torrents.begin ();
				while (it != m_Torrents.end ())
				{
					if (it->second->CleanUp (ts))
						it = m_Torrents.erase (it);
					else
						it++;
				}
			}
			{
				auto it = m_HeardFrom.begin ();
				while (it != m_HeardFrom.end ())
				{
					if (ts > it->second + DHT_HEARD_FROM_EXPIRATION_TIME)
						it = m_HeardFrom.erase (it);
					else
						it++;
				}
			}
			{
				auto it = m_FailedPeers.begin ();
				while (it != m_FailedPeers.end ())
				{
					if (ts > it->second + DHT_PEER_FAILURE_INTERVAL)
						it = m_FailedPeers.erase (it);
					else
						it++;
				}
			}
			{
				// The bounded token store owns capacity, per-node quota and
				// expiration; the sweep only ages it out.
				const auto expired = m_IncomingTokens.Expire (ts);
				if (expired) LogPrint (eLogDebug, "TorrentsDHT: ", expired, " announce tokens expired");
			}
			ScheduleDHTExpirationCheck ();
		}
	}

	void TorrentsDHT::ScheduleDHTSendPingCheck ()
	{
		m_DHTSendPingCheckTimer.cancel ();
		m_DHTSendPingCheckTimer.expires_after (std::chrono::seconds (DHT_SEND_PING_CHECK_INTERVAL));
		m_DHTSendPingCheckTimer.async_wait (
			[this] (const boost::system::error_code & ecode) { HandleDHTSendPingCheckTimer (ecode); });
	}

	void TorrentsDHT::HandleDHTSendPingCheckTimer (const boost::system::error_code & ecode)
	{
		if (ecode != boost::asio::error::operation_aborted)
		{
			auto ts = i2p::util::GetMonotonicSeconds ();
			if (m_RoutingTable)
			{
				auto toPing = m_RoutingTable->GetNodesToPing (ts);
				for (const auto & it : toPing) SendPingQuery (it->peer, it->port);
			}
			ScheduleDHTSendPingCheck ();
		}
	}

	void TorrentsDHT::ScheduleDHTQueryExpirationCheck ()
	{
		m_DHTQueryExpirationCheckTimer.cancel ();
		m_DHTQueryExpirationCheckTimer.expires_after (std::chrono::seconds (DHT_QUERY_EXPIRATION_CHECK_INTERVAL));
		m_DHTQueryExpirationCheckTimer.async_wait (
			[this] (const boost::system::error_code & ecode) { DHTQueryExpirationCheckTimer (ecode); });
	}

	void TorrentsDHT::DHTQueryExpirationCheckTimer (const boost::system::error_code & ecode)
	{
		if (ecode != boost::asio::error::operation_aborted)
		{
			std::list<std::shared_ptr<RequestInfo>> getpeers, findnode;
			auto ts = i2p::util::GetMonotonicSeconds ();
			auto it = m_Queries.begin ();
			while (it != m_Queries.end ())
			{
				if (ts > std::get<4> (it->second) + DHT_QUERY_EXPIRATION_TIME)
				{
					if (m_FailedPeers.contains (std::get<0> (it->second))
						|| m_FailedPeers.size () < MAX_DHT_FAILED_PEERS)
						m_FailedPeers.insert_or_assign (std::get<0> (it->second), ts);
					switch (std::get<2> (it->second))
					{
					case eKRPCQueryPing:
					{
						// A ping query only stores the peer identity. Resolve
						// the routing-table node so the heard-from window is
						// checked against its node ID, not the peer hash.
						auto node
							= m_RoutingTable ? m_RoutingTable->FindNodeByPeer (std::get<0> (it->second)) : nullptr;
						if (node && !m_HeardFrom.contains (node->id)) m_RoutingTable->RemoveNodeByPeer (node->peer);
						break;
					}
					case eKRPCQueryGetPeers: getpeers.push_back (std::get<3> (it->second)); break;
					case eKRPCQueryFindNode: findnode.push_back (std::get<3> (it->second)); break;
					default:;
					}
					it = m_Queries.erase (it);
				}
				else
					it++;
			}
			for (auto it1 : getpeers)
			{
				LogPrint (eLogDebug, "TorrentsDHT: get_peers response timeout after attempt #", it1->numAttempts);
				if (m_RoutingTable && it1->lastNode && !m_HeardFrom.contains (it1->lastNode->id))
				{
					auto bucket = m_RoutingTable->FindBucket (it1->lastNode->id);
					if (bucket && bucket->nodes.size () > MAX_BUCKET_CAPACITY / 2)
						bucket->nodes.erase (it1->lastNode->id);
				}
				SendNextGetPeersQuery (it1);
			}
			for (auto it1 : findnode)
			{
				LogPrint (eLogDebug, "TorrentsDHT: find_node response timeout after attempt #", it1->numAttempts);
				if (m_RoutingTable && it1->lastNode && !m_HeardFrom.contains (it1->lastNode->id))
				{
					auto bucket = m_RoutingTable->FindBucket (it1->lastNode->id);
					if (bucket && bucket->nodes.size () > MAX_BUCKET_CAPACITY / 2)
						bucket->nodes.erase (it1->lastNode->id);
				}
				SendNextFindNodeQuery (it1);
			}
			ScheduleDHTQueryExpirationCheck ();
		}
	}

	size_t TorrentsDHT::GetNumQueuedDatagrams () const
	{
		std::lock_guard<std::mutex> lock (m_IncomingMutex);
		return m_Incoming.size ();
	}

	void TorrentsDHT::EnqueueIncomingDatagram (const i2p::data::IdentityEx & from, uint16_t fromPort, uint16_t toPort,
		const uint8_t * buf, size_t len, const i2p::util::Mapping * options)
	{
		// Hostile or oversized frames are dropped before anything is copied.
		if (!buf || !len || len > MAX_DHT_DATAGRAM_SIZE) return;
		IncomingDatagram dgram;
		dgram.from = std::make_shared<const i2p::data::IdentityEx> (from);
		if (options) dgram.options = *options;
		dgram.payload.assign (buf, buf + len);
		dgram.fromPort = fromPort;
		dgram.toPort = toPort;
		std::lock_guard<std::mutex> lock (m_IncomingMutex);
		// Bounded drop-oldest: a flood may delay, but never grow, the queue.
		if (m_Incoming.size () >= MAX_DHT_INCOMING_QUEUE) m_Incoming.pop_front ();
		m_Incoming.push_back (std::move (dgram));
	}

	void TorrentsDHT::DrainIncomingDatagrams ()
	{
		// Swap the whole bounded batch out under the lock so the network thread
		// is never blocked by the parsers. HandleDatagram() runs only here.
		std::deque<IncomingDatagram> batch;
		{
			std::lock_guard<std::mutex> lock (m_IncomingMutex);
			batch.swap (m_Incoming);
		}
		for (const auto & dgram : batch)
		{
			if (!dgram.from || dgram.payload.empty ()) continue;
			HandleDatagram (*dgram.from, dgram.fromPort, dgram.toPort, dgram.payload.data (), dgram.payload.size (),
				dgram.options.IsEmpty () ? nullptr : &dgram.options);
		}
	}

	void TorrentsDHT::ScheduleDHTIncomingDrain ()
	{
		m_DHTIncomingDrainTimer.cancel ();
		m_DHTIncomingDrainTimer.expires_after (std::chrono::milliseconds (DHT_INCOMING_DRAIN_INTERVAL));
		std::weak_ptr<DHTServiceState> guard = m_ServiceState;
		m_DHTIncomingDrainTimer.async_wait (
			[this, guard] (const boost::system::error_code & ecode)
			{
				// The guard is authoritative: after Stop() the shared state is
				// marked stopping, and a torn-down service is never entered.
				const auto state = guard.lock ();
				if (!state || state->stopping.load (std::memory_order_acquire)
					|| ecode == boost::asio::error::operation_aborted)
					return;
				DrainIncomingDatagrams ();
				ScheduleDHTIncomingDrain ();
			});
	}

	void TorrentsDHT::ScheduleDHTStateSave ()
	{
		m_DHTStateSaveTimer.cancel ();
		m_DHTStateSaveTimer.expires_after (std::chrono::seconds (DHT_STATE_SAVE_INTERVAL));
		std::weak_ptr<DHTServiceState> guard = m_ServiceState;
		m_DHTStateSaveTimer.async_wait (
			[this, guard] (const boost::system::error_code & ecode)
			{
				const auto state = guard.lock ();
				if (!state || state->stopping.load (std::memory_order_acquire)
					|| ecode == boost::asio::error::operation_aborted)
					return;
				HandleDHTStateSaveTimer (ecode);
			});
	}

	void TorrentsDHT::HandleDHTStateSaveTimer (const boost::system::error_code & ecode)
	{
		if (ecode == boost::asio::error::operation_aborted) return;
		if (m_ServiceState->stopping.load (std::memory_order_acquire))
		{
			// Stop() performs the final synchronous save with the still-live
			// storage authority.
			return;
		}
		ScheduleDHTStateSave ();
		if (m_DHTFileName.empty () || !m_DHTFileName[0] || !IsValidStateFileName (m_DHTFileName)) return;
		if (!m_RoutingTable) return;
		// Single flight: a save that is still queued or running makes this tick a
		// no-op, so the disk domain never accumulates writes.
		if (m_ServiceState->saveInFlight.exchange (true, std::memory_order_acq_rel)) return;
		const auto file = GetDHTFilePath (m_DHTFileName);
		if (file.empty ())
		{
			m_ServiceState->saveInFlight.store (false, std::memory_order_release);
			return;
		}
		// The snapshot is built on the service thread because it reads the
		// routing table and node identity. The write task captures no `this`:
		// only the shared state, the path, the bytes and a copy of the storage
		// callable, so it stays valid even if persistence is revoked first.
		std::vector<uint8_t> bytes;
		if (!BuildStateSnapshot (bytes))
		{
			m_ServiceState->saveInFlight.store (false, std::memory_order_release);
			return;
		}
		const bool delegated = m_DelegatedPersistence;
		auto save = m_SaveState;
		auto state = m_ServiceState;
		boost::asio::post (m_Tunnel.GetDiskIOService (),
			[state, file, bytes = std::move (bytes), delegated, save = std::move (save)] () mutable
			{
				if (state->stopping.load (std::memory_order_acquire))
				{
					state->saveInFlight.store (false, std::memory_order_release);
					return;
				}
				std::string error;
				if (delegated)
				{
					// Delegated persistence never falls back to parent IO: a
					// missing or failing worker means this save is lost, not
					// written by the core.
					if (!save || !save (file, bytes, error))
						LogPrint (eLogError, "TorrentsDHT: Delegated periodic save failed: ", error);
				}
				else if (!WriteFileAtomicNoFollow (
							 file.parent_path (), file.filename (), bytes.data (), bytes.size (), error))
					LogPrint (eLogError, "TorrentsDHT: Can't write state file: ", error);
				state->saveInFlight.store (false, std::memory_order_release);
			});
	}

	void TorrentsDHT::Unannounce (const Torrent::InfoHash & infoHash)
	{
		auto dest = m_Tunnel.GetLocalDestination ();
		if (!dest) return;
		auto it = m_Torrents.find (infoHash);
		if (it == m_Torrents.end ()) return;
		it->second->RemovePeer (dest->GetIdentHash ());
		if (it->second->CleanUp (i2p::util::GetMonotonicSeconds ())) m_Torrents.erase (it);
	}

	void TorrentsDHT::PurgeInfoHash (const Torrent::InfoHash & infoHash)
	{
		// Served peers, our own rendezvous identity and every token issued for
		// the hash go at once: a token would otherwise stay usable for the rest
		// of its ten-minute window after the metadata revealed privacy.
		m_Torrents.erase (infoHash);
		m_IncomingTokens.RemoveInfoHash (infoHash);
		// Outstanding lookups for the hash stop querying the network too: a
		// removed or newly private swarm must not keep advertising our interest
		// in it. The response to a dropped transaction is simply ignored.
		for (auto it = m_Queries.begin (); it != m_Queries.end ();)
		{
			const auto & request = std::get<3> (it->second);
			if (std::get<2> (it->second) == eKRPCQueryGetPeers && request && request->torrent
				&& request->torrent->GetInfoHash () == infoHash)
				it = m_Queries.erase (it);
			else
				++it;
		}
	}

	void TorrentsDHT::MarkPrivate (const Torrent::InfoHash & infoHash)
	{
		if (m_PrivateInfoHashes.Insert (infoHash) == PrivateInfoHashSet::Admission::EvictedOldest)
			LogPrint (eLogWarning, "TorrentsDHT: Private info hash set at its limit ", PrivateInfoHashSet::MaxHashes,
				"; the oldest entry was evicted");
		PurgeInfoHash (infoHash);
	}

	void TorrentsDHT::Forget (const Torrent::InfoHash & infoHash)
	{
		PurgeInfoHash (infoHash);
	}

	void TorrentsDHT::GetPeersAndAnnounce (std::shared_ptr<Torrent> torrent)
	{
		if (!torrent || torrent->IsPrivate () || IsLocallyPrivate (torrent->GetInfoHash ()) || torrent->IsStopped ()
			|| torrent->IsRemoved () || !m_RoutingTable)
			return;

		// If this torrent's hash is in our own k-bucket, we are one of the
		// closest DHT nodes for the swarm. Reuse peers that already announced
		// themselves here and publish our destination as a rendezvous point
		// instead of immediately performing a full iterative lookup.
		auto dest = m_Tunnel.GetLocalDestination ();
		if (dest && m_RoutingTable->IsInOurBucket (torrent->GetInfoHash ()))
		{
			auto it = m_Torrents.find (torrent->GetInfoHash ());
			if (it == m_Torrents.end () && m_Torrents.size () < MAX_DHT_TORRENTS)
				it = m_Torrents.emplace (torrent->GetInfoHash (), std::make_shared<DHTTorrent> ()).first;
			if (it != m_Torrents.end ())
			{
				auto peers = it->second->GetPeers ();
				peers.erase (dest->GetIdentHash ());
				if (!peers.empty ())
				{
					LogPrint (eLogDebug, "TorrentsDHT: ", peers.size (), " rendezvous peers received");
					m_Tunnel.ConnectToNewPeers (torrent, peers, ePeerConnectionOriginDHT);
				}
				// Mark ourselves as a seed when complete so a later noseed
				// query does not receive a leecher-only result. Reserve one
				// slot so a full remote peer map cannot suppress the
				// rendezvous announcement.
				if (!it->second->AddPeer (dest->GetIdentHash (), torrent->IsComplete (), true))
					LogPrint (eLogDebug, "TorrentsDHT: Rendezvous peer was refreshed or the map is full");
			}
		}

		auto request = std::make_shared<RequestInfo> (torrent, DHT_MAX_NUM_GET_PEERS_ATTEMPTS);
		for (const auto & node : m_RoutingTable->GetClosestNodes (torrent->GetInfoHash ())) request->AddNode (node);
		SendNextGetPeersQuery (request);
	}
}
}

#endif
