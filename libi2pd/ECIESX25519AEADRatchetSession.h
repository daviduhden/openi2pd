/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: ECIESX25519AEADRatchetSession.
 *
 * Declares the ECIES-X25519-AEAD-Ratchet garlic session state machine
 * with its DH and tag ratchets, session negotiation, ACK tracking and
 * replay-protected New Session handling. Enforces protocol timing, tag
 * and payload bounds, and supports optional ML-KEM hybrid keys when
 * OpenSSL provides them.
 *
 */

#ifndef ECIES_X25519_AEAD_RATCHET_SESSION_H__
#define ECIES_X25519_AEAD_RATCHET_SESSION_H__

#include <string.h>
#include <inttypes.h>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>
#include <array>
#include <deque>
#include <list>
#include <mutex>
#include <unordered_map>
#include <utility>
#include "Identity.h"
#include "Crypto.h"
#include "PostQuantum.h"
#include "Garlic.h"
#include "Tag.h"

namespace i2p
{
namespace garlic
{
	const int ECIESX25519_RESTART_TIMEOUT = 120; // number of second since session creation we can restart session after
	const int ECIESX25519_INACTIVITY_TIMEOUT = 90; // number of seconds we receive nothing and should restart if we can
	const int ECIESX25519_SEND_INACTIVITY_TIMEOUT
		= 5000; // number of milliseconds we can send empty(payload only) packet after
	const int ECIESX25519_SEND_EXPIRATION_TIMEOUT = 480; // in seconds
	const int ECIESX25519_RECEIVE_EXPIRATION_TIMEOUT = 600; // in seconds
	const int ECIESX25519_SESSION_CREATE_TIMEOUT = 3; // in seconds, NSR must be send after NS received
	const int ECIESX25519_SESSION_ESTABLISH_TIMEOUT = 15; // in seconds
	const int ECIESX25519_PREVIOUS_TAGSET_EXPIRATION_TIMEOUT = 180; // in seconds
	const int ECIESX25519_DEFAULT_ACK_REQUEST_INTERVAL = 33000; // in milliseconds
	const int ECIESX25519_ACK_REQUEST_MAX_NUM_ATTEMPTS = 3;
	const int ECIESX25519_TAGSET_MAX_NUM_TAGS = 8192; // number of tags we request new tagset after
	const int ECIESX25519_MIN_NUM_GENERATED_TAGS = 24;
	const int ECIESX25519_MAX_NUM_GENERATED_TAGS = 800;
	const int ECIESX25519_NSR_NUM_GENERATED_TAGS = 12;
	// Java 2.13.0 ECIESAEADEngine.MAX_NS_AGE / MAX_NS_FUTURE
	const uint64_t ECIESX25519_DATETIME_MAX_AGE = 5 * 60; // seconds
	const uint64_t ECIESX25519_DATETIME_MAX_FUTURE = 2 * 60; // seconds
	// Java 2.13.0 RatchetSKM.OutboundSession.MAX_SEND_ACKS
	const int ECIESX25519_MAX_ACKS_PER_MESSAGE = 16;
	// Upper bound for ACK requests queued on one session. Java drains at
	// most MAX_SEND_ACKS per message, so the queue is bounded in practice
	// too.
	const size_t ECIESX25519_MAX_PENDING_ACKS = 128;
	// Java RatchetSKM.OutboundSession keeps every New Session Reply split
	// in _unackedTagSets until the matching inbound tagset consumes its
	// first tag. Bound that list and expire unused splits after Java's
	// SESSION_PENDING_DURATION_MS (3 minutes).
	const size_t ECIESX25519_MAX_PENDING_REPLY_TAGSETS = 8;
	const uint64_t ECIESX25519_PENDING_REPLY_TAGSET_TIMEOUT = 180; // seconds

	const size_t ECIESX25519_OPTIMAL_PAYLOAD_SIZE = 1912; // 1912 = 1956 /* to fit 2 tunnel messages */
	// - 16 /* I2NP header */ - 16 /* poly hash */ - 8 /* tag */ - 4 /*
	//   garlic length */

	// Payload-size normalisation status: this implementation pads a
	// payload to the optimal size above and keeps a New Session out of the
	// 2 (mod 16) residue that selects the ElGamal/AES path. Java's
	// additional B1/B2/B3 bucket normalisation (which also moves the
	// plaintext out of the size classes a peer can distinguish) is
	// deliberately deferred: applying it without the exact bucket
	// boundaries from the reference would change the length distribution
	// without matching it, which is worse than not applying it. Tracked as
	// an open tuning item, not as a security fix.

	/// Holds the tag and message-key chains for one ECIES ratchet
	/// generation. Derived intermediate keys support out-of-order
	/// delivery; retrieving a cached key consumes it. The destructor wipes
	/// the retained key material.
	/// @see https://i2p.net/en/docs/specs/ecies/
	class RatchetTagSet
	{
	public:
		RatchetTagSet () { };
		virtual ~RatchetTagSet ()
		{
			m_SessionTagKeyData.Wipe ();
			OPENSSL_cleanse (m_SessTagConstant, sizeof (m_SessTagConstant));
			OPENSSL_cleanse (m_SymmKeyCK, sizeof (m_SymmKeyCK));
			OPENSSL_cleanse (m_CurrentSymmKeyCK, sizeof (m_CurrentSymmKeyCK));
			OPENSSL_cleanse (m_NextRootKey, sizeof (m_NextRootKey));
			for (auto & it : m_ItermediateSymmKeys) it.second.Wipe ();
			m_ItermediateSymmKeys.clear ();
		};

		void DHInitialize (const uint8_t * rootKey, const uint8_t * k);
		void NextSessionTagRatchet ();
		uint64_t GetNextSessionTag (i2p::crypto::HKDFContext & hkdfCtx);
		const uint8_t * GetNextRootKey () const { return m_NextRootKey; };
		int GetNextIndex () const { return m_NextIndex; };
		/// Derive or consume a cached key for the 16-bit message index.
		/// @param key Writable 32-byte output.
		/// @return False for an invalid index, null output or missing cached
		///         key.
		bool GetSymmKey (int index, uint8_t * key);
		void DeleteSymmKey (int index);

		int GetTagSetID () const { return m_TagSetID; };
		void SetTagSetID (int tagsetID) { m_TagSetID = tagsetID; };

		uint32_t GetMsgID () const
		{
			// ACKs carry a 16-bit tagset ID and a 16-bit message number. Do not
			// shift a signed/out-of-range value (UB) or invent an ID that the
			// peer can never acknowledge.
			if (m_TagSetID < 0 || m_TagSetID > 0xFFFF || m_NextIndex < 0 || m_NextIndex > 0xFFFF) return 0;
			return (static_cast<uint32_t> (m_TagSetID) << 16) | static_cast<uint32_t> (m_NextIndex);
		}; // (tagsetid << 16) + N

	private:
		i2p::data::Tag<64> m_SessionTagKeyData;
		uint8_t m_SessTagConstant[32], m_SymmKeyCK[32], m_CurrentSymmKeyCK[64], m_NextRootKey[32];
		int m_NextIndex = 0, m_NextSymmKeyIndex = 0;
		std::unordered_map<int, i2p::data::Tag<32>> m_ItermediateSymmKeys;

		int m_TagSetID = 0;
	};

	class ECIESX25519AEADRatchetSession;
	/// Receive-side tag set that retains its session while tags can be
	/// used. Destruction of a New Session tag set also releases the
	/// session's NSR keys.
	class ReceiveRatchetTagSet : public RatchetTagSet, public std::enable_shared_from_this<ReceiveRatchetTagSet>
	{
	public:
		ReceiveRatchetTagSet (std::shared_ptr<ECIESX25519AEADRatchetSession> session, bool isNS = false);
		~ReceiveRatchetTagSet () override;

		bool IsNS () const { return m_IsNS; };
		std::shared_ptr<ECIESX25519AEADRatchetSession> GetSession () { return m_Session; };
		void SetTrimBehind (int index)
		{
			if (index > m_TrimBehindIndex) m_TrimBehindIndex = index;
		};
		int GetTrimBehind () const { return m_TrimBehindIndex; };

		void Expire ();
		bool IsExpired (uint64_t ts) const;
		// Java RatchetSKM consumes the first tag of an inbound ES tagset only
		// once and promotes the matching pending outbound tagset then.
		bool MarkFirstTagConsumed () { return !m_FirstTagConsumed.exchange (true, std::memory_order_acq_rel); }

		virtual bool IsIndexExpired (int index) const;
		virtual bool HandleNextMessage (uint8_t * buf, size_t len, int index);
		virtual bool IsSessionTerminated () const;

	private:
		int m_TrimBehindIndex = 0;
		std::shared_ptr<ECIESX25519AEADRatchetSession> m_Session;
		bool m_IsNS;
		std::atomic_bool m_FirstTagConsumed { false };
		uint64_t m_ExpirationTimestamp = 0;
	};

	class SymmetricKeyTagSet : public ReceiveRatchetTagSet
	{
	public:
		SymmetricKeyTagSet (GarlicDestination * destination, const uint8_t * key);
		~SymmetricKeyTagSet () override { OPENSSL_cleanse (m_Key, sizeof (m_Key)); }

		bool IsIndexExpired (int /* index */) const override { return false; };
		bool HandleNextMessage (uint8_t * buf, size_t len, int index) override;
		bool IsSessionTerminated () const override { return false; }

	private:
		GarlicDestination * m_Destination;
		uint8_t m_Key[32];
	};

	enum ECIESx25519BlockType
	{
		eECIESx25519BlkDateTime = 0,
		eECIESx25519BlkSessionID = 1,
		eECIESx25519BlkTermination = 4,
		eECIESx25519BlkOptions = 5,
		eECIESx25519BlkNextKey = 7,
		eECIESx25519BlkAck = 8,
		eECIESx25519BlkAckRequest = 9,
		eECIESx25519BlkGalicClove = 11,
		eECIESx25519BlkPadding = 254
	};

	const uint8_t ECIESX25519_NEXT_KEY_KEY_PRESENT_FLAG = 0x01;
	const uint8_t ECIESX25519_NEXT_KEY_REVERSE_KEY_FLAG = 0x02;
	const uint8_t ECIESX25519_NEXT_KEY_REQUEST_REVERSE_KEY_FLAG = 0x04;

	// Payload context. An established-session payload may carry ratchet
	// keys, ACKs and a Termination block; a New Session Reply may carry
	// only DateTime, Garlic, Options and Padding; a New Session is
	// stricter still.
	enum class RatchetPayloadMode
	{
		NewSession,
		NewSessionReply,
		ExistingSession
	};

	// Validates an established payload before any side effects are
	// applied: exact block framing, DateTime uniqueness/skew,
	// NEXT_KEY/ACK/ACKREQUEST lengths, and Padding/Termination ordering.
	bool ValidateRatchetPayload (const uint8_t * buf, size_t len);
	// Validates the payload of a New Session (NS/N): exactly one DateTime
	// block inside the Java 2.14.0 window, a deliberately stricter
	// non-empty Garlic policy, no Termination/SessionID/unknown blocks and
	// nothing after Padding. Exposed for the self-contained regression
	// tests.
	bool ValidateNewSessionPayload (const uint8_t * buf, size_t len);
	// Validates the payload of a New Session Reply (NSR): only DateTime,
	// Garlic, Options and Padding. Any Termination/NEXT_KEY/SessionID/ACK/
	// ACK_REQUEST or unknown block is rejected before the split is
	// installed, so an NSR can never remove a session.
	bool ValidateNewSessionReplyPayload (const uint8_t * buf, size_t len);

	/// ECIES-X25519 garlic session with handshake, tag-set and ACK state.
	/// A received New Session candidate is validated and authenticated
	/// before its Noise state and remote key are committed. The first
	/// consumed inbound tag selects a matching pending reply split; when
	/// no send tag set exists, the send path can use the oldest pending
	/// split as a fallback.
	/// @see https://i2p.net/en/docs/specs/ecies/
	class ECIESX25519AEADRatchetSession : public GarlicRoutingSession,
										  private i2p::crypto::NoiseSymmetricState,
										  public std::enable_shared_from_this<ECIESX25519AEADRatchetSession>
	{
		enum SessionState
		{
			eSessionStateNew = 0,
			eSessionStateNewSessionReceived,
			eSessionStateNewSessionSent,
			eSessionStateNewSessionReplySent,
			eSessionStateEstablished
		};

		struct DHRatchet
		{
			int keyID = 0;
			std::shared_ptr<i2p::crypto::X25519Keys> key;
			uint8_t remote[32] = { }; // last remote public key
			int remoteKeyID = -1; // key ID that remote belongs to
			bool remoteKeyPresent = false;
			bool lastHadKey = false; // shape of the last received NEXT_KEY
			bool newKey = true;
			int GetReceiveTagSetID () const { return newKey ? (2 * keyID + 1) : 2 * keyID; }
		};

	public:
		ECIESX25519AEADRatchetSession (GarlicDestination * owner, bool attachLeaseSetNS);
		~ECIESX25519AEADRatchetSession ();

		bool HandleNextMessage (
			uint8_t * buf, size_t len, std::shared_ptr<ReceiveRatchetTagSet> receiveTagset, int index = 0);
		std::shared_ptr<I2NPMessage> WrapSingleMessage (std::shared_ptr<const I2NPMessage> msg) override;
		std::vector<std::shared_ptr<I2NPMessage>> WrapMultipleMessages (
			const std::vector<std::shared_ptr<const I2NPMessage>> & msgs) override;

		const uint8_t * GetRemoteStaticKey () const
		{
			std::lock_guard<std::recursive_mutex> l (m_StateMutex);
			return m_RemoteStaticKey;
		}
		i2p::data::CryptoKeyType GetRemoteStaticKeyType () const
		{
			std::lock_guard<std::recursive_mutex> l (m_StateMutex);
			return m_RemoteStaticKeyType;
		}
		void SetRemoteStaticKey (i2p::data::CryptoKeyType keyType, const uint8_t * key)
		{
			std::lock_guard<std::recursive_mutex> l (m_StateMutex);
			m_RemoteStaticKeyType = keyType;
			memcpy (m_RemoteStaticKey, key, 32);
		}
		void Terminate () { m_IsTerminated.store (true, std::memory_order_release); }
		void SetDestination (const i2p::data::IdentHash & dest)
		{
			std::lock_guard<std::recursive_mutex> l (m_StateMutex);
			if (!m_Destination) m_Destination.reset (new i2p::data::IdentHash (dest));
		}
		const i2p::data::IdentHash * GetDestinationPtr () const
		{
			std::lock_guard<std::recursive_mutex> l (m_StateMutex);
			return m_Destination ? m_Destination.get () : nullptr;
		}; // for pongs
		bool CheckExpired (uint64_t ts); // true is expired
		bool CanBeRestarted (uint64_t ts) const
		{
			return ts > m_SessionCreatedTimestamp.load (std::memory_order_relaxed) + ECIESX25519_RESTART_TIMEOUT;
		}
		bool IsInactive (uint64_t ts) const override
		{
			return ts > m_LastActivityTimestamp.load (std::memory_order_relaxed) + ECIESX25519_INACTIVITY_TIMEOUT
				&& CanBeRestarted (ts);
		}
		void CleanupReceiveNSRKeys (); // called from ReceiveRatchetTagSet at Alice's side
		bool IsResponseRequired () const
		{
			std::lock_guard<std::recursive_mutex> l (m_StateMutex);
			return m_State == eSessionStateNewSessionReceived || m_SendReverseKey || !m_AckRequests.empty ();
		};
		// One fallback ACK/NSR timer per session. ACK-request floods must not
		// allocate an unbounded number of timers.
		bool ArmResponseTimer () { return !m_ResponseTimerArmed.exchange (true); }
		void DisarmResponseTimer () { m_ResponseTimerArmed.store (false); }

		bool IsRatchets () const override { return true; };
		bool IsReadyToSend () const override
		{
			std::lock_guard<std::recursive_mutex> l (m_StateMutex);
			return m_State != eSessionStateNewSessionSent && !IsExhausted ();
		}
		// Java RatchetSKM.consumeNext() creates a new Session when the
		// outbound tagset is empty. The exhausted object stays alive to
		// receive but must never be handed out for a new send.
		bool IsExhausted () const override { return m_SendTagsetExhausted.load (std::memory_order_acquire); }
		bool IsTerminated () const override { return m_IsTerminated.load (std::memory_order_acquire); }
		uint64_t GetLastActivityTimestamp () const override
		{
			return m_LastActivityTimestamp.load (std::memory_order_relaxed);
		};
		void SetAckRequestInterval (int interval) override
		{
			std::lock_guard<std::recursive_mutex> l (m_StateMutex);
			m_AckRequestInterval = interval;
		};
		bool CleanupUnconfirmedTags () override; // return true if unanswered Ack requests, called from I2CP

	protected:
		i2p::crypto::NoiseSymmetricState & GetNoiseState () { return *this; };
		void SetNoiseState (const i2p::crypto::NoiseSymmetricState & state) { GetNoiseState () = state; };
		void CreateNonce (uint64_t seqn, uint8_t * nonce);
		void HandlePayload (const uint8_t * buf, size_t len,
			const std::shared_ptr<ReceiveRatchetTagSet> & receiveTagset, int index, RatchetPayloadMode mode);
		bool MessageConfirmed (uint32_t msgID) override;

	private:
		// Java RatchetSKM.OutboundSession._unackedTagSets: one entry per New
		// Session Reply split until its inbound tagset consumes the first
		// tag.
		struct PendingReplyTagsets
		{
			std::shared_ptr<ReceiveRatchetTagSet> receive;
			std::shared_ptr<RatchetTagSet> send;
			uint64_t created = 0; // seconds
		};

		bool GenerateEphemeralKeysAndEncode (uint8_t * buf); // buf is 32 bytes
		void InitNewSessionTagset (std::shared_ptr<RatchetTagSet> tagsetNsr) const;
		void RestoreNewSessionState ();

		void SetSendTagset (std::shared_ptr<RatchetTagSet> tagset);
		void AddPendingReplyTagset (std::shared_ptr<ReceiveRatchetTagSet> receive, std::shared_ptr<RatchetTagSet> send);
		std::shared_ptr<RatchetTagSet> TakeMatchingPendingSendTagset (
			const std::shared_ptr<ReceiveRatchetTagSet> & receiveTagset);
		std::shared_ptr<RatchetTagSet> TakeFallbackPendingSendTagset ();
		void ClearPendingReplyTagsets ();
		void ExpirePendingReplyTagsets (uint64_t ts);

		bool HandleNewIncomingSession (const uint8_t * buf, size_t len);
		// Try one locally supported ratchet key type. A trial never touches
		// the session state until every AEAD tag and the payload are
		// authenticated.
		bool HandleNewIncomingSessionCandidate (i2p::data::CryptoKeyType cryptoType, const uint8_t * buf, size_t len);
		bool HandleNewOutgoingSessionReply (uint8_t * buf, size_t len);
		bool HandleExistingSessionMessage (
			uint8_t * buf, size_t len, std::shared_ptr<ReceiveRatchetTagSet> receiveTagset, int index);
		void HandleNextKey (
			const uint8_t * buf, size_t len, const std::shared_ptr<ReceiveRatchetTagSet> & receiveTagset);

		bool NewOutgoingSessionMessage (const uint8_t * payload, size_t len, uint8_t * out, size_t outLen);
		bool NewSessionReplyMessage (const uint8_t * payload, size_t len, uint8_t * out, size_t outLen);
		bool NextNewSessionReplyMessage (const uint8_t * payload, size_t len, uint8_t * out, size_t outLen);
		bool CreateNewSessionReplyMessage (const uint8_t * payload, size_t len, uint8_t * out, size_t outLen);
		bool NewExistingSessionMessage (const uint8_t * payload, size_t len, uint8_t * out, size_t outLen);

		size_t CreatePayload (std::shared_ptr<const I2NPMessage> msg, bool first, uint8_t * payload);
		size_t CreateGarlicClove (
			std::shared_ptr<const I2NPMessage> msg, uint8_t * buf, size_t len, bool alwaysLocal = false);
		size_t CreateLeaseSetClove (
			std::shared_ptr<const i2p::data::LocalLeaseSet> ls, uint64_t ts, uint8_t * buf, size_t len);
		size_t CreatePaddingClove (uint8_t paddingSize, uint8_t * buf, size_t len);

		void GenerateMoreReceiveTags (std::shared_ptr<ReceiveRatchetTagSet> receiveTagset, int numTags);
		void NewNextSendRatchet ();

		std::shared_ptr<I2NPMessage> WrapPayload (const uint8_t * payload, size_t len);
		uint8_t GetNextPaddingSize (size_t payloadLen);

	private:
		i2p::data::CryptoKeyType m_RemoteStaticKeyType;
		uint8_t m_RemoteStaticKey[32] = { }; // zero while an anonymous New Session is processed
		uint8_t m_Aepk[32] = { }; // Alice's ephemeral keys, for incoming only
		uint8_t m_NSRH[32] = { }; // hash at the end of the New Session
		uint8_t m_NSRPostNSCK[32] = { }; // chain key at the end of the New Session
		bool m_NSRStateReady = false; // New Session state captured, NSR possible
		std::shared_ptr<i2p::crypto::X25519Keys> m_EphemeralKeys;
#if OPENSSL_PQ
		std::unique_ptr<i2p::crypto::MLKEMKeys> m_PQKeys;
#endif
		SessionState m_State = eSessionStateNew;
		// Written by the payload path and read by the administrative and
		// liveness accessors from other threads.
		std::atomic<uint64_t> m_SessionCreatedTimestamp { 0 }, m_LastActivityTimestamp { 0 }, // incoming (in seconds)
			m_LastSentTimestamp { 0 }; // in milliseconds
		std::shared_ptr<RatchetTagSet> m_SendTagset, m_NSRSendTagset;
		// Every New Session Reply split that has not seen its first inbound
		// ES tag yet. Guarded by m_PendingReplyTagsetsMutex; the actual
		// outbound tagset is only promoted when the matching inbound tagset
		// is used.
		mutable std::mutex m_PendingReplyTagsetsMutex;
		std::list<PendingReplyTagsets> m_PendingReplyTagsets;
		std::atomic_bool m_SendTagsetExhausted { false };
		std::unique_ptr<i2p::data::IdentHash> m_Destination; // must be set for NS if outgoing and NSR if incoming
		std::list<std::pair<uint16_t, int>> m_AckRequests; // incoming (tagsetid, index)
		// Java caps how often a forward/reverse key is re-announced while the
		// peer has not acknowledged it (MAX_SEND_REVERSE_KEY). That cap could
		// not be verified against the pinned reference in this environment,
		// so it is intentionally not guessed: the flags are cleared as soon
		// as the matching NEXT_KEY or tagset arrives, and the per-session tag
		// budget already bounds the work a peer can cause.
		bool m_SendReverseKey = false, m_SendForwardKey = false;
		std::atomic_bool m_IsTerminated { false };
		std::atomic_bool m_ResponseTimerArmed { false };
		std::unique_ptr<DHRatchet> m_NextReceiveRatchet, m_NextSendRatchet;

		uint64_t m_LastAckRequestSendTime = 0; // milliseconds
		uint32_t m_AckRequestMsgID = 0;
		int m_AckRequestNumAttempts = 0;
		int m_AckRequestInterval = ECIESX25519_DEFAULT_ACK_REQUEST_INTERVAL; // milliseconds

	public:
		// for administrative views
		int GetState () const
		{
			std::lock_guard<std::recursive_mutex> l (m_StateMutex);
			return (int)m_State;
		}
		int GetSendTagsetID () const
		{
			std::lock_guard<std::recursive_mutex> l (m_StateMutex);
			return m_SendTagset ? m_SendTagset->GetTagSetID () : -1;
		}
		int GetSendTagsetNextIndex () const
		{
			std::lock_guard<std::recursive_mutex> l (m_StateMutex);
			return m_SendTagset ? m_SendTagset->GetNextIndex () : -1;
		}
		size_t GetNumPendingReplyTagsets () const
		{
			std::lock_guard<std::mutex> l (m_PendingReplyTagsetsMutex);
			return m_PendingReplyTagsets.size ();
		}
		i2p::data::IdentHash GetDestination () const
		{
			std::lock_guard<std::recursive_mutex> l (m_StateMutex);
			return m_Destination ? *m_Destination : i2p::data::IdentHash ();
		}
	};

	// single session for all incoming messages
	class RouterIncomingRatchetSession : public ECIESX25519AEADRatchetSession
	{
	public:
		RouterIncomingRatchetSession (const i2p::crypto::NoiseSymmetricState & initState);
		bool HandleNextMessage (const uint8_t * buf, size_t len);
		i2p::crypto::NoiseSymmetricState & GetCurrentNoiseState () { return m_CurrentNoiseState; };

	private:
		i2p::crypto::NoiseSymmetricState m_CurrentNoiseState;
		// Java ECIESAEADEngine.decryptNewSession_N checks
		// keyManager.isDuplicate(pk) and drops a replayed router N message.
		// Keep a bounded, time-limited cache; only authenticated and
		// structurally valid N messages enter it.
		mutable std::mutex m_EphemeralReplayMutex;
		std::deque<std::pair<std::array<uint8_t, 32>, uint64_t>> m_EphemeralReplay;
	};

	std::shared_ptr<I2NPMessage> WrapECIESX25519Message (
		std::shared_ptr<I2NPMessage> msg, const uint8_t * key, uint64_t tag);
	std::shared_ptr<I2NPMessage> WrapECIESX25519MessageForRouter (
		std::shared_ptr<I2NPMessage> msg, const uint8_t * routerPublicKey);
}
}

#endif
