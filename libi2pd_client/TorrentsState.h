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
/// Administrative state kept separately from metainfo and resume bitfields.
struct TorrentLocalState
{
	bool stopped = false;
	TorrentCommentStore comments;
	// Empty means default priorities; otherwise indexed by metainfo file
	// order. The caller checks the count against its specific torrent.
	std::vector<uint8_t> priorities;
};

/// Encode the local v1 bencode dictionary: comments, priorities (binary
/// values 0..2), stopped (0/1) and version (1). This is not an I2PSnark state
/// format and must not replace metainfo or a .resume bitfield.
/// @param error Cleared on entry and set if validation or size checks fail.
/// @return Encoded state, or an empty string on failure.
std::string EncodeTorrentLocalState (const TorrentLocalState & state, std::string & error);
/// Decode exactly one bounded v1 dictionary, rejecting unknown keys or version.
/// @param now Local epoch seconds used when restoring comment timestamps.
/// @param state Replaced only after the entire input passes validation.
/// @param error Cleared on entry and set on failure.
/// @return false for malformed, trailing or unsupported data; @a state is then unchanged.
bool DecodeTorrentLocalState (std::string_view encoded, uint64_t now, TorrentLocalState & state, std::string & error);
}
#endif
#endif
