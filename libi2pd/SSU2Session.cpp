/*
* Copyright (c) 2022-2026, The PurpleI2P Project
* Copyright (c) 2026, David Uhden Collado
* SPDX-License-Identifier: BSD-3-Clause
*
* This file is part of openi2pd and licensed under BSD-3-Clause
*
* See full license text in LICENSE file at top of project tree
*/

#include <string.h>
#include <algorithm>
#include <openssl/rand.h>
#include "Log.h"
#include "Transports.h"
#include "Gzip.h"
#include "NetDb.hpp"
#include "SSU2.h"
#include "SSU2Session.h"

namespace i2p
{
namespace transport
{
	void SSU2IncompleteMessage::AttachNextFragment (const uint8_t * fragment, size_t fragmentSize)
	{
		if (msg->len + fragmentSize > msg->maxLen)
		{
			LogPrint (eLogInfo, "SSU2: I2NP message size ", msg->maxLen, " is not enough");
			auto newMsg = NewI2NPMessage (msg->len + fragmentSize);
			*newMsg = *msg;
			msg = newMsg;
		}
		if (msg->Concat (fragment, fragmentSize) < fragmentSize)
			LogPrint (eLogError, "SSU2: I2NP buffer overflow ", msg->maxLen);
		nextFragmentNum++;
	}

	bool SSU2IncompleteMessage::ConcatOutOfSequenceFragments ()
	{
		bool isLast = false;
		while (outOfSequenceFragments)
		{
			if (outOfSequenceFragments->fragmentNum == nextFragmentNum)
			{
				AttachNextFragment (outOfSequenceFragments->buf, outOfSequenceFragments->len);
				isLast = outOfSequenceFragments->isLast;
				if (isLast)
					outOfSequenceFragments = nullptr;
				else
					outOfSequenceFragments = outOfSequenceFragments->next;
			}
			else
				break;
		}
		return isLast;
	}

	void SSU2IncompleteMessage::AddOutOfSequenceFragment (std::shared_ptr<SSU2IncompleteMessage::Fragment> fragment)
	{
		if (!fragment || !fragment->fragmentNum) return; // fragment 0 not allowed
		if (fragment->fragmentNum < nextFragmentNum) return; // already processed
		if (!outOfSequenceFragments)
			outOfSequenceFragments = fragment;
		else
		{
			auto frag = outOfSequenceFragments;
			std::shared_ptr<Fragment> prev;
			do
			{
				if (fragment->fragmentNum < frag->fragmentNum) break; // found
				if (fragment->fragmentNum == frag->fragmentNum) return; // duplicate
				prev = frag;
				frag = frag->next;
			} while (frag);
			fragment->next = frag;
			if (prev)
				prev->next = fragment;
			else
				outOfSequenceFragments = fragment;
		}
		lastFragmentInsertTime = i2p::util::GetSecondsSinceEpoch ();
	}

	SSU2Session::SSU2Session (SSU2Server & server, std::shared_ptr<const i2p::data::RouterInfo> in_RemoteRouter,
		std::shared_ptr<const i2p::data::RouterInfo::Address> addr)
		: TransportSession (in_RemoteRouter, SSU2_CONNECT_TIMEOUT)
		, m_Server (server)
		, m_Address (addr)
		, m_RemoteTransports (0)
		, m_RemotePeerTestTransports (0)
		, m_RemoteVersion (0)
		, m_DestConnID (0)
		, m_SourceConnID (0)
		, m_State (eSSU2SessionStateUnknown)
		, m_SendPacketNum (0)
		, m_ReceivePacketNum (0)
		, m_LastDatetimeSentPacketNum (0)
		, m_IsDataReceived (false)
		, m_IsInvalidMessage (false)
		, m_RTT (SSU2_UNKNOWN_RTT)
		, m_MsgLocalExpirationTimeout (I2NP_MESSAGE_LOCAL_EXPIRATION_TIMEOUT_MAX)
		, m_MsgLocalSemiExpirationTimeout (I2NP_MESSAGE_LOCAL_EXPIRATION_TIMEOUT_MAX / 2)
		, m_WindowSize (SSU2_MIN_WINDOW_SIZE)
		, m_RTO (SSU2_INITIAL_RTO)
		, m_RelayTag (0)
		, m_ConnectTimer (server.GetService ())
		, m_TerminationReason (eSSU2TerminationReasonNormalClose)
		, m_MaxPayloadSize (SSU2_MAX_PACKET_SIZE - IPV6_HEADER_SIZE - UDP_HEADER_SIZE - 32)
		, // max size
		m_LastResendTime (0)
		, m_LastResendAttemptTime (0)
		, m_NextRouterInfoResendTime (0)
		, m_NumRanges (0)
	{
		if (in_RemoteRouter && m_Address)
		{
			// outgoing
			m_RemoteEndpoint = boost::asio::ip::udp::endpoint (m_Address->host, m_Address->port);
			m_RemoteTransports = in_RemoteRouter->GetCompatibleTransports (false);
			m_RemoteVersion = in_RemoteRouter->GetVersion ();
			if (in_RemoteRouter->IsSSU2PeerTesting (true)) m_RemotePeerTestTransports |= i2p::data::RouterInfo::eSSU2V4;
			if (in_RemoteRouter->IsSSU2PeerTesting (false))
				m_RemotePeerTestTransports |= i2p::data::RouterInfo::eSSU2V6;
			RAND_bytes ((uint8_t *)&m_DestConnID, 8);
			RAND_bytes ((uint8_t *)&m_SourceConnID, 8);
			AdjustMaxPayloadSize ();
		}
	}

	SSU2Session::~SSU2Session () { }

	void SSU2Session::Connect ()
	{
		if (m_State == eSSU2SessionStateUnknown || m_State == eSSU2SessionStateTokenReceived)
		{
			LogPrint (eLogDebug, "SSU2: Connecting to ", GetRemoteEndpoint (), " (",
				i2p::data::GetIdentHashAbbreviation (GetRemoteIdentity ()->GetIdentHash ()), ")");
			ScheduleConnectTimer ();
			auto token = m_Server.FindOutgoingToken (m_RemoteEndpoint);
			if (token)
			{
				if (!SendSessionRequest (token)) Terminate ();
			}
			else
			{
				m_State = eSSU2SessionStateUnknown;
				SendTokenRequest ();
			}
		}
	}

	void SSU2Session::ScheduleConnectTimer ()
	{
		m_ConnectTimer.cancel ();
		m_ConnectTimer.expires_after (std::chrono::seconds (SSU2_CONNECT_TIMEOUT));
		m_ConnectTimer.async_wait (
			std::bind (&SSU2Session::HandleConnectTimer, shared_from_this (), std::placeholders::_1));
	}

	void SSU2Session::HandleConnectTimer (const boost::system::error_code & ecode)
	{
		if (!ecode && m_State != eSSU2SessionStateTerminated)
		{
			// timeout expired
			if (m_State == eSSU2SessionStateIntroduced) // WaitForIntroducer
				LogPrint (eLogWarning, "SSU2: Session was not introduced after ", SSU2_CONNECT_TIMEOUT, " seconds");
			else
				LogPrint (eLogWarning, "SSU2: Session with ", m_RemoteEndpoint, " was not established after ",
					SSU2_CONNECT_TIMEOUT, " seconds");
			Terminate ();
		}
	}

	bool SSU2Session::Introduce (std::shared_ptr<SSU2Session> session, uint32_t relayTag)
	{
		// we are Alice
		if (!session || !relayTag) return false;
		// find local address to introduce
		auto localAddress = session->FindLocalAddress ();
		if (!localAddress || localAddress->host.is_unspecified () || !localAddress->port)
		{
			// can't introduce invalid endpoint
			LogPrint (eLogWarning, "SSU2: Can't find local address to introduce");
			return false;
		}
		// create nonce
		uint32_t nonce;
		RAND_bytes ((uint8_t *)&nonce, 4);
		if (m_RelaySessions.size () >= SSU2_MAX_RELAY_SESSIONS)
		{
			LogPrint (eLogWarning, "SSU2: Too many pending relay introductions ", m_RelaySessions.size ());
			return false;
		}
		auto ts = i2p::util::GetMillisecondsSinceEpoch ();
		// payload
		auto packet = m_Server.GetSentPacketsPool ().AcquireShared ();
		uint8_t * payload = packet->payload;
		payload[0] = eSSU2BlkRelayRequest;
		payload[3] = 0; // flag
		htobe32buf (payload + 4, nonce);
		htobe32buf (payload + 8, relayTag);
		htobe32buf (payload + 12, ts / 1000);
		payload[16] = 2; // relay protocol version is fixed to 2
		size_t asz = CreateEndpoint (payload + 18, m_MaxPayloadSize - 18,
			boost::asio::ip::udp::endpoint (localAddress->host, localAddress->port));
		if (!asz) return false;
		payload[17] = asz;
		packet->payloadSize = asz + 18;
		SignedData<128> s;
		s.Insert ((const uint8_t *)"RelayRequestData", 16); // prologue
		s.Insert (GetRemoteIdentity ()->GetIdentHash (), 32); // bhash
		s.Insert (session->GetRemoteIdentity ()->GetIdentHash (), 32); // chash
		s.Insert (payload + 4, 14 + asz); // nonce, relay tag, timestamp, ver, asz and Alice's endpoint
		s.Sign (i2p::context.GetPrivateKeys (), payload + packet->payloadSize);
		packet->payloadSize += i2p::context.GetIdentity ()->GetSignatureLen ();
		htobe16buf (payload + 1, packet->payloadSize - 3); // size
		packet->payloadSize
			+= CreatePaddingBlock (payload + packet->payloadSize, m_MaxPayloadSize - packet->payloadSize);
		// send
		if (!m_RelaySessions.emplace (nonce, std::make_pair (session, ts / 1000)).second)
		{
			LogPrint (eLogWarning, "SSU2: Relay nonce ", nonce, " already exists");
			return false;
		}
		session->m_SourceConnID = htobe64 (((uint64_t)nonce << 32) | nonce);
		session->m_DestConnID = ~session->m_SourceConnID;
		m_Server.AddSession (session);
		int32_t packetNum = SendData (packet->payload, packet->payloadSize);
		packet->sendTime = ts;
		m_SentPackets.emplace (packetNum, packet);

		return true;
	}

	void SSU2Session::WaitForIntroduction ()
	{
		m_State = eSSU2SessionStateIntroduced;
		ScheduleConnectTimer ();
	}

	void SSU2Session::ConnectAfterIntroduction ()
	{
		if (m_State == eSSU2SessionStateIntroduced)
		{
			// we are Alice
			//  keep ConnIDs used for introduction, because Charlie waits for SessionRequest from us
			m_State = eSSU2SessionStateTokenReceived;
			// move session to pending outgoing
			if (m_Server.AddPendingOutgoingSession (shared_from_this ()))
			{
				m_Server.RemoveSession (GetConnID ());
				// update endpoint in profile because we know it now
				auto identity = GetRemoteIdentity ();
				if (identity)
				{
					auto profile = i2p::data::GetRouterProfile (identity->GetIdentHash ());
					if (profile) profile->SetLastEndpoint (m_RemoteEndpoint);
				}
				// connect
				LogPrint (eLogDebug, "SSU2: Connecting after introduction to ", GetIdentHashBase64 ());
				Connect ();
			}
			else
			{
				LogPrint (eLogError, "SSU2: Session ", GetConnID (), " is already pending");
				m_Server.RequestRemoveSession (GetConnID ());
			}
		}
	}

	void SSU2Session::SendPeerTest ()
	{
		// we are Alice
		uint32_t nonce;
		RAND_bytes ((uint8_t *)&nonce, 4);
		auto ts = i2p::util::GetMillisecondsSinceEpoch ();
		// session for message 5
		auto session = std::make_shared<SSU2PeerTestSession> (m_Server, htobe64 (((uint64_t)nonce << 32) | nonce), 0);
		m_Server.AddRequestedPeerTest (nonce, session, ts / 1000);
		m_Server.AddSession (session);
		// peer test block
		auto packet = m_Server.GetSentPacketsPool ().AcquireShared ();
		packet->payloadSize = CreatePeerTestBlock (packet->payload, m_MaxPayloadSize, nonce);
		if (packet->payloadSize > 0)
		{
			packet->payloadSize
				+= CreatePaddingBlock (packet->payload + packet->payloadSize, m_MaxPayloadSize - packet->payloadSize);
			uint32_t packetNum = SendData (packet->payload, packet->payloadSize, SSU2_FLAG_IMMEDIATE_ACK_REQUESTED);
			packet->sendTime = ts;
			m_SentPackets.emplace (packetNum, packet);
			LogPrint (eLogDebug, "SSU2: PeerTest msg=1 sent to ",
				i2p::data::GetIdentHashAbbreviation (GetRemoteIdentity ()->GetIdentHash ()));
		}
	}

	void SSU2Session::SendKeepAlive ()
	{
		if (IsEstablished ())
		{
			uint8_t payload[20];
			size_t payloadSize = CreatePaddingBlock (payload, 20, 8);
			SendData (payload, payloadSize, SSU2_FLAG_IMMEDIATE_ACK_REQUESTED);
		}
	}

	void SSU2Session::Terminate ()
	{
		auto state = m_State.exchange (eSSU2SessionStateTerminated);
		if (state != eSSU2SessionStateTerminated)
		{
			m_ConnectTimer.cancel ();
			m_OnEstablished = nullptr;
			if (m_RelayTag) m_Server.RemoveRelay (m_RelayTag);
			m_Server.AddConnectedRecently (m_RemoteEndpoint, GetLastActivityTimestamp ());
			m_SentHandshakePacket.reset (nullptr);
			m_SessionConfirmedFragment.reset (nullptr);
			m_PathChallenge.reset (nullptr);
#if OPENSSL_PQ
			m_PQKeys.reset (nullptr);
#endif
			{
				std::lock_guard<std::mutex> l (m_IntermediateQueueMutex);
				if (!m_IntermediateQueue.empty ()) m_SendQueue.splice (m_SendQueue.end (), m_IntermediateQueue);
			}
			for (auto & it : m_SendQueue) it->Drop ();
			m_SendQueue.clear ();
			SetSendQueueSize (0);
			m_SentPackets.clear ();
			m_IncompleteMessages.clear ();
			m_RelaySessions.clear ();
			m_RecentRelayRequests.clear ();
			m_ReceivedI2NPMsgIDs.clear ();
			m_Server.RemoveSession (m_SourceConnID);
			transports.PeerDisconnected (shared_from_this ());
			auto remoteIdentity = GetRemoteIdentity ();
			if (remoteIdentity)
				LogPrint (eLogDebug, "SSU2: Session with ", GetRemoteEndpoint (), " (",
					i2p::data::GetIdentHashAbbreviation (remoteIdentity->GetIdentHash ()), ") terminated");
			else
				LogPrint (eLogDebug, "SSU2: Session with ", GetRemoteEndpoint (), " terminated");
		}
	}

	void SSU2Session::RequestTermination (SSU2TerminationReason reason)
	{
		if (m_State == eSSU2SessionStateEstablished || m_State == eSSU2SessionStateClosing)
		{
			m_TerminationReason = reason;
			SendTermination ();
			m_State = eSSU2SessionStateClosing;
		}
		else
			Done ();
	}

	void SSU2Session::Established ()
	{
		m_State = eSSU2SessionStateEstablished;
		m_EphemeralKeys = nullptr;
		m_NoiseState.reset (nullptr);
		m_SessionConfirmedFragment.reset (nullptr);
		m_SessionConfirmedFragments.clear ();
		m_SessionConfirmedNumFragments = 0;
		m_SentHandshakePacket.reset (nullptr);
#if OPENSSL_PQ
		m_PQKeys.reset (nullptr);
#endif
		m_ConnectTimer.cancel ();
		SetTerminationTimeout (SSU2_TERMINATION_TIMEOUT);
		m_NextRouterInfoResendTime = i2p::util::GetMillisecondsSinceEpoch () + SSU2_ROUTERINFO_RESEND_INTERVAL
			+ m_Server.GetRng () () % SSU2_ROUTERINFO_RESEND_INTERVAL_VARIANCE;
		SendQueue ();
		transports.PeerConnected (shared_from_this ());

		LogPrint (eLogDebug, "SSU2: Session with ", GetRemoteEndpoint (), " (",
			i2p::data::GetIdentHashAbbreviation (GetRemoteIdentity ()->GetIdentHash ()), ") established");
		if (m_OnEstablished)
		{
			m_OnEstablished ();
			m_OnEstablished = nullptr;
		}
	}

	void SSU2Session::Done ()
	{
		boost::asio::post (m_Server.GetService (), std::bind (&SSU2Session::Terminate, shared_from_this ()));
	}

	void SSU2Session::SendLocalRouterInfo (bool update)
	{
		if (update || !IsOutgoing ())
		{
			auto s = shared_from_this ();
			boost::asio::post (m_Server.GetService (),
				[s] ()
				{
					if (!s->IsEstablished ()) return;
					uint8_t payload[SSU2_MAX_PACKET_SIZE];
					size_t payloadSize = s->CreateRouterInfoBlock (
						payload, s->m_MaxPayloadSize - 32, i2p::context.CopyRouterInfoBuffer ());
					if (payloadSize)
					{
						if (payloadSize < s->m_MaxPayloadSize)
							payloadSize
								+= s->CreatePaddingBlock (payload + payloadSize, s->m_MaxPayloadSize - payloadSize);
						s->SendData (payload, payloadSize);
					}
					else
						s->SendFragmentedMessage (CreateDatabaseStoreMsg ());
				});
		}
	}

	void SSU2Session::SendI2NPMessages (std::list<std::shared_ptr<I2NPMessage>> & msgs)
	{
		if (m_State == eSSU2SessionStateTerminated || msgs.empty ())
		{
			msgs.clear ();
			return;
		}
		bool empty = false;
		{
			std::lock_guard<std::mutex> l (m_IntermediateQueueMutex);
			empty = m_IntermediateQueue.empty ();
			m_IntermediateQueue.splice (m_IntermediateQueue.end (), msgs);
		}
		if (empty)
			boost::asio::post (m_Server.GetService (), std::bind (&SSU2Session::PostI2NPMessages, shared_from_this ()));
	}

	void SSU2Session::PostI2NPMessages ()
	{
		if (m_State == eSSU2SessionStateTerminated) return;
		std::list<std::shared_ptr<I2NPMessage>> msgs;
		{
			std::lock_guard<std::mutex> l (m_IntermediateQueueMutex);
			m_IntermediateQueue.swap (msgs);
		}
		uint64_t mts = i2p::util::GetMonotonicMicroseconds ();
		bool isSemiFull = false;
		if (m_SendQueue.size ())
		{
			int64_t queueLag = (int64_t)mts - (int64_t)m_SendQueue.front ()->GetEnqueueTime ();
			isSemiFull = queueLag > m_MsgLocalSemiExpirationTimeout;
			if (isSemiFull)
			{
				LogPrint (eLogWarning, "SSU2: Outgoing messages queue to ",
					i2p::data::GetIdentHashAbbreviation (GetRemoteIdentity ()->GetIdentHash ()),
					" is semi-full (size = ", m_SendQueue.size (), ", lag = ", queueLag / 1000, ", rtt = ", (int)m_RTT,
					")");
			}
		}
		if (isSemiFull)
		{
			for (auto it : msgs)
			{
				if (it->onDrop)
					it->Drop (); // drop earlier because we can handle it
				else
				{
					it->SetEnqueueTime (mts);
					m_SendQueue.push_back (std::move (it));
				}
			}
		}
		else
		{
			for (auto & it : msgs) it->SetEnqueueTime (mts);
			m_SendQueue.splice (m_SendQueue.end (), msgs);
		}
		if (IsEstablished ())
		{
			SendQueue ();
			if (m_SendQueue.size () > 0) // windows is full
				Resend (i2p::util::GetMillisecondsSinceEpoch ());
		}
		SetSendQueueSize (m_SendQueue.size ());
	}

	void SSU2Session::MoveSendQueue (std::shared_ptr<SSU2Session> other)
	{
		if (!other || m_SendQueue.empty ()) return;
		std::list<std::shared_ptr<I2NPMessage>> msgs;
		auto ts = i2p::util::GetMillisecondsSinceEpoch ();
		for (auto it : m_SendQueue)
			if (!it->IsExpired (ts))
				msgs.push_back (it);
			else
				it->Drop ();
		m_SendQueue.clear ();
		if (!msgs.empty ()) other->SendI2NPMessages (msgs);
	}

	bool SSU2Session::SendQueue ()
	{
		if (!m_SendQueue.empty () && m_SentPackets.size () <= m_WindowSize && IsEstablished ())
		{
			auto ts = i2p::util::GetMillisecondsSinceEpoch ();
			uint64_t mts = i2p::util::GetMonotonicMicroseconds ();
			auto packet = m_Server.GetSentPacketsPool ().AcquireShared ();
			size_t ackBlockSize = CreateAckBlock (packet->payload, m_MaxPayloadSize);
			bool ackBlockSent = false;
			packet->payloadSize += ackBlockSize;
			while (!m_SendQueue.empty () && m_SentPackets.size () <= m_WindowSize)
			{
				auto msg = m_SendQueue.front ();
				if (!msg || msg->IsExpired (ts)
					|| msg->GetEnqueueTime () + I2NP_MESSAGE_LOCAL_EXPIRATION_TIMEOUT_TRANSIT < mts)
				{
					// drop null or expired message
					if (msg) msg->Drop ();
					m_SendQueue.pop_front ();
					continue;
				}
				size_t len = msg->GetNTCP2Length () + 3;
				if (len > m_MaxPayloadSize) // message too long
				{
					m_SendQueue.pop_front ();
					if (SendFragmentedMessage (msg)) ackBlockSent = true;
				}
				else if (packet->payloadSize + len <= m_MaxPayloadSize)
				{
					m_SendQueue.pop_front ();
					packet->payloadSize += CreateI2NPBlock (
						packet->payload + packet->payloadSize, m_MaxPayloadSize - packet->payloadSize, std::move (msg));
				}
				else
				{
					// create new packet and copy ack block
					auto newPacket = m_Server.GetSentPacketsPool ().AcquireShared ();
					memcpy (newPacket->payload, packet->payload, ackBlockSize);
					newPacket->payloadSize = ackBlockSize;
					// complete current packet
					if (packet->payloadSize > ackBlockSize) // more than just ack block
					{
						ackBlockSent = true;
						// try to add padding
						if (packet->payloadSize + 16 < m_MaxPayloadSize)
							packet->payloadSize += CreatePaddingBlock (
								packet->payload + packet->payloadSize, m_MaxPayloadSize - packet->payloadSize);
					}
					else
					{
						// Do not expand an ACK block without writing valid ranges: the
						// old branch exposed uninitialized payload bytes as acknowledgements.
						// The message is more important; drop the ACK and let a later packet
						// carry it.
						packet->payloadSize = 0;
						// msg fits single packet
						m_SendQueue.pop_front ();
						packet->payloadSize += CreateI2NPBlock (packet->payload + packet->payloadSize,
							m_MaxPayloadSize - packet->payloadSize, std::move (msg));
					}
					// send right a way
					uint32_t packetNum = SendData (packet->payload, packet->payloadSize);
					packet->sendTime = ts;
					m_SentPackets.emplace (packetNum, packet);
					packet = newPacket; // just ack block
				}
			};
			if (packet->payloadSize > ackBlockSize)
			{
				// last
				ackBlockSent = true;
				if (packet->payloadSize + 16 < m_MaxPayloadSize)
					packet->payloadSize += CreatePaddingBlock (
						packet->payload + packet->payloadSize, m_MaxPayloadSize - packet->payloadSize);
				uint32_t packetNum = SendData (packet->payload, packet->payloadSize, SSU2_FLAG_IMMEDIATE_ACK_REQUESTED);
				packet->sendTime = ts;
				m_SentPackets.emplace (packetNum, packet);
			}
			return ackBlockSent;
		}
		return false;
	}

	bool SSU2Session::SendFragmentedMessage (std::shared_ptr<I2NPMessage> msg)
	{
		if (!msg) return false;
		auto ts = i2p::util::GetMillisecondsSinceEpoch ();
		if (msg->GetNTCP2Length () + 3 <= m_MaxPayloadSize)
		{
			// non fragmented, send regular I2NP block
			auto packet = m_Server.GetSentPacketsPool ().AcquireShared ();
			packet->payloadSize = CreateI2NPBlock (packet->payload, m_MaxPayloadSize, std::move (msg));
			uint32_t packetNum = SendData (packet->payload, packet->payloadSize);
			packet->sendTime = ts;
			m_SentPackets.emplace (packetNum, packet);
			return true;
		}
		size_t lastFragmentSize = (msg->GetNTCP2Length () + 3 - m_MaxPayloadSize) % (m_MaxPayloadSize - 8);
		size_t extraSize = m_MaxPayloadSize - lastFragmentSize;
		bool ackBlockSent = false;
		uint32_t msgID;
		memcpy (&msgID, msg->GetHeader () + I2NP_HEADER_MSGID_OFFSET, 4);
		auto packet = m_Server.GetSentPacketsPool ().AcquireShared ();
		if (extraSize >= 8)
		{
			packet->payloadSize = CreateAckBlock (packet->payload, extraSize);
			ackBlockSent = true;
			if (packet->payloadSize + 12 < m_MaxPayloadSize)
			{
				uint32_t packetNum = SendData (packet->payload, packet->payloadSize);
				packet->sendTime = ts;
				m_SentPackets.emplace (packetNum, packet);
				packet = m_Server.GetSentPacketsPool ().AcquireShared ();
			}
			else
				extraSize -= packet->payloadSize;
		}
		size_t offset = extraSize > 0 ? (m_Server.GetRng () () % extraSize) : 0;
		if (offset + packet->payloadSize >= m_MaxPayloadSize) offset = 0;
		auto size = CreateFirstFragmentBlock (
			packet->payload + packet->payloadSize, m_MaxPayloadSize - offset - packet->payloadSize, msg);
		if (!size) return false;
		extraSize -= offset;
		packet->payloadSize += size;
		uint32_t firstPacketNum = SendData (packet->payload, packet->payloadSize);
		packet->sendTime = ts;
		m_SentPackets.emplace (firstPacketNum, packet);
		uint8_t fragmentNum = 0;
		while (msg->offset < msg->len)
		{
			offset = extraSize > 0 ? (m_Server.GetRng () () % extraSize) : 0;
			packet = m_Server.GetSentPacketsPool ().AcquireShared ();
			packet->payloadSize
				= CreateFollowOnFragmentBlock (packet->payload, m_MaxPayloadSize - offset, msg, fragmentNum, msgID);
			extraSize -= offset;
			uint8_t flags = 0;
			if (msg->offset >= msg->len && packet->payloadSize + 16 < m_MaxPayloadSize) // last fragment
			{
				packet->payloadSize += CreatePaddingBlock (
					packet->payload + packet->payloadSize, m_MaxPayloadSize - packet->payloadSize);
				if (fragmentNum > 2) // 3 or more fragments
					flags |= SSU2_FLAG_IMMEDIATE_ACK_REQUESTED;
			}
			uint32_t followonPacketNum = SendData (packet->payload, packet->payloadSize, flags);
			packet->sendTime = ts;
			m_SentPackets.emplace (followonPacketNum, packet);
		}
		return ackBlockSent;
	}

	size_t SSU2Session::Resend (uint64_t ts)
	{
		if (ts + SSU2_RESEND_ATTEMPT_MIN_INTERVAL < m_LastResendAttemptTime) return 0;
		m_LastResendAttemptTime = ts;
		// resend handshake packet. Java backs off 1.25, 2.5, 5, ... seconds
		// (OutboundEstablishState.java:117,123) while the overall budget is
		// MAX_OB_ESTABLISH_TIME (EstablishmentManager.java:153).
		if (m_SentHandshakePacket)
		{
			int shift = std::min (m_SentHandshakePacket->numResends, 3);
			uint64_t interval = ((uint64_t)SSU2_HANDSHAKE_RESEND_INTERVAL) << shift;
			if (ts >= m_SentHandshakePacket->sendTime + interval)
			{
				LogPrint (eLogDebug, "SSU2: Resending ", (int)m_State);
				ResendHandshakePacket ();
				m_SentHandshakePacket->sendTime = ts;
				m_SentHandshakePacket->numResends++;
				return 0;
			}
		}
		// schedule resend local RouterInfo
		if (m_NextRouterInfoResendTime && ts > m_NextRouterInfoResendTime)
		{
			SendLocalRouterInfo (true);
			m_NextRouterInfoResendTime = ts + SSU2_ROUTERINFO_RESEND_INTERVAL
				+ m_Server.GetRng () () % SSU2_ROUTERINFO_RESEND_INTERVAL_VARIANCE;
		}
		// resend data packets
		if (m_SentPackets.empty ()) return 0;
		std::map<uint32_t, std::shared_ptr<SSU2SentPacket>> resentPackets;
		for (auto it = m_SentPackets.begin (); it != m_SentPackets.end ();)
			if (ts >= it->second->sendTime + (it->second->numResends + 1) * m_RTO)
			{
				if (it->second->numResends > SSU2_MAX_NUM_RESENDS)
				{
					LogPrint (eLogInfo, "SSU2: Packet was not Acked after ", it->second->numResends,
						" attempts. Terminate session");
					m_SentPackets.clear ();
					m_SendQueue.clear ();
					SetSendQueueSize (0);
					RequestTermination (eSSU2TerminationReasonTimeout);
					return resentPackets.size ();
				}
				else
				{
					uint32_t packetNum = SendData (it->second->payload, it->second->payloadSize,
						it->second->numResends > 1 ? SSU2_FLAG_IMMEDIATE_ACK_REQUESTED : 0);
					it->second->numResends++;
					it->second->sendTime = ts;
					resentPackets.emplace (packetNum, it->second);
					it = m_SentPackets.erase (it);
				}
			}
			else
				it++;
		if (!resentPackets.empty ())
		{
			m_LastResendTime = ts;
			m_SentPackets.merge (resentPackets);
			m_WindowSize >>= 1; // /2
			if (m_WindowSize < SSU2_MIN_WINDOW_SIZE) m_WindowSize = SSU2_MIN_WINDOW_SIZE;
			return resentPackets.size ();
		}
		return 0;
	}

	void SSU2Session::ResendHandshakePacket ()
	{
		if (m_SentHandshakePacket)
		{
			m_Server.Send (m_SentHandshakePacket->header.buf, 16, m_SentHandshakePacket->headerX, 48,
				m_SentHandshakePacket->payload, m_SentHandshakePacket->payloadSize, m_RemoteEndpoint);
			if (m_SessionConfirmedFragment && m_State == eSSU2SessionStateSessionConfirmedSent)
				// resend second fragment of SessionConfirmed
				m_Server.Send (m_SessionConfirmedFragment->header.buf, 16, m_SessionConfirmedFragment->payload,
					m_SessionConfirmedFragment->payloadSize, m_RemoteEndpoint);
		}
	}

	bool SSU2Session::ProcessFirstIncomingMessage (uint64_t connID, uint8_t * buf, size_t len)
	{
		// we are Bob
		m_SourceConnID = connID;
		Header header;
		header.h.connID = connID;
		memcpy (header.buf + 8, buf + 8, 8);
		header.ll[1] ^= CreateHeaderMask (i2p::context.GetSSU2IntroKey (), buf + (len - 12));
		switch (header.h.type)
		{
		case eSSU2SessionRequest: return ProcessSessionRequest (header, buf, len); break;
		case eSSU2TokenRequest: return ProcessTokenRequest (header, buf, len); break;
		case eSSU2PeerTest:
		{
			// TODO: remove later
			if (len < 32)
			{
				LogPrint (eLogWarning, "SSU2: PeerTest message too short ", len);
				break;
			}
			const uint8_t nonce[12] = { 0 };
			uint64_t headerX[2];
			m_Server.ChaCha20 (buf + 16, 16, i2p::context.GetSSU2IntroKey (), nonce, (uint8_t *)headerX);
			LogPrint (
				eLogWarning, "SSU2: Unexpected PeerTest message SourceConnID=", connID, " DestConnID=", headerX[0]);
			break;
		}
		case eSSU2HolePunch: LogPrint (eLogDebug, "SSU2: Late HolePunch for ", connID); break;
		default:
		{
			LogPrint (eLogWarning, "SSU2: Unexpected message type ", (int)header.h.type, " from ", m_RemoteEndpoint,
				" of ", len, " bytes");
			return false;
		}
		}
		return true;
	}

	bool SSU2Session::SendSessionRequest (uint64_t token)
	{
		// we are Alice
#if OPENSSL_PQ
		if (m_Server.GetVersion () > 2 && !m_IsVersionFallback) // we support post quantum in config
			SetVersion (m_Address->v);
		else if (m_IsVersionFallback)
			m_Version = 2;
#endif
		m_EphemeralKeys = i2p::transport::transports.GetNextX25519KeysPair ();
		m_SentHandshakePacket.reset (new HandshakePacket);
		auto ts = i2p::util::GetMillisecondsSinceEpoch ();
		m_SentHandshakePacket->sendTime = ts;

		Header & header = m_SentHandshakePacket->header;
		uint8_t *headerX = m_SentHandshakePacket->headerX, *payload = m_SentHandshakePacket->payload;
		// fill packet
		header.h.connID = m_DestConnID; // dest id
		RAND_bytes (header.buf + 8, 4); // random packet num
		header.h.type = eSSU2SessionRequest;
		header.h.flags[0] = m_Version; // ver
		header.h.flags[1] = (uint8_t)i2p::context.GetNetID (); // netID
		header.h.flags[2] = 0; // flag
		memcpy (headerX, &m_SourceConnID, 8); // source id
		memcpy (headerX + 8, &token, 8); // token
		memcpy (headerX + 16, m_EphemeralKeys->GetPublicKey (), 32); // X
		// payload
		size_t payloadSize = 0, offset = 0;
#if OPENSSL_PQ
		if (m_Version > 2)
		{
			i2p::data::CryptoKeyType cryptoType = (i2p::data::CryptoKeyType)(m_Version + 2);
			m_PQKeys = i2p::crypto::CreateMLKEMKeys (cryptoType);
			if (!m_PQKeys || !m_PQKeys->GenerateKeys ()) return false;
			offset = m_PQKeys->GetKeyLen () + 16;
			payloadSize += offset;
		}
#endif
		payload[payloadSize] = eSSU2BlkDateTime;
		htobe16buf (payload + payloadSize + 1, 4);
		htobe32buf (payload + payloadSize + 3, (ts + 500) / 1000);
		payloadSize += 7;
		if (GetRouterStatus () == eRouterStatusFirewalled && m_Address->IsIntroducer ())
		{
			if (!m_Server.IsMaxNumIntroducers (m_RemoteEndpoint.address ().is_v4 ())
				|| m_Server.GetRng () () & 0x01) // request tag with probability 1/2 if we have enough introducers
			{
				// relay tag request
				payload[payloadSize] = eSSU2BlkRelayTagRequest;
				memset (payload + payloadSize + 1, 0, 2); // size = 0
				payloadSize += 3;
			}
		}
		if (payloadSize <= m_MaxPayloadSize - 48)
			payloadSize += CreatePaddingBlock (
				payload + payloadSize, std::min (m_MaxPayloadSize - payloadSize - 48, (size_t)32));
		else
		{
			LogPrint (eLogError, "SSU2: SessionRequest max payload size is too small ", m_MaxPayloadSize);
			return false;
		}
		// create and init noise state
		if (!m_NoiseState) m_NoiseState.reset (new i2p::crypto::NoiseSymmetricState);
#if OPENSSL_PQ
		if (m_Version > 2)
		{
			InitNoiseXKStateMLKEM1 (*m_NoiseState, (i2p::data::CryptoKeyType)(m_Version + 2), m_Address->s);
			m_NoiseState->MixHash (GetRemoteIdentity ()->GetIdentHash (), 32); // h = SHA256(h || bhash)
		}
		else
#endif
			InitNoiseXKState1 (*m_NoiseState, m_Address->s);
		// KDF for session request
		m_NoiseState->MixHash ({ { header.buf, 16 }, { headerX, 16 } }); // h = SHA256(h || header)
		m_NoiseState->MixHash (m_EphemeralKeys->GetPublicKey (), 32); // h = SHA256(h || aepk)
		uint8_t sharedSecret[32];
		m_EphemeralKeys->Agree (m_Address->s, sharedSecret);
		m_NoiseState->MixKey (sharedSecret);
		// encrypt
#if OPENSSL_PQ
		if (m_PQKeys)
		{
			size_t keyLen = m_PQKeys->GetKeyLen ();
			std::vector<uint8_t> encapsKey (keyLen);
			if (!m_PQKeys || !m_PQKeys->GetPublicKey (encapsKey.data ())) return false;
			if (!m_NoiseState->Encrypt (encapsKey.data (), payload, keyLen))
			{
				LogPrint (eLogWarning, "SSU2: SessionRequest ML-KEM encap_key frame AEAD encryption failed ");
				return false;
			}
			m_NoiseState->MixHash (payload, keyLen + 16); // h = SHA256(h || ciphertext)
		}
#endif
		if (!m_NoiseState->Encrypt (payload + offset, payload + offset, payloadSize - offset))
		{
			LogPrint (eLogWarning, "SSU2: SessionRequest payload encryption failed ");
			return false;
		}
		payloadSize += 16;
		header.ll[0] ^= CreateHeaderMask (m_Address->i, payload + (payloadSize - 24));
		header.ll[1] ^= CreateHeaderMask (m_Address->i, payload + (payloadSize - 12));
		const uint8_t nonce[12] = { 0 }; // always 0
		m_Server.ChaCha20 (headerX, 48, m_Address->i, nonce, headerX);
		m_NoiseState->MixHash (payload + offset,
			payloadSize - offset); // h = SHA256(h || encrypted payload from Session Request) for SessionCreated
		m_SentHandshakePacket->payloadSize = payloadSize;
		// send
		if (m_State == eSSU2SessionStateTokenReceived || m_Server.AddPendingOutgoingSession (shared_from_this ()))
		{
			m_State = eSSU2SessionStateSessionRequestSent;
			m_HandshakeInterval = ts;
			m_Server.Send (header.buf, 16, headerX, 48, payload, payloadSize, m_RemoteEndpoint);
		}
		else
		{
			LogPrint (eLogWarning, "SSU2: SessionRequest request to ", m_RemoteEndpoint, " already pending");
			return false;
		}
		return true;
	}

	bool SSU2Session::ProcessSessionRequest (Header & header, uint8_t * buf, size_t len)
	{
		// we are Bob
		if (len < 90)
		{
			LogPrint (eLogWarning, "SSU2: SessionRequest message too short ", len);
			return false;
		}
		if (header.h.flags[1] && header.h.flags[1] != i2p::context.GetNetID ())
		{
			LogPrint (eLogWarning, "SSU2: SessionRequest has wrong network ID ", (int)header.h.flags[1]);
			return false;
		}
#if OPENSSL_PQ
		if (header.h.flags[0] >= 2 && header.h.flags[0] <= 4) // ver
		{
			if (m_Server.GetVersion () > 2)
			{
				if (SetVersion (header.h.flags[0]))
				{
					if (m_Version > 2)
					{
						auto keyLen = i2p::crypto::GetMLKEMPublicKeyLen ((i2p::data::CryptoKeyType)(m_Version + 2));
						if (len < keyLen + 16 + 90)
						{
							LogPrint (
								eLogWarning, "SSU2: SessionRequest version ", m_Version, " message too short ", len);
							return false;
						}
					}
				}
				else
				{
					m_TerminationReason = eSSU2TerminationReasonIncompatibleVersion;
					SendRetry ();
					return true;
				}
			}
		}
		else
#else
		if (header.h.flags[0] != 2) // ver
#endif
		{
			LogPrint (
				eLogWarning, "SSU2: SessionRequest protocol version ", (int)header.h.flags[0], " is not supported");
			return false;
		}
		const uint8_t nonce[12] = { 0 };
		uint8_t headerX[48];
		m_Server.ChaCha20 (buf + 16, 48, i2p::context.GetSSU2IntroKey (), nonce, headerX);
		memcpy (&m_DestConnID, headerX, 8);
		uint64_t token;
		memcpy (&token, headerX + 8, 8);
		if (!token || token != m_Server.GetIncomingToken (m_RemoteEndpoint))
		{
			LogPrint (eLogDebug, "SSU2: SessionRequest token mismatch. Retry");
			SendRetry ();
			return true;
		}
		// create and init noise state
		if (!m_NoiseState) m_NoiseState.reset (new i2p::crypto::NoiseSymmetricState);
#if OPENSSL_PQ
		if (m_Version > 2)
		{
			InitNoiseXKStateMLKEM1 (
				*m_NoiseState, (i2p::data::CryptoKeyType)(m_Version + 2), i2p::context.GetSSU2StaticPublicKey ());
			m_NoiseState->MixHash (i2p::context.GetIdentHash (), 32); // h = SHA256(h || bhash)
		}
		else
#endif
			InitNoiseXKState1 (*m_NoiseState, i2p::context.GetSSU2StaticPublicKey ());
		// KDF for session request
		m_NoiseState->MixHash ({ { header.buf, 16 }, { headerX, 16 } }); // h = SHA256(h || header)
		m_NoiseState->MixHash (headerX + 16, 32); // h = SHA256(h || aepk);
		uint8_t sharedSecret[32];
		i2p::context.GetSSU2StaticKeys ().Agree (headerX + 16, sharedSecret);
		m_NoiseState->MixKey (sharedSecret);
		// decrypt
		size_t offset = 64;
#if OPENSSL_PQ
		if (m_Version > 2)
		{
			auto cryptoType = (i2p::data::CryptoKeyType)(m_Version + 2);
			auto keyLen = i2p::crypto::GetMLKEMPublicKeyLen (cryptoType);
			if (!i2p::crypto::IsMLKEMSupported (cryptoType) || offset + keyLen + 16 > len) return false;
			std::vector<uint8_t> encapsKey (keyLen);
			if (!m_NoiseState->Decrypt (buf + offset, encapsKey.data (), keyLen))
			{
				LogPrint (eLogWarning, "SSU2: SessionRequest ML-KEM ciphertext section AEAD decryption failed");
				return false;
			}
			m_NoiseState->MixHash (buf + offset, keyLen + 16);
			offset += keyLen + 16;
			m_PQKeys = i2p::crypto::CreateMLKEMKeys (cryptoType);
			if (!m_PQKeys || !m_PQKeys->SetPublicKey (encapsKey.data ())) return false;
		}
#endif
		if (offset + 16 > len)
		{
			LogPrint (eLogWarning, "SSU2: SessionRequest message is too short ", len);
			return false;
		}
		uint8_t * payload = buf + offset;
		std::vector<uint8_t> decryptedPayload (len - offset - 16);
		if (!m_NoiseState->Decrypt (payload, decryptedPayload.data (), decryptedPayload.size ()))
		{
			LogPrint (eLogWarning, "SSU2: SessionRequest AEAD verification failed ");
			return false;
		}
		m_NoiseState->MixHash (
			payload, len - offset); // h = SHA256(h || encrypted payload from Session Request) for SessionCreated
		// payload
		m_State = eSSU2SessionStateSessionRequestReceived;
		HandlePayload (decryptedPayload.data (), decryptedPayload.size ());

		if (m_TerminationReason == eSSU2TerminationReasonNormalClose)
		{
			m_Server.AddSession (shared_from_this ());
			SendSessionCreated (headerX + 16);
		}
		else
			SendRetry ();
		return true;
	}

	void SSU2Session::SendSessionCreated (const uint8_t * X)
	{
		// we are Bob
		m_EphemeralKeys = i2p::transport::transports.GetNextX25519KeysPair ();
		m_SentHandshakePacket.reset (new HandshakePacket);
		auto ts = i2p::util::GetMillisecondsSinceEpoch ();
		m_SentHandshakePacket->sendTime = ts;

		uint8_t kh2[32];
		i2p::crypto::HKDF (m_NoiseState->m_CK, nullptr, 0, "SessCreateHeader", kh2,
			32); // k_header_2 = HKDF(chainKey, ZEROLEN, "SessCreateHeader", 32)
		// fill packet
		Header & header = m_SentHandshakePacket->header;
		uint8_t *headerX = m_SentHandshakePacket->headerX, *payload = m_SentHandshakePacket->payload;
		header.h.connID = m_DestConnID; // dest id
		RAND_bytes (header.buf + 8, 4); // random packet num
		header.h.type = eSSU2SessionCreated;
		header.h.flags[0] = m_Version; // ver
		header.h.flags[1] = (uint8_t)i2p::context.GetNetID (); // netID
		header.h.flags[2] = 0; // flag
		memcpy (headerX, &m_SourceConnID, 8); // source id
		memset (headerX + 8, 0, 8); // token = 0
		memcpy (headerX + 16, m_EphemeralKeys->GetPublicKey (), 32); // Y
		// KDF for SessionCreated
		m_NoiseState->MixHash ({ { header.buf, 16 }, { headerX, 16 } }); // h = SHA256(h || header)
		m_NoiseState->MixHash (headerX + 16, 32); // h = SHA256(h || bepk);
		uint8_t sharedSecret[32];
		m_EphemeralKeys->Agree (X, sharedSecret);
		m_NoiseState->MixKey (sharedSecret);
		// payload
		size_t maxPayloadSize = m_MaxPayloadSize - 48;
		size_t payloadSize = 0, offset = 0;
#if OPENSSL_PQ
		if (m_Version > 2)
		{
			if (!m_PQKeys) return;
			size_t cipherTextLen = m_PQKeys->GetCTLen ();
			std::vector<uint8_t> kemCiphertext (cipherTextLen);
			if (!m_PQKeys || !m_PQKeys->Encaps (kemCiphertext.data (), sharedSecret)) return;
			if (!m_NoiseState->Encrypt (kemCiphertext.data (), payload, cipherTextLen))
			{
				LogPrint (eLogWarning, "SSU2: SessionCreated ML-KEM ciphertext section AEAD encryption failed");
				return;
			}
			offset = cipherTextLen + 16;
			m_NoiseState->MixHash (payload, offset); // encrypt ML-KEM frame
			m_NoiseState->MixKey (sharedSecret);
			payloadSize += offset;
		}
#endif
		payload[payloadSize] = eSSU2BlkDateTime;
		htobe16buf (payload + payloadSize + 1, 4);
		htobe32buf (payload + payloadSize + 3, (ts + 500) / 1000);
		payloadSize += 7;
		payloadSize += CreateAddressBlock (payload + payloadSize, maxPayloadSize - payloadSize, m_RemoteEndpoint);
		if (m_RelayTag)
		{
			payload[payloadSize] = eSSU2BlkRelayTag;
			htobe16buf (payload + payloadSize + 1, 4);
			htobe32buf (payload + payloadSize + 3, m_RelayTag);
			payloadSize += 7;
		}
		auto token = m_Server.NewIncomingToken (m_RemoteEndpoint);
		if (ts + SSU2_TOKEN_EXPIRATION_THRESHOLD > token.second) // not expired?
		{
			payload[payloadSize] = eSSU2BlkNewToken;
			htobe16buf (payload + payloadSize + 1, 12);
			htobe32buf (payload + payloadSize + 3, token.second - SSU2_TOKEN_EXPIRATION_THRESHOLD); // expires
			memcpy (payload + payloadSize + 7, &token.first, 8); // token
			payloadSize += 15;
		}
		if (payloadSize <= maxPayloadSize)
			payloadSize
				+= CreatePaddingBlock (payload + payloadSize, std::min (maxPayloadSize - payloadSize, (size_t)64));
		else
		{
			LogPrint (eLogError, "SSU2: SessionCreated max payload size is too small ", maxPayloadSize);
			return;
		}
		// encrypt
		const uint8_t nonce[12] = { 0 }; // always zero
		if (!m_NoiseState->Encrypt (payload + offset, payload + offset, payloadSize - offset))
		{
			LogPrint (eLogWarning, "SSU2: SessionCreated payload encryption failed ");
			return;
		}
		payloadSize += 16;
		m_NoiseState->MixHash (
			payload + offset, payloadSize - offset); // h = SHA256(h || encrypted Noise payload from Session Created)
		header.ll[0] ^= CreateHeaderMask (i2p::context.GetSSU2IntroKey (), payload + (payloadSize - 24));
		header.ll[1] ^= CreateHeaderMask (kh2, payload + (payloadSize - 12));
		m_Server.ChaCha20 (headerX, 48, kh2, nonce, headerX);
		m_State = eSSU2SessionStateSessionCreatedSent;
		m_SentHandshakePacket->payloadSize = payloadSize;
		// send
		m_HandshakeInterval = ts;
		m_Server.Send (header.buf, 16, headerX, 48, payload, payloadSize, m_RemoteEndpoint);
	}

	bool SSU2Session::ProcessSessionCreated (uint8_t * buf, size_t len)
	{
		// we are Alice
		if (!m_NoiseState)
		{
			LogPrint (eLogWarning, "SSU2: Unexpected SessionCreated message");
			return false;
		}
		if (len < 80)
		{
			LogPrint (eLogWarning, "SSU2: SessionCreated message too short ", len);
			return false;
		}
		Header header;
		memcpy (header.buf, buf, 16);
		header.ll[0] ^= CreateHeaderMask (m_Address->i, buf + (len - 24));
		uint8_t kh2[32];
		i2p::crypto::HKDF (m_NoiseState->m_CK, nullptr, 0, "SessCreateHeader", kh2,
			32); // k_header_2 = HKDF(chainKey, ZEROLEN, "SessCreateHeader", 32)
		header.ll[1] ^= CreateHeaderMask (kh2, buf + (len - 12));
		if (header.h.type != eSSU2SessionCreated)
			// this situation is valid, because it might be Retry with different encryption
			return false;

		m_HandshakeInterval = i2p::util::GetMillisecondsSinceEpoch () - m_HandshakeInterval;
		const uint8_t nonce[12] = { 0 };
		uint8_t headerX[48];
		m_Server.ChaCha20 (buf + 16, 48, kh2, nonce, headerX);
		// KDF for SessionCreated
		m_NoiseState->MixHash ({ { header.buf, 16 }, { headerX, 16 } }); // h = SHA256(h || header)
		m_NoiseState->MixHash (headerX + 16, 32); // h = SHA256(h || bepk);
		uint8_t sharedSecret[32];
		m_EphemeralKeys->Agree (headerX + 16, sharedSecret);
		m_NoiseState->MixKey (sharedSecret);
		size_t offset = 64;
#if OPENSSL_PQ
		if (m_Version > 2)
		{
			if (!m_PQKeys) return false;
			i2p::data::CryptoKeyType cryptoType = (i2p::data::CryptoKeyType)(m_Version + 2);
			size_t cipherTextLen = i2p::crypto::GetMLKEMCipherTextLen (cryptoType);
			if (!i2p::crypto::IsMLKEMSupported (cryptoType) || offset + cipherTextLen + 16 > len) return false;
			std::vector<uint8_t> kemCiphertext (cipherTextLen);
			if (!m_NoiseState->Decrypt (buf + offset, kemCiphertext.data (), cipherTextLen))
			{
				LogPrint (eLogWarning, "SSU2: SessionCreated ML-KEM ciphertext section AEAD decryption failed");
				return false;
			}
			m_NoiseState->MixHash (buf + offset, cipherTextLen + 16);
			offset += cipherTextLen + 16;
			if (!m_PQKeys || !m_PQKeys->Decaps (kemCiphertext.data (), sharedSecret)) return false;
			m_NoiseState->MixKey (sharedSecret);
		}
#endif
		// decrypt
		if (offset + 16 > len)
		{
			LogPrint (eLogWarning, "SSU2: SessionCreated message is too short ", len);
			return false;
		}
		uint8_t * payload = buf + offset;
		std::vector<uint8_t> decryptedPayload (len - offset - 16);
		if (!m_NoiseState->Decrypt (payload, decryptedPayload.data (), decryptedPayload.size ()))
		{
			LogPrint (eLogWarning, "SSU2: SessionCreated AEAD verification failed");
			if (GetRemoteIdentity ())
				i2p::data::netdb.SetUnreachable (GetRemoteIdentity ()->GetIdentHash (), true); // assume wrong s key
			return false;
		}
		m_NoiseState->MixHash (
			payload, len - offset); // h = SHA256(h || encrypted payload from SessionCreated) for SessionConfirmed
		// payload
		m_State = eSSU2SessionStateSessionCreatedReceived;
		HandlePayload (decryptedPayload.data (), decryptedPayload.size ());

		m_Server.AddSession (shared_from_this ());
		SendSessionConfirmed (headerX + 16);
		KDFDataPhase (m_KeyDataSend, m_KeyDataReceive);

		return true;
	}

	void SSU2Session::SendSessionConfirmed (const uint8_t * Y)
	{
		// we are Alice
		m_SentHandshakePacket.reset (new HandshakePacket);
		m_SentHandshakePacket->sendTime = i2p::util::GetMillisecondsSinceEpoch ();

		uint8_t kh2[32];
		i2p::crypto::HKDF (m_NoiseState->m_CK, nullptr, 0, "SessionConfirmed", kh2,
			32); // k_header_2 = HKDF(chainKey, ZEROLEN, "SessionConfirmed", 32)
		// fill packet
		Header & header = m_SentHandshakePacket->header;
		header.h.connID = m_DestConnID; // dest id
		header.h.packetNum = 0; // always zero
		header.h.type = eSSU2SessionConfirmed;
		memset (header.h.flags, 0, 3);
		header.h.flags[0] = 1; // frag, total fragments always 1
		// payload
		size_t maxPayloadSize = m_MaxPayloadSize - 48; // for part 2, 48 is part1
		uint8_t * payload = m_SentHandshakePacket->payload;
		size_t payloadSize = CreateRouterInfoBlock (payload, maxPayloadSize, i2p::context.CopyRouterInfoBuffer ());
		if (!payloadSize)
		{
			// split by two fragments
			maxPayloadSize += m_MaxPayloadSize;
			payloadSize = CreateRouterInfoBlock (payload, maxPayloadSize, i2p::context.CopyRouterInfoBuffer ());
			header.h.flags[0] = 0x02; // frag 0, total fragments 2
			// TODO: check if we need more fragments
		}
		if (payloadSize < maxPayloadSize)
			payloadSize += CreatePaddingBlock (payload + payloadSize, maxPayloadSize - payloadSize);
		// KDF for Session Confirmed part 1
		m_NoiseState->MixHash (header.buf, 16); // h = SHA256(h || header)
		// Encrypt part 1
		uint8_t * part1 = m_SentHandshakePacket->headerX;
		m_NoiseState->Encrypt (i2p::context.GetSSU2StaticPublicKey (), part1, 32);
		m_NoiseState->MixHash (part1, 48); // h = SHA256(h || ciphertext);
		// KDF for Session Confirmed part 2
		uint8_t sharedSecret[32];
		i2p::context.GetSSU2StaticKeys ().Agree (Y, sharedSecret);
		m_NoiseState->MixKey (sharedSecret);
		// Encrypt part2
		m_NoiseState->Encrypt (payload, payload, payloadSize);
		payloadSize += 16;
		m_NoiseState->MixHash (payload, payloadSize); // h = SHA256(h || ciphertext);
		m_SentHandshakePacket->payloadSize = payloadSize;
		if (header.h.flags[0] > 1)
		{
			if (payloadSize > m_MaxPayloadSize - 48)
			{
				payloadSize = m_MaxPayloadSize - 48 - (m_Server.GetRng () () % 16);
				if (m_SentHandshakePacket->payloadSize - payloadSize < 24) payloadSize -= 24;
			}
			else
				header.h.flags[0] = 1;
		}
		// Encrypt header
		header.ll[0] ^= CreateHeaderMask (m_Address->i, payload + (payloadSize - 24));
		header.ll[1] ^= CreateHeaderMask (kh2, payload + (payloadSize - 12));
		m_State = eSSU2SessionStateSessionConfirmedSent;
		// send
		m_Server.Send (header.buf, 16, part1, 48, payload, payloadSize, m_RemoteEndpoint);
		m_SendPacketNum++;
		if (m_SentHandshakePacket->payloadSize > payloadSize)
		{
			// send second fragment
			m_SessionConfirmedFragment.reset (new HandshakePacket);
			Header & header = m_SessionConfirmedFragment->header;
			header.h.connID = m_DestConnID; // dest id
			header.h.packetNum = 0;
			header.h.type = eSSU2SessionConfirmed;
			memset (header.h.flags, 0, 3);
			header.h.flags[0] = 0x12; // frag 1, total fragments 2
			m_SessionConfirmedFragment->payloadSize = m_SentHandshakePacket->payloadSize - payloadSize;
			memcpy (m_SessionConfirmedFragment->payload, m_SentHandshakePacket->payload + payloadSize,
				m_SessionConfirmedFragment->payloadSize);
			m_SentHandshakePacket->payloadSize = payloadSize;
			header.ll[0] ^= CreateHeaderMask (
				m_Address->i, m_SessionConfirmedFragment->payload + (m_SessionConfirmedFragment->payloadSize - 24));
			header.ll[1] ^= CreateHeaderMask (
				kh2, m_SessionConfirmedFragment->payload + (m_SessionConfirmedFragment->payloadSize - 12));
			m_Server.Send (header.buf, 16, m_SessionConfirmedFragment->payload, m_SessionConfirmedFragment->payloadSize,
				m_RemoteEndpoint);
		}
	}

	bool SSU2Session::ProcessSessionConfirmed (uint8_t * buf, size_t len)
	{
		// we are Bob
		if (!m_NoiseState)
		{
			LogPrint (eLogWarning, "SSU2: Unexpected SessionConfirmed message");
			return false;
		}
		Header header;
		memcpy (header.buf, buf, 16);
		header.ll[0] ^= CreateHeaderMask (i2p::context.GetSSU2IntroKey (), buf + (len - 24));
		uint8_t kh2[32];
		i2p::crypto::HKDF (m_NoiseState->m_CK, nullptr, 0, "SessionConfirmed", kh2,
			32); // k_header_2 = HKDF(chainKey, ZEROLEN, "SessionConfirmed", 32)
		header.ll[1] ^= CreateHeaderMask (kh2, buf + (len - 12));
		if (header.h.type != eSSU2SessionConfirmed)
		{
			LogPrint (eLogInfo, "SSU2: Unexpected message type ", (int)header.h.type, " instead ",
				(int)eSSU2SessionConfirmed);
			// TODO: queue up
			return true;
		}
		// packet num must be always zero
		if (header.h.packetNum)
		{
			LogPrint (eLogError, "SSU2: Non zero packet number in SessionConfirmed");
			return false;
		}
		// check if fragmented. The SSU2 count field permits up to 15 fragments;
		// Java uses that range for SessionConfirmed with large RouterInfos.
		std::vector<uint8_t> reconstructed;
		uint8_t numFragments = header.h.flags[0] & 0x0F;
		if (numFragments > 1)
		{
			if (numFragments > 15 || len < 32)
			{
				LogPrint (eLogError, "SSU2: Invalid SessionConfirmed fragment count or length ",
					(int)numFragments, " ", len);
				m_SessionConfirmedFragments.clear ();
				m_SessionConfirmedNumFragments = 0;
				return false;
			}
			uint8_t index = header.h.flags[0] >> 4;
			if (index >= numFragments)
			{
				LogPrint (eLogError, "SSU2: SessionConfirmed fragment index ", (int)index, " exceeds count");
				m_SessionConfirmedFragments.clear ();
				m_SessionConfirmedNumFragments = 0;
				return false;
			}
			// Java rejects a changed total even when fragment 0 arrives last.
			if (m_SessionConfirmedNumFragments && m_SessionConfirmedNumFragments != numFragments)
			{
				LogPrint (eLogError, "SSU2: SessionConfirmed fragment count changed");
				m_SessionConfirmedFragments.clear ();
				m_SessionConfirmedNumFragments = 0;
				return false;
			}
			if (!m_SessionConfirmedNumFragments) m_SessionConfirmedNumFragments = numFragments;
			if (index == 0) m_SessionConfirmedHeader = header;
			m_SessionConfirmedFragments.emplace (
				index, std::vector<uint8_t> (buf + 16, buf + len));
			if (m_SessionConfirmedFragments.size () < m_SessionConfirmedNumFragments)
				return true; // still waiting for fragments
			if (m_SessionConfirmedHeader.h.type != eSSU2SessionConfirmed)
				return true; // hold until the first fragment arrives
			std::vector<uint8_t> assembled;
			for (uint8_t i = 0; i < m_SessionConfirmedNumFragments; i++)
			{
				auto it = m_SessionConfirmedFragments.find (i);
				if (it == m_SessionConfirmedFragments.end ()) return true;
				assembled.insert (assembled.end (), it->second.begin (), it->second.end ());
			}
			if (assembled.size () > SSU2_MAX_PACKET_SIZE * 15)
			{
				LogPrint (eLogError, "SSU2: SessionConfirmed reassembly is too large ", assembled.size ());
				m_SessionConfirmedFragments.clear ();
				m_SessionConfirmedNumFragments = 0;
				return false;
			}
			header = m_SessionConfirmedHeader;
			reconstructed.assign (16 + assembled.size (), 0);
			memcpy (reconstructed.data () + 16, assembled.data (), assembled.size ());
			buf = reconstructed.data ();
			len = reconstructed.size ();
			m_SessionConfirmedFragments.clear ();
			m_SessionConfirmedNumFragments = 0;
		}
		else
		{
			m_SessionConfirmedFragments.clear ();
			m_SessionConfirmedNumFragments = 0;
		}
		if (len < 83) // 64 bytes header/part1 + 16 bytes MAC + at least 1 byte block type + 2 bytes block size
		{
			LogPrint (eLogWarning, "SSU2: SessionConfirmed message too short ", len);
			if (m_SessionConfirmedFragment) m_SessionConfirmedFragment.reset (nullptr);
			return false;
		}
		m_HandshakeInterval = i2p::util::GetMillisecondsSinceEpoch () - m_HandshakeInterval;
		// KDF for Session Confirmed part 1
		m_NoiseState->MixHash (header.buf, 16); // h = SHA256(h || header)
		// decrypt part1
		uint8_t S[32];
		if (!m_NoiseState->Decrypt (buf + 16, S, 32))
		{
			LogPrint (eLogWarning, "SSU2: SessionConfirmed part 1 AEAD verification failed ");
			if (m_SessionConfirmedFragment) m_SessionConfirmedFragment.reset (nullptr);
			return false;
		}
		m_NoiseState->MixHash (buf + 16, 48); // h = SHA256(h || ciphertext);
		// KDF for Session Confirmed part 2 and data phase
		uint8_t sharedSecret[32];
		m_EphemeralKeys->Agree (S, sharedSecret);
		m_NoiseState->MixKey (sharedSecret);
		KDFDataPhase (m_KeyDataReceive, m_KeyDataSend);
		// decrypt part2
		uint8_t * payload = buf + 64;
		std::vector<uint8_t> decryptedPayload (len - 80);
		if (!m_NoiseState->Decrypt (payload, decryptedPayload.data (), len - 80))
		{
			LogPrint (eLogWarning, "SSU2: SessionConfirmed part 2 AEAD verification failed ");
			if (m_SessionConfirmedFragment) m_SessionConfirmedFragment.reset (nullptr);
			return false;
		}
		m_NoiseState->MixHash (payload, len - 64); // h = SHA256(h || ciphertext);
		if (m_SessionConfirmedFragment) m_SessionConfirmedFragment.reset (nullptr);
		// payload
		// handle RouterInfo block that must be first
		if (decryptedPayload[0] != eSSU2BlkRouterInfo)
		{
			LogPrint (eLogError, "SSU2: SessionConfirmed unexpected first block type ", (int)decryptedPayload[0]);
			return false;
		}
		size_t riSize = bufbe16toh (decryptedPayload.data () + 1);
		if (riSize + 3 > decryptedPayload.size ())
		{
			LogPrint (eLogError, "SSU2: SessionConfirmed RouterInfo block is too long ", riSize);
			return false;
		}
		LogPrint (eLogDebug, "SSU2: RouterInfo in SessionConfirmed");
		auto ri = ExtractRouterInfo (decryptedPayload.data () + 3, riSize);
		if (!ri)
		{
			LogPrint (eLogError, "SSU2: SessionConfirmed malformed RouterInfo block");
			return false;
		}
		auto ts = i2p::util::GetMillisecondsSinceEpoch ();
		if (ts > ri->GetTimestamp () + i2p::data::NETDB_MIN_EXPIRATION_TIMEOUT * 1000LL) // 90 minutes
		{
			LogPrint (eLogError, "SSU2: RouterInfo in SessionConfirmed is too old for ",
				(ts - ri->GetTimestamp ()) / 1000LL, " seconds");
			return false;
		}
		if (ts + i2p::data::NETDB_EXPIRATION_TIMEOUT_THRESHOLD * 1000LL < ri->GetTimestamp ()) // 2 minutes
		{
			LogPrint (eLogError, "SSU2: RouterInfo in SessionConfirmed is from future for ",
				(ri->GetTimestamp () - ts) / 1000LL, " seconds");
			return false;
		}
		if (ri->GetVersion () < i2p::data::NETDB_MIN_ALLOWED_VERSION && !ri->IsHighBandwidth ())
		{
			LogPrint (eLogInfo, "SSU2: Router version ", ri->GetVersion (), " is too old in SessionConfirmed");
			return false;
		}
		// update RouterInfo in netdb
		auto ri1
			= i2p::data::netdb.AddRouterInfo (ri->GetBuffer (), ri->GetBufferLen ()); // ri points to one from netdb now
		if (!ri1)
		{
			LogPrint (eLogError, "SSU2: Couldn't update RouterInfo from SessionConfirmed in netdb");
			return false;
		}

		bool isOlder = false;
		if (ri->GetTimestamp () + i2p::data::NETDB_EXPIRATION_TIMEOUT_THRESHOLD * 1000LL < ri1->GetTimestamp ())
		{
			// received RouterInfo is older than one in netdb
			isOlder = true;
			if (ri->HasProfile ())
			{
				auto profile = i2p::data::GetRouterProfile (ri->GetIdentHash ()); // retrieve profile
				if (profile && profile->IsDuplicated ()) return false;
			}
		}
		ri = ri1;

		m_Address = m_RemoteEndpoint.address ().is_v6 () ? ri->GetSSU2V6Address () : ri->GetSSU2V4Address ();
		if (!m_Address)
		{
			LogPrint (eLogError, "SSU2: Address not found in SessionConfirmed from ",
				i2p::data::GetIdentHashAbbreviation (ri->GetIdentHash ()));
			return false;
		}
		if (m_Address->published && m_RemoteEndpoint.address () != m_Address->host
			&& (!m_RemoteEndpoint.address ().is_v6 ()
				|| memcmp (m_RemoteEndpoint.address ().to_v6 ().to_bytes ().data (),
					m_Address->host.to_v6 ().to_bytes ().data (), 8))) // temporary address
		{
			if (isOlder) // older router?
				i2p::data::UpdateRouterProfile (ri->GetIdentHash (),
					[] (std::shared_ptr<i2p::data::RouterProfile> profile)
					{
						if (profile) profile->Duplicated (); // mark router as duplicated in profile
					});
			else
				LogPrint (eLogInfo, "SSU2: Host mismatch between published address ", m_Address->host,
					" and actual endpoint ", m_RemoteEndpoint.address (), " from ",
					i2p::data::GetIdentHashAbbreviation (ri->GetIdentHash ()));
			return false;
		}
		if (memcmp (S, m_Address->s, 32))
		{
			LogPrint (eLogError, "SSU2: Wrong static key in SessionConfirmed from ",
				i2p::data::GetIdentHashAbbreviation (ri->GetIdentHash ()));
			if (m_Address->published) i2p::transport::transports.AddBan (m_RemoteEndpoint.address ());
			return false;
		}
		if (!m_Address->published)
		{
			if (ri->HasProfile ())
				ri->GetProfile ()->SetLastEndpoint (m_RemoteEndpoint);
			else
				i2p::data::UpdateRouterProfile (ri->GetIdentHash (),
					[ep = m_RemoteEndpoint] (std::shared_ptr<i2p::data::RouterProfile> profile)
					{
						if (profile) profile->SetLastEndpoint (ep);
					});
		}
		SetRemoteIdentity (ri->GetRouterIdentity ());
		AdjustMaxPayloadSize ();
		m_Server.AddSessionByRouterHash (shared_from_this ()); // we know remote router now
		m_RemoteTransports = ri->GetCompatibleTransports (false);
		m_RemotePeerTestTransports = 0;
		if (ri->IsSSU2PeerTesting (true)) m_RemotePeerTestTransports |= i2p::data::RouterInfo::eSSU2V4;
		if (ri->IsSSU2PeerTesting (false)) m_RemotePeerTestTransports |= i2p::data::RouterInfo::eSSU2V6;
		m_RemoteVersion = ri->GetVersion ();

		// handle other blocks
		HandlePayload (decryptedPayload.data () + riSize + 3, decryptedPayload.size () - riSize - 3);
		if (m_IsInvalidMessage)
		{
			LogPrint (eLogError, "SSU2: Invalid block in SessionConfirmed from ",
				i2p::data::GetIdentHashAbbreviation (ri->GetIdentHash ()));
			if (m_Address->published) i2p::transport::transports.AddBan (m_RemoteEndpoint.address ());
			return false;
		}

		Established ();
		if (ri->GetCongestion () == i2p::data::RouterInfo::eRejectAll)
		{
			auto terminationTimeout = GetTerminationTimeout () / 2;
			if (terminationTimeout < SSU2_CONNECT_TIMEOUT) terminationTimeout = SSU2_CONNECT_TIMEOUT;
			SetTerminationTimeout (terminationTimeout);
		}
		SendQuickAck ();

		return true;
	}

	void SSU2Session::KDFDataPhase (uint8_t * keydata_ab, uint8_t * keydata_ba)
	{
		uint8_t keydata[64];
		i2p::crypto::HKDF (m_NoiseState->m_CK, nullptr, 0, "", keydata); // keydata = HKDF(chainKey, ZEROLEN, "", 64)
		// ab
		i2p::crypto::HKDF (keydata, nullptr, 0, "HKDFSSU2DataKeys",
			keydata_ab); // keydata_ab = HKDF(keydata, ZEROLEN, "HKDFSSU2DataKeys", 64)
		// ba
		i2p::crypto::HKDF (keydata + 32, nullptr, 0, "HKDFSSU2DataKeys",
			keydata_ba); // keydata_ba = HKDF(keydata + 32, ZEROLEN, "HKDFSSU2DataKeys", 64)
	}

	void SSU2Session::SendTokenRequest ()
	{
		// we are Alice
#if OPENSSL_PQ
		if (m_Server.GetVersion () > 2 && !m_IsVersionFallback) // we support post quantum in config
			SetVersion (m_Address->v);
		else if (m_IsVersionFallback)
			m_Version = 2;
#endif
		Header header;
		uint8_t h[32], payload[41];
		// fill packet
		header.h.connID = m_DestConnID; // dest id
		RAND_bytes (header.buf + 8, 4); // random packet num
		header.h.type = eSSU2TokenRequest;
		header.h.flags[0] = GetVersion (); // ver
		header.h.flags[1] = (uint8_t)i2p::context.GetNetID (); // netID
		header.h.flags[2] = 0; // flag
		memcpy (h, header.buf, 16);
		memcpy (h + 16, &m_SourceConnID, 8); // source id
		memset (h + 24, 0, 8); // zero token
		// payload
		payload[0] = eSSU2BlkDateTime;
		htobe16buf (payload + 1, 4);
		htobe32buf (payload + 3, (i2p::util::GetMillisecondsSinceEpoch () + 500) / 1000);
		size_t payloadSize = 7;
		payloadSize += CreatePaddingBlock (payload + payloadSize, 25 - payloadSize, 1);
		// encrypt
		uint8_t nonce[12];
		CreateNonce (be32toh (header.h.packetNum), nonce);
		i2p::crypto::AEADChaCha20Poly1305 (
			payload, payloadSize, h, 32, m_Address->i, nonce, payload, payloadSize + 16, true);
		payloadSize += 16;
		header.ll[0] ^= CreateHeaderMask (m_Address->i, payload + (payloadSize - 24));
		header.ll[1] ^= CreateHeaderMask (m_Address->i, payload + (payloadSize - 12));
		memset (nonce, 0, 12);
		m_Server.ChaCha20 (h + 16, 16, m_Address->i, nonce, h + 16);
		// send
		if (m_Server.AddPendingOutgoingSession (shared_from_this ()))
			m_Server.Send (header.buf, 16, h + 16, 16, payload, payloadSize, m_RemoteEndpoint);
		else
		{
			LogPrint (eLogWarning, "SSU2: TokenRequest request to ", m_RemoteEndpoint, " already pending");
			Terminate ();
		}
	}

	bool SSU2Session::ProcessTokenRequest (Header & header, uint8_t * buf, size_t len)
	{
		// we are Bob
		if (len < 48)
		{
			LogPrint (eLogWarning, "SSU2: Incorrect TokenRequest len ", len);
			return false;
		}
		if (header.h.flags[1] && header.h.flags[1] != i2p::context.GetNetID ())
		{
			LogPrint (eLogWarning, "SSU2: TokenRequest has wrong network ID ", (int)header.h.flags[1]);
			return false;
		}
#if OPENSSL_PQ
		if (header.h.flags[0] >= 2 && header.h.flags[0] <= 4) // ver
		{
			if (m_Server.GetVersion () > 2) SetVersion (header.h.flags[0]);
		}
		else
#else
		if (header.h.flags[0] != 2) // ver
#endif
		{
			LogPrint (eLogWarning, "SSU2: TokenRequest protocol version ", (int)header.h.flags[0], " is not supported");
			return false;
		}
		uint8_t nonce[12] = { 0 };
		uint8_t h[32];
		memcpy (h, header.buf, 16);
		m_Server.ChaCha20 (buf + 16, 16, i2p::context.GetSSU2IntroKey (), nonce, h + 16);
		memcpy (&m_DestConnID, h + 16, 8);
		// decrypt
		CreateNonce (be32toh (header.h.packetNum), nonce);
		uint8_t * payload = buf + 32;
		if (!i2p::crypto::AEADChaCha20Poly1305 (
				payload, len - 48, h, 32, i2p::context.GetSSU2IntroKey (), nonce, payload, len - 48, false))
		{
			LogPrint (eLogWarning, "SSU2: TokenRequest AEAD verification failed ");
			return false;
		}
		// payload
		m_State = eSSU2SessionStateTokenRequestReceived;
		HandlePayload (payload, len - 48);
		SendRetry ();
		return true;
	}

	void SSU2Session::SendRetry ()
	{
		// we are Bob
		Header header;
		uint8_t h[32], payload[72];
		// fill packet
		header.h.connID = m_DestConnID; // dest id
		RAND_bytes (header.buf + 8, 4); // random packet num
		header.h.type = eSSU2Retry;
		header.h.flags[0] = GetVersion (); // ver
		header.h.flags[1] = (uint8_t)i2p::context.GetNetID (); // netID
		header.h.flags[2] = 0; // flag
		memcpy (h, header.buf, 16);
		memcpy (h + 16, &m_SourceConnID, 8); // source id
		uint64_t token = 0;
		if (m_TerminationReason == eSSU2TerminationReasonNormalClose)
			token = m_Server.GetIncomingToken (m_RemoteEndpoint);
		memcpy (h + 24, &token, 8); // token
		// payload
		payload[0] = eSSU2BlkDateTime;
		htobe16buf (payload + 1, 4);
		htobe32buf (payload + 3, (i2p::util::GetMillisecondsSinceEpoch () + 500) / 1000);
		size_t payloadSize = 7;
		payloadSize += CreateAddressBlock (payload + payloadSize, 56 - payloadSize, m_RemoteEndpoint);
		if (m_TerminationReason != eSSU2TerminationReasonNormalClose)
			payloadSize += CreateTerminationBlock (payload + payloadSize, 56 - payloadSize);
		payloadSize += CreatePaddingBlock (payload + payloadSize, 56 - payloadSize);
		// encrypt
		uint8_t nonce[12];
		CreateNonce (be32toh (header.h.packetNum), nonce);
		i2p::crypto::AEADChaCha20Poly1305 (
			payload, payloadSize, h, 32, i2p::context.GetSSU2IntroKey (), nonce, payload, payloadSize + 16, true);
		payloadSize += 16;
		header.ll[0] ^= CreateHeaderMask (i2p::context.GetSSU2IntroKey (), payload + (payloadSize - 24));
		header.ll[1] ^= CreateHeaderMask (i2p::context.GetSSU2IntroKey (), payload + (payloadSize - 12));
		memset (nonce, 0, 12);
		m_Server.ChaCha20 (h + 16, 16, i2p::context.GetSSU2IntroKey (), nonce, h + 16);
		// send
		m_Server.Send (header.buf, 16, h + 16, 16, payload, payloadSize, m_RemoteEndpoint);
	}

	bool SSU2Session::ProcessRetry (uint8_t * buf, size_t len)
	{
		// we are Alice
		Header header;
		memcpy (header.buf, buf, 16);
		header.ll[0] ^= CreateHeaderMask (m_Address->i, buf + (len - 24));
		header.ll[1] ^= CreateHeaderMask (m_Address->i, buf + (len - 12));
		if (header.h.type != eSSU2Retry)
		{
			LogPrint (eLogWarning, "SSU2: Unexpected message type ", (int)header.h.type, " instead ", (int)eSSU2Retry);
			return false;
		}
		if (header.h.flags[1] && header.h.flags[1] != i2p::context.GetNetID ())
		{
			LogPrint (eLogWarning, "SSU2: Retry has wrong network ID ", (int)header.h.flags[1]);
			return false;
		}
		if (len < 48)
		{
			LogPrint (eLogWarning, "SSU2: Retry message too short ", len);
			return false;
		}
		uint8_t nonce[12] = { 0 };
		uint64_t headerX[2]; // sourceConnID, token
		m_Server.ChaCha20 (buf + 16, 16, m_Address->i, nonce, (uint8_t *)headerX);
		uint64_t token = headerX[1];
		if (token)
			m_Server.UpdateOutgoingToken (
				m_RemoteEndpoint, token, i2p::util::GetSecondsSinceEpoch () + SSU2_TOKEN_EXPIRATION_TIMEOUT);
		// decrypt and handle payload
		uint8_t * payload = buf + 32;
		CreateNonce (be32toh (header.h.packetNum), nonce);
		uint8_t h[32];
		memcpy (h, header.buf, 16);
		memcpy (h + 16, &headerX, 16);
		if (!i2p::crypto::AEADChaCha20Poly1305 (
				payload, len - 48, h, 32, m_Address->i, nonce, payload, len - 48, false))
		{
			LogPrint (eLogWarning, "SSU2: Retry AEAD verification failed");
			return false;
		}
		m_State = eSSU2SessionStateTokenReceived;
		HandlePayload (payload, len - 48);
		if (m_TerminationReason == eSSU2TerminationReasonIncompatibleVersion)
		{
			m_Version = 2; // fallback to non-PQ
			m_IsVersionFallback = true;
		}
		if (!token)
		{
			// we should handle payload even for zero token to handle Datetime block and adjust clock in case of clock skew
			LogPrint (eLogWarning, "SSU2: Retry token is zero");
			return false;
		}

		if (!m_NoiseState) m_NoiseState.reset (new i2p::crypto::NoiseSymmetricState);
#if OPENSSL_PQ
		if (m_Version > 2)
			InitNoiseXKStateMLKEM1 (*m_NoiseState, (i2p::data::CryptoKeyType)(m_Version + 2), m_Address->s);
		else
#endif
			InitNoiseXKState1 (*m_NoiseState, m_Address->s); // reset Noise TODO: check state

		m_TerminationReason = eSSU2TerminationReasonNormalClose;
		return SendSessionRequest (token);
	}

	static bool ValidateHolePunchDateTime (const uint8_t * buf, size_t len)
	{
		bool found = false;
		size_t offset = 0;
		while (offset + 3 <= len)
		{
			uint8_t type = buf[offset++];
			size_t size = bufbe16toh (buf + offset);
			offset += 2;
			if (offset + size > len) return false;
			if (type == eSSU2BlkDateTime)
			{
				if (found || size != 4) return false;
				found = true;
				int64_t skew = (int64_t)bufbe32toh (buf + offset) - (int64_t)i2p::util::GetSecondsSinceEpoch ();
				if (skew > SSU2_RELAY_MAX_SKEW || skew < -SSU2_RELAY_MAX_SKEW) return false;
			}
			offset += size;
		}
		return true; // DateTime is optional for this out-of-session path
	}

	bool SSU2Session::ProcessHolePunch (uint8_t * buf, size_t len)
	{
		// we are Alice
		LogPrint (eLogDebug, "SSU2: HolePunch");
		Header header;
		memcpy (header.buf, buf, 16);
		header.ll[0] ^= CreateHeaderMask (i2p::context.GetSSU2IntroKey (), buf + (len - 24));
		header.ll[1] ^= CreateHeaderMask (i2p::context.GetSSU2IntroKey (), buf + (len - 12));
		if (header.h.type != eSSU2HolePunch)
		{
			LogPrint (
				eLogWarning, "SSU2: Unexpected message type ", (int)header.h.type, " instead ", (int)eSSU2HolePunch);
			return false;
		}
		if (header.h.flags[1] && header.h.flags[1] != i2p::context.GetNetID ())
		{
			LogPrint (eLogWarning, "SSU2: HolePunch has wrong network ID ", (int)header.h.flags[1]);
			return false;
		}
		if (len < 48)
		{
			LogPrint (eLogWarning, "SSU2: HolePunch message too short ", len);
			return false;
		}
		uint8_t nonce[12] = { 0 };
		uint64_t headerX[2]; // sourceConnID, token
		m_Server.ChaCha20 (buf + 16, 16, i2p::context.GetSSU2IntroKey (), nonce, (uint8_t *)headerX);
		m_DestConnID = headerX[0];
		// decrypt and handle payload
		uint8_t * payload = buf + 32;
		CreateNonce (be32toh (header.h.packetNum), nonce);
		uint8_t h[32];
		memcpy (h, header.buf, 16);
		memcpy (h + 16, &headerX, 16);
		if (!i2p::crypto::AEADChaCha20Poly1305 (
				payload, len - 48, h, 32, i2p::context.GetSSU2IntroKey (), nonce, payload, len - 48, false))
		{
			LogPrint (eLogWarning, "SSU2: HolePunch AEAD verification failed ");
			return false;
		}
		// Validate DateTime before HandleRelayResponse() can call
		// ConnectAfterIntroduction(); HandlePayload() would otherwise reset the
		// skew flag before the caller can observe it.
		if (!ValidateHolePunchDateTime (payload, len - 48))
		{
			LogPrint (eLogWarning, "SSU2: Invalid HolePunch DateTime/skew");
			return false;
		}
		m_IsInvalidMessage = false;
		HandlePayload (payload, len - 48);
		m_IsDataReceived = false;
		if (m_IsInvalidMessage)
		{
			LogPrint (eLogWarning, "SSU2: Invalid block in HolePunch");
			return false;
		}
		return true;
	}

	bool SSU2Session::ProcessPeerTest (uint8_t * buf, size_t len)
	{
		LogPrint (eLogWarning, "SSU2:  Unexpected peer test message for this session type");
		return false;
	}

	uint32_t SSU2Session::SendData (const uint8_t * buf, size_t len, uint8_t flags)
	{
		if (len < 8 || len + 32 > SSU2_MAX_PACKET_SIZE)
		{
			LogPrint (eLogWarning, "SSU2: Data message payload length ", (int)len, " is out of range");
			return 0;
		}
		Header header;
		header.h.connID = m_DestConnID;
		header.h.packetNum = htobe32 (m_SendPacketNum);
		header.h.type = eSSU2Data;
		memset (header.h.flags, 0, 3);
		if (flags) header.h.flags[0] = flags;
		uint8_t nonce[12];
		CreateNonce (m_SendPacketNum, nonce);
		uint8_t payload[SSU2_MAX_PACKET_SIZE];
		if (!m_Server.AEADChaCha20Poly1305Encrypt (
				buf, len, header.buf, 16, m_KeyDataSend, nonce, payload, SSU2_MAX_PACKET_SIZE))
		{
			LogPrint (eLogError, "SSU2: Data message AEAD encryption failed");
			return 0;
		}
		header.ll[0] ^= CreateHeaderMask (m_Address->i, payload + (len - 8));
		header.ll[1] ^= CreateHeaderMask (m_KeyDataSend + 32, payload + (len + 4));
		m_Server.Send (header.buf, 16, payload, len + 16, m_RemoteEndpoint);
		m_SendPacketNum++;
		UpdateNumSentBytes (len + 32);
		return m_SendPacketNum - 1;
	}

	void SSU2Session::ProcessData (uint8_t * buf, size_t len, const boost::asio::ip::udp::endpoint & from)
	{
		Header header;
		header.ll[0] = m_SourceConnID;
		memcpy (header.buf + 8, buf + 8, 8);
		header.ll[1] ^= CreateHeaderMask (m_KeyDataReceive + 32, buf + (len - 12));
		if (header.h.type != eSSU2Data)
		{
			LogPrint (eLogWarning, "SSU2: Unexpected message type ", (int)header.h.type, " instead ", (int)eSSU2Data);
			if (IsEstablished ())
				SendQuickAck (); // in case it was SessionConfirmed
			else
				ResendHandshakePacket (); // assume we receive
			return;
		}
		if (from != m_RemoteEndpoint && !i2p::transport::transports.IsInReservedRange (from.address ())
			&& (!m_PathChallenge
				|| from != m_PathChallenge->second)) // path challenge was not sent to this endpoint yet
		{
			LogPrint (eLogInfo, "SSU2: Remote endpoint update ", m_RemoteEndpoint, "->", from);
			SendPathChallenge (from);
		}
		if (len < 32)
		{
			LogPrint (eLogWarning, "SSU2: Data message too short ", len);
			return;
		}
		uint8_t payload[SSU2_MAX_PACKET_SIZE];
		size_t payloadSize = len - 32;
		uint32_t packetNum = be32toh (header.h.packetNum);
		uint8_t nonce[12];
		CreateNonce (packetNum, nonce);
		if (!m_Server.AEADChaCha20Poly1305Decrypt (
				buf + 16, payloadSize, header.buf, 16, m_KeyDataReceive, nonce, payload, payloadSize))
		{
			LogPrint (eLogWarning, "SSU2: Data AEAD verification failed ");
			return;
		}
		UpdateNumReceivedBytes (len);
		if (header.h.flags[0] & SSU2_FLAG_IMMEDIATE_ACK_REQUESTED) m_IsDataReceived = true;
		bool acceptPacket = false;
		if (packetNum == 0 && !m_ReceivedPacketZero)
		{
			// Packet 0 is the session's first data packet number. Accept it
			// once even if it arrives after higher-numbered reordered packets.
			m_ReceivedPacketZero = true;
			m_IsFirstReceivePacket = false;
			m_ReceivePacketNum = 0;
			acceptPacket = true;
		}
		else if (m_IsFirstReceivePacket)
		{
			// Do not baseline the receiver on an out-of-order first packet.
			m_IsFirstReceivePacket = false;
			m_ReceivePacketNum = 0;
			if (packetNum) m_OutOfSequencePackets.insert (packetNum);
			acceptPacket = true;
		}
		else
			acceptPacket = UpdateReceivePacketNum (packetNum);
		if (acceptPacket)
		{
			// A decryptable data packet proves that Bob processed SessionConfirmed even
			// if Java's single ACK0 was lost.
			if (m_State == eSSU2SessionStateSessionConfirmedSent) Established ();
			m_IsInvalidMessage = false;
			HandlePayload (payload, payloadSize);
			if (m_IsInvalidMessage)
			{
				LogPrint (eLogError, "SSU2: Invalid block in data phase, terminating");
				RequestTermination (eSSU2TerminationReasonPayloadFormatError);
			}
		}
	}

	void SSU2Session::HandlePayload (const uint8_t * buf, size_t len)
	{
		m_IsInvalidMessage = false;
		size_t offset = 0;
		bool gotPadding = false, gotTermination = false;
		// the block header is three bytes: without all of them the size field below
		// would be read past the end of the payload
		while (offset + 3 <= len)
		{
			uint8_t blk = buf[offset];
			offset++;
			auto size = bufbe16toh (buf + offset);
			offset += 2;
			LogPrint (eLogDebug, "SSU2: Block type ", (int)blk, " of size ", size);
			// SSU2Payload.processPayload() rejects everything after padding,
			// and everything except padding after a termination block.
			if (gotPadding || (gotTermination && blk != eSSU2BlkPadding))
			{
				LogPrint (eLogWarning, "SSU2: Block type ", (int)blk, " after padding/termination");
				m_IsInvalidMessage = true;
				return;
			}
			if (offset + size > len)
			{
				LogPrint (eLogError, "SSU2: Unexpected block length ", size);
				m_IsInvalidMessage = true;
				return;
			}
			switch (blk)
			{
			case eSSU2BlkDateTime:
				LogPrint (eLogDebug, "SSU2: Datetime");
				HandleDateTime (buf + offset, size);
				break;
			case eSSU2BlkOptions: LogPrint (eLogDebug, "SSU2: Options"); break;
			case eSSU2BlkRouterInfo:
				LogPrint (eLogDebug, "SSU2: RouterInfo");
				HandleRouterInfo (buf + offset, size);
				break;
			case eSSU2BlkI2NPMessage:
			{
				LogPrint (eLogDebug, "SSU2: I2NP message");
				if (size < 9)
				{
					LogPrint (eLogWarning, "SSU2: I2NP message block size ", size, " is too short");
					m_IsInvalidMessage = true;
					break;
				}
				auto nextMsg = (buf[offset] == eI2NPTunnelData) ? NewI2NPTunnelMessage (true) : NewI2NPShortMessage ();
				if (nextMsg->offset + size + 7 > nextMsg->maxLen) // 7 more bytes for full I2NP header
				{
					LogPrint (eLogWarning, "SSU2: I2NP message block size ", size, " exceeds max message size ",
						nextMsg->maxLen);
					m_IsInvalidMessage = true;
					break;
				}
				nextMsg->len = nextMsg->offset + size + 7; // 7 more bytes for full I2NP header
				memcpy (nextMsg->GetNTCP2Header (), buf + offset, size);
				nextMsg->FromNTCP2 (); // SSU2 has the same format as NTCP2
				HandleI2NPMsg (std::move (nextMsg));
				m_IsDataReceived = true;
				break;
			}
			case eSSU2BlkFirstFragment:
				LogPrint (eLogDebug, "SSU2: First fragment");
				HandleFirstFragment (buf + offset, size);
				m_IsDataReceived = true;
				break;
			case eSSU2BlkFollowOnFragment:
				LogPrint (eLogDebug, "SSU2: Follow-on fragment");
				HandleFollowOnFragment (buf + offset, size);
				m_IsDataReceived = true;
				break;
			case eSSU2BlkTermination:
			{
				if (size >= 9)
				{
					gotTermination = true;
					uint8_t rsn = buf[offset + 8]; // reason
					LogPrint (eLogDebug, "SSU2: Termination reason=", (int)rsn);
					if (IsEstablished () && rsn != eSSU2TerminationReasonTerminationReceived)
						RequestTermination (eSSU2TerminationReasonTerminationReceived);
					else if (m_State != eSSU2SessionStateTerminated)
					{
						if (m_State == eSSU2SessionStateClosing && rsn == eSSU2TerminationReasonTerminationReceived)
							m_State = eSSU2SessionStateClosingConfirmed;
						Done ();
					}
					else
						m_TerminationReason = (SSU2TerminationReason)rsn;
				}
				else
				{
					LogPrint (eLogWarning, "SSU2: Unexpected termination block size ", size);
					m_IsInvalidMessage = true;
				}
				break;
			}
			case eSSU2BlkRelayRequest:
				LogPrint (eLogDebug, "SSU2: RelayRequest");
				HandleRelayRequest (buf + offset, size);
				m_IsDataReceived = true;
				break;
			case eSSU2BlkRelayResponse:
				LogPrint (eLogDebug, "SSU2: RelayResponse");
				HandleRelayResponse (buf + offset, size);
				m_IsDataReceived = true;
				break;
			case eSSU2BlkRelayIntro:
				LogPrint (eLogDebug, "SSU2: RelayIntro");
				HandleRelayIntro (buf + offset, size);
				m_IsDataReceived = true;
				break;
			case eSSU2BlkPeerTest:
				if (size >= 2)
					LogPrint (eLogDebug, "SSU2: PeerTest msg=", (int)buf[offset], " code=", (int)buf[offset + 1]);
				else
					LogPrint (eLogWarning, "SSU2: PeerTest block is too short ", size);
				HandlePeerTest (buf + offset, size);
				if (size >= 1 && buf[offset] < 5) m_IsDataReceived = true;
				break;
			case eSSU2BlkNextNonce: break;
			case eSSU2BlkAck:
				LogPrint (eLogDebug, "SSU2: Ack");
				HandleAck (buf + offset, size);
				break;
			case eSSU2BlkAddress:
				LogPrint (eLogDebug, "SSU2: Address");
				HandleAddress (buf + offset, size);
				break;
			case eSSU2BlkIntroKey: break;
			case eSSU2BlkRelayTagRequest:
				LogPrint (eLogDebug, "SSU2: RelayTagRequest");
				if (!m_RelayTag)
				{
					if (i2p::context.AcceptsTunnels ())
					{
						auto addr = FindLocalAddress ();
						if (addr && addr->IsIntroducer ())
						{
							RAND_bytes ((uint8_t *)&m_RelayTag, 4);
							m_Server.AddRelay (m_RelayTag, shared_from_this ());
						}
					}
				}
				break;
			case eSSU2BlkRelayTag:
				LogPrint (eLogDebug, "SSU2: RelayTag");
				if (size >= 4)
					m_RelayTag = bufbe32toh (buf + offset);
				else
					LogPrint (eLogWarning, "SSU2: RelayTag block is too short ", size);
				break;
			case eSSU2BlkNewToken:
			{
				LogPrint (eLogDebug, "SSU2: New token");
				if (size >= 12)
				{
					uint64_t token;
					memcpy (&token, buf + offset + 4, 8);
					m_Server.UpdateOutgoingToken (m_RemoteEndpoint, token, bufbe32toh (buf + offset));
				}
				else
					LogPrint (eLogWarning, "SSU2: NewToken block is too short ", size);
				break;
			}
			case eSSU2BlkPathChallenge:
				LogPrint (eLogDebug, "SSU2: Path challenge");
				SendPathResponse (buf + offset, size);
				break;
			case eSSU2BlkPathResponse:
			{
				LogPrint (eLogDebug, "SSU2: Path response");
				if (size >= 8 && m_PathChallenge)
				{
					if (buf64toh (buf + offset) == m_PathChallenge->first)
					{
						m_RemoteEndpoint = m_PathChallenge->second;
						m_PathChallenge.reset (nullptr);
					}
				}
				break;
			}
			case eSSU2BlkFirstPacketNumber: break;
			case eSSU2BlkPadding: LogPrint (eLogDebug, "SSU2: Padding"); gotPadding = true; break;
			default: LogPrint (eLogWarning, "SSU2: Unknown block type ", (int)blk);
			}
			offset += size;
		}
		if (offset < len)
		{
			LogPrint (eLogWarning, "SSU2: Truncated block header at ", offset, " of ", len);
			m_IsInvalidMessage = true;
		}
	}

	void SSU2Session::HandleDateTime (const uint8_t * buf, size_t len)
	{
		if (len < 4) return;
		int64_t offset = (int64_t)i2p::util::GetSecondsSinceEpoch () - (int64_t)bufbe32toh (buf);
		switch (m_State)
		{
		case eSSU2SessionStateSessionRequestReceived:
		case eSSU2SessionStateTokenRequestReceived:
		case eSSU2SessionStateEstablished:
			if (std::abs (offset) > SSU2_CLOCK_SKEW) m_TerminationReason = eSSU2TerminationReasonClockSkew;
			break;
		case eSSU2SessionStateSessionCreatedReceived:
		case eSSU2SessionStateTokenReceived:
			if ((m_RemoteEndpoint.address ().is_v4 () && i2p::context.GetTesting ())
				|| (m_RemoteEndpoint.address ().is_v6 () && i2p::context.GetTestingV6 ()))
			{
				if (m_Server.IsSyncClockFromPeers ())
				{
					if (std::abs (offset) > SSU2_CLOCK_THRESHOLD)
					{
						LogPrint (eLogWarning, "SSU2: Time offset ", offset, " from ", m_RemoteEndpoint);
						m_Server.AdjustTimeOffset (-offset, GetRemoteIdentity ());
					}
					else
						m_Server.AdjustTimeOffset (0, nullptr);
				}
				else if (std::abs (offset) > SSU2_CLOCK_SKEW)
				{
					LogPrint (eLogError, "SSU2: Clock skew detected ", offset, ". Check your clock");
					i2p::context.SetError (eRouterErrorClockSkew);
				}
			}
			break;
		case eSSU2SessionStateIntroduced:
			// Java EstablishmentManager.receiveHolePunch() checks the DateTime
			// block skew before accepting the RelayResponse.
			if (std::abs (offset) > SSU2_RELAY_MAX_SKEW) m_IsInvalidMessage = true;
			break;
		default:;
		};
	}

	void SSU2Session::HandleRouterInfo (const uint8_t * buf, size_t len)
	{
		if (len < 2) return;
		// not from SessionConfirmed, we must add it instantly to use in next block
		std::shared_ptr<const i2p::data::RouterInfo> newRi;
		if (buf[0] & SSU2_ROUTER_INFO_FLAG_GZIP) // compressed?
		{
			auto ri = ExtractRouterInfo (buf, len);
			if (ri) newRi = i2p::data::netdb.AddRouterInfo (ri->GetBuffer (), ri->GetBufferLen ());
		}
		else // use buffer directly. TODO: handle frag
			newRi = i2p::data::netdb.AddRouterInfo (buf + 2, len - 2);

		if (newRi)
		{
			auto remoteIdentity = GetRemoteIdentity ();
			if (remoteIdentity && remoteIdentity->GetIdentHash () == newRi->GetIdentHash ())
			{
				// peer's RouterInfo update
				SetRemoteIdentity (newRi->GetIdentity ());
				auto address
					= m_RemoteEndpoint.address ().is_v6 () ? newRi->GetSSU2V6Address () : newRi->GetSSU2V4Address ();
				if (address)
				{
					m_Address = address;
					if (IsOutgoing () && m_RelayTag && !address->IsIntroducer ())
						m_RelayTag = 0; // not longer introducer
				}
			}
			i2p::transport::transports.UpdatePeerParams (newRi);
		}
	}

	void SSU2Session::HandleAck (const uint8_t * buf, size_t len)
	{
		if (m_State == eSSU2SessionStateSessionConfirmedSent)
		{
			Established ();
			return;
		}
		if (m_SentPackets.empty ()) return;
		// Java SSU2Payload requires ACK length 5 + 2*n; an odd range byte is
		// malformed, not a shorter ACK.
		if (len < 5 || (len % 2) != 1)
		{
			LogPrint (eLogWarning, "SSU2: Bad ACK block length ", len);
			return;
		}
		// acnt
		uint32_t ackThrough = bufbe32toh (buf);
		uint32_t firstPacketNum = ackThrough > buf[4] ? ackThrough - buf[4] : 0;
		HandleAckRange (firstPacketNum, ackThrough, i2p::util::GetMillisecondsSinceEpoch ()); // acnt
		// ranges
		len -= 5;
		if (!len || m_SentPackets.empty ()) return; // don't handle ranges if nothing to acknowledge
		const uint8_t * ranges = buf + 5;
		while (len >= 2 && firstPacketNum && ackThrough - firstPacketNum < SSU2_MAX_NUM_ACK_PACKETS)
		{
			uint32_t lastPacketNum = firstPacketNum - 1;
			if (*ranges > lastPacketNum) break;
			lastPacketNum -= *ranges;
			ranges++; // nacks
			if (*ranges > lastPacketNum + 1) break;
			firstPacketNum = lastPacketNum + 1 - *ranges;
			ranges++; // acks
			len -= 2;
			HandleAckRange (firstPacketNum, lastPacketNum, 0);
		}
	}

	void SSU2Session::HandleAckRange (uint32_t firstPacketNum, uint32_t lastPacketNum, uint64_t ts)
	{
		if (firstPacketNum > lastPacketNum) return;
		auto it = m_SentPackets.begin ();
		while (it != m_SentPackets.end () && it->first < firstPacketNum) it++; // find first acked packet
		if (it == m_SentPackets.end () || it->first > lastPacketNum) return; // not found
		auto it1 = it;
		int numPackets = 0;
		while (it1 != m_SentPackets.end () && it1->first <= lastPacketNum)
		{
			if (ts && !it1->second->numResends)
			{
				if (ts > it1->second->sendTime)
				{
					auto rtt = ts - it1->second->sendTime;
					if (m_RTT != SSU2_UNKNOWN_RTT)
						m_RTT = SSU2_RTT_EWMA_ALPHA * rtt + (1.0 - SSU2_RTT_EWMA_ALPHA) * m_RTT;
					else
						m_RTT = rtt;
					m_RTO = m_RTT * SSU2_kAPPA;
					m_MsgLocalExpirationTimeout = std::max (I2NP_MESSAGE_LOCAL_EXPIRATION_TIMEOUT_MIN,
						std::min (I2NP_MESSAGE_LOCAL_EXPIRATION_TIMEOUT_MAX,
							(unsigned int)(m_RTT * 1000 * I2NP_MESSAGE_LOCAL_EXPIRATION_TIMEOUT_FACTOR)));
					m_MsgLocalSemiExpirationTimeout = m_MsgLocalExpirationTimeout / 2;
					if (m_RTO < SSU2_MIN_RTO) m_RTO = SSU2_MIN_RTO;
					if (m_RTO > SSU2_MAX_RTO) m_RTO = SSU2_MAX_RTO;
				}
				ts = 0; // update RTT one time per range
			}
			it1++;
			numPackets++;
		}
		m_SentPackets.erase (it, it1);
		if (numPackets > 0)
		{
			m_WindowSize += numPackets;
			if (m_WindowSize > SSU2_MAX_WINDOW_SIZE) m_WindowSize = SSU2_MAX_WINDOW_SIZE;
		}
	}

	void SSU2Session::HandleAddress (const uint8_t * buf, size_t len)
	{
		boost::asio::ip::udp::endpoint ep;
		if (ExtractEndpoint (buf, len, ep))
		{
			LogPrint (eLogInfo, "SSU2: Our external address is ", ep);
			if (!i2p::transport::transports.IsInReservedRange (ep.address ()))
			{
				i2p::context.UpdateAddress (ep.address ());
				// check our port
				bool isV4 = ep.address ().is_v4 ();
				if (ep.port () != m_Server.GetPort (isV4))
				{
					LogPrint (eLogInfo, "SSU2: Our port ", ep.port (), " received from ", m_RemoteEndpoint,
						" is different from ", m_Server.GetPort (isV4));
					if (isV4)
					{
						if (i2p::context.GetTesting ())
							i2p::context.SetError (eRouterErrorSymmetricNAT);
						else if (m_State == eSSU2SessionStatePeerTest)
						{
							i2p::context.SetError (
								eRouterErrorFullConeNAT); // TODO: Full-Cone NAT detection isn't working.
							// i2p::context.PublishNTCP2Address (TCP_PORT, true, true, false, false); // TODO: TCP_PORT to be filled similar to ep.port()
							i2p::context.PublishSSU2Address (ep.port (), true, true, false, m_Server.GetVersion ());
						}
					}
					else
					{
						if (i2p::context.GetTestingV6 ())
							i2p::context.SetErrorV6 (eRouterErrorSymmetricNAT);
						else if (m_State == eSSU2SessionStatePeerTest)
						{
							i2p::context.SetErrorV6 (eRouterErrorFullConeNAT);
							// i2p::context.PublishNTCP2Address (TCP_PORT, true, false, true, false); // TODO: TCP_PORT to be filled similar to ep.port()
							i2p::context.PublishSSU2Address (ep.port (), true, false, true, m_Server.GetVersion ());
						}
					}
				}
				else
				{
					if (isV4)
					{
						if (i2p::context.GetError () == eRouterErrorSymmetricNAT)
						{
							if (m_State == eSSU2SessionStatePeerTest) i2p::context.SetStatus (eRouterStatusOK);
							i2p::context.SetError (eRouterErrorNone);
						}
						else if (i2p::context.GetError () == eRouterErrorFullConeNAT)
							i2p::context.SetError (eRouterErrorNone);
					}
					else
					{
						if (i2p::context.GetErrorV6 () == eRouterErrorSymmetricNAT)
						{
							if (m_State == eSSU2SessionStatePeerTest) i2p::context.SetStatusV6 (eRouterStatusOK);
							i2p::context.SetErrorV6 (eRouterErrorNone);
						}
						else if (i2p::context.GetErrorV6 () == eRouterErrorFullConeNAT)
							i2p::context.SetErrorV6 (eRouterErrorNone);
					}
				}
			}
		}
	}

	void SSU2Session::HandleFirstFragment (const uint8_t * buf, size_t len)
	{
		if (len <= 9) // Java SSU2Payload: len <= 9 is invalid for FIRSTFRAG
		{
			LogPrint (eLogWarning, "SSU2: First fragment size ", len, " is too short");
			m_IsInvalidMessage = true;
			return;
		}
		auto msg = (buf[0] == eI2NPTunnelData) ? NewI2NPTunnelMessage (true) : NewI2NPShortMessage ();
		if (msg->offset + len + 7 > msg->maxLen)
		{
			LogPrint (eLogWarning, "SSU2: First fragment size ", len, " exceeds max message size ", msg->maxLen);
			m_IsInvalidMessage = true;
			return;
		}
		uint32_t msgID;
		memcpy (&msgID, buf + 1, 4);
		// same format as I2NP message block
		msg->len = msg->offset + len + 7;
		memcpy (msg->GetNTCP2Header (), buf, len);
		std::shared_ptr<SSU2IncompleteMessage> m;
		bool found = false;
		auto it = m_IncompleteMessages.find (msgID);
		if (it != m_IncompleteMessages.end ())
		{
			found = true;
			m = it->second;
			if (m->msg)
			{
				// Java InboundMessageState.receiveFragment() ignores a repeated
				// fragment 0 instead of resetting reassembly progress.
				LogPrint (eLogDebug, "SSU2: Duplicate first fragment for message ", msgID);
				return;
			}
		}
		else
		{
			if (m_IncompleteMessages.size () >= SSU2_MAX_NUM_INCOMPLETE_MESSAGES)
			{
				LogPrint (eLogWarning, "SSU2: Too many incomplete messages ", m_IncompleteMessages.size ());
				m_IsInvalidMessage = true;
				return;
			}
			m = m_Server.GetIncompleteMessagesPool ().AcquireShared ();
			m_IncompleteMessages.emplace (msgID, m);
		}
		m->msg = msg;
		m->nextFragmentNum = 1;
		m->lastFragmentInsertTime = i2p::util::GetSecondsSinceEpoch ();
		if (found && m->ConcatOutOfSequenceFragments ())
		{
			// we have all follow-on fragments already
			m->msg->FromNTCP2 ();
			HandleI2NPMsg (std::move (m->msg));
			m_IncompleteMessages.erase (it);
		}
	}

	void SSU2Session::HandleFollowOnFragment (const uint8_t * buf, size_t len)
	{
		if (len <= 5) return; // Java SSU2Payload: len <= 5 is invalid for FOLLOWON
		if (len - 5 > SSU2_MAX_PACKET_SIZE)
		{
			LogPrint (eLogWarning, "SSU2: Follow-on fragment size ", len, " exceeds max packet size");
			m_IsInvalidMessage = true;
			return;
		}
		uint8_t fragmentNum = buf[0] >> 1;
		if (!fragmentNum || fragmentNum >= SSU2_MAX_NUM_FRAGMENTS)
		{
			LogPrint (eLogWarning, "SSU2: Invalid follow-on fragment num ", fragmentNum);
			m_IsInvalidMessage = true;
			return;
		}
		bool isLast = buf[0] & 0x01;
		uint32_t msgID;
		memcpy (&msgID, buf + 1, 4);
		auto it = m_IncompleteMessages.find (msgID);
		if (it != m_IncompleteMessages.end ())
		{
			if (fragmentNum < it->second->nextFragmentNum) return; // duplicate
			if (it->second->nextFragmentNum == fragmentNum && fragmentNum < SSU2_MAX_NUM_FRAGMENTS && it->second->msg)
			{
				// in sequence
				it->second->AttachNextFragment (buf + 5, len - 5);
				if (isLast)
				{
					it->second->msg->FromNTCP2 ();
					HandleI2NPMsg (std::move (it->second->msg));
					m_IncompleteMessages.erase (it);
				}
				else
				{
					if (it->second->ConcatOutOfSequenceFragments ())
					{
						HandleI2NPMsg (std::move (it->second->msg));
						m_IncompleteMessages.erase (it);
					}
					else
						it->second->lastFragmentInsertTime = i2p::util::GetSecondsSinceEpoch ();
				}
				return;
			}
		}
		else
		{
			// follow-on fragment before first fragment
			if (m_IncompleteMessages.size () >= SSU2_MAX_NUM_INCOMPLETE_MESSAGES)
			{
				LogPrint (eLogWarning, "SSU2: Too many incomplete messages ", m_IncompleteMessages.size ());
				m_IsInvalidMessage = true;
				return;
			}
			auto msg = m_Server.GetIncompleteMessagesPool ().AcquireShared ();
			msg->nextFragmentNum = 0;
			it = m_IncompleteMessages.emplace (msgID, msg).first;
		}
		// insert new out of sequence fragment
		it->second->AddOutOfSequenceFragment (
			m_Server.GetFragmentsPool ().AcquireShared (buf + 5, len - 5, fragmentNum, isLast));
	}

	void SSU2Session::HandleRelayRequest (const uint8_t * buf, size_t len)
	{
		// we are Bob. Layout follows IntroductionManager.receiveRelayRequest():
		// flag, nonce, relay tag, timestamp, ver, asz, Alice endpoint, signature.
		// The block must carry at least the fixed fields and the flag.
		if (!IsEstablished ())
		{
			LogPrint (eLogWarning, "SSU2: RelayRequest in non-established state ", (int)m_State);
			return;
		}
		// flag(1) + nonce(4) + relay tag(4) + timestamp(4) + ver(1) + asz(1)
		if (len < 15)
		{
			LogPrint (eLogWarning, "SSU2: RelayRequest too short ", len);
			return;
		}
		auto mts = i2p::util::GetMillisecondsSinceEpoch ();
		auto now = i2p::util::GetSecondsSinceEpoch ();
		uint32_t nonce = bufbe32toh (buf + 1); // nonce
		uint32_t relayTag = bufbe32toh (buf + 5); // relay tag
		uint32_t timestamp = bufbe32toh (buf + 9); // seconds
		// Java rejects stale/future relay requests before doing any lookup.
		int64_t skew = (int64_t)timestamp - (int64_t)now;
		if (skew > SSU2_RELAY_MAX_SKEW || skew < -SSU2_RELAY_MAX_SKEW)
		{
			LogPrint (eLogWarning, "SSU2: RelayRequest timestamp skew ", skew, " seconds");
			return;
		}
		if (buf[13] != 2) // ver, IntroductionManager.java:452-457
		{
			LogPrint (eLogWarning, "SSU2: RelayRequest version ", (int)buf[13], " is not 2");
			return;
		}
		uint8_t asz = buf[14]; // 6 IPv4 or 18 IPv6
		if ((asz != 6 && asz != 18) || len < 15 + (size_t)asz)
		{
			LogPrint (eLogWarning, "SSU2: RelayRequest invalid endpoint size ", (int)asz);
			return;
		}
		auto alice = GetRemoteIdentity ();
		if (!alice)
		{
			LogPrint (eLogWarning, "SSU2: RelayRequest without Alice identity");
			return;
		}
		size_t sigLen = alice->GetSignatureLen ();
		if (len != 15 + (size_t)asz + sigLen)
		{
			LogPrint (eLogWarning, "SSU2: RelayRequest length ", len, " does not match endpoint/signature");
			return;
		}
		auto sendRelayResponse = [this, nonce, mts] (SSU2RelayResponseCode code)
		{
			auto packet = m_Server.GetSentPacketsPool ().AcquireShared ();
			packet->payloadSize = CreateAckBlock (packet->payload, m_MaxPayloadSize);
			packet->payloadSize += CreateRelayResponseBlock (packet->payload + packet->payloadSize,
				m_MaxPayloadSize - packet->payloadSize, code, nonce, 0, false);
			packet->payloadSize
				+= CreatePaddingBlock (packet->payload + packet->payloadSize, m_MaxPayloadSize - packet->payloadSize);
			uint32_t packetNum = SendData (packet->payload, packet->payloadSize);
			if (m_RemoteVersion >= SSU2_MIN_RELAY_RESPONSE_RESEND_VERSION)
			{
				// sometimes Alice doesn't ack this RelayResponse in older versions
				packet->sendTime = mts;
				m_SentPackets.emplace (packetNum, packet);
			}
		};
		auto session = m_Server.FindRelaySession (relayTag);
		if (!session)
		{
			LogPrint (eLogWarning, "SSU2: RelayRequest session with relay tag ", relayTag, " not found");
			sendRelayResponse (eSSU2RelayResponseCodeBobRelayTagNotFound);
			return;
		}
		// Recent relay nonces are bounded and drop duplicates. Java uses an
		// LHMCache(32) for this purpose (_recentRelaysAsBob).
		if (m_RecentRelayRequests.find (nonce) != m_RecentRelayRequests.end ())
		{
			LogPrint (eLogInfo, "SSU2: Duplicate RelayRequest nonce ", nonce, " ignored");
			return;
		}
		if (m_RecentRelayRequests.size () >= SSU2_MAX_RECENT_RELAY_REQUESTS)
		{
			auto oldest = m_RecentRelayRequests.begin ();
			for (auto it = m_RecentRelayRequests.begin (); it != m_RecentRelayRequests.end (); ++it)
				if (it->second < oldest->second) oldest = it;
			m_RecentRelayRequests.erase (oldest);
		}
		auto charlie = session->GetRemoteIdentity ();
		if (!charlie)
		{
			sendRelayResponse (eSSU2RelayResponseCodeBobUnspecified);
			return;
		}
		// RelayRequestData || bhash || chash || nonce, tag, timestamp, ver,
		// asz, Alice endpoint. Java validateSig(RELAY_REQUEST_PROLOGUE, ...).
		SignedData<192> s;
		s.Insert ((const uint8_t *)"RelayRequestData", 16); // prologue
		s.Insert (i2p::context.GetIdentHash (), 32); // bhash
		s.Insert (charlie->GetIdentHash (), 32); // chash
		s.Insert (buf + 1, 14 + (size_t)asz); // nonce, tag, timestamp, ver, asz, endpoint
		if (!s.Verify (alice, buf + 15 + asz))
		{
			LogPrint (eLogWarning, "SSU2: RelayRequest signature verification failed");
			sendRelayResponse (eSSU2RelayResponseCodeBobSignatureFailure);
			return;
		}
		// Record the nonce only after the signature and timestamp are valid.
		m_RecentRelayRequests.emplace (nonce, (uint32_t)now);
		if (session->m_RelaySessions.size () >= SSU2_MAX_RELAY_SESSIONS)
		{
			LogPrint (eLogWarning, "SSU2: Too many pending relay nonces for Charlie");
			sendRelayResponse (eSSU2RelayResponseCodeBobLimit);
			return;
		}
		if (session->m_RelaySessions.emplace (nonce, std::make_pair (shared_from_this (), mts / 1000)).second)
		{
			// send relay intro to Charlie
			auto r = i2p::data::netdb.FindRouter (alice->GetIdentHash ()); // Alice's RI
			if (r && (r->IsUnreachable () || !i2p::data::netdb.PopulateRouterInfoBuffer (r))) r = nullptr;
			if (!r) LogPrint (eLogWarning, "SSU2: RelayRequest Alice's router info not found");

			auto packet = m_Server.GetSentPacketsPool ().AcquireShared ();
			size_t riMaxLen = (len + 32 < m_MaxPayloadSize) ? m_MaxPayloadSize - len - 32 : 0;
			packet->payloadSize = r ? CreateRouterInfoBlock (packet->payload, riMaxLen, r) : 0;
			if (!packet->payloadSize && r) session->SendFragmentedMessage (CreateDatabaseStoreMsg (r));
			packet->payloadSize += CreateRelayIntroBlock (
				packet->payload + packet->payloadSize, m_MaxPayloadSize - packet->payloadSize, buf + 1, len - 1);
			if (packet->payloadSize < m_MaxPayloadSize)
				packet->payloadSize += CreatePaddingBlock (
					packet->payload + packet->payloadSize, m_MaxPayloadSize - packet->payloadSize);
			uint32_t packetNum = session->SendData (packet->payload, packet->payloadSize);
			packet->sendTime = mts;
			// Charlie always responds with RelayResponse
			session->m_SentPackets.emplace (packetNum, packet);
		}
		else
			LogPrint (eLogInfo, "SSU2: Relay request nonce ", nonce, " already exists. Ignore");
	}

	void SSU2Session::HandleRelayIntro (const uint8_t * buf, size_t len, int attempts)
	{
		// we are Charlie
		if (!IsEstablished ())
		{
			LogPrint (eLogWarning, "SSU2: RelayIntro in non-established state ", (int)m_State);
			return;
		}
		// flag, Alice hash, nonce, tag, timestamp, ver, asz, endpoint, signature
		if (len < 47) return;
		uint32_t timestamp = bufbe32toh (buf + 41);
		int64_t skew = (int64_t)timestamp - (int64_t)i2p::util::GetSecondsSinceEpoch ();
		if (skew > SSU2_RELAY_MAX_SKEW || skew < -SSU2_RELAY_MAX_SKEW)
		{
			LogPrint (eLogWarning, "SSU2: RelayIntro timestamp skew ", skew, " seconds");
			return;
		}
		if (buf[45] != 2) // IntroductionManager.java:773-783
		{
			LogPrint (eLogWarning, "SSU2: RelayIntro version ", (int)buf[45], " is not 2");
			return;
		}
		uint8_t asz = buf[46]; // 6 IPv4 or 18 IPv6
		if (asz != 6 && asz != 18)
		{
			LogPrint (eLogWarning, "SSU2: RelayIntro invalid endpoint size ", (int)asz);
			return;
		}
		SSU2RelayResponseCode code = eSSU2RelayResponseCodeAccept;
		boost::asio::ip::udp::endpoint ep;
		std::shared_ptr<const i2p::data::RouterInfo::Address> addr;
		auto r = i2p::data::netdb.FindRouter (buf + 1); // Alice
		if (r)
		{
			size_t sigLen = r->GetIdentity ()->GetSignatureLen ();
			if (len != 47 + (size_t)asz + sigLen)
			{
				LogPrint (eLogWarning, "SSU2: Malformed RelayIntro len=", len);
				return;
			}
			auto bob = GetRemoteIdentity ();
			if (!bob)
			{
				LogPrint (eLogWarning, "SSU2: RelayIntro without Bob identity");
				return;
			}
			SignedData<192> s;
			s.Insert ((const uint8_t *)"RelayRequestData", 16); // prologue
			s.Insert (bob->GetIdentHash (), 32); // bhash
			s.Insert (i2p::context.GetIdentHash (), 32); // chash
			s.Insert (buf + 33, 14); // nonce, relay tag, timestamp, ver, asz
			s.Insert (buf + 47, asz); // Alice Port, Alice IP
			if (s.Verify (r->GetIdentity (), buf + 47 + asz))
			{
				// obtain and check endpoint and address for HolePunch
				if (ExtractEndpoint (buf + 47, asz, ep))
				{
					if (!ep.address ().is_unspecified () && ep.port ())
					{
						if (m_Server.IsSupported (ep.address ()))
						{
							addr = ep.address ().is_v6 () ? r->GetSSU2V6Address () : r->GetSSU2V4Address ();
							if (!addr)
							{
								LogPrint (eLogWarning, "SSU2: RelayIntro address for endpoint not found");
								code = eSSU2RelayResponseCodeCharlieAliceIsUnknown;
							}
						}
						else
						{
							LogPrint (eLogWarning, "SSU2: RelayIntro unsupported address");
							code = eSSU2RelayResponseCodeCharlieUnsupportedAddress;
						}
					}
					else
					{
						LogPrint (eLogWarning, "SSU2: RelayIntro invalid endpoint");
						code = eSSU2RelayResponseCodeCharlieAliceIsUnknown;
					}
				}
				else
				{
					LogPrint (eLogWarning, "SSU2: RelayIntro can't extract endpoint");
					code = eSSU2RelayResponseCodeCharlieAliceIsUnknown;
				}
			}
			else
			{
				LogPrint (eLogWarning, "SSU2: RelayIntro signature verification failed");
				code = eSSU2RelayResponseCodeCharlieSignatureFailure;
			}
		}
		else if (!attempts)
		{
			// RouterInfo might come in the next packet, try again
			auto vec = std::make_shared<std::vector<uint8_t>> (len);
			memcpy (vec->data (), buf, len);
			auto s = shared_from_this ();
			boost::asio::post (m_Server.GetService (),
				[s, vec, attempts] ()
				{
					LogPrint (eLogDebug, "SSU2: RelayIntro attempt ", attempts + 1);
					s->HandleRelayIntro (vec->data (), vec->size (), attempts + 1);
				});
			return;
		}
		else
		{
			LogPrint (eLogWarning, "SSU2: RelayIntro unknown router to introduce");
			code = eSSU2RelayResponseCodeCharlieAliceIsUnknown;
		}
		// send relay response to Bob
		auto packet = m_Server.GetSentPacketsPool ().AcquireShared ();
		uint32_t nonce = bufbe32toh (buf + 33);
		packet->payloadSize = CreateRelayResponseBlock (
			packet->payload, m_MaxPayloadSize, code, nonce, m_Server.GetIncomingToken (ep), ep.address ().is_v4 ());
		if (code == eSSU2RelayResponseCodeAccept && addr)
		{
			// send HolePunch
			auto holePunchSession = std::make_shared<SSU2HolePunchSession> (m_Server, nonce, ep, addr);
			if (m_Server.AddSession (holePunchSession))
				holePunchSession->SendHolePunch (packet->payload, packet->payloadSize); // relay response block
			else
			{
				LogPrint (eLogInfo, "SSU2: Relay intro nonce ", nonce, " already exists. Ignore");
				return;
			}
		}
		packet->payloadSize
			+= CreatePaddingBlock (packet->payload + packet->payloadSize, m_MaxPayloadSize - packet->payloadSize);
		uint32_t packetNum = SendData (packet->payload, packet->payloadSize);
		if (m_RemoteVersion >= SSU2_MIN_RELAY_RESPONSE_RESEND_VERSION)
		{
			// sometimes Bob doesn't ack this RelayResponse in older versions
			packet->sendTime = i2p::util::GetMillisecondsSinceEpoch ();
			m_SentPackets.emplace (packetNum, packet);
		}
	}

	void SSU2Session::HandleRelayResponse (const uint8_t * buf, size_t len)
	{
		// flag, status, nonce, timestamp, ver, csz, endpoint, signature[, token].
		// IntroductionManager.receiveRelayResponse() validates time/version for
		// every path before it looks up the nonce.
		if (!IsEstablished () && m_State != eSSU2SessionStateIntroduced)
		{
			LogPrint (eLogWarning, "SSU2: RelayResponse in invalid state ", (int)m_State);
			return;
		}
		if (m_IsInvalidMessage)
		{
			LogPrint (eLogWarning, "SSU2: RelayResponse after invalid block");
			return;
		}
		if (len < 12)
		{
			LogPrint (eLogWarning, "SSU2: RelayResponse too short ", len);
			return;
		}
		uint32_t nonce = bufbe32toh (buf + 2);
		uint32_t timestamp = bufbe32toh (buf + 6);
		int64_t skew = (int64_t)timestamp - (int64_t)i2p::util::GetSecondsSinceEpoch ();
		if (skew > SSU2_RELAY_MAX_SKEW || skew < -SSU2_RELAY_MAX_SKEW)
		{
			LogPrint (eLogWarning, "SSU2: RelayResponse timestamp skew ", skew, " seconds");
			return;
		}
		if (buf[10] != 2) // ver, IntroductionManager.java:919-929
		{
			LogPrint (eLogWarning, "SSU2: RelayResponse version ", (int)buf[10], " is not 2");
			return;
		}
		if (m_State == eSSU2SessionStateIntroduced)
		{
			// HolePunch from Charlie. The nonce must occupy both halves of the
			// connection ID we used for the introduction.
			if (htobe64 (((uint64_t)nonce << 32) | nonce) != m_SourceConnID)
			{
				LogPrint (eLogWarning, "SSU2: Relay response nonce mismatch ", nonce, " connID=", m_SourceConnID);
				return;
			}
			if (buf[1] != eSSU2RelayResponseCodeAccept)
			{
				LogPrint (eLogWarning, "SSU2: HolePunch response code ", (int)buf[1]);
				return;
			}
			uint8_t csz = buf[11];
			if (csz != 6 && csz != 18)
			{
				LogPrint (eLogWarning, "SSU2: HolePunch invalid endpoint size ", (int)csz);
				return;
			}
			auto charlie = GetRemoteIdentity ();
			if (!charlie)
			{
				LogPrint (eLogWarning, "SSU2: HolePunch without Charlie identity");
				return;
			}
			size_t sigLen = charlie->GetSignatureLen ();
			if (len < 2 + 10 + (size_t)csz + sigLen + 8)
			{
				LogPrint (eLogWarning, "SSU2: HolePunch too short for signature/token ", len);
				return;
			}
			size_t signedLen = len - 2 - sigLen - 8; // token is not signed
			if (signedLen < 10 + (size_t)csz)
			{
				LogPrint (eLogWarning, "SSU2: HolePunch signed data too short ", signedLen);
				return;
			}
			// Charlie signs with the introducer's hash in the prologue. Try the
			// introducers advertised in Charlie's RouterInfo, like Java does.
			bool verified = false;
			auto address = GetAddress ();
			if (address && address->ssu)
			{
				for (const auto & introducer : address->ssu->introducers)
				{
					SignedData<192> s;
					s.Insert ((const uint8_t *)"RelayAgreementOK", 16); // prologue
					s.Insert (introducer.iH, 32); // bob hash
					s.Insert (buf + 2, signedLen);
					if (s.Verify (charlie, buf + 2 + signedLen))
					{
						verified = true;
						break;
					}
				}
			}
			if (!verified)
			{
				LogPrint (eLogWarning, "SSU2: HolePunch signature verification failed");
				return;
			}
			// new token follows the signature
			uint64_t token;
			memcpy (&token, buf + 2 + signedLen + sigLen, 8);
			m_Server.UpdateOutgoingToken (
				m_RemoteEndpoint, token, i2p::util::GetSecondsSinceEpoch () + SSU2_TOKEN_EXPIRATION_TIMEOUT);
			ConnectAfterIntroduction ();
			return;
		}
		auto it = m_RelaySessions.find (nonce);
		if (it != m_RelaySessions.end ())
		{
			auto relaySession = it->second.first;
			m_RelaySessions.erase (it);
			if (relaySession && relaySession->IsEstablished ())
			{
				// we are Bob, message from Charlie
				size_t maxPayloadSize = relaySession->GetMaxPayloadSize ();
				if (len + 3 > maxPayloadSize)
				{
					LogPrint (eLogWarning, "SSU2: RelayResponse block size ", len, " is too long");
					return;
				}
				auto packet = m_Server.GetSentPacketsPool ().AcquireShared ();
				uint8_t * payload = packet->payload;
				payload[0] = eSSU2BlkRelayResponse;
				htobe16buf (payload + 1, len);
				memcpy (payload + 3, buf, len); // forward to Alice as is
				packet->payloadSize = len + 3;
				packet->payloadSize
					+= CreatePaddingBlock (payload + packet->payloadSize, maxPayloadSize - packet->payloadSize);
				uint32_t packetNum = relaySession->SendData (packet->payload, packet->payloadSize);
				if (m_RemoteVersion >= SSU2_MIN_RELAY_RESPONSE_RESEND_VERSION)
				{
					// sometimes Alice doesn't ack this RelayResponse in older versions
					packet->sendTime = i2p::util::GetMillisecondsSinceEpoch ();
					relaySession->m_SentPackets.emplace (packetNum, packet);
				}
			}
			else if (relaySession)
			{
				// we are Alice, message from Bob
				int code = buf[1];
				auto bob = GetRemoteIdentity ();
				auto charlie = relaySession->GetRemoteIdentity ();
				// code 1..63 is signed by Bob, 0 and >=64 by Charlie.
				auto signer = ((code > 0 && code < 64) ? bob : charlie);
				if (!bob || !signer)
				{
					LogPrint (eLogWarning, "SSU2: RelayResponse signer identity is unknown");
					relaySession->Done ();
					return;
				}
				size_t sigLen = signer->GetSignatureLen ();
				size_t signedLen;
				if (code == eSSU2RelayResponseCodeAccept)
				{
					uint8_t csz = buf[11];
					if (csz != 6 && csz != 18)
					{
						LogPrint (eLogWarning, "SSU2: RelayResponse invalid endpoint size ", (int)csz);
						relaySession->Done ();
						return;
					}
					if (len != 2 + 10 + (size_t)csz + sigLen + 8)
					{
						LogPrint (eLogWarning, "SSU2: Malformed RelayResponse len=", len);
						relaySession->Done ();
						return;
					}
					signedLen = 10 + (size_t)csz;
				}
				else
				{
					if (len < 2 + 10 + sigLen)
					{
						LogPrint (eLogWarning, "SSU2: Malformed RelayResponse rejection len=", len);
						relaySession->Done ();
						return;
					}
					signedLen = len - 2 - sigLen;
				}
				SignedData<192> s;
				s.Insert ((const uint8_t *)"RelayAgreementOK", 16); // prologue
				s.Insert (bob->GetIdentHash (), 32); // bhash
				s.Insert (buf + 2, signedLen); // nonce, timestamp, ver, csz and endpoint
				if (!s.Verify (signer, buf + 2 + signedLen))
				{
					LogPrint (eLogWarning, "SSU2: RelayResponse signature verification failed");
					relaySession->Done ();
					return;
				}
				if (code == eSSU2RelayResponseCodeAccept)
				{
					uint8_t csz = buf[11];
					if (relaySession->m_State == eSSU2SessionStateIntroduced) // HolePunch not received yet
					{
						// update Charlie's endpoint
						if (ExtractEndpoint (buf + 12, csz, relaySession->m_RemoteEndpoint))
						{
							// update token, it follows the signature
							uint64_t token;
							memcpy (&token, buf + 2 + signedLen + sigLen, 8);
							m_Server.UpdateOutgoingToken (relaySession->m_RemoteEndpoint, token,
								i2p::util::GetSecondsSinceEpoch () + SSU2_TOKEN_EXPIRATION_TIMEOUT);
							// connect to Charlie, HolePunch will be ignored
							relaySession->ConnectAfterIntroduction ();
						}
						else
							LogPrint (eLogWarning, "SSU2: RelayResponse can't extract endpoint");
					}
				}
				else
				{
					LogPrint (eLogInfo, "SSU2: RelayResponse status code=", code, " nonce=", nonce);
					relaySession->Done ();
				}
			}
			else
				LogPrint (eLogDebug, "SSU2: RelayResponse unknown session for nonce ", nonce);
		}
		else
			LogPrint (eLogDebug, "SSU2: RelayResponse unknown nonce ", nonce);
	}

	void SSU2Session::HandlePeerTest (const uint8_t * buf, size_t len)
	{
		// msgs 1-4
		if (len < 3) return;
		uint8_t msg = buf[0];
		size_t offset = 3; // points to signed data
		if (msg == 2 || msg == 4) offset += 32; // hash is presented for msg 2 and 4 only
		if (len < offset + 5) return;
		auto ts = i2p::util::GetMillisecondsSinceEpoch ();
		uint32_t nonce = bufbe32toh (buf + offset + 1);
		switch (msg) // msg
		{
		case 1: // Bob from Alice
		{
			if (len < 13) return;
			auto session = m_Server.GetRandomPeerTestSession (
				(buf[12] == 6) ? i2p::data::RouterInfo::eSSU2V4 : i2p::data::RouterInfo::eSSU2V6,
				GetRemoteIdentity ()->GetIdentHash ());
			if (session) // session with Charlie
			{
				if (m_Server.AddPeerTest (nonce, shared_from_this (), ts / 1000))
				{
					auto packet = m_Server.GetSentPacketsPool ().AcquireShared ();
					// Alice's RouterInfo
					auto r = i2p::data::netdb.FindRouter (GetRemoteIdentity ()->GetIdentHash ());
					if (r && (r->IsUnreachable () || !i2p::data::netdb.PopulateRouterInfoBuffer (r))) r = nullptr;
					size_t riMaxLen = (len + 32 < m_MaxPayloadSize) ? m_MaxPayloadSize - len - 32 : 0;
					packet->payloadSize = r ? CreateRouterInfoBlock (packet->payload, riMaxLen, r) : 0;
					if (!packet->payloadSize && r) session->SendFragmentedMessage (CreateDatabaseStoreMsg (r));
					if (packet->payloadSize + len + 48 > m_MaxPayloadSize)
					{
						// doesn't fit one message, send RouterInfo in separate message
						uint32_t packetNum = session->SendData (
							packet->payload, packet->payloadSize, SSU2_FLAG_IMMEDIATE_ACK_REQUESTED);
						packet->sendTime = ts;
						session->m_SentPackets.emplace (packetNum, packet);
						packet = m_Server.GetSentPacketsPool ().AcquireShared (); // new packet
					}
					// PeerTest to Charlie
					packet->payloadSize += CreatePeerTestBlock (packet->payload + packet->payloadSize,
						m_MaxPayloadSize - packet->payloadSize, 2, eSSU2PeerTestCodeAccept,
						GetRemoteIdentity ()->GetIdentHash (), buf + offset, len - offset);
					packet->payloadSize += CreatePaddingBlock (
						packet->payload + packet->payloadSize, m_MaxPayloadSize - packet->payloadSize);
					uint32_t packetNum
						= session->SendData (packet->payload, packet->payloadSize, SSU2_FLAG_IMMEDIATE_ACK_REQUESTED);
					packet->sendTime = ts;
					session->m_SentPackets.emplace (packetNum, packet);
				}
				else
					LogPrint (eLogInfo, "SSU2: Peer test 1 nonce ", nonce, " already exists. Ignored");
			}
			else
			{
				// Charlie not found, send error back to Alice
				auto packet = m_Server.GetSentPacketsPool ().AcquireShared ();
				uint8_t zeroHash[32] = { 0 };
				packet->payloadSize = CreatePeerTestBlock (packet->payload, m_MaxPayloadSize, 4,
					eSSU2PeerTestCodeBobNoCharlieAvailable, zeroHash, buf + offset, len - offset);
				packet->payloadSize += CreatePaddingBlock (
					packet->payload + packet->payloadSize, m_MaxPayloadSize - packet->payloadSize);
				uint32_t packetNum = SendData (packet->payload, packet->payloadSize);
				packet->sendTime = ts;
				m_SentPackets.emplace (packetNum, packet);
			}
			break;
		}
		case 2: // Charlie from Bob
		{
			// sign with Charlie's key
			if (len < offset + 10) return;
			uint8_t asz = buf[offset + 9];
			size_t l = asz + 10 + i2p::context.GetIdentity ()->GetSignatureLen ();
			if (len < offset + l) return;
			std::vector<uint8_t> newSignedData (l);
			memcpy (newSignedData.data (), buf + offset, asz + 10);
			SignedData<128> s;
			s.Insert ((const uint8_t *)"PeerTestValidate", 16); // prologue
			s.Insert (GetRemoteIdentity ()->GetIdentHash (), 32); // bhash
			s.Insert (buf + 3, 32); // ahash
			s.Insert (newSignedData.data (), asz + 10); // ver, nonce, ts, asz, Alice's endpoint
			s.Sign (i2p::context.GetPrivateKeys (), newSignedData.data () + 10 + asz);
			// send response (msg 3) back and msg 5 if accepted
			SSU2PeerTestCode code = eSSU2PeerTestCodeAccept;
			auto r = i2p::data::netdb.FindRouter (buf + 3); // find Alice
			if (r)
			{
				size_t signatureLen = r->GetIdentity ()->GetSignatureLen ();
				if (len >= offset + asz + 10 + signatureLen)
				{
					s.Reset ();
					s.Insert ((const uint8_t *)"PeerTestValidate", 16); // prologue
					s.Insert (GetRemoteIdentity ()->GetIdentHash (), 32); // bhash
					s.Insert (buf + offset, asz + 10); // signed data
					if (s.Verify (r->GetIdentity (), buf + offset + asz + 10))
					{
						if (!m_Server.FindSession (r->GetIdentity ()->GetIdentHash ()))
						{
							boost::asio::ip::udp::endpoint ep;
							std::shared_ptr<const i2p::data::RouterInfo::Address> addr;
							if (ExtractEndpoint (buf + offset + 10, asz, ep) && !ep.address ().is_unspecified ()
								&& ep.port ())
								addr = r->GetSSU2Address (ep.address ().is_v4 ());
							if (addr && m_Server.IsSupported (ep.address ())
								&& i2p::context.GetRouterInfo ().IsSSU2PeerTesting (ep.address ().is_v4 ()))
							{
								if (!m_Server.IsConnectedRecently (ep)) // no alive hole punch
								{
									// send msg 5 to Alice
									auto session = std::make_shared<SSU2PeerTestSession> (
										m_Server, 0, htobe64 (((uint64_t)nonce << 32) | nonce));
									session->m_RemoteEndpoint = ep; // might be different
									m_Server.AddSession (session);
									session->SendPeerTest (5, newSignedData.data (), newSignedData.size (), addr);
								}
								else
									code = eSSU2PeerTestCodeCharlieAliceIsAlreadyConnected;
							}
							else
								code = eSSU2PeerTestCodeCharlieUnsupportedAddress;
						}
						else
							code = eSSU2PeerTestCodeCharlieAliceIsAlreadyConnected;
					}
					else
						code = eSSU2PeerTestCodeCharlieSignatureFailure;
				}
				else // malformed message
					code = eSSU2PeerTestCodeCharlieReasonUnspecified;
			}
			else
				code = eSSU2PeerTestCodeCharlieAliceIsUnknown;
			// send msg 3 back to Bob
			auto packet = m_Server.GetSentPacketsPool ().AcquireShared ();
			packet->payloadSize = CreatePeerTestBlock (
				packet->payload, m_MaxPayloadSize, 3, code, nullptr, newSignedData.data (), newSignedData.size ());
			packet->payloadSize
				+= CreatePaddingBlock (packet->payload + packet->payloadSize, m_MaxPayloadSize - packet->payloadSize);
			uint32_t packetNum = SendData (packet->payload, packet->payloadSize);
			packet->sendTime = ts;
			m_SentPackets.emplace (packetNum, packet);
			break;
		}
		case 3: // Bob from Charlie
		{
			auto aliceSession = m_Server.GetPeerTest (nonce);
			if (aliceSession && aliceSession->IsEstablished ())
			{
				auto packet = m_Server.GetSentPacketsPool ().AcquireShared ();
				// Charlie's RouterInfo
				auto r = i2p::data::netdb.FindRouter (GetRemoteIdentity ()->GetIdentHash ());
				if (r && (r->IsUnreachable () || !i2p::data::netdb.PopulateRouterInfoBuffer (r))) r = nullptr;
				size_t riMaxLen = (len + 32 < m_MaxPayloadSize) ? m_MaxPayloadSize - len - 32 : 0;
				packet->payloadSize = r ? CreateRouterInfoBlock (packet->payload, riMaxLen, r) : 0;
				if (!packet->payloadSize && r) aliceSession->SendFragmentedMessage (CreateDatabaseStoreMsg (r));
				if (packet->payloadSize + len + 16 > m_MaxPayloadSize)
				{
					// doesn't fit one message, send RouterInfo in separate message
					uint32_t packetNum = aliceSession->SendData (packet->payload, packet->payloadSize);
					packet->sendTime = ts;
					aliceSession->m_SentPackets.emplace (packetNum, packet);
					packet = m_Server.GetSentPacketsPool ().AcquireShared ();
				}
				// PeerTest to Alice
				packet->payloadSize += CreatePeerTestBlock (packet->payload + packet->payloadSize, m_MaxPayloadSize, 4,
					(SSU2PeerTestCode)buf[1], GetRemoteIdentity ()->GetIdentHash (), buf + offset, len - offset);
				if (packet->payloadSize < m_MaxPayloadSize)
					packet->payloadSize += CreatePaddingBlock (
						packet->payload + packet->payloadSize, m_MaxPayloadSize - packet->payloadSize);
				uint32_t packetNum = aliceSession->SendData (packet->payload, packet->payloadSize);
				packet->sendTime = ts;
				aliceSession->m_SentPackets.emplace (packetNum, packet);
			}
			else
				LogPrint (eLogDebug, "SSU2: Unknown peer test 3 nonce ", nonce);
			break;
		}
		case 4: // Alice from Bob
		{
			auto session = m_Server.GetRequestedPeerTest (nonce);
			if (session)
			{
				if (buf[1] == eSSU2PeerTestCodeAccept)
				{
					if (GetRouterStatus () == eRouterStatusUnknown) SetTestingState (true);
					auto r = i2p::data::netdb.FindRouter (buf + 3); // find Charlie
					if (r && len >= offset + 10)
					{
						uint8_t asz = buf[offset + 9];
						if (len < offset + asz + 10 + r->GetIdentity ()->GetSignatureLen ())
						{
							LogPrint (eLogWarning, "Malformed PeerTest 4 len=", len);
							session->Done ();
							return;
						}
						SignedData<128> s;
						s.Insert ((const uint8_t *)"PeerTestValidate", 16); // prologue
						s.Insert (GetRemoteIdentity ()->GetIdentHash (), 32); // bhash
						s.Insert (i2p::context.GetIdentity ()->GetIdentHash (), 32); // ahash
						s.Insert (buf + offset, asz + 10); // ver, nonce, ts, asz, Alice's endpoint
						if (s.Verify (r->GetIdentity (), buf + offset + asz + 10))
						{
							session->SetRemoteIdentity (r->GetIdentity ());
							auto addr = r->GetSSU2Address (m_Address->IsV4 ());
							if (addr && addr->IsPeerTesting ())
							{
								if (session->GetMsgNumReceived () >= 5)
								{
									// msg 5 already received and we know remote endpoint
									if (session->GetMsgNumReceived () == 5)
									{
										if (!session->IsConnectedRecently ()) SetRouterStatus (eRouterStatusOK);
										// send msg 6 immediately
										session->SendPeerTest (6, buf + offset, len - offset, addr);
									}
									else
										LogPrint (eLogWarning, "SSU2: PeerTest 4 received, but msg ",
											session->GetMsgNumReceived (), " already received");
								}
								else
								{
									session->m_Address = addr;
									if (GetTestingState ())
									{
										// schedule msg 6 with delay
										if (!addr->host.is_unspecified () && addr->port)
										{
											session->SetRemoteEndpoint (
												boost::asio::ip::udp::endpoint (addr->host, addr->port));
											session->SendPeerTest (6, buf + offset, len - offset, addr, true);
										}
										SetTestingState (false);
										if (i2p::context.GetError () == eRouterErrorFullConeNAT)
											SetRouterStatus (eRouterStatusOK);
										else if (GetRouterStatus () != eRouterStatusFirewalled
											&& addr->IsPeerTesting ())
										{
											SetRouterStatus (eRouterStatusFirewalled);
											session->SetStatusChanged ();
											if (m_Address->IsV4 ())
												m_Server.RescheduleIntroducersUpdateTimer ();
											else
												m_Server.RescheduleIntroducersUpdateTimerV6 ();
										}
									}
								}
								LogPrint (eLogDebug, "SSU2: Peer test 4 received from ",
									i2p::data::GetIdentHashAbbreviation (GetRemoteIdentity ()->GetIdentHash ()),
									" with information about ",
									i2p::data::GetIdentHashAbbreviation (i2p::data::IdentHash (buf + 3)));
							}
							else
							{
								LogPrint (eLogWarning, "SSU2: Peer test 4 address not found or not supported");
								session->Done ();
							}
						}
						else
						{
							LogPrint (eLogWarning, "SSU2: Peer test 4 signature verification failed");
							session->Done ();
						}
					}
					else
					{
						LogPrint (eLogWarning, "SSU2: Peer test 4 router not found");
						session->Done ();
					}
				}
				else
				{
					LogPrint (eLogInfo, "SSU2: Peer test 4 error code ", (int)buf[1], " from ",
						i2p::data::GetIdentHashAbbreviation (
							buf[1] < 64 ? GetRemoteIdentity ()->GetIdentHash () : i2p::data::IdentHash (buf + 3)));
					if (GetTestingState () && GetRouterStatus () != eRouterStatusFirewalled)
						SetRouterStatus (eRouterStatusUnknown);
					session->Done ();
				}
			}
			else
				LogPrint (eLogDebug, "SSU2: Unknown peer test 4 nonce ", nonce);
			break;
		}
		default: LogPrint (eLogWarning, "SSU2: PeerTest unexpected msg num ", buf[0]);
		}
	}

	void SSU2Session::HandleI2NPMsg (std::shared_ptr<I2NPMessage> && msg)
	{
		if (!msg) return;
		uint32_t msgID = msg->GetMsgID ();
		// m_LastActivityTimestamp is updated in ProcessData before
		if (!msg->IsExpired (GetLastActivityTimestamp () * 1000LL)) // to milliseconds
		{
			if (m_ReceivedI2NPMsgIDs.emplace (msgID, (uint32_t)GetLastActivityTimestamp ()).second)
				m_Handler.PutNextMessage (std::move (msg));
			else
				LogPrint (eLogDebug, "SSU2: Message ", msgID, " already received");
		}
		else
			LogPrint (eLogDebug, "SSU2: Message ", msgID, " expired");
	}

	bool SSU2Session::ExtractEndpoint (const uint8_t * buf, size_t size, boost::asio::ip::udp::endpoint & ep)
	{
		if (size < 2) return false;
		int port = bufbe16toh (buf);
		if (size == 6)
		{
			boost::asio::ip::address_v4::bytes_type bytes;
			memcpy (bytes.data (), buf + 2, 4);
			ep = boost::asio::ip::udp::endpoint (boost::asio::ip::address_v4 (bytes), port);
		}
		else if (size == 18)
		{
			boost::asio::ip::address_v6::bytes_type bytes;
			memcpy (bytes.data (), buf + 2, 16);
			ep = boost::asio::ip::udp::endpoint (boost::asio::ip::address_v6 (bytes), port);
		}
		else
		{
			LogPrint (eLogWarning, "SSU2: Address size ", int (size), " is not supported");
			return false;
		}
		return true;
	}

	size_t SSU2Session::CreateEndpoint (uint8_t * buf, size_t len, const boost::asio::ip::udp::endpoint & ep)
	{
		if (len < 6) return 0;
		htobe16buf (buf, ep.port ());
		size_t size = 0;
		if (ep.address ().is_v4 ())
		{
			memcpy (buf + 2, ep.address ().to_v4 ().to_bytes ().data (), 4);
			size = 6;
		}
		else if (ep.address ().is_v6 ())
		{
			if (len < 18) return 0;
			memcpy (buf + 2, ep.address ().to_v6 ().to_bytes ().data (), 16);
			size = 18;
		}
		else
		{
			LogPrint (eLogWarning, "SSU2: Wrong address type ", ep.address ().to_string ());
			return 0;
		}
		return size;
	}

	std::shared_ptr<const i2p::data::RouterInfo::Address> SSU2Session::FindLocalAddress () const
	{
		if (m_Address)
			return i2p::context.GetRouterInfo ().GetSSU2Address (m_Address->IsV4 ());
		else if (!m_RemoteEndpoint.address ().is_unspecified ())
			return i2p::context.GetRouterInfo ().GetSSU2Address (m_RemoteEndpoint.address ().is_v4 ());
		return nullptr;
	}

	void SSU2Session::AdjustMaxPayloadSize (size_t maxMtu)
	{
		auto addr = FindLocalAddress ();
		if (addr && addr->ssu)
		{
			int mtu = addr->ssu->mtu;
			if (!mtu) mtu = SSU2_MAX_PACKET_SIZE;
			if (mtu > (int)maxMtu) mtu = maxMtu;
			if (m_Address && m_Address->ssu && m_Address->ssu->mtu && (!mtu || m_Address->ssu->mtu < mtu))
				mtu = m_Address->ssu->mtu;
			if (mtu)
			{
				if (mtu > (int)SSU2_MAX_PACKET_SIZE) mtu = SSU2_MAX_PACKET_SIZE;
				if (mtu < (int)SSU2_MIN_PACKET_SIZE) mtu = SSU2_MIN_PACKET_SIZE;
				m_MaxPayloadSize = mtu - (addr->IsV6 () ? IPV6_HEADER_SIZE : IPV4_HEADER_SIZE) - UDP_HEADER_SIZE - 32;
				LogPrint (eLogDebug, "SSU2: Session MTU=", mtu, ", max payload size=", m_MaxPayloadSize);
			}
		}
	}

	RouterStatus SSU2Session::GetRouterStatus () const
	{
		if (m_Address)
		{
			if (m_Address->IsV4 ()) return i2p::context.GetStatus ();
			if (m_Address->IsV6 ()) return i2p::context.GetStatusV6 ();
		}
		return eRouterStatusUnknown;
	}

	void SSU2Session::SetRouterStatus (RouterStatus status) const
	{
		if (m_Address)
		{
			if (m_Address->IsV4 ())
				i2p::context.SetStatus (status);
			else if (m_Address->IsV6 ())
				i2p::context.SetStatusV6 (status);
		}
	}

	bool SSU2Session::GetTestingState () const
	{
		if (m_Address)
		{
			if (m_Address->IsV4 ()) return i2p::context.GetTesting ();
			if (m_Address->IsV6 ()) return i2p::context.GetTestingV6 ();
		}
		return false;
	}

	void SSU2Session::SetTestingState (bool testing) const
	{
		if (m_Address)
		{
			if (m_Address->IsV4 ())
				i2p::context.SetTesting (testing);
			else if (m_Address->IsV6 ())
				i2p::context.SetTestingV6 (testing);
		}
		if (!testing) m_Server.AdjustTimeOffset (0, nullptr); // reset time offset when testing is over
	}

	size_t SSU2Session::CreateAddressBlock (uint8_t * buf, size_t len, const boost::asio::ip::udp::endpoint & ep)
	{
		if (len < 9) return 0;
		buf[0] = eSSU2BlkAddress;
		size_t size = CreateEndpoint (buf + 3, len - 3, ep);
		if (!size) return 0;
		htobe16buf (buf + 1, size);
		return size + 3;
	}

	size_t SSU2Session::CreateRouterInfoBlock (
		uint8_t * buf, size_t len, std::shared_ptr<const i2p::data::RouterInfo> r)
	{
		if (!r || len < 5) return 0;
		return CreateRouterInfoBlock (buf, len, r->GetSharedBuffer ());
	}

	size_t SSU2Session::CreateRouterInfoBlock (
		uint8_t * buf, size_t len, std::shared_ptr<const i2p::data::RouterInfo::Buffer> riBuffer)
	{
		if (!riBuffer || len < 5) return 0;
		buf[0] = eSSU2BlkRouterInfo;
		size_t size = riBuffer->GetBufferLen ();
		if (size + 5 < len)
		{
			memcpy (buf + 5, riBuffer->data (), size);
			buf[3] = 0; // flag
		}
		else
		{
			i2p::data::GzipDeflator deflator;
			deflator.SetCompressionLevel (9);
			size = deflator.Deflate (riBuffer->data (), riBuffer->GetBufferLen (), buf + 5, len - 5);
			if (!size) return 0; // doesn't fit
			buf[3] = SSU2_ROUTER_INFO_FLAG_GZIP; // flag
		}
		htobe16buf (buf + 1, size + 2); // size
		buf[4] = 1; // frag
		return size + 5;
	}

	size_t SSU2Session::CreateAckBlock (uint8_t * buf, size_t len)
	{
		if (len < 8) return 0;
		buf[0] = eSSU2BlkAck;
		uint32_t ackThrough = m_OutOfSequencePackets.empty () ? m_ReceivePacketNum : *m_OutOfSequencePackets.rbegin ();
		htobe32buf (buf + 3, ackThrough); // Ack Through
		uint16_t acnt = 0;
		if (ackThrough)
		{
			if (m_OutOfSequencePackets.empty ())
			{
				acnt = std::min ((int)ackThrough, SSU2_MAX_NUM_ACNT); // no gaps
				m_NumRanges = 0;
			}
			else
			{
				auto it = m_OutOfSequencePackets.rbegin ();
				it++; // prev packet num
				while (it != m_OutOfSequencePackets.rend () && *it == ackThrough - acnt - 1)
				{
					acnt++;
					if (acnt >= SSU2_MAX_NUM_ACK_PACKETS)
						break;
					else
						it++;
				}
				// ranges
				if (!m_NumRanges)
				{
					int maxNumRanges = (len - 8) >> 1;
					if (maxNumRanges > SSU2_MAX_NUM_ACK_RANGES) maxNumRanges = SSU2_MAX_NUM_ACK_RANGES;
					int numRanges = 0;
					uint32_t lastNum = ackThrough - acnt;
					if (acnt > SSU2_MAX_NUM_ACNT)
					{
						auto d = std::div (acnt - SSU2_MAX_NUM_ACNT, SSU2_MAX_NUM_ACNT);
						acnt = SSU2_MAX_NUM_ACNT;
						if (d.quot > maxNumRanges)
						{
							d.quot = maxNumRanges;
							d.rem = 0;
						}
						// Acks only ranges for acnt
						for (int i = 0; i < d.quot; i++)
						{
							m_Ranges[numRanges * 2] = 0;
							m_Ranges[numRanges * 2 + 1] = SSU2_MAX_NUM_ACNT; // NACKs 0, Acks 255
							numRanges++;
						}
						if (d.rem > 0)
						{
							m_Ranges[numRanges * 2] = 0;
							m_Ranges[numRanges * 2 + 1] = d.rem;
							numRanges++;
						}
					}
					int numPackets = acnt + numRanges * SSU2_MAX_NUM_ACNT;
					while (it != m_OutOfSequencePackets.rend () && numRanges < maxNumRanges
						&& numPackets < SSU2_MAX_NUM_ACK_PACKETS)
					{
						if (lastNum - (*it) > SSU2_MAX_NUM_ACNT)
						{
							// NACKs only ranges
							if (lastNum > (*it) + SSU2_MAX_NUM_ACNT * (maxNumRanges - numRanges))
								break; // too many NACKs
							while (lastNum - (*it) > SSU2_MAX_NUM_ACNT)
							{
								m_Ranges[numRanges * 2] = SSU2_MAX_NUM_ACNT;
								m_Ranges[numRanges * 2 + 1] = 0; // NACKs 255, Acks 0
								lastNum -= SSU2_MAX_NUM_ACNT;
								numRanges++;
								numPackets += SSU2_MAX_NUM_ACNT;
							}
						}
						// NACKs and Acks ranges
						m_Ranges[numRanges * 2] = lastNum - (*it) - 1; // NACKs
						numPackets += m_Ranges[numRanges * 2];
						lastNum = *it;
						it++;
						int numAcks = 1;
						while (it != m_OutOfSequencePackets.rend () && lastNum > 0 && *it == lastNum - 1)
						{
							numAcks++;
							lastNum--;
							it++;
						}
						while (numAcks > SSU2_MAX_NUM_ACNT)
						{
							// Acks only ranges
							m_Ranges[numRanges * 2 + 1] = SSU2_MAX_NUM_ACNT; // Acks 255
							numAcks -= SSU2_MAX_NUM_ACNT;
							numRanges++;
							numPackets += SSU2_MAX_NUM_ACNT;
							m_Ranges[numRanges * 2] = 0; // NACKs 0
							if (numRanges >= maxNumRanges || numPackets >= SSU2_MAX_NUM_ACK_PACKETS) break;
						}
						if (numAcks > SSU2_MAX_NUM_ACNT) numAcks = SSU2_MAX_NUM_ACNT;
						m_Ranges[numRanges * 2 + 1] = (uint8_t)numAcks; // Acks
						numPackets += numAcks;
						numRanges++;
					}
					if (it == m_OutOfSequencePackets.rend () && numRanges < maxNumRanges
						&& numPackets < SSU2_MAX_NUM_ACK_PACKETS)
					{
						// add range between out-of-sequence and received
						int nacks = *m_OutOfSequencePackets.begin () - m_ReceivePacketNum - 1;
						if (nacks > 0)
						{
							if (nacks > SSU2_MAX_NUM_ACNT) nacks = SSU2_MAX_NUM_ACNT;
							m_Ranges[numRanges * 2] = nacks;
							m_Ranges[numRanges * 2 + 1] = std::min ((int)m_ReceivePacketNum + 1, SSU2_MAX_NUM_ACNT);
							numRanges++;
						}
					}
					m_NumRanges = numRanges;
				}
				if (m_NumRanges) memcpy (buf + 8, m_Ranges, m_NumRanges * 2);
			}
		}
		buf[7] = (uint8_t)acnt; // acnt
		htobe16buf (buf + 1, 5 + m_NumRanges * 2);
		return 8 + m_NumRanges * 2;
	}

	size_t SSU2Session::CreatePaddingBlock (uint8_t * buf, size_t len, size_t minSize)
	{
		if (len < 3 || len < minSize) return 0;
		size_t paddingSize = m_Server.GetRng () () & 0x1F; // 0 - 31
		if (paddingSize + 3 > len)
			paddingSize = len - 3;
		else if (paddingSize + 3 < minSize)
			paddingSize = minSize - 3;
		buf[0] = eSSU2BlkPadding;
		htobe16buf (buf + 1, paddingSize);
		memset (buf + 3, 0, paddingSize);
		return paddingSize + 3;
	}

	size_t SSU2Session::CreateI2NPBlock (uint8_t * buf, size_t len, std::shared_ptr<I2NPMessage> && msg)
	{
		msg->ToNTCP2 ();
		auto msgBuf = msg->GetNTCP2Header ();
		auto msgLen = msg->GetNTCP2Length ();
		if (msgLen + 3 > len) msgLen = len - 3;
		buf[0] = eSSU2BlkI2NPMessage;
		htobe16buf (buf + 1, msgLen); // size
		memcpy (buf + 3, msgBuf, msgLen);
		return msgLen + 3;
	}

	size_t SSU2Session::CreateFirstFragmentBlock (uint8_t * buf, size_t len, std::shared_ptr<I2NPMessage> msg)
	{
		if (len < 12) return 0;
		msg->ToNTCP2 ();
		auto msgBuf = msg->GetNTCP2Header ();
		auto msgLen = msg->GetNTCP2Length ();
		if (msgLen + 3 <= len) return 0;
		msgLen = len - 3;
		buf[0] = eSSU2BlkFirstFragment;
		htobe16buf (buf + 1, msgLen); // size
		memcpy (buf + 3, msgBuf, msgLen);
		msg->offset = (msgBuf - msg->buf) + msgLen;
		return msgLen + 3;
	}

	size_t SSU2Session::CreateFollowOnFragmentBlock (
		uint8_t * buf, size_t len, std::shared_ptr<I2NPMessage> msg, uint8_t & fragmentNum, uint32_t msgID)
	{
		if (len < 8) return 0;
		bool isLast = true;
		auto msgLen = msg->len - msg->offset;
		if (msgLen + 8 > len)
		{
			msgLen = len - 8;
			isLast = false;
		}
		buf[0] = eSSU2BlkFollowOnFragment;
		htobe16buf (buf + 1, msgLen + 5); // size
		fragmentNum++;
		buf[3] = fragmentNum << 1;
		if (isLast) buf[3] |= 0x01;
		memcpy (buf + 4, &msgID, 4);
		memcpy (buf + 8, msg->buf + msg->offset, msgLen);
		msg->offset += msgLen;
		return msgLen + 8;
	}

	size_t SSU2Session::CreateRelayIntroBlock (
		uint8_t * buf, size_t len, const uint8_t * introData, size_t introDataLen)
	{
		buf[0] = eSSU2BlkRelayIntro;
		size_t payloadSize = 1 /* flag */ + 32 /* Alice router hash */ + introDataLen;
		if (payloadSize + 3 > len) return 0;
		htobe16buf (buf + 1, payloadSize); // size
		buf[3] = 0; // flag
		memcpy (buf + 4, GetRemoteIdentity ()->GetIdentHash (), 32); // Alice router hash
		memcpy (buf + 36, introData, introDataLen);
		return payloadSize + 3;
	}

	size_t SSU2Session::CreateRelayResponseBlock (
		uint8_t * buf, size_t len, SSU2RelayResponseCode code, uint32_t nonce, uint64_t token, bool v4)
	{
		buf[0] = eSSU2BlkRelayResponse;
		buf[3] = 0; // flag
		buf[4] = code; // code
		htobe32buf (buf + 5, nonce); // nonce
		htobe32buf (buf + 9, i2p::util::GetSecondsSinceEpoch ()); // timestamp
		buf[13] = 2; // ver
		size_t csz = 0;
		if (code == eSSU2RelayResponseCodeAccept)
		{
			auto addr = i2p::context.GetRouterInfo ().GetSSU2Address (v4);
			if (!addr)
			{
				LogPrint (eLogError, "SSU2: Can't find local address for RelayResponse");
				return 0;
			}
			csz = CreateEndpoint (buf + 15, len - 15, boost::asio::ip::udp::endpoint (addr->host, addr->port));
			if (!csz)
			{
				LogPrint (eLogError, "SSU2: Can't create local endpoint for RelayResponse");
				return 0;
			}
		}
		buf[14] = csz; // csz
		// signature
		size_t signatureLen = i2p::context.GetIdentity ()->GetSignatureLen ();
		if (15 + csz + signatureLen > len)
		{
			LogPrint (eLogError, "SSU2: Buffer for RelayResponse signature is too small ", len);
			return 0;
		}
		SignedData<128> s;
		s.Insert ((const uint8_t *)"RelayAgreementOK", 16); // prologue
		if (code == eSSU2RelayResponseCodeAccept || code >= 64) // Charlie
			s.Insert (GetRemoteIdentity ()->GetIdentHash (), 32); // bhash
		else // Bob's reject
			s.Insert (i2p::context.GetIdentity ()->GetIdentHash (), 32); // bhash
		s.Insert (buf + 5, 10 + csz); // nonce, timestamp, ver, csz and Charlie's endpoint
		s.Sign (i2p::context.GetPrivateKeys (), buf + 15 + csz);
		size_t payloadSize = 12 + csz + signatureLen;
		if (!code)
		{
			if (payloadSize + 11 > len)
			{
				LogPrint (eLogError, "SSU2: Buffer for RelayResponse token is too small ", len);
				return 0;
			}
			memcpy (buf + 3 + payloadSize, &token, 8);
			payloadSize += 8;
		}
		htobe16buf (buf + 1, payloadSize); // size
		return payloadSize + 3;
	}

	size_t SSU2Session::CreatePeerTestBlock (uint8_t * buf, size_t len, uint8_t msg, SSU2PeerTestCode code,
		const uint8_t * routerHash, const uint8_t * signedData, size_t signedDataLen)
	{
		buf[0] = eSSU2BlkPeerTest;
		size_t payloadSize = 3 /* msg, code, flag */ + signedDataLen;
		if (routerHash) payloadSize += 32; // router hash
		if (payloadSize + 3 > len) return 0;
		htobe16buf (buf + 1, payloadSize); // size
		buf[3] = msg; // msg
		buf[4] = (uint8_t)code; // code
		buf[5] = 0; //flag
		size_t offset = 6;
		if (routerHash)
		{
			memcpy (buf + offset, routerHash, 32); // router hash
			offset += 32;
		}
		memcpy (buf + offset, signedData, signedDataLen);
		return payloadSize + 3;
	}

	size_t SSU2Session::CreatePeerTestBlock (uint8_t * buf, size_t len, uint32_t nonce)
	{
		auto localAddress = FindLocalAddress ();
		if (!localAddress || !localAddress->port || localAddress->host.is_unspecified ()
			|| localAddress->host.is_v4 () != m_RemoteEndpoint.address ().is_v4 ())
		{
			LogPrint (eLogWarning, "SSU2: Can't find local address for peer test");
			return 0;
		}
		// signed data
		auto ts = i2p::util::GetSecondsSinceEpoch ();
		auto signatureLen = i2p::context.GetIdentity ()->GetSignatureLen ();
		std::vector<uint8_t> signedData (10 + 18 + signatureLen);
		signedData[0] = 2; // ver
		htobe32buf (signedData.data () + 1, nonce);
		htobe32buf (signedData.data () + 5, ts);
		size_t asz = CreateEndpoint (
			signedData.data () + 10, signedData.size () - 10, boost::asio::ip::udp::endpoint (localAddress->host, localAddress->port));
		signedData[9] = asz;
		// signature
		SignedData<128> s;
		s.Insert ((const uint8_t *)"PeerTestValidate", 16); // prologue
		s.Insert (GetRemoteIdentity ()->GetIdentHash (), 32); // bhash
		s.Insert (signedData.data (), 10 + asz); // ver, nonce, ts, asz, Alice's endpoint
		s.Sign (i2p::context.GetPrivateKeys (), signedData.data () + 10 + asz);
		return CreatePeerTestBlock (buf, len, 1, eSSU2PeerTestCodeAccept, nullptr, signedData.data (),
			10 + asz + signatureLen);
	}

	size_t SSU2Session::CreateTerminationBlock (uint8_t * buf, size_t len)
	{
		buf[0] = eSSU2BlkTermination;
		htobe16buf (buf + 1, 9);
		htobe64buf (buf + 3, m_ReceivePacketNum);
		buf[11] = (uint8_t)m_TerminationReason;
		return 12;
	}

	std::shared_ptr<const i2p::data::RouterInfo> SSU2Session::ExtractRouterInfo (const uint8_t * buf, size_t size)
	{
		if (size < 2) return nullptr;
		// TODO: handle frag
		std::shared_ptr<const i2p::data::RouterInfo> ri;
		if (buf[0] & SSU2_ROUTER_INFO_FLAG_GZIP)
		{
			i2p::data::GzipInflator inflator;
			uint8_t uncompressed[i2p::data::MAX_RI_BUFFER_SIZE];
			size_t uncompressedSize = inflator.Inflate (buf + 2, size - 2, uncompressed, i2p::data::MAX_RI_BUFFER_SIZE);
			if (uncompressedSize && uncompressedSize <= i2p::data::MAX_RI_BUFFER_SIZE)
				ri = std::make_shared<i2p::data::RouterInfo> (uncompressed, uncompressedSize);
			else
				LogPrint (eLogInfo, "SSU2: RouterInfo decompression failed ", uncompressedSize);
		}
		else if (size <= i2p::data::MAX_RI_BUFFER_SIZE + 2)
			ri = std::make_shared<i2p::data::RouterInfo> (buf + 2, size - 2);
		else
			LogPrint (eLogInfo, "SSU2: RouterInfo is too long ", size);
		return ri;
	}

	bool SSU2Session::UpdateReceivePacketNum (uint32_t packetNum)
	{
		if (packetNum <= m_ReceivePacketNum) return false; // duplicate
		if (packetNum == m_ReceivePacketNum + 1)
		{
			if (!m_OutOfSequencePackets.empty ())
			{
				auto it = m_OutOfSequencePackets.begin ();
				if (*it == packetNum + 1)
				{
					// first out of sequence packet is in sequence now
					packetNum++;
					it++;
					while (it != m_OutOfSequencePackets.end ())
					{
						if (*it == packetNum + 1)
						{
							packetNum++;
							it++;
						}
						else // next out of sequence
							break;
					}
					m_OutOfSequencePackets.erase (m_OutOfSequencePackets.begin (), it);
				}
				m_NumRanges = 0; // recalculate ranges when create next Ack
			}
			m_ReceivePacketNum = packetNum;
		}
		else
		{
			if (m_NumRanges
				&& (m_OutOfSequencePackets.empty () || packetNum != (*m_OutOfSequencePackets.rbegin ()) + 1))
				m_NumRanges = 0; // reset ranges if received packet is not next
			m_OutOfSequencePackets.insert (packetNum);
		}
		return true;
	}

	void SSU2Session::SendQuickAck ()
	{
		uint8_t payload[SSU2_MAX_PACKET_SIZE];
		size_t payloadSize = 0;
		if (m_SendPacketNum > m_LastDatetimeSentPacketNum + SSU2_SEND_DATETIME_NUM_PACKETS)
		{
			payload[0] = eSSU2BlkDateTime;
			htobe16buf (payload + 1, 4);
			htobe32buf (payload + 3, (i2p::util::GetMillisecondsSinceEpoch () + 500) / 1000);
			payloadSize += 7;
			m_LastDatetimeSentPacketNum = m_SendPacketNum;
		}
		payloadSize += CreateAckBlock (payload + payloadSize, m_MaxPayloadSize - payloadSize);
		payloadSize += CreatePaddingBlock (payload + payloadSize, m_MaxPayloadSize - payloadSize);
		SendData (payload, payloadSize);
	}

	void SSU2Session::SendTermination ()
	{
		uint8_t payload[32];
		size_t payloadSize = CreateTerminationBlock (payload, 32);
		payloadSize += CreatePaddingBlock (payload + payloadSize, 32 - payloadSize);
		SendData (payload, payloadSize);
	}

	void SSU2Session::SendPathResponse (const uint8_t * data, size_t len)
	{
		uint8_t payload[SSU2_MAX_PACKET_SIZE];
		size_t payloadSize = 0;
		// datetime block
		payload[0] = eSSU2BlkDateTime;
		htobe16buf (payload + 1, 4);
		htobe32buf (payload + 3, (i2p::util::GetMillisecondsSinceEpoch () + 500) / 1000);
		payloadSize += 7;
		// address block
		payloadSize += CreateAddressBlock (payload + payloadSize, m_MaxPayloadSize - payloadSize, m_RemoteEndpoint);
		// path response
		if (payloadSize + len + 3 > m_MaxPayloadSize) // 3 bytes block header
		{
			LogPrint (eLogWarning, "SSU2: Incorrect data size for path response ", len);
			return;
		}
		payload[payloadSize] = eSSU2BlkPathResponse;
		htobe16buf (payload + payloadSize + 1, len);
		memcpy (payload + payloadSize + 3, data, len);
		payloadSize += len + 3;
		// ack block
		if (payloadSize < m_MaxPayloadSize)
			payloadSize += CreateAckBlock (payload + payloadSize, m_MaxPayloadSize - payloadSize);
		// padding
		if (payloadSize < m_MaxPayloadSize)
			payloadSize += CreatePaddingBlock (payload + payloadSize, m_MaxPayloadSize - payloadSize);
		SendData (payload, payloadSize);
	}

	void SSU2Session::SendPathChallenge (const boost::asio::ip::udp::endpoint & to)
	{
		AdjustMaxPayloadSize (SSU2_MIN_PACKET_SIZE); // reduce to minimum
		m_WindowSize = SSU2_MIN_WINDOW_SIZE; // reduce window to minimum

		uint8_t payload[SSU2_MAX_PACKET_SIZE];
		size_t payloadSize = 0;
		// datetime block
		payload[0] = eSSU2BlkDateTime;
		htobe16buf (payload + 1, 4);
		htobe32buf (payload + 3, (i2p::util::GetMillisecondsSinceEpoch () + 500) / 1000);
		payloadSize += 7;
		// address block with new address
		payloadSize += CreateAddressBlock (payload + payloadSize, m_MaxPayloadSize - payloadSize, to);
		// path challenge block
		payload[payloadSize] = eSSU2BlkPathChallenge;
		uint64_t challenge;
		RAND_bytes ((uint8_t *)&challenge, 8);
		htobe16buf (payload + payloadSize + 1, 8); // always 8 bytes
		htobuf64 (payload + payloadSize + 3, challenge);
		payloadSize += 11;
		m_PathChallenge = std::make_unique<std::pair<uint64_t, boost::asio::ip::udp::endpoint>> (challenge, to);
		// ack block
		if (payloadSize < m_MaxPayloadSize)
			payloadSize += CreateAckBlock (payload + payloadSize, m_MaxPayloadSize - payloadSize);
		// padding block
		if (payloadSize < m_MaxPayloadSize)
			payloadSize += CreatePaddingBlock (payload + payloadSize, m_MaxPayloadSize - payloadSize);
		// send to new endpoint
		auto existing = m_RemoteEndpoint;
		m_RemoteEndpoint = to; // send path challenge to new endpoint
		SendData (payload, payloadSize);
		m_RemoteEndpoint = existing; // restore endpoint back until path response received
	}

	void SSU2Session::CleanUp (uint64_t ts)
	{
		for (auto it = m_IncompleteMessages.begin (); it != m_IncompleteMessages.end ();)
		{
			if (ts > it->second->lastFragmentInsertTime + SSU2_INCOMPLETE_MESSAGES_CLEANUP_TIMEOUT)
			{
				LogPrint (eLogWarning, "SSU2: message ", it->first, " was not completed in ",
					SSU2_INCOMPLETE_MESSAGES_CLEANUP_TIMEOUT, " seconds, deleted");
				it = m_IncompleteMessages.erase (it);
			}
			else
				++it;
		}
		if (m_ReceivedI2NPMsgIDs.size () > SSU2_MAX_NUM_RECEIVED_I2NP_MSGIDS
			|| ts > GetLastActivityTimestamp () + SSU2_DECAY_INTERVAL)
			// decay
			m_ReceivedI2NPMsgIDs.clear ();
		else
		{
			// delete old received msgIDs
			for (auto it = m_ReceivedI2NPMsgIDs.begin (); it != m_ReceivedI2NPMsgIDs.end ();)
			{
				if (ts > it->second + SSU2_RECEIVED_I2NP_MSGIDS_CLEANUP_TIMEOUT)
					it = m_ReceivedI2NPMsgIDs.erase (it);
				else
					++it;
			}
		}
		if (!m_OutOfSequencePackets.empty ())
		{
			int ranges = 0;
			while (ranges < 8 && !m_OutOfSequencePackets.empty ()
				&& (m_OutOfSequencePackets.size () > 2 * SSU2_MAX_NUM_ACK_RANGES
					|| *m_OutOfSequencePackets.rbegin () > m_ReceivePacketNum + SSU2_MAX_NUM_ACK_PACKETS))
			{
				uint32_t packet = *m_OutOfSequencePackets.begin ();
				if (packet > m_ReceivePacketNum + 1)
				{
					// like we've just received all packets before first
					packet--;
					m_ReceivePacketNum = packet - 1;
					UpdateReceivePacketNum (packet);
					ranges++;
				}
				else
				{
					LogPrint (eLogError, "SSU2: Out of sequence packet ", packet, " is less than last received ",
						m_ReceivePacketNum);
					break;
				}
			}
			if (m_OutOfSequencePackets.size () > 255 * 4)
			{
				// seems we have a serious network issue
				m_ReceivePacketNum = *m_OutOfSequencePackets.rbegin ();
				m_OutOfSequencePackets.clear ();
			}
		}

		for (auto it = m_RelaySessions.begin (); it != m_RelaySessions.end ();)
		{
			if (ts > it->second.second + SSU2_RELAY_NONCE_EXPIRATION_TIMEOUT)
			{
				LogPrint (eLogInfo, "SSU2: Relay nonce ", it->first, " was not responded in ",
					SSU2_RELAY_NONCE_EXPIRATION_TIMEOUT, " seconds, deleted");
				it = m_RelaySessions.erase (it);
			}
			else
				++it;
		}
		if (m_PathChallenge) RequestTermination (eSSU2TerminationReasonNormalClose);
	}

	void SSU2Session::FlushData ()
	{
		bool sent = SendQueue (); // if we have something to send
		if (sent) SetSendQueueSize (m_SendQueue.size ());
		if (m_IsDataReceived)
		{
			if (!sent) SendQuickAck ();
			m_Handler.Flush ();
			m_IsDataReceived = false;
		}
		else if (!sent
			&& !m_SentPackets.empty ()) // if only acks received, nothing sent and we still have something to resend
			Resend (i2p::util::GetMillisecondsSinceEpoch ()); // than right time to resend
	}

	i2p::data::RouterInfo::SupportedTransports SSU2Session::GetTransportType () const
	{
		return m_RemoteEndpoint.address ().is_v4 () ? i2p::data::RouterInfo::eSSU2V4 : i2p::data::RouterInfo::eSSU2V6;
	}

	bool SSU2Session::SetVersion (uint8_t version)
	{
		switch (version)
		{
		case 3:
#ifndef LIBRESSL_VERSION_NUMBER
			m_Version = 3;
#else
			m_Version = 2; // MLKEM512 is not supported by LibreSSL
#endif
			break;
		case 4: m_Version = (m_MaxPayloadSize >= SSU2_MLKEM768_MIN_PAYLOAD_SIZE) ? 4 : 2; break;
		default: m_Version = 2;
		}
		return m_Version == version;
	}
}
}
