/*
 * Copyright (c) 2013-2026, The PurpleI2P Project
 * Copyright (c) 2026, David Uhden Collado
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <cassert>
#include <string.h>

#include "Base.h"

using namespace i2p::data;

int main ()
{
	const char * in = "test";
	size_t in_len = strlen (in);
	char out[16];

	/* bytes -> b64 */
	assert (ByteStreamToBase64 (NULL, 0) == "");

	assert (Base64EncodingBufferSize (2) == 4);
	assert (Base64EncodingBufferSize (4) == 8);
	assert (Base64EncodingBufferSize (6) == 8);
	assert (Base64EncodingBufferSize (7) == 12);
	assert (Base64EncodingBufferSize (9) == 12);
	assert (Base64EncodingBufferSize (10) == 16);
	assert (Base64EncodingBufferSize (12) == 16);
	assert (Base64EncodingBufferSize (13) == 20);

	const std::string out_str (ByteStreamToBase64 ((uint8_t *)in, in_len));
	assert (out_str.size () == 8);
	assert (out_str == "dGVzdA==");

	/* b64 -> bytes */
	assert (Base64ToByteStream ("", NULL, 0) == 0);
	assert (Base64ToByteStream ("", (uint8_t *)out, sizeof (out)) == 0);

	in = "dGVzdA=="; /* valid b64 */
	std::span<uint8_t> span_out (reinterpret_cast<uint8_t *> (out), sizeof (out));
	assert (Base64ToByteStream (in, span_out) == 4);
	assert (memcmp (out, "test", 4) == 0);
	/* a short span must be rejected before any write */
	assert (Base64ToByteStream (in, span_out.first (3)) == 0);

	in = "dGVzdA="; /* invalid b64 : not padded */
	assert (Base64ToByteStream (in, (uint8_t *)out, sizeof (out)) == 0);

	in = "dG/z.A=="; /* invalid b64 : char not from alphabet */
	assert (Base64ToByteStream (in, (uint8_t *)out, sizeof (out)) == 0);

	in = "===="; /* padding cannot start a quartet */
	assert (Base64ToByteStream (in, (uint8_t *)out, sizeof (out)) == 0);

	in = "A==="; /* padding must be at the end */
	assert (Base64ToByteStream (in, (uint8_t *)out, sizeof (out)) == 0);

	in = "dGVzdA"; /* missing padding is outside this wire format */
	assert (Base64ToByteStream (in, (uint8_t *)out, sizeof (out)) == 0);

	/* The reverse table used to be initialized lazily and raced on first use. */
	assert (Base64ToByteStream ("dGVzdA==", (uint8_t *)out, sizeof (out)) == 4);
	assert (memcmp (out, "test", 4) == 0);

	return 0;
}
