/*
* Copyright (c) 2024, The PurpleI2P Project
* Copyright (c) 2026, David Uhden Collado
* SPDX-License-Identifier: BSD-3-Clause
*
* This file is part of openi2pd and licensed under BSD-3-Clause
*
* See full license text in LICENSE file at top of project tree
*/

#ifndef SSU2_OUT_OF_SESSION_H__
#define SSU2_OUT_OF_SESSION_H__

#include <vector>
#include "SSU2Session.h"

namespace i2p
{
namespace transport
{
	const int SSU2_PEER_TEST_RESEND_INTERVAL = 3000; // in milliseconds
	const int SSU2_PEER_TEST_RESEND_INTERVAL_VARIANCE = 2000; // in milliseconds
	const int SSU2_PEER_TEST_MAX_NUM_RESENDS = 3;

	/**
	 * The HolePunch long header always carries SSU2 version 2, independent of
	 * the version that Alice will negotiate in the following SessionRequest
	 * (SSU2 spec, Hole Punch: "ver :: 2"). Java PacketBuilder2.buildHolePunch()
	 * hard-codes PROTOCOL_VERSION as well.
	 */
	constexpr uint8_t SSU2_HOLE_PUNCH_VERSION = 2;

	/**
	 * Whether an out-of-session Peer Test packet may be answered.
	 *
	 * Java PeerTestManager.receiveTest() rejects a source with an invalid
	 * port, a non-routable address or a blocklisted address before doing any
	 * work. The same checks apply here: a spoofed msg 5/6 source must not make
	 * us send msg 6/7 to a privileged port or a private address. Port
	 * validation uses the Java TransportUtil.isValidPort() port classes, and
	 * reserved-range/ban checks use Transports::IsInReservedRange()/IsBanned()
	 * (including the configured reserved-range switch so local test networks
	 * can opt out).
	 */
	bool IsValidPeerTestEndpoint (const boost::asio::ip::udp::endpoint & ep);

	class SSU2PeerTestSession : public SSU2Session // for PeerTest msgs 5,6,7
	{
	public:
		SSU2PeerTestSession (SSU2Server & server, uint64_t sourceConnID, uint64_t destConnID);

		uint8_t GetMsgNumReceived () const { return m_MsgNumReceived; }
		bool IsConnectedRecently () const { return m_IsConnectedRecently; }
		void SetStatusChanged () { m_IsStatusChanged = true; }

		void SendPeerTest (uint8_t msg, const uint8_t * signedData, size_t signedDataLen,
			std::shared_ptr<const i2p::data::RouterInfo::Address> addr, bool delayed = false);
		bool ProcessPeerTest (uint8_t * buf, size_t len) override;
		void Connect () override; // outgoing
		bool ProcessFirstIncomingMessage (uint64_t connID, uint8_t * buf, size_t len) override; // incoming

	protected:
		bool IsPayloadBlockAllowed (uint8_t block) const override { return IsPeerTestPayloadBlockAllowed (block); }

	private:
		void SendPeerTest (
			uint8_t msg, const uint8_t * signedData, size_t signedDataLen, bool delayed = false); // PeerTest message
		void SendPeerTest (uint8_t msg); // send or resend m_SignedData
		void HandlePeerTest (const uint8_t * buf, size_t len) override;
		void HandleAddress (const uint8_t * buf, size_t len) override;

		void ScheduleResend (uint8_t msg);
		void ApplyOurEndpoint (); // update our external host/port from an Address block

	private:
		uint8_t m_MsgNumReceived, m_NumResends;
		bool m_IsConnectedRecently, m_IsStatusChanged;
		std::vector<uint8_t> m_SignedData; // for resends
		boost::asio::steady_timer m_PeerTestResendTimer;
		boost::asio::ip::udp::endpoint m_OurEndpoint; // as seen by peer
	};

	const int SSU2_HOLE_PUNCH_RESEND_INTERVAL = 1000; // in milliseconds
	const int SSU2_HOLE_PUNCH_RESEND_INTERVAL_VARIANCE = 500; // in milliseconds
	const int SSU2_HOLE_PUNCH_MAX_NUM_RESENDS = 3;

	class SSU2HolePunchSession : public SSU2Session // Charlie
	{
	public:
		SSU2HolePunchSession (SSU2Server & server, uint32_t nonce,
			const boost::asio::ip::udp::endpoint & remoteEndpoint,
			std::shared_ptr<const i2p::data::RouterInfo::Address> addr);

		void SendHolePunch (const uint8_t * relayResponseBlock, size_t relayResponseBlockLen);

		bool ProcessFirstIncomingMessage (uint64_t connID, uint8_t * buf, size_t len) override; // SessionRequest

	private:
		void SendHolePunch ();
		void ScheduleResend ();

	private:
		int m_NumResends;
		std::vector<uint8_t> m_RelayResponseBlock;
		boost::asio::steady_timer m_HolePunchResendTimer;
	};
}
}

#endif
