/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Streaming.
 *
 * Implements Streaming packet parsing and generation, window and congestion
 * control, ACK/NACK handling, resend and pacing timers, gzip and data-message
 * plumbing, stream lifecycle, and destination-level admission of incoming
 * streams with per-key rate limiting.
 *
 */

#include <algorithm>
#include <chrono>
#include <future>
#include <utility>
#include <cstring>
#include "Crypto.h"
#include "Log.h"
#include "RouterInfo.h"
#include "RouterContext.h"
#include "Tunnel.h"
#include "Timestamp.h"
#include "Destination.h"
#include "Streaming.h"

namespace i2p
{
namespace stream
{
	namespace
	{
		// Validate the fixed part of a packet before any option accessor is
		// used. Packet buffers are fixed-size, but a short/malformed packet must
		// not make GetFlags()/GetOptionSize() read stale pool memory.
		bool ValidatePacket (const Packet * packet)
		{
			if (!packet || packet->len < 22) return false;
			size_t numNacks = packet->buf[16];
			if (packet->len < 22 + numNacks * 4) return false;
			size_t optionsOffset = 17 + numNacks * 4 + 3; // option size field
			if (packet->len < optionsOffset + 2) return false;
			size_t optionsSize = bufbe16toh (packet->buf + optionsOffset);
			return optionsSize <= packet->len - optionsOffset - 2;
		}
	} // namespace

	std::optional<i2p::data::IdentHash> ExtractSynPeerIdentity (const Packet * packet)
	{
		if (!ValidatePacket (packet)) return std::nullopt;
		if (!(packet->GetFlags () & PACKET_FLAG_FROM_INCLUDED))
		{
			// A ratchet-authenticated SYN without a FROM option is identified
			// by the session's destination hash.
			if (packet->from)
				if (auto destination = packet->from->GetDestinationPtr ()) return *destination;
			return std::nullopt;
		}
		const uint8_t * optionData = packet->GetOptionData ();
		const size_t optionOffset = optionData - packet->buf;
		const size_t optionSize = packet->GetOptionSize ();
		if (optionOffset > packet->len || optionSize > packet->len - optionOffset || !optionSize)
			return std::nullopt;
		// The FROM identity is the first option of a SYN (streaming spec order,
		// same assumption ProcessOptions validates before using it). The
		// signature is verified later by the normal path; a forged identity
		// only makes its sender choose its own admission outcome.
		i2p::data::IdentityEx identity;
		if (!identity.FromBuffer (optionData, optionSize)) return std::nullopt;
		return identity.GetIdentHash ();
	}

	SendBuffer::SendBuffer (
		const std::vector<std::pair<const uint8_t *, size_t>> & bufs, size_t totalLen, SendHandler && h)
		: m_Buffer (std::make_unique_for_overwrite<uint8_t[]> (totalLen))
		, buf (m_Buffer.get ())
		, len (totalLen)
		, offset (0)
		, handler (std::move (h))
	{
		size_t offset1 = 0;
		for (const auto & [b, l] : bufs)
		{
			if (offset1 >= totalLen) break;
			auto copyLen = std::min (l, totalLen - offset1);
			if (copyLen) memcpy (buf + offset1, b, copyLen);
			offset1 += copyLen;
		}
	}

	void SendBufferQueue::Add (std::shared_ptr<SendBuffer> && buf)
	{
		if (buf)
		{
			m_Size += buf->len;
			m_Buffers.push_back (std::move (buf));
		}
	}

	size_t SendBufferQueue::Get (uint8_t * buf, size_t len)
	{
		if (!m_Size) return 0;
		size_t offset = 0;
		if (len >= m_Size)
		{
			for (auto & it : m_Buffers)
			{
				auto rem = it->GetRemainingSize ();
				memcpy (buf + offset, it->GetRemaningBuffer (), rem);
				offset += rem;
			}
			m_Buffers.clear ();
			m_Size = 0;
			return offset;
		}
		else
		{
			while (!m_Buffers.empty () && offset < len)
			{
				auto nextBuffer = m_Buffers.front ();
				auto rem = nextBuffer->GetRemainingSize ();
				if (offset + rem <= len)
				{
					// whole buffer
					memcpy (buf + offset, nextBuffer->GetRemaningBuffer (), rem);
					offset += rem;
					m_Buffers.pop_front (); // delete it
				}
				else
				{
					// partially
					rem = len - offset;
					memcpy (buf + offset, nextBuffer->GetRemaningBuffer (), rem);
					nextBuffer->offset += rem;
					offset = len; // break
				}
			}
			m_Size -= offset;
		}
		return offset;
	}

	void SendBufferQueue::CleanUp ()
	{
		if (!m_Buffers.empty ())
		{
			for (auto & it : m_Buffers) it->Cancel ();
			m_Buffers.clear ();
			m_Size = 0;
		}
	}

	Stream::Stream (boost::asio::io_context & service, std::shared_ptr<StreamingDestination> local,
		std::shared_ptr<const i2p::data::LeaseSet> remote, int port)
		: m_Service (service)
		, m_SendStreamID (0)
		, m_SequenceNumber (0)
		, m_DropWindowDelaySequenceNumber (INITIAL_WINDOW_SIZE)
		, m_TunnelsChangeSequenceNumber (0)
		, m_LastReceivedSequenceNumber (-1)
		, m_PreviousReceivedSequenceNumber (-1)
		, m_LastConfirmedReceivedSequenceNumber (0)
		, // for limit inbound speed
		m_Status (eStreamStatusNew)
		, m_IsIncoming (false)
		, m_IsAckSendScheduled (false)
		, m_IsNAcked (false)
		, m_IsFirstACK (false)
		, m_IsResendNeeded (false)
		, m_IsFirstRttSample (false)
		, m_IsSendTime (true)
		, m_IsWinDropped (true)
		, m_IsChoking2 (false)
		, m_IsChoking3 (false)
		, m_IsClientChoked (false)
		, m_IsClientChoked2 (false)
		, m_IsTimeOutResend (false)
		, m_IsImmediateAckRequested (false)
		, m_IsRemoteLeaseChangeInProgress (false)
		, m_IsBufferEmpty (false)
		, m_IsJavaClient (false)
		, m_DontSign (local->GetOwner ()->IsStreamingDontSign ())
		, m_LocalDestinationOwner (local) // copy: later initializers still use local
		, m_LocalDestination (*m_LocalDestinationOwner)
		, m_RemoteLeaseSet (remote)
		, m_SizeToReceive (0)
		, m_ReceiveTimer (m_Service)
		, m_SendTimer (m_Service)
		, m_ResendTimer (m_Service)
		, m_AckSendTimer (m_Service)
		, m_NumSentBytes (0)
		, m_NumReceivedBytes (0)
		, m_Port (static_cast<uint16_t> (port))
		, m_LocalPort (local->GetLocalPort ())
		, m_RTT (INITIAL_RTT)
		, m_MinRTT (INITIAL_RTT)
		, m_SlowRTT (INITIAL_RTT)
		, m_FastRTT (INITIAL_RTT)
		, m_WindowSize (INITIAL_WINDOW_SIZE)
		, m_MaxWindowSize (static_cast<float> (local->GetOwner ()->GetStreamingMaxWindowSize ()))
		, m_LastWindowDropSize (0)
		, m_WindowDropTargetSize (0)
		, m_WindowIncCounter (0)
		, m_RTO (INITIAL_RTO)
		, m_AckDelay (local->GetOwner ()->GetStreamingAckDelay ())
		, m_PrevRTTSample (INITIAL_RTT)
		, m_Jitter (0)
		, m_MinPacingTime (0)
		, m_PacingTime (INITIAL_PACING_TIME)
		, m_PacingTimeRem (0)
		, m_LastSendTime (0)
		, m_LastACKRecieveTime (0)
		, m_ACKRecieveInterval (local->GetOwner ()->GetStreamingAckDelay ())
		, m_RemoteLeaseChangeTime (0)
		, m_LastWindowIncTime (0)
		, m_LastACKRequestTime (0)
		, m_LastACKSendTime (0)
		, m_PacketACKInterval (1)
		, m_PacketACKIntervalRem (0)
		, // for limit inbound speed
		m_MaxNumResendAttempts (local->GetOwner ()->GetStreamingMaxResends ())
		, m_NumResendAttempts (0)
		, m_NumPacketsToSend (0)
		, m_JitterAccum (0)
		, m_JitterDiv (1)
		, m_MTU (STREAMING_MTU)
	{
		RAND_bytes ((uint8_t *)&m_RecvStreamID, 4);
		m_RemoteIdentity = remote->GetIdentity ();
		auto outboundSpeed = m_LocalDestinationOwner->GetOwner ()->GetStreamingOutboundSpeed ();
		if (outboundSpeed) m_MinPacingTime = (1000000LL * STREAMING_MTU) / outboundSpeed;

		auto inboundSpeed = m_LocalDestinationOwner->GetOwner ()->GetStreamingInboundSpeed (); // for limit inbound speed
		if (inboundSpeed) m_PacketACKInterval = (1000000LL * STREAMING_MTU) / inboundSpeed;
	}

	Stream::Stream (boost::asio::io_context & service, std::shared_ptr<StreamingDestination> local)
		: m_Service (service)
		, m_SendStreamID (0)
		, m_SequenceNumber (0)
		, m_DropWindowDelaySequenceNumber (INITIAL_WINDOW_SIZE)
		, m_TunnelsChangeSequenceNumber (0)
		, m_LastReceivedSequenceNumber (-1)
		, m_PreviousReceivedSequenceNumber (-1)
		, m_LastConfirmedReceivedSequenceNumber (0)
		, // for limit inbound speed
		m_Status (eStreamStatusNew)
		, m_IsIncoming (true)
		, m_IsAckSendScheduled (false)
		, m_IsNAcked (false)
		, m_IsFirstACK (false)
		, m_IsResendNeeded (false)
		, m_IsFirstRttSample (false)
		, m_IsSendTime (true)
		, m_IsWinDropped (true)
		, m_IsChoking2 (false)
		, m_IsChoking3 (false)
		, m_IsClientChoked (false)
		, m_IsClientChoked2 (false)
		, m_IsTimeOutResend (false)
		, m_IsImmediateAckRequested (false)
		, m_IsRemoteLeaseChangeInProgress (false)
		, m_IsBufferEmpty (false)
		, m_IsJavaClient (false)
		, m_DontSign (local->GetOwner ()->IsStreamingDontSign ())
		, m_LocalDestinationOwner (local) // copy: later initializers still use local
		, m_LocalDestination (*m_LocalDestinationOwner)
		, m_SizeToReceive (0)
		, m_ReceiveTimer (m_Service)
		, m_SendTimer (m_Service)
		, m_ResendTimer (m_Service)
		, m_AckSendTimer (m_Service)
		, m_NumSentBytes (0)
		, m_NumReceivedBytes (0)
		, m_Port (0)
		, m_LocalPort (0)
		, m_RTT (INITIAL_RTT)
		, m_MinRTT (INITIAL_RTT)
		, m_SlowRTT (INITIAL_RTT)
		, m_FastRTT (INITIAL_RTT)
		, m_WindowSize (INITIAL_WINDOW_SIZE)
		, m_MaxWindowSize (static_cast<float> (local->GetOwner ()->GetStreamingMaxWindowSize ()))
		, m_LastWindowDropSize (0)
		, m_WindowDropTargetSize (0)
		, m_WindowIncCounter (0)
		, m_RTO (INITIAL_RTO)
		, m_AckDelay (local->GetOwner ()->GetStreamingAckDelay ())
		, m_PrevRTTSample (INITIAL_RTT)
		, m_Jitter (0)
		, m_MinPacingTime (0)
		, m_PacingTime (INITIAL_PACING_TIME)
		, m_PacingTimeRem (0)
		, m_LastSendTime (0)
		, m_LastACKRecieveTime (0)
		, m_ACKRecieveInterval (local->GetOwner ()->GetStreamingAckDelay ())
		, m_RemoteLeaseChangeTime (0)
		, m_LastWindowIncTime (0)
		, m_LastACKRequestTime (0)
		, m_LastACKSendTime (0)
		, m_PacketACKInterval (1)
		, m_PacketACKIntervalRem (0)
		, // for limit inbound speed
		m_MaxNumResendAttempts (local->GetOwner ()->GetStreamingMaxResends ())
		, m_NumResendAttempts (0)
		, m_NumPacketsToSend (0)
		, m_JitterAccum (0)
		, m_JitterDiv (1)
		, m_MTU (STREAMING_MTU)
	{
		RAND_bytes ((uint8_t *)&m_RecvStreamID, 4);
		auto outboundSpeed = m_LocalDestinationOwner->GetOwner ()->GetStreamingOutboundSpeed ();
		if (outboundSpeed) m_MinPacingTime = (1000000LL * STREAMING_MTU) / outboundSpeed;

		auto inboundSpeed = m_LocalDestinationOwner->GetOwner ()->GetStreamingInboundSpeed (); // for limit inbound speed
		if (inboundSpeed) m_PacketACKInterval = (1000000LL * STREAMING_MTU) / inboundSpeed;
	}

	Stream::~Stream ()
	{
		CleanUp ();
		LogPrint (eLogDebug, "Streaming: Stream deleted");
	}

	void Stream::Terminate (bool deleteFromDestination) // should be called from StreamingDestination::Stop only
	{
		m_AckSendTimer.cancel ();
		m_ReceiveTimer.cancel ();
		m_ResendTimer.cancel ();
		m_SendTimer.cancel ();
		m_AckSendTimer.cancel ();
		m_Status = eStreamStatusTerminated;
		//CleanUp (); /* Need to recheck - broke working on windows */
		if (deleteFromDestination) m_LocalDestination.DeleteStream (shared_from_this ());
	}

	void Stream::CleanUp ()
	{
		if (m_RoutingSession && !m_SentPackets.empty ()) // free up space in shared window
		{
			int numPackets = static_cast<int> (m_SentPackets.size ());
			int numSentPackets = m_RoutingSession->NumSentPackets ();
			numSentPackets -= numPackets;
			if (numSentPackets < 0) numSentPackets = 0;
			m_RoutingSession->SetNumSentPackets (numSentPackets);
		}

		m_SendBuffer.CleanUp ();

		for (auto it : m_ReceiveQueue) delete it;
		m_ReceiveQueue.clear ();

		m_NACKedPackets.clear ();

		for (auto it : m_SentPackets) delete it;
		m_SentPackets.clear ();

		for (auto it : m_SavedPackets) delete it;
		m_SavedPackets.clear ();
	}

	void Stream::HandleNextPacket (Packet * packet)
	{
		if (m_Status == eStreamStatusTerminated)
		{
			m_LocalDestination.DeletePacket (packet);
			return;
		}
		if (!ValidatePacket (packet))
		{
			LogPrint (eLogWarning, "Streaming: Malformed packet of ", packet ? packet->len : 0, " bytes, dropped");
			m_LocalDestination.DeletePacket (packet);
			return;
		}
		m_NumReceivedBytes += packet->GetLength ();
		if (!m_SendStreamID)
		{
			m_SendStreamID = packet->GetReceiveStreamID ();
			if (!m_RemoteIdentity && !packet->from && packet->GetNACKCount () == 8 && // first incoming packet
				memcmp (packet->GetNACKs (), m_LocalDestination.GetOwner ()->GetIdentHash (), 32))
			{
				LogPrint (eLogWarning, "Streaming: Destination mismatch for ",
					m_LocalDestination.GetOwner ()->GetIdentHash ().ToBase32 ());
				m_LocalDestination.DeletePacket (packet);
				return;
			}
		}

		// The ackThrough field is ignored on the initial connection packet
		// (sendStreamId still 0), whether or not the sender set NO_ACK. Older
		// clients do not set it.
		if (!packet->IsNoAck () && !(packet->IsSYN () && !packet->GetSendStreamID ())) // ack received
			ProcessAck (packet);

		int32_t receivedSeqn = packet->GetSeqn ();
		if (!receivedSeqn && m_LastReceivedSequenceNumber >= 0)
		{
			uint16_t flags = packet->GetFlags ();
			if (flags)
			{
				// plain ack with options or SYNACK retrans
				ProcessOptions (flags, packet);
				if (flags & PACKET_FLAG_SYNCHRONIZE) SendQuickAck (); // to ack SYNACK retrans
			}
			else
			{
				// plain ack
				LogPrint (eLogDebug, "Streaming: Plain ACK received");
				if (m_IsImmediateAckRequested)
				{
					auto ts = i2p::util::GetMillisecondsSinceEpoch ();
					if (m_IsFirstRttSample)
					{
						m_RTT = static_cast<double> (ts - m_LastSendTime);
						m_IsFirstRttSample = false;
					}
					else
						m_RTT = (m_RTT + (ts - m_LastSendTime)) / 2;
					m_IsImmediateAckRequested = false;
				}
			}
			m_LocalDestination.DeletePacket (packet);
			return;
		}

		LogPrint (eLogDebug, "Streaming: Received seqn=", receivedSeqn, " on sSID=", m_SendStreamID);
		if (m_ReceiveQueue.size () > m_MaxWindowSize * 3)
		{
			LogPrint (eLogDebug, "Streaming: ReceiveQueue is full, delete packet");
			m_LocalDestination.DeletePacket (packet);
			m_IsChoking3 = true;
			if (!m_IsAckSendScheduled)
			{
				SendQuickAck ();
				auto ackTimeout = m_RTT / 10;
				if (ackTimeout > m_AckDelay) ackTimeout = m_AckDelay;
				ScheduleAck (static_cast<int> (ackTimeout));
			}
			return;
		}

		if (receivedSeqn == m_LastReceivedSequenceNumber + 1)
		{
			// we have received next in sequence message
			ProcessPacket (packet);
			if (m_Status == eStreamStatusTerminated) return;

			if (!m_SavedPackets.empty ())
			{
				// we should also try stored messages if any
				auto it = m_SavedPackets.begin ();
				while (it != m_SavedPackets.end ())
				{
					if ((*it)->GetSeqn () == (uint32_t)(m_LastReceivedSequenceNumber + 1))
					{
						ProcessPacket (*it);
						it++;
					}
					else
						break;
				}
				m_SavedPackets.erase (m_SavedPackets.begin (), it);
				if (m_Status == eStreamStatusTerminated) return; // status migth have changed in ProcessPacket
			}

			// schedule ack for last message
			if (m_Status == eStreamStatusOpen)
			{
				if (!m_IsAckSendScheduled)
				{
					auto ackTimeout = m_RTT / 10;
					if (ackTimeout > m_AckDelay) ackTimeout = m_AckDelay;
					ScheduleAck (static_cast<int> (ackTimeout));
				}
			}
			else if (packet->IsSYN ())
				// we have to send SYN back to incoming connection
				SendBuffer (); // also sets m_IsOpen
		}
		else
		{
			if (receivedSeqn <= m_LastReceivedSequenceNumber)
			{
				// we have received duplicate
				LogPrint (eLogInfo, "Streaming: Duplicate message ", receivedSeqn, " on sSID=", m_SendStreamID);
				if (receivedSeqn <= m_PreviousReceivedSequenceNumber || receivedSeqn == m_LastReceivedSequenceNumber)
				{
					m_CurrentOutboundTunnel = m_LocalDestination.GetOwner ()->GetTunnelPool ()->GetNextOutboundTunnel (
						m_CurrentOutboundTunnel);
					CancelRemoteLeaseChange ();
					UpdateCurrentRemoteLease ();
				}
				m_PreviousReceivedSequenceNumber = receivedSeqn;
				m_LocalDestination.DeletePacket (packet); // packet dropped
				if (!m_IsAckSendScheduled)
				{
					SendQuickAck (); // resend ack for previous message again
					auto ackTimeout = m_RTT / 10;
					if (ackTimeout > m_AckDelay) ackTimeout = m_AckDelay;
					ScheduleAck (static_cast<int> (ackTimeout));
				}
			}
			else
			{
				LogPrint (eLogInfo, "Streaming: Missing messages on sSID=", m_SendStreamID, ": from ",
					m_LastReceivedSequenceNumber + 1, " to ", receivedSeqn - 1);
				if ((receivedSeqn - m_LastReceivedSequenceNumber) >= m_MaxWindowSize * 3)
				{
					m_LocalDestination.DeletePacket (packet);
					m_IsChoking2 = true;
				}
				else if (m_SavedPackets.empty () && (receivedSeqn - m_LastReceivedSequenceNumber) >= 256)
				{
					m_LocalDestination.DeletePacket (packet);
					m_IsChoking2 = true;
				}
				else if (!m_SavedPackets.empty ())
				{
					uint8_t numNacks = 0;
					auto lastSavedSeq = 0;
					auto nextSeqn = m_LastReceivedSequenceNumber + 1;
					for (auto it : m_SavedPackets)
					{
						auto seqn = it->GetSeqn ();
						if ((int)seqn > lastSavedSeq) lastSavedSeq = seqn;
						for (uint32_t i = nextSeqn; i < seqn; i++) numNacks++;
						nextSeqn = seqn + 1;
					}

					if (numNacks + (receivedSeqn - lastSavedSeq) >= 256)
					{
						m_LocalDestination.DeletePacket (packet);
						m_IsChoking2 = true;
					}
					else
						// save message and wait for missing message again
						SavePacket (packet);
				}
				else
					// save message and wait for missing message again
					SavePacket (packet);
				if (m_LastReceivedSequenceNumber >= 0)
				{
					if (!m_IsAckSendScheduled)
					{
						// send NACKs for missing messages
						SendQuickAck ();
						auto ackTimeout = m_RTT / 10;
						if (ackTimeout > m_AckDelay) ackTimeout = m_AckDelay;
						ScheduleAck (static_cast<int> (ackTimeout));
					}
				}
				else
					// wait for SYN
					ScheduleAck (SYN_TIMEOUT);
			}
		}
	}

	void Stream::SavePacket (Packet * packet)
	{
		if (!m_SavedPackets.insert (packet).second) m_LocalDestination.DeletePacket (packet);
	}

	void Stream::ProcessPacket (Packet * packet)
	{
		uint32_t receivedSeqn = packet->GetSeqn ();
		uint16_t flags = packet->GetFlags ();
		LogPrint (eLogDebug, "Streaming: Process seqn=", receivedSeqn, ", flags=", flags);

		if (!ProcessOptions (flags, packet))
		{
			m_LocalDestination.DeletePacket (packet);
			Terminate ();
			return;
		}

		packet->offset = packet->GetPayload () - packet->buf;
		size_t packetLength = packet->GetLength ();
		if (packetLength > 0)
		{
			m_ReceiveQueue.push_back (packet);
			if (m_SizeToReceive)
			{
				if (packetLength < m_SizeToReceive)
					m_SizeToReceive -= packetLength;
				else
					m_SizeToReceive = 0;
			}
			if (!m_SizeToReceive) m_ReceiveTimer.cancel ();
		}
		else
			m_LocalDestination.DeletePacket (packet);

		m_LastReceivedSequenceNumber = receivedSeqn;

		if (flags & PACKET_FLAG_RESET)
		{
			LogPrint (eLogDebug, "Streaming: closing stream sSID=", m_SendStreamID, ", rSID=", m_RecvStreamID,
				": reset flag received in packet #", receivedSeqn);
			m_IsResetReceived = true;
			m_Status = eStreamStatusReset;
			Close ();
		}
		else if (flags & PACKET_FLAG_CLOSE)
		{
			if (m_Status != eStreamStatusClosed) SendClose ();
			m_Status = eStreamStatusClosed;
			boost::asio::post (GetService (), std::bind (&Stream::Terminate, shared_from_this (), true));
		}
	}

	bool Stream::ProcessOptions (uint16_t flags, Packet * packet)
	{
		const uint8_t * optionData = packet->GetOptionData ();
		size_t optionOffset = optionData - packet->buf;
		size_t optionSize = packet->GetOptionSize ();
		if (optionOffset > packet->len || optionSize > packet->len - optionOffset)
		{
			LogPrint (eLogInfo, "Streaming: Invalid option size ", optionSize, " Discarded");
			return false;
		}
		if (!flags) return true;
		const uint8_t * optionEnd = optionData + optionSize;
		auto hasOption = [&] (size_t n) { return (size_t)(optionEnd - optionData) >= n; };
		bool immediateAckRequested = false;
		if (flags & PACKET_FLAG_DELAY_REQUESTED)
		{
			if (!hasOption (2))
			{
				LogPrint (eLogInfo, "Streaming: Truncated delay option, dropped");
				return false;
			}
			uint16_t delayRequested = bufbe16toh (optionData);
			if (!delayRequested) // 0 requests an immediate ack
				immediateAckRequested = true;
			else if (!m_IsAckSendScheduled)
			{
				if (delayRequested < m_RTT)
				{
					m_IsAckSendScheduled = true;
					m_AckSendTimer.expires_after (std::chrono::milliseconds (delayRequested));
					m_AckSendTimer.async_wait (
						std::bind (&Stream::HandleAckSendTimer, shared_from_this (), std::placeholders::_1));
				}
				if (delayRequested >= DELAY_CHOKING)
				{
					if (delayRequested == DELAY_CHOKING_3)
					{
						m_NumResendAttempts = 0;
						m_WindowDropTargetSize = MIN_WINDOW_SIZE;
						m_IsClientChoked = true;
						ResetWindowSize ();
					}
					else if (delayRequested == DELAY_CHOKING_2)
					{
						m_IsClientChoked2 = true;
						m_DropWindowDelaySequenceNumber = m_SequenceNumber - 1;
					}
					else if (!m_IsClientChoked)
					{
						LogPrint (eLogDebug, "Streaming: Client choked, set min. window size");
						if (delayRequested == DELAY_CHOKING_JAVA) // java detected
						{
							LogPrint (eLogDebug, "Streaming: limit window size for java client");
							m_MaxWindowSize = 64;
							m_IsJavaClient = true;
							if (m_RoutingSession) m_RoutingSession->SetIsWithJava (true);
						}
						m_WindowDropTargetSize = MIN_WINDOW_SIZE;
						m_LastWindowDropSize = 0;
						m_WindowIncCounter = 0;
						m_IsClientChoked = true;
						m_IsWinDropped = false;
						m_DropWindowDelaySequenceNumber = m_SequenceNumber - 1;
						m_IsFirstRttSample = true;
						UpdatePacingTime ();
					}
				}
			}
			optionData += 2;
		}

		bool verified = true;
		if (flags & PACKET_FLAG_FROM_INCLUDED)
		{
			verified = false;
			// Parse the identity that is actually in this option block instead
			// of assuming the length of a previously learned identity.
			i2p::data::IdentityEx optionIdentity;
			size_t identityLen = optionIdentity.FromBuffer (optionData, optionEnd - optionData);
			if (!identityLen)
			{
				LogPrint (eLogInfo, "Streaming: Truncated FROM option, dropped");
				return false;
			}
			if (m_RemoteLeaseSet) m_RemoteIdentity = m_RemoteLeaseSet->GetIdentity ();
			if (!m_RemoteIdentity)
				m_RemoteIdentity = std::make_shared<i2p::data::IdentityEx> (optionIdentity);
			else if (m_RemoteIdentity->GetIdentHash () != optionIdentity.GetIdentHash ())
			{
				LogPrint (eLogError, "Streaming: FROM identity changed for sSID=", m_SendStreamID);
				return false;
			}
			if (m_RemoteIdentity->IsRSA ())
			{
				LogPrint (eLogInfo, "Streaming: Incoming stream from RSA destination ",
					m_RemoteIdentity->GetIdentHash ().ToBase64 (), " Discarded");
				return false;
			}
			optionData += identityLen;
			if (!m_RemoteLeaseSet)
			{
				LogPrint (eLogDebug, "Streaming: Incoming stream from ", m_RemoteIdentity->GetIdentHash ().ToBase32 (),
					", sSID=", m_SendStreamID, ", rSID=", m_RecvStreamID);
				if (packet->from) // try to obtain LeaseSet if came from ratchets session
					m_RemoteLeaseSet = m_LocalDestination.GetOwner ()->FindLeaseSet (m_RemoteIdentity->GetIdentHash ());
			}
			if (packet->from && m_RemoteLeaseSet)
			{
				// stream came from ratchets session and static key must match one from LeaseSet
				if (!m_RemoteLeaseSet->IsEncryptionKey (
						packet->from->GetRemoteStaticKeyType (), packet->from->GetRemoteStaticKey ()))
				{
					LogPrint (eLogError, "Streaming: Remote LeaseSet static key mismatch for stream from ",
						m_RemoteIdentity->GetIdentHash ().ToBase32 ());
					return false;
				}
				verified = true;
				if (!(flags & PACKET_FLAG_SIGNATURE_INCLUDED))
					m_DontSign = true; // don't sign if the remote didn't sign
			}
		}

		// Java ConnectionPacketHandler rejects a SYN that has no FROM identity
		// unless the stream was already authenticated by a ratchet session.
		if ((flags & PACKET_FLAG_SYNCHRONIZE) && !(flags & PACKET_FLAG_FROM_INCLUDED) && !m_RemoteIdentity)
		{
			LogPrint (eLogInfo, "Streaming: SYN without authenticated FROM identity, dropped");
			return false;
		}

		if (flags & PACKET_FLAG_MAX_PACKET_SIZE_INCLUDED)
		{
			if (!hasOption (2))
			{
				LogPrint (eLogInfo, "Streaming: Truncated max packet size option, dropped");
				return false;
			}
			const uint16_t maxPacketSize = bufbe16toh (optionData);
			optionData += 2;
			// Java clamps the peer's advertised MTU to MIN_MESSAGE_SIZE and
			// never raises it above the local/ratchet value. Apply it to the
			// incoming initial SYN too: the SYNACK and any initial payload must
			// respect the peer's advertised receive size.
			const size_t peerMtu = std::max<size_t> (maxPacketSize, STREAMING_MIN_MTU);
			if (peerMtu < m_MTU) m_MTU = peerMtu;
			LogPrint (eLogDebug, "Streaming: Max packet size ", maxPacketSize, ", MTU ", m_MTU);
		}

		if (flags & (PACKET_FLAG_CLOSE | PACKET_FLAG_RESET))
		{
			verified = false;
			if (packet->from)
			{
				if (!m_RemoteLeaseSet && m_RemoteIdentity)
					m_RemoteLeaseSet = m_LocalDestination.GetOwner ()->FindLeaseSet (m_RemoteIdentity->GetIdentHash ());
				if (m_RemoteLeaseSet)
				{
					if (!m_RemoteLeaseSet->IsEncryptionKey (
							packet->from->GetRemoteStaticKeyType (), packet->from->GetRemoteStaticKey ()))
					{
						LogPrint (eLogError, "Streaming: Remote LeaseSet static key mismatch for stream from ",
							m_RemoteIdentity->GetIdentHash ().ToBase32 ());
						return false;
					}
					verified = true;
				}
				else
				{
					// No cached LeaseSet does not make an unsigned CLOSE/RESET
					// authentic. Require a real packet signature below; the
					// signature path verifies it against the destination
					// identity/transient key.
					if (!(flags & PACKET_FLAG_SIGNATURE_INCLUDED) || !m_RemoteIdentity)
					{
						LogPrint (eLogWarning, "Streaming: Unsigned CLOSE/RESET without LeaseSet, dropped");
						return false;
					}
					verified = false;
				}
			}
		}

		if (flags & PACKET_FLAG_OFFLINE_SIGNATURE)
		{
			if (!m_RemoteIdentity)
			{
				LogPrint (eLogInfo, "Streaming: offline signature without identity");
				return false;
			}
			// Always authenticate the offline section when the flag is present.
			// A cached LeaseSet delegation must not let an expired or
			// differently-signed section bypass the expiry/delegation checks.
			if (!hasOption (6))
			{
				LogPrint (eLogInfo, "Streaming: Truncated offline signature option, dropped");
				return false;
			}
			size_t offset = 0;
			m_TransientVerifier = i2p::data::ProcessOfflineSignature (
				m_RemoteIdentity, optionData, optionSize - (optionData - packet->GetOptionData ()), offset, true);
			optionData += offset;
			if (!m_TransientVerifier)
			{
				LogPrint (eLogError, "Streaming: offline signature failed");
				return false;
			}
			if (optionData > optionEnd)
			{
				LogPrint (eLogInfo, "Streaming: Offline signature exceeds options, dropped");
				return false;
			}
		}

		if (flags & PACKET_FLAG_SIGNATURE_INCLUDED)
		{
			if (!m_TransientVerifier && !m_RemoteIdentity)
			{
				LogPrint (eLogError, "Streaming: Signature included without remote identity");
				return false;
			}
			auto signatureLen
				= m_TransientVerifier ? m_TransientVerifier->GetSignatureLen () : m_RemoteIdentity->GetSignatureLen ();
			// SIGNATURE_INCLUDED is the last option (streaming spec, option 5).
			if (!hasOption (signatureLen))
			{
				LogPrint (eLogError, "Streaming: Signature too big, ", signatureLen, " bytes");
				return false;
			}
			if (!verified) // packet was not verified through session
			{
				if (!m_TransientVerifier && !m_RemoteIdentity)
				{
					LogPrint (eLogWarning, "Streaming: No verifier or remote identity for signature");
					return false;
				}
				// verify actual signature
				if (signatureLen <= 256)
				{
					// standard
					uint8_t signature[256];
					memcpy (signature, optionData, signatureLen);
					memset (const_cast<uint8_t *> (optionData), 0, signatureLen);
					verified = m_TransientVerifier
						? m_TransientVerifier->Verify (packet->GetBuffer (), packet->GetLength (), signature)
						: m_RemoteIdentity->Verify (packet->GetBuffer (), packet->GetLength (), signature);
					if (verified) memcpy (const_cast<uint8_t *> (optionData), signature, signatureLen);
				}
				else
				{
					// post quantum
					std::vector<uint8_t> signature (signatureLen);
					memcpy (signature.data (), optionData, signatureLen);
					memset (const_cast<uint8_t *> (optionData), 0, signatureLen);
					verified = m_TransientVerifier
						? m_TransientVerifier->Verify (packet->GetBuffer (), packet->GetLength (), signature.data ())
						: m_RemoteIdentity->Verify (packet->GetBuffer (), packet->GetLength (), signature.data ());
				}
			}
			if (verified)
				optionData += signatureLen;
			else
			{
				LogPrint (eLogError, "Streaming: Signature verification failed, sSID=", m_SendStreamID,
					", rSID=", m_RecvStreamID);
				return false;
			}
		}
		if (!verified)
		{
			LogPrint (eLogError, "Streaming: Missing signature, sSID=", m_SendStreamID, ", rSID=", m_RecvStreamID);
			return false;
		}
		if (immediateAckRequested)
		{
			auto ts = i2p::util::GetMillisecondsSinceEpoch ();
			if (m_LastACKSendTime != ts) // preventing multiple acks when reading m_SavedPackets
			{
				// schedule sending quick ack after all packets get processed
				boost::asio::post (m_Service,
					[s = shared_from_this ()] ()
					{
						if (s->m_IsAckSendScheduled)
						{
							s->SendQuickAck ();
							auto ackTimeout = s->m_RTT / 10;
							if (ackTimeout > s->m_AckDelay) ackTimeout = s->m_AckDelay;
							s->ScheduleAck (static_cast<int> (ackTimeout));
						}
						else
							s->SendQuickAck ();
					});
			}
		}
		return true;
	}

	void Stream::HandlePing (Packet * packet)
	{
		uint16_t flags = packet->GetFlags ();
		if (ProcessOptions (flags, packet) && m_RemoteIdentity)
		{
			// send pong
			Packet p;
			memset (p.buf, 0, 22); // minimal header all zeroes
			memcpy (p.buf + 4, packet->buf, 4); // but receiveStreamID is the sendStreamID from the ping
			htobe16buf (p.buf + 18, PACKET_FLAG_ECHO | PACKET_FLAG_NO_ACK); // echo and no-ack flags
			auto payloadLen = int (packet->len) - (packet->GetPayload () - packet->buf);
			constexpr int MAX_PONG_PAYLOAD = 32; // Java ConnectionManager
			if (payloadLen > MAX_PONG_PAYLOAD) payloadLen = MAX_PONG_PAYLOAD;
			if (payloadLen > 0)
				memcpy (p.buf + 22, packet->GetPayload (), payloadLen);
			else
				payloadLen = 0;
			p.len = payloadLen + 22;
			SendPackets ({ &p });
			LogPrint (eLogDebug, "Streaming: Pong of ", p.len, " bytes sent");
		}
		m_LocalDestination.DeletePacket (packet);
	}

	void Stream::ProcessAck (Packet * packet)
	{
		bool acknowledged = false;
		auto ts = i2p::util::GetMillisecondsSinceEpoch ();
		uint32_t ackThrough = packet->GetAckThrough ();
		if (ackThrough > m_SequenceNumber)
		{
			LogPrint (eLogError, "Streaming: Unexpected ackThrough=", ackThrough, " > seqn=", m_SequenceNumber);
			return;
		}
		int rttSample = INT_MAX;
		int incCounter = 0;
		int ackPacketsCounter = 0;
		m_IsNAcked = false;
		m_IsResendNeeded = false;
		int nackCount = packet->GetNACKCount ();
		std::list<Packet *> newNACKedPackets;
		for (auto it = m_SentPackets.begin (); it != m_SentPackets.end ();)
		{
			auto seqn = (*it)->GetSeqn ();
			if (seqn <= ackThrough)
			{
				if (nackCount > 0)
				{
					bool nacked = false;
					for (int i = 0; i < nackCount; i++)
						if (seqn == packet->GetNACK (i))
						{
							newNACKedPackets.push_back (*it);
							m_IsNAcked = true;
							nacked = true;
							break;
						}
					if (nacked)
					{
						LogPrint (eLogDebug, "Streaming: Packet ", seqn, " NACK");
						it++;
						continue;
					}
				}
				auto sentPacket = *it;
				int64_t rtt = (int64_t)ts - (int64_t)sentPacket->sendTime;
				if (rtt < 0)
					LogPrint (
						eLogError, "Streaming: Packet ", seqn, "sent from the future, sendTime=", sentPacket->sendTime);
				if (!seqn)
				{
					m_IsFirstRttSample = true;
					rttSample = rtt < 0 ? 1 : static_cast<int> (rtt);
				}
				else if (!sentPacket->resent && seqn > m_TunnelsChangeSequenceNumber && rtt >= 0)
					rttSample = std::min (rttSample, (int)rtt);
				LogPrint (eLogDebug, "Streaming: Packet ", seqn, " acknowledged rtt=", rtt,
					" sentTime=", sentPacket->sendTime);
				it = m_SentPackets.erase (it);
				m_LocalDestination.DeletePacket (sentPacket);
				acknowledged = true;
				ackPacketsCounter++;
				if (m_WindowIncCounter < m_MaxWindowSize && !m_IsFirstACK && !m_IsWinDropped) incCounter++;
			}
			else
				break;
		}
		m_NACKedPackets.swap (newNACKedPackets);

		if (m_LastACKRecieveTime)
		{
			uint64_t interval = ts - m_LastACKRecieveTime;
			if (m_ACKRecieveInterval)
				m_ACKRecieveInterval = (m_ACKRecieveInterval + interval) / 2;
			else
				m_ACKRecieveInterval = interval;
		}
		m_LastACKRecieveTime = ts;
		if (rttSample != INT_MAX)
		{
			if (m_IsFirstRttSample && !m_IsFirstACK)
			{
				m_RTT = rttSample;
				m_MinRTT = m_RTT;
				m_SlowRTT = rttSample;
				m_FastRTT = rttSample;
				m_PrevRTTSample = rttSample;
				m_Jitter = rttSample / 5; // 20%
				m_Jitter += 5; // for low-latency connections
				m_JitterAccum = static_cast<uint64_t> (m_Jitter);
				m_JitterDiv = 1;
				m_IsFirstRttSample = false;
			}
			else
			{
				m_RTT = (m_PrevRTTSample + rttSample) / 2;
			}
			if (!m_IsWinDropped)
			{
				m_SlowRTT = SLOWRTT_EWMA_ALPHA * m_RTT + (1.0 - SLOWRTT_EWMA_ALPHA) * m_SlowRTT;
				m_FastRTT = RTT_EWMA_ALPHA * m_RTT + (1.0 - RTT_EWMA_ALPHA) * m_FastRTT;
				// calculate jitter
				double jitter = 0;
				if (rttSample > m_PrevRTTSample)
					jitter = rttSample - m_PrevRTTSample;
				else if (rttSample < m_PrevRTTSample)
					jitter = m_PrevRTTSample - rttSample;
				if (jitter)
				{
					jitter += 5; // for low-latency connections
					m_JitterAccum += static_cast<uint64_t> (jitter);
					m_Jitter = static_cast<double> (m_JitterAccum / m_JitterDiv);
					m_JitterDiv++;
				}
				if (m_MinRTT > m_RTT)
				{
					m_MinRTT = m_RTT;
					m_FastRTT = m_MinRTT + m_Jitter;
					m_SlowRTT = m_MinRTT + m_Jitter;
				}
			}
			if (m_IsBufferEmpty || m_FastRTT >= m_MinRTT + m_Jitter * 4 || m_RTT >= m_MinRTT + m_Jitter * 4
				|| m_SlowRTT >= m_MinRTT + m_Jitter * 4 || m_RTT > m_FastRTT)
			{
				incCounter = 0;
				m_WindowIncCounter = 0;
			}
			m_WindowIncCounter = m_WindowIncCounter + incCounter;
			//
			// delay-based CC
			if ((m_RTT > m_SlowRTT) && (m_SlowRTT >= m_FastRTT) && (m_FastRTT > m_MinRTT + m_Jitter * 8)
				&& (m_SlowRTT > m_MinRTT + m_Jitter * 8) && !m_IsWinDropped
				&& !m_IsClientChoked) // Drop window if RTT grows too fast
			{
				LogPrint (eLogDebug, "Streaming: Congestion detected, reduce window size");
				ProcessWindowDrop ();
			}
			UpdatePacingTime ();
			m_PrevRTTSample = rttSample;

			bool wasInitial = m_RTO == INITIAL_RTO;
			m_RTO = std::max (MIN_RTO, (int)(m_RTT + m_Jitter * 2 + m_ACKRecieveInterval)); // TODO: implement it better

			if (wasInitial) ScheduleResend ();
		}
		if (m_IsClientChoked && (ackThrough >= m_DropWindowDelaySequenceNumber || m_SentPackets.empty ()))
			m_IsClientChoked = false;
		if (m_IsClientChoked2 && (ackThrough >= m_DropWindowDelaySequenceNumber || m_SentPackets.empty ()))
			m_IsClientChoked2 = false;
		if (m_IsWinDropped && ackThrough > m_DropWindowDelaySequenceNumber)
		{
			m_IsFirstRttSample = true;
			m_IsWinDropped = false;
		}
		if (m_WindowDropTargetSize && int (m_SentPackets.size ()) <= m_WindowDropTargetSize)
		{
			m_WindowSize = m_WindowDropTargetSize;
			m_WindowDropTargetSize = 0;
		}
		if (acknowledged || m_IsNAcked)
		{
			ScheduleResend ();
		}
		if (m_SendBuffer.IsEmpty () && m_SentPackets.size () > 0) // tail loss
		{
			m_IsResendNeeded = true;
			m_RTO = std::max (
				MIN_RTO, (int)(m_RTT * 1.5 + m_Jitter + m_ACKRecieveInterval)); // to prevent spurious retransmit
		}
		if (m_SentPackets.empty () && m_SendBuffer.IsEmpty ())
		{
			m_ResendTimer.cancel ();
			m_SendTimer.cancel ();
			m_LastACKRecieveTime = 0;
			m_ACKRecieveInterval = m_AckDelay;
		}
		if (acknowledged && m_IsFirstACK)
		{
			if (m_RoutingSession)
				m_RoutingSession->SetSharedRoutingPath (std::make_shared<i2p::garlic::GarlicRoutingPath> (
					i2p::garlic::GarlicRoutingPath { m_CurrentOutboundTunnel, m_CurrentRemoteLease, (int)m_RTT, 0 }));
			m_IsFirstACK = false;
		}
		if (acknowledged)
		{
			if (m_RoutingSession)
			{
				int numSentPackets = m_RoutingSession->NumSentPackets ();
				numSentPackets -= ackPacketsCounter;
				if (numSentPackets < 0) numSentPackets = 0;
				m_RoutingSession->SetNumSentPackets (numSentPackets);
			}
			m_NumResendAttempts = 0;
			m_IsTimeOutResend = false;
			SendBuffer ();
		}
		if (m_Status == eStreamStatusClosed)
			Terminate ();
		else if (m_Status == eStreamStatusClosing)
			Close (); // check is all outgoing messages have been sent and we can send close
	}

	size_t Stream::Receive (uint8_t * buf, size_t len, int timeout)
	{
		if (!len) return 0;
		size_t ret = 0;
		volatile bool done = false;
		std::condition_variable newDataReceived;
		std::mutex newDataReceivedMutex;
		AsyncReceive (
			boost::asio::buffer (buf, len),
			[&ret, &done, &newDataReceived, &newDataReceivedMutex] (
				const boost::system::error_code & ecode, std::size_t bytes_transferred)
			{
				if (ecode == boost::asio::error::timed_out)
					ret = 0;
				else
					ret = bytes_transferred;
				std::unique_lock<std::mutex> l (newDataReceivedMutex);
				newDataReceived.notify_all ();
				done = true;
			},
			timeout);
		if (!done)
		{
			std::unique_lock<std::mutex> l (newDataReceivedMutex);
			if (!done && newDataReceived.wait_for (l, std::chrono::seconds (timeout)) == std::cv_status::timeout)
				ret = 0;
		}
		if (!done)
		{
			// make sure that AsyncReceive complete
			auto s = shared_from_this ();
			boost::asio::post (m_Service, [s] () { s->m_ReceiveTimer.cancel (); });
			int i = 0;
			while (!done && i < 100) // 1 sec
			{
				std::this_thread::sleep_for (std::chrono::milliseconds (10));
				i++;
			}
		}
		return ret;
	}

	size_t Stream::Send (const uint8_t * buf, size_t len)
	{
		AsyncSend (buf, len, nullptr);
		return len;
	}

	size_t Stream::Send (std::span<const uint8_t> buf)
	{
		return Send (buf.data (), buf.size ());
	}

	void Stream::AsyncSend (const uint8_t * buf, size_t len, SendHandler && handler)
	{
		std::shared_ptr<i2p::stream::SendBuffer> buffer;
		if (len > 0 && buf)
			buffer = std::make_shared<i2p::stream::SendBuffer> (buf, len, std::move (handler));
		else if (handler)
		{
			boost::asio::post (GetService (),
				[handler = std::move (handler)] () mutable { handler (boost::system::error_code (), 0); });
		}
		Send (std::move (buffer));
	}

	void Stream::AsyncSend (std::span<const uint8_t> buf, SendHandler && handler)
	{
		AsyncSend (buf.data (), buf.size (), std::move (handler));
	}

	void Stream::Send (std::shared_ptr<i2p::stream::SendBuffer> && buf)
	{
		boost::asio::post (m_Service,
			[s = shared_from_this (), buf = std::move (buf)] () mutable
			{
				if (buf) s->m_SendBuffer.Add (std::move (buf));
				s->SendBuffer ();
			});
	}

	void Stream::SendBuffer ()
	{
		if (m_Status == eStreamStatusTerminated)
		{
			// A queued send or asynchronous LeaseSet callback can arrive after
			// the destination was stopped and m_LocalDestination released.
			m_SendBuffer.CleanUp ();
			return;
		}
		if (m_Status == eStreamStatusClosed || m_Status == eStreamStatusReset)
		{
			// No data may follow a CLOSE or RESET. CleanUp completes the queued
			// write handlers with operation_aborted, so a caller that writes
			// after the connection ended sees the failure instead of silent
			// success. Orderly half-close is unaffected: eStreamStatusClosing
			// still flushes the send buffer before the FIN.
			m_SendBuffer.CleanUp ();
			return;
		}
		if (m_RemoteLeaseSet) // don't schedule send for first SYN for incoming stream
			ScheduleSend ();
		auto ts = i2p::util::GetMillisecondsSinceEpoch ();
		int numMsgs = static_cast<int> (m_WindowSize - m_SentPackets.size ());
		if (numMsgs <= 0 || !m_IsSendTime) // window is full
		{
			m_LastSendTime = ts;
			return;
		}
		else if (numMsgs > m_NumPacketsToSend)
			numMsgs = m_NumPacketsToSend;

		if (!m_RemoteLeaseSet && m_RemoteIdentity)
			m_RemoteLeaseSet = m_LocalDestination.GetOwner ()->FindLeaseSet (m_RemoteIdentity->GetIdentHash ());
		if (m_RemoteLeaseSet)
		{
			if (!m_RoutingSession)
				m_RoutingSession = m_LocalDestination.GetOwner ()->GetRoutingSession (m_RemoteLeaseSet, true, false);
		}

		if (m_RoutingSession)
		{
			m_IsJavaClient = m_RoutingSession->IsWithJava ();
			if (m_IsJavaClient) m_MaxWindowSize = 64;
			int numSentPackets = m_RoutingSession->NumSentPackets ();
			int numPacketsToSend = static_cast<int> (m_MaxWindowSize - numSentPackets);
			if (numPacketsToSend <= 0) // shared window is full
			{
				if (m_LastReceivedSequenceNumber <= 0 && m_SequenceNumber == 0)
				{
					LogPrint (eLogWarning,
						"Streaming: limit of unacknowledged packets has been reached, terminate, rSID=", m_RecvStreamID,
						", sSID=", m_SendStreamID);
					m_Status = eStreamStatusReset;
					Close ();
					return;
				}
				m_LastSendTime = ts;
				return;
			}
			else if (numMsgs > numPacketsToSend)
				numMsgs = numPacketsToSend;
		}
		bool isNoAck = m_LastReceivedSequenceNumber < 0; // first packet
		std::list<Packet *> packets;
		while ((m_Status == eStreamStatusNew) || (IsEstablished () && !m_SendBuffer.IsEmpty () && numMsgs > 0))
		{
			Packet * p = m_LocalDestination.NewPacket ();
			uint8_t * packet = p->GetBuffer ();
			// TODO: implement setters
			size_t size = 0;
			htobe32buf (packet + size, m_SendStreamID);
			size += 4; // sendStreamID
			htobe32buf (packet + size, m_RecvStreamID);
			size += 4; // receiveStreamID
			htobe32buf (packet + size, m_SequenceNumber++);
			size += 4; // sequenceNum
			if (isNoAck)
				htobuf32 (packet + size, 0);
			else
				htobe32buf (packet + size, m_LastReceivedSequenceNumber);
			size += 4; // ack Through
			if (m_Status == eStreamStatusNew && !m_SendStreamID && m_RemoteIdentity)
			{
				// first SYN packet
				if (m_DontSign)
				{
					// remote ident is useless without signature, don't include it
					packet[size] = 0;
					size++; // NACK count
				}
				else
				{
					packet[size] = 8;
					size++; // NACK count
					memcpy (packet + size, m_RemoteIdentity->GetIdentHash (), 32);
					size += 32;
				}
			}
			else
			{
				packet[size] = 0;
				size++; // NACK count
			}
			packet[size] = static_cast<uint8_t> (m_RTO / 1000);
			size++; // resend delay
			if (m_Status == eStreamStatusNew)
			{
				// initial packet
				m_Status = eStreamStatusOpen;
				if (!m_RemoteLeaseSet && m_RemoteIdentity)
					m_RemoteLeaseSet = m_LocalDestination.GetOwner ()->FindLeaseSet (m_RemoteIdentity->GetIdentHash ());
				if (m_RemoteLeaseSet)
				{
					m_RoutingSession
						= m_LocalDestination.GetOwner ()->GetRoutingSession (m_RemoteLeaseSet, true, !m_IsIncoming);
					m_MTU = (m_RoutingSession && m_RoutingSession->IsRatchets ()) ? STREAMING_MTU_RATCHETS
																				  : STREAMING_MTU;
				}
				uint16_t flags
					= PACKET_FLAG_SYNCHRONIZE | PACKET_FLAG_FROM_INCLUDED | PACKET_FLAG_MAX_PACKET_SIZE_INCLUDED;
				if (isNoAck) flags |= PACKET_FLAG_NO_ACK;
				bool isOfflineSignature = false;
				if (!m_DontSign)
				{
					flags |= PACKET_FLAG_SIGNATURE_INCLUDED;
					isOfflineSignature = m_LocalDestination.GetOwner ()->GetPrivateKeys ().IsOfflineSignature ();
					if (isOfflineSignature) flags |= PACKET_FLAG_OFFLINE_SIGNATURE;
				}
				htobe16buf (packet + size, flags);
				size += 2; // flags
				size_t identityLen = m_LocalDestination.GetOwner ()->GetIdentity ()->GetFullLen ();
				size_t signatureLen = m_LocalDestination.GetOwner ()->GetPrivateKeys ().GetSignatureLen ();
				uint8_t * optionsSize = packet + size; // set options size later
				size += 2; // options size
				m_LocalDestination.GetOwner ()->GetIdentity ()->ToBuffer (packet + size, identityLen);
				size += identityLen; // from
				htobe16buf (packet + size, static_cast<uint16_t> (m_MTU));
				size += 2; // max packet size
				if (m_DontSign)
				{
					htobe16buf (
						optionsSize, static_cast<uint16_t> (packet + size - 2 - optionsSize)); // actual options size
					size += m_SendBuffer.Get (packet + size, m_MTU); // payload
				}
				else
				{
					if (isOfflineSignature)
					{
						const auto & offlineSignature
							= m_LocalDestination.GetOwner ()->GetPrivateKeys ().GetOfflineSignature ();
						memcpy (packet + size, offlineSignature.data (), offlineSignature.size ());
						size += offlineSignature.size (); // offline signature
					}
					uint8_t * signature = packet + size; // set it later
					memset (signature, 0, signatureLen); // zeroes for now
					size += signatureLen; // signature
					htobe16buf (
						optionsSize, static_cast<uint16_t> (packet + size - 2 - optionsSize)); // actual options size
					size += m_SendBuffer.Get (packet + size, m_MTU); // payload
					m_LocalDestination.GetOwner ()->Sign (packet, static_cast<int> (size), signature);
				}
			}
			else
			{
				// follow on packet
				if (!m_LastACKRequestTime || ts - m_LastACKRequestTime > m_MinRTT / 10)
				{
					m_LastACKRequestTime = ts;
					htobe16buf (packet + size, PACKET_FLAG_DELAY_REQUESTED);
					size += 2; // flags
					htobe16buf (packet + size, 2); // 2 bytes delay interval
					htobe16buf (packet + size + 2, 0); // set immediate ack interval
					size += 2;
				}
				else
				{
					htobuf16 (packet + size, 0);
					size += 2; // flags
					htobuf16 (packet + size, 0); // no options
				}
				size += 2; // options size
				size += m_SendBuffer.Get (packet + size, m_MTU); // payload
			}
			p->len = size;
			p->sendTime = ts;
			packets.emplace_back (p);
			numMsgs--;
		}
		if (m_SendBuffer.GetSize () == 0)
			m_IsBufferEmpty = true;
		else
			m_IsBufferEmpty = false;
		int numPackets = static_cast<int> (packets.size ());
		if (packets.size () > 0)
		{
			if (m_SavedPackets.empty ()) // no NACKS
			{
				m_IsAckSendScheduled = false;
				m_AckSendTimer.cancel ();
			}
			SendPackets (packets);
			m_LastSendTime = ts;
			m_IsSendTime = false;
			bool isEmpty = m_SentPackets.empty ();
			m_SentPackets.splice (m_SentPackets.end (), packets);
			if (m_RoutingSession)
			{
				int numSentPackets = m_RoutingSession->NumSentPackets ();
				m_RoutingSession->SetNumSentPackets (numSentPackets + numPackets);
				m_RoutingSession->SetLastSendTime (ts);
			}
			if (m_Status == eStreamStatusClosing && m_SendBuffer.IsEmpty ()) SendClose ();
			if (isEmpty) ScheduleResend ();
		}
	}

	void Stream::SendQuickAck ()
	{
		int32_t lastReceivedSeqn = m_LastReceivedSequenceNumber;
		// for limit inbound speed
		auto ts = i2p::util::GetMillisecondsSinceEpoch ();
		int64_t numPackets = 0;
		bool lostPackets = false;
		int64_t passedTime
			= m_PacketACKInterval * INITIAL_WINDOW_SIZE; // in microseconds // while m_LastACKSendTime == 0
		if (m_LastACKSendTime) passedTime = (ts - m_LastACKSendTime) * 1000; // in microseconds
		numPackets = (passedTime + (int64_t)m_PacketACKIntervalRem) / (int64_t)m_PacketACKInterval;
		int64_t delta = numPackets * (int64_t)m_PacketACKInterval - passedTime;
		if (delta < (int64_t)m_PacketACKIntervalRem)
			m_PacketACKIntervalRem -= delta;
		else
			m_PacketACKIntervalRem = 0;
		if (m_LastConfirmedReceivedSequenceNumber + numPackets < m_LastReceivedSequenceNumber)
		{
			lastReceivedSeqn = static_cast<int32_t> (m_LastConfirmedReceivedSequenceNumber + numPackets);
			if (!m_IsAckSendScheduled)
			{
				auto ackTimeout = m_RTT / 10;
				if (ackTimeout > m_AckDelay) ackTimeout = m_AckDelay;
				ScheduleAck (static_cast<int> (ackTimeout));
			}
		}
		if (numPackets == 0) return;
		// for limit inbound speed
		if (!m_SavedPackets.empty ())
		{
			for (auto it : m_SavedPackets)
			{
				auto seqn = it->GetSeqn ();
				// for limit inbound speed
				if (m_LastConfirmedReceivedSequenceNumber + numPackets < int (seqn))
				{
					if (!m_IsAckSendScheduled)
					{
						auto ackTimeout = m_RTT / 10;
						if (ackTimeout > m_AckDelay) ackTimeout = m_AckDelay;
						ScheduleAck (static_cast<int> (ackTimeout));
					}
					if (lostPackets)
						break;
					else
						return;
				}
				// for limit inbound speed
				if ((int)seqn > lastReceivedSeqn)
				{
					lastReceivedSeqn = seqn;
					lostPackets = true; // for limit inbound speed
				}
			}
		}
		if (lastReceivedSeqn < 0)
		{
			LogPrint (eLogError, "Streaming: No packets have been received yet");
			if (m_SequenceNumber == 0)
			{
				if (m_NumResendAttempts > 1)
				{
					m_Status = eStreamStatusReset;
					Close ();
					return;
				}
				m_NumResendAttempts++;
			}
			return;
		}

		Packet p;
		uint8_t * packet = p.GetBuffer ();
		size_t size = 0;
		htobe32buf (packet + size, m_SendStreamID);
		size += 4; // sendStreamID
		htobe32buf (packet + size, m_RecvStreamID);
		size += 4; // receiveStreamID
		htobuf32 (packet + size, 0); // this is plain Ack message
		size += 4; // sequenceNum
		htobe32buf (packet + size, lastReceivedSeqn);
		size += 4; // ack Through
		uint8_t numNacks = 0;
		bool choking = (m_IsChoking2 || m_IsChoking3);
		if (lastReceivedSeqn > m_LastReceivedSequenceNumber)
		{
			// fill NACKs
			uint8_t * nacks = packet + size + 1;
			auto nextSeqn = m_LastReceivedSequenceNumber + 1;
			for (auto it : m_SavedPackets)
			{
				auto seqn = it->GetSeqn ();
				if (m_LastConfirmedReceivedSequenceNumber + numPackets < int (seqn)) // for limit inbound speed
				{
					htobe32buf (packet + 12, nextSeqn - 1);
					break;
				}
				if (numNacks + (seqn - nextSeqn) >= 256)
				{
					LogPrint (eLogError, "Streaming: Number of NACKs exceeds 256. seqn=", seqn, " nextSeqn=", nextSeqn);
					htobe32buf (packet + 12, nextSeqn - 1); // change ack Through back
					choking = true;
					break;
				}
				for (uint32_t i = nextSeqn; i < seqn; i++)
				{
					htobe32buf (nacks, i);
					nacks += 4;
					numNacks++;
				}
				nextSeqn = seqn + 1;
			}
			packet[size] = numNacks;
			size++; // NACK count
			size += numNacks * 4; // NACKs
		}
		else
		{
			// No NACKs
			packet[size] = 0;
			size++; // NACK count
		}
		packet[size] = 0;
		size++; // resend delay
		bool requestImmediateAck = false;
		if (!choking)
			requestImmediateAck = m_LastSendTime && ts > m_LastSendTime + REQUEST_IMMEDIATE_ACK_INTERVAL
				&& ts > m_LastSendTime + REQUEST_IMMEDIATE_ACK_INTERVAL
						+ m_LocalDestination.GetRandom () % REQUEST_IMMEDIATE_ACK_INTERVAL_VARIANCE;
		htobe16buf (packet + size,
			(choking || requestImmediateAck) ? PACKET_FLAG_DELAY_REQUESTED : 0); // no flags set or delay requested
		size += 2; // flags
		if (choking || requestImmediateAck)
		{
			htobe16buf (packet + size, 2); // 2 bytes delay interval
			if (m_IsChoking3)
				htobe16buf (packet + size + 2, DELAY_CHOKING_3); // set choking3
			else if (m_IsChoking2)
				htobe16buf (packet + size + 2, DELAY_CHOKING_2); // set choking2
			else
				htobe16buf (packet + size + 2, choking ? DELAY_CHOKING : 0); // set choking or immediate ack interval
			size += 2;
			if (requestImmediateAck) // ack request sent
			{
				m_LastSendTime = ts;
				m_IsImmediateAckRequested = true;
			}
		}
		else
			htobuf16 (packet + size, 0); // no options
		size += 2; // options size
		p.len = size;

		SendPackets ({ &p });
		m_LastACKSendTime = ts; // for limit inbound speed
		m_LastConfirmedReceivedSequenceNumber = lastReceivedSeqn; // for limit inbound speed
		m_IsChoking2 = false;
		m_IsChoking3 = false;
		LogPrint (eLogDebug, "Streaming: Quick Ack sent. ", (int)numNacks, " NACKs");
	}

	void Stream::SendPing ()
	{
		Packet p;
		uint8_t * packet = p.GetBuffer ();
		size_t size = 0;
		htobe32buf (packet, m_RecvStreamID);
		size += 4; // sendStreamID
		memset (packet + size, 0, 14);
		size += 14; // all zeroes
		uint16_t flags = PACKET_FLAG_ECHO | PACKET_FLAG_NO_ACK | PACKET_FLAG_FROM_INCLUDED;
		if (!m_DontSign) flags |= PACKET_FLAG_SIGNATURE_INCLUDED;
		bool isOfflineSignature = m_LocalDestination.GetOwner ()->GetPrivateKeys ().IsOfflineSignature ();
		if (isOfflineSignature) flags |= PACKET_FLAG_OFFLINE_SIGNATURE;
		htobe16buf (packet + size, flags);
		size += 2; // flags
		size_t identityLen = m_LocalDestination.GetOwner ()->GetIdentity ()->GetFullLen ();
		uint8_t * optionsSize = packet + size; // set options size later
		size += 2; // options size
		m_LocalDestination.GetOwner ()->GetIdentity ()->ToBuffer (packet + size, identityLen);
		size += identityLen; // from
		if (m_DontSign)
			htobe16buf (optionsSize, static_cast<uint16_t> (packet + size - 2 - optionsSize)); // actual options size
		else
		{
			if (isOfflineSignature)
			{
				const auto & offlineSignature
					= m_LocalDestination.GetOwner ()->GetPrivateKeys ().GetOfflineSignature ();
				memcpy (packet + size, offlineSignature.data (), offlineSignature.size ());
				size += offlineSignature.size (); // offline signature
			}
			size_t signatureLen = m_LocalDestination.GetOwner ()->GetPrivateKeys ().GetSignatureLen ();
			uint8_t * signature = packet + size; // set it later
			memset (signature, 0, signatureLen); // zeroes for now
			size += signatureLen; // signature
			htobe16buf (optionsSize, static_cast<uint16_t> (packet + size - 2 - optionsSize)); // actual options size
			m_LocalDestination.GetOwner ()->Sign (packet, static_cast<int> (size), signature);
		}
		p.len = size;
		SendPackets ({ &p });
		LogPrint (eLogDebug, "Streaming: Ping of ", p.len, " bytes sent");
	}

	void Stream::Close ()
	{
		LogPrint (eLogDebug, "Streaming: closing stream with sSID=", m_SendStreamID, ", rSID=", m_RecvStreamID,
			", status=", m_Status);
		switch (m_Status)
		{
		case eStreamStatusOpen:
			m_Status = eStreamStatusClosing;
			Close (); // recursion
			if (m_Status == eStreamStatusClosing) //still closing
				LogPrint (eLogDebug, "Streaming: Trying to send stream data before closing, sSID=", m_SendStreamID);
			break;
		case eStreamStatusReset:
			if (!m_IsResetReceived && m_SendStreamID) SendReset ();
			boost::asio::post (GetService (), std::bind (&Stream::Terminate, shared_from_this (), true));
			break;
		case eStreamStatusClosing:
			if (m_SentPackets.empty () && m_SendBuffer.IsEmpty ()) // nothing to send
			{
				m_Status = eStreamStatusClosed;
				boost::asio::post (GetService (), std::bind (&Stream::SendClose, shared_from_this ()));
			}
			break;
		case eStreamStatusClosed:
		case eStreamStatusNew:
			// already closed
			boost::asio::post (GetService (), std::bind (&Stream::Terminate, shared_from_this (), true));
			break;
		default:
			LogPrint (eLogWarning, "Streaming: Unexpected stream status=", (int)m_Status, " for sSID=", m_SendStreamID);
		};
	}

	void Stream::SendClose ()
	{
		Packet * p = m_LocalDestination.NewPacket ();
		uint8_t * packet = p->GetBuffer ();
		size_t size = 0;
		htobe32buf (packet + size, m_SendStreamID);
		size += 4; // sendStreamID
		htobe32buf (packet + size, m_RecvStreamID);
		size += 4; // receiveStreamID
		htobe32buf (packet + size, m_SequenceNumber++);
		size += 4; // sequenceNum
		htobe32buf (packet + size, m_LastReceivedSequenceNumber >= 0 ? m_LastReceivedSequenceNumber : 0);
		size += 4; // ack Through
		packet[size] = 0;
		size++; // NACK count
		packet[size] = 0;
		size++; // resend delay
		bool isOfflineSignature = false;
		uint16_t flags = PACKET_FLAG_CLOSE;
		if (!m_DontSign)
		{
			flags |= PACKET_FLAG_SIGNATURE_INCLUDED;
			isOfflineSignature = m_LocalDestination.GetOwner ()->GetPrivateKeys ().IsOfflineSignature ();
			if (isOfflineSignature) flags |= PACKET_FLAG_OFFLINE_SIGNATURE | PACKET_FLAG_FROM_INCLUDED;
		}
		htobe16buf (packet + size, flags);
		size += 2; // flags
		if (m_DontSign)
		{
			memset (packet + size, 0, 2); // no options
			size += 2; // options size
		}
		else
		{
			uint8_t * optionsSize = packet + size; // set options size later
			size += 2; // options size
			if (isOfflineSignature)
			{
				// we must include FROM  if offline signature for compatibility with Java-I2P
				size_t identityLen = m_LocalDestination.GetOwner ()->GetIdentity ()->GetFullLen ();
				m_LocalDestination.GetOwner ()->GetIdentity ()->ToBuffer (packet + size, identityLen);
				size += identityLen; // from
				const auto & offlineSignature
					= m_LocalDestination.GetOwner ()->GetPrivateKeys ().GetOfflineSignature ();
				memcpy (packet + size, offlineSignature.data (), offlineSignature.size ());
				size += offlineSignature.size (); // offline signature
			}
			size_t signatureLen = m_LocalDestination.GetOwner ()->GetPrivateKeys ().GetSignatureLen ();
			uint8_t * signature = packet + size; // set it later
			memset (signature, 0, signatureLen); // zeroes for now
			size += signatureLen; // signature
			htobe16buf (optionsSize, static_cast<uint16_t> (packet + size - 2 - optionsSize)); // actual options size
			m_LocalDestination.GetOwner ()->Sign (packet, static_cast<int> (size), signature);
		}

		p->len = size;
		// The FIN is sent once: it is not retransmitted and no TIME-WAIT state
		// retains the closed connection. FIN/TIME-WAIT retention is tracked as
		// an open divergence from I2P Java; do not add retransmission here
		// without a specification reference.
		boost::asio::post (m_Service, std::bind (&Stream::SendPacket, shared_from_this (), p));
		if (m_RoutingSession)
		{
			int numSentPackets = m_RoutingSession->NumSentPackets ();
			m_RoutingSession->SetNumSentPackets (numSentPackets + 1);
		}
		LogPrint (eLogDebug, "Streaming: FIN sent, sSID=", m_SendStreamID);
	}

	Packet * Stream::CreateResetPacket ()
	{
		Packet * p = m_LocalDestination.NewPacket ();
		uint8_t * packet = p->GetBuffer ();
		size_t size = 0;
		htobe32buf (packet + size, m_SendStreamID);
		size += 4; // sendStreamID
		htobe32buf (packet + size, m_RecvStreamID);
		size += 4; // receiveStreamID
		htobe32buf (packet + size, m_SequenceNumber++);
		size += 4; // sequenceNum
		htobe32buf (packet + size, m_LastReceivedSequenceNumber >= 0 ? m_LastReceivedSequenceNumber : 0);
		size += 4; // ack Through
		packet[size] = 0;
		size++; // NACK count
		packet[size] = 0;
		size++; // resend delay
		bool isOfflineSignature = false;
		uint16_t flags = PACKET_FLAG_RESET;
		if (!m_DontSign)
		{
			flags |= PACKET_FLAG_SIGNATURE_INCLUDED;
			isOfflineSignature = m_LocalDestination.GetOwner ()->GetPrivateKeys ().IsOfflineSignature ();
			if (isOfflineSignature) flags |= PACKET_FLAG_OFFLINE_SIGNATURE | PACKET_FLAG_FROM_INCLUDED;
		}
		htobe16buf (packet + size, flags);
		size += 2; // flags
		if (m_DontSign)
		{
			memset (packet + size, 0, 2); // no options
			size += 2; // options size
		}
		else
		{
			uint8_t * optionsSize = packet + size; // set options size later
			size += 2; // options size
			if (isOfflineSignature)
			{
				size_t identityLen = m_LocalDestination.GetOwner ()->GetIdentity ()->GetFullLen ();
				m_LocalDestination.GetOwner ()->GetIdentity ()->ToBuffer (packet + size, identityLen);
				size += identityLen; // from
				const auto & offlineSignature
					= m_LocalDestination.GetOwner ()->GetPrivateKeys ().GetOfflineSignature ();
				memcpy (packet + size, offlineSignature.data (), offlineSignature.size ());
				size += offlineSignature.size (); // offline signature
			}
			size_t signatureLen = m_LocalDestination.GetOwner ()->GetPrivateKeys ().GetSignatureLen ();
			uint8_t * signature = packet + size;
			memset (signature, 0, signatureLen);
			size += signatureLen;
			htobe16buf (optionsSize, static_cast<uint16_t> (packet + size - 2 - optionsSize));
			m_LocalDestination.GetOwner ()->Sign (packet, static_cast<int> (size), signature);
		}
		p->len = size;
		return p;
	}

	void Stream::SendReset ()
	{
		Packet * p = CreateResetPacket ();
		boost::asio::post (m_Service, std::bind (&Stream::SendPacket, shared_from_this (), p));
		if (m_RoutingSession)
		{
			int numSentPackets = m_RoutingSession->NumSentPackets ();
			m_RoutingSession->SetNumSentPackets (numSentPackets + 1);
		}
		LogPrint (eLogDebug, "Streaming: RST sent, sSID=", m_SendStreamID);
	}

	bool Stream::SendResetOnce ()
	{
		if (!m_SendStreamID) return false;
		Packet * p = CreateResetPacket ();
		// Send synchronously and do not retain the packet: a RESET for a
		// connection we never accepted must not enter the resend queue, which
		// would retransmit it and arm timers for a stream nobody owns.
		SendPackets ({ p });
		m_LocalDestination.DeletePacket (p);
		LogPrint (eLogDebug, "Streaming: RST sent once, sSID=", m_SendStreamID);
		return true;
	}

	bool Stream::RejectWithReset (Packet * packet)
	{
		if (!packet) return false;
		// Suppress the SYNACK that HandleNextPacket sends for a first SYN: this
		// connection is being refused, not accepted.
		m_Status = eStreamStatusReset;
		HandleNextPacket (packet); // consumes the packet
		if (m_Status == eStreamStatusTerminated || !m_SendStreamID) return false;
		return SendResetOnce ();
	}

	void Stream::AbortWithReset ()
	{
		if (m_Status == eStreamStatusTerminated) return;
		m_Status = eStreamStatusReset;
		m_IsResetReceived = false;
		SendResetOnce ();
		Terminate (true);
	}

	size_t Stream::ConcatenatePackets (uint8_t * buf, size_t len)
	{
		if (m_ReceiveQueue.empty ()) return 0;
		size_t pos = 0;
		auto it = m_ReceiveQueue.begin ();
		while (pos < len && it != m_ReceiveQueue.end ())
		{
			Packet * packet = *it;
			size_t l = std::min (packet->GetLength (), len - pos);
			memcpy (buf + pos, packet->GetBuffer (), l);
			pos += l;
			packet->offset += l;
			if (!packet->GetLength ())
			{
				m_LocalDestination.DeletePacket (packet);
				it++;
			}
		}
		m_ReceiveQueue.erase (m_ReceiveQueue.begin (), it);
		return pos;
	}

	bool Stream::SendPacket (Packet * packet)
	{
		if (packet)
		{
			if (m_IsAckSendScheduled)
			{
				m_IsAckSendScheduled = false;
				m_AckSendTimer.cancel ();
			}
			if (!packet->sendTime) packet->sendTime = i2p::util::GetMillisecondsSinceEpoch ();
			SendPackets ({ packet });
			bool isEmpty = m_SentPackets.empty ();
			m_SentPackets.emplace_back (packet);
			if (isEmpty) ScheduleResend ();
			return true;
		}
		else
			return false;
	}

	void Stream::SendPackets (const std::list<Packet *> & packets)
	{
		// the local destination is a reference and it can be gone already: a handler
		// queued before the destination was stopped runs after it
		if (m_Status == eStreamStatusTerminated) return;
		if (!m_RemoteLeaseSet)
		{
			CancelRemoteLeaseChange ();
			UpdateCurrentRemoteLease ();
			if (!m_RemoteLeaseSet)
			{
				LogPrint (eLogError, "Streaming: Can't send packets, missing remote LeaseSet, sSID=", m_SendStreamID);
				return;
			}
		}
		if (!m_RoutingSession || m_RoutingSession->IsTerminated ()
			|| !m_RoutingSession->IsReadyToSend ()) // expired and detached or new session sent
		{
			m_RoutingSession = m_LocalDestination.GetOwner ()->GetRoutingSession (
				m_RemoteLeaseSet, true, !m_IsIncoming || m_SequenceNumber > 1);
			if (!m_RoutingSession)
			{
				LogPrint (eLogError, "Streaming: Can't obtain routing session, sSID=", m_SendStreamID);
				Terminate ();
				return;
			}
		}
		if (!m_CurrentOutboundTunnel && m_RoutingSession) // first message to send
		{
			// try to get shared path first
			auto routingPath = m_RoutingSession->GetSharedRoutingPath ();
			if (routingPath)
			{
				m_CurrentOutboundTunnel = routingPath->outboundTunnel;
				m_CurrentRemoteLease = routingPath->remoteLease;
				m_RTT = routingPath->rtt;
			}
		}

		auto ts = i2p::util::GetMillisecondsSinceEpoch ();
		if (!m_CurrentRemoteLease || !m_CurrentRemoteLease->endDate) // excluded from LeaseSet
		{
			CancelRemoteLeaseChange ();
			UpdateCurrentRemoteLease (true);
		}
		if (m_RemoteLeaseChangeTime && m_IsRemoteLeaseChangeInProgress && ts > m_RemoteLeaseChangeTime + INITIAL_RTO)
		{
			LogPrint (eLogDebug, "Streaming: RemoteLease changed, set initial window size");
			CancelRemoteLeaseChange ();
			m_CurrentRemoteLease = m_NextRemoteLease;
			ResetWindowSize ();
		}
		auto currentRemoteLease = m_CurrentRemoteLease;
		if (!m_IsRemoteLeaseChangeInProgress && m_RemoteLeaseSet && m_CurrentRemoteLease
			&& ts + i2p::data::LEASE_ENDDATE_THRESHOLD >= m_CurrentRemoteLease->endDate)
		{
			auto leases = m_RemoteLeaseSet->GetNonExpiredLeases (false);
			if (leases.size ())
			{
				m_IsRemoteLeaseChangeInProgress = true;
				UpdateCurrentRemoteLease (true);
				m_NextRemoteLease = m_CurrentRemoteLease;
			}
			else
				UpdateCurrentRemoteLease (true);
		}
		if (m_CurrentRemoteLease && ts < m_CurrentRemoteLease->endDate + i2p::data::LEASE_ENDDATE_THRESHOLD)
		{
			bool freshTunnel = false;
			if (!m_CurrentOutboundTunnel)
			{
				auto leaseRouter = i2p::data::netdb.FindRouter (m_CurrentRemoteLease->tunnelGateway);
				m_CurrentOutboundTunnel = m_LocalDestination.GetOwner ()->GetTunnelPool ()->GetNextOutboundTunnel (
					nullptr,
					leaseRouter ? leaseRouter->GetCompatibleTransports (false)
								: (i2p::data::RouterInfo::CompatibleTransports)i2p::data::RouterInfo::eAllTransports);
				freshTunnel = true;
			}
			else if (!m_CurrentOutboundTunnel->IsEstablished ())
				std::tie (m_CurrentOutboundTunnel, freshTunnel)
					= m_LocalDestination.GetOwner ()->GetTunnelPool ()->GetNewOutboundTunnel (m_CurrentOutboundTunnel);
			if (!m_CurrentOutboundTunnel)
			{
				LogPrint (eLogError, "Streaming: No outbound tunnels in the pool, sSID=", m_SendStreamID);
				m_CurrentRemoteLease = nullptr;
				return;
			}
			if (freshTunnel)
			{
				LogPrint (eLogDebug, "Streaming: OutboundTunnel changed, set initial window size");
				ResetWindowSize ();
				//				m_TunnelsChangeSequenceNumber = m_SequenceNumber; // should be determined more precisely
			}

			std::vector<i2p::tunnel::TunnelMessageBlock> msgs;
			for (const auto & it : packets)
			{
				auto msg = m_RoutingSession->WrapSingleMessage (m_LocalDestination.CreateDataMessage (
					it->GetBuffer (), it->GetLength (), m_Port, !m_RoutingSession->IsRatchets (), it->IsSYN ()));
				if (!msg)
				{
					LogPrint (eLogWarning, "Streaming: Failed to wrap outbound packet");
					continue;
				}
				msgs.push_back (i2p::tunnel::TunnelMessageBlock { i2p::tunnel::eDeliveryTypeTunnel,
					m_CurrentRemoteLease->tunnelGateway, m_CurrentRemoteLease->tunnelID, msg });
				m_NumSentBytes += it->GetLength ();
				if (m_IsRemoteLeaseChangeInProgress && !m_RemoteLeaseChangeTime)
				{
					m_RemoteLeaseChangeTime = ts;
					m_CurrentRemoteLease = currentRemoteLease; // change it back before new lease is confirmed
				}
			}
			m_CurrentOutboundTunnel->SendTunnelDataMsgs (msgs);
		}
		else
		{
			LogPrint (eLogWarning, "Streaming: Remote lease is not available, sSID=", m_SendStreamID);
			if (m_RoutingSession) m_RoutingSession->SetSharedRoutingPath (nullptr); // invalidate routing path
		}
	}

	void Stream::SendUpdatedLeaseSet ()
	{
		if (m_RoutingSession && !m_RoutingSession->IsTerminated ())
		{
			if (m_RoutingSession->IsLeaseSetNonConfirmed ())
			{
				auto ts = i2p::util::GetMillisecondsSinceEpoch ();
				if (ts > m_RoutingSession->GetLeaseSetSubmissionTime () + i2p::garlic::LEASESET_CONFIRMATION_TIMEOUT)
				{
					// LeaseSet was not confirmed, should try other tunnels
					LogPrint (eLogWarning, "Streaming: LeaseSet was not confirmed in ",
						i2p::garlic::LEASESET_CONFIRMATION_TIMEOUT, " milliseconds. Trying to resubmit");
					m_RoutingSession->SetSharedRoutingPath (nullptr);
					m_CurrentOutboundTunnel = nullptr;
					m_CurrentRemoteLease = nullptr;
					SendQuickAck ();
				}
			}
			else if (m_RoutingSession->IsLeaseSetUpdated ())
			{
				LogPrint (eLogDebug, "Streaming: sending updated LeaseSet");
				SendQuickAck ();
			}
		}
		else
			SendQuickAck ();
	}

	void Stream::ScheduleSend ()
	{
		if (m_Status != eStreamStatusTerminated)
		{
			m_SendTimer.cancel ();
			uint64_t interval = SEND_INTERVAL + m_LocalDestination.GetRandom () % SEND_INTERVAL_VARIANCE;
			if (interval < m_PacingTime) interval = m_PacingTime;
			m_SendTimer.expires_after (std::chrono::microseconds (interval));
			m_SendTimer.async_wait (std::bind (&Stream::HandleSendTimer, shared_from_this (), std::placeholders::_1));
		}
	}

	void Stream::HandleSendTimer (const boost::system::error_code & ecode)
	{
		if (ecode != boost::asio::error::operation_aborted)
		{
			auto ts = i2p::util::GetMillisecondsSinceEpoch ();
			if (m_LastSendTime && ts * 1000 > m_LastSendTime * 1000 + m_PacingTime)
			{
				if (m_PacingTime)
				{
					if (m_RoutingSession)
					{
						uint64_t lastSendTime = m_RoutingSession->LastSendTime ();
						if (lastSendTime)
						{
							auto numPackets
								= std::lldiv (m_PacingTimeRem + ts * 1000 - lastSendTime * 1000, m_PacingTime);
							m_NumPacketsToSend = static_cast<int> (numPackets.quot);
							m_PacingTimeRem = numPackets.rem;
						}
						else
						{
							auto numPackets
								= std::lldiv (m_PacingTimeRem + ts * 1000 - m_LastSendTime * 1000, m_PacingTime);
							m_NumPacketsToSend = static_cast<int> (numPackets.quot);
							m_PacingTimeRem = numPackets.rem;
						}
					}
				}
				else
				{
					LogPrint (eLogError, "Streaming: pacing time is zero");
					m_NumPacketsToSend = 1;
					m_PacingTimeRem = 0;
				}
				m_IsSendTime = true;
				if (m_NumPacketsToSend && m_WindowIncCounter
					&& (m_WindowSize < m_MaxWindowSize || m_WindowDropTargetSize) && !m_SendBuffer.IsEmpty ()
					&& m_PacingTime > m_MinPacingTime)
				{
					float winSize = m_WindowSize;
					if (m_WindowDropTargetSize) winSize = m_WindowDropTargetSize;
					float maxWinSize = m_MaxWindowSize;
					if (m_LastWindowIncTime)
						maxWinSize = static_cast<float> (
							(ts - m_LastWindowIncTime) / (m_RTT / MAX_WINDOW_SIZE_INC_PER_RTT) + winSize);

					int windowIncCounter = std::min (m_NumPacketsToSend, m_WindowIncCounter);
					if (m_WindowDropTargetSize)
					{
						if (m_LastWindowDropSize && (m_LastWindowDropSize >= m_WindowDropTargetSize))
							m_WindowDropTargetSize += static_cast<float> (windowIncCounter
								* (1
									- (1
										/ ((m_LastWindowDropSize + PREV_SPEED_KEEP_TIME_COEFF)
											/ m_WindowDropTargetSize)))); // some magic here
						else if (m_LastWindowDropSize && (m_LastWindowDropSize < m_WindowDropTargetSize))
							m_WindowDropTargetSize += static_cast<float> (windowIncCounter
								* ((m_WindowDropTargetSize - (m_LastWindowDropSize - PREV_SPEED_KEEP_TIME_COEFF)))
								/ m_WindowDropTargetSize); // some magic here
						else
							m_WindowDropTargetSize += static_cast<float> (windowIncCounter
								* ((m_WindowDropTargetSize - (1 - PREV_SPEED_KEEP_TIME_COEFF))
									/ m_WindowDropTargetSize));
						if (m_WindowDropTargetSize > m_MaxWindowSize) m_WindowDropTargetSize = m_MaxWindowSize;
						m_WindowIncCounter -= windowIncCounter;
						if (m_WindowDropTargetSize >= maxWinSize)
						{
							m_WindowDropTargetSize = maxWinSize;
						}
					}
					else
					{
						if (m_LastWindowDropSize && (m_LastWindowDropSize >= m_WindowSize))
							m_WindowSize += static_cast<float> (windowIncCounter
								* (1
									- (1
										/ ((m_LastWindowDropSize + PREV_SPEED_KEEP_TIME_COEFF)
											/ m_WindowSize)))); // some magic here
						else if (m_LastWindowDropSize && (m_LastWindowDropSize < m_WindowSize))
							m_WindowSize += static_cast<float> (windowIncCounter
								* ((m_WindowSize - (m_LastWindowDropSize - PREV_SPEED_KEEP_TIME_COEFF))
									/ m_WindowSize)); // some magic here
						else
							m_WindowSize += static_cast<float> (
								windowIncCounter * ((m_WindowSize - (1 - PREV_SPEED_KEEP_TIME_COEFF)) / m_WindowSize));
						if (m_WindowSize > m_MaxWindowSize) m_WindowSize = m_MaxWindowSize;
						m_WindowIncCounter -= windowIncCounter;
						if (m_WindowSize >= maxWinSize)
						{
							m_WindowSize = maxWinSize;
						}
					}
					UpdatePacingTime ();
				}
				m_LastWindowIncTime = ts;
				if (m_IsNAcked || m_IsResendNeeded || m_IsClientChoked || m_IsClientChoked2) // resend packets
					ResendPacket ();
				else if (m_WindowSize > int (m_SentPackets.size ())) // send packets
					SendBuffer ();
			}
			else // pass
				ScheduleSend ();
		}
	}

	void Stream::ScheduleResend ()
	{
		if (m_Status != eStreamStatusTerminated)
		{
			m_ResendTimer.cancel ();
			// check for invalid value
			if (m_RTO <= 0) m_RTO = INITIAL_RTO;
			m_ResendTimer.expires_after (std::chrono::milliseconds (m_RTO));
			m_ResendTimer.async_wait (
				std::bind (&Stream::HandleResendTimer, shared_from_this (), std::placeholders::_1));
		}
	}

	void Stream::HandleResendTimer (const boost::system::error_code & ecode)
	{
		if (ecode != boost::asio::error::operation_aborted)
		{
			m_IsSendTime = true;
			if (m_RTO > INITIAL_RTO) m_RTO = INITIAL_RTO;
			m_SendTimer.cancel (); // if no ack's in RTO, disable fast retransmit
			m_IsTimeOutResend = true;
			m_IsNAcked = false;
			m_IsResendNeeded = false;
			m_NumPacketsToSend = 1;
			ResendPacket (); // send one packet per RTO, waiting for ack
		}
	}

	void Stream::ResendPacket ()
	{
		// check for resend attempts
		if (m_IsIncoming && m_SequenceNumber == 1 && m_NumResendAttempts > 0
			&& (!m_RoutingSession || !m_RoutingSession->HasSharedRoutingPath ())) // ack not received
		{
			LogPrint (eLogWarning, "Streaming: SYNACK packet was not ACKed after ", m_NumResendAttempts,
				" attempts, terminate, rSID=", m_RecvStreamID, ", sSID=", m_SendStreamID);
			m_Status = eStreamStatusReset;
			Close ();
			return;
		}
		if (m_NumResendAttempts >= m_MaxNumResendAttempts)
		{
			LogPrint (eLogWarning, "Streaming: packet was not ACKed after ", m_MaxNumResendAttempts,
				" attempts, terminate, rSID=", m_RecvStreamID, ", sSID=", m_SendStreamID);
			m_Status = eStreamStatusReset;
			Close ();
			return;
		}

		// collect packets to resend
		auto ts = i2p::util::GetMillisecondsSinceEpoch ();
		std::list<Packet *> packets;
		if (m_IsNAcked && !m_IsClientChoked && !m_IsClientChoked2)
		{
			for (auto it : m_NACKedPackets)
			{
				if (ts >= it->sendTime + m_RTO)
				{
					if (ts < it->sendTime + m_RTO * 3)
						it->resent = true;
					else
						it->resent = false;
					it->sendTime = ts;
					packets.emplace_back (it);
					if ((int)packets.size () >= m_NumPacketsToSend) break;
				}
			}
		}
		else
		{
			for (auto it : m_SentPackets)
			{
				if (ts >= it->sendTime + m_RTO)
				{
					if (ts < it->sendTime + m_RTO * 3)
						it->resent = true;
					else
						it->resent = false;
					it->sendTime = ts;
					packets.emplace_back (it);
					if (m_IsClientChoked2 && it->GetSeqn () == m_DropWindowDelaySequenceNumber)
						m_IsClientChoked2 = false;
					if ((int)packets.size () >= m_NumPacketsToSend) break;
				}
			}
		}

		// select tunnels if necessary and send
		if (packets.size () > 0 && m_IsSendTime)
		{
			if (m_IsNAcked)
				m_NumResendAttempts = 1;
			else if (m_IsTimeOutResend)
				m_NumResendAttempts++;
			if (m_NumResendAttempts == 1 && m_RTO != INITIAL_RTO)
			{
				// loss-based CC
				if (!m_IsWinDropped && LOSS_BASED_CONTROL_ENABLED && !m_IsClientChoked)
				{
					LogPrint (eLogDebug, "Streaming: Packet loss, reduce window size");
					if (m_WindowDropTargetSize)
						m_LastWindowDropSize = m_WindowDropTargetSize;
					else
						m_LastWindowDropSize = m_WindowSize;
					m_WindowDropTargetSize = static_cast<float> (m_LastWindowDropSize * 0.5); // -50% to drain queue
					if (m_WindowDropTargetSize < MIN_WINDOW_SIZE) m_WindowDropTargetSize = MIN_WINDOW_SIZE;
					m_WindowIncCounter = 0; // disable window growth
					m_DropWindowDelaySequenceNumber = m_SequenceNumber + int (m_WindowDropTargetSize);
					m_IsFirstACK = true; // ignore first RTT sample
					m_IsWinDropped = true; // don't drop window twice
					UpdatePacingTime ();
				}
			}
			else if (m_IsTimeOutResend && m_NumResendAttempts > 1)
			{
				m_RTO = INITIAL_RTO; // drop RTO to initial upon tunnels pair change
				m_WindowDropTargetSize = INITIAL_WINDOW_SIZE;
				m_LastWindowDropSize = 0;
				m_WindowIncCounter = 0;
				m_IsWinDropped = true;
				m_IsFirstRttSample = true;
				m_DropWindowDelaySequenceNumber = 0;
				m_IsFirstACK = true;
				m_LastACKRecieveTime = 0;
				m_ACKRecieveInterval = m_AckDelay;
				UpdatePacingTime ();
				if (m_RoutingSession) m_RoutingSession->SetSharedRoutingPath (nullptr);
				if (m_NumResendAttempts & 1)
				{
					// pick another outbound tunnel
					m_CurrentOutboundTunnel = m_LocalDestination.GetOwner ()->GetTunnelPool ()->GetNextOutboundTunnel (
						m_CurrentOutboundTunnel);
					LogPrint (eLogWarning, "Streaming: Resend #", m_NumResendAttempts,
						", another outbound tunnel has been selected for stream with sSID=", m_SendStreamID);
				}
				else
				{
					CancelRemoteLeaseChange ();
					UpdateCurrentRemoteLease (); // pick another lease
					LogPrint (eLogWarning, "Streaming: Resend #", m_NumResendAttempts,
						", another remote lease has been selected for stream with rSID=", m_RecvStreamID,
						", sSID=", m_SendStreamID);
				}
			}
			if (m_IsTimeOutResend) ScheduleResend ();
			SendPackets (packets);
			m_LastSendTime = ts;
			if (m_RoutingSession) m_RoutingSession->SetLastSendTime (ts);
			m_IsSendTime = false;
		}
		else if (!m_IsClientChoked && !m_IsClientChoked2)
			SendBuffer ();
		m_IsSendTime = false;
		if (m_IsTimeOutResend)
			ScheduleResend (); // ^ m_IsTimeOutResend = false
		else if (m_IsNAcked || m_IsResendNeeded || m_IsClientChoked || m_IsClientChoked2)
			ScheduleSend ();
	}

	void Stream::ScheduleAck (int timeout)
	{
		if (m_IsAckSendScheduled) m_AckSendTimer.cancel ();
		m_IsAckSendScheduled = true;
		if (timeout < MIN_SEND_ACK_TIMEOUT) timeout = MIN_SEND_ACK_TIMEOUT;
		m_AckSendTimer.expires_after (std::chrono::milliseconds (timeout));
		m_AckSendTimer.async_wait (std::bind (&Stream::HandleAckSendTimer, shared_from_this (), std::placeholders::_1));
	}

	void Stream::HandleAckSendTimer (const boost::system::error_code & ecode)
	{
		if (ecode != boost::asio::error::operation_aborted)
		{
			if (m_IsAckSendScheduled)
			{
				if (m_LastReceivedSequenceNumber < 0)
				{
					LogPrint (eLogWarning, "Streaming: SYN has not been received after ", SYN_TIMEOUT,
						" milliseconds after follow on, terminate rSID=", m_RecvStreamID, ", sSID=", m_SendStreamID);
					m_Status = eStreamStatusReset;
					Close ();
					return;
				}
				if (m_Status == eStreamStatusOpen)
				{
					if (m_RoutingSession && m_RoutingSession->IsLeaseSetNonConfirmed ())
					{
						auto ts = i2p::util::GetMillisecondsSinceEpoch ();
						if (ts > m_RoutingSession->GetLeaseSetSubmissionTime ()
								+ i2p::garlic::LEASESET_CONFIRMATION_TIMEOUT)
						{
							// seems something went wrong and we should re-select tunnels
							m_CurrentOutboundTunnel = nullptr;
							m_CurrentRemoteLease = nullptr;
						}
					}
					if (m_LastReceivedSequenceNumber == 0 && m_SequenceNumber == 1)
					{
						if (m_NumResendAttempts > 1)
						{
							m_Status = eStreamStatusReset;
							Close ();
							return;
						}
						m_NumResendAttempts++;
						ScheduleAck (INITIAL_RTO);
					}
					SendQuickAck ();
				}
				m_IsAckSendScheduled = false;
			}
		}
	}

	void Stream::UpdateCurrentRemoteLease (bool expired)
	{
		bool isLeaseChanged = true;
		if (!m_RemoteLeaseSet || m_RemoteLeaseSet->IsExpired ())
		{
			if (!m_RemoteIdentity)
			{
				// no remote identity established yet, nothing to look up
				m_Status = eStreamStatusClosed;
				AsyncClose ();
				return;
			}
			auto remoteLeaseSet = m_LocalDestination.GetOwner ()->FindLeaseSet (m_RemoteIdentity->GetIdentHash ());
			if (!remoteLeaseSet)
			{
				LogPrint (eLogWarning, "Streaming: LeaseSet ", m_RemoteIdentity->GetIdentHash ().ToBase32 (),
					m_RemoteLeaseSet ? " expired" : " not found");
				if (!m_IsIncoming) // outgoing
				{
					auto requestCallback = [s = shared_from_this ()] (std::shared_ptr<i2p::data::LeaseSet> ls)
					{
						if (!ls && s->m_Status == eStreamStatusOpen) // LeaseSet not found
						{
							// close the socket without sending FIN or RST
							s->m_Status = eStreamStatusClosed;
							s->AsyncClose ();
						}
					};

					if (m_RemoteLeaseSet && m_RemoteLeaseSet->IsPublishedEncrypted ())
					{
						m_LocalDestination.GetOwner ()->RequestDestinationWithEncryptedLeaseSet (
							std::make_shared<i2p::data::BlindedPublicKey> (m_RemoteIdentity), requestCallback);
						return; // we keep m_RemoteLeaseSet for possible next request
					}
					else
					{
						m_RemoteLeaseSet = nullptr;
						m_LocalDestination.GetOwner ()->RequestDestination (
							m_RemoteIdentity->GetIdentHash (), requestCallback); // try to request for a next attempt
					}
				}
				else // incoming
				{
					// Java can accept an inbound SYN without a locally cached
					// LeaseSet; the router resolves the destination for the
					// SYNACK. Request it asynchronously and keep the stream
					// pending instead of terminating a valid first contact.
					if (!m_IsRequestingLeaseSet)
					{
						m_IsRequestingLeaseSet = true;
						auto callback = [s = shared_from_this ()] (std::shared_ptr<i2p::data::LeaseSet> ls)
						{
							s->m_IsRequestingLeaseSet = false;
							if (!ls)
							{
								if (s->m_Status != eStreamStatusClosed)
								{
									s->m_Status = eStreamStatusClosed;
									s->AsyncClose ();
								}
								return;
							}
							s->m_RemoteLeaseSet = ls;
							s->m_RemoteIdentity = ls->GetIdentity ();
							s->m_TransientVerifier = ls->GetTransientVerifier ();
							if (s->m_Status != eStreamStatusClosed && s->m_Status != eStreamStatusReset
								&& s->m_Status != eStreamStatusTerminated)
								s->ResumeSend ();
						};
						if (!m_LocalDestination.GetOwner ()->RequestDestination (
								m_RemoteIdentity->GetIdentHash (), callback))
						{
							m_IsRequestingLeaseSet = false;
							m_Status = eStreamStatusClosed;
							AsyncClose ();
						}
					}
					return;
				}
			}
			else
			{
				// LeaseSet updated
				m_RemoteLeaseSet = remoteLeaseSet;
				m_RemoteIdentity = m_RemoteLeaseSet->GetIdentity ();
				m_TransientVerifier = m_RemoteLeaseSet->GetTransientVerifier ();
			}
		}
		if (m_RemoteLeaseSet)
		{
			if (!m_RoutingSession)
				m_RoutingSession = m_LocalDestination.GetOwner ()->GetRoutingSession (m_RemoteLeaseSet, true);
			auto leases = m_RemoteLeaseSet->GetNonExpiredLeases (false); // try without threshold first
			if (leases.empty ())
			{
				expired = false;
				// time to request
				if (m_RemoteLeaseSet->IsPublishedEncrypted ())
					m_LocalDestination.GetOwner ()->RequestDestinationWithEncryptedLeaseSet (
						std::make_shared<i2p::data::BlindedPublicKey> (m_RemoteIdentity));
				else
					m_LocalDestination.GetOwner ()->RequestDestination (m_RemoteIdentity->GetIdentHash ());
				leases = m_RemoteLeaseSet->GetNonExpiredLeases (true); // then with threshold
			}
			if (!leases.empty ())
			{
				bool updated = false;
				if (expired && m_CurrentRemoteLease)
				{
					for (const auto & it : leases)
						if ((it->tunnelGateway == m_CurrentRemoteLease->tunnelGateway)
							&& (it->tunnelID != m_CurrentRemoteLease->tunnelID))
						{
							m_CurrentRemoteLease = it;
							updated = true;
							break;
						}
				}
				if (!updated)
				{
					uint32_t i = m_LocalDestination.GetRandom () % leases.size ();
					if (m_CurrentRemoteLease && leases[i]->tunnelID == m_CurrentRemoteLease->tunnelID)
					{
						// make sure we don't select previous
						if (leases.size () > 1)
							i = (i + 1) % leases.size (); // if so, pick next
						else
							isLeaseChanged = false;
					}
					m_CurrentRemoteLease = leases[i];
				}
			}
			else
			{
				LogPrint (eLogWarning, "Streaming: All remote leases are expired");
				m_RemoteLeaseSet = nullptr;
				m_CurrentRemoteLease = nullptr;
				// we have requested expired before, no need to do it twice
			}
		}
		else
		{
			LogPrint (eLogWarning, "Streaming: Remote LeaseSet not found");
			m_CurrentRemoteLease = nullptr;
		}
		if (isLeaseChanged && !m_IsRemoteLeaseChangeInProgress)
		{
			LogPrint (eLogDebug, "Streaming: RemoteLease changed, set initial window size");
			ResetWindowSize ();
		}
	}

	void Stream::ResetRoutingPath ()
	{
		m_CurrentOutboundTunnel = nullptr;
		m_CurrentRemoteLease = nullptr;
		m_RTT = INITIAL_RTT;
		m_RTO = INITIAL_RTO;
		if (m_RoutingSession) m_RoutingSession->SetSharedRoutingPath (nullptr); // TODO: count failures
	}

	void Stream::UpdatePacingTime ()
	{
		double rtt = m_MinRTT;
		if (m_IsWinDropped) rtt = m_SlowRTT;
		if (m_WindowDropTargetSize)
			m_PacingTime = static_cast<uint64_t> (std::round (rtt * 1000 / m_WindowDropTargetSize));
		else
			m_PacingTime = static_cast<uint64_t> (std::round (rtt * 1000 / m_WindowSize));
		if (m_MinPacingTime && m_PacingTime < m_MinPacingTime) m_PacingTime = m_MinPacingTime;
	}

	void Stream::ProcessWindowDrop ()
	{
		if (m_WindowDropTargetSize)
		{
			m_LastWindowDropSize
				= static_cast<float> (m_WindowDropTargetSize * ((m_MinRTT + m_Jitter * 6) / m_SlowRTT));
			m_WindowDropTargetSize = static_cast<float> (
				m_LastWindowDropSize * m_MinRTT / m_SlowRTT); // we start send faster when rtt will decrease
		}
		else
		{
			m_LastWindowDropSize = static_cast<float> (m_WindowSize * ((m_MinRTT + m_Jitter * 6) / m_SlowRTT));
			m_WindowDropTargetSize = static_cast<float> (
				m_WindowSize * m_MinRTT / m_SlowRTT); // we start send faster when rtt will decrease
		}
		if (m_WindowDropTargetSize < MIN_WINDOW_SIZE) m_WindowDropTargetSize = MIN_WINDOW_SIZE;
		m_WindowIncCounter = 0; // disable window growth
		m_DropWindowDelaySequenceNumber = m_SequenceNumber + int (m_WindowDropTargetSize);
		m_IsFirstACK = true; // ignore first RTT sample
		m_IsWinDropped = true; // don't drop window twice
		UpdatePacingTime ();
	}

	void Stream::ResetWindowSize ()
	{
		m_RTO = INITIAL_RTO;
		if (!m_IsClientChoked)
		{
			if (m_WindowSize > INITIAL_WINDOW_SIZE)
			{
				m_WindowDropTargetSize = (float)INITIAL_WINDOW_SIZE;
				m_IsWinDropped = true;
			}
			else
				m_WindowSize = INITIAL_WINDOW_SIZE;
		}
		m_LastWindowDropSize = 0;
		m_IsFirstRttSample = true;
		m_IsFirstACK = true;
		m_WindowIncCounter = 0; // disable window growth
		m_DropWindowDelaySequenceNumber = m_SequenceNumber - int (m_SentPackets.size ()) + INITIAL_WINDOW_SIZE;
		m_IsWinDropped = true; // don't drop window twice
		UpdatePacingTime ();
	}

	void Stream::CancelRemoteLeaseChange ()
	{
		m_RemoteLeaseChangeTime = 0;
		m_IsRemoteLeaseChangeInProgress = false;
	}

	bool SyncAcceptState::Deliver (std::shared_ptr<Stream> stream)
	{
		{
			std::lock_guard<std::mutex> lock (m_Mutex);
			if (m_Cancelled) return false;
			m_Stream = std::move (stream);
			m_Completed = true;
		}
		m_Changed.notify_all ();
		return true;
	}

	bool SyncAcceptState::Cancel ()
	{
		std::lock_guard<std::mutex> lock (m_Mutex);
		if (m_Completed) return false;
		m_Cancelled = true;
		return true;
	}

	bool SyncAcceptState::Wait (int timeout)
	{
		std::unique_lock<std::mutex> lock (m_Mutex);
		auto ready = [this] () { return m_Completed; };
		// A zero timeout waits indefinitely, a positive one bounds the wait and a
		// negative one returns immediately, matching the previous helper.
		if (timeout != 0) return m_Changed.wait_for (lock, std::chrono::seconds (timeout), ready);
		m_Changed.wait (lock, ready);
		return true;
	}

	std::shared_ptr<Stream> SyncAcceptState::GetStream () const
	{
		std::lock_guard<std::mutex> lock (m_Mutex);
		return m_Stream;
	}

	StreamingDestination::SynAdmission StreamingDestination::DecideSynAdmission (
		uint32_t receiveStreamID, i2p::garlic::ECIESX25519AEADRatchetSession * from)
	{
		if (IsRecentlyClosed (receiveStreamID)) return SynAdmission::eRecentlyClosed;
		{
			std::lock_guard<std::mutex> lock (m_StreamsMutex);
			if (m_IncomingStreams.find (receiveStreamID) != m_IncomingStreams.end ())
				return SynAdmission::eDuplicate;
		}
		if (m_Owner->GetStreamingMaxConcurrentStreams () > 0
			&& (int)GetNumStreams () > m_Owner->GetStreamingMaxConcurrentStreams ())
			return SynAdmission::eOverLimit;
		{
			// Only the owning service thread appends to the backlog, so this
			// check is exact when the caller is that thread.
			std::lock_guard<std::mutex> lock (m_AcceptorMutex);
			if (!m_Acceptor && m_ArmedAcceptors.empty ()
				&& m_PendingIncomingStreams.size () >= MAX_PENDING_INCOMING_BACKLOG)
				return SynAdmission::eBacklogFull;
		}
		if (from && m_ConnectionThrottle)
		{
			auto ts = i2p::util::GetSecondsSinceEpoch ();
			if (m_ConnectionThrottle->Check (from->GetRemoteStaticKey (), ts) > 0) return SynAdmission::eThrottled;
		}
		return SynAdmission::eAccept;
	}

	bool StreamingDestination::RejectSynWithReset (Packet * packet, uint32_t receiveStreamID)
	{
		if (!packet) return false;
		// A detached stream consumes the SYN so only an authenticated packet is
		// answered, and it is never registered: follow-on packets for this
		// receiveStreamID still find no stream and are treated as unknown.
		auto stream = std::make_shared<Stream> (m_Owner->GetService (), shared_from_this ());
		stream->SetPorts (m_LastFromPort, m_LastToPort);
		bool sent = stream->RejectWithReset (packet);
		if (sent)
		{
			m_NumRejectionResets.fetch_add (1);
			LogPrint (eLogDebug, "Streaming: RESET sent for rejected SYN rSID=", receiveStreamID);
		}
		stream->Terminate (false);
		return sent;
	}

	bool StreamingDestination::BuildRejectionResetForTesting (uint32_t receiveStreamID)
	{
		auto stream = std::make_shared<Stream> (m_Owner->GetService (), shared_from_this ());
		stream->SetStreamIDsForTesting (receiveStreamID, GetRandom ());
		bool sent = stream->SendResetOnce ();
		if (sent) m_NumRejectionResets.fetch_add (1);
		stream->Terminate (false);
		return sent;
	}

	StreamAdmission StreamingDestination::EvaluateAdmission (const i2p::data::IdentHash & peer) const
	{
		std::shared_ptr<const AdmissionPredicate> predicate;
		{
			std::lock_guard<std::mutex> lock (m_AcceptorMutex);
			predicate = m_AdmissionPredicate;
		}
		// Invoke outside the lock: the filter takes its own lock and may read a
		// cached file list, so no streaming lock may be held here.
		if (!predicate || !*predicate) return StreamAdmission::eAllow;
		return (*predicate) (peer, m_LocalPort);
	}

	StreamAdmission StreamingDestination::ApplyAdmissionForTesting (
		uint32_t receiveStreamID, const i2p::data::IdentHash & peer)
	{
		auto action = EvaluateAdmission (peer);
		switch (action)
		{
		case StreamAdmission::eDrop:
			LogSynRejection ("access filter drop", receiveStreamID);
			break;
		case StreamAdmission::eReset:
			LogSynRejection ("access filter deny", receiveStreamID);
			RememberRecentlyClosed (receiveStreamID);
			BuildRejectionResetForTesting (receiveStreamID);
			break;
		case StreamAdmission::eAllow: break;
		}
		return action;
	}

	StreamAdmission StreamingDestination::ApplyRejectionActionForTesting (uint32_t receiveStreamID)
	{
		auto action = m_RejectionAction.load ();
		if (action == StreamAdmission::eDrop)
			LogSynRejection ("limit drop", receiveStreamID);
		else
		{
			LogSynRejection ("limit reset", receiveStreamID);
			RememberRecentlyClosed (receiveStreamID);
			BuildRejectionResetForTesting (receiveStreamID);
		}
		return action;
	}

	void StreamingDestination::RejectSyn (Packet * packet, uint32_t receiveStreamID, const char * reason)
	{
		LogSynRejection (reason, receiveStreamID);
		if (m_RejectionAction.load () == StreamAdmission::eDrop)
		{
			// `limitAction=drop`: the peer gets no answer at all, so the SYN is
			// consumed without a stream and without polluting the
			// recently-closed table (a retransmission is dropped again).
			DeletePacket (packet);
			return;
		}
		RememberRecentlyClosed (receiveStreamID);
		RejectSynWithReset (packet, receiveStreamID);
	}

	bool StreamingDestination::IsRecentlyClosed (uint32_t receiveStreamID) const
	{
		if (!receiveStreamID) return false;
		auto ts = i2p::util::GetSecondsSinceEpoch ();
		std::lock_guard<std::mutex> lock (m_RecentlyClosedMutex);
		auto it = m_RecentlyClosed.find (receiveStreamID);
		if (it == m_RecentlyClosed.end ()) return false;
		if (ts > it->second + RECENTLY_CLOSED_TIMEOUT)
		{
			m_RecentlyClosed.erase (it);
			return false;
		}
		return true;
	}

	void StreamingDestination::RememberRecentlyClosed (uint32_t receiveStreamID)
	{
		if (!receiveStreamID) return;
		auto ts = i2p::util::GetSecondsSinceEpoch ();
		std::lock_guard<std::mutex> lock (m_RecentlyClosedMutex);
		// Decay first so the bound applies to the live window only.
		for (auto it = m_RecentlyClosed.begin (); it != m_RecentlyClosed.end ();)
		{
			if (ts > it->second + RECENTLY_CLOSED_TIMEOUT)
				it = m_RecentlyClosed.erase (it);
			else
				++it;
		}
		while (m_RecentlyClosed.size () >= MAX_RECENTLY_CLOSED_STREAMS)
		{
			auto oldest = m_RecentlyClosed.begin ();
			for (auto it = m_RecentlyClosed.begin (); it != m_RecentlyClosed.end (); ++it)
				if (it->second < oldest->second) oldest = it;
			if (oldest == m_RecentlyClosed.end ()) break;
			m_RecentlyClosed.erase (oldest);
		}
		m_RecentlyClosed[receiveStreamID] = ts;
	}

	void StreamingDestination::ClearRecentlyClosed ()
	{
		std::lock_guard<std::mutex> lock (m_RecentlyClosedMutex);
		m_RecentlyClosed.clear ();
	}

	void StreamingDestination::LogSynRejection (const char * reason, uint32_t receiveStreamID)
	{
		// Throttle: a rejected peer retransmits, and each retransmission must not
		// produce an unbounded log line.
		auto ts = i2p::util::GetMillisecondsSinceEpoch ();
		if (ts < m_LastSynRejectLogTime + SYN_REJECT_LOG_INTERVAL)
		{
			m_NumSuppressedSynRejects++;
			return;
		}
		if (m_NumSuppressedSynRejects)
			LogPrint (eLogInfo, "Streaming: Incoming SYN rSID=", receiveStreamID, " rejected: ", reason, " (",
				m_NumSuppressedSynRejects, " similar lines suppressed)");
		else
			LogPrint (eLogInfo, "Streaming: Incoming SYN rSID=", receiveStreamID, " rejected: ", reason);
		m_NumSuppressedSynRejects = 0;
		m_LastSynRejectLogTime = ts;
	}

	StreamingDestination::StreamingDestination (
		std::shared_ptr<i2p::client::ClientDestination> owner, uint16_t localPort, bool gzip)
		: m_Owner (owner)
		, m_LocalPort (localPort)
		, m_Gzip (gzip)
		, m_PendingIncomingTimer (m_Owner->GetService ())
		, m_ConnectionCleanupTimer (m_Owner->GetService ())
		, m_LastCleanupTime (i2p::util::GetSecondsSinceEpoch ())
		, m_ConnectionThrottle (owner->GetStreamingConnectionThrottle ())
	{
		UpdateConnectionLimits ();
	}

	void StreamingDestination::SetOwner (std::shared_ptr<i2p::client::ClientDestination> owner)
	{
		m_Owner = std::move (owner);
		m_ConnectionThrottle = m_Owner ? m_Owner->GetStreamingConnectionThrottle () : nullptr;
		UpdateConnectionLimits ();
	}

	void StreamingDestination::UpdateConnectionLimits ()
	{
		StreamingConnectionLimits limits;
		if (m_Owner)
		{
			limits.perPeerMinute = m_Owner->GetStreamingMaxConnsPerMinute ();
			limits.perPeerHour = m_Owner->GetStreamingMaxConnsPerHour ();
			limits.perPeerDay = m_Owner->GetStreamingMaxConnsPerDay ();
			limits.totalMinute = m_Owner->GetStreamingMaxTotalConnsPerMinute ();
			limits.totalHour = m_Owner->GetStreamingMaxTotalConnsPerHour ();
			limits.totalDay = m_Owner->GetStreamingMaxTotalConnsPerDay ();
		}
		if (m_ConnectionThrottle) m_ConnectionThrottle->SetLimits (limits);
	}

	void StreamingDestination::ScheduleConnectionCleanup ()
	{
		m_ConnectionCleanupTimer.expires_after (std::chrono::seconds (STREAMING_CONNECTION_CLEANUP_INTERVAL));
		m_ConnectionCleanupTimer.async_wait ([weak = weak_from_this ()] (const boost::system::error_code & ecode)
			{
				if (auto self = weak.lock ()) self->HandleConnectionCleanup (ecode);
			});
	}

	void StreamingDestination::HandleConnectionCleanup (const boost::system::error_code & ecode)
	{
		if (ecode) return;
		if (m_ConnectionThrottle) m_ConnectionThrottle->Cleanup (i2p::util::GetSecondsSinceEpoch ());
		ScheduleConnectionCleanup ();
	}

	StreamingDestination::~StreamingDestination ()
	{
		for (auto & it : m_SavedPackets)
		{
			for (auto it1 : it.second) DeletePacket (it1);
			it.second.clear ();
		}
		m_SavedPackets.clear ();
	}

	void StreamingDestination::Start ()
	{
		// Like Stop(), timer arming belongs to the owning io_context. SAM
		// creates and starts an additional per-port destination from its own
		// control thread while the destination service may already be running.
		if (m_Owner)
		{
			auto & service = m_Owner->GetService ();
			if (!service.stopped () && !service.get_executor ().running_in_this_thread ())
			{
				auto self = shared_from_this ();
				boost::asio::post (service, [self] () { self->StartInternal (); });
				return;
			}
		}
		StartInternal ();
	}

	void StreamingDestination::StartInternal ()
	{
		// Start must stay idempotent even when SAM posts it and the owning
		// destination also starts the same object from its service thread.
		if (m_Started.exchange (true)) return;
		ScheduleConnectionCleanup ();
	}

	void StreamingDestination::Stop ()
	{
		// Timers, stream maps and acceptor callbacks belong to the owning
		// destination's io_context. SAM removes a streaming destination from its
		// own control thread, so marshal the teardown like ClientDestination::Stop
		// does instead of cancelling timers and terminating streams concurrently.
		if (m_Owner)
		{
			auto & service = m_Owner->GetService ();
			if (!service.stopped () && !service.get_executor ().running_in_this_thread ())
			{
				auto done = std::make_shared<std::promise<void>> ();
				auto future = done->get_future ();
				auto self = shared_from_this ();
				boost::asio::post (service,
					[this, self, done] ()
					{
						StopInternal ();
						done->set_value ();
					});
				if (future.wait_for (std::chrono::seconds (i2p::client::STOP_ON_SERVICE_TIMEOUT)) == std::future_status::ready) return;
				LogPrint (eLogError, "Streaming: Service didn't stop the destination in ", i2p::client::STOP_ON_SERVICE_TIMEOUT,
					" seconds");
				return;
			}
		}
		StopInternal ();
	}

	void StreamingDestination::StopInternal ()
	{
		m_Started.store (false);
		ResetAcceptor ();
		ResetPongHandler ();
		m_PendingIncomingTimer.cancel ();
		m_ConnectionCleanupTimer.cancel ();
		ClearRecentlyClosed ();
		{
			std::lock_guard<std::mutex> l (m_AcceptorMutex);
			m_PendingIncomingStreams.clear ();
		}
		{
			std::lock_guard<std::mutex> l (m_StreamsMutex);
			for (auto it : m_Streams) it.second->Terminate (false); // we delete here
			m_Streams.clear ();
			m_IncomingStreams.clear ();
			m_LastStream = nullptr;
		}
	}

	void StreamingDestination::HandleNextPacket (Packet * packet)
	{
		if (!ValidatePacket (packet))
		{
			LogPrint (eLogWarning, "Streaming: Malformed packet of ", packet ? packet->len : 0, " bytes, dropped");
			DeletePacket (packet);
			return;
		}
		uint32_t sendStreamID = packet->GetSendStreamID ();
		if (sendStreamID)
		{
			std::shared_ptr<Stream> stream;
			uint16_t lastFromPort = 0, lastToPort = 0;
			{
				std::lock_guard<std::mutex> l (m_StreamsMutex);
				if (!m_LastStream || sendStreamID != m_LastStream->GetRecvStreamID ())
				{
					auto it = m_Streams.find (sendStreamID);
					m_LastStream = (it != m_Streams.end ()) ? it->second : nullptr;
				}
				stream = m_LastStream;
				lastFromPort = m_LastFromPort;
				lastToPort = m_LastToPort;
			}
			if (stream)
				stream->HandleNextPacket (packet);
			else if (packet->IsEcho () && m_Owner->IsStreamingAnswerPings ())
			{
				// ping
				LogPrint (eLogInfo, "Streaming: Ping received sSID=", sendStreamID);
				auto s = std::make_shared<Stream> (m_Owner->GetService (), shared_from_this ());
				s->SetPorts (lastFromPort, lastToPort);
				s->HandlePing (packet);
			}
			else
			{
				LogPrint (eLogInfo, "Streaming: Unknown stream sSID=", sendStreamID);
				DeletePacket (packet);
			}
		}
		else
		{
			if (packet->IsEcho ())
			{
				// pong
				LogPrint (eLogInfo, "Streaming: Pong received rSID=", packet->GetReceiveStreamID ());
				std::shared_ptr<const PongHandler> pongHandler;
				{
					std::lock_guard<std::mutex> l (m_AcceptorMutex);
					pongHandler = m_PongHandler;
				}
				// Invoke the snapshot outside the lock: the handler is replaced
				// from other threads while this one runs.
				if (pongHandler && *pongHandler)
					(*pongHandler) (packet->from ? packet->from->GetDestinationPtr () : nullptr);
				DeletePacket (packet);
				return;
			}
			if (packet->IsSYN () && !packet->GetSeqn ()) // new incoming stream
			{
				uint32_t receiveStreamID = packet->GetReceiveStreamID ();
				switch (DecideSynAdmission (receiveStreamID, packet->from))
				{
				case SynAdmission::eDuplicate:
					// Already pending: the SYNACK or its ACK is still in flight.
					// Drop the retransmission and let the normal SYNACK resend
					// re-acknowledge it, but never reset the routing path of a
					// live stream (that would throw away a working path and the
					// connection's RTT/RTO history).
					LogPrint (eLogInfo, "Streaming: Incoming streaming with rSID=", receiveStreamID, " already exists");
					DeletePacket (packet);
					return;
				case SynAdmission::eRecentlyClosed:
					// A retransmitted SYN must not resurrect a connection that is
					// already gone (Java ConnectionManager._recentlyClosed).
					RejectSyn (packet, receiveStreamID, "recently closed"); // consumes the packet
					return;
				case SynAdmission::eOverLimit:
					RejectSyn (packet, receiveStreamID, "concurrent stream limit");
					return;
				case SynAdmission::eBacklogFull:
					RejectSyn (packet, receiveStreamID, "pending incoming backlog full");
					return;
				case SynAdmission::eThrottled:
					RejectSyn (packet, receiveStreamID, "connection throttle");
					return;
				case SynAdmission::eAccept: break;
				}
				// Server-tunnel access filter (Java access/*). The decision is
				// taken on the identity the SYN announces, before the stream is
				// created, so a refused peer gets no SYNACK and never reaches
				// an acceptor. A denial is silent (`drop`) or answered with one
				// signed RESET (Java limitAction=reset).
				if (IsAdmissionPredicateSet ())
				{
					auto peer = ExtractSynPeerIdentity (packet);
					if (peer)
					{
						switch (EvaluateAdmission (*peer))
						{
						case StreamAdmission::eDrop:
							LogSynRejection ("access filter drop", receiveStreamID);
							DeletePacket (packet);
							return;
						case StreamAdmission::eReset:
							LogSynRejection ("access filter deny", receiveStreamID);
							RememberRecentlyClosed (receiveStreamID);
							RejectSynWithReset (packet, receiveStreamID); // consumes the packet
							return;
						case StreamAdmission::eAllow: break;
						}
					}
				}
				auto incomingStream = CreateNewIncomingStream (receiveStreamID);
				incomingStream->HandleNextPacket (packet); // SYN
				if (incomingStream->GetStatus () != eStreamStatusOpen)
				{
					LogPrint (eLogInfo, "Streaming: SYN did not create an open stream, rSID=", receiveStreamID);
					// Do not leave a half-open registration behind: follow-on
					// packets for this receiveStreamID must never be routed to a
					// stream that never opened.
					incomingStream->Terminate (false);
					{
						std::lock_guard<std::mutex> l (m_StreamsMutex);
						m_Streams.erase (incomingStream->GetRecvStreamID ());
						m_IncomingStreams.erase (receiveStreamID);
					}
					return;
				}
				if (!incomingStream->GetRemoteLeaseSet ())
				{
					// The stream will request the peer LeaseSet asynchronously
					// and send its SYNACK when it arrives. Do not reject a valid
					// first contact merely because this router has not cached it.
					LogPrint (
						eLogDebug, "Streaming: Incoming stream has no cached LeaseSet yet, rSID=", receiveStreamID);
				}

				// handle saved packets if any
				{
					auto it = m_SavedPackets.find (receiveStreamID);
					if (it != m_SavedPackets.end ())
					{
						LogPrint (eLogDebug, "Streaming: Processing ", it->second.size (),
							" saved packets for rSID=", receiveStreamID);
						for (auto savedPacket : it->second) incomingStream->HandleNextPacket (savedPacket);
						m_SavedPackets.erase (it);
					}
				}
				// Hand the stream to an acceptor or queue it. Admission and the
				// acceptor decision happen under one lock, so a concurrent
				// ResetAcceptor cannot orphan the stream between the check and
				// the invocation.
				if (!AddPendingIncomingStream (incomingStream))
				{
					// The backlog can only fill on this thread, so this is a
					// defensive branch: refuse the stream instead of leaving the
					// peer to retransmit against silence.
					LogSynRejection ("pending incoming backlog full", receiveStreamID);
					RememberRecentlyClosed (receiveStreamID);
					incomingStream->AbortWithReset ();
				}
			}
			else // follow on packet without SYN
			{
				uint32_t receiveStreamID = packet->GetReceiveStreamID ();
				if (IsRecentlyClosed (receiveStreamID))
				{
					// Late data for a connection that is already gone: do not
					// buffer it for a stream that will never be created.
					DeletePacket (packet);
					return;
				}
				auto it1 = m_IncomingStreams.find (receiveStreamID);
				if (it1 != m_IncomingStreams.end ())
				{
					// found
					it1->second->HandleNextPacket (packet);
					return;
				}
				// save follow on packet
				packet->from = nullptr; // session might be invalid when saved packet gets handled
				auto it = m_SavedPackets.find (receiveStreamID);
				if (it != m_SavedPackets.end ())
				{
					if (it->second.size () >= MAX_SAVED_PACKETS_PER_STREAM)
					{
						LogPrint (eLogWarning, "Streaming: Saved packet list is full for ", receiveStreamID);
						DeletePacket (packet);
					}
					else
						it->second.push_back (packet);
				}
				else
				{
					if (m_SavedPackets.size () >= MAX_SAVED_STREAM_IDS)
					{
						LogPrint (eLogWarning, "Streaming: Saved stream table is full");
						DeletePacket (packet);
						return;
					}
					m_SavedPackets.emplace (receiveStreamID, std::list<Packet *> { packet });
					auto timer = std::make_shared<boost::asio::steady_timer> (m_Owner->GetService ());
					timer->expires_after (std::chrono::seconds (PENDING_INCOMING_TIMEOUT));
					auto s = shared_from_this ();
					timer->async_wait (
						[s, timer, receiveStreamID] (const boost::system::error_code & ecode)
						{
							if (ecode != boost::asio::error::operation_aborted)
							{
								auto it = s->m_SavedPackets.find (receiveStreamID);
								if (it != s->m_SavedPackets.end ())
								{
									for (auto it1 : it->second) s->DeletePacket (it1);
									it->second.clear ();
									s->m_SavedPackets.erase (it);
								}
							}
						});
				}
			}
		}
	}

	std::shared_ptr<Stream> StreamingDestination::CreateNewOutgoingStream (
		std::shared_ptr<const i2p::data::LeaseSet> remote, int port)
	{
		auto s = std::make_shared<Stream> (m_Owner->GetService (), shared_from_this (), remote, port);
		std::lock_guard<std::mutex> l (m_StreamsMutex);
		m_Streams.emplace (s->GetRecvStreamID (), s);
		return s;
	}

	void StreamingDestination::SendPing (
		std::shared_ptr<const i2p::data::LeaseSet> remote, uint16_t fromPort, uint16_t toPort)
	{
		auto s = std::make_shared<Stream> (m_Owner->GetService (), shared_from_this (), remote, toPort);
		s->SetPorts (toPort, fromPort); // remote port, local port
		s->SendPing ();
	}

	std::shared_ptr<Stream> StreamingDestination::CreateNewIncomingStream (uint32_t receiveStreamID)
	{
		auto s = std::make_shared<Stream> (m_Owner->GetService (), shared_from_this ());
		// An incoming stream replies to the peer's source port, and reports the
		// destination port it arrived on (SAM 3.2 FROM_PORT/TO_PORT).
		s->SetPorts (m_LastFromPort, m_LastToPort);
		std::lock_guard<std::mutex> l (m_StreamsMutex);
		m_Streams.emplace (s->GetRecvStreamID (), s);
		m_IncomingStreams.emplace (receiveStreamID, s);
		return s;
	}

	void StreamingDestination::DeleteStream (std::shared_ptr<Stream> stream)
	{
		if (stream)
		{
			uint32_t recentlyClosedID = 0;
			{
				std::lock_guard<std::mutex> l (m_StreamsMutex);
				// Only a stream that was really registered counts as closed; the
				// detached stream used to refuse a SYN is never in the map, so a
				// forged SYN cannot poison the recently-closed set.
				if (m_Streams.erase (stream->GetRecvStreamID ()) && stream->IsIncoming ())
					recentlyClosedID = stream->GetSendStreamID ();
				if (stream->IsIncoming ()) m_IncomingStreams.erase (stream->GetSendStreamID ());
				if (m_LastStream == stream) m_LastStream = nullptr;
			}
			// Java ConnectionManager._recentlyClosed: a retransmitted SYN for
			// this connection must not open it again.
			if (recentlyClosedID) RememberRecentlyClosed (recentlyClosedID);
		}
		auto ts = i2p::util::GetSecondsSinceEpoch ();
		if (GetNumStreams () == 0 || ts > m_LastCleanupTime + STREAMING_DESTINATION_POOLS_CLEANUP_INTERVAL)
		{
			m_PacketsPool.CleanUp ();
			m_I2NPMsgsPool.CleanUp ();
			m_LastCleanupTime = ts;
		}
	}

	std::vector<std::shared_ptr<const Stream>> StreamingDestination::GetStreamsList ()
	{
		std::vector<std::shared_ptr<const Stream>> streams;
		std::lock_guard<std::mutex> l (m_StreamsMutex);
		for (const auto & it : m_Streams) streams.push_back (it.second);
		return streams;
	}

	bool StreamingDestination::DeleteStream (uint32_t recvStreamID)
	{
		std::lock_guard<std::mutex> lock (m_StreamsMutex);
		auto it = m_Streams.find (recvStreamID);
		if (it == m_Streams.end ()) return false;
		auto s = it->second;
		auto weak = weak_from_this ();
		boost::asio::post (m_Owner->GetService (),
			[weak, s] ()
			{
				auto self = weak.lock ();
				if (!self) return;
				s->Close (); // try to send FIN
				s->Terminate (false);
				self->DeleteStream (s);
			});
		return true;
	}

	StreamingDestination::Acceptor StreamingDestination::SetAcceptor (const Acceptor & acceptor)
	{
		Acceptor replaced;
		{
			std::lock_guard<std::mutex> lock (m_AcceptorMutex);
			replaced = std::move (m_Acceptor);
			m_Acceptor = acceptor; // we must set it immediately for IsAcceptorSet
		}
		if (acceptor)
		{
			auto self = shared_from_this ();
			boost::asio::post (m_Owner->GetService (), [self] () { self->DispatchPendingIncoming (); });
		}
		// Hand the previous registration back: the caller owns exactly what it
		// displaced and can tell whether it took over another tunnel's acceptor.
		return replaced;
	}

	void StreamingDestination::SetAdmissionPredicate (const AdmissionPredicate & predicate)
	{
		auto holder = predicate ? std::make_shared<const AdmissionPredicate> (predicate) : nullptr;
		std::lock_guard<std::mutex> lock (m_AcceptorMutex);
		m_AdmissionPredicate = std::move (holder);
	}

	void StreamingDestination::ResetAdmissionPredicate ()
	{
		std::lock_guard<std::mutex> lock (m_AcceptorMutex);
		m_AdmissionPredicate = nullptr;
	}

	bool StreamingDestination::IsAdmissionPredicateSet () const
	{
		std::lock_guard<std::mutex> lock (m_AcceptorMutex);
		return m_AdmissionPredicate != nullptr;
	}

	void StreamingDestination::ResetAcceptor ()
	{
		std::vector<Acceptor> registrations;
		Acceptor persistent;
		{
			std::lock_guard<std::mutex> lock (m_AcceptorMutex);
			persistent = std::move (m_Acceptor);
			m_Acceptor = nullptr;
			registrations.reserve (m_ArmedAcceptors.size ());
			// Newest first, the order the old lazy AcceptOnce chain unwound in.
			for (auto it = m_ArmedAcceptors.rbegin (); it != m_ArmedAcceptors.rend (); ++it)
				registrations.push_back (std::move (it->acceptor));
			m_ArmedAcceptors.clear ();
		}
		// Every armed registration is completed with nullptr, outside the lock:
		// a single reset must not strand the other waiters (a SAM socket would
		// otherwise stay in its accepting state forever).
		for (auto & acceptor : registrations)
			if (acceptor) acceptor (nullptr);
		if (persistent) persistent (nullptr);
	}

	bool StreamingDestination::IsAcceptorSet () const
	{
		std::lock_guard<std::mutex> lock (m_AcceptorMutex);
		return m_Acceptor != nullptr || !m_ArmedAcceptors.empty ();
	}

	bool StreamingDestination::RemoveAcceptor (AcceptorToken token)
	{
		std::lock_guard<std::mutex> lock (m_AcceptorMutex);
		auto it = std::find_if (m_ArmedAcceptors.begin (), m_ArmedAcceptors.end (),
			[token] (const AcceptorRegistration & registration) { return registration.id == token; });
		if (it == m_ArmedAcceptors.end ()) return false;
		m_ArmedAcceptors.erase (it);
		return true;
	}

	StreamingDestination::Acceptor StreamingDestination::TakeArmedAcceptorForTesting (AcceptorToken token)
	{
		std::lock_guard<std::mutex> lock (m_AcceptorMutex);
		auto it = std::find_if (m_ArmedAcceptors.begin (), m_ArmedAcceptors.end (),
			[token] (const AcceptorRegistration & registration) { return registration.id == token; });
		if (it == m_ArmedAcceptors.end ()) return nullptr;
		auto acceptor = std::move (it->acceptor);
		m_ArmedAcceptors.erase (it);
		return acceptor;
	}

	size_t StreamingDestination::GetNumArmedAcceptorsForTesting () const
	{
		std::lock_guard<std::mutex> lock (m_AcceptorMutex);
		return m_ArmedAcceptors.size ();
	}

	size_t StreamingDestination::GetNumPendingIncomingForTesting () const
	{
		std::lock_guard<std::mutex> lock (m_AcceptorMutex);
		return m_PendingIncomingStreams.size ();
	}

	size_t StreamingDestination::GetNumRecentlyClosedForTesting () const
	{
		std::lock_guard<std::mutex> lock (m_RecentlyClosedMutex);
		return m_RecentlyClosed.size ();
	}

	bool StreamingDestination::AddPendingIncomingStream (std::shared_ptr<Stream> stream)
	{
		if (!stream) return false;
		{
			std::lock_guard<std::mutex> lock (m_AcceptorMutex);
			if (m_PendingIncomingStreams.size () >= MAX_PENDING_INCOMING_BACKLOG) return false;
			m_PendingIncomingStreams.push_back (stream);
		}
		DispatchPendingIncoming ();
		return true;
	}

	void StreamingDestination::DispatchPendingIncoming ()
	{
		for (;;)
		{
			std::shared_ptr<Stream> stream;
			Acceptor acceptor;
			{
				// Decide and dequeue under one lock: a concurrent ResetAcceptor
				// either sees the stream in the queue or the acceptor already
				// took it, never neither.
				std::lock_guard<std::mutex> lock (m_AcceptorMutex);
				if (m_PendingIncomingStreams.empty ()) break;
				if (!m_ArmedAcceptors.empty ())
				{
					acceptor = std::move (m_ArmedAcceptors.back ().acceptor);
					m_ArmedAcceptors.pop_back ();
				}
				else if (m_Acceptor)
					acceptor = m_Acceptor;
				else
					break;
				stream = m_PendingIncomingStreams.front ();
				m_PendingIncomingStreams.pop_front ();
			}
			// Invoke outside the lock: an acceptor may re-enter the destination.
			if (acceptor && stream) acceptor (std::move (stream));
		}
		bool isEmpty;
		{
			std::lock_guard<std::mutex> lock (m_AcceptorMutex);
			isEmpty = m_PendingIncomingStreams.empty ();
		}
		// Only the owning service thread touches the timer.
		m_PendingIncomingTimer.cancel ();
		if (!isEmpty)
		{
			m_PendingIncomingTimer.expires_after (std::chrono::seconds (PENDING_INCOMING_TIMEOUT));
			m_PendingIncomingTimer.async_wait (std::bind (&StreamingDestination::HandlePendingIncomingTimer,
				shared_from_this (), std::placeholders::_1));
		}
	}

	void StreamingDestination::SetPongHandler (const PongHandler & handler)
	{
		// Publish an immutable copy: the service thread invokes the handler while
		// BOB/other clients may replace or clear it.
		auto holder = handler ? std::make_shared<const PongHandler> (handler) : nullptr;
		std::lock_guard<std::mutex> lock (m_AcceptorMutex);
		m_PongHandler = std::move (holder);
	}

	void StreamingDestination::ResetPongHandler ()
	{
		std::lock_guard<std::mutex> lock (m_AcceptorMutex);
		m_PongHandler = nullptr;
	}

	StreamingDestination::AcceptorToken StreamingDestination::AcceptOnce (const Acceptor & acceptor)
	{
		AcceptorToken token = m_NextAcceptorToken.fetch_add (1);
		if (!token) token = m_NextAcceptorToken.fetch_add (1);
		{
			// Publish the registration before returning: a concurrent SYN on the
			// service thread either sees it or the posted dispatch below feeds a
			// queued stream to it.
			std::lock_guard<std::mutex> lock (m_AcceptorMutex);
			m_ArmedAcceptors.push_back (AcceptorRegistration { token, acceptor });
		}
		auto self = shared_from_this ();
		boost::asio::post (m_Owner->GetService (), [self] () { self->DispatchPendingIncoming (); });
		return token;
	}

	StreamingDestination::AcceptorToken StreamingDestination::ArmSyncAccept (
		const std::shared_ptr<SyncAcceptState> & state)
	{
		auto weak = weak_from_this ();
		return AcceptOnce (
			[weak, state] (std::shared_ptr<Stream> stream)
			{
				// Pass a copy: the parameter must stay valid for the re-offer below
				// when the waiter already cancelled.
				if (state && state->Deliver (stream)) return;
				// The blocking caller already gave up (its timeout raced with the
				// delivery). Re-offer the stream on the service thread instead of
				// dropping it: it goes back to the next acceptor or the backlog.
				auto self = weak.lock ();
				if (!self) return;
				boost::asio::post (self->GetOwner ()->GetService (),
					[self, stream] ()
					{
						// A full backlog is the only way this can fail; answer the
						// peer with a RESET instead of dropping an established
						// connection silently.
						if (!self->AddPendingIncomingStream (stream)) stream->AbortWithReset ();
					});
			});
	}

	std::shared_ptr<Stream> StreamingDestination::AcceptStream (int timeout)
	{
		if (m_Owner && m_Owner->GetService ().get_executor ().running_in_this_thread ())
		{
			// Blocking here deadlocks: the acceptor is invoked on this very thread,
			// so the wait could never be completed.
			LogPrint (eLogError, "Streaming: AcceptStream called from the destination service thread");
			return nullptr;
		}
		auto state = std::make_shared<SyncAcceptState> ();
		auto token = ArmSyncAccept (state);
		if (state->Wait (timeout)) return state->GetStream ();
		// Timed out. Cancel first and unlink afterwards: when the cancellation
		// wins, no invocation can reach this wait any more and the armSyncAccept
		// callback re-offers any stream it may still receive. When a delivery won
		// the race the stream exists and is returned, never dropped.
		if (state->Cancel ())
		{
			RemoveAcceptor (token);
			return nullptr;
		}
		return state->GetStream ();
	}

	void StreamingDestination::HandlePendingIncomingTimer (const boost::system::error_code & ecode)
	{
		if (ecode == boost::asio::error::operation_aborted) return;
		LogPrint (eLogWarning, "Streaming: Pending incoming timeout expired");
		std::list<std::shared_ptr<Stream>> pending;
		{
			std::lock_guard<std::mutex> lock (m_AcceptorMutex);
			pending.swap (m_PendingIncomingStreams);
		}
		for (auto & it : pending) it->Close ();
	}

	void StreamingDestination::HandleDataMessagePayload (uint16_t fromPort, uint16_t toPort, const uint8_t * buf,
		size_t len, i2p::garlic::ECIESX25519AEADRatchetSession * from)
	{
		// The port fields belong to the Data message and to the stream created from
		// a SYN in this call. Everything here runs on the destination service thread.
		m_LastFromPort = fromPort;
		m_LastToPort = toPort;
		HandleDataMessagePayload (buf, len, from);
	}

	void StreamingDestination::HandleDataMessagePayload (
		const uint8_t * buf, size_t len, i2p::garlic::ECIESX25519AEADRatchetSession * from)
	{
		// unzip it
		Packet * uncompressed = NewPacket ();
		uncompressed->offset = 0;
		uncompressed->len = m_Inflator.Inflate (buf, len, uncompressed->buf, MAX_PACKET_SIZE);
		if (uncompressed->len)
		{
			uncompressed->from = from;
			HandleNextPacket (uncompressed);
		}
		else
			DeletePacket (uncompressed);
	}

	std::shared_ptr<I2NPMessage> StreamingDestination::CreateDataMessage (
		const uint8_t * payload, size_t len, uint16_t toPort, bool checksum, bool gzip)
	{
		size_t size;
		auto msg = (len <= STREAMING_MTU_RATCHETS) ? m_I2NPMsgsPool.AcquireShared () : NewI2NPMessage ();
		uint8_t * buf = msg->GetPayload ();
		buf += 4; // reserve for length
		msg->len += 4;

		if (m_Gzip || gzip)
			size = m_Deflator.Deflate (payload, len, buf, msg->maxLen - msg->len);
		else
			size = i2p::data::GzipNoCompression (payload, static_cast<uint16_t> (len), buf, msg->maxLen - msg->len);

		if (size)
		{
			htobe32buf (msg->GetPayload (), static_cast<uint32_t> (size)); // length
			htobe16buf (buf + 4, m_LocalPort); // source port
			htobe16buf (buf + 6, toPort); // destination port
			buf[9] = i2p::client::PROTOCOL_TYPE_STREAMING; // streaming protocol
			msg->len += size;
			msg->FillI2NPMessageHeader (eI2NPData, m_Owner->GetRng () (), checksum);
		}
		else
			msg = nullptr;
		return msg;
	}

	uint32_t StreamingDestination::GetRandom ()
	{
		return m_Owner ? m_Owner->GetRng () () : i2p::crypto::RandomUInt32 ();
	}
}
}
