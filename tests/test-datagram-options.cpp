/*
 * Copyright (c) 2026, David Uhden Collado
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Regression checks for the Datagram2/3 options block. Java's Datagram2.load
 * and Datagram3.load accept an OPTIONS flag with a zero-length mapping; the
 * C++ parser must not confuse that with a malformed/truncated block.
 */

#include <cassert>
#include <cstdint>
#include <cstring>

#include "Datagram.h"
#include "Gzip.h"

int main ()
{
	using namespace i2p::datagram;

	i2p::util::Mapping options;

	// Zero-length Mapping: two length bytes, no properties.
	const uint8_t empty[] = { 0x00, 0x00, 0xff };
	size_t offset = 0;
	options.CleanUp ();
	assert (ParseDatagramOptions (options, empty, sizeof (empty), offset));
	assert (offset == 2);
	assert (options.IsEmpty ());

	// A normal mapping written by Mapping::ToBuffer (length prefix included).
	i2p::util::Mapping source;
	assert (source.Insert ("a", "b"));
	uint8_t encoded[64];
	auto encodedLen = source.ToBuffer (encoded, sizeof (encoded));
	assert (encodedLen > 2);

	offset = 0;
	options.CleanUp ();
	assert (ParseDatagramOptions (options, encoded, encodedLen, offset));
	assert (offset == encodedLen);
	assert (options["a"] == "b");

	// Trailing application data is not consumed by the options parser.
	uint8_t withPayload[80];
	std::memcpy (withPayload, encoded, encodedLen);
	withPayload[encodedLen] = 0x42;
	offset = 0;
	options.CleanUp ();
	assert (ParseDatagramOptions (options, withPayload, encodedLen + 1, offset));
	assert (offset == encodedLen);

	// Truncated length prefix.
	offset = 0;
	assert (!ParseDatagramOptions (options, empty, 1, offset));
	// Declared mapping length exceeds the buffer.
	const uint8_t tooLong[] = { 0x00, 0x08, 0x01, 0x61 };
	offset = 0;
	assert (!ParseDatagramOptions (options, tooLong, sizeof (tooLong), offset));
	// Offset already at/after the end.
	offset = sizeof (empty);
	assert (!ParseDatagramOptions (options, empty, sizeof (empty), offset));

	// A valid gzip stream may produce zero uncompressed bytes. Datagram2/3
	// must deliver that payload rather than treating it as decompression
	// failure.
	// Minimal empty gzip member: header, one final empty DEFLATE block and
	// the CRC/ISIZE trailer.
	const uint8_t gzip[] = { 0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x03,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	i2p::data::GzipInflator inflator;
	uint8_t uncompressed[1] = { 0xaa };
	size_t uncompressedLen = 123;
	assert (inflator.InflateChecked (gzip, sizeof (gzip), uncompressed, sizeof (uncompressed), uncompressedLen));
	assert (uncompressedLen == 0);
	assert (uncompressed[0] == 0xaa);

	return 0;
}
