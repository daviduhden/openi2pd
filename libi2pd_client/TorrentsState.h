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
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include "TorrentsComments.h"

namespace i2p::torrents
{
inline constexpr size_t MAX_TORRENT_LOCAL_STATE_BYTES = 256 * 1024;
inline constexpr size_t MAX_TORRENT_MAGNET_STATE_BYTES = 16 * 1024;
/// Native-only unresolved magnet v1. Hash names the record; display name
/// and ordered I2P tracker hints are not payload paths or credentials.
struct TorrentMagnetState
{
	std::array<uint8_t, 20> infoHash { };
	std::string name;
	std::vector<std::string> trackers;
	bool stopped = false;
};
/// Encode one bounded v1 record; invalid hints return empty with error.
std::string EncodeTorrentMagnetState (const TorrentMagnetState & state, std::string & error);
/// Decode one complete v1 record; failure leaves state unchanged.
bool DecodeTorrentMagnetState (std::string_view encoded, TorrentMagnetState & state, std::string & error);
/// Administrative state kept separately from metainfo and resume
/// bitfields.
struct TorrentLocalState
{
	bool stopped = false;
	TorrentCommentStore comments;
	// Empty means default priorities; otherwise indexed by metainfo file
	// order. The caller checks the count against its specific torrent.
	std::vector<uint8_t> priorities;
};

/// Encode the local v1 bencode dictionary: comments, priorities (binary
/// values 0..2), stopped (0/1) and version (1). This is not an I2PSnark
/// state format and must not replace metainfo or a .resume bitfield.
/// @param state Validated state snapshot to encode.
/// @param error Cleared on entry and set if validation or size checks
///              fail.
/// @return Encoded state, or an empty string on failure.
std::string EncodeTorrentLocalState (const TorrentLocalState & state, std::string & error);
/// Decode exactly one bounded v1 dictionary, rejecting unknown keys or
/// version.
/// @param encoded Bencoded dictionary, consumed only when it validates
///                completely.
/// @param now Local epoch seconds used when restoring comment
///            timestamps.
/// @param state Replaced only after the entire input passes validation.
/// @param error Cleared on entry and set on failure.
/// @return false for malformed, trailing or unsupported data; @a state
///         is then unchanged.
bool DecodeTorrentLocalState (std::string_view encoded, uint64_t now, TorrentLocalState & state, std::string & error);
}
#endif
#endif
