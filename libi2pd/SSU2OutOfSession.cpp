/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: SSU2OutOfSession.
 *
 * Implements SSU2 peer-test message exchange with endpoint validation,
 * adoption of the externally observed address and bounded resends, plus
 * hole-punch construction and retransmission for relayed introductions.
 *
 */

#include <cstring>
#include "Log.h"
#include "SSU2.h"
#include "SSU2OutOfSession.h"
#include "Transports.h"
#include "Blocklist.h"

namespace i2p
{
namespace transport
{
	namespace
	{
		// Mirrors Java TransportUtil.isValidPort(): the SSU2 random port
		// range is always acceptable; outside it, privileged and well-known
		// service ports are not. Deliberately not the local port-selection
		// blacklist, which also excludes ports a Java peer may legitimately
		// bind.
		bool IsValidPeerTestPort (uint16_t port)
		{
			if (port < 1024) return false;
			if (port >= 9151 && port <= 30777) return true;
			switch (port)
			{
			case 1719:
			case 1720:
			case 1900:
			case 2049:
			case 2827:
			case 3659:
			case 4045:
			case 4444:
			case 4445:
			case 5060:
			case 5061:
			case 6000:
			case 6697:
			case 7070:
			case 8080:
			case 9001:
			case 9030:
			case 9050:
			case 9100:
			case 9150:
			case 31000:
			case 32000: return false;
			default:;
			}
			if (port >= 6665 && port <= 6669) return false; // IRC and alternates
			if (port >= 7650 && port <= 7668) return false; // standard I2P ports
			return true;
		}
	} // namespace

	bool IsValidPeerTestEndpoint (const boost::asio::ip::udp::endpoint & ep)
	{
		// A zero port and an unspecified address carry no usable return path.
		if (ep.address ().is_unspecified () || !ep.port ()) return false;
		if (!IsValidPeerTestPort (ep.port ())) return false;
		// Respects the reservedrange switch used by local/test networks.
		if (transports.IsInReservedRange (ep.address ())) return false;
		// The operator blocklist is checked before any PeerTest state or
		// cryptographic work for this endpoint.
		if (i2p::data::IsOperatorBlockedAddress (ep.address ())) return false;
		return !transports.IsBanned (ep.address ());
	}

	SSU2PeerTestSession::SSU2PeerTestSession (SSU2Server & server, uint64_t sourceConnID, uint64_t destConnID)
		: SSU2Session (server)
		, m_MsgNumReceived (0)
		, m_NumResends (0)
		, m_IsConnectedRecently (false)
		, m_IsStatusChanged (false)
		, m_PeerTestResendTimer (server.GetService ())
	{
		if (!sourceConnID) sourceConnID = ~destConnID;
		if (!destConnID) destConnID = ~sourceConnID;
		SetSourceConnID (sourceConnID);
		SetDestConnID (destConnID);
		SetState (eSSU2SessionStatePeerTest);
		SetTerminationTimeout (SSU2_PEER_TEST_EXPIRATION_TIMEOUT);
		// Java PacketBuilder2 caps PeerTest padding at the 1280-byte MTU.
		AdjustMaxPayloadSize (SSU2_MIN_PACKET_SIZE);
	}

	bool SSU2PeerTestSession::ProcessPeerTest (uint8_t * buf, size_t len)
	{
		// Order matters here: the source endpoint is validated against the
		// reserved range, blocklist and ban list first, so no state or AEAD
		// work happens for an excluded peer; then the long header is unmasked
		// and the source connection ID is checked against the one derived
		// from the test nonce before the payload is authenticated with the
		// intro key. Only an authenticated payload is parsed, and a payload
		// carrying an unknown or invalid block fails the whole test instead
		// of partially applying it. we are Alice or Charlie, msgs 5,6,7
		const auto & sender = GetRemoteEndpoint ();
		if (!IsValidPeerTestEndpoint (sender))
		{
			LogPrint (eLogWarning, "SSU2: PeerTest from invalid source ", sender);
			return false;
		}
		if (len < 56)
		{
			LogPrint (eLogWarning, "SSU2: PeerTest message too short ", len);
			return false;
		}
		Header header;
		memcpy (header.buf, buf, 16);
		header.ll[0] ^= CreateHeaderMask (i2p::context.GetSSU2IntroKey (), buf + (len - 24));
		header.ll[1] ^= CreateHeaderMask (i2p::context.GetSSU2IntroKey (), buf + (len - 12));
		if (header.h.type != eSSU2PeerTest)
		{
			LogPrint (
				eLogWarning, "SSU2: Unexpected message type ", (int)header.h.type, " instead ", (int)eSSU2PeerTest);
			return false;
		}
		// Out-of-session PeerTest is always version 2, including when the
		// in-session handshake negotiated a PQ version (Java PacketBuilder2).
		// Accept 2..4 on receive for a peer that advertises its negotiated
		// version in the long header.
		if (!IsSSU2OutOfSessionVersionAccepted (header.h.flags[0]))
		{
			LogPrint (eLogWarning, "SSU2: PeerTest has wrong version ", (int)header.h.flags[0]);
			return false;
		}
		if (header.h.flags[1] != i2p::context.GetNetID ())
		{
			LogPrint (eLogWarning, "SSU2: PeerTest has wrong network ID ", (int)header.h.flags[1]);
			return false;
		}
		uint8_t nonce[12] = { 0 };
		uint64_t headerX[2]; // sourceConnID, token
		GetServer ().ChaCha20 (buf + 16, 16, i2p::context.GetSSU2IntroKey (), nonce, (uint8_t *)headerX);
		if (headerX[0] != GetDestConnID ())
		{
			LogPrint (eLogWarning, "SSU2: PeerTest has wrong Source Connection ID");
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
			LogPrint (eLogWarning, "SSU2: PeerTest AEAD verification failed ");
			return false;
		}
		HandlePayload (payload, len - 48);
		SetIsDataReceived (false);
		if (HasInvalidPayload ())
		{
			LogPrint (eLogWarning, "SSU2: Invalid block in PeerTest payload");
			return false;
		}
		return true;
	}

	void SSU2PeerTestSession::HandleAddress (const uint8_t * buf, size_t len)
	{
		boost::asio::ip::udp::endpoint ep;
		if (!ExtractEndpoint (buf, len, ep))
		{
			LogPrint (eLogWarning, "SSU2: Can't handle address block from peer test message");
			return;
		}
		// The Address block reports our own apparent endpoint. Ignore an
		// unspecified/zero endpoint instead of letting it drive the SNAT
		// checks. Also ignore a cross-family block: GetAddress() is the
		// Charlie/Bob address for this test, and the supplied remote endpoint
		// is the packet source; either identifies the test family.
		if (ep.address ().is_unspecified () || !ep.port ())
		{
			LogPrint (eLogWarning, "SSU2: Invalid address block in peer test message");
			return;
		}
		bool expectV4;
		if (GetAddress () && GetAddress ()->IsV4 () != GetAddress ()->IsV6 ())
			expectV4 = GetAddress ()->IsV4 ();
		else if (!GetRemoteEndpoint ().address ().is_unspecified ())
			expectV4 = GetRemoteEndpoint ().address ().is_v4 ();
		else
		{
			m_OurEndpoint = ep;
			return;
		}
		if (ep.address ().is_v4 () != expectV4)
		{
			LogPrint (eLogWarning, "SSU2: Ignoring cross-family address block ", ep, " in ", expectV4 ? "IPv4" : "IPv6",
				" peer test");
			return;
		}
		m_OurEndpoint = ep;
	}

	void SSU2PeerTestSession::ApplyOurEndpoint (uint8_t msg)
	{
		// The Address block in msgs 5/7 reports our apparent endpoint. Java
		// never adopts it from msg 5 (Charlie's first contact) and only
		// completes the test when msg 7 comes from the Charlie learned in msg
		// 4, so a spoofed source cannot rewrite our external host. A single
		// test result also must not publish a port: only the host observation
		// is applied here.
		if (!AllowPeerTestExternalAddressUpdate (msg, GetRemoteEndpoint () == m_CharlieEndpoint))
		{
			LogPrint (eLogDebug, "SSU2: PeerTest msg ", (int)msg, " address observation not applied");
			return;
		}
		const auto & ep = m_OurEndpoint;
		if (ep.address ().is_unspecified () || !ep.port () || !IsValidPeerTestPort (ep.port ())) return;
		if (transports.IsInReservedRange (ep.address ())) return;
		const bool v4 = ep.address ().is_v4 ();
		i2p::context.UpdateAddress (ep.address ());
		if (ep.port () == GetServer ().GetPort (v4)) return;
		// A different external port is reported for NAT analysis, but the
		// published port and host stay unchanged until a separate test
		// supplies corroborating evidence.
		LogPrint (eLogInfo, "SSU2: PeerTest observed external port ", ep.port (), " (local ", GetServer ().GetPort (v4),
			") kept unpublished");
	}

	void SSU2PeerTestSession::HandlePeerTest (const uint8_t * buf, size_t len)
	{
		// msgs 5-7. This session type is deliberately not "established": the
		// out-of-session PeerTest exchange is authenticated by the test nonce
		// and the connection IDs derived from it, not by a Noise handshake.
		// m_MsgNumReceived is a monotonic watermark: a repeated or
		// out-of-order message number is ignored outright, which is what
		// makes the resend timers safe to cancel from any branch without
		// risking a second delivery of the same step. The timestamp check is
		// the Java +/-120 s window, applied before the nonce is compared so
		// an old capture cannot drive the NAT assessment.
		if (len < 13)
		{
			LogPrint (eLogWarning, "SSU2: PeerTest block is too short ", len);
			return;
		}
		uint8_t msg = buf[0];
		if (msg < 5 || msg > 7)
		{
			LogPrint (eLogWarning, "SSU2: PeerTest unexpected msg num ", (int)msg);
			return;
		}
		if (msg <= m_MsgNumReceived)
		{
			LogPrint (eLogDebug, "SSU2: PeerTest msg num ", msg, " received after ", m_MsgNumReceived, ". Ignored");
			return;
		}
		size_t offset = 3; // points to signed data after msg + code + flag
		if (!IsSSU2OutOfSessionVersionAccepted (buf[offset])) // ver
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
		const int64_t peerTestSkew = static_cast<int64_t> (bufbe32toh (buf + offset + 5))
			- static_cast<int64_t> (i2p::util::GetSecondsSinceEpoch ());
		if (peerTestSkew > SSU2_RELAY_MAX_SKEW || peerTestSkew < -SSU2_RELAY_MAX_SKEW)
		{
			LogPrint (eLogWarning, "SSU2: PeerTest timestamp skew ", peerTestSkew, " seconds");
			return;
		}
		uint32_t nonce = bufbe32toh (buf + offset + 1); // 1 - ver
		switch (msg) // msg
		{
		case 5: // Alice from Charlie 1
		{
			if (htobe64 (((uint64_t)nonce << 32) | nonce) == GetSourceConnID ())
			{
				m_PeerTestResendTimer.cancel (); // cancel delayed msg 6 if any
				if (GetServer ().IsForcedFirewalled (GetRemoteEndpoint ().address ().is_v4 ()))
					// we assume that msg 5 was not received if forced firewalled
					return;
				// Msg 5 is Charlie's first contact: Java never publishes an
				// external address from it. m_OurEndpoint is only recorded by
				// HandleAddress() for the msg 7 corroboration.
				m_IsConnectedRecently = GetServer ().IsConnectedRecently (GetRemoteEndpoint ());
				if (GetAddress ())
				{
					if (!m_IsConnectedRecently)
						SetRouterStatus (eRouterStatusOK);
					else if (m_IsStatusChanged && GetRouterStatus () == eRouterStatusFirewalled)
						SetRouterStatus (eRouterStatusUnknown);
					SendPeerTest (6, buf + offset, len - offset);
				}
			}
			else
			{
				LogPrint (eLogWarning, "SSU2: Peer test 5 nonce mismatch ", nonce, " connID=", GetSourceConnID ());
				return;
			}
			break;
		}
		case 6: // Charlie from Alice
		{
			// The two connection IDs are derived from the test nonce (SSU2
			// spec, Peer Test, Connection IDs). Alice sends msg 6 with our
			// Destination ID, i.e. ((nonce << 32) | nonce).
			if (htobe64 (((uint64_t)nonce << 32) | nonce) != GetDestConnID ())
			{
				LogPrint (eLogWarning, "SSU2: Peer test 6 nonce mismatch ", nonce, " connID=", GetDestConnID ());
				return;
			}
			m_PeerTestResendTimer.cancel (); // no more msg 5 resends
			if (GetAddress ())
				SendPeerTest (7, buf + offset, len - offset);
			else
				LogPrint (eLogWarning, "SSU2: Unknown address for peer test 6");
			GetServer ().AddConnectedRecently (GetRemoteEndpoint (), i2p::util::GetSecondsSinceEpoch ());
			GetServer ().RequestRemoveSession (GetConnID (), shared_from_this ());
			break;
		}
		case 7: // Alice from Charlie 2
		{
			// Our Source ID was set to ((nonce << 32) | nonce) when the test
			// session was created; msg 7 must use the same nonce.
			if (htobe64 (((uint64_t)nonce << 32) | nonce) != GetSourceConnID ())
			{
				LogPrint (eLogWarning, "SSU2: Peer test 7 nonce mismatch ", nonce, " connID=", GetSourceConnID ());
				return;
			}
			m_PeerTestResendTimer.cancel (); // no more msg 6 resends
			if (m_MsgNumReceived < 5 && m_OurEndpoint.port ()) // msg 5 was not received
			{
				if (m_OurEndpoint.address ().is_v4 ()) // ipv4
				{
					if (i2p::context.GetStatus () == eRouterStatusFirewalled)
					{
						if (m_OurEndpoint.port () != GetServer ().GetPort (true))
							i2p::context.SetError (eRouterErrorSymmetricNAT);
						else if (i2p::context.GetError () == eRouterErrorSymmetricNAT)
							i2p::context.SetError (eRouterErrorNone);
					}
				}
				else
				{
					if (i2p::context.GetStatusV6 () == eRouterStatusFirewalled)
					{
						if (m_OurEndpoint.port () != GetServer ().GetPort (false))
							i2p::context.SetErrorV6 (eRouterErrorSymmetricNAT);
						else if (i2p::context.GetErrorV6 () == eRouterErrorSymmetricNAT)
							i2p::context.SetErrorV6 (eRouterErrorNone);
					}
				}
			}
			ApplyOurEndpoint (msg);
			GetServer ().AddConnectedRecently (GetRemoteEndpoint (), i2p::util::GetSecondsSinceEpoch ());
			GetServer ().RequestRemoveSession (GetConnID (), shared_from_this ());
			break;
		}
		default: LogPrint (eLogWarning, "SSU2: PeerTest unexpected msg num ", msg); return;
		}
		m_MsgNumReceived = msg;
	}

	void SSU2PeerTestSession::SendPeerTest (uint8_t msg)
	{
		auto addr = GetAddress ();
		if (!addr) return;
		// Out-of-session PeerTest uses the 1280-byte SSU2 floor, matching
		// PacketBuilder2.getPadding(..., 1280).
		AdjustMaxPayloadSize (SSU2_MIN_PACKET_SIZE);
		Header header;
		uint8_t h[32], payload[SSU2_MAX_PACKET_SIZE];
		// fill packet
		header.h.connID = GetDestConnID (); // dest id
		RAND_bytes (header.buf + 8, 4); // random packet num
		header.h.type = eSSU2PeerTest;
		header.h.flags[0] = 2; // ver, always 2 even for post-quantum
		header.h.flags[1] = (uint8_t)i2p::context.GetNetID (); // netID
		header.h.flags[2] = 0; // flag
		memcpy (h, header.buf, 16);
		htobuf64 (h + 16, GetSourceConnID ()); // source id
		memset (h + 24, 0, 8); // header token
		// payload
		payload[0] = eSSU2BlkDateTime;
		htobe16buf (payload + 1, 4);
		htobe32buf (payload + 3, static_cast<uint32_t> ((i2p::util::GetMillisecondsSinceEpoch () + 500) / 1000));
		size_t payloadSize = 7;
		if (msg == 6 || msg == 7)
			payloadSize
				+= CreateAddressBlock (payload + payloadSize, GetMaxPayloadSize () - payloadSize, GetRemoteEndpoint ());
		payloadSize += CreatePeerTestBlock (payload + payloadSize, GetMaxPayloadSize () - payloadSize, msg,
			eSSU2PeerTestCodeAccept, nullptr, m_SignedData.data (), m_SignedData.size ());
		payloadSize += CreatePaddingBlock (payload + payloadSize, GetMaxPayloadSize () - payloadSize);
		// encrypt
		uint8_t n[12];
		CreateNonce (be32toh (header.h.packetNum), n);
		i2p::crypto::AEADChaCha20Poly1305 (payload, payloadSize, h, 32, addr->i, n, payload, payloadSize + 16, true);
		payloadSize += 16;
		header.ll[0] ^= CreateHeaderMask (addr->i, payload + (payloadSize - 24));
		header.ll[1] ^= CreateHeaderMask (addr->i, payload + (payloadSize - 12));
		memset (n, 0, 12);
		GetServer ().ChaCha20 (h + 16, 16, addr->i, n, h + 16);
		// send
		GetServer ().Send (header.buf, 16, h + 16, 16, payload, payloadSize, GetRemoteEndpoint ());
		UpdateNumSentBytes (payloadSize + 32);
	}

	void SSU2PeerTestSession::SendPeerTest (uint8_t msg, const uint8_t * signedData, size_t signedDataLen, bool delayed)
	{
		m_SignedData.assign (signedData, signedData + signedDataLen);
		if (!delayed) SendPeerTest (msg);
		// schedule resend for msgs 5 or 6
		if (msg == 5 || msg == 6) ScheduleResend (msg);
	}

	void SSU2PeerTestSession::SendPeerTest (uint8_t msg, const uint8_t * signedData, size_t signedDataLen,
		std::shared_ptr<const i2p::data::RouterInfo::Address> addr, bool delayed)
	{
		if (!addr) return;
		SetAddress (addr);
		SendPeerTest (msg, signedData, signedDataLen, delayed);
	}

	void SSU2PeerTestSession::Connect ()
	{
		LogPrint (eLogError, "SSU2: Can't connect peer test session");
	}

	bool SSU2PeerTestSession::ProcessFirstIncomingMessage (uint64_t /* connID */, uint8_t * /* buf */, size_t /* len */)
	{
		LogPrint (eLogError, "SSU2: Can't handle incoming message in peer test session");
		return false;
	}

	void SSU2PeerTestSession::ScheduleResend (uint8_t msg)
	{
		if (m_NumResends < SSU2_PEER_TEST_MAX_NUM_RESENDS)
		{
			m_PeerTestResendTimer.expires_after (std::chrono::milliseconds (
				SSU2_PEER_TEST_RESEND_INTERVAL + GetServer ().GetRng () () % SSU2_PEER_TEST_RESEND_INTERVAL_VARIANCE));
			std::weak_ptr<SSU2PeerTestSession> s (std::static_pointer_cast<SSU2PeerTestSession> (shared_from_this ()));
			m_PeerTestResendTimer.async_wait (
				[s, msg] (const boost::system::error_code & ecode)
				{
					if (ecode != boost::asio::error::operation_aborted)
					{
						auto s1 = s.lock ();
						if (s1)
						{
							if (msg > s1->m_MsgNumReceived)
							{
								s1->SendPeerTest (msg);
								s1->m_NumResends++;
								s1->ScheduleResend (msg);
							}
						}
					}
				});
		}
	}

	SSU2HolePunchSession::SSU2HolePunchSession (SSU2Server & server, uint32_t nonce,
		const boost::asio::ip::udp::endpoint & remoteEndpoint,
		std::shared_ptr<const i2p::data::RouterInfo::Address> addr)
		: SSU2Session (server)
		, // we create full incoming session
		m_NumResends (0)
		, m_HolePunchResendTimer (server.GetService ())
	{
		// we are Charlie
		uint64_t destConnID = htobe64 (((uint64_t)nonce << 32) | nonce); // dest id
		uint64_t sourceConnID = ~destConnID;
		SetSourceConnID (sourceConnID);
		SetDestConnID (destConnID);
		SetState (eSSU2SessionStateHolePunch);
		SetRemoteEndpoint (remoteEndpoint);
		SetAddress (addr);
		// A HolePunch header is always SSU2 version 2 (SSU2 spec, Hole Punch,
		// "ver :: 2"), even when Alice will follow it with a PQ
		// SessionRequest. The incoming SessionRequest selects its own version
		// in ProcessFirstIncomingMessage(), so do not pre-set a PQ version
		// here.
		SetVersion (SSU2_HOLE_PUNCH_VERSION);
		SetTerminationTimeout (SSU2_RELAY_NONCE_EXPIRATION_TIMEOUT);
		AdjustMaxPayloadSize (SSU2_MIN_PACKET_SIZE);
	}

	void SSU2HolePunchSession::SendHolePunch ()
	{
		auto addr = GetAddress ();
		if (!addr) return;
		auto & ep = GetRemoteEndpoint ();
		LogPrint (eLogDebug, "SSU2: Sending HolePunch to ", ep);
		Header header;
		uint8_t h[32], payload[SSU2_MAX_PACKET_SIZE];
		// fill packet
		header.h.connID = GetDestConnID (); // dest id
		RAND_bytes (header.buf + 8, 4); // random packet num
		header.h.type = eSSU2HolePunch;
		header.h.flags[0] = SSU2_HOLE_PUNCH_VERSION; // ver, always 2
		header.h.flags[1] = (uint8_t)i2p::context.GetNetID (); // netID
		header.h.flags[2] = 0; // flag
		memcpy (h, header.buf, 16);
		htobuf64 (h + 16, GetSourceConnID ()); // source id
		RAND_bytes (h + 24, 8); // header token, to be ignored by Alice
		// payload
		payload[0] = eSSU2BlkDateTime;
		htobe16buf (payload + 1, 4);
		htobe32buf (payload + 3, static_cast<uint32_t> ((i2p::util::GetMillisecondsSinceEpoch () + 500) / 1000));
		size_t payloadSize = 7;
		payloadSize += CreateAddressBlock (payload + payloadSize, GetMaxPayloadSize () - payloadSize, ep);
		// relay response block
		if (m_RelayResponseBlock.size () <= GetMaxPayloadSize () - payloadSize)
		{
			memcpy (payload + payloadSize, m_RelayResponseBlock.data (), m_RelayResponseBlock.size ());
			payloadSize += m_RelayResponseBlock.size ();
		}
		else
			LogPrint (eLogError, "SSU2: HolePunch relay response does not fit ", m_RelayResponseBlock.size ());
		payloadSize += CreatePaddingBlock (payload + payloadSize, GetMaxPayloadSize () - payloadSize);
		// encrypt
		uint8_t n[12];
		CreateNonce (be32toh (header.h.packetNum), n);
		i2p::crypto::AEADChaCha20Poly1305 (payload, payloadSize, h, 32, addr->i, n, payload, payloadSize + 16, true);
		payloadSize += 16;
		header.ll[0] ^= CreateHeaderMask (addr->i, payload + (payloadSize - 24));
		header.ll[1] ^= CreateHeaderMask (addr->i, payload + (payloadSize - 12));
		memset (n, 0, 12);
		GetServer ().ChaCha20 (h + 16, 16, addr->i, n, h + 16);
		// send
		GetServer ().Send (header.buf, 16, h + 16, 16, payload, payloadSize, ep);
		UpdateNumSentBytes (payloadSize + 32);
	}

	void SSU2HolePunchSession::SendHolePunch (const uint8_t * relayResponseBlock, size_t relayResponseBlockLen)
	{
		m_RelayResponseBlock.assign (relayResponseBlock, relayResponseBlock + relayResponseBlockLen);
		SendHolePunch ();
		ScheduleResend ();
	}

	void SSU2HolePunchSession::ScheduleResend ()
	{
		if (m_NumResends < SSU2_HOLE_PUNCH_MAX_NUM_RESENDS)
		{
			m_HolePunchResendTimer.expires_after (std::chrono::milliseconds (SSU2_HOLE_PUNCH_RESEND_INTERVAL
				+ GetServer ().GetRng () () % SSU2_HOLE_PUNCH_RESEND_INTERVAL_VARIANCE));
			std::weak_ptr<SSU2HolePunchSession> s (
				std::static_pointer_cast<SSU2HolePunchSession> (shared_from_this ()));
			m_HolePunchResendTimer.async_wait (
				[s] (const boost::system::error_code & ecode)
				{
					if (ecode != boost::asio::error::operation_aborted)
					{
						auto s1 = s.lock ();
						if (s1 && s1->GetState () == eSSU2SessionStateHolePunch)
						{
							s1->SendHolePunch ();
							s1->m_NumResends++;
							s1->ScheduleResend ();
						}
					}
				});
		}
	}

	bool SSU2HolePunchSession::ProcessFirstIncomingMessage (uint64_t connID, uint8_t * buf, size_t len)
	{
		m_HolePunchResendTimer.cancel ();
		return SSU2Session::ProcessFirstIncomingMessage (connID, buf, len);
	}
}
}
