/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test tunnel endpoint fragment reassembly.
 *
 * A duplicate follow-on fragment below the next expected number must
 * never be stored as out-of-sequence data, and reassembly state must
 * survive longer than the 8 s I2NP lifetime (45 s, Java
 * FragmentHandler). Deterministic: the decrypted tunnel message is
 * crafted in memory and delivery is a no-op while the transports are
 * offline.
 *
 */

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "Crypto.h"
#include "I2PEndian.h"
#include "I2NPProtocol.h"
#include "TunnelBase.h"
#include "TunnelEndpoint.h"

namespace
{
using namespace i2p;
using namespace i2p::tunnel;

/*
 * Decrypted tunnel data message layout (TunnelGatewayBuffer):
 * payload[0..3]   unused payload[4..19]  IV payload[20..23] first four
 * bytes of SHA256 over the fragments and IV payload[24]     zero
 * terminator payload[25..]   fragments
 *
 * Every crafted message ends with one terminal single-message fragment
 * so the parser cannot run into random padding.
 */
class TunnelDataCraft
{
public:
	explicit TunnelDataCraft ()
		: m_Msg (CreateEmptyTunnelDataMsg (true))
	{
		auto payload = m_Msg->GetPayload ();
		std::memset (payload, 0, TUNNEL_DATA_MSG_SIZE);
		for (size_t i = 4; i < 20; ++i) payload[i] = static_cast<uint8_t> (0x40 + i);
		payload[24] = 0;
		m_Cursor = payload + 25;
	}

	bool HasRoomForTerminal () const { return Remaining () > 3; }

	void AddFirstFragment (
		uint32_t msgID, TunnelDeliveryType deliveryType, const uint8_t * toHash, const uint8_t * data, uint16_t size)
	{
		// The first-fragment flag shares the byte with the delivery type.
		*m_Cursor++ = static_cast<uint8_t> (0x08 | (deliveryType << 5));
		if (deliveryType == eDeliveryTypeRouter || deliveryType == eDeliveryTypeTunnel)
		{
			// Fragment routing uses the leading 32 bytes of the ident hash.
			std::memcpy (m_Cursor, toHash, 32);
			m_Cursor += 32;
		}
		WriteBe32 (msgID);
		WriteBe16 (size);
		WriteData (data, size);
	}

	void AddFollowOnFragment (uint32_t msgID, uint8_t fragmentNumber, bool last, const uint8_t * data, uint16_t size)
	{
		// Follow-on flags encode the sequence number and final-fragment bit.
		*m_Cursor++ = static_cast<uint8_t> (0x80 | ((fragmentNumber & 0x3F) << 1) | (last ? 0x01 : 0x00));
		WriteBe32 (msgID);
		WriteBe16 (size);
		WriteData (data, size);
	}

	std::shared_ptr<I2NPMessage> Finish ()
	{
		// Terminal fragment: a non-fragmented LOCAL delivery is dropped by an
		// outbound endpoint, so it only ends the parse loop.
		if (Remaining () > 3)
		{
			const size_t bodySize = Remaining () - 3;
			*m_Cursor++ = 0x00;
			WriteBe16 (static_cast<uint16_t> (bodySize));
			std::memset (m_Cursor, 0, bodySize);
			m_Cursor += bodySize;
		}

		auto payload = m_Msg->GetPayload ();
		std::memcpy (payload + TUNNEL_DATA_MSG_SIZE, payload + 4, 16); // IV for the checksum
		uint8_t hash[32];
		SHA256 (payload + 25, TUNNEL_DATA_MSG_SIZE - 25 + 16, hash);
		std::memcpy (payload + 20, hash, 4);
		return m_Msg;
	}

private:
	size_t Remaining () const { return static_cast<size_t> (m_Msg->GetPayload () + TUNNEL_DATA_MSG_SIZE - m_Cursor); }
	void WriteBe32 (uint32_t value)
	{
		uint8_t encoded[4];
		htobe32buf (encoded, value);
		std::memcpy (m_Cursor, encoded, sizeof (encoded));
		m_Cursor += sizeof (encoded);
	}
	void WriteBe16 (uint16_t value)
	{
		uint8_t encoded[2];
		htobe16buf (encoded, value);
		std::memcpy (m_Cursor, encoded, sizeof (encoded));
		m_Cursor += sizeof (encoded);
	}
	void WriteData (const uint8_t * data, uint16_t size)
	{
		if (size) std::memcpy (m_Cursor, data, size);
		m_Cursor += size;
	}

private:
	std::shared_ptr<I2NPMessage> m_Msg;
	uint8_t * m_Cursor;
};

std::vector<uint8_t> MakeI2NPBody ()
{
	// A real I2NP message so assembled messages pass the header checks;
	// the outbound endpoint sends it to a router, a no-op while offline.
	auto msg = NewI2NPShortMessage ();
	msg->FillI2NPMessageHeader (eI2NPData, 1);
	std::vector<uint8_t> body (msg->GetBuffer (), msg->GetBuffer () + msg->GetLength ());
	body.push_back (0x11);
	body.push_back (0x22);
	return body;
}

void TestStaleFollowOnFragmentIsDropped ()
{
	TunnelEndpoint endpoint (false); // outbound endpoint
	auto body = MakeI2NPBody ();
	const auto firstSize = static_cast<uint16_t> (body.size () / 2);
	const auto secondSize = static_cast<uint16_t> (body.size () - firstSize);
	uint8_t toHash[32];
	std::memset (toHash, 0x33, sizeof (toHash));

	// Open message 7, then move it to the incomplete table with another
	// one.
	{
		TunnelDataCraft craft;
		craft.AddFirstFragment (7, eDeliveryTypeRouter, toHash, body.data (), firstSize);
		craft.AddFirstFragment (9, eDeliveryTypeRouter, toHash, body.data (), firstSize);
		assert (craft.HasRoomForTerminal ());
		endpoint.HandleDecryptedTunnelDataMsg (craft.Finish ());
	}
	assert (endpoint.GetNumIncompleteMessages () >= 1);
	assert (endpoint.GetNumOutOfSequenceFragments () == 0);

	// Fragment #1 of message 7 is consumed, so #2 is next.
	{
		TunnelDataCraft craft;
		craft.AddFollowOnFragment (7, 1, false, body.data () + firstSize, secondSize);
		assert (craft.HasRoomForTerminal ());
		endpoint.HandleDecryptedTunnelDataMsg (craft.Finish ());
	}

	// A duplicate of fragment #1 is below the next expected number. It
	// must be dropped instead of being stored as out-of-sequence data.
	{
		TunnelDataCraft craft;
		craft.AddFollowOnFragment (7, 1, false, body.data () + firstSize, secondSize);
		assert (craft.HasRoomForTerminal ());
		endpoint.HandleDecryptedTunnelDataMsg (craft.Finish ());
	}
	assert (endpoint.GetNumOutOfSequenceFragments () == 0);
	assert (endpoint.GetNumIncompleteMessages () >= 1);

	// The remaining fragment still completes the message, so the stale
	// duplicate did not corrupt the reassembly state.
	{
		TunnelDataCraft craft;
		craft.AddFollowOnFragment (7, 2, true, body.data (), static_cast<uint16_t> (body.size ()));
		assert (craft.HasRoomForTerminal ());
		endpoint.HandleDecryptedTunnelDataMsg (craft.Finish ());
	}
	assert (endpoint.GetNumIncompleteMessages () == 0);
	assert (endpoint.GetNumOutOfSequenceFragments () == 0);
}

void TestOutOfSequenceExpiry ()
{
	TunnelEndpoint endpoint (false);
	uint8_t data[4] = { 1, 2, 3, 4 };
	TunnelDataCraft craft;
	craft.AddFollowOnFragment (21, 5, false, data, sizeof (data));
	assert (craft.HasRoomForTerminal ());
	const uint64_t now = i2p::util::GetMillisecondsSinceEpoch ();
	endpoint.HandleDecryptedTunnelDataMsg (craft.Finish ());
	assert (endpoint.GetNumOutOfSequenceFragments () == 1);

	// A duplicate of the same out-of-sequence fragment is rejected.
	TunnelDataCraft duplicate;
	duplicate.AddFollowOnFragment (21, 5, false, data, sizeof (data));
	endpoint.HandleDecryptedTunnelDataMsg (duplicate.Finish ());
	assert (endpoint.GetNumOutOfSequenceFragments () == 1);

	// Retained well past the 8 s I2NP lifetime, released after 45 s.
	endpoint.Cleanup (now + 30 * 1000);
	assert (endpoint.GetNumOutOfSequenceFragments () == 1);
	endpoint.Cleanup (now + 60 * 1000);
	assert (endpoint.GetNumOutOfSequenceFragments () == 0);
}

void TestIncompleteExpiry ()
{
	TunnelEndpoint endpoint (false);
	auto body = MakeI2NPBody ();
	uint8_t toHash[32];
	std::memset (toHash, 0x44, sizeof (toHash));

	TunnelDataCraft craft;
	craft.AddFirstFragment (11, eDeliveryTypeRouter, toHash, body.data (), static_cast<uint16_t> (body.size ()));
	craft.AddFirstFragment (12, eDeliveryTypeRouter, toHash, body.data (), static_cast<uint16_t> (body.size ()));
	assert (craft.HasRoomForTerminal ());
	const uint64_t now = i2p::util::GetMillisecondsSinceEpoch ();
	endpoint.HandleDecryptedTunnelDataMsg (craft.Finish ());
	assert (endpoint.GetNumIncompleteMessages () >= 1);

	endpoint.Cleanup (now + 30 * 1000);
	assert (endpoint.GetNumIncompleteMessages () >= 1);
	endpoint.Cleanup (now + 60 * 1000);
	assert (endpoint.GetNumIncompleteMessages () == 0);
}

void TestFragmentTimeoutConstant ()
{
	// Java FragmentHandler keeps an incomplete message for 45 s.
	assert (TUNNEL_FRAGMENT_REASSEMBLY_TIMEOUT == 45 * 1000);
	assert (TUNNEL_FRAGMENT_REASSEMBLY_TIMEOUT > i2p::I2NP_MESSAGE_EXPIRATION_TIMEOUT);
}

void TestTruncatedFragmentHeaders ()
{
	struct HeaderCase
	{
		uint8_t flag;
		size_t size; // includes the flag, address, optional ID and size field
	};
	constexpr std::array cases {
		HeaderCase { 0x00, 3 }, // local, unfragmented
		HeaderCase { 0x08, 7 }, // local, fragmented
		HeaderCase { 0x20, 39 }, // tunnel, unfragmented
		HeaderCase { 0x28, 43 }, // tunnel, fragmented
		HeaderCase { 0x40, 35 }, // router, unfragmented
		HeaderCase { 0x48, 39 }, // router, fragmented
		HeaderCase { 0x80, 7 } // follow-on
	};

	for (const auto & header : cases)
	{
		for (size_t present = 1; present < header.size; ++present)
		{
			// Only the IV suffix used for the checksum follows the cell. ASan
			// therefore catches an address read extending beyond that suffix.
			constexpr size_t prefix = 2 + I2NP_HEADER_SIZE;
			std::vector<uint8_t> storage (prefix + TUNNEL_DATA_MSG_SIZE + 16, 1);
			auto msg = std::make_shared<I2NPMessage> ();
			msg->buf = storage.data ();
			msg->len = prefix + TUNNEL_DATA_MSG_SIZE;
			msg->maxLen = storage.size ();
			auto payload = msg->GetPayload ();
			auto fragment = payload + TUNNEL_DATA_MSG_SIZE - present;
			fragment[-1] = 0; // padding terminator immediately before the fragment
			fragment[0] = header.flag;
			std::memset (fragment + 1, 0, present - 1);
			std::memcpy (payload + TUNNEL_DATA_MSG_SIZE, payload + 4, 16);
			uint8_t hash[32];
			SHA256 (fragment, present + 16, hash);
			std::memcpy (payload + 20, hash, 4);

			TunnelEndpoint endpoint (false);
			endpoint.HandleDecryptedTunnelDataMsg (msg);
			assert (endpoint.GetNumIncompleteMessages () == 0);
			assert (endpoint.GetNumOutOfSequenceFragments () == 0);
		}
	}
}
}

int main ()
{
	TestStaleFollowOnFragmentIsDropped ();
	TestOutOfSequenceExpiry ();
	TestIncompleteExpiry ();
	TestFragmentTimeoutConstant ();
	TestTruncatedFragmentHeaders ();
	return 0;
}
