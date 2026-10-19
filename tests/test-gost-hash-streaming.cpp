/*
 * Copyright (c) 2026, David Uhden Collado
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * GOST R 34.11-2012 incremental contexts must produce the same digest as the
 * one-shot helpers for every message length and split. The previous context
 * processed a partial block as soon as it had any data and reversed each
 * block, so it disagreed with the one-shot implementation even for lengths
 * below 64 bytes.
 */

#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

#include "Gost.h"

int main ()
{
	std::vector<uint8_t> message (600);
	for (size_t i = 0; i < message.size (); ++i) message[i] = (uint8_t)(i * 37 + 11);

	const size_t lengths[] = { 0, 1, 7, 63, 64, 65, 127, 128, 129, 200, 511, 600 };
	const size_t chunks[] = { 1, 7, 63, 64, 65 };

	for (size_t length : lengths)
	{
		uint8_t expected256[32], expected512[64];
		i2p::crypto::GOSTR3411_2012_256 (message.data (), length, expected256);
		i2p::crypto::GOSTR3411_2012_512 (message.data (), length, expected512);

		for (size_t chunk : chunks)
		{
			i2p::crypto::GOSTR3411_2012_CTX * ctx = i2p::crypto::GOSTR3411_2012_CTX_new ();
			assert (ctx);

			i2p::crypto::GOSTR3411_2012_CTX_Init (ctx, false);
			for (size_t offset = 0; offset < length; offset += chunk)
			{
				size_t n = chunk;
				if (n > length - offset) n = length - offset;
				i2p::crypto::GOSTR3411_2012_CTX_Update (message.data () + offset, n, ctx);
			}
			uint8_t actual256[32];
			i2p::crypto::GOSTR3411_2012_CTX_Finish (actual256, ctx);
			assert (!memcmp (actual256, expected256, sizeof (actual256)));

			i2p::crypto::GOSTR3411_2012_CTX_Init (ctx, true);
			for (size_t offset = 0; offset < length; offset += chunk)
			{
				size_t n = chunk;
				if (n > length - offset) n = length - offset;
				i2p::crypto::GOSTR3411_2012_CTX_Update (message.data () + offset, n, ctx);
			}
			uint8_t actual512[64];
			i2p::crypto::GOSTR3411_2012_CTX_Finish (actual512, ctx);
			assert (!memcmp (actual512, expected512, sizeof (actual512)));

			i2p::crypto::GOSTR3411_2012_CTX_free (ctx);
		}
	}
	return 0;
}
