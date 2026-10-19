/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TorrentsCreator.
 *
 * Declares the torrent-creation options and the storage-worker entry point
 * that hashes an authorized directory tree without modifying payload files.
 *
 */

#ifndef TORRENTS_CREATOR_H__
#define TORRENTS_CREATOR_H__
#ifndef NO_TORRENTS
#include "Torrents.h"
#include <functional>
namespace i2p::torrents
{
struct TorrentCreateOptions
{
	size_t pieceLength = 256 * 1024;
	Torrent::TrackerTiers trackers;
	bool privateTorrent = false;
	std::function<bool ()> cancelled;
};
// Run on the storage worker. Reads only; never creates or overwrites payload.
// The root is an administrator-authorized directory, relativePath is below
// it. POSIX opens every descendant with openat/O_NOFOLLOW. Windows rejects
// observed reparse points but retains the documented check/open race.
// The resulting torrent is not marked complete or verified for seeding.
std::shared_ptr<Torrent> CreateTorrentFromPath (const std::filesystem::path & root,
	const std::filesystem::path & relativePath, const TorrentCreateOptions & options, std::string & error);
}
#endif
#endif
