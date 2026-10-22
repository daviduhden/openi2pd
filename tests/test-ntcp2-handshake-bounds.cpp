/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test ntcp2 handshake bounds.
 *
 * Bounds NTCP2 handshake payloads across version-dependent padding and replay caches, checking SessionConfirmed block legality, size limits and separation of inbound/outbound replay state.
 *
 */

#include <cassert>
#include <cstdint>
#include <vector>

#include "I2PEndian.h"
#include "NTCP2.h"

namespace
{
using Payload = std::vector<uint8_t>;

void AppendBe16 (Payload & out, uint16_t value)
{
	out.push_back (static_cast<uint8_t> (value >> 8));
	out.push_back (static_cast<uint8_t> (value));
}

void AppendBlock (Payload & out, uint8_t type, const Payload & data)
{
	assert (data.size () <= 0xFFFF);
	out.push_back (type);
	AppendBe16 (out, static_cast<uint16_t> (data.size ()));
	out.insert (out.end (), data.begin (), data.end ());
}

bool Validate (const Payload & payload)
{
	size_t ignored = 0;
	return i2p::transport::ValidateNTCP2SessionConfirmedPayload (payload.data (), payload.size (), ignored);
}
}

int main ()
{
	// spec/ntcp2-hybrid Maximum Padding: message 1 (SessionRequest).
	static_assert (i2p::transport::NTCP2GetMaxHandshakePadding (2, false) == 880);
	static_assert (i2p::transport::NTCP2GetMaxHandshakePadding (3, false) == 880);
	static_assert (i2p::transport::NTCP2GetMaxHandshakePadding (4, false) == 1264);
	static_assert (i2p::transport::NTCP2GetMaxHandshakePadding (5, false) == 1648);
	// Message 2 (SessionCreated).
	static_assert (i2p::transport::NTCP2GetMaxHandshakePadding (2, true) == 848);
	static_assert (i2p::transport::NTCP2GetMaxHandshakePadding (3, true) == 848);
	static_assert (i2p::transport::NTCP2GetMaxHandshakePadding (4, true) == 1136);
	static_assert (i2p::transport::NTCP2GetMaxHandshakePadding (5, true) == 1616);
	// Unknown versions are treated as non-PQ by SetVersion().
	assert (i2p::transport::NTCP2GetMaxHandshakePadding (0, false) == 880);
	assert (i2p::transport::NTCP2GetMaxHandshakePadding (6, true) == 848);
	assert (i2p::transport::NTCP2GetMaxHandshakePadding (-1, false) == 880);

	// The per-version limits must fit the shared handshake buffer. For
	// MLKEM1024 the base SessionRequest is 1648 bytes and its maximum padding
	// is also 1648, so the largest total is 3296 bytes. SessionCreated is
	// bounded by 1648 + 1616 = 3264.
	static_assert (1648 + i2p::transport::NTCP2GetMaxHandshakePadding (5, false) == 3296);
	static_assert (1648 + i2p::transport::NTCP2GetMaxHandshakePadding (5, true) <= 3296);

	// Incoming and outgoing replay detection use separate caches: the same
	// eight-byte prefix must not make Alice reject Bob's SessionCreated just
	// because it matched a previously seen SessionRequest prefix.
	{
		i2p::transport::NTCP2Server server;
		uint8_t value[16] = { 0x10, 0x21, 0x32, 0x43, 0x54, 0x65, 0x76, 0x87, 0, 0, 0, 0, 0, 0, 0, 0 };
		assert (!server.IsHandshakeReplay (value));
		assert (server.IsHandshakeReplay (value));
		assert (!server.IsHandshakeReplayOutgoing (value));
		assert (server.IsHandshakeReplayOutgoing (value));
		assert (server.IsHandshakeReplay (value));
		assert (server.IsHandshakeReplayOutgoing (value));
	}

	// SessionConfirmed part 2: the largest plaintext accepted by the 4048-byte
	// frame cap (4048 - 16-byte MAC) must still validate as an ordered
	// RouterInfo + final Padding payload.
	{
		Payload payload;
		AppendBlock (payload, i2p::transport::eNTCP2BlkRouterInfo, Payload (16, 0));
		const size_t maxPlaintext = i2p::transport::NTCP2_SESSION_CONFIRMED_PART2_MAX_SIZE - 16;
		assert (payload.size () < maxPlaintext);
		const size_t paddingData = maxPlaintext - payload.size () - 3;
		AppendBlock (payload, i2p::transport::eNTCP2BlkPadding, Payload (paddingData, 0));
		assert (payload.size () == maxPlaintext);
		assert (Validate (payload));
	}

	// Unknown block types remain illegal in the handshake even when the block
	// fits the frame exactly.
	{
		Payload payload;
		AppendBlock (payload, i2p::transport::eNTCP2BlkRouterInfo, Payload (16, 0));
		AppendBlock (payload, 200, Payload (8, 0));
		assert (!Validate (payload));
	}

	return 0;
}
