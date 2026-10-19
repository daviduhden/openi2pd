/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TorrentsComments.
 *
 * Declares the ut_comment codec and the per-torrent local comment store,
 * including count/size limits, hidden-entry retention and versioned
 * local-state persistence helpers.
 *
 */

#ifndef TORRENTS_COMMENTS_H__
#define TORRENTS_COMMENTS_H__
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace i2p::torrents
{
inline constexpr std::string_view COMMENT_EXTENSION_NAME = "ut_comment";
inline constexpr size_t MAX_COMMENT_COUNT = 256;
inline constexpr size_t MAX_COMMENT_MESSAGE_COUNT = 20;
inline constexpr size_t MAX_COMMENT_MESSAGE_BYTES = 60 * 1024;
// Owner is an unauthenticated user-supplied pseudonym, never an identity.
// Times are local epoch seconds; wire timestamp is an AGE in seconds.
struct TorrentComment
{
	std::string owner, text;
	uint8_t rating = 0;
	uint64_t time = 0;
	bool hidden = false;
	uint64_t localId = 0;
	bool local = false;
};
struct TorrentCommentMessage
{
	bool request = false;
	size_t requested = MAX_COMMENT_MESSAGE_COUNT;
	std::vector<TorrentComment> comments;
};
std::optional<TorrentCommentMessage> DecodeTorrentComments (std::string_view encoded, uint64_t now);
std::string EncodeTorrentCommentRequest (size_t count = MAX_COMMENT_MESSAGE_COUNT);
std::string EncodeTorrentComments (
	const std::vector<TorrentComment> & comments, uint64_t now, size_t count = MAX_COMMENT_MESSAGE_COUNT);
// One store per torrent; caller serializes access. Hidden entries remain
// for deduplication, so a peer cannot immediately undo local moderation.
// No filesystem, network, author verification or automatic publication.
class TorrentCommentStore
{
public:
	void SetEnabled (bool enabled)
	{
		if (m_Enabled != enabled)
		{
			m_Enabled = enabled;
			++m_Revision;
		}
	}
	uint64_t Revision () const { return m_Revision; }
	bool IsEnabled () const { return m_Enabled; }
	bool Add (TorrentComment comment);
	bool Hide (size_t localIndex);
	bool HideId (uint64_t localId);
	std::vector<TorrentComment> Snapshot () const { return m_Comments; }
	bool Restore (bool enabled, const std::vector<TorrentComment> & comments);
	size_t RatingCount () const;
	double AverageRating () const;
	std::vector<TorrentComment> Visible () const;

private:
	uint64_t m_Revision = 0;
	uint64_t m_NextId = 1;
	bool m_Enabled = false;
	size_t m_TextBytes = 0;
	std::vector<TorrentComment> m_Comments;
};
// Version 1 local state; distinct from the peer-wire age-based format.
// Decode replaces the store only after full bounded validation.
std::string EncodeTorrentCommentState (const TorrentCommentStore & store);
bool DecodeTorrentCommentState (std::string_view encoded, uint64_t now, TorrentCommentStore & store);
}
#endif
