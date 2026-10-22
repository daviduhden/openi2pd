/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TorrentsDHT.
 *
 * Declares the DHT node identity, bucket routing table, KRPC request
 * tracking, bounded peer/token stores and the service that exchanges them
 * over I2P datagrams.
 *
 */

#ifndef TORRENTS_DHT_H__
#define TORRENTS_DHT_H__

#ifndef NO_TORRENTS

#include <inttypes.h>
#include <openssl/sha.h>
#include <string>
#include <string_view>
#include <tuple>
#include <memory>
#include <random>
#include <array>
#include <list>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <utility>
#include <optional>
#include <vector>
#include <filesystem>
#include <functional>
#include <span>
#include <boost/asio.hpp>
#include "Base.h"
#include "Identity.h"
#include "I2PService.h"
#include "util.h"
#include "Timestamp.h"
#include "Torrents.h"

namespace i2p
{
namespace torrents
{
	constexpr int DHT_UPDATE_CHECK_INTERVAL = 24; // in seconds
	constexpr int DHT_EXPIRATION_CHECK_INTERVAL = 73; // in seconds
	constexpr int DHT_SEND_PING_CHECK_INTERVAL = 38; // in seconds
	constexpr int DHT_QUERY_EXPIRATION_CHECK_INTERVAL = 8; // in seconds
	constexpr int DHT_EXPLORATORY_INTERVAL = 230; // in seconds
	constexpr int DHT_EXPLORATORY_INTERVAL_VARIANCE = 40; // in seconds
	constexpr int DHT_INITIAL_EXPLORATORY_INTERVAL = 90; // in seconds
	constexpr int DHT_NODE_SEND_PING_TIME = 740; // in seconds
	constexpr int DHT_NODE_EXPIRATION_TIME = 855; // in seconds
	constexpr int DHT_BUCKET_EXPIRATION_THRESHOLD = 290; // in seconds
	constexpr int DHT_TORRENT_PEER_EXPIRATION_TIME = 3 * 3600; // in seconds
	constexpr int DHT_INCOMING_GET_PEERS_TOKEN_EXPIRATION_TIME = 600; // in seconds
	constexpr int DHT_HEARD_FROM_EXPIRATION_TIME = 330; // in seconds
	// I2P stream/datagram round trips routinely exceed 25 s. Mirror Java's
	// 75 s class of timeout while keeping a bounded sweep cadence.
	constexpr int DHT_QUERY_EXPIRATION_TIME = 60; // in seconds
	constexpr int DHT_PEER_FAILURE_INTERVAL = 600; // in seconds
	constexpr int DHT_MAX_NUM_GET_PEERS_ATTEMPTS = 22;
	constexpr int DHT_MAX_NUM_FIND_NODE_ATTEMPTS = 8;
	constexpr int DHT_MAX_NUM_CLOSEST_NODES_TO_ANNOUNCE = 4;
	// Bounds for state created by untrusted DHT datagrams. They keep a flood of
	// announces, get_peers queries or find_node responses from growing the maps
	// without limit between expiration sweeps.
	constexpr size_t MAX_DHT_TORRENTS = 2048;
	constexpr size_t MAX_DHT_PEERS_PER_TORRENT = 128;
	constexpr size_t MAX_DHT_INCOMING_TOKENS = 4096;
	constexpr size_t MAX_DHT_HEARD_FROM = 8192;
	constexpr size_t MAX_DHT_FAILED_PEERS = 2048;
	constexpr size_t MAX_DHT_QUERIES = 2048;
	constexpr size_t MAX_DHT_REQUEST_NODES = 256;
	// A compliant reply carries at most k nodes/peer values. Bound the work
	// and follow-up pings triggered by one hostile response.
	constexpr size_t MAX_DHT_NODES_PER_RESPONSE = 8; // Kademlia k
	constexpr size_t MAX_DHT_VALUES_PER_RESPONSE = 64;
	constexpr size_t MAX_DHT_LOADED_NODES = 2048;
	constexpr size_t MAX_DHT_REQUEST_TOKENS = 8;
	constexpr size_t MAX_DHT_QUERY_ORIGINS = 1024;
	constexpr size_t MAX_DHT_QUERIES_PER_ORIGIN = 64;
	constexpr uint64_t DHT_QUERY_RATE_WINDOW = 60;

	using Distance = Torrent::InfoHash;
	struct NodeID : public Torrent::InfoHash
	{
		static constexpr size_t len = std::tuple_size<Torrent::InfoHash>::value;
		constexpr Distance operator^ (const Torrent::InfoHash & hash) const
		{
			Distance d;
			for (size_t i = 0; i < size (); i++) d[i] = (*this)[i] ^ hash[i];
			return d;
		}
		constexpr int FindLowestBit () const // -1 in not found
		{
			for (int i = size () - 1; i >= 0; i--)
			{
				uint8_t byte = (*this)[i];
				if (byte)
				{
					for (int j = 7; j >= 0; j--)
						if (byte & (0x80 >> j)) return i * 8 + j;
				}
			}
			return -1;
		}
		std::string ToBase64 () const { return i2p::data::ByteStreamToBase64 (data (), size ()); }
	};

	using NodeInfo = std::array<uint8_t, NodeID::len + i2p::data::IdentHash::len + 2>;
	struct Node
	{
		NodeID id;
		i2p::data::IdentHash peer;
		uint16_t port;
		uint64_t lastUpdateTime; // monotonic seconds

		Node (const NodeID & id1, const i2p::data::IdentHash & peer1, uint16_t port1)
			: id (id1)
			, peer (peer1)
			, port (port1)
			, lastUpdateTime (i2p::util::GetMonotonicSeconds ())
		{
		}
		Node (const NodeInfo & nodeInfo);

		NodeInfo GetNodeInfo () const;
		bool VerifyID () const;
	};

	constexpr size_t MAX_BUCKET_CAPACITY = 8;
	struct Bucket
	{
		std::unique_ptr<Bucket> next; // owned successor in the bucket chain
		std::map<NodeID, std::shared_ptr<Node>> nodes;
		NodeID start;
		uint64_t lastUpdateTime; // monotonic seconds

		Bucket ()
			: next (nullptr)
			, start { }
			, lastUpdateTime (i2p::util::GetMonotonicSeconds ())
		{
		}
		Bucket (const NodeID & start1)
			: next (nullptr)
			, start (start1)
			, lastUpdateTime (i2p::util::GetMonotonicSeconds ())
		{
		}
		bool IsFull () const { return nodes.size () >= MAX_BUCKET_CAPACITY; }
		bool IsEmpty () const { return nodes.empty (); }
		bool IsInBucket (const NodeID & id) const { return id >= start && (!next || id < next->start); }
		std::optional<NodeID> GetMiddleID () const;
		NodeID GetRandomID (std::mt19937 & rng) const;
		bool Split ();
	};

	class RoutingTable
	{
	public:
		RoutingTable (const NodeID & ourNode);
		~RoutingTable ();
		void CleanUp ();
		size_t GetNumBuckets () const;
		size_t GetNumNodes () const;
		std::list<std::shared_ptr<Node>> GetNodes ();
		std::vector<std::shared_ptr<Node>> GetClosestNodes (const Torrent::InfoHash & target,
			size_t count = MAX_BUCKET_CAPACITY, const NodeID * exclude = nullptr) const;
		Bucket * FindBucket (const Torrent::InfoHash & id) const; // non-owning; valid until the chain is mutated
		// True when the target maps to the same bucket as our own node ID.
		// Used by the torrent self-announce rendezvous path.
		bool IsInOurBucket (const Torrent::InfoHash & id) const
		{
			auto bucket = FindBucket (id);
			if (!bucket) return false;
			// Match upstream's empty-bucket fallback: an empty leaf is not a
			// useful rendezvous signal, but a populated successor can be.
			if (bucket->IsEmpty ())
			{
				if (bucket->next && !bucket->next->IsEmpty ()) bucket = bucket->next.get ();
				else return false;
			}
			return bucket->IsInBucket (m_OurNode);
		}

		std::shared_ptr<Node> AddNode (std::shared_ptr<Node> node);
		void RemoveNode (const NodeID & id);
		void RemoveNodeByPeer (const i2p::data::IdentHash & peer);
		std::list<std::pair<NodeID, Bucket *>> GetExploratoryTargets (
			std::mt19937 & rng, uint64_t ts); // (target, non-owning node to send find_node to)
		size_t DeleteExpiredNodes (uint64_t ts);
		std::list<std::shared_ptr<Node>> GetNodesToPing (uint64_t ts);

	private:
		std::unique_ptr<Bucket> m_Buckets; // owns the first bucket and, transitively, the chain
		NodeID m_OurNode;
	};

	using GetPeersToken = std::string; // opaque wire bytes, never an integer
	struct DHTErrorReply
	{
		std::string transactionID;
		int32_t code = 0;
	};
	// KRPC errors have e:[integer,byte-string], t and y="e", without a
	// response ID. The untrusted message text is bounded and never logged.
	std::optional<DHTErrorReply> DecodeDHTErrorReply (std::string_view encoded);
	// Called only after signed identity or a previously issued token identifies
	// the origin. Never key this budget by an unauthenticated claimed node ID.
	class DHTQueryLimiter
	{
	public:
		bool Allow (const i2p::data::IdentHash & origin, uint64_t now);
		void Expire (uint64_t now);

	private:
		std::unordered_map<i2p::data::IdentHash, std::pair<uint64_t, size_t>> m_Origins;
	};
	class DHTTorrent
	{
	public:
		DHTTorrent () = default;

		std::string GetBEncodedPeers (bool noSeeds = false, const i2p::data::IdentHash * exclude = nullptr) const;
		// local=true reserves one slot for our own rendezvous identity even
		// when remote announcers have filled the normal per-torrent cap.
		bool AddPeer (const i2p::data::IdentHash & peer, bool seed = false, bool local = false);
		bool RemovePeer (const i2p::data::IdentHash & peer);
		bool CleanUp (uint64_t ts); // return true if empty
		bool HasPeers (bool noSeeds = false) const;
		bool IsFresh (uint64_t now, uint64_t lastUpdate, bool seed, bool noSeeds) const
		{
			return (!noSeeds || !seed)
				&& (now < lastUpdate || now - lastUpdate < DHT_TORRENT_PEER_EXPIRATION_TIME);
		}
		// Non-expired peers for a rendezvous reuse; copied so callers can
		// erase/modify the set without touching the DHT map under iteration.
		std::unordered_set<i2p::data::IdentHash> GetPeers () const;

	private:
		std::unordered_map<i2p::data::IdentHash, std::pair<uint64_t, bool>> m_Peers; // monotonic update time, seed
	};

	enum KRPCQuery
	{
		eKRPCQueryPing = 0,
		eKRPCQueryFindNode,
		eKRPCQueryGetPeers,
		eKRPCQueryAnnouncePeer,
		eNumKRPCQueries
	};

	constexpr std::array<std::string_view, eNumKRPCQueries> KRPCQueryStr { "ping", "find_node", "get_peers",
		"announce_peer" };

	struct RequestInfo
	{
		using NodeToRequest = std::pair<Distance, std::shared_ptr<Node>>;
		struct NodeToRequestCmp
		{
			bool operator() (const NodeToRequest & n1, const NodeToRequest & n2) const
			{
				auto cmp = n1.first <=> n2.first;
				if (cmp != 0) return cmp < 0;
				return n1.second->id < n2.second->id;
			}
		};
		using NodeToken = std::tuple<Distance, std::shared_ptr<Node>, GetPeersToken>;
		struct NodeTokenCmp
		{
			bool operator() (const NodeToken & n1, const NodeToken & n2) const
			{
				auto cmp = std::get<0> (n1) <=> std::get<0> (n2);
				if (cmp != 0) return cmp < 0;
				return std::get<1> (n1)->id < std::get<1> (n2)->id;
			}
		};

		std::shared_ptr<Torrent> torrent; // for get_peers
		NodeID target; // for find_node
		std::set<NodeToRequest, NodeToRequestCmp> nodesToRequest;
		std::set<NodeToken, NodeTokenCmp> tokens; // token received from nodes for get_peers
		std::set<NodeID> tried;
		int numAttempts, maxNumAttempts;
		bool announced = false;
		std::shared_ptr<Node> lastNode;

		RequestInfo (std::shared_ptr<Torrent> t, int maxNumAttempts1)
			: torrent (t)
			, numAttempts (0)
			, maxNumAttempts (maxNumAttempts1)
		{
		}
		bool IsDone () const { return numAttempts >= maxNumAttempts; }
		bool AddNode (std::shared_ptr<Node> node);
		bool AddNodeToken (std::shared_ptr<Node> node, GetPeersToken token);
		std::shared_ptr<Node> GetNextNode ();
	};

	class TorrentsTunnel;
	class TorrentsDHT
	{
	public:
		TorrentsDHT (TorrentsTunnel & tunnel, uint16_t port, std::string_view dhtFileName);

		// Set before Start. Delegated persistence never falls back to parent IO.
		using LoadState = std::function<bool (const std::filesystem::path &, std::vector<uint8_t> &, std::string &)>;
		using SaveState = std::function<bool (const std::filesystem::path &, std::span<const uint8_t>, std::string &)>;
		void SetPersistence (LoadState load, SaveState save)
		{ m_LoadState = std::move (load); m_SaveState = std::move (save); m_DelegatedPersistence = true; }
		static constexpr size_t MaxStateBytes = 8 * 1024 * 1024;
		// Legacy flat DHT filename: one component beneath <datadir>/torrents,
		// no traversal, separators, reserved prefixes or control bytes.
		static bool IsValidStateFileName (std::string_view name);
		void Start ();
		void Stop ();

		uint16_t GetPort () const { return m_Port; };
		uint16_t GetRPort () const { return m_Port + 1; };
		void HandleRawDatagram (const uint8_t * buf, size_t len, uint16_t fromPort, uint16_t toPort);

		void SendPingQuery (const i2p::data::IdentHash & toIdent, uint16_t toPort);
		void GetPeersAndAnnounce (std::shared_ptr<Torrent> torrent);
		// Remove only our own rendezvous identity after stop/remove. Remote
		// announcers remain until their normal expiry.
		void Unannounce (const Torrent::InfoHash & infoHash);

		size_t GetNumBuckets () const { return m_RoutingTable ? m_RoutingTable->GetNumBuckets () : 0; }
		size_t GetNumNodes () const { return m_RoutingTable ? m_RoutingTable->GetNumNodes () : 0; }
		size_t GetNumTorrents () const { return m_Torrents.size (); }
		size_t GetNumHeardFrom () const { return m_HeardFrom.size (); }
		size_t GetNumFailedPeers () const { return m_FailedPeers.size (); }

	private:
		void HandleDatagram (const i2p::data::IdentityEx & from, uint16_t fromPort, uint16_t toPort,
			const uint8_t * buf, size_t len, const i2p::util::Mapping * options);
		void HandlePingQuery (const i2p::data::IdentHash & fromIdent, uint16_t fromPort, std::string_view transactionID,
			const NodeID & nodeID);
		void HandleGetPeersQuery (std::string_view transactionID, std::shared_ptr<Node> from,
			const Torrent::InfoHash & infoHash, bool noSeeds);
		void HandleFindNodeQuery (const i2p::data::IdentHash & fromIdent, uint16_t fromPort,
			std::string_view transactionID, const NodeID & target);
		void HandleResponse (std::string_view transactionID, const NodeID & nodeID, std::optional<GetPeersToken> token,
			const std::vector<std::string_view> & values, bool hasValues, std::string_view nodes, uint16_t fromPort);
		void HandleErrorReply (const DHTErrorReply & error, uint16_t fromPort);
		void HandleGetPeersResponseNodes (std::shared_ptr<RequestInfo> info, std::string_view nodes);
		void HandleGetPeersResponsePeersAndAnnounce (std::shared_ptr<RequestInfo> info,
			const std::vector<std::string_view> & peers, std::optional<GetPeersToken> token,
			const i2p::data::IdentHash & toIdent, uint16_t toPort);
		void HandleFindNodeResponse (std::shared_ptr<RequestInfo> info, std::string_view nodes);
		void HandleAnnouncePeerResponse (std::shared_ptr<RequestInfo> info);
		void HandleAnnouncePeer (std::string_view transactionID, const NodeID & nodeID,
			const Torrent::InfoHash & infoHash, const GetPeersToken & token, bool seed, uint16_t fromPort);
		void SendDatagram (std::string_view msg, const i2p::data::IdentHash & toIdent, uint16_t toPort);
		void SendRawDatagram (std::string_view msg, const i2p::data::IdentHash & toIdent, uint16_t toPort);
		void SendQueryMsg (KRPCQuery query, std::string_view arguments, const i2p::data::IdentHash & toIdent,
			uint16_t toPort, bool isRaw = false, std::shared_ptr<RequestInfo> info = nullptr);
		void SendFindNodeQuery (
			std::shared_ptr<RequestInfo> info, const i2p::data::IdentHash & toIdent, uint16_t toPort);
		void SendNextFindNodeQuery (std::shared_ptr<RequestInfo> info);
		void SendGetPeersQuery (
			std::shared_ptr<RequestInfo> info, const i2p::data::IdentHash & toIdent, uint16_t toPort);
		void SendNextGetPeersQuery (std::shared_ptr<RequestInfo> info);
		void SendResponseMsg (std::string_view response, std::string_view transactionID,
			const i2p::data::IdentHash & toIdent, uint16_t toPort);
		void SendPingResponse (std::string_view transactionID, const i2p::data::IdentHash & toIdent, uint16_t toPort);
		void SendGetPeersResponse (std::string_view transactionID, std::shared_ptr<DHTTorrent> torrent,
			GetPeersToken token, const i2p::data::IdentHash & toIdent, uint16_t toPort, bool noSeeds);
		void SendGetPeersResponse (std::string_view transactionID, std::string_view nodes, GetPeersToken token,
			const i2p::data::IdentHash & toIdent, uint16_t toPort);
		void SendFindNodeResponse (std::string_view transactionID, std::string_view nodes,
			const i2p::data::IdentHash & toIdent, uint16_t toPort);
		void SendAnnouncePeerQuery (std::shared_ptr<RequestInfo> info, GetPeersToken token,
			const i2p::data::IdentHash & toIdent, uint16_t toPort);

		std::filesystem::path GetDHTFilePath (std::string_view filename) const;
		void Save (const std::filesystem::path & file);
		void Load (const std::filesystem::path & file);
		void Explore ();
		std::shared_ptr<Node> UpdateNode (std::shared_ptr<Node> node); // return true if added
		void UpdateHeardFrom (const NodeID & nodeID);

		void ScheduleDHTUpdateCheck ();
		void HandleDHTUpdateCheckTimer (const boost::system::error_code & ecode);

		void ScheduleDHTExpirationCheck ();
		void HandleDHTExpirationCheckTimer (const boost::system::error_code & ecode);

		void ScheduleDHTSendPingCheck ();
		void HandleDHTSendPingCheckTimer (const boost::system::error_code & ecode);

		void ScheduleDHTQueryExpirationCheck ();
		void DHTQueryExpirationCheckTimer (const boost::system::error_code & ecode);

	private:
		TorrentsTunnel & m_Tunnel;
		uint16_t m_Port;
		std::string m_DHTFileName;
		LoadState m_LoadState;
		SaveState m_SaveState;
		bool m_DelegatedPersistence = false;
		NodeID m_NodeID;
		boost::asio::steady_timer m_DHTUpdateCheckTimer, m_DHTExpirationCheckTimer, m_DHTSendPingCheckTimer,
			m_DHTQueryExpirationCheckTimer;
		std::unique_ptr<RoutingTable> m_RoutingTable;
		std::map<NodeID, uint64_t> m_HeardFrom; // nodeid -> time in monotonic seconds
		std::unordered_map<i2p::data::IdentHash, uint64_t> m_FailedPeers; // peer -> failure time in monotonic seconds
		struct IncomingToken
		{
			std::shared_ptr<Node> node;
			Torrent::InfoHash infoHash;
			uint64_t time; // monotonic seconds
		};
		std::unordered_map<GetPeersToken, IncomingToken> m_IncomingTokens;
		// transactionID -> (ident, port, query,  request info, time in monotonic seconds)
		std::unordered_map<uint64_t,
			std::tuple<i2p::data::IdentHash, uint16_t, KRPCQuery, std::shared_ptr<RequestInfo>, uint64_t>>
			m_Queries;
		std::map<Torrent::InfoHash, std::shared_ptr<DHTTorrent>> m_Torrents;
		DHTQueryLimiter m_QueryLimiter;
		uint64_t m_NextDHTExploratoryTime; // monotonic seconds
	};
}
}

#endif

#endif
