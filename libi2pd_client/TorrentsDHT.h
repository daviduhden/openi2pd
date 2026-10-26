/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TorrentsDHT.
 *
 * Declares the DHT node identity, bucket routing table, KRPC request
 * tracking, bounded peer/token stores and the service that exchanges them
 * over I2P datagrams. The side-effect free KRPC frame decoder, the bounded
 * issued-token store, the anonymous raw-frame budget and the service-thread
 * hand-off queue live here so regressions can pin them without sockets.
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
#include <atomic>
#include <deque>
#include <list>
#include <map>
#include <mutex>
#include <set>
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
#include "Datagram.h"
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
	// One KRPC frame is bounded independently of the I2P datagram limit; the
	// decoder must never look at more than this, and a larger datagram is
	// dropped before parsing.
	constexpr size_t MAX_DHT_KRPC_FRAME = 32768;
	constexpr size_t MAX_DHT_TRANSACTION_ID = 16;
	constexpr size_t MAX_DHT_QUERY_NAME = 64;
	constexpr size_t MAX_DHT_ERROR_TEXT = 1024;
	// A destination can mint many valid node IDs because only six of the 20
	// node-ID bytes are bound to its identity and port. Cap how many routing
	// entries one peer may own so a single destination cannot fill buckets.
	constexpr size_t MAX_DHT_NODES_PER_PEER = 2;
	// A destination that keeps issuing get_peers may not evict every other
	// destination's token: the store replaces only that node's oldest entry.
	constexpr size_t MAX_DHT_TOKENS_PER_NODE = 16;
	// A raw KRPC frame carries no authenticated sender, so one fixed global
	// window bounds the parsing work an anonymous flood can cause.
	constexpr size_t MAX_DHT_RAW_FRAMES = 1024;
	constexpr uint64_t DHT_RAW_FRAME_RATE_WINDOW = 60; // in seconds
	// Inbound repliable datagrams are handed from the network thread to the
	// service thread through this bounded queue and drained on this cadence.
	constexpr size_t MAX_DHT_INCOMING_QUEUE = 64;
	constexpr size_t MAX_DHT_DATAGRAM_SIZE = 32768;
	constexpr int DHT_INCOMING_DRAIN_INTERVAL = 50; // in milliseconds
	// Periodic routing-state persistence; Stop() still performs the final save.
	constexpr int DHT_STATE_SAVE_INTERVAL = 900; // in seconds
	// The repliable KRPC port is `port` and the raw KRPC port is `port + 1`.
	// 65535 would wrap GetRPort() to 0, so the accepted range is 1..65534:
	// port 0 is never advertised and never expected.
	constexpr uint16_t DHT_MIN_PORT = 1;
	constexpr uint16_t DHT_MAX_PORT = 65534;
	constexpr bool IsValidDHTPort (uint32_t port)
	{
		return port >= DHT_MIN_PORT && port <= DHT_MAX_PORT;
	}
	// An unusable configured or persisted port is saturated into the accepted
	// range instead of being advertised, so GetRPort() can never wrap.
	constexpr uint16_t ClampDHTPort (uint16_t port)
	{
		if (IsValidDHTPort (port)) return port;
		return port == 0 ? DHT_MIN_PORT : DHT_MAX_PORT;
	}

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

	// std::array has no standard hash, and node IDs key the per-node token
	// quota. Node IDs are public routing identifiers and are never logged here.
	struct NodeIDHash
	{
		size_t operator() (const NodeID & id) const noexcept
		{
			uint64_t h = 1469598103934665603ULL; // FNV-1a offset basis
			for (uint8_t byte : id)
			{
				h ^= byte;
				h *= 1099511628211ULL;
			}
			return static_cast<size_t> (h);
		}
	};

	using NodeInfo = std::array<uint8_t, NodeID::len + i2p::data::IdentHash::len + 2>;
	// BEP 5 compact node record: node ID, destination hash and a big-endian
	// port. The layout is wire-fixed; only this size is normative here.
	constexpr size_t DHT_COMPACT_NODE_SIZE = NodeID::len + i2p::data::IdentHash::len + 2;
	static_assert (DHT_COMPACT_NODE_SIZE == 54, "compact node records stay 54 bytes");

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
				if (bucket->next && !bucket->next->IsEmpty ())
					bucket = bucket->next.get ();
				else
					return false;
			}
			return bucket->IsInBucket (m_OurNode);
		}

		std::shared_ptr<Node> AddNode (std::shared_ptr<Node> node);
		void RemoveNode (const NodeID & id);
		// Return the routing-table entry bound to a peer identity, if any.
		// Used to translate a timed-out ping (which only knows the peer) back
		// to the node ID checked against the heard-from window.
		std::shared_ptr<Node> FindNodeByPeer (const i2p::data::IdentHash & peer) const;
		void RemoveNodeByPeer (const i2p::data::IdentHash & peer);
		// Number of routing entries owned by one peer destination. Used by the
		// per-peer admission cap.
		size_t CountPeerNodes (const i2p::data::IdentHash & peer) const;
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
			return (!noSeeds || !seed) && (now < lastUpdate || now - lastUpdate < DHT_TORRENT_PEER_EXPIRATION_TIME);
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

	// Bounded record of info hashes whose metadata declared the swarm private.
	// A private hash must never be answered from get_peers, stored from
	// announce_peer or announced to the public DHT, so this set is checked by
	// every DHT entry point that could publish or accept peers. Insertion order
	// is retained so the oldest entry is evicted instead of growing without
	// limit; the cap equals MAX_DHT_TORRENTS, which is the same bound as the
	// served peer map, so eviction requires more distinct private torrents than
	// a session can hold at once.
	class PrivateInfoHashSet
	{
	public:
		static constexpr size_t MaxHashes = MAX_DHT_TORRENTS;
		enum class Admission
		{
			Inserted = 0,
			AlreadyKnown,
			EvictedOldest
		};

		Admission Insert (const Torrent::InfoHash & infoHash);
		bool Erase (const Torrent::InfoHash & infoHash);
		bool Contains (const Torrent::InfoHash & infoHash) const { return m_Hashes.contains (infoHash); }
		size_t Size () const { return m_Hashes.size (); }

	private:
		std::set<Torrent::InfoHash> m_Hashes;
		std::deque<Torrent::InfoHash> m_Order; // insertion order, same bound
	};

	constexpr std::array<std::string_view, eNumKRPCQueries> KRPCQueryStr { "ping", "find_node", "get_peers",
		"announce_peer" };

	// Query name to enum, or nullopt for an unknown (but well-formed) name.
	// Unknown names are not a framing error: the caller decides whether to log
	// and drop, exactly as before.
	std::optional<KRPCQuery> GetKRPCQueryKind (std::string_view name);

	enum class DHTFrameKind : uint8_t
	{
		Malformed = 0,
		Query, // y="q"
		Response, // y="r"
		Error // y="e"
	};

	// Top-level KRPC envelope. The views point into the decoded input and stay
	// valid only while it does. The nested `a`/`r` dictionaries are returned
	// raw: their fields are decoded by the handler that knows which query or
	// response they belong to, so this decoder stays side-effect free.
	struct DHTFrame
	{
		DHTFrameKind kind = DHTFrameKind::Malformed;
		std::string_view transaction; // 1..16 bytes, BEP 5 `t`
		std::string_view query; // y="q": query name
		std::string_view arguments; // y="q": the whole encoded `a` dictionary
		std::string_view response; // y="r": the whole encoded `r` dictionary
		int32_t errorCode = 0; // y="e"
		std::string_view errorText; // y="e", bounded and never logged

		bool IsValid () const { return kind != DHTFrameKind::Malformed; }
	};

	// Decode the KRPC envelope. The dict must consume the whole input, keys
	// must be unique, the envelope is closed (y/t/q/a/r/e plus the optional
	// BEP 5 `v` client version, which is ignored), `y` must be exactly "q",
	// "r" or "e", `t` must be 1..16 bytes, and a query/response must carry
	// exactly one well-formed `a`/`r` dictionary. Never throws, never
	// allocates more than the input and never logs.
	DHTFrame DecodeDHTFrame (std::string_view encoded);

	// Decode up to `maxCount` compact BEP 5 node records. Records that fail the
	// node-ID check are skipped, never repaired; work is bounded by both the
	// input length and maxCount.
	size_t DecodeCompactNodes (std::string_view nodes, std::vector<Node> & out,
		size_t maxCount = MAX_DHT_NODES_PER_RESPONSE);
	// Decode a BEP 5 `values` list (or an already parsed entry list) into peer
	// destination hashes. Wrong-length and all-zero entries are dropped.
	size_t DecodeCompactPeers (const std::vector<std::string_view> & peers, std::vector<i2p::data::IdentHash> & out,
		size_t maxCount = MAX_DHT_VALUES_PER_RESPONSE);
	size_t DecodeCompactPeers (std::string_view encodedList, std::vector<i2p::data::IdentHash> & out,
		size_t maxCount = MAX_DHT_VALUES_PER_RESPONSE);

	// A persisted routing state is a flat sequence of fixed-size NodeInfo
	// records. Empty, oversized and misaligned input is rejected before any
	// parser can walk it; the result is the record count, bounded by
	// `maxRecords`.
	std::optional<size_t> DecodeDHTStateRecords (std::span<const uint8_t> bytes,
		size_t maxRecords = MAX_DHT_LOADED_NODES);

	// One token issued in reply to a get_peers query. The wire token stays
	// opaque; the node ID, destination hash, repliable port and info hash bind
	// it so announce_peer can only confirm the swarm it was issued for, from
	// the node and port that asked. Tokens other nodes send us are opaque
	// 0..64-byte values held in RequestInfo::tokens and never enter this store.
	struct DHTIncomingToken
	{
		GetPeersToken token; // exactly 8 wire bytes
		NodeID nodeID;
		i2p::data::IdentHash peer;
		Torrent::InfoHash infoHash;
		uint16_t port = 0;
		uint64_t issued = 0; // monotonic seconds
	};

	// Bounded FIFO of issued tokens. Insertion and oldest-first eviction are
	// constant time; a per-node quota means one destination that keeps issuing
	// get_peers can only reclaim its own tokens, never every other
	// destination's. A removed or newly private info hash is purged explicitly.
	class DHTTokenStore
	{
	public:
		static constexpr size_t MaxTokens = MAX_DHT_INCOMING_TOKENS; // 4096
		static constexpr size_t MaxTokensPerNode = MAX_DHT_TOKENS_PER_NODE; // 16
		static constexpr uint64_t TokenLifetime = DHT_INCOMING_GET_PEERS_TOKEN_EXPIRATION_TIME; // 600 s

		static bool IsValidToken (std::string_view token) { return token.size () == 8; }
		static bool IsValidPort (uint32_t port) { return port > 0 && port < 65535; }

		enum class Admission
		{
			Issued = 0,
			AlreadyPresent, // the same 8-byte token is still live; the caller retries
			ReplacedOwnOldest, // this node's quota was full; its own oldest token went
			EvictedOldest, // the global capacity was full; the oldest token went
			Rejected // malformed entry, nothing stored
		};
		// `entry.issued` is the issue time in monotonic seconds.
		Admission Issue (const DHTIncomingToken & entry);

		enum class Validation
		{
			Valid = 0,
			Invalid, // malformed token or port
			Unknown, // never issued, already consumed or evicted
			Expired, // older than TokenLifetime; the entry is removed
			WrongInfoHash,
			WrongNode,
			WrongPort
		};
		// Expired entries are removed; every other rejection keeps the entry so
		// a legitimate announce from the right node still succeeds.
		Validation Validate (std::string_view token, const NodeID & nodeID, const Torrent::InfoHash & infoHash,
			uint16_t port, uint64_t now, DHTIncomingToken & out);

		// Extend the lifetime of a validated token without reordering it.
		bool Refresh (std::string_view token, uint64_t now);
		bool Contains (std::string_view token) const;
		size_t Size () const { return m_Tokens.size (); }
		size_t SizeForNode (const NodeID & nodeID) const;
		void RemoveInfoHash (const Torrent::InfoHash & infoHash);
		size_t Expire (uint64_t now); // returns the number of expired tokens
		void Clear ();

	private:
		using Order = std::list<DHTIncomingToken>;
		void Erase (Order::iterator it);

		Order m_Order; // insertion order
		std::unordered_map<GetPeersToken, Order::iterator> m_Tokens; // token -> entry
		std::unordered_map<NodeID, std::deque<Order::iterator>, NodeIDHash> m_ByNode; // quota index
	};

	// Global fixed-window budget for sender-less raw KRPC frames. A monotonic
	// clock regression never refills the window: the current window is kept
	// until the clock passes its end, so a skewed clock cannot buy budget.
	class DHTRawFrameLimiter
	{
	public:
		static constexpr size_t MaxFrames = MAX_DHT_RAW_FRAMES;
		static constexpr uint64_t Window = DHT_RAW_FRAME_RATE_WINDOW;

		bool Allow (uint64_t now);
		size_t GetCount () const { return m_Count; }
		uint64_t GetWindowStart () const { return m_WindowStart; }
		bool HasWindow () const { return m_HasWindow; }

	private:
		uint64_t m_WindowStart = 0;
		size_t m_Count = 0;
		bool m_HasWindow = false;
	};

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
	// Shared between the DHT service thread, its timers and the disk domain.
	// Every new asynchronous callback captures a weak reference and enters the
	// DHT only after locking a live guard with `stopping` false; Stop() sets it
	// before cancelling, so a torn-down service is never entered from a timer
	// or a disk-domain task.
	struct DHTServiceState
	{
		std::atomic<bool> stopping { false };
		std::atomic<bool> saveInFlight { false };
	};

	class TorrentsDHT
	{
	public:
		TorrentsDHT (TorrentsTunnel & tunnel, uint16_t port, std::string_view dhtFileName);
		~TorrentsDHT ();
		TorrentsDHT (const TorrentsDHT &) = delete;
		TorrentsDHT & operator= (const TorrentsDHT &) = delete;

		// Set before Start. Delegated persistence never falls back to parent IO.
		using LoadState = std::function<bool (const std::filesystem::path &, std::vector<uint8_t> &, std::string &)>;
		using SaveState = std::function<bool (const std::filesystem::path &, std::span<const uint8_t>, std::string &)>;
		void SetPersistence (LoadState load, SaveState save)
		{
			m_LoadState = std::move (load);
			m_SaveState = std::move (save);
			m_DelegatedPersistence = true;
		}
		static constexpr size_t MaxStateBytes = 8 * 1024 * 1024;
		// Legacy flat DHT filename: one component beneath <datadir>/torrents,
		// no traversal, separators, reserved prefixes or control bytes.
		static bool IsValidStateFileName (std::string_view name);
		void Start ();
		void Stop ();

		// Both are always in 1..65535: 65535 is refused at construction and on
		// state load so the raw repliable port can never wrap to 0.
		uint16_t GetPort () const { return m_Port; };
		uint16_t GetRPort () const { return static_cast<uint16_t> (m_Port + 1); };
		// Service-thread entry point for raw (sender-less) KRPC frames. The
		// caller is the torrent tunnel's raw receiver, which owns sender
		// attribution; this method only bounds and decodes what it is given.
		void HandleRawDatagram (const uint8_t * buf, size_t len, uint16_t fromPort, uint16_t toPort);

		void SendPingQuery (const i2p::data::IdentHash & toIdent, uint16_t toPort);
		void GetPeersAndAnnounce (std::shared_ptr<Torrent> torrent);
		// Remove only our own rendezvous identity after stop/remove. Remote
		// announcers remain until their normal expiry.
		void Unannounce (const Torrent::InfoHash & infoHash);
		// Metadata disclosed that the swarm is private: remember the hash so no
		// later get_peers/announce_peer is answered or accepted for it, and purge
		// every DHT trace that was collected before the metadata arrived.
		void MarkPrivate (const Torrent::InfoHash & infoHash);
		// A torrent was removed: drop every DHT trace of its hash (served peers,
		// our own rendezvous identity and any token issued for that hash). The
		// private marker, if any, is retained so a removed private hash cannot be
		// served between removal and a later re-add.
		void Forget (const Torrent::InfoHash & infoHash);
		bool IsLocallyPrivate (const Torrent::InfoHash & infoHash) const
		{
			return m_PrivateInfoHashes.Contains (infoHash);
		}

		size_t GetNumBuckets () const { return m_RoutingTable ? m_RoutingTable->GetNumBuckets () : 0; }
		size_t GetNumNodes () const { return m_RoutingTable ? m_RoutingTable->GetNumNodes () : 0; }
		size_t GetNumTorrents () const { return m_Torrents.size (); }
		size_t GetNumPrivateInfoHashes () const { return m_PrivateInfoHashes.Size (); }
		size_t GetNumIncomingTokens () const { return m_IncomingTokens.Size (); }
		size_t GetNumHeardFrom () const { return m_HeardFrom.size (); }
		size_t GetNumFailedPeers () const { return m_FailedPeers.size (); }
		// Hand-off queue depth; the network thread writes it, the service thread
		// drains it.
		size_t GetNumQueuedDatagrams () const;

	private:
		// Bounded service-thread hand-off. The network thread only copies the
		// frame here under the mutex: it never parses, sends or touches routing,
		// query or torrent state.
		struct IncomingDatagram
		{
			std::shared_ptr<const i2p::data::IdentityEx> from;
			i2p::util::Mapping options;
			std::vector<uint8_t> payload;
			uint16_t fromPort = 0;
			uint16_t toPort = 0;
		};
		void EnqueueIncomingDatagram (const i2p::data::IdentityEx & from, uint16_t fromPort, uint16_t toPort,
			const uint8_t * buf, size_t len, const i2p::util::Mapping * options);
		void DrainIncomingDatagrams ();
		void ScheduleDHTIncomingDrain ();

		void HandleDatagram (const i2p::data::IdentityEx & from, uint16_t fromPort, uint16_t toPort,
			const uint8_t * buf, size_t len, const i2p::util::Mapping * options);
		// Drops every served peer set and every get_peers token bound to one info
		// hash. Shared by MarkPrivate() and Forget(); never logs the hash.
		void PurgeInfoHash (const Torrent::InfoHash & infoHash);
		void HandlePingQuery (const i2p::data::IdentHash & fromIdent, uint16_t fromPort, std::string_view transactionID,
			const NodeID & nodeID);
		void HandleGetPeersQuery (std::string_view transactionID, std::shared_ptr<Node> from,
			const Torrent::InfoHash & infoHash, bool noSeeds);
		void HandleFindNodeQuery (const i2p::data::IdentHash & fromIdent, uint16_t fromPort,
			std::string_view transactionID, const NodeID & target);
		void HandleResponse (std::string_view transactionID, const NodeID & nodeID, std::optional<GetPeersToken> token,
			const std::vector<std::string_view> & values, bool hasValues, std::string_view nodes, uint16_t fromPort);
		void HandleErrorReply (const DHTErrorReply & error, uint16_t fromPort);
		// Parse referral nodes without advancing the lookup; the public
		// Progress* variants add the advance for a nodes-only reply.
		void ParseGetPeersResponseNodes (std::shared_ptr<RequestInfo> info, std::string_view nodes);
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
		// Build the flat NodeInfo snapshot; service thread only, because it
		// reads the routing table and node identity.
		bool BuildStateSnapshot (std::vector<uint8_t> & bytes) const;
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

		void ScheduleDHTStateSave ();
		void HandleDHTStateSaveTimer (const boost::system::error_code & ecode);

	private:
		TorrentsTunnel & m_Tunnel;
		uint16_t m_Port;
		std::string m_DHTFileName;
		LoadState m_LoadState;
		SaveState m_SaveState;
		bool m_DelegatedPersistence = false;
		NodeID m_NodeID;
		boost::asio::steady_timer m_DHTUpdateCheckTimer, m_DHTExpirationCheckTimer, m_DHTSendPingCheckTimer,
			m_DHTQueryExpirationCheckTimer, m_DHTIncomingDrainTimer, m_DHTStateSaveTimer;
		std::unique_ptr<RoutingTable> m_RoutingTable;
		std::map<NodeID, uint64_t> m_HeardFrom; // nodeid -> time in monotonic seconds
		std::unordered_map<i2p::data::IdentHash, uint64_t> m_FailedPeers; // peer -> failure time in monotonic seconds
		DHTTokenStore m_IncomingTokens;
		DHTRawFrameLimiter m_RawFrameLimiter;
		// transactionID -> (ident, port, query,  request info, time in monotonic seconds)
		std::unordered_map<uint64_t,
			std::tuple<i2p::data::IdentHash, uint16_t, KRPCQuery, std::shared_ptr<RequestInfo>, uint64_t>>
			m_Queries;
		std::map<Torrent::InfoHash, std::shared_ptr<DHTTorrent>> m_Torrents;
		PrivateInfoHashSet m_PrivateInfoHashes;
		DHTQueryLimiter m_QueryLimiter;
		i2p::datagram::DatagramDestination::ReceiverRegistration m_DatagramReceiver;
		std::shared_ptr<DHTServiceState> m_ServiceState = std::make_shared<DHTServiceState> ();
		mutable std::mutex m_IncomingMutex;
		std::deque<IncomingDatagram> m_Incoming;
		uint64_t m_NextDHTExploratoryTime; // monotonic seconds
	};
}
}

#endif

#endif
