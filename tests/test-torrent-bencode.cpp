/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test torrent bencode.
 *
 * Checks bencode metainfo parsing and creation bounds: piece/name validation, 64-bit length limits, file-count and depth caps, path sanitization and collisions, and bounded torrent-file reads.
 *
 */

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Torrents.h"
#include "TorrentsControl.h"

#ifndef NO_TORRENTS

using namespace i2p::torrents;

// 256 TiB does not fit in a 32-bit size_t. The constant and every length
// comparison must therefore be performed in 64 bits.
static_assert (MAX_TORRENT_LENGTH == (uint64_t { 256 } << 40), "limit must stay at 256 TiB");
static_assert (MAX_TORRENT_LENGTH > std::numeric_limits<uint32_t>::max (), "limit must exceed 32-bit size_t");

static std::string Bytes (std::string_view value)
{
	return std::to_string (value.size ()) + ":" + std::string (value);
}

static std::string Int (int64_t value)
{
	return "i" + std::to_string (value) + "e";
}

static std::string Dict (std::initializer_list<std::pair<std::string, std::string>> items)
{
	std::string result ("d");
	for (const auto & [key, value] : items)
	{
		result += Bytes (key);
		result += value;
	}
	result += "e";
	return result;
}

static std::string List (const std::vector<std::string> & items)
{
	std::string result ("l");
	for (const auto & item : items) result += item;
	result += "e";
	return result;
}

[[maybe_unused]] static std::string List (std::initializer_list<std::string> items)
{
	return List (std::vector<std::string> (items));
}

static std::string DeepList (size_t depth)
{
	return std::string (depth, 'l') + std::string (depth, 'e');
}

static std::string Pieces (size_t numPieces, char fill = 'A')
{
	return std::string (numPieces * SHA_DIGEST_LENGTH, fill);
}

static std::string SingleFileInfo (
	uint64_t length, size_t pieceLength, size_t numHashes, const std::string & name = "file.bin")
{
	return Dict ({ { "length", Int ((int64_t)length) }, { "name", Bytes (name) },
		{ "piece length", Int ((int64_t)pieceLength) }, { "pieces", Bytes (Pieces (numHashes)) } });
}

static std::string MultiFileInfo (
	const std::vector<std::pair<uint64_t, std::vector<std::string>>> & files, size_t pieceLength, size_t numHashes)
{
	std::vector<std::string> entries;
	for (const auto & [length, components] : files)
	{
		std::vector<std::string> path;
		for (const auto & component : components) path.push_back (Bytes (component));
		entries.push_back (Dict ({ { "length", Int ((int64_t)length) }, { "path", List (path) } }));
	}
	return Dict ({ { "files", List (entries) }, { "name", Bytes ("root") },
		{ "piece length", Int ((int64_t)pieceLength) }, { "pieces", Bytes (Pieces (numHashes)) } });
}

static std::string MakeTorrentFile (
	const std::string & info, const std::string & announce = "http://tracker.i2p/announce")
{
	return Dict ({ { "announce", Bytes (announce) }, { "info", info } });
}

static void ExpectMalformed (std::string_view data)
{
	Torrent torrent (data);
	assert (torrent.GetError () != eTorrentErrorNoError);
}

static void TestValidSingleFile ()
{
	auto data = MakeTorrentFile (SingleFileInfo (16384, 16384, 1));
	Torrent torrent (data);
	assert (torrent.GetError () == eTorrentErrorNoError);
	assert (torrent.GetName () == "file.bin");
	assert (torrent.GetLength () == 16384);
	assert (torrent.GetPieceLength () == 16384);
	assert (torrent.GetNumPieces () == 1);
	assert (torrent.IsSingleFile ());
	assert (torrent.GetFiles ().size () == 1);
	assert (torrent.GetFiles ().front ()->GetFullFilePath ().generic_string () == "file.bin");
	assert (torrent.GetFiles ().front ()->GetFileLength () == 16384);
}

static void TestValidMultiFile ()
{
	auto data = MakeTorrentFile (MultiFileInfo ({ { 100, { "dir", "a.txt" } }, { 200, { "b.txt" } } }, 16384, 1));
	Torrent torrent (data);
	assert (torrent.GetError () == eTorrentErrorNoError);
	assert (!torrent.IsSingleFile ());
	assert (torrent.GetLength () == 300);
	assert (torrent.GetNumPieces () == 1);
	assert (torrent.GetFiles ().size () == 2);
	auto it = torrent.GetFiles ().begin ();
	assert ((*it)->GetFullFilePath ().generic_string () == "dir/a.txt");
	assert ((*it)->GetFileLength () == 100);
	++it;
	assert ((*it)->GetFullFilePath ().generic_string () == "b.txt");
	assert ((*it)->GetFileLength () == 200);
}

static void TestTruncatedAndMalformed ()
{
	assert (Torrent (std::string_view { }).GetError () != eTorrentErrorNoError);
	ExpectMalformed ("d");
	ExpectMalformed ("d4:info");
	ExpectMalformed ("d4:infod4:name3:abce"); // missing dictionary terminator
	ExpectMalformed ("d4:infod4:name0:e"); // empty name
	ExpectMalformed ("d4:infoi1e"); // info must be a dictionary
	ExpectMalformed ("d4:infod4:name3:abc"); // truncated value
	ExpectMalformed ("d4:infod4:name03:abce"); // leading zero in string length
	ExpectMalformed ("d4:infod4:name3:abcee"); // trailing dictionary terminator mismatch
	ExpectMalformed (
		"d4:infod4:lengthi01e4:name1:a4:piece lengthi16384e6:pieces20:AAAAAAAAAAAAAAAAAAAAee"); // leading zero in integer
	ExpectMalformed (
		"d4:infod4:lengthi-1e4:name1:a4:piece lengthi16384e6:pieces20:AAAAAAAAAAAAAAAAAAAAee"); // negative length
	ExpectMalformed (
		"d4:infod4:lengthi16384e4:name1:a4:piece lengthi1e6:pieces20:AAAAAAAAAAAAAAAAAAAAee"); // piece length too small
	ExpectMalformed ("d8:announce1:a8:announce1:be"); // duplicate dictionary key

	auto valid = MakeTorrentFile (SingleFileInfo (16384, 16384, 1));
	ExpectMalformed (valid.substr (0, valid.size () - 1)); // truncated
	ExpectMalformed (valid + "junk"); // trailing data changes the parsed object
	ExpectMalformed (MakeTorrentFile (SingleFileInfo (16384, 16384, 1)) + "e");
}

static void TestDepthAndBounds ()
{
	auto info = SingleFileInfo (16384, 16384, 1);
	auto nested = MakeTorrentFile (Dict ({
		{ "announce", Bytes ("http://tracker.i2p/announce") }, { "info", info },
		{ "x", DeepList (12) } // deeper than the parser's bound of 10
	}));
	ExpectMalformed (nested);

	// Piece hash count must match the length/piece-length pair exactly.
	ExpectMalformed (MakeTorrentFile (SingleFileInfo (32768, 16384, 1)));
	ExpectMalformed (MakeTorrentFile (SingleFileInfo (16384, 16384, 2)));
	ExpectMalformed (MakeTorrentFile (Dict ({ { "length", Int (16384) }, { "name", Bytes ("file.bin") },
		{ "piece length", Int (16384) }, { "pieces", Bytes (std::string (SHA_DIGEST_LENGTH + 1, 'A')) } })));

	// A valid length that would require more than MAX_NUM_TORRENT_PIECES is rejected.
	ExpectMalformed (MakeTorrentFile (SingleFileInfo (MAX_TORRENT_LENGTH, MIN_PIECE_LENGTH, 1)));
	// The check happens in 64 bits: the limit itself is rejected, and the
	// value one past it must never wrap back into an accepted length.
	ExpectMalformed (MakeTorrentFile (SingleFileInfo (MAX_TORRENT_LENGTH + 1, MIN_PIECE_LENGTH, 1)));
	// 4 GiB is the first length that does not fit in an ILP32 size_t. It is
	// rejected either by the size_t check (32-bit) or by the piece-count/hash
	// consistency check (64-bit).
	ExpectMalformed (MakeTorrentFile (SingleFileInfo (uint64_t { 1 } << 32, MIN_PIECE_LENGTH, 1)));

	// Two individually valid file lengths must not overflow the total length bound.
	ExpectMalformed (MakeTorrentFile (
		MultiFileInfo ({ { MAX_TORRENT_LENGTH, { "a" } }, { MAX_TORRENT_LENGTH, { "b" } } }, MIN_PIECE_LENGTH, 1)));

	// File count is bounded before unbounded metadata can create files.
	std::vector<std::pair<uint64_t, std::vector<std::string>>> tooManyFiles;
	for (size_t i = 0; i <= MAX_NUM_TORRENT_FILES; i++)
		tooManyFiles.emplace_back (1, std::vector<std::string> { "f" + std::to_string (i) });
	ExpectMalformed (MakeTorrentFile (MultiFileInfo (tooManyFiles, MIN_PIECE_LENGTH, 1)));
}

static void TestPathSanitisation ()
{
	// Traversal, absolute and control-character components are rejected.
	ExpectMalformed (MakeTorrentFile (MultiFileInfo ({ { 1, { ".." } } }, 16384, 1)));
	ExpectMalformed (MakeTorrentFile (MultiFileInfo ({ { 1, { "." } } }, 16384, 1)));
	ExpectMalformed (MakeTorrentFile (MultiFileInfo ({ { 1, { "/etc/passwd" } } }, 16384, 1)));
	ExpectMalformed (MakeTorrentFile (MultiFileInfo ({ { 1, { "\\windows\\system32" } } }, 16384, 1)));
	ExpectMalformed (MakeTorrentFile (MultiFileInfo ({ { 1, { std::string ("a\0b", 3) } } }, 16384, 1)));
	ExpectMalformed (MakeTorrentFile (MultiFileInfo ({ { 1, { std::string ("a\037b", 3) } } }, 16384, 1)));
	ExpectMalformed (MakeTorrentFile (MultiFileInfo ({ { 1, { std::string (255 + 1, 'a') } } }, 16384, 1)));

	// Embedded separators are sanitised into a single safe component.
	auto separated = MakeTorrentFile (MultiFileInfo ({ { 1, { "a/b" } } }, 16384, 1));
	Torrent separatedTorrent (separated);
	assert (separatedTorrent.GetError () == eTorrentErrorNoError);
	assert (separatedTorrent.GetFiles ().front ()->GetFullFilePath ().generic_string () == "a_b");

	// Valid UTF-8 names are preserved byte for byte.
	auto utf8 = MakeTorrentFile (MultiFileInfo ({ { 1, { "caf\xc3\xa9" } } }, 16384, 1));
	Torrent utf8Torrent (utf8);
	assert (utf8Torrent.GetError () == eTorrentErrorNoError);
	assert (utf8Torrent.GetFiles ().front ()->GetFullFilePath ().generic_string () == "caf\xc3\xa9");
}

static void TestCollidingPaths ()
{
	// Exact duplicate.
	ExpectMalformed (MakeTorrentFile (MultiFileInfo ({ { 1, { "a" } }, { 1, { "a" } } }, 16384, 1)));

	// Two different encodings can sanitise to the same component.
	ExpectMalformed (MakeTorrentFile (MultiFileInfo ({ { 1, { "a/b" } }, { 1, { "a_b" } } }, 16384, 1)));

	// A file cannot also be a directory used by another file.
	ExpectMalformed (MakeTorrentFile (MultiFileInfo ({ { 1, { "a" } }, { 1, { "a", "b" } } }, 16384, 1)));
	ExpectMalformed (MakeTorrentFile (MultiFileInfo ({ { 1, { "a", "b" } }, { 1, { "a" } } }, 16384, 1)));
}

static void TestRequiredName ()
{
	// An empty or missing root name leaves no safe download path even though
	// the other metadata fields can be individually valid.
	ExpectMalformed (MakeTorrentFile (SingleFileInfo (16384, 16384, 1, "")));
	ExpectMalformed (MakeTorrentFile (
		Dict ({ { "length", Int (16384) }, { "piece length", Int (16384) }, { "pieces", Bytes (Pieces (1)) } })));
	ExpectMalformed (MakeTorrentFile (
		Dict ({ { "files",
					List (std::vector<std::string> { Dict (
						{ { "length", Int (1) }, { "path", List (std::vector<std::string> { Bytes ("a") }) } }) }) },
			{ "piece length", Int (16384) }, { "pieces", Bytes (Pieces (1)) } })));

	Torrent emptyName (MakeTorrentFile (SingleFileInfo (16384, 16384, 1, "")));
	assert (!emptyName.IsValid ());
}

static std::filesystem::path MakeTempDir ()
{
	auto base = std::filesystem::temp_directory_path ();
	std::error_code ec;
	for (unsigned i = 0; i < 1000; i++)
	{
		auto candidate = base / ("i2pd-torrent-bounded-" + std::to_string (i));
		if (std::filesystem::create_directory (candidate, ec)) return candidate;
		ec.clear ();
	}
	assert (false);
	return { };
}

static void WriteFile (const std::filesystem::path & path, std::string_view data)
{
	std::ofstream file (path, std::ofstream::binary);
	assert (file);
	file.write (data.data (), static_cast<std::streamsize> (data.size ()));
	assert (file);
}

struct TempDir
{
	std::filesystem::path path;
	~TempDir ()
	{
		std::error_code ec;
		std::filesystem::remove_all (path, ec);
	}
};

static void TestReadTorrentFileBounded ()
{
	TempDir dir;
	dir.path = MakeTempDir ();
	std::vector<uint8_t> content;
	std::string error;

	auto valid = dir.path / "valid.torrent";
	WriteFile (valid, "d4:infoe");
	assert (ReadTorrentFileBounded (valid, 1024, content, error));
	assert (std::string (content.begin (), content.end ()) == "d4:infoe");
	assert (error.empty ());

	auto wrongExtension = dir.path / "valid.txt";
	WriteFile (wrongExtension, "d4:infoe");
	assert (!ReadTorrentFileBounded (wrongExtension, 1024, content, error));
	assert (!error.empty ());

	auto empty = dir.path / "empty.torrent";
	WriteFile (empty, "");
	assert (!ReadTorrentFileBounded (empty, 1024, content, error));

	auto tooLarge = dir.path / "large.torrent";
	WriteFile (tooLarge, "12345");
	assert (!ReadTorrentFileBounded (tooLarge, 4, content, error));

	auto directory = dir.path / "directory.torrent";
	assert (std::filesystem::create_directory (directory));
	assert (!ReadTorrentFileBounded (directory, 1024, content, error));

#ifndef _WIN32
	auto symlink = dir.path / "symlink.torrent";
	std::error_code ec;
	std::filesystem::create_symlink (valid, symlink, ec);
	if (!ec) assert (!ReadTorrentFileBounded (symlink, 1024, content, error));
#endif
}

int main ()
{
	TestValidSingleFile ();
	TestValidMultiFile ();
	TestTruncatedAndMalformed ();
	TestDepthAndBounds ();
	TestPathSanitisation ();
	TestCollidingPaths ();
	TestRequiredName ();
	TestReadTorrentFileBounded ();
	return 0;
}

#else

int main ()
{
	return 0;
}

#endif
