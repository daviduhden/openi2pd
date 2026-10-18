/*
* Copyright (c) 2013-2026, The PurpleI2P Project
* Copyright (c) 2026, David Uhden Collado
* SPDX-License-Identifier: BSD-3-Clause
*
* This file is part of openi2pd and licensed under BSD-3-Clause
*
* See full license text in LICENSE file at top of project tree
*/

#include <string.h>
#include "Crypto.h"
#include "Log.h"
#include "TunnelBase.h"
#include "RouterContext.h"
#include "Destination.h"
#include "Datagram.h"

namespace i2p
{
namespace datagram
{
	DatagramDestination::DatagramDestination (
		std::shared_ptr<i2p::client::ClientDestination> owner, bool gzip, DatagramVersion version)
		: m_Owner (owner)
		, m_DefaultReceiver (nullptr)
		, m_DefaultHashReceiver (nullptr)
		, m_DefaultRawReceiver (nullptr)
		, m_DefaultReceiverPort (0)
		, m_DefaultHashReceiverPort (0)
		, m_DefaultRawReceiverPort (0)
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

	void DatagramDestination::SendDatagramTo (
		const uint8_t * payload, size_t len, const i2p::data::IdentHash & identity, uint16_t fromPort, uint16_t toPort)
	{
		auto session = ObtainSession (identity);
		SendDatagram (session, payload, len, fromPort, toPort);
		FlushSendQueue (session);
	}

	void DatagramDestination::SendRawDatagramTo (const uint8_t * payload, size_t len, const i2p::data::IdentHash & identity,
		uint16_t fromPort, uint16_t toPort, uint8_t protocol)
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
		size_t len, uint16_t fromPort, uint16_t toPort, const i2p::util::Mapping * options)
	{
		if (session)
		{
			std::shared_ptr<I2NPMessage> msg;
			switch (session->GetVersion ())
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
				if (m_Owner->GetIdentity ()->GetSigningKeyType () == i2p::data::SIGNING_KEY_TYPE_DSA_SHA1)
				{
					uint8_t hash[32];
					SHA256 (payload, len, hash);
					m_Owner->Sign (hash, 32, signature.data ());
				}
				else
					m_Owner->Sign (payload, len, signature.data ());
				msg = CreateDataMessage ({ { m_From.data (), m_From.size () }, { signature.data (), signature.size () },
											 { payload, len } },
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
				uint8_t flags[2] = { 0x00, (uint8_t)(0x02 | (isOfflineSignature ? DATAGRAM2_FLAG_OFFLINE_SIGNATURE : 0)) };
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
					if (offlineSignature.size () < 6)
					{
						LogPrint (eLogError, "Datagram: malformed offline signature section");
						break;
					}
				}
				std::vector<uint8_t> signedData;
				signedData.reserve (32 + 2 + optionsBuf.size () + offlineSignature.size () + len);
				signedData.insert (signedData.end (), session->GetRemoteIdent ().data (),
					session->GetRemoteIdent ().data () + 32);
				signedData.insert (signedData.end (), flags, flags + 2);
				signedData.insert (signedData.end (), optionsBuf.begin (), optionsBuf.end ());
				signedData.insert (signedData.end (), offlineSignature.begin (), offlineSignature.end ());
				signedData.insert (signedData.end (), payload, payload + len);
				std::vector<uint8_t> signature (keys.GetSignatureLen ());
				if (!signature.empty ())
				{
					m_Owner->Sign (signedData.data (), signedData.size (), signature.data ());
					msg = CreateDataMessage ({ { m_From.data (), m_From.size () }, { flags, 2 },
												 { optionsBuf.data (), optionsBuf.size () },
												 { offlineSignature.data (), offlineSignature.size () }, { payload, len },
												 { signature.data (), signature.size () } },
						fromPort, toPort, i2p::client::PROTOCOL_TYPE_DATAGRAM2, false); // datagram2
				}
				break;
			}
			default: LogPrint (eLogError, "Datagram: datagram type ", (int)session->GetVersion (), " is not supported");
			}
			if (msg) session->SendMsg (msg);
		}
	}

	void DatagramDestination::SendRawDatagram (std::shared_ptr<DatagramSession> session, const uint8_t * payload,
		size_t len, uint16_t fromPort, uint16_t toPort, uint8_t protocol)
	{
		if (session)
			session->SendMsg (CreateDataMessage ({ { payload, len } }, fromPort, toPort,
				!protocol ? i2p::client::PROTOCOL_TYPE_RAW : protocol, !session->IsRatchets ())); // raw
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
			auto session = ObtainSession (identity.GetIdentHash ());
			if (ls) session->SetRemoteLeaseSet (ls);
			session->Ack ();
			auto r = FindReceiver (toPort);
			if (r)
				r (identity, fromPort, toPort, buf + headerLen, len - headerLen, nullptr);
			else
				LogPrint (eLogWarning, "DatagramDestination: no receiver for port ", toPort);
		}
		else
			LogPrint (eLogWarning, "Datagram signature verification failed");
	}

	void DatagramDestination::HandleRawDatagram (
		uint8_t protocol, uint16_t fromPort, uint16_t toPort, const uint8_t * buf, size_t len)
	{
		auto r = FindRawReceiver (toPort);
		if (r)
			r (protocol, fromPort, toPort, buf, len);
		else
			LogPrint (eLogWarning, "DatagramDestination: no receiver for raw datagram");
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
		if (flags[1] & DATAGRAM2_FLAG_OPTIONS)
		{
			isOptions = true;
			m_Options.CleanUp ();
			auto optionsLen = m_Options.FromBuffer (buf + offset, len - offset);
			if (optionsLen)
				offset += optionsLen;
			else
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
			transientVerifier = i2p::data::ProcessOfflineSignature (&identity, buf, len, offset);
			if (!transientVerifier)
			{
				LogPrint (eLogWarning, "Datagram: datagram2 offline signature failed");
				return;
			}
		}
		size_t signatureLen
			= transientVerifier ? transientVerifier->GetSignatureLen () : identity.GetSignatureLen ();
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

		auto session = ObtainSession (identity.GetIdentHash ());
		if (session)
		{
			session->SetVersion (eDatagramV2);
			if (ls) session->SetRemoteLeaseSet (ls);
			session->Ack ();
		}
		auto r = FindReceiver (toPort);
		if (r)
			r (identity, fromPort, toPort, buf + offset, len - offset - signatureLen, isOptions ? &m_Options : nullptr);
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
		if (!from)
		{
			// Datagram3 has no signature of its own; it is only meaningful inside an
			// authenticated ratchet session (Datagram3.java, proposal 163).
			LogPrint (eLogInfo, "Datagram: datagram3 received from non-ratchets session");
			return;
		}
		const uint8_t * flags = buf + 32;
		if ((flags[1] & 0x0f) != 0x03) // Datagram3.java VERSION_MASK/VERSION
		{
			LogPrint (eLogWarning, "Datagram: datagram3 bad version ", (int)(flags[1] & 0x0f));
			return;
		}
		i2p::data::IdentHash ident (buf);
		// If the ratchet session already established the peer destination, the
		// unauthenticated sender hash must not contradict it.
		if (from->GetDestinationPtr () && *from->GetDestinationPtr () != ident)
		{
			LogPrint (eLogError, "Datagram: datagram3 sender hash mismatch for ", ident.ToBase32 ());
			return;
		}
		size_t offset = 34;
		bool isOptions = false;
		if (flags[1] & DATAGRAM3_FLAG_OPTIONS)
		{
			isOptions = true;
			m_Options.CleanUp ();
			auto optionsLen = m_Options.FromBuffer (buf + offset, len - offset);
			if (optionsLen)
				offset += optionsLen;
			else
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

		// First contact has no cached LeaseSet. The ratchet session already
		// authenticates the sender, so deliver the hash immediately instead of
		// dropping the datagram or issuing an unbounded lookup.
		auto session = FindSession (ident);
		std::shared_ptr<const i2p::data::LeaseSet> ls;
		if (session) ls = session->GetRemoteLeaseSet ();
		if (!ls) ls = m_Owner->FindLeaseSet (ident);
		if (ls)
		{
			if (!ls->IsEncryptionKey (from->GetRemoteStaticKeyType (), from->GetRemoteStaticKey ()))
			{
				LogPrint (eLogError, "Datagram: Remote LeaseSet static key mismatch for datagram3 from ",
					ident.ToBase32 ());
				return;
			}
			if (!session)
			{
				session = ObtainSession (ident);
				if (session) session->SetVersion (eDatagramV3);
			}
			if (session) session->SetRemoteLeaseSet (ls);
			// The LeaseSet and the ratchet static key now agree; remember the
			// association so later datagrams use the authenticated identity.
			if (!from->GetDestinationPtr ()) from->SetDestination (ident);
		}

		auto r = FindHashReceiver (toPort);
		if (r)
			r (ident, fromPort, toPort, buf + offset, len - offset, isOptions ? &m_Options : nullptr);
		else
		{
			auto receiver = FindReceiver (toPort);
			if (receiver && ls)
				receiver (*ls->GetIdentity (), fromPort, toPort, buf + offset, len - offset,
					isOptions ? &m_Options : nullptr);
			else if (receiver)
				LogPrint (eLogWarning, "Datagram: datagram3 sender identity is not cached for ", ident.ToBase32 ());
			else
				LogPrint (eLogWarning, "DatagramDestination: no receiver for port ", toPort);
		}
		if (session) session->Ack ();
	}

	void DatagramDestination::SetReceiver (const Receiver & receiver, uint16_t port)
	{
		std::lock_guard<std::mutex> lock (m_ReceiversMutex);
		if (port) m_ReceiversByPorts[port] = receiver;
		if (!m_DefaultReceiver)
		{
			m_DefaultReceiver = receiver;
			m_DefaultReceiverPort = port;
		}
	}

	void DatagramDestination::ResetReceiver (uint16_t port)
	{
		std::lock_guard<std::mutex> lock (m_ReceiversMutex);
		if (port) m_ReceiversByPorts.erase (port);
		if (!port || m_DefaultReceiverPort == port)
		{
			m_DefaultReceiver = nullptr;
			m_DefaultReceiverPort = 0;
		}
	}

	void DatagramDestination::SetHashReceiver (const HashReceiver & receiver, uint16_t port)
	{
		std::lock_guard<std::mutex> lock (m_HashReceiversMutex);
		if (port) m_HashReceiversByPorts[port] = receiver;
		if (!m_DefaultHashReceiver)
		{
			m_DefaultHashReceiver = receiver;
			m_DefaultHashReceiverPort = port;
		}
	}

	void DatagramDestination::ResetHashReceiver (uint16_t port)
	{
		std::lock_guard<std::mutex> lock (m_HashReceiversMutex);
		if (port) m_HashReceiversByPorts.erase (port);
		if (!port || m_DefaultHashReceiverPort == port)
		{
			m_DefaultHashReceiver = nullptr;
			m_DefaultHashReceiverPort = 0;
		}
	}

	void DatagramDestination::SetRawReceiver (const RawReceiver & receiver, uint16_t port)
	{
		std::lock_guard<std::mutex> lock (m_RawReceiversMutex);
		if (port) m_RawReceiversByPorts[port] = receiver;
		if (!m_DefaultRawReceiver)
		{
			m_DefaultRawReceiver = receiver;
			m_DefaultRawReceiverPort = port;
		}
	}

	void DatagramDestination::ResetRawReceiver (uint16_t port)
	{
		std::lock_guard<std::mutex> lock (m_RawReceiversMutex);
		if (port) m_RawReceiversByPorts.erase (port);
		if (!port || m_DefaultRawReceiverPort == port)
		{
			m_DefaultRawReceiver = nullptr;
			m_DefaultRawReceiverPort = 0;
		}
	}

	DatagramDestination::Receiver DatagramDestination::FindReceiver (uint16_t port)
	{
		std::lock_guard<std::mutex> lock (m_ReceiversMutex);
		Receiver r = nullptr;
		auto itr = m_ReceiversByPorts.find (port);
		if (itr != m_ReceiversByPorts.end ())
			r = itr->second;
		else
		{
			r = m_DefaultReceiver;
		}
		return r;
	}

	DatagramDestination::HashReceiver DatagramDestination::FindHashReceiver (uint16_t port)
	{
		std::lock_guard<std::mutex> lock (m_HashReceiversMutex);
		HashReceiver r = nullptr;
		auto itr = m_HashReceiversByPorts.find (port);
		if (itr != m_HashReceiversByPorts.end ())
			r = itr->second;
		else
			r = m_DefaultHashReceiver;
		return r;
	}

	DatagramDestination::RawReceiver DatagramDestination::FindRawReceiver (uint16_t port)
	{
		std::lock_guard<std::mutex> lock (m_RawReceiversMutex);
		RawReceiver r = nullptr;
		auto itr = m_RawReceiversByPorts.find (port);
		if (itr != m_RawReceiversByPorts.end ())
			r = itr->second;
		else
		{
			r = m_DefaultRawReceiver;
		}
		return r;
	}

	void DatagramDestination::HandleDataMessagePayload (uint16_t fromPort, uint16_t toPort, const uint8_t * buf,
		size_t len, uint8_t protocolType, i2p::garlic::ECIESX25519AEADRatchetSession * from)
	{
		// unzip it
		uint8_t uncompressed[MAX_DATAGRAM_SIZE];
		size_t uncompressedLen = m_Inflator.Inflate (buf, len, uncompressed, MAX_DATAGRAM_SIZE);
		if (uncompressedLen)
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
		auto msg = m_I2NPMsgsPool.AcquireShared ();
		uint8_t * buf = msg->GetPayload ();
		buf += 4; // reserve for length

		if (m_Gzip && m_Deflator)
			size = m_Deflator->Deflate (payloads, buf, msg->maxLen - msg->len);
		else
			size = i2p::data::GzipNoCompression (payloads, buf, msg->maxLen - msg->len);

		if (size)
		{
			htobe32buf (msg->GetPayload (), size); // length
			htobe16buf (buf + 4, fromPort); // source port
			htobe16buf (buf + 6, toPort); // destination port
			buf[9] = protocolType; // raw or datagram protocol
			msg->len += size + 4;
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
			session->SetVersion (m_Version);
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
		, m_LastUse (0)
		, m_LastFlush (0)
		, m_RequestingLS (false)
		, m_Version (eDatagramV1)
	{
	}

	void DatagramSession::Start ()
	{
		m_LastUse = i2p::util::GetMillisecondsSinceEpoch ();
	}

	void DatagramSession::Stop () { }

	void DatagramSession::SendMsg (std::shared_ptr<I2NPMessage> msg)
	{
		// we used this session
		m_LastUse = i2p::util::GetMillisecondsSinceEpoch ();
		if (msg || m_SendQueue.empty ()) m_SendQueue.push_back (msg);
		// flush queue right away if full
		if (!msg || m_SendQueue.size () >= DATAGRAM_SEND_QUEUE_MAX_SIZE
			|| m_LastUse > m_LastFlush + DATAGRAM_MAX_FLUSH_INTERVAL)
		{
			FlushSendQueue ();
			m_LastFlush = m_LastUse;
		}
	}

	DatagramSession::Info DatagramSession::GetSessionInfo () const
	{
		if (!m_RoutingSession) return DatagramSession::Info (nullptr, nullptr, m_LastUse);

		auto routingPath = m_RoutingSession->GetSharedRoutingPath ();
		if (!routingPath) return DatagramSession::Info (nullptr, nullptr, m_LastUse);
		auto lease = routingPath->remoteLease;
		auto tunnel = routingPath->outboundTunnel;
		if (lease)
		{
			if (tunnel)
				return DatagramSession::Info (lease->tunnelGateway, tunnel->GetEndpointIdentHash (), m_LastUse);
			else
				return DatagramSession::Info (lease->tunnelGateway, nullptr, m_LastUse);
		}
		else if (tunnel)
			return DatagramSession::Info (nullptr, tunnel->GetEndpointIdentHash (), m_LastUse);
		else
			return DatagramSession::Info (nullptr, nullptr, m_LastUse);
	}

	void DatagramSession::Ack ()
	{
		m_LastUse = i2p::util::GetMillisecondsSinceEpoch ();
		auto path = GetSharedRoutingPath ();
		if (path) path->updateTime = i2p::util::GetSecondsSinceEpoch ();
		if (IsRatchets ()) SendMsg (nullptr); // send empty message in case if we don't have some data to send
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
						std::bind (&DatagramSession::HandleLeaseSetUpdated, this, std::placeholders::_1));
				}
				return nullptr;
			}
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
						if (idx < 0) idx = rand () % sz;
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
					if (idx < 0) idx = rand () % sz;
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
		m_RequestingLS = false;
		if (ls)
		{
			// only update lease set if found and newer than previous lease set
			uint64_t oldExpire = 0;
			if (m_RemoteLeaseSet) oldExpire = m_RemoteLeaseSet->GetExpirationTime ();
			if (ls && ls->GetExpirationTime () > oldExpire) m_RemoteLeaseSet = ls;
		}
		FlushSendQueue (false); // process deferred flush
	}

	void DatagramSession::RequestUpdatedLeaseSet ()
	{
		if (!m_RequestingLS)
		{
			m_RequestingLS = true;
			m_LocalDestination->RequestDestination (
				m_RemoteIdent, std::bind (&DatagramSession::HandleLeaseSetUpdated, this, std::placeholders::_1));
		}
	}

	void DatagramSession::FlushSendQueue (bool waitForLeaseSet)
	{
		if (m_SendQueue.empty ()) return;
		auto routingPath = GetSharedRoutingPath ();
		if (!routingPath && waitForLeaseSet && m_RequestingLS) return; // defer to HandleLeaseSetUpdated
		// if we don't have a routing path we will drop all queued messages
		if (routingPath && routingPath->outboundTunnel && routingPath->remoteLease)
		{
			if (m_Version == eDatagramV3)
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
			m_NumDroppedNoPath += m_SendQueue.size ();
			LogPrint (eLogWarning, "Datagram: No routing path, dropped ", m_SendQueue.size (), " messages, ",
				m_NumDroppedNoPath, " total");
		}
		m_SendQueue.clear ();
	}
}
}
