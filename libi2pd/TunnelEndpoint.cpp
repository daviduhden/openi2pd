/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TunnelEndpoint.
 *
 * Implements decrypted tunnel message parsing, in-order and out-of-sequence
 * fragment reassembly with cleanup, delivery dispatch and buffered sends to the
 * next peer.
 *
 */

#include "I2PEndian.h"
#include <string.h>
#include "Crypto.h"
#include "Log.h"
#include "NetDb.hpp"
#include "I2NPProtocol.h"
#include "Transports.h"
#include "RouterContext.h"
#include "Timestamp.h"
#include "TunnelEndpoint.h"

namespace i2p
{
namespace tunnel
{

	void TunnelEndpoint::HandleDecryptedTunnelDataMsg (std::shared_ptr<I2NPMessage> msg)
	{
		m_NumReceivedBytes += TUNNEL_DATA_MSG_SIZE;
		m_LastReceiveTime.store (i2p::util::GetMillisecondsSinceEpoch (), std::memory_order_relaxed);

		uint8_t * decrypted = msg->GetPayload () + 20; // 4 + 16
		uint8_t * zero
			= (uint8_t *)memchr (decrypted + 4, 0, TUNNEL_DATA_ENCRYPTED_SIZE - 4); // without 4-byte checksum
		if (zero)
		{
			uint8_t * fragment = zero + 1;
			// verify checksum
			memcpy (msg->GetPayload () + TUNNEL_DATA_MSG_SIZE, msg->GetPayload () + 4, 16); // copy iv to the end
			uint8_t hash[32];
			SHA256 (fragment, TUNNEL_DATA_MSG_SIZE - (fragment - msg->GetPayload ()) + 16, hash); // payload + iv
			if (memcmp (hash, decrypted, 4))
			{
				LogPrint (eLogError, "TunnelMessage: Checksum verification failed");
				return;
			}
			// process fragments
			uint8_t * const end = decrypted + TUNNEL_DATA_ENCRYPTED_SIZE;
			while (fragment < end)
			{
				uint8_t flag = fragment[0];
				fragment++;

				bool isFollowOnFragment = flag & 0x80, isLastFragment = true;
				// The size field follows the optional delivery address and message
				// ID. Check the entire header before decoding any of those fields:
				// the checksum covers the cell's contents, not their framing.
				size_t headerSize = 2; // fragment size
				if (isFollowOnFragment)
					headerSize += 4; // message ID
				else
				{
					auto delivery = static_cast<TunnelDeliveryType> ((flag >> 5) & 0x03);
					if (delivery == eDeliveryTypeTunnel) headerSize += 4 + 32;
					else if (delivery == eDeliveryTypeRouter) headerSize += 32;
					if (flag & 0x08) headerSize += 4; // fragmented first message ID
				}
				if (static_cast<size_t> (end - fragment) < headerSize)
				{
					LogPrint (eLogError, "TunnelMessage: Truncated fragment header, message dropped");
					m_CurrentMsgID = 0;
					m_CurrentMessage.data = nullptr;
					return;
				}
				uint32_t msgID = 0;
				int fragmentNum = 0;
				if (!isFollowOnFragment)
				{
					// first fragment
					if (m_CurrentMsgID)
						AddIncompleteCurrentMessage (); // we have got a new message while previous is not complete

					m_CurrentMessage.deliveryType = (TunnelDeliveryType)((flag >> 5) & 0x03);
					switch (m_CurrentMessage.deliveryType)
					{
					case eDeliveryTypeLocal: // 0
						break;
					case eDeliveryTypeTunnel: // 1
						m_CurrentMessage.tunnelID = bufbe32toh (fragment);
						fragment += 4; // tunnelID
						m_CurrentMessage.hash = i2p::data::IdentHash (fragment);
						fragment += 32; // hash
						break;
					case eDeliveryTypeRouter: // 2
						m_CurrentMessage.hash = i2p::data::IdentHash (fragment);
						fragment += 32; // to hash
						break;
					default:;
					}

					bool isFragmented = flag & 0x08;
					if (isFragmented)
					{
						// Message ID
						msgID = bufbe32toh (fragment);
						fragment += 4;
						m_CurrentMsgID = msgID;
						isLastFragment = false;
					}
				}
				else
				{
					// follow on
					msgID = bufbe32toh (fragment); // MessageID
					fragment += 4;
					fragmentNum = (flag >> 1) & 0x3F; // 6 bits
					isLastFragment = flag & 0x01;
				}

				uint16_t size = bufbe16toh (fragment);
				fragment += 2;

				if (size > static_cast<size_t> (end - fragment))
				{
					LogPrint (eLogError, "TunnelMessage: Fragment size ", size,
						" exceeds tunnel data message, message dropped");
					m_CurrentMsgID = 0;
					m_CurrentMessage.data = nullptr;
					return;
				}

				// handle fragment
				if (isFollowOnFragment)
				{
					// existing message
					if (m_CurrentMsgID && m_CurrentMsgID == msgID && m_CurrentMessage.nextFragmentNum == fragmentNum)
						HandleCurrenMessageFollowOnFragment (fragment, size, isLastFragment); // previous
					else if (m_CurrentMsgID && m_CurrentMsgID == msgID
						&& fragmentNum < m_CurrentMessage.nextFragmentNum)
					{
						// A duplicate of a fragment this message already
						// consumed. Storing it as out-of-sequence data would
						// corrupt the reassembly, so drop it and keep the
						// current message.
						LogPrint (eLogDebug, "TunnelMessage: Stale fragment ", (int)fragmentNum, " below ",
							(int)m_CurrentMessage.nextFragmentNum, " of message ", msgID, ", dropped");
					}
					else
					{
						HandleFollowOnFragment (
							msgID, isLastFragment, static_cast<uint8_t> (fragmentNum), fragment, size); // another
						m_CurrentMsgID = 0;
						m_CurrentMessage.data = nullptr;
					}
				}
				else
				{
					// new message
					msg->offset = fragment - msg->buf;
					msg->len = msg->offset + size;
					// check message size
					if (msg->len > msg->maxLen)
					{
						LogPrint (eLogError, "TunnelMessage: Fragment is too long ", (int)size);
						m_CurrentMsgID = 0;
						m_CurrentMessage.data = nullptr;
						return;
					}
					// create new or assign I2NP message
					if (fragment + size < decrypted + TUNNEL_DATA_ENCRYPTED_SIZE)
					{
						// this is not last message. we have to copy it
						m_CurrentMessage.data = NewI2NPTunnelMessage (true);
						*(m_CurrentMessage.data) = *msg;
					}
					else
						m_CurrentMessage.data = msg;

					if (isLastFragment)
					{
						// single message
						HandleNextMessage (m_CurrentMessage);
						m_CurrentMsgID = 0;
						m_CurrentMessage.data = nullptr;
					}
					else if (msgID)
					{
						// first fragment of a new message
						m_CurrentMessage.nextFragmentNum = 1;
						m_CurrentMessage.receiveTime = i2p::util::GetMillisecondsSinceEpoch ();
						HandleOutOfSequenceFragments (msgID, m_CurrentMessage);
					}
					else
					{
						LogPrint (eLogError, "TunnelMessage: Message is fragmented, but msgID is not presented");
						m_CurrentMsgID = 0;
						m_CurrentMessage.data = nullptr;
					}
				}

				fragment += size;
			}
		}
	}

	void TunnelEndpoint::HandleFollowOnFragment (
		uint32_t msgID, bool isLastFragment, uint8_t fragmentNum, const uint8_t * fragment, size_t size)
	{
		auto it = m_IncompleteMessages.find (msgID);
		if (it != m_IncompleteMessages.end ())
		{
			auto & msg = it->second;
			if (fragmentNum == msg.nextFragmentNum)
			{
				if (ConcatFollowOnFragment (msg, fragment, size))
				{
					if (isLastFragment)
					{
						// message complete
						HandleNextMessage (msg);
						m_IncompleteMessages.erase (it);
					}
					else
					{
						msg.nextFragmentNum++;
						HandleOutOfSequenceFragments (msgID, msg);
					}
				}
				else
				{
					LogPrint (eLogError, "TunnelMessage: Fragment ", fragmentNum, " of message ", msgID,
						"exceeds max I2NP message size, message dropped");
					m_IncompleteMessages.erase (it);
				}
			}
			else
			{
				if (fragmentNum < msg.nextFragmentNum)
				{
					// Already covered by data we hold; replaying it would corrupt
					// the message. Never store it as an out-of-sequence fragment.
					LogPrint (eLogDebug, "TunnelMessage: Stale fragment ", (int)fragmentNum, " below ",
						(int)msg.nextFragmentNum, " of message ", msgID, ", dropped");
				}
				else
				{
					LogPrint (eLogWarning, "TunnelMessage: Unexpected fragment ", (int)fragmentNum, " instead ",
						(int)msg.nextFragmentNum, " of message ", msgID, ", saved");
					AddOutOfSequenceFragment (msgID, fragmentNum, isLastFragment, fragment, size);
				}
			}
		}
		else
		{
			LogPrint (eLogDebug, "TunnelMessage: First fragment of message ", msgID, " not found, saved");
			AddOutOfSequenceFragment (msgID, fragmentNum, isLastFragment, fragment, size);
		}
	}

	bool TunnelEndpoint::ConcatFollowOnFragment (
		TunnelMessageBlockEx & msg, const uint8_t * fragment, size_t size) const
	{
		if (msg.data->len + size < I2NP_MAX_MESSAGE_SIZE) // check if message is not too long
		{
			if (msg.data->len + size > msg.data->maxLen)
			{
				//	LogPrint (eLogWarning, "TunnelMessage: I2NP message size ", msg.data->maxLen, " is not enough");
				auto newMsg = NewI2NPMessage (msg.data->len + size);
				*newMsg = *(msg.data);
				msg.data = newMsg;
			}
			if (msg.data->Concat (fragment, size) < size) // concatenate fragment
			{
				LogPrint (eLogError, "TunnelMessage: I2NP buffer overflow ", msg.data->maxLen);
				return false;
			}
		}
		else
			return false;
		return true;
	}

	void TunnelEndpoint::HandleCurrenMessageFollowOnFragment (
		const uint8_t * fragment, size_t size, bool isLastFragment)
	{
		if (ConcatFollowOnFragment (m_CurrentMessage, fragment, size))
		{
			if (isLastFragment)
			{
				// message complete
				HandleNextMessage (m_CurrentMessage);
				m_CurrentMsgID = 0;
				m_CurrentMessage.data = nullptr;
			}
			else
			{
				m_CurrentMessage.nextFragmentNum++;
				HandleOutOfSequenceFragments (m_CurrentMsgID, m_CurrentMessage);
			}
		}
		else
		{
			LogPrint (eLogError, "TunnelMessage: Fragment ", m_CurrentMessage.nextFragmentNum, " of message ",
				m_CurrentMsgID, " exceeds max I2NP message size, message dropped");
			m_CurrentMsgID = 0;
			m_CurrentMessage.data = nullptr;
		}
	}

	void TunnelEndpoint::AddIncompleteCurrentMessage ()
	{
		if (m_CurrentMsgID)
		{
			constexpr size_t MAX_INCOMPLETE_MESSAGES = 128; // Java FragmentHandler
			if (m_IncompleteMessages.size () >= MAX_INCOMPLETE_MESSAGES)
			{
				LogPrint (eLogWarning, "TunnelMessage: Incomplete message table is full");
			}
			else
			{
				auto ret = m_IncompleteMessages.emplace (m_CurrentMsgID, m_CurrentMessage);
				if (!ret.second)
					LogPrint (eLogError, "TunnelMessage: Incomplete message ", m_CurrentMsgID, " already exists");
			}
			m_CurrentMessage.data = nullptr;
			m_CurrentMsgID = 0;
		}
	}

	void TunnelEndpoint::AddOutOfSequenceFragment (
		uint32_t msgID, uint8_t fragmentNum, bool isLastFragment, const uint8_t * fragment, size_t size)
	{
		constexpr size_t MAX_OUT_OF_SEQUENCE_FRAGMENTS = 4096;
		if (m_OutOfSequenceFragments.size () >= MAX_OUT_OF_SEQUENCE_FRAGMENTS)
		{
			LogPrint (eLogWarning, "TunnelMessage: Out-of-sequence fragment table is full");
			return;
		}
		if (!m_OutOfSequenceFragments
				.try_emplace ((uint64_t)msgID << 32 | fragmentNum, isLastFragment,
					i2p::util::GetMillisecondsSinceEpoch (), fragment, size)
				.second)
			LogPrint (
				eLogInfo, "TunnelMessage: Duplicate out-of-sequence fragment ", fragmentNum, " of message ", msgID);
	}

	void TunnelEndpoint::HandleOutOfSequenceFragments (uint32_t msgID, TunnelMessageBlockEx & msg)
	{
		while (ConcatNextOutOfSequenceFragment (msgID, msg))
		{
			if (!msg.nextFragmentNum) // message complete
			{
				HandleNextMessage (msg);
				if (&msg == &m_CurrentMessage)
				{
					m_CurrentMsgID = 0;
					m_CurrentMessage.data = nullptr;
				}
				else
					m_IncompleteMessages.erase (msgID);
				LogPrint (eLogDebug, "TunnelMessage: All fragments of message ", msgID, " found");
				break;
			}
		}
	}

	bool TunnelEndpoint::ConcatNextOutOfSequenceFragment (uint32_t msgID, TunnelMessageBlockEx & msg)
	{
		auto it = m_OutOfSequenceFragments.find ((uint64_t)msgID << 32 | msg.nextFragmentNum);
		if (it != m_OutOfSequenceFragments.end ())
		{
			LogPrint (eLogDebug, "TunnelMessage: Out-of-sequence fragment ", (int)msg.nextFragmentNum, " of message ",
				msgID, " found");
			size_t size = it->second.data.size ();
			if (msg.data->len + size > msg.data->maxLen)
			{
				LogPrint (eLogWarning, "TunnelMessage: Tunnel endpoint I2NP message size ", msg.data->maxLen,
					" is not enough");
				auto newMsg = NewI2NPMessage (msg.data->len + size);
				*newMsg = *(msg.data);
				msg.data = newMsg;
			}
			if (msg.data->Concat (it->second.data.data (), size) < size) // concatenate out-of-sync fragment
				LogPrint (eLogError, "TunnelMessage: Tunnel endpoint I2NP buffer overflow ", msg.data->maxLen);
			if (it->second.isLastFragment)
				// message complete
				msg.nextFragmentNum = 0;
			else
				msg.nextFragmentNum++;
			m_OutOfSequenceFragments.erase (it);
			return true;
		}
		return false;
	}

	void TunnelEndpoint::HandleNextMessage (const TunnelMessageBlock & msg)
	{
		if (msg.data->GetLength () < I2NP_HEADER_SIZE)
		{
			// GetPayloadLength subtracts the header size, a shorter message wraps it around
			LogPrint (eLogError, "TunnelMessage: Message of ", msg.data->GetLength (),
				" bytes is shorter than I2NP header, dropped");
			return;
		}
		if (!m_IsInbound && msg.data->IsExpired ())
		{
			LogPrint (eLogInfo, "TunnelMessage: Message expired");
			return;
		}
		uint8_t typeID = msg.data->GetTypeID ();
		LogPrint (
			eLogDebug, "TunnelMessage: Handle fragment of ", msg.data->GetLength (), " bytes, msg type ", (int)typeID);

		switch (msg.deliveryType)
		{
		case eDeliveryTypeLocal:
			// Java OutboundTunnelEndpoint rejects LOCAL delivery. Accepting it
			// here would make tunnel-controlled plaintext look like a directly
			// received I2NP message (token DSM reflection, DLM admission, etc.).
			if (!m_IsInbound)
			{
				LogPrint (eLogWarning, "TunnelMessage: Delivery type 'local' at outbound endpoint, dropped");
				break;
			}
			i2p::HandleI2NPMessage (msg.data);
			break;
		case eDeliveryTypeTunnel:
			if (!m_IsInbound) // outbound transit tunnel
				SendMessageTo (msg.hash, i2p::CreateTunnelGatewayMsg (msg.tunnelID, msg.data));
			else
				LogPrint (eLogError, "TunnelMessage: Delivery type 'tunnel' arrived from an inbound tunnel, dropped");
			break;
		case eDeliveryTypeRouter:
			if (!m_IsInbound) // outbound transit tunnel
				i2p::transport::transports.SendMessage (
					msg.hash, msg.data); // send right away, because most likely it's single message
			else // we shouldn't send this message. possible leakage
				LogPrint (eLogError, "TunnelMessage: Delivery type 'router' arrived from an inbound tunnel, dropped");
			break;
		default: LogPrint (eLogError, "TunnelMessage: Unknown delivery type ", (int)msg.deliveryType);
		};
	}

	void TunnelEndpoint::Cleanup ()
	{
		Cleanup (i2p::util::GetMillisecondsSinceEpoch ());
	}

	void TunnelEndpoint::Cleanup (uint64_t nowMs)
	{
		auto ts = nowMs;
		// out-of-sequence fragments
		for (auto it = m_OutOfSequenceFragments.begin (); it != m_OutOfSequenceFragments.end ();)
		{
			if (ts > it->second.receiveTime + TUNNEL_FRAGMENT_REASSEMBLY_TIMEOUT)
				it = m_OutOfSequenceFragments.erase (it);
			else
				++it;
		}
		// incomplete messages
		for (auto it = m_IncompleteMessages.begin (); it != m_IncompleteMessages.end ();)
		{
			if (ts > it->second.receiveTime + TUNNEL_FRAGMENT_REASSEMBLY_TIMEOUT)
				it = m_IncompleteMessages.erase (it);
			else
				++it;
		}
	}

	void TunnelEndpoint::SendMessageTo (const i2p::data::IdentHash & to, std::shared_ptr<i2p::I2NPMessage> msg)
	{
		if (msg)
		{
			if (!m_Sender && m_I2NPMsgs.empty ()) // first message
				m_CurrentHash = to;
			else if (m_CurrentHash != to) // new target router
			{
				FlushI2NPMsgs (); //  flush message to previous
				if (m_Sender) m_Sender->Reset (); // reset sender
				m_CurrentHash = to; // set new target router
			} // otherwise add msg to the list for current target router
			m_I2NPMsgs.push_back (msg);
		}
	}

	void TunnelEndpoint::FlushI2NPMsgs ()
	{
		if (!m_I2NPMsgs.empty ())
		{
			if (!m_Sender) m_Sender = std::make_unique<TunnelTransportSender> ();
			m_Sender->SendMessagesTo (m_CurrentHash, m_I2NPMsgs); // send and clear
		}
	}

	const i2p::data::IdentHash * TunnelEndpoint::GetCurrentHash () const
	{
		return (m_Sender || !m_I2NPMsgs.empty ()) ? &m_CurrentHash : nullptr;
	}
}
}
