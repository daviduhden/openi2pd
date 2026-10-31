/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: I2CP.
 *
 * Implements the router-side I2CP server: session handshake,
 * destination creation, lease-set publication and message send/receive
 * over the binary protocol. Session identifiers are validated against
 * collisions, message and send-queue sizes are bounded, payloads are
 * routed through the local destination or garlic sessions, and cached
 * routing sessions are keyed by the parsed identity hash of the wire
 * destination.
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
		// equivalent is bounded and only trusted for the duration announced
		// by the client.
		const size_t I2CP_MAX_BLINDING_INFOS = 64;

		// CreateLeaseSet2Message private-key lengths from Java EncType. The
		// local storage for type 1 is 256 bytes for legacy file
		// compatibility, but the I2CP wire scalar is 32 bytes.
		size_t GetI2CPPrivateKeyLen (uint16_t type)
		{
			switch (type)
			{
			case i2p::data::CRYPTO_KEY_TYPE_ELGAMAL: return 256;
			case i2p::data::CRYPTO_KEY_TYPE_ECIES_P256_SHA256_AES256CBC: return 32;
			case i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD:
			case i2p::data::CRYPTO_KEY_TYPE_ECIES_MLKEM512_X25519_AEAD:
			case i2p::data::CRYPTO_KEY_TYPE_ECIES_MLKEM768_X25519_AEAD:
			case i2p::data::CRYPTO_KEY_TYPE_ECIES_MLKEM1024_X25519_AEAD: return 32;
			default: return 0;
			}
		}

		typedef std::pair<uint16_t, std::vector<uint8_t>> BlindingInfoKey;

		struct BlindingInfoEntry
		{
			I2CPBlindingInfo info;
			uint64_t updated = 0;
		};

		std::mutex g_BlindingInfosMutex;
		std::map<BlindingInfoKey, BlindingInfoEntry> g_BlindingInfos;

		BlindingInfoKey MakeBlindingInfoKey (i2p::data::SigningKeyType keyType, const uint8_t * key, size_t keyLen)
		{
			return { (uint16_t)keyType, std::vector<uint8_t> (key, key + keyLen) };
		}

		// Drops expired records and, once the cap is reached, the least
		// recently updated one. The caller holds g_BlindingInfosMutex.
		void PruneBlindingInfos (uint64_t now)
		{
			for (auto it = g_BlindingInfos.begin (); it != g_BlindingInfos.end ();)
			{
				if (it->second.info.expiration && now > it->second.info.expiration)
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
	} // namespace

	I2CPAdmissionPolicy MakeI2CPAdmissionPolicy (size_t configuredConnections, size_t configuredSessions)
	{
		I2CPAdmissionPolicy policy;
		policy.maxConnections = configuredConnections ? configuredConnections : I2CP_MAX_CONNECTIONS_DEFAULT;
		if (policy.maxConnections > I2CP_MAX_CONNECTIONS_LIMIT) policy.maxConnections = I2CP_MAX_CONNECTIONS_LIMIT;
		policy.maxSessions = configuredSessions ? configuredSessions : I2CP_MAX_SESSIONS_DEFAULT;
		if (policy.maxSessions > I2CP_MAX_SESSIONS_LIMIT) policy.maxSessions = I2CP_MAX_SESSIONS_LIMIT;
		return policy;
	}

	namespace
	{
		// Reads the optional i2cp.maxconnections / i2cp.maxsessions limits.
		// An absent option or a non-numeric value falls back to the default;
		// the hard ceiling is applied by MakeI2CPAdmissionPolicy(). The
		// option is declared as uint32_t in Config.cpp, so the value must be
		// read with the same type: boost::any_cast&lt;int&gt; on it would
		// throw.
		size_t ReadI2CPLimit (const char * name)
		{
			uint32_t configured = 0;
			if (!i2p::config::GetOption (name, configured)) return 0;
			return static_cast<size_t> (configured);
		}

		I2CPAdmissionPolicy ReadI2CPAdmissionPolicy ()
		{
			return MakeI2CPAdmissionPolicy (ReadI2CPLimit ("i2cp.maxconnections"), ReadI2CPLimit ("i2cp.maxsessions"));
		}
	} // namespace

	uint8_t GetI2CPHostReplyResultForAuth (uint8_t authType, bool hasUsableSecret)
	{
		if (hasUsableSecret) return eI2CPHostReplySuccess;
		switch (authType)
		{
		case eI2CPBlindingAuthNone: return eI2CPHostReplySuccess;
		// A PSK-only destination needs the shared secret; a DH destination
		// the per-client private key; DH+PSK needs one of the two.
		case eI2CPBlindingAuthPSK: return eI2CPHostReplySecretRequired;
		case eI2CPBlindingAuthDH: return eI2CPHostReplyKeyRequired;
		case eI2CPBlindingAuthDHAndPSK: return eI2CPHostReplySecretAndKeyRequired;
		default: return eI2CPHostReplyFailure;
		}
	}

	std::vector<uint8_t> BuildI2CPHostReplyPayload (uint32_t requestID, uint16_t sessionID,
		std::shared_ptr<const i2p::data::IdentityEx> identity, const std::vector<uint8_t> & options, uint8_t result)
	{
		// A failure reply never carries an identity: HostReplyMessage writes
		// only sessionId, requestId and the result code in that case.
		if (result != eI2CPHostReplySuccess) identity = nullptr;
		std::vector<uint8_t> buf;
		if (identity)
		{
			const size_t identityLen = identity->GetFullLen ();
			buf.resize (identityLen + 7 + options.size ());
			htobe16buf (buf.data (), sessionID);
			htobe32buf (buf.data () + 2, requestID);
			buf[6] = eI2CPHostReplySuccess;
			identity->ToBuffer (buf.data () + 7, identityLen);
			if (!options.empty ()) memcpy (buf.data () + 7 + identityLen, options.data (), options.size ());
		}
		else
		{
			buf.resize (7);
			htobe16buf (buf.data (), sessionID);
			htobe32buf (buf.data () + 2, requestID);
			// An identity-less reply without an explicit result still reports
			// the generic failure, matching the previous behaviour.
			buf[6] = result != eI2CPHostReplySuccess ? result : (uint8_t)eI2CPHostReplyFailure;
		}
		return buf;
	}

	bool ParseI2CPSendMessage (const uint8_t * buf, size_t len, bool withExpiration, I2CPSendMessageRequest & request)
	{
		request = I2CPSendMessageRequest { };
		if (!buf || len < 2) return false;
		request.sessionID = bufbe16toh (buf);
		size_t offset = 2;
		// GetIdentityBufferLen() rejects a truncated or reserved identity
		// without trusting the advertised length field.
		const size_t identSize = i2p::data::GetIdentityBufferLen (buf + offset, len - offset);
		if (!identSize || identSize > len - offset) return false;
		request.identity = buf + offset;
		request.identityLen = identSize;
		offset += identSize;
		if (len - offset < 4) return false;
		const uint32_t payloadLen = bufbe32toh (buf + offset);
		offset += 4;
		// Subtraction form: a hostile 32-bit length must not wrap the offset.
		if ((size_t)payloadLen > len - offset) return false;
		// A zero-length payload keeps a valid (non-null) pointer: the send
		// path distinguishes "no payload buffer" from "empty payload".
		request.payload = buf + offset;
		request.payloadLen = payloadLen;
		offset += payloadLen;
		// nonce, plus the expires form's flags(2) and expiration(6)
		const size_t trailer = withExpiration ? 4 + 2 + 6 : 4;
		if (len - offset < trailer) return false;
		request.nonce = bufbe32toh (buf + offset);
		request.hasNonce = true;
		offset += 4;
		if (withExpiration)
		{
			request.flags = bufbe16toh (buf + offset);
			offset += 2;
			// 48-bit milliseconds-since-epoch; 0 means "does not expire".
			uint64_t expiration = 0;
			for (int i = 0; i < 6; i++) expiration = (expiration << 8) | buf[offset + i];
			request.expiration = expiration;
			offset += 6;
		}
		// Java's readMessage() rejects a payload size that does not exactly
		// fill the message, so trailing bytes are malformed rather than
		// ignored.
		return offset == len;
	}

	I2CPSendMessageCheck CheckI2CPSendMessage (
		const I2CPSendMessageRequest & request, uint16_t ownSessionID, bool hasDestination, uint64_t nowMilliseconds)
	{
		if (request.sessionID != ownSessionID) return I2CPSendMessageCheck::BadSession;
		if (!hasDestination) return I2CPSendMessageCheck::BadSession;
		if (!request.hasNonce || !request.identity || !request.identityLen) return I2CPSendMessageCheck::BadMessage;
		if (request.payloadLen && !request.payload) return I2CPSendMessageCheck::BadMessage;
		// Java uses "expiration > 0 && expiration <= now".
		if (request.expiration && request.expiration <= nowMilliseconds) return I2CPSendMessageCheck::Expired;
		return I2CPSendMessageCheck::Ok;
	}

	I2CPBlindingInfoResult ParseI2CPBlindingInfoMessage (
		const uint8_t * buf, size_t len, I2CPBlindingInfo & info, std::string & error)
	{
		error.clear ();
		info = I2CPBlindingInfo { };
		constexpr uint8_t FLAG_AUTH = 0x0f;
		constexpr uint8_t FLAG_SECRET = 0x10;
		// sessionId(2), flags(1), endpointType(1), blindType(2), expiration
		// seconds(4), endpoint, optional 32-byte auth private key, optional
		// 1-byte length secret.
		if (!buf || len < 10)
		{
			error = "message is too short";
			return eI2CPBlindingInfoMalformed;
		}
		size_t offset = 2; // sessionId is validated by the caller
		const uint8_t flags = buf[offset++];
		const uint8_t authType = flags & FLAG_AUTH;
		// Java accepts none, DH, PSK and DH+PSK only; a value above 3 is a
		// corrupt flags byte, not an unknown scheme to ignore.
		if (authType > eI2CPBlindingAuthDHAndPSK)
		{
			error = "bad auth type";
			return eI2CPBlindingInfoMalformed;
		}
		info.authType = authType;
		const uint8_t endpointType = buf[offset++];
		if (endpointType > eI2CPBlindingEndpointKey)
		{
			error = "bad endpoint type";
			return eI2CPBlindingInfoMalformed;
		}
		info.endpointType = endpointType;
		const uint16_t blindType = bufbe16toh (buf + offset);
		offset += 2;
		{
			std::unique_ptr<i2p::crypto::Verifier> verifier (
				i2p::data::IdentityEx::CreateVerifier ((i2p::data::SigningKeyType)blindType));
			if (!verifier)
			{
				error = "unsupported blind type";
				return eI2CPBlindingInfoUnsupported;
			}
		}
		info.blindType = (i2p::data::SigningKeyType)blindType;
		if (len - offset < 4)
		{
			error = "message is truncated";
			return eI2CPBlindingInfoMalformed;
		}
		// seconds on the wire, milliseconds in the registry; 0 = never.
		info.expiration = (uint64_t)bufbe32toh (buf + offset) * 1000;
		offset += 4;

		switch (endpointType)
		{
		case eI2CPBlindingEndpointHash:
			if (len - offset < 32)
			{
				error = "hash is truncated";
				return eI2CPBlindingInfoMalformed;
			}
			offset += 32;
			break;
		case eI2CPBlindingEndpointHost:
		{
			if (len - offset < 1)
			{
				error = "host is truncated";
				return eI2CPBlindingInfoMalformed;
			}
			const uint8_t hostLen = buf[offset++];
			if (!hostLen || len - offset < hostLen)
			{
				error = "host is malformed";
				return eI2CPBlindingInfoMalformed;
			}
			info.host.assign ((const char *)buf + offset, hostLen);
			offset += hostLen;
			break;
		}
		case eI2CPBlindingEndpointDestination:
		{
			auto identity = std::make_shared<i2p::data::IdentityEx> ();
			const size_t identityLen = identity->FromBuffer (buf + offset, len - offset);
			if (!identityLen || identityLen > len - offset)
			{
				error = "destination is malformed";
				return eI2CPBlindingInfoMalformed;
			}
			const auto * key = identity->GetSigningPublicKeyBuffer ();
			const auto keyLen = identity->GetSigningPublicKeyLen ();
			if (!key || !keyLen)
			{
				error = "destination has no signing public key";
				return eI2CPBlindingInfoMalformed;
			}
			info.identity = identity;
			info.signingKeyType = identity->GetSigningKeyType ();
			info.signingKey.assign (key, key + keyLen);
			offset += identityLen;
			break;
		}
		case eI2CPBlindingEndpointKey:
		{
			if (len - offset < 2)
			{
				error = "key type is truncated";
				return eI2CPBlindingInfoMalformed;
			}
			const uint16_t keyType = bufbe16toh (buf + offset);
			offset += 2;
			std::unique_ptr<i2p::crypto::Verifier> verifier (
				i2p::data::IdentityEx::CreateVerifier ((i2p::data::SigningKeyType)keyType));
			if (!verifier)
			{
				error = "unsupported endpoint key type";
				return eI2CPBlindingInfoUnsupported;
			}
			const auto keyLen = verifier->GetPublicKeyLen ();
			if (!keyLen || len - offset < keyLen)
			{
				error = "key is truncated";
				return eI2CPBlindingInfoMalformed;
			}
			info.signingKeyType = (i2p::data::SigningKeyType)keyType;
			info.signingKey.assign (buf + offset, buf + offset + keyLen);
			offset += keyLen;
			break;
		}
		default: error = "bad endpoint type"; return eI2CPBlindingInfoMalformed;
		}

		// The 32-byte auth private key exists for the DH schemes only; PSK
		// carries the secret and nothing else.
		if (authType == eI2CPBlindingAuthDH || authType == eI2CPBlindingAuthDHAndPSK)
		{
			if (len - offset < 32)
			{
				error = "auth key is truncated";
				return eI2CPBlindingInfoMalformed;
			}
			info.authPrivateKey.assign (buf + offset, buf + offset + 32);
			offset += 32;
		}
		if (flags & FLAG_SECRET)
		{
			if (len - offset < 1)
			{
				error = "secret is truncated";
				return eI2CPBlindingInfoMalformed;
			}
			const uint8_t secretLen = buf[offset++];
			if (len - offset < secretLen)
			{
				error = "secret is malformed";
				return eI2CPBlindingInfoMalformed;
			}
			info.secret.assign ((const char *)buf + offset, secretLen);
			offset += secretLen;
		}
		if (offset != len)
		{
			error = "message has trailing bytes";
			return eI2CPBlindingInfoMalformed;
		}
		return eI2CPBlindingInfoOk;
	}

	void StoreI2CPBlindingInfo (I2CPBlindingInfo info)
	{
		if (info.signingKey.empty ()) return;
		auto key = MakeBlindingInfoKey (info.signingKeyType, info.signingKey.data (), info.signingKey.size ());
		const uint64_t now = i2p::util::GetMillisecondsSinceEpoch ();
		std::lock_guard<std::mutex> l (g_BlindingInfosMutex);
		PruneBlindingInfos (now);
		auto it = g_BlindingInfos.find (key);
		if (it != g_BlindingInfos.end ())
		{
			// A second registration for the same signing key must not erase
			// material that the first one already supplied.
			auto & existing = it->second.info;
			if (!info.identity) info.identity = existing.identity;
			if (info.authType == eI2CPBlindingAuthNone)
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
		g_BlindingInfos.insert_or_assign (std::move (key), BlindingInfoEntry { std::move (info), now });
	}

	bool FindI2CPBlindingInfoBySigningKey (
		i2p::data::SigningKeyType keyType, const uint8_t * key, size_t keyLen, I2CPBlindingInfo & info)
	{
		if (!key || !keyLen) return false;
		const uint64_t now = i2p::util::GetMillisecondsSinceEpoch ();
		std::lock_guard<std::mutex> l (g_BlindingInfosMutex);
		PruneBlindingInfos (now);
		auto it = g_BlindingInfos.find (MakeBlindingInfoKey (keyType, key, keyLen));
		if (it == g_BlindingInfos.end ()) return false;
		info = it->second.info;
		return true;
	}

	bool FindI2CPBlindingInfoByBlindedKey (const i2p::data::BlindedPublicKey & blinded, I2CPBlindingInfo & info)
	{
		if (!blinded.IsValid () || !blinded.GetPublicKeyLen ()) return false;
		// Blinding is one-way and a b33 address never carries the unblinded
		// key, so the association to a registered destination has to be
		// re-derived. Only destination endpoints carry an identity (key/hash
		// endpoints name the unblinded key without one) and are therefore
		// matchable here; the scan is bounded by the registry cap.
		std::vector<I2CPBlindingInfo> candidates;
		{
			const uint64_t now = i2p::util::GetMillisecondsSinceEpoch ();
			std::lock_guard<std::mutex> l (g_BlindingInfosMutex);
			PruneBlindingInfos (now);
			for (const auto & it : g_BlindingInfos)
			{
				const auto & candidate = it.second.info;
				if (!candidate.identity) continue;
				if ((uint16_t)candidate.signingKeyType != (uint16_t)blinded.GetSigType ()) continue;
				candidates.push_back (candidate);
			}
		}
		char date[9];
		i2p::util::GetCurrentDate (date);
		for (auto & candidate : candidates)
		{
			i2p::data::BlindedPublicKey derived (candidate.identity);
			if (!derived.IsValid () || derived.GetBlindedSigType () != blinded.GetBlindedSigType ()) continue;
			uint8_t blindedKey[i2p::crypto::EDDSA25519_PUBLIC_KEY_LENGTH];
			const size_t len = derived.GetBlindedKey (date, blindedKey);
			if (len && len == blinded.GetPublicKeyLen () && !memcmp (blindedKey, blinded.GetPublicKey (), len))
			{
				info = std::move (candidate);
				return true;
			}
		}
		return false;
	}

	std::vector<uint8_t> GetI2CPBlindingAuthSecret (const I2CPBlindingInfo & info)
	{
		// LeaseSet2::ExtractClientAuthData() consumes exactly 32 bytes: the
		// per-client DH private key or the PSK. Anything else cannot be used
		// and must not be passed on as if it were a key.
		static constexpr size_t SECRET_LEN = 32;
		switch (info.authType)
		{
		case eI2CPBlindingAuthDH:
			if (info.authPrivateKey.size () == SECRET_LEN) return info.authPrivateKey;
			return { };
		case eI2CPBlindingAuthPSK:
			if (info.secret.size () == SECRET_LEN)
				return std::vector<uint8_t> (info.secret.begin (), info.secret.end ());
			return { };
		case eI2CPBlindingAuthDHAndPSK:
			if (info.authPrivateKey.size () == SECRET_LEN) return info.authPrivateKey;
			if (info.secret.size () == SECRET_LEN)
				return std::vector<uint8_t> (info.secret.begin (), info.secret.end ());
			return { };
		default: return { };
		}
	}

	size_t GetI2CPBlindingInfoCount ()
	{
		const uint64_t now = i2p::util::GetMillisecondsSinceEpoch ();
		std::lock_guard<std::mutex> l (g_BlindingInfosMutex);
		PruneBlindingInfos (now);
		return g_BlindingInfos.size ();
	}

	void ClearI2CPBlindingInfos ()
	{
		std::lock_guard<std::mutex> l (g_BlindingInfosMutex);
		g_BlindingInfos.clear ();
	}

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
		// ClientConnectionRunner parses i2cp.leaseSetEncType once; the first
		// listed selectable type is the preferred publication key. Only the
		// installed key set is consulted for availability later, so an option
		// naming a key the client never supplied degrades to the highest
		// installed type instead of publishing a dead section.
		m_ConfiguredPreferredCryptoType
			= ParseLeaseSetEncryptionTypes (params[I2CP_PARAM_LEASESET_ENCRYPTION_TYPE]).preferred;
	}

	void I2CPDestination::Stop ()
	{
		m_LeaseSetCreationTimer.cancel ();
		m_ReadinessCheckTimer.cancel ();
		LeaseSetDestination::Stop ();
		m_Owner = nullptr;
	}

	void I2CPDestination::SetEncryptionPrivateKey (i2p::data::CryptoKeyType keyType, const uint8_t * key, size_t keyLen)
	{
		if (!key) return;
		auto encryptionKey = std::make_shared<i2p::crypto::LocalEncryptionKey> (keyType);
		const size_t expected = keyLen ? keyLen : encryptionKey->priv.size ();
		if (!expected || expected > encryptionKey->priv.size ())
		{
			LogPrint (eLogError, "I2CP: invalid private key length ", expected, " for type ", (int)keyType);
			return;
		}
		auto it = m_EncryptionKeys.find (keyType);
		if (it != m_EncryptionKeys.end () && it->second->priv.size () == encryptionKey->priv.size ()
			&& !memcmp (it->second->priv.data (), key, expected)) // same key
			return;
		// Legacy type-1 storage is 256 bytes while the I2CP scalar is 32
		// bytes. Zero the remainder so no stale key material is read by a
		// decryptor.
		memset (encryptionKey->priv.data (), 0, encryptionKey->priv.size ());
		memcpy (encryptionKey->priv.data (), key, expected);
		encryptionKey->CreateDecryptor (); // from private key
		if (!encryptionKey->decryptor)
		{
			LogPrint (eLogError, "I2CP: unsupported encryption key type ", (int)keyType);
			return;
		}
		m_EncryptionKeys.insert_or_assign (keyType, encryptionKey);
	}

	void I2CPDestination::SetECIESx25519EncryptionPrivateKey (
		i2p::data::CryptoKeyType keyType, const uint8_t * key, size_t keyLen)
	{
		if (!key) return;
		// Reject reserved EC types and hybrid parameter sets that the active
		// provider cannot decapsulate instead of publishing a dead LeaseSet.
		if (!i2p::data::IsSelectableLS2EncryptionKeyType (keyType))
		{
			LogPrint (eLogError, "I2CP: encryption key type ", (int)keyType, " is unavailable in this provider");
			return;
		}
		const size_t expected = keyLen ? keyLen : 32;
		if (expected != 32)
		{
			LogPrint (eLogError, "I2CP: invalid X25519 private key length ", expected);
			return;
		}
		auto it = m_EncryptionKeys.find (keyType);
		if (it != m_EncryptionKeys.end () && it->second->priv.size () == 32
			&& !memcmp (it->second->priv.data (), key, 32)) // same key
			return;
		auto encryptionKey = std::make_shared<i2p::crypto::LocalEncryptionKey> (keyType);
		if (encryptionKey->priv.size () != 32)
		{
			LogPrint (eLogError, "I2CP: unsupported ratchet encryption key type ", (int)keyType);
			return;
		}
		memcpy (encryptionKey->priv.data (), key, 32);
		// create decryptor manually
		auto decryptor = std::make_shared<i2p::crypto::ECIESX25519AEADRatchetDecryptor> (key, true);
		encryptionKey->decryptor = decryptor;
		// assign public key from decryptor
		memcpy (encryptionKey->pub.data (), decryptor->GetPubicKey (), encryptionKey->pub.size ());
		m_EncryptionKeys.insert_or_assign (keyType, encryptionKey);
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
		auto it = m_EncryptionKeys.find (keyType);
		return it != m_EncryptionKeys.end () && it->second && it->second->decryptor && !it->second->pub.empty ();
	}

	bool I2CPDestination::IsAdvertisedEncryptionKey (i2p::data::CryptoKeyType keyType, const uint8_t * key,
		size_t keyLen, const i2p::data::LeaseSet2 & ls, bool & verifiable) const
	{
		verifiable = false;
		if (!key) return false;
		if (keyType < i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD)
		{
			// ElGamal (and the legacy P-256 storage form) cannot be re-derived
			// from the private scalar with the current providers. The caller
			// keeps the install-time decryptor check for those types.
			return true;
		}
		if (keyLen != 32) return false;
		i2p::crypto::ECIESX25519AEADRatchetDecryptor decryptor (key, true);
		// Pad to the longest key section any known LS2 type can advertise
		// (ElGamal/P-256 are 256/64 bytes): IsEncryptionKey() compares
		// section-length bytes, so the candidate buffer must be at least that
		// long for every type we verify here.
		std::vector<uint8_t> candidate (256, 0);
		memcpy (candidate.data (), decryptor.GetPubicKey (), 32);
		verifiable = true;
		return ls.IsEncryptionKey (keyType, candidate.data ());
	}

	i2p::data::CryptoKeyType I2CPDestination::GetPreferredCryptoType () const
	{
		// The client's i2cp.leaseSetEncType list is the authority for the
		// preferred key (Java publishes in that order). Only the numeric
		// "highest installed type" was used before, which contradicts an
		// explicit "4,6" order. A configured type that the client never
		// supplied falls back to the highest installed one.
		if (m_EncryptionKeys.empty ()) return 0;
		if (m_ConfiguredPreferredCryptoType)
		{
			auto it = m_EncryptionKeys.find (m_ConfiguredPreferredCryptoType);
			if (it != m_EncryptionKeys.end () && it->second && it->second->decryptor) return it->first;
		}
		return m_EncryptionKeys.rbegin ()->first;
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
		if (!session || !session->ArmResponseTimer ()) return;
		auto timer = std::make_shared<boost::asio::steady_timer> (GetService ());
		timer->expires_after (std::chrono::milliseconds (I2CP_RATCHETS_RESPONSE_TIMEOUT));
		timer->async_wait (
			[session, s = GetSharedFromThis (), timer] (const boost::system::error_code & ecode)
			{
				session->DisarmResponseTimer ();
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
		const uint8_t * buf, size_t len, i2p::garlic::ECIESX25519AEADRatchetSession *)
	{
		if (len < 4)
		{
			LogPrint (eLogWarning, "I2CP: data message is too short ", len);
			return;
		}
		uint32_t length = bufbe32toh (buf);
		if (length > len - 4) length = static_cast<uint32_t> (len - 4);
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
		if (!msg)
		{
			if (m_Owner) m_Owner->SendMessageStatusMessage (nonce, eI2CPMessageStatusGuaranteedFailure);
			return;
		}
		uint8_t * buf = msg->GetPayload ();
		htobe32buf (buf, static_cast<uint32_t> (len));
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
		if (!garlic)
		{
			LogPrint (eLogError, "I2CP: Failed to wrap message");
			return false;
		}
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
		// Key the cache by the destination's identity hash. The former key
		// was the 32-byte signing-key slice at identity offset 352 (DSA's
		// last key bytes, but type-specific padding or a key fragment for
		// every other signature type); a crafted identity could reproduce
		// that slice with a different public key and select another
		// destination's routing session.
		m_Owner->AddRoutingSession (remote->GetIdentHash (), remoteSession);
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

	bool I2CPDestination::SendMsg (
		const uint8_t * payload, size_t len, std::shared_ptr<i2p::garlic::GarlicRoutingSession> remoteSession)
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
			return false;
		}
		auto msg = m_I2NPMsgsPool.AcquireSharedMt ();
		if (!msg) return false;
		uint8_t * buf = msg->GetPayload ();
		htobe32buf (buf, static_cast<uint32_t> (len));
		if (len) memcpy (buf + 4, payload, len);
		msg->len += len + 4;
		msg->FillI2NPMessageHeader (eI2NPData, GetRng () ());
		// wrap in gralic
		auto garlic = remoteSession->WrapSingleMessage (msg);
		if (!garlic)
		{
			remoteSession->SetSharedRoutingPath (nullptr);
			return false;
		}
		// send
		const bool sent = SendMsg (garlic, outboundTunnel, remoteLease);
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
		// m_Self keeps the object alive while its service thread is running,
		// so this only runs after the thread has been joined. The explicit
		// Stop is still needed for a destination that was never started
		// through Start().
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

	I2CPSession::I2CPSession (
		I2CPServer & owner, std::shared_ptr<boost::asio::ip::tcp::socket> socket, bool connectionAdmitted)
		: m_Owner (owner)
		, m_Socket (socket)
		, m_SessionID (0xFFFF)
		, m_MessageID (0)
		, m_IsSendAccepted (true)
		, m_IsSending (false)
		, m_ReadTimer (owner.GetService ())
		, m_ConnectionAdmitted (connectionAdmitted)
	{
	}

	I2CPSession::~I2CPSession ()
	{
		// Sessions must normally be destroyed on the owner thread:
		// Terminate() posts there, and the posted handler holds a reference
		// until cleanup completes. Use the inline teardown here so the
		// destructor never calls shared_from_this().
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

	void I2CPSession::ArmReadTimeout (int seconds)
	{
		// basic_waitable_timer has no error_code cancel overload on the
		// supported Boost range; cancel() is safe without a pending wait and
		// each arm replaces the previous deadline.
		m_ReadTimer.cancel ();
		m_ReadTimer.expires_after (std::chrono::seconds (seconds));
		m_ReadTimer.async_wait (
			[s = shared_from_this ()] (const boost::system::error_code & ecode) { s->HandleReadTimeout (ecode); });
	}

	void I2CPSession::CancelReadTimeout ()
	{
		m_ReadTimer.cancel ();
	}

	void I2CPSession::HandleReadTimeout (const boost::system::error_code & ecode)
	{
		if (ecode == boost::asio::error::operation_aborted) return;
		if (m_Terminated.load ())
		{
			// The connection is already being torn down; a delivered timer must
			// not defer another termination.
			return;
		}
		LogPrint (eLogWarning, "I2CP: Read timeout on control connection");
		// Runs on the owner service thread, and TerminateInOwnerThread closes
		// the socket, which aborts the pending read.
		Terminate ();
	}

	void I2CPSession::ReadProtocolByte ()
	{
		auto socket = m_Socket;
		if (socket)
		{
			// The protocol byte is part of the handshake: a peer that connects
			// and sends nothing must not hold the slot.
			ArmReadTimeout (I2CP_HANDSHAKE_TIMEOUT);
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
		// The header is read incrementally so a peer that stalls in the
		// middle of it is covered by the read deadline, while a connection
		// that has not started a message yet is idle and must not be killed:
		// established clients keep the socket open between messages.
		if (!m_HandshakeComplete)
			ArmReadTimeout (I2CP_HANDSHAKE_TIMEOUT);
		else if (m_HeaderReceived > 0)
			ArmReadTimeout (I2CP_BODY_TIMEOUT);
		else
			CancelReadTimeout ();
		// the handler keeps the socket alive: Terminate resets m_Socket,
		// while a pending read holds a reference to the socket object until
		// it completes
		socket->async_read_some (boost::asio::buffer (m_Header + m_HeaderReceived, I2CP_HEADER_SIZE - m_HeaderReceived),
			[s = shared_from_this (), socket] (const boost::system::error_code & ecode, std::size_t bytes_transferred)
			{
				if (ecode)
				{
					s->Terminate ();
					return;
				}
				s->m_HeaderReceived += bytes_transferred;
				if (s->m_HeaderReceived < I2CP_HEADER_SIZE)
				{
					s->ReceiveHeader ();
					return;
				}
				s->m_HeaderReceived = 0;
				s->HandleReceivedHeader ();
			});
	}

	void I2CPSession::HandleReceivedHeader ()
	{
		m_PayloadLen = bufbe32toh (m_Header + I2CP_HEADER_LENGTH_OFFSET);
		if (m_PayloadLen > I2CP_MAX_MESSAGE_LENGTH)
		{
			LogPrint (eLogError, "I2CP: Unexpected payload length ", m_PayloadLen);
			Terminate ();
			return;
		}
		if (m_PayloadLen > 0)
		{
			ReceivePayload ();
			return;
		}
		// no following payload
		CancelReadTimeout ();
		m_HandshakeComplete = true;
		HandleMessage ();
		if (m_Terminated.load ()) return;
		ReceiveHeader (); // next message
	}

	void I2CPSession::ReceivePayload ()
	{
		auto socket = m_Socket;
		if (!socket)
		{
			LogPrint (eLogError, "I2CP: Can't receive payload");
			return;
		}
		// A declared body must arrive within the body deadline; the
		// synchronous available()/read() fast path is gone so no read blocks
		// the service thread.
		ArmReadTimeout (I2CP_BODY_TIMEOUT);
		boost::asio::async_read (*socket, boost::asio::buffer (m_Payload, m_PayloadLen), boost::asio::transfer_all (),
			[s = shared_from_this (), socket] (const boost::system::error_code & ecode, std::size_t bytes_transferred)
			{ s->HandleReceivedPayload (ecode, bytes_transferred); });
	}

	void I2CPSession::HandleReceivedPayload (const boost::system::error_code & ecode, std::size_t)
	{
		if (ecode)
		{
			Terminate ();
			return;
		}
		CancelReadTimeout ();
		m_HandshakeComplete = true;
		HandleMessage ();
		m_PayloadLen = 0;
		if (m_Terminated.load ()) return;
		ReceiveHeader (); // next message
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

		// Stop the read deadline before the socket is closed: the pending
		// timer handler holds a reference to this session and would otherwise
		// keep it alive for up to the full timeout.
		CancelReadTimeout ();
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
		// The admission slot is released exactly once, and only when this
		// session was actually charged for one.
		if (m_ConnectionAdmitted && !m_ConnectionReleased.exchange (true)) m_Owner.ReleaseConnection ();
	}

	void I2CPSession::SendI2CPMessage (uint8_t type, const uint8_t * payload, size_t len)
	{
		auto l = len + I2CP_HEADER_SIZE;
		if (l > I2CP_MAX_MESSAGE_LENGTH)
		{
			LogPrint (eLogError, "I2CP: Message to send is too long ", l);
			return;
		}
		// Build a complete, caller-independent buffer. The service thread is
		// the only place where it is copied into the in-flight buffer or
		// queued.
		auto sendBuf = std::make_shared<i2p::stream::SendBuffer> (l);
		uint8_t * buf = sendBuf->buf;
		htobe32buf (buf + I2CP_HEADER_LENGTH_OFFSET, static_cast<uint32_t> (len));
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

	void I2CPSession::HandleI2CPMessageSent (const boost::system::error_code & ecode, std::size_t)
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

	void I2CPSession::GetDateMessageHandler (const uint8_t *, size_t)
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
		// A second CREATE SESSION must not touch the identifier of the
		// session that is already inserted. Reject it before any local state
		// changes.
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

		// SessionConfig.verifySignature(): an offline-signed session carries
		// the three leaseSetOffline* options in the mapping and its Session
		// Config signature is made by the transient key.
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

	void I2CPSession::DestroySessionMessageHandler (const uint8_t *, size_t)
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

	void I2CPSession::SendMessageStatusMessage (uint32_t nonce, I2CPMessageStatus status, uint16_t sessionID)
	{
		if (!nonce) return; // don't send status with zero nonce
		uint8_t buf[15];
		if (sessionID == 0xFFFF) sessionID = m_SessionID.load ();
		htobe16buf (buf, sessionID);
		htobe32buf (buf + 2, m_MessageID.fetch_add (1));
		buf[6] = (uint8_t)status;
		memset (buf + 7, 0, 4); // size
		htobe32buf (buf + 11, nonce);
		SendI2CPMessage (I2CP_MESSAGE_STATUS_MESSAGE, buf, 15);
	}

	// Full-identity hash: the same value IdentityEx derives for a
	// destination and LeaseSet::GetIdentHash() returns for the cached
	// lease set, so inserts and lookups agree for every key type.
	i2p::data::IdentHash GetI2CPRoutingSessionKey (const uint8_t * identity, size_t len)
	{
		i2p::data::IdentHash key;
		if (identity && len)
			SHA256 (identity, len, key);
		else
			key.Fill (0);
		return key;
	}

	void I2CPSession::AddRoutingSession (
		const i2p::data::IdentHash & identHash, std::shared_ptr<i2p::garlic::GarlicRoutingSession> remoteSession)
	{
		if (!remoteSession) return;
		remoteSession->SetAckRequestInterval (I2CP_SESSION_ACK_REQUEST_INTERVAL);
		std::lock_guard<std::mutex> l (m_RoutingSessionsMutex);
		m_RoutingSessions[identHash] = remoteSession;
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
		if (len < 2)
		{
			LogPrint (eLogError, "I2CP: CreateLeaseSetMessage is too short ", len);
			return;
		}
		const uint16_t sessionID = bufbe16toh (buf);
		if (sessionID != m_SessionID.load ())
		{
			LogPrint (eLogError, "I2CP: Unexpected sessionID ", sessionID);
			return;
		}
		auto destination = m_Destination;
		if (!destination)
		{
			LogPrint (eLogError, "I2CP: CreateLeaseSetMessage without a session destination");
			return;
		}
		auto identity = destination->GetIdentity ();
		if (!identity)
		{
			LogPrint (eLogError, "I2CP: CreateLeaseSetMessage without a session identity");
			return;
		}
		// Java reads the signing private key with the session destination's
		// signature type and the encryption private key with its crypto type.
		// A LeaseSet (CLS1) is ElGamal-encrypted by definition, so a
		// destination that is not an ElGamal destination must publish through
		// CLS2 instead.
		const size_t signingKeyLen = identity->GetSigningPrivateKeyLen ();
		if (identity->GetCryptoKeyType () != i2p::data::CRYPTO_KEY_TYPE_ELGAMAL
			|| identity->GetSigningKeyType () != i2p::data::SIGNING_KEY_TYPE_DSA_SHA1
			|| signingKeyLen != i2p::crypto::DSA_PRIVATE_KEY_LENGTH || !signingKeyLen)
		{
			// Fail closed: the per-signature-type wire layout is only certain
			// for DSA here, and a mis-sized offset would install an unrelated
			// scalar as the destination's encryption key. DSA-only install
			// (residual risk tracked in the audit notes) is preferable to
			// guessing.
			LogPrint (eLogError, "I2CP: CreateLeaseSet for signing type ", (int)identity->GetSigningKeyType (),
				" and crypto type ", (int)identity->GetCryptoKeyType (), " is not supported; use CreateLeaseSet2");
			return;
		}
		const size_t encryptionKeyLen = 256; // ElGamal private key on the wire
		const size_t offset = 2 + signingKeyLen + encryptionKeyLen;
		if (len < offset + 1)
		{
			LogPrint (eLogError, "I2CP: CreateLeaseSetMessage is too short ", len);
			return;
		}
		// The embedded LeaseSet must belong to this session's destination and
		// carry a valid signature over the whole structure.
		i2p::data::IdentityEx embedded (buf + offset, len - offset);
		if (!embedded.GetFullLen () || embedded.GetFullLen () > len - offset)
		{
			LogPrint (eLogError, "I2CP: CreateLeaseSetMessage has no LeaseSet identity");
			return;
		}
		if (embedded.GetIdentHash () != identity->GetIdentHash ())
		{
			LogPrint (eLogError, "I2CP: CreateLeaseSetMessage is for a foreign destination");
			return;
		}
		uint64_t expires = 0;
		if (!i2p::data::LeaseSetBufferValidate (buf + offset, len - offset, expires))
		{
			LogPrint (eLogError, "I2CP: Invalid LeaseSet in CreateLeaseSetMessage");
			return;
		}
		// The LeaseSet must advertise this destination's own encryption
		// public key, otherwise the private key below would decrypt for
		// someone else.
		const uint8_t * advertisedKey = buf + offset + embedded.GetFullLen ();
		if (len - offset - embedded.GetFullLen () < 256
			|| memcmp (advertisedKey, identity->GetEncryptionPublicKey (), 256))
		{
			LogPrint (eLogError, "I2CP: LeaseSet encryption key does not match the destination");
			return;
		}
		const uint8_t * encryptionKey = buf + 2 + signingKeyLen;
		// ElGamal public keys cannot be re-derived from the private scalar
		// with the current providers, so the advertised-key comparison above
		// is the available proof that both halves belong together.
		destination->SetEncryptionPrivateKey (i2p::data::CRYPTO_KEY_TYPE_ELGAMAL, encryptionKey, encryptionKeyLen);
		if (!destination->SupportsEncryptionType (i2p::data::CRYPTO_KEY_TYPE_ELGAMAL))
		{
			LogPrint (eLogError, "I2CP: Failed to install the LeaseSet encryption key");
			return;
		}
		destination->LeaseSetCreated (buf + offset, len - offset);
	}

	void I2CPSession::CreateLeaseSet2MessageHandler (const uint8_t * buf, size_t len)
	{
		if (len < 3)
		{
			LogPrint (eLogError, "I2CP: CreateLeaseSet2Message is too short ", len);
			return;
		}
		uint16_t sessionID = bufbe16toh (buf);
		if (sessionID != m_SessionID.load ())
		{
			LogPrint (eLogError, "I2CP: Unexpected sessionID ", sessionID);
			return;
		}
		size_t offset = 2;
		if (!m_Destination)
		{
			LogPrint (eLogError, "I2CP: CreateLeaseSet2Message without a session destination");
			return;
		}
		uint8_t storeType = buf[offset];
		offset++; // store type
		i2p::data::LeaseSet2 ls (storeType, buf + offset, len - offset); // outer layer only for encrypted
		if (!ls.IsValid ())
		{
			LogPrint (eLogError, "I2CP: Invalid LeaseSet2 of type ", storeType);
			return;
		}
		offset += ls.GetBufferLen ();
		if (storeType == i2p::data::NETDB_STORE_TYPE_META_LEASESET2)
		{
			// A MetaLeaseSet2 carries no private-key list on the wire (Java's
			// CreateLeaseSet2Message writes one only for key-bearing store
			// types), so requiring a count byte here rejected every valid meta
			// message.
			if (offset != len)
			{
				LogPrint (eLogError, "I2CP: CreateLeaseSet2Message has trailing bytes");
				return;
			}
			m_Destination->LeaseSet2Created (storeType, ls.GetBuffer (), ls.GetBufferLen ());
			return;
		}
		// private keys
		if (offset >= len)
		{
			LogPrint (eLogError, "I2CP: CreateLeaseSet2Message is too short for private keys");
			return;
		}
		int numPrivateKeys = buf[offset];
		offset++;
		std::vector<std::pair<i2p::data::CryptoKeyType, std::vector<uint8_t>>> stagedKeys;
		stagedKeys.reserve (numPrivateKeys > 0 ? numPrivateKeys : 0);
		for (int i = 0; i < numPrivateKeys; i++)
		{
			if (offset + 4 > len) return;
			uint16_t keyType = bufbe16toh (buf + offset);
			offset += 2; // encryption type
			uint16_t keyLen = bufbe16toh (buf + offset);
			offset += 2; // private key length
			const size_t expected = GetI2CPPrivateKeyLen (keyType);
			if (!expected || keyLen != expected || offset + keyLen > len)
			{
				LogPrint (eLogError, "I2CP: bad private key length ", keyLen, " for type ", keyType);
				return;
			}
			stagedKeys.emplace_back (static_cast<i2p::data::CryptoKeyType> (keyType),
				std::vector<uint8_t> (buf + offset, buf + offset + keyLen));
			offset += keyLen;
		}
		if (offset != len)
		{
			LogPrint (eLogError, "I2CP: CreateLeaseSet2Message has trailing bytes");
			return;
		}
		// Java CreateLeaseSet2Message requires the private-key list to match
		// the standard LS2 key sections exactly before installing any key or
		// publishing the set. Only a standard LeaseSet2 carries comparable
		// key sections; an encrypted outer layer has none.
		const bool standardLeaseSet = storeType == i2p::data::NETDB_STORE_TYPE_STANDARD_LEASESET2;
		if (standardLeaseSet)
		{
			if (stagedKeys.size () != ls.GetNumEncryptionKeys ())
			{
				LogPrint (eLogError, "I2CP: LeaseSet2 key count does not match private keys");
				return;
			}
			for (size_t i = 0; i < stagedKeys.size (); ++i)
			{
				if (stagedKeys[i].first != ls.GetEncryptionKeyType (i))
				{
					LogPrint (eLogError, "I2CP: LeaseSet2 key type does not match private key ", i);
					return;
				}
			}
			// Every staged private key must derive the public key the LeaseSet
			// advertises for that type; otherwise the session would publish a
			// set whose traffic nobody (including this destination) can decrypt.
			for (const auto & key : stagedKeys)
			{
				bool verifiable = false;
				if (!m_Destination->IsAdvertisedEncryptionKey (
						key.first, key.second.data (), key.second.size (), ls, verifiable))
				{
					LogPrint (
						eLogError, "I2CP: LeaseSet2 key type ", (int)key.first, " does not match the private key");
					return;
				}
				if (!verifiable)
					LogPrint (eLogDebug, "I2CP: LeaseSet2 key type ", (int)key.first,
						" public key cannot be re-derived; install-time check only");
			}
		}
		for (const auto & key : stagedKeys)
		{
			if (key.first >= i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD)
				m_Destination->SetECIESx25519EncryptionPrivateKey (key.first, key.second.data (), key.second.size ());
			else
				m_Destination->SetEncryptionPrivateKey (key.first, key.second.data (), key.second.size ());
			if (!m_Destination->SupportsEncryptionType (key.first))
			{
				// A partially installed key set would publish a LeaseSet that no
				// peer can use for this type.
				LogPrint (eLogError, "I2CP: failed to install LeaseSet2 encryption key type ", (int)key.first);
				return;
			}
		}

		m_Destination->LeaseSet2Created (storeType, ls.GetBuffer (), ls.GetBufferLen ());
	}

	void I2CPSession::SendMessageMessageHandler (const uint8_t * buf, size_t len)
	{
		SendMessageRequest (buf, len, false);
	}

	void I2CPSession::SendMessageExpiresMessageHandler (const uint8_t * buf, size_t len)
	{
		// flags(2) and expiration(6) follow the nonce; keep them instead of
		// truncating them away. The expiration is enforced as a deadline and
		// the flags are carried through the request.
		SendMessageRequest (buf, len, true);
	}

	void I2CPSession::SendMessageRequest (const uint8_t * buf, size_t len, bool withExpiration)
	{
		I2CPSendMessageRequest request;
		if (!ParseI2CPSendMessage (buf, len, withExpiration, request))
		{
			// The control connection stays open: a single malformed send is not
			// a protocol violation. A status reply needs the nonce, which a
			// malformed layout does not reliably expose, so only log.
			LogPrint (eLogError, "I2CP: Malformed send message ", len);
			if (request.hasNonce)
				SendMessageStatusMessage (request.nonce, eI2CPMessageStatusBadMessage, request.sessionID);
			return;
		}
		uint64_t now = i2p::util::GetMillisecondsSinceEpoch ();
		switch (CheckI2CPSendMessage (request, m_SessionID.load (), m_Destination != nullptr, now))
		{
		case I2CPSendMessageCheck::BadSession:
			LogPrint (eLogWarning, "I2CP: Send message for an unknown session ", request.sessionID);
			SendMessageStatusMessage (request.nonce, eI2CPMessageStatusBadSession, request.sessionID);
			return;
		case I2CPSendMessageCheck::BadMessage:
			LogPrint (eLogError, "I2CP: Send message has no usable destination or payload");
			SendMessageStatusMessage (request.nonce, eI2CPMessageStatusBadMessage, request.sessionID);
			return;
		case I2CPSendMessageCheck::Expired:
			// Java answers SendFailureExpired() without touching the tunnel
			// pool.
			LogPrint (eLogDebug, "I2CP: Send message expired");
			SendMessageStatusMessage (request.nonce, eI2CPMessageStatusSendFailureExpired);
			return;
		case I2CPSendMessageCheck::Ok: break;
		}
		if (request.flags)
		{
			// Proposal 167 defines no bit yet; an undefined bit must not change
			// the delivery semantics of the send. The parsed value is kept so a
			// future definition does not silently disappear here.
			LogPrint (eLogWarning, "I2CP: Send message carries undefined flags ", request.flags);
		}

		if (!m_Destination->IsReady ())
		{
			LogPrint (eLogInfo, "I2CP: Destination is not ready");
			SendMessageStatusMessage (request.nonce, eI2CPMessageStatusNoLocalTunnels);
			return;
		}
		if (m_IsSendAccepted.load ()) SendMessageStatusMessage (request.nonce, eI2CPMessageStatusAccepted); // accepted
		// The cache is indexed by the parsed identity hash, so every
		// signature/crypto type keys its own entry and a crafted identity
		// cannot reuse another peer's session.
		auto remoteIdentHash = GetI2CPRoutingSessionKey (request.identity, request.identityLen);
		std::shared_ptr<i2p::garlic::GarlicRoutingSession> remoteSession;
		{
			std::lock_guard<std::mutex> l (m_RoutingSessionsMutex);
			auto it = m_RoutingSessions.find (remoteIdentHash);
			if (it != m_RoutingSessions.end ())
			{
				if (!it->second->IsTerminated () && it->second->IsReadyToSend ())
					remoteSession = it->second;
				else
					m_RoutingSessions.erase (it);
			}
		}
		const bool sent = remoteSession && m_Destination->SendMsg (request.payload, request.payloadLen, remoteSession);
		if (sent)
			SendMessageStatusMessage (request.nonce, eI2CPMessageStatusGuaranteedSuccess);
		else
		{
			// SendMsgTo owns the single terminal status for the fallback path;
			// SendMsg must not emit one itself.
			m_Destination->SendMsgTo (request.payload, request.payloadLen, remoteIdentHash, request.nonce);
		}
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
					blinded = addr->blindedPublicKey;
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
					// A client-authorised b33 destination can only be decrypted
					// with the material the client registered through a
					// BlindingInfo message. The record is keyed by the unblinded
					// signing key, so it is looked up through today's blinded key;
					// without usable material the reply tells the client which
					// part is missing instead of reporting a bare failure.
					std::vector<uint8_t> secret;
					if (blinded)
					{
						I2CPBlindingInfo blindingInfo;
						if (FindI2CPBlindingInfoByBlindedKey (*blinded, blindingInfo))
						{
							secret = GetI2CPBlindingAuthSecret (blindingInfo);
							const uint8_t result
								= GetI2CPHostReplyResultForAuth (blindingInfo.authType, !secret.empty ());
							if (result != eI2CPHostReplySuccess)
							{
								// Never log the key or the secret, only the scheme.
								LogPrint (eLogWarning, "I2CP: Blinded LeaseSet needs client auth type ",
									(int)blindingInfo.authType);
								SendHostReplyMessage (requestID, sessionID, nullptr, requestOptions, nullptr, result);
								return;
							}
						}
					}
					auto s = shared_from_this ();
					auto complete = [s, requestID, sessionID, blinded, requestOptions, secret] (
										std::shared_ptr<i2p::data::LeaseSet> leaseSet)
					{
						// The captured secret keeps the client-auth bytes alive for
						// the whole pending request, so no future asynchronous copy
						// can read a released buffer.
						(void)secret;
						uint8_t result = eI2CPHostReplySuccess;
						// A b33 LeaseSet is stored under the blinded store key.
						// Its inner identity is necessarily different from the
						// unblinded BlindingInfo destination, so compare the
						// store key that was actually requested.
						if (leaseSet && blinded)
						{
							auto ls2 = dynamic_cast<const i2p::data::LeaseSet2 *> (leaseSet.get ());
							if (!ls2 || !ls2->IsStoreKey (blinded->GetStoreHash ()))
							{
								LogPrint (eLogError, "I2CP: Blinded LeaseSet store key does not match request");
								leaseSet = nullptr;
								// The reply arrived but could not be authenticated
								// or decrypted: that is DECRYPTION_FAILURE, not a
								// plain lookup failure.
								result = eI2CPHostReplyDecryptionFailure;
							}
						}
						// A null completion without a decryption result is a
						// lookup that timed out or found no LeaseSet.
						if (!leaseSet && result == eI2CPHostReplySuccess) result = eI2CPHostReplyLeaseSetLookupFailure;
						s->SendHostReplyMessage (requestID, sessionID, leaseSet ? leaseSet->GetIdentity () : nullptr,
							requestOptions, std::move (leaseSet), result);
					};
					if (blinded)
						destination->RequestDestinationWithEncryptedLeaseSet (
							blinded, complete, secret.empty () ? nullptr : secret.data (), secret.size ());
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
		if (len < 10)
		{
			LogPrint (eLogError, "I2CP: BlindingInfo message is too short ", len);
			Terminate ();
			return;
		}
		const uint16_t sessionID = bufbe16toh (buf);
		if (sessionID != m_SessionID.load () && sessionID != 0xFFFF)
		{
			LogPrint (eLogWarning, "I2CP: BlindingInfo for unexpected session ", sessionID);
			return;
		}
		I2CPBlindingInfo info;
		std::string error;
		if (ParseI2CPBlindingInfoMessage (buf, len, info, error) != eI2CPBlindingInfoOk)
		{
			// Malformed and unsupported requests are protocol violations, as
			// before; the diagnostic never contains key or secret bytes.
			LogPrint (eLogError, "I2CP: BlindingInfo ", error);
			Terminate ();
			return;
		}
		if (info.endpointType == eI2CPBlindingEndpointHash || info.endpointType == eI2CPBlindingEndpointHost)
		{
			// Java's handleBlindingInfo() cannot use these either: the record
			// cannot be matched to a LeaseSet store key.
			LogPrint (eLogWarning, "I2CP: BlindingInfo endpoint type ", (int)info.endpointType, " is not supported",
				info.endpointType == eI2CPBlindingEndpointHost ? (": " + info.host) : "");
			return;
		}
		LogPrint (eLogDebug, "I2CP: BlindingInfo for ",
			info.identity ? info.identity->GetIdentHash ().ToBase32 () : "a signing key", ", auth type ",
			(int)info.authType, ", expires ", info.expiration);
		// Only the record and the scheme are logged: the auth private key and
		// the secret never reach a log line.
		StoreI2CPBlindingInfo (std::move (info));
	}

	void I2CPSession::SendHostReplyMessage (uint32_t requestID, uint16_t sessionID,
		std::shared_ptr<const i2p::data::IdentityEx> identity, bool withOptions,
		std::shared_ptr<const i2p::data::LeaseSet> leaseSet, uint8_t result)
	{
		std::vector<uint8_t> options;
		if (withOptions && result == eI2CPHostReplySuccess && identity)
		{
			// Proposal 167: types 2-4 return the standard LeaseSet2 options
			// mapping when available, otherwise an empty Mapping.
			if (leaseSet && leaseSet->GetStoreType () == i2p::data::NETDB_STORE_TYPE_STANDARD_LEASESET2)
			{
				const auto & properties = static_cast<const i2p::data::LeaseSet2 *> (leaseSet.get ())->GetProperties ();
				if (!properties.IsEmpty ())
				{
					options.resize (4096);
					options.resize (properties.ToBuffer (options.data (), options.size ()));
				}
			}
			if (options.empty ()) options = { 0, 0 };
		}
		// Success is framed exactly as before (identity + optional options
		// with result 0); every failure is the 7-byte layout with the Java
		// result code, so a client can tell a missing secret/key from a
		// missing set.
		auto payload = BuildI2CPHostReplyPayload (requestID, sessionID, std::move (identity), options, result);
		SendI2CPMessage (I2CP_HOST_REPLY_MESSAGE, payload.data (), payload.size ());
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

	void I2CPSession::GetBandwidthLimitsMessageHandler (const uint8_t *, size_t)
	{
		uint8_t limits[64];
		memset (limits, 0, 64);
		uint32_t limit;
		i2p::config::GetOption ("i2cp.inboundlimit", limit);
		if (!limit) limit = static_cast<uint32_t> (i2p::context.GetBandwidthLimit ());
		htobe32buf (limits, limit); // inbound
		i2p::config::GetOption ("i2cp.outboundlimit", limit);
		if (!limit) limit = static_cast<uint32_t> (i2p::context.GetBandwidthLimit ());
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
		htobe32buf (buf + I2CP_HEADER_LENGTH_OFFSET, static_cast<uint32_t> (len + 10));
		buf[I2CP_HEADER_TYPE_OFFSET] = I2CP_MESSAGE_PAYLOAD_MESSAGE;
		htobe16buf (buf + I2CP_HEADER_SIZE, m_SessionID.load ());
		htobe32buf (buf + I2CP_HEADER_SIZE + 2, m_MessageID.fetch_add (1));
		htobe32buf (buf + I2CP_HEADER_SIZE + 6, static_cast<uint32_t> (len));
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
		, m_AdmissionPolicy (ReadI2CPAdmissionPolicy ())
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

		// Session sockets and send queues belong to the service thread. Run
		// the teardown there, then join from this (non-owner) thread. When
		// the server was never started there is no owner thread to marshal
		// to.
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
			if (!ec && TryAcquireConnection ())
			{
				LogPrint (eLogDebug, "I2CP: New connection from ", ep);
				auto session = std::make_shared<I2CPSession> (*this, socket, true);
				session->Start ();
			}
			else
			{
				// No I2CP message has been exchanged yet, so there is no
				// session to answer with STATUS REFUSED: close the socket and
				// keep the admission slot free for a real client.
				if (ec)
					LogPrint (eLogError, "I2CP: Incoming connection error ", ec.message ());
				else
					LogPrint (eLogWarning, "I2CP: Refused connection, ", GetNumConnections (), " of ",
						m_AdmissionPolicy.maxConnections, " in use");
				boost::system::error_code closeEc;
				socket->shutdown (boost::asio::ip::tcp::socket::shutdown_both, closeEc);
				socket->close (closeEc);
			}
		}
		else
			LogPrint (eLogError, "I2CP: Accept error: ", ecode.message ());

		if (ecode != boost::asio::error::operation_aborted) Accept ();
	}

	bool I2CPServer::TryAcquireConnection ()
	{
		// Compare-and-increment under the admission policy: a rejected accept
		// never charges a slot, and ReleaseConnection() can therefore never
		// underflow.
		size_t current = m_NumConnections.load ();
		do
		{
			if (!m_AdmissionPolicy.AcceptConnection (current)) return false;
		} while (!m_NumConnections.compare_exchange_weak (current, current + 1));
		return true;
	}

	void I2CPServer::ReleaseConnection ()
	{
		size_t current = m_NumConnections.load ();
		while (current > 0 && !m_NumConnections.compare_exchange_weak (current, current - 1))
		{
		}
	}

	bool I2CPServer::InsertSession (std::shared_ptr<I2CPSession> session, uint16_t sessionID)
	{
		if (!session || sessionID == 0xFFFF) return false;
		std::lock_guard<std::mutex> l (m_SessionsMutex);
		if (!m_AdmissionPolicy.AcceptSession (m_Sessions.size ()))
		{
			LogPrint (eLogWarning, "I2CP: Refused session, ", m_Sessions.size (), " of ", m_AdmissionPolicy.maxSessions,
				" in use");
			return false;
		}
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
