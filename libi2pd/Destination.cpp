/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Destination.
 *
 * Implements the client-destination lifecycle: tunnel-pool construction,
 * LeaseSet generation and re-signing, floodfill publication with retry and
 * delivery verification, and remote LeaseSet resolution. Garlic and delivery
 * status messages are posted to the destination's own asio service, while
 * cached LeaseSets, publication intervals and pending lookups are expired on
 * the destination's cleanup timer.
 *
 */

#include <algorithm>
#include <cassert>
#include <string>
#include <set>
#include <span>
#include <vector>
#include <charconv>
#include <chrono>
#include <future>
#include <cstring>
#include "Crypto.h"
#include "PostQuantum.h"
#include "ECIESX25519AEADRatchetSession.h"
#include "Log.h"
#include "FS.h"
#include "PersistentStorage.h"
#include "Timestamp.h"
#include "NetDb.hpp"
#include "I2NPProtocol.h"
#include "Destination.h"

namespace i2p
{
namespace client
{
	namespace
	{
		// Throttled diagnostics for the clove/data parse paths. A remote peer
		// controls the payloads, so a malformed message must not flood the log.
		I2NPDropCounter g_CloveParseDrops;

		// Strict decimal CryptoKeyType token. std::stoi() ignores leading
		// whitespace and accepts a leading '+' plus trailing garbage, so
		// " 6", "+6" and "6x" all used to select type 6. Java's
		// ClientConnectionRunner rejects all three.
		bool ParseLeaseSetEncTypeToken (std::string_view token, uint16_t & value)
		{
			if (token.empty () || token.size () > 5) return false;
			uint32_t result = 0;
			for (char ch : token)
			{
				if (ch < '0' || ch > '9') return false;
				result = result * 10 + static_cast<uint32_t> (ch - '0');
				if (result > 0xFFFF) return false;
			}
			value = static_cast<uint16_t> (result);
			return true;
		}

		bool IsHybridCryptoKeyType (i2p::data::CryptoKeyType type)
		{
			switch (type)
			{
			case i2p::data::CRYPTO_KEY_TYPE_ECIES_MLKEM512_X25519_AEAD:
			case i2p::data::CRYPTO_KEY_TYPE_ECIES_MLKEM768_X25519_AEAD:
			case i2p::data::CRYPTO_KEY_TYPE_ECIES_MLKEM1024_X25519_AEAD: return true;
			default: return false;
			}
		}
	} // namespace

	LeaseSetEncryptionTypes ParseLeaseSetEncryptionTypes (std::string_view value)
	{
		LeaseSetEncryptionTypes result;
		// Absent and empty are the same: the destination uses its provider
		// defaults, so there is nothing to validate.
		if (value.empty ()) return result;
		result.specified = true;
		result.valid = true;

		size_t numHybrid = 0;
		bool hasElGamal = false;
		size_t begin = 0;
		while (begin <= value.size () && result.valid)
		{
			const size_t end = value.find (',', begin);
			const auto token
				= value.substr (begin, end == std::string_view::npos ? std::string_view::npos : end - begin);
			uint16_t parsed = 0;
			if (!ParseLeaseSetEncTypeToken (token, parsed))
			{
				LogPrint (eLogError, "Destination: Malformed i2cp.leaseSetEncType entry ", token);
				result.valid = false;
				break;
			}
			const auto cryptoType = static_cast<i2p::data::CryptoKeyType> (parsed);
			if (cryptoType == i2p::data::CRYPTO_KEY_TYPE_ELGAMAL)
				hasElGamal = true;
			else if (IsHybridCryptoKeyType (cryptoType))
				numHybrid++;
			if (i2p::data::IsSelectableLS2EncryptionKeyType (cryptoType))
			{
				if (!result.preferred) result.preferred = cryptoType;
				result.types.insert (cryptoType);
			}
			else
				LogPrint (eLogInfo, "Destination: LeaseSet2 encryption type ", parsed, " is not available");
			if (end == std::string_view::npos) break;
			begin = end + 1;
		}
		if (result.valid && numHybrid > 1)
		{
			LogPrint (eLogError, "Destination: i2cp.leaseSetEncType names more than one hybrid type");
			result.valid = false;
		}
		if (result.valid && numHybrid && hasElGamal)
		{
			LogPrint (eLogError, "Destination: i2cp.leaseSetEncType cannot combine ElGamal with a hybrid type");
			result.valid = false;
		}
		if (!result.valid)
		{
			// Fail closed: an operator-configured list that this build cannot
			// honour must not silently become a different, weaker set.
			result.types.clear ();
			result.preferred = 0;
		}
		return result;
	}

	LeaseSetDestination::LeaseSetDestination (
		boost::asio::io_context & service, bool isPublic, const i2p::util::Mapping * params)
		: m_Service (service)
		, m_IsPublic (isPublic)
		, m_PublishReplyToken (0)
		, m_LastSubmissionTime (0)
		, m_PublishConfirmationTimer (m_Service)
		, m_PublishVerificationTimer (m_Service)
		, m_PublishDelayTimer (m_Service)
		, m_CleanupTimer (m_Service)
		, m_LeaseSetType (DEFAULT_LEASESET_TYPE)
		, m_AuthType (i2p::data::ENCRYPTED_LEASESET_AUTH_TYPE_NONE)
	{
		int inLen = DEFAULT_INBOUND_TUNNEL_LENGTH;
		int inQty = DEFAULT_INBOUND_TUNNELS_QUANTITY;
		int outLen = DEFAULT_OUTBOUND_TUNNEL_LENGTH;
		int outQty = DEFAULT_OUTBOUND_TUNNELS_QUANTITY;
		int inVar = DEFAULT_INBOUND_TUNNELS_LENGTH_VARIANCE;
		int outVar = DEFAULT_OUTBOUND_TUNNELS_LENGTH_VARIANCE;
		int numTags = DEFAULT_TAGS_TO_SEND;
		bool isHighBandwidth = true;
		std::shared_ptr<std::vector<i2p::data::IdentHash>> explicitPeers;
		std::string_view explicitPeersStr, trustedRoutersStr, inboundRandomKeyStr, outboundRandomKeyStr;
		try
		{
			if (params)
			{
				params->Get (I2CP_PARAM_INBOUND_TUNNEL_LENGTH, inLen);
				params->Get (I2CP_PARAM_OUTBOUND_TUNNEL_LENGTH, outLen);
				params->Get (I2CP_PARAM_INBOUND_TUNNELS_QUANTITY, inQty);
				params->Get (I2CP_PARAM_OUTBOUND_TUNNELS_QUANTITY, outQty);
				params->Get (I2CP_PARAM_INBOUND_TUNNELS_LENGTH_VARIANCE, inVar);
				params->Get (I2CP_PARAM_OUTBOUND_TUNNELS_LENGTH_VARIANCE, outVar);
				params->Get (I2CP_PARAM_TAGS_TO_SEND, numTags);
				LogPrint (eLogInfo, "Destination: Parameters for tunnel set to: ", inQty, " inbound (", inLen,
					" hops), ", outQty, " outbound (", outLen, " hops), ", numTags, " tags");
				int ratchetsInboundTags = 0;
				if (params->Get (I2CP_PARAM_RATCHET_INBOUND_TAGS, ratchetsInboundTags))
				{
					if (ratchetsInboundTags && ratchetsInboundTags < i2p::garlic::ECIESX25519_MIN_NUM_GENERATED_TAGS)
						ratchetsInboundTags = i2p::garlic::ECIESX25519_MIN_NUM_GENERATED_TAGS;
					SetNumRatchetInboundTags (ratchetsInboundTags);
				}
				explicitPeersStr = (*params)[I2CP_PARAM_EXPLICIT_PEERS];
				trustedRoutersStr = (*params)[I2CP_PARAM_TRUSTED_ROUTERS];
				inboundRandomKeyStr = (*params)[I2CP_PARAM_INBOUND_RANDOM_KEY];
				outboundRandomKeyStr = (*params)[I2CP_PARAM_OUTBOUND_RANDOM_KEY];
				m_Nickname = (*params)[I2CP_PARAM_INBOUND_NICKNAME];
				if (m_Nickname.empty ()) // try outbound
					m_Nickname = (*params)[I2CP_PARAM_OUTBOUND_NICKNAME];
				// otherwise we set default nickname in Start when we know local address
				bool dontPublishLeaseSet = true;
				if (params->Get (I2CP_PARAM_DONT_PUBLISH_LEASESET, dontPublishLeaseSet))
					m_IsPublic = !dontPublishLeaseSet; // override isPublic
				params->Get (I2CP_PARAM_LEASESET_TYPE, m_LeaseSetType);
				if (m_LeaseSetType == i2p::data::NETDB_STORE_TYPE_ENCRYPTED_LEASESET2)
				{
					// authentication for encrypted LeaseSet
					int authType = 0;
					if (params->Get (I2CP_PARAM_LEASESET_AUTH_TYPE, authType))
					{
						if (authType >= i2p::data::ENCRYPTED_LEASESET_AUTH_TYPE_NONE
							&& authType <= i2p::data::ENCRYPTED_LEASESET_AUTH_TYPE_PSK)
							m_AuthType = authType;
						else
							LogPrint (eLogError, "Destination: Unknown auth type: ", authType);
					}
				}
				auto leaseSetPrivKey = (*params)[I2CP_PARAM_LEASESET_PRIV_KEY];
				if (!leaseSetPrivKey.empty ())
				{
					m_LeaseSetPrivKey.reset (new i2p::data::Tag<32> ());
					if (m_LeaseSetPrivKey->FromBase64 (leaseSetPrivKey) != 32)
					{
						// The parameter carries a private key: log only the
						// failure class, never the value.
						LogPrint (eLogCritical,
							"Destination: i2cp.leaseSetPrivKey is not a valid base64 32-byte key; parameter rejected");
						m_LeaseSetPrivKey.reset (nullptr);
					}
				}
				int streamingProfile = 0;
				if (params->Get (I2CP_PARAM_STREAMING_PROFILE, streamingProfile))
					isHighBandwidth = streamingProfile != STREAMING_PROFILE_INTERACTIVE;
			}
		}
		catch (std::exception & ex)
		{
			LogPrint (eLogError, "Destination: Unable to parse parameters for destination: ", ex.what ());
		}
		SetNumTags (numTags);
		m_Pool = i2p::tunnel::tunnels.CreateTunnelPool (inLen, outLen, inQty, outQty, inVar, outVar, isHighBandwidth);
		if (!explicitPeersStr.empty ()) m_Pool->SetExplicitPeers (i2p::data::ExtractIdentHashes (explicitPeersStr));
		if (!trustedRoutersStr.empty ()) m_Pool->SetTrustedRouters (i2p::data::ExtractIdentHashes (trustedRoutersStr));
		if (!inboundRandomKeyStr.empty ())
		{
			uint8_t key[32]; // might be 32 bytes, but only first 16 bytes are used
			if (i2p::data::Base64ToByteStream (inboundRandomKeyStr, key, 32) >= 16)
				m_Pool->SetInboundPeerOrderingKey (key);
		}
		if (!outboundRandomKeyStr.empty ())
		{
			uint8_t key[32]; // might be 32 bytes, but only first 16 bytes are used
			if (i2p::data::Base64ToByteStream (outboundRandomKeyStr, key, 32) >= 16)
				m_Pool->SetOutboundPeerOrderingKey (key);
		}
		if (params)
		{
			int maxLatency = 0;
			if (params->Get (I2CP_PARAM_MAX_TUNNEL_LATENCY, maxLatency))
			{
				int minLatency = 0;
				if (params->Get (I2CP_PARAM_MIN_TUNNEL_LATENCY, minLatency))
				{
					if (minLatency > 0 && maxLatency > 0)
					{
						// set tunnel pool latency
						LogPrint (
							eLogInfo, "Destination: Requiring tunnel latency [", minLatency, "ms, ", maxLatency, "ms]");
						m_Pool->RequireLatency (minLatency, maxLatency);
					}
				}
			}
		}
	}

	LeaseSetDestination::~LeaseSetDestination ()
	{
		if (m_Pool) i2p::tunnel::tunnels.DeleteTunnelPool (m_Pool);
		for (auto & it : m_LeaseSetRequests) it.second->Complete (nullptr);
	}

	void LeaseSetDestination::Start ()
	{
		if (m_Nickname.empty ())
			m_Nickname = i2p::data::GetIdentHashAbbreviation (GetIdentHash ()); // set default nickname
		LoadTags ();
		m_Pool->SetLocalDestination (shared_from_this ());
		m_Pool->SetActive (true);
		m_CleanupTimer.expires_after (std::chrono::seconds (DESTINATION_CLEANUP_TIMEOUT));
		m_CleanupTimer.async_wait (
			std::bind (&LeaseSetDestination::HandleCleanupTimer, shared_from_this (), std::placeholders::_1));
	}

	void LeaseSetDestination::Stop ()
	{
		m_CleanupTimer.cancel ();
		m_PublishConfirmationTimer.cancel ();
		m_PublishVerificationTimer.cancel ();
		m_PublishDelayTimer.cancel ();
		if (m_Pool)
		{
			m_Pool->SetLocalDestination (nullptr);
			i2p::tunnel::tunnels.StopTunnelPool (m_Pool);
		}
		SaveTags ();
		CleanUp (); // GarlicDestination
	}

	bool LeaseSetDestination::Reconfigure (const i2p::util::Mapping & params)
	{
		bool dontPublishLeaseSet = !m_IsPublic;
		params.Get (I2CP_PARAM_DONT_PUBLISH_LEASESET, dontPublishLeaseSet);
		m_IsPublic = !dontPublishLeaseSet;
		auto numTags = GetNumTags ();
		params.Get (I2CP_PARAM_TAGS_TO_SEND, numTags);
		auto numRatchetInboundTags = GetNumRatchetInboundTags ();
		params.Get (I2CP_PARAM_RATCHET_INBOUND_TAGS, numRatchetInboundTags);
		auto pool = GetTunnelPool ();
		auto inLen = pool->GetNumInboundHops ();
		params.Get (I2CP_PARAM_INBOUND_TUNNEL_LENGTH, inLen);
		auto outLen = pool->GetNumOutboundHops ();
		params.Get (I2CP_PARAM_OUTBOUND_TUNNEL_LENGTH, outLen);
		auto inQuant = pool->GetNumInboundTunnels ();
		params.Get (I2CP_PARAM_INBOUND_TUNNELS_QUANTITY, inQuant);
		auto outQuant = pool->GetNumOutboundTunnels ();
		params.Get (I2CP_PARAM_OUTBOUND_TUNNELS_QUANTITY, outQuant);
		int minLatency = 0;
		params.Get (I2CP_PARAM_MIN_TUNNEL_LATENCY, minLatency);
		int maxLatency = 0;
		params.Get (I2CP_PARAM_MAX_TUNNEL_LATENCY, maxLatency);

		SetNumTags (numTags);
		SetNumRatchetInboundTags (numRatchetInboundTags);
		pool->RequireLatency (minLatency, maxLatency);
		return pool->Reconfigure (inLen, outLen, inQuant, outQuant);
	}

	std::shared_ptr<i2p::data::LeaseSet> LeaseSetDestination::FindLeaseSet (const i2p::data::IdentHash & ident)
	{
		// Callable from any thread. The lookup and the expired-entry removal take
		// only m_RemoteLeaseSetsMutex; the "expires soon" refresh is posted to the
		// destination service and re-locks the map inside its own callback, so the
		// caller keeps using the snapshot it already received rather than a
		// half-updated entry.
		std::shared_ptr<i2p::data::LeaseSet> remoteLS;
		{
			std::lock_guard<std::mutex> lock (m_RemoteLeaseSetsMutex);
			auto it = m_RemoteLeaseSets.find (ident);
			if (it != m_RemoteLeaseSets.end ()) remoteLS = it->second;
		}

		if (remoteLS)
		{
			if (!remoteLS->IsExpired ())
			{
				if (remoteLS->ExpiresSoon ())
				{
					LogPrint (eLogDebug, "Destination: Lease Set expires soon, updating before expire");
					// update now before expiration for smooth handover
					auto s = shared_from_this ();
					RequestDestination (ident,
						[s, ident] (std::shared_ptr<i2p::data::LeaseSet> ls)
						{
							if (ls && !ls->IsExpired ())
							{
								ls->PopulateLeases ();
								{
									std::lock_guard<std::mutex> _lock (s->m_RemoteLeaseSetsMutex);
									s->m_RemoteLeaseSets[ident] = ls;
								}
							}
						});
				}
				return remoteLS;
			}
			else
			{
				LogPrint (eLogWarning, "Destination: Remote LeaseSet expired");
				std::lock_guard<std::mutex> lock (m_RemoteLeaseSetsMutex);
				m_RemoteLeaseSets.erase (ident);
				return nullptr;
			}
		}
		return nullptr;
	}

	std::shared_ptr<const i2p::data::LocalLeaseSet> LeaseSetDestination::GetLeaseSet ()
	{
		if (!m_Pool) return nullptr;
		if (!m_LeaseSet) UpdateLeaseSet ();
		auto ls = GetLeaseSetMt ();
		return (ls && ls->GetInnerLeaseSet ()) ? ls->GetInnerLeaseSet () : ls; // always non-encrypted
	}

	std::shared_ptr<const i2p::data::LocalLeaseSet> LeaseSetDestination::GetLeaseSetMt ()
	{
		std::lock_guard<std::mutex> l (m_LeaseSetMutex);
		return m_LeaseSet;
	}

	void LeaseSetDestination::SetLeaseSet (std::shared_ptr<const i2p::data::LocalLeaseSet> newLeaseSet)
	{
		{
			std::lock_guard<std::mutex> l (m_LeaseSetMutex);
			m_LeaseSet = newLeaseSet;
		}
		i2p::garlic::GarlicDestination::SetLeaseSetUpdated ();
		if (m_IsPublic)
		{
			auto s = shared_from_this ();
			boost::asio::post (m_Service,
				[s] (void)
				{
					s->m_PublishVerificationTimer.cancel ();
					s->Publish ();
				});
		}
	}

	void LeaseSetDestination::UpdateLeaseSet ()
	{
		int numTunnels = m_Pool->GetNumInboundTunnels () + 2; // 2 backup tunnels
		if (numTunnels > i2p::data::MAX_NUM_LEASES) numTunnels = i2p::data::MAX_NUM_LEASES; // 16 tunnels maximum
		auto tunnels = m_Pool->GetInboundTunnels (numTunnels);
		if (!tunnels.empty ())
			CreateNewLeaseSet (tunnels);
		else
			LogPrint (eLogInfo, "Destination: No inbound tunnels for LeaseSet");
	}

	bool LeaseSetDestination::SubmitSessionKey (const uint8_t * key, const uint8_t * tag)
	{
		struct
		{
			uint8_t k[32], t[32];
		} data;
		memcpy (data.k, key, 32);
		memcpy (data.t, tag, 32);
		auto s = shared_from_this ();
		boost::asio::post (m_Service, [s, data] (void) { s->AddSessionKey (data.k, data.t); });
		return true;
	}

	void LeaseSetDestination::SubmitECIESx25519Key (const uint8_t * key, uint64_t tag)
	{
		struct
		{
			uint8_t k[32];
			uint64_t t;
		} data;
		memcpy (data.k, key, 32);
		data.t = tag;
		auto s = shared_from_this ();
		boost::asio::post (m_Service, [s, data] (void) { s->AddECIESx25519Key (data.k, data.t); });
	}

	void LeaseSetDestination::ProcessGarlicMessage (std::shared_ptr<I2NPMessage> msg)
	{
		if (!msg) return;
		static I2NPDropCounter queueDropLogs;
		bool post = false;
		uint64_t dropped = 0;
		{
			std::lock_guard<std::mutex> l (m_IncomingMsgsQueueMutex);
			// The queue is fed by network input before the destination service
			// runs, so it is bounded: drop the oldest message and count it.
			if (IsIncomingGarlicQueueFull (m_IncomingMsgsQueue.size ()) && !m_IncomingMsgsQueue.empty ())
			{
				m_IncomingMsgsQueue.pop_front ();
				dropped = m_NumDroppedIncomingMsgs.fetch_add (1, std::memory_order_relaxed) + 1;
			}
			post = m_IncomingMsgsQueue.empty ();
			m_IncomingMsgsQueue.push_back (msg);
		}
		if (dropped && queueDropLogs.Record ())
			LogPrint (eLogWarning, "Destination: Incoming garlic queue is full, dropped the oldest message (", dropped,
				" dropped)");
		if (post)
			boost::asio::post (m_Service,
				[s = shared_from_this ()] ()
				{
					// Drain until the queue stays empty. A producer that pushed
					// while a previous swap was in progress saw a non-empty
					// queue and therefore scheduled no handler of its own, so a
					// single swap would leave its message queued indefinitely.
					for (;;)
					{
						std::list<std::shared_ptr<I2NPMessage>> receivedMsgs;
						{
							std::lock_guard<std::mutex> l (s->m_IncomingMsgsQueueMutex);
							s->m_IncomingMsgsQueue.swap (receivedMsgs);
						}
						if (receivedMsgs.empty ()) return;
						for (auto & it : receivedMsgs) s->HandleGarlicMessage (it);
						{
							std::lock_guard<std::mutex> l (s->m_IncomingMsgsQueueMutex);
							if (s->m_IncomingMsgsQueue.empty ()) return;
						}
					}
				});
	}

	void LeaseSetDestination::ProcessDeliveryStatusMessage (std::shared_ptr<I2NPMessage> msg)
	{
		if (!msg || msg->GetLength () < I2NP_HEADER_SIZE || !IsValidDeliveryStatusPayload (msg->GetPayloadLength ()))
		{
			if (g_CloveParseDrops.Record ())
				LogPrint (eLogWarning, "Destination: DeliveryStatus message is not ", DELIVERY_STATUS_SIZE, " bytes");
			return;
		}
		uint32_t msgID = bufbe32toh (msg->GetPayload () + DELIVERY_STATUS_MSGID_OFFSET);
		boost::asio::post (
			m_Service, std::bind (&LeaseSetDestination::HandleDeliveryStatusMessage, shared_from_this (), msgID));
	}

	void LeaseSetDestination::HandleI2NPMessage (const uint8_t * buf, size_t len)
	{
		if (len < I2NP_HEADER_SIZE)
		{
			if (g_CloveParseDrops.Record ()) LogPrint (eLogWarning, "Destination: I2NP message is too short ", len);
			return;
		}
		I2NPMessageType typeID = (I2NPMessageType)(buf[I2NP_HEADER_TYPEID_OFFSET]);
		uint32_t msgID = bufbe32toh (buf + I2NP_HEADER_MSGID_OFFSET);
		const size_t messageLength = GetI2NPMessageLength (buf, len);
		if (messageLength < I2NP_HEADER_SIZE || messageLength > len)
		{
			if (g_CloveParseDrops.Record ())
				LogPrint (eLogError, "Destination: Invalid I2NP clove length ", messageLength);
			return;
		}
		LeaseSetDestination::HandleCloveI2NPMessage (
			typeID, buf + I2NP_HEADER_SIZE, messageLength - I2NP_HEADER_SIZE, msgID, nullptr);
	}

	bool LeaseSetDestination::HandleCloveI2NPMessage (I2NPMessageType typeID, const uint8_t * payload, size_t len,
		uint32_t msgID, i2p::garlic::ECIESX25519AEADRatchetSession * from)
	{
		switch (typeID)
		{
		case eI2NPData: HandleDataMessage (payload, len, from); break;
		case eI2NPDeliveryStatus:
			// Java DeliveryStatusMessage is exactly msgID (4) + timestamp (8).
			if (len == DELIVERY_STATUS_SIZE)
				HandleDeliveryStatusMessage (bufbe32toh (payload + DELIVERY_STATUS_MSGID_OFFSET));
			else if (g_CloveParseDrops.Record ())
				LogPrint (eLogWarning, "Destination: DeliveryStatus clove is ", len, " bytes instead of ",
					DELIVERY_STATUS_SIZE);
			break;
		case eI2NPTunnelTest:
			if (m_Pool && len >= TUNNEL_TEST_SIZE)
				m_Pool->ProcessTunnelTest (bufbe32toh (payload + TUNNEL_TEST_MSGID_OFFSET),
					bufbe64toh (payload + TUNNEL_TEST_TIMESTAMP_OFFSET));
			break;
		case eI2NPDatabaseStore: HandleDatabaseStoreMessage (payload, len, from); break;
		case eI2NPDatabaseSearchReply: HandleDatabaseSearchReplyMessage (payload, len); break;
		case eI2NPShortTunnelBuildReply: // might come as garlic encrypted
		{
			auto msg = CreateI2NPMessage (typeID, payload, len, msgID);
			if (!msg)
			{
				if (g_CloveParseDrops.Record ())
					LogPrint (eLogError, "Destination: Can't allocate clove message of ", len, " bytes");
				return false;
			}
			msg->fromClove = true;
			i2p::HandleI2NPMessage (msg);
			break;
		}
		default:
			if (g_CloveParseDrops.Record ())
				LogPrint (eLogWarning, "Destination: Unexpected I2NP message type ", typeID);
			return false;
		}
		return true;
	}

	void LeaseSetDestination::HandleDatabaseStoreMessage (
		const uint8_t * buf, size_t len, i2p::garlic::ECIESX25519AEADRatchetSession * from)
	{
		// Reached only from the destination service (garlic/data dispatch), so
		// m_LeaseSetRequests is deliberately unguarded here and in
		// RequestLeaseSet/CancelDestinationRequest/HandleRequestTimeoutTimer: all
		// of them run on that one executor. Only m_RemoteLeaseSets is shared,
		// because FindLeaseSet() is called from streaming, datagram and torrent
		// threads. A candidate that fails validation keeps the previous snapshot
		// installed, so readers are never left without a usable LeaseSet.
		if (len < DATABASE_STORE_HEADER_SIZE)
		{
			LogPrint (eLogError, "Destination: Database store msg is too short ", len);
			return;
		}
		uint32_t replyToken = bufbe32toh (buf + DATABASE_STORE_REPLY_TOKEN_OFFSET);
		size_t offset = DATABASE_STORE_HEADER_SIZE;
		if (replyToken)
		{
			LogPrint (eLogInfo, "Destination: Reply token is ignored for DatabaseStore");
			offset += 36;
		}
		if (offset > len || len > i2p::data::MAX_LS_BUFFER_SIZE + offset)
		{
			LogPrint (eLogError, "Destination: Database store message is too long ", len);
			return;
		}
		i2p::data::IdentHash key (buf + DATABASE_STORE_KEY_OFFSET);
		std::shared_ptr<i2p::data::LeaseSet> leaseSet;
		std::shared_ptr<LeaseSetRequest> request;
		// DatabaseStoreMessage masks the upper four type bits. Future option
		// bits must not make an otherwise valid store disappear at the client
		// layer; the router-level handler already masks them.
		const uint8_t storeType = buf[DATABASE_STORE_TYPE_OFFSET] & 0x0f;
		switch (storeType)
		{
		case i2p::data::NETDB_STORE_TYPE_LEASESET: // 1
		case i2p::data::NETDB_STORE_TYPE_STANDARD_LEASESET2: // 3
		{
			LogPrint (eLogDebug, "Destination: Remote LeaseSet");
			// Build a candidate instead of mutating a LeaseSet that may be
			// referenced by streaming/datagram readers. Readers can keep using
			// the old immutable object until the map replacement is published.
			auto makeLeaseSet = [&] () -> std::shared_ptr<i2p::data::LeaseSet>
			{
				if (storeType == i2p::data::NETDB_STORE_TYPE_LEASESET)
				{
					auto ls = std::make_shared<i2p::data::LeaseSet> (buf + offset, len - offset);
					if (!SupportsEncryptionType (i2p::data::CRYPTO_KEY_TYPE_ELGAMAL) || from)
						ls->SetIsIncompatibleCrypto (true);
					return ls;
				}

				auto ls = std::make_shared<i2p::data::LeaseSet2> (storeType, buf + offset, len - offset, true,
					shared_from_this (), from ? from->GetRemoteStaticKeyType () : GetPreferredCryptoType ());
				if (from && !ls->IsEncryptionKey (from->GetRemoteStaticKeyType (), from->GetRemoteStaticKey ()))
				{
					LogPrint (eLogError, "Destination: Remote LeaseSet static key mismatch");
					return nullptr;
				}
				return ls;
			};

			std::lock_guard<std::mutex> lock (m_RemoteLeaseSetsMutex);
			auto it = m_RemoteLeaseSets.find (key);
			if (it != m_RemoteLeaseSets.end () && it->second->GetStoreType () == storeType) // update only if same type
			{
				if (it->second->IsNewer (buf + offset, len - offset))
				{
					auto candidate = makeLeaseSet ();
					if (candidate && candidate->IsValid () && candidate->GetIdentHash () == key
						&& !candidate->IsExpired () && candidate->GetIdentHash () != GetIdentHash ())
					{
						m_RemoteLeaseSets.insert_or_assign (key, candidate);
						leaseSet = candidate;
						if (from) from->SetDestination (key);
						LogPrint (eLogDebug, "Destination: Remote LeaseSet updated");
					}
					else
					{
						// Keep the previous, still valid snapshot rather than
						// invalidating it for every reader after a bad update.
						LogPrint (eLogDebug, "Destination: Remote LeaseSet update failed");
						leaseSet = nullptr;
					}
				}
				else
					LogPrint (eLogDebug, "Destination: Remote LeaseSet is older. Not updated");
			}
			else
			{
				leaseSet = makeLeaseSet ();
				if (leaseSet && leaseSet->IsValid () && leaseSet->GetIdentHash () == key && !leaseSet->IsExpired ())
				{
					if (leaseSet->GetIdentHash () != GetIdentHash ())
					{
						LogPrint (eLogDebug, "Destination: New remote LeaseSet added");
						m_RemoteLeaseSets.insert_or_assign (key, leaseSet);
						if (from) from->SetDestination (key);
					}
					else
						LogPrint (eLogDebug, "Destination: Own remote LeaseSet dropped");
				}
				else
				{
					LogPrint (eLogError, "Destination: New remote LeaseSet failed");
					leaseSet = nullptr;
				}
			}
			break;
		}
		case i2p::data::NETDB_STORE_TYPE_ENCRYPTED_LEASESET2: // 5
		{
			auto it2 = m_LeaseSetRequests.find (key);
			if (it2 != m_LeaseSetRequests.end ())
			{
				request = it2->second;
				m_LeaseSetRequests.erase (it2);
				if (request->requestedBlindedKey)
				{
					// Prefer the per-destination client-auth material from the
					// I2CP BlindingInfo; fall back to the session-wide
					// i2cp.leaseSetPrivKey when the request carried none.
					const uint8_t * authSecret = nullptr;
					if (request->authSecret.size () == 32)
						authSecret = request->authSecret.data ();
					else if (m_LeaseSetPrivKey)
						authSecret = (const uint8_t *)*m_LeaseSetPrivKey;
					auto ls2 = std::make_shared<i2p::data::LeaseSet2> (buf + offset, len - offset,
						request->requestedBlindedKey, shared_from_this (), authSecret, GetPreferredCryptoType ());
					if (ls2->IsValid () && !ls2->IsExpired ())
					{
						// Types 1/3 above reject a reply that is not newer than
						// the cached LeaseSet. Apply the same replay rule here:
						// the outer published timestamp is signed and preserved
						// by both the outer-only and the fully decrypted parse,
						// so an authentic but older encrypted LeaseSet2 replayed
						// by a malicious floodfill must not roll the cache back.
						std::lock_guard<std::mutex> lock (m_RemoteLeaseSetsMutex);
						auto cached = m_RemoteLeaseSets.find (key);
						if (cached != m_RemoteLeaseSets.end ()
							&& cached->second->GetPublishedTimestamp () >= ls2->GetPublishedTimestamp ())
						{
							// Equal is a duplicate and older is a replay: keep the
							// cached snapshot. Answer the lookup with it while it
							// is still usable instead of failing a request this
							// node can serve.
							LogPrint (eLogDebug, "Destination: Remote encrypted LeaseSet2 is not newer. Not updated");
							if (cached->second->IsValid () && !cached->second->IsExpired ()) leaseSet = cached->second;
						}
						else
						{
							leaseSet = ls2;
							// The ident alias must not overwrite a newer entry
							// either: a decrypted LeaseSet2 carries the real
							// destination hash, which can already hold a newer
							// standard LS1/LS2 for the same destination.
							auto identKey = ls2->GetIdentHash ();
							auto alias = m_RemoteLeaseSets.find (identKey);
							if (alias == m_RemoteLeaseSets.end ()
								|| alias->second->GetPublishedTimestamp () < ls2->GetPublishedTimestamp ())
								m_RemoteLeaseSets[identKey] = ls2; // ident is not key
							m_RemoteLeaseSets[key] = ls2; // also store as key for next lookup
						}
					}
					else
						LogPrint (eLogError, "Destination: New remote encrypted LeaseSet2 failed");
				}
				else
				{
					// publishing verification doesn't have requestedBlindedKey
					auto localLeaseSet = GetLeaseSetMt ();
					if (localLeaseSet->GetStoreHash () == key)
					{
						auto ls
							= std::make_shared<i2p::data::LeaseSet2> (i2p::data::NETDB_STORE_TYPE_ENCRYPTED_LEASESET2,
								localLeaseSet->GetBuffer (), localLeaseSet->GetBufferLen (), false);
						leaseSet = ls;
					}
					else
						LogPrint (
							eLogWarning, "Destination: Encrypted LeaseSet2 received for request without blinded key");
				}
			}
			else
				LogPrint (eLogWarning, "Destination: Couldn't find request for encrypted LeaseSet2");
			break;
		}
		default:
			// Store type 7 (Meta LS2) is a known parity gap, not a malformed
			// message: a floodfill stores and serves it, but this client switch
			// resolves only 1/3/5 and does not follow MetaLease entries to the
			// selected sub-destinations yet. Any other type is unexpected.
			// Tracked in FEATURE-PARITY/TODO; do not turn this into a silent
			// accept path.
			LogPrint (eLogError, "Destination: Unexpected client's DatabaseStore type ", storeType, ", dropped");
		}

		if (!request)
		{
			auto it1 = m_LeaseSetRequests.find (key);
			if (it1 != m_LeaseSetRequests.end ())
			{
				request = it1->second;
				m_LeaseSetRequests.erase (it1);
			}
		}
		if (request)
		{
			request->requestTimeoutTimer.cancel ();
			request->Complete (leaseSet);
		}
	}

	void LeaseSetDestination::HandleDatabaseSearchReplyMessage (const uint8_t * buf, size_t len)
	{
		i2p::DatabaseSearchReply reply;
		if (!i2p::ParseDatabaseSearchReply (buf, len, reply))
		{
			LogPrint (eLogWarning, "Destination: Malformed database search reply, ", len, " bytes");
			return;
		}
		if (reply.from.IsZero ())
		{
			LogPrint (eLogWarning, "Destination: Database search reply has zero sender hash");
			return;
		}
		LogPrint (
			eLogDebug, "Destination: DatabaseSearchReply for ", reply.key.ToBase64 (), " num=", reply.peers.size ());
		auto it = m_LeaseSetRequests.find (reply.key);
		if (it != m_LeaseSetRequests.end ())
		{
			auto request = it->second;
			// A DSRM is matched to the queried peer, not merely to the public
			// destination hash. Accept one reply per peer to bound replays.
			if (!request->requestedPeers.count (reply.from))
			{
				LogPrint (eLogWarning, "Destination: DatabaseSearchReply from a peer that was not queried");
				return;
			}
			if (!request->repliedPeers.insert (reply.from).second)
			{
				LogPrint (eLogDebug, "Destination: Duplicate DatabaseSearchReply from ", reply.from.ToBase64 ());
				return;
			}
			size_t followed = 0;
			for (const auto & peerHash : reply.peers)
			{
				if (followed >= i2p::data::NETDB_MAX_NUM_SEARCH_REPLY_PEER_HASHES) break;
				if (peerHash.IsZero () || peerHash == i2p::context.GetIdentHash () || peerHash == reply.from
					|| request->excluded.count (peerHash) || request->requestedPeers.count (peerHash)
					|| i2p::data::netdb.FindRouter (peerHash))
					continue;
				LogPrint (eLogInfo, "Destination: Found new floodfill, request it");
				i2p::data::netdb.RequestDestination (peerHash, nullptr, false); // through exploratory
				++followed;
			}
			SendNextLeaseSetRequest (reply.key, request);
		}
		else
			LogPrint (eLogWarning, "Destination: Request for ", reply.key.ToBase64 (), " not found");
	}

	void LeaseSetDestination::SendNextLeaseSetRequest (
		const i2p::data::IdentHash & key, std::shared_ptr<LeaseSetRequest> request)
	{
		bool found = false;
		if (request->excluded.size () < MAX_NUM_FLOODFILLS_PER_REQUEST)
		{
			auto floodfill = i2p::data::netdb.GetClosestFloodfill (key, request->excluded);
			if (floodfill)
			{
				LogPrint (eLogInfo, "Destination: Requesting ", key.ToBase64 (), " at ",
					floodfill->GetIdentHash ().ToBase64 ());
				if (SendLeaseSetRequest (key, floodfill, request)) found = true;
			}
		}
		if (!found)
		{
			LogPrint (eLogInfo, "Destination: ", key.ToBase64 (), " was not found on ", MAX_NUM_FLOODFILLS_PER_REQUEST,
				" floodfills");
			request->Complete (nullptr);
			m_LeaseSetRequests.erase (key);
		}
	}

	void LeaseSetDestination::HandleDeliveryStatusMessage (uint32_t msgID)
	{
		if (msgID == m_PublishReplyToken)
		{
			LogPrint (eLogDebug, "Destination: Publishing LeaseSet confirmed for ", GetIdentHash ().ToBase32 ());
			m_ExcludedFloodfills.clear ();
			m_PublishReplyToken = 0;
			// schedule verification
			m_PublishVerificationTimer.expires_after (std::chrono::seconds (
				PUBLISH_VERIFICATION_TIMEOUT + GetRng () () % PUBLISH_VERIFICATION_TIMEOUT_VARIANCE));
			m_PublishVerificationTimer.async_wait (std::bind (
				&LeaseSetDestination::HandlePublishVerificationTimer, shared_from_this (), std::placeholders::_1));
		}
		else
			i2p::garlic::GarlicDestination::HandleDeliveryStatusMessage (msgID);
	}

	void LeaseSetDestination::SetLeaseSetUpdated (bool post)
	{
		if (post)
			boost::asio::post (m_Service, [s = shared_from_this ()] () { s->UpdateLeaseSet (); });
		else
			UpdateLeaseSet ();
	}

	void LeaseSetDestination::Publish ()
	{
		// Single-flight publication on the destination service: m_PublishReplyToken
		// stays non-zero for the whole attempt (including the retry value installed
		// by SchedulePublishRetry()), so a concurrent Publish() only logs. Each
		// attempt excludes the floodfills it already tried and needs a tunnel pair
		// whose far end is compatible with the chosen floodfill. The confirmation
		// timeout is scaled by the real (or hop-estimated) latency of both tunnels,
		// because a fixed timeout would retry a slow but healthy path.
		auto leaseSet = GetLeaseSetMt ();
		if (!leaseSet || !m_Pool)
		{
			LogPrint (eLogError, "Destination: Can't publish non-existing LeaseSet");
			return;
		}
		if (m_PublishReplyToken)
		{
			LogPrint (eLogDebug, "Destination: Publishing LeaseSet is pending");
			return;
		}
		auto ts = i2p::util::GetSecondsSinceEpoch ();
		if (ts < m_LastSubmissionTime + PUBLISH_MIN_INTERVAL)
		{
			LogPrint (
				eLogDebug, "Destination: Publishing LeaseSet is too fast. Wait for ", PUBLISH_MIN_INTERVAL, " seconds");
			m_PublishDelayTimer.cancel ();
			m_PublishDelayTimer.expires_after (std::chrono::seconds (PUBLISH_MIN_INTERVAL));
			m_PublishDelayTimer.async_wait (
				std::bind (&LeaseSetDestination::HandlePublishDelayTimer, shared_from_this (), std::placeholders::_1));
			return;
		}
		auto floodfill = i2p::data::netdb.GetClosestFloodfill (leaseSet->GetStoreHash (), m_ExcludedFloodfills);
		if (!floodfill)
		{
			LogPrint (eLogError, "Destination: Can't publish LeaseSet, no more floodfills found");
			m_ExcludedFloodfills.clear ();
			// try again later
			SchedulePublishRetry ();
			return;
		}
		auto outbound = m_Pool->GetNextOutboundTunnel (nullptr, floodfill->GetCompatibleTransports (false));
		auto inbound = m_Pool->GetNextInboundTunnel (nullptr, floodfill->GetCompatibleTransports (true));
		if (!outbound || !inbound)
		{
			if (m_Pool->HasInboundTunnels () && m_Pool->HasOutboundTunnels ())
			{
				LogPrint (eLogInfo, "Destination: No compatible tunnels with ", floodfill->GetIdentHash ().ToBase64 (),
					". Trying another floodfill");
				m_ExcludedFloodfills.insert (floodfill->GetIdentHash ());
				floodfill = i2p::data::netdb.GetClosestFloodfill (leaseSet->GetStoreHash (), m_ExcludedFloodfills);
				if (floodfill)
				{
					outbound = m_Pool->GetNextOutboundTunnel (nullptr, floodfill->GetCompatibleTransports (false));
					if (outbound)
					{
						inbound = m_Pool->GetNextInboundTunnel (nullptr, floodfill->GetCompatibleTransports (true));
						if (!inbound) LogPrint (eLogError, "Destination: Can't publish LeaseSet. No inbound tunnels");
					}
					else
						LogPrint (eLogError, "Destination: Can't publish LeaseSet. No outbound tunnels");
				}
				else
					LogPrint (eLogError, "Destination: Can't publish LeaseSet, no more floodfills found");
			}
			else
				LogPrint (eLogDebug, "Destination: No tunnels in pool");

			if (!floodfill || !outbound || !inbound)
			{
				// we can't publish now
				// try again layer
				SchedulePublishRetry ();
				return;
			}
		}
		m_ExcludedFloodfills.insert (floodfill->GetIdentHash ());
		LogPrint (eLogDebug, "Destination: Publish LeaseSet of ", GetIdentHash ().ToBase32 ());
		RAND_bytes ((uint8_t *)&m_PublishReplyToken, 4);
		auto msg
			= WrapMessageForRouter (floodfill, i2p::CreateDatabaseStoreMsg (leaseSet, m_PublishReplyToken, inbound));
		auto s = shared_from_this ();
		msg->onDrop = [s] ()
		{
			boost::asio::post (s->GetService (),
				[s] ()
				{
					s->m_PublishConfirmationTimer.cancel ();
					s->HandlePublishConfirmationTimer (boost::system::error_code (), 0);
				});
		};
		uint64_t publishConfirmationTimeout = PUBLISH_CONFIRMATION_TIMEOUT;
		publishConfirmationTimeout += inbound->LatencyIsKnown ()
			? (uint64_t)(inbound->GetMeanLatency ()) / 1000LL
			: inbound->GetNumHops () * (uint64_t)(i2p::tunnel::HIGH_LATENCY_PER_HOP) / 1000LL;
		publishConfirmationTimeout += outbound->LatencyIsKnown ()
			? (uint64_t)(outbound->GetMeanLatency ()) / 1000LL
			: outbound->GetNumHops () * (uint64_t)(i2p::tunnel::HIGH_LATENCY_PER_HOP) / 1000LL;
		m_PublishConfirmationTimer.expires_after (std::chrono::milliseconds (publishConfirmationTimeout));
		m_PublishConfirmationTimer.async_wait (std::bind (&LeaseSetDestination::HandlePublishConfirmationTimer,
			shared_from_this (), std::placeholders::_1, publishConfirmationTimeout));
		outbound->SendTunnelDataMsgTo (floodfill->GetIdentHash (), 0, msg);
		m_LastSubmissionTime = ts;
	}

	void LeaseSetDestination::SchedulePublishRetry ()
	{
		m_ExcludedFloodfills.clear ();
		m_PublishReplyToken = 1; // dummy non-zero value
		LogPrint (eLogInfo,
			"Destination: Can't publish LeaseSet because destination is not ready. Try publishing again after ",
			PUBLISH_RETRY_INTERVAL, " milliseconds");
		m_PublishConfirmationTimer.expires_after (std::chrono::milliseconds (PUBLISH_RETRY_INTERVAL));
		m_PublishConfirmationTimer.async_wait (std::bind (&LeaseSetDestination::HandlePublishConfirmationTimer,
			shared_from_this (), std::placeholders::_1, PUBLISH_RETRY_INTERVAL));
	}

	void LeaseSetDestination::HandlePublishConfirmationTimer (
		const boost::system::error_code & ecode, uint64_t publishConfirmationTimeout)
	{
		if (ecode != boost::asio::error::operation_aborted)
		{
			if (m_PublishReplyToken)
			{
				LogPrint (eLogWarning, "Destination: Publish confirmation was not received in ",
					publishConfirmationTimeout, " milliseconds or failed. will try again");
				m_PublishReplyToken = 0;
				Publish ();
			}
		}
	}

	void LeaseSetDestination::HandlePublishVerificationTimer (const boost::system::error_code & ecode)
	{
		if (ecode != boost::asio::error::operation_aborted)
		{
			auto ls = GetLeaseSetMt ();
			if (!ls)
			{
				LogPrint (eLogWarning, "Destination: Couldn't verify LeaseSet for ", GetIdentHash ().ToBase32 ());
				return;
			}
			auto s = shared_from_this ();
			RequestLeaseSet (ls->GetStoreHash (),
				[s, ls] (std::shared_ptr<const i2p::data::LeaseSet> leaseSet)
				{
					if (leaseSet)
					{
						if (*ls == *leaseSet)
						{
							// we got latest LeaseSet
							LogPrint (eLogDebug, "Destination: Published LeaseSet verified for ",
								s->GetIdentHash ().ToBase32 ());
							s->m_PublishVerificationTimer.expires_after (
								std::chrono::seconds (PUBLISH_REGULAR_VERIFICATION_INTERNAL));
							s->m_PublishVerificationTimer.async_wait (std::bind (
								&LeaseSetDestination::HandlePublishVerificationTimer, s, std::placeholders::_1));
							return;
						}
						else
							LogPrint (eLogDebug, "Destination: LeaseSet is different than just published for ",
								s->GetIdentHash ().ToBase32 ());
					}
					else
						LogPrint (eLogWarning, "Destination: Couldn't find published LeaseSet for ",
							s->GetIdentHash ().ToBase32 ());
					// we have to publish again
					s->Publish ();
				});
		}
	}

	void LeaseSetDestination::HandlePublishDelayTimer (const boost::system::error_code & ecode)
	{
		if (ecode != boost::asio::error::operation_aborted) Publish ();
	}

	bool LeaseSetDestination::RequestDestination (const i2p::data::IdentHash & dest, RequestComplete requestComplete)
	{
		if (!m_Pool || !IsReady ())
		{
			if (requestComplete) boost::asio::post (m_Service, [requestComplete] (void) { requestComplete (nullptr); });
			return false;
		}
		boost::asio::post (m_Service,
			std::bind (&LeaseSetDestination::RequestLeaseSet, shared_from_this (), dest, requestComplete, nullptr,
				std::vector<uint8_t> { }));
		return true;
	}

	bool LeaseSetDestination::RequestDestinationWithEncryptedLeaseSet (
		std::shared_ptr<const i2p::data::BlindedPublicKey> dest, RequestComplete requestComplete,
		const uint8_t * secret, size_t secretLen)
	{
		if (!dest || !m_Pool || !IsReady ())
		{
			if (requestComplete) boost::asio::post (m_Service, [requestComplete] (void) { requestComplete (nullptr); });
			return false;
		}
		// The client-auth material is a 32-byte DH private key or PSK. Any other
		// length is not usable and is ignored rather than truncated: a wrong
		// cookie would silently turn a decryption failure into a false negative.
		std::vector<uint8_t> authSecret;
		if (secret && secretLen == 32) authSecret.assign (secret, secret + secretLen);
		auto storeHash = dest->GetStoreHash ();
		auto leaseSet = FindLeaseSet (storeHash);
		if (leaseSet)
		{
			if (requestComplete)
				boost::asio::post (m_Service, [requestComplete, leaseSet] (void) { requestComplete (leaseSet); });
			return true;
		}
		boost::asio::post (m_Service,
			std::bind (&LeaseSetDestination::RequestLeaseSet, shared_from_this (), storeHash, requestComplete, dest,
				std::move (authSecret)));
		return true;
	}

	void LeaseSetDestination::CancelDestinationRequest (const i2p::data::IdentHash & dest, bool notify)
	{
		auto s = shared_from_this ();
		boost::asio::post (m_Service,
			[dest, notify, s] (void)
			{
				auto it = s->m_LeaseSetRequests.find (dest);
				if (it != s->m_LeaseSetRequests.end ())
				{
					auto requestComplete = it->second;
					s->m_LeaseSetRequests.erase (it);
					if (notify && requestComplete) requestComplete->Complete (nullptr);
				}
			});
	}

	void LeaseSetDestination::CancelDestinationRequestWithEncryptedLeaseSet (
		std::shared_ptr<const i2p::data::BlindedPublicKey> dest, bool notify)
	{
		if (dest) CancelDestinationRequest (dest->GetStoreHash (), notify);
	}

	void LeaseSetDestination::RequestLeaseSet (const i2p::data::IdentHash & dest, RequestComplete requestComplete,
		std::shared_ptr<const i2p::data::BlindedPublicKey> requestedBlindedKey, std::vector<uint8_t> authSecret)
	{
		std::unordered_set<i2p::data::IdentHash> excluded;
		auto floodfill = i2p::data::netdb.GetClosestFloodfill (dest, excluded);
		if (floodfill)
		{
			auto request = std::make_shared<LeaseSetRequest> (m_Service);
			request->requestedBlindedKey = requestedBlindedKey; // for encrypted LeaseSet2
			request->authSecret = std::move (authSecret);
			if (requestComplete) request->requestComplete.push_back (requestComplete);
			auto ts = i2p::util::GetMillisecondsSinceEpoch ();
			auto ret = m_LeaseSetRequests.insert (
				std::pair<i2p::data::IdentHash, std::shared_ptr<LeaseSetRequest>> (dest, request));
			if (ret.second) // inserted
			{
				request->requestTime = ts;
				if (!SendLeaseSetRequest (dest, floodfill, request))
				{
					// try another
					LogPrint (eLogWarning, "Destination: Couldn't send LeaseSet request to ",
						floodfill->GetIdentHash ().ToBase64 (), ". Trying another");
					request->excluded.insert (floodfill->GetIdentHash ());
					floodfill = i2p::data::netdb.GetClosestFloodfill (dest, request->excluded);
					if (!SendLeaseSetRequest (dest, floodfill, request))
					{
						// request failed
						LogPrint (eLogWarning, "Destination: LeaseSet request for ", dest.ToBase32 (), " was not sent");
						m_LeaseSetRequests.erase (ret.first);
						if (requestComplete) requestComplete (nullptr);
					}
				}
			}
			else // duplicate
			{
				LogPrint (eLogInfo, "Destination: Request of LeaseSet ", dest.ToBase64 (), " is pending already");
				if (ts > ret.first->second->requestTime + MAX_LEASESET_REQUEST_TIMEOUT)
				{
					// something went wrong
					m_LeaseSetRequests.erase (ret.first);
					if (requestComplete) requestComplete (nullptr);
				}
				else if (requestComplete)
					ret.first->second->requestComplete.push_back (requestComplete);
			}
		}
		else
		{
			LogPrint (eLogError, "Destination: Can't request LeaseSet, no floodfills found");
			if (requestComplete) requestComplete (nullptr);
		}
	}

	bool LeaseSetDestination::SendLeaseSetRequest (const i2p::data::IdentHash & dest,
		std::shared_ptr<const i2p::data::RouterInfo> nextFloodfill, std::shared_ptr<LeaseSetRequest> request)
	{
		// The reply path is a fresh inbound tunnel plus a throw-away ECIES
		// key/tag pair registered before the lookup leaves: the floodfill answers
		// through that tunnel to a destination-only garlic clove, so the key must
		// already exist or the reply is undecryptable. A dropped outbound message
		// (onDrop) retries with the next floodfill instead of failing the request,
		// and the timeout is re-armed from the real tunnel latency so a slow path
		// is not mistaken for a missing LeaseSet.
		if (!request->replyTunnel || !request->replyTunnel->IsEstablished ())
			request->replyTunnel = m_Pool->GetNextInboundTunnel (
				nullptr, nextFloodfill->GetCompatibleTransports (false)); // outbound from floodfill
		if (!request->replyTunnel)
			LogPrint (eLogWarning, "Destination: Can't send LeaseSet request, no compatible inbound tunnels found");
		if (!request->outboundTunnel || !request->outboundTunnel->IsEstablished ())
			request->outboundTunnel = m_Pool->GetNextOutboundTunnel (
				nullptr, nextFloodfill->GetCompatibleTransports (true)); // inbound from floodfill
		if (!request->outboundTunnel)
			LogPrint (eLogWarning, "Destination: Can't send LeaseSet request, no compatible outbound tunnels found");

		if (request->replyTunnel && request->outboundTunnel)
		{
			request->excluded.insert (nextFloodfill->GetIdentHash ());
			request->requestedPeers.insert (nextFloodfill->GetIdentHash ());
			request->requestTimeoutTimer.cancel ();

			uint8_t replyKey[32];
			uint64_t replyTag;
			RAND_bytes (replyKey, 32); // random session key
			RAND_bytes ((uint8_t *)&replyTag, 8); // random session tag
			AddECIESx25519Key (replyKey, replyTag);

			auto msg = WrapMessageForRouter (nextFloodfill,
				CreateLeaseSetDatabaseLookupMsg (dest, request->excluded, request->replyTunnel, replyKey, replyTag));
			auto s = shared_from_this ();
			msg->onDrop = [s, dest, request] ()
			{
				boost::asio::post (
					s->GetService (), [s, dest, request] () { s->SendNextLeaseSetRequest (dest, request); });
			};
			request->outboundTunnel->SendTunnelDataMsgs ({ i2p::tunnel::TunnelMessageBlock {
				i2p::tunnel::eDeliveryTypeRouter, nextFloodfill->GetIdentHash (), 0, msg } });

			uint64_t requestLeaseSetTimeout = LEASESET_REQUEST_TIMEOUT;
			requestLeaseSetTimeout += request->replyTunnel->LatencyIsKnown ()
				? (uint64_t)(request->replyTunnel->GetMeanLatency ()) / 1000LL
				: request->replyTunnel->GetNumHops () * (uint64_t)(i2p::tunnel::HIGH_LATENCY_PER_HOP) / 1000LL;
			requestLeaseSetTimeout += request->outboundTunnel->LatencyIsKnown ()
				? (uint64_t)(request->outboundTunnel->GetMeanLatency ()) / 1000LL
				: request->outboundTunnel->GetNumHops () * (uint64_t)(i2p::tunnel::HIGH_LATENCY_PER_HOP) / 1000LL;
			request->requestTimeoutTimer.expires_after (std::chrono::milliseconds (requestLeaseSetTimeout));
			request->requestTimeoutTimer.async_wait (std::bind (
				&LeaseSetDestination::HandleRequestTimeoutTimer, shared_from_this (), std::placeholders::_1, dest));
		}
		else
			return false;
		return true;
	}

	void LeaseSetDestination::HandleRequestTimeoutTimer (
		const boost::system::error_code & ecode, const i2p::data::IdentHash & dest)
	{
		if (ecode != boost::asio::error::operation_aborted)
		{
			auto it = m_LeaseSetRequests.find (dest);
			if (it != m_LeaseSetRequests.end ())
			{
				bool done = false;
				uint64_t ts = i2p::util::GetMillisecondsSinceEpoch ();
				if (ts < it->second->requestTime + MAX_LEASESET_REQUEST_TIMEOUT)
				{
					auto floodfill = i2p::data::netdb.GetClosestFloodfill (dest, it->second->excluded);
					if (floodfill)
					{
						// reset tunnels, because one them might fail
						it->second->outboundTunnel = nullptr;
						it->second->replyTunnel = nullptr;
						done = !SendLeaseSetRequest (dest, floodfill, it->second);
					}
					else
						done = true;
				}
				else
				{
					LogPrint (eLogWarning, "Destination: ", dest.ToBase64 (), " was not found within ",
						MAX_LEASESET_REQUEST_TIMEOUT, " seconds");
					done = true;
				}

				if (done)
				{
					auto requestComplete = it->second;
					m_LeaseSetRequests.erase (it);
					if (requestComplete) requestComplete->Complete (nullptr);
				}
			}
		}
	}

	void LeaseSetDestination::HandleCleanupTimer (const boost::system::error_code & ecode)
	{
		if (ecode != boost::asio::error::operation_aborted)
		{
			CleanupExpiredTags ();
			CleanupRemoteLeaseSets ();
			CleanupDestination ();
			m_CleanupTimer.expires_after (std::chrono::seconds (
				DESTINATION_CLEANUP_TIMEOUT + GetRng () () % DESTINATION_CLEANUP_TIMEOUT_VARIANCE));
			m_CleanupTimer.async_wait (
				std::bind (&LeaseSetDestination::HandleCleanupTimer, shared_from_this (), std::placeholders::_1));
		}
	}

	void LeaseSetDestination::CleanupRemoteLeaseSets ()
	{
		auto ts = i2p::util::GetMillisecondsSinceEpoch ();
		std::lock_guard<std::mutex> lock (m_RemoteLeaseSetsMutex);
		for (auto it = m_RemoteLeaseSets.begin (); it != m_RemoteLeaseSets.end ();)
		{
			if (it->second->IsEmpty () || ts > it->second->GetExpirationTime ()) // leaseset expired
			{
				LogPrint (
					eLogDebug, "Destination: Remote LeaseSet ", it->second->GetIdentHash ().ToBase64 (), " expired");
				it = m_RemoteLeaseSets.erase (it);
			}
			else
				++it;
		}
	}

	ClientDestination::ClientDestination (boost::asio::io_context & service, const i2p::data::PrivateKeys & keys,
		bool isPublic, const i2p::util::Mapping * params)
		: LeaseSetDestination (service, isPublic, params)
		, m_Keys (keys)
		, m_PreferredCryptoType (0)
		, m_StreamingAckDelay (DEFAULT_INITIAL_ACK_DELAY)
		, m_StreamingOutboundSpeed (DEFAULT_MAX_OUTBOUND_SPEED)
		, m_StreamingInboundSpeed (DEFAULT_MAX_INBOUND_SPEED)
		, m_StreamingMaxConcurrentStreams (DEFAULT_MAX_CONCURRENT_STREAMS)
		, m_StreamingMaxConnsPerMinute (DEFAULT_MAX_CONNS_PER_MINUTE)
		, m_StreamingMaxConnsPerHour (DEFAULT_MAX_CONNS_PER_HOUR)
		, m_StreamingMaxConnsPerDay (DEFAULT_MAX_CONNS_PER_DAY)
		, m_StreamingMaxTotalConnsPerMinute (DEFAULT_MAX_TOTAL_CONNS_PER_MINUTE)
		, m_StreamingMaxTotalConnsPerHour (DEFAULT_MAX_TOTAL_CONNS_PER_HOUR)
		, m_StreamingMaxTotalConnsPerDay (DEFAULT_MAX_TOTAL_CONNS_PER_DAY)
		, m_StreamingMaxWindowSize (i2p::stream::MAX_WINDOW_SIZE)
		, m_StreamingMaxResends (i2p::stream::MAX_NUM_RESEND_ATTEMPTS)
		, m_StreamingConnectionThrottle (std::make_shared<i2p::stream::StreamingConnectionThrottle> ())
		, m_IsStreamingAnswerPings (DEFAULT_ANSWER_PINGS)
		, m_IsStreamingDontSign (DEFAULT_DONT_SIGN)
		, m_LastPort (0)
		, m_RefCounter (0)
		, m_LastPublishedTimestamp (0)
		, m_ReadyChecker (service)
	{
		if (keys.IsOfflineSignature () && GetLeaseSetType () == i2p::data::NETDB_STORE_TYPE_LEASESET)
			SetLeaseSetType (
				i2p::data::NETDB_STORE_TYPE_STANDARD_LEASESET2); // offline keys can be published with LS2 only

		// extract encryption type params for LS2
		std::set<i2p::data::CryptoKeyType> encryptionKeyTypes;
		bool encryptionTypesSpecified = false;
		// Parsed once so the policy is testable without constructing a
		// destination (see ParseLeaseSetEncryptionTypes()).
		LeaseSetEncryptionTypes encTypeConfig = ParseLeaseSetEncryptionTypes (
			params ? std::string_view ((*params)[I2CP_PARAM_LEASESET_ENCRYPTION_TYPE]) : std::string_view ());
		encryptionTypesSpecified = encTypeConfig.specified;
		m_PreferredCryptoType = encTypeConfig.preferred;
		encryptionKeyTypes = std::move (encTypeConfig.types);
		// If no encryption type was specified use the provider defaults. An
		// explicitly configured list that is malformed or wholly unsupported
		// must fail closed instead of silently publishing a weaker set.
		if (encryptionKeyTypes.empty ())
		{
			if (encryptionTypesSpecified)
			{
				if (encTypeConfig.valid)
					LogPrint (eLogError, "Destination: no configured leaseSetEncType is supported by this provider");
				else
					LogPrint (eLogError,
						"Destination: invalid leaseSetEncType configuration; no LeaseSet2 encryption key will be "
						"installed");
			}
			else
			{
				encryptionKeyTypes.insert ({ GetIdentity ()->GetCryptoKeyType (),
#if OPENSSL_PQ
					i2p::data::CRYPTO_KEY_TYPE_ECIES_MLKEM768_X25519_AEAD,
#endif
					i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD });
#if OPENSSL_PQ
				m_PreferredCryptoType = i2p::data::CRYPTO_KEY_TYPE_ECIES_MLKEM768_X25519_AEAD;
#else
				m_PreferredCryptoType = i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD;
#endif
			}
		}

		for (auto & it : encryptionKeyTypes)
		{
			// Hybrid encryption types are LeaseSet keys, not identity types.
			// Re-check at creation time in case the provider changed between
			// parsing and key generation.
			if (!i2p::data::IsSelectableLS2EncryptionKeyType (it)) continue;
			auto encryptionKey = std::make_shared<i2p::crypto::LocalEncryptionKey> (it);
			if (IsPublic ())
				PersistTemporaryKeys (encryptionKey);
			else if (!encryptionKey->GenerateKeys ())
			{
				// Never install (and therefore publish) a zeroed key section.
				LogPrint (eLogError, "Destination: failed to generate LeaseSet2 encryption key of type ", (int)it);
				continue;
			}
			encryptionKey->CreateDecryptor ();
			if (!encryptionKey->decryptor)
			{
				// A destination can only advertise a key it can actually use.
				LogPrint (eLogError, "Destination: no decryptor for LeaseSet2 encryption key type ", (int)it);
				continue;
			}
			if (it > i2p::data::CRYPTO_KEY_TYPE_ELGAMAL && GetLeaseSetType () == i2p::data::NETDB_STORE_TYPE_LEASESET)
				SetLeaseSetType (i2p::data::NETDB_STORE_TYPE_STANDARD_LEASESET2); // Only DSA can use LeaseSet1
			m_EncryptionKeys.emplace (it, encryptionKey);
		}
		// The preferred type must name a key that was actually installed. A
		// provider without ML-KEM-768 falls back to the strongest available
		// key instead of selecting a type with no local private key.
		if (m_PreferredCryptoType && m_EncryptionKeys.find (m_PreferredCryptoType) == m_EncryptionKeys.end ())
			m_PreferredCryptoType = m_EncryptionKeys.empty () ? 0 : m_EncryptionKeys.rbegin ()->first;

		if (IsPublic ()) LogPrint (eLogInfo, "Destination: Local address ", GetIdentHash ().ToBase32 (), " created");

		try
		{
			if (params)
			{
				// extract streaming params
				params->Get (I2CP_PARAM_STREAMING_INITIAL_ACK_DELAY, m_StreamingAckDelay);
				params->Get (I2CP_PARAM_STREAMING_MAX_OUTBOUND_SPEED, m_StreamingOutboundSpeed);
				if (m_StreamingOutboundSpeed > DEFAULT_MAX_OUTBOUND_SPEED)
					m_StreamingOutboundSpeed = DEFAULT_MAX_OUTBOUND_SPEED;
				params->Get (I2CP_PARAM_STREAMING_MAX_INBOUND_SPEED, m_StreamingInboundSpeed);
				if (m_StreamingInboundSpeed > DEFAULT_MAX_INBOUND_SPEED)
					m_StreamingInboundSpeed = DEFAULT_MAX_INBOUND_SPEED;
				params->Get (I2CP_PARAM_STREAMING_MAX_CONCURRENT_STREAMS, m_StreamingMaxConcurrentStreams);
				params->Get (I2CP_PARAM_STREAMING_MAX_CONNS_PER_MINUTE, m_StreamingMaxConnsPerMinute);
				params->Get (I2CP_PARAM_STREAMING_MAX_CONNS_PER_HOUR, m_StreamingMaxConnsPerHour);
				params->Get (I2CP_PARAM_STREAMING_MAX_CONNS_PER_DAY, m_StreamingMaxConnsPerDay);
				params->Get (I2CP_PARAM_STREAMING_MAX_TOTAL_CONNS_PER_MINUTE, m_StreamingMaxTotalConnsPerMinute);
				params->Get (I2CP_PARAM_STREAMING_MAX_TOTAL_CONNS_PER_HOUR, m_StreamingMaxTotalConnsPerHour);
				params->Get (I2CP_PARAM_STREAMING_MAX_TOTAL_CONNS_PER_DAY, m_StreamingMaxTotalConnsPerDay);
				if (params->Get (I2CP_PARAM_STREAMING_MAX_WINDOW_SIZE, m_StreamingMaxWindowSize)
					&& (m_StreamingMaxWindowSize < i2p::stream::MIN_WINDOW_SIZE))
					m_StreamingMaxWindowSize = i2p::stream::MIN_WINDOW_SIZE;
				params->Get (I2CP_PARAM_STREAMING_ANSWER_PINGS, m_IsStreamingAnswerPings);
				params->Get (I2CP_PARAM_STREAMING_DONT_SIGN, m_IsStreamingDontSign);
				params->Get (I2CP_PARAM_STREAMING_MAX_RESENDS, m_StreamingMaxResends);

				if (GetLeaseSetType () == i2p::data::NETDB_STORE_TYPE_ENCRYPTED_LEASESET2)
				{
					// authentication for encrypted LeaseSet
					auto authType = GetAuthType ();
					if (authType > 0)
					{
						m_AuthKeys = std::make_shared<std::vector<i2p::data::AuthPublicKey>> ();
						if (authType == i2p::data::ENCRYPTED_LEASESET_AUTH_TYPE_DH)
							ReadAuthKey (I2CP_PARAM_LEASESET_CLIENT_DH, params);
						else if (authType == i2p::data::ENCRYPTED_LEASESET_AUTH_TYPE_PSK)
							ReadAuthKey (I2CP_PARAM_LEASESET_CLIENT_PSK, params);
						else
							LogPrint (eLogError, "Destination: Unexpected auth type: ", authType);
						if (m_AuthKeys->size ())
							LogPrint (eLogInfo, "Destination: ", m_AuthKeys->size (), " auth keys read");
						else
						{
							LogPrint (eLogCritical, "Destination: No auth keys read for auth type: ", authType);
							m_AuthKeys = nullptr;
						}
					}
				}
			}
		}
		catch (std::exception & ex)
		{
			LogPrint (eLogCritical, "Destination: Unable to parse parameters for destination: ", ex.what ());
		}
	}

	ClientDestination::~ClientDestination () { }

	void ClientDestination::Start ()
	{
		// Idempotent: a second Start() on a running destination must not re-create
		// m_StreamingDestination (orphaning the live one) nor re-arm leaseset/pool.
		std::vector<std::shared_ptr<i2p::stream::StreamingDestination>> targets;
		{
			std::lock_guard<std::mutex> lock (m_StreamingDestinationsMutex);
			if (m_StreamingDestination) return;
			LeaseSetDestination::Start ();
			m_StreamingDestination
				= std::make_shared<i2p::stream::StreamingDestination> (GetSharedFromThis ()); // TODO:
			targets.push_back (m_StreamingDestination);
			for (auto & it : m_StreamingDestinationsByPorts) targets.push_back (it.second);
		}
		for (auto & it : targets) it->Start ();
	}

	void ClientDestination::Stop ()
	{
		// Stop may be called from any thread, while the service thread is still
		// inside handlers of this destination, so the teardown goes there. A
		// stopped service would never run it, in that case there is nobody left
		// to race with and it is done right here
		auto & service = GetService ();
		if (!service.stopped () && !service.get_executor ().running_in_this_thread ())
		{
			// the promise is shared because the wait below is bounded: a service
			// stopped right after the check would never run the handler, and a
			// stack promise would be gone by then
			auto done = std::make_shared<std::promise<void>> ();
			auto future = done->get_future ();
			boost::asio::post (service,
				[this, done] ()
				{
					StopInternal ();
					done->set_value ();
				});
			if (future.wait_for (std::chrono::seconds (STOP_ON_SERVICE_TIMEOUT)) == std::future_status::ready) return;
			LogPrint (
				eLogError, "Destination: Service didn't stop the destination in ", STOP_ON_SERVICE_TIMEOUT, " seconds");
			return;
		}
		StopInternal ();
	}

	void ClientDestination::StopInternal ()
	{
		LogPrint (eLogDebug, "Destination: Stopping destination ", GetIdentHash ().ToBase32 (), ".b32.i2p");
		m_ReadyChecker.cancel ();
		LogPrint (eLogDebug, "Destination: -> Stopping Streaming Destination");
		std::vector<std::shared_ptr<i2p::stream::StreamingDestination>> targets;
		{
			std::lock_guard<std::mutex> lock (m_StreamingDestinationsMutex);
			if (m_StreamingDestination) targets.push_back (std::move (m_StreamingDestination));
			m_StreamingDestination = nullptr;
			LogPrint (eLogDebug, "Destination: -> Stopping Streaming Destination by ports");
			for (auto & it : m_StreamingDestinationsByPorts) targets.push_back (std::move (it.second));
			m_StreamingDestinationsByPorts.clear ();
			m_LastStreamingDestination = nullptr;
		}
		for (auto & it : targets) it->Stop ();

		if (m_DatagramDestination)
		{
			LogPrint (eLogDebug, "Destination: -> Stopping Datagram Destination");
			m_DatagramDestination = nullptr;
		}
		LeaseSetDestination::Stop ();
		LogPrint (eLogDebug, "Destination: -> Stopping done");
	}

	void ClientDestination::ScheduleSessionResponseTimer (
		std::shared_ptr<i2p::garlic::ECIESX25519AEADRatchetSession> session)
	{
		if (!session || !session->ArmResponseTimer ()) return;
		auto timer = std::make_shared<boost::asio::steady_timer> (GetService ());
		timer->expires_after (std::chrono::milliseconds (CLIENT_RATCHETS_RESPONSE_TIMEOUT));
		timer->async_wait (
			[session, s = GetSharedFromThis (), timer] (const boost::system::error_code & ecode)
			{
				session->DisarmResponseTimer ();
				if (ecode == boost::asio::error::operation_aborted || session->IsTerminated ()
					|| !session->IsResponseRequired () || !session->GetDestinationPtr ())
					return;
				auto pool = s->GetTunnelPool ();
				if (!pool) return;
				auto ls = s->FindLeaseSet (*session->GetDestinationPtr ());
				if (!ls) return;
				auto garlic = session->WrapSingleMessage (nullptr);
				if (!garlic) return;
				auto leases = ls->GetNonExpiredLeases (false);
				if (leases.empty ()) return;
				auto remoteLease = leases[s->GetRng () () % leases.size ()];
				auto outboundTunnel = pool->GetNextOutboundTunnel ();
				if (outboundTunnel)
				{
					LogPrint (eLogDebug, "Destination: Sending ratchets session response");
					outboundTunnel->SendTunnelDataMsgTo (remoteLease->tunnelGateway, remoteLease->tunnelID, garlic);
				}
			});
	}

	void ClientDestination::SetPrivateKeys (const i2p::data::PrivateKeys & keys)
	{
		auto streaming = GetDefaultStreamingDestination ();
		if (streaming) streaming->Stop (); // close all streams
		CleanUp (); // delete sessions and tags
		auto pool = GetTunnelPool ();
		if (pool) pool->DetachTunnels ();
		m_Keys = keys;
		if (m_DatagramDestination) m_DatagramDestination->RefreshIdentity ();
		// update static keys
		for (auto it : m_EncryptionKeys)
			if (it.second)
			{
				if (!it.second->GenerateKeys ())
				{
					// A failed regeneration must not leave the previous key
					// installed; GenerateKeys() zeroed the material, so drop
					// the decryptor and let the next publication rebuild it.
					LogPrint (eLogError, "Destination: failed to regenerate LeaseSet2 encryption key of type ",
						(int)it.second->keyType);
					it.second->decryptor.reset ();
					continue;
				}
				it.second->CreateDecryptor ();
			}
		if (streaming) streaming->Start ();
	}

	void ClientDestination::UpdateOfflineSignature (const i2p::data::PrivateKeys & keys)
	{
		// Adopt a newer offline transient of the same identity in place, on the
		// destination's service so it stays single-threaded with LeaseSet and stream
		// signing. Only the transient is refreshed, the identity is unchanged.
		if (!keys.IsOfflineSignature ()) return;
		boost::asio::post (GetService (),
			[s = GetSharedFromThis (), keys] ()
			{
				if (!s->m_Keys.IsOfflineSignature ()) return;
				if (keys.GetPublic ()->GetIdentHash () != s->GetIdentHash ()) return;
				const auto & next = keys.GetOfflineSignature ();
				const auto & cur = s->m_Keys.GetOfflineSignature ();
				if (next == cur) return; // same transient
				if (bufbe32toh (next.data ()) < bufbe32toh (cur.data ())) return; // do not shorten validity
				LogPrint (eLogInfo, "Destination: Refreshing offline signature for ", s->GetIdentHash ().ToBase32 (),
					", transient expires ", bufbe32toh (next.data ()));
				s->m_Keys.UpdateOfflineSignature (keys);
				if (s->m_DatagramDestination) s->m_DatagramDestination->RefreshIdentity ();
				s->UpdateLeaseSet ();
			});
	}

	void ClientDestination::HandleDataMessage (
		const uint8_t * buf, size_t len, i2p::garlic::ECIESX25519AEADRatchetSession * from)
	{
		if (len < 14) // 4-byte length prefix, then fromPort/toPort/protocol read at offsets up to 9 past it
		{
			if (g_CloveParseDrops.Record ()) LogPrint (eLogWarning, "Destination: Data message is too short ", len);
			return;
		}
		uint32_t length = bufbe32toh (buf);
		// Java DataMessage.readMessage() requires the declared length to be the
		// whole payload: trailing bytes would otherwise be silently ignored by
		// every protocol handler below.
		if (!IsValidDataMessageLength (length, len))
		{
			if (g_CloveParseDrops.Record ())
				LogPrint (eLogError, "Destination: Data message length ", length, " does not match payload ", len - 4);
			return;
		}
		buf += 4;
		// we assume I2CP payload
		uint16_t fromPort = bufbe16toh (buf + 4), // source
			toPort = bufbe16toh (buf + 6); // destination
		switch (buf[9])
		{
		case PROTOCOL_TYPE_STREAMING:
		{
			// streaming protocol
			if (toPort != m_LastPort || !m_LastStreamingDestination)
			{
				m_LastStreamingDestination = GetStreamingDestination (toPort);
				if (!m_LastStreamingDestination)
					m_LastStreamingDestination
						= GetDefaultStreamingDestination (); // if no destination on port use default
				m_LastPort = toPort;
			}
			if (m_LastStreamingDestination)
				m_LastStreamingDestination->HandleDataMessagePayload (fromPort, toPort, buf, length, from);
			else
				LogPrint (eLogError, "Destination: Missing streaming destination");
		}
		break;
		case PROTOCOL_TYPE_DATAGRAM:
		case PROTOCOL_TYPE_RAW:
		case PROTOCOL_TYPE_DATAGRAM2:
		case PROTOCOL_TYPE_DATAGRAM3:
		// A SAM RAW session may register any I2CP protocol number; unknown
		// protocols are dispatched to its raw receiver instead of being dropped.
		default:
			// datagram protocol
			if (m_DatagramDestination)
				m_DatagramDestination->HandleDataMessagePayload (fromPort, toPort, buf, length, buf[9], from);
			else
				LogPrint (eLogError, "Destination: Missing datagram destination");
			break;
		}
	}

	void ClientDestination::CreateStream (
		StreamRequestComplete streamRequestComplete, const i2p::data::IdentHash & dest, uint16_t port)
	{
		if (!streamRequestComplete)
		{
			LogPrint (eLogError, "Destination: Request callback is not specified in CreateStream");
			return;
		}
		auto leaseSet = FindLeaseSet (dest);
		if (leaseSet)
		{
			auto stream = (!leaseSet->IsIncompatibleCrypto ()) ? CreateStream (leaseSet, port) : nullptr;
			boost::asio::post (GetService (), [streamRequestComplete, stream] () { streamRequestComplete (stream); });
		}
		else
		{
			auto s = GetSharedFromThis ();
			RequestDestination (dest,
				[s, streamRequestComplete, port] (std::shared_ptr<const i2p::data::LeaseSet> ls)
				{
					if (ls && !ls->IsIncompatibleCrypto ())
						streamRequestComplete (s->CreateStream (ls, port));
					else
						streamRequestComplete (nullptr);
				});
		}
	}

	void ClientDestination::CreateStream (StreamRequestComplete streamRequestComplete,
		std::shared_ptr<const i2p::data::BlindedPublicKey> dest, uint16_t port)
	{
		if (!streamRequestComplete)
		{
			LogPrint (eLogError, "Destination: Request callback is not specified in CreateStream");
			return;
		}
		auto s = GetSharedFromThis ();
		RequestDestinationWithEncryptedLeaseSet (dest,
			[s, streamRequestComplete, port] (std::shared_ptr<i2p::data::LeaseSet> ls)
			{
				if (ls && !ls->IsIncompatibleCrypto ())
					streamRequestComplete (s->CreateStream (ls, port));
				else
					streamRequestComplete (nullptr);
			});
	}

	template <typename Dest>
	std::shared_ptr<i2p::stream::Stream> ClientDestination::CreateStreamSync (const Dest & dest, uint16_t port)
	{
		volatile bool done = false;
		std::shared_ptr<i2p::stream::Stream> stream;
		std::condition_variable streamRequestComplete;
		std::mutex streamRequestCompleteMutex;
		CreateStream (
			[&done, &streamRequestComplete, &streamRequestCompleteMutex, &stream] (
				std::shared_ptr<i2p::stream::Stream> s)
			{
				stream = s;
				std::unique_lock<std::mutex> l (streamRequestCompleteMutex);
				streamRequestComplete.notify_all ();
				done = true;
			},
			dest, port);
		while (!done)
		{
			std::unique_lock<std::mutex> l (streamRequestCompleteMutex);
			if (!done) streamRequestComplete.wait (l);
		}
		return stream;
	}

	std::shared_ptr<i2p::stream::Stream> ClientDestination::CreateStream (
		const i2p::data::IdentHash & dest, uint16_t port)
	{
		return CreateStreamSync (dest, port);
	}

	std::shared_ptr<i2p::stream::Stream> ClientDestination::CreateStream (
		std::shared_ptr<const i2p::data::BlindedPublicKey> dest, uint16_t port)
	{
		return CreateStreamSync (dest, port);
	}

	std::shared_ptr<i2p::stream::Stream> ClientDestination::CreateStream (
		std::shared_ptr<const i2p::data::LeaseSet> remote, uint16_t port)
	{
		auto streaming = GetDefaultStreamingDestination ();
		return streaming ? streaming->CreateNewOutgoingStream (remote, port) : nullptr;
	}

	void ClientDestination::SendPing (const i2p::data::IdentHash & to, uint16_t fromPort, uint16_t toPort)
	{
		if (auto s = GetDefaultStreamingDestination ())
		{
			auto leaseSet = FindLeaseSet (to);
			if (leaseSet)
				s->SendPing (leaseSet, fromPort, toPort);
			else
			{
				RequestDestination (to,
					[s, fromPort, toPort] (std::shared_ptr<const i2p::data::LeaseSet> ls)
					{
						if (ls) s->SendPing (ls, fromPort, toPort);
					});
			}
		}
	}

	void ClientDestination::SendPing (
		std::shared_ptr<const i2p::data::BlindedPublicKey> to, uint16_t fromPort, uint16_t toPort)
	{
		auto s = GetDefaultStreamingDestination ();
		if (!s) return;
		RequestDestinationWithEncryptedLeaseSet (to,
			[s, fromPort, toPort] (std::shared_ptr<const i2p::data::LeaseSet> ls)
			{
				if (ls) s->SendPing (ls, fromPort, toPort);
			});
	}

	std::shared_ptr<i2p::stream::StreamingDestination> ClientDestination::GetStreamingDestination (uint16_t port) const
	{
		std::lock_guard<std::mutex> lock (m_StreamingDestinationsMutex);
		if (port)
		{
			auto it = m_StreamingDestinationsByPorts.find (port);
			if (it != m_StreamingDestinationsByPorts.end ()) return it->second;
			return nullptr;
		}
		return m_StreamingDestination; // if port is zero, use default destination
	}

	std::shared_ptr<i2p::stream::StreamingDestination> ClientDestination::GetDefaultStreamingDestination () const
	{
		std::lock_guard<std::mutex> lock (m_StreamingDestinationsMutex);
		return m_StreamingDestination;
	}

	void ClientDestination::AcceptStreams (const i2p::stream::StreamingDestination::Acceptor & acceptor)
	{
		if (auto streaming = GetDefaultStreamingDestination ()) streaming->SetAcceptor (acceptor);
	}

	void ClientDestination::StopAcceptingStreams (uint16_t port)
	{
		auto destination = GetStreamingDestination (port);
		if (destination) destination->ResetAcceptor ();
	}

	bool ClientDestination::IsAcceptingStreams () const
	{
		auto streaming = GetDefaultStreamingDestination ();
		return streaming ? streaming->IsAcceptorSet () : false;
	}

	void ClientDestination::AcceptOnce (const i2p::stream::StreamingDestination::Acceptor & acceptor)
	{
		if (auto streaming = GetDefaultStreamingDestination ()) streaming->AcceptOnce (acceptor);
	}

	std::shared_ptr<i2p::stream::StreamingDestination> ClientDestination::CreateStreamingDestination (
		uint16_t port, bool gzip)
	{
		auto dest = std::make_shared<i2p::stream::StreamingDestination> (GetSharedFromThis (), port, gzip);
		std::lock_guard<std::mutex> lock (m_StreamingDestinationsMutex);
		if (port)
			m_StreamingDestinationsByPorts[port] = dest;
		else // update default
			m_StreamingDestination = dest;
		return dest;
	}

	std::shared_ptr<i2p::stream::StreamingDestination> ClientDestination::RemoveStreamingDestination (uint16_t port)
	{
		if (!port) return nullptr;
		std::lock_guard<std::mutex> lock (m_StreamingDestinationsMutex);
		auto it = m_StreamingDestinationsByPorts.find (port);
		if (it == m_StreamingDestinationsByPorts.end ()) return nullptr;
		auto ret = it->second;
		m_StreamingDestinationsByPorts.erase (it);
		return ret;
	}

	std::shared_ptr<i2p::datagram::DatagramDestination> ClientDestination::CreateDatagramDestination (
		bool gzip, i2p::datagram::DatagramVersion version)
	{
		if (!m_DatagramDestination)
		{
			if (!GetNumRatchetInboundTags ())
				SetNumRatchetInboundTags (
					i2p::garlic::ECIESX25519_MAX_NUM_GENERATED_TAGS); // set max tags if not specified
			m_DatagramDestination
				= std::make_shared<i2p::datagram::DatagramDestination> (GetSharedFromThis (), gzip, version);
		}
		return m_DatagramDestination;
	}

	std::vector<std::shared_ptr<const i2p::stream::Stream>> ClientDestination::GetAllStreams () const
	{
		std::vector<std::shared_ptr<i2p::stream::StreamingDestination>> destinations;
		{
			std::lock_guard<std::mutex> lock (m_StreamingDestinationsMutex);
			if (m_StreamingDestination) destinations.push_back (m_StreamingDestination);
			for (auto & it : m_StreamingDestinationsByPorts) destinations.push_back (it.second);
		}
		std::vector<std::shared_ptr<const i2p::stream::Stream>> ret;
		for (auto & it : destinations)
		{
			auto streams = it->GetStreamsList ();
			ret.insert (ret.end (), streams.begin (), streams.end ());
		}
		return ret;
	}

	void ClientDestination::PersistTemporaryKeys (std::shared_ptr<i2p::crypto::LocalEncryptionKey> keys)
	{
		if (!keys) return;
		std::string ident = GetIdentHash ().ToBase32 ();
		std::string path = i2p::fs::DataDirPath ("destinations", ident + "." + std::to_string (keys->keyType) + ".dat");
		std::ifstream f (path, std::ifstream::binary);
		if (f)
		{
			size_t len = 0;
			if (keys->keyType == i2p::data::CRYPTO_KEY_TYPE_ELGAMAL)
				len = 512;
			else if (keys->keyType == i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD)
			{
				f.seekg (0, std::ios::end);
				len = f.tellg ();
				f.seekg (0, std::ios::beg);
			}

			if (len == 512)
			{
				char pub[256], priv[256];
				f.read (pub, 256);
				memcpy (keys->pub.data (), pub, keys->pub.size ());
				f.read (priv, 256);
				memcpy (keys->priv.data (), priv, keys->priv.size ());
			}
			else
			{
				f.read ((char *)keys->pub.data (), keys->pub.size ());
				f.read ((char *)keys->priv.data (), keys->priv.size ());
			}
			if (f)
				return;
			else
				LogPrint (eLogWarning, "Destination: Can't read keys from ", path);
		}

		LogPrint (eLogInfo, "Destination: Creating new temporary keys of type ", keys->keyType, " for address ", ident,
			".b32.i2p");
		memset (keys->priv.data (), 0, keys->priv.size ());
		memset (keys->pub.data (), 0, keys->pub.size ());
		if (!keys->GenerateKeys ())
		{
			// Persisting a zeroed pair would make the failure permanent and
			// publish an unusable key section on the next startup.
			LogPrint (eLogError, "Destination: failed to generate temporary keys of type ", (int)keys->keyType,
				" for address ", ident, ".b32.i2p");
			return;
		}

		// Keep the exact pub||priv layout the read path above expects. The
		// destination key file is secret material, so the writer replaces it
		// atomically with mode 0600 and a failed write leaves the previous pair
		// intact instead of truncating it.
		std::vector<uint8_t> blob;
		blob.reserve (keys->pub.size () + keys->priv.size ());
		blob.insert (blob.end (), keys->pub.begin (), keys->pub.end ());
		blob.insert (blob.end (), keys->priv.begin (), keys->priv.end ());
		const auto artifactName = ident + "." + std::to_string (keys->keyType);
		const bool persisted
			= i2p::data::WriteArtifact (i2p::data::StorageArtifact::DestinationKeys, artifactName, blob);
		OPENSSL_cleanse (blob.data (), blob.size ());
		if (!persisted)
			LogPrint (eLogError, "Destination: Can't persist temporary keys of type ", keys->keyType, " for address ",
				ident, ".b32.i2p");
	}

	void ClientDestination::CreateNewLeaseSet (const std::vector<std::shared_ptr<i2p::tunnel::InboundTunnel>> & tunnels)
	{
		// One immutable LeaseSet object per call: the previous one stays readable
		// through GetLeaseSetMt() until SetLeaseSet() swaps the pointer, so no
		// reader ever observes a half-built or unsigned buffer. LS2 key sections
		// are published highest-type-first with the local preferred type moved to
		// the front, and the published timestamp is forced strictly increasing: a
		// second update within the same second would otherwise be rejected as an
		// older store by every receiver (types 1/3 compare it before replacing).
		std::shared_ptr<i2p::data::LocalLeaseSet> leaseSet;
		if (GetLeaseSetType () == i2p::data::NETDB_STORE_TYPE_LEASESET)
		{
			if (SupportsEncryptionType (i2p::data::CRYPTO_KEY_TYPE_ELGAMAL))
			{
				auto it = m_EncryptionKeys.find (i2p::data::CRYPTO_KEY_TYPE_ELGAMAL);
				leaseSet
					= std::make_shared<i2p::data::LocalLeaseSet> (GetIdentity (), it->second->pub.data (), tunnels);
				// Sign this concrete LS1 object through the base accessors.
				// A virtual GetBuffer()/GetBufferLen() call here makes GCC
				// consider a derived LS2 layout for a base allocation and
				// emit a false array-bounds warning.
				uint8_t * leaseSetBuffer = leaseSet->LocalLeaseSet::GetBuffer ();
				const size_t leaseSetBufferLen = leaseSet->LocalLeaseSet::GetBufferLen ();
				const size_t signatureLen = leaseSet->GetSignatureLen ();
				if (leaseSetBuffer && leaseSetBufferLen >= signatureLen)
					Sign (leaseSetBuffer, static_cast<int> (leaseSetBufferLen - signatureLen),
						leaseSetBuffer + leaseSetBufferLen - signatureLen);
				else
					LogPrint (eLogError, "Destination: LS1 buffer is too short to sign");
			}
			else
				LogPrint (eLogError, "Destinations: Wrong encryption key type for LeaseSet type 1");
		}
		else
		{
			// standard LS2 (type 3) first
			if (m_EncryptionKeys.empty ())
			{
				LogPrint (eLogError, "Destinations: No encryption keys");
				return;
			}

			i2p::data::LocalLeaseSet2::EncryptionKeys keySections;
			std::shared_ptr<const i2p::crypto::LocalEncryptionKey> preferredSection;
			for (const auto & it : m_EncryptionKeys)
			{
				// Do not publish a key whose generation or decryptor creation
				// failed: peers would select it and the handshake could not
				// complete.
				if (!SupportsEncryptionType (it.first))
				{
					LogPrint (eLogWarning, "Destination: Skipping unusable encryption key type ", (int)it.first);
					continue;
				}
				if (it.first == m_PreferredCryptoType)
					preferredSection = it.second;
				else
					keySections.push_front (it.second); // higher key type should appear first
			}
			if (preferredSection) keySections.push_front (preferredSection); // make preferred first
			if (keySections.empty ())
			{
				LogPrint (eLogError, "Destinations: No usable encryption keys");
				return;
			}

			auto publishedTimestamp = i2p::util::GetSecondsSinceEpoch ();
			if (publishedTimestamp <= m_LastPublishedTimestamp)
			{
				LogPrint (eLogDebug, "Destination: LeaseSet update at the same second");
				publishedTimestamp++; // force newer timestamp
			}
			bool isPublishedEncrypted = GetLeaseSetType () == i2p::data::NETDB_STORE_TYPE_ENCRYPTED_LEASESET2;
			auto ls2 = std::make_shared<i2p::data::LocalLeaseSet2> (i2p::data::NETDB_STORE_TYPE_STANDARD_LEASESET2,
				m_Keys, keySections, tunnels, IsPublic (), publishedTimestamp, isPublishedEncrypted);
			if (isPublishedEncrypted) // encrypt if type 5
				ls2 = std::make_shared<i2p::data::LocalEncryptedLeaseSet2> (ls2, m_Keys, GetAuthType (), m_AuthKeys);
			if (!ls2->GetBuffer ())
			{
				LogPrint (eLogError, "Destinations: Couldn't serialize local LeaseSet2");
				return;
			}
			leaseSet = ls2;
			m_LastPublishedTimestamp = publishedTimestamp;
		}
		if (!leaseSet || !leaseSet->GetBuffer ())
		{
			LogPrint (eLogError, "Destinations: Couldn't create local LeaseSet");
			return;
		}
		SetLeaseSet (leaseSet);
	}

	void ClientDestination::CleanupDestination ()
	{
		if (m_DatagramDestination) m_DatagramDestination->CleanUp ();
	}

	bool ClientDestination::Decrypt (
		const uint8_t * encrypted, uint8_t * data, i2p::data::CryptoKeyType preferredCrypto) const
	{
		// A type selected by the peer (ratchet static key type or ElGamal/AES)
		// must map to the same local key type. Falling back to another type
		// would combine a shared secret from the wrong static key (or, worse,
		// run the ElGamal and ratchet decoders on each other's encodings).
		auto it = m_EncryptionKeys.find (preferredCrypto);
		if (it == m_EncryptionKeys.end () || !it->second || !it->second->decryptor)
		{
			LogPrint (eLogError, "Destination: No decryptor for encryption type ", (int)preferredCrypto);
			return false;
		}
		return it->second->decryptor->Decrypt (encrypted, data);
	}

	bool ClientDestination::SupportsEncryptionType (i2p::data::CryptoKeyType keyType) const
	{
		auto it = m_EncryptionKeys.find (keyType);
		// A key whose generation or decryptor creation failed must not be
		// advertised as supported: peers would select it and we could not
		// complete the handshake.
		return it != m_EncryptionKeys.end () && it->second && it->second->decryptor && !it->second->pub.empty ();
	}

	i2p::data::CryptoKeyType ClientDestination::GetRatchetsHighestCryptoType () const
	{
		// std::map is ordered by type ascending. Skip types that are not
		// usable for ratchets (ElGamal) or whose key/decryptor is missing.
		for (auto it = m_EncryptionKeys.rbegin (); it != m_EncryptionKeys.rend (); ++it)
		{
			if (it->first < i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD) return 0;
			if (it->second && it->second->decryptor && !it->second->pub.empty ()) return it->first;
		}
		return 0;
	}

	const uint8_t * ClientDestination::GetEncryptionPublicKey (i2p::data::CryptoKeyType keyType) const
	{
		auto it = m_EncryptionKeys.find (keyType);
		if (it != m_EncryptionKeys.end () && it->second && !it->second->pub.empty ()) return it->second->pub.data ();
		return nullptr;
	}

	void ClientDestination::ReadAuthKey (const std::string & group, const i2p::util::Mapping * params)
	{
		for (const auto & it : params->GetOptions ())
			if (it.first.length () >= group.length () && !it.first.compare (0, group.length (), group))
			{
				auto pos = it.second.find (':');
				if (pos != std::string::npos)
				{
					i2p::data::AuthPublicKey pubKey;
					if (pubKey.FromBase64 (it.second.substr (pos + 1)))
						m_AuthKeys->push_back (pubKey);
					else
						LogPrint (eLogCritical, "Destination: Unexpected auth key: ", it.second.substr (pos + 1));
				}
			}
	}

	bool ClientDestination::DeleteStream (uint32_t recvStreamID)
	{
		std::vector<std::shared_ptr<i2p::stream::StreamingDestination>> destinations;
		{
			std::lock_guard<std::mutex> lock (m_StreamingDestinationsMutex);
			if (m_StreamingDestination) destinations.push_back (m_StreamingDestination);
			for (auto & it : m_StreamingDestinationsByPorts) destinations.push_back (it.second);
		}
		for (auto & it : destinations)
			if (it->DeleteStream (recvStreamID)) return true;
		return false;
	}

	RunnableClientDestination::RunnableClientDestination (
		const i2p::data::PrivateKeys & keys, bool isPublic, const i2p::util::Mapping * params)
		: RunnableService ("Destination")
		, ClientDestination (GetIOService (), keys, isPublic, params)
	{
		if (!GetNickname ().empty ()) RunnableService::SetName (GetNickname ());
	}

	RunnableClientDestination::~RunnableClientDestination ()
	{
		if (IsRunning ()) Stop ();
	}

	void RunnableClientDestination::Start ()
	{
		if (!IsRunning ())
		{
			ClientDestination::Start ();
			StartIOService ();
		}
	}

	void RunnableClientDestination::Stop ()
	{
		if (IsRunning ())
		{
			ClientDestination::Stop (); // takes care of the thread it runs on
			StopIOService ();
		}
	}

}
}
