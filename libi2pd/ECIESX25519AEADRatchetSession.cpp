/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: ECIESX25519AEADRatchetSession.
 *
 * Implements the ECIES-X25519 AEAD ratchet (Noise IK) for garlic sessions:
 * new-session and reply handshakes, optional ML-KEM hybrid key exchange,
 * session-tag and symmetric-key ratchets with 16-bit indices, acknowledgements
 * and ratchet rotation. Handshakes are trial-authenticated before any state is
 * committed, and all-zero X25519 outputs, timestamp deviations, replays and
 * oversized tag/ACK sets are rejected.
 *
 */

#include <string.h>
#include <algorithm>
#include <memory>
#include "Log.h"
#include "util.h"
#include "Crypto.h"
#include "PostQuantum.h"
#include "Elligator.h"
#include "Tag.h"
#include "I2PEndian.h"
#include "Timestamp.h"
#include "Tunnel.h"
#include "TunnelPool.h"
#include "Transports.h"
#include "ECIESX25519AEADRatchetSession.h"

namespace i2p
{
namespace garlic
{
	namespace
	{
		// X25519 accepts low-order points and returns an all-zero shared secret.
		// Such a value is known to everybody, so it must never enter the Noise
		// chain key of an authenticated session.
		bool IsZeroBuf (const uint8_t * buf, size_t len)
		{
			uint8_t acc = 0;
			for (size_t i = 0; i < len; i++) acc |= buf[i];
			return acc == 0;
		}

		bool AgreeNonZero (i2p::crypto::X25519Keys & keys, const uint8_t * pub, uint8_t * sharedSecret)
		{
			return keys.Agree (pub, sharedSecret) && !IsZeroBuf (sharedSecret, 32);
		}
	}

	void RatchetTagSet::DHInitialize (const uint8_t * rootKey, const uint8_t * k)
	{
		// DH_INITIALIZE(rootKey, k)
		uint8_t keydata[64];
		i2p::crypto::HKDF (
			rootKey, k, 32, "KDFDHRatchetStep", keydata); // keydata = HKDF(rootKey, k, "KDFDHRatchetStep", 64)
		memcpy (m_NextRootKey, keydata, 32); // nextRootKey = keydata[0:31]
		i2p::crypto::HKDF (keydata + 32, nullptr, 0, "TagAndKeyGenKeys", m_SessionTagKeyData);
		// [sessTag_ck, symmKey_ck] = HKDF(keydata[32:63], ZEROLEN, "TagAndKeyGenKeys", 64)
		memcpy (m_SymmKeyCK, (const uint8_t *)m_SessionTagKeyData + 32, 32);
		m_NextSymmKeyIndex = 0;
	}

	void RatchetTagSet::NextSessionTagRatchet ()
	{
		i2p::crypto::HKDF (m_SessionTagKeyData, nullptr, 0, "STInitialization",
			m_SessionTagKeyData); // [sessTag_ck, sesstag_constant] = HKDF(sessTag_ck, ZEROLEN, "STInitialization", 64)
		memcpy (m_SessTagConstant, (const uint8_t *)m_SessionTagKeyData + 32, 32); // SESSTAG_CONSTANT = keydata[32:63]
		m_NextIndex = 0;
	}

	uint64_t RatchetTagSet::GetNextSessionTag (i2p::crypto::HKDFContext & hkdfCtx)
	{
		// Java RatchetTagSet.consumeNext(): _lastTag starts at -1 and accepts
		// the full 16-bit index space 0..65535, then refuses the next call.
		// Check before advancing so index 0xFFFF is emitted exactly once.
		if (m_NextIndex > 0xFFFF)
		{
			LogPrint (eLogError, "Garlic: Tagset ", GetTagSetID (), " is empty");
			return 0;
		}
		hkdfCtx (m_SessionTagKeyData, m_SessTagConstant, 32, "SessionTagKeyGen",
			m_SessionTagKeyData); // [sessTag_ck, tag] = HKDF(sessTag_chainkey, SESSTAG_CONSTANT, "SessionTagKeyGen", 64)
		m_NextIndex++;
		return m_SessionTagKeyData.GetLL ()[4]; // tag = keydata[32:39]
	}

	bool RatchetTagSet::GetSymmKey (int index, uint8_t * key)
	{
		if (!key) return false;
		// A tagset can never index beyond the 16-bit message number space.
		if (index < 0 || index > 0xFFFF)
		{
			LogPrint (eLogError, "Garlic: Symmetric key index ", index, " is out of range");
			return false;
		}
		if (index >= m_NextSymmKeyIndex)
		{
			auto num = index + 1 - m_NextSymmKeyIndex;
			if (!m_NextSymmKeyIndex)
			{
				i2p::crypto::HKDF (m_SymmKeyCK, nullptr, 0, "SymmetricRatchet",
					m_CurrentSymmKeyCK); // keydata_0 = HKDF(symmKey_ck, SYMMKEY_CONSTANT, "SymmetricRatchet", 64)
				m_NextSymmKeyIndex = 1;
				num--;
			}
			for (int i = 0; i < num; i++)
			{
				i2p::crypto::HKDF (m_CurrentSymmKeyCK, nullptr, 0, "SymmetricRatchet", m_CurrentSymmKeyCK);
				if (i < num - 1) m_ItermediateSymmKeys.emplace (m_NextSymmKeyIndex + i, m_CurrentSymmKeyCK + 32);
			}
			m_NextSymmKeyIndex += num;
			memcpy (key, m_CurrentSymmKeyCK + 32, 32);
			return true;
		}
		auto it = m_ItermediateSymmKeys.find (index);
		if (it != m_ItermediateSymmKeys.end ())
		{
			memcpy (key, it->second, 32);
			m_ItermediateSymmKeys.erase (it);
			return true;
		}
		LogPrint (eLogError, "Garlic: Missing symmetric key for index ", index);
		return false;
	}

	void RatchetTagSet::DeleteSymmKey (int index)
	{
		m_ItermediateSymmKeys.erase (index);
	}

	ReceiveRatchetTagSet::ReceiveRatchetTagSet (std::shared_ptr<ECIESX25519AEADRatchetSession> session, bool isNS)
		: m_Session (session)
		, m_IsNS (isNS)
	{
	}

	ReceiveRatchetTagSet::~ReceiveRatchetTagSet ()
	{
		if (m_IsNS && m_Session) m_Session->CleanupReceiveNSRKeys ();
	}

	void ReceiveRatchetTagSet::Expire ()
	{
		if (!m_ExpirationTimestamp)
			m_ExpirationTimestamp = i2p::util::GetSecondsSinceEpoch () + ECIESX25519_PREVIOUS_TAGSET_EXPIRATION_TIMEOUT;
	}

	bool ReceiveRatchetTagSet::IsExpired (uint64_t ts) const
	{
		return m_ExpirationTimestamp && ts > m_ExpirationTimestamp;
	}

	bool ReceiveRatchetTagSet::IsIndexExpired (int index) const
	{
		return index < m_TrimBehindIndex;
	}

	bool ReceiveRatchetTagSet::HandleNextMessage (uint8_t * buf, size_t len, int index)
	{
		auto session = GetSession ();
		if (!session) return false;
		return session->HandleNextMessage (buf, len, shared_from_this (), index);
	}

	bool ReceiveRatchetTagSet::IsSessionTerminated () const
	{
		return !m_Session || m_Session->IsTerminated ();
	}

	SymmetricKeyTagSet::SymmetricKeyTagSet (GarlicDestination * destination, const uint8_t * key)
		: ReceiveRatchetTagSet (nullptr)
		, m_Destination (destination)
	{
		memcpy (m_Key, key, 32);
		Expire ();
	}

	bool SymmetricKeyTagSet::HandleNextMessage (uint8_t * buf, size_t len, int /* index */)
	{
		if (len < 24) return false;
		uint8_t nonce[12];
		memset (nonce, 0, 12); // n = 0
		size_t offset = 8; // first 8 bytes is reply tag used as AD
		len -= 16; // poly1305
		if (!i2p::crypto::AEADChaCha20Poly1305 (
				buf + offset, len - offset, buf, 8, m_Key, nonce, buf + offset, len - offset, false)) // decrypt
		{
			LogPrint (eLogWarning, "Garlic: Symmetric key tagset AEAD decryption failed");
			return false;
		}
		// we assume 1 I2NP block with delivery type local
		if (offset + 3 > len)
		{
			LogPrint (eLogWarning, "Garlic: Symmetric key tagset is too short ", len);
			return false;
		}
		if (buf[offset] != eECIESx25519BlkGalicClove)
		{
			LogPrint (eLogWarning, "Garlic: Symmetric key tagset unexpected block ", (int)buf[offset]);
			return false;
		}
		offset++;
		auto size = bufbe16toh (buf + offset);
		offset += 2;
		if (offset + size > len)
		{
			LogPrint (eLogWarning, "Garlic: Symmetric key tagset block is too long ", size);
			return false;
		}
		if (m_Destination) m_Destination->HandleECIESx25519GarlicClove (buf + offset, size, nullptr);
		return true;
	}

	ECIESX25519AEADRatchetSession::ECIESX25519AEADRatchetSession (GarlicDestination * owner, bool attachLeaseSetNS)
		: GarlicRoutingSession (owner, true)
		, m_RemoteStaticKeyType (0)
	{
		if (!attachLeaseSetNS) SetLeaseSetUpdateStatus (eLeaseSetUpToDate);
	}

	ECIESX25519AEADRatchetSession::~ECIESX25519AEADRatchetSession ()
	{
		OPENSSL_cleanse (m_RemoteStaticKey, sizeof (m_RemoteStaticKey));
		OPENSSL_cleanse (m_Aepk, sizeof (m_Aepk));
		OPENSSL_cleanse (m_NSRH, sizeof (m_NSRH));
		OPENSSL_cleanse (m_NSRPostNSCK, sizeof (m_NSRPostNSCK));
	}

	void ECIESX25519AEADRatchetSession::CreateNonce (uint64_t seqn, uint8_t * nonce)
	{
		memset (nonce, 0, 4);
		htole64buf (nonce + 4, seqn);
	}

	bool ECIESX25519AEADRatchetSession::GenerateEphemeralKeysAndEncode (uint8_t * buf)
	{
		bool ineligible = false;
		while (!ineligible)
		{
			m_EphemeralKeys = i2p::transport::transports.GetNextX25519KeysPair ();
			if (!m_EphemeralKeys) return false;
			ineligible = m_EphemeralKeys->IsElligatorIneligible ();
			if (!ineligible) // we haven't tried it yet
			{
				if (i2p::crypto::GetElligator ()->Encode (m_EphemeralKeys->GetPublicKey (), buf))
					return true; // success
				// otherwise return back
				m_EphemeralKeys->SetElligatorIneligible ();
				i2p::transport::transports.ReuseX25519KeysPair (m_EphemeralKeys);
			}
			else
				i2p::transport::transports.ReuseX25519KeysPair (m_EphemeralKeys);
		}
		// we still didn't find elligator eligible pair
		for (int i = 0; i < 25; i++)
		{
			// create new
			m_EphemeralKeys = std::make_shared<i2p::crypto::X25519Keys> ();
			m_EphemeralKeys->GenerateKeys ();
			if (i2p::crypto::GetElligator ()->Encode (m_EphemeralKeys->GetPublicKey (), buf))
				return true; // success
			else
			{
				// let NTCP2 use it
				m_EphemeralKeys->SetElligatorIneligible ();
				i2p::transport::transports.ReuseX25519KeysPair (m_EphemeralKeys);
			}
		}
		LogPrint (eLogError, "Garlic: Can't generate elligator eligible x25519 keys");
		return false;
	}

	void ECIESX25519AEADRatchetSession::InitNewSessionTagset (std::shared_ptr<RatchetTagSet> tagsetNsr) const
	{
		uint8_t tagsetKey[32];
		i2p::crypto::HKDF (m_CK, nullptr, 0, "SessionReplyTags", tagsetKey,
			32); // tagsetKey = HKDF(chainKey, ZEROLEN, "SessionReplyTags", 32)
		// Session Tag Ratchet
		tagsetNsr->DHInitialize (m_CK, tagsetKey); // tagset_nsr = DH_INITIALIZE(chainKey, tagsetKey)
		tagsetNsr->NextSessionTagRatchet ();
	}

	bool ECIESX25519AEADRatchetSession::MessageConfirmed (uint32_t msgID)
	{
		auto ret = GarlicRoutingSession::MessageConfirmed (msgID); // LeaseSet
		if (m_AckRequestMsgID && m_AckRequestMsgID == msgID)
		{
			m_AckRequestMsgID = 0;
			m_AckRequestNumAttempts = 0;
			ret = true;
		}
		return ret;
	}

	bool ECIESX25519AEADRatchetSession::CleanupUnconfirmedTags ()
	{
		if (m_AckRequestMsgID && m_AckRequestNumAttempts > ECIESX25519_ACK_REQUEST_MAX_NUM_ATTEMPTS)
		{
			m_AckRequestMsgID = 0;
			m_AckRequestNumAttempts = 0;
			return true;
		}
		return false;
	}

	void ECIESX25519AEADRatchetSession::CleanupReceiveNSRKeys ()
	{
		m_EphemeralKeys = nullptr;
#if OPENSSL_PQ
		m_PQKeys = nullptr;
#endif
	}

	namespace
	{
		bool ValidateRatchetPayloadInternal (const uint8_t * buf, size_t len, bool handshake, bool requireGarlic)
		{
			if (len && !buf) return false;
			if (!len) return !handshake;
			bool gotDateTime = false, gotPadding = false, gotTermination = false, gotGarlic = false;
			int nextKeyBlocks = 0, forwardKeys = 0, reverseKeys = 0;
			size_t offset = 0;
			while (offset < len)
			{
				if (offset + 3 > len) return false;
				const uint8_t type = buf[offset++];
				const uint16_t size = bufbe16toh (buf + offset);
				offset += 2;
				if (gotPadding) return false;
				if (gotTermination && type != eECIESx25519BlkPadding) return false;
				if (offset + size > len) return false;
				switch (type)
				{
				case eECIESx25519BlkDateTime:
				{
					if (size != 4 || gotDateTime) return false;
					gotDateTime = true;
					// widen before adding: a 32-bit second count overflows int
					const uint64_t timestamp = bufbe32toh (buf + offset);
					const uint64_t now = i2p::util::GetSecondsSinceEpoch ();
					if (timestamp + ECIESX25519_DATETIME_MAX_AGE < now
						|| timestamp > now + ECIESX25519_DATETIME_MAX_FUTURE)
						return false;
					break;
				}
				case eECIESx25519BlkGalicClove:
					if (!size) return false; // Java's GarlicClove parser rejects it
					gotGarlic = true;
					break;
				case eECIESx25519BlkOptions: break;
				case eECIESx25519BlkNextKey:
					if (handshake || (size != 3 && size != 35)) return false;
					if (++nextKeyBlocks > 2) return false;
					if ((buf[offset] & ECIESX25519_NEXT_KEY_REVERSE_KEY_FLAG) ? ++reverseKeys > 1 : ++forwardKeys > 1)
						return false;
					break;
				case eECIESx25519BlkAck:
					if (size < 4 || (size & 3)) return false;
					break;
				case eECIESx25519BlkAckRequest:
					if (size < 1) return false;
					break;
				case eECIESx25519BlkTermination:
					if (handshake || size < 1) return false;
					gotTermination = true;
					break;
				case eECIESx25519BlkSessionID:
					if (handshake || size < 2) return false;
					break;
				case eECIESx25519BlkPadding: gotPadding = true; break;
				default:
					// Java ignores unknown blocks only outside the handshake.
					if (handshake) return false;
					break;
				}
				offset += size;
			}
			return !handshake || (gotDateTime && (!requireGarlic || gotGarlic));
		}
	} // namespace

	bool ValidateRatchetPayload (const uint8_t * buf, size_t len)
	{
		return ValidateRatchetPayloadInternal (buf, len, false, false);
	}

	bool ValidateNewSessionPayload (const uint8_t * buf, size_t len)
	{
		return ValidateRatchetPayloadInternal (buf, len, true, true);
	}

	bool ECIESX25519AEADRatchetSession::HandleNewIncomingSession (const uint8_t * buf, size_t len)
	{
		if (!GetOwner ()) return false;
		// we are Bob
		// KDF1

		if (len < 32 || !i2p::crypto::GetElligator ()->Decode (buf, m_Aepk))
		{
			LogPrint (eLogError, "Garlic: Can't decode elligator");
			return false;
		}
		if (GetOwner ()->IsDuplicateNewSessionKey (m_Aepk))
		{
			LogPrint (eLogWarning, "Garlic: Duplicate ECIES-X25519 new session ephemeral key");
			return false;
		}
		buf += 32;
		len -= 32;

		// The sender selects one key from our published LeaseSet and is not
		// required to use the strongest one, so try every ratchet key type we
		// advertise. The list is fixed and bounded; no attacker-supplied type
		// is accepted.
		static constexpr i2p::data::CryptoKeyType candidates[]
			= { i2p::data::CRYPTO_KEY_TYPE_ECIES_MLKEM1024_X25519_AEAD,
				  i2p::data::CRYPTO_KEY_TYPE_ECIES_MLKEM768_X25519_AEAD,
				  i2p::data::CRYPTO_KEY_TYPE_ECIES_MLKEM512_X25519_AEAD, i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD };
		bool hasCandidates = false;
		for (auto cryptoType : candidates)
		{
			if (cryptoType > i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD)
			{
#if OPENSSL_PQ
				// LibreSSL does not provide ML-KEM-512.
				if (!i2p::crypto::IsMLKEMSupported (cryptoType)) continue;
#else
				continue;
#endif
			}
			if (!GetOwner ()->SupportsEncryptionType (cryptoType) || !GetOwner ()->GetEncryptionPublicKey (cryptoType))
				continue;
			hasCandidates = true;
			if (HandleNewIncomingSessionCandidate (cryptoType, buf, len)) return true;
		}

		if (hasCandidates)
			LogPrint (eLogWarning, "Garlic: New session decryption failed for all supported encryption types");
		else
			LogPrint (eLogError, "Garlic: No supported encryption type");
		return false;
	}

	bool ECIESX25519AEADRatchetSession::HandleNewIncomingSessionCandidate (
		i2p::data::CryptoKeyType cryptoType, const uint8_t * buf, size_t len)
	{
		auto owner = GetOwner ();
		if (!owner) return false;
		const uint8_t * bpk = owner->GetEncryptionPublicKey (cryptoType);
		if (!bpk) return false;
		// flags/static section, payload MAC and a mandatory DateTime block
		static constexpr size_t MIN_PAYLOAD_LEN = 7;
		if (len < 48 + 16 + MIN_PAYLOAD_LEN) return false;

		// Trial handshake state. The session's Noise state, static key and PQ
		// key are committed only after every AEAD tag and the payload have
		// been authenticated.
		i2p::crypto::NoiseSymmetricState noiseState;
		uint8_t sharedSecret[32];
#if OPENSSL_PQ
		std::unique_ptr<i2p::crypto::MLKEMKeys> pqKeys;
		if (cryptoType > i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD)
		{
			if (!i2p::crypto::IsMLKEMSupported (cryptoType)) return false;
			auto keyLen = i2p::crypto::GetMLKEMPublicKeyLen (cryptoType);
			// encrypted ML-KEM public key + MAC before flags/static and payload
			if (!keyLen || keyLen + 16 + 48 + 16 + MIN_PAYLOAD_LEN > len) return false;

			i2p::crypto::InitNoiseIKStateMLKEM (noiseState, cryptoType, bpk); // bpk
			noiseState.MixHash (m_Aepk, 32); // h = SHA256(h || aepk)
			if (!owner->Decrypt (m_Aepk, sharedSecret, cryptoType) || IsZeroBuf (sharedSecret, 32))
				return false; // x25519(bsk, aepk)
			noiseState.MixKey (sharedSecret);

			std::vector<uint8_t> encapsKey (keyLen);
			if (!noiseState.Decrypt (buf, encapsKey.data (), keyLen)) return false; // encaps section has right hash
			noiseState.MixHash (buf, keyLen + 16);
			buf += keyLen + 16;
			len -= keyLen + 16;

			pqKeys = i2p::crypto::CreateMLKEMKeys (cryptoType);
			if (!pqKeys || !pqKeys->SetPublicKey (encapsKey.data ())) return false;
		}
		else
#else
		if (cryptoType != i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD) return false;
#endif
		{
			i2p::crypto::InitNoiseIKState (noiseState, bpk); // bpk
			noiseState.MixHash (m_Aepk, 32); // h = SHA256(h || aepk)
			if (!owner->Decrypt (m_Aepk, sharedSecret, cryptoType) || IsZeroBuf (sharedSecret, 32))
				return false; // x25519(bsk, aepk)
			noiseState.MixKey (sharedSecret);
		}

		// decrypt flags/static
		if (len < 48) return false;
		uint8_t fs[32];
		if (!noiseState.Decrypt (buf, fs, 32)) return false;
		noiseState.MixHash (buf, 48); // h = SHA256(h || ciphertext)
		buf += 48;
		len -= 48; // 32 data + 16 poly

		// KDF2 for payload
		bool isStatic = !i2p::data::Tag<32> (fs).IsZero ();
		if (isStatic)
		{
			// static key, fs is apk
			if (!owner->Decrypt (fs, sharedSecret, cryptoType) || IsZeroBuf (sharedSecret, 32))
				return false; // x25519(bsk, apk)
			noiseState.MixKey (sharedSecret);
		}

		// decrypt payload
		if (len < 16) return false;
		std::vector<uint8_t> payload (len - 16); // we must save original ciphertext
		if (!noiseState.Decrypt (buf, payload.data (), len - 16)) return false;

		if (!ValidateNewSessionPayload (payload.data (), len - 16))
		{
			LogPrint (eLogWarning, "Garlic: Invalid ECIES-X25519 new session payload");
			return false;
		}

		// Authenticated: commit the handshake and session state.
		SetNoiseState (noiseState);
#if OPENSSL_PQ
		m_PQKeys = std::move (pqKeys);
#endif
		if (isStatic)
		{
			SetRemoteStaticKey (cryptoType, fs);
			MixHash (buf, len); // h = SHA256(h || ciphertext)
			// Snapshot the state the New Session ended with. Each New Session
			// Reply, including a retransmission, is derived from here.
			memcpy (m_NSRH, m_H, 32);
			memcpy (m_NSRPostNSCK, m_CK, 32);
			m_NSRStateReady = true;
			m_State = eSessionStateNewSessionReceived;
			// Start the inbound replacement-age clock now, not at the first
			// NSR. Otherwise CanBeRestarted() is always true for a pending
			// inbound session.
			m_SessionCreatedTimestamp = i2p::util::GetSecondsSinceEpoch ();
			owner->AddECIESx25519Session (m_RemoteStaticKey, shared_from_this ());
		}
		else
		{
			// Anonymous (N-in-IK) New Session: process it once, never reply.
#if OPENSSL_PQ
			m_PQKeys = nullptr; // don't retain KEM key
#endif
			m_NSRStateReady = false;
		}
		owner->AddNewSessionKey (m_Aepk);
		HandlePayload (payload.data (), len - 16, nullptr, 0);

		return true;
	}

	void ECIESX25519AEADRatchetSession::HandlePayload (
		const uint8_t * buf, size_t len, const std::shared_ptr<ReceiveRatchetTagSet> & receiveTagset, int index)
	{
		if (len < 3) return;
		size_t offset = 0;
		bool gotDateTime = false, gotPadding = false, gotTermination = false;
		int nextKeyBlocks = 0, forwardKeys = 0, reverseKeys = 0;
		// the block header is three bytes: without all of them the size field below
		// would be read past the end of the payload
		while (offset < len)
		{
			if (offset + 3 > len)
			{
				LogPrint (eLogError, "Garlic: Truncated block header");
				break;
			}
			uint8_t blk = buf[offset];
			offset++;
			auto size = bufbe16toh (buf + offset);
			offset += 2;
			LogPrint (eLogDebug, "Garlic: Block type ", (int)blk, " of size ", size);
			if (gotPadding)
			{
				LogPrint (eLogError, "Garlic: Block after padding ", (int)blk);
				break;
			}
			if (gotTermination && blk != eECIESx25519BlkPadding)
			{
				LogPrint (eLogError, "Garlic: Block after termination ", (int)blk);
				break;
			}
			if (offset + size > len)
			{
				LogPrint (eLogError, "Garlic: Unexpected block length ", size);
				break;
			}
			switch (blk)
			{
			case eECIESx25519BlkGalicClove:
				if (GetOwner ()) GetOwner ()->HandleECIESx25519GarlicClove (buf + offset, size, this);
				break;
			case eECIESx25519BlkNextKey:
				LogPrint (eLogDebug, "Garlic: Next key");
				if (++nextKeyBlocks > 2)
				{
					LogPrint (eLogError, "Garlic: Too many NEXT_KEY blocks in one payload");
					return;
				}
				if (size != 3 && size != 35)
				{
					LogPrint (eLogError, "Garlic: Invalid next key block length ", size);
					break;
				}
				if (buf[offset] & ECIESX25519_NEXT_KEY_REVERSE_KEY_FLAG)
				{
					if (++reverseKeys > 1)
					{
						LogPrint (eLogError, "Garlic: Duplicate reverse NEXT_KEY block");
						break;
					}
				}
				else if (++forwardKeys > 1)
				{
					// Java RatchetSKM accepts at most one forward and one reverse
					// NEXT_KEY per payload; otherwise one authenticated message
					// can allocate multiple 800-tag windows.
					LogPrint (eLogError, "Garlic: Duplicate forward NEXT_KEY block");
					break;
				}
				if (receiveTagset)
					HandleNextKey (buf + offset, size, receiveTagset);
				else
					LogPrint (eLogError, "Garlic: Unexpected next key block");
				break;
			case eECIESx25519BlkAck:
			{
				LogPrint (eLogDebug, "Garlic: Ack");
				if (size < 4 || (size & 3))
				{
					LogPrint (eLogError, "Garlic: Invalid ack block length ", size);
					break;
				}
				if (receiveTagset)
				{
					// ACKs in a handshake payload have no established session
					// to confirm. Java ignores them instead of letting an
					// unauthenticated New Session sender clear delivery state.
					int numAcks = size >> 2; // /4
					auto offset1 = offset;
					for (auto i = 0; i < numAcks; i++)
					{
						uint32_t tagsetid = bufbe16toh (buf + offset1);
						offset1 += 2; // tagsetid
						uint16_t n = bufbe16toh (buf + offset1);
						offset1 += 2; // N
						MessageConfirmed ((static_cast<uint32_t> (tagsetid) << 16) | n); // msgid = (tagsetid << 16) + N
					}
				}
				else
					LogPrint (eLogDebug, "Garlic: Ignoring ack block in a handshake");
				break;
			}
			case eECIESx25519BlkAckRequest:
			{
				LogPrint (eLogDebug, "Garlic: Ack request");
				if (size < 1)
				{
					LogPrint (eLogError, "Garlic: Invalid ack request length ", size);
					break;
				}
				if (receiveTagset)
				{
					const int tagsetID = receiveTagset->GetTagSetID ();
					if (tagsetID < 0 || tagsetID > 0xFFFF || index < 0 || index > 0xFFFF)
					{
						// A wider ID cannot be encoded in the 4-byte ACK entry
						// and would confirm an unrelated message ID.
						LogPrint (eLogWarning, "Garlic: Ack request index is not encodable");
					}
					else if (m_AckRequests.size () < ECIESX25519_MAX_PENDING_ACKS)
						m_AckRequests.push_back ({ static_cast<uint16_t> (tagsetID), index });
					else
						LogPrint (eLogWarning, "Garlic: Too many pending ack requests, dropping one");
				}
				break;
			}
			case eECIESx25519BlkTermination:
				LogPrint (eLogDebug, "Garlic: Termination");
				if (size < 1)
				{
					LogPrint (eLogError, "Garlic: Invalid termination length ", size);
					break;
				}
				gotTermination = true;
				if (GetOwner ()) GetOwner ()->RemoveECIESx25519Session (m_RemoteStaticKey);
				if (receiveTagset) receiveTagset->Expire ();
				break;
			case eECIESx25519BlkDateTime:
				if (size != 4 || gotDateTime)
				{
					LogPrint (eLogError, "Garlic: Invalid datetime block");
					break;
				}
				gotDateTime = true;
				{
					// Java's PayloadCallback.gotDateTime() applies the same
					// window to every payload, not only to handshakes.
					const uint64_t timestamp = bufbe32toh (buf + offset);
					const uint64_t now = i2p::util::GetSecondsSinceEpoch ();
					if (timestamp + ECIESX25519_DATETIME_MAX_AGE < now
						|| timestamp > now + ECIESX25519_DATETIME_MAX_FUTURE)
					{
						LogPrint (eLogWarning, "Garlic: Excess clock skew in datetime block");
						break;
					}
				}
				LogPrint (eLogDebug, "Garlic: Datetime");
				break;
			case eECIESx25519BlkOptions: LogPrint (eLogDebug, "Garlic: Options"); break;
			case eECIESx25519BlkPadding:
				LogPrint (eLogDebug, "Garlic: Padding");
				gotPadding = true;
				break;
			default: LogPrint (eLogWarning, "Garlic: Unknown block type ", (int)blk);
			}
			offset += size;
		}
		if (IsResponseRequired () && GetOwner ()) GetOwner ()->ScheduleSessionResponseTimer (shared_from_this ());
	}

	void ECIESX25519AEADRatchetSession::HandleNextKey (
		const uint8_t * buf, size_t len, const std::shared_ptr<ReceiveRatchetTagSet> & receiveTagset)
	{
		if (len < 3)
		{
			LogPrint (eLogWarning, "Garlic: Next key block is too short ", len);
			return;
		}
		uint8_t flag = buf[0];
		buf++; // flag
		const bool hasKey = flag & ECIESX25519_NEXT_KEY_KEY_PRESENT_FLAG;
		const bool isReverse = flag & ECIESX25519_NEXT_KEY_REVERSE_KEY_FLAG;
		const bool isRequest = flag & ECIESX25519_NEXT_KEY_REQUEST_REVERSE_KEY_FLAG;
		if (hasKey && len < 35)
		{
			LogPrint (eLogWarning, "Garlic: Next key block with key is too short ", len);
			return;
		}
		if (isReverse && isRequest)
		{
			LogPrint (eLogWarning, "Garlic: Invalid reverse+request next key");
			return;
		}
		uint16_t keyID = bufbe16toh (buf);
		buf += 2; // keyID
		if (isReverse)
		{
			// This acknowledges one of our outbound keys and creates the next
			// send tagset. Match the expected key ID before changing state.
			if (!m_SendForwardKey || !m_NextSendRatchet || !m_NextSendRatchet->key || !m_SendTagset) return;
			if (!(((!m_NextSendRatchet->newKey || !m_NextSendRatchet->keyID) && keyID == m_NextSendRatchet->keyID)
					|| (m_NextSendRatchet->newKey && keyID == m_NextSendRatchet->keyID - 1)))
			{
				LogPrint (eLogDebug, "Garlic: Unexpected next key ", keyID);
				return;
			}
			// Java RatchetSKM.nextKeyReceived() only accepts an increasing
			// reverse key ID, or the already stored one without key data.
			if (hasKey)
			{
				if (m_NextSendRatchet->remoteKeyPresent && keyID <= m_NextSendRatchet->remoteKeyID)
				{
					LogPrint (eLogWarning, "Garlic: Duplicate or stale reverse next key ", keyID);
					return;
				}
			}
			else if (!m_NextSendRatchet->remoteKeyPresent || m_NextSendRatchet->remoteKeyID != (int)keyID)
			{
				LogPrint (eLogWarning, "Garlic: Reverse next key without a matching stored key ", keyID);
				return;
			}
			uint8_t remote[32];
			memcpy (remote, hasKey ? buf : m_NextSendRatchet->remote, 32);
			uint8_t sharedSecret[32], tagsetKey[32];
			if (!AgreeNonZero (*m_NextSendRatchet->key, remote, sharedSecret))
			{
				LogPrint (eLogWarning, "Garlic: Invalid reverse next key ", keyID);
				return;
			}
			i2p::crypto::HKDF (sharedSecret, nullptr, 0, "XDHRatchetTagSet", tagsetKey,
				32); // tagsetKey = HKDF(sharedSecret, ZEROLEN, "XDHRatchetTagSet", 32)
			auto newTagset = std::make_shared<RatchetTagSet> ();
			newTagset->SetTagSetID (1 + m_NextSendRatchet->keyID + keyID);
			newTagset->DHInitialize (m_SendTagset->GetNextRootKey (), tagsetKey);
			newTagset->NextSessionTagRatchet ();
			if (hasKey)
			{
				memcpy (m_NextSendRatchet->remote, remote, 32);
				m_NextSendRatchet->remoteKeyPresent = true;
				m_NextSendRatchet->remoteKeyID = keyID;
			}
			m_SendTagset = newTagset;
			m_SendForwardKey = false;
			LogPrint (eLogDebug, "Garlic: Next send tagset ", newTagset->GetTagSetID (), " created");
		}
		else
		{
			// This announces the peer's next inbound key (or repeats an old
			// key ID when the peer is reusing a key pair). Java accepts an old
			// ID with or without the request flag, so the request bit is not
			// used to decide whether a new reverse key pair is generated.
			if (!m_NextReceiveRatchet)
			{
				if (keyID != 0 || !hasKey)
				{
					LogPrint (eLogWarning, "Garlic: First forward next key needs key 0 with data");
					return;
				}
			}
			else
			{
				const int lastID = m_NextReceiveRatchet->keyID;
				if (keyID == lastID)
				{
					if (hasKey)
					{
						LogPrint (eLogWarning, "Garlic: Old next key ID ", keyID, " carried new key data");
						return;
					}
					if (!m_NextReceiveRatchet->remoteKeyPresent)
					{
						LogPrint (eLogWarning, "Garlic: Forward next key has no stored key ", keyID);
						return;
					}
					if (!m_NextReceiveRatchet->lastHadKey)
					{
						LogPrint (eLogDebug, "Garlic: Duplicate old key ", keyID, " received");
						return;
					}
				}
				else if (keyID == lastID + 1)
				{
					if (!hasKey)
					{
						LogPrint (eLogWarning, "Garlic: New next key ID ", keyID, " has no key data");
						return;
					}
				}
				else
				{
					LogPrint (eLogWarning, "Garlic: Unexpected forward next key ", keyID, " after ", lastID);
					return;
				}
			}
			// Java generates a fresh reverse key pair for the first key and
			// whenever the peer repeats an ID (even tagset), and reuses the
			// previous pair for a new ID (odd tagset). The request flag alone
			// must not control this, so a peer that omits it still ratchets.
			const bool generate = !m_NextReceiveRatchet || keyID == m_NextReceiveRatchet->keyID;
			uint8_t remote[32];
			memcpy (remote, hasKey ? buf : m_NextReceiveRatchet->remote, 32);
			std::shared_ptr<i2p::crypto::X25519Keys> key;
			if (generate)
			{
				key = i2p::transport::transports.GetNextX25519KeysPair ();
				if (!key) return;
			}
			else
			{
				if (!m_NextReceiveRatchet || !m_NextReceiveRatchet->key)
				{
					LogPrint (eLogError, "Garlic: Forward next key received, but key is not set");
					return;
				}
				key = m_NextReceiveRatchet->key;
			}
			uint8_t sharedSecret[32], tagsetKey[32];
			if (!AgreeNonZero (*key, remote, sharedSecret))
			{
				LogPrint (eLogWarning, "Garlic: Invalid forward next key ", keyID);
				return;
			}
			i2p::crypto::HKDF (sharedSecret, nullptr, 0, "XDHRatchetTagSet", tagsetKey,
				32); // tagsetKey = HKDF(sharedSecret, ZEROLEN, "XDHRatchetTagSet", 32)
			if (!m_NextReceiveRatchet) m_NextReceiveRatchet.reset (new DHRatchet ());
			m_NextReceiveRatchet->keyID = keyID;
			m_NextReceiveRatchet->key = key;
			memcpy (m_NextReceiveRatchet->remote, remote, 32);
			m_NextReceiveRatchet->remoteKeyPresent = true;
			m_NextReceiveRatchet->lastHadKey = hasKey;
			m_NextReceiveRatchet->newKey = generate;
			const int tagsetID = m_NextReceiveRatchet->GetReceiveTagSetID ();
			auto newTagset = std::make_shared<ReceiveRatchetTagSet> (shared_from_this ());
			newTagset->SetTagSetID (tagsetID);
			newTagset->DHInitialize (receiveTagset->GetNextRootKey (), tagsetKey);
			newTagset->NextSessionTagRatchet ();
			GenerateMoreReceiveTags (newTagset,
				(GetOwner () && GetOwner ()->GetNumRatchetInboundTags () > 0) ? GetOwner ()->GetNumRatchetInboundTags ()
																			  : ECIESX25519_MAX_NUM_GENERATED_TAGS);
			receiveTagset->Expire ();

			LogPrint (eLogDebug, "Garlic: Next receive tagset ", tagsetID, " created");
			m_SendReverseKey = true;
		}
	}

	void ECIESX25519AEADRatchetSession::NewNextSendRatchet ()
	{
		if (m_NextSendRatchet)
		{
			if (!m_NextSendRatchet->newKey || !m_NextSendRatchet->keyID)
			{
				m_NextSendRatchet->keyID++;
				m_NextSendRatchet->newKey = true;
			}
			else
				m_NextSendRatchet->newKey = false;
		}
		else
			m_NextSendRatchet.reset (new DHRatchet ());
		if (m_NextSendRatchet->newKey)
		{
			m_NextSendRatchet->key = i2p::transport::transports.GetNextX25519KeysPair ();
			if (!m_NextSendRatchet->key)
			{
				LogPrint (eLogError, "Garlic: Can't create new X25519 keys pair for send ratchet");
				return;
			}
		}

		m_SendForwardKey = true;
		LogPrint (eLogDebug, "Garlic: New send ratchet ", m_NextSendRatchet->newKey ? "new" : "old", " key ",
			m_NextSendRatchet->keyID, " created");
	}

	void ECIESX25519AEADRatchetSession::RestoreNewSessionState ()
	{
		memcpy (m_H, m_NSRH, 32);
		memcpy (m_CK, m_NSRPostNSCK, 32);
		memset (m_CK + 32, 0, 32); // k is set by the first MixKey
		m_N = 0;
	}

	bool ECIESX25519AEADRatchetSession::NewOutgoingSessionMessage (
		const uint8_t * payload, size_t len, uint8_t * out, size_t outLen, bool isStatic)
	{
		// we are Alice, bpk is m_RemoteStaticKey
		auto owner = GetOwner ();
		if (!owner || !out) return false;
		if (i2p::data::Tag<32> (m_RemoteStaticKey).IsZero ())
		{
			LogPrint (eLogError, "Garlic: Zero static key for New Session");
			return false;
		}
		if (outLen < 32 + 48 + 16) return false;

		size_t offset = 0;
		if (!GenerateEphemeralKeysAndEncode (out + offset))
		{
			LogPrint (eLogError, "Garlic: Can't encode elligator");
			return false;
		}
		offset += 32;

		// Trial state: a failed build must not leave a half-mixed chain key
		// behind for a retry on the same session object.
		i2p::crypto::NoiseSymmetricState noiseState;
#if OPENSSL_PQ
		std::unique_ptr<i2p::crypto::MLKEMKeys> pqKeys;
		if (m_RemoteStaticKeyType >= i2p::data::CRYPTO_KEY_TYPE_ECIES_MLKEM512_X25519_AEAD)
		{
			i2p::crypto::InitNoiseIKStateMLKEM (noiseState, m_RemoteStaticKeyType, m_RemoteStaticKey); // bpk
			pqKeys = i2p::crypto::CreateMLKEMKeys (m_RemoteStaticKeyType);
			if (!pqKeys || !pqKeys->GenerateKeys ()) return false;
		}
		else
#endif
			i2p::crypto::InitNoiseIKState (noiseState, m_RemoteStaticKey); // bpk
		noiseState.MixHash (m_EphemeralKeys->GetPublicKey (), 32); // h = SHA256(h || aepk)
		uint8_t sharedSecret[32];
		if (!AgreeNonZero (*m_EphemeralKeys, m_RemoteStaticKey, sharedSecret)) // x25519(aesk, bpk)
		{
			LogPrint (eLogWarning, "Garlic: Incorrect Bob static key");
			return false;
		}
		noiseState.MixKey (sharedSecret);
#if OPENSSL_PQ
		if (pqKeys)
		{
			auto keyLen = i2p::crypto::GetMLKEMPublicKeyLen (m_RemoteStaticKeyType);
			if (!keyLen || offset + keyLen + 16 > outLen) return false;
			std::vector<uint8_t> encapsKey (keyLen);
			if (!pqKeys->GetPublicKey (encapsKey.data ())) return false;
			// encrypt encapsKey
			if (!noiseState.Encrypt (encapsKey.data (), out + offset, keyLen))
			{
				LogPrint (eLogWarning, "Garlic: ML-KEM encap_key section AEAD encryption failed ");
				return false;
			}
			noiseState.MixHash (out + offset, keyLen + 16); // h = SHA256(h || ciphertext)
			offset += keyLen + 16;
		}
#endif
		if (offset + 48 + 16 > outLen) return false;
		// encrypt flags/static key section
		const uint8_t * fs;
		if (isStatic)
		{
			fs = owner->GetEncryptionPublicKey (m_RemoteStaticKeyType);
			if (!fs)
			{
				LogPrint (eLogError, "Garlic: No local static key for encryption type ", m_RemoteStaticKeyType);
				return false;
			}
		}
		else
		{
			memset (out + offset, 0, 32); // all zeros flags section
			fs = out + offset;
		}
		if (!noiseState.Encrypt (fs, out + offset, 32))
		{
			LogPrint (eLogWarning, "Garlic: Flags/static section AEAD encryption failed ");
			return false;
		}

		noiseState.MixHash (out + offset, 48); // h = SHA256(h || ciphertext)
		offset += 48;
		// KDF2
		if (isStatic)
		{
			if (!owner->Decrypt (m_RemoteStaticKey, sharedSecret, m_RemoteStaticKeyType)
				|| IsZeroBuf (sharedSecret, 32)) // x25519 (ask, bpk)
			{
				LogPrint (
					eLogWarning, "Garlic: Incorrect local static key for encryption type ", m_RemoteStaticKeyType);
				return false;
			}
			noiseState.MixKey (sharedSecret);
		}
		// encrypt payload
		if (offset + len + 16 > outLen)
		{
			LogPrint (eLogError, "Garlic: New Session payload does not fit");
			return false;
		}
		if (!noiseState.Encrypt (payload, out + offset, len))
		{
			LogPrint (eLogWarning, "Garlic: Payload section AEAD encryption failed");
			return false;
		}

		// Authenticated build complete: commit the handshake state.
		if (isStatic)
		{
			noiseState.MixHash (out + offset, len + 16); // h = SHA256(h || ciphertext)
			SetNoiseState (noiseState);
#if OPENSSL_PQ
			m_PQKeys = std::move (pqKeys);
#endif
			auto tagsetNsr = std::make_shared<ReceiveRatchetTagSet> (shared_from_this (), true);
			InitNewSessionTagset (tagsetNsr);
			tagsetNsr->Expire (); // let non-replied session expire
			GenerateMoreReceiveTags (tagsetNsr, ECIESX25519_NSR_NUM_GENERATED_TAGS);
			m_State = eSessionStateNewSessionSent;
		}
		else
		{
			// One-time/anonymous New Session: no reply is expected, and the
			// receiver must not create a session for the zero static key.
			SetNoiseState (noiseState);
#if OPENSSL_PQ
			m_PQKeys = nullptr; // no reply, no KEM private key to retain
#endif
			m_State = eSessionStateOneTime;
		}
		return true;
	}

	bool ECIESX25519AEADRatchetSession::CreateNewSessionReplyMessage (
		const uint8_t * payload, size_t len, uint8_t * out, size_t outLen)
	{
		// we are Bob
		auto owner = GetOwner ();
		if (!owner || !out) return false;
		if (!m_NSRStateReady)
		{
			LogPrint (eLogError, "Garlic: No New Session state to reply from");
			return false;
		}
		if (i2p::data::Tag<32> (m_RemoteStaticKey).IsZero ())
		{
			LogPrint (eLogError, "Garlic: Can't reply to an anonymous New Session");
			return false;
		}

		// Every reply, including a retransmission, repeats the whole Noise IK
		// message from the chain key the New Session ended with. Spec 1g
		// requires a fresh ephemeral key for every New Session Reply.
		RestoreNewSessionState ();
		if (!m_NSRSendTagset)
		{
			m_NSRSendTagset = std::make_shared<RatchetTagSet> ();
			InitNewSessionTagset (m_NSRSendTagset);
		}
		uint64_t tag = m_NSRSendTagset->GetNextSessionTag (owner->GetHKDFContext ());
		if (!tag)
		{
			LogPrint (eLogError, "Garlic: Can't create New Session Reply tag");
			return false;
		}

		if (outLen < 8 + 32 + 16 + 16) return false;
		size_t offset = 0;
		memcpy (out + offset, &tag, 8);
		offset += 8;
		if (!GenerateEphemeralKeysAndEncode (out + offset)) // fresh bepk
		{
			LogPrint (eLogError, "Garlic: Can't encode elligator");
			return false;
		}
		const uint8_t * bepk = m_EphemeralKeys->GetPublicKey ();
		offset += 32;
		// KDF for Reply Key Section
		MixHash (out, 8); // h = SHA256(h || tag)
		MixHash (bepk, 32); // h = SHA256(h || bepk)
		uint8_t sharedSecret[32];
		if (!AgreeNonZero (*m_EphemeralKeys, m_Aepk, sharedSecret)) // sharedSecret = x25519(besk, aepk)
		{
			LogPrint (eLogWarning, "Garlic: Incorrect Alice ephemeral key");
			return false;
		}
		MixKey (sharedSecret);
#if OPENSSL_PQ
		if (m_PQKeys)
		{
			size_t cipherTextLen = i2p::crypto::GetMLKEMCipherTextLen (m_RemoteStaticKeyType);
			if (!cipherTextLen || offset + cipherTextLen + 16 > outLen) return false;
			std::vector<uint8_t> kemCiphertext (cipherTextLen);
			if (!m_PQKeys->Encaps (kemCiphertext.data (), sharedSecret)) return false;

			if (!Encrypt (kemCiphertext.data (), out + offset, cipherTextLen))
			{
				LogPrint (eLogWarning, "Garlic: NSR ML-KEM ciphertext section AEAD encryption failed");
				return false;
			}
			MixHash (out + offset, cipherTextLen + 16);
			MixKey (sharedSecret);
			offset += cipherTextLen + 16;
		}
#endif
		if (!AgreeNonZero (*m_EphemeralKeys, m_RemoteStaticKey, sharedSecret)) // sharedSecret = x25519(besk, apk)
		{
			LogPrint (eLogWarning, "Garlic: Incorrect Alice static key");
			return false;
		}
		MixKey (sharedSecret);
		// calculate hash for zero length
		if (!Encrypt (m_NSRH /* can be anything */, out + offset,
				0)) // encrypt, ciphertext = ENCRYPT(k, n, ZEROLEN, ad)
		{
			LogPrint (eLogWarning, "Garlic: Reply key section AEAD encryption failed");
			return false;
		}
		MixHash (out + offset, 16); // h = SHA256(h || ciphertext)
		offset += 16;
		// KDF for payload
		uint8_t keydata[64];
		i2p::crypto::HKDF (m_CK, nullptr, 0, "", keydata); // keydata = HKDF(chainKey, ZEROLEN, "", 64)
		// k_ab = keydata[0:31], k_ba = keydata[32:63]
		auto receiveTagset = std::make_shared<ReceiveRatchetTagSet> (shared_from_this ());
		receiveTagset->DHInitialize (m_CK, keydata); // tagset_ab = DH_INITIALIZE(chainKey, k_ab)
		receiveTagset->NextSessionTagRatchet ();
		auto sendTagset = std::make_shared<RatchetTagSet> ();
		sendTagset->DHInitialize (m_CK, keydata + 32); // tagset_ba = DH_INITIALIZE(chainKey, k_ba)
		sendTagset->NextSessionTagRatchet ();
		uint8_t payloadKey[32];
		i2p::crypto::HKDF (keydata + 32, nullptr, 0, "AttachPayloadKDF", payloadKey,
			32); // k = HKDF(k_ba, ZEROLEN, "AttachPayloadKDF", 32)
		// encrypt payload
		if (offset + len + 16 > outLen)
		{
			LogPrint (eLogError, "Garlic: New Session Reply payload does not fit");
			return false;
		}
		uint8_t nonce[12];
		memset (nonce, 0, 12); // seqn = 0
		if (!i2p::crypto::AEADChaCha20Poly1305 (
				payload, len, m_H, 32, payloadKey, nonce, out + offset, len + 16, true)) // encrypt
		{
			LogPrint (eLogWarning, "Garlic: NSR payload section AEAD encryption failed");
			return false;
		}
		// Authenticated build complete: install the split keys. Earlier
		// tagsets stay registered through their tags, so delayed messages sent
		// before the peer saw this reply still decrypt.
		m_SendTagset = sendTagset;
		GenerateMoreReceiveTags (receiveTagset,
			(owner->GetNumRatchetInboundTags () > 0) ? owner->GetNumRatchetInboundTags ()
													 : ECIESX25519_MIN_NUM_GENERATED_TAGS);
		return true;
	}

	bool ECIESX25519AEADRatchetSession::NewSessionReplyMessage (
		const uint8_t * payload, size_t len, uint8_t * out, size_t outLen)
	{
		if (!CreateNewSessionReplyMessage (payload, len, out, outLen)) return false;
		m_State = eSessionStateNewSessionReplySent;
		m_SessionCreatedTimestamp = i2p::util::GetSecondsSinceEpoch ();
		return true;
	}

	bool ECIESX25519AEADRatchetSession::NextNewSessionReplyMessage (
		const uint8_t * payload, size_t len, uint8_t * out, size_t outLen)
	{
		// we are Bob and sent an NSR already; the next one is a full new
		// handshake message with a fresh ephemeral key and a new tag
		return CreateNewSessionReplyMessage (payload, len, out, outLen);
	}

	bool ECIESX25519AEADRatchetSession::HandleNewOutgoingSessionReply (uint8_t * buf, size_t len)
	{
		// we are Alice
		LogPrint (eLogDebug, "Garlic: Reply received");
		auto owner = GetOwner ();
		if (!owner || !m_EphemeralKeys) return false;
		if (len < 8 + 32 + 16 + 16)
		{
			LogPrint (eLogWarning, "Garlic: Reply message is too short ", len);
			return false;
		}
		const uint8_t * tag = buf;
		buf += 8;
		len -= 8; // tag
		uint8_t bepk[32]; // Bob's ephemeral key
		if (!i2p::crypto::GetElligator ()->Decode (buf, bepk))
		{
			LogPrint (eLogError, "Garlic: Can't decode elligator");
			return false;
		}
		buf += 32;
		len -= 32;
		// KDF for Reply Key Section
		i2p::util::SaveStateHelper<i2p::crypto::NoiseSymmetricState> s (
			GetNoiseState ()); // restore noise state on exit
		MixHash (tag, 8); // h = SHA256(h || tag)
		MixHash (bepk, 32); // h = SHA256(h || bepk)
		uint8_t sharedSecret[32];
		if (!AgreeNonZero (*m_EphemeralKeys, bepk, sharedSecret)) // sharedSecret = x25519(aesk, bepk)
		{
			LogPrint (eLogWarning, "Garlic: Incorrect Bob ephemeral key");
			return false;
		}
		MixKey (sharedSecret);
#if OPENSSL_PQ
		if (m_RemoteStaticKeyType >= i2p::data::CRYPTO_KEY_TYPE_ECIES_MLKEM512_X25519_AEAD)
		{
			// decrypt kem_ciphertext section
			size_t cipherTextLen = i2p::crypto::GetMLKEMCipherTextLen (m_RemoteStaticKeyType);
			if (cipherTextLen + 16 > len || !cipherTextLen)
			{
				LogPrint (eLogWarning, "Garlic: ML-KEM cipher test section is too short ", len, ". Expected ",
					cipherTextLen + 16);
				return false;
			}
			std::vector<uint8_t> kemCiphertext (cipherTextLen);
			if (!Decrypt (buf, kemCiphertext.data (), cipherTextLen))
			{
				LogPrint (eLogWarning, "Garlic: Reply ML-KEM ciphertext section AEAD decryption failed");
				return false;
			}
			MixHash (buf, cipherTextLen + 16);
			buf += cipherTextLen + 16;
			len -= cipherTextLen + 16;
			// decaps
			if (!m_PQKeys || !m_PQKeys->Decaps (kemCiphertext.data (), sharedSecret) || IsZeroBuf (sharedSecret, 32))
				return false;
			MixKey (sharedSecret);
		}
#endif
		if (!owner->Decrypt (bepk, sharedSecret, m_RemoteStaticKeyType)
			|| IsZeroBuf (sharedSecret, 32)) // x25519 (ask, bepk)
		{
			LogPrint (eLogWarning, "Garlic: Incorrect local static key for reply type ", m_RemoteStaticKeyType);
			return false;
		}
		MixKey (sharedSecret);

		// calculate hash for zero length
		if (len < 16)
		{
			LogPrint (eLogWarning, "Garlic: Zero length section is too short ", len);
			return false;
		}
		if (!Decrypt (
				buf, sharedSecret /* can be anything */, 0)) // decrypt, DECRYPT(k, n, ZEROLEN, ad) verification only
		{
			LogPrint (eLogWarning, "Garlic: Reply key section AEAD decryption failed");
			return false;
		}
		MixHash (buf, 16); // h = SHA256(h || ciphertext)
		buf += 16;
		len -= 16;
		// KDF for payload
		uint8_t keydata[64];
		i2p::crypto::HKDF (m_CK, nullptr, 0, "", keydata); // keydata = HKDF(chainKey, ZEROLEN, "", 64)
		// Every reply (a retransmission carries a fresh ephemeral key) derives
		// a new split. Install it only after the payload is authenticated.
		// k_ab = keydata[0:31], k_ba = keydata[32:63]
		auto sendTagset = std::make_shared<RatchetTagSet> ();
		sendTagset->DHInitialize (m_CK, keydata); // tagset_ab = DH_INITIALIZE(chainKey, k_ab)
		sendTagset->NextSessionTagRatchet ();
		auto receiveTagset = std::make_shared<ReceiveRatchetTagSet> (shared_from_this ());
		receiveTagset->DHInitialize (m_CK, keydata + 32); // tagset_ba = DH_INITIALIZE(chainKey, k_ba)
		receiveTagset->NextSessionTagRatchet ();
		i2p::crypto::HKDF (keydata + 32, nullptr, 0, "AttachPayloadKDF", keydata,
			32); // k = HKDF(k_ba, ZEROLEN, "AttachPayloadKDF", 32)
		// decrypt payload
		if (len < 16)
		{
			LogPrint (eLogWarning, "Garlic: Payload section is too short ", len);
			return false;
		}
		uint8_t nonce[12];
		memset (nonce, 0, 12); // seqn = 0
		if (!i2p::crypto::AEADChaCha20Poly1305 (
				buf, len - 16, m_H, 32, keydata, nonce, buf, len - 16, false)) // decrypt
		{
			LogPrint (eLogWarning, "Garlic: Payload section AEAD decryption failed");
			return false;
		}

		// Parse the complete reply before committing the new split or
		// delivering any clove. Java discards an NSR with invalid block
		// framing/DateTime/order as a unit.
		if (!ValidateRatchetPayload (buf, len - 16))
		{
			LogPrint (eLogWarning, "Garlic: Invalid New Session Reply payload");
			return false;
		}

		m_SendTagset = sendTagset;
		GenerateMoreReceiveTags (receiveTagset,
			(owner->GetNumRatchetInboundTags () > 0) ? owner->GetNumRatchetInboundTags ()
													 : ECIESX25519_MIN_NUM_GENERATED_TAGS);
		if (m_State == eSessionStateNewSessionSent)
		{
			m_State = eSessionStateEstablished;
			// don't delete m_EphemeralKey and m_PQKeys because delayed NSR's might come
			// done in CleanupReceiveNSRKeys called from NSR tagset destructor
			m_SessionCreatedTimestamp = i2p::util::GetSecondsSinceEpoch ();
			owner->AddECIESx25519Session (m_RemoteStaticKey, shared_from_this ());
		}
		HandlePayload (buf, len - 16, nullptr, 0);

		// we have received reply to NS with LeaseSet in it
		SetLeaseSetUpdateStatus (eLeaseSetUpToDate);
		SetLeaseSetUpdateMsgID (0);

		return true;
	}

	bool ECIESX25519AEADRatchetSession::NewExistingSessionMessage (
		const uint8_t * payload, size_t len, uint8_t * out, size_t outLen)
	{
		auto owner = GetOwner ();
		if (!owner || !m_SendTagset || !out || (len && !payload)) return false;
		if (outLen < 8 || len > outLen - 8 || outLen - 8 - len < 16) return false;
		uint8_t nonce[12];
		auto index = m_SendTagset->GetNextIndex ();
		CreateNonce (index, nonce); // tag's index
		uint64_t tag = m_SendTagset->GetNextSessionTag (GetOwner ()->GetHKDFContext ());
		if (!tag)
		{
			LogPrint (eLogError, "Garlic: Can't create new ECIES-X25519-AEAD-Ratchet tag for send tagset");
			owner->RemoveECIESx25519Session (m_RemoteStaticKey);
			return false;
		}
		memcpy (out, &tag, 8);
		// ad = The session tag, 8 bytes
		// ciphertext = ENCRYPT(k, n, payload, ad)
		uint8_t key[32];
		if (!m_SendTagset->GetSymmKey (index, key))
		{
			LogPrint (eLogError, "Garlic: Can't create symmetric key for tag index ", index);
			return false;
		}
		if (!owner->AEADChaCha20Poly1305Encrypt (payload, len, out, 8, key, nonce, out + 8, outLen - 8))
		{
			LogPrint (eLogWarning, "Garlic: Payload section AEAD encryption failed");
			return false;
		}
		if (index >= ECIESX25519_TAGSET_MAX_NUM_TAGS && !m_SendForwardKey) NewNextSendRatchet ();
		return true;
	}

	bool ECIESX25519AEADRatchetSession::HandleExistingSessionMessage (
		uint8_t * buf, size_t len, std::shared_ptr<ReceiveRatchetTagSet> receiveTagset, int index)
	{
		if (len < 8)
		{
			LogPrint (eLogWarning, "Garlic: Existing session message is too short ", len);
			return false;
		}
		uint8_t nonce[12];
		CreateNonce (index, nonce); // tag's index
		len -= 8; // tag
		uint8_t * payload = buf + 8;
		uint8_t key[32];
		if (!receiveTagset || !receiveTagset->GetSymmKey (index, key))
		{
			LogPrint (eLogWarning, "Garlic: Symmetric key for tag index ", index, " is not available");
			return false;
		}
		auto owner = GetOwner ();
		if (!owner) return true; // drop message

		if (len < 16 || !owner->AEADChaCha20Poly1305Decrypt (payload, len - 16, buf, 8, key, nonce, payload, len - 16))
		{
			LogPrint (eLogWarning, "Garlic: Payload section AEAD decryption failed");
			return false;
		}
		// Validate the entire payload before applying cloves, ratchet keys,
		// ACKs or termination. A malformed tail must not leave a prefix
		// delivered/applied (Java throws before the callback state is used).
		if (!ValidateRatchetPayload (payload, len - 16))
		{
			LogPrint (eLogWarning, "Garlic: Invalid existing-session payload");
			return false;
		}
		HandlePayload (payload, len - 16, receiveTagset, index);

		int moreTags = 0;
		if (owner->GetNumRatchetInboundTags () > 0) // override in settings?
		{
			if (receiveTagset->GetNextIndex () - index < owner->GetNumRatchetInboundTags () / 2)
				moreTags = owner->GetNumRatchetInboundTags ();
			index -= owner->GetNumRatchetInboundTags (); // trim behind
		}
		else
		{
			moreTags = (receiveTagset->GetTagSetID () > 0) ? ECIESX25519_MAX_NUM_GENERATED_TAGS
														   : // for non first tagset
				(ECIESX25519_MIN_NUM_GENERATED_TAGS + 2 * index); // 2*N
			if (moreTags > ECIESX25519_MAX_NUM_GENERATED_TAGS) moreTags = ECIESX25519_MAX_NUM_GENERATED_TAGS;
			moreTags -= (receiveTagset->GetNextIndex () - index);
			index -= ECIESX25519_MAX_NUM_GENERATED_TAGS; // trim behind
		}
		if (moreTags > 0) GenerateMoreReceiveTags (receiveTagset, moreTags);
		if (index > 0) receiveTagset->SetTrimBehind (index);
		return true;
	}

	bool ECIESX25519AEADRatchetSession::HandleNextMessage (
		uint8_t * buf, size_t len, std::shared_ptr<ReceiveRatchetTagSet> receiveTagset, int index)
	{
		if (IsTerminated ()) return false;
		m_LastActivityTimestamp = i2p::util::GetSecondsSinceEpoch ();
		switch (m_State)
		{
		case eSessionStateNewSessionReplySent:
			m_State = eSessionStateEstablished;
			m_NSRSendTagset = nullptr;
			m_EphemeralKeys = nullptr;
			m_NSRStateReady = false;
#if OPENSSL_PQ
			m_PQKeys = nullptr;
#endif
			[[fallthrough]];
		case eSessionStateEstablished:
			if (!receiveTagset) return false;
			if (m_SendReverseKey && m_NextReceiveRatchet
				&& receiveTagset->GetTagSetID () == m_NextReceiveRatchet->GetReceiveTagSetID ())
				m_SendReverseKey = false; // tag received on new tagset
			if (receiveTagset->IsNS ())
			{
				// our of sequence NSR
				LogPrint (eLogDebug, "Garlic: Check for out of order NSR with index ", index);
				if (receiveTagset->GetNextIndex () - index < ECIESX25519_NSR_NUM_GENERATED_TAGS / 2)
					GenerateMoreReceiveTags (receiveTagset, ECIESX25519_NSR_NUM_GENERATED_TAGS);
				return HandleNewOutgoingSessionReply (buf, len);
			}
			else
				return HandleExistingSessionMessage (buf, len, receiveTagset, index);
		case eSessionStateNew: return HandleNewIncomingSession (buf, len);
		case eSessionStateNewSessionSent: return HandleNewOutgoingSessionReply (buf, len);
		default: return false;
		}
		return true;
	}

	std::shared_ptr<I2NPMessage> ECIESX25519AEADRatchetSession::WrapSingleMessage (
		std::shared_ptr<const I2NPMessage> msg)
	{
		auto owner = GetOwner ();
		if (!owner) return nullptr;
		auto payload = std::make_unique<uint8_t[]> (I2NP_MAX_MESSAGE_SIZE);
		size_t len = CreatePayload (msg, m_State != eSessionStateEstablished, payload.get ());
		if (!len) return nullptr;
		return WrapPayload (payload.get (), len);
	}

	std::vector<std::shared_ptr<I2NPMessage>> ECIESX25519AEADRatchetSession::WrapMultipleMessages (
		const std::vector<std::shared_ptr<const I2NPMessage>> & msgs)
	{
		if (m_State != eSessionStateEstablished) return GarlicRoutingSession::WrapMultipleMessages (msgs);

		std::vector<std::shared_ptr<I2NPMessage>> ret;
		if (!msgs.empty ())
		{
			ret.push_back (WrapSingleMessage (msgs[0]));
			if (msgs.size () > 1)
			{
				if (!GetOwner ()) return ret;
				auto payloadBuffer = std::make_unique<uint8_t[]> (I2NP_MAX_MESSAGE_SIZE);
				uint8_t * payload = payloadBuffer.get ();
				size_t len = 0;
				auto it = msgs.begin ();
				it++;
				while (it != msgs.end ())
				{
					if (*it != nullptr)
					{
						// These cloves are delivered locally (alwaysLocal), so
						// no destination hash is added.
						const size_t cloveOverhead = 13;
						if ((*it)->GetPayloadLength () + cloveOverhead + len > ECIESX25519_OPTIMAL_PAYLOAD_SIZE)
						{
							auto paddingSize = GetNextPaddingSize (len);
							if (paddingSize)
							{
								const auto added
									= CreatePaddingClove (paddingSize, payload + len, I2NP_MAX_MESSAGE_SIZE - len);
								if (!added)
								{
									LogPrint (eLogWarning, "Garlic: Dropping oversized batch before padding");
									return ret;
								}
								len += added;
							}
							ret.push_back (WrapPayload (payload, len));
							len = 0;
						}
						len += CreateGarlicClove (*it, payload + len, I2NP_MAX_MESSAGE_SIZE - len, true);
					}
					it++;
				}
				if (len > 0)
				{
					auto paddingSize = GetNextPaddingSize (len);
					if (paddingSize)
					{
						const auto added = CreatePaddingClove (paddingSize, payload + len, I2NP_MAX_MESSAGE_SIZE - len);
						if (!added)
						{
							LogPrint (eLogWarning, "Garlic: Dropping oversized final batch before padding");
							return ret;
						}
						len += added;
					}
					ret.push_back (WrapPayload (payload, len));
				}
			}
		}
		return ret;
	}

	std::shared_ptr<I2NPMessage> ECIESX25519AEADRatchetSession::WrapPayload (const uint8_t * payload, size_t len)
	{
#if OPENSSL_PQ
		auto m = NewI2NPMessage (len
			+ (m_State == eSessionStateEstablished ? 28
												   : i2p::crypto::GetMLKEMPublicKeyLen (m_RemoteStaticKeyType) + 116));
#else
		auto m = NewI2NPMessage (len + 100); // 96 + 4
#endif
		if (!m) return nullptr;
		m->Align (12); // in order to get buf aligned to 16 (12 + 4)
		uint8_t * buf = m->GetPayload () + 4; // 4 bytes for length
		const size_t used = static_cast<size_t> (buf - m->buf);
		if (used >= m->maxLen) return nullptr;
		const size_t capacity = m->maxLen - used;

		switch (m_State)
		{
		case eSessionStateEstablished:
			if (!NewExistingSessionMessage (payload, len, buf, capacity)) return nullptr;
			len += 24;
			break;
		case eSessionStateNew:
			if (!NewOutgoingSessionMessage (payload, len, buf, capacity)) return nullptr;
			len += 96;
#if OPENSSL_PQ
			if (m_RemoteStaticKeyType >= i2p::data::CRYPTO_KEY_TYPE_ECIES_MLKEM512_X25519_AEAD)
				len += i2p::crypto::GetMLKEMPublicKeyLen (m_RemoteStaticKeyType) + 16;
#endif
			break;
		case eSessionStateNewSessionReceived:
			if (!NewSessionReplyMessage (payload, len, buf, capacity)) return nullptr;
			len += 72;
#if OPENSSL_PQ
			if (m_RemoteStaticKeyType >= i2p::data::CRYPTO_KEY_TYPE_ECIES_MLKEM512_X25519_AEAD)
				len += i2p::crypto::GetMLKEMCipherTextLen (m_RemoteStaticKeyType) + 16;
#endif
			break;
		case eSessionStateNewSessionReplySent:
			if (!NextNewSessionReplyMessage (payload, len, buf, capacity)) return nullptr;
			len += 72;
#if OPENSSL_PQ
			if (m_RemoteStaticKeyType >= i2p::data::CRYPTO_KEY_TYPE_ECIES_MLKEM512_X25519_AEAD)
				len += i2p::crypto::GetMLKEMCipherTextLen (m_RemoteStaticKeyType) + 16;
#endif
			break;
		case eSessionStateOneTime:
			if (!NewOutgoingSessionMessage (payload, len, buf, capacity, false)) return nullptr;
			len += 96;
#if OPENSSL_PQ
			// The anonymous New Session still carries the local hybrid key
			// when the remote static key type is hybrid.
			if (m_RemoteStaticKeyType >= i2p::data::CRYPTO_KEY_TYPE_ECIES_MLKEM512_X25519_AEAD)
				len += i2p::crypto::GetMLKEMPublicKeyLen (m_RemoteStaticKeyType) + 16;
#endif
			break;
		default: return nullptr;
		}

		htobe32buf (m->GetPayload (), static_cast<uint32_t> (len));
		m->len += len + 4;
		m->FillI2NPMessageHeader (eI2NPGarlic, GetOwner ()->GetRng () ());
		return m;
	}

	std::shared_ptr<I2NPMessage> ECIESX25519AEADRatchetSession::WrapOneTimeMessage (
		std::shared_ptr<const I2NPMessage> msg)
	{
		m_State = eSessionStateOneTime;
		return WrapSingleMessage (msg);
	}

	size_t ECIESX25519AEADRatchetSession::CreatePayload (
		std::shared_ptr<const I2NPMessage> msg, bool first, uint8_t * payload)
	{
		if (!payload) return 0;
		if (!first && !m_SendTagset) return 0;
		uint64_t ts = i2p::util::GetMillisecondsSinceEpoch ();
		size_t payloadLen = 0;
		bool sendAckRequest = false;
		bool leaseSetSubmission = false;
		if (first) payloadLen += 7; // datatime
		if (msg)
		{
			payloadLen += msg->GetPayloadLength () + 13;
			if (m_Destination) payloadLen += 32;
		}
		if (GetLeaseSetUpdateStatus () == eLeaseSetSubmitted
			&& ts > GetLeaseSetSubmissionTime () + LEASESET_CONFIRMATION_TIMEOUT)
		{
			// resubmit non-confirmed LeaseSet
			SetLeaseSetUpdateStatus (eLeaseSetUpdated);
			SetSharedRoutingPath (nullptr); // invalidate path since leaseset was not confirmed
		}
		auto leaseSet = (GetLeaseSetUpdateStatus () == eLeaseSetUpdated) ? GetOwner ()->GetLeaseSet () : nullptr;
		if (leaseSet)
		{
			payloadLen += leaseSet->GetBufferLen () + DATABASE_STORE_HEADER_SIZE + 13;
			if (!first)
			{
				// ack request for LeaseSet; the submission state is committed
				// only after the clove has actually been written. If the
				// tagset ID is too wide for the ACK wire field, send the
				// LeaseSet without an acknowledgment the peer cannot return.
				const uint32_t msgID = m_SendTagset->GetMsgID ();
				if (msgID)
				{
					m_AckRequestMsgID = msgID;
					sendAckRequest = true;
					leaseSetSubmission = true;
				}
				else
					LogPrint (eLogWarning, "Garlic: Can't request an ACK for this LeaseSet tagset");
			}
		}
		if (!sendAckRequest && !first
			&& ((!m_AckRequestMsgID && ts > m_LastAckRequestSendTime + m_AckRequestInterval) || // regular request
				(m_AckRequestMsgID
					&& ts > m_LastAckRequestSendTime
							+ LEASESET_CONFIRMATION_TIMEOUT))) // previous request failed. try again
		{
			// not LeaseSet
			m_AckRequestMsgID = m_SendTagset->GetMsgID ();
			if (m_AckRequestMsgID)
			{
				m_AckRequestNumAttempts++;
				sendAckRequest = true;
			}
		}
		if (sendAckRequest) payloadLen += 4;
		// Java RatchetSKM drains at most MAX_SEND_ACKS per message; sending an
		// unbounded ACK block can overflow I2NP_MAX_MESSAGE_SIZE and stop the
		// session from ever sending again.
		const size_t numAcks = std::min<size_t> (m_AckRequests.size (), ECIESX25519_MAX_ACKS_PER_MESSAGE);
		if (numAcks) payloadLen += numAcks * 4 + 3;
		if (m_SendReverseKey)
		{
			payloadLen += 6;
			if (m_NextReceiveRatchet->newKey) payloadLen += 32;
		}
		if (m_SendForwardKey)
		{
			payloadLen += 6;
			if (m_NextSendRatchet->newKey) payloadLen += 32;
		}
		uint8_t paddingSize = 0;
		if (payloadLen || ts > m_LastSentTimestamp + ECIESX25519_SEND_INACTIVITY_TIMEOUT)
		{
			paddingSize = GetNextPaddingSize (payloadLen);
			if (paddingSize) payloadLen += paddingSize + 3;
		}
		if (payloadLen)
		{
			if (payloadLen > I2NP_MAX_MESSAGE_SIZE)
			{
				LogPrint (eLogError, "Garlic: Payload length ", payloadLen, " is too long");
				if (leaseSetSubmission) m_AckRequestMsgID = 0;
				return 0;
			}
			const uint64_t previousLastSentTimestamp = m_LastSentTimestamp;
			const uint64_t previousAckRequestSendTime = m_LastAckRequestSendTime;
			auto abortPayload = [&] () -> size_t
			{
				if (leaseSetSubmission)
				{
					m_AckRequestMsgID = 0;
					m_LastSentTimestamp = previousLastSentTimestamp;
					m_LastAckRequestSendTime = previousAckRequestSendTime;
				}
				return 0;
			};
			m_LastSentTimestamp = ts;
			size_t offset = 0;
			// DateTime
			if (first)
			{
				payload[offset] = eECIESx25519BlkDateTime;
				offset++;
				htobe16buf (payload + offset, 4);
				offset += 2;
				htobe32buf (payload + offset, static_cast<uint32_t> (ts / 1000));
				offset += 4; // in seconds
			}
			// LeaseSet
			if (leaseSet)
			{
				const auto added = CreateLeaseSetClove (leaseSet, ts, payload + offset, payloadLen - offset);
				if (!added)
				{
					// Do not encrypt a payload with an unwritten clove slot.
					LogPrint (eLogError, "Garlic: Can't write the LeaseSet clove");
					return abortPayload ();
				}
				offset += added;
			}
			// ack request
			if (sendAckRequest)
			{
				payload[offset] = eECIESx25519BlkAckRequest;
				offset++;
				htobe16buf (payload + offset, 1);
				offset += 2;
				payload[offset] = 0;
				offset++; // flags
				m_LastAckRequestSendTime = ts;
			}
			// msg
			if (msg)
			{
				const auto added = CreateGarlicClove (msg, payload + offset, payloadLen - offset);
				if (!added)
				{
					LogPrint (eLogError, "Garlic: Can't write the message clove");
					return abortPayload ();
				}
				offset += added;
			}
			// ack
			if (numAcks)
			{
				payload[offset] = eECIESx25519BlkAck;
				offset++;
				htobe16buf (payload + offset, static_cast<uint16_t> (numAcks * 4));
				offset += 2;
				auto it = m_AckRequests.begin ();
				for (size_t i = 0; i < numAcks; i++)
				{
					htobe16buf (payload + offset, it->first);
					offset += 2;
					htobe16buf (payload + offset, static_cast<uint16_t> (it->second));
					offset += 2;
					++it;
				}
				m_AckRequests.erase (m_AckRequests.begin (), it);
			}
			// next keys
			if (m_SendReverseKey)
			{
				payload[offset] = eECIESx25519BlkNextKey;
				offset++;
				htobe16buf (payload + offset, m_NextReceiveRatchet->newKey ? 35 : 3);
				offset += 2;
				payload[offset] = ECIESX25519_NEXT_KEY_REVERSE_KEY_FLAG;
				int keyID = m_NextReceiveRatchet->keyID - 1;
				if (m_NextReceiveRatchet->newKey)
				{
					payload[offset] |= ECIESX25519_NEXT_KEY_KEY_PRESENT_FLAG;
					keyID++;
				}
				offset++; // flag
				htobe16buf (payload + offset, static_cast<uint16_t> (keyID));
				offset += 2; // keyid
				if (m_NextReceiveRatchet->newKey)
				{
					memcpy (payload + offset, m_NextReceiveRatchet->key->GetPublicKey (), 32);
					offset += 32; // public key
				}
			}
			if (m_SendForwardKey)
			{
				payload[offset] = eECIESx25519BlkNextKey;
				offset++;
				htobe16buf (payload + offset, m_NextSendRatchet->newKey ? 35 : 3);
				offset += 2;
				payload[offset] = m_NextSendRatchet->newKey ? ECIESX25519_NEXT_KEY_KEY_PRESENT_FLAG
															: ECIESX25519_NEXT_KEY_REQUEST_REVERSE_KEY_FLAG;
				// A reused old key always requests a reverse key; the first
				// new key also does (Java RatchetTagSet.getNextKey()).
				if (!m_NextSendRatchet->newKey || !m_NextSendRatchet->keyID)
					payload[offset] |= ECIESX25519_NEXT_KEY_REQUEST_REVERSE_KEY_FLAG;
				offset++; // flag
				htobe16buf (payload + offset, static_cast<uint16_t> (m_NextSendRatchet->keyID));
				offset += 2; // keyid
				if (m_NextSendRatchet->newKey)
				{
					memcpy (payload + offset, m_NextSendRatchet->key->GetPublicKey (), 32);
					offset += 32; // public key
				}
			}
			// padding
			if (paddingSize)
			{
				const auto added = CreatePaddingClove (paddingSize, payload + offset, payloadLen - offset);
				if (!added) return abortPayload ();
				offset += added;
			}
			if (leaseSetSubmission)
			{
				SetLeaseSetUpdateStatus (eLeaseSetSubmitted);
				SetLeaseSetUpdateMsgID (m_AckRequestMsgID);
				SetLeaseSetSubmissionTime (ts);
			}
		}
		return payloadLen;
	}

	uint8_t ECIESX25519AEADRatchetSession::GetNextPaddingSize (size_t payloadLen)
	{
		uint8_t paddingSize = 0;
		int delta = (int)ECIESX25519_OPTIMAL_PAYLOAD_SIZE - (int)payloadLen;
		if (delta < 0 || delta > 3) // don't create padding if we are close to optimal size
		{
			paddingSize = GetOwner ()->GetRng () () % 16; // 0 - 15
			if (delta > 3)
			{
				delta -= 3;
				if (paddingSize >= delta) paddingSize %= delta;
			}
			paddingSize++;
		}
		return paddingSize;
	}

	size_t ECIESX25519AEADRatchetSession::CreateGarlicClove (
		std::shared_ptr<const I2NPMessage> msg, uint8_t * buf, size_t len, bool alwaysLocal)
	{
		if (!msg) return 0;
		// The clove length is a 16-bit wire field.
		const size_t destinationOverhead = m_Destination ? 32 : 0;
		if (msg->GetPayloadLength () > 0xFFFF - 10 - destinationOverhead) return 0;
		uint16_t cloveSize = static_cast<uint16_t> (msg->GetPayloadLength () + 9 + 1);
		if (m_Destination) cloveSize += 32;
		if ((int)len < cloveSize + 3) return 0;
		buf[0] = eECIESx25519BlkGalicClove; // clove type
		htobe16buf (buf + 1, cloveSize); // size
		buf += 3;
		if (!alwaysLocal && m_Destination)
		{
			*buf = (eGarlicDeliveryTypeDestination << 5);
			memcpy (buf + 1, *m_Destination, 32);
			buf += 32;
		}
		else
			*buf = 0;
		buf++; // flag and delivery instructions
		*buf = msg->GetTypeID (); // I2NP msg type
		htobe32buf (buf + 1, msg->GetMsgID ()); // msgID
		htobe32buf (buf + 5, static_cast<uint32_t> (msg->GetExpiration () / 1000)); // expiration in seconds
		memcpy (buf + 9, msg->GetPayload (), msg->GetPayloadLength ());
		return cloveSize + 3;
	}

	size_t ECIESX25519AEADRatchetSession::CreateLeaseSetClove (
		std::shared_ptr<const i2p::data::LocalLeaseSet> ls, uint64_t ts, uint8_t * buf, size_t len)
	{
		if (!ls || ls->GetStoreType () != i2p::data::NETDB_STORE_TYPE_STANDARD_LEASESET2)
		{
			LogPrint (eLogError, "Garlic: Incorrect LeaseSet type to send");
			return 0;
		}
		uint16_t cloveSize
			= static_cast<uint16_t> (1 + 9 + DATABASE_STORE_HEADER_SIZE + ls->GetBufferLen ()); // to local
		if ((int)len < cloveSize + 3) return 0;
		buf[0] = eECIESx25519BlkGalicClove; // clove type
		htobe16buf (buf + 1, cloveSize); // size
		buf += 3;
		*buf = 0;
		buf++; // flag and delivery instructions
		*buf = eI2NPDatabaseStore;
		buf++; // I2NP msg type
		if (RAND_bytes (buf, 4) != 1)
		{
			LogPrint (eLogError, "Garlic: Can't generate the LeaseSet message ID");
			return 0;
		}
		buf += 4; // msgID
		htobe32buf (buf, static_cast<uint32_t> ((ts + I2NP_MESSAGE_EXPIRATION_TIMEOUT) / 1000));
		buf += 4; // expiration
		// payload
		memcpy (buf + DATABASE_STORE_KEY_OFFSET, ls->GetStoreHash (), 32);
		buf[DATABASE_STORE_TYPE_OFFSET] = i2p::data::NETDB_STORE_TYPE_STANDARD_LEASESET2;
		memset (buf + DATABASE_STORE_REPLY_TOKEN_OFFSET, 0, 4); // replyToken = 0
		buf += DATABASE_STORE_HEADER_SIZE;
		memcpy (buf, ls->GetBuffer (), ls->GetBufferLen ());

		return cloveSize + 3;
	}

	size_t ECIESX25519AEADRatchetSession::CreatePaddingClove (uint8_t paddingSize, uint8_t * buf, size_t len)
	{
		if (!buf || len < static_cast<size_t> (paddingSize) + 3)
		{
			LogPrint (eLogWarning, "Garlic: Padding does not fit in the payload buffer");
			return 0;
		}
		buf[0] = eECIESx25519BlkPadding;
		htobe16buf (buf + 1, paddingSize);
		memset (buf + 3, 0, paddingSize);
		return paddingSize + 3;
	}

	void ECIESX25519AEADRatchetSession::GenerateMoreReceiveTags (
		std::shared_ptr<ReceiveRatchetTagSet> receiveTagset, int numTags)
	{
		if (GetOwner ())
		{
			for (int i = 0; i < numTags; i++)
			{
				auto tag = GetOwner ()->AddECIESx25519SessionNextTag (receiveTagset);
				if (!tag)
				{
					LogPrint (eLogError, "Garlic: Can't create new ECIES-X25519-AEAD-Ratchet tag for receive tagset");
					break;
				}
			}
		}
	}

	bool ECIESX25519AEADRatchetSession::CheckExpired (uint64_t ts)
	{
		CleanupUnconfirmedLeaseSet (ts);
		if (!m_Destination && ts > m_LastActivityTimestamp + ECIESX25519_SESSION_CREATE_TIMEOUT)
			return true; // m_LastActivityTimestamp is NS receive time
		if (m_State != eSessionStateEstablished && m_SessionCreatedTimestamp
			&& ts > m_SessionCreatedTimestamp + ECIESX25519_SESSION_ESTABLISH_TIMEOUT)
			return true;
		return ts > m_LastActivityTimestamp + ECIESX25519_RECEIVE_EXPIRATION_TIMEOUT && // seconds
			ts * 1000 > m_LastSentTimestamp + ECIESX25519_SEND_EXPIRATION_TIMEOUT * 1000; // milliseconds
	}

	RouterIncomingRatchetSession::RouterIncomingRatchetSession (const i2p::crypto::NoiseSymmetricState & initState)
		: ECIESX25519AEADRatchetSession (&i2p::context, false)
	{
		SetLeaseSetUpdateStatus (eLeaseSetDoNotSend);
		SetNoiseState (initState);
	}

	bool RouterIncomingRatchetSession::HandleNextMessage (const uint8_t * buf, size_t len)
	{
		if (!GetOwner ()) return false;
		// 32-byte ephemeral key, payload with a mandatory DateTime block and
		// the 16-byte MAC (Java ECIESAEADEngine.MIN_NS_N_SIZE = 55).
		if (len < 32 + 7 + 16)
		{
			LogPrint (eLogWarning, "Garlic: Message for router is too short ", len);
			return false;
		}
		std::array<uint8_t, 32> ephemeral;
		memcpy (ephemeral.data (), buf, ephemeral.size ());
		m_CurrentNoiseState = GetNoiseState ();
		// we are Bob
		m_CurrentNoiseState.MixHash (buf, 32);
		uint8_t sharedSecret[32];
		if (!GetOwner ()->Decrypt (buf, sharedSecret, i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD)
			|| IsZeroBuf (sharedSecret, 32)) // x25519(bsk, aepk)
		{
			LogPrint (eLogWarning, "Garlic: Incorrect N ephemeral public key");
			return false;
		}
		m_CurrentNoiseState.MixKey (sharedSecret);
		buf += 32;
		len -= 32;
		if (len < 16)
		{
			LogPrint (eLogWarning, "Garlic: Payload for router is too short ", len);
			return false;
		}
		uint8_t nonce[12];
		CreateNonce (0, nonce);
		std::vector<uint8_t> payload (len - 16);
		if (!i2p::crypto::AEADChaCha20Poly1305 (buf, len - 16, m_CurrentNoiseState.m_H, 32,
				m_CurrentNoiseState.m_CK + 32, nonce, payload.data (), len - 16, false)) // decrypt
		{
			LogPrint (eLogWarning, "Garlic: Payload for router AEAD verification failed");
			return false;
		}
		if (!ValidateNewSessionPayload (payload.data (), payload.size ()))
		{
			LogPrint (eLogWarning, "Garlic: Invalid ECIES-X25519 message payload for router");
			return false;
		}
		{
			const auto now = i2p::util::GetSecondsSinceEpoch ();
			std::lock_guard<std::mutex> l (m_EphemeralReplayMutex);
			constexpr uint64_t REPLAY_WINDOW = 600;
			while (!m_EphemeralReplay.empty () && now > m_EphemeralReplay.front ().second + REPLAY_WINDOW)
				m_EphemeralReplay.pop_front ();
			for (const auto & entry : m_EphemeralReplay)
				if (entry.first == ephemeral)
				{
					LogPrint (eLogWarning, "Garlic: Duplicate router N ephemeral key ignored");
					return false;
				}
			constexpr size_t MAX_REPLAY_ENTRIES = 256;
			if (m_EphemeralReplay.size () >= MAX_REPLAY_ENTRIES) m_EphemeralReplay.pop_front ();
			m_EphemeralReplay.emplace_back (ephemeral, now);
		}
		HandlePayload (payload.data (), len - 16, nullptr, 0);
		return true;
	}

	static size_t CreateGarlicPayload (
		std::shared_ptr<const I2NPMessage> msg, uint8_t * payload, bool datetime, size_t optimalSize)
	{
		size_t len = 0;
		if (datetime)
		{
			// DateTime
			payload[0] = eECIESx25519BlkDateTime;
			htobe16buf (payload + 1, 4);
			htobe32buf (payload + 3, static_cast<uint32_t> (i2p::util::GetSecondsSinceEpoch ()));
			len = 7;
		}
		// I2NP
		payload += len;
		if (msg->GetPayloadLength () > 0xFFFF - 10) return 0;
		uint16_t cloveSize = static_cast<uint16_t> (msg->GetPayloadLength () + 10);
		payload[0] = eECIESx25519BlkGalicClove; // clove type
		htobe16buf (payload + 1, cloveSize); // size
		payload += 3;
		payload[0] = 0; // flag and delivery instructions
		payload[1] = msg->GetTypeID (); // I2NP msg type
		htobe32buf (payload + 2, msg->GetMsgID ()); // msgID
		htobe32buf (payload + 6, static_cast<uint32_t> (msg->GetExpiration () / 1000)); // expiration in seconds
		memcpy (payload + 10, msg->GetPayload (), msg->GetPayloadLength ());
		len += cloveSize + 3;
		payload += cloveSize;
		// padding
		int delta = (int)optimalSize - (int)len;
		if (delta < 0 || delta > 3) // don't create padding if we are close to optimal size
		{
			uint8_t paddingSize = static_cast<uint8_t> (i2p::crypto::RandomBelow (16)); // 0 - 15
			if (delta > 3)
			{
				delta -= 3;
				if (paddingSize > delta) paddingSize %= delta;
			}
			payload[0] = eECIESx25519BlkPadding;
			htobe16buf (payload + 1, paddingSize);
			if (paddingSize) memset (payload + 3, 0, paddingSize);
			len += paddingSize + 3;
		}
		return len;
	}

	std::shared_ptr<I2NPMessage> WrapECIESX25519Message (
		std::shared_ptr<I2NPMessage> msg, const uint8_t * key, uint64_t tag)
	{
		if (!msg || !key) return nullptr;
		auto m = NewI2NPMessage (msg->GetPayloadLength () + 128);
		if (!m) return nullptr;
		m->Align (12); // in order to get buf aligned to 16 (12 + 4)
		uint8_t * buf = m->GetPayload () + 4; // 4 bytes for length
		const size_t used = static_cast<size_t> (buf - m->buf);
		if (used >= m->maxLen) return nullptr;
		const size_t capacity = m->maxLen - used;
		size_t offset = 0;
		// tag, clove header/fields, worst-case padding and Poly1305 tag.
		constexpr size_t MAX_PADDING = 18; // 3-byte header + 15 bytes
		constexpr size_t MAX_POLY1305 = 16;
		if (msg->GetPayloadLength () > capacity
			|| capacity - msg->GetPayloadLength () < 8 + 13 + MAX_PADDING + MAX_POLY1305)
		{
			LogPrint (eLogError, "Garlic: Message does not fit the ECIES envelope");
			return nullptr;
		}
		memcpy (buf + offset, &tag, 8);
		offset += 8;
		auto payload = buf + offset;
		size_t len = CreateGarlicPayload (msg, payload, false,
			956); // 1003 - 8 tag - 16 Poly1305 hash - 16 I2NP header - 4 garlic length - 3 local tunnel delivery
		if (!len || len + MAX_POLY1305 > capacity - offset)
		{
			LogPrint (eLogError, "Garlic: Bad ECIES payload length ", len);
			return nullptr;
		}
		uint8_t nonce[12];
		memset (nonce, 0, 12); // n = 0
		if (!i2p::crypto::AEADChaCha20Poly1305 (payload, len, buf, 8, key, nonce, payload, len + 16, true)) // encrypt
		{
			LogPrint (eLogWarning, "Garlic: Payload section AEAD encryption failed");
			return nullptr;
		}
		offset += len + 16;
		htobe32buf (m->GetPayload (), static_cast<uint32_t> (offset));
		m->len += offset + 4;
		m->FillI2NPMessageHeader (eI2NPGarlic);
		if (msg->onDrop)
		{
			// move onDrop to the wrapping I2NP messages
			m->onDrop = msg->onDrop;
			msg->onDrop = nullptr;
		}
		return m;
	}

	std::shared_ptr<I2NPMessage> WrapECIESX25519MessageForRouter (
		std::shared_ptr<I2NPMessage> msg, const uint8_t * routerPublicKey)
	{
		// Noise_N, we are Alice, routerPublicKey is Bob's
		if (!msg || !routerPublicKey) return nullptr;
		i2p::crypto::NoiseSymmetricState noiseState;
		i2p::crypto::InitNoiseNState (noiseState, routerPublicKey);
		auto m = NewI2NPMessage (msg->GetPayloadLength () + 128);
		if (!m) return nullptr;
		m->Align (12); // in order to get buf aligned to 16 (12 + 4)
		uint8_t * buf = m->GetPayload () + 4; // 4 bytes for length
		const size_t used = static_cast<size_t> (buf - m->buf);
		if (used >= m->maxLen) return nullptr;
		const size_t capacity = m->maxLen - used;
		size_t offset = 0;
		auto ephemeralKeys = i2p::transport::transports.GetNextX25519KeysPair ();
		if (!ephemeralKeys) return nullptr;
		constexpr size_t MAX_PADDING = 18; // 3-byte header + 15 bytes
		constexpr size_t MAX_POLY1305 = 16;
		// ephemeral key, DateTime, clove header/fields, padding and MAC.
		if (msg->GetPayloadLength () > capacity
			|| capacity - msg->GetPayloadLength () < 32 + 20 + MAX_PADDING + MAX_POLY1305)
		{
			LogPrint (eLogError, "Garlic: Message does not fit the router ECIES envelope");
			i2p::transport::transports.ReuseX25519KeysPair (ephemeralKeys);
			return nullptr;
		}
		memcpy (buf + offset, ephemeralKeys->GetPublicKey (), 32);
		noiseState.MixHash (buf + offset, 32); // h = SHA256(h || aepk)
		offset += 32;
		uint8_t sharedSecret[32];
		if (!AgreeNonZero (*ephemeralKeys, routerPublicKey, sharedSecret)) // x25519(aesk, bpk)
		{
			LogPrint (eLogWarning, "Garlic: Incorrect Bob static key");
			i2p::transport::transports.ReuseX25519KeysPair (ephemeralKeys);
			return nullptr;
		}
		noiseState.MixKey (sharedSecret);
		auto payload = buf + offset;
		size_t len = CreateGarlicPayload (msg, payload, true,
			900); // 1003 - 32 eph key - 16 Poly1305 hash - 16 I2NP header - 4 garlic length - 35 router tunnel delivery
		if (!len || len + 16 > capacity - offset)
		{
			LogPrint (eLogError, "Garlic: Bad router ECIES payload length ", len);
			i2p::transport::transports.ReuseX25519KeysPair (ephemeralKeys);
			return nullptr;
		}
		uint8_t nonce[12];
		memset (nonce, 0, 12);
		// encrypt payload
		if (!i2p::crypto::AEADChaCha20Poly1305 (
				payload, len, noiseState.m_H, 32, noiseState.m_CK + 32, nonce, payload, len + 16, true)) // encrypt
		{
			LogPrint (eLogWarning, "Garlic: Payload for router AEAD encryption failed");
			i2p::transport::transports.ReuseX25519KeysPair (ephemeralKeys);
			return nullptr;
		}
		offset += len + 16;
		htobe32buf (m->GetPayload (), static_cast<uint32_t> (offset));
		m->len += offset + 4;
		m->FillI2NPMessageHeader (eI2NPGarlic);
		if (msg->onDrop)
		{
			// move onDrop to the wrapping I2NP messages
			m->onDrop = msg->onDrop;
			msg->onDrop = nullptr;
		}
		return m;
	}
}
}
