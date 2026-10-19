/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: I2CP.
 *
 * Implements the router-side I2CP server: session handshake, destination
 * creation, lease-set publication and message send/receive over the binary
 * protocol. Session identifiers are validated against collisions, message and
 * send-queue sizes are bounded, and payloads are routed through the local
 * destination or garlic sessions.
 *
 */

#include <string.h>
#include <stdlib.h>
#include <charconv>
#include <chrono>
#include <future>
#include <openssl/rand.h>
#include "Base.h"
#include "I2PEndian.h"
#include "Log.h"
#include "Timestamp.h"
#include "LeaseSet.h"
#include "ClientContext.h"
#include "Transports.h"
#include "Signature.h"
#include "Config.h"
#include "I2CP.h"

namespace i2p
{
namespace client
{
	namespace
	{
		// ClientMessageEventListener.handleBlindingInfo() keeps this in the
		// router's netDb keyed by the unblinded SigningPublicKey. The local
		// equivalent is bounded and only trusted for the duration announced by
		// the client.
		const size_t I2CP_MAX_BLINDING_INFOS = 64;

		struct BlindingInfo
		{
			std::shared_ptr<const i2p::data::IdentityEx> identity; // TYPE_DEST only
			i2p::data::SigningKeyType blindType = i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519;
			uint8_t authType = 0;
			std::vector<uint8_t> authPrivateKey;
			std::string secret;
			uint64_t expiration = 0; // milliseconds since epoch, 0 = forever
			uint64_t updated = 0;
		};

		typedef std::pair<uint16_t, std::vector<uint8_t>> BlindingInfoKey;

		std::mutex g_BlindingInfosMutex;
		std::map<BlindingInfoKey, BlindingInfo> g_BlindingInfos;

		BlindingInfoKey MakeBlindingInfoKey (i2p::data::SigningKeyType keyType, const uint8_t * key, size_t keyLen)
		{
			return { (uint16_t)keyType, std::vector<uint8_t> (key, key + keyLen) };
		}

		void PruneBlindingInfos (uint64_t now)
		{
			for (auto it = g_BlindingInfos.begin (); it != g_BlindingInfos.end ();)
			{
				if (it->second.expiration && now > it->second.expiration)
					it = g_BlindingInfos.erase (it);
				else
					++it;
			}
			while (g_BlindingInfos.size () >= I2CP_MAX_BLINDING_INFOS)
			{
				// bounded cache: drop the least recently updated non-expired entry
				auto oldest = g_BlindingInfos.begin ();
				for (auto it = g_BlindingInfos.begin (); it != g_BlindingInfos.end (); ++it)
					if (it->second.updated < oldest->second.updated) oldest = it;
				g_BlindingInfos.erase (oldest);
			}
		}

		void StoreBlindingInfo (BlindingInfoKey key, BlindingInfo info)
		{
			std::lock_guard<std::mutex> l (g_BlindingInfosMutex);
			info.updated = i2p::util::GetMillisecondsSinceEpoch ();
			PruneBlindingInfos (info.updated);
			auto it = g_BlindingInfos.find (key);
			if (it != g_BlindingInfos.end ())
			{
				// A TYPE_KEY update must not erase a TYPE_DEST identity, auth
				// key or secret already associated with the same signing key.
				auto & existing = it->second;
				if (!info.identity) info.identity = existing.identity;
				if (info.authType == 0)
				{
					info.authType = existing.authType;
					info.authPrivateKey = existing.authPrivateKey;
				}
				if (info.secret.empty ()) info.secret = existing.secret;
				if (!info.expiration)
					info.expiration = existing.expiration;
				else if (existing.expiration && info.expiration < existing.expiration)
					info.expiration = existing.expiration;
			}
			g_BlindingInfos.insert_or_assign (std::move (key), std::move (info));
		}

		std::shared_ptr<const i2p::data::IdentityEx> FindBlindedDestination (
			const i2p::data::BlindedPublicKey & blinded)
		{
			auto keyLen = blinded.GetPublicKeyLen ();
			if (!keyLen) return nullptr;
			std::lock_guard<std::mutex> l (g_BlindingInfosMutex);
			auto now = i2p::util::GetMillisecondsSinceEpoch ();
			// A b33 carries the unblinded signing public key. TYPE_DEST and
			// TYPE_KEY are stored under exactly that key; deriving today's
			// blinded key and comparing it with the stored unblinded key can
			// never match.
			auto it
				= g_BlindingInfos.find (MakeBlindingInfoKey (blinded.GetSigType (), blinded.GetPublicKey (), keyLen));
			if (it == g_BlindingInfos.end ()) return nullptr;
			if (it->second.expiration && now > it->second.expiration) return nullptr;
			return it->second.identity;
		}
	} // namespace

	I2CPDestination::I2CPDestination (boost::asio::io_context & service, std::shared_ptr<I2CPSession> owner,
		std::shared_ptr<const i2p::data::IdentityEx> identity, bool isPublic, bool isSameThread,
		const i2p::util::Mapping & params)
		: LeaseSetDestination (service, isPublic, &params)
		, m_Owner (owner)
		, m_Identity (identity)
		, m_IsCreatingLeaseSet (false)
		, m_IsSameThread (isSameThread)
		, m_LeaseSetCreationTimer (service)
		, m_ReadinessCheckTimer (service)
	{
	}

	void I2CPDestination::Stop ()
	{
		m_LeaseSetCreationTimer.cancel ();
		m_ReadinessCheckTimer.cancel ();
		LeaseSetDestination::Stop ();
		m_Owner = nullptr;
	}

	void I2CPDestination::SetEncryptionPrivateKey (i2p::data::CryptoKeyType keyType, const uint8_t * key)
	{
		bool exist = false;
		auto it = m_EncryptionKeys.find (keyType);
		if (it != m_EncryptionKeys.end ()
			&& !memcmp (it->second->priv.data (), key, it->second->priv.size ())) // new key?
			exist = true;
		if (!exist)
		{
			auto encryptionKey = std::make_shared<i2p::crypto::LocalEncryptionKey> (keyType);
			memcpy (encryptionKey->priv.data (), key, encryptionKey->priv.size ());
			encryptionKey->CreateDecryptor (); // from private key
			m_EncryptionKeys.insert_or_assign (keyType, encryptionKey);
		}
	}

	void I2CPDestination::SetECIESx25519EncryptionPrivateKey (i2p::data::CryptoKeyType keyType, const uint8_t * key)
	{
		bool exist = false;
		auto it = m_EncryptionKeys.find (keyType);
		if (it != m_EncryptionKeys.end ()
			&& !memcmp (it->second->priv.data (), key, it->second->priv.size ())) // new key?
			exist = true;
		if (!exist)
		{
			auto encryptionKey = std::make_shared<i2p::crypto::LocalEncryptionKey> (keyType);
			memcpy (encryptionKey->priv.data (), key, encryptionKey->priv.size ());
			// create decryptor manually
			auto decryptor
				= std::make_shared<i2p::crypto::ECIESX25519AEADRatchetDecryptor> (key, true); // calculate publicKey
			encryptionKey->decryptor = decryptor;
			// assign public key from decryptor
			memcpy (encryptionKey->pub.data (), decryptor->GetPubicKey (), encryptionKey->pub.size ());
			// insert or replace new
			m_EncryptionKeys.insert_or_assign (keyType, encryptionKey);
		}
	}

	bool I2CPDestination::Decrypt (
		const uint8_t * encrypted, uint8_t * data, i2p::data::CryptoKeyType preferredCrypto) const
	{
		std::shared_ptr<i2p::crypto::LocalEncryptionKey> encryptionKey;
		if (!m_EncryptionKeys.empty ())
		{
			if (m_EncryptionKeys.rbegin ()->first == preferredCrypto)
				encryptionKey = m_EncryptionKeys.rbegin ()->second;
			else
			{
				auto it = m_EncryptionKeys.find (preferredCrypto);
				if (it != m_EncryptionKeys.end ()) encryptionKey = it->second;
			}
			if (!encryptionKey) encryptionKey = m_EncryptionKeys.rbegin ()->second;
		}
		if (encryptionKey)
			return encryptionKey->decryptor->Decrypt (encrypted, data);
		else
			LogPrint (eLogError, "I2CP: Decryptor is not set");
		return false;
	}

	const uint8_t * I2CPDestination::GetEncryptionPublicKey (i2p::data::CryptoKeyType keyType) const
	{
		auto it = m_EncryptionKeys.find (keyType);
		if (it != m_EncryptionKeys.end ()) return it->second->pub.data ();
		return nullptr;
	}

	bool I2CPDestination::SupportsEncryptionType (i2p::data::CryptoKeyType keyType) const
	{
		return m_EncryptionKeys.contains (keyType);
	}

	i2p::data::CryptoKeyType I2CPDestination::GetPreferredCryptoType () const
	{
		return !m_EncryptionKeys.empty () ? m_EncryptionKeys.rbegin ()->first : 0;
	}

	i2p::data::CryptoKeyType I2CPDestination::GetRatchetsHighestCryptoType () const
	{
		if (m_EncryptionKeys.empty ()) return 0;
		auto cryptoType = m_EncryptionKeys.rbegin ()->first;
		return cryptoType >= i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD ? cryptoType : 0;
	}

	void I2CPDestination::ScheduleSessionResponseTimer (
		std::shared_ptr<i2p::garlic::ECIESX25519AEADRatchetSession> session)
	{
		if (!session) return;
		auto timer = std::make_shared<boost::asio::steady_timer> (GetService ());
		timer->expires_after (std::chrono::milliseconds (I2CP_RATCHETS_RESPONSE_TIMEOUT));
		timer->async_wait (
			[session, s = GetSharedFromThis (), timer] (const boost::system::error_code & ecode)
			{
				if (ecode != boost::asio::error::operation_aborted && !session->IsTerminated ()
					&& session->IsResponseRequired () && // response still required
					session->GetDestinationPtr ()) // we know ident hash
				{
					auto pool = s->GetTunnelPool ();
					if (!pool) return;
					auto ls = s->FindLeaseSet (*session->GetDestinationPtr ());
					if (!ls) return;
					auto garlic = session->WrapSingleMessage (nullptr); // no data messages
					if (!garlic) return;
					auto leases = ls->GetNonExpiredLeases (false);
					if (!leases.empty ())
					{
						auto remoteLease = leases[s->GetRng () () % leases.size ()];
						auto outboundTunnel = pool->GetNextOutboundTunnel ();
						if (outboundTunnel)
						{
							LogPrint (eLogDebug, "I2CP: Sending ratchets session response");
							s->SendMsg (garlic, outboundTunnel, remoteLease);
						}
					}
				}
			});
	}

	void I2CPDestination::HandleDataMessage (
		const uint8_t * buf, size_t len, i2p::garlic::ECIESX25519AEADRatchetSession * from)
	{
		if (len < 4)
		{
			LogPrint (eLogWarning, "I2CP: data message is too short ", len);
			return;
		}
		uint32_t length = bufbe32toh (buf);
		if (length > len - 4) length = len - 4;
		if (m_Owner) m_Owner->SendMessagePayloadMessage (buf + 4, length);
	}

	void I2CPDestination::CreateNewLeaseSet (const std::vector<std::shared_ptr<i2p::tunnel::InboundTunnel>> & tunnels)
	{
		boost::asio::post (
			GetService (), std::bind (&I2CPDestination::PostCreateNewLeaseSet, GetSharedFromThis (), tunnels));
	}

	void I2CPDestination::PostCreateNewLeaseSet (std::vector<std::shared_ptr<i2p::tunnel::InboundTunnel>> tunnels)
	{
		if (m_IsCreatingLeaseSet)
		{
			LogPrint (eLogInfo, "I2CP: LeaseSet is being created");
			return;
		}
		m_ReadinessCheckTimer.cancel ();
		auto pool = GetTunnelPool ();
		if (!pool || !pool->HasOutboundTunnels ())
		{
			// try again later
			m_ReadinessCheckTimer.expires_after (std::chrono::seconds (I2CP_DESTINATION_READINESS_CHECK_INTERVAL));
			m_ReadinessCheckTimer.async_wait (
				[s = GetSharedFromThis (), tunnels = std::move (tunnels)] (const boost::system::error_code & ecode)
				{
					if (ecode != boost::asio::error::operation_aborted) s->PostCreateNewLeaseSet (tunnels);
				});
			return;
		}
		uint8_t priv[256] = { 0 };
		i2p::data::LocalLeaseSet ls (
			m_Identity, priv, tunnels); // we don't care about encryption key, we need leases only
		m_LeaseSetExpirationTime = ls.GetExpirationTime ();
		uint8_t * leases = ls.GetLeases ();
		int numLeases = leases[-1];
		if (m_Owner && numLeases)
		{
			uint16_t sessionID = m_Owner->GetSessionID ();
			if (sessionID != 0xFFFF)
			{
				m_IsCreatingLeaseSet = true;
				htobe16buf (leases - 3, sessionID);
				size_t l = 2 /*sessionID*/ + 1 /*num leases*/ + i2p::data::LEASE_SIZE * numLeases;
				m_Owner->SendI2CPMessage (I2CP_REQUEST_VARIABLE_LEASESET_MESSAGE, leases - 3, l);
				m_LeaseSetCreationTimer.expires_after (std::chrono::seconds (I2CP_LEASESET_CREATION_TIMEOUT));
				auto s = GetSharedFromThis ();
				m_LeaseSetCreationTimer.async_wait (
					[s] (const boost::system::error_code & ecode)
					{
						if (ecode != boost::asio::error::operation_aborted)
						{
							LogPrint (eLogInfo, "I2CP: LeaseSet creation timeout expired. Terminate");
							if (s->m_Owner) s->m_Owner->Stop ();
						}
					});
			}
		}
		else
			LogPrint (eLogError, "I2CP: Can't request LeaseSet");
	}

	void I2CPDestination::LeaseSetCreated (const uint8_t * buf, size_t len)
	{
		m_IsCreatingLeaseSet = false;
		m_LeaseSetCreationTimer.cancel ();
		auto ls = std::make_shared<i2p::data::LocalLeaseSet> (m_Identity, buf, len);
		ls->SetExpirationTime (m_LeaseSetExpirationTime);
		SetLeaseSet (ls);
	}

	void I2CPDestination::LeaseSet2Created (uint8_t storeType, const uint8_t * buf, size_t len)
	{
		m_IsCreatingLeaseSet = false;
		m_LeaseSetCreationTimer.cancel ();
		auto ls = (storeType == i2p::data::NETDB_STORE_TYPE_ENCRYPTED_LEASESET2)
			? std::make_shared<i2p::data::LocalEncryptedLeaseSet2> (m_Identity, buf, len)
			: std::make_shared<i2p::data::LocalLeaseSet2> (storeType, m_Identity, buf, len);
		ls->SetExpirationTime (m_LeaseSetExpirationTime);
		SetLeaseSet (ls);
	}

	void I2CPDestination::SendMsgTo (
		const uint8_t * payload, size_t len, const i2p::data::IdentHash & ident, uint32_t nonce)
	{
		// I2CP allows a 64 KiB payload, but an I2NP Data message has a
		// 4-byte length prefix inside a fixed I2NP_MAX_MESSAGE_SIZE buffer.
		// Reject before copying or consuming a pool buffer.
		if (!payload || len > I2NP_MAX_MESSAGE_SIZE - 4)
		{
			LogPrint (eLogError, "I2CP: I2NP Data payload is too large ", len);
			if (m_Owner) m_Owner->SendMessageStatusMessage (nonce, eI2CPMessageStatusGuaranteedFailure);
			return;
		}
		auto msg = m_I2NPMsgsPool.AcquireSharedMt ();
		if (!msg) return;
		uint8_t * buf = msg->GetPayload ();
		htobe32buf (buf, len);
		if (len) memcpy (buf + 4, payload, len);
		msg->len += len + 4;
		msg->FillI2NPMessageHeader (eI2NPData, GetRng () ());
		auto remote = FindLeaseSet (ident);
		if (remote)
		{
			if (m_IsSameThread)
			{
				// send right a way
				bool sent = SendMsg (msg, remote);
				if (m_Owner)
					m_Owner->SendMessageStatusMessage (
						nonce, sent ? eI2CPMessageStatusGuaranteedSuccess : eI2CPMessageStatusGuaranteedFailure);
			}
			else
			{
				// send in destination's thread
				auto s = GetSharedFromThis ();
				boost::asio::post (GetService (),
					[s, msg, remote, nonce] ()
					{
						bool sent = s->SendMsg (msg, remote);
						if (s->m_Owner)
							s->m_Owner->SendMessageStatusMessage (nonce,
								sent ? eI2CPMessageStatusGuaranteedSuccess : eI2CPMessageStatusGuaranteedFailure);
					});
			}
		}
		else
		{
			auto s = GetSharedFromThis ();
			RequestDestination (ident,
				[s, msg, nonce] (std::shared_ptr<i2p::data::LeaseSet> ls)
				{
					if (ls)
					{
						bool sent = s->SendMsg (msg, ls);
						if (s->m_Owner)
							s->m_Owner->SendMessageStatusMessage (nonce,
								sent ? eI2CPMessageStatusGuaranteedSuccess : eI2CPMessageStatusGuaranteedFailure);
					}
					else if (s->m_Owner)
						s->m_Owner->SendMessageStatusMessage (nonce, eI2CPMessageStatusNoLeaseSet);
				});
		}
	}

	bool I2CPDestination::SendMsg (std::shared_ptr<I2NPMessage> msg, std::shared_ptr<const i2p::data::LeaseSet> remote)
	{
		auto remoteSession = GetRoutingSession (remote, true);
		if (!remoteSession)
		{
			LogPrint (eLogError, "I2CP: Failed to create remote session");
			return false;
		}
		auto garlic = remoteSession->WrapSingleMessage (msg); // shared routing path mitgh be dropped here
		auto path = remoteSession->GetSharedRoutingPath ();
		std::shared_ptr<i2p::tunnel::OutboundTunnel> outboundTunnel;
		std::shared_ptr<const i2p::data::Lease> remoteLease;
		if (path)
		{
			if (!remoteSession->CleanupUnconfirmedTags ()) // no stuck tags
			{
				outboundTunnel = path->outboundTunnel;
				remoteLease = path->remoteLease;
			}
			else
				remoteSession->SetSharedRoutingPath (nullptr);
		}
		if (!outboundTunnel || !remoteLease)
		{
			auto leases = remote->GetNonExpiredLeases (false); // without threshold
			if (leases.empty ()) leases = remote->GetNonExpiredLeases (true); // with threshold
			if (!leases.empty ())
			{
				remoteLease = leases[GetRng () () % leases.size ()];
				auto leaseRouter = i2p::data::netdb.FindRouter (remoteLease->tunnelGateway);
				auto pool = GetTunnelPool ();
				if (pool)
					outboundTunnel = pool->GetNextOutboundTunnel (nullptr,
						leaseRouter
							? leaseRouter->GetCompatibleTransports (false)
							: (i2p::data::RouterInfo::CompatibleTransports)i2p::data::RouterInfo::eAllTransports);
				else
					outboundTunnel = nullptr;
			}
			if (remoteLease && outboundTunnel)
				remoteSession->SetSharedRoutingPath (std::make_shared<i2p::garlic::GarlicRoutingPath> (
					i2p::garlic::GarlicRoutingPath { outboundTunnel, remoteLease, 10000, 0 })); // 10 secs RTT
			else
				remoteSession->SetSharedRoutingPath (nullptr);
		}
		m_Owner->AddRoutingSession (
			remote->GetIdentity ()->GetStandardIdentity ().signingKey + 96, remoteSession); // last 32 bytes
		return SendMsg (garlic, outboundTunnel, remoteLease);
	}

	bool I2CPDestination::SendMsg (std::shared_ptr<I2NPMessage> garlic,
		std::shared_ptr<i2p::tunnel::OutboundTunnel> outboundTunnel,
		std::shared_ptr<const i2p::data::Lease> remoteLease)
	{
		if (remoteLease && outboundTunnel)
		{
			outboundTunnel->SendTunnelDataMsgs ({ i2p::tunnel::TunnelMessageBlock {
				i2p::tunnel::eDeliveryTypeTunnel, remoteLease->tunnelGateway, remoteLease->tunnelID, garlic } });
			return true;
		}
		else
		{
			if (outboundTunnel)
				LogPrint (eLogWarning, "I2CP: Failed to send message. All leases expired");
			else
				LogPrint (eLogWarning, "I2CP: Failed to send message. No outbound tunnels");
			return false;
		}
	}

	bool I2CPDestination::SendMsg (const uint8_t * payload, size_t len,
		std::shared_ptr<i2p::garlic::GarlicRoutingSession> remoteSession, uint32_t nonce)
	{
		if (!remoteSession) return false;
		auto path = remoteSession->GetSharedRoutingPath ();
		if (!path) return false;
		// get tunnels
		std::shared_ptr<i2p::tunnel::OutboundTunnel> outboundTunnel;
		std::shared_ptr<const i2p::data::Lease> remoteLease;
		if (!remoteSession->CleanupUnconfirmedTags ()) // no stuck tags
		{
			outboundTunnel = path->outboundTunnel;
			remoteLease = path->remoteLease;
		}
		else
		{
			remoteSession->SetSharedRoutingPath (nullptr);
			return false;
		}
		// create Data message
		if (!payload || len > I2NP_MAX_MESSAGE_SIZE - 4)
		{
			LogPrint (eLogError, "I2CP: I2NP Data payload is too large ", len);
			if (m_Owner) m_Owner->SendMessageStatusMessage (nonce, eI2CPMessageStatusGuaranteedFailure);
			return false;
		}
		auto msg = m_I2NPMsgsPool.AcquireSharedMt ();
		if (!msg)
		{
			if (m_Owner) m_Owner->SendMessageStatusMessage (nonce, eI2CPMessageStatusGuaranteedFailure);
			return false;
		}
		uint8_t * buf = msg->GetPayload ();
		htobe32buf (buf, len);
		if (len) memcpy (buf + 4, payload, len);
		msg->len += len + 4;
		msg->FillI2NPMessageHeader (eI2NPData, GetRng () ());
		// wrap in gralic
		auto garlic = remoteSession->WrapSingleMessage (msg);
		if (!garlic)
		{
			if (m_Owner) m_Owner->SendMessageStatusMessage (nonce, eI2CPMessageStatusGuaranteedFailure);
			remoteSession->SetSharedRoutingPath (nullptr);
			return false;
		}
		// send
		bool sent = SendMsg (garlic, outboundTunnel, remoteLease);
		if (m_Owner)
			m_Owner->SendMessageStatusMessage (
				nonce, sent ? eI2CPMessageStatusGuaranteedSuccess : eI2CPMessageStatusGuaranteedFailure);
		if (!sent) remoteSession->SetSharedRoutingPath (nullptr);
		return sent;
	}

	void I2CPDestination::CleanupDestination ()
	{
		m_I2NPMsgsPool.CleanUpMt ();
		if (m_Owner) m_Owner->CleanupRoutingSessions ();
	}

	RunnableI2CPDestination::RunnableI2CPDestination (std::shared_ptr<I2CPSession> owner,
		std::shared_ptr<const i2p::data::IdentityEx> identity, bool isPublic, const i2p::util::Mapping & params)
		: RunnableService ("I2CP")
		, I2CPDestination (GetIOService (), owner, identity, isPublic, false, params)
	{
	}

	RunnableI2CPDestination::~RunnableI2CPDestination ()
	{
		// m_Self keeps the object alive while its service thread is running, so
		// this only runs after the thread has been joined. The explicit Stop is
		// still needed for a destination that was never started through Start().
		if (IsRunning ()) Stop ();
	}

	std::shared_ptr<RunnableI2CPDestination> RunnableI2CPDestination::GetRunnableSharedFromThis ()
	{
		return std::static_pointer_cast<RunnableI2CPDestination> (GetSharedFromThis ());
	}

	bool RunnableI2CPDestination::IsInServiceThread () const
	{
		std::lock_guard<std::mutex> l (m_ServiceThreadMutex);
		return m_ServiceThreadID == std::this_thread::get_id ();
	}

	void RunnableI2CPDestination::Start ()
	{
		if (!IsRunning ())
		{
			// Hold a self-reference for the whole lifetime of the worker. A
			// last owner released from a timer/handler can then never destroy
			// this RunnableService on its own thread while the thread is still
			// joinable.
			m_StopRequested.store (false);
			m_Self = GetRunnableSharedFromThis ();
			boost::asio::post (GetIOService (),
				[this] ()
				{
					std::lock_guard<std::mutex> l (m_ServiceThreadMutex);
					m_ServiceThreadID = std::this_thread::get_id ();
				});
			I2CPDestination::Start ();
			StartIOService ();
		}
	}

	void RunnableI2CPDestination::Stop ()
	{
		if (!IsRunning ())
		{
			I2CPDestination::Stop ();
			m_Self.reset ();
			return;
		}
		if (m_StopRequested.exchange (true)) return;

		if (IsInServiceThread ())
		{
			// Cancel timers and detach from the session immediately, but let a
			// different thread perform the blocking join. Joining from this
			// thread would throw std::system_error (or abort in a destructor).
			I2CPDestination::Stop ();
			auto self = m_Self;
			if (!self)
			{
				LogPrint (eLogError, "I2CP: Destination service cannot defer its own stop");
				return;
			}
			std::thread (
				[self] () mutable
				{
					self->StopIOService ();
					self->m_Self.reset ();
				})
				.detach ();
			return;
		}

		I2CPDestination::Stop ();
		StopIOService ();
		m_Self.reset ();
	}

	I2CPSession::I2CPSession (I2CPServer & owner, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
		: m_Owner (owner)
		, m_Socket (socket)
		, m_SessionID (0xFFFF)
		, m_MessageID (0)
		, m_IsSendAccepted (true)
		, m_IsSending (false)
	{
	}

	I2CPSession::~I2CPSession ()
	{
		// Sessions must normally be destroyed on the owner thread: Terminate()
		// posts there, and the posted handler holds a reference until cleanup
		// completes. Use the inline teardown here so the destructor never calls
		// shared_from_this().
		TerminateInOwnerThread ();
	}

	void I2CPSession::Start ()
	{
		if (m_Socket)
		{
			m_Socket->set_option (boost::asio::socket_base::receive_buffer_size (I2CP_MAX_MESSAGE_LENGTH));
			m_Socket->set_option (boost::asio::socket_base::send_buffer_size (I2CP_MAX_MESSAGE_LENGTH));
		}
		ReadProtocolByte ();
	}

	void I2CPSession::Stop ()
	{
		Terminate ();
	}

	void I2CPSession::ReadProtocolByte ()
	{
		auto socket = m_Socket;
		if (socket)
		{
			auto s = shared_from_this ();
			socket->async_read_some (boost::asio::buffer (m_Header, 1),
				[s, socket] (const boost::system::error_code & ecode, std::size_t bytes_transferred)
				{
					if (!ecode && bytes_transferred > 0 && s->m_Header[0] == I2CP_PROTOCOL_BYTE)
						s->ReceiveHeader ();
					else
						s->Terminate ();
				});
		}
	}

	void I2CPSession::ReceiveHeader ()
	{
		auto socket = m_Socket;
		if (!socket)
		{
			LogPrint (eLogError, "I2CP: Can't receive header");
			return;
		}
		// the handler keeps the socket alive: Terminate resets m_Socket, while a composed
		// read holds a reference to the socket object until the whole read completes
		boost::asio::async_read (*socket, boost::asio::buffer (m_Header, I2CP_HEADER_SIZE),
			boost::asio::transfer_all (),
			[s = shared_from_this (), socket] (const boost::system::error_code & ecode, std::size_t bytes_transferred)
			{ s->HandleReceivedHeader (ecode, bytes_transferred); });
	}

	void I2CPSession::HandleReceivedHeader (const boost::system::error_code & ecode, std::size_t bytes_transferred)
	{
		if (ecode)
			Terminate ();
		else
		{
			m_PayloadLen = bufbe32toh (m_Header + I2CP_HEADER_LENGTH_OFFSET);
			if (m_PayloadLen > 0)
			{
				if (m_PayloadLen <= I2CP_MAX_MESSAGE_LENGTH)
				{
					if (!m_Socket) return;
					boost::system::error_code ec;
					size_t moreBytes = m_Socket->available (ec);
					if (!ec)
					{
						if (moreBytes >= m_PayloadLen)
						{
							// read and process payload immediately if available
							moreBytes = boost::asio::read (*m_Socket, boost::asio::buffer (m_Payload, m_PayloadLen),
								boost::asio::transfer_all (), ec);
							HandleReceivedPayload (ec, moreBytes);
						}
						else
							ReceivePayload ();
					}
					else
					{
						LogPrint (eLogWarning, "I2CP: Socket error: ", ec.message ());
						Terminate ();
					}
				}
				else
				{
					LogPrint (eLogError, "I2CP: Unexpected payload length ", m_PayloadLen);
					Terminate ();
				}
			}
			else // no following payload
			{
				HandleMessage ();
				if (m_Terminated.load ()) return;
				ReceiveHeader (); // next message
			}
		}
	}

	void I2CPSession::ReceivePayload ()
	{
		auto socket = m_Socket;
		if (!socket)
		{
			LogPrint (eLogError, "I2CP: Can't receive payload");
			return;
		}
		boost::asio::async_read (*socket, boost::asio::buffer (m_Payload, m_PayloadLen), boost::asio::transfer_all (),
			[s = shared_from_this (), socket] (const boost::system::error_code & ecode, std::size_t bytes_transferred)
			{ s->HandleReceivedPayload (ecode, bytes_transferred); });
	}

	void I2CPSession::HandleReceivedPayload (const boost::system::error_code & ecode, std::size_t bytes_transferred)
	{
		if (ecode)
			Terminate ();
		else
		{
			HandleMessage ();
			m_PayloadLen = 0;
			if (m_Terminated.load ()) return;
			ReceiveHeader (); // next message
		}
	}

	void I2CPSession::HandleMessage ()
	{
		auto handler = m_Owner.GetMessagesHandlers ()[m_Header[I2CP_HEADER_TYPE_OFFSET]];
		if (handler)
			(this->*handler) (m_Payload, m_PayloadLen);
		else
			LogPrint (eLogError, "I2CP: Unknown I2CP message ", (int)m_Header[I2CP_HEADER_TYPE_OFFSET]);
	}

	void I2CPSession::Terminate ()
	{
		if (m_Terminated.load ()) return;
		if (!m_Owner.IsInServiceThread ())
		{
			// The socket and the send queue are owned by the I2CP service
			// thread. A timer or a destination worker reaching this path must
			// not close them (or join the destination) from its own thread.
			auto s = shared_from_this ();
			boost::asio::post (m_Owner.GetService (), [s] () { s->TerminateInOwnerThread (); });
			return;
		}
		TerminateInOwnerThread ();
	}

	void I2CPSession::TerminateInOwnerThread ()
	{
		if (m_Terminated.exchange (true)) return;

		if (m_Destination)
		{
			m_Destination->Stop ();
			m_Destination = nullptr;
		}
		if (m_Socket)
		{
			boost::system::error_code ec;
			m_Socket->shutdown (boost::asio::ip::tcp::socket::shutdown_both, ec);
			m_Socket->close (ec);
			m_Socket = nullptr;
		}
		{
			std::lock_guard<std::mutex> l (m_SendMutex);
			if (!m_SendQueue.IsEmpty ()) m_SendQueue.CleanUp ();
			m_IsSending = false;
		}
		auto sessionID = m_SessionID.exchange (0xFFFF);
		if (sessionID != 0xFFFF)
		{
			// Remove exactly the identifier that was inserted, even if a second
			// CREATE SESSION arrived in the meantime.
			m_Owner.RemoveSession (sessionID);
			LogPrint (eLogDebug, "I2CP: Session ", sessionID, " terminated");
		}
	}

	void I2CPSession::SendI2CPMessage (uint8_t type, const uint8_t * payload, size_t len)
	{
		auto l = len + I2CP_HEADER_SIZE;
		if (l > I2CP_MAX_MESSAGE_LENGTH)
		{
			LogPrint (eLogError, "I2CP: Message to send is too long ", l);
			return;
		}
		// Build a complete, caller-independent buffer. The service thread is the
		// only place where it is copied into the in-flight buffer or queued.
		auto sendBuf = std::make_shared<i2p::stream::SendBuffer> (l);
		uint8_t * buf = sendBuf->buf;
		htobe32buf (buf + I2CP_HEADER_LENGTH_OFFSET, len);
		buf[I2CP_HEADER_TYPE_OFFSET] = type;
		if (len) memcpy (buf + I2CP_HEADER_SIZE, payload, len);

		if (m_Owner.IsInServiceThread ())
			QueueI2CPMessage (std::move (sendBuf), l);
		else
		{
			auto s = shared_from_this ();
			boost::asio::post (m_Owner.GetService (),
				[s, sendBuf = std::move (sendBuf), l] () mutable { s->QueueI2CPMessage (std::move (sendBuf), l); });
		}
	}

	void I2CPSession::QueueI2CPMessage (std::shared_ptr<i2p::stream::SendBuffer> && sendBuf, size_t len)
	{
		std::lock_guard<std::mutex> l (m_SendMutex);
		if (m_Terminated.load () || !m_Socket) return;
		if (m_IsSending)
		{
			if (m_SendQueue.GetSize () + len <= I2CP_MAX_SEND_QUEUE_SIZE)
				m_SendQueue.Add (std::move (sendBuf));
			else
				LogPrint (eLogWarning, "I2CP: Send queue size exceeds ", I2CP_MAX_SEND_QUEUE_SIZE);
			return;
		}
		if (len > sizeof (m_SendBuffer))
		{
			LogPrint (eLogError, "I2CP: Message to send is too long ", len);
			return;
		}
		memcpy (m_SendBuffer, sendBuf->buf, len);
		m_IsSending = true;
		StartNextI2CPWrite (len);
	}

	void I2CPSession::StartNextI2CPWrite (size_t len)
	{
		auto socket = m_Socket;
		if (!socket)
		{
			m_IsSending = false;
			return;
		}
		boost::asio::async_write (*socket, boost::asio::buffer (m_SendBuffer, len), boost::asio::transfer_all (),
			[s = shared_from_this (), socket] (const boost::system::error_code & ecode, std::size_t bytes_transferred)
			{ s->HandleI2CPMessageSent (ecode, bytes_transferred); });
	}

	void I2CPSession::HandleI2CPMessageSent (const boost::system::error_code & ecode, std::size_t bytes_transferred)
	{
		if (ecode)
		{
			if (ecode != boost::asio::error::operation_aborted)
			{
				// The completion handler runs on the owner thread.
				TerminateInOwnerThread ();
			}
			return;
		}
		std::lock_guard<std::mutex> l (m_SendMutex);
		if (m_Terminated.load () || !m_Socket)
		{
			m_IsSending = false;
			return;
		}
		if (!m_SendQueue.IsEmpty ())
		{
			auto len = m_SendQueue.Get (m_SendBuffer, I2CP_MAX_MESSAGE_LENGTH);
			StartNextI2CPWrite (len);
		}
		else
			m_IsSending = false;
	}

	void I2CPSession::GetDateMessageHandler (const uint8_t * buf, size_t len)
	{
		constexpr std::string_view version (I2P_VERSION);
		std::array<uint8_t, version.size () + 8 + 1> payload;
		// set date
		auto ts = i2p::util::GetMillisecondsSinceEpoch ();
		htobe64buf (payload.data (), ts);
		// send our version back
		i2p::util::Mapping::WriteString (version, payload.data () + 8, payload.size () - 8);
		SendI2CPMessage (I2CP_SET_DATE_MESSAGE, payload.data (), payload.size ());
	}

	void I2CPSession::CreateSessionMessageHandler (const uint8_t * buf, size_t len)
	{
		// A second CREATE SESSION must not touch the identifier of the session
		// that is already inserted. Reject it before any local state changes.
		if (m_Destination)
		{
			LogPrint (eLogError, "I2CP: Session already exists");
			SendSessionStatusMessage (eI2CPSessionStatusRefused); // refused
			return;
		}
		auto identity = std::make_shared<i2p::data::IdentityEx> ();
		size_t offset = identity->FromBuffer (buf, len);
		if (!offset)
		{
			LogPrint (eLogError, "I2CP: Create session malformed identity");
			SendSessionStatusMessage (eI2CPSessionStatusInvalid); // invalid
			return;
		}
		if (m_Owner.FindSessionByIdentHash (identity->GetIdentHash ()))
		{
			LogPrint (eLogError, "I2CP: Create session duplicate address ", identity->GetIdentHash ().ToBase32 ());
			SendSessionStatusMessage (eI2CPSessionStatusInvalid); // invalid
			return;
		}
		if (offset + 2 > len)
		{
			LogPrint (eLogError, "I2CP: Create session message is too short");
			SendSessionStatusMessage (eI2CPSessionStatusInvalid); // invalid
			return;
		}
		uint16_t optionsSize = bufbe16toh (buf + offset);
		offset += 2;
		if (optionsSize > len - offset)
		{
			LogPrint (eLogError, "I2CP: Options size ", optionsSize, "exceeds message size");
			SendSessionStatusMessage (eI2CPSessionStatusInvalid); // invalid
			return;
		}
		i2p::util::Mapping params;
		params.FromBuffer (optionsSize, buf + offset, len - offset);
		offset += optionsSize; // options
		bool isSendAccepted = params[I2CP_PARAM_MESSAGE_RELIABILITY] != "none";

		if (offset + 8 > len)
		{
			LogPrint (eLogError, "I2CP: Create session message is too short for date");
			SendSessionStatusMessage (eI2CPSessionStatusInvalid); // invalid
			return;
		}
		uint64_t creationDate = bufbe64toh (buf + offset);
		int64_t skew = (int64_t)creationDate - (int64_t)i2p::util::GetMillisecondsSinceEpoch ();
		// SessionConfig.tooOld(): Java uses +/- 3 minutes here (the spec's
		// +/- 30 seconds is stricter than the reference implementation).
		if (skew > 3 * 60 * 1000 || skew < -3 * 60 * 1000)
		{
			LogPrint (eLogError, "I2CP: Create session date is outside the accepted window, skew=", skew);
			SendSessionStatusMessage (eI2CPSessionStatusInvalid); // invalid
			return;
		}
		offset += 8; // date

		// SessionConfig.verifySignature(): an offline-signed session carries the
		// three leaseSetOffline* options in the mapping and its Session Config
		// signature is made by the transient key.
		std::unique_ptr<i2p::crypto::Verifier> transientVerifier;
		const auto & expirationStr = params["i2cp.leaseSetOfflineExpiration"];
		std::string_view transientKeyStr = params["i2cp.leaseSetTransientPublicKey"];
		const auto & offlineSigStr = params["i2cp.leaseSetOfflineSignature"];
		if (!expirationStr.empty () || !transientKeyStr.empty () || !offlineSigStr.empty ())
		{
			bool offlineOk = !expirationStr.empty () && !transientKeyStr.empty () && !offlineSigStr.empty ();
			uint64_t expirationSeconds = 0;
			if (offlineOk)
			{
				auto res = std::from_chars (
					expirationStr.data (), expirationStr.data () + expirationStr.size (), expirationSeconds);
				offlineOk = res.ec == std::errc () && res.ptr == expirationStr.data () + expirationStr.size ()
					&& expirationSeconds <= UINT32_MAX && expirationSeconds <= UINT64_MAX / 1000;
			}
			uint16_t transientType = i2p::data::SIGNING_KEY_TYPE_DSA_SHA1;
			if (offlineOk)
			{
				auto colon = transientKeyStr.find (':');
				if (colon != std::string_view::npos)
				{
					auto typeStr = transientKeyStr.substr (0, colon);
					auto res = std::from_chars (typeStr.data (), typeStr.data () + typeStr.size (), transientType);
					offlineOk = res.ec == std::errc () && res.ptr == typeStr.data () + typeStr.size ();
					transientKeyStr.remove_prefix (colon + 1);
				}
			}
			if (offlineOk) transientVerifier.reset (i2p::data::IdentityEx::CreateVerifier (transientType));
			if (offlineOk && transientVerifier)
			{
				auto keyLen = transientVerifier->GetPublicKeyLen ();
				std::vector<uint8_t> transientKey (keyLen);
				offlineOk = keyLen > 0
					&& i2p::data::Base64ToByteStream (transientKeyStr, transientKey.data (), keyLen) == keyLen;
				if (offlineOk)
				{
					transientVerifier->SetPublicKey (transientKey.data ());
					std::vector<uint8_t> offlineSignature (identity->GetSignatureLen ());
					offlineOk = i2p::data::Base64ToByteStream (
									offlineSigStr, offlineSignature.data (), offlineSignature.size ())
						== offlineSignature.size ();
					if (offlineOk)
					{
						std::vector<uint8_t> signedData (6 + keyLen);
						htobe32buf (signedData.data (), (uint32_t)expirationSeconds);
						htobe16buf (signedData.data () + 4, transientType);
						memcpy (signedData.data () + 6, transientKey.data (), keyLen);
						offlineOk = identity->Verify (signedData.data (), signedData.size (), offlineSignature.data ());
						// Same check as SessionConfig.verifySignature(): the
						// delegation must not expire before the config date.
						if (offlineOk && expirationSeconds * 1000 < creationDate) offlineOk = false;
					}
				}
			}
			else
				offlineOk = false;
			if (!offlineOk)
			{
				LogPrint (eLogError, "I2CP: Invalid offline session config");
				SendSessionStatusMessage (eI2CPSessionStatusInvalid); // invalid
				return;
			}
		}

		auto signatureLen = transientVerifier ? transientVerifier->GetSignatureLen () : identity->GetSignatureLen ();
		if (offset + signatureLen > len)
		{
			LogPrint (eLogError, "I2CP: Create session message is too short for signature");
			SendSessionStatusMessage (eI2CPSessionStatusInvalid); // invalid
			return;
		}
		bool signatureValid = transientVerifier ? transientVerifier->Verify (buf, offset, buf + offset)
												: identity->Verify (buf, offset, buf + offset);
		if (signatureValid)
		{
			auto destination = m_Owner.IsSingleThread ()
				? std::make_shared<I2CPDestination> (
					  m_Owner.GetService (), shared_from_this (), identity, true, true, params)
				: std::make_shared<RunnableI2CPDestination> (shared_from_this (), identity, true, params);
			// Reserve a fresh, non-sentinel identifier and insert atomically.
			// The local variable is never written to m_SessionID until
			// InsertSession has accepted it.
			bool inserted = false;
			for (int attempt = 0; attempt < 32 && !inserted; attempt++)
			{
				uint16_t sessionID = 0xFFFF;
				if (RAND_bytes ((uint8_t *)&sessionID, sizeof (sessionID)) != 1) break;
				if (sessionID == 0xFFFF) continue; // reserved
				inserted = m_Owner.InsertSession (shared_from_this (), sessionID);
			}
			if (inserted)
			{
				m_Destination = destination;
				m_IsSendAccepted.store (isSendAccepted);
				LogPrint (eLogDebug, "I2CP: Session ", GetSessionID (), " created");
				m_Destination->Start ();
				SendSessionStatusMessage (eI2CPSessionStatusCreated); // created
			}
			else
			{
				// The failed candidate never became visible and no identifier
				// was assigned to this session.
				destination->Stop ();
				LogPrint (eLogError, "I2CP: Session could not be inserted");
				SendSessionStatusMessage (eI2CPSessionStatusRefused);
			}
		}
		else
		{
			LogPrint (eLogError, "I2CP: Create session signature verification failed");
			SendSessionStatusMessage (eI2CPSessionStatusInvalid); // invalid
		}
	}

	void I2CPSession::DestroySessionMessageHandler (const uint8_t * buf, size_t len)
	{
		SendSessionStatusMessage (eI2CPSessionStatusDestroyed); // destroy
		LogPrint (eLogDebug, "I2CP: Session ", m_SessionID.load (), " destroyed");
		Terminate ();
	}

	void I2CPSession::ReconfigureSessionMessageHandler (const uint8_t * buf, size_t len)
	{
		I2CPSessionStatus status = eI2CPSessionStatusInvalid; // rejected
		if (len > sizeof (uint16_t))
		{
			uint16_t sessionID = bufbe16toh (buf);
			if (sessionID == m_SessionID.load () && m_Destination)
			{
				buf += sizeof (uint16_t);
				const uint8_t * body = buf;
				i2p::data::IdentityEx ident;
				if (ident.FromBuffer (buf, len - sizeof (uint16_t)))
				{
					if (ident == *m_Destination->GetIdentity ())
					{
						size_t identsz = ident.GetFullLen ();
						buf += identsz;
						if (sizeof (uint16_t) + identsz + sizeof (uint16_t) <= len)
						{
							uint16_t optssize = bufbe16toh (buf);
							if (sizeof (uint16_t) + identsz + sizeof (uint16_t) + (size_t)optssize + sizeof (uint64_t)
									+ ident.GetSignatureLen ()
								<= len)
							{
								buf += sizeof (uint16_t);
								i2p::util::Mapping opts;
								opts.FromBuffer (optssize, buf, optssize);
								buf += optssize;
								//uint64_t date = bufbe64toh(buf);
								buf += sizeof (uint64_t);
								const uint8_t * sig = buf;
								if (ident.Verify (body, len - sizeof (uint16_t) - ident.GetSignatureLen (), sig))
								{
									if (m_Destination->Reconfigure (opts))
									{
										LogPrint (eLogInfo, "I2CP: Reconfigured destination");
										status = eI2CPSessionStatusUpdated; // updated
									}
									else
										LogPrint (eLogWarning, "I2CP: Failed to reconfigure destination");
								}
								else
									LogPrint (eLogError, "I2CP: Invalid reconfigure message signature");
							}
							else
								LogPrint (eLogError, "I2CP: Mapping size mismatch");
						}
						else
							LogPrint (eLogError, "I2CP: Reconfigure message is too short");
					}
					else
						LogPrint (eLogError, "I2CP: Destination mismatch");
				}
				else
					LogPrint (eLogError, "I2CP: Malfromed destination");
			}
			else
				LogPrint (eLogError, "I2CP: Session mismatch");
		}
		else
			LogPrint (eLogError, "I2CP: Short message");
		SendSessionStatusMessage (status);
	}

	void I2CPSession::SendSessionStatusMessage (I2CPSessionStatus status)
	{
		uint8_t buf[3];
		htobe16buf (buf, m_SessionID.load ());
		buf[2] = (uint8_t)status;
		SendI2CPMessage (I2CP_SESSION_STATUS_MESSAGE, buf, 3);
	}

	void I2CPSession::SendMessageStatusMessage (uint32_t nonce, I2CPMessageStatus status)
	{
		if (!nonce) return; // don't send status with zero nonce
		uint8_t buf[15];
		htobe16buf (buf, m_SessionID.load ());
		htobe32buf (buf + 2, m_MessageID.fetch_add (1));
		buf[6] = (uint8_t)status;
		memset (buf + 7, 0, 4); // size
		htobe32buf (buf + 11, nonce);
		SendI2CPMessage (I2CP_MESSAGE_STATUS_MESSAGE, buf, 15);
	}

	void I2CPSession::AddRoutingSession (
		const i2p::data::IdentHash & signingKey, std::shared_ptr<i2p::garlic::GarlicRoutingSession> remoteSession)
	{
		if (!remoteSession) return;
		remoteSession->SetAckRequestInterval (I2CP_SESSION_ACK_REQUEST_INTERVAL);
		std::lock_guard<std::mutex> l (m_RoutingSessionsMutex);
		m_RoutingSessions[signingKey] = remoteSession;
	}

	void I2CPSession::CleanupRoutingSessions ()
	{
		std::lock_guard<std::mutex> l (m_RoutingSessionsMutex);
		for (auto it = m_RoutingSessions.begin (); it != m_RoutingSessions.end ();)
		{
			if (it->second->IsTerminated ())
				it = m_RoutingSessions.erase (it);
			else
				it++;
		}
	}

	void I2CPSession::CreateLeaseSetMessageHandler (const uint8_t * buf, size_t len)
	{
		size_t offset = 2 + i2p::crypto::DSA_PRIVATE_KEY_LENGTH + 256;
		if (len < offset)
		{
			LogPrint (eLogError, "I2CP: CreateLeaseSetMessage is too short ", len);
			return;
		}
		uint16_t sessionID = bufbe16toh (buf);
		if (sessionID == m_SessionID.load ())
		{
			if (m_Destination)
			{
				// we always assume this field as 20 bytes (DSA) regardless actual size
				// instead of
				//offset += m_Destination->GetIdentity ()->GetSigningPrivateKeyLen ();
				m_Destination->SetEncryptionPrivateKey (
					i2p::data::CRYPTO_KEY_TYPE_ELGAMAL, buf + 2 + i2p::crypto::DSA_PRIVATE_KEY_LENGTH);
				m_Destination->LeaseSetCreated (buf + offset, len - offset);
			}
		}
		else
			LogPrint (eLogError, "I2CP: Unexpected sessionID ", sessionID);
	}

	void I2CPSession::CreateLeaseSet2MessageHandler (const uint8_t * buf, size_t len)
	{
		if (len < 3)
		{
			LogPrint (eLogError, "I2CP: CreateLeaseSet2Message is too short ", len);
			return;
		}
		uint16_t sessionID = bufbe16toh (buf);
		if (sessionID == m_SessionID.load ())
		{
			size_t offset = 2;
			if (m_Destination)
			{
				uint8_t storeType = buf[offset];
				offset++; // store type
				i2p::data::LeaseSet2 ls (storeType, buf + offset, len - offset); // outer layer only for encrypted
				if (!ls.IsValid ())
				{
					LogPrint (eLogError, "I2CP: Invalid LeaseSet2 of type ", storeType);
					return;
				}
				offset += ls.GetBufferLen ();
				// private keys
				if (offset >= len)
				{
					LogPrint (eLogError, "I2CP: CreateLeaseSet2Message is too short for private keys");
					return;
				}
				int numPrivateKeys = buf[offset];
				offset++;
				for (int i = 0; i < numPrivateKeys; i++)
				{
					if (offset + 4 > len) return;
					uint16_t keyType = bufbe16toh (buf + offset);
					offset += 2; // encryption type
					uint16_t keyLen = bufbe16toh (buf + offset);
					offset += 2; // private key length
					if (offset + keyLen > len) return;
					if (keyType >= i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD)
						m_Destination->SetECIESx25519EncryptionPrivateKey (keyType, buf + offset);
					else
						m_Destination->SetEncryptionPrivateKey (keyType, buf + offset); // from identity
					offset += keyLen;
				}

				m_Destination->LeaseSet2Created (storeType, ls.GetBuffer (), ls.GetBufferLen ());
			}
		}
		else
			LogPrint (eLogError, "I2CP: Unexpected sessionID ", sessionID);
	}

	void I2CPSession::SendMessageMessageHandler (const uint8_t * buf, size_t len)
	{
		if (len < 2)
		{
			LogPrint (eLogError, "I2CP: Send message is too short");
			return;
		}
		uint16_t sessionID = bufbe16toh (buf);
		if (sessionID == m_SessionID.load ())
		{
			size_t offset = 2;
			if (m_Destination)
			{
				const uint8_t * ident = buf + offset;
				size_t identSize = i2p::data::GetIdentityBufferLen (ident, len - offset);
				if (identSize && offset <= len && identSize <= len - offset && len - offset - identSize >= 4)
				{
					offset += identSize;
					uint32_t payloadLen = bufbe32toh (buf + offset);
					// Subtraction form: a 32-bit size_t must not wrap when adding a
					// hostile 32-bit payload length to the offset plus framing.
					if (offset <= len && len - offset >= 8 && (size_t)payloadLen <= len - offset - 8)
					{
						offset += 4;
						uint32_t nonce = bufbe32toh (buf + offset + payloadLen);
						if (m_Destination->IsReady ())
						{
							if (m_IsSendAccepted.load ())
								SendMessageStatusMessage (nonce, eI2CPMessageStatusAccepted); // accepted
							std::shared_ptr<i2p::garlic::GarlicRoutingSession> remoteSession;
							{
								std::lock_guard<std::mutex> l (m_RoutingSessionsMutex);
								auto it = m_RoutingSessions.find (
									ident + i2p::data::DEFAULT_IDENTITY_SIZE - 35); // 32 bytes signing key
								if (it != m_RoutingSessions.end ())
								{
									if (!it->second->IsTerminated () && it->second->IsReadyToSend ())
										remoteSession = it->second;
									else
										m_RoutingSessions.erase (it);
								}
							}
							if (!remoteSession
								|| !m_Destination->SendMsg (buf + offset, payloadLen, remoteSession, nonce))
							{
								i2p::data::IdentHash identHash;
								SHA256 (ident, identSize,
									identHash); // calculate ident hash, because we don't need full identity
								m_Destination->SendMsgTo (buf + offset, payloadLen, identHash, nonce);
							}
						}
						else
						{
							LogPrint (eLogInfo, "I2CP: Destination is not ready");
							SendMessageStatusMessage (nonce, eI2CPMessageStatusNoLocalTunnels);
						}
					}
					else
						LogPrint (eLogError, "I2CP: Cannot send message, too big");
				}
				else
					LogPrint (eLogError, "I2CP: Invalid identity");
			}
		}
		else
			LogPrint (eLogError, "I2CP: Unexpected sessionID ", sessionID);
	}

	void I2CPSession::SendMessageExpiresMessageHandler (const uint8_t * buf, size_t len)
	{
		if (len < 8)
		{
			LogPrint (eLogError, "I2CP: Send message expires is too short");
			return;
		}
		SendMessageMessageHandler (buf, len - 8); // ignore flags(2) and expiration(6)
	}

	void I2CPSession::HostLookupMessageHandler (const uint8_t * buf, size_t len)
	{
		if (len < 11)
		{
			LogPrint (eLogError, "I2CP: HostLookup message is too short ", len);
			return;
		}
		uint16_t sessionID = bufbe16toh (buf);
		if (sessionID == m_SessionID.load () || sessionID == 0xFFFF) // -1 means without session
		{
			uint32_t requestID = bufbe32toh (buf + 2);
			//uint32_t timeout = bufbe32toh (buf + 6);
			i2p::data::IdentHash ident;
			std::shared_ptr<const i2p::data::BlindedPublicKey> blinded;
			std::shared_ptr<const i2p::data::IdentityEx> associated;
			uint8_t lookupType = buf[10];
			// Proposal 167: types 2-4 request the LeaseSet options mapping.
			bool requestOptions = lookupType == 2 || lookupType == 3 || lookupType == 4;
			switch (lookupType)
			{
			case 0: // hash
			case 2: // hash + LS2 options
				if (len < 43)
				{
					LogPrint (eLogError, "I2CP: HostLookup hash is truncated");
					SendHostReplyMessage (requestID, sessionID, nullptr);
					return;
				}
				ident = i2p::data::IdentHash (buf + 11);
				break;
			case 1: // address
			case 3: // address + LS2 options
			{
				// DataHelper.readString() uses a 1-byte length; reject truncation
				// instead of clamping like Mapping::ExtractString() does.
				if (len < 13)
				{
					LogPrint (eLogError, "I2CP: Address lookup is truncated");
					SendHostReplyMessage (requestID, sessionID, nullptr);
					return;
				}
				uint8_t nameLen = buf[11];
				if (!nameLen || 12 + (size_t)nameLen > len)
				{
					LogPrint (eLogError, "I2CP: Address lookup is malformed");
					SendHostReplyMessage (requestID, sessionID, nullptr);
					return;
				}
				std::string_view name ((const char *)buf + 12, nameLen);
				auto addr = i2p::client::context.GetAddressBook ().GetAddress (name);
				if (!addr)
				{
					LogPrint (eLogError, "I2CP: Address ", name, " not found");
					SendHostReplyMessage (requestID, sessionID, nullptr);
					return;
				}
				if (addr->IsIdentHash ())
					ident = addr->identHash;
				else if (addr->blindedPublicKey)
				{
					blinded = addr->blindedPublicKey;
					// A matching BlindingInfo associates the b33 with its
					// unblinded destination; use it to validate the reply.
					associated = FindBlindedDestination (*blinded);
				}
				else
				{
					LogPrint (eLogError, "I2CP: Address ", name, " has no destination");
					SendHostReplyMessage (requestID, sessionID, nullptr);
					return;
				}
				break;
			}
			case 4: // Destination + LS2 options
			{
				i2p::data::IdentityEx identity;
				if (!identity.FromBuffer (buf + 11, len - 11))
				{
					LogPrint (eLogError, "I2CP: HostLookup destination is malformed");
					SendHostReplyMessage (requestID, sessionID, nullptr);
					return;
				}
				ident = identity.GetIdentHash ();
				break;
			}
			default:
				LogPrint (eLogError, "I2CP: Request type ", (int)lookupType, " is not supported");
				SendHostReplyMessage (requestID, sessionID, nullptr);
				return;
			}

			std::shared_ptr<LeaseSetDestination> destination = m_Destination;
			if (!destination) destination = i2p::client::context.GetSharedLocalDestination ();
			if (destination)
			{
				std::shared_ptr<i2p::data::LeaseSet> ls;
				if (!blinded) ls = destination->FindLeaseSet (ident);
				if (ls)
					SendHostReplyMessage (requestID, sessionID, ls->GetIdentity (), requestOptions, ls);
				else
				{
					auto s = shared_from_this ();
					auto complete = [s, requestID, sessionID, associated, requestOptions] (
										std::shared_ptr<i2p::data::LeaseSet> leaseSet)
					{
						if (leaseSet && associated
							&& leaseSet->GetIdentity ()->GetIdentHash () != associated->GetIdentHash ())
						{
							LogPrint (eLogError, "I2CP: Blinded LeaseSet identity does not match BlindingInfo for ",
								associated->GetIdentHash ().ToBase32 ());
							leaseSet = nullptr;
						}
						s->SendHostReplyMessage (requestID, sessionID, leaseSet ? leaseSet->GetIdentity () : nullptr,
							requestOptions, std::move (leaseSet));
					};
					if (blinded)
						destination->RequestDestinationWithEncryptedLeaseSet (blinded, complete);
					else
						destination->RequestDestination (ident, complete);
				}
			}
			else
				SendHostReplyMessage (requestID, sessionID, nullptr);
		}
		else
			LogPrint (eLogError, "I2CP: Unexpected sessionID ", sessionID);
	}

	void I2CPSession::BlindingInfoMessageHandler (const uint8_t * buf, size_t len)
	{
		// BlindingInfoMessage.doReadMessage(): sessionId(2), flags(1),
		// endpointType(1), blindType(2), expiration seconds(4), endpoint,
		// optional 32-byte auth private key, optional 1-byte-length secret.
		constexpr uint8_t FLAG_AUTH = 0x0f;
		constexpr uint8_t FLAG_SECRET = 0x10;
		constexpr uint8_t TYPE_HASH = 0;
		constexpr uint8_t TYPE_HOST = 1;
		constexpr uint8_t TYPE_DEST = 2;
		constexpr uint8_t TYPE_KEY = 3;
		if (len < 10)
		{
			LogPrint (eLogError, "I2CP: BlindingInfo message is too short ", len);
			Terminate ();
			return;
		}
		size_t offset = 0;
		uint16_t sessionID = bufbe16toh (buf + offset);
		offset += 2;
		if (sessionID != m_SessionID.load () && sessionID != 0xFFFF)
		{
			LogPrint (eLogWarning, "I2CP: BlindingInfo for unexpected session ", sessionID);
			return;
		}
		uint8_t flags = buf[offset++];
		uint8_t authType = flags & FLAG_AUTH;
		if (authType != 0 && authType != 1 && authType != 3) // none, DH, PSK
		{
			LogPrint (eLogError, "I2CP: BlindingInfo bad auth type ", (int)authType);
			Terminate ();
			return;
		}
		uint8_t endpointType = buf[offset++];
		uint16_t blindType = bufbe16toh (buf + offset);
		offset += 2;
		{
			std::unique_ptr<i2p::crypto::Verifier> verifier (
				i2p::data::IdentityEx::CreateVerifier ((i2p::data::SigningKeyType)blindType));
			if (!verifier)
			{
				LogPrint (eLogError, "I2CP: BlindingInfo unsupported blind type ", blindType);
				Terminate ();
				return;
			}
		}
		if (offset + 4 > len)
		{
			LogPrint (eLogError, "I2CP: BlindingInfo message is truncated");
			Terminate ();
			return;
		}
		uint64_t expiration = (uint64_t)bufbe32toh (buf + offset) * 1000; // milliseconds
		offset += 4;

		BlindingInfo info;
		info.blindType = (i2p::data::SigningKeyType)blindType;
		info.expiration = expiration;
		BlindingInfoKey key;
		std::string host;
		switch (endpointType)
		{
		case TYPE_HASH:
			if (offset + 32 > len)
			{
				LogPrint (eLogError, "I2CP: BlindingInfo hash is truncated");
				Terminate ();
				return;
			}
			offset += 32;
			break;
		case TYPE_HOST:
		{
			if (offset + 1 > len)
			{
				LogPrint (eLogError, "I2CP: BlindingInfo host is truncated");
				Terminate ();
				return;
			}
			uint8_t hostLen = buf[offset++];
			if (!hostLen || offset + hostLen > len)
			{
				LogPrint (eLogError, "I2CP: BlindingInfo host is malformed");
				Terminate ();
				return;
			}
			host.assign ((const char *)buf + offset, hostLen);
			offset += hostLen;
			break;
		}
		case TYPE_DEST:
		{
			auto identity = std::make_shared<i2p::data::IdentityEx> ();
			size_t identityLen = identity->FromBuffer (buf + offset, len - offset);
			if (!identityLen || offset + identityLen > len)
			{
				LogPrint (eLogError, "I2CP: BlindingInfo destination is malformed");
				Terminate ();
				return;
			}
			auto identityKey = identity->GetSigningPublicKeyBuffer ();
			auto identityKeyLen = identity->GetSigningPublicKeyLen ();
			if (!identityKey || !identityKeyLen)
			{
				LogPrint (eLogError, "I2CP: BlindingInfo destination has no signing public key");
				Terminate ();
				return;
			}
			key = MakeBlindingInfoKey (identity->GetSigningKeyType (), identityKey, identityKeyLen);
			info.identity = identity;
			offset += identityLen;
			break;
		}
		case TYPE_KEY:
		{
			if (offset + 2 > len)
			{
				LogPrint (eLogError, "I2CP: BlindingInfo key type is truncated");
				Terminate ();
				return;
			}
			uint16_t keyType = bufbe16toh (buf + offset);
			offset += 2;
			std::unique_ptr<i2p::crypto::Verifier> verifier (
				i2p::data::IdentityEx::CreateVerifier ((i2p::data::SigningKeyType)keyType));
			if (!verifier)
			{
				LogPrint (eLogError, "I2CP: BlindingInfo unsupported endpoint key type ", keyType);
				Terminate ();
				return;
			}
			auto keyLen = verifier->GetPublicKeyLen ();
			if (!keyLen || offset + keyLen > len)
			{
				LogPrint (eLogError, "I2CP: BlindingInfo key is truncated");
				Terminate ();
				return;
			}
			key = MakeBlindingInfoKey ((i2p::data::SigningKeyType)keyType, buf + offset, keyLen);
			offset += keyLen;
			break;
		}
		default:
			LogPrint (eLogError, "I2CP: BlindingInfo bad endpoint type ", (int)endpointType);
			Terminate ();
			return;
		}
		info.authType = authType;
		if (authType != 0)
		{
			if (offset + 32 > len)
			{
				LogPrint (eLogError, "I2CP: BlindingInfo auth key is truncated");
				Terminate ();
				return;
			}
			info.authPrivateKey.assign (buf + offset, buf + offset + 32);
			offset += 32;
		}
		if (flags & FLAG_SECRET)
		{
			if (offset + 1 > len)
			{
				LogPrint (eLogError, "I2CP: BlindingInfo secret is truncated");
				Terminate ();
				return;
			}
			uint8_t secretLen = buf[offset++];
			if (offset + secretLen > len)
			{
				LogPrint (eLogError, "I2CP: BlindingInfo secret is malformed");
				Terminate ();
				return;
			}
			info.secret.assign ((const char *)buf + offset, secretLen);
			offset += secretLen;
		}
		if (offset != len)
		{
			LogPrint (eLogError, "I2CP: BlindingInfo has ", len - offset, " trailing bytes");
			Terminate ();
			return;
		}
		if (endpointType == TYPE_HASH || endpointType == TYPE_HOST)
		{
			// Java's handleBlindingInfo() cannot use these either.
			LogPrint (eLogWarning, "I2CP: BlindingInfo endpoint type ", (int)endpointType, " is not supported",
				endpointType == TYPE_HOST ? (": " + host) : "");
			return;
		}
		LogPrint (eLogDebug, "I2CP: BlindingInfo for ",
			info.identity ? info.identity->GetIdentHash ().ToBase32 () : "a signing key", ", auth type ", (int)authType,
			", expires ", expiration);
		StoreBlindingInfo (std::move (key), std::move (info));
	}

	void I2CPSession::SendHostReplyMessage (uint32_t requestID, uint16_t sessionID,
		std::shared_ptr<const i2p::data::IdentityEx> identity, bool withOptions,
		std::shared_ptr<const i2p::data::LeaseSet> leaseSet)
	{
		if (identity)
		{
			std::vector<uint8_t> options;
			if (withOptions)
			{
				// Proposal 167: types 2-4 return the standard LeaseSet2 options
				// mapping when available, otherwise an empty Mapping.
				if (leaseSet && leaseSet->GetStoreType () == i2p::data::NETDB_STORE_TYPE_STANDARD_LEASESET2)
				{
					const auto & properties
						= static_cast<const i2p::data::LeaseSet2 *> (leaseSet.get ())->GetProperties ();
					if (!properties.IsEmpty ())
					{
						options.resize (4096);
						options.resize (properties.ToBuffer (options.data (), options.size ()));
					}
				}
				if (options.empty ()) options = { 0, 0 };
			}
			size_t l = identity->GetFullLen () + 7 + options.size ();
			std::vector<uint8_t> buf (l);
			htobe16buf (buf.data (), sessionID);
			htobe32buf (buf.data () + 2, requestID);
			buf[6] = 0; // result code
			identity->ToBuffer (buf.data () + 7, identity->GetFullLen ());
			if (!options.empty ()) memcpy (buf.data () + 7 + identity->GetFullLen (), options.data (), options.size ());
			SendI2CPMessage (I2CP_HOST_REPLY_MESSAGE, buf.data (), l);
		}
		else
		{
			uint8_t buf[7];
			htobe16buf (buf, sessionID);
			htobe32buf (buf + 2, requestID);
			buf[6] = 1; // result code
			SendI2CPMessage (I2CP_HOST_REPLY_MESSAGE, buf, 7);
		}
	}

	void I2CPSession::DestLookupMessageHandler (const uint8_t * buf, size_t len)
	{
		if (len < 32)
		{
			LogPrint (eLogError, "I2CP: DestLookupMessage is too short ", len);
			return;
		}
		if (m_Destination)
		{
			auto ls = m_Destination->FindLeaseSet (buf);
			if (ls)
			{
				auto l = ls->GetIdentity ()->GetFullLen ();
				std::vector<uint8_t> identBuf (l);
				ls->GetIdentity ()->ToBuffer (identBuf.data (), l);
				SendI2CPMessage (I2CP_DEST_REPLY_MESSAGE, identBuf.data (), l);
			}
			else
			{
				auto s = shared_from_this ();
				i2p::data::IdentHash ident (buf);
				m_Destination->RequestDestination (ident,
					[s, ident] (std::shared_ptr<i2p::data::LeaseSet> leaseSet)
					{
						if (leaseSet) // found
						{
							auto l = leaseSet->GetIdentity ()->GetFullLen ();
							std::vector<uint8_t> identBuf (l);
							leaseSet->GetIdentity ()->ToBuffer (identBuf.data (), l);
							s->SendI2CPMessage (I2CP_DEST_REPLY_MESSAGE, identBuf.data (), l);
						}
						else
							s->SendI2CPMessage (I2CP_DEST_REPLY_MESSAGE, ident, 32); // not found
					});
			}
		}
		else
			SendI2CPMessage (I2CP_DEST_REPLY_MESSAGE, buf, 32);
	}

	void I2CPSession::GetBandwidthLimitsMessageHandler (const uint8_t * buf, size_t len)
	{
		uint8_t limits[64];
		memset (limits, 0, 64);
		uint32_t limit;
		i2p::config::GetOption ("i2cp.inboundlimit", limit);
		if (!limit) limit = i2p::context.GetBandwidthLimit ();
		htobe32buf (limits, limit); // inbound
		i2p::config::GetOption ("i2cp.outboundlimit", limit);
		if (!limit) limit = i2p::context.GetBandwidthLimit ();
		htobe32buf (limits + 4, limit); // outbound
		SendI2CPMessage (I2CP_BANDWIDTH_LIMITS_MESSAGE, limits, 64);
	}

	void I2CPSession::SendMessagePayloadMessage (const uint8_t * payload, size_t len)
	{
		// we don't use SendI2CPMessage to eliminate additional copy
		auto l = len + 10 + I2CP_HEADER_SIZE;
		if (l > I2CP_MAX_MESSAGE_LENGTH)
		{
			LogPrint (eLogError, "I2CP: Message to send is too long ", l);
			return;
		}
		auto sendBuf = std::make_shared<i2p::stream::SendBuffer> (l);
		uint8_t * buf = sendBuf->buf;
		htobe32buf (buf + I2CP_HEADER_LENGTH_OFFSET, len + 10);
		buf[I2CP_HEADER_TYPE_OFFSET] = I2CP_MESSAGE_PAYLOAD_MESSAGE;
		htobe16buf (buf + I2CP_HEADER_SIZE, m_SessionID.load ());
		htobe32buf (buf + I2CP_HEADER_SIZE + 2, m_MessageID.fetch_add (1));
		htobe32buf (buf + I2CP_HEADER_SIZE + 6, len);
		if (len) memcpy (buf + I2CP_HEADER_SIZE + 10, payload, len);

		if (m_Owner.IsInServiceThread ())
			QueueI2CPMessage (std::move (sendBuf), l);
		else
		{
			auto s = shared_from_this ();
			boost::asio::post (m_Owner.GetService (),
				[s, sendBuf = std::move (sendBuf), l] () mutable { s->QueueI2CPMessage (std::move (sendBuf), l); });
		}
	}

	I2CPServer::I2CPServer (const std::string & interface, uint16_t port, bool isSingleThread)
		: RunnableService ("I2CP")
		, m_IsSingleThread (isSingleThread)
		, m_Acceptor (GetIOService (), boost::asio::ip::tcp::endpoint (boost::asio::ip::make_address (interface), port))
	{
		memset (m_MessagesHandlers, 0, sizeof (m_MessagesHandlers));
		m_MessagesHandlers[I2CP_GET_DATE_MESSAGE] = &I2CPSession::GetDateMessageHandler;
		m_MessagesHandlers[I2CP_CREATE_SESSION_MESSAGE] = &I2CPSession::CreateSessionMessageHandler;
		m_MessagesHandlers[I2CP_DESTROY_SESSION_MESSAGE] = &I2CPSession::DestroySessionMessageHandler;
		m_MessagesHandlers[I2CP_RECONFIGURE_SESSION_MESSAGE] = &I2CPSession::ReconfigureSessionMessageHandler;
		m_MessagesHandlers[I2CP_CREATE_LEASESET_MESSAGE] = &I2CPSession::CreateLeaseSetMessageHandler;
		m_MessagesHandlers[I2CP_CREATE_LEASESET2_MESSAGE] = &I2CPSession::CreateLeaseSet2MessageHandler;
		m_MessagesHandlers[I2CP_SEND_MESSAGE_MESSAGE] = &I2CPSession::SendMessageMessageHandler;
		m_MessagesHandlers[I2CP_SEND_MESSAGE_EXPIRES_MESSAGE] = &I2CPSession::SendMessageExpiresMessageHandler;
		m_MessagesHandlers[I2CP_HOST_LOOKUP_MESSAGE] = &I2CPSession::HostLookupMessageHandler;
		m_MessagesHandlers[I2CP_DEST_LOOKUP_MESSAGE] = &I2CPSession::DestLookupMessageHandler;
		m_MessagesHandlers[I2CP_BLINDING_INFO_MESSAGE] = &I2CPSession::BlindingInfoMessageHandler;
		m_MessagesHandlers[I2CP_GET_BANDWIDTH_LIMITS_MESSAGE] = &I2CPSession::GetBandwidthLimitsMessageHandler;
	}

	I2CPServer::~I2CPServer ()
	{
		if (IsRunning ()) Stop ();
	}

	void I2CPServer::Start ()
	{
		// Record the owner thread before any handler can ask for a deferred
		// termination. The posted callback runs first on the new worker.
		boost::asio::post (GetIOService (),
			[this] ()
			{
				std::lock_guard<std::mutex> l (m_ServiceThreadMutex);
				m_ServiceThreadID = std::this_thread::get_id ();
			});
		Accept ();
		StartIOService ();
	}

	void I2CPServer::StopSessions ()
	{
		decltype (m_Sessions) sessions;
		{
			std::lock_guard<std::mutex> l (m_SessionsMutex);
			m_Sessions.swap (sessions);
		}
		for (auto & it : sessions) it.second->Stop ();
	}

	void I2CPServer::Stop ()
	{
		m_Acceptor.cancel ();

		// Session sockets and send queues belong to the service thread. Run the
		// teardown there, then join from this (non-owner) thread. When the
		// server was never started there is no owner thread to marshal to.
		if (IsRunning () && !IsInServiceThread ())
		{
			auto done = std::make_shared<std::promise<void>> ();
			auto future = done->get_future ();
			boost::asio::post (GetIOService (),
				[this, done] ()
				{
					StopSessions ();
					done->set_value ();
				});
			if (future.wait_for (std::chrono::seconds (STOP_DESTINATIONS_TIMEOUT)) != std::future_status::ready)
			{
				LogPrint (eLogError, "I2CP: Service didn't stop sessions in time");
				StopSessions ();
			}
		}
		else
			StopSessions ();

		StopIOService ();
	}

	void I2CPServer::Accept ()
	{
		auto newSocket = std::make_shared<boost::asio::ip::tcp::socket> (GetIOService ());
		m_Acceptor.async_accept (
			*newSocket, std::bind (&I2CPServer::HandleAccept, this, std::placeholders::_1, newSocket));
	}

	void I2CPServer::HandleAccept (
		const boost::system::error_code & ecode, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
	{
		if (!ecode && socket)
		{
			boost::system::error_code ec;
			auto ep = socket->remote_endpoint (ec);
			if (!ec)
			{
				LogPrint (eLogDebug, "I2CP: New connection from ", ep);
				auto session = std::make_shared<I2CPSession> (*this, socket);
				session->Start ();
			}
			else
				LogPrint (eLogError, "I2CP: Incoming connection error ", ec.message ());
		}
		else
			LogPrint (eLogError, "I2CP: Accept error: ", ecode.message ());

		if (ecode != boost::asio::error::operation_aborted) Accept ();
	}

	bool I2CPServer::InsertSession (std::shared_ptr<I2CPSession> session, uint16_t sessionID)
	{
		if (!session || sessionID == 0xFFFF) return false;
		std::lock_guard<std::mutex> l (m_SessionsMutex);
		if (!m_Sessions.emplace (sessionID, session).second)
		{
			LogPrint (eLogError, "I2CP: Duplicate session id ", sessionID);
			return false;
		}
		// Assign only after the collision-free insertion. GetSessionID() and
		// later RemoveSession() therefore agree on the inserted value.
		session->AssignSessionID (sessionID);
		return true;
	}

	void I2CPServer::RemoveSession (uint16_t sessionID)
	{
		std::lock_guard<std::mutex> l (m_SessionsMutex);
		m_Sessions.erase (sessionID);
	}

	std::shared_ptr<I2CPSession> I2CPServer::FindSessionByIdentHash (const i2p::data::IdentHash & ident) const
	{
		std::lock_guard<std::mutex> l (m_SessionsMutex);
		for (const auto & it : m_Sessions)
		{
			if (it.second)
			{
				auto dest = it.second->GetDestination ();
				if (dest && dest->GetIdentHash () == ident) return it.second;
			}
		}
		return nullptr;
	}
}
}
