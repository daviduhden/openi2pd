/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Garlic.
 *
 * Implements garlic routing sessions and clove encoding for
 * ElGamal/AES+ SessionTag and ECIES-X25519 destinations, including
 * session-tag creation, expiry and persistence, delivery
 * instructions/status and routing-path reuse. Incoming blocks are
 * bounded to 200 ElGamal tags and fixed per-destination tag pools, and
 * unknown or malformed tags are discarded without growing state. A
 * clove payload is limited to I2NP_MAX_CLOVES well-framed cloves, a
 * stale or foreign clove is skipped without stopping the walk, a
 * replayed clove is dropped by a bounded per-destination filter, and
 * every session serializes its own state so concurrent wrapping cannot
 * share tags or IVs.
 *
 */

#include <inttypes.h>
#include <cstring>
#include "I2PEndian.h"
#include <algorithm>
#include <limits>
#include <map>
#include <string>
#include <openssl/crypto.h>
#include "Crypto.h"
#include "RouterContext.h"
#include "I2NPProtocol.h"
#include "Tunnel.h"
#include "TunnelPool.h"
#include "Transports.h"
#include "Timestamp.h"
#include "Log.h"
#include "FS.h"
#include "ECIESX25519AEADRatchetSession.h"
#include "Garlic.h"

namespace i2p
{
namespace garlic
{
	// Java's ElGamalAESEngine.MAX_TAGS_RECEIVED and the ElGamal/AES
	// specification allow at most 200 tags in one AES block. A larger
	// count must be rejected before the receiver grows its tag table.
	constexpr uint16_t MAX_ELGAMAL_AES_TAGS = 200;
	// Per-destination bounds. A peer can otherwise add up to 200 ElGamal
	// tags per block (retained for INCOMING_TAGS_EXPIRATION_TIMEOUT) or
	// request up to 800 ECIES tags in one NEXT_KEY block.
	constexpr size_t MAX_INCOMING_ELGAMAL_TAGS = 4096;
	constexpr size_t MAX_INCOMING_ECIES_TAGS = 8192;

	namespace
	{
		// Throttled diagnostics for the garlic parse paths. A peer controls
		// the ciphertext, clove headers and tag sets, so a malformed block or
		// a stale clove must not be able to flood the log.
		I2NPDropCounter g_GarlicParseDrops;
		I2NPDropCounter g_ElGamalTagBudgetDrops;
		I2NPDropCounter g_CloveStaleDrops;
		I2NPDropCounter g_CloveReplayDrops;
		I2NPDropCounter g_CloveDestinationDrops;
	} // namespace

	// Java MessageValidator: reject a clove whose expiration is older than
	// 90 seconds or more than 240 seconds in the future. The same window
	// is applied by the I2NP dispatcher (IsFreshI2NPExpiration).
	bool IsFreshCloveDate (uint64_t dateMs)
	{
		return IsFreshI2NPExpiration (dateMs, i2p::util::GetMillisecondsSinceEpoch ());
	}

	i2p::crypto::HKDFContext & GetThreadLocalHKDFContext ()
	{
		// A provider derivation context is not reentrant. One instance per
		// thread keeps concurrent tag generation from interleaving inside a
		// single derivation without holding a lock across crypto work.
		thread_local i2p::crypto::HKDFContext ctx;
		return ctx;
	}

	GarlicRoutingSession::GarlicRoutingSession (GarlicDestination * owner, bool attachLeaseSet)
		: m_Owner (owner)
		, m_LeaseSetUpdateStatus (attachLeaseSet ? eLeaseSetUpdated : eLeaseSetDoNotSend)
		, m_LeaseSetUpdateMsgID (0)
		, m_IsWithJava (false)
		, m_NumSentPackets (0)
		, m_LastSendTime (0)
	{
	}

	GarlicRoutingSession::GarlicRoutingSession ()
		: m_Owner (nullptr)
		, m_LeaseSetUpdateStatus (eLeaseSetDoNotSend)
		, m_LeaseSetUpdateMsgID (0)
	{
	}

	GarlicRoutingSession::~GarlicRoutingSession () { }

	std::shared_ptr<GarlicRoutingPath> GarlicRoutingSession::GetSharedRoutingPath ()
	{
		std::lock_guard<std::recursive_mutex> l (m_StateMutex);
		if (!m_SharedRoutingPath) return nullptr;
		uint32_t ts = static_cast<uint32_t> (i2p::util::GetSecondsSinceEpoch ());
		if (!m_SharedRoutingPath->outboundTunnel || !m_SharedRoutingPath->remoteLease
			|| !m_SharedRoutingPath->outboundTunnel->IsEstablished ()
			|| static_cast<uint64_t> (ts) * 1000LL > m_SharedRoutingPath->remoteLease->endDate
			|| ts > m_SharedRoutingPath->updateTime + ROUTING_PATH_EXPIRATION_TIMEOUT)
			m_SharedRoutingPath = nullptr;
		return m_SharedRoutingPath;
	}

	void GarlicRoutingSession::SetSharedRoutingPath (std::shared_ptr<GarlicRoutingPath> path)
	{
		if (path && path->outboundTunnel && path->remoteLease)
			path->updateTime = static_cast<uint32_t> (i2p::util::GetSecondsSinceEpoch ());
		else
			path = nullptr;
		std::lock_guard<std::recursive_mutex> l (m_StateMutex);
		m_SharedRoutingPath = path;
	}

	bool GarlicRoutingSession::MessageConfirmed (uint32_t msgID)
	{
		std::lock_guard<std::recursive_mutex> l (m_StateMutex);
		if (msgID == m_LeaseSetUpdateMsgID)
		{
			m_LeaseSetUpdateStatus = eLeaseSetUpToDate;
			m_LeaseSetUpdateMsgID = 0;
			LogPrint (eLogInfo, "Garlic: LeaseSet update confirmed");
			return true;
		}
		return false;
	}

	void GarlicRoutingSession::CleanupUnconfirmedLeaseSet (uint64_t ts)
	{
		uint32_t updateMsgID = 0;
		{
			std::lock_guard<std::recursive_mutex> l (m_StateMutex);
			if (m_LeaseSetUpdateMsgID && ts * 1000LL > m_LeaseSetSubmissionTime + LEASESET_CONFIRMATION_TIMEOUT)
			{
				updateMsgID = m_LeaseSetUpdateMsgID;
				m_LeaseSetUpdateMsgID = 0;
			}
		}
		// Drop the delivery-status ownership outside the session lock.
		if (updateMsgID && GetOwner ()) GetOwner ()->RemoveDeliveryStatusSession (updateMsgID);
	}

	std::shared_ptr<I2NPMessage> GarlicRoutingSession::CreateEncryptedDeliveryStatusMsg (uint32_t msgID)
	{
		auto msg = CreateDeliveryStatusMsg (msgID);
		if (GetOwner ())
		{
			//encrypt
			uint8_t key[32], tag[32];
			if (RAND_bytes (key, sizeof (key)) != 1 || RAND_bytes (tag, sizeof (tag)) != 1)
			{
				OPENSSL_cleanse (key, sizeof (key));
				OPENSSL_cleanse (tag, sizeof (tag));
				LogPrint (eLogError, "Garlic: Can't generate DeliveryStatus session key");
				return nullptr;
			}
			GetOwner ()->SubmitSessionKey (key, tag);
			ElGamalAESSession garlic (key, tag);
			OPENSSL_cleanse (key, sizeof (key));
			OPENSSL_cleanse (tag, sizeof (tag));
			msg = garlic.WrapSingleMessage (msg);
		}
		return msg;
	}

	std::vector<std::shared_ptr<I2NPMessage>> GarlicRoutingSession::WrapMultipleMessages (
		const std::vector<std::shared_ptr<const I2NPMessage>> & msgs)
	{
		std::vector<std::shared_ptr<I2NPMessage>> ret;
		for (auto & msg : msgs) ret.push_back (WrapSingleMessage (msg));
		return ret;
	}

	ElGamalAESSession::ElGamalAESSession (GarlicDestination * owner,
		std::shared_ptr<const i2p::data::RoutingDestination> destination, int numTags, bool attachLeaseSet)
		: GarlicRoutingSession (owner, attachLeaseSet)
		, m_Destination (destination)
		, m_NumTags (std::clamp (numTags, 0, (int)MAX_ELGAMAL_AES_TAGS))
	{
		// create new session tags and session key
		if (RAND_bytes (m_SessionKey, 32) == 1)
			m_SessionKeyValid = true;
		else
		{
			OPENSSL_cleanse (m_SessionKey, sizeof (m_SessionKey));
			LogPrint (eLogError, "Garlic: Can't generate ElGamal/AES session key");
		}
		m_Encryption.SetKey (m_SessionKey);
	}

	ElGamalAESSession::ElGamalAESSession (const uint8_t * sessionKey, const SessionTag & sessionTag)
		: m_NumTags (1)
	{
		if (sessionKey)
		{
			memcpy (m_SessionKey, sessionKey, 32);
			m_SessionKeyValid = true;
		}
		else
			OPENSSL_cleanse (m_SessionKey, sizeof (m_SessionKey));
		m_Encryption.SetKey (m_SessionKey);
		m_SessionTags.push_back (sessionTag);
		m_SessionTags.back ().creationTime = static_cast<uint32_t> (i2p::util::GetSecondsSinceEpoch ());
	}

	std::shared_ptr<I2NPMessage> ElGamalAESSession::WrapSingleMessage (std::shared_ptr<const I2NPMessage> msg)
	{
		// The tag list, the CBC state, the IV and the unconfirmed-tag map are
		// shared by every caller that wraps to this destination, and the
		// delivery-status callback below re-enters the owner, so the whole
		// wrapping path runs under the recursive session lock.
		std::lock_guard<std::recursive_mutex> l (m_StateMutex);
		if (!m_SessionKeyValid)
		{
			LogPrint (eLogError, "Garlic: ElGamal/AES session key is not available");
			return nullptr;
		}
		auto m = NewI2NPMessage ();
		m->Align (12); // in order to get buf aligned to 16 (12 + 4)
		const size_t baseLen = m->len;
		const size_t capacity = baseLen <= m->maxLen ? m->maxLen - baseLen : 0;
		// Worst case envelope: length, ElGamal block, freshly generated tags,
		// AES block header, garlic payload and padding. Refuse oversized
		// input before consuming a tag or mutating session state.
		size_t worstTags = 0;
		if (m_NumTags > 0)
		{
			if (static_cast<size_t> (m_NumTags) > (std::numeric_limits<size_t>::max () - 636) / 32) return nullptr;
			worstTags = static_cast<size_t> (m_NumTags) * 32;
		}
		const size_t worstOverhead = 636 + worstTags;
		if (msg && (capacity < worstOverhead || msg->GetLength () > capacity - worstOverhead))
		{
			LogPrint (eLogWarning, "Garlic: message of ", msg->GetLength (), " bytes is too large to wrap");
			return nullptr;
		}
		size_t len = 0;
		uint8_t * buf = m->GetPayload () + 4; // 4 bytes for length

		// find non-expired tag
		bool tagFound = false;
		SessionTag tag;
		if (m_NumTags > 0)
		{
			uint32_t ts = static_cast<uint32_t> (i2p::util::GetSecondsSinceEpoch ());
			while (!m_SessionTags.empty ())
			{
				if (ts < m_SessionTags.front ().creationTime + OUTGOING_TAGS_EXPIRATION_TIMEOUT)
				{
					tag = m_SessionTags.front ();
					m_SessionTags.pop_front (); // use same tag only once
					tagFound = true;
					break;
				}
				else
					m_SessionTags.pop_front (); // remove expired tag
			}
		}
		// create message
		if (!tagFound) // new session
		{
			LogPrint (eLogInfo, "Garlic: No tags available, will use ElGamal");
			if (!m_Destination)
			{
				LogPrint (eLogError, "Garlic: Can't use ElGamal for unknown destination");
				return nullptr;
			}
			if (capacity < 4 + 514)
			{
				LogPrint (eLogError, "Garlic: No room for ElGamal block");
				return nullptr;
			}
			// create ElGamal block. The padding is ignored by the receiver but
			// is part of the encrypted plaintext, so it must not carry
			// uninitialised stack contents.
			ElGamalBlock elGamal;
			memcpy (elGamal.sessionKey, m_SessionKey, 32);
			if (RAND_bytes (elGamal.preIV, 32) != 1) // Pre-IV
			{
				OPENSSL_cleanse (&elGamal, sizeof (elGamal));
				LogPrint (eLogError, "Garlic: Can't generate ElGamal pre-IV");
				return nullptr;
			}
			memset (elGamal.padding, 0, sizeof (elGamal.padding));
			uint8_t iv[32]; // IV is first 16 bytes
			SHA256 (elGamal.preIV, 32, iv);
			if (!m_Destination->Encrypt ((uint8_t *)&elGamal, buf))
			{
				OPENSSL_cleanse (&elGamal, sizeof (elGamal));
				LogPrint (eLogError, "Garlic: Destination has no usable ElGamal encryptor");
				return nullptr;
			}
			m_IV = iv;
			buf += 514;
			len += 514;
		}
		else // existing session
		{
			if (capacity < 4 + 32)
			{
				LogPrint (eLogError, "Garlic: No room for session tag");
				return nullptr;
			}
			// session tag
			memcpy (buf, tag, 32);
			uint8_t iv[32]; // IV is first 16 bytes
			SHA256 (tag, 32, iv);
			m_IV = iv;
			buf += 32;
			len += 32;
		}
		// AES block
		if (len > capacity - 4)
		{
			LogPrint (eLogError, "Garlic: No room for AES block");
			return nullptr;
		}
		const size_t blockSize = CreateAESBlock (buf, capacity - 4 - len, msg);
		if (!blockSize)
		{
			LogPrint (eLogError, "Garlic: Can't create AES block");
			return nullptr;
		}
		len += blockSize;
		htobe32buf (m->GetPayload (), static_cast<uint32_t> (len));
		m->len = baseLen + 4 + len;
		m->FillI2NPMessageHeader (eI2NPGarlic);
		return m;
	}

	size_t ElGamalAESSession::CreateAESBlock (uint8_t * buf, size_t capacity, std::shared_ptr<const I2NPMessage> msg)
	{
		size_t blockSize = 0;
		bool createNewTags
			= GetOwner () && m_NumTags && (m_SessionTags.size () <= static_cast<size_t> (m_NumTags) * 2 / 3);
		const size_t headerSize = 2 + 4 + 32 + 1; // tag count, payload size, hash, flag
		size_t tagsSize = 0;
		if (createNewTags)
		{
			if (static_cast<size_t> (m_NumTags) > (std::numeric_limits<size_t>::max () - headerSize) / 32) return 0;
			tagsSize = static_cast<size_t> (m_NumTags) * 32;
		}
		// Keep room for the worst-case PKCS#7-style block padding below.
		constexpr size_t maxPadding = 15;
		if (capacity < headerSize + tagsSize + maxPadding) return 0;
		std::unique_ptr<UnconfirmedTags> newTags = createNewTags ? GenerateSessionTags () : nullptr;
		htobuf16 (buf, newTags ? htobe16 (static_cast<uint16_t> (newTags->sessionTags.size ())) : 0); // tag count
		blockSize += 2;
		if (newTags) // session tags recreated
		{
			for (const auto & tag : newTags->sessionTags)
			{
				memcpy (buf + blockSize, tag.data (), 32); // tags
				blockSize += 32;
			}
		}
		uint8_t * payloadSize = buf + blockSize; // avoid unaligned uint32_t access
		blockSize += 4;
		uint8_t * payloadHash = buf + blockSize;
		blockSize += 32;
		buf[blockSize] = 0; // flag
		blockSize++;
		// Ownership of newTags moves into CreateGarlicPayload: it either
		// stores the tags together with the DeliveryStatus message that
		// confirms them or releases them on failure.
		size_t len = CreateGarlicPayload (buf + blockSize, capacity - blockSize - maxPadding, msg, std::move (newTags));
		if (!len) return 0;
		htobe32buf (payloadSize, static_cast<uint32_t> (len));
		SHA256 (buf + blockSize, len, payloadHash);
		blockSize += len;
		const size_t rem = blockSize % 16;
		if (rem)
		{
			const size_t padding = 16 - rem;
			// The receiver ignores this padding, but it must not expose stale
			// heap contents. Java AESEngine.getPadding() uses the CSPRNG.
			if (RAND_bytes (buf + blockSize, static_cast<int> (padding)) != 1)
			{
				LogPrint (eLogError, "Garlic: Can't generate AES block padding");
				return 0;
			}
			blockSize += padding;
		}
		if (!m_Encryption.EncryptChecked (buf, blockSize, m_IV, buf))
		{
			LogPrint (eLogError, "Garlic: Can't encrypt the AES block");
			return 0;
		}
		return blockSize;
	}

	size_t ElGamalAESSession::CreateGarlicPayload (uint8_t * payload, size_t capacity,
		std::shared_ptr<const I2NPMessage> msg, std::unique_ptr<UnconfirmedTags> newTags)
	{
		const bool isDestination = m_Destination ? m_Destination->IsDestination () : false;
		const size_t mainCloveSize = msg ? 1 + (isDestination ? 32 : 0) + msg->GetLength () + 4 + 8 + 3 : 0;
		const size_t reserved = 1 + mainCloveSize + 15; // clove count, message clove, trailer
		if (capacity < reserved)
		{
			LogPrint (eLogWarning, "Garlic: payload does not fit into the message");
			return 0; // newTags released by the unique_ptr
		}
		size_t optionalCapacity = capacity - reserved;
		uint64_t ts = i2p::util::GetMillisecondsSinceEpoch ();
		uint32_t msgID;
		if (RAND_bytes ((uint8_t *)&msgID, sizeof (msgID)) != 1)
		{
			LogPrint (eLogError, "Garlic: Can't generate garlic message ID");
			return 0;
		}
		size_t size = 0;
		uint8_t * numCloves = payload + size;
		*numCloves = 0;
		size++;

		if (GetOwner ())
		{
			// resubmit non-confirmed LeaseSet
			if (GetLeaseSetUpdateStatus () == eLeaseSetSubmitted
				&& ts > GetLeaseSetSubmissionTime () + LEASESET_CONFIRMATION_TIMEOUT)
			{
				SetLeaseSetUpdateStatus (eLeaseSetUpdated);
				SetSharedRoutingPath (nullptr); // invalidate path since leaseset was not confirmed
			}

			// attach DeliveryStatus if necessary
			if (newTags || GetLeaseSetUpdateStatus () == eLeaseSetUpdated) // new tags created or leaseset updated
			{
				// clove is DeliveryStatus
				auto cloveSize = CreateDeliveryStatusClove (payload + size, optionalCapacity, msgID);
				if (cloveSize > 0) // successive?
				{
					size += cloveSize;
					optionalCapacity -= cloveSize;
					(*numCloves)++;
					if (newTags) // new tags created
					{
						newTags->msgID = msgID;
						m_UnconfirmedTagsMsgs.emplace (msgID, std::move (newTags));
					}
					GetOwner ()->DeliveryStatusSent (shared_from_this (), msgID);
				}
				else
					LogPrint (eLogWarning, "Garlic: DeliveryStatus clove was not created");
			}
			// attach LeaseSet
			if (GetLeaseSetUpdateStatus () == eLeaseSetUpdated)
			{
				auto leaseSet = GetOwner ()->GetLeaseSet ();
				auto leaseSetMsg = leaseSet ? CreateDatabaseStoreMsg (leaseSet) : nullptr;
				auto cloveSize
					= leaseSetMsg ? CreateGarlicClove (payload + size, optionalCapacity, leaseSetMsg, false) : 0;
				if (cloveSize > 0)
				{
					if (GetLeaseSetUpdateMsgID ())
						GetOwner ()->RemoveDeliveryStatusSession (GetLeaseSetUpdateMsgID ()); // remove previous
					SetLeaseSetUpdateStatus (eLeaseSetSubmitted);
					SetLeaseSetUpdateMsgID (msgID);
					SetLeaseSetSubmissionTime (ts);
					size += cloveSize;
					optionalCapacity -= cloveSize;
					(*numCloves)++;
				}
				else
					LogPrint (eLogWarning, "Garlic: LeaseSet clove was not created");
			}
		}
		if (msg) // clove message itself if presented
		{
			// reserved space guarantees this succeeds
			auto cloveSize = CreateGarlicClove (payload + size, capacity - size, msg, isDestination);
			if (cloveSize > 0)
			{
				size += cloveSize;
				(*numCloves)++;
			}
			else
			{
				LogPrint (eLogError, "Garlic: Message clove was not created");
				return 0;
			}
		}
		if (!*numCloves)
		{
			// A garlic payload that announces no clove is invalid on the
			// receiving side (IsValidCloveCount) and confirms nothing. Refuse to
			// emit it instead of producing a message every peer must drop.
			LogPrint (eLogWarning, "Garlic: no clove could be created for the payload");
			return 0;
		}
		memset (payload + size, 0, 3); // certificate of message
		size += 3;
		htobe32buf (payload + size, msgID); // MessageID
		size += 4;
		htobe64buf (payload + size, ts + 8000); // Expiration of message, 8 sec
		size += 8;

		// newTags was either moved into m_UnconfirmedTagsMsgs above or is
		// released here when no DeliveryStatus clove could be attached.
		return size;
	}

	size_t ElGamalAESSession::CreateGarlicClove (
		uint8_t * buf, size_t capacity, std::shared_ptr<const I2NPMessage> msg, bool isDestination)
	{
		if (!msg) return 0;
		const size_t needed = 1 + (isDestination ? 32 : 0) + msg->GetLength () + 4 + 8 + 3;
		if (capacity < needed) return 0;
		uint64_t ts = i2p::util::GetMillisecondsSinceEpoch () + 8000; // 8 sec
		size_t size = 0;
		if (isDestination)
		{
			buf[size] = eGarlicDeliveryTypeDestination << 5; // delivery instructions flag destination
			size++;
			memcpy (buf + size, m_Destination->GetIdentHash (), 32);
			size += 32;
		}
		else
		{
			buf[size] = 0; // delivery instructions flag local
			size++;
		}

		memcpy (buf + size, msg->GetBuffer (), msg->GetLength ());
		size += msg->GetLength ();
		uint32_t cloveID;
		if (RAND_bytes ((uint8_t *)&cloveID, sizeof (cloveID)) != 1)
		{
			LogPrint (eLogError, "Garlic: Can't generate clove ID");
			return 0;
		}
		htobe32buf (buf + size, cloveID); // CloveID
		size += 4;
		htobe64buf (buf + size, ts); // Expiration of clove
		size += 8;
		memset (buf + size, 0, 3); // certificate of clove
		size += 3;
		return size;
	}

	size_t ElGamalAESSession::CreateDeliveryStatusClove (uint8_t * buf, size_t capacity, uint32_t msgID)
	{
		size_t size = 0;
		if (GetOwner ())
		{
			auto pool = GetOwner ()->GetTunnelPool ();
			auto inboundTunnel = pool ? pool->GetNextInboundTunnel () : nullptr;
			if (inboundTunnel)
			{
				// The encrypted DeliveryStatus (one fixed-size tag set) plus
				// the tunnel/clove headers is far below this bound. Check it
				// before creating the key so a failed attempt does not leave
				// an unused tag behind.
				if (capacity < 512) return 0;
				// create msg first so the exact capacity check happens before
				// writing
				auto msg = CreateEncryptedDeliveryStatusMsg (msgID);
				if (!msg) return 0;
				const size_t needed = 1 + 32 + 4 + msg->GetLength () + 4 + 8 + 3;
				if (capacity < needed) return 0;
				buf[size] = eGarlicDeliveryTypeTunnel << 5; // delivery instructions flag tunnel
				size++;
				// hash and tunnelID sequence is reversed for Garlic
				memcpy (buf + size, inboundTunnel->GetNextIdentHash (), 32); // To Hash
				size += 32;
				htobe32buf (buf + size, inboundTunnel->GetNextTunnelID ()); // tunnelID
				size += 4;
				memcpy (buf + size, msg->GetBuffer (), msg->GetLength ());
				size += msg->GetLength ();
				// fill clove
				uint64_t ts = i2p::util::GetMillisecondsSinceEpoch () + 8000; // 8 sec
				uint32_t cloveID;
				if (RAND_bytes ((uint8_t *)&cloveID, sizeof (cloveID)) != 1)
				{
					LogPrint (eLogError, "Garlic: Can't generate DeliveryStatus clove ID");
					return 0;
				}
				htobe32buf (buf + size, cloveID); // CloveID
				size += 4;
				htobe64buf (buf + size, ts); // Expiration of clove
				size += 8;
				memset (buf + size, 0, 3); // certificate of clove
				size += 3;
			}
			else
				LogPrint (eLogError, "Garlic: No inbound tunnels in the pool for DeliveryStatus");
		}
		else
			LogPrint (eLogWarning, "Garlic: Missing local LeaseSet");

		return size;
	}

	std::unique_ptr<ElGamalAESSession::UnconfirmedTags> ElGamalAESSession::GenerateSessionTags ()
	{
		auto tags = std::make_unique<UnconfirmedTags> (m_NumTags);
		tags->tagsCreationTime = static_cast<uint32_t> (i2p::util::GetSecondsSinceEpoch ());
		for (auto & tag : tags->sessionTags)
		{
			if (!tag.Randomize ())
			{
				// Never announce a partially random or zero tag set.
				for (auto & failed : tags->sessionTags) failed.Fill (0);
				LogPrint (eLogError, "Garlic: Can't generate ElGamal/AES session tags");
				return nullptr;
			}
			tag.creationTime = tags->tagsCreationTime;
		}
		return tags;
	}

	bool ElGamalAESSession::MessageConfirmed (uint32_t msgID)
	{
		std::lock_guard<std::recursive_mutex> l (m_StateMutex);
		TagsConfirmed (msgID);
		if (!GarlicRoutingSession::MessageConfirmed (msgID)) CleanupExpiredTags ();
		return true;
	}

	void ElGamalAESSession::TagsConfirmed (uint32_t msgID)
	{
		uint32_t ts = static_cast<uint32_t> (i2p::util::GetSecondsSinceEpoch ());
		auto it = m_UnconfirmedTagsMsgs.find (msgID);
		if (it != m_UnconfirmedTagsMsgs.end ())
		{
			auto & tags = it->second;
			if (ts < tags->tagsCreationTime + OUTGOING_TAGS_EXPIRATION_TIMEOUT)
			{
				for (const auto & tag : tags->sessionTags) m_SessionTags.push_back (tag);
			}
			m_UnconfirmedTagsMsgs.erase (it);
		}
	}

	bool ElGamalAESSession::CleanupExpiredTags ()
	{
		std::lock_guard<std::recursive_mutex> l (m_StateMutex);
		auto ts = i2p::util::GetSecondsSinceEpoch ();
		for (auto it = m_SessionTags.begin (); it != m_SessionTags.end ();)
		{
			if (ts >= it->creationTime + OUTGOING_TAGS_EXPIRATION_TIMEOUT)
				it = m_SessionTags.erase (it);
			else
				++it;
		}
		CleanupUnconfirmedTags ();
		CleanupUnconfirmedLeaseSet (ts);
		return !m_SessionTags.empty () || !m_UnconfirmedTagsMsgs.empty ();
	}

	bool ElGamalAESSession::CleanupUnconfirmedTags ()
	{
		std::lock_guard<std::recursive_mutex> l (m_StateMutex);
		bool ret = false;
		uint32_t ts = static_cast<uint32_t> (i2p::util::GetSecondsSinceEpoch ());
		// delete expired unconfirmed tags
		for (auto it = m_UnconfirmedTagsMsgs.begin (); it != m_UnconfirmedTagsMsgs.end ();)
		{
			if (ts >= it->second->tagsCreationTime + OUTGOING_TAGS_CONFIRMATION_TIMEOUT)
			{
				if (GetOwner ()) GetOwner ()->RemoveDeliveryStatusSession (it->first);
				it = m_UnconfirmedTagsMsgs.erase (it);
				ret = true;
			}
			else
				++it;
		}
		return ret;
	}

	GarlicDestination::GarlicDestination ()
		: m_IsIdling (false)
		, m_NumTags (32)
		, // 32 tags by default
		m_LastIncomingSessionTimestamp (0)
		, m_NumRatchetInboundTags (0) // 0 means standard
	{
	}

	std::mt19937 & GarlicDestination::GetRng ()
	{
		// One generator per thread: the destination service, streaming,
		// datagram, torrent and timer threads all ask for random selection,
		// and a shared std::mt19937 would be read and written concurrently.
		thread_local std::mt19937 rng (i2p::util::GetRngSeed ());
		return rng;
	}

	GarlicDestination::~GarlicDestination () = default;

	void GarlicDestination::CleanUp ()
	{
		// Detach and destroy every session outside the locks. The ElGamal map
		// used to be iterated and cleared without any lock while another
		// thread could insert into it, and every map was destroyed under its
		// mutex.
		std::unordered_map<i2p::data::IdentHash, ElGamalAESSessionPtr> sessions;
		std::unordered_map<i2p::data::Tag<32>, ECIESX25519AEADRatchetSessionPtr> ratchetSessions;
		std::unordered_map<uint32_t, GarlicRoutingSessionPtr> deliveryStatusSessions;
		std::unordered_map<SessionTag, std::shared_ptr<AESDecryption>, std::hash<i2p::data::Tag<32>>> tags;
		std::unordered_map<uint64_t, ECIESX25519AEADRatchetIndexTagset> ratchetTags;
		std::deque<std::pair<i2p::data::Tag<32>, uint64_t>> newSessionKeys;
		{
			std::lock_guard<std::mutex> l (m_SessionsMutex);
			sessions.swap (m_Sessions);
			ratchetSessions.swap (m_ECIESx25519Sessions);
		}
		{
			std::lock_guard<std::mutex> l (m_DeliveryStatusSessionsMutex);
			deliveryStatusSessions.swap (m_DeliveryStatusSessions);
		}
		{
			std::lock_guard<std::mutex> l (m_TagsMutex);
			tags.swap (m_Tags);
			ratchetTags.swap (m_ECIESx25519Tags);
		}
		{
			std::lock_guard<std::mutex> l (m_NewSessionKeysMutex);
			newSessionKeys.swap (m_NewSessionKeys);
		}
		m_CloveReplayFilter.Clear ();
		for (auto & it : sessions) it.second->SetOwner (nullptr);
		for (auto & it : ratchetSessions)
		{
			it.second->Terminate ();
			it.second->SetOwner (nullptr);
		}
		// The local maps and the tag maps are destroyed here, outside every
		// lock, because a tagset destructor can call back into its session.
	}
	void GarlicDestination::AddSessionKey (const uint8_t * key, const uint8_t * tag)
	{
		if (key && tag)
		{
			uint32_t ts = static_cast<uint32_t> (i2p::util::GetSecondsSinceEpoch ());
			auto decryption = std::make_shared<AESDecryption> (key);
			std::lock_guard<std::mutex> l (m_TagsMutex);
			m_Tags[SessionTag (tag, ts)] = std::move (decryption);
		}
	}

	void GarlicDestination::MakeRoomForECIESx25519Tag (size_t minSlots, std::vector<ReceiveRatchetTagSetPtr> * removed)
	{
		if (!minSlots) minSlots = 1;
		if (m_ECIESx25519Tags.size () + minSlots <= MAX_INCOMING_ECIES_TAGS) return;
		auto drop = [removed] (ECIESX25519AEADRatchetIndexTagset & entry)
		{
			// Keep the tagset alive until the caller has released m_TagsMutex.
			if (removed && entry.tagset) removed->push_back (std::move (entry.tagset));
		};
		const auto now = i2p::util::GetSecondsSinceEpoch ();
		for (auto it = m_ECIESx25519Tags.begin (); it != m_ECIESx25519Tags.end ();)
		{
			const auto & entry = it->second;
			if (!entry.tagset || entry.tagset->IsExpired (now) || entry.tagset->IsIndexExpired (entry.index)
				|| entry.tagset->IsSessionTerminated ())
			{
				drop (it->second);
				it = m_ECIESx25519Tags.erase (it);
			}
			else
				++it;
		}
		// The map has no insertion-order metadata. Evicting arbitrary entries
		// only affects old tag windows; keeping the hard bound is more
		// important than preserving one abusive session's tags. A later
		// message fails authentication/decryption instead of blocking all new
		// handshakes.
		while (m_ECIESx25519Tags.size () + minSlots > MAX_INCOMING_ECIES_TAGS && !m_ECIESx25519Tags.empty ())
		{
			auto it = m_ECIESx25519Tags.begin ();
			drop (it->second);
			m_ECIESx25519Tags.erase (it);
		}
	}

	void GarlicDestination::AddECIESx25519Key (const uint8_t * key, uint64_t tag)
	{
		if (!key) return;
		auto tagset = std::make_shared<SymmetricKeyTagSet> (this, key);
		std::vector<ReceiveRatchetTagSetPtr> removed;
		{
			std::lock_guard<std::mutex> l (m_TagsMutex);
			MakeRoomForECIESx25519Tag (1, &removed);
			if (m_ECIESx25519Tags.size () >= MAX_INCOMING_ECIES_TAGS)
				LogPrint (eLogWarning, "Garlic: Incoming ECIES tag budget exceeded");
			else
				m_ECIESx25519Tags.emplace (tag, ECIESX25519AEADRatchetIndexTagset { 0, tagset });
		}
		// Any removed tagset is released here, outside m_TagsMutex, because
		// its destructor calls back into the session.
		removed.clear ();
	}

	bool GarlicDestination::SubmitSessionKey (const uint8_t * key, const uint8_t * tag)
	{
		AddSessionKey (key, tag);
		return true;
	}

	void GarlicDestination::SubmitECIESx25519Key (const uint8_t * key, uint64_t tag)
	{
		AddECIESx25519Key (key, tag);
	}

	void GarlicDestination::HandleGarlicMessage (std::shared_ptr<I2NPMessage> msg)
	{
		// Envelope selection is a fixed order, not a guess: (1) an
		// ECIES-X25519 ratchet tag, (2) an ElGamal/AES+SessionTag block
		// (16-byte aligned and at least 32 bytes for the tag), (3) a
		// new-session envelope, chosen as ElGamal/AES only when the length is
		// 2 mod 16, at least 514 bytes (the encrypted block) and the
		// destination still has an ElGamal key; otherwise an ECIES-X25519 New
		// Session message is assumed. A failed step must not consume state
		// for the next one, so a tag is erased only by the branch that then
		// uses it. Runs on the executor that owns this destination
		// (LeaseSetDestination drains its incoming queue on its own service).
		// The shared tag and session maps are still locked because transport
		// threads insert received session keys and tags from outside that
		// executor.
		if (!msg || msg->GetLength () < I2NP_HEADER_SIZE + 4)
		{
			if (g_GarlicParseDrops.Record ())
				LogPrint (eLogWarning, "Garlic: Message is too short for a length prefix");
			return;
		}
		uint8_t * buf = msg->GetPayload ();
		const size_t payloadLength = msg->GetLength () - I2NP_HEADER_SIZE;
		uint32_t length = bufbe32toh (buf);
		if (length > payloadLength - 4)
		{
			if (g_GarlicParseDrops.Record ())
				LogPrint (
					eLogWarning, "Garlic: Message length ", length, " exceeds payload length ", payloadLength - 4);
			return;
		}
		if (length < 8)
		{
			// every supported encryption envelope is longer than the tag
			if (g_GarlicParseDrops.Record ())
				LogPrint (eLogWarning, "Garlic: Message length ", length, " is too short");
			return;
		}
		auto mod = length & 0x0f; // %16
		buf += 4; // length

		// try ECIESx25519 tag, might be used even is ratchets not supported
		bool found = HandleECIESx25519TagMessage (buf, length);
		if (!found)
		{
			// AES block is a multiple of 16 and starts with a 32-byte tag.
			std::shared_ptr<AESDecryption> decryption;
			if (!mod && length >= 32)
			{
				std::lock_guard<std::mutex> l (m_TagsMutex);
				auto it = m_Tags.find (SessionTag (buf));
				if (it != m_Tags.end ())
				{
					// tag found. Use AES; a tag is consumed by exactly one
					// message even if decryption or the payload later fails.
					decryption = it->second;
					m_Tags.erase (it);
				}
			}
			if (decryption)
			{
				uint8_t iv[32]; // IV is first 16 bytes
				SHA256 (buf, 32, iv);
				decryption->Decrypt (buf + 32, length - 32, iv, buf + 32);
				HandleAESBlock (buf + 32, length - 32, decryption, msg->from);
				found = true;
			}
			if (!found) // assume new session
			{
				// AES tag not found. Handle depending on encryption type
				// try ElGamal/AES first if leading block is 514
				ElGamalBlock elGamal;
				if (mod == 2 && length >= 514 && SupportsEncryptionType (i2p::data::CRYPTO_KEY_TYPE_ELGAMAL)
					&& Decrypt (buf, (uint8_t *)&elGamal, i2p::data::CRYPTO_KEY_TYPE_ELGAMAL))
				{
					auto newSessionDecryption = std::make_shared<AESDecryption> (elGamal.sessionKey);
					uint8_t iv[32]; // IV is first 16 bytes
					SHA256 (elGamal.preIV, 32, iv);
					newSessionDecryption->Decrypt (buf + 514, length - 514, iv, buf + 514);
					HandleAESBlock (buf + 514, length - 514, newSessionDecryption, msg->from);
				}
				else if (SupportsRatchets ())
				{
					// otherwise ECIESx25519
					auto ts = i2p::util::GetMillisecondsSinceEpoch ();
					if (ts > m_LastIncomingSessionTimestamp + INCOMING_SESSIONS_MINIMAL_INTERVAL)
					{
						auto session = std::make_shared<ECIESX25519AEADRatchetSession> (this, false); // incoming
						if (session->HandleNextMessage (buf, length, nullptr, 0))
							m_LastIncomingSessionTimestamp = ts;
						else if (g_GarlicParseDrops.Record ())
							LogPrint (eLogError, "Garlic: Can't handle ECIES-X25519-AEAD-Ratchet message");
					}
					else if (g_GarlicParseDrops.Record ())
						LogPrint (eLogWarning, "Garlic: Incoming sessions come too often");
				}
				else if (g_GarlicParseDrops.Record ())
					LogPrint (eLogError, "Garlic: Failed to decrypt message");
			}
		}
	}

	bool GarlicDestination::HandleECIESx25519TagMessage (uint8_t * buf, size_t len)
	{
		if (!buf || len < 8) return false;
		uint64_t tag;
		memcpy (&tag, buf, 8);
		ReceiveRatchetTagSetPtr tagset;
		int index = 0;
		{
			std::lock_guard<std::mutex> l (m_TagsMutex);
			auto it = m_ECIESx25519Tags.find (tag);
			if (it == m_ECIESx25519Tags.end ()) return false;
			// The callback can insert more tags into the same unordered_map
			// (GenerateMoreReceiveTags / NEXT_KEY). Erase before invoking it so
			// a rehash cannot invalidate this iterator or erase the wrong tag.
			tagset = it->second.tagset;
			index = it->second.index;
			m_ECIESx25519Tags.erase (it);
		}
		if (!tagset || !tagset->HandleNextMessage (buf, len, index))
		{
			if (g_GarlicParseDrops.Record ())
				LogPrint (eLogError, "Garlic: Can't handle ECIES-X25519-AEAD-Ratchet message");
		}
		return true;
	}

	void GarlicDestination::HandleAESBlock (uint8_t * buf, size_t len, std::shared_ptr<AESDecryption> decryption,
		std::shared_ptr<i2p::tunnel::InboundTunnel> from)
	{
		if (len < 2)
		{
			if (g_GarlicParseDrops.Record ()) LogPrint (eLogError, "Garlic: AES block length ", len, " is too short");
			return;
		}
		uint16_t tagCount = bufbe16toh (buf);
		buf += 2;
		len -= 2;
		if (tagCount > MAX_ELGAMAL_AES_TAGS)
		{
			if (g_GarlicParseDrops.Record ())
				LogPrint (
					eLogError, "Garlic: Tag count ", tagCount, " exceeds the maximum ", (int)MAX_ELGAMAL_AES_TAGS);
			return;
		}
		const uint8_t * tagsStart = buf;
		uint32_t tagsCreationTime = 0;
		if (tagCount > 0)
		{
			const size_t tagBytes = static_cast<size_t> (tagCount) * 32;
			if (tagBytes > len)
			{
				if (g_GarlicParseDrops.Record ())
					LogPrint (eLogError, "Garlic: Tag count ", tagCount, " exceeds length ", len);
				return;
			}
			tagsCreationTime = static_cast<uint32_t> (i2p::util::GetSecondsSinceEpoch ());
		}
		buf += tagCount * 32;
		len -= tagCount * 32;
		if (len < 37) // 4 (payload size) + 32 (payload hash) + 1 (flag)
		{
			if (g_GarlicParseDrops.Record ())
				LogPrint (eLogError, "Garlic: AES block is too short for payload header, length ", len);
			return;
		}
		uint32_t payloadSize = bufbe32toh (buf);
		buf += 4;
		len -= 4;
		uint8_t * payloadHash = buf;
		buf += 32;
		len -= 32; // payload hash.
		const uint8_t * newSessionKey = nullptr;
		if (*buf == 0x01) // rekey flag; any other value means no session key
		{
			if (len < 33) // 32 (new session key) + 1 (flag)
			{
				if (g_GarlicParseDrops.Record ())
					LogPrint (eLogError, "Garlic: AES block is too short for session key, length ", len);
				return;
			}
			newSessionKey = buf + 1;
			buf += 32;
			len -= 32; // new session key
		}
		buf++;
		len--; // flag
		if (payloadSize > len)
		{
			if (g_GarlicParseDrops.Record ()) LogPrint (eLogError, "Garlic: Unexpected payload size ", payloadSize);
			return;
		}

		// payload
		uint8_t digest[32];
		SHA256 (buf, payloadSize, digest);
		if (CRYPTO_memcmp (payloadHash, digest, 32)) // payload hash doesn't match
		{
			if (g_GarlicParseDrops.Record ()) LogPrint (eLogError, "Garlic: Wrong payload hash");
			return;
		}
		// Only install the announced session tags once the payload hash has
		// been verified. Previously unauthenticated tags were inserted before
		// the check and could be used to grow m_Tags with forged entries. A
		// rekey flag associates the new tags with the announced session key,
		// exactly as Java's tagsReceived(foundKey, tags) does.
		std::shared_ptr<AESDecryption> tagDecryption = decryption;
		if (newSessionKey) tagDecryption = std::make_shared<AESDecryption> (newSessionKey);
		{
			std::lock_guard<std::mutex> l (m_TagsMutex);
			// A full tag budget must not make the receiver drop an otherwise
			// authenticated message: skip the tag installation and keep
			// processing the payload. The peer will fall back to ElGamal or a
			// ratchet once its tags are not confirmed.
			if (m_Tags.size () + static_cast<size_t> (tagCount) > MAX_INCOMING_ELGAMAL_TAGS)
			{
				if (g_ElGamalTagBudgetDrops.Record ())
					LogPrint (eLogError, "Garlic: Incoming ElGamal/AES tag budget exceeded, tags skipped (",
						g_ElGamalTagBudgetDrops.GetCount (), " blocks)");
			}
			else
			{
				for (int i = 0; i < tagCount; i++)
					m_Tags[SessionTag (tagsStart + i * 32, tagsCreationTime)] = tagDecryption;
			}
		}
		HandleGarlicPayload (buf, payloadSize, from);
	}

	bool GarlicDestination::IsDuplicateClove (uint32_t msgID, uint64_t expiration)
	{
		const uint32_t key = GetI2NPDuplicateKey (msgID, expiration, I2NP_DUPLICATE_SALT_DESTINATION);
		if (!m_CloveReplayFilter.CheckAndInsert (key, i2p::util::GetSecondsSinceEpoch ())) return false;
		++GetI2NPDispatchCounters ().replayedClove;
		if (g_CloveReplayDrops.Record ())
			LogPrint (eLogWarning, "Garlic: Skipped replayed clove msgID ", msgID, " (",
				GetI2NPDispatchCounters ().replayedClove.load (), " replayed cloves)");
		return true;
	}

	void GarlicDestination::HandleGarlicPayload (
		uint8_t * buf, size_t len, std::shared_ptr<i2p::tunnel::InboundTunnel> from)
	{
		if (len < 1)
		{
			if (g_GarlicParseDrops.Record ()) LogPrint (eLogError, "Garlic: Payload is too short");
			return;
		}
		const unsigned int numCloves = buf[0];
		LogPrint (eLogDebug, "Garlic: ", numCloves, " cloves");
		if (!IsValidCloveCount (numCloves))
		{
			++GetI2NPDispatchCounters ().badCloveCount;
			if (g_GarlicParseDrops.Record ())
				LogPrint (eLogWarning, "Garlic: Clove count ", numCloves, " is outside 1..", I2NP_MAX_CLOVES);
			return;
		}
		buf++;
		len--;
		// Two-phase walk per clove: classify first (framing, freshness,
		// addressed destination), then deliver. A clove that is merely
		// skipped still has intact framing, so the shared advance at the
		// bottom of the loop moves past exactly one clove and the walk
		// continues; broken framing is the only reason to stop the whole
		// payload. `from` marks tunnel delivery, which decides whether a
		// TUNNEL clove is forwarded through this destination's outbound
		// tunnel or sent directly with a gateway wrapper.
		for (unsigned int i = 0; i < numCloves; i++)
		{
			if (len < 1) // no flag byte left for the announced clove
			{
				if (g_GarlicParseDrops.Record ()) LogPrint (eLogError, "Garlic: Payload ended before clove ", i);
				break;
			}
			uint8_t * const buf1 = buf;
			// delivery instructions
			uint8_t flag = buf[0];
			buf++; // flag
			if (flag & 0x80) // encrypted?
			{
				// TODO: implement
				if (g_GarlicParseDrops.Record ()) LogPrint (eLogWarning, "Garlic: Clove encrypted");
				if (static_cast<size_t> (buf - buf1) + 32 > len)
				{
					if (g_GarlicParseDrops.Record ()) LogPrint (eLogError, "Garlic: Encrypted clove is too short");
					break;
				}
				buf += 32;
			}
			ptrdiff_t offset = buf - buf1;
			const GarlicDeliveryType deliveryType = (GarlicDeliveryType)((flag >> 5) & 0x03);
			// Locate the embedded I2NP message. LOCAL carries no delivery hash;
			// DESTINATION, ROUTER and TUNNEL prefix one (TUNNEL also a 4-byte
			// tunnel ID). Everything is relative to buf1 so the shared advance
			// below can skip exactly one clove.
			size_t messageOffset = buf - buf1;
			if (deliveryType != eGarlicDeliveryTypeLocal) messageOffset += 32;
			if (deliveryType == eGarlicDeliveryTypeTunnel) messageOffset += 4;
			bool wellFramed = messageOffset <= len;
			bool fresh = false;
			if (wellFramed)
			{
				uint8_t * message = buf1 + messageOffset;
				const size_t messageLength = GetI2NPMessageLength (message, len - messageOffset);
				// The trailer (CloveID, Date, Certificate) has a fixed length, so
				// the message plus trailer must fit in what is left.
				wellFramed = messageLength >= I2NP_HEADER_SIZE && messageLength + messageOffset + 15 <= len;
				if (wellFramed) fresh = IsFreshCloveDate (bufbe64toh (message + messageLength + 4));
			}
			// Only a DESTINATION clove names a destination; the other types are
			// local or explicitly addressed and need no local-hash check.
			const bool destinationMatches = deliveryType != eGarlicDeliveryTypeDestination
				|| (static_cast<size_t> (buf - buf1) + 32 <= len && IsCloveForDestination (buf, GetIdentHash ()));
			const GarlicCloveDisposition disposition = ClassifyGarlicClove (wellFramed, fresh, destinationMatches);
			if (disposition == GarlicCloveDisposition::RejectMessage)
			{
				if (g_GarlicParseDrops.Record ()) LogPrint (eLogError, "Garlic: Invalid clove framing, walk stopped");
				break;
			}
			if (disposition == GarlicCloveDisposition::SkipStale)
			{
				++GetI2NPDispatchCounters ().staleClove;
				if (g_CloveStaleDrops.Record ())
					LogPrint (eLogWarning, "Garlic: Skipped stale clove (",
						GetI2NPDispatchCounters ().staleClove.load (), " stale cloves)");
			}
			else if (disposition == GarlicCloveDisposition::SkipWrongDestination)
			{
				++GetI2NPDispatchCounters ().wrongDestination;
				if (g_CloveDestinationDrops.Record ())
					LogPrint (eLogWarning, "Garlic: Skipped clove for another destination (",
						GetI2NPDispatchCounters ().wrongDestination.load (), " such cloves)");
			}
			else
				switch (deliveryType)
				{
				case eGarlicDeliveryTypeLocal:
					LogPrint (eLogDebug, "Garlic: Type local");
					if (IsDuplicateClove (bufbe32toh (buf + I2NP_HEADER_MSGID_OFFSET),
							bufbe64toh (buf + I2NP_HEADER_EXPIRATION_OFFSET)))
						break;
					HandleI2NPMessage (buf, len - offset);
					break;
				case eGarlicDeliveryTypeDestination:
					LogPrint (eLogDebug, "Garlic: Type destination");
					buf += 32; // destination, checked above
					offset = buf - buf1;
					if (IsDuplicateClove (bufbe32toh (buf + I2NP_HEADER_MSGID_OFFSET),
							bufbe64toh (buf + I2NP_HEADER_EXPIRATION_OFFSET)))
						break;
					HandleI2NPMessage (buf, len - offset);
					break;
				case eGarlicDeliveryTypeTunnel:
				{
					LogPrint (eLogDebug, "Garlic: Type tunnel");
					// gwHash and gwTunnel sequence is reverted
					uint8_t * gwHash = buf;
					buf += 32;
					offset = buf - buf1;
					const uint32_t gwTunnel = bufbe32toh (buf);
					buf += 4;
					offset += 4;
					const size_t messageLength = GetI2NPMessageLength (buf, len - offset);
					if (IsDuplicateClove (bufbe32toh (buf + I2NP_HEADER_MSGID_OFFSET),
							bufbe64toh (buf + I2NP_HEADER_EXPIRATION_OFFSET)))
						break;
					auto msg = CreateI2NPMessage (buf, messageLength, from);
					if (!msg)
					{
						if (g_GarlicParseDrops.Record ()) LogPrint (eLogError, "Garlic: Can't allocate I2NP message");
						break;
					}
					if (from) // received through an inbound tunnel
					{
						std::shared_ptr<i2p::tunnel::OutboundTunnel> tunnel;
						if (from->GetTunnelPool ())
							tunnel = from->GetTunnelPool ()->GetNextOutboundTunnel ();
						else
							LogPrint (eLogError, "Garlic: Tunnel pool is not set for inbound tunnel");
						if (tunnel) // we have sent it through an outbound tunnel
							tunnel->SendTunnelDataMsgTo (gwHash, gwTunnel, msg);
						else
							LogPrint (eLogWarning, "Garlic: No outbound tunnels available for garlic clove");
					}
					else // received directly
						i2p::transport::transports.SendMessage (
							gwHash, i2p::CreateTunnelGatewayMsg (gwTunnel, msg)); // send directly
					break;
				}
				case eGarlicDeliveryTypeRouter:
				{
					uint8_t * ident = buf; // router hash
					buf += 32;
					offset = buf - buf1;
					if (!from) // received directly
					{
						// A repeated ROUTER clove would forward the same message
						// again; drop it like every other delivery type.
						if (IsDuplicateClove (bufbe32toh (buf + I2NP_HEADER_MSGID_OFFSET),
								bufbe64toh (buf + I2NP_HEADER_EXPIRATION_OFFSET)))
							break;
						const size_t messageLength = GetI2NPMessageLength (buf, len - offset);
						auto msg = CreateI2NPMessage (buf, messageLength);
						if (!msg)
						{
							if (g_GarlicParseDrops.Record ())
								LogPrint (eLogError, "Garlic: Can't allocate I2NP message");
							break;
						}
						i2p::transport::transports.SendMessage (ident, msg);
					}
					else
						LogPrint (eLogWarning, "Garlic: Type router for inbound tunnels not supported");
					break;
				}
				default: break;
				}
			// Advance past exactly this clove: skip the embedded message and
			// the fixed trailer. A skipped clove has intact framing by
			// definition, so the walk continues with the next one.
			buf = buf1 + messageOffset;
			offset = messageOffset;
			const size_t messageLength = GetI2NPMessageLength (buf, len - offset);
			buf += messageLength + 15; // I2NP + CloveID + Date + Certificate
			len -= (buf - buf1);
		}
	}

	std::shared_ptr<I2NPMessage> GarlicDestination::WrapMessageForRouter (
		std::shared_ptr<const i2p::data::RouterInfo> router, std::shared_ptr<I2NPMessage> msg)
	{
		if (router->GetEncryptionType () == i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD)
			return WrapECIESX25519MessageForRouter (msg, router->GetIdentity ()->GetEncryptionPublicKey ());
		else
		{
			auto session = GetRoutingSession (router, false);
			if (!session)
			{
				LogPrint (eLogError, "Garlic: No routing session for router encryption type ",
					(int)router->GetEncryptionType ());
				return nullptr;
			}
			return session->WrapSingleMessage (msg);
		}
	}

	std::shared_ptr<GarlicRoutingSession> GarlicDestination::GetRoutingSession (
		std::shared_ptr<const i2p::data::RoutingDestination> destination, bool attachLeaseSet,
		bool requestNewIfNotFound)
	{
		if (destination->GetEncryptionType () >= i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD)
		{
			if (SupportsEncryptionType (destination->GetEncryptionType ()))
			{
				ECIESX25519AEADRatchetSessionPtr session;
				uint8_t staticKey[32] { };
				if (!destination->Encrypt (nullptr, staticKey)) // we are supposed to get static key
				{
					LogPrint (eLogError, "Garlic: Destination has no ratchet static key for type ",
						(int)destination->GetEncryptionType ());
					return nullptr;
				}
				{
					std::unique_lock<std::mutex> l (m_SessionsMutex);
					auto it = m_ECIESx25519Sessions.find (staticKey);
					if (it != m_ECIESx25519Sessions.end ())
					{
						session = it->second;
						if (session->IsExhausted ())
						{
							// Java RatchetSKM.consumeNext() starts a new session
							// when the outbound tagset is empty. Keep the old
							// object for receiving and let the caller create a
							// fresh New Session for sending.
							LogPrint (eLogDebug, "Garlic: Sending session exhausted, starting a new one");
							requestNewIfNotFound = true;
							session = nullptr;
						}
						else if (session->IsInactive (i2p::util::GetSecondsSinceEpoch ()))
						{
							LogPrint (eLogDebug, "Garlic: Session restarted");
							requestNewIfNotFound = true; // it's not a new session
							session = nullptr;
						}
					}
				}
				if (!session && requestNewIfNotFound)
				{
					session = std::make_shared<ECIESX25519AEADRatchetSession> (this, true);
					session->SetRemoteStaticKey (destination->GetEncryptionType (), staticKey);
				}
				if (session && destination->IsDestination ())
					session->SetDestination (destination->GetIdentHash ()); // NS or NSR
				return session;
			}
			else
				LogPrint (eLogError, "Garlic: Non-supported encryption type ", destination->GetEncryptionType ());
		}
		else
		{
			ElGamalAESSessionPtr session;
			{
				std::unique_lock<std::mutex> l (m_SessionsMutex);
				auto it = m_Sessions.find (destination->GetIdentHash ());
				if (it != m_Sessions.end ()) session = it->second;
			}
			if (!session)
			{
				// Built outside the map lock: the constructor generates the
				// session key and tag set. Two racing callers can both create a
				// session and the later insert wins; that is harmless because no
				// tag from the loser has been announced yet, so nothing can be
				// encrypted with it.
				session = std::make_shared<ElGamalAESSession> (this, destination, attachLeaseSet ? m_NumTags : 4,
					attachLeaseSet); // specified num tags for connections and 4 for LS requests
				std::unique_lock<std::mutex> l (m_SessionsMutex);
				m_Sessions[destination->GetIdentHash ()] = session;
			}
			return session;
		}
		return nullptr;
	}

	void GarlicDestination::CleanupExpiredTags ()
	{
		// incoming
		uint32_t ts = static_cast<uint32_t> (i2p::util::GetSecondsSinceEpoch ());
		int numExpiredTags = 0;
		{
			std::lock_guard<std::mutex> l (m_TagsMutex);
			for (auto it = m_Tags.begin (); it != m_Tags.end ();)
			{
				if (ts > it->first.creationTime + INCOMING_TAGS_EXPIRATION_TIMEOUT)
				{
					numExpiredTags++;
					it = m_Tags.erase (it);
				}
				else
					++it;
			}
		}
		if (numExpiredTags > 0)
			LogPrint (eLogDebug, "Garlic: ", numExpiredTags, " tags expired for ", GetIdentHash ().ToBase64 ());

		// outgoing
		{
			// A session method takes the session's own state lock, and a session
			// that registers itself with this destination takes that lock before
			// m_SessionsMutex. Calling one while holding the map lock would be
			// the opposite order and could deadlock, so the map is only
			// snapshotted here and every callback runs outside the lock.
			std::vector<std::pair<i2p::data::IdentHash, ElGamalAESSessionPtr>> sessions;
			{
				std::unique_lock<std::mutex> l (m_SessionsMutex);
				sessions.reserve (m_Sessions.size ());
				for (const auto & it : m_Sessions) sessions.emplace_back (it.first, it.second);
			}
			for (auto & it : sessions)
			{
				if (!it.second) continue;
				it.second->GetSharedRoutingPath (); // delete shared path if necessary
				if (it.second->CleanupExpiredTags ()) continue;
				bool erased = false;
				{
					std::unique_lock<std::mutex> l (m_SessionsMutex);
					auto found = m_Sessions.find (it.first);
					// Never erase a session that replaced this one meanwhile.
					if (found != m_Sessions.end () && found->second == it.second)
					{
						m_Sessions.erase (found);
						erased = true;
					}
				}
				if (erased)
				{
					LogPrint (eLogInfo, "Garlic: Routing session to ", it.first.ToBase32 (), " deleted");
					it.second->SetOwner (nullptr);
				}
			}
		}
		// delivery status sessions
		{
			std::unique_lock<std::mutex> l (m_DeliveryStatusSessionsMutex);
			for (auto it = m_DeliveryStatusSessions.begin (); it != m_DeliveryStatusSessions.end ();)
			{
				if (it->second->GetOwner () != this)
					it = m_DeliveryStatusSessions.erase (it);
				else
					++it;
			}
		}
		// ECIESx25519 sessions
		{
			// Same rule as the ElGamal map: evaluate and terminate outside the
			// map lock, then erase only the entry that is still the same object.
			std::vector<std::pair<i2p::data::Tag<32>, ECIESX25519AEADRatchetSessionPtr>> sessions;
			{
				std::unique_lock<std::mutex> l (m_SessionsMutex);
				sessions.reserve (m_ECIESx25519Sessions.size ());
				for (const auto & it : m_ECIESx25519Sessions) sessions.emplace_back (it.first, it.second);
			}
			for (auto & it : sessions)
			{
				if (!it.second || !it.second->CheckExpired (ts)) continue;
				it.second->Terminate ();
				std::unique_lock<std::mutex> l (m_SessionsMutex);
				auto found = m_ECIESx25519Sessions.find (it.first);
				if (found != m_ECIESx25519Sessions.end () && found->second == it.second)
					m_ECIESx25519Sessions.erase (found);
			}
		}

		numExpiredTags = 0;
		std::vector<ReceiveRatchetTagSetPtr> removedTagsets;
		{
			std::lock_guard<std::mutex> l (m_TagsMutex);
			for (auto it = m_ECIESx25519Tags.begin (); it != m_ECIESx25519Tags.end ();)
			{
				if (!it->second.tagset || it->second.tagset->IsExpired (ts)
					|| it->second.tagset->IsIndexExpired (it->second.index))
				{
					if (it->second.tagset) it->second.tagset->DeleteSymmKey (it->second.index);
					// Hold the tagset until after the unlock: its destructor
					// calls back into the session.
					if (it->second.tagset) removedTagsets.push_back (it->second.tagset);
					it = m_ECIESx25519Tags.erase (it);
					numExpiredTags++;
				}
				else
				{
					if (it->second.tagset->IsSessionTerminated ())
					{
						removedTagsets.push_back (it->second.tagset);
						it = m_ECIESx25519Tags.erase (it);
						numExpiredTags++;
					}
					else
						++it;
				}
			}
		}
		removedTagsets.clear ();
		if (numExpiredTags > 0)
			LogPrint (
				eLogDebug, "Garlic: ", numExpiredTags, " ECIESx25519 tags expired for ", GetIdentHash ().ToBase64 ());
	}

	void GarlicDestination::RemoveDeliveryStatusSession (uint32_t msgID)
	{
		std::unique_lock<std::mutex> l (m_DeliveryStatusSessionsMutex);
		m_DeliveryStatusSessions.erase (msgID);
	}

	void GarlicDestination::DeliveryStatusSent (GarlicRoutingSessionPtr session, uint32_t msgID)
	{
		std::unique_lock<std::mutex> l (m_DeliveryStatusSessionsMutex);
		m_DeliveryStatusSessions[msgID] = session;
	}

	void GarlicDestination::HandleDeliveryStatusMessage (uint32_t msgID)
	{
		GarlicRoutingSessionPtr session;
		{
			std::unique_lock<std::mutex> l (m_DeliveryStatusSessionsMutex);
			auto it = m_DeliveryStatusSessions.find (msgID);
			if (it != m_DeliveryStatusSessions.end ())
			{
				session = it->second;
				m_DeliveryStatusSessions.erase (it);
			}
		}
		if (session)
		{
			session->MessageConfirmed (msgID);
			LogPrint (eLogDebug, "Garlic: Message ", msgID, " acknowledged");
		}
	}

	void GarlicDestination::SetLeaseSetUpdated (bool /* post */)
	{
		// Snapshot both maps and notify outside m_SessionsMutex: the setter
		// takes the session state lock, which a self-registering session
		// takes before this map lock.
		std::vector<GarlicRoutingSessionPtr> sessions;
		{
			std::unique_lock<std::mutex> l (m_SessionsMutex);
			sessions.reserve (m_Sessions.size () + m_ECIESx25519Sessions.size ());
			for (const auto & it : m_Sessions) sessions.push_back (it.second);
			for (const auto & it : m_ECIESx25519Sessions) sessions.push_back (it.second);
		}
		for (auto & session : sessions)
			if (session) session->SetLeaseSetUpdated ();
	}

	void GarlicDestination::ProcessGarlicMessage (std::shared_ptr<I2NPMessage> msg)
	{
		HandleGarlicMessage (msg);
	}

	void GarlicDestination::ProcessDeliveryStatusMessage (std::shared_ptr<I2NPMessage> msg)
	{
		// Java DeliveryStatusMessage is exactly msgID (4) + timestamp (8); a
		// longer or shorter payload means the sender and this handler
		// disagree about the message layout, so it is not used to confirm
		// anything.
		if (!msg || msg->GetLength () < I2NP_HEADER_SIZE || !IsValidDeliveryStatusPayload (msg->GetPayloadLength ()))
		{
			if (g_GarlicParseDrops.Record ())
				LogPrint (eLogWarning, "Garlic: DeliveryStatus message is not ", DELIVERY_STATUS_SIZE, " bytes");
			return;
		}
		uint32_t msgID = bufbe32toh (msg->GetPayload () + DELIVERY_STATUS_MSGID_OFFSET);
		HandleDeliveryStatusMessage (msgID);
	}

	void GarlicDestination::SaveTags ()
	{
		std::lock_guard<std::mutex> l (m_TagsMutex);
		if (m_Tags.empty ()) return;
		std::string ident = GetIdentHash ().ToBase32 ();
		std::string path = i2p::fs::DataDirPath ("tags", (ident + ".tags"));
		std::ofstream f (
			i2p::fs::CreatePrivateFile (path), std::ofstream::binary | std::ofstream::out | std::ofstream::trunc);
		uint32_t ts = static_cast<uint32_t> (i2p::util::GetSecondsSinceEpoch ());
		// 4 bytes timestamp, 32 bytes tag, 32 bytes key
		for (auto it : m_Tags)
		{
			if (ts < it.first.creationTime + INCOMING_TAGS_EXPIRATION_TIMEOUT)
			{
				f.write ((char *)&it.first.creationTime, 4);
				f.write ((char *)it.first.data (), 32);
				f.write ((char *)it.second->GetKey ().data (), 32);
			}
		}
	}

	void GarlicDestination::LoadTags ()
	{
		// Tags are handed over exactly once per clean shutdown: SaveTags()
		// writes them, this function installs only records still inside
		// INCOMING_TAGS_EXPIRATION_TIMEOUT and then removes the file (see the
		// Remove at the end). A crash therefore cannot leave a stale tag set
		// that a later start would install as if it were fresh. A short read
		// stops the loop instead of publishing a partially filled tag or key.
		std::string ident = GetIdentHash ().ToBase32 ();
		std::string path = i2p::fs::DataDirPath ("tags", (ident + ".tags"));
		uint32_t ts = static_cast<uint32_t> (i2p::util::GetSecondsSinceEpoch ());
		if (ts < i2p::fs::GetLastUpdateTime (path) + INCOMING_TAGS_EXPIRATION_TIMEOUT)
		{
			// might contain non-expired tags
			std::ifstream f (path, std::ifstream::binary);
			if (f)
			{
				std::map<i2p::crypto::AESKey, std::shared_ptr<AESDecryption>> keys;
				// 4 bytes timestamp, 32 bytes tag, 32 bytes key
				while (f)
				{
					uint32_t t = 0;
					uint8_t tag[32] = { }, key[32] = { };
					if (!f.read ((char *)&t, 4)) break;
					if (ts < t + INCOMING_TAGS_EXPIRATION_TIMEOUT)
					{
						// a short read must not leave tag/key partially
						// initialised and then install a tag from it
						if (!f.read ((char *)tag, 32) || !f.read ((char *)key, 32)) break;
					}
					else
					{
						f.seekg (64, std::ios::cur); // skip
						if (!f) break;
					}

					std::shared_ptr<AESDecryption> decryption;
					auto it = keys.find (key);
					if (it != keys.end ())
						decryption = it->second;
					else
						decryption = std::make_shared<AESDecryption> (key);
					{
						std::lock_guard<std::mutex> l (m_TagsMutex);
						m_Tags.insert (std::make_pair (SessionTag (tag, ts), decryption));
					}
				}
				{
					std::lock_guard<std::mutex> l (m_TagsMutex);
					if (!m_Tags.empty ()) LogPrint (eLogInfo, "Garlic: ", m_Tags.size (), " tags loaded for ", ident);
				}
			}
		}
		i2p::fs::Remove (path);
	}

	void CleanUpTagsFiles ()
	{
		std::vector<std::string> files;
		i2p::fs::ReadDir (i2p::fs::DataDirPath ("tags"), files);
		uint32_t ts = static_cast<uint32_t> (i2p::util::GetSecondsSinceEpoch ());
		for (auto it : files)
			if (ts >= i2p::fs::GetLastUpdateTime (it) + INCOMING_TAGS_EXPIRATION_TIMEOUT) i2p::fs::Remove (it);
	}

	void GarlicDestination::HandleECIESx25519GarlicClove (
		const uint8_t * buf, size_t len, ECIESX25519AEADRatchetSession * from)
	{
		if (!len)
		{
			if (g_GarlicParseDrops.Record ()) LogPrint (eLogError, "Garlic: Clove is empty");
			return;
		}
		const uint8_t * buf1 = buf;
		uint8_t flag = buf[0];
		buf++; // flag
		GarlicDeliveryType deliveryType = (GarlicDeliveryType)((flag >> 5) & 0x03);
		switch (deliveryType)
		{
		case eGarlicDeliveryTypeDestination:
			LogPrint (eLogDebug, "Garlic: Type destination");
			if (len < 33) // flag + 32-byte destination hash
			{
				if (g_GarlicParseDrops.Record ()) LogPrint (eLogError, "Garlic: Message is too short");
				break;
			}
			// The delivery instruction names the destination this clove is
			// for. A clove addressed to another destination must not be
			// delivered locally.
			if (!IsCloveForDestination (buf, GetIdentHash ()))
			{
				++GetI2NPDispatchCounters ().wrongDestination;
				if (g_CloveDestinationDrops.Record ())
					LogPrint (eLogWarning, "Garlic: Skipped ECIES clove for another destination (",
						GetI2NPDispatchCounters ().wrongDestination.load (), " such cloves)");
				break;
			}
			buf += 32; // destination
			[[fallthrough]];
			// no break here
		case eGarlicDeliveryTypeLocal:
		{
			LogPrint (eLogDebug, "Garlic: Type local");
			if ((size_t)(buf - buf1) + 9 > len) // typeid(1) + msgID(4) + expiration(4)
			{
				if (g_GarlicParseDrops.Record ()) LogPrint (eLogError, "Garlic: Message is too short");
				break;
			}
			I2NPMessageType typeID = (I2NPMessageType)(buf[0]);
			buf++; // typeid
			uint32_t msgID = bufbe32toh (buf);
			buf += 4; // msgID
			const uint64_t expiration = static_cast<uint64_t> (bufbe32toh (buf)) * 1000;
			buf += 4; // expiration
			if (!IsFreshCloveDate (expiration))
			{
				// This clove cannot be delivered, but the remaining blocks of
				// the same payload can: the block framing is known, so only
				// this clove is skipped.
				++GetI2NPDispatchCounters ().staleClove;
				if (g_CloveStaleDrops.Record ())
					LogPrint (eLogWarning, "Garlic: Skipped stale ECIES clove (",
						GetI2NPDispatchCounters ().staleClove.load (), " stale cloves)");
				break;
			}
			if (IsDuplicateClove (msgID, expiration)) break;
			ptrdiff_t offset = buf - buf1;
			if (offset <= (int)len)
				HandleCloveI2NPMessage (typeID, buf, len - offset, msgID, from);
			else if (g_GarlicParseDrops.Record ())
				LogPrint (eLogError, "Garlic: Clove is too long");
			break;
		}
		case eGarlicDeliveryTypeTunnel:
		{
			LogPrint (eLogDebug, "Garlic: Type tunnel");
			// gwHash and gwTunnel sequence is reverted
			const uint8_t * gwHash = buf;
			buf += 32;
			ptrdiff_t offset = buf - buf1;
			if (offset + 13 > (int)len)
			{
				if (g_GarlicParseDrops.Record ()) LogPrint (eLogError, "Garlic: Message is too short");
				break;
			}
			uint32_t gwTunnel = bufbe32toh (buf);
			buf += 4;
			I2NPMessageType typeID = (I2NPMessageType)(buf[0]);
			buf++; // typeid
			uint32_t msgID = bufbe32toh (buf);
			buf += 4; // msgID
			const uint64_t expiration = static_cast<uint64_t> (bufbe32toh (buf)) * 1000;
			buf += 4; // expiration
			if (!IsFreshCloveDate (expiration))
			{
				++GetI2NPDispatchCounters ().staleClove;
				if (g_CloveStaleDrops.Record ())
					LogPrint (eLogWarning, "Garlic: Skipped stale ECIES tunnel clove (",
						GetI2NPDispatchCounters ().staleClove.load (), " stale cloves)");
				break;
			}
			if (IsDuplicateClove (msgID, expiration)) break;
			offset += 13;
			if (GetTunnelPool ())
			{
				auto tunnel = GetTunnelPool ()->GetNextOutboundTunnel ();
				if (tunnel)
				{
					auto msg = CreateI2NPMessage (typeID, buf, len - offset, msgID);
					if (!msg)
					{
						if (g_GarlicParseDrops.Record ()) LogPrint (eLogError, "Garlic: Can't allocate I2NP message");
						break;
					}
					tunnel->SendTunnelDataMsgTo (gwHash, gwTunnel, msg);
				}
				else
					LogPrint (eLogWarning, "Garlic: No outbound tunnels available for garlic clove");
			}
			else
				LogPrint (eLogError, "Garlic: Tunnel pool is not set for inbound tunnel");
			break;
		}
		case eGarlicDeliveryTypeRouter:
			// Java can carry a ROUTER delivery instruction in a ratchet
			// garlic, but this destination has no authenticated router
			// forwarding path: the clove would have to be re-addressed and
			// re-encrypted for a different router identity. Reject it
			// explicitly instead of sending the plaintext anywhere.
			LogPrint (eLogDebug, "Garlic: Rejected router-mode ECIES clove");
			break;
		default:
			if (g_GarlicParseDrops.Record ())
				LogPrint (eLogWarning, "Garlic: Unexpected delivery type ", (int)deliveryType);
		}
	}

	bool GarlicDestination::IsDuplicateNewSessionKey (const uint8_t * key) const
	{
		if (!key) return true;
		const uint64_t now = i2p::util::GetSecondsSinceEpoch ();
		std::lock_guard<std::mutex> l (m_NewSessionKeysMutex);
		while (!m_NewSessionKeys.empty () && now > m_NewSessionKeys.front ().second + NEW_SESSION_KEY_CACHE_AGE)
			m_NewSessionKeys.pop_front ();
		for (const auto & it : m_NewSessionKeys)
			if (!memcmp (it.first.data (), key, 32)) return true;
		return false;
	}

	void GarlicDestination::AddNewSessionKey (const uint8_t * key)
	{
		if (!key) return;
		const uint64_t now = i2p::util::GetSecondsSinceEpoch ();
		std::lock_guard<std::mutex> l (m_NewSessionKeysMutex);
		while (!m_NewSessionKeys.empty () && now > m_NewSessionKeys.front ().second + NEW_SESSION_KEY_CACHE_AGE)
			m_NewSessionKeys.pop_front ();
		if (m_NewSessionKeys.size () >= NEW_SESSION_KEY_CACHE_SIZE) m_NewSessionKeys.pop_front ();
		m_NewSessionKeys.emplace_back (i2p::data::Tag<32> (key), now);
	}

	uint64_t GarlicDestination::AddECIESx25519SessionNextTag (ReceiveRatchetTagSetPtr tagset)
	{
		if (!tagset) return 0;
		// Reserve the budget before advancing the tag chain. If the map is
		// full, leaving the index unchanged lets a later cleanup generate the
		// same tag instead of permanently skipping it.
		std::vector<ReceiveRatchetTagSetPtr> removed;
		uint64_t tag = 0;
		{
			std::lock_guard<std::mutex> l (m_TagsMutex);
			MakeRoomForECIESx25519Tag (1, &removed);
			if (m_ECIESx25519Tags.size () >= MAX_INCOMING_ECIES_TAGS)
				LogPrint (eLogWarning, "Garlic: Incoming ECIES tag budget exceeded");
			else
			{
				auto index = tagset->GetNextIndex ();
				tag = tagset->GetNextSessionTag (GetThreadLocalHKDFContext ());
				if (tag) m_ECIESx25519Tags.emplace (tag, ECIESX25519AEADRatchetIndexTagset { index, tagset });
			}
		}
		// Removed tagsets are released outside m_TagsMutex.
		removed.clear ();
		return tag;
	}

	void GarlicDestination::AddECIESx25519Session (const uint8_t * staticKey, ECIESX25519AEADRatchetSessionPtr session)
	{
		if (!staticKey || !session) return;
		i2p::data::Tag<32> staticKeyTag (staticKey);
		std::unique_lock<std::mutex> l (m_SessionsMutex);
		auto it = m_ECIESx25519Sessions.find (staticKeyTag);
		if (it != m_ECIESx25519Sessions.end ())
		{
			if (it->second->IsExhausted ())
			{
				// The old session cannot send any more but may still receive
				// delayed ES messages on its registered tags. Drop the map entry
				// without terminating it; the tag map keeps it reachable until
				// those tags expire.
				if (it->second == session) return;
				m_ECIESx25519Sessions.erase (it);
			}
			else if (it->second->CanBeRestarted (i2p::util::GetSecondsSinceEpoch ()))
			{
				it->second->Terminate (); // detach
				m_ECIESx25519Sessions.erase (it);
			}
			else
			{
				LogPrint (eLogInfo, "Garlic: ECIESx25519 session with static key ", staticKeyTag.ToBase64 (),
					" already exists");
				return;
			}
		}
		m_ECIESx25519Sessions.emplace (staticKeyTag, session);
	}

	void GarlicDestination::RemoveECIESx25519Session (const uint8_t * staticKey)
	{
		if (!staticKey) return;
		std::unique_lock<std::mutex> l (m_SessionsMutex);
		auto it = m_ECIESx25519Sessions.find (i2p::data::Tag<32> (staticKey));
		if (it != m_ECIESx25519Sessions.end ())
		{
			it->second->Terminate ();
			m_ECIESx25519Sessions.erase (it);
		}
	}

	bool GarlicDestination::AEADChaCha20Poly1305Encrypt (const uint8_t * msg, size_t msgLen, const uint8_t * ad,
		size_t adLen, const uint8_t * key, const uint8_t * nonce, uint8_t * buf, size_t len)
	{
		return m_Encryptor.Encrypt (msg, msgLen, ad, adLen, key, nonce, buf, len);
	}

	bool GarlicDestination::AEADChaCha20Poly1305Decrypt (const uint8_t * msg, size_t msgLen, const uint8_t * ad,
		size_t adLen, const uint8_t * key, const uint8_t * nonce, uint8_t * buf, size_t len)
	{
		return m_Decryptor.Decrypt (msg, msgLen, ad, adLen, key, nonce, buf, len);
	}
}
}
