/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test torrents security.
 *
 * Adversarial torrent checks: 64-bit size limits, piece-block bounds and
 * stopped-torrent rejection, peer framing, corrupt pieces and resume
 * bitfields, metadata copy avoidance, and DHT routing-table ownership.
 *
 */

#include <cassert>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Torrents.h"
#include "TorrentsControl.h"
#include "TorrentsDHT.h"

#ifndef NO_TORRENTS

using namespace i2p::torrents;

// The size constant must be computed in 64 bits even on ILP32, where the
// product would otherwise wrap to zero.
static_assert (MAX_TORRENT_LENGTH == (uint64_t { 256 } << 40), "limit must stay at 256 TiB");
static_assert (MAX_TORRENT_LENGTH > std::numeric_limits<uint32_t>::max (), "limit must exceed 32-bit size_t");

namespace
{
std::string Bytes (std::string_view value)
{
	return std::to_string (value.size ()) + ":" + std::string (value);
}

std::string Dict (std::initializer_list<std::pair<std::string, std::string>> items)
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

std::string MakeSingleFileTorrent ()
{
	std::string hashes (SHA_DIGEST_LENGTH, 'A');
	return Dict ({ { "announce", Bytes ("http://tracker.i2p/announce") },
		{ "info",
			Dict ({ { "length", "i16384e" }, { "name", Bytes ("file.bin") }, { "piece length", "i16384e" },
				{ "pieces", Bytes (hashes) } }) } });
}

std::string StringList (std::initializer_list<std::string_view> items)
{
	std::string result ("l");
	for (const auto & item : items) result += Bytes (item);
	result += "e";
	return result;
}

std::string MakeMultiFileTorrent (std::string_view name, std::initializer_list<std::string_view> pathComponents,
	size_t fileLength = 16384, size_t numPieces = 1)
{
	std::string hashes (numPieces * SHA_DIGEST_LENGTH, 'A');
	auto file
		= Dict ({ { "length", "i" + std::to_string (fileLength) + "e" }, { "path", StringList (pathComponents) } });
	return Dict ({ { "info",
		Dict ({ { "files", "l" + file + "e" }, { "name", Bytes (name) }, { "piece length", "i16384e" },
			{ "pieces", Bytes (hashes) } }) } });
}

std::filesystem::path MakeTempDir ()
{
	auto base = std::filesystem::temp_directory_path ();
	std::error_code ec;
	for (unsigned i = 0; i < 1000; i++)
	{
		auto candidate = base / ("i2pd-torrent-security-" + std::to_string (i));
		if (std::filesystem::create_directory (candidate, ec)) return candidate;
		ec.clear ();
	}
	assert (false);
	return { };
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

void TestI2PTrackerHostValidation ()
{
	assert (IsI2PTrackerHost ("tracker.i2p"));
	assert (IsI2PTrackerHost ("tracker.example.i2p"));
	assert (IsI2PTrackerHost ("TRACKER.I2P")); // host names are case-insensitive
	assert (!IsI2PTrackerHost ("tracker.example"));
	assert (!IsI2PTrackerHost ("tracker.i2p.example"));
	assert (!IsI2PTrackerHost ("tracker.i2p."));
	assert (!IsI2PTrackerHost (".i2p"));
	assert (!IsI2PTrackerHost ("tracker..i2p"));
	assert (!IsI2PTrackerHost ("tracker.i2p/path"));
	assert (!IsI2PTrackerHost ("tracker.i2p:80"));
	assert (!IsI2PTrackerHost ("user@tracker.i2p"));
	assert (!IsI2PTrackerHost (std::string ("tracker.i2p\0evil", 16)));
}

// A peer-selected offset/length must never be able to index outside the
// piece block map or wrap the bounds check.
void TestPieceBlockBounds ()
{
	uint8_t hash[SHA_DIGEST_LENGTH] = { };
	{
		Piece piece (16384, hash);
		auto [offset, len] = piece.GetNextBlockToRequest ();
		assert (offset == 0 && len == 16384);
		std::vector<uint8_t> block (len, 0x42);
		piece.BlockReceived (block.data (), block.size (), offset);
		assert (piece.IsComplete ());
	}
	{
		Piece piece (16384, hash);
		auto [offset, len] = piece.GetNextBlockToRequest ();
		assert (len == 16384);
		uint8_t block[8] = { };
		// Offset beyond the piece would wrap `offset + len` on a 32-bit
		// size_t in the old code.
		piece.BlockReceived (block, sizeof (block), std::numeric_limits<size_t>::max () - 4);
		assert (!piece.IsComplete ());
		// Length beyond the piece and an offset exactly at the end.
		piece.BlockReceived (block, len + 1, offset);
		assert (!piece.IsComplete ());
		piece.BlockReceived (block, 1, piece.GetSize ());
		assert (!piece.IsComplete ());
		// A NULL source must be ignored rather than dereferenced.
		piece.BlockReceived (nullptr, 1, 0);
		assert (!piece.IsComplete ());
		// A one-byte response for a requested 16 KiB block must not mark the
		// whole block available (which would expose uninitialised heap).
		uint8_t partial[1] = { 0x42 };
		piece.BlockReceived (partial, sizeof (partial), offset);
		assert (!piece.HasBlock (offset));
		assert (!piece.IsComplete ());
	}
}

// A block is accepted only while the torrent is running and only when the
// peer-supplied offset/length is a non-empty, in-range slice of the actual
// piece. The helper uses the piece size, not the nominal piece length, so
// the short final piece is handled without accepting bytes beyond it.
void TestCanAcceptPieceBlock ()
{
	// 20000 bytes at a 16 KiB piece length produces a 16384-byte first piece
	// and a 3616-byte final piece.
	Torrent torrent (MakeMultiFileTorrent ("block-bounds", { "file.bin" }, 20000, 2));
	assert (torrent.GetError () == eTorrentErrorNoError);
	assert (torrent.GetNumPieces () == 2);
	assert (torrent.GetPiece (0).GetSize () == 16384);
	assert (torrent.GetPiece (1).GetSize () == 3616);
	assert (!torrent.IsStopped ());

	// Running: valid full and final-piece blocks are accepted.
	assert (torrent.CanAcceptPieceBlock (0, 0, 16384));
	assert (torrent.CanAcceptPieceBlock (0, 0, 1));
	assert (torrent.CanAcceptPieceBlock (1, 0, 3616));
	assert (torrent.CanAcceptPieceBlock (1, 3615, 1));

	// Running: empty, out-of-index, cross-boundary and wrapping ranges are
	// rejected by the same helper used by HandlePieceMsg.
	assert (!torrent.CanAcceptPieceBlock (0, 0, 0));
	assert (!torrent.CanAcceptPieceBlock (2, 0, 1));
	assert (!torrent.CanAcceptPieceBlock (0, 0, 16385));
	assert (!torrent.CanAcceptPieceBlock (0, 16384, 1));
	assert (!torrent.CanAcceptPieceBlock (0, 16383, 2));
	assert (!torrent.CanAcceptPieceBlock (1, 0, 3617));
	assert (!torrent.CanAcceptPieceBlock (1, 3616, 1));
	assert (!torrent.CanAcceptPieceBlock (0, std::numeric_limits<size_t>::max () - 4, 8));

	// Stopped: even a perfectly bounded block is rejected, and resuming
	// restores the bounds-only decision.
	torrent.SetStopped (true);
	assert (torrent.IsStopped ());
	assert (!torrent.CanAcceptPieceBlock (0, 0, 16384));
	assert (!torrent.CanAcceptPieceBlock (1, 0, 3616));
	torrent.SetStopped (false);
	assert (!torrent.IsStopped ());
	assert (torrent.CanAcceptPieceBlock (0, 0, 16384));
}

// A 65535-byte payload plus its 4-byte prefix does not fit in the receive
// buffer. It must be rejected before it is stored as m_NextMsgLength, which
// would otherwise wait forever for bytes that cannot be buffered.
void TestPeerMessageFraming ()
{
	static_assert (MAX_PEER_MSG_LEN + 4 == PEER_CONNECTION_RECEIVE_BUFFER_SIZE);
	assert (IsPeerMessageLengthAllowed (0)); // keep-alive
	assert (IsPeerMessageLengthAllowed (MAX_PEER_MSG_LEN));
	assert (!IsPeerMessageLengthAllowed (MAX_PEER_MSG_LEN + 1));
	assert (!IsPeerMessageLengthAllowed (65535));
	assert (!IsPeerMessageLengthAllowed (std::numeric_limits<uint32_t>::max ()));
}

// A complete piece whose bytes do not match the metainfo hash must be turned
// back into a Missing block map, not left complete with corrupt data. Reset()
// of a complete piece has the same contract so it can be downloaded again.
void TestPieceCorruptionAndReset ()
{
	std::vector<uint8_t> good (16384, 0x42);
	uint8_t hash[SHA_DIGEST_LENGTH];
	SHA1 (good.data (), good.size (), hash);

	{
		Piece piece (16384, hash);
		auto [offset, len] = piece.GetNextBlockToRequest ();
		assert (offset == 0 && len == good.size ());
		std::vector<uint8_t> wrong (len, 0x43);
		piece.BlockReceived (wrong.data (), wrong.size (), offset);
		assert (piece.IsComplete ());
		assert (!piece.VerifyHash ());
		assert (!piece.IsVerified ());

		piece.MarkCorrupt ();
		assert (!piece.IsComplete ());
		assert (!piece.HasBlock (0));
		auto [nextOffset, nextLen] = piece.GetNextBlockToRequest ();
		assert (nextLen == good.size ()); // downloadable again
		piece.BlockReceived (good.data (), good.size (), nextOffset);
		assert (piece.IsComplete ());
		assert (piece.VerifyHash ());
		assert (piece.IsVerified ());

		// Eviction keeps completion and the verification record (the disk
		// bytes were verified); Reset() forgets both.
		piece.ReleaseData ();
		assert (piece.IsComplete ());
		assert (!piece.HasData ());
		assert (piece.IsVerified ());
	}

	{
		Piece piece (16384, hash);
		auto [offset, len] = piece.GetNextBlockToRequest ();
		piece.BlockReceived (good.data (), good.size (), offset);
		assert (piece.IsComplete ());
		assert (piece.VerifyHash ());
		piece.Reset ();
		assert (!piece.IsComplete ());
		assert (!piece.IsVerified ());
		assert (!piece.HasBlock (0));
		auto [nextOffset, nextLen] = piece.GetNextBlockToRequest ();
		assert (nextLen == good.size ());
		(void)nextOffset;
	}
}

// A resume bitfield is unverified input. Applying it may keep complete pieces,
// but it must not make AreAllPiecesVerified() true, and an individual corrupt
// piece must remain recoverable.
void TestCorruptResumeBitfield ()
{
	Torrent torrent (MakeMultiFileTorrent ("resume-corrupt", { "file.bin" }, 32768, 2));
	assert (torrent.GetError () == eTorrentErrorNoError);
	std::vector<uint8_t> allOnes (1, 0xFF);
	assert (torrent.ApplyBitfield (allOnes));
	assert (!torrent.IsComplete ()); // Torrent-level completion is separate
	assert (!torrent.AreAllPiecesVerified ());

	auto [bitfield, have] = torrent.CreateBitfield ();
	assert (have == true);
	// Two pieces occupy the two high bits; unused tail bits stay zero.
	assert (bitfield.size () == 1 && bitfield[0] == 0xC0);

	torrent.GetPiece (0).MarkCorrupt ();
	assert (!torrent.GetPiece (0).IsComplete ());
	assert (!torrent.AreAllPiecesVerified ());
	auto [repaired, repairedHave] = torrent.CreateBitfield ();
	assert (repaired.size () == 1);
	assert (repaired[0] == 0x40); // first piece is Missing again
	assert (boost::logic::indeterminate (repairedHave));
}

// GetInfo() used to return a fresh copy of up to 20 MiB per ut_metadata
// request. It must expose the stored metadata by reference.
void TestInfoIsNotCopied ()
{
	Torrent torrent (MakeSingleFileTorrent ());
	assert (torrent.GetError () == eTorrentErrorNoError);
	const auto & first = torrent.GetInfo ();
	const auto & second = torrent.GetInfo ();
	assert (&first == &second);
	assert (!first.empty ());

	// Bitfields from a peer are indexed only up to the local piece count.
	torrent.SetComplete ();
	boost::dynamic_bitset<> oversized (torrent.GetNumPieces () + 8);
	oversized.set ();
	assert (!torrent.HasIncompletePieces (oversized));
	torrent.ApplyPeerRemoteBitfield (oversized);
}

void TestRoutingTableOwnership ()
{
	NodeID ourNode { };
	RoutingTable table (ourNode);
	assert (table.GetNumBuckets () == 1);

	auto addNode = [&table] (uint8_t first, uint8_t last)
	{
		NodeID id { };
		id[0] = first;
		id[19] = last;
		i2p::data::IdentHash peer { };
		peer[0] = first;
		return table.AddNode (std::make_shared<Node> (id, peer, 0));
	};

	// Force a split of the first bucket. All nodes sort above the middle
	// ID and move into the new bucket while our own node stays below it.
	for (uint8_t i = 0; i < MAX_BUCKET_CAPACITY; i++) assert (addNode (0x80, i));
	assert (table.GetNumBuckets () == 1);
	assert (addNode (0x80, static_cast<uint8_t> (MAX_BUCKET_CAPACITY + 1)) == nullptr);
	assert (table.GetNumBuckets () == 2);
	assert (table.GetNumNodes () == MAX_BUCKET_CAPACITY);

	// Expiring every node must leave a valid, empty head bucket behind.
	table.DeleteExpiredNodes (std::numeric_limits<uint64_t>::max ());
	assert (table.GetNumNodes () == 0);
	assert (table.GetNumBuckets () >= 1);
}

void TestDHTRequestBounds ()
{
	RequestInfo info (nullptr, 1);
	info.target.fill (0);
	for (size_t i = 0; i < MAX_DHT_REQUEST_NODES + 32; i++)
	{
		NodeID id { };
		id[0] = (uint8_t)(i >> 8);
		id[1] = (uint8_t)i;
		i2p::data::IdentHash peer { };
		info.AddNode (std::make_shared<Node> (id, peer, 0));
	}
	assert (info.nodesToRequest.size () <= MAX_DHT_REQUEST_NODES);
}

void TestDHTPeerBounds ()
{
	DHTTorrent torrent;
	size_t added = 0;
	for (size_t i = 0; i < MAX_DHT_PEERS_PER_TORRENT + 32; i++)
	{
		i2p::data::IdentHash peer { };
		peer[0] = (uint8_t)(i >> 8);
		peer[1] = (uint8_t)i;
		if (torrent.AddPeer (peer)) added++;
	}
	assert (added == MAX_DHT_PEERS_PER_TORRENT);
	// Re-adding an existing peer refreshes it but does not grow the map.
	i2p::data::IdentHash peer { };
	assert (!torrent.AddPeer (peer));
}

void TestTorrentFileIO ()
{
	TempDir dir;
	dir.path = MakeTempDir ();
	// TorrentFile opens an existing ".part" with in|out; the session
	// creates it before any piece is written.
	{
		std::ofstream part (dir.path / "piece.bin.part", std::ofstream::binary);
		assert (part);
	}
	auto file = std::make_shared<TorrentFile> (dir.path / "piece.bin", 16);
	uint8_t written[16];
	for (size_t i = 0; i < sizeof (written); i++) written[i] = (uint8_t)i;
	assert (file->Save (0, written, sizeof (written)));

	uint8_t loaded[16] = { };
	assert (file->Load (0, loaded, sizeof (loaded)));
	assert (!memcmp (written, loaded, sizeof (written)));

	// Reading past the reserved length must be reported as failure instead
	// of silently succeeding with partially initialised data.
	uint8_t extra = 0;
	assert (!file->Load (16, &extra, 1));
	// Offset/length combinations must be bounded without wrapping.
	assert (!file->Load (15, loaded, 2));
	assert (!file->Save (15, written, 2));
	assert (!file->Load (std::numeric_limits<size_t>::max () - 4, &extra, 8));
	assert (!file->Save (std::numeric_limits<size_t>::max () - 4, written, 8));
	// A NULL buffer with a non-zero length is rejected rather than passed to
	// the kernel.
	assert (!file->Load (0, nullptr, 1));
	assert (!file->Save (0, nullptr, 1));

	// A truncated ".part" must make Load fail, not return uninitialised bytes.
	auto truncated = std::make_shared<TorrentFile> (dir.path / "truncated.bin", 16);
	{
		std::ofstream part (dir.path / "truncated.bin.part", std::ofstream::binary);
		part.write ("abc", 3);
	}
	memset (loaded, 0, sizeof (loaded));
	assert (!truncated->Load (0, loaded, sizeof (loaded)));

	// A valid file of the reserved length is still readable.
	{
		std::ofstream part (dir.path / "complete.bin.part", std::ofstream::binary);
		part.write ((const char *)written, sizeof (written));
	}
	auto complete = std::make_shared<TorrentFile> (dir.path / "complete.bin", 16);
	assert (complete->Load (0, loaded, sizeof (loaded)));
	assert (!memcmp (written, loaded, sizeof (written)));

	// A complete piece whose backing file cannot be loaded must be rebuilt as
	// Missing rather than silently remaining complete.
	{
		uint8_t zeroHash[SHA_DIGEST_LENGTH] = { };
		Piece missingPiece (16, zeroHash);
		missingPiece.Complete ();
		assert (missingPiece.IsComplete ());
		auto missing = std::make_shared<TorrentFile> (dir.path / "missing.bin", 16);
		PieceFileFragment fragment (missing, 0, 0, 16);
		assert (!missingPiece.Load (std::move (fragment)));
		missingPiece.MarkCorrupt ();
		assert (!missingPiece.IsComplete ());
		assert (!missingPiece.HasBlock (0));
	}
}

// Path components from metainfo must never escape the torrent directory. The
// parser rejects "." / "..", absolute components and NUL before a path is
// constructed.
void TestUnsafeTorrentPaths ()
{
	Torrent traversal (MakeMultiFileTorrent ("root", { "..", "escape.bin" }));
	assert (traversal.GetError () == eTorrentErrorMalformedMetaInfo);

	Torrent relativeTraversal (MakeMultiFileTorrent ("root", { "sub", "..", "escape.bin" }));
	assert (relativeTraversal.GetError () == eTorrentErrorMalformedMetaInfo);

	Torrent absoluteComponent (MakeMultiFileTorrent ("root", { "/abs", "escape.bin" }));
	assert (absoluteComponent.GetError () == eTorrentErrorMalformedMetaInfo);

	Torrent backslashComponent (MakeMultiFileTorrent ("root", { "..\\escape.bin" }));
	// Separators inside a component are neutralised, so this is not traversal;
	// it must not produce an error but also must not create a directory.
	assert (backslashComponent.GetError () == eTorrentErrorNoError);
	assert (!backslashComponent.GetFiles ().empty ());

	const std::string embeddedNul ("bad\0name", 8);
	Torrent nulComponent (MakeMultiFileTorrent ("root", { embeddedNul }));
	assert (nulComponent.GetError () == eTorrentErrorMalformedMetaInfo);

	Torrent absoluteName (MakeMultiFileTorrent ("/root", { "file.bin" }));
	assert (absoluteName.GetError () == eTorrentErrorMalformedMetaInfo);

	Torrent valid (MakeMultiFileTorrent ("root", { "sub", "file.bin" }));
	assert (valid.GetError () == eTorrentErrorNoError);
	assert (!valid.GetFiles ().empty ());
}

// Lexical containment and symlink rejection used by the session before it
// creates, opens or removes files.
void TestPathContainment ()
{
	assert (!IsSafeRelativePath (std::filesystem::path ("../escape")));
	assert (!IsSafeRelativePath (std::filesystem::path ("a/../b")));
	assert (!IsSafeRelativePath (std::filesystem::path ("/absolute")));
	assert (!IsSafeRelativePath (std::filesystem::path (".")));
	assert (!IsSafeRelativePath (std::filesystem::path ()));
	assert (IsSafeRelativePath (std::filesystem::path ("a/b")));
	assert (IsSafeRelativePath (std::filesystem::path ("file.bin")));

	// This is the network-thread check: it must be purely lexical and reject
	// any path that leaves the configured root before storage is asked to
	// touch the filesystem.
	assert (IsPathLexicallyWithinRoot (std::filesystem::path ("root"), std::filesystem::path ("root/file.bin")));
	assert (IsPathLexicallyWithinRoot (std::filesystem::path ("root"), std::filesystem::path ("root/sub/file")));
	assert (!IsPathLexicallyWithinRoot (std::filesystem::path ("root"), std::filesystem::path ("root/../escape")));
	assert (!IsPathLexicallyWithinRoot (std::filesystem::path ("root"), std::filesystem::path ("other/file")));

	TempDir dir;
	dir.path = MakeTempDir ();
	auto root = dir.path / "root";
	auto outside = dir.path / "outside";
	std::error_code ec;
	std::filesystem::create_directories (root / "sub", ec);
	assert (!ec);
	std::filesystem::create_directories (outside, ec);
	assert (!ec);

	// Download paths are built from sanitized components; an absolute or
	// traversal path must be rejected before it is anchored.
	{
		TorrentFile traversal ("../escape.bin", 16);
		assert (!traversal.UpdateFullPath (root));
		assert (traversal.GetFullFilePath ().empty ());
		TorrentFile absolute ("/escape.bin", 16);
		assert (!absolute.UpdateFullPath (root));
		TorrentFile validRelative ("sub/file.bin", 16);
		assert (validRelative.UpdateFullPath (root));
		assert (validRelative.GetFullFilePath () == root / "sub" / "file.bin");
	}

	assert (IsPathWithinRoot (root, root / "sub" / "file.bin", false));
	assert (!IsPathWithinRoot (root, root / ".." / "outside" / "file.bin", false));
	assert (!IsPathWithinRoot (root, outside / "file.bin", false));
	assert (IsPathWithinRoot (root, root / "missing" / "file.bin", false)); // creation path

	// Anchored creation traverses existing components without following
	// links and rejects a path that is not safe and relative.
	assert (CreateDirectoriesWithinRootNoFollow (root, std::filesystem::path ("sub")));
	assert (CreateFileWithSizeNoFollow (root, std::filesystem::path ("sub/created.bin"), 4));
	assert (std::filesystem::file_size (root / "sub" / "created.bin") == 4);
	assert (!CreateDirectoriesWithinRootNoFollow (root, std::filesystem::path ("../escape")));
	assert (!CreateFileWithSizeNoFollow (root, std::filesystem::path ("../escape.bin"), 4));

#ifndef _WIN32
	auto outsideFile = outside / "file.bin";
	{
		std::ofstream f (outsideFile, std::ofstream::binary);
		f << "outside";
	}
	ec.clear ();
	std::filesystem::create_directory_symlink (outside, root / "link", ec);
	if (!ec)
	{
		assert (std::filesystem::is_symlink (std::filesystem::symlink_status (root / "link")));
		assert (!IsPathWithinRoot (root, root / "link" / "file.bin", false));
		assert (!CreateDirectoriesWithinRootNoFollow (root, std::filesystem::path ("link/newdir")));
		assert (!CreateFileWithSizeNoFollow (root, std::filesystem::path ("link/new.bin"), 4));
	}
	ec.clear ();
	std::filesystem::create_symlink (outsideFile, root / "file-link", ec);
	if (!ec) assert (!IsPathWithinRoot (root, root / "file-link", false));
#endif
}

#ifndef _WIN32
// A swapped part file or intermediate directory must never redirect reads or
// writes outside the torrent root. The victim file must stay byte-identical.
void TestTorrentFileSymlinkRejection ()
{
	TempDir dir;
	dir.path = MakeTempDir ();
	auto victim = dir.path / "victim.bin";
	const std::string secret ("SECRET-DATA");
	{
		std::ofstream f (victim, std::ofstream::binary);
		f.write (secret.data (), secret.size ());
	}

	uint8_t block[16] = { };
	std::error_code ec;
	std::filesystem::create_symlink (victim, dir.path / "leaf.bin.part", ec);
	if (!ec)
	{
		auto leaf = std::make_shared<TorrentFile> (dir.path / "leaf.bin", sizeof (block));
		leaf->SetRootDir (dir.path);
		assert (!leaf->Save (0, block, sizeof (block)));
		assert (!leaf->Load (0, block, sizeof (block)));
	}

	auto outside = dir.path / "outside";
	std::filesystem::create_directories (outside / "sub", ec);
	assert (!ec);
	{
		std::ofstream f (outside / "sub" / "nested.bin.part", std::ofstream::binary);
		f.write ((const char *)block, sizeof (block));
	}
	ec.clear ();
	// Use a nested target so the leaf's parent itself is not the symlink.
	std::filesystem::create_directory_symlink (outside, dir.path / "linkdir", ec);
	if (!ec)
	{
		auto nested = std::make_shared<TorrentFile> (dir.path / "linkdir" / "sub" / "nested.bin", sizeof (block));
		nested->SetRootDir (dir.path);
		assert (!nested->Save (0, block, sizeof (block)));
		assert (!nested->Load (0, block, sizeof (block)));
	}

	// Nothing above may have modified the victim or the outside file.
	{
		std::ifstream f (victim, std::ifstream::binary);
		std::string content ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char> ());
		assert (content == secret);
	}
	assert (std::filesystem::file_size (victim) == secret.size ());
	assert (std::filesystem::file_size (outside / "sub" / "nested.bin.part") == sizeof (block));
}
#endif

// Resume files are derived data but their write path is still attacker
// reachable through the torrent directory. A symlinked ".resume" must not
// truncate its target, and reads must be bounded.
void TestResumeWriteAndRead ()
{
	TempDir dir;
	dir.path = MakeTempDir ();

	const std::string secret ("KEEP");
	auto victim = dir.path / "victim.txt";
	{
		std::ofstream f (victim, std::ofstream::binary);
		f.write (secret.data (), secret.size ());
	}

	Torrent torrent (MakeMultiFileTorrent ("resume-root", { "file.bin" }, 32768, 2));
	assert (torrent.GetError () == eTorrentErrorNoError);
	torrent.SetFullPath (dir.path / "resume-root");
	// One complete and one incomplete piece makes the bitfield indeterminate,
	// which is the state that is persisted to the resume file.
	torrent.GetPiece (0).Complete ();

	auto resumePath = dir.path / "resume-root.resume";
	std::error_code ec;
	std::filesystem::remove (resumePath, ec);
	std::filesystem::create_symlink (victim, resumePath, ec);
	if (!ec)
	{
		torrent.SaveTorrentResumeFile ();
		assert (std::filesystem::is_symlink (std::filesystem::symlink_status (resumePath)));
	}
	std::filesystem::remove (resumePath, ec);
	torrent.SaveTorrentResumeFile ();
	assert (std::filesystem::is_regular_file (std::filesystem::symlink_status (resumePath)));
	assert (std::filesystem::file_size (resumePath) == 1); // 2 pieces -> one byte

	// Nothing above may have modified the victim.
	{
		std::ifstream f (victim, std::ifstream::binary);
		std::string content ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char> ());
		assert (content == secret);
	}

	std::vector<uint8_t> content;
	std::string error;
	// Reading through a symlinked resume path must not expose the target.
	ec.clear ();
	std::filesystem::remove (resumePath, ec);
	std::filesystem::create_symlink (victim, resumePath, ec);
	if (!ec)
	{
		assert (!ReadFileBoundedNoFollow (resumePath, 64, content, error));
		std::filesystem::remove (resumePath, ec);
	}
	// Empty and oversized/truncated resume payloads are rejected before any
	// bitfield is applied.
	{
		std::ofstream f (resumePath, std::ofstream::binary | std::ofstream::trunc);
	}
	assert (!ReadFileBoundedNoFollow (resumePath, 1, content, error));
	{
		std::ofstream f (resumePath, std::ofstream::binary | std::ofstream::trunc);
		f.put ('\x80');
		f.put ('\x00');
	}
	assert (!ReadFileBoundedNoFollow (resumePath, 1, content, error));
	assert (ReadFileBoundedNoFollow (resumePath, 2, content, error));
	assert (content.size () == 2);
}
}

int main ()
{
	TestI2PTrackerHostValidation ();
	TestPieceBlockBounds ();
	TestCanAcceptPieceBlock ();
	TestPeerMessageFraming ();
	TestPieceCorruptionAndReset ();
	TestCorruptResumeBitfield ();
	TestInfoIsNotCopied ();
	TestRoutingTableOwnership ();
	TestDHTRequestBounds ();
	TestDHTPeerBounds ();
	TestTorrentFileIO ();
	TestUnsafeTorrentPaths ();
	TestPathContainment ();
#ifndef _WIN32
	TestTorrentFileSymlinkRejection ();
#endif
	TestResumeWriteAndRead ();
	return 0;
}

#else

int main ()
{
	return 0;
}

#endif
