/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test ntcp2 session confirmed payload.
 *
 * Validates NTCP2 SessionConfirmed block ordering and lengths: RouterInfo first, optional Options, terminal Padding, and rejection of duplicates, unknown blocks and overruns.
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

void AppendRouterInfoBlock (Payload & out, size_t riSize = 3)
{
	Payload data (riSize, 0); // first byte is the RouterInfo flag
	assert (!data.empty ());
	out.push_back (i2p::transport::eNTCP2BlkRouterInfo);
	AppendBe16 (out, static_cast<uint16_t> (data.size ()));
	out.insert (out.end (), data.begin (), data.end ());
}

bool Validate (const Payload & payload, size_t & riDataSize)
{
	return i2p::transport::ValidateNTCP2SessionConfirmedPayload (payload.data (), payload.size (), riDataSize);
}

bool Validate (const Payload & payload)
{
	size_t ignored = 0;
	return Validate (payload, ignored);
}
}

int main ()
{
	static_assert (i2p::transport::NTCP2_SESSION_CONFIRMED_PART2_MAX_SIZE == 6000,
		"Java 2.13.0 InboundEstablishState.MSG3P2_MAX is 6000");

	// RouterInfo block only.
	{
		Payload payload;
		AppendRouterInfoBlock (payload, 16);
		size_t riDataSize = 0;
		assert (Validate (payload, riDataSize));
		assert (riDataSize == 16);
	}

	// RouterInfo followed by optional Options and a final Padding block.
	{
		Payload payload;
		AppendRouterInfoBlock (payload, 16);
		AppendBlock (payload, i2p::transport::eNTCP2BlkOptions, Payload (12, 0));
		AppendBlock (payload, i2p::transport::eNTCP2BlkPadding, Payload (7, 0));
		assert (Validate (payload));
	}

	// RouterInfo + Padding (Options is optional).
	{
		Payload payload;
		AppendRouterInfoBlock (payload, 16);
		AppendBlock (payload, i2p::transport::eNTCP2BlkPadding, Payload ());
		assert (Validate (payload));
	}

	// Largest RouterInfo block accepted by the common RouterInfo limit.
	{
		Payload payload;
		AppendRouterInfoBlock (payload, i2p::data::MAX_RI_BUFFER_SIZE + 1);
		assert (Validate (payload));
	}

	// Wrong first block type.
	{
		Payload payload;
		AppendBlock (payload, i2p::transport::eNTCP2BlkOptions, Payload (12, 0));
		assert (!Validate (payload));
	}

	// Empty or truncated RouterInfo block.
	{
		Payload payload;
		AppendRouterInfoBlock (payload, 1);
		payload.pop_back ();
		assert (!Validate (payload));
	}
	{
		Payload payload;
		AppendBlock (payload, i2p::transport::eNTCP2BlkRouterInfo, Payload ());
		assert (!Validate (payload));
	}

	// RouterInfo block larger than MAX_RI_BUFFER_SIZE + flag.
	{
		Payload payload;
		AppendRouterInfoBlock (payload, i2p::data::MAX_RI_BUFFER_SIZE + 2);
		assert (!Validate (payload));
	}

	// Blocks that are not allowed in the handshake.
	const uint8_t illegalBlocks[] = { static_cast<uint8_t> (i2p::transport::eNTCP2BlkDateTime),
		static_cast<uint8_t> (i2p::transport::eNTCP2BlkI2NPMessage),
		static_cast<uint8_t> (i2p::transport::eNTCP2BlkTermination), 200 };
	for (uint8_t type : illegalBlocks)
	{
		Payload payload;
		AppendRouterInfoBlock (payload, 16);
		AppendBlock (payload, type, Payload (16, 0));
		assert (!Validate (payload));
	}

	// Options after Padding.
	{
		Payload payload;
		AppendRouterInfoBlock (payload, 16);
		AppendBlock (payload, i2p::transport::eNTCP2BlkPadding, Payload ());
		AppendBlock (payload, i2p::transport::eNTCP2BlkOptions, Payload (12, 0));
		assert (!Validate (payload));
	}

	// Duplicate Options.
	{
		Payload payload;
		AppendRouterInfoBlock (payload, 16);
		AppendBlock (payload, i2p::transport::eNTCP2BlkOptions, Payload (12, 0));
		AppendBlock (payload, i2p::transport::eNTCP2BlkOptions, Payload (12, 0));
		assert (!Validate (payload));
	}

	// Duplicate Padding (the first Padding is not last).
	{
		Payload payload;
		AppendRouterInfoBlock (payload, 16);
		AppendBlock (payload, i2p::transport::eNTCP2BlkPadding, Payload ());
		AppendBlock (payload, i2p::transport::eNTCP2BlkPadding, Payload ());
		assert (!Validate (payload));
	}

	// Padding followed by arbitrary trailing bytes is not a valid block.
	{
		Payload payload;
		AppendRouterInfoBlock (payload, 16);
		AppendBlock (payload, i2p::transport::eNTCP2BlkPadding, Payload ());
		payload.push_back (0);
		assert (!Validate (payload));
	}

	// Truncated block header after RouterInfo.
	for (size_t trailing = 1; trailing < 3; ++trailing)
	{
		Payload payload;
		AppendRouterInfoBlock (payload, 16);
		payload.insert (payload.end (), trailing, 0);
		assert (!Validate (payload));
	}

	// Block length runs past the end of the frame.
	{
		Payload payload;
		AppendRouterInfoBlock (payload, 16);
		payload.push_back (i2p::transport::eNTCP2BlkOptions);
		AppendBe16 (payload, 100);
		payload.push_back (0);
		assert (!Validate (payload));
	}

	// RouterInfo block length runs past the end of the frame.
	{
		Payload payload;
		payload.push_back (i2p::transport::eNTCP2BlkRouterInfo);
		AppendBe16 (payload, 100);
		payload.push_back (0);
		assert (!Validate (payload));
	}

	// Empty payload.
	{
		Payload payload;
		assert (!Validate (payload));
	}

	return 0;
}
