/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: SSU2OutOfSession.
 *
 * Declares out-of-session SSU2 helpers: peer-test sessions for messages 5-7,
 * which validate the reported endpoint before replying, and Charlie-side
 * hole-punch sessions that consume relay responses. Both derive from
 * SSU2Session to share header parsing and crypto state.
 *
 */

#ifndef SSU2_OUT_OF_SESSION_H__
#define SSU2_OUT_OF_SESSION_H__

#include <vector>
#include <memory>
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

	/**
	 * Whether an out-of-session PeerTest Address block may update our external
	 * address.
	 *
	 * Java never publishes an address from msg 5 (Charlie's first contact) and
	 * only completes a test when the msg 7 source is the Charlie learned from
	 * msg 4. A single test result also must not publish a port, so this only
	 * gates the corroborated observation; the caller applies the host without
	 * a PublishSSU2Address() call.
	 */
	constexpr bool AllowPeerTestExternalAddressUpdate (uint8_t msg, bool senderIsLearnedCharlie)
	{
		return msg == 7 && senderIsLearnedCharlie;
	}

	class SSU2PeerTestSession : public SSU2Session // for PeerTest msgs 5,6,7
	{
	public:
		SSU2PeerTestSession (SSU2Server & server, uint64_t sourceConnID, uint64_t destConnID);

		uint8_t GetMsgNumReceived () const { return m_MsgNumReceived; }
		bool IsConnectedRecently () const { return m_IsConnectedRecently; }
		void SetStatusChanged () { m_IsStatusChanged = true; }
		// Charlie's endpoint as learned from msg 4. It is the endpoint msg 6 is
		// sent to, and the only source whose msg 7 may corroborate our external
		// address.
		const boost::asio::ip::udp::endpoint & GetCharlieEndpoint () const { return m_CharlieEndpoint; }
		void SetCharlieEndpoint (const boost::asio::ip::udp::endpoint & ep) { m_CharlieEndpoint = ep; }

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
		void ApplyOurEndpoint (uint8_t msg); // update our external host from a corroborated Address block

	private:
		uint8_t m_MsgNumReceived, m_NumResends;
		bool m_IsConnectedRecently, m_IsStatusChanged;
		std::vector<uint8_t> m_SignedData; // for resends
		boost::asio::steady_timer m_PeerTestResendTimer;
		boost::asio::ip::udp::endpoint m_OurEndpoint; // as seen by peer
		boost::asio::ip::udp::endpoint m_CharlieEndpoint; // learned from msg 4, msg 6 target
	};

	const int SSU2_HOLE_PUNCH_RESEND_INTERVAL = 1000; // in milliseconds
	const int SSU2_HOLE_PUNCH_RESEND_INTERVAL_VARIANCE = 500; // in milliseconds
	const int SSU2_HOLE_PUNCH_MAX_NUM_RESENDS = 3;

	/// Charlie-side hole-punch state: answer a RelayResponse by sending
	/// HolePunch packets to the introduced peer until a SessionRequest arrives,
	/// then hand the session to the normal handshake path.
	///
	/// The relay-response block is retained verbatim because every resend must
	/// carry the same signed bytes; the packet itself is always version 2
	/// (SSU2_HOLE_PUNCH_VERSION) regardless of the version negotiated later.
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
