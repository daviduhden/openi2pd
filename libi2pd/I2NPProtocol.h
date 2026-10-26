/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: I2NPProtocol.
 *
 * I2NP wire format: message type IDs, field offsets, buffer accessors and header conversion
 * for NTCP2 framing, with expiration and checksum handling. Also defines tunnel-build record
 * layouts, database lookup/search-reply bounds, strict DatabaseSearchReply parsing, message
 * factories and a handler that throttles tunnel-build traffic. The dispatcher applies Java's
 * expiration window, a bounded router-wide duplicate filter for non-tunnel messages, and the
 * clove-type allow-list that depends on whether the enclosing garlic crossed a tunnel.
 *
 */

#ifndef I2NP_PROTOCOL_H__
#define I2NP_PROTOCOL_H__

#include <inttypes.h>
#include <string.h>
#include <atomic>
#include <deque>
#include <mutex>
#include <unordered_set>
#include <memory>
#include <list>
#include <functional>
#include <vector>
#include "Crypto.h"
#include "I2PEndian.h"
#include "Identity.h"
#include "RouterInfo.h"
#include "LeaseSet.h"

namespace i2p
{
// I2NP header
const size_t I2NP_HEADER_TYPEID_OFFSET = 0;
const size_t I2NP_HEADER_MSGID_OFFSET = I2NP_HEADER_TYPEID_OFFSET + 1;
const size_t I2NP_HEADER_EXPIRATION_OFFSET = I2NP_HEADER_MSGID_OFFSET + 4;
const size_t I2NP_HEADER_SIZE_OFFSET = I2NP_HEADER_EXPIRATION_OFFSET + 8;
const size_t I2NP_HEADER_CHKS_OFFSET = I2NP_HEADER_SIZE_OFFSET + 2;
const size_t I2NP_HEADER_SIZE = I2NP_HEADER_CHKS_OFFSET + 1;

// I2NP short header
const size_t I2NP_SHORT_HEADER_TYPEID_OFFSET = 0;
const size_t I2NP_SHORT_HEADER_EXPIRATION_OFFSET = I2NP_SHORT_HEADER_TYPEID_OFFSET + 1;
const size_t I2NP_SHORT_HEADER_SIZE = I2NP_SHORT_HEADER_EXPIRATION_OFFSET + 4;

// I2NP NTCP2 header
const size_t I2NP_NTCP2_HEADER_SIZE = I2NP_HEADER_EXPIRATION_OFFSET + 4;

// Tunnel Gateway header
const size_t TUNNEL_GATEWAY_HEADER_TUNNELID_OFFSET = 0;
const size_t TUNNEL_GATEWAY_HEADER_LENGTH_OFFSET = TUNNEL_GATEWAY_HEADER_TUNNELID_OFFSET + 4;
const size_t TUNNEL_GATEWAY_HEADER_SIZE = TUNNEL_GATEWAY_HEADER_LENGTH_OFFSET + 2;

// DeliveryStatus
const size_t DELIVERY_STATUS_MSGID_OFFSET = 0;
const size_t DELIVERY_STATUS_TIMESTAMP_OFFSET = DELIVERY_STATUS_MSGID_OFFSET + 4;
const size_t DELIVERY_STATUS_SIZE = DELIVERY_STATUS_TIMESTAMP_OFFSET + 8;

// Java DeliveryStatusMessage carries exactly msgID (4) + timestamp (8). Any
// other payload length means the sender and this handler disagree about the
// layout, so the message must not confirm or clear any delivery state.
constexpr bool IsValidDeliveryStatusPayload (size_t payloadLen) noexcept
{
	return payloadLen == DELIVERY_STATUS_SIZE;
}

// TunnelTest
const size_t TUNNEL_TEST_MSGID_OFFSET = 0;
const size_t TUNNEL_TEST_TIMESTAMP_OFFSET = TUNNEL_TEST_MSGID_OFFSET + 4;
const size_t TUNNEL_TEST_SIZE = TUNNEL_TEST_TIMESTAMP_OFFSET + 8;

// DatabaseStore
const size_t DATABASE_STORE_KEY_OFFSET = 0;
const size_t DATABASE_STORE_TYPE_OFFSET = DATABASE_STORE_KEY_OFFSET + 32;
const size_t DATABASE_STORE_REPLY_TOKEN_OFFSET = DATABASE_STORE_TYPE_OFFSET + 1;
const size_t DATABASE_STORE_HEADER_SIZE = DATABASE_STORE_REPLY_TOKEN_OFFSET + 4;
// Java DatabaseStoreMessage.readMessage() masks the type byte with 0x0f
// ("ignore other 4 bits ... in preparation for future options"). Keep the
// same mask so future-flagged stores are still recognized.
const uint8_t DATABASE_STORE_TYPE_MASK = 0x0f;

// TunnelBuild
const size_t TUNNEL_BUILD_RECORD_SIZE = 528;
const size_t SHORT_TUNNEL_BUILD_RECORD_SIZE = 218;

// BuildRequestRecordEncrypted
const size_t BUILD_REQUEST_RECORD_TO_PEER_OFFSET = 0;
const size_t BUILD_REQUEST_RECORD_ENCRYPTED_OFFSET = BUILD_REQUEST_RECORD_TO_PEER_OFFSET + 16;

// ECIES BuildRequestRecordClearText
const size_t ECIES_BUILD_REQUEST_RECORD_RECEIVE_TUNNEL_OFFSET = 0;
const size_t ECIES_BUILD_REQUEST_RECORD_NEXT_TUNNEL_OFFSET = ECIES_BUILD_REQUEST_RECORD_RECEIVE_TUNNEL_OFFSET + 4;
const size_t ECIES_BUILD_REQUEST_RECORD_NEXT_IDENT_OFFSET = ECIES_BUILD_REQUEST_RECORD_NEXT_TUNNEL_OFFSET + 4;
const size_t ECIES_BUILD_REQUEST_RECORD_LAYER_KEY_OFFSET = ECIES_BUILD_REQUEST_RECORD_NEXT_IDENT_OFFSET + 32;
const size_t ECIES_BUILD_REQUEST_RECORD_IV_KEY_OFFSET = ECIES_BUILD_REQUEST_RECORD_LAYER_KEY_OFFSET + 32;
const size_t ECIES_BUILD_REQUEST_RECORD_REPLY_KEY_OFFSET = ECIES_BUILD_REQUEST_RECORD_IV_KEY_OFFSET + 32;
const size_t ECIES_BUILD_REQUEST_RECORD_REPLY_IV_OFFSET = ECIES_BUILD_REQUEST_RECORD_REPLY_KEY_OFFSET + 32;
const size_t ECIES_BUILD_REQUEST_RECORD_FLAG_OFFSET = ECIES_BUILD_REQUEST_RECORD_REPLY_IV_OFFSET + 16;
const size_t ECIES_BUILD_REQUEST_RECORD_MORE_FLAGS_OFFSET = ECIES_BUILD_REQUEST_RECORD_FLAG_OFFSET + 1;
const size_t ECIES_BUILD_REQUEST_RECORD_REQUEST_TIME_OFFSET = ECIES_BUILD_REQUEST_RECORD_MORE_FLAGS_OFFSET + 3;
const size_t ECIES_BUILD_REQUEST_RECORD_REQUEST_EXPIRATION_OFFSET = ECIES_BUILD_REQUEST_RECORD_REQUEST_TIME_OFFSET + 4;
const size_t ECIES_BUILD_REQUEST_RECORD_SEND_MSG_ID_OFFSET = ECIES_BUILD_REQUEST_RECORD_REQUEST_EXPIRATION_OFFSET + 4;
const size_t ECIES_BUILD_REQUEST_RECORD_PADDING_OFFSET = ECIES_BUILD_REQUEST_RECORD_SEND_MSG_ID_OFFSET + 4;
const size_t ECIES_BUILD_REQUEST_RECORD_CLEAR_TEXT_SIZE = 464;

// ECIES BuildResponseRecord
const size_t ECIES_BUILD_RESPONSE_RECORD_OPTIONS_OFFSET = 0;
const size_t ECIES_BUILD_RESPONSE_RECORD_RET_OFFSET = 511;

// ShortRequestRecordClearText
const size_t SHORT_REQUEST_RECORD_ENCRYPTED_OFFSET = 16;
const size_t SHORT_REQUEST_RECORD_RECEIVE_TUNNEL_OFFSET = 0;
const size_t SHORT_REQUEST_RECORD_NEXT_TUNNEL_OFFSET = SHORT_REQUEST_RECORD_RECEIVE_TUNNEL_OFFSET + 4;
const size_t SHORT_REQUEST_RECORD_NEXT_IDENT_OFFSET = SHORT_REQUEST_RECORD_NEXT_TUNNEL_OFFSET + 4;
const size_t SHORT_REQUEST_RECORD_FLAG_OFFSET = SHORT_REQUEST_RECORD_NEXT_IDENT_OFFSET + 32;
const size_t SHORT_REQUEST_RECORD_MORE_FLAGS_OFFSET = SHORT_REQUEST_RECORD_FLAG_OFFSET + 1;
const size_t SHORT_REQUEST_RECORD_LAYER_ENCRYPTION_TYPE = SHORT_REQUEST_RECORD_MORE_FLAGS_OFFSET + 2;
const size_t SHORT_REQUEST_RECORD_REQUEST_TIME_OFFSET = SHORT_REQUEST_RECORD_LAYER_ENCRYPTION_TYPE + 1;
const size_t SHORT_REQUEST_RECORD_REQUEST_EXPIRATION_OFFSET = SHORT_REQUEST_RECORD_REQUEST_TIME_OFFSET + 4;
const size_t SHORT_REQUEST_RECORD_SEND_MSG_ID_OFFSET = SHORT_REQUEST_RECORD_REQUEST_EXPIRATION_OFFSET + 4;
const size_t SHORT_REQUEST_RECORD_TUNNEL_BUILD_OPTIONS_OFFSET = SHORT_REQUEST_RECORD_SEND_MSG_ID_OFFSET + 4;
const size_t SHORT_REQUEST_RECORD_CLEAR_TEXT_SIZE = 154;

// ShortResponseRecord
const size_t SHORT_RESPONSE_RECORD_OPTIONS_OFFSET = 0;
const size_t SHORT_RESPONSE_RECORD_RET_OFFSET = 201;

enum I2NPMessageType
{
	eI2NPDatabaseStore = 1,
	eI2NPDatabaseLookup = 2,
	eI2NPDatabaseSearchReply = 3,
	eI2NPDeliveryStatus = 10,
	eI2NPGarlic = 11,
	eI2NPTunnelData = 18,
	eI2NPTunnelGateway = 19,
	eI2NPData = 20,
	eI2NPTunnelBuild = 21,
	eI2NPTunnelBuildReply = 22,
	eI2NPVariableTunnelBuild = 23,
	eI2NPVariableTunnelBuildReply = 24,
	eI2NPShortTunnelBuild = 25,
	eI2NPShortTunnelBuildReply = 26,
	eI2NPTunnelTest = 231
};

const uint8_t TUNNEL_BUILD_RECORD_GATEWAY_FLAG = 0x80;
const uint8_t TUNNEL_BUILD_RECORD_ENDPOINT_FLAG = 0x40;
const int NUM_TUNNEL_BUILD_RECORDS = 8;

// DatabaseLookup flags
const uint8_t DATABASE_LOOKUP_DELIVERY_FLAG = 0x01;
const uint8_t DATABASE_LOOKUP_ENCRYPTION_FLAG = 0x02;
// DatabaseLookupMessage.FLAG_RATCHET (0x10, proposal 154 bit 4). Alone it
// selects an ECIES tag reply; together with ENCRYPTION_FLAG it means a
// 32-byte ratchet public key and no tag.
const uint8_t DATABASE_LOOKUP_RATCHET_FLAG = 0x10;
const uint8_t DATABASE_LOOKUP_MAX_NUM_TAGS = 32; // proposal 154
const size_t DATABASE_LOOKUP_MAX_NUM_PEERS = 512; // DatabaseLookupMessage.MAX_NUM_PEERS
const uint8_t DATABASE_LOOKUP_TYPE_FLAGS_MASK = 0x0C;
const uint8_t DATABASE_LOOKUP_TYPE_NORMAL_LOOKUP = 0;
const uint8_t DATABASE_LOOKUP_TYPE_LEASESET_LOOKUP = 0x04; // 0100
const uint8_t DATABASE_LOOKUP_TYPE_ROUTERINFO_LOOKUP = 0x08; // 1000
const uint8_t DATABASE_LOOKUP_TYPE_EXPLORATORY_LOOKUP = 0x0C; // 1100

// DatabaseSearchReply: key (32) + num (1) + num*32 peer hashes + from (32).
// I2NP specifies num as a 1-byte integer (0-255), a typical count of 3 and a
// recommended maximum of 16 returned hashes.
const size_t DATABASE_SEARCH_REPLY_KEY_OFFSET = 0;
const size_t DATABASE_SEARCH_REPLY_COUNT_OFFSET = DATABASE_SEARCH_REPLY_KEY_OFFSET + 32;
const size_t DATABASE_SEARCH_REPLY_PEERS_OFFSET = DATABASE_SEARCH_REPLY_COUNT_OFFSET + 1;
const size_t DATABASE_SEARCH_REPLY_MIN_SIZE = DATABASE_SEARCH_REPLY_PEERS_OFFSET + 32; // num=0 plus from
const size_t DATABASE_SEARCH_REPLY_MAX_NUM_PEERS = 255;

namespace tunnel
{
	class InboundTunnel;
	class TunnelPool;
}

const int CONGESTION_LEVEL_MEDIUM = 70;
const int CONGESTION_LEVEL_HIGH = 90;
const int CONGESTION_LEVEL_FULL = 100;

const size_t I2NP_MAX_MESSAGE_SIZE = 62708;
const size_t I2NP_MAX_SHORT_MESSAGE_SIZE = 4096;
const size_t I2NP_MAX_MEDIUM_MESSAGE_SIZE = 16384;
const unsigned int I2NP_MESSAGE_LOCAL_EXPIRATION_TIMEOUT_FACTOR = 3; // multiples of RTT
const unsigned int I2NP_MESSAGE_LOCAL_EXPIRATION_TIMEOUT_MIN = 200000; // in microseconds
const unsigned int I2NP_MESSAGE_LOCAL_EXPIRATION_TIMEOUT_MAX = 2000000; // in microseconds
const unsigned int I2NP_MESSAGE_LOCAL_EXPIRATION_TIMEOUT_TRANSIT = 4000000; // in microseconds
const unsigned int I2NP_MESSAGE_EXPIRATION_TIMEOUT = 8000; // in milliseconds (as initial RTT)
const unsigned int I2NP_MESSAGE_CLOCK_SKEW = 60 * 1000; // 1 minute in milliseconds

/**
 * Java MessageValidator window applied before an I2NP message reaches a
 * handler: an expiration date older than 90 s or further than 240 s in the
 * future is invalid. The stricter per-hop tunnel window
 * (I2NP_MESSAGE_CLOCK_SKEW) of the tunnel data path is unchanged; this only
 * guards the dispatcher, where a direct, garlic-clove or tunnel-delivered
 * message would otherwise reach netDb, garlic and delivery-status handlers
 * with an unbounded expiration date.
 */
const uint64_t I2NP_DISPATCH_MAX_AGE = 90 * 1000; // milliseconds
const uint64_t I2NP_DISPATCH_MAX_FUTURE = 240 * 1000; // milliseconds

// A garlic payload announces its clove count in a single byte. Java's clove
// walker processes a bounded number of cloves per message; the bound also
// stops one authenticated garlic from turning into thousands of dispatch
// calls, clove-replay lookups and log lines.
const int I2NP_MAX_CLOVES = 32;

constexpr bool IsFreshI2NPExpiration (uint64_t expiration, uint64_t now) noexcept
{
	// Written as additions so a hostile 64-bit expiration cannot underflow;
	// UINT64_MAX wraps to a small value and is rejected.
	return expiration + I2NP_DISPATCH_MAX_AGE >= now && expiration <= now + I2NP_DISPATCH_MAX_FUTURE;
}

/**
 * Throttled diagnostics for a drop path an unauthenticated peer can trigger.
 * The first drop and every `logEvery`-th drop are logged; the total stays
 * readable so an operator can still see the rate.
 */
class I2NPDropCounter
{
public:
	explicit I2NPDropCounter (uint64_t logEvery = 256)
		: m_LogEvery (logEvery ? logEvery : 1)
	{
	}

	// Records one drop; returns true when this drop should be logged.
	bool Record ()
	{
		const uint64_t n = m_Count.fetch_add (1, std::memory_order_relaxed) + 1;
		return n == 1 || (n % m_LogEvery) == 0;
	}
	uint64_t GetCount () const { return m_Count.load (std::memory_order_relaxed); }
	void Reset () { m_Count.store (0, std::memory_order_relaxed); }

private:
	std::atomic<uint64_t> m_Count { 0 };
	uint64_t m_LogEvery;
};

/**
 * Drop accounting for the I2NP dispatcher and the garlic clove walker. All
 * counters are relaxed atomics: they only feed throttled diagnostics and the
 * administrative view, never a decision.
 */
struct I2NPDispatchCounters
{
	std::atomic<uint64_t> expired { 0 };
	std::atomic<uint64_t> duplicate { 0 };
	std::atomic<uint64_t> disallowedCloveType { 0 };
	std::atomic<uint64_t> badCloveCount { 0 };
	std::atomic<uint64_t> staleClove { 0 };
	std::atomic<uint64_t> replayedClove { 0 };
	std::atomic<uint64_t> wrongDestination { 0 };
	std::atomic<uint64_t> oversizedMessage { 0 };

	void Reset ()
	{
		expired.store (0, std::memory_order_relaxed);
		duplicate.store (0, std::memory_order_relaxed);
		disallowedCloveType.store (0, std::memory_order_relaxed);
		badCloveCount.store (0, std::memory_order_relaxed);
		staleClove.store (0, std::memory_order_relaxed);
		replayedClove.store (0, std::memory_order_relaxed);
		wrongDestination.store (0, std::memory_order_relaxed);
		oversizedMessage.store (0, std::memory_order_relaxed);
	}
};

I2NPDispatchCounters & GetI2NPDispatchCounters ();

// Java DecayingHashSet.TIME_MASK: the expiration second is masked before it is
// mixed into the duplicate key.
const uint32_t I2NP_DUPLICATE_TIME_MASK = 0x7fffffff;
// Per-context salts keep a router-wide filter and a destination-scoped garlic
// clove filter from treating each other's message IDs as duplicates.
const uint32_t I2NP_DUPLICATE_SALT_ROUTER = 0x9e3779b9;
const uint32_t I2NP_DUPLICATE_SALT_DESTINATION = 0x85ebca6b;
const size_t I2NP_DUPLICATE_MAX_ENTRIES = 8192; // per generation
// One entry stays visible for one to two windows, i.e. 5-10 minutes, which is
// far longer than the 90/240 s dispatch window it has to cover.
const uint64_t I2NP_DUPLICATE_GENERATION_TIMEOUT = 300; // seconds

/**
 * Java DecayingHashSet key for I2NP duplicate suppression: the message ID is
 * combined with the message's expiration second and a per-context salt. The
 * expiration component means a peer cannot keep one message ID alive forever
 * by resending it with a later date, and it separates two different messages
 * that happen to share an ID.
 */
constexpr uint32_t GetI2NPDuplicateKey (uint32_t msgID, uint64_t expiration, uint32_t salt) noexcept
{
	const uint32_t seconds = static_cast<uint32_t> ((expiration / 1000) & I2NP_DUPLICATE_TIME_MASK);
	uint32_t key = msgID ^ salt;
	key *= 0x9e3779b1U; // odd multiplier spreads adjacent message IDs
	key ^= seconds * 0x85ebca6bU;
	key ^= key >> 15;
	return key;
}

/**
 * Bounded, decaying duplicate filter. Two generations are kept and rotated by
 * age (Java DecayingBloomFilter's double-buffered set), so an entry survives at
 * most two generation windows and the memory is fixed. The oldest key of the
 * current generation is evicted when the hard bound is reached: keeping the
 * bound is more important than perfect recall, because a dropped entry can only
 * let one duplicate through, never block a new message.
 */
class I2NPDuplicateFilter
{
public:
	explicit I2NPDuplicateFilter (
		size_t maxEntries = I2NP_DUPLICATE_MAX_ENTRIES, uint64_t generationTimeout = I2NP_DUPLICATE_GENERATION_TIMEOUT)
		: m_GenerationTimeout (generationTimeout ? generationTimeout : 1)
		, m_MaxEntries (maxEntries ? maxEntries : 1)
	{
	}

	// Returns true when the key was already present, i.e. a duplicate.
	bool CheckAndInsert (uint32_t key, uint64_t nowSeconds);
	size_t GetSize () const;
	void Clear ();

private:
	void Rotate (uint64_t nowSeconds);

	mutable std::mutex m_Mutex;
	std::unordered_set<uint32_t> m_Current;
	std::unordered_set<uint32_t> m_Previous;
	std::deque<uint32_t> m_Order; // insertion order in m_Current, for eviction
	uint64_t m_NextRotation = 0;
	uint64_t m_GenerationTimeout;
	size_t m_MaxEntries;
};

// Router-wide filter used by HandleI2NPMessage(). Destination-scoped filters
// (garlic clove replay) own their instance and use the destination salt.
I2NPDuplicateFilter & GetI2NPRouterDuplicateFilter ();

/**
 * Where a garlic clove came from.
 *
 * Java separates a garlic message received directly from a peer
 * (HandleGarlicMessageJob) from one that arrived through an I2NP tunnel
 * (InboundMessageDistributor). A clove has no InboundTunnel pointer of its own,
 * so the enclosing garlic's origin must be carried explicitly.
 */
enum class I2NPCloveOrigin : uint8_t
{
	Direct = 0, // direct transport, not inside garlic
	DirectGarlic, // clove of a garlic received directly from a peer
	TunnelGarlic // clove of a garlic that arrived through an I2NP tunnel
};

/// Message view over a byte buffer containing the normal 16-byte I2NP header.
/// `buf` is borrowed; the allocating I2NPMessageBuffer specialization owns its
/// storage. The logical message occupies [offset, len), with maxLen limiting
/// writes. Accessors do not check framing, so callers must validate lengths
/// before reading an untrusted header or payload.
/// @see https://i2p.net/en/docs/specs/i2np/
struct I2NPMessage
{
	uint8_t * buf;
	size_t len, offset, maxLen;
	std::shared_ptr<i2p::tunnel::InboundTunnel> from;
	// A clove has no InboundTunnel pointer even when the enclosing garlic came
	// through an exploratory tunnel. Preserve that untrusted origin so netDb
	// does not treat a clove-delivered DatabaseStore/Lookup as direct.
	bool fromClove = false;
	// True only when the enclosing garlic was delivered through an I2NP
	// tunnel. `fromClove` alone is ambiguous: a garlic received directly from
	// a peer is trusted enough for a DatabaseLookup/DatabaseStore reply token,
	// a tunnel-delivered one is not. Set by the clove dispatch paths from the
	// outer garlic's `from` pointer; never set for a non-clove message.
	bool fromTunnel = false;
	std::function<void ()> onDrop;
	uint64_t enqueueTime; // monotonic microseconds

	I2NPMessage ()
		: buf (nullptr)
		, len (I2NP_HEADER_SIZE + 2)
		, offset (2)
		, maxLen (0)
		, from (nullptr)
		, enqueueTime (0) { }; // reserve 2 bytes for NTCP header

	I2NPCloveOrigin GetCloveOrigin () const
	{
		if (!fromClove) return I2NPCloveOrigin::Direct;
		return fromTunnel ? I2NPCloveOrigin::TunnelGarlic : I2NPCloveOrigin::DirectGarlic;
	}
	// True when this message crossed an I2NP tunnel, either directly
	// (`from` set) or as a clove of a tunnel-delivered garlic.
	bool IsTunnelOrigin () const { return from != nullptr || fromTunnel; }

	// header accessors
	uint8_t * GetHeader () { return GetBuffer (); };
	const uint8_t * GetHeader () const { return GetBuffer (); };
	void SetTypeID (uint8_t typeID) { GetHeader ()[I2NP_HEADER_TYPEID_OFFSET] = typeID; };
	uint8_t GetTypeID () const { return GetHeader ()[I2NP_HEADER_TYPEID_OFFSET]; };
	void SetMsgID (uint32_t msgID) { htobe32buf (GetHeader () + I2NP_HEADER_MSGID_OFFSET, msgID); };
	uint32_t GetMsgID () const { return bufbe32toh (GetHeader () + I2NP_HEADER_MSGID_OFFSET); };
	void SetExpiration (uint64_t expiration) { htobe64buf (GetHeader () + I2NP_HEADER_EXPIRATION_OFFSET, expiration); };
	void SetEnqueueTime (uint64_t mts) { enqueueTime = mts; };
	uint64_t GetExpiration () const { return bufbe64toh (GetHeader () + I2NP_HEADER_EXPIRATION_OFFSET); };
	uint64_t GetEnqueueTime () const { return enqueueTime; };
	void SetSize (uint16_t size) { htobe16buf (GetHeader () + I2NP_HEADER_SIZE_OFFSET, size); };
	uint16_t GetSize () const { return bufbe16toh (GetHeader () + I2NP_HEADER_SIZE_OFFSET); };
	void UpdateSize () { SetSize (static_cast<uint16_t> (GetPayloadLength ())); };
	void SetChks (uint8_t chks) { GetHeader ()[I2NP_HEADER_CHKS_OFFSET] = chks; };
	void UpdateChks ()
	{
		uint8_t hash[32];
		SHA256 (GetPayload (), GetPayloadLength (), hash);
		GetHeader ()[I2NP_HEADER_CHKS_OFFSET] = hash[0];
	}

	// payload
	uint8_t * GetPayload () { return GetBuffer () + I2NP_HEADER_SIZE; };
	const uint8_t * GetPayload () const { return GetBuffer () + I2NP_HEADER_SIZE; };
	uint8_t * GetBuffer () { return buf + offset; };
	const uint8_t * GetBuffer () const { return buf + offset; };
	size_t GetLength () const { return len - offset; };
	size_t GetPayloadLength () const { return GetLength () - I2NP_HEADER_SIZE; };

	void Align (size_t alignment)
	{
		if (len + alignment > maxLen) return;
		size_t rem = ((size_t)GetBuffer ()) % alignment;
		if (rem)
		{
			offset += (alignment - rem);
			len += (alignment - rem);
		}
	}

	/// Append at most the remaining capacity; a short result means truncation.
	/// @param buf1 Source bytes, valid for at least len1 bytes.
	/// @return Number of bytes copied, which may be less than len1.
	size_t Concat (const uint8_t * buf1, size_t len1)
	{
		// make sure with don't write beyond maxLen
		if (len + len1 > maxLen) len1 = maxLen - len;
		memcpy (buf + len, buf1, len1);
		len += len1;
		return len1;
	}

	/// Copy the message bytes and tunnel-origin metadata into this buffer.
	/// @pre This buffer has at least other.GetLength() bytes after its offset.
	/// Drop callback and enqueue time remain properties of the destination.
	I2NPMessage & operator= (const I2NPMessage & other)
	{
		memcpy (buf + offset, other.buf + other.offset, other.GetLength ());
		len = offset + other.GetLength ();
		from = other.from;
		fromClove = other.fromClove;
		fromTunnel = other.fromTunnel;
		return *this;
	}

	// for NTCP2 and SSU2
	uint8_t * GetNTCP2Header () { return GetPayload () - I2NP_NTCP2_HEADER_SIZE; };
	size_t GetNTCP2Length () const { return GetPayloadLength () + I2NP_NTCP2_HEADER_SIZE; };
	void FromNTCP2 ()
	{
		const uint8_t * ntcp2 = GetNTCP2Header ();
		memcpy (GetHeader () + I2NP_HEADER_TYPEID_OFFSET, ntcp2 + I2NP_HEADER_TYPEID_OFFSET, 5); // typeid + msgid
		SetExpiration (bufbe32toh (ntcp2 + I2NP_HEADER_EXPIRATION_OFFSET) * 1000LL);
		SetSize (static_cast<uint16_t> (len - offset - I2NP_HEADER_SIZE));
		SetChks (0);
	}
	void ToNTCP2 ()
	{
		uint8_t * ntcp2 = GetNTCP2Header ();
		htobe32buf (ntcp2 + I2NP_HEADER_EXPIRATION_OFFSET,
			static_cast<uint32_t> (bufbe64toh (GetHeader () + I2NP_HEADER_EXPIRATION_OFFSET) / 1000LL));
		memcpy (ntcp2 + I2NP_HEADER_TYPEID_OFFSET, GetHeader () + I2NP_HEADER_TYPEID_OFFSET, 5); // typeid + msgid
	}

	void FillI2NPMessageHeader (I2NPMessageType msgType, uint32_t msgID = 0, bool checksum = true);
	bool IsExpired () const;
	bool IsExpired (uint64_t ts) const; // in milliseconds

	void Drop ()
	{
		if (onDrop)
		{
			onDrop ();
			onDrop = nullptr;
		};
	}
};

template <int sz> struct I2NPMessageBuffer : public I2NPMessage
{
	I2NPMessageBuffer ()
	{
		buf = m_Buffer;
		maxLen = sz;
	};
	uint8_t m_Buffer[sz + 32]; // 16 alignment + 16 padding
};

// True when the dispatcher must drop the message because of its expiration
// date. A TunnelData cell carries no meaningful expiration (the endpoint
// validates the inner I2NP message after decryption), so it is exempt.
inline bool IsI2NPDispatchExpired (const I2NPMessage & msg, uint64_t now)
{
	if (msg.GetTypeID () == eI2NPTunnelData) return false;
	return !IsFreshI2NPExpiration (msg.GetExpiration (), now);
}

// True when this non-TunnelData message repeats one the dispatcher already
// saw. TunnelData is exempt (the tunnel IV filter rejects replays), and SSU2
// keeps its own per-session message-ID map.
bool IsI2NPDispatchDuplicate (const I2NPMessage & msg, uint64_t nowSeconds);

/**
 * Java InboundMessageDistributor.handleClove() allow-list for a clove of a
 * garlic that arrived through an I2NP tunnel. A clove may carry a data or
 * control-reply message only; tunnel/build/request types would inject directly
 * into the tunnel worker or bypass the build throttle. DatabaseLookup is
 * allowed because HandleGarlicMessageJob dispatches it to the netDb, where the
 * origin check below still decides whether it may be answered.
 */
constexpr bool IsAllowedCloveMessageType (uint8_t typeID) noexcept
{
	switch (typeID)
	{
	case eI2NPDatabaseStore:
	case eI2NPDatabaseLookup:
	case eI2NPDatabaseSearchReply:
	case eI2NPDeliveryStatus:
	case eI2NPGarlic:
	case eI2NPData:
	case eI2NPTunnelBuildReply:
	case eI2NPVariableTunnelBuildReply:
	case eI2NPShortTunnelBuildReply: return true;
	default: return false;
	}
}

/**
 * Clove types of a garlic received directly from a peer (Java
 * HandleGarlicMessageJob). The set is the tunnel set plus TunnelTest: a peer
 * that has no inbound tunnel of its own can only test ours through garlic, and
 * the tunnel-test handler validates the payload before use. TunnelData and the
 * tunnel/build request types stay rejected either way.
 */
constexpr bool IsAllowedDirectGarlicCloveMessageType (uint8_t typeID) noexcept
{
	return IsAllowedCloveMessageType (typeID) || typeID == eI2NPTunnelTest;
}

// The clove allow-list depends on where the enclosing garlic came from: only a
// tunnel-delivered garlic is Java's InboundMessageDistributor path.
constexpr bool IsAllowedCloveMessageType (uint8_t typeID, bool fromTunnel) noexcept
{
	return fromTunnel ? IsAllowedCloveMessageType (typeID) : IsAllowedDirectGarlicCloveMessageType (typeID);
}

/**
 * Java DatabaseLookup admission. A directly received message and a clove of a
 * garlic received directly from a peer are dispatched to the netDb; a message
 * or clove that crossed an I2NP tunnel is dropped. The caller still requires
 * floodfill mode.
 */
inline bool IsDatabaseLookupDispatchAllowed (const I2NPMessage & msg)
{
	return msg.from == nullptr && !msg.fromTunnel;
}

std::shared_ptr<I2NPMessage> NewI2NPMessage ();
std::shared_ptr<I2NPMessage> NewI2NPShortMessage ();
std::shared_ptr<I2NPMessage> NewI2NPMediumMessage ();
std::shared_ptr<I2NPMessage> NewI2NPTunnelMessage (bool endpoint);
std::shared_ptr<I2NPMessage> NewI2NPMessage (size_t len);

// Both factories return nullptr when the payload does not fit any I2NP buffer
// instead of handing back an empty or truncated message that still carries a
// header and a size field. Callers must check.
std::shared_ptr<I2NPMessage> CreateI2NPMessage (
	I2NPMessageType msgType, const uint8_t * buf, size_t len, uint32_t replyMsgID = 0);
std::shared_ptr<I2NPMessage> CreateI2NPMessage (
	const uint8_t * buf, size_t len, std::shared_ptr<i2p::tunnel::InboundTunnel> from = nullptr);
std::shared_ptr<I2NPMessage> CopyI2NPMessage (std::shared_ptr<I2NPMessage> msg);

std::shared_ptr<I2NPMessage> CreateTunnelTestMsg (uint32_t msgID);
std::shared_ptr<I2NPMessage> CreateDeliveryStatusMsg (uint32_t msgID);
std::shared_ptr<I2NPMessage> CreateRouterInfoDatabaseLookupMsg (const uint8_t * key, const uint8_t * from,
	uint32_t replyTunnelID, bool exploratory = false,
	std::unordered_set<i2p::data::IdentHash> * excludedPeers = nullptr);
std::shared_ptr<I2NPMessage> CreateLeaseSetDatabaseLookupMsg (const i2p::data::IdentHash & dest,
	const std::unordered_set<i2p::data::IdentHash> & excludedFloodfills,
	std::shared_ptr<const i2p::tunnel::InboundTunnel> replyTunnel, const uint8_t * replyKey, uint64_t replyTag);
std::shared_ptr<I2NPMessage> CreateDatabaseSearchReply (
	const i2p::data::IdentHash & ident, std::vector<i2p::data::IdentHash> routers);

// Strict structural parser for a DatabaseSearchReply payload (without the
// I2NP header). The declared count and the trailing "from" hash must fit in
// the supplied payload; trailing bytes beyond the defined body are ignored,
// matching Java DatabaseSearchReplyMessage.readMessage(). Semantic checks on
// the key, "from" and peer hashes belong to the caller because the sender
// hash is unauthenticated (I2NP DatabaseSearchReply notes).
struct DatabaseSearchReply
{
	i2p::data::IdentHash key;
	i2p::data::IdentHash from;
	std::vector<i2p::data::IdentHash> peers;
};

bool ParseDatabaseSearchReply (const uint8_t * buf, size_t len, DatabaseSearchReply & reply);

std::shared_ptr<I2NPMessage> CreateDatabaseStoreMsg (std::shared_ptr<const i2p::data::RouterInfo> router = nullptr,
	uint32_t replyToken = 0, std::shared_ptr<const i2p::tunnel::InboundTunnel> replyTunnel = nullptr);
std::shared_ptr<I2NPMessage> CreateDatabaseStoreMsg (
	const i2p::data::IdentHash & storeHash, std::shared_ptr<const i2p::data::LeaseSet> leaseSet); // for floodfill only
std::shared_ptr<I2NPMessage> CreateDatabaseStoreMsg (std::shared_ptr<const i2p::data::LocalLeaseSet> leaseSet,
	uint32_t replyToken = 0, std::shared_ptr<const i2p::tunnel::InboundTunnel> replyTunnel = nullptr);
bool IsRouterInfoMsg (std::shared_ptr<I2NPMessage> msg);

std::shared_ptr<I2NPMessage> CreateTunnelDataMsg (const uint8_t * buf);
std::shared_ptr<I2NPMessage> CreateTunnelDataMsg (uint32_t tunnelID, const uint8_t * payload);
std::shared_ptr<I2NPMessage> CreateEmptyTunnelDataMsg (bool endpoint);

std::shared_ptr<I2NPMessage> CreateTunnelGatewayMsg (uint32_t tunnelID, const uint8_t * buf, size_t len);
std::shared_ptr<I2NPMessage> CreateTunnelGatewayMsg (
	uint32_t tunnelID, I2NPMessageType msgType, const uint8_t * buf, size_t len, uint32_t replyMsgID = 0);
std::shared_ptr<I2NPMessage> CreateTunnelGatewayMsg (uint32_t tunnelID, std::shared_ptr<I2NPMessage> msg);

/// Total wire length of the I2NP message starting at @a msg, from the 16-bit
/// size field in its header. @a len is the number of readable bytes: the caller
/// is parsing an untrusted stream, so a short prefix or a declared size beyond
/// @a len yields @a len instead of an exception, and the offending message is
/// counted (the log is throttled because any peer can trigger it).
size_t GetI2NPMessageLength (const uint8_t * msg, size_t len);
void HandleI2NPMessage (std::shared_ptr<I2NPMessage> msg);

const uint64_t TUNNEL_BUILD_MESSAGES_MIN_INTERVAL = 500; // in milliseconds
const int MAX_NUM_THROTTLED_TUNNEL_BUILD_MESSAGES = 5;
const int MAX_NUM_DROPPED_TUNNEL_BUILD_MESSAGES = 1000;
/// Groups the I2NP messages a session accepted before dispatching them.
/// TunnelData and TunnelGateway messages are buffered and flushed in order,
/// while tunnel-build requests are rate limited by
/// `TUNNEL_BUILD_MESSAGES_MIN_INTERVAL` so a burst cannot monopolise the
/// tunnel worker; over the cap they are counted and dropped instead of queued.
/// One handler belongs to one session and `Flush()` runs on that session's
/// service thread.
class I2NPMessagesHandler
{
public:
	I2NPMessagesHandler ()
		: m_LastTunnelBuildMessageTimestamp (0)
		, m_NumThrottledTunnelBuildMessages (0)
		, m_NumDroppedTunnelBuildMessages (0) { };
	~I2NPMessagesHandler ();
	void PutNextMessage (std::shared_ptr<I2NPMessage> && msg);
	void Flush ();

private:
	std::list<std::shared_ptr<I2NPMessage>> m_TunnelMsgs, m_TunnelGatewayMsgs;
	uint64_t m_LastTunnelBuildMessageTimestamp; // in milliseconds
	int m_NumThrottledTunnelBuildMessages, m_NumDroppedTunnelBuildMessages;
};
}

#endif
