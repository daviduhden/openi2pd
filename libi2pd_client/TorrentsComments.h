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
// Java's peer connection rejects an extension payload above 16482 bytes
// (MAX_MSG_SIZE 16484 minus the type and extension-ID bytes).
inline constexpr size_t MAX_COMMENT_MESSAGE_BYTES = 16484 - 2; // Java MAX_MSG_SIZE minus type+extension ID
inline constexpr size_t MAX_COMMENT_OWNER_UNITS = 32;
inline constexpr size_t MAX_COMMENT_TEXT_UNITS = 512;
inline constexpr size_t MAX_COMMENT_TEXT_TOTAL_UNITS = 4096;
inline constexpr size_t MAX_COMMENT_PER_OWNER = 8;
inline constexpr size_t MAX_COMMENT_HIDDEN_COUNT = 64;
// An empty owner carries no stable label to attribute, so anonymous entries get
// a separate, smaller visible budget instead of escaping the per-pseudonym cap.
// Hidden entries stay for deduplication but count toward neither budget.
inline constexpr size_t MAX_COMMENT_ANONYMOUS_VISIBLE = 16;
// ut_comment admission. The ingress window still allows 8 messages per minute
// and a message may carry MAX_COMMENT_MESSAGE_COUNT accepted entries, so the
// store needs an explicit entry budget per connection and per torrent; without
// it one peer could flush the 256-entry store in two minutes and reset the
// budget by reconnecting. Both budgets are fixed, non-sliding windows and
// neither replaces the 256-entry/4096-unit/64-hidden store limits.
inline constexpr size_t MAX_COMMENT_ENTRIES_PER_CONNECTION_WINDOW = 40;
inline constexpr size_t MAX_COMMENT_ENTRIES_PER_TORRENT_WINDOW = 80;
inline constexpr uint64_t COMMENT_ENTRY_WINDOW = 60; // in seconds
// A local mutation (add, hide, moderation restore) is worth an immediate
// request once the store revision changed, but never faster than the minimum
// interval: RefreshComments() runs once per connection for every local
// mutation, so a burst of edits must coalesce. An unchanged revision keeps the
// periodic cadence.
inline constexpr uint64_t COMMENT_REQUEST_MIN_INTERVAL = 10; // in seconds
inline constexpr uint64_t COMMENT_REQUEST_PERIODIC_INTERVAL = 600; // in seconds

// Fixed-window admission counter for comment entries. Pure and
// clock-parameterised so the regression test can pin the boundaries without a
// connection or a timer. The owner supplies the storage and the clock; no
// locking is performed. A zero count always succeeds without consuming the
// window, and a backwards clock never restarts it (that would hand out a
// second full budget).
struct CommentAdmissionWindow
{
	uint64_t start = 0; // monotonic seconds
	size_t admitted = 0;
	constexpr bool Admit (size_t count, size_t cap, uint64_t nowSeconds)
	{
		if (!count) return true;
		if (!nowSeconds || count > cap) return false;
		// Fixed window: it restarts exactly at start + COMMENT_ENTRY_WINDOW and
		// the comparison cannot wrap for a real monotonic clock.
		if (!start || nowSeconds >= start + COMMENT_ENTRY_WINDOW)
		{
			start = nowSeconds;
			admitted = 0;
		}
		if (count > cap - admitted) return false;
		admitted += count;
		return true;
	}
	constexpr size_t Remaining (size_t cap) const { return admitted >= cap ? 0 : cap - admitted; }
};
/// A comment's owner is an unauthenticated pseudonym, never an identity.
/// The stored time is epoch seconds; the wire timestamp is an age in seconds.
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
/// Decode a bounded ut_comment payload and convert ages using @a now.
/// @return nullopt for an invalid envelope; invalid individual comment entries
/// can be skipped so other usable entries in the same message survive.
std::optional<TorrentCommentMessage> DecodeTorrentComments (std::string_view encoded, uint64_t now);
/// Encode a request with a bounded count and a 64-byte empty filter hint.
std::string EncodeTorrentCommentRequest (size_t count = MAX_COMMENT_MESSAGE_COUNT);
/// Encode the newest valid, non-hidden comments, dropping older entries if the
/// negotiated extension payload would exceed MAX_COMMENT_MESSAGE_BYTES.
std::string EncodeTorrentComments (
	const std::vector<TorrentComment> & comments, uint64_t now, size_t count = MAX_COMMENT_MESSAGE_COUNT);
/// One in-memory store per torrent; the caller serializes access. Hidden
/// entries remain for deduplication, so peers cannot immediately undo local
/// moderation. This class performs no IO or author verification.
///
/// Deduplication compares normalized owner/text without rewriting stored text:
/// ASCII whitespace is trimmed/collapsed and U+200B/U+200C/U+200D/U+FEFF are
/// ignored. Rating is not part of the key, so replay cannot re-rate the same
/// normalized text.
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
	// Same normalized-key predicate Add uses, including hidden entries. Callers
	// that charge an admission budget call this first so a duplicate replay
	// cannot consume another peer's share of it.
	bool IsDuplicate (const TorrentComment & comment) const;
	bool Hide (size_t localIndex);
	bool HideId (uint64_t localId);
	bool UnhideId (uint64_t localId);
	std::vector<TorrentComment> Snapshot () const { return m_Comments; }
	// Stored-entry counts; visible = !hidden. They are plain field counts, so
	// a snapshot builder can expose them without iterating a copy.
	size_t VisibleCount () const;
	size_t HiddenCount () const;
	size_t LocalCount () const;
	bool Restore (bool enabled, const std::vector<TorrentComment> & comments);
	size_t RatingCount () const;
	double AverageRating () const;
	std::vector<TorrentComment> Visible () const;

private:
	// Returns true when at least one hidden entry was dropped.
	bool TrimHidden ();
	uint64_t m_Revision = 0;
	uint64_t m_NextId = 1;
	bool m_Enabled = false;
	size_t m_TextUnits = 0; // visible text only
	std::vector<TorrentComment> m_Comments;
};
// Version 1 local state; distinct from the peer-wire age-based format.
// Decode replaces the store only after full bounded validation: a structural
// error, an unknown version or a duplicate local id rejects the whole state,
// while a comment timestamp in the future (backwards clock or restored backup)
// is clamped to `now` and the entry is kept.
std::string EncodeTorrentCommentState (const TorrentCommentStore & store);
bool DecodeTorrentCommentState (std::string_view encoded, uint64_t now, TorrentCommentStore & store);
}
#endif
