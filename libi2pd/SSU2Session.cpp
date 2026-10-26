/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: SSU2Session.
 *
 * Implements the SSU2 handshake (SessionRequest/Created/Confirmed, Retry,
 * TokenRequest), data-phase packet encoding, ACK generation, fragmentation and
 * block handlers including relay, address, RouterInfo and peer-test processing.
 * Timers and the negotiated MTU govern retransmission and payload sizing.
 *
 */

#include <string.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <openssl/rand.h>
#include "Log.h"
#include "Modern.h"
#include "Blocklist.h"
#include "Transports.h"
#include "Gzip.h"
#include "NetDb.hpp"
#include "SSU2.h"
#include "SSU2Session.h"

namespace i2p
{
namespace transport
{
	namespace
	{
		bool HasDateTimeFirstBlock (const uint8_t * buf, size_t len)
		{
			// SSU2Payload.processPayload(..., isHandshake=true) requires the
			// first handshake block to be DateTime with exactly four data bytes.
			return len >= 7 && buf[0] == eSSU2BlkDateTime && bufbe16toh (buf + 1) == 4;
		}

		bool SamePeerTestAddress (const boost::asio::ip::address & left, const boost::asio::ip::address & right)
		{
			if (left.is_v4 () != right.is_v4 ()) return false;
			if (left.is_v4 ()) return left.to_v4 () == right.to_v4 ();
			// Java compares IPv6 PeerTest sources on the /64 prefix.
			const auto l = left.to_v6 ().to_bytes ();
			const auto r = right.to_v6 ().to_bytes ();
			return std::equal (l.begin (), l.begin () + 8, r.begin ());
		}
	}

	bool SSU2IncompleteMessage::AttachNextFragment (const uint8_t * fragment, size_t fragmentSize)
	{
		if (!msg || !fragment) return false;
		if (msg->len + fragmentSize > I2NP_MAX_MESSAGE_SIZE)
		{
			LogPrint (eLogError, "SSU2: I2NP message exceeds maximum size ", I2NP_MAX_MESSAGE_SIZE);
			return false;
		}
		if (msg->len + fragmentSize > msg->maxLen)
		{
			LogPrint (eLogInfo, "SSU2: I2NP message size ", msg->maxLen, " is not enough");
			auto newMsg = NewI2NPMessage (msg->len + fragmentSize);
			if (!newMsg || newMsg->maxLen < msg->len + fragmentSize)
			{
				LogPrint (eLogError, "SSU2: I2NP message cannot grow to ", msg->len + fragmentSize);
				return false;
			}
			*newMsg = *msg;
			msg = newMsg;
		}
		if (msg->Concat (fragment, fragmentSize) < fragmentSize)
		{
			LogPrint (eLogError, "SSU2: I2NP buffer overflow ", msg->maxLen);
			return false;
		}
		nextFragmentNum++;
		return true;
	}

	bool SSU2IncompleteMessage::ConcatOutOfSequenceFragments ()
	{
		bool isLast = false;
		while (outOfSequenceFragments)
		{
			if (outOfSequenceFragments->fragmentNum == nextFragmentNum)
			{
				if (!AttachNextFragment (outOfSequenceFragments->buf, outOfSequenceFragments->len)) return false;
				// Release the same fixed slab that AddOutOfSequenceFragment()
				// charged, independently of the payload length.
				constexpr size_t slab = sizeof (SSU2IncompleteMessage::Fragment);
				outOfSequenceBytes -= std::min (outOfSequenceBytes, slab);
				if (numOutOfSequenceFragments) --numOutOfSequenceFragments;
				isLast = outOfSequenceFragments->isLast;
				if (isLast)
				{
					outOfSequenceFragments = nullptr;
					outOfSequenceBytes = 0;
					numOutOfSequenceFragments = 0;
				}
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
		// Java InboundMessageState.receiveFragment() rejects additional
		// fragments at or above an already-known last fragment. Do the same so
		// an attacker cannot make ConcatOutOfSequenceFragments() truncate the
		// message at a lower "last" fragment.
		if (lastFragmentNum >= 0 && (fragment->isLast || fragment->fragmentNum >= lastFragmentNum))
		{
			LogPrint (eLogWarning, "SSU2: Fragment ", fragment->fragmentNum, " after last fragment ", lastFragmentNum);
			return;
		}
		// Charge the fixed fragment slab, not the bytes it carries: every
		// queued Fragment already owns a full SSU2_MAX_PACKET_SIZE buffer, so
		// accounting for `len` would under-count the memory an attacker can
		// pin with tiny fragments. The per-message fragment count is also
		// bounded by what a reassembled I2NP message can need.
		constexpr size_t slab = sizeof (SSU2IncompleteMessage::Fragment);
		if (numOutOfSequenceFragments >= SSU2_MAX_FRAGMENTS_PER_MESSAGE
			|| numOutOfSequenceFragments >= SSU2_MAX_OUT_OF_SEQUENCE_FRAGMENTS
			|| outOfSequenceBytes + slab > SSU2_MAX_OUT_OF_SEQUENCE_BYTES)
		{
			LogPrint (eLogWarning, "SSU2: Out-of-sequence fragment budget exceeded for message");
			return;
		}
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
		outOfSequenceBytes += slab;
		++numOutOfSequenceFragments;
		if (fragment->isLast) lastFragmentNum = fragment->fragmentNum;
		lastFragmentInsertTime = static_cast<uint32_t> (i2p::util::GetSecondsSinceEpoch ());
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
		, m_ReceivedTermination (false)
		, m_RTT (SSU2_UNKNOWN_RTT)
		, m_RTTDeviation (0)
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
			m_RemoteEndpoint
				= boost::asio::ip::udp::endpoint (m_Address->host, static_cast<uint16_t> (m_Address->port));
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
		htobe32buf (payload + 12, static_cast<uint32_t> (ts / 1000));
		payload[16] = 2; // relay protocol version is fixed to 2
		size_t asz = CreateEndpoint (payload + 18, m_MaxPayloadSize - 18,
			boost::asio::ip::udp::endpoint (localAddress->host, static_cast<uint16_t> (localAddress->port)));
		if (!asz) return false;
		payload[17] = static_cast<uint8_t> (asz);
		packet->payloadSize = asz + 18;
		SignedData<128> s;
		s.Insert ((const uint8_t *)"RelayRequestData", 16); // prologue
		s.Insert (GetRemoteIdentity ()->GetIdentHash (), 32); // bhash
		s.Insert (session->GetRemoteIdentity ()->GetIdentHash (), 32); // chash
		s.Insert (payload + 4, 14 + asz); // nonce, relay tag, timestamp, ver, asz and Alice's endpoint
		s.Sign (i2p::context.GetPrivateKeys (), payload + packet->payloadSize);
		packet->payloadSize += i2p::context.GetIdentity ()->GetSignatureLen ();
		htobe16buf (payload + 1, static_cast<uint16_t> (packet->payloadSize - 3)); // size
		packet->payloadSize
			+= CreatePaddingBlock (payload + packet->payloadSize, m_MaxPayloadSize - packet->payloadSize);
		// send
		if (!m_RelaySessions.emplace (nonce, RelaySession { session, ts / 1000, eSSU2RelayRoleAlice }).second)
		{
			LogPrint (eLogWarning, "SSU2: Relay nonce ", nonce, " already exists");
			return false;
		}
		session->m_SourceConnID = htobe64 (((uint64_t)nonce << 32) | nonce);
		session->m_DestConnID = ~session->m_SourceConnID;
		m_Server.AddSession (session);
		uint32_t packetNum = 0;
		if (!SendData (packet->payload, packet->payloadSize, 0, &packetNum))
		{
			m_RelaySessions.erase (nonce);
			return false;
		}
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
				m_Server.RemoveSession (GetConnID (), shared_from_this ());
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
				m_Server.RequestRemoveSession (GetConnID (), shared_from_this ());
			}
		}
	}

	void SSU2Session::SendPeerTest ()
	{
		// we are Alice
		if (!m_Server.IsPeerTestSessionAdmissionAllowed ())
		{
			LogPrint (eLogWarning, "SSU2: Too many PeerTest sessions, not starting a test");
			return;
		}
		uint32_t nonce;
		RAND_bytes ((uint8_t *)&nonce, 4);
		auto ts = i2p::util::GetMillisecondsSinceEpoch ();
		// session for message 5
		auto session = std::make_shared<SSU2PeerTestSession> (m_Server, htobe64 (((uint64_t)nonce << 32) | nonce), 0);
		m_Server.AddRequestedPeerTest (nonce, session, ts / 1000);
		if (!m_Server.AddSession (session))
		{
			LogPrint (eLogWarning, "SSU2: Could not register the PeerTest session for nonce ", nonce);
			session->Done ();
			return;
		}
		// peer test block
		auto packet = m_Server.GetSentPacketsPool ().AcquireShared ();
		packet->payloadSize = CreatePeerTestBlock (packet->payload, m_MaxPayloadSize, nonce);
		if (packet->payloadSize > 0)
		{
			packet->payloadSize
				+= CreatePaddingBlock (packet->payload + packet->payloadSize, m_MaxPayloadSize - packet->payloadSize);
			uint32_t packetNum = 0;
			if (SendData (packet->payload, packet->payloadSize, SSU2_FLAG_IMMEDIATE_ACK_REQUESTED, &packetNum))
			{
				packet->sendTime = ts;
				m_SentPackets.emplace (packetNum, packet);
			}
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
		if (m_Terminated.exchange (true, std::memory_order_acq_rel)) return;
		auto state = m_State.exchange (eSSU2SessionStateTerminated);
		if (state != eSSU2SessionStateTerminated)
		{
			m_ConnectTimer.cancel ();
			ClearOnEstablished ();
			if (m_RelayTag) m_Server.RemoveRelay (m_RelayTag);
			// Only sessions that actually reached the data phase may feed the
			// hole-punch cache. A pre-auth handshake (TokenRequest/SessionRequest
			// or a failed SessionCreated) must not publish an endpoint.
			if (state == eSSU2SessionStateEstablished || state == eSSU2SessionStateClosing
				|| state == eSSU2SessionStateClosingConfirmed)
				m_Server.AddConnectedRecently (m_RemoteEndpoint, GetLastActivityTimestamp ());
			m_SentHandshakePacket.reset (nullptr);
			m_SessionConfirmedFragmentsOut.clear ();
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
			m_OutOfSequencePackets.clear ();
			m_SessionConfirmedFragments.clear ();
			m_SessionConfirmedNumFragments = 0;
			m_IncompleteMessages.clear ();
			m_RelaySessions.clear ();
			m_RecentRelayRequests.clear ();
			m_ReceivedI2NPMsgIDs.clear ();
			m_Server.RemovePendingOutgoingSession (m_RemoteEndpoint, shared_from_this ());
			m_Server.RemoveSession (m_SourceConnID, shared_from_this ());
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
		m_SessionConfirmedFragmentsOut.clear ();
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
		// Consume callbacks with exchange() under the mutex. A callback that
		// appends another one while running is observed by the next iteration;
		// TakeOnEstablished() guarantees each function is invoked only once.
		while (auto onEstablished = TakeOnEstablished ()) onEstablished ();
	}

	void SSU2Session::AddOnEstablished (OnEstablished e)
	{
		if (!e) return;
		OnEstablished late;
		{
			std::lock_guard<std::mutex> l (m_OnEstablishedMutex);
			// Established() publishes the state before draining the callback
			// under this mutex. If it has already drained, run the late
			// callback on the SSU2 service thread instead of losing it (peer
			// tests/introduction callbacks are registered from transports).
			if (m_State.load () == eSSU2SessionStateEstablished)
				late = std::move (e);
			else if (!m_OnEstablished)
				m_OnEstablished = std::move (e);
			else
			{
				auto previous = std::move (m_OnEstablished);
				m_OnEstablished = [previous = std::move (previous), e = std::move (e)] () mutable
				{
					previous ();
					e ();
				};
			}
		}
		if (late) boost::asio::post (m_Server.GetService (), std::move (late));
	}

	SSU2Session::OnEstablished SSU2Session::TakeOnEstablished ()
	{
		std::lock_guard<std::mutex> l (m_OnEstablishedMutex);
		return std::exchange (m_OnEstablished, nullptr);
	}

	void SSU2Session::ClearOnEstablished ()
	{
		std::lock_guard<std::mutex> l (m_OnEstablishedMutex);
		m_OnEstablished = nullptr;
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
		if (!m_SendQueue.empty () && m_SentPackets.size () < m_WindowSize && IsEstablished ())
		{
			auto ts = i2p::util::GetMillisecondsSinceEpoch ();
			uint64_t mts = i2p::util::GetMonotonicMicroseconds ();
			auto packet = m_Server.GetSentPacketsPool ().AcquireShared ();
			size_t ackBlockSize = CreateAckBlock (packet->payload, m_MaxPayloadSize);
			bool ackBlockSent = false;
			auto sendPacket = [this, ts, ackBlockSize, &ackBlockSent] (
								  const std::shared_ptr<SSU2SentPacket> & p, uint8_t flags, bool containsAck) -> bool
			{
				if (!p || p->payloadSize < 8 || p->payloadSize > m_MaxPayloadSize)
				{
					LogPrint (eLogWarning, "SSU2: Refusing malformed outgoing data packet of ", p ? p->payloadSize : 0,
						" bytes");
					return false;
				}
				uint32_t packetNum = 0;
				if (!SendData (p->payload, p->payloadSize, flags, &packetNum))
				{
					LogPrint (eLogWarning, "SSU2: Failed to send data packet, message is dropped");
					return false;
				}
				p->sendTime = ts;
				m_SentPackets.emplace (packetNum, p);
				if (containsAck && ackBlockSize) ackBlockSent = true;
				return true;
			};
			packet->payloadSize += ackBlockSize;
			while (!m_SendQueue.empty () && m_SentPackets.size () < m_WindowSize)
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
					const size_t added = CreateI2NPBlock (
						packet->payload + packet->payloadSize, m_MaxPayloadSize - packet->payloadSize, std::move (msg));
					if (!added)
					{
						LogPrint (eLogError, "SSU2: Failed to frame I2NP message, dropping it");
						continue;
					}
					packet->payloadSize += added;
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
						// try to add padding
						if (packet->payloadSize + 16 < m_MaxPayloadSize)
							packet->payloadSize += CreatePaddingBlock (
								packet->payload + packet->payloadSize, m_MaxPayloadSize - packet->payloadSize);
						sendPacket (packet, 0, true);
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
						const size_t added = CreateI2NPBlock (packet->payload + packet->payloadSize,
							m_MaxPayloadSize - packet->payloadSize, std::move (msg));
						if (!added)
						{
							LogPrint (eLogError, "SSU2: Failed to frame I2NP message, dropping it");
							packet = newPacket;
							continue;
						}
						packet->payloadSize += added;
						sendPacket (packet, 0, false);
					}
					packet = newPacket; // just ack block
				}
			};
			if (packet->payloadSize > ackBlockSize)
			{
				// last
				if (packet->payloadSize + 16 < m_MaxPayloadSize)
					packet->payloadSize += CreatePaddingBlock (
						packet->payload + packet->payloadSize, m_MaxPayloadSize - packet->payloadSize);
				sendPacket (packet, SSU2_FLAG_IMMEDIATE_ACK_REQUESTED, true);
			}
			return ackBlockSent;
		}
		return false;
	}

	bool SSU2Session::SendFragmentedMessage (std::shared_ptr<I2NPMessage> msg)
	{
		if (!msg || m_MaxPayloadSize < 16) return false;
		auto ts = i2p::util::GetMillisecondsSinceEpoch ();
		auto sendPacket = [this, ts] (const std::shared_ptr<SSU2SentPacket> & packet, uint8_t flags) -> bool
		{
			if (!packet || packet->payloadSize < 8 || packet->payloadSize > m_MaxPayloadSize) return false;
			uint32_t packetNum = 0;
			if (!SendData (packet->payload, packet->payloadSize, flags, &packetNum)) return false;
			packet->sendTime = ts;
			m_SentPackets.emplace (packetNum, packet);
			return true;
		};
		if (msg->GetNTCP2Length () + 3 <= m_MaxPayloadSize)
		{
			// non fragmented, send regular I2NP block
			auto packet = m_Server.GetSentPacketsPool ().AcquireShared ();
			packet->payloadSize = CreateI2NPBlock (packet->payload, m_MaxPayloadSize, std::move (msg));
			return sendPacket (packet, 0);
		}
		const size_t payloadLen = msg->GetNTCP2Length () + 3;
		if (m_MaxPayloadSize <= 8) return false;
		size_t lastFragmentSize = (payloadLen - m_MaxPayloadSize) % (m_MaxPayloadSize - 8);
		size_t extraSize = m_MaxPayloadSize - lastFragmentSize;
		bool ackBlockSent = false;
		uint32_t msgID;
		memcpy (&msgID, msg->GetHeader () + I2NP_HEADER_MSGID_OFFSET, 4);
		auto packet = m_Server.GetSentPacketsPool ().AcquireShared ();
		if (extraSize >= 8)
		{
			packet->payloadSize = CreateAckBlock (packet->payload, extraSize);
			if (packet->payloadSize && packet->payloadSize + 12 < m_MaxPayloadSize)
			{
				if (!sendPacket (packet, 0)) return false;
				ackBlockSent = true;
				packet = m_Server.GetSentPacketsPool ().AcquireShared ();
			}
			else
				extraSize -= packet->payloadSize;
		}
		// Leave room for at least a block header and 10 bytes of fragment data;
		// a 9-byte FIRSTFRAG data field is rejected by the peer.
		auto pickOffset = [this, &extraSize] (size_t minCapacity) -> size_t
		{
			if (!extraSize) return 0;
			size_t maxShave = extraSize - 1;
			size_t room = m_MaxPayloadSize > minCapacity ? m_MaxPayloadSize - minCapacity : 0;
			if (maxShave > room) maxShave = room;
			return maxShave ? m_Server.GetRng () () % (maxShave + 1) : 0;
		};
		size_t offset = pickOffset (13);
		auto size = CreateFirstFragmentBlock (packet->payload, m_MaxPayloadSize - offset, msg);
		if (!size) return false;
		extraSize -= std::min (extraSize, offset);
		packet->payloadSize = size;
		if (!sendPacket (packet, 0)) return false;
		uint8_t fragmentNum = 0;
		while (msg->offset < msg->len)
		{
			offset = pickOffset (9);
			packet = m_Server.GetSentPacketsPool ().AcquireShared ();
			packet->payloadSize
				= CreateFollowOnFragmentBlock (packet->payload, m_MaxPayloadSize - offset, msg, fragmentNum, msgID);
			if (!packet->payloadSize)
			{
				LogPrint (
					eLogError, "SSU2: Could not create follow-on fragment ", (int)fragmentNum, " for message ", msgID);
				break;
			}
			extraSize -= std::min (extraSize, offset);
			uint8_t flags = 0;
			if (msg->offset >= msg->len && packet->payloadSize + 16 < m_MaxPayloadSize) // last fragment
			{
				packet->payloadSize += CreatePaddingBlock (
					packet->payload + packet->payloadSize, m_MaxPayloadSize - packet->payloadSize);
				if (fragmentNum > 2) // 3 or more fragments
					flags |= SSU2_FLAG_IMMEDIATE_ACK_REQUESTED;
			}
			if (!sendPacket (packet, flags)) break;
		}
		return ackBlockSent;
	}

	size_t SSU2Session::Resend (uint64_t ts)
	{
		if (ts < m_LastResendAttemptTime + SSU2_RESEND_ATTEMPT_MIN_INTERVAL) return 0;
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
		// Drop an expired connection migration promptly. This only restores
		// the old endpoint; it never terminates the session.
		if (m_PathChallenge && IsSSU2PathChallengeExpired (ts, m_PathChallenge->startedTime, m_PathChallenge->numSends))
		{
			LogPrint (eLogInfo, "SSU2: Connection migration to ", m_PathChallenge->endpoint, " timed out; keeping ",
				m_RemoteEndpoint);
			CancelPathChallenge (false);
		}
		// Resend data packets. RFC 6298 section 5.5 requires exponential
		// backoff, and the SSU2 spec does not require tearing down the session
		// when one I2NP message's delivery eventually fails.
		if (m_SentPackets.empty ()) return 0;
		std::map<uint32_t, std::shared_ptr<SSU2SentPacket>> resentPackets;
		bool congestion = false;
		for (auto it = m_SentPackets.begin (); it != m_SentPackets.end ();)
		{
			const size_t shift = std::min<size_t> (it->second->numResends, 6);
			const uint64_t interval = (uint64_t)m_RTO << shift;
			if (ts < it->second->sendTime + interval)
			{
				++it;
				continue;
			}
			congestion = true;
			if (it->second->numResends >= SSU2_MAX_NUM_RESENDS)
			{
				LogPrint (eLogInfo, "SSU2: Dropping packet after ", it->second->numResends,
					" retransmissions; the message may be lost");
				it = m_SentPackets.erase (it);
				continue;
			}
			uint32_t packetNum = 0;
			if (SendData (it->second->payload, it->second->payloadSize,
					it->second->numResends > 1 ? SSU2_FLAG_IMMEDIATE_ACK_REQUESTED : 0, &packetNum))
			{
				it->second->numResends++;
				it->second->sendTime = ts;
				resentPackets.emplace (packetNum, it->second);
			}
			else
				LogPrint (eLogWarning, "SSU2: Failed to retransmit data packet; dropping it");
			it = m_SentPackets.erase (it);
		}
		if (congestion)
		{
			m_LastResendTime = ts;
			m_WindowSize >>= 1; // /2
			if (m_WindowSize < SSU2_MIN_WINDOW_SIZE) m_WindowSize = SSU2_MIN_WINDOW_SIZE;
		}
		m_SentPackets.merge (resentPackets);
		return resentPackets.size ();
	}

	void SSU2Session::ResendHandshakePacket ()
	{
		if (m_SentHandshakePacket)
		{
			m_Server.Send (m_SentHandshakePacket->header.buf, 16, m_SentHandshakePacket->headerX, 48,
				m_SentHandshakePacket->payload, m_SentHandshakePacket->payloadSize, m_RemoteEndpoint);
			if (m_State == eSSU2SessionStateSessionConfirmedSent)
			{
				// Retransmit all SessionConfirmed fragments unchanged, including
				// fragment 1..n-1. The spec forbids re-splitting the jumbo frame.
				for (const auto & fragment : m_SessionConfirmedFragmentsOut)
				{
					if (!fragment) continue;
					m_Server.Send (
						fragment->header.buf, 16, fragment->payload, fragment->payloadSize, m_RemoteEndpoint);
				}
			}
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
			if (len < 56)
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

	void SSU2Session::ResetHandshakeCryptoState ()
	{
		// Noise state, the ephemeral X25519 pair and the ML-KEM key pair all
		// belong to the version of the rejected attempt. They must never prefix
		// a SessionRequest for a different version, and a stale handshake hash
		// would make the next attempt fail in a confusing way.
		m_NoiseState.reset ();
		m_EphemeralKeys.reset ();
#if OPENSSL_PQ
		m_PQKeys.reset ();
#endif
	}

	void SSU2Session::SelectOutgoingVersion ()
	{
#if OPENSSL_PQ
		if (m_IsVersionFallback)
		{
			// A Retry already selected a lower mutually supported version.
			// Do not negotiate back up on the next SessionRequest.
			SetVersion (m_Version);
			if (m_Version != 2 && m_Version != 3 && m_Version != 4) m_Version = 2;
		}
		else if (m_Server.GetVersion () > 2 && m_Address)
		{
			// Do not exceed the locally configured maximum, and use the best
			// version that both sides advertised in "pq". SetVersion() also
			// enforces the local MTU, so continue down the list if version 4
			// does not fit.
			bool selected = false;
			int localMax = std::min (m_Server.GetVersion (), 4);
			for (int version = localMax; version >= 3; --version)
			{
				if (m_Address->SupportsSSU2Version (version) && SetVersion ((uint8_t)version))
				{
					selected = true;
					break;
				}
			}
			if (!selected) m_Version = 2;
		}
		else
			m_Version = 2;
#else
		m_Version = 2;
#endif
	}

	bool SSU2Session::SendSessionRequest (uint64_t token)
	{
		// we are Alice
		if (!m_Address) return false;
		SelectOutgoingVersion ();
#if OPENSSL_PQ
		// A Retry may downgrade an existing attempt to version 2. The hybrid
		// key from the previous version must never prefix a non-hybrid
		// SessionRequest; every attempt regenerates fresh KEM material.
		m_PQKeys.reset ();
#endif
		m_EphemeralKeys = i2p::transport::transports.GetNextX25519KeysPair ();
		if (!m_EphemeralKeys) return false;
		// Never send an all-zero X because the provider failed to generate a
		// key for a pooled pair.
		if (!m_EphemeralKeys->HasPrivateKey () && !m_EphemeralKeys->GenerateKeys ())
		{
			LogPrint (eLogError, "SSU2: no usable ephemeral key for SessionRequest");
			return false;
		}
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
		htobe32buf (payload + payloadSize + 3, static_cast<uint32_t> ((ts + 500) / 1000));
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
			if (!InitNoiseXKStateMLKEM1 (*m_NoiseState, (i2p::data::CryptoKeyType)(m_Version + 2), m_Address->s))
			{
				LogPrint (eLogError, "SSU2: unsupported hybrid handshake version ", (int)m_Version);
				return false;
			}
			m_NoiseState->MixHash (GetRemoteIdentity ()->GetIdentHash (), 32); // h = SHA256(h || bhash)
		}
		else
#endif
			InitNoiseXKState1 (*m_NoiseState, m_Address->s);
		// KDF for session request
		m_NoiseState->MixHash ({ { header.buf, 16 }, { headerX, 16 } }); // h = SHA256(h || header)
		m_NoiseState->MixHash (m_EphemeralKeys->GetPublicKey (), 32); // h = SHA256(h || aepk)
		uint8_t sharedSecret[32];
		if (!m_EphemeralKeys || !m_Address || !m_EphemeralKeys->Agree (m_Address->s, sharedSecret)) return false;
		m_NoiseState->MixKey (sharedSecret);
		// encrypt
#if OPENSSL_PQ
		if (m_Version > 2 && m_PQKeys)
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
		// Decrypt the long-header tail and X before validating the version, so
		// an incompatible-version Retry can carry the real Destination
		// Connection ID instead of the zero placeholder.
		const uint8_t nonce[12] = { 0 };
		uint8_t headerX[48];
		m_Server.ChaCha20 (buf + 16, 48, i2p::context.GetSSU2IntroKey (), nonce, headerX);
		memcpy (&m_DestConnID, headerX, 8);
		uint64_t token;
		memcpy (&token, headerX + 8, 8);
		// SSU2 spec, SessionRequest notes: Bob must drop the message if the
		// Source and Destination Connection IDs are identical. Java
		// InboundEstablishState2 throws for the same condition.
		if (!m_DestConnID || m_DestConnID == m_SourceConnID)
		{
			LogPrint (eLogWarning, "SSU2: SessionRequest has invalid Connection IDs");
			return false;
		}
		if (header.h.flags[1] != i2p::context.GetNetID ())
		{
			LogPrint (eLogWarning, "SSU2: SessionRequest has wrong network ID ", (int)header.h.flags[1]);
			return false;
		}
		if (m_TerminationReason == eSSU2TerminationReasonConnectionLimits)
		{
			// Inbound admission (SSU2Server::RejectInboundHandshake) rejected
			// this handshake before any session state was created. Answer with
			// a Retry carrying the connection-limit reason so the peer stops
			// retrying, and do not build or insert a session.
			SendRetry ();
			return true;
		}
		int version = header.h.flags[0];
		if (version < 2 || version > 4)
		{
			LogPrint (eLogWarning, "SSU2: SessionRequest protocol version ", version, " is not supported");
			return false;
		}
#if OPENSSL_PQ
		if (version > 2 && len > 32)
		{
			// Java InboundEstablishState2 uses the SessionRequest size as the
			// MTU for the PQ SessionCreated response, capped by the local MTU,
			// with the SSU2 minimum MTU as a floor. The long header and MAC
			// account for 32 of the datagram bytes here.
			size_t packetPayload = len - 32;
			const size_t ipHeader = m_RemoteEndpoint.address ().is_v6 () ? IPV6_HEADER_SIZE : IPV4_HEADER_SIZE;
			size_t minPayload = SSU2_MIN_PACKET_SIZE - ipHeader - UDP_HEADER_SIZE - 32;
			if (version == 4 && minPayload < SSU2_MLKEM768_MIN_PAYLOAD_SIZE)
				minPayload = SSU2_MLKEM768_MIN_PAYLOAD_SIZE;
			size_t estimate = std::max (packetPayload, minPayload);
			// Never raise the local capability; SetVersion(4) checks this value.
			if (estimate < m_MaxPayloadSize) m_MaxPayloadSize = estimate;
		}
#endif
		bool compatible;
#if OPENSSL_PQ
		compatible = version <= m_Server.GetVersion () && SetVersion ((uint8_t)version);
#else
		compatible = version == 2;
#endif
		if (!compatible)
		{
			// Keep the requested version in the Retry header (Java does the
			// same in InboundEstablishState2.buildRetryPacket) but do not
			// initialize a handshake that this router cannot complete.
			m_Version = (uint8_t)version;
			m_TerminationReason = eSSU2TerminationReasonIncompatibleVersion;
			SendRetry ();
			return true;
		}
#if OPENSSL_PQ
		if (m_Version > 2)
		{
			auto keyLen = i2p::crypto::GetMLKEMPublicKeyLen ((i2p::data::CryptoKeyType)(m_Version + 2));
			if (len < keyLen + 16 + 90)
			{
				LogPrint (eLogWarning, "SSU2: SessionRequest version ", m_Version, " message too short ", len);
				return false;
			}
		}
#endif
		if (!token || token != m_Server.GetIncomingToken (m_RemoteEndpoint))
		{
			LogPrint (eLogDebug, "SSU2: SessionRequest token mismatch. Retry");
			SendRetry ();
			return true;
		}
		// SSU2 spec, Tokens: a token may only be used once. Java
		// EstablishmentManager.isInboundTokenValid() removes it on use.
		m_Server.RemoveIncomingToken (m_RemoteEndpoint);
		// create and init noise state
		if (!m_NoiseState) m_NoiseState.reset (new i2p::crypto::NoiseSymmetricState);
#if OPENSSL_PQ
		if (m_Version > 2)
		{
			if (!InitNoiseXKStateMLKEM1 (
					*m_NoiseState, (i2p::data::CryptoKeyType)(m_Version + 2), i2p::context.GetSSU2StaticPublicKey ()))
			{
				LogPrint (eLogError, "SSU2: unsupported hybrid handshake version ", (int)m_Version);
				return false;
			}
			m_NoiseState->MixHash (i2p::context.GetIdentHash (), 32); // h = SHA256(h || bhash)
		}
		else
#endif
			InitNoiseXKState1 (*m_NoiseState, i2p::context.GetSSU2StaticPublicKey ());
		// KDF for session request
		m_NoiseState->MixHash ({ { header.buf, 16 }, { headerX, 16 } }); // h = SHA256(h || header)
		m_NoiseState->MixHash (headerX + 16, 32); // h = SHA256(h || aepk);
		uint8_t sharedSecret[32];
		if (!i2p::context.GetSSU2StaticKeys ().Agree (headerX + 16, sharedSecret)) return false;
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
		if (!HasDateTimeFirstBlock (decryptedPayload.data (), decryptedPayload.size ()))
		{
			LogPrint (eLogWarning, "SSU2: SessionRequest has no valid DateTime first block");
			return false;
		}
		m_State = eSSU2SessionStateSessionRequestReceived;
		HandlePayload (decryptedPayload.data (), decryptedPayload.size ());

		if (m_IsInvalidMessage || m_ReceivedTermination)
		{
			LogPrint (eLogWarning, "SSU2: Invalid or terminating block in SessionRequest");
			return false;
		}
		if (m_TerminationReason == eSSU2TerminationReasonNormalClose)
		{
			if (!SendSessionCreated (headerX + 16))
			{
				LogPrint (eLogError, "SSU2: Could not build SessionCreated for ", m_RemoteEndpoint);
				return false;
			}
			if (!m_Server.AddSession (shared_from_this ()))
			{
				LogPrint (eLogError, "SSU2: Duplicate connection ID for SessionCreated");
				return false;
			}
		}
		else
			SendRetry ();
		return true;
	}

	bool SSU2Session::SendSessionCreated (const uint8_t * X)
	{
		// we are Bob
		if (!m_NoiseState || m_MaxPayloadSize <= 48) return false;
		m_EphemeralKeys = i2p::transport::transports.GetNextX25519KeysPair ();
		if (!m_EphemeralKeys) return false;
		// A pooled pair whose generation failed would put an all-zero Y into
		// SessionCreated and derive a useless shared secret.
		if (!m_EphemeralKeys->HasPrivateKey () && !m_EphemeralKeys->GenerateKeys ())
		{
			LogPrint (eLogError, "SSU2: no usable ephemeral key for SessionCreated");
			return false;
		}
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
		if (!m_EphemeralKeys || !m_EphemeralKeys->Agree (X, sharedSecret)) return false;
		m_NoiseState->MixKey (sharedSecret);
		// payload
		size_t maxPayloadSize = m_MaxPayloadSize - 48;
		size_t payloadSize = 0, offset = 0;
#if OPENSSL_PQ
		if (m_Version > 2)
		{
			if (!m_PQKeys) return false;
			size_t cipherTextLen = m_PQKeys->GetCTLen ();
			std::vector<uint8_t> kemCiphertext (cipherTextLen);
			if (!m_PQKeys || !m_PQKeys->Encaps (kemCiphertext.data (), sharedSecret)) return false;
			if (!m_NoiseState->Encrypt (kemCiphertext.data (), payload, cipherTextLen))
			{
				LogPrint (eLogWarning, "SSU2: SessionCreated ML-KEM ciphertext section AEAD encryption failed");
				return false;
			}
			offset = cipherTextLen + 16;
			m_NoiseState->MixHash (payload, offset); // encrypt ML-KEM frame
			m_NoiseState->MixKey (sharedSecret);
			payloadSize += offset;
		}
#endif
		payload[payloadSize] = eSSU2BlkDateTime;
		htobe16buf (payload + payloadSize + 1, 4);
		htobe32buf (payload + payloadSize + 3, static_cast<uint32_t> ((ts + 500) / 1000));
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
			return false;
		}
		// encrypt
		const uint8_t nonce[12] = { 0 }; // always zero
		if (!m_NoiseState->Encrypt (payload + offset, payload + offset, payloadSize - offset))
		{
			LogPrint (eLogWarning, "SSU2: SessionCreated payload encryption failed ");
			return false;
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
		return true;
	}

	bool SSU2Session::ProcessSessionCreated (uint8_t * buf, size_t len)
	{
		if (IsTerminated ()) return false;
		// we are Alice
		if (!m_NoiseState || !m_Address)
		{
			LogPrint (eLogWarning, "SSU2: Unexpected SessionCreated message");
			return false;
		}
		if (len < 88)
		{
			LogPrint (eLogWarning, "SSU2: SessionCreated message too short ", len);
			return false;
		}
		// Once MixHash/MixKey has run, the Noise state is consumed. A failed
		// authentication must discard it so a Retry can reinitialize cleanly
		// and stale SessionCreated retransmissions fail cheaply.
		auto failHandshake = [this] () -> bool
		{
			m_NoiseState.reset ();
			m_EphemeralKeys.reset ();
			m_SentHandshakePacket.reset ();
#if OPENSSL_PQ
			m_PQKeys.reset ();
#endif
			return false;
		};
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
		if (header.h.connID != m_SourceConnID)
		{
			LogPrint (eLogWarning, "SSU2: SessionCreated has wrong Destination Connection ID");
			return false;
		}
		if (header.h.flags[0] != m_Version || header.h.flags[0] < 2 || header.h.flags[0] > 4)
		{
			LogPrint (eLogWarning, "SSU2: SessionCreated version ", (int)header.h.flags[0], " does not match ",
				(int)m_Version);
			return false;
		}
		if (header.h.flags[1] != i2p::context.GetNetID ())
		{
			LogPrint (eLogWarning, "SSU2: SessionCreated has wrong network ID ", (int)header.h.flags[1]);
			return false;
		}

		m_HandshakeInterval = i2p::util::GetMillisecondsSinceEpoch () - m_HandshakeInterval;
		const uint8_t nonce[12] = { 0 };
		uint8_t headerX[48];
		m_Server.ChaCha20 (buf + 16, 48, kh2, nonce, headerX);
		if (memcmp (&headerX[0], &m_DestConnID, 8))
		{
			LogPrint (eLogWarning, "SSU2: SessionCreated has wrong Source Connection ID");
			return false;
		}
		// KDF for SessionCreated
		m_NoiseState->MixHash ({ { header.buf, 16 }, { headerX, 16 } }); // h = SHA256(h || header)
		m_NoiseState->MixHash (headerX + 16, 32); // h = SHA256(h || bepk);
		uint8_t sharedSecret[32];
		if (!m_EphemeralKeys || !m_EphemeralKeys->Agree (headerX + 16, sharedSecret)) return failHandshake ();
		m_NoiseState->MixKey (sharedSecret);
		size_t offset = 64;
#if OPENSSL_PQ
		if (m_Version > 2)
		{
			if (!m_PQKeys) return failHandshake ();
			i2p::data::CryptoKeyType cryptoType = (i2p::data::CryptoKeyType)(m_Version + 2);
			size_t cipherTextLen = i2p::crypto::GetMLKEMCipherTextLen (cryptoType);
			if (!i2p::crypto::IsMLKEMSupported (cryptoType) || offset + cipherTextLen + 16 > len)
				return failHandshake ();
			std::vector<uint8_t> kemCiphertext (cipherTextLen);
			if (!m_NoiseState->Decrypt (buf + offset, kemCiphertext.data (), cipherTextLen))
			{
				LogPrint (eLogWarning, "SSU2: SessionCreated ML-KEM ciphertext section AEAD decryption failed");
				return failHandshake ();
			}
			m_NoiseState->MixHash (buf + offset, cipherTextLen + 16);
			offset += cipherTextLen + 16;
			if (!m_PQKeys || !m_PQKeys->Decaps (kemCiphertext.data (), sharedSecret)) return failHandshake ();
			m_NoiseState->MixKey (sharedSecret);
		}
#endif
		// decrypt
		if (offset + 16 > len)
		{
			LogPrint (eLogWarning, "SSU2: SessionCreated message is too short ", len);
			return failHandshake ();
		}
		uint8_t * payload = buf + offset;
		std::vector<uint8_t> decryptedPayload (len - offset - 16);
		if (!m_NoiseState->Decrypt (payload, decryptedPayload.data (), decryptedPayload.size ()))
		{
			LogPrint (eLogWarning, "SSU2: SessionCreated AEAD verification failed");
			if (GetRemoteIdentity ())
				i2p::data::netdb.SetUnreachable (GetRemoteIdentity ()->GetIdentHash (), true); // assume wrong s key
			return failHandshake ();
		}
		m_NoiseState->MixHash (
			payload, len - offset); // h = SHA256(h || encrypted payload from SessionCreated) for SessionConfirmed
		// payload
		if (!HasDateTimeFirstBlock (decryptedPayload.data (), decryptedPayload.size ()))
		{
			LogPrint (eLogWarning, "SSU2: SessionCreated has no valid DateTime first block");
			return failHandshake ();
		}
		m_State = eSSU2SessionStateSessionCreatedReceived;
		HandlePayload (decryptedPayload.data (), decryptedPayload.size ());
		if (m_IsInvalidMessage || m_ReceivedTermination)
		{
			LogPrint (eLogWarning, "SSU2: Invalid or terminating block in SessionCreated");
			return failHandshake ();
		}

		if (!SendSessionConfirmed (headerX + 16))
		{
			LogPrint (eLogError, "SSU2: Could not build SessionConfirmed for ", m_RemoteEndpoint);
			m_Server.RemovePendingOutgoingSession (m_RemoteEndpoint, shared_from_this ());
			return failHandshake ();
		}
		if (!m_Server.AddSession (shared_from_this ()))
		{
			LogPrint (eLogError, "SSU2: Duplicate connection ID for SessionConfirmed");
			m_Server.RemovePendingOutgoingSession (m_RemoteEndpoint, shared_from_this ());
			return failHandshake ();
		}
		KDFDataPhase (m_KeyDataSend, m_KeyDataReceive);

		return true;
	}

	bool SSU2Session::SendSessionConfirmed (const uint8_t * Y)
	{
		// we are Alice
		if (!m_NoiseState || !m_Address || !Y || m_MaxPayloadSize < 80) return false;
		m_SentHandshakePacket.reset (new HandshakePacket);
		m_SentHandshakePacket->sendTime = i2p::util::GetMillisecondsSinceEpoch ();

		uint8_t kh2[32];
		i2p::crypto::HKDF (m_NoiseState->m_CK, nullptr, 0, "SessionConfirmed", kh2,
			32); // k_header_2 = HKDF(chainKey, ZEROLEN, "SessionConfirmed", 32)

		// Fragment 0 carries the 32-byte static key (+16 MAC) after the short
		// header. The jumbo payload is one AEAD frame split over fragments.
		// Maximum ciphertext bytes in fragment 0 and in each later fragment
		// follow from the MTU budget (wire = 16 header + ciphertext, with the
		// 48-byte part 1 frame added to fragment 0).
		const size_t macLen = 16;
		const size_t part1Len = 48;
		const size_t firstChunkCapacity = m_MaxPayloadSize - 32; // m_MaxPayloadSize + 32 wire - 16 header - 48 part1
		const size_t additionalChunkCapacity = m_MaxPayloadSize + 16; // m_MaxPayloadSize + 32 wire - 16 header
		const size_t maxPlaintext
			= firstChunkCapacity + (SSU2_MAX_SESSION_CONFIRMED_FRAGMENTS - 1) * additionalChunkCapacity - macLen;
		const size_t firstPlaintextCapacity = firstChunkCapacity - macLen;

		// The in-place Noise encryption appends a 16-byte MAC.
		std::vector<uint8_t> jumbo (maxPlaintext + macLen);
		auto riBuffer = i2p::context.CopyRouterInfoBuffer ();
		size_t payloadSize = CreateRouterInfoBlock (jumbo.data (), firstPlaintextCapacity, riBuffer);
		if (!payloadSize)
		{
			// A fragmented RouterInfo is gzipped if possible. Force the
			// compressor by offering only raw_size - 1 bytes, then fall back to
			// the uncompressed block if it does not help.
			const size_t rawSize = riBuffer ? riBuffer->GetBufferLen () : 0;
			size_t gzipCapacity = std::min (maxPlaintext, rawSize + 4);
			if (rawSize > 1000 && gzipCapacity > 5 && gzipCapacity < rawSize + 5)
				payloadSize = CreateRouterInfoBlock (jumbo.data (), gzipCapacity, riBuffer);
			if (!payloadSize) payloadSize = CreateRouterInfoBlock (jumbo.data (), maxPlaintext, riBuffer);
			if (!payloadSize)
			{
				LogPrint (eLogError, "SSU2: RouterInfo does not fit in 15 SessionConfirmed fragments");
				return false;
			}
		}

		size_t numFragments = 1;
		size_t lastChunk = 0;
		auto calculateFragments = [&] (size_t plaintextSize)
		{
			size_t ciphertextSize = plaintextSize + macLen;
			size_t firstChunk = std::min (ciphertextSize, firstChunkCapacity);
			size_t remaining = ciphertextSize - firstChunk;
			numFragments = 1 + (remaining + additionalChunkCapacity - 1) / additionalChunkCapacity;
			lastChunk = remaining ? remaining - (numFragments - 2) * additionalChunkCapacity : firstChunk;
		};
		calculateFragments (payloadSize);
		size_t paddingTotal = 0;
		int paddingAttempts = 0;
		while (numFragments > 1 && lastChunk < 24 && paddingAttempts++ < 32)
		{
			// The header mask reads the final 24 bytes of every fragment. Add a
			// single padding block to the jumbo plaintext and recompute.
			size_t need = 24 - lastChunk;
			if (!paddingTotal) need = std::max<size_t> (need, 3); // room for the block header
			if (payloadSize + paddingTotal + need > maxPlaintext)
			{
				LogPrint (eLogError, "SSU2: Cannot pad final SessionConfirmed fragment to 24 bytes");
				return false;
			}
			paddingTotal += need;
			calculateFragments (payloadSize + paddingTotal);
			if (numFragments > SSU2_MAX_SESSION_CONFIRMED_FRAGMENTS)
			{
				LogPrint (eLogError, "SSU2: RouterInfo needs more than ", (int)SSU2_MAX_SESSION_CONFIRMED_FRAGMENTS,
					" SessionConfirmed fragments");
				return false;
			}
		}
		if (numFragments > SSU2_MAX_SESSION_CONFIRMED_FRAGMENTS)
		{
			LogPrint (eLogError, "SSU2: RouterInfo needs more than ", (int)SSU2_MAX_SESSION_CONFIRMED_FRAGMENTS,
				" SessionConfirmed fragments");
			return false;
		}
		if (numFragments > 1 && lastChunk < 24)
		{
			LogPrint (eLogError, "SSU2: SessionConfirmed final fragment cannot reach 24 bytes");
			return false;
		}
		if (paddingTotal)
		{
			jumbo[payloadSize] = eSSU2BlkPadding;
			htobe16buf (jumbo.data () + payloadSize + 1, static_cast<uint16_t> (paddingTotal - 3));
			memset (jumbo.data () + payloadSize + 3, 0, paddingTotal - 3);
			payloadSize += paddingTotal;
		}
		else if (numFragments == 1 && payloadSize < firstPlaintextCapacity)
		{
			payloadSize += CreatePaddingBlock (jumbo.data () + payloadSize, firstPlaintextCapacity - payloadSize);
		}
		calculateFragments (payloadSize);

		Header & header = m_SentHandshakePacket->header;
		header.h.connID = m_DestConnID; // dest id
		header.h.packetNum = 0; // always zero
		header.h.type = eSSU2SessionConfirmed;
		memset (header.h.flags, 0, 3);
		header.h.flags[0] = (numFragments == 1) ? 1 : ((uint8_t)numFragments);
		// KDF for Session Confirmed part 1
		m_NoiseState->MixHash (header.buf, 16); // h = SHA256(h || header)
		// Encrypt part 1
		uint8_t * part1 = m_SentHandshakePacket->headerX;
		if (!m_NoiseState->Encrypt (i2p::context.GetSSU2StaticPublicKey (), part1, 32))
		{
			LogPrint (eLogError, "SSU2: SessionConfirmed static key encryption failed");
			return false;
		}
		m_NoiseState->MixHash (part1, part1Len); // h = SHA256(h || ciphertext);
		// KDF for Session Confirmed part 2
		uint8_t sharedSecret[32];
		if (!i2p::context.GetSSU2StaticKeys ().Agree (Y, sharedSecret)) return false;
		m_NoiseState->MixKey (sharedSecret);
		if (!m_NoiseState->Encrypt (jumbo.data (), jumbo.data (), payloadSize))
		{
			LogPrint (eLogError, "SSU2: SessionConfirmed payload encryption failed");
			return false;
		}
		m_NoiseState->MixHash (jumbo.data (), payloadSize + macLen);

		const size_t ciphertextSize = payloadSize + macLen;
		const size_t firstChunk = std::min (ciphertextSize, firstChunkCapacity);
		if (firstChunk < 24)
		{
			LogPrint (eLogError, "SSU2: SessionConfirmed first fragment is too short");
			return false;
		}
		m_SentHandshakePacket->payloadSize = firstChunk;
		memcpy (m_SentHandshakePacket->payload, jumbo.data (), firstChunk);
		header.ll[0] ^= CreateHeaderMask (m_Address->i, m_SentHandshakePacket->payload + (firstChunk - 24));
		header.ll[1] ^= CreateHeaderMask (kh2, m_SentHandshakePacket->payload + (firstChunk - 12));
		m_SessionConfirmedFragmentsOut.clear ();

		size_t offset = firstChunk;
		uint8_t fragmentIndex = 1;
		while (offset < ciphertextSize)
		{
			size_t chunkSize = std::min (additionalChunkCapacity, ciphertextSize - offset);
			if (chunkSize < 24)
			{
				LogPrint (eLogError, "SSU2: SessionConfirmed fragment ", (int)fragmentIndex, " is too short");
				m_SessionConfirmedFragmentsOut.clear ();
				return false;
			}
			auto fragment = std::make_unique<HandshakePacket> ();
			Header & fragmentHeader = fragment->header;
			fragmentHeader.h.connID = m_DestConnID;
			fragmentHeader.h.packetNum = 0;
			fragmentHeader.h.type = eSSU2SessionConfirmed;
			memset (fragmentHeader.h.flags, 0, 3);
			fragmentHeader.h.flags[0] = (uint8_t)((fragmentIndex << 4) | (numFragments & 0x0F));
			fragment->payloadSize = chunkSize;
			memcpy (fragment->payload, jumbo.data () + offset, chunkSize);
			fragmentHeader.ll[0] ^= CreateHeaderMask (m_Address->i, fragment->payload + (chunkSize - 24));
			fragmentHeader.ll[1] ^= CreateHeaderMask (kh2, fragment->payload + (chunkSize - 12));
			m_SessionConfirmedFragmentsOut.push_back (std::move (fragment));
			offset += chunkSize;
			fragmentIndex++;
		}
		if ((size_t)fragmentIndex != numFragments)
		{
			LogPrint (
				eLogError, "SSU2: SessionConfirmed fragment count mismatch ", (int)fragmentIndex, " != ", numFragments);
			m_SessionConfirmedFragmentsOut.clear ();
			return false;
		}

		m_State = eSSU2SessionStateSessionConfirmedSent;
		m_Server.Send (header.buf, 16, part1, part1Len, m_SentHandshakePacket->payload,
			m_SentHandshakePacket->payloadSize, m_RemoteEndpoint);
		for (const auto & fragment : m_SessionConfirmedFragmentsOut)
			m_Server.Send (fragment->header.buf, 16, fragment->payload, fragment->payloadSize, m_RemoteEndpoint);
		m_SendPacketNum++;
		return true;
	}

	bool SSU2Session::ProcessSessionConfirmed (uint8_t * buf, size_t len)
	{
		if (IsTerminated ()) return false;
		// we are Bob
		if (!m_NoiseState)
		{
			LogPrint (eLogWarning, "SSU2: Unexpected SessionConfirmed message");
			return false;
		}
		if (len < 40) // 16 byte header + 8 byte minimum ciphertext body/MAC
		{
			LogPrint (eLogWarning, "SSU2: SessionConfirmed message too short ", len);
			return false;
		}
		Header header;
		memcpy (header.buf, buf, 16);
		header.ll[0] ^= CreateHeaderMask (i2p::context.GetSSU2IntroKey (), buf + (len - 24));
		uint8_t kh2[32];
		i2p::crypto::HKDF (m_NoiseState->m_CK, nullptr, 0, "SessionConfirmed", kh2,
			32); // k_header_2 = HKDF(chainKey, ZEROLEN, "SessionConfirmed", 32)
		header.ll[1] ^= CreateHeaderMask (kh2, buf + (len - 12));
		if (header.h.connID != GetConnID ())
		{
			LogPrint (eLogWarning, "SSU2: SessionConfirmed has wrong Destination Connection ID");
			return false;
		}
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
		uint8_t index = header.h.flags[0] >> 4;
		if (!numFragments)
		{
			// Older Java development builds used 0/0 for "one fragment".
			if (index) return false;
			numFragments = 1;
		}
		if (numFragments == 1 && index)
		{
			LogPrint (eLogWarning, "SSU2: Invalid unfragmented SessionConfirmed index ", (int)index);
			return false;
		}
		if (numFragments > 1)
		{
			if (numFragments > SSU2_MAX_SESSION_CONFIRMED_FRAGMENTS || len < 32 || index >= numFragments)
			{
				LogPrint (eLogError, "SSU2: Invalid SessionConfirmed fragment ", (int)index, " of ", (int)numFragments,
					" len ", len);
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
			std::vector<uint8_t> fragmentData (buf + 16, buf + len);
			if (index == 0)
			{
				// Preserve the original fragment 0 header. A duplicate fragment
				// must not replace it while the stored ciphertext stays the same,
				// otherwise reassembly uses a different AD and cannot decrypt.
				if (!m_SessionConfirmedFragments.emplace (index, std::move (fragmentData)).second)
				{
					LogPrint (eLogWarning, "SSU2: Duplicate SessionConfirmed fragment 0");
					return true;
				}
				m_SessionConfirmedHeader = header;
			}
			else if (!m_SessionConfirmedFragments.emplace (index, std::move (fragmentData)).second)
			{
				LogPrint (eLogDebug, "SSU2: Duplicate SessionConfirmed fragment ", (int)index);
				return true;
			}
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
			if (assembled.size () > SSU2_MAX_PACKET_SIZE * SSU2_MAX_SESSION_CONFIRMED_FRAGMENTS)
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
			m_SessionConfirmedFragmentsOut.clear ();
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
			m_SessionConfirmedFragmentsOut.clear ();
			return false;
		}
		m_NoiseState->MixHash (buf + 16, 48); // h = SHA256(h || ciphertext);
		// KDF for Session Confirmed part 2 and data phase
		uint8_t sharedSecret[32];
		if (!m_EphemeralKeys || !m_EphemeralKeys->Agree (S, sharedSecret)) return false;
		m_NoiseState->MixKey (sharedSecret);
		KDFDataPhase (m_KeyDataReceive, m_KeyDataSend);
		// decrypt part2
		uint8_t * payload = buf + 64;
		std::vector<uint8_t> decryptedPayload (len - 80);
		if (!m_NoiseState->Decrypt (payload, decryptedPayload.data (), len - 80))
		{
			LogPrint (eLogWarning, "SSU2: SessionConfirmed part 2 AEAD verification failed ");
			m_SessionConfirmedFragmentsOut.clear ();
			return false;
		}
		m_NoiseState->MixHash (payload, len - 64); // h = SHA256(h || ciphertext);
		m_SessionConfirmedFragmentsOut.clear ();
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
		// The leading RouterInfo must be a single unfragmented block: the frag
		// field is `index << 4 | count` for fragments, but the SSU2 spec only
		// defines 1 (fragment 0 of 1) in SessionConfirmed. Reject anything else
		// instead of handing a fragment to the RouterInfo parser.
		if (riSize < 2 || !IsSSU2RouterInfoFragmentAcceptable (decryptedPayload[4]))
		{
			LogPrint (eLogError, "SSU2: SessionConfirmed RouterInfo is fragmented or too short");
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
		auto ri1 = i2p::data::netdb.AddRouterInfo (
			ri->GetBuffer (), static_cast<int> (ri->GetBufferLen ())); // ri points to one from netdb now
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

		auto address = m_RemoteEndpoint.address ().is_v6 () ? ri->GetSSU2V6Address () : ri->GetSSU2V4Address ();
		if (!address)
		{
			LogPrint (eLogError, "SSU2: Address not found in SessionConfirmed from ",
				i2p::data::GetIdentHashAbbreviation (ri->GetIdentHash ()));
			return false;
		}
		SetAddress (address);
		if (address->published && m_RemoteEndpoint.address () != address->host
			&& (!m_RemoteEndpoint.address ().is_v6 ()
				|| memcmp (m_RemoteEndpoint.address ().to_v6 ().to_bytes ().data (),
					address->host.to_v6 ().to_bytes ().data (), 8))) // temporary address
		{
			if (isOlder) // older router?
				i2p::data::UpdateRouterProfile (ri->GetIdentHash (),
					[] (std::shared_ptr<i2p::data::RouterProfile> profile)
					{
						if (profile) profile->Duplicated (); // mark router as duplicated in profile
					});
			else
				LogPrint (eLogInfo, "SSU2: Host mismatch between published address ", address->host,
					" and actual endpoint ", m_RemoteEndpoint.address (), " from ",
					i2p::data::GetIdentHashAbbreviation (ri->GetIdentHash ()));
			return false;
		}
		if (memcmp (S, address->s, 32))
		{
			LogPrint (eLogError, "SSU2: Wrong static key in SessionConfirmed from ",
				i2p::data::GetIdentHashAbbreviation (ri->GetIdentHash ()));
			if (address->published) i2p::transport::transports.AddBan (m_RemoteEndpoint.address ());
			return false;
		}
		if (!address->published)
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
		if (m_IsInvalidMessage || m_ReceivedTermination)
		{
			LogPrint (eLogError, "SSU2: Invalid block in SessionConfirmed from ",
				i2p::data::GetIdentHashAbbreviation (ri->GetIdentHash ()));
			if (address->published) i2p::transport::transports.AddBan (m_RemoteEndpoint.address ());
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
		if (!m_Address) return;
		SelectOutgoingVersion ();
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
		htobe32buf (payload + 3, static_cast<uint32_t> ((i2p::util::GetMillisecondsSinceEpoch () + 500) / 1000));
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
		if (m_Server.FindPendingOutgoingSession (m_RemoteEndpoint) == shared_from_this ()
			|| m_Server.AddPendingOutgoingSession (shared_from_this ()))
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
		if (len < 56)
		{
			LogPrint (eLogWarning, "SSU2: Incorrect TokenRequest len ", len);
			return false;
		}
		uint8_t nonce[12] = { 0 };
		uint8_t h[32];
		memcpy (h, header.buf, 16);
		m_Server.ChaCha20 (buf + 16, 16, i2p::context.GetSSU2IntroKey (), nonce, h + 16);
		memcpy (&m_DestConnID, h + 16, 8);
		if (!m_DestConnID || m_DestConnID == m_SourceConnID)
		{
			LogPrint (eLogWarning, "SSU2: TokenRequest has invalid Connection IDs");
			return false;
		}
		if (header.h.flags[1] != i2p::context.GetNetID ())
		{
			LogPrint (eLogWarning, "SSU2: TokenRequest has wrong network ID ", (int)header.h.flags[1]);
			return false;
		}
		if (m_TerminationReason == eSSU2TerminationReasonConnectionLimits)
		{
			// See ProcessSessionRequest(): refuse without creating state.
			SendRetry ();
			return true;
		}
		int version = header.h.flags[0];
		if (version < 2 || version > 4)
		{
			LogPrint (eLogWarning, "SSU2: TokenRequest protocol version ", version, " is not supported");
			return false;
		}
		bool compatible;
#if OPENSSL_PQ
		compatible = version <= m_Server.GetVersion () && SetVersion ((uint8_t)version);
#else
		compatible = version == 2;
#endif
		if (!compatible)
		{
			// TokenRequest is symmetric-only, so a Retry can still be produced
			// before any Noise state exists. Mirror Java's version selection
			// and tell Alice not to continue with this version.
			m_Version = (uint8_t)version;
			m_TerminationReason = eSSU2TerminationReasonIncompatibleVersion;
			SendRetry ();
			return true;
		}
		// decrypt
		CreateNonce (be32toh (header.h.packetNum), nonce);
		uint8_t * payload = buf + 32;
		if (!i2p::crypto::AEADChaCha20Poly1305 (
				payload, len - 48, h, 32, i2p::context.GetSSU2IntroKey (), nonce, payload, len - 48, false))
		{
			LogPrint (eLogWarning, "SSU2: TokenRequest AEAD verification failed ");
			return false;
		}
		if (!HasDateTimeFirstBlock (payload, len - 48))
		{
			LogPrint (eLogWarning, "SSU2: TokenRequest has no valid DateTime first block");
			return false;
		}
		// payload
		m_State = eSSU2SessionStateTokenRequestReceived;
		HandlePayload (payload, len - 48);
		if (m_IsInvalidMessage || m_ReceivedTermination)
		{
			LogPrint (eLogWarning, "SSU2: Invalid or terminating block in TokenRequest");
			return false;
		}
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
		htobe32buf (payload + 3, static_cast<uint32_t> ((i2p::util::GetMillisecondsSinceEpoch () + 500) / 1000));
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
		if (IsTerminated ()) return false;
		// we are Alice
		if (!m_Address) return false;
		if (len < 56)
		{
			LogPrint (eLogWarning, "SSU2: Retry message too short ", len);
			return false;
		}
		Header header;
		memcpy (header.buf, buf, 16);
		header.ll[0] ^= CreateHeaderMask (m_Address->i, buf + (len - 24));
		header.ll[1] ^= CreateHeaderMask (m_Address->i, buf + (len - 12));
		if (header.h.type != eSSU2Retry)
		{
			LogPrint (eLogWarning, "SSU2: Unexpected message type ", (int)header.h.type, " instead ", (int)eSSU2Retry);
			return false;
		}
		if (header.h.connID != m_SourceConnID)
		{
			LogPrint (eLogWarning, "SSU2: Retry has wrong Destination Connection ID");
			return false;
		}
		if (header.h.flags[0] < 2 || header.h.flags[0] > 4)
		{
			LogPrint (eLogWarning, "SSU2: Retry protocol version ", (int)header.h.flags[0], " is not supported");
			return false;
		}
		if (header.h.flags[1] != i2p::context.GetNetID ())
		{
			LogPrint (eLogWarning, "SSU2: Retry has wrong network ID ", (int)header.h.flags[1]);
			return false;
		}
		uint8_t nonce[12] = { 0 };
		uint64_t headerX[2]; // sourceConnID, token
		m_Server.ChaCha20 (buf + 16, 16, m_Address->i, nonce, (uint8_t *)headerX);
		if (headerX[0] != m_DestConnID)
		{
			LogPrint (eLogWarning, "SSU2: Retry has wrong Source Connection ID");
			return false;
		}
		// A Retry header is authenticated only with the peer's published intro
		// key, which is public, so an on-path attacker can forge it. Its "ver"
		// byte therefore never authorises a version change on its own: adopt a
		// change only when the peer's signed RouterInfo advertises that exact
		// version and it is not below the highest version that RouterInfo
		// advertises. In practice that means the only accepted change is the
		// peer's own maximum; a lower or unadvertised value fails the attempt
		// instead of silently downgrading the PQ handshake (the previous code
		// adopted any lower value). See IsSSU2RetryVersionAccepted().
		const auto address = GetAddress ();
		if (!address) return false;
		const uint8_t advertisedVersions = GetSSU2AdvertisedVersionMask (address->pqVersions);
		const uint8_t retryVersion = header.h.flags[0];
		const bool versionChanged = retryVersion != m_Version;
		if (versionChanged && !IsSSU2RetryVersionAccepted (retryVersion, advertisedVersions))
		{
			LogPrint (eLogWarning, "SSU2: Retry version ", (int)retryVersion,
				" is not authorised by the peer RouterInfo (advertised mask ", (int)advertisedVersions,
				"); failing the handshake attempt instead of downgrading");
			Done ();
			return false;
		}
		uint64_t token = headerX[1];
		if (token)
			m_Server.UpdateOutgoingToken (m_RemoteEndpoint, token,
				static_cast<uint32_t> (i2p::util::GetSecondsSinceEpoch () + SSU2_TOKEN_EXPIRATION_TIMEOUT));
		// decrypt and handle payload
		uint8_t * payload = buf + 32;
		CreateNonce (be32toh (header.h.packetNum), nonce);
		uint8_t h[32];
		memcpy (h, header.buf, 16);
		memcpy (h + 16, headerX, 16);
		if (!i2p::crypto::AEADChaCha20Poly1305 (
				payload, len - 48, h, 32, m_Address->i, nonce, payload, len - 48, false))
		{
			LogPrint (eLogWarning, "SSU2: Retry AEAD verification failed");
			return false;
		}
		if (!HasDateTimeFirstBlock (payload, len - 48))
		{
			LogPrint (eLogWarning, "SSU2: Retry has no valid DateTime first block");
			return false;
		}
		if (versionChanged)
		{
			// Only now, after the Retry proved it is well formed, apply the
			// version the signed RouterInfo authorised above. SetVersion()
			// enforces the local provider capability and MTU as well, so an
			// authorised version this router cannot use fails the attempt
			// instead of falling back to a weaker one.
			const uint8_t previousVersion = m_Version;
			if (!SetVersion (retryVersion))
			{
				m_Version = previousVersion;
				LogPrint (eLogWarning, "SSU2: Retry version ", (int)retryVersion, " is not usable locally");
				Done ();
				return false;
			}
			m_IsVersionFallback = true;
			// The rejected attempt's Noise/KEM/ephemeral material belongs to
			// the old version and must never prefix the next SessionRequest.
			ResetHandshakeCryptoState ();
			LogPrint (eLogInfo, "SSU2: Retry selected advertised version ", (int)m_Version, " instead of ",
				(int)previousVersion);
		}
		m_State = eSSU2SessionStateTokenReceived;
		m_ReceivedTermination = false;
		m_TerminationReason = eSSU2TerminationReasonNormalClose;
		HandlePayload (payload, len - 48);
		if (m_IsInvalidMessage)
		{
			LogPrint (eLogWarning, "SSU2: Invalid block in Retry");
			return false;
		}
		if (m_TerminationReason == eSSU2TerminationReasonIncompatibleVersion)
		{
			// The termination block travels inside the AEAD payload, so this
			// rejection hint is authentic. Consume it before the generic
			// termination abort and retry only with a version the peer's signed
			// RouterInfo advertised (GetSSU2FallbackVersion()); when there is
			// none the attempt fails instead of forcing 3 or 2. Noise/KEM
			// state of the rejected version is never reused.
			const int fallback = GetSSU2FallbackVersion (m_Version, advertisedVersions);
			if (!fallback)
			{
				LogPrint (eLogWarning, "SSU2: peer rejected version ", (int)m_Version,
					" and advertises no lower version; failing the handshake attempt");
				Done ();
				return false;
			}
			const uint8_t rejectedVersion = m_Version;
			if (!SetVersion (static_cast<uint8_t> (fallback)))
			{
				m_Version = rejectedVersion;
				LogPrint (eLogWarning, "SSU2: advertised fallback version ", fallback, " is not usable locally");
				Done ();
				return false;
			}
			LogPrint (eLogInfo, "SSU2: peer rejected version ", (int)rejectedVersion,
				"; retrying with advertised version ", (int)m_Version);
			m_IsVersionFallback = true;
			m_ReceivedTermination = false;
			ResetHandshakeCryptoState ();
		}
		if (m_ReceivedTermination)
		{
			LogPrint (eLogInfo, "SSU2: Retry contains a termination block");
			return false;
		}
		if (!token)
		{
			// The payload still has to be handled for a zero token (DateTime
			// clock adjustment), which happened above.
			if (m_IsVersionFallback)
			{
				// A version-rejection Retry carries token 0 by construction
				// (SendRetry() only attaches a token for a normal Retry). The
				// previous attempt's token belongs to the rejected version, so
				// request a fresh one instead of tearing the session down.
				LogPrint (eLogInfo, "SSU2: Retry selected version ", (int)m_Version, "; requesting a new token");
				m_State = eSSU2SessionStateUnknown;
				SendTokenRequest ();
				return true;
			}
			LogPrint (eLogWarning, "SSU2: Retry token is zero");
			Done ();
			return false;
		}

		if (!m_NoiseState) m_NoiseState.reset (new i2p::crypto::NoiseSymmetricState);
#if OPENSSL_PQ
		if (m_Version > 2)
		{
			if (!InitNoiseXKStateMLKEM1 (*m_NoiseState, (i2p::data::CryptoKeyType)(m_Version + 2), m_Address->s))
			{
				LogPrint (eLogError, "SSU2: unsupported hybrid handshake version ", (int)m_Version);
				Done ();
				return false;
			}
		}
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
		if (IsTerminated ()) return false;
		// we are Alice
		LogPrint (eLogDebug, "SSU2: HolePunch");
		if (len < 56)
		{
			LogPrint (eLogWarning, "SSU2: HolePunch message too short ", len);
			return false;
		}
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
		// The SSU2 spec fixes the HolePunch version to 2 and validates the
		// network ID exactly (Java PacketHandler does the same before dispatch).
		// A peer that already negotiated a post-quantum session may still put
		// its version in this long header, so accept 2..4 on receive while
		// continuing to send 2.
		if (!IsSSU2OutOfSessionVersionAccepted (header.h.flags[0]))
		{
			LogPrint (eLogWarning, "SSU2: HolePunch has wrong version ", (int)header.h.flags[0]);
			return false;
		}
		if (header.h.flags[1] != i2p::context.GetNetID ())
		{
			LogPrint (eLogWarning, "SSU2: HolePunch has wrong network ID ", (int)header.h.flags[1]);
			return false;
		}
		uint8_t nonce[12] = { 0 };
		uint64_t headerX[2]; // sourceConnID, token
		m_Server.ChaCha20 (buf + 16, 16, i2p::context.GetSSU2IntroKey (), nonce, (uint8_t *)headerX);
		if (headerX[0] != m_DestConnID)
		{
			LogPrint (eLogWarning, "SSU2: HolePunch has wrong Source Connection ID");
			return false;
		}
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

	bool SSU2Session::ProcessPeerTest (uint8_t * /* buf */, size_t /* len */)
	{
		LogPrint (eLogWarning, "SSU2:  Unexpected peer test message for this session type");
		return false;
	}

	bool SSU2Session::SendData (const uint8_t * buf, size_t len, uint8_t flags, uint32_t * packetNum)
	{
		if (!m_Address || len < 8 || len > m_MaxPayloadSize || len + 32 > SSU2_MAX_PACKET_SIZE)
		{
			LogPrint (eLogWarning, "SSU2: Data message payload length ", (int)len, " is out of range");
			return false;
		}
		// Packet number 0 is the first valid data packet, so a failure must not
		// be reported as packet 0. Do not wrap around either; the spec requires
		// a new session well before the number space is exhausted.
		if (m_SendPacketNum == UINT32_MAX)
		{
			LogPrint (eLogError, "SSU2: Packet number space exhausted, terminating session");
			auto self = shared_from_this ();
			boost::asio::post (
				m_Server.GetService (), [self] () { self->RequestTermination (eSSU2TerminationReasonTimeout); });
			return false;
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
			return false;
		}
		header.ll[0] ^= CreateHeaderMask (m_Address->i, payload + (len - 8));
		header.ll[1] ^= CreateHeaderMask (m_KeyDataSend + 32, payload + (len + 4));
		m_Server.Send (header.buf, 16, payload, len + 16, m_RemoteEndpoint);
		if (packetNum) *packetNum = m_SendPacketNum;
		m_SendPacketNum++;
		UpdateNumSentBytes (len + 32);
		return true;
	}

	void SSU2Session::ProcessData (uint8_t * buf, size_t len, const boost::asio::ip::udp::endpoint & from)
	{
		// Minimum wire size: 16 byte short header + 8 byte payload + 16 byte MAC.
		// The header mask also reads the final 24 bytes, so reject before any
		// header or AEAD processing.
		if (len < 40)
		{
			LogPrint (eLogWarning, "SSU2: Data message too short ", len);
			return;
		}
		Header header;
		header.ll[0] = m_SourceConnID;
		memcpy (header.buf + 8, buf + 8, 8);
		header.ll[1] ^= CreateHeaderMask (m_KeyDataReceive + 32, buf + (len - 12));
		if (header.h.type != eSSU2Data)
		{
			// The type is read from the masked header before AEAD. Never turn
			// unauthenticated input into a handshake/RouterInfo retransmission:
			// that is a bounded-input/unbounded-output amplification path.
			LogPrint (eLogWarning, "SSU2: Unexpected message type ", (int)header.h.type, " instead ", (int)eSSU2Data);
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
			// Never rewind m_ReceivePacketNum: Java SSU2Bitfield.set() treats
			// bits below the window as already seen, and ACK blocks must not
			// regress after a late packet 0.
			m_ReceivedPacketZero = true;
			m_IsFirstReceivePacket = false;
			acceptPacket = true;
		}
		else if (m_IsFirstReceivePacket)
		{
			m_IsFirstReceivePacket = false;
			if (!packetNum)
			{
				m_ReceivedPacketZero = true;
				m_ReceivePacketNum = 0;
				acceptPacket = true;
			}
			else if (IsOutgoing ())
			{
				// Alice receives Bob's first data packet as packet 0. If a
				// higher-numbered packet is reordered ahead of it, retain it
				// until packet 0 and the contiguous prefix arrive.
				m_ReceivePacketNum = 0;
				m_OutOfSequencePackets.insert (packetNum);
				acceptPacket = true;
			}
			else
			{
				// Bob consumed Alice's SessionConfirmed as packet 0. Her first
				// data packet is packet 1, so advance the contiguous receive
				// point instead of parking every subsequent packet out of
				// sequence forever.
				m_ReceivedPacketZero = true;
				m_ReceivePacketNum = 0;
				acceptPacket = UpdateReceivePacketNum (packetNum);
			}
		}
		else
			acceptPacket = UpdateReceivePacketNum (packetNum);
		if (acceptPacket)
		{
			// Only a packet that passed AEAD and was accepted may trigger
			// connection migration. Java PeerState2 also requires the same
			// address family, so an IPv4/IPv6 change never starts migration.
			const auto now = i2p::util::GetMillisecondsSinceEpoch ();
			if (m_PathChallenge)
			{
				if (from == m_RemoteEndpoint)
				{
					// A valid packet from the old endpoint cancels the pending
					// migration and restores the previous MTU/window.
					LogPrint (eLogInfo, "SSU2: Connection migration to ", m_PathChallenge->endpoint,
						" cancelled by packet from ", from);
					CancelPathChallenge (false);
				}
				else if (from == m_PathChallenge->endpoint)
				{
					if (IsSSU2PathChallengeExpired (now, m_PathChallenge->startedTime, m_PathChallenge->numSends))
					{
						LogPrint (eLogInfo, "SSU2: Connection migration to ", m_PathChallenge->endpoint,
							" timed out; keeping ", m_RemoteEndpoint);
						CancelPathChallenge (false);
					}
					else if (now > m_PathChallenge->nextSendTime)
						RetransmitPathChallenge (now);
				}
				else
					LogPrint (
						eLogDebug, "SSU2: Ignoring packet from third address ", from, " during connection migration");
			}
			else if (m_State == eSSU2SessionStateEstablished && from != m_RemoteEndpoint
				&& from.address ().is_v4 () == m_RemoteEndpoint.address ().is_v4 () && from.port () != 0
				&& packetNum == GetHighestReceivedPacketNum () // not a reordered packet
				&& !i2p::transport::transports.IsInReservedRange (from.address ()))
			{
				LogPrint (eLogInfo, "SSU2: Remote endpoint update ", m_RemoteEndpoint, "->", from);
				SendPathChallenge (from);
			}
			// A decryptable data packet proves that Bob processed SessionConfirmed even
			// if Java's single ACK0 was lost.
			if (m_State == eSSU2SessionStateSessionConfirmedSent) Established ();
			m_IsInvalidMessage = false;
			HandlePayload (payload, payloadSize, &from);
			if (m_IsInvalidMessage)
			{
				LogPrint (eLogError, "SSU2: Invalid block in data phase, terminating");
				RequestTermination (eSSU2TerminationReasonPayloadFormatError);
			}
		}
	}

	void SSU2Session::HandlePayload (const uint8_t * buf, size_t len, const boost::asio::ip::udp::endpoint * from)
	{
		m_IsInvalidMessage = false;
		m_ReceivedTermination = false;
		// Validate the complete block sequence before dispatching the first
		// handler. This guarantees that a payload containing a disallowed block
		// cannot mutate state through an allowed block that precedes it.
		if (!ValidateSSU2PayloadBlocks (m_State.load (), buf, len))
		{
			LogPrint (eLogWarning, "SSU2: Invalid block sequence in state ", (int)m_State.load ());
			m_IsInvalidMessage = true;
			return;
		}
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
			// Check the message-type whitelist before dispatching any handler.
			// IsPayloadBlockAllowed() is the PeerTest-specific override; the
			// state table covers SessionRequest/SessionCreated/Retry/
			// TokenRequest/SessionConfirmed.
			if (!IsPayloadBlockAllowed (blk) || !IsSSU2PayloadBlockAllowedForState (m_State.load (), blk))
			{
				LogPrint (eLogWarning, "SSU2: Block type ", (int)blk, " is not valid in this payload context");
				m_IsInvalidMessage = true;
				return;
			}
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
				if (size != 4)
				{
					LogPrint (eLogWarning, "SSU2: Unexpected DateTime block size ", size);
					m_IsInvalidMessage = true;
					break;
				}
				HandleDateTime (buf + offset, size);
				break;
			case eSSU2BlkOptions: LogPrint (eLogDebug, "SSU2: Options"); break;
			case eSSU2BlkRouterInfo:
				LogPrint (eLogDebug, "SSU2: RouterInfo");
				if (size < 2)
				{
					LogPrint (eLogWarning, "SSU2: RouterInfo block is too short ", size);
					m_IsInvalidMessage = true;
					break;
				}
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
					m_ReceivedTermination = true;
					uint8_t rsn = buf[offset + 8]; // reason
					LogPrint (eLogDebug, "SSU2: Termination reason=", (int)rsn);
					if (IsEstablished () && rsn != eSSU2TerminationReasonTerminationReceived)
						RequestTermination (eSSU2TerminationReasonTerminationReceived);
					else if (!IsEstablished () && rsn == eSSU2TerminationReasonIncompatibleVersion)
					{
						// Handshake fallback: ProcessRetry consumes this hint and
						// retries at the next supported version. Do not schedule
						// Done() before it can act on the reason.
						m_TerminationReason = eSSU2TerminationReasonIncompatibleVersion;
					}
					else if (m_State != eSSU2SessionStateTerminated)
					{
						if (m_State == eSSU2SessionStateClosing && rsn == eSSU2TerminationReasonTerminationReceived)
							m_State = eSSU2SessionStateClosingConfirmed;
						m_TerminationReason = (SSU2TerminationReason)rsn;
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
							// A zero relay tag means "no tag" everywhere in this
							// implementation, so never mint one. A failing RNG
							// leaves us without a tag rather than publishing an
							// unusable one.
							const uint32_t tag = MintSSU2RelayTag (
								[this] () -> uint64_t
								{
									uint32_t value = 0;
									RAND_bytes (reinterpret_cast<uint8_t *> (&value), sizeof (value));
									return value;
								});
							if (tag)
							{
								m_RelayTag = tag;
								m_Server.AddRelay (m_RelayTag, shared_from_this ());
							}
						}
					}
				}
				break;
			case eSSU2BlkRelayTag:
				LogPrint (eLogDebug, "SSU2: RelayTag");
				if (size >= 4)
				{
					// First tag wins: a later RelayTag must not replace the tag
					// we already published to our introducers.
					const uint32_t tag = bufbe32toh (buf + offset);
					if (AcceptSSU2RelayTag (m_RelayTag, tag))
						m_RelayTag = tag;
					else
						LogPrint (eLogDebug, "SSU2: Ignoring RelayTag ", tag, " with current tag ", m_RelayTag);
				}
				else
				{
					LogPrint (eLogWarning, "SSU2: RelayTag block is too short ", size);
					m_IsInvalidMessage = true;
				}
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
				{
					LogPrint (eLogWarning, "SSU2: NewToken block is too short ", size);
					m_IsInvalidMessage = true;
				}
				break;
			}
			case eSSU2BlkPathChallenge:
				LogPrint (eLogDebug, "SSU2: Path challenge");
				if (!IsSSU2PathChallengeResponseAllowed (m_State))
				{
					LogPrint (eLogWarning, "SSU2: PathChallenge in invalid state ", (int)m_State);
					break;
				}
				SendPathResponse (buf + offset, size);
				break;
			case eSSU2BlkPathResponse:
			{
				LogPrint (eLogDebug, "SSU2: Path response");
				if (size >= 8 && m_PathChallenge)
				{
					const auto now = i2p::util::GetMillisecondsSinceEpoch ();
					if (from && *from == m_PathChallenge->endpoint
						&& buf64toh (buf + offset) == m_PathChallenge->challenge
						&& !IsSSU2PathChallengeExpired (now, m_PathChallenge->startedTime, m_PathChallenge->numSends))
					{
						LogPrint (eLogInfo, "SSU2: Connection migration to ", *from, " confirmed");
						CancelPathChallenge (true);
					}
					else
						LogPrint (eLogDebug, "SSU2: Ignoring PathResponse that does not complete migration");
				}
				break;
			}
			case eSSU2BlkFirstPacketNumber: break;
			case eSSU2BlkPadding:
				LogPrint (eLogDebug, "SSU2: Padding");
				gotPadding = true;
				break;
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
		// Java adds half the measured RTT to the clock window on an established
		// path. m_RTT is in milliseconds and stays SSU2_UNKNOWN_RTT (-1) until
		// the first ACK sample, so an unmeasured session keeps the plain
		// SSU2_CLOCK_SKEW window.
		const double allowedSkew = SSU2_CLOCK_SKEW + (m_RTT > 0 ? m_RTT / 2000.0 : 0.0);
		switch (m_State)
		{
		case eSSU2SessionStateSessionRequestReceived:
		case eSSU2SessionStateTokenRequestReceived:
		case eSSU2SessionStateEstablished:
			if (std::abs (offset) > allowedSkew) m_TerminationReason = eSSU2TerminationReasonClockSkew;
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
		// A data-phase RouterInfo block must be a single unfragmented RI. The
		// frag field is `index << 4 | count` for fragments, but the SSU2 spec
		// only defines 0x01 for a complete RouterInfo in this block. Rejecting
		// it here prevents a fragment being parsed as a complete RouterInfo.
		if (!IsSSU2RouterInfoFragmentAcceptable (buf[1]))
		{
			LogPrint (eLogWarning, "SSU2: RouterInfo block frag ", (int)buf[1], " is not a single fragment, ignored");
			return;
		}
		// not from SessionConfirmed, we must add it instantly to use in next block
		std::shared_ptr<const i2p::data::RouterInfo> newRi;
		bool updated = false;
		if (buf[0] & SSU2_ROUTER_INFO_FLAG_GZIP) // compressed?
		{
			auto ri = ExtractRouterInfo (buf, len);
			if (ri)
				newRi = i2p::data::netdb.AddRouterInfo (
					ri->GetBuffer (), static_cast<int> (ri->GetBufferLen ()), updated);
		}
		else // use buffer directly (the single-fragment byte was checked above)
			newRi = i2p::data::netdb.AddRouterInfo (buf + 2, static_cast<int> (len - 2), updated);

		if (newRi)
		{
			// PeerState2.gotRI honors the request-flood bit: only a newly
			// accepted/replaced RouterInfo is forwarded, and Java additionally
			// requires floodfillEnabled() plus a spare per-key FloodThrottler
			// budget. A replayed old RI must not cause repeated flooding.
			if (updated && (buf[0] & SSU2_ROUTER_INFO_FLAG_REQUEST_FLOOD))
				i2p::data::netdb.FloodRouterInfoConditional (newRi);
			auto remoteIdentity = GetRemoteIdentity ();
			if (remoteIdentity && remoteIdentity->GetIdentHash () == newRi->GetIdentHash ())
			{
				// peer's RouterInfo update
				SetRemoteIdentity (newRi->GetIdentity ());
				auto address
					= m_RemoteEndpoint.address ().is_v6 () ? newRi->GetSSU2V6Address () : newRi->GetSSU2V4Address ();
				if (address)
				{
					SetAddress (address);
					if (IsOutgoing () && m_RelayTag && !address->IsIntroducer ())
						m_RelayTag = 0; // not longer introducer
				}
			}
			i2p::transport::transports.UpdatePeerParams (newRi);
		}
	}

	void SSU2Session::HandleAck (const uint8_t * buf, size_t len)
	{
		// Java SSU2Payload requires ACK length 5 + 2*n; an odd range byte is
		// malformed, not a shorter ACK. Validate before using ACK0 as the
		// handshake confirmation.
		if (len < 5 || (len % 2) != 1)
		{
			LogPrint (eLogWarning, "SSU2: Bad ACK block length ", len);
			return;
		}
		const bool sessionConfirmedSent = m_State == eSSU2SessionStateSessionConfirmedSent;
		if (m_SentPackets.empty () && !sessionConfirmedSent) return;
		// acnt
		uint32_t ackThrough = bufbe32toh (buf);
		uint32_t acnt = buf[4];
		// We do not negotiate a non-zero First Packet Number, so acnt cannot
		// reach below packet 0. Java SSU2Bitfield throws for the same input.
		if (acnt > ackThrough)
		{
			LogPrint (eLogWarning, "SSU2: ACK acnt ", acnt, " exceeds ack through ", ackThrough);
			return;
		}
		// Process every ACK first, including while SessionConfirmed is
		// unacknowledged: a non-covering ACK for a later packet is still valid
		// and must retire those packets and feed the RTT sampler. The previous
		// early return discarded them and left stale send timestamps behind.
		uint32_t firstPacketNum = ackThrough - acnt;
		HandleAckRange (firstPacketNum, ackThrough, i2p::util::GetMillisecondsSinceEpoch ()); // acnt
		// ranges
		len -= 5;
		if (len && !m_SentPackets.empty ()) // don't handle ranges if nothing to acknowledge
		{
			const uint8_t * ranges = buf + 5;
			while (len >= 2 && firstPacketNum)
			{
				uint32_t nacks = *ranges++;
				uint32_t acks = *ranges++;
				len -= 2;
				if (!nacks && !acks) break; // spec: both bytes may not be zero
				const uint64_t lastBelow = (uint64_t)firstPacketNum - 1;
				if (nacks > lastBelow) break; // would run below packet 0
				uint32_t lastPacketNum = (uint32_t)(lastBelow - nacks);
				if (acks > (uint64_t)lastPacketNum + 1) break; // would underflow
				firstPacketNum = lastPacketNum + 1 - acks;
				HandleAckRange (firstPacketNum, lastPacketNum, 0);
			}
		}
		if (!sessionConfirmedSent) return;
		// Only an ACK that actually covers packet 0 (the SessionConfirmed)
		// completes Alice's handshake; Java waits for the ACK of packet 0 as
		// well. The ACK handling above ran first, so any tracked packet 0 is
		// retired (and its RTT sample taken) before Established() resets the
		// timers; SessionConfirmed itself is retransmitted from
		// m_SentHandshakePacket, not from m_SentPackets.
		if (!IsSSU2AckCoveringPacketZero (ackThrough, acnt)) return;
		Established ();
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
					const double sample = (double)(ts - it1->second->sendTime);
					if (m_RTT != SSU2_UNKNOWN_RTT)
					{
						// RFC 6298: beta = 1/4, alpha = 1/8, K = 4.
						m_RTTDeviation = 0.75 * m_RTTDeviation + 0.25 * std::abs (sample - m_RTT);
						m_RTT = SSU2_RTT_EWMA_ALPHA * sample + (1.0 - SSU2_RTT_EWMA_ALPHA) * m_RTT;
					}
					else
					{
						m_RTT = sample;
						m_RTTDeviation = sample / 2;
					}
					m_RTO = (size_t)(m_RTT + 4.0 * m_RTTDeviation);
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
		// Address handling updates RouterContext. Accept it only in the
		// authenticated contexts where the protocol places it (SessionCreated,
		// Retry and the established data phase) and only when the peer identity
		// is known. The state whitelist already excludes it elsewhere; this is
		// the independent defensive guard.
		if (!IsSSU2AddressBlockContextAllowed (m_State.load (), GetRemoteIdentity () != nullptr))
		{
			LogPrint (eLogWarning, "SSU2: Address block ignored in state ", (int)m_State.load ());
			return;
		}
		boost::asio::ip::udp::endpoint ep;
		if (ExtractEndpoint (buf, len, ep))
		{
			// The Address block reports the recipient's apparent endpoint, so it
			// must use the same address family as this session. Java only stores
			// the value (PeerState2.gotAddress(), SSU2Payload.java BLOCK_ADDRESS);
			// this implementation actively updates RouterContext, so an
			// unvalidated cross-family value could overwrite the wrong family's
			// external address. The SSU2 spec also permits treating a new address
			// in the other family as invalid. Ignore the block rather than marking
			// the whole message invalid, matching Java's non-fatal handling.
			bool isV4 = ep.address ().is_v4 ();
			// m_Address identifies the peer's SSU2 address for this session. Use
			// its family when it is unambiguous; fall back to the UDP endpoint
			// family for a dual/unspecified address object.
			bool sessionV4 = m_RemoteEndpoint.address ().is_v4 ();
			if (m_Address && m_Address->IsV4 () != m_Address->IsV6 ()) sessionV4 = m_Address->IsV4 ();
			if (isV4 != sessionV4)
			{
				LogPrint (eLogWarning, "SSU2: Ignoring Address block for ", ep, " in ", sessionV4 ? "IPv4" : "IPv6",
					" session");
				return;
			}
			// Java PeerState2.gotAddress() only records the reported value for
			// NAT/PeerTest analysis; an ordinary SSU2 session cannot publish a
			// new external host. Restrict RouterInfo mutation to the PeerTest
			// state, where the endpoint is corroborated by the peer-test flow.
			if (m_State != eSSU2SessionStatePeerTest)
			{
				LogPrint (eLogDebug, "SSU2: Address observation ", ep, " ignored outside PeerTest");
				return;
			}
			LogPrint (eLogInfo, "SSU2: Our external address is ", ep);
			if (!i2p::transport::transports.IsInReservedRange (ep.address ()))
			{
				i2p::context.UpdateAddress (ep.address ());
				// check our port
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
		else
		{
			// Java SSU2Payload BLOCK_ADDRESS rejects any length other than
			// 6 (IPv4) or 18 (IPv6) by throwing, so the packet is dropped.
			m_IsInvalidMessage = true;
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
			m->lastFragmentNum = -1;
			m->outOfSequenceBytes = 0;
			m->numOutOfSequenceFragments = 0;
			m_IncompleteMessages.emplace (msgID, m);
		}
		m->msg = msg;
		m->nextFragmentNum = 1;
		m->lastFragmentInsertTime = static_cast<uint32_t> (i2p::util::GetSecondsSinceEpoch ());
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
		if (len <= 5) // Java SSU2Payload: len <= 5 is invalid for FOLLOWON
		{
			LogPrint (eLogWarning, "SSU2: Follow-on fragment size ", len, " is too short");
			m_IsInvalidMessage = true;
			return;
		}
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
				if (it->second->lastFragmentNum >= 0 && fragmentNum >= it->second->lastFragmentNum)
				{
					LogPrint (eLogWarning, "SSU2: Fragment ", fragmentNum, " after last fragment ",
						it->second->lastFragmentNum);
					return;
				}
				// in sequence
				if (!it->second->AttachNextFragment (buf + 5, len - 5))
				{
					m_IncompleteMessages.erase (it);
					return;
				}
				if (isLast)
				{
					it->second->lastFragmentNum = fragmentNum;
					it->second->msg->FromNTCP2 ();
					HandleI2NPMsg (std::move (it->second->msg));
					m_IncompleteMessages.erase (it);
				}
				else
				{
					if (it->second->ConcatOutOfSequenceFragments ())
					{
						// The completed message still carries the compact NTCP2
						// header used on the wire.
						it->second->msg->FromNTCP2 ();
						HandleI2NPMsg (std::move (it->second->msg));
						m_IncompleteMessages.erase (it);
					}
					else
						it->second->lastFragmentInsertTime = static_cast<uint32_t> (i2p::util::GetSecondsSinceEpoch ());
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
			msg->lastFragmentNum = -1;
			msg->outOfSequenceBytes = 0;
			msg->numOutOfSequenceFragments = 0;
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
			packet->payloadSize += CreateRelayResponseBlock (
				packet->payload + packet->payloadSize, m_MaxPayloadSize - packet->payloadSize, code, nonce, 0, false);
			packet->payloadSize
				+= CreatePaddingBlock (packet->payload + packet->payloadSize, m_MaxPayloadSize - packet->payloadSize);
			uint32_t packetNum = 0;
			if (!SendData (packet->payload, packet->payloadSize, 0, &packetNum)) return;
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
		if (i2p::data::IsRouterBanned (charlie->GetIdentHash ())
			|| i2p::data::IsOperatorBlockedRouter (charlie->GetIdentHash ()))
		{
			LogPrint (eLogWarning, "SSU2: RelayRequest relay tag belongs to a banned or blocked router");
			sendRelayResponse (eSSU2RelayResponseCodeBobRelayTagNotFound);
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
		if (session->m_RelaySessions
				.emplace (nonce, RelaySession { shared_from_this (), mts / 1000, eSSU2RelayRoleBob })
				.second)
		{
			// send relay intro to Charlie
			auto r = i2p::data::netdb.FindRouter (alice->GetIdentHash ()); // Alice's RI
			if (r && (r->IsUnreachable () || !i2p::data::netdb.PopulateRouterInfoBuffer (r))) r = nullptr;
			if (!r) LogPrint (eLogWarning, "SSU2: RelayRequest Alice's router info not found");

			auto packet = m_Server.GetSentPacketsPool ().AcquireShared ();
			// RelayIntro needs a block header (3), flag (1), Alice hash (32) and
			// the RelayRequest data after its flag (len - 1): len + 35 bytes.
			size_t riMaxLen = (m_MaxPayloadSize > len + 35) ? m_MaxPayloadSize - len - 35 : 0;
			packet->payloadSize = r ? CreateRouterInfoBlock (packet->payload, riMaxLen, r) : 0;
			if (!packet->payloadSize && r) session->SendFragmentedMessage (CreateDatabaseStoreMsg (r));
			const size_t relayIntroSize = CreateRelayIntroBlock (
				packet->payload + packet->payloadSize, m_MaxPayloadSize - packet->payloadSize, buf + 1, len - 1);
			if (!relayIntroSize)
			{
				LogPrint (eLogWarning, "SSU2: RelayIntro does not fit with RouterInfo, dropping relay");
				return;
			}
			packet->payloadSize += relayIntroSize;
			if (packet->payloadSize < m_MaxPayloadSize)
				packet->payloadSize += CreatePaddingBlock (
					packet->payload + packet->payloadSize, m_MaxPayloadSize - packet->payloadSize);
			uint32_t packetNum = 0;
			if (!session->SendData (packet->payload, packet->payloadSize, 0, &packetNum)) return;
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
		auto bob = GetRemoteIdentity ();
		if (!bob)
		{
			LogPrint (eLogWarning, "SSU2: RelayIntro without Bob identity");
			return;
		}
		if (i2p::data::IsRouterBanned (bob->GetIdentHash ())
			|| i2p::data::IsOperatorBlockedRouter (bob->GetIdentHash ()))
		{
			LogPrint (eLogWarning, "SSU2: RelayIntro from banned or operator-blocked Bob ignored");
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
			SignedData<192> s;
			s.Insert ((const uint8_t *)"RelayRequestData", 16); // prologue
			s.Insert (bob->GetIdentHash (), 32); // bhash
			s.Insert (i2p::context.GetIdentHash (), 32); // chash
			s.Insert (buf + 33, 14); // nonce, relay tag, timestamp, ver, asz
			s.Insert (buf + 47, asz); // Alice Port, Alice IP
			const bool aliceBlocked = i2p::data::IsRouterBanned (r->GetIdentHash ())
				|| i2p::data::IsOperatorBlockedRouter (r->GetIdentHash ());
			if (aliceBlocked)
			{
				// A banned or operator-blocked router is never introduced, even
				// with a valid signature: no HolePunch and no fresh token are
				// produced for it.
				LogPrint (eLogInfo, "SSU2: RelayIntro Alice is banned or operator-blocked");
				code = eSSU2RelayResponseCodeCharlieAliceIsUnknown;
			}
			else if (s.Verify (r->GetIdentity (), buf + 47 + asz))
			{
				// obtain and check endpoint and address for HolePunch
				if (ExtractEndpoint (buf + 47, asz, ep))
				{
					if (IsValidPeerTestEndpoint (ep))
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
		if (code == eSSU2RelayResponseCodeAccept && addr && !m_Server.AllowRelayHolePunch (ep))
		{
			LogPrint (eLogInfo, "SSU2: RelayIntro rate limit for ", ep);
			code = eSSU2RelayResponseCodeCharlieLimit;
		}
		// send relay response to Bob. Do not mint a token for a rejected intro.
		auto packet = m_Server.GetSentPacketsPool ().AcquireShared ();
		uint32_t nonce = bufbe32toh (buf + 33);
		const uint64_t incomingToken = code == eSSU2RelayResponseCodeAccept ? m_Server.GetIncomingToken (ep) : 0;
		packet->payloadSize = CreateRelayResponseBlock (
			packet->payload, m_MaxPayloadSize, code, nonce, incomingToken, ep.address ().is_v4 ());
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
		uint32_t packetNum = 0;
		if (!SendData (packet->payload, packet->payloadSize, 0, &packetNum)) return;
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
		// eSSU2SessionStateIntroduced is required here: Charlie's HolePunch reply
		// arrives while Bob is still waiting for the introducer's relay response,
		// and upstream 54068be9 only applies the established-state guard after the
		// Introduced branch has been handled. Rejecting it earlier would break
		// hole punching.
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
				const auto introducers = address->ssu->GetIntroducers ();
				for (const auto & introducer : introducers)
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
			m_Server.UpdateOutgoingToken (m_RemoteEndpoint, token,
				static_cast<uint32_t> (i2p::util::GetSecondsSinceEpoch () + SSU2_TOKEN_EXPIRATION_TIMEOUT));
			ConnectAfterIntroduction ();
			return;
		}
		auto it = m_RelaySessions.find (nonce);
		if (it != m_RelaySessions.end ())
		{
			auto relaySession = it->second.session;
			const auto role = it->second.role;
			m_RelaySessions.erase (it);
			if (!relaySession)
				LogPrint (eLogDebug, "SSU2: RelayResponse unknown session for nonce ", nonce);
			else if (role == eSSU2RelayRoleBob)
			{
				// we are Bob, message from Charlie. Branch on the stored role,
				// never on IsEstablished(): a session that closes between the
				// request and the response must not receive Alice's traffic.
				if (!relaySession->IsEstablished ())
				{
					LogPrint (eLogInfo, "SSU2: RelayResponse from Charlie has no established Alice session");
					return;
				}
				size_t maxPayloadSize = relaySession->GetMaxPayloadSize ();
				if (len + 3 > maxPayloadSize)
				{
					LogPrint (eLogWarning, "SSU2: RelayResponse block size ", len, " is too long");
					return;
				}
				auto packet = m_Server.GetSentPacketsPool ().AcquireShared ();
				uint8_t * payload = packet->payload;
				payload[0] = eSSU2BlkRelayResponse;
				htobe16buf (payload + 1, static_cast<uint16_t> (len));
				memcpy (payload + 3, buf, len); // forward to Alice as is
				packet->payloadSize = len + 3;
				packet->payloadSize
					+= CreatePaddingBlock (payload + packet->payloadSize, maxPayloadSize - packet->payloadSize);
				uint32_t packetNum = 0;
				if (relaySession->SendData (packet->payload, packet->payloadSize, 0, &packetNum)
					&& m_RemoteVersion >= SSU2_MIN_RELAY_RESPONSE_RESEND_VERSION)
				{
					// sometimes Alice doesn't ack this RelayResponse in older versions
					packet->sendTime = i2p::util::GetMillisecondsSinceEpoch ();
					relaySession->m_SentPackets.emplace (packetNum, packet);
				}
			}
			else
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
								static_cast<uint32_t> (
									i2p::util::GetSecondsSinceEpoch () + SSU2_TOKEN_EXPIRATION_TIMEOUT));
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
		}
		else
			LogPrint (eLogDebug, "SSU2: RelayResponse unknown nonce ", nonce);
	}

	void SSU2Session::HandlePeerTest (const uint8_t * buf, size_t len)
	{
		// msgs 1-4. Layout: msg, code, flag [, 32-byte hash for 2/4],
		// ver, nonce, timestamp, asz, endpoint[, signature].
		// PeerTest blocks in this session type are data-phase only and are
		// forwarded to a third router. Never process them before the peer is
		// authenticated, and never dereference a missing identity.
		auto remoteIdentity = GetRemoteIdentity ();
		if (!IsSSU2EstablishedPayloadContext (m_State.load (), remoteIdentity != nullptr))
		{
			LogPrint (eLogWarning, "SSU2: PeerTest ignored in state ", (int)m_State.load ());
			return;
		}
		if (len < 3)
		{
			LogPrint (eLogWarning, "SSU2: PeerTest block is too short ", len);
			return;
		}
		uint8_t msg = buf[0];
		if (msg < 1 || msg > 4)
		{
			LogPrint (eLogWarning, "SSU2: PeerTest unexpected msg num ", (int)msg);
			return;
		}
		size_t offset = 3; // points to signed data
		if (msg == 2 || msg == 4) offset += 32; // hash is presented for msg 2 and 4 only
		if (len < offset + 10)
		{
			LogPrint (eLogWarning, "SSU2: PeerTest block is too short for signed data ", len);
			return;
		}
		// PeerTest always sends version 2 (Java PacketBuilder2), but a peer that
		// negotiated a post-quantum session may advertise 3 or 4 here.
		if (!IsSSU2OutOfSessionVersionAccepted (buf[offset]))
		{
			LogPrint (eLogWarning, "SSU2: PeerTest has wrong version ", (int)buf[offset]);
			return;
		}
		uint8_t asz = buf[offset + 9];
		if (asz != 0 && asz != 6 && asz != 18)
		{
			LogPrint (eLogWarning, "SSU2: PeerTest invalid endpoint size ", (int)asz);
			return;
		}
		if (len < offset + 10 + (size_t)asz)
		{
			LogPrint (eLogWarning, "SSU2: PeerTest endpoint is truncated");
			return;
		}
		// PeerTestManager rejects a signed timestamp outside +/-2 minutes on
		// every message, before any state change or forwarding.
		const int64_t peerTestSkew = static_cast<int64_t> (bufbe32toh (buf + offset + 5))
			- static_cast<int64_t> (i2p::util::GetSecondsSinceEpoch ());
		if (peerTestSkew > SSU2_RELAY_MAX_SKEW || peerTestSkew < -SSU2_RELAY_MAX_SKEW)
		{
			LogPrint (eLogWarning, "SSU2: PeerTest timestamp skew ", peerTestSkew, " seconds");
			return;
		}
		if (msg == 1)
		{
			boost::asio::ip::udp::endpoint testEndpoint;
			if (!ExtractEndpoint (buf + offset + 10, asz, testEndpoint) || !IsValidPeerTestEndpoint (testEndpoint)
				|| !SamePeerTestAddress (testEndpoint.address (), m_RemoteEndpoint.address ()))
			{
				LogPrint (eLogWarning, "SSU2: PeerTest msg 1 target does not match the authenticated source");
				return;
			}
		}
		if (msg == 1 || msg == 2)
		{
			if (buf[1] != 0)
			{
				LogPrint (eLogWarning, "SSU2: PeerTest msg ", (int)msg, " has non-zero status ", (int)buf[1]);
				return;
			}
			if (asz != 6 && asz != 18)
			{
				LogPrint (eLogWarning, "SSU2: PeerTest msg ", (int)msg, " has no test address");
				return;
			}
			// Java PeerTestManager admits 12 PeerTest msgs 1/2 per source
			// prefix (IPv4 /32, IPv6 /64) per 10 minutes. Charge it after the
			// fixed-field validation so malformed packets cannot exhaust the
			// budget of a legitimate peer.
			if (!m_Server.AllowPeerTestMessage (
					m_RemoteEndpoint.address (), static_cast<uint64_t> (i2p::util::GetSecondsSinceEpoch ())))
			{
				LogPrint (
					eLogWarning, "SSU2: PeerTest msg ", (int)msg, " rate limit for ", m_RemoteEndpoint.address ());
				return;
			}
		}
		auto ts = i2p::util::GetMillisecondsSinceEpoch ();
		uint32_t nonce = bufbe32toh (buf + offset + 1);
		// Bob rejects a msg 1 with a msg 4 code and Alice's signed data echoed
		// back. Never evict another pending test to make room.
		auto sendPeerTest4Error = [this, ts, buf, len, offset] (SSU2PeerTestCode code)
		{
			auto packet = m_Server.GetSentPacketsPool ().AcquireShared ();
			uint8_t zeroHash[32] = { 0 };
			packet->payloadSize = CreatePeerTestBlock (
				packet->payload, m_MaxPayloadSize, 4, code, zeroHash, buf + offset, len - offset);
			packet->payloadSize
				+= CreatePaddingBlock (packet->payload + packet->payloadSize, m_MaxPayloadSize - packet->payloadSize);
			uint32_t packetNum = 0;
			if (SendData (packet->payload, packet->payloadSize, 0, &packetNum))
			{
				packet->sendTime = ts;
				m_SentPackets.emplace (packetNum, packet);
			}
		};
		switch (msg) // msg
		{
		case 1: // Bob from Alice
		{
			auto session = m_Server.GetRandomPeerTestSession (
				static_cast<i2p::data::RouterInfo::CompatibleTransports> (
					(asz == 6) ? i2p::data::RouterInfo::eSSU2V4 : i2p::data::RouterInfo::eSSU2V6),
				GetRemoteIdentity ()->GetIdentHash ());
			if (session) // session with Charlie
			{
				const auto admission = m_Server.AddPeerTest (nonce, shared_from_this (), ts / 1000);
				if (admission == SSU2PeerTestAdmission::Duplicate)
					LogPrint (eLogInfo, "SSU2: Peer test 1 nonce ", nonce, " already exists. Ignored");
				else if (admission == SSU2PeerTestAdmission::LimitExceeded)
				{
					LogPrint (eLogInfo, "SSU2: Too many pending peer tests, rejecting nonce ", nonce);
					sendPeerTest4Error (eSSU2PeerTestCodeBobLimitExceeded);
				}
				else
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
						uint32_t packetNum = 0;
						if (session->SendData (
								packet->payload, packet->payloadSize, SSU2_FLAG_IMMEDIATE_ACK_REQUESTED, &packetNum))
						{
							packet->sendTime = ts;
							session->m_SentPackets.emplace (packetNum, packet);
						}
						packet = m_Server.GetSentPacketsPool ().AcquireShared (); // new packet
					}
					// PeerTest to Charlie
					packet->payloadSize += CreatePeerTestBlock (packet->payload + packet->payloadSize,
						m_MaxPayloadSize - packet->payloadSize, 2, eSSU2PeerTestCodeAccept,
						GetRemoteIdentity ()->GetIdentHash (), buf + offset, len - offset);
					packet->payloadSize += CreatePaddingBlock (
						packet->payload + packet->payloadSize, m_MaxPayloadSize - packet->payloadSize);
					uint32_t packetNum = 0;
					if (session->SendData (
							packet->payload, packet->payloadSize, SSU2_FLAG_IMMEDIATE_ACK_REQUESTED, &packetNum))
					{
						packet->sendTime = ts;
						session->m_SentPackets.emplace (packetNum, packet);
					}
				}
			}
			else
			{
				// Charlie not found, send error back to Alice
				sendPeerTest4Error (eSSU2PeerTestCodeBobNoCharlieAvailable);
			}
			break;
		}
		case 2: // Charlie from Bob
		{
			// Bob (the authenticated session peer) must not be banned or
			// operator-blocked before Charlie signs anything on his behalf.
			auto bob = GetRemoteIdentity ();
			if (!bob)
			{
				LogPrint (eLogWarning, "SSU2: PeerTest msg 2 without a peer identity ignored");
				return;
			}
			if (i2p::data::IsRouterBanned (bob->GetIdentHash ())
				|| i2p::data::IsOperatorBlockedRouter (bob->GetIdentHash ()))
			{
				LogPrint (eLogWarning, "SSU2: PeerTest msg 2 from banned or operator-blocked Bob ignored");
				return;
			}
			// Sign the forwarded request with Charlie's key. The incoming
			// signature is Alice's and may use a different signature type, so
			// its length must not be compared with Charlie's signature length.
			size_t l = asz + 10 + i2p::context.GetIdentity ()->GetSignatureLen ();
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
						// Never open a test toward Alice when her router hash is
						// excluded by the operator (reason 69, the existing
						// "Alice is banned" code).
						if (i2p::data::IsRouterBanned (r->GetIdentHash ())
							|| i2p::data::IsOperatorBlockedRouter (r->GetIdentHash ()))
						{
							LogPrint (eLogInfo, "SSU2: PeerTest msg 2 Alice is banned or operator-blocked");
							code = eSSU2PeerTestCodeCharlieAliceIsBanned;
						}
						else if (IsSSU2PeerTestFamilyAvailable (m_Server.FindSession (r->GetIdentHash ()), asz == 6))
						{
							boost::asio::ip::udp::endpoint ep;
							std::shared_ptr<const i2p::data::RouterInfo::Address> addr;
							const bool endpointValid
								= ExtractEndpoint (buf + offset + 10, asz, ep) && IsValidPeerTestEndpoint (ep);
							if (endpointValid) addr = r->GetSSU2Address (ep.address ().is_v4 ());
							const bool supported = endpointValid && m_Server.IsSupported (ep.address ());
							// This endpoint is where Charlie will send msg 5, so
							// it must be routable, supported and not blocked.
							if (AllowSSU2PeerTestTarget (endpointValid, supported,
									endpointValid && i2p::data::IsOperatorBlockedAddress (ep.address ()))
								&& addr && i2p::context.GetRouterInfo ().IsSSU2PeerTesting (ep.address ().is_v4 ()))
							{
								if (!m_Server.IsConnectedRecently (ep)) // no alive hole punch
								{
									if (!m_Server.IsPeerTestSessionAdmissionAllowed ())
									{
										LogPrint (eLogWarning,
											"SSU2: Too many PeerTest sessions, refusing msg 2 for nonce ", nonce);
										code = eSSU2PeerTestCodeCharlieLimitExceeded;
									}
									else
									{
										// send msg 5 to Alice
										auto session = std::make_shared<SSU2PeerTestSession> (
											m_Server, 0, htobe64 (((uint64_t)nonce << 32) | nonce));
										session->SetRemoteEndpoint (ep); // might be different
										if (m_Server.AddSession (session))
											session->SendPeerTest (
												5, newSignedData.data (), newSignedData.size (), addr);
										else
										{
											LogPrint (eLogWarning,
												"SSU2: Could not register PeerTest session for nonce ", nonce);
											code = eSSU2PeerTestCodeCharlieLimitExceeded;
										}
									}
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
			uint32_t packetNum = 0;
			if (SendData (packet->payload, packet->payloadSize, 0, &packetNum))
			{
				packet->sendTime = ts;
				m_SentPackets.emplace (packetNum, packet);
			}
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
					uint32_t packetNum = 0;
					if (aliceSession->SendData (packet->payload, packet->payloadSize, 0, &packetNum))
					{
						packet->sendTime = ts;
						aliceSession->m_SentPackets.emplace (packetNum, packet);
					}
					packet = m_Server.GetSentPacketsPool ().AcquireShared ();
				}
				// PeerTest to Alice. Pass the remaining capacity, not the full
				// packet size, or a RouterInfo + fixed fields can overrun it.
				const size_t peerTestSize = CreatePeerTestBlock (packet->payload + packet->payloadSize,
					m_MaxPayloadSize - packet->payloadSize, 4, (SSU2PeerTestCode)buf[1],
					GetRemoteIdentity ()->GetIdentHash (), buf + offset, len - offset);
				if (!peerTestSize)
				{
					LogPrint (eLogWarning, "SSU2: PeerTest 4 does not fit in the data packet");
					return;
				}
				packet->payloadSize += peerTestSize;
				if (packet->payloadSize < m_MaxPayloadSize)
					packet->payloadSize += CreatePaddingBlock (
						packet->payload + packet->payloadSize, m_MaxPayloadSize - packet->payloadSize);
				uint32_t packetNum = 0;
				if (aliceSession->SendData (packet->payload, packet->payloadSize, 0, &packetNum))
				{
					packet->sendTime = ts;
					aliceSession->m_SentPackets.emplace (packetNum, packet);
				}
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
					if (asz != 6 && asz != 18)
					{
						LogPrint (eLogWarning, "SSU2: Peer test 4 accept has no valid endpoint");
						session->Done ();
						return;
					}
					// Bob and Charlie must both be usable before Alice spends
					// any state or sends msg 6.
					auto bobIdentity = GetRemoteIdentity ();
					const i2p::data::IdentHash charlieHash (buf + 3);
					const auto & bobHash = bobIdentity->GetIdentHash ();
					if (i2p::data::IsRouterBanned (bobHash) || i2p::data::IsOperatorBlockedRouter (bobHash)
						|| i2p::data::IsRouterBanned (charlieHash) || i2p::data::IsOperatorBlockedRouter (charlieHash))
					{
						LogPrint (eLogInfo, "SSU2: Peer test 4 from banned or operator-blocked router ignored");
						session->Done ();
						return;
					}
					auto r = i2p::data::netdb.FindRouter (buf + 3); // find Charlie
					if (r && len >= offset + 10)
					{
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
							// Testing is published only for a signature-verified
							// msg 4 and cleared on every exit path below.
							if (GetRouterStatus () == eRouterStatusUnknown) SetTestingState (true);
							[[maybe_unused]] modern::ScopeExit clearTesting ([this] () { SetTestingState (false); });
							session->SetRemoteIdentity (r->GetIdentity ());
							// m_Address identifies this session's peer; fall back to
							// the UDP endpoint family if an older/testing session
							// has no RouterInfo-derived address yet.
							bool v4 = m_Address ? m_Address->IsV4 () : m_RemoteEndpoint.address ().is_v4 ();
							auto addr = r->GetSSU2Address (v4);
							if (addr && addr->IsPeerTesting ())
							{
								if (session->GetMsgNumReceived () >= 5)
								{
									// msg 5 already received and we know remote endpoint
									if (session->GetMsgNumReceived () == 5)
									{
										const auto & charlieEndpoint = session->GetRemoteEndpoint ();
										if (!AllowSSU2PeerTestTarget (IsValidPeerTestEndpoint (charlieEndpoint),
												m_Server.IsSupported (charlieEndpoint.address ()),
												i2p::data::IsOperatorBlockedAddress (charlieEndpoint.address ())))
										{
											LogPrint (eLogWarning, "SSU2: PeerTest 4 Charlie endpoint ",
												charlieEndpoint, " is not acceptable");
											session->Done ();
											return;
										}
										if (!session->IsConnectedRecently ()) SetRouterStatus (eRouterStatusOK);
										session->SetCharlieEndpoint (charlieEndpoint);
										// send msg 6 immediately
										session->SendPeerTest (6, buf + offset, len - offset, addr);
									}
									else
										LogPrint (eLogWarning, "SSU2: PeerTest 4 received, but msg ",
											session->GetMsgNumReceived (), " already received");
								}
								else
								{
									session->SetAddress (addr);
									if (GetTestingState ())
									{
										// schedule msg 6 with delay
										if (!addr->host.is_unspecified () && addr->port)
										{
											const boost::asio::ip::udp::endpoint charlieEndpoint (
												addr->host, static_cast<uint16_t> (addr->port));
											if (!AllowSSU2PeerTestTarget (IsValidPeerTestEndpoint (charlieEndpoint),
													m_Server.IsSupported (charlieEndpoint.address ()),
													i2p::data::IsOperatorBlockedAddress (charlieEndpoint.address ())))
											{
												LogPrint (eLogWarning, "SSU2: PeerTest 4 target ", charlieEndpoint,
													" is not acceptable");
												session->Done ();
												return;
											}
											session->SetRemoteEndpoint (charlieEndpoint);
											session->SetCharlieEndpoint (charlieEndpoint);
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
		// I2NP injection is a data-phase operation. Fragment reassembly calls
		// this too, so guard here as well as in HandlePayload and never queue
		// messages for an unauthenticated session.
		if (!IsSSU2EstablishedPayloadContext (m_State.load (), GetRemoteIdentity () != nullptr))
		{
			LogPrint (eLogWarning, "SSU2: I2NP message ignored in state ", (int)m_State.load ());
			return;
		}
		uint32_t msgID = msg->GetMsgID ();
		// m_LastActivityTimestamp is updated in ProcessData before
		if (!msg->IsExpired (GetLastActivityTimestamp () * 1000LL)) // to milliseconds
		{
			// Bound the duplicate-detection cache between CleanUp() runs; a peer
			// that sends many unique messages must not grow it without limit.
			if (m_ReceivedI2NPMsgIDs.size () >= SSU2_MAX_NUM_RECEIVED_I2NP_MSGIDS) m_ReceivedI2NPMsgIDs.clear ();
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
			ep = boost::asio::ip::udp::endpoint (boost::asio::ip::address_v4 (bytes), static_cast<uint16_t> (port));
		}
		else if (size == 18)
		{
			boost::asio::ip::address_v6::bytes_type bytes;
			memcpy (bytes.data (), buf + 2, 16);
			ep = boost::asio::ip::udp::endpoint (boost::asio::ip::address_v6 (bytes), static_cast<uint16_t> (port));
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
			LogPrint (eLogWarning, "SSU2: Wrong address type ", i2p::util::net::FormatAddress (ep.address ()));
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
			if (!mtu) mtu = static_cast<int> (SSU2_MAX_PACKET_SIZE);
			if (mtu > (int)maxMtu) mtu = static_cast<int> (maxMtu);
			if (m_Address && m_Address->ssu && m_Address->ssu->mtu && (!mtu || m_Address->ssu->mtu < mtu))
				mtu = m_Address->ssu->mtu;
			if (mtu)
			{
				if (mtu > (int)SSU2_MAX_PACKET_SIZE) mtu = SSU2_MAX_PACKET_SIZE;
				// Keep the effective IPv6 MTU at or below 1488 bytes: the IPv6
				// header plus UDP header leaves no room above that inside a
				// 1500-byte path MTU, and Java PeerState2 uses the same cap.
				// The 1280 byte SSU2 floor still applies.
				if (addr->IsV6 () && mtu > SSU2_MAX_IPV6_MTU) mtu = SSU2_MAX_IPV6_MTU;
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
		htobe16buf (buf + 1, static_cast<uint16_t> (size));
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
		htobe16buf (buf + 1, static_cast<uint16_t> (size + 2)); // size
		buf[4] = 1; // frag
		return size + 5;
	}

	size_t SSU2Session::CreateAckBlock (uint8_t * buf, size_t len)
	{
		if (len < 8) return 0;
		// Defensive: the out-of-sequence set must only contain packets above
		// the contiguous receive point. A stale entry would make the range
		// arithmetic underflow.
		while (!m_OutOfSequencePackets.empty () && *m_OutOfSequencePackets.begin () <= m_ReceivePacketNum)
		{
			LogPrint (eLogError, "SSU2: Dropping stale out-of-sequence packet ", *m_OutOfSequencePackets.begin (),
				" at receive ", m_ReceivePacketNum);
			m_OutOfSequencePackets.erase (m_OutOfSequencePackets.begin ());
			m_NumRanges = 0;
		}
		buf[0] = eSSU2BlkAck;
		uint32_t ackThrough = GetHighestReceivedPacketNum ();
		htobe32buf (buf + 3, ackThrough); // Ack Through
		uint16_t acnt = 0;
		if (!ackThrough) m_NumRanges = 0;
		if (ackThrough)
		{
			if (m_OutOfSequencePackets.empty ())
			{
				acnt = (uint16_t)std::min<uint32_t> (ackThrough, SSU2_MAX_NUM_ACNT); // no gaps
				m_NumRanges = 0;
			}
			else
			{
				int maxNumRanges = static_cast<int> ((len - 8) >> 1);
				if (maxNumRanges > SSU2_MAX_NUM_ACK_RANGES) maxNumRanges = SSU2_MAX_NUM_ACK_RANGES;
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
				// A cached range list is tied to the previous ACK window and
				// to the old available space. Rebuild it if either changed.
				if ((m_NumRanges && m_NumRanges > maxNumRanges) || (m_NumRanges && acnt > SSU2_MAX_NUM_ACNT))
					m_NumRanges = 0;
				// ranges
				if (!m_NumRanges)
				{
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
						for (int i = 0; i < d.quot && numRanges < maxNumRanges; i++)
						{
							m_Ranges[numRanges * 2] = 0;
							m_Ranges[numRanges * 2 + 1] = SSU2_MAX_NUM_ACNT; // NACKs 0, Acks 255
							numRanges++;
						}
						if (d.rem > 0 && numRanges < maxNumRanges)
						{
							m_Ranges[numRanges * 2] = 0;
							m_Ranges[numRanges * 2 + 1] = static_cast<uint8_t> (d.rem);
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
							while (lastNum - (*it) > SSU2_MAX_NUM_ACNT && numRanges < maxNumRanges)
							{
								m_Ranges[numRanges * 2] = SSU2_MAX_NUM_ACNT;
								m_Ranges[numRanges * 2 + 1] = 0; // NACKs 255, Acks 0
								lastNum -= SSU2_MAX_NUM_ACNT;
								numRanges++;
								numPackets += SSU2_MAX_NUM_ACNT;
							}
						}
						// Never write the final NACK/ACK pair without a free range
						// slot. Without this guard a crafted out-of-sequence set
						// could advance numRanges past maxNumRanges and write past
						// m_Ranges.
						if (numRanges >= maxNumRanges) break;
						// NACKs and Acks ranges
						m_Ranges[numRanges * 2] = static_cast<uint8_t> (lastNum - (*it) - 1); // NACKs
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
						while (numAcks > SSU2_MAX_NUM_ACNT && numRanges < maxNumRanges)
						{
							// Acks only ranges
							m_Ranges[numRanges * 2 + 1] = SSU2_MAX_NUM_ACNT; // Acks 255
							numAcks -= SSU2_MAX_NUM_ACNT;
							numRanges++;
							numPackets += SSU2_MAX_NUM_ACNT;
							m_Ranges[numRanges * 2] = 0; // NACKs 0
							if (numRanges >= maxNumRanges || numPackets >= SSU2_MAX_NUM_ACK_PACKETS) break;
						}
						if (numRanges >= maxNumRanges) break;
						if (numAcks > SSU2_MAX_NUM_ACNT) numAcks = SSU2_MAX_NUM_ACNT;
						m_Ranges[numRanges * 2 + 1] = (uint8_t)numAcks; // Acks
						numPackets += numAcks;
						numRanges++;
					}
					if (it == m_OutOfSequencePackets.rend () && numRanges < maxNumRanges
						&& numPackets < SSU2_MAX_NUM_ACK_PACKETS)
					{
						// Add the range between the lowest out-of-sequence packet
						// and the contiguous receive point. A gap larger than 255
						// needs NACK-only ranges first; the old code truncated it
						// to 255 and then ACKed packets that were never received.
						const uint32_t lowestOutOfSequence = *m_OutOfSequencePackets.begin ();
						if (lowestOutOfSequence <= m_ReceivePacketNum)
						{
							LogPrint (eLogError, "SSU2: Out-of-sequence set is inconsistent with receive packet ",
								m_ReceivePacketNum);
							m_OutOfSequencePackets.clear ();
							m_NumRanges = 0;
						}
						else
						{
							uint64_t nacks64 = (uint64_t)lowestOutOfSequence - (uint64_t)m_ReceivePacketNum - 1;
							while (nacks64 > SSU2_MAX_NUM_ACNT && numRanges < maxNumRanges
								&& numPackets + SSU2_MAX_NUM_ACNT <= SSU2_MAX_NUM_ACK_PACKETS)
							{
								m_Ranges[numRanges * 2] = SSU2_MAX_NUM_ACNT;
								m_Ranges[numRanges * 2 + 1] = 0; // NACKs 255, Acks 0
								nacks64 -= SSU2_MAX_NUM_ACNT;
								numRanges++;
								numPackets += SSU2_MAX_NUM_ACNT;
							}
							if (nacks64 > 0 && nacks64 <= SSU2_MAX_NUM_ACNT && numRanges < maxNumRanges
								&& numPackets + nacks64 <= SSU2_MAX_NUM_ACK_PACKETS)
							{
								// Packet numbers are uint32. Do not round-trip them
								// through int: values above INT_MAX would wrap.
								const uint32_t acks = (m_ReceivePacketNum >= (uint32_t)SSU2_MAX_NUM_ACNT)
									? (uint32_t)SSU2_MAX_NUM_ACNT
									: m_ReceivePacketNum + 1;
								if (numPackets + nacks64 + acks <= SSU2_MAX_NUM_ACK_PACKETS)
								{
									m_Ranges[numRanges * 2] = (uint8_t)nacks64;
									m_Ranges[numRanges * 2 + 1] = (uint8_t)acks;
									numRanges++;
									numPackets += static_cast<int> (nacks64 + acks);
								}
							}
						}
					}
					m_NumRanges = numRanges;
				}
				if (m_NumRanges > maxNumRanges) m_NumRanges = maxNumRanges;
				if (m_NumRanges > SSU2_MAX_NUM_ACK_RANGES)
					m_NumRanges = SSU2_MAX_NUM_ACK_RANGES; // defensive: stay inside m_Ranges
				if (m_NumRanges) memcpy (buf + 8, m_Ranges, m_NumRanges * 2);
			}
		}
		// acnt is one byte on the wire. The cached-range path can leave acnt
		// above 255; truncating it would describe a different window than the
		// ranges that follow.
		if (acnt > SSU2_MAX_NUM_ACNT) acnt = SSU2_MAX_NUM_ACNT;
		buf[7] = (uint8_t)acnt; // acnt
		htobe16buf (buf + 1, static_cast<uint16_t> (5 + m_NumRanges * 2));
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
		htobe16buf (buf + 1, static_cast<uint16_t> (paddingSize));
		memset (buf + 3, 0, paddingSize);
		return paddingSize + 3;
	}

	size_t SSU2Session::CreateI2NPBlock (uint8_t * buf, size_t len, std::shared_ptr<I2NPMessage> && msg)
	{
		if (!msg || len < 3) return 0;
		msg->ToNTCP2 ();
		auto msgBuf = msg->GetNTCP2Header ();
		auto msgLen = msg->GetNTCP2Length ();
		// A truncated I2NP block is not a valid fragment. Callers must use the
		// fragmentation path when the message does not fit.
		if (msgLen + 3 > len) return 0;
		buf[0] = eSSU2BlkI2NPMessage;
		htobe16buf (buf + 1, static_cast<uint16_t> (msgLen)); // size
		memcpy (buf + 3, msgBuf, msgLen);
		return msgLen + 3;
	}

	size_t SSU2Session::CreateFirstFragmentBlock (uint8_t * buf, size_t len, std::shared_ptr<I2NPMessage> msg)
	{
		// 3 bytes block header plus a FIRSTFRAG data field. Java SSU2Payload
		// rejects data lengths <= 9, so require at least 10 data bytes.
		if (!msg || len < 13) return 0;
		msg->ToNTCP2 ();
		auto msgBuf = msg->GetNTCP2Header ();
		auto msgLen = msg->GetNTCP2Length ();
		if (msgLen + 3 <= len) return 0;
		msgLen = len - 3;
		buf[0] = eSSU2BlkFirstFragment;
		htobe16buf (buf + 1, static_cast<uint16_t> (msgLen)); // size
		memcpy (buf + 3, msgBuf, msgLen);
		msg->offset = (msgBuf - msg->buf) + msgLen;
		return msgLen + 3;
	}

	size_t SSU2Session::CreateFollowOnFragmentBlock (
		uint8_t * buf, size_t len, std::shared_ptr<I2NPMessage> msg, uint8_t & fragmentNum, uint32_t msgID)
	{
		// 3 bytes block header + frag/msgid + at least one byte of data. The
		// receiver rejects FOLLOWON data lengths <= 5.
		if (!msg || len < 9) return 0;
		if (fragmentNum >= SSU2_MAX_NUM_FRAGMENTS - 1) return 0; // next number would be rejected
		bool isLast = true;
		auto msgLen = msg->len - msg->offset;
		if (msgLen + 8 > len)
		{
			msgLen = len - 8;
			isLast = false;
		}
		if (!msgLen) return 0;
		buf[0] = eSSU2BlkFollowOnFragment;
		htobe16buf (buf + 1, static_cast<uint16_t> (msgLen + 5)); // size
		fragmentNum++;
		buf[3] = static_cast<uint8_t> (fragmentNum << 1);
		if (isLast) buf[3] |= 0x01;
		memcpy (buf + 4, &msgID, 4);
		memcpy (buf + 8, msg->buf + msg->offset, msgLen);
		msg->offset += msgLen;
		return msgLen + 8;
	}

	size_t SSU2Session::CreateRelayIntroBlock (
		uint8_t * buf, size_t len, const uint8_t * introData, size_t introDataLen)
	{
		if (len < 36) return 0; // block header + flag + Alice hash
		buf[0] = eSSU2BlkRelayIntro;
		size_t payloadSize = 1 /* flag */ + 32 /* Alice router hash */ + introDataLen;
		if (payloadSize + 3 > len) return 0;
		htobe16buf (buf + 1, static_cast<uint16_t> (payloadSize)); // size
		buf[3] = 0; // flag
		memcpy (buf + 4, GetRemoteIdentity ()->GetIdentHash (), 32); // Alice router hash
		memcpy (buf + 36, introData, introDataLen);
		return payloadSize + 3;
	}

	size_t SSU2Session::CreateRelayResponseBlock (
		uint8_t * buf, size_t len, SSU2RelayResponseCode code, uint32_t nonce, uint64_t token, bool v4)
	{
		if (len < 15) return 0; // fixed fields before the optional endpoint/signature
		buf[0] = eSSU2BlkRelayResponse;
		buf[3] = 0; // flag
		buf[4] = static_cast<uint8_t> (code); // code
		htobe32buf (buf + 5, nonce); // nonce
		htobe32buf (buf + 9, static_cast<uint32_t> (i2p::util::GetSecondsSinceEpoch ())); // timestamp
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
			csz = CreateEndpoint (
				buf + 15, len - 15, boost::asio::ip::udp::endpoint (addr->host, static_cast<uint16_t> (addr->port)));
			if (!csz)
			{
				LogPrint (eLogError, "SSU2: Can't create local endpoint for RelayResponse");
				return 0;
			}
		}
		buf[14] = static_cast<uint8_t> (csz); // csz
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
		htobe16buf (buf + 1, static_cast<uint16_t> (payloadSize)); // size
		return payloadSize + 3;
	}

	size_t SSU2Session::CreatePeerTestBlock (uint8_t * buf, size_t len, uint8_t msg, SSU2PeerTestCode code,
		const uint8_t * routerHash, const uint8_t * signedData, size_t signedDataLen)
	{
		if (len < 6) return 0; // block header + msg + code + flag
		buf[0] = eSSU2BlkPeerTest;
		size_t payloadSize = 3 /* msg, code, flag */ + signedDataLen;
		if (routerHash) payloadSize += 32; // router hash
		if (payloadSize + 3 > len) return 0;
		htobe16buf (buf + 1, static_cast<uint16_t> (payloadSize)); // size
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
		htobe32buf (signedData.data () + 5, static_cast<uint32_t> (ts));
		size_t asz = CreateEndpoint (signedData.data () + 10, signedData.size () - 10,
			boost::asio::ip::udp::endpoint (localAddress->host, static_cast<uint16_t> (localAddress->port)));
		signedData[9] = static_cast<uint8_t> (asz);
		// signature
		SignedData<128> s;
		s.Insert ((const uint8_t *)"PeerTestValidate", 16); // prologue
		s.Insert (GetRemoteIdentity ()->GetIdentHash (), 32); // bhash
		s.Insert (signedData.data (), 10 + asz); // ver, nonce, ts, asz, Alice's endpoint
		s.Sign (i2p::context.GetPrivateKeys (), signedData.data () + 10 + asz);
		return CreatePeerTestBlock (
			buf, len, 1, eSSU2PeerTestCodeAccept, nullptr, signedData.data (), 10 + asz + signatureLen);
	}

	size_t SSU2Session::CreateTerminationBlock (uint8_t * buf, size_t len)
	{
		if (len < 12) return 0;
		buf[0] = eSSU2BlkTermination;
		htobe16buf (buf + 1, 9);
		// Java PacketBuilder2 uses the highest received packet (not merely the
		// contiguous receive point) here.
		htobe64buf (buf + 3, GetHighestReceivedPacketNum ());
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
			// The packet itself may still be recorded as out of sequence (for
			// example the first packet was numbered 1 because ACK 0 was lost).
			// Remove it, and any stale lower entries, before advancing the
			// contiguous receive point.
			auto staleEnd = m_OutOfSequencePackets.upper_bound (packetNum);
			if (staleEnd != m_OutOfSequencePackets.begin ())
			{
				m_OutOfSequencePackets.erase (m_OutOfSequencePackets.begin (), staleEnd);
				m_NumRanges = 0;
			}
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
			if (m_OutOfSequencePackets.size () >= SSU2_MAX_OUT_OF_SEQUENCE_PACKETS)
				return false; // bound memory between cleanup passes
			if (m_NumRanges
				&& (m_OutOfSequencePackets.empty () || packetNum != (*m_OutOfSequencePackets.rbegin ()) + 1))
				m_NumRanges = 0; // reset ranges if received packet is not next
			if (!m_OutOfSequencePackets.insert (packetNum).second)
			{
				LogPrint (eLogDebug, "SSU2: Duplicate out-of-sequence packet ", packetNum);
				return false;
			}
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
			htobe32buf (payload + 3, static_cast<uint32_t> ((i2p::util::GetMillisecondsSinceEpoch () + 500) / 1000));
			payloadSize += 7;
			m_LastDatetimeSentPacketNum = m_SendPacketNum;
		}
		payloadSize += CreateAckBlock (payload + payloadSize, m_MaxPayloadSize - payloadSize);
		payloadSize += CreatePaddingBlock (payload + payloadSize, m_MaxPayloadSize - payloadSize);
		SendData (payload, payloadSize);
	}

	void SSU2Session::SendTermination ()
	{
		// The SSU2 spec recommends including an ACK block with Termination.
		// Java PacketBuilder2.buildSessionDestroyPacket() always does so.
		uint8_t payload[SSU2_MAX_PACKET_SIZE];
		size_t payloadSize = CreateAckBlock (payload, m_MaxPayloadSize - 15);
		payloadSize += CreateTerminationBlock (payload + payloadSize, m_MaxPayloadSize - payloadSize);
		if (!payloadSize) return;
		payloadSize += CreatePaddingBlock (payload + payloadSize, m_MaxPayloadSize - payloadSize);
		SendData (payload, payloadSize);
	}

	void SSU2Session::SendPathResponse (const uint8_t * data, size_t len)
	{
		if (!IsEstablished () || !m_Address)
		{
			LogPrint (eLogWarning, "SSU2: PathResponse ignored outside an established data session");
			return;
		}
		uint8_t payload[SSU2_MAX_PACKET_SIZE];
		size_t payloadSize = 0;
		// datetime block
		payload[0] = eSSU2BlkDateTime;
		htobe16buf (payload + 1, 4);
		htobe32buf (payload + 3, static_cast<uint32_t> ((i2p::util::GetMillisecondsSinceEpoch () + 500) / 1000));
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
		htobe16buf (payload + payloadSize + 1, static_cast<uint16_t> (len));
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
		if (m_PathChallenge) return; // one pending connection migration at a time
		const auto now = i2p::util::GetMillisecondsSinceEpoch ();
		auto state = std::make_unique<SSU2PathChallengeState> ();
		if (RAND_bytes ((uint8_t *)&state->challenge, 8) != 1)
		{
			LogPrint (eLogError, "SSU2: Can't generate PathChallenge value");
			return;
		}
		state->endpoint = to;
		state->startedTime = now;
		state->numSends = 1;
		state->nextSendTime = now + GetSSU2PathChallengeRetryDelay (state->numSends);
		state->previousWindowSize = m_WindowSize;
		state->previousMaxPayloadSize = m_MaxPayloadSize;

		AdjustMaxPayloadSize (SSU2_MIN_PACKET_SIZE); // reduce to minimum
		m_WindowSize = SSU2_MIN_WINDOW_SIZE; // reduce window to minimum
		if (!SendPathChallengePacket (*state))
		{
			// Do not leave a pending migration that was never sent.
			LogPrint (eLogWarning, "SSU2: Can't send PathChallenge to ", to);
			m_WindowSize = state->previousWindowSize;
			m_MaxPayloadSize = state->previousMaxPayloadSize;
			return;
		}
		m_PathChallenge = std::move (state);
	}

	bool SSU2Session::SendPathChallengePacket (const SSU2PathChallengeState & state)
	{
		uint8_t payload[SSU2_MAX_PACKET_SIZE];
		size_t payloadSize = 0;
		// datetime block
		payload[0] = eSSU2BlkDateTime;
		htobe16buf (payload + 1, 4);
		htobe32buf (payload + 3, static_cast<uint32_t> ((i2p::util::GetMillisecondsSinceEpoch () + 500) / 1000));
		payloadSize += 7;
		// address block with new address
		size_t addressSize = CreateAddressBlock (payload + payloadSize, m_MaxPayloadSize - payloadSize, state.endpoint);
		if (!addressSize) return false;
		payloadSize += addressSize;
		// path challenge block, always 8 bytes, reused by every retransmission
		if (payloadSize + 11 > m_MaxPayloadSize) return false;
		payload[payloadSize] = eSSU2BlkPathChallenge;
		htobe16buf (payload + payloadSize + 1, 8);
		htobuf64 (payload + payloadSize + 3, state.challenge);
		payloadSize += 11;
		// ack block
		if (payloadSize < m_MaxPayloadSize)
			payloadSize += CreateAckBlock (payload + payloadSize, m_MaxPayloadSize - payloadSize);
		// padding block
		if (payloadSize < m_MaxPayloadSize)
			payloadSize += CreatePaddingBlock (payload + payloadSize, m_MaxPayloadSize - payloadSize);
		// send to new endpoint
		auto existing = m_RemoteEndpoint;
		m_RemoteEndpoint = state.endpoint; // send path challenge to new endpoint
		bool sent = SendData (payload, payloadSize);
		m_RemoteEndpoint = existing; // restore endpoint back until path response received
		return sent;
	}

	void SSU2Session::RetransmitPathChallenge (uint64_t now)
	{
		if (!m_PathChallenge) return;
		LogPrint (eLogInfo, "SSU2: Retransmitting PathChallenge to ", m_PathChallenge->endpoint, " (",
			m_PathChallenge->numSends + 1, " of ", SSU2_MAX_PATH_CHALLENGE_SENDS, ")");
		SendPathChallengePacket (*m_PathChallenge);
		m_PathChallenge->numSends++;
		m_PathChallenge->nextSendTime = now + GetSSU2PathChallengeRetryDelay (m_PathChallenge->numSends);
	}

	void SSU2Session::CancelPathChallenge (bool migrated)
	{
		if (!m_PathChallenge) return;
		if (migrated)
		{
			m_RemoteEndpoint = m_PathChallenge->endpoint;
			// Java resets the MTU to MIN_MTU on a successful migration.
			AdjustMaxPayloadSize (SSU2_MIN_PACKET_SIZE);
			m_WindowSize = SSU2_MIN_WINDOW_SIZE;
		}
		else
		{
			// Expired or cancelled migration must not leave the reduced
			// MTU/window behind: keep using the old endpoint as before.
			m_WindowSize = m_PathChallenge->previousWindowSize;
			m_MaxPayloadSize = m_PathChallenge->previousMaxPayloadSize;
		}
		m_PathChallenge.reset (nullptr);
	}

	void SSU2Session::CleanUp (uint64_t ts)
	{
		// Java's MAX_RECEIVE_TIME is 10 s. Expiry is only observed on the next
		// server cleanup pass (SSU2_CLEANUP_INTERVAL), so the effective bound is
		// the timeout plus that interval; no per-message timer is introduced.
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
		// Never advance m_ReceivePacketNum across packets that were not
		// received. Doing so makes CreateAckBlock acknowledge gaps and makes the
		// peer erase still-unacknowledged messages from m_SentPackets. The
		// out-of-sequence set is bounded at insertion; CreateAckBlock emits
		// NACK-only ranges and drops entries that do not fit the ACK budget.
		// Missing packets are retransmitted normally.

		for (auto it = m_RelaySessions.begin (); it != m_RelaySessions.end ();)
		{
			// Bob answers immediately, Alice waits for Charlie's HolePunch or
			// Bob's RelayResponse. Expire per role on monotonic seconds.
			const int timeout = it->second.role == eSSU2RelayRoleAlice ? SSU2_RELAY_NONCE_EXPIRATION_ALICE
																	   : SSU2_RELAY_NONCE_EXPIRATION_BOB;
			if (ts > it->second.timestamp + static_cast<uint64_t> (timeout))
			{
				LogPrint (
					eLogInfo, "SSU2: Relay nonce ", it->first, " was not responded in ", timeout, " seconds, deleted");
				it = m_RelaySessions.erase (it);
			}
			else
				++it;
		}
		// Bob's RelayRequest replay cache is bounded by count as well as by a
		// monotonic window, so a peer cannot pin entries until restart.
		for (auto it = m_RecentRelayRequests.begin (); it != m_RecentRelayRequests.end ();)
		{
			if (ts > it->second + SSU2_RECENT_RELAY_REQUEST_EXPIRATION)
				it = m_RecentRelayRequests.erase (it);
			else
				++it;
		}
		if (m_PathChallenge
			&& IsSSU2PathChallengeExpired (
				i2p::util::GetMillisecondsSinceEpoch (), m_PathChallenge->startedTime, m_PathChallenge->numSends))
		{
			// A lost PathChallenge must not terminate the session. Java ends
			// only the pending migration and keeps the old endpoint.
			LogPrint (eLogInfo, "SSU2: Connection migration to ", m_PathChallenge->endpoint, " timed out; keeping ",
				m_RemoteEndpoint);
			CancelPathChallenge (false);
		}
	}

	void SSU2Session::FlushData ()
	{
		// A packet batch can terminate a session (Termination block or a bad
		// payload). Do not emit an ACK/resend afterwards from the flushed state.
		if (m_State.load () == eSSU2SessionStateTerminated) return;
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
#if OPENSSL_PQ
		switch (version)
		{
		case 3: m_Version = i2p::crypto::CanPublishPQ (3) ? 3 : 2; break;
		case 4:
			m_Version = (i2p::crypto::CanPublishPQ (4) && m_MaxPayloadSize >= SSU2_MLKEM768_MIN_PAYLOAD_SIZE) ? 4 : 2;
			break;
		default: m_Version = 2;
		}
#else
		(void)version;
		m_Version = 2;
#endif
		return m_Version == version;
	}
}
}
