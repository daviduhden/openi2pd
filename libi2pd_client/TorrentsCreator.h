/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TorrentsCreator.
 *
 * Declares the torrent-creation options and the storage-worker entry
 * point that hashes an authorized directory tree without modifying
 * payload files.
 *
 */

#ifndef TORRENTS_CREATOR_H__
#define TORRENTS_CREATOR_H__
#ifndef NO_TORRENTS
#include "Torrents.h"
#include <functional>
namespace i2p::torrents
{
/// Configuration and callbacks for one v1 metainfo creation job. The
/// caller must keep callback captures valid until the storage-worker
/// job returns.
struct TorrentCreateOptions
{
	size_t pieceLength = 0; // zero selects a bounded automatic size
	Torrent::TrackerTiers trackers;
	bool privateTorrent = false;
	std::function<bool ()> cancelled; ///< Cooperative cancellation predicate.
	/// Called synchronously with completed/total bytes and completed/total
	/// files.
	std::function<void (uint64_t, uint64_t, size_t, size_t)> progress;
};
/// Select a valid power-of-two v1 piece size within engine budgets.
/// @param requested Explicit size, or 0 for the automatic target of
///                  <=2048 pieces.
/// @return The selected size, or 0 for an invalid input or exceeded
///         budget.
size_t SelectTorrentPieceLength (uint64_t totalBytes, size_t requested = 0);
/// Hash an authorized tree into BEP 3 v1 metadata without modifying
/// payloads.
/// @param root Administrator-authorized directory used as the traversal
///             anchor.
/// @param relativePath Safe path beneath that anchor.
/// @param error Receives a failure reason; cleared on entry.
/// @return The new torrent, or nullptr on invalid input, cancellation
///         or IO error.
/// @warning Run on the storage worker. POSIX uses
///          openat/O_NOFOLLOW for each descendant; Windows rejects
///          observed reparse points but retains a check/open race. The
///          result is not marked complete or verified for seeding.
/// @see https://www.bittorrent.org/beps/bep_0003.html
std::shared_ptr<Torrent> CreateTorrentFromPath (const std::filesystem::path & root,
	const std::filesystem::path & relativePath, const TorrentCreateOptions & options, std::string & error);
}
#endif
#endif
