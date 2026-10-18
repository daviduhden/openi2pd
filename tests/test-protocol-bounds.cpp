/*
* Copyright (c) 2026, The PurpleI2P Project
* Copyright (c) 2026, David Uhden Collado
* SPDX-License-Identifier: BSD-3-Clause
*
* This file is part of openi2pd and licensed under BSD-3-Clause
*
* See full license text in LICENSE file at top of project tree
*/

#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

#include "I2NPProtocol.h"
#include "I2PEndian.h"
#include "Identity.h"
#include "LeaseSet.h"

// Regression checks for length handling in untrusted protocol payloads.
// These only exercise pure parsers and bounded helpers; no network setup is
// required.

static void TestLeaseSetTruncatedBuffer ()
{
	// A standard (DSA) identity is 387 bytes, followed by a 256-byte
	// encryption key and a 128-byte signing key. With a 771-byte buffer the
	// "num leases" byte is just past the end. Parsing must reject this without
	// reading it.
	std::vector<uint8_t> buffer (771, 0);
	uint64_t expires = 1;
	assert (!i2p::data::LeaseSetBufferValidate (buffer.data (), buffer.size (), expires));

	// ExtractExpirationTimestamp() is reached through IsNewer(). Before the
	// off-by-one fix this read buffer[771].
	i2p::data::LeaseSet leaseSet (buffer.data (), buffer.size (), false);
	assert (!leaseSet.IsValid ());
	assert (!leaseSet.IsNewer (buffer.data (), buffer.size ()));
}

static void TestLeaseSetShortBuffers ()
{
	std::array<uint8_t, 128> buffer{};
	for (size_t len = 0; len <= buffer.size (); ++len)
	{
		uint64_t expires = 0;
		assert (!i2p::data::LeaseSetBufferValidate (buffer.data (), len, expires));
	}
}

static void TestIdentityTruncation ()
{
	// IdentityEx::FromBuffer() must reject a certificate that claims more
	// bytes than the supplied buffer.
	std::array<uint8_t, i2p::data::DEFAULT_IDENTITY_SIZE> buffer{};
	htobe16buf (buffer.data () + 385, 64); // certificate length
	i2p::data::IdentityEx identity;
	assert (identity.FromBuffer (buffer.data (), buffer.size ()) == 0);

	for (size_t len = 0; len < i2p::data::DEFAULT_IDENTITY_SIZE; ++len)
	{
		i2p::data::IdentityEx truncated;
		assert (truncated.FromBuffer (buffer.data (), len) == 0);
	}
}

static void TestI2NPMessageLength ()
{
	std::array<uint8_t, 32> buffer{};

	// Anything shorter than the size field is returned unchanged.
	for (size_t len = 0; len < i2p::I2NP_HEADER_SIZE_OFFSET + 2; ++len)
		assert (i2p::GetI2NPMessageLength (buffer.data (), len) == len);

	htobe16buf (buffer.data () + i2p::I2NP_HEADER_SIZE_OFFSET, 10);
	assert (i2p::GetI2NPMessageLength (buffer.data (), buffer.size ()) ==
		i2p::I2NP_HEADER_SIZE + 10);

	// A declared size larger than the buffer is clamped to the buffer length.
	htobe16buf (buffer.data () + i2p::I2NP_HEADER_SIZE_OFFSET, 1000);
	assert (i2p::GetI2NPMessageLength (buffer.data (), buffer.size ()) == buffer.size ());
}

int main ()
{
	TestLeaseSetTruncatedBuffer ();
	TestLeaseSetShortBuffers ();
	TestIdentityTruncation ();
	TestI2NPMessageLength ();
	return 0;
}
