/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: bounded local torrent administration state codec.
 * Defines the versioned stopped/comment/priority snapshot independently
 * of interoperable torrent metainfo and piece-resume formats.
 */

#ifndef TORRENTS_STATE_H__
#define TORRENTS_STATE_H__
#ifndef NO_TORRENTS
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include "TorrentsComments.h"

namespace i2p::torrents
{
inline constexpr size_t MAX_TORRENT_LOCAL_STATE_BYTES = 256 * 1024;
struct TorrentLocalState
{
	bool stopped = false;
	TorrentCommentStore comments;
	// Empty means default priorities; otherwise indexed by metainfo file
	// order. The caller checks the count against its specific torrent.
	std::vector<uint8_t> priorities;
};

// Local v1 bencode dictionary with exactly four required keys:
// comments = nested EncodeTorrentCommentState dictionary,
// priorities = binary string of values 0..2, stopped = integer 0 or 1,
// version = integer 1. Unknown keys/versions are rejected. This format
// has no I2PSnark persisted-state compatibility claim; it must never be
// written over .torrent metainfo or an existing .resume piece bitfield.
// Encoding returns empty on invalid state; error is cleared on success.
std::string EncodeTorrentLocalState (const TorrentLocalState & state, std::string & error);
// Entire encoded input must be consumed. Invalid input leaves state
// unchanged, including comment moderation, enablement and revisions.
bool DecodeTorrentLocalState (std::string_view encoded, uint64_t now, TorrentLocalState & state, std::string & error);
}
#endif
#endif
