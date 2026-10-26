/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Datagram.
 *
 * Serialises datagram1, datagram2 and datagram3 payloads into I2NP Data
 * messages and dispatches inbound Data, raw and protocol-specific payloads to
 * (protocol, port)-keyed receivers. Datagram2 signatures cover the remote
 * hash, flags, options, offline section and payload; Datagram3 options use a
 * bounded 16-bit-length-prefixed Mapping, and per-peer sessions handle routing
 * paths, LeaseSet refresh, send queues and idle cleanup. The framing version is
 * selected per send, and the session queue/routing state is mutex-guarded for
 * SAM sends from its own socket threads.
 *
 */

#include <string.h>
#include "Crypto.h"
#include "Log.h"
#include "TunnelBase.h"
#include "RouterContext.h"
#include "Destination.h"
#include "Datagram.h"
#include <stdexcept>

namespace i2p
{
namespace datagram
{
	namespace
	{
		// All receiver tables are addressed by (protocol, port). SAM 3.3 keys a
		// subsession on its listen protocol so two subsessions may share a port;
		// the protocol is the high half to keep the map key bounded. Protocol 0
		// is the catch-all on every table.
		constexpr uint32_t MakeReceiverKey (uint8_t protocol, uint16_t port)
		{
			return (static_cast<uint32_t> (protocol) << 16) | port;
		}

		// The project signer is a void interface that leaves zeroed output behind
		// when the provider operation fails. A zero signature can never verify,
		// so it must never be published; the fallible Signer::Sign interface is
		// tracked separately.
		bool IsZeroSignature (const uint8_t * signature, size_t len)
		{
			if (!signature || !len) return true;
			uint8_t accumulator = 0;
			for (size_t i = 0; i < len; ++i) accumulator |= signature[i];
			return accumulator == 0;
		}

		// Datagram2 offline section: expires(4) || transient key type(2) ||
		// transient public key || long-term signature (PrivateKeys::
		// CreateOfflineKeys). A shorter section cannot be parsed by the receiver,
		// so it is dropped instead of published as an unverifiable datagram.
		bool IsCompleteOfflineSection (const std::vector<uint8_t> & offline, size_t ownerSignatureLen)
		{
			if (offline.size () < i2p::data::OFFLINE_SIGNATURE_HEADER_LENGTH) return false;
			std::unique_ptr<i2p::crypto::Verifier> verifier (
				i2p::data::IdentityEx::CreateVerifier (static_cast<i2p::data::SigningKeyType> (bufbe16toh (offline.data () + 4))));
			if (!verifier) return false;
			const size_t expected
				= i2p::data::OFFLINE_SIGNATURE_HEADER_LENGTH + verifier->GetPublicKeyLen () + ownerSignatureLen;
			return offline.size () >= expected;
		}

		// Signing failures are locally generated, but a broken provider would
		// otherwise log once per datagram.
		i2p::I2NPDropCounter g_SignatureFailures;
		// An unsupported framing request is a caller error; keep it observable
		// without letting a misbehaving subsession flood the log.
		i2p::I2NPDropCounter g_UnsupportedFraming;
	}

	// Datagram2/3 options are a Mapping prefixed by its 16-bit length.
	// A zero-length Mapping is valid (Java reads the length, then only parses
	// properties when it is non-zero); Mapping::FromBuffer alone cannot
	// distinguish that from a parse failure.
	bool ParseDatagramOptions (i2p::util::Mapping & options, const uint8_t * buf, size_t len, size_t & offset)
	{
		if (offset > len || len - offset < 2) return false;
		size_t optionsLen = bufbe16toh (buf + offset);
		if (optionsLen > len - offset - 2) return false;
		if (optionsLen && !options.FromBuffer (optionsLen, buf + offset + 2, optionsLen)) return false;
		offset += 2 + optionsLen;
		return true;
	}

	DatagramDestination::DatagramDestination (
		std::shared_ptr<i2p::client::ClientDestination> owner, bool gzip, DatagramVersion version)
		: m_Owner (owner)
		, m_Gzip (gzip)
		, m_Version (version)
	{
		if (m_Gzip) m_Deflator.reset (new i2p::data::GzipDeflator);

		auto identityLen = m_Owner->GetIdentity ()->GetFullLen ();
		m_From.resize (identityLen);
		m_Owner->GetIdentity ()->ToBuffer (m_From.data (), identityLen);
	}

	DatagramDestination::~DatagramDestination ()
	{
		m_Sessions.clear ();
	}

	void DatagramDestination::RefreshIdentity ()
	{
		auto identity = m_Owner ? m_Owner->GetIdentity () : nullptr;
		if (!identity) return;
		const auto len = identity->GetFullLen ();
		std::vector<uint8_t> from (len);
		identity->ToBuffer (from.data (), len);
		std::lock_guard<std::mutex> l (m_FromMutex);
		m_From.swap (from);
	}

	void DatagramDestination::SendDatagramTo (const uint8_t * payload, size_t len, const i2p::data::IdentHash & identity,
		uint16_t fromPort, uint16_t toPort, DatagramVersion version)
	{
		auto session = GetSession (identity);
		SendDatagram (session, payload, len, fromPort, toPort, nullptr, version);
		FlushSendQueue (session);
	}

	void DatagramDestination::SendRawDatagramTo (const uint8_t * payload, size_t len,
		const i2p::data::IdentHash & identity, uint16_t fromPort, uint16_t toPort, uint8_t protocol)
	{
		auto session = ObtainSession (identity);
		SendRawDatagram (session, payload, len, fromPort, toPort, protocol);
		FlushSendQueue (session);
	}

	std::shared_ptr<DatagramSession> DatagramDestination::GetSession (const i2p::data::IdentHash & ident)
	{
		return ObtainSession (ident);
	}

	void DatagramDestination::SendDatagram (std::shared_ptr<DatagramSession> session, const uint8_t * payload,
		size_t len, uint16_t fromPort, uint16_t toPort, const i2p::util::Mapping * options, DatagramVersion version)
	{
		if (session)
		{
			// The framing is selected per send. The session's version is only
			// the default: a DATAGRAM3 SAM subsession must not inherit the
			// Datagram1 framing that created the shared destination.
			const auto framing = ResolveDatagramVersion (version, session->GetVersion ());
			if (!IsDatagramVersionSupported (framing))
			{
				if (g_UnsupportedFraming.Record ())
					LogPrint (eLogError, "Datagram: datagram type ", (int)framing, " is not supported (",
						g_UnsupportedFraming.GetCount (), " such sends dropped)");
				return;
			}
			std::vector<uint8_t> from;
			{
				std::lock_guard<std::mutex> l (m_FromMutex);
				from = m_From;
			}
			std::shared_ptr<I2NPMessage> msg;
			switch (framing)
			{
			case eDatagramV3:
			{
				uint8_t flags[] = { 0x00, 0x03 }; // datagram3, no options
				if (options)
				{
					uint8_t optionsBuf[256]; // TODO: evaluate actual size
					size_t optionsLen = options->ToBuffer (optionsBuf, 256);
					if (optionsLen) flags[1] |= DATAGRAM3_FLAG_OPTIONS;
					msg = CreateDataMessage ({ { m_Owner->GetIdentity ()->GetIdentHash (), 32 }, { flags, 2 },
												 { optionsBuf, optionsLen }, { payload, len } },
						fromPort, toPort, i2p::client::PROTOCOL_TYPE_DATAGRAM3, false); // datagram3
				}
				else
					msg = CreateDataMessage (
						{ { m_Owner->GetIdentity ()->GetIdentHash (), 32 }, { flags, 2 }, { payload, len } }, fromPort,
						toPort, i2p::client::PROTOCOL_TYPE_DATAGRAM3, false); // datagram3
				break;
			}
			case eDatagramV1:
			{
				// Java I2PDatagramMaker rejects offline destinations: Datagram1 has no
				// offline signature section (I2PDatagramMaker.java:49-52).
				if (m_Owner->GetPrivateKeys ().IsOfflineSignature ())
				{
					LogPrint (eLogError, "Datagram: datagram1 doesn't support offline signature");
					break;
				}
				std::vector<uint8_t> signature (m_Owner->GetPrivateKeys ().GetSignatureLen ());
				if (signature.empty ())
				{
					LogPrint (eLogError, "Datagram: no usable signing key for datagram1");
					break;
				}
				if (m_Owner->GetIdentity ()->GetSigningKeyType () == i2p::data::SIGNING_KEY_TYPE_DSA_SHA1)
				{
					uint8_t hash[32];
					SHA256 (payload, len, hash);
					m_Owner->Sign (hash, 32, signature.data ());
				}
				else
					m_Owner->Sign (payload, static_cast<int> (len), signature.data ());
				if (IsZeroSignature (signature.data (), signature.size ()))
				{
					if (g_SignatureFailures.Record ())
						LogPrint (eLogError, "Datagram: datagram1 signing failed, message dropped (",
							g_SignatureFailures.GetCount (), " failures)");
					break;
				}
				msg = CreateDataMessage (
					{ { from.data (), from.size () }, { signature.data (), signature.size () }, { payload, len } },
					fromPort, toPort, i2p::client::PROTOCOL_TYPE_DATAGRAM, !session->IsRatchets ()); // datagram1
				break;
			}
			case eDatagramV2:
			{
				// Datagram2.java make(): Destination || flags || options || offline section ||
				// payload || signature. The signature covers toHash || flags || options ||
				// offline section || payload; the toHash is not transmitted.
				const auto & keys = m_Owner->GetPrivateKeys ();
				bool isOfflineSignature = keys.IsOfflineSignature ();
				uint8_t flags[2]
					= { 0x00, (uint8_t)(0x02 | (isOfflineSignature ? DATAGRAM2_FLAG_OFFLINE_SIGNATURE : 0)) };
				std::vector<uint8_t> optionsBuf;
				if (options)
				{
					optionsBuf.resize (512);
					auto optionsLen = options->ToBuffer (optionsBuf.data (), optionsBuf.size ());
					if (optionsLen > 2)
					{
						optionsBuf.resize (optionsLen);
						flags[1] |= DATAGRAM2_FLAG_OPTIONS;
					}
					else
						optionsBuf.clear ();
				}
				std::vector<uint8_t> offlineSignature;
				if (isOfflineSignature)
				{
					offlineSignature = keys.GetOfflineSignature ();
					if (!IsCompleteOfflineSection (offlineSignature, m_Owner->GetIdentity ()->GetSignatureLen ()))
					{
						LogPrint (eLogError, "Datagram: malformed offline signature section");
						break;
					}
				}
				std::vector<uint8_t> signedData;
				signedData.reserve (32 + 2 + optionsBuf.size () + offlineSignature.size () + len);
				signedData.insert (
					signedData.end (), session->GetRemoteIdent ().data (), session->GetRemoteIdent ().data () + 32);
				signedData.insert (signedData.end (), flags, flags + 2);
				signedData.insert (signedData.end (), optionsBuf.begin (), optionsBuf.end ());
				signedData.insert (signedData.end (), offlineSignature.begin (), offlineSignature.end ());
				signedData.insert (signedData.end (), payload, payload + len);
				std::vector<uint8_t> signature (keys.GetSignatureLen ());
				if (!signature.empty ())
				{
					m_Owner->Sign (signedData.data (), static_cast<int> (signedData.size ()), signature.data ());
					if (IsZeroSignature (signature.data (), signature.size ()))
					{
						if (g_SignatureFailures.Record ())
							LogPrint (eLogError, "Datagram: datagram2 signing failed, message dropped (",
								g_SignatureFailures.GetCount (), " failures)");
						break;
					}
					msg = CreateDataMessage (
						{ { from.data (), from.size () }, { flags, 2 }, { optionsBuf.data (), optionsBuf.size () },
							{ offlineSignature.data (), offlineSignature.size () }, { payload, len },
							{ signature.data (), signature.size () } },
						fromPort, toPort, i2p::client::PROTOCOL_TYPE_DATAGRAM2, false); // datagram2
				}
				break;
			}
			default: LogPrint (eLogError, "Datagram: datagram type ", (int)framing, " is not supported");
			}
			if (msg) session->SendMsg (msg);
		}
	}

	void DatagramDestination::SendRawDatagram (std::shared_ptr<DatagramSession> session, const uint8_t * payload,
		size_t len, uint16_t fromPort, uint16_t toPort, uint8_t protocol)
	{
		if (session)
			// Protocol 0 (I2CP "any") is a valid per-message value and must not be
			// rewritten to 18; callers that want the default pass
			// DATAGRAM_RAW_PROTOCOL explicitly.
			session->SendMsg (CreateDataMessage ({ { payload, len } }, fromPort, toPort, protocol,
				!session->IsRatchets ())); // raw
	}

	void DatagramDestination::FlushSendQueue (std::shared_ptr<DatagramSession> session)
	{
		if (session) session->FlushSendQueue ();
	}

	void DatagramDestination::HandleDatagram (uint16_t fromPort, uint16_t toPort, const uint8_t * buf, size_t len,
		i2p::garlic::ECIESX25519AEADRatchetSession * from)
	{
		i2p::data::IdentityEx identity;
		size_t identityLen = identity.FromBuffer (buf, len);
		if (!identityLen) return;
		const uint8_t * signature = buf + identityLen;
		size_t headerLen = identityLen + identity.GetSignatureLen ();
		if (headerLen > len) return;

		std::shared_ptr<i2p::data::LeaseSet> ls;
		bool verified = false;
		if (from)
		{
			ls = m_Owner->FindLeaseSet (identity.GetIdentHash ());
			if (ls)
			{
				if (ls->IsEncryptionKey (from->GetRemoteStaticKeyType (), from->GetRemoteStaticKey ()))
					verified = true;
				else
				{
					LogPrint (eLogError, "Datagram: Remote LeaseSet static key mismatch for datagram from ",
						identity.GetIdentHash ().ToBase32 ());
					return;
				}
			}
		}
		if (!verified)
		{
			if (identity.GetSigningKeyType () == i2p::data::SIGNING_KEY_TYPE_DSA_SHA1)
			{
				uint8_t hash[32];
				SHA256 (buf + headerLen, len - headerLen, hash);
				verified = identity.Verify (hash, 32, signature);
			}
			else
				verified = identity.Verify (buf + headerLen, len - headerLen, signature);
		}

		if (verified)
		{
			const bool hadSession = FindSession (identity.GetIdentHash ()) != nullptr;
			auto session = ObtainSession (identity.GetIdentHash ());
			if (!session)
			{
				LogPrint (eLogWarning, "DatagramDestination: session table is full, dropping Datagram1");
				return;
			}
			if (!hadSession) session->SetVersion (eDatagramV1);
			if (ls) session->SetRemoteLeaseSet (ls);
			session->Ack ();
			// Datagram1 framing is carried by protocol 17, so a receiver
			// registered for another style on the same port is not eligible.
			auto r = FindReceiver (toPort, i2p::client::PROTOCOL_TYPE_DATAGRAM);
			if (r)
				r->Invoke (identity, fromPort, toPort, buf + headerLen, len - headerLen, nullptr);
			else
				LogPrint (eLogWarning, "DatagramDestination: no receiver for port ", toPort);
		}
		else
			LogPrint (eLogWarning, "Datagram signature verification failed");
	}

	void DatagramDestination::HandleRawDatagram (
		uint8_t protocol, uint16_t fromPort, uint16_t toPort, const uint8_t * buf, size_t len)
	{
		// I2CP forwards the payload protocol byte verbatim, and the OS-level
		// "unspecified" value 0xff reaches us from a peer or a local UDP client.
		// Java maps it to PROTO_UNSPECIFIED (0) before selecting a listener.
		if (protocol == 0xff) protocol = 0;
		// Streaming (6) and the typed datagram protocols are never raw payloads.
		// Java's SAM 3.3 PrimarySession refuses to route them to a RAW subsession
		// and a catch-all (protocol 0) registration must not capture them either.
		if (IsReservedRawProtocol (protocol))
		{
			LogPrint (eLogWarning, "DatagramDestination: protocol ", (int)protocol, " is not a raw payload");
			return;
		}
		auto r = FindRawReceiver (toPort, protocol);
		if (r)
			r->Invoke (protocol, fromPort, toPort, buf, len);
		else
			LogPrint (eLogWarning, "DatagramDestination: no receiver for raw datagram protocol ", (int)protocol,
				" port ", toPort);
	}

	void DatagramDestination::HandleDatagram2 (uint16_t fromPort, uint16_t toPort, const uint8_t * buf, size_t len,
		i2p::garlic::ECIESX25519AEADRatchetSession * from)
	{
		// Datagram2.java MIN_DGRAM_SIZE = 387 + 2 + 40
		if (len < 429)
		{
			LogPrint (eLogWarning, "Datagram: datagram2 is too short ", len);
			return;
		}
		i2p::data::IdentityEx identity;
		size_t identityLen = identity.FromBuffer (buf, len);
		if (!identityLen || identityLen + 2 > len) return;
		const uint8_t * flags = buf + identityLen;
		if ((flags[1] & 0x0f) != 0x02) // Datagram2.java VERSION_MASK/VERSION
		{
			LogPrint (eLogWarning, "Datagram: datagram2 bad version ", (int)(flags[1] & 0x0f));
			return;
		}

		std::shared_ptr<i2p::data::LeaseSet> ls;
		if (from)
		{
			ls = m_Owner->FindLeaseSet (identity.GetIdentHash ());
			if (ls && !ls->IsEncryptionKey (from->GetRemoteStaticKeyType (), from->GetRemoteStaticKey ()))
			{
				LogPrint (eLogError, "Datagram: Remote LeaseSet static key mismatch for datagram2 from ",
					identity.GetIdentHash ().ToBase32 ());
				return;
			}
		}

		size_t offset = identityLen + 2;
		bool isOptions = false;
		// Per-call options: the receiver may hold the pointer for the duration of
		// the callback only, and a member would be overwritten by the next
		// datagram while the callback still runs.
		i2p::util::Mapping options;
		if (flags[1] & DATAGRAM2_FLAG_OPTIONS)
		{
			isOptions = true;
			if (!ParseDatagramOptions (options, buf, len, offset))
			{
				LogPrint (eLogWarning, "Datagram: datagram2 can't read options");
				return;
			}
		}
		if (offset > len)
		{
			LogPrint (eLogWarning, "Datagram: datagram2 is too short ", len, " expected ", offset);
			return;
		}

		// The offline section is part of the signed data and must be validated even
		// when the ratchet session's static key already matched the sender's
		// LeaseSet. Datagram2.java always verifies it and rejects expired delegations.
		std::shared_ptr<i2p::crypto::Verifier> transientVerifier;
		if (flags[1] & DATAGRAM2_FLAG_OFFLINE_SIGNATURE)
		{
			transientVerifier = i2p::data::ProcessOfflineSignature (&identity, buf, len, offset, true);
			if (!transientVerifier)
			{
				LogPrint (eLogWarning, "Datagram: datagram2 offline signature failed");
				return;
			}
		}
		size_t signatureLen = transientVerifier ? transientVerifier->GetSignatureLen () : identity.GetSignatureLen ();
		if (offset > len || signatureLen > len - offset)
		{
			LogPrint (eLogWarning, "Datagram: datagram2 is too short ", len, " expected ", offset + signatureLen);
			return;
		}
		// signed data = destination hash (not transmitted) || flags || options ||
		// offline section || payload
		if (len < identityLen + signatureLen) return;
		std::vector<uint8_t> signedData (32 + len - identityLen - signatureLen);
		memcpy (signedData.data (), m_Owner->GetIdentity ()->GetIdentHash (), 32);
		memcpy (signedData.data () + 32, buf + identityLen, len - identityLen - signatureLen);
		const uint8_t * signature = buf + len - signatureLen;
		bool verified = transientVerifier
			? transientVerifier->Verify (signedData.data (), signedData.size (), signature)
			: identity.Verify (signedData.data (), signedData.size (), signature);
		if (!verified)
		{
			LogPrint (eLogWarning, "Datagram: datagram2 signature verification failed");
			return;
		}

		const bool hadSession = FindSession (identity.GetIdentHash ()) != nullptr;
		auto session = ObtainSession (identity.GetIdentHash ());
		if (session)
		{
			// Do not mutate an existing session's outgoing version based on
			// the received datagram. Only a newly observed peer inherits the
			// protocol version that created it.
			if (!hadSession) session->SetVersion (eDatagramV2);
			if (ls) session->SetRemoteLeaseSet (ls);
			session->Ack ();
		}
		auto r = FindReceiver (toPort, i2p::client::PROTOCOL_TYPE_DATAGRAM2);
		if (r)
			r->Invoke (identity, fromPort, toPort, buf + offset, len - offset - signatureLen,
				isOptions ? &options : nullptr);
		else
			LogPrint (eLogWarning, "DatagramDestination: no receiver for port ", toPort);
	}

	void DatagramDestination::HandleDatagram3 (uint16_t fromPort, uint16_t toPort, const uint8_t * buf, size_t len,
		i2p::garlic::ECIESX25519AEADRatchetSession * from)
	{
		if (len < 34)
		{
			LogPrint (eLogWarning, "Datagram: datagram3 is too short ", len);
			return;
		}
		// Datagram3 is explicitly unauthenticated (proposal 163 / datagrams
		// spec). When it arrives through a ratchet session the router can
		// additionally validate the sender, but legacy/raw delivery must still
		// hand the hash to a HashReceiver. Datagram3.java load() itself does not
		// require a session or a cached LeaseSet.
		const uint8_t * flags = buf + 32;
		if ((flags[1] & 0x0f) != 0x03) // Datagram3.java VERSION_MASK/VERSION
		{
			LogPrint (eLogWarning, "Datagram: datagram3 bad version ", (int)(flags[1] & 0x0f));
			return;
		}
		i2p::data::IdentHash ident (buf);
		// If the ratchet session already established the peer destination, the
		// unauthenticated sender hash must not contradict it.
		if (from && from->GetDestinationPtr () && *from->GetDestinationPtr () != ident)
		{
			LogPrint (eLogError, "Datagram: datagram3 sender hash mismatch for ", ident.ToBase32 ());
			return;
		}
		size_t offset = 34;
		bool isOptions = false;
		i2p::util::Mapping options;
		if (flags[1] & DATAGRAM3_FLAG_OPTIONS)
		{
			isOptions = true;
			if (!ParseDatagramOptions (options, buf, len, offset))
			{
				LogPrint (eLogWarning, "Datagram: datagram3 can't read options");
				return;
			}
		}
		if (offset > len)
		{
			LogPrint (eLogWarning, "Datagram: datagram3 is too short ", len, " expected ", offset);
			return;
		}

		// First contact has no cached LeaseSet. Datagram3 deliberately carries
		// only an unauthenticated sender hash, so deliver it to the HashReceiver
		// without forcing a lookup; a cached LeaseSet (or a ratchet session)
		// can still be used for the optional extra checks above.
		auto session = FindSession (ident);
		bool acknowledgeSession = false;
		std::shared_ptr<const i2p::data::LeaseSet> ls;
		if (session) ls = session->GetRemoteLeaseSet ();
		if (ls && ls->IsExpired ()) ls = nullptr;
		if (!ls) ls = m_Owner->FindLeaseSet (ident);
		if (ls)
		{
			if (from && !ls->IsEncryptionKey (from->GetRemoteStaticKeyType (), from->GetRemoteStaticKey ()))
			{
				LogPrint (
					eLogError, "Datagram: Remote LeaseSet static key mismatch for datagram3 from ", ident.ToBase32 ());
				return;
			}
			// Datagram3 itself is unauthenticated. Only a ratchet session whose
			// destination matches, or whose static key is present in the cached
			// LeaseSet, may create/refresh a reply session. Hash-only delivery
			// below remains available without authentication.
			const bool authenticated = from
				&& (from->GetDestinationPtr ()
						? *from->GetDestinationPtr () == ident
						: ls->IsEncryptionKey (from->GetRemoteStaticKeyType (), from->GetRemoteStaticKey ()));
			if (authenticated)
			{
				if (!session)
				{
					session = ObtainSession (ident);
					if (session) session->SetVersion (eDatagramV3);
				}
				if (session) session->SetRemoteLeaseSet (ls);
				if (from && !from->GetDestinationPtr ()) from->SetDestination (ident);
				acknowledgeSession = session != nullptr;
			}
			else
				LogPrint (eLogWarning, "Datagram: unauthenticated datagram3 will not create a reply session");
		}

		auto r = FindHashReceiver (toPort, i2p::client::PROTOCOL_TYPE_DATAGRAM3);
		if (r)
			r->Invoke (ident, fromPort, toPort, buf + offset, len - offset, isOptions ? &options : nullptr);
		else
		{
			// Internal consumers such as the torrent tracker still use the
			// full-identity callback. Only promote when a verified cached
			// LeaseSet is available; a Datagram3 hash alone is never trusted.
			auto receiver = FindReceiver (toPort, i2p::client::PROTOCOL_TYPE_DATAGRAM3);
			if (receiver && ls)
				receiver->Invoke (*ls->GetIdentity (), fromPort, toPort, buf + offset, len - offset,
					isOptions ? &options : nullptr);
			else
				LogPrint (eLogWarning, "DatagramDestination: no hash receiver or cached identity for port ", toPort);
		}
		if (acknowledgeSession && session) session->Ack ();
	}

	DatagramDestination::ReceiverRegistration DatagramDestination::SetReceiver (
		const Receiver & receiver, uint16_t port, uint8_t protocol)
	{
		auto owner = weak_from_this ();
		if (owner.expired ()) throw std::logic_error ("DatagramDestination receiver requires shared ownership");
		auto slot = std::make_shared<detail::ReceiverSlot<Receiver>> (receiver);
		std::vector<ReceiverSlotPtr> retired;
		{
			std::lock_guard<std::mutex> lock (m_ReceiversMutex);
			if (port || protocol)
			{
				// A (protocol, port) registration stays keyed by both fields. It
				// is never promoted to the destination-wide fallback: Java only
				// falls back to an explicitly registered (PROTO_ANY, PORT_ANY)
				// listener, and promoting would disclose one subsession's traffic
				// to another.
				const auto key = MakeReceiverKey (protocol, port);
				auto it = m_ReceiversByProtocolPorts.find (key);
				if (it != m_ReceiversByProtocolPorts.end ()) retired.push_back (std::move (it->second));
				m_ReceiversByProtocolPorts[key] = slot;
			}
			else
			{
				if (m_DefaultReceiver) retired.push_back (std::move (m_DefaultReceiver));
				m_DefaultReceiver = slot;
			}
		}
		return ReceiverRegistration (std::move (owner), slot, port, ReceiverRegistration::Kind::Identity, protocol);
	}

	DatagramDestination::ReceiverRegistration DatagramDestination::SetHashReceiver (
		const HashReceiver & receiver, uint16_t port, uint8_t protocol)
	{
		auto owner = weak_from_this ();
		if (owner.expired ()) throw std::logic_error ("DatagramDestination receiver requires shared ownership");
		auto slot = std::make_shared<detail::ReceiverSlot<HashReceiver>> (receiver);
		std::vector<HashReceiverSlotPtr> retired;
		{
			std::lock_guard<std::mutex> lock (m_HashReceiversMutex);
			if (port || protocol)
			{
				const auto key = MakeReceiverKey (protocol, port);
				auto it = m_HashReceiversByProtocolPorts.find (key);
				if (it != m_HashReceiversByProtocolPorts.end ()) retired.push_back (std::move (it->second));
				m_HashReceiversByProtocolPorts[key] = slot;
			}
			else
			{
				if (m_DefaultHashReceiver) retired.push_back (std::move (m_DefaultHashReceiver));
				m_DefaultHashReceiver = slot;
			}
		}
		return ReceiverRegistration (std::move (owner), slot, port, ReceiverRegistration::Kind::Hash, protocol);
	}

	DatagramDestination::ReceiverRegistration DatagramDestination::SetRawReceiver (
		const RawReceiver & receiver, uint16_t port)
	{
		// Legacy callers (UDP tunnels, torrents) keep receiving every raw
		// protocol on the selected port through the protocol-0 catch-all.
		return SetRawReceiver (receiver, port, 0);
	}

	DatagramDestination::ReceiverRegistration DatagramDestination::SetRawReceiver (
		const RawReceiver & receiver, uint16_t port, uint8_t protocol)
	{
		auto owner = weak_from_this ();
		if (owner.expired ()) throw std::logic_error ("DatagramDestination receiver requires shared ownership");
		if (IsReservedRawProtocol (protocol))
		{
			// Streaming and the typed datagram protocols are consumed by their
			// own handlers, so such a registration could never receive
			// anything; refusing it keeps the rejection observable to SAM.
			LogPrint (eLogWarning, "DatagramDestination: protocol ", (int)protocol,
				" is not a raw receiver protocol");
			return { };
		}
		auto slot = std::make_shared<detail::ReceiverSlot<RawReceiver>> (receiver);
		std::vector<RawReceiverSlotPtr> retired;
		{
			std::lock_guard<std::mutex> lock (m_RawReceiversMutex);
			const auto key = MakeReceiverKey (protocol, port);
			auto it = m_RawReceiversByProtocolPorts.find (key);
			if (it != m_RawReceiversByProtocolPorts.end ()) retired.push_back (std::move (it->second));
			m_RawReceiversByProtocolPorts[key] = slot;
			// Only a catch-all registration participates in the historical
			// destination-wide fallback; a protocol-specific registration must
			// never capture unrelated raw protocols. The first registration (or
			// an explicit port-0 one) supplies that fallback, as before.
			if (!protocol && (!port || !m_DefaultRawReceiver || !m_DefaultRawReceiver->HasCallback ()))
			{
				if (m_DefaultRawReceiver) retired.push_back (std::move (m_DefaultRawReceiver));
				m_DefaultRawReceiver = slot;
			}
		}
		return ReceiverRegistration (std::move (owner), slot, port, ReceiverRegistration::Kind::Raw, protocol);
	}

	bool DatagramDestination::ReceiverRegistration::Reset () const
	{
		if (auto destination = m_Destination.lock ()) return destination->ResetRegistration (*this);
		return true;
	}

	bool DatagramDestination::ResetRegistration (const ReceiverRegistration & registration)
	{
		auto identity = registration.m_Slot.lock ();
		if (!identity) return true;
		bool drained = false;
		switch (registration.m_Kind)
		{
		case ReceiverRegistration::Kind::Identity:
		{
			auto slot = std::static_pointer_cast<detail::ReceiverSlot<Receiver>> (identity);
			{
				std::lock_guard<std::mutex> lock (m_ReceiversMutex);
				if (registration.m_Port || registration.m_Protocol)
				{
					auto it = m_ReceiversByProtocolPorts.find (
						MakeReceiverKey (registration.m_Protocol, registration.m_Port));
					if (it != m_ReceiversByProtocolPorts.end () && it->second.get () == identity.get ())
					{
						m_ReceiversByProtocolPorts.erase (it);
					}
				}
				else if (m_DefaultReceiver.get () == identity.get ())
				{
					m_DefaultReceiver.reset ();
				}
			}
			if (slot) drained = slot->DeactivateAndWait ();
			break;
		}
		case ReceiverRegistration::Kind::Hash:
		{
			auto slot = std::static_pointer_cast<detail::ReceiverSlot<HashReceiver>> (identity);
			{
				std::lock_guard<std::mutex> lock (m_HashReceiversMutex);
				if (registration.m_Port || registration.m_Protocol)
				{
					auto it = m_HashReceiversByProtocolPorts.find (
						MakeReceiverKey (registration.m_Protocol, registration.m_Port));
					if (it != m_HashReceiversByProtocolPorts.end () && it->second.get () == identity.get ())
					{
						m_HashReceiversByProtocolPorts.erase (it);
					}
				}
				else if (m_DefaultHashReceiver.get () == identity.get ())
				{
					m_DefaultHashReceiver.reset ();
				}
			}
			if (slot) drained = slot->DeactivateAndWait ();
			break;
		}
		case ReceiverRegistration::Kind::Raw:
		{
			auto slot = std::static_pointer_cast<detail::ReceiverSlot<RawReceiver>> (identity);
			{
				std::lock_guard<std::mutex> lock (m_RawReceiversMutex);
				auto it = m_RawReceiversByProtocolPorts.find (
					MakeReceiverKey (registration.m_Protocol, registration.m_Port));
				if (it != m_RawReceiversByProtocolPorts.end () && it->second.get () == identity.get ())
				{
					m_RawReceiversByProtocolPorts.erase (it);
				}
				if (m_DefaultRawReceiver.get () == identity.get ())
				{
					m_DefaultRawReceiver.reset ();
				}
			}
			if (slot) drained = slot->DeactivateAndWait ();
			break;
		}
		}
		return drained;
	}

	DatagramDestination::ReceiverSlotPtr DatagramDestination::FindReceiver (uint16_t port, uint8_t protocol)
	{
		std::lock_guard<std::mutex> lock (m_ReceiversMutex);
		// Fallback order (Java PrimarySession): the exact (protocol, port) pair,
		// the protocol-0 catch-all on that port, the protocol wildcard, and only
		// then the explicitly registered destination-wide fallback.
		if (protocol)
		{
			auto it = m_ReceiversByProtocolPorts.find (MakeReceiverKey (protocol, port));
			if (it != m_ReceiversByProtocolPorts.end ()) return it->second;
			if (port)
			{
				it = m_ReceiversByProtocolPorts.find (MakeReceiverKey (0, port));
				if (it != m_ReceiversByProtocolPorts.end ()) return it->second;
			}
			it = m_ReceiversByProtocolPorts.find (MakeReceiverKey (protocol, 0));
			if (it != m_ReceiversByProtocolPorts.end ()) return it->second;
		}
		else if (port)
		{
			auto it = m_ReceiversByProtocolPorts.find (MakeReceiverKey (0, port));
			if (it != m_ReceiversByProtocolPorts.end ()) return it->second;
		}
		return m_DefaultReceiver;
	}

	DatagramDestination::HashReceiverSlotPtr DatagramDestination::FindHashReceiver (uint16_t port, uint8_t protocol)
	{
		std::lock_guard<std::mutex> lock (m_HashReceiversMutex);
		if (protocol)
		{
			auto it = m_HashReceiversByProtocolPorts.find (MakeReceiverKey (protocol, port));
			if (it != m_HashReceiversByProtocolPorts.end ()) return it->second;
			if (port)
			{
				it = m_HashReceiversByProtocolPorts.find (MakeReceiverKey (0, port));
				if (it != m_HashReceiversByProtocolPorts.end ()) return it->second;
			}
			it = m_HashReceiversByProtocolPorts.find (MakeReceiverKey (protocol, 0));
			if (it != m_HashReceiversByProtocolPorts.end ()) return it->second;
		}
		else if (port)
		{
			auto it = m_HashReceiversByProtocolPorts.find (MakeReceiverKey (0, port));
			if (it != m_HashReceiversByProtocolPorts.end ()) return it->second;
		}
		return m_DefaultHashReceiver;
	}

	DatagramDestination::RawReceiverSlotPtr DatagramDestination::FindRawReceiver (uint16_t port, uint8_t protocol)
	{
		// Defence in depth: registration already refuses these, and no typed
		// protocol may be forwarded to a raw receiver even if one exists.
		if (IsReservedRawProtocol (protocol)) return nullptr;
		std::lock_guard<std::mutex> lock (m_RawReceiversMutex);
		// Java's SAM 3.3 routing prefers an exact protocol match on the
		// addressed port, then the protocol-0 catch-all on that port, then the
		// port wildcards, and finally the destination-wide fallback.
		if (protocol)
		{
			auto it = m_RawReceiversByProtocolPorts.find (MakeReceiverKey (protocol, port));
			if (it != m_RawReceiversByProtocolPorts.end ()) return it->second;
			if (port)
			{
				it = m_RawReceiversByProtocolPorts.find (MakeReceiverKey (0, port));
				if (it != m_RawReceiversByProtocolPorts.end ()) return it->second;
			}
			it = m_RawReceiversByProtocolPorts.find (MakeReceiverKey (protocol, 0));
			if (it != m_RawReceiversByProtocolPorts.end ()) return it->second;
		}
		else if (port)
		{
			auto it = m_RawReceiversByProtocolPorts.find (MakeReceiverKey (0, port));
			if (it != m_RawReceiversByProtocolPorts.end ()) return it->second;
		}
		return m_DefaultRawReceiver;
	}

	void DatagramDestination::HandleDataMessagePayload (uint16_t fromPort, uint16_t toPort, const uint8_t * buf,
		size_t len, uint8_t protocolType, i2p::garlic::ECIESX25519AEADRatchetSession * from)
	{
		// I2CP passes the payload protocol byte through unchanged and a peer (or
		// a local UDP client) can send the OS-level "unspecified" value 0xff.
		// Java maps it to PROTO_UNSPECIFIED (0) before listener selection.
		if (protocolType == 0xff) protocolType = 0;
		// unzip it. A valid gzip stream may produce zero bytes; only a
		// decompression failure is dropped. The inflator keeps zlib state per
		// call, so concurrent delivery must not share it.
		uint8_t uncompressed[MAX_DATAGRAM_SIZE];
		size_t uncompressedLen = 0;
		bool inflated;
		{
			std::lock_guard<std::mutex> lock (m_InflatorMutex);
			inflated = m_Inflator.InflateChecked (buf, len, uncompressed, MAX_DATAGRAM_SIZE, uncompressedLen);
		}
		if (inflated)
		{
			switch (protocolType)
			{
			case i2p::client::PROTOCOL_TYPE_RAW:
				HandleRawDatagram (protocolType, fromPort, toPort, uncompressed, uncompressedLen);
				break;
			case i2p::client::PROTOCOL_TYPE_DATAGRAM3:
				HandleDatagram3 (fromPort, toPort, uncompressed, uncompressedLen, from);
				break;
			case i2p::client::PROTOCOL_TYPE_DATAGRAM:
				HandleDatagram (fromPort, toPort, uncompressed, uncompressedLen, from);
				break;
			case i2p::client::PROTOCOL_TYPE_DATAGRAM2:
				HandleDatagram2 (fromPort, toPort, uncompressed, uncompressedLen, from);
				break;
			default: // SAM RAW sessions can register arbitrary I2CP protocols
				HandleRawDatagram (protocolType, fromPort, toPort, uncompressed, uncompressedLen);
			};
		}
		else
			LogPrint (eLogWarning, "Datagram: decompression failed");
	}

	std::shared_ptr<I2NPMessage> DatagramDestination::CreateDataMessage (
		const std::vector<std::pair<const uint8_t *, size_t>> & payloads, uint16_t fromPort, uint16_t toPort,
		uint8_t protocolType, bool checksum)
	{
		size_t size;
		// The pool is thread-safe (SAM sends on its socket thread while inbound
		// Acks release messages on the destination service); the deflator holds
		// one zlib stream and is serialized here.
		auto msg = m_I2NPMsgsPool.AcquireSharedMt ();
		uint8_t * buf = msg->GetPayload ();
		buf += 4; // reserve for length
		msg->len += 4; // account for the reserved length before deflating

		{
			std::lock_guard<std::mutex> lock (m_DeflatorMutex);
			if (m_Gzip && m_Deflator)
				size = m_Deflator->Deflate (payloads, buf, msg->maxLen - msg->len);
			else
				size = i2p::data::GzipNoCompression (payloads, buf, msg->maxLen - msg->len);
		}

		if (size)
		{
			htobe32buf (msg->GetPayload (), static_cast<uint32_t> (size)); // length
			htobe16buf (buf + 4, fromPort); // source port
			htobe16buf (buf + 6, toPort); // destination port
			buf[9] = protocolType; // raw or datagram protocol
			msg->len += size;
			msg->FillI2NPMessageHeader (eI2NPData, m_Owner->GetRng () (), checksum);
		}
		else
			msg = nullptr;
		return msg;
	}

	void DatagramDestination::CleanUp ()
	{
		auto now = i2p::util::GetMillisecondsSinceEpoch ();
		std::unique_lock<std::mutex> lock (m_SessionsMutex);
		if (m_Sessions.empty ()) return;
		LogPrint (eLogDebug, "DatagramDestination: clean up sessions");
		// for each session ...
		for (auto it = m_Sessions.begin (); it != m_Sessions.end ();)
		{
			// check if expired
			if (now - it->second->LastActivity () >= DATAGRAM_SESSION_MAX_IDLE)
			{
				LogPrint (eLogInfo, "DatagramDestination: expiring idle session with ", it->first.ToBase32 ());
				it->second->Stop ();
				it = m_Sessions.erase (it); // we are expired
			}
			else
				it++;
		}
	}

	std::shared_ptr<DatagramSession> DatagramDestination::ObtainSession (const i2p::data::IdentHash & identity)
	{
		std::shared_ptr<DatagramSession> session = nullptr;
		std::lock_guard<std::mutex> lock (m_SessionsMutex);
		auto itr = m_Sessions.find (identity);
		if (itr == m_Sessions.end ())
		{
			if (m_Sessions.size () >= DATAGRAM_SESSIONS_MAX_SIZE)
			{
				// Datagram3 first contact names an unauthenticated hash before any
				// LeaseSet exists, so never let these create unbounded state. Reclaim
				// idle entries if possible, otherwise refuse the new one.
				auto now = i2p::util::GetMillisecondsSinceEpoch ();
				for (auto it = m_Sessions.begin (); it != m_Sessions.end ();)
				{
					if (now - it->second->LastActivity () >= DATAGRAM_SESSION_MAX_IDLE)
					{
						it->second->Stop ();
						it = m_Sessions.erase (it);
					}
					else
						++it;
				}
				if (m_Sessions.size () >= DATAGRAM_SESSIONS_MAX_SIZE)
				{
					LogPrint (eLogWarning, "Datagram: session table is full (", m_Sessions.size (),
						"), dropping new session for ", identity.ToBase32 ());
					return nullptr;
				}
			}
			// not found, create new session
			session = std::make_shared<DatagramSession> (m_Owner, identity);
			// The destination default is normally V1/V2/V3; an unset or unknown
			// value must not leave every send unsupported.
			session->SetVersion (IsDatagramVersionSupported (m_Version) ? m_Version : eDatagramV1);
			session->Start ();
			m_Sessions.emplace (identity, session);
		}
		else
			session = itr->second;
		return session;
	}

	std::shared_ptr<DatagramSession> DatagramDestination::FindSession (const i2p::data::IdentHash & ident) const
	{
		std::lock_guard<std::mutex> lock (m_SessionsMutex);
		auto it = m_Sessions.find (ident);
		if (it != m_Sessions.end ()) return it->second;
		return nullptr;
	}

	std::shared_ptr<DatagramSession::Info> DatagramDestination::GetInfoForRemote (const i2p::data::IdentHash & remote)
	{
		std::lock_guard<std::mutex> lock (m_SessionsMutex);
		for (auto & item : m_Sessions)
		{
			if (item.first == remote) return std::make_shared<DatagramSession::Info> (item.second->GetSessionInfo ());
		}
		return nullptr;
	}

	DatagramSession::DatagramSession (
		std::shared_ptr<i2p::client::ClientDestination> localDestination, const i2p::data::IdentHash & remoteIdent)
		: m_LocalDestination (localDestination)
		, m_RemoteIdent (remoteIdent)
	{
	}

	void DatagramSession::Start ()
	{
		m_LastUse.store (i2p::util::GetMillisecondsSinceEpoch (), std::memory_order_relaxed);
	}

	void DatagramSession::Stop () { }

	bool DatagramSession::IsRatchets () const
	{
		std::lock_guard<std::mutex> lock (m_Mutex);
		return IsRatchetsLocked ();
	}

	void DatagramSession::SetRemoteLeaseSet (std::shared_ptr<const i2p::data::LeaseSet> ls)
	{
		std::lock_guard<std::mutex> lock (m_Mutex);
		m_RemoteLeaseSet = std::move (ls);
		m_LoggedIncompatibleLeaseSet = false;
	}

	std::shared_ptr<const i2p::data::LeaseSet> DatagramSession::GetRemoteLeaseSet () const
	{
		std::lock_guard<std::mutex> lock (m_Mutex);
		return m_RemoteLeaseSet;
	}

	void DatagramSession::DropSharedRoutingPath ()
	{
		std::lock_guard<std::mutex> lock (m_Mutex);
		if (m_RoutingSession)
		{
			m_RoutingSession->SetSharedRoutingPath (nullptr);
			m_NumPathDrops.fetch_add (1, std::memory_order_relaxed);
		}
	}

	void DatagramSession::SetSendObserverForTesting (std::function<void (const I2NPMessage &)> observer)
	{
		std::lock_guard<std::mutex> lock (m_Mutex);
		m_SendObserver = std::move (observer);
	}

	void DatagramSession::SendMsg (std::shared_ptr<I2NPMessage> msg)
	{
		// Read the observer under the lock, then invoke it outside so a test
		// callback can inspect the message without re-entering the lock.
		std::function<void (const I2NPMessage &)> observer;
		{
			std::lock_guard<std::mutex> lock (m_Mutex);
			observer = m_SendObserver;
		}
		if (observer)
		{
			if (msg) observer (*msg);
			return;
		}
		std::lock_guard<std::mutex> lock (m_Mutex);
		SendMsgLocked (std::move (msg));
	}

	void DatagramSession::SendMsgLocked (std::shared_ptr<I2NPMessage> msg)
	{
		// we used this session
		const auto now = i2p::util::GetMillisecondsSinceEpoch ();
		m_LastUse.store (now, std::memory_order_relaxed);
		if (msg || m_SendQueue.empty ()) m_SendQueue.push_back (msg);
		// flush queue right away if full
		if (!msg || m_SendQueue.size () >= DATAGRAM_SEND_QUEUE_MAX_SIZE || now > m_LastFlush + DATAGRAM_MAX_FLUSH_INTERVAL)
		{
			FlushSendQueueLocked (true);
			m_LastFlush = now;
		}
	}

	DatagramSession::Info DatagramSession::GetSessionInfo () const
	{
		const auto activity = m_LastUse.load (std::memory_order_relaxed);
		std::shared_ptr<i2p::garlic::GarlicRoutingSession> routingSession;
		{
			std::lock_guard<std::mutex> lock (m_Mutex);
			routingSession = m_RoutingSession;
		}
		if (!routingSession) return DatagramSession::Info (nullptr, nullptr, activity);

		auto routingPath = routingSession->GetSharedRoutingPath ();
		if (!routingPath) return DatagramSession::Info (nullptr, nullptr, activity);
		auto lease = routingPath->remoteLease;
		auto tunnel = routingPath->outboundTunnel;
		if (lease)
		{
			if (tunnel)
				return DatagramSession::Info (lease->tunnelGateway, tunnel->GetEndpointIdentHash (), activity);
			else
				return DatagramSession::Info (lease->tunnelGateway, nullptr, activity);
		}
		else if (tunnel)
			return DatagramSession::Info (nullptr, tunnel->GetEndpointIdentHash (), activity);
		else
			return DatagramSession::Info (nullptr, nullptr, activity);
	}

	void DatagramSession::Ack ()
	{
		std::lock_guard<std::mutex> lock (m_Mutex);
		m_LastUse.store (i2p::util::GetMillisecondsSinceEpoch (), std::memory_order_relaxed);
		auto path = GetSharedRoutingPath ();
		if (path) path->updateTime = static_cast<uint32_t> (i2p::util::GetSecondsSinceEpoch ());
		if (IsRatchetsLocked ()) SendMsgLocked (nullptr); // send empty message in case if we don't have some data to send
	}

	std::shared_ptr<i2p::garlic::GarlicRoutingPath> DatagramSession::GetSharedRoutingPath ()
	{
		if (!m_RemoteLeaseSet || m_RemoteLeaseSet->IsExpired ())
		{
			m_RemoteLeaseSet = m_LocalDestination->FindLeaseSet (m_RemoteIdent);
			if (!m_RemoteLeaseSet)
			{
				if (!m_RequestingLS)
				{
					m_RequestingLS = true;
					m_LocalDestination->RequestDestination (m_RemoteIdent,
						std::bind (
							&DatagramSession::HandleLeaseSetUpdated, shared_from_this (), std::placeholders::_1));
				}
				return nullptr;
			}
		}

		if (m_RemoteLeaseSet)
		{
			// A standard LS2 that advertises no key type this destination can
			// use has no encryptor (LeaseSet2::IsIncompatibleCrypto). The
			// ElGamal/AES fallback in GetRoutingSession would then encrypt
			// with a null encryptor and send an uninitialised block. LS1 is
			// excluded because its incompatibility flag describes inbound
			// ElGamal support, while outbound encryption still uses the
			// advertised LS1 key.
			if (m_RemoteLeaseSet->GetStoreType () != i2p::data::NETDB_STORE_TYPE_LEASESET
				&& m_RemoteLeaseSet->IsIncompatibleCrypto ())
			{
				if (!m_LoggedIncompatibleLeaseSet)
				{
					LogPrint (eLogError, "Datagram: Remote LeaseSet has no usable encryption key for ",
						m_RemoteIdent.ToBase32 ());
					m_LoggedIncompatibleLeaseSet = true;
				}
				return nullptr;
			}
			m_LoggedIncompatibleLeaseSet = false;
		}

		if (!m_RoutingSession || m_RoutingSession->IsTerminated () || !m_RoutingSession->IsReadyToSend ()
			|| (m_RoutingSession->GetLastActivityTimestamp ()
				&& m_RoutingSession->IsInactive (i2p::util::
						GetSecondsSinceEpoch ()))) // request new garlic session if established session became inactive
		{
			bool found = false;
			if (!m_PendingRoutingSessions.empty ())
			{
				std::vector<std::weak_ptr<i2p::garlic::GarlicRoutingSession>> tmp;
				for (auto & it : m_PendingRoutingSessions)
				{
					auto s = it.lock ();
					if (s)
					{
						if (s->GetOwner () && s->IsReadyToSend ()) // found established session
						{
							m_RoutingSession = s;
							tmp.clear ();
							found = true;
							break;
						}
						tmp.push_back (s);
					}
				}
				m_PendingRoutingSessions.swap (tmp);
			}
			if (!found)
			{
				m_RoutingSession = m_LocalDestination->GetRoutingSession (m_RemoteLeaseSet, true);
				if (m_RoutingSession)
				{
					m_RoutingSession->SetAckRequestInterval (DATAGRAM_SESSION_ACK_REQUEST_INTERVAL);
					if (!m_RoutingSession->GetOwner () || !m_RoutingSession->IsReadyToSend ())
						m_PendingRoutingSessions.push_back (m_RoutingSession);
				}
			}
		}

		if (!m_RoutingSession) return nullptr;
		auto path = m_RoutingSession->GetSharedRoutingPath ();
		if (path && m_RoutingSession->IsRatchets () && m_RoutingSession->CleanupUnconfirmedTags ())
		{
			LogPrint (eLogDebug, "Datagram: path reset");
			m_RoutingSession->SetSharedRoutingPath (nullptr);
			m_RoutingSession = nullptr; // re-query garlic session, it gets restarted if inactive
			return GetSharedRoutingPath ();
		}

		if (path)
		{
			if (path->outboundTunnel && !path->outboundTunnel->IsEstablished ())
			{
				// bad outbound tunnel, switch outbound tunnel
				path->outboundTunnel
					= m_LocalDestination->GetTunnelPool ()->GetNextOutboundTunnel (path->outboundTunnel);
				if (!path->outboundTunnel) m_RoutingSession->SetSharedRoutingPath (nullptr);
			}

			if (path->remoteLease && path->remoteLease->ExpiresWithin (DATAGRAM_SESSION_LEASE_HANDOVER_WINDOW))
			{
				// bad lease, switch to next one
				if (m_RemoteLeaseSet)
				{
					auto ls = m_RemoteLeaseSet->GetNonExpiredLeasesExcluding (
						[&] (const i2p::data::Lease & l) -> bool { return l.tunnelID == path->remoteLease->tunnelID; });
					auto sz = ls.size ();
					if (sz)
					{
						int idx = -1;
						if (m_LocalDestination) idx = m_LocalDestination->GetRng () () % sz;
						if (idx < 0) idx = i2p::crypto::RandomBelow (static_cast<uint32_t> (sz));
						path->remoteLease = ls[idx];
					}
					else
						m_RoutingSession->SetSharedRoutingPath (nullptr);
				}
				else
				{
					// no remote lease set?
					LogPrint (
						eLogWarning, "DatagramSession: no cached remote lease set for ", m_RemoteIdent.ToBase32 ());
					m_RoutingSession->SetSharedRoutingPath (nullptr);
				}
			}
		}
		else
		{
			// no current path, make one
			path = std::make_shared<i2p::garlic::GarlicRoutingPath> ();

			if (m_RemoteLeaseSet)
			{
				// pick random next good lease
				auto ls = m_RemoteLeaseSet->GetNonExpiredLeases ();
				auto sz = ls.size ();
				if (sz)
				{
					int idx = -1;
					if (m_LocalDestination) idx = m_LocalDestination->GetRng () () % sz;
					if (idx < 0) idx = i2p::crypto::RandomBelow (static_cast<uint32_t> (sz));
					path->remoteLease = ls[idx];
				}
				else
					return nullptr;

				auto leaseRouter = i2p::data::netdb.FindRouter (path->remoteLease->tunnelGateway);
				path->outboundTunnel = m_LocalDestination->GetTunnelPool ()->GetNextOutboundTunnel (nullptr,
					leaseRouter ? leaseRouter->GetCompatibleTransports (false)
								: (i2p::data::RouterInfo::CompatibleTransports)i2p::data::RouterInfo::eAllTransports);
				if (!path->outboundTunnel) return nullptr;
			}
			else
			{
				// no remote lease set currently, bail
				LogPrint (eLogWarning, "DatagramSession: no remote lease set found for ", m_RemoteIdent.ToBase32 ());
				return nullptr;
			}
			m_RoutingSession->SetSharedRoutingPath (path);
		}
		return path;
	}

	void DatagramSession::HandleLeaseSetUpdated (std::shared_ptr<i2p::data::LeaseSet> ls)
	{
		std::lock_guard<std::mutex> lock (m_Mutex);
		m_RequestingLS = false;
		if (ls)
		{
			// only update lease set if found and newer than previous lease set
			uint64_t oldExpire = 0;
			if (m_RemoteLeaseSet) oldExpire = m_RemoteLeaseSet->GetExpirationTime ();
			if (ls && ls->GetExpirationTime () > oldExpire)
			{
				m_RemoteLeaseSet = ls;
				m_LoggedIncompatibleLeaseSet = false;
			}
		}
		FlushSendQueueLocked (false); // process deferred flush
	}

	void DatagramSession::RequestUpdatedLeaseSet ()
	{
		std::lock_guard<std::mutex> lock (m_Mutex);
		if (!m_RequestingLS)
		{
			m_RequestingLS = true;
			m_LocalDestination->RequestDestination (m_RemoteIdent,
				std::bind (&DatagramSession::HandleLeaseSetUpdated, shared_from_this (), std::placeholders::_1));
		}
	}

	void DatagramSession::FlushSendQueue (bool waitForLeaseSet)
	{
		std::lock_guard<std::mutex> lock (m_Mutex);
		FlushSendQueueLocked (waitForLeaseSet);
	}

	void DatagramSession::FlushSendQueueLocked (bool waitForLeaseSet)
	{
		if (m_SendQueue.empty ()) return;
		auto routingPath = GetSharedRoutingPath ();
		if (!routingPath && waitForLeaseSet && m_RequestingLS) return; // defer to HandleLeaseSetUpdated
		// if we don't have a routing path we will drop all queued messages
		if (routingPath && routingPath->outboundTunnel && routingPath->remoteLease)
		{
			if (m_Version.load (std::memory_order_relaxed) == eDatagramV3)
			{
				auto msgs = m_RoutingSession->WrapMultipleMessages (m_SendQueue);
				if (!msgs.empty ())
					routingPath->outboundTunnel->SendTunnelDataMsgsTo (
						routingPath->remoteLease->tunnelGateway, routingPath->remoteLease->tunnelID, msgs);
			}
			else
			{
				// for compatibility with older versions
				std::vector<i2p::tunnel::TunnelMessageBlock> send;
				for (const auto & msg : m_SendQueue)
				{
					auto m = m_RoutingSession->WrapSingleMessage (msg);
					if (m)
						send.push_back (i2p::tunnel::TunnelMessageBlock { i2p::tunnel::eDeliveryTypeTunnel,
							routingPath->remoteLease->tunnelGateway, routingPath->remoteLease->tunnelID, m });
				}
				routingPath->outboundTunnel->SendTunnelDataMsgs (send);
			}
		}
		else
		{
			const auto dropped = static_cast<uint32_t> (m_SendQueue.size ());
			const auto total = m_NumDroppedNoPath.fetch_add (dropped, std::memory_order_relaxed) + dropped;
			LogPrint (eLogWarning, "Datagram: No routing path, dropped ", m_SendQueue.size (), " messages, ", total,
				" total");
		}
		m_SendQueue.clear ();
	}
}
}
