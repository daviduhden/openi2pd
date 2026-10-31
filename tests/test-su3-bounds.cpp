/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test su3 bounds.
 *
 * Feeds deliberately malformed SU3 reseed headers to the bounded
 * parser, checking magic, versions, signature type/length, signer and
 * content lengths, and rejection before allocation or decompression.
 *
 */

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "Config.h"
#include "Reseed.h"

namespace
{
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

void AppendBe16 (std::vector<uint8_t> & out, uint16_t value)
{
	out.push_back (static_cast<uint8_t> (value >> 8));
	out.push_back (static_cast<uint8_t> (value));
}

void AppendBe64 (std::vector<uint8_t> & out, uint64_t value)
{
	for (int shift = 56; shift >= 0; shift -= 8) out.push_back (static_cast<uint8_t> (value >> shift));
}

std::vector<uint8_t> HeaderPrefix ()
{
	return { 'I', '2', 'P', 's', 'u', '3' };
}
}

int main ()
{
	const auto directory = std::filesystem::temp_directory_path () / "openi2pd-su3-bounds";
	std::error_code error;
	std::filesystem::remove_all (directory, error);
	std::filesystem::create_directories (directory, error);
	assert (!error);

	std::vector<std::string> cases;

	// Empty and short files.
	cases.push_back (WriteCase (directory, "empty.su3", { }));
	cases.push_back (WriteCase (directory, "short-magic.su3", { 'I', '2', 'P', 's', 'u' }));

	// Valid magic followed by a deliberately truncated fixed header.
	{
		auto bytes = HeaderPrefix ();
		bytes.push_back (0); // unused byte
		cases.push_back (WriteCase (directory, "truncated-header.su3", bytes));
	}

	// Java validates the byte after the unused one as FILE_VERSION (0).
	{
		auto bytes = HeaderPrefix ();
		bytes.push_back (0); // unused
		bytes.push_back (1); // unsupported file version
		cases.push_back (WriteCase (directory, "bad-file-version.su3", bytes));
	}

	// Valid prefix, then a signature type/length mismatch. This must be
	// rejected before any signature-sized allocation.
	{
		auto bytes = HeaderPrefix ();
		bytes.push_back (0); // unused
		bytes.push_back (0); // file version
		AppendBe16 (bytes, 0); // DSA-SHA1
		AppendBe16 (bytes, 41); // DSA signatures are 40 bytes
		cases.push_back (WriteCase (directory, "mismatched-signature-length.su3", bytes));
	}

	// Java rejects an empty signer id and an empty content length.
	{
		auto bytes = HeaderPrefix ();
		bytes.push_back (0); // unused
		bytes.push_back (0); // file version
		AppendBe16 (bytes, 0); // signature type
		AppendBe16 (bytes, 40); // signature length
		bytes.push_back (0); // unused
		bytes.push_back (16); // version length
		bytes.push_back (0); // unused
		bytes.push_back (0); // empty signer id
		AppendBe64 (bytes, 1); // content length
		cases.push_back (WriteCase (directory, "empty-signer.su3", bytes));
	}

	// Valid prefix, then an impossible content length.  The parser must
	// not reserve or seek based on this value.
	{
		auto bytes = HeaderPrefix ();
		bytes.push_back (0); // format version
		AppendBe16 (bytes, 0); // signature type
		AppendBe16 (bytes, 0); // signature length
		bytes.push_back (0); // unused
		bytes.push_back (0); // version length
		bytes.push_back (0); // unused
		bytes.push_back (0); // signer id length
		AppendBe64 (bytes, 128ULL * 1024 * 1024 + 1); // > MAX_RESEED_CONTENT_LENGTH
		cases.push_back (WriteCase (directory, "oversized-content.su3", bytes));
	}

	// Wrong signer/file/content version paths must fail closed too.
	{
		auto bytes = HeaderPrefix ();
		bytes.push_back (0); // format version
		AppendBe16 (bytes, 0); // signature type
		AppendBe16 (bytes, 0); // signature length
		bytes.push_back (0); // unused
		bytes.push_back (0); // version length
		bytes.push_back (0); // unused
		bytes.push_back (0); // signer id length
		AppendBe64 (bytes, 0); // content length
		bytes.push_back (0); // unused
		bytes.push_back (1); // unsupported file type
		cases.push_back (WriteCase (directory, "bad-file-type.su3", bytes));
	}

	i2p::data::Reseeder reseeder;
	for (const auto & path : cases) assert (reseeder.ProcessSU3File (path.c_str ()) == 0);

	// Java SU3File always verifies. The compiled default must be
	// fail-closed when the configuration does not mention reseed.verify at
	// all.
	{
		i2p::config::Init ();
		const auto configPath = directory / "empty.conf";
		{
			std::ofstream file (configPath, std::ios::trunc);
			assert (file);
			file << "# reseed.verify intentionally omitted\n";
			file.close ();
			assert (file);
		}
		i2p::config::ParseConfig (configPath.string ());
		i2p::config::Finalize ();
		bool verify = false;
		assert (i2p::config::GetOption ("reseed.verify", verify));
		assert (verify);
	}

	std::filesystem::remove_all (directory, error);
	return 0;
}
