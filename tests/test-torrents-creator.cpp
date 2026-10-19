/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test torrents creator.
 *
 * Creates a multi-file torrent from temporary files and compares independently constructed info bytes, covering cross-file piece hashing, zero-length entries, path/symlink rejection and no-follow atomic state I/O.
 *
 */

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include "TorrentsCreator.h"
#ifndef NO_TORRENTS
int main ()
{
	using namespace i2p::torrents;
	namespace fs = std::filesystem;
	auto root = fs::temp_directory_path ()
		/ ("openi2pd-creator-" + std::to_string (std::chrono::steady_clock::now ().time_since_epoch ().count ()));
	assert (fs::create_directory (root));
	fs::create_directory (root / "payload");
	std::string first (12000, 'a'), second (10000, 'b');
	{
		std::ofstream out (root / "payload/a", std::ios::binary);
		out << first;
	}
	{
		std::ofstream out (root / "payload/b", std::ios::binary);
		out << second;
	}
	{
		std::ofstream out (root / "payload/empty", std::ios::binary);
	}
	TorrentCreateOptions options;
	options.pieceLength = MIN_PIECE_LENGTH;
	std::string error;
	auto torrent = CreateTorrentFromPath (root, "payload", options, error);
	assert (torrent && error.empty () && torrent->GetNumPieces () == 2 && torrent->GetFiles ().size () == 3);
	std::string bytes = first + second;
	Torrent::InfoHash hash;
	SHA1 (reinterpret_cast<const uint8_t *> (bytes.data ()), MIN_PIECE_LENGTH, hash.data ());
	// Exact independently constructed info bytes check cross-file piece hashing
	// and deterministic enumeration, including the empty file entry.
	std::vector<Torrent::InfoHash> hashes { hash };
	SHA1 (reinterpret_cast<const uint8_t *> (bytes.data () + MIN_PIECE_LENGTH), bytes.size () - MIN_PIECE_LENGTH,
		hash.data ());
	hashes.push_back (hash);
	auto expected = Torrent::CreateV1 (
		"payload", { { "a", first.size () }, { "b", second.size () }, { "empty", 0 } }, MIN_PIECE_LENGTH, hashes);
	assert (expected && torrent->GetInfoHash () == expected->GetInfoHash ());
	assert (!torrent->IsComplete ());
	auto single = CreateTorrentFromPath (root, "payload/a", options, error);
	assert (single && single->IsSingleFile () && single->GetName () == "a");
	assert (!CreateTorrentFromPath (root, "../escape", options, error));
	assert (!CreateTorrentFromPath (root, root / "payload", options, error));
	assert (!CreateTorrentFromPath (root, "payload/empty", options, error));
	options.pieceLength = 0;
	assert (!CreateTorrentFromPath (root, "payload", options, error));
	options.pieceLength = MIN_PIECE_LENGTH;
#ifndef _WIN32
	fs::create_symlink (root / "payload/a", root / "link");
	assert (!CreateTorrentFromPath (root, "link", options, error));
	fs::create_symlink (root / "payload/a", root / "payload/link");
	assert (!CreateTorrentFromPath (root, "payload", options, error));
	fs::remove (root / "payload/link");
	fs::create_directory_symlink (root / "payload", root / "alias");
	assert (!CreateTorrentFromPath (root, "alias/a", options, error));
#endif
	options.cancelled = [] { return true; };
	assert (!CreateTorrentFromPath (root, "payload", options, error));
	assert (WriteFileAtomicNoFollow (root, "state", "one", 3, error));
	assert (WriteFileAtomicNoFollow (root, "state", "two", 3, error));
	std::vector<uint8_t> state;
	assert (ReadFileBoundedNoFollow (root, "state", 100, state, error));
	assert (std::string (state.begin (), state.end ()) == "two");
	assert (!WriteFileAtomicNoFollow (root, "payload", "x", 1, error));
	assert (!WriteFileAtomicNoFollow (root, "../escape", "x", 1, error));
#ifndef _WIN32
	assert (!WriteFileAtomicNoFollow (root, "link", "x", 1, error));
	fs::create_hard_link (root / "state", root / "state-alias");
	assert (!WriteFileAtomicNoFollow (root, "state", "x", 1, error));
#endif
	fs::remove_all (root);
	return 0;
}
#else
int main ()
{
	return 0;
}
#endif
