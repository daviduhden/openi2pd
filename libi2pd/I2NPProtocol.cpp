/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: I2NPProtocol.
 *
 * Implements I2NP message allocation, header/checksum framing and
 * type-specific constructors for database lookup/store/search-reply,
 * delivery status, tunnel data/gateway and tunnel-build messages, with
 * expiry and clock skew checks. The batching handler groups tunnel data
 * and gateway traffic and rate-limits tunnel-build messages under
 * bounded drop counters. The dispatcher applies Java's 90/240 s
 * expiration window, a bounded decaying duplicate filter for every
 * non-tunnel message and the origin-dependent clove allow-list before a
 * message reaches netDb, garlic or a status handler.
 *
 */

#include <string.h>
#include <atomic>
#include <algorithm>
#include <limits>
#include "Base.h"
#include "Log.h"
#include "I2PEndian.h"
#include "Timestamp.h"
#include "RouterContext.h"
#include "NetDb.hpp"
#include "Tunnel.h"
#include "TransitTunnel.h"
#include "I2NPProtocol.h"
#include "version.h"

namespace i2p
{
std::shared_ptr<I2NPMessage> NewI2NPMessage ()
{
	return std::make_shared<I2NPMessageBuffer<I2NP_MAX_MESSAGE_SIZE>> ();
}

std::shared_ptr<I2NPMessage> NewI2NPShortMessage ()
{
	return std::make_shared<I2NPMessageBuffer<I2NP_MAX_SHORT_MESSAGE_SIZE>> ();
}

std::shared_ptr<I2NPMessage> NewI2NPMediumMessage ()
{
	return std::make_shared<I2NPMessageBuffer<I2NP_MAX_MEDIUM_MESSAGE_SIZE>> ();
}

std::shared_ptr<I2NPMessage> NewI2NPTunnelMessage (bool endpoint)
{
	return i2p::tunnel::tunnels.NewI2NPTunnelMessage (endpoint);
}

std::shared_ptr<I2NPMessage> NewI2NPMessage (size_t len)
{
	len += I2NP_HEADER_SIZE + 2;
	if (len <= I2NP_MAX_SHORT_MESSAGE_SIZE) return NewI2NPShortMessage ();
	if (len <= I2NP_MAX_MEDIUM_MESSAGE_SIZE) return NewI2NPMediumMessage ();
	return NewI2NPMessage ();
}

void I2NPMessage::FillI2NPMessageHeader (I2NPMessageType msgType, uint32_t msgID, bool checksum)
{
	SetTypeID (static_cast<uint8_t> (msgType));
	if (!msgID) RAND_bytes ((uint8_t *)&msgID, 4);
	SetMsgID (msgID);
	SetExpiration (i2p::util::GetMillisecondsSinceEpoch () + I2NP_MESSAGE_EXPIRATION_TIMEOUT);
	UpdateSize ();
	if (checksum) UpdateChks ();
}

bool I2NPMessage::IsExpired (uint64_t ts) const
{
	auto exp = GetExpiration ();
	return (ts > exp + I2NP_MESSAGE_CLOCK_SKEW)
		|| (ts < exp - 3 * I2NP_MESSAGE_CLOCK_SKEW); // check if expired or too far in future
}

bool I2NPMessage::IsExpired () const
{
	return IsExpired (i2p::util::GetMillisecondsSinceEpoch ());
}

std::shared_ptr<I2NPMessage> CreateI2NPMessage (
	I2NPMessageType msgType, const uint8_t * buf, size_t len, uint32_t replyMsgID)
{
	// Refuse an oversized payload before allocating: the previous version
	// kept the truncated bytes and then wrote a header and size field
	// describing a message that did not exist.
	if (!buf && len) return nullptr;
	if (len > I2NP_MAX_MESSAGE_SIZE - I2NP_HEADER_SIZE - 2)
	{
		++GetI2NPDispatchCounters ().oversizedMessage;
		LogPrint (eLogError, "I2NP: Message length ", len, " exceeds the maximum I2NP length");
		return nullptr;
	}
	auto msg = NewI2NPMessage (len);
	if (!msg) return nullptr;
	if (msg->Concat (buf, len) < len)
	{
		LogPrint (eLogError, "I2NP: Message length ", len, " exceeds max length ", msg->maxLen);
		return nullptr;
	}
	msg->FillI2NPMessageHeader (msgType, replyMsgID);
	return msg;
}

std::shared_ptr<I2NPMessage> CreateI2NPMessage (
	const uint8_t * buf, size_t len, std::shared_ptr<i2p::tunnel::InboundTunnel> from)
{
	if (!buf && len) return nullptr;
	auto msg = NewI2NPMessage ();
	// `<=`: a payload that exactly fills the buffer is valid. Returning
	// the half-written message on overflow used to hand callers a header
	// with a zero type and a stale length.
	if (len <= msg->maxLen - msg->offset)
	{
		memcpy (msg->GetBuffer (), buf, len);
		msg->len = msg->offset + len;
		msg->from = from;
		return msg;
	}
	++GetI2NPDispatchCounters ().oversizedMessage;
	LogPrint (eLogError, "I2NP: Message length ", len, " exceeds max length ", msg->maxLen);
	return nullptr;
}

std::shared_ptr<I2NPMessage> CopyI2NPMessage (std::shared_ptr<I2NPMessage> msg)
{
	if (!msg) return nullptr;
	auto newMsg = NewI2NPMessage (msg->GetLength ());
	if (!newMsg) return nullptr;
	if (msg->GetLength () > newMsg->maxLen - msg->offset) return nullptr;
	newMsg->offset = msg->offset;
	*newMsg = *msg;
	return newMsg;
}

std::shared_ptr<I2NPMessage> CreateTunnelTestMsg (uint32_t msgID)
{
	auto m = NewI2NPShortMessage ();
	uint8_t * buf = m->GetPayload ();
	htobe32buf (buf + TUNNEL_TEST_MSGID_OFFSET, msgID);
	htobe64buf (buf + TUNNEL_TEST_TIMESTAMP_OFFSET, i2p::util::GetMonotonicMicroseconds ());
	m->len += TUNNEL_TEST_SIZE;
	m->FillI2NPMessageHeader (eI2NPTunnelTest);
	return m;
}

std::shared_ptr<I2NPMessage> CreateDeliveryStatusMsg (uint32_t msgID)
{
	auto m = NewI2NPShortMessage ();
	uint8_t * buf = m->GetPayload ();
	if (msgID)
	{
		htobe32buf (buf + DELIVERY_STATUS_MSGID_OFFSET, msgID);
		htobe64buf (buf + DELIVERY_STATUS_TIMESTAMP_OFFSET, i2p::util::GetMillisecondsSinceEpoch ());
	}
	else // for SSU establishment
	{
		RAND_bytes ((uint8_t *)&msgID, 4);
		htobe32buf (buf + DELIVERY_STATUS_MSGID_OFFSET, msgID);
		htobe64buf (buf + DELIVERY_STATUS_TIMESTAMP_OFFSET, i2p::context.GetNetID ());
	}
	m->len += DELIVERY_STATUS_SIZE;
	m->FillI2NPMessageHeader (eI2NPDeliveryStatus);
	return m;
}

std::shared_ptr<I2NPMessage> CreateRouterInfoDatabaseLookupMsg (const uint8_t * key, const uint8_t * from,
	uint32_t replyTunnelID, bool exploratory, std::unordered_set<i2p::data::IdentHash> * excludedPeers)
{
	// The number of excluded peers is a 16-bit field and the message
	// itself is bounded. Cap the set before sizing the buffer so a large
	// exclusion set cannot make the serialized block overflow.
	size_t headerLen = 32 + 32 + (replyTunnelID ? 5 : 1) + 2;
	size_t maxPeers = (I2NP_MAX_MESSAGE_SIZE - 2 - I2NP_HEADER_SIZE - headerLen) / 32;
	maxPeers = std::min<size_t> (maxPeers, DATABASE_LOOKUP_MAX_NUM_PEERS);
	size_t cnt = excludedPeers ? std::min<size_t> (excludedPeers->size (), maxPeers) : 0;
	auto m = NewI2NPMessage (headerLen + cnt * 32);
	uint8_t * buf = m->GetPayload ();
	memcpy (buf, key, 32); // key
	buf += 32;
	memcpy (buf, from, 32); // from
	buf += 32;
	uint8_t flag = exploratory ? DATABASE_LOOKUP_TYPE_EXPLORATORY_LOOKUP : DATABASE_LOOKUP_TYPE_ROUTERINFO_LOOKUP;
	if (replyTunnelID)
	{
		*buf = flag | DATABASE_LOOKUP_DELIVERY_FLAG; // set delivery flag
		htobe32buf (buf + 1, replyTunnelID);
		buf += 5;
	}
	else
	{
		*buf = flag; // flag
		buf++;
	}

	if (excludedPeers)
	{
		htobe16buf (buf, (uint16_t)cnt);
		buf += 2;
		size_t i = 0;
		for (auto & it : *excludedPeers)
		{
			if (i++ >= cnt) break;
			memcpy (buf, it, 32);
			buf += 32;
		}
	}
	else
	{
		// nothing to exclude
		htobuf16 (buf, 0);
		buf += 2;
	}

	m->len += (buf - m->GetPayload ());
	m->FillI2NPMessageHeader (eI2NPDatabaseLookup);
	return m;
}

std::shared_ptr<I2NPMessage> CreateLeaseSetDatabaseLookupMsg (const i2p::data::IdentHash & dest,
	const std::unordered_set<i2p::data::IdentHash> & excludedFloodfills,
	std::shared_ptr<const i2p::tunnel::InboundTunnel> replyTunnel, const uint8_t * replyKey, uint64_t replyTag)
{
	size_t cnt = excludedFloodfills.size ();
	if (cnt > DATABASE_LOOKUP_MAX_NUM_PEERS)
	{
		LogPrint (eLogWarning, "I2NP: Too many peers to exclude ", cnt, " for DatabaseLookup");
		cnt = 0;
	}
	// Fixed part: key (32), reply gateway (32), flags (1), tunnel ID (4),
	// excluded count (2), reply key (32) + tags (1 + 8).
	auto m = NewI2NPMessage (112 + cnt * 32);
	uint8_t * buf = m->GetPayload ();
	memcpy (buf, dest, 32); // key
	buf += 32;
	memcpy (buf, replyTunnel->GetNextIdentHash (), 32); // reply tunnel GW
	buf += 32;
	// Ratchet-only reply: reply key followed by one 8-byte ECIES tag.
	*buf = DATABASE_LOOKUP_DELIVERY_FLAG | DATABASE_LOOKUP_TYPE_LEASESET_LOOKUP | DATABASE_LOOKUP_RATCHET_FLAG; // flags
	buf++;
	htobe32buf (buf, replyTunnel->GetNextTunnelID ()); // reply tunnel ID
	buf += 4;

	// excluded
	htobe16buf (buf, (uint16_t)cnt);
	buf += 2;
	if (cnt > 0)
	{
		size_t i = 0;
		for (auto & it : excludedFloodfills)
		{
			if (i++ >= cnt) break;
			memcpy (buf, it, 32);
			buf += 32;
		}
	}
	// encryption
	memcpy (buf, replyKey, 32);
	buf[32] = 1; // 1 tag
	memcpy (buf + 33, &replyTag, 8); // 8 bytes tag
	buf += 41;

	m->len += (buf - m->GetPayload ());
	m->FillI2NPMessageHeader (eI2NPDatabaseLookup);
	return m;
}

bool ParseDatabaseSearchReply (const uint8_t * buf, size_t len, DatabaseSearchReply & reply)
{
	if (!buf || len < DATABASE_SEARCH_REPLY_MIN_SIZE) return false;
	const size_t num = buf[DATABASE_SEARCH_REPLY_COUNT_OFFSET];
	const size_t peersEnd = DATABASE_SEARCH_REPLY_PEERS_OFFSET + num * 32;
	// num*32 cannot overflow: num is at most 255. Reject a declared count
	// whose peer list or mandatory trailing "from" hash is truncated.
	if (peersEnd > len || len - peersEnd < 32) return false;
	reply.key = i2p::data::IdentHash (buf + DATABASE_SEARCH_REPLY_KEY_OFFSET);
	reply.peers.clear ();
	reply.peers.reserve (num);
	for (size_t i = 0; i < num; i++) reply.peers.emplace_back (buf + DATABASE_SEARCH_REPLY_PEERS_OFFSET + i * 32);
	reply.from = i2p::data::IdentHash (buf + peersEnd);
	return true;
}

std::shared_ptr<I2NPMessage> CreateDatabaseSearchReply (
	const i2p::data::IdentHash & ident, std::vector<i2p::data::IdentHash> routers)
{
	// "num" is one byte. Also keep the whole serialized message inside the
	// selected I2NP buffer instead of truncating the count field.
	size_t cnt = std::min<size_t> (routers.size (), DATABASE_SEARCH_REPLY_MAX_NUM_PEERS);
	auto m = NewI2NPMessage (DATABASE_SEARCH_REPLY_PEERS_OFFSET + cnt * 32 + 32);
	uint8_t * buf = m->GetPayload ();
	size_t len = 0;
	memcpy (buf, ident, 32);
	len += 32;
	buf[len] = (uint8_t)cnt;
	len++;
	for (size_t i = 0; i < cnt; i++)
	{
		memcpy (buf + len, routers[i], 32);
		len += 32;
	}
	memcpy (buf + len, i2p::context.GetRouterInfo ().GetIdentHash (), 32);
	len += 32;
	m->len += len;
	m->FillI2NPMessageHeader (eI2NPDatabaseSearchReply);
	return m;
}

std::shared_ptr<I2NPMessage> CreateDatabaseStoreMsg (std::shared_ptr<const i2p::data::RouterInfo> router,
	uint32_t replyToken, std::shared_ptr<const i2p::tunnel::InboundTunnel> replyTunnel)
{
	if (!router) // we send own RouterInfo
		router = context.GetSharedRouterInfo ();

	// Keep one owning snapshot for the whole serialization. GetBuffer()
	// only returns a raw pointer after releasing m_BufferMutex, so a
	// concurrent DeleteBuffer()/Update() could otherwise free or rewrite
	// it while Gzip reads it.
	auto routerBuffer = router->GetSharedBuffer ();
	if (!routerBuffer)
	{
		LogPrint (eLogError, "I2NP: Invalid RouterInfo buffer for DatabaseStore");
		return nullptr;
	}
	const size_t routerBufferLen = routerBuffer->GetBufferLen ();
	const uint8_t * routerBufferData = routerBuffer->data ();

	// Select a buffer that can hold the uncompressed RouterInfo too. A
	// RouterInfo may be up to MAX_RI_BUFFER_SIZE (4096) bytes; a fixed
	// short-message buffer would otherwise reject valid large RouterInfos
	// even if compression is not applied.
	size_t required = DATABASE_STORE_HEADER_SIZE + (replyToken ? 36 : 0) + 2 + routerBufferLen;
	auto m = NewI2NPMessage (required);
	if (!m) return nullptr;
	uint8_t * payload = m->GetPayload ();

	memcpy (payload + DATABASE_STORE_KEY_OFFSET, router->GetIdentHash (), 32);
	payload[DATABASE_STORE_TYPE_OFFSET] = 0; // RouterInfo
	htobe32buf (payload + DATABASE_STORE_REPLY_TOKEN_OFFSET, replyToken);
	uint8_t * buf = payload + DATABASE_STORE_HEADER_SIZE;
	if (replyToken)
	{
		if (replyTunnel)
		{
			htobe32buf (buf, replyTunnel->GetNextTunnelID ());
			buf += 4; // reply tunnelID
			memcpy (buf, replyTunnel->GetNextIdentHash (), 32);
			buf += 32; // reply tunnel gateway
		}
		else
		{
			memset (buf, 0, 4); // zero tunnelID means direct reply
			buf += 4;
			memcpy (buf, context.GetIdentHash (), 32);
			buf += 32;
		}
	}

	uint8_t * sizePtr = buf;
	buf += 2;
	m->len += (buf - payload); // payload size
	size_t size = 0;
	if (routerBufferLen + (buf - payload) <= 940) // fits one tunnel message
		size = i2p::data::GzipNoCompression (
			routerBufferData, static_cast<uint16_t> (routerBufferLen), buf, m->maxLen - m->len);
	else
	{
		i2p::data::GzipDeflator deflator;
		size = deflator.Deflate (routerBufferData, routerBufferLen, buf, m->maxLen - m->len);
	}
	if (size)
	{
		htobe16buf (sizePtr, static_cast<uint16_t> (size)); // size
		m->len += size;
	}
	else
		m = nullptr;
	if (m) m->FillI2NPMessageHeader (eI2NPDatabaseStore);
	return m;
}

std::shared_ptr<I2NPMessage> CreateDatabaseStoreMsg (
	const i2p::data::IdentHash & storeHash, std::shared_ptr<const i2p::data::LeaseSet> leaseSet)
{
	if (!leaseSet) return nullptr;
	size_t required = DATABASE_STORE_HEADER_SIZE + leaseSet->GetBufferLen ();
	auto m = NewI2NPMessage (required);
	if (required > m->maxLen)
	{
		LogPrint (eLogError, "I2NP: LeaseSet is too long for DatabaseStore ", leaseSet->GetBufferLen ());
		return nullptr;
	}
	uint8_t * payload = m->GetPayload ();
	memcpy (payload + DATABASE_STORE_KEY_OFFSET, storeHash, 32);
	payload[DATABASE_STORE_TYPE_OFFSET] = leaseSet->GetStoreType (); // 1 for LeaseSet
	htobe32buf (payload + DATABASE_STORE_REPLY_TOKEN_OFFSET, 0);
	size_t size = DATABASE_STORE_HEADER_SIZE;
	memcpy (payload + size, leaseSet->GetBuffer (), leaseSet->GetBufferLen ());
	size += leaseSet->GetBufferLen ();
	m->len += size;
	m->FillI2NPMessageHeader (eI2NPDatabaseStore);
	return m;
}

std::shared_ptr<I2NPMessage> CreateDatabaseStoreMsg (std::shared_ptr<const i2p::data::LocalLeaseSet> leaseSet,
	uint32_t replyToken, std::shared_ptr<const i2p::tunnel::InboundTunnel> replyTunnel)
{
	if (!leaseSet) return nullptr;
	// A non-zero reply token always requires the 4-byte reply tunnel ID
	// and 32-byte reply gateway. Java's
	// DatabaseStoreMessage.writeMessageBody() writes a zero tunnel ID plus
	// the writer's hash when no tunnel is set; omitting the block entirely
	// would make the receiver treat the first LeaseSet bytes as reply
	// information.
	size_t required = DATABASE_STORE_HEADER_SIZE + (replyToken ? 36 : 0) + leaseSet->GetBufferLen ();
	auto m = NewI2NPMessage (required);
	if (required > m->maxLen)
	{
		LogPrint (eLogError, "I2NP: LeaseSet is too long for DatabaseStore ", leaseSet->GetBufferLen ());
		return nullptr;
	}
	uint8_t * payload = m->GetPayload ();
	memcpy (payload + DATABASE_STORE_KEY_OFFSET, leaseSet->GetStoreHash (), 32);
	payload[DATABASE_STORE_TYPE_OFFSET] = leaseSet->GetStoreType (); // LeaseSet or LeaseSet2
	htobe32buf (payload + DATABASE_STORE_REPLY_TOKEN_OFFSET, replyToken);
	size_t size = DATABASE_STORE_HEADER_SIZE;
	if (replyToken)
	{
		if (replyTunnel)
		{
			htobe32buf (payload + size, replyTunnel->GetNextTunnelID ());
			size += 4; // reply tunnelID
			memcpy (payload + size, replyTunnel->GetNextIdentHash (), 32);
			size += 32; // reply tunnel gateway
		}
		else
		{
			htobe32buf (payload + size, 0); // zero tunnelID means direct reply
			size += 4;
			memcpy (payload + size, i2p::context.GetIdentHash (), 32);
			size += 32;
		}
	}
	memcpy (payload + size, leaseSet->GetBuffer (), leaseSet->GetBufferLen ());
	size += leaseSet->GetBufferLen ();
	m->len += size;
	m->FillI2NPMessageHeader (eI2NPDatabaseStore);
	return m;
}

bool IsRouterInfoMsg (std::shared_ptr<I2NPMessage> msg)
{
	if (!msg || msg->GetTypeID () != eI2NPDatabaseStore) return false;
	if (msg->GetPayloadLength () < DATABASE_STORE_HEADER_SIZE) return false;
	return !(msg->GetPayload ()[DATABASE_STORE_TYPE_OFFSET] & DATABASE_STORE_TYPE_MASK); // 0- RouterInfo
}

std::shared_ptr<I2NPMessage> CreateTunnelDataMsg (const uint8_t * buf)
{
	auto msg = NewI2NPTunnelMessage (false);
	msg->Concat (buf, i2p::tunnel::TUNNEL_DATA_MSG_SIZE);
	msg->FillI2NPMessageHeader (eI2NPTunnelData);
	return msg;
}

std::shared_ptr<I2NPMessage> CreateTunnelDataMsg (uint32_t tunnelID, const uint8_t * payload)
{
	auto msg = NewI2NPTunnelMessage (false);
	htobe32buf (msg->GetPayload (), tunnelID);
	msg->len += 4; // tunnelID
	msg->Concat (payload, i2p::tunnel::TUNNEL_DATA_MSG_SIZE - 4);
	msg->FillI2NPMessageHeader (eI2NPTunnelData);
	return msg;
}

std::shared_ptr<I2NPMessage> CreateEmptyTunnelDataMsg (bool endpoint)
{
	auto msg = NewI2NPTunnelMessage (endpoint);
	msg->len += i2p::tunnel::TUNNEL_DATA_MSG_SIZE;
	return msg;
}

std::shared_ptr<I2NPMessage> CreateTunnelGatewayMsg (uint32_t tunnelID, const uint8_t * buf, size_t len)
{
	// Reserve the six-byte gateway header in addition to the I2NP header.
	// The previous +0 reservation truncated payloads exactly at
	// buffer-tier edges while still advertising the full length.
	auto msg = NewI2NPMessage (len + TUNNEL_GATEWAY_HEADER_SIZE);
	if (len > std::numeric_limits<uint16_t>::max ()) return nullptr;
	uint8_t * payload = msg->GetPayload ();
	htobe32buf (payload + TUNNEL_GATEWAY_HEADER_TUNNELID_OFFSET, tunnelID);
	htobe16buf (payload + TUNNEL_GATEWAY_HEADER_LENGTH_OFFSET, static_cast<uint16_t> (len));
	msg->len += TUNNEL_GATEWAY_HEADER_SIZE;
	if (msg->Concat (buf, len) < len)
	{
		LogPrint (eLogError, "I2NP: Tunnel gateway buffer overflow ", msg->maxLen);
		return nullptr;
	}
	msg->FillI2NPMessageHeader (eI2NPTunnelGateway);
	return msg;
}

std::shared_ptr<I2NPMessage> CreateTunnelGatewayMsg (uint32_t tunnelID, std::shared_ptr<I2NPMessage> msg)
{
	if (msg->offset >= I2NP_HEADER_SIZE + TUNNEL_GATEWAY_HEADER_SIZE)
	{
		// message is capable to be used without copying
		uint8_t * payload = msg->GetBuffer () - TUNNEL_GATEWAY_HEADER_SIZE;
		htobe32buf (payload + TUNNEL_GATEWAY_HEADER_TUNNELID_OFFSET, tunnelID);
		const size_t len = msg->GetLength ();
		htobe16buf (payload + TUNNEL_GATEWAY_HEADER_LENGTH_OFFSET, static_cast<uint16_t> (len));
		msg->offset -= (I2NP_HEADER_SIZE + TUNNEL_GATEWAY_HEADER_SIZE);
		msg->len = msg->offset + I2NP_HEADER_SIZE + TUNNEL_GATEWAY_HEADER_SIZE + len;
		msg->FillI2NPMessageHeader (eI2NPTunnelGateway);
		return msg;
	}
	else
	{
		auto newMsg = CreateTunnelGatewayMsg (tunnelID, msg->GetBuffer (), msg->GetLength ());
		if (!newMsg) return nullptr;
		if (msg->onDrop) newMsg->onDrop = msg->onDrop;
		return newMsg;
	}
}

std::shared_ptr<I2NPMessage> CreateTunnelGatewayMsg (
	uint32_t tunnelID, I2NPMessageType msgType, const uint8_t * buf, size_t len, uint32_t replyMsgID)
{
	if (len > std::numeric_limits<uint16_t>::max ()) return nullptr;
	auto msg = NewI2NPMessage (len + TUNNEL_GATEWAY_HEADER_SIZE);
	size_t gatewayMsgOffset = I2NP_HEADER_SIZE + TUNNEL_GATEWAY_HEADER_SIZE;
	msg->offset += gatewayMsgOffset;
	msg->len += gatewayMsgOffset;
	if (msg->Concat (buf, len) < len)
	{
		LogPrint (eLogError, "I2NP: Tunnel gateway buffer overflow ", msg->maxLen);
		return nullptr;
	}
	msg->FillI2NPMessageHeader (msgType, replyMsgID); // create content message
	len = msg->GetLength ();
	msg->offset -= gatewayMsgOffset;
	uint8_t * payload = msg->GetPayload ();
	htobe32buf (payload + TUNNEL_GATEWAY_HEADER_TUNNELID_OFFSET, tunnelID);
	htobe16buf (payload + TUNNEL_GATEWAY_HEADER_LENGTH_OFFSET, static_cast<uint16_t> (len));
	msg->FillI2NPMessageHeader (eI2NPTunnelGateway); // gateway message
	return msg;
}

size_t GetI2NPMessageLength (const uint8_t * msg, size_t len)
{
	// Both failures are reachable from an unauthenticated peer, so the log
	// is throttled to the first and every 256th occurrence.
	static I2NPDropCounter lengthDrops;
	if (len < I2NP_HEADER_SIZE_OFFSET + 2)
	{
		if (lengthDrops.Record ())
			LogPrint (eLogError, "I2NP: Message length ", len, " is smaller than header (", lengthDrops.GetCount (),
				" such messages)");
		return len;
	}
	auto l = bufbe16toh (msg + I2NP_HEADER_SIZE_OFFSET) + I2NP_HEADER_SIZE;
	if (l > len)
	{
		if (lengthDrops.Record ())
			LogPrint (eLogError, "I2NP: Message length ", l, " exceeds buffer length ", len, " (",
				lengthDrops.GetCount (), " such messages)");
		l = len;
	}
	return l;
}

namespace
{
	I2NPDispatchCounters g_DispatchCounters;
	I2NPDuplicateFilter g_RouterDuplicateFilter;
	I2NPDropCounter g_ExpiredDrops;
	I2NPDropCounter g_DuplicateDrops;
	I2NPDropCounter g_CloveTypeDrops;
} // namespace

I2NPDispatchCounters & GetI2NPDispatchCounters ()
{
	return g_DispatchCounters;
}

I2NPDuplicateFilter & GetI2NPRouterDuplicateFilter ()
{
	return g_RouterDuplicateFilter;
}

void I2NPDuplicateFilter::Rotate (uint64_t nowSeconds)
{
	// The previous generation is dropped wholesale and the current one
	// becomes the previous, so an entry stays visible for at most two
	// windows.
	m_Previous.swap (m_Current);
	m_Current.clear ();
	m_Order.clear ();
	m_NextRotation = nowSeconds + m_GenerationTimeout;
}

bool I2NPDuplicateFilter::CheckAndInsert (uint32_t key, uint64_t nowSeconds)
{
	std::lock_guard<std::mutex> l (m_Mutex);
	if (!m_NextRotation || nowSeconds >= m_NextRotation) Rotate (nowSeconds);
	if (m_Current.find (key) != m_Current.end () || m_Previous.find (key) != m_Previous.end ()) return true;
	while (m_Current.size () >= m_MaxEntries && !m_Order.empty ())
	{
		m_Current.erase (m_Order.front ());
		m_Order.pop_front ();
	}
	m_Current.insert (key);
	m_Order.push_back (key);
	return false;
}

size_t I2NPDuplicateFilter::GetSize () const
{
	std::lock_guard<std::mutex> l (m_Mutex);
	return m_Current.size () + m_Previous.size ();
}

void I2NPDuplicateFilter::Clear ()
{
	std::lock_guard<std::mutex> l (m_Mutex);
	m_Current.clear ();
	m_Previous.clear ();
	m_Order.clear ();
	m_NextRotation = 0;
}

bool IsI2NPDispatchDuplicate (const I2NPMessage & msg, uint64_t nowSeconds)
{
	// A TunnelData cell has its own IV-based replay filter at the tunnel
	// endpoint, and SSU2 keeps a per-session message-ID map, so neither
	// path may be second-guessed here.
	if (msg.GetTypeID () == eI2NPTunnelData) return false;
	// A clove is deduplicated by its destination's per-context clove
	// filter (GarlicDestination), which includes the destination-specific
	// salt Java uses. Applying the router-wide key here as well would let
	// a message to one destination suppress a different destination's
	// clove with the same ID.
	if (msg.fromClove) return false;
	const uint32_t key = GetI2NPDuplicateKey (msg.GetMsgID (), msg.GetExpiration (), I2NP_DUPLICATE_SALT_ROUTER);
	return g_RouterDuplicateFilter.CheckAndInsert (key, nowSeconds);
}

void HandleI2NPMessage (std::shared_ptr<I2NPMessage> msg)
{
	if (!msg) return;
	uint8_t typeID = msg->GetTypeID ();
	// The clove allow-list depends on the origin of the enclosing garlic:
	// a tunnel-delivered clove is Java's InboundMessageDistributor path, a
	// clove of a garlic received directly from a peer is
	// HandleGarlicMessageJob.
	if (msg->fromClove && !IsAllowedCloveMessageType (typeID, msg->fromTunnel))
	{
		const uint64_t dropped = GetI2NPDispatchCounters ().disallowedCloveType.fetch_add (1) + 1;
		if (g_CloveTypeDrops.Record ())
			LogPrint (eLogWarning, "I2NP: Dropped clove-delivered message type ", (int)typeID, " (", dropped,
				" such cloves)");
		return;
	}
	// Java MessageValidator: drop a message whose expiration date is older
	// than 90 s or further than 240 s in the future before any handler
	// sees it. Replaying a very old netDb, garlic or DeliveryStatus
	// message must not reach netDb/garlic/status state.
	const uint64_t now = i2p::util::GetMillisecondsSinceEpoch ();
	if (IsI2NPDispatchExpired (*msg, now))
	{
		const uint64_t dropped = GetI2NPDispatchCounters ().expired.fetch_add (1) + 1;
		if (g_ExpiredDrops.Record ())
			LogPrint (eLogWarning, "I2NP: Dropped message type ", (int)typeID, " with expiration ",
				msg->GetExpiration (), " (", dropped, " expired messages)");
		return;
	}
	if (IsI2NPDispatchDuplicate (*msg, now / 1000))
	{
		const uint64_t dropped = GetI2NPDispatchCounters ().duplicate.fetch_add (1) + 1;
		if (g_DuplicateDrops.Record ())
			LogPrint (eLogWarning, "I2NP: Dropped duplicate message type ", (int)typeID, " msgID ", msg->GetMsgID (),
				" (", dropped, " duplicate messages)");
		return;
	}
	LogPrint (eLogDebug, "I2NP: Handling message with type ", (int)typeID);
	switch (typeID)
	{
	case eI2NPTunnelData:
		if (!msg->from) i2p::tunnel::tunnels.PostTunnelData (msg);
		break;
	case eI2NPTunnelGateway:
		if (!msg->from) i2p::tunnel::tunnels.PostTunnelData (msg);
		break;
	case eI2NPGarlic:
	{
		if (msg->from && msg->from->GetTunnelPool ())
			msg->from->GetTunnelPool ()->ProcessGarlicMessage (msg);
		else
			i2p::context.ProcessGarlicMessage (msg);
		break;
	}
	case eI2NPDatabaseStore:
		// forward to netDb if came directly or through exploratory tunnel as
		// response to our request
		if (!msg->from || !msg->from->GetTunnelPool () || msg->from->GetTunnelPool ()->IsExploratory ())
			i2p::data::netdb.PostI2NPMsg (msg);
		break;
	case eI2NPDatabaseSearchReply:
		if (!msg->from || !msg->from->GetTunnelPool () || msg->from->GetTunnelPool ()->IsExploratory ())
			i2p::data::netdb.PostDatabaseSearchReplyMsg (msg);
		break;
	case eI2NPDatabaseLookup:
		// Java accepts a DatabaseLookup from a directly received garlic
		// (HandleGarlicMessageJob) but refuses one dispatched by
		// InboundMessageDistributor, i.e. a garlic or plain message that
		// crossed a tunnel. `fromTunnel` records exactly that for a clove;
		// `from` covers a plain tunnel-delivered message.
		if (IsDatabaseLookupDispatchAllowed (*msg) && i2p::context.IsFloodfill ()) i2p::data::netdb.PostI2NPMsg (msg);
		break;
	case eI2NPDeliveryStatus:
	{
		if (msg->from && msg->from->GetTunnelPool ())
			msg->from->GetTunnelPool ()->ProcessDeliveryStatus (msg);
		else
			i2p::context.ProcessDeliveryStatusMessage (msg);
		break;
	}
	case eI2NPTunnelTest:
		if (msg->from && msg->from->GetTunnelPool ()) msg->from->GetTunnelPool ()->ProcessTunnelTest (msg);
		break;
	case eI2NPVariableTunnelBuild:
	case eI2NPTunnelBuild:
	case eI2NPShortTunnelBuild:
		// forward to tunnel thread
		if (!msg->from) i2p::tunnel::tunnels.PostTunnelData (msg);
		break;
	case eI2NPVariableTunnelBuildReply:
	case eI2NPTunnelBuildReply:
	case eI2NPShortTunnelBuildReply:
		// forward to tunnel thread
		i2p::tunnel::tunnels.PostTunnelData (msg);
		break;
	default:
		LogPrint (eLogError, "I2NP: Unexpected I2NP message with type ", int (typeID), " during handling; skipping");
	}
}

I2NPMessagesHandler::~I2NPMessagesHandler ()
{
	Flush ();
}

void I2NPMessagesHandler::PutNextMessage (std::shared_ptr<I2NPMessage> && msg)
{
	if (msg)
	{
		switch (msg->GetTypeID ())
		{
		case eI2NPTunnelData: m_TunnelMsgs.push_back (msg); break;
		case eI2NPTunnelGateway: m_TunnelGatewayMsgs.push_back (msg); break;
		case eI2NPVariableTunnelBuild:
		case eI2NPTunnelBuild:
		case eI2NPShortTunnelBuild:
		{
			auto ts = i2p::util::GetMonotonicMilliseconds ();
			if (!m_LastTunnelBuildMessageTimestamp
				|| ts > m_LastTunnelBuildMessageTimestamp + TUNNEL_BUILD_MESSAGES_MIN_INTERVAL
				|| m_NumDroppedTunnelBuildMessages > MAX_NUM_DROPPED_TUNNEL_BUILD_MESSAGES)
			{
				m_NumThrottledTunnelBuildMessages = 0;
				if (m_NumDroppedTunnelBuildMessages > 0)
				{
					LogPrint (eLogWarning, "I2NP: ", m_NumDroppedTunnelBuildMessages, " tunnel build messages dropped");
					m_NumDroppedTunnelBuildMessages = 0;
				}
				HandleI2NPMessage (msg);
			}
			else
			{
				if (m_NumThrottledTunnelBuildMessages < MAX_NUM_THROTTLED_TUNNEL_BUILD_MESSAGES)
				{
					// let TBM go through
					m_NumThrottledTunnelBuildMessages++;
					HandleI2NPMessage (msg);
				}
				else // drop TBM
					m_NumDroppedTunnelBuildMessages++;
			}
			m_LastTunnelBuildMessageTimestamp = ts;
			break;
		}
		default: HandleI2NPMessage (msg);
		}
	}
}

void I2NPMessagesHandler::Flush ()
{
	if (!m_TunnelMsgs.empty ()) i2p::tunnel::tunnels.PostTunnelData (m_TunnelMsgs);
	if (!m_TunnelGatewayMsgs.empty ()) i2p::tunnel::tunnels.PostTunnelData (m_TunnelGatewayMsgs);
}
}
