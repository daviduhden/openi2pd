/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test reseed zip bounds.
 *
 * Checks the reseed ZIP reader's local-header and descriptor bounds,
 * deflate/CRC handling, proposal-147 netid URL composition and
 * rejection of oversized, truncated or inconsistent entries before
 * AddRouterInfo.
 *
 */

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "Reseed.h"

namespace
{
void AppendLe16 (std::vector<uint8_t> & out, uint16_t value)
{
	out.push_back (static_cast<uint8_t> (value));
	out.push_back (static_cast<uint8_t> (value >> 8));
}

void AppendLe32 (std::vector<uint8_t> & out, uint32_t value)
{
	for (int shift = 0; shift < 32; shift += 8) out.push_back (static_cast<uint8_t> (value >> shift));
}

std::vector<uint8_t> LocalFileHeader (uint32_t compressedSize, uint32_t uncompressedSize, uint16_t fileNameLength,
	uint16_t compressionMethod = 0, uint16_t bitFlag = 0, uint32_t crc32 = 0)
{
	std::vector<uint8_t> out;
	AppendLe32 (out, 0x04034B50); // local file header signature
	AppendLe16 (out, 20); // version needed
	AppendLe16 (out, bitFlag); // flags
	AppendLe16 (out, compressionMethod); // compression: stored or deflate
	AppendLe16 (out, 0); // modification time
	AppendLe16 (out, 0); // modification date
	AppendLe32 (out, crc32);
	AppendLe32 (out, compressedSize);
	AppendLe32 (out, uncompressedSize);
	AppendLe16 (out, fileNameLength);
	AppendLe16 (out, 0); // extra field length
	return out;
}

// A single final stored block is valid raw deflate and needs no zlib
// helper in the test. It lets the fixture reach inflate, CRC and
// AddRouterInfo without embedding precomputed compressed bytes.
std::vector<uint8_t> RawDeflateStored (const std::vector<uint8_t> & data)
{
	assert (data.size () <= 0xFFFF);
	const auto length = static_cast<uint16_t> (data.size ());
	std::vector<uint8_t> out;
	out.push_back (0x01); // BFINAL = 1, BTYPE = 00 (stored)
	AppendLe16 (out, length);
	AppendLe16 (out, static_cast<uint16_t> (~length));
	out.insert (out.end (), data.begin (), data.end ());
	return out;
}

std::string WriteCase (
	const std::filesystem::path & directory, const std::string & name, const std::vector<uint8_t> & bytes)
{
	const auto path = directory / name;
	std::ofstream file (path, std::ios::binary | std::ios::trunc);
	assert (file);
	if (!bytes.empty ())
		file.write (reinterpret_cast<const char *> (bytes.data ()), static_cast<std::streamsize> (bytes.size ()));
	file.close ();
	assert (file);
	return path.string ();
}
}

int main ()
{
	const auto directory = std::filesystem::temp_directory_path () / "openi2pd-reseed-zip-bounds";
	std::error_code error;
	std::filesystem::remove_all (directory, error);
	std::filesystem::create_directories (directory, error);
	assert (!error);

	i2p::data::Reseeder reseeder;
	std::vector<std::string> cases;

	// Both declared sizes exceed MAX_RESEED_FILE_SIZE. The parser must
	// reject them before allocating the compressed/uncompressed buffers.
	cases.push_back (WriteCase (directory, "oversized.zip", LocalFileHeader (0xFFFFFFFFu, 0xFFFFFFFFu, 0)));

	// A 255-byte name is rejected by the fixed-size name buffer.
	{
		auto bytes = LocalFileHeader (1, 1, 255);
		bytes.resize (bytes.size () + 255, 'a');
		cases.push_back (WriteCase (directory, "long-name.zip", bytes));
	}

	// The header claims 10 stored bytes but the file has none.
	{
		auto bytes = LocalFileHeader (10, 10, 0);
		cases.push_back (WriteCase (directory, "truncated-data.zip", bytes));
	}

	// A truncated header must not be accepted either.
	cases.push_back (WriteCase (directory, "truncated-header.zip", { 0x50, 0x4B, 0x03, 0x04, 20 }));

	// A stored entry reaches the uncompressed AddRouterInfo path. The
	// payload
	// is not a RouterInfo, so the parser must reject it after the buffers
	// have
	// been allocated and released.
	{
		auto bytes = LocalFileHeader (4, 4, 4);
		const uint8_t name[4] = { 't', 'e', 's', 't' };
		bytes.insert (bytes.end (), name, name + sizeof (name));
		const uint8_t payload[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
		bytes.insert (bytes.end (), payload, payload + sizeof (payload));
		cases.push_back (WriteCase (directory, "stored-payload.zip", bytes));
	}

	// A complete deflate stream with a wrong CRC exercises the
	// uncompressed buffer and the CRC failure path.
	{
		const std::vector<uint8_t> payload (32, 'A');
		const auto deflated = RawDeflateStored (payload);
		auto bytes = LocalFileHeader (static_cast<uint32_t> (deflated.size ()), static_cast<uint32_t> (payload.size ()),
			0, 8 /* deflate */, 0, 0 /* wrong CRC */);
		bytes.insert (bytes.end (), deflated.begin (), deflated.end ());
		cases.push_back (WriteCase (directory, "deflated-bad-crc.zip", bytes));
	}

	// A truncated deflate stream must not reach CRC32/AddRouterInfo and
	// must release both buffers on the error path.
	{
		const std::vector<uint8_t> payload (32, 'A');
		auto deflated = RawDeflateStored (payload);
		deflated.resize (deflated.size () - 8); // cut into the stored block
		auto bytes = LocalFileHeader (
			static_cast<uint32_t> (deflated.size ()), static_cast<uint32_t> (payload.size ()), 0, 8 /* deflate */);
		bytes.insert (bytes.end (), deflated.begin (), deflated.end ());
		cases.push_back (WriteCase (directory, "deflated-truncated.zip", bytes));
	}

	// Streaming entry: the local header has zero sizes and the real values
	// are in the trailing data descriptor. The descriptor signature is
	// consumed as part of the compressed data, as the reader has always
	// done.
	{
		const std::vector<uint8_t> payload { 'n', 'o', 't', 'a', 'r', 'o', 'u', 't', 'e', 'r' };
		auto bytes = LocalFileHeader (0, 0, 0, 0, 0x0008 /* data descriptor */);
		bytes.insert (bytes.end (), payload.begin (), payload.end ());
		AppendLe32 (bytes, 0x08074B50); // data descriptor signature
		AppendLe32 (bytes, 0); // CRC32
		AppendLe32 (bytes, static_cast<uint32_t> (payload.size ())); // compressed size
		AppendLe32 (bytes, static_cast<uint32_t> (payload.size ())); // uncompressed size
		cases.push_back (WriteCase (directory, "data-descriptor.zip", bytes));
	}

	// The descriptor sizes are attacker controlled as well. A value that
	// would wrap when the 4-byte signature is added must be rejected
	// before the buffers are allocated.
	{
		auto bytes = LocalFileHeader (0, 0, 0, 0, 0x0008 /* data descriptor */);
		bytes.push_back ('x'); // one byte before the descriptor
		AppendLe32 (bytes, 0x08074B50); // data descriptor signature
		AppendLe32 (bytes, 0); // CRC32
		AppendLe32 (bytes, 0xFFFFFFFEu); // compressed size; +4 would wrap
		AppendLe32 (bytes, 0); // uncompressed size
		cases.push_back (WriteCase (directory, "data-descriptor-oversized.zip", bytes));
	}

	for (const auto & path : cases) assert (reseeder.ProcessZIPFile (path.c_str ()) == 0);

	// Proposal 147: the router network ID is appended without destroying
	// an existing query string, and an explicit netid is never duplicated.
	assert (i2p::data::AppendReseedNetId ("https://example.invalid/i2pseeds.su3", 2)
		== "https://example.invalid/i2pseeds.su3?netid=2");
	assert (i2p::data::AppendReseedNetId ("https://example.invalid/i2pseeds.su3?x=1", 2)
		== "https://example.invalid/i2pseeds.su3?x=1&netid=2");
	assert (i2p::data::AppendReseedNetId ("https://example.invalid/i2pseeds.su3?netid=7", 2)
		== "https://example.invalid/i2pseeds.su3?netid=7");

	std::filesystem::remove_all (directory, error);
	return 0;
}
