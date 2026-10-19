/*
 * Copyright (c) 2026, David Uhden Collado
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Regression checks for the owning Streaming SendBuffer and its span API.
 * This does not exercise a socket or an asynchronous write.
 */

#include <cassert>
#include <cstdint>
#include <cstring>
#include <span>

#include "Streaming.h"

int main ()
{
	const uint8_t source[] = { 0x10, 0x20, 0x30, 0x40 };
	i2p::stream::SendBuffer copy (source, sizeof (source), i2p::stream::SendHandler ());
	assert (copy.GetRemainingSize () == sizeof (source));
	assert (std::memcmp (copy.GetRemaningBuffer (), source, sizeof (source)) == 0);

	// The constructor must copy, not retain a caller stack pointer.
	const uint8_t * view = copy.GetRemaningBuffer ();
	assert (view != source);
	assert (view[0] == 0x10);

	i2p::stream::SendBuffer fromSpan { std::span<const uint8_t> (source), i2p::stream::SendHandler () };
	assert (fromSpan.GetRemainingSize () == sizeof (source));
	assert (std::memcmp (fromSpan.GetRemaningBuffer (), source, sizeof (source)) == 0);

	// The writable constructor still exposes an owned, writable buffer.
	i2p::stream::SendBuffer writable (sizeof (source));
	assert (writable.buf != nullptr);
	std::memcpy (writable.buf, source, sizeof (source));
	assert (std::memcmp (writable.GetRemaningBuffer (), source, sizeof (source)) == 0);

	// A queue owns its buffers: dropping the producer's shared_ptr and reusing
	// the source must not change the bytes later copied by Get().
	i2p::stream::SendBufferQueue queue;
	auto queued = std::make_shared<i2p::stream::SendBuffer> (sizeof (source));
	std::memcpy (queued->buf, source, sizeof (source));
	queue.Add (std::move (queued));
	assert (!queued);
	uint8_t out[sizeof (source)] = { };
	assert (queue.Get (out, sizeof (out)) == sizeof (source));
	assert (std::memcmp (out, source, sizeof (source)) == 0);
	assert (queue.IsEmpty ());

	return 0;
}
