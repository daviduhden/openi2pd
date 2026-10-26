/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: local torrent state codec regressions.
 * Covers complete-input validation, version and resource bounds,
 * comment moderation, future-timestamp clamping, binary priorities and
 * failed-decode atomicity.
 */

#include <cassert>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "TorrentsState.h"
#include "Torrents.h"
#ifndef NO_TORRENTS
namespace
{
std::string Envelope (std::string_view comments, std::string_view priorities, std::string_view stopped = "i0e",
	std::string_view version = "i1e")
{
	using namespace i2p::torrents;
	return CreateDictionary (
		{ { "comments", comments }, { "priorities", priorities }, { "stopped", stopped }, { "version", version } });
}

// Version 1 comment-state dictionary holding exactly the supplied entries.
std::string CommentState (const std::vector<std::string> & entries)
{
	using namespace i2p::torrents;
	return CreateDictionary (
		{ { "comments", CreateList (entries) }, { "enabled", CreateInteger (1) }, { "version", CreateInteger (1) } });
}

std::string CommentEntry (int64_t id, std::string_view owner, std::string_view text, int64_t time)
{
	using namespace i2p::torrents;
	return CreateDictionary ({ { "hidden", CreateInteger (0) }, { "id", CreateInteger (id) },
		{ "like", CreateInteger (0) }, { "local", CreateInteger (0) }, { "owner", CreateByteString (owner) },
		{ "text", CreateByteString (text) }, { "time", CreateInteger (time) } });
}
}

int main ()
{
	using namespace i2p::torrents;
	constexpr uint64_t now = 1800000000;
	TorrentLocalState original;
	original.stopped = true;
	original.priorities = { 0, 1, 2, 0 };
	original.comments.SetEnabled (true);
	TorrentComment comment { "pseudonym", "Useful content", 5, now - 120, false };
	comment.local = true;
	assert (original.comments.Add (comment));
	assert (original.comments.Hide (0));
	std::string error = "stale error";
	auto encoded = EncodeTorrentLocalState (original, error);
	assert (!encoded.empty () && error.empty ());
	TorrentLocalState decoded;
	assert (DecodeTorrentLocalState (encoded, now, decoded, error) && error.empty ());
	assert (decoded.stopped && decoded.priorities == original.priorities);
	assert (decoded.comments.IsEnabled () && decoded.comments.Visible ().empty ());
	auto comments = decoded.comments.Snapshot ();
	assert (comments.size () == 1 && comments[0].hidden && comments[0].local && comments[0].localId != 0);
	assert (comments[0].time == comment.time && comments[0].owner == comment.owner && comments[0].rating == 5);
	assert (EncodeTorrentLocalState (decoded, error) == encoded);

	const auto revision = decoded.comments.Revision ();
	auto rejectWithoutChanges = [&] (std::string_view invalid)
	{
		error.clear ();
		assert (!DecodeTorrentLocalState (invalid, now, decoded, error));
		assert (!error.empty ());
		assert (decoded.comments.Revision () == revision);
		assert (EncodeTorrentLocalState (decoded, error) == encoded);
	};
	const auto commentState = EncodeTorrentCommentState (original.comments);
	const auto defaults = CreateByteString ("");
	rejectWithoutChanges ("");
	rejectWithoutChanges (encoded + "trailing");
	rejectWithoutChanges (encoded + std::string (1, '\0'));
	rejectWithoutChanges (encoded.substr (0, encoded.size () - 1));
	rejectWithoutChanges (std::string (MAX_TORRENT_LOCAL_STATE_BYTES + 1, 'x'));
	rejectWithoutChanges (std::string (MAX_TORRENT_LOCAL_STATE_BYTES, 'x'));
	rejectWithoutChanges (Envelope (commentState, defaults, "i0e", "i2e"));
	rejectWithoutChanges (Envelope (commentState, defaults, "i0e", "i0e"));
	rejectWithoutChanges (Envelope (commentState, defaults, "i0e", "1:1"));
	rejectWithoutChanges (Envelope (commentState, defaults, "i0e", "i01e"));
	rejectWithoutChanges (Envelope (commentState, defaults, "i2e"));
	rejectWithoutChanges (Envelope (commentState, defaults, "i-1e"));
	rejectWithoutChanges (Envelope (commentState, defaults, "i-0e"));
	rejectWithoutChanges (Envelope (commentState, defaults, "1:0"));
	rejectWithoutChanges (Envelope (commentState, "le"));
	rejectWithoutChanges (Envelope (commentState, CreateByteString (std::string (1, '\3'))));
	rejectWithoutChanges (Envelope (commentState, CreateByteString (std::string (1, static_cast<char> (255)))));
	rejectWithoutChanges (Envelope (commentState, CreateByteString (std::string (MAX_NUM_TORRENT_FILES + 1, '\1'))));
	rejectWithoutChanges (Envelope ("i0e", defaults));
	rejectWithoutChanges (
		Envelope (CreateDictionary ({ { "comments", "le" }, { "enabled", "i2e" }, { "version", "i1e" } }), defaults));
	rejectWithoutChanges (
		CreateDictionary ({ { "priorities", defaults }, { "stopped", "i0e" }, { "version", "i1e" } }));
	rejectWithoutChanges (CreateDictionary ({ { "comments", commentState }, { "priorities", defaults },
		{ "stopped", "i0e" }, { "unknown", "i0e" }, { "version", "i1e" } }));
	rejectWithoutChanges (encoded.substr (0, encoded.size () - 1) + "7:versioni1ee");
	// Structural comment-state corruption is still rejected as a whole: two
	// entries sharing a local id, a non-positive id, a negative timestamp and
	// terminal control characters are invalid entries, not clampable ones.
	rejectWithoutChanges (
		Envelope (CommentState ({ CommentEntry (1, "dup", "dup", 1), CommentEntry (1, "dup", "dup", 1) }), defaults));
	rejectWithoutChanges (Envelope (CommentState ({ CommentEntry (0, "zero", "zero", 1) }), defaults));
	rejectWithoutChanges (Envelope (CommentState ({ CommentEntry (1, "neg", "neg", -1) }), defaults));
	rejectWithoutChanges (Envelope (CommentState ({ CommentEntry (1, "ctl", "bad \x01", 1) }), defaults));
	rejectWithoutChanges (Envelope (CommentState ({ CommentEntry (1, "c1", "bad \xC2\x9B", 1) }), defaults));
	std::vector<std::string> tooManyComments;
	for (size_t i = 0; i < MAX_COMMENT_COUNT + 1; ++i)
		tooManyComments.push_back (
			CommentEntry (static_cast<int64_t> (i) + 1, "owner" + std::to_string (i), "text " + std::to_string (i), 1));
	rejectWithoutChanges (Envelope (CommentState (tooManyComments), defaults));

	// A timestamp ahead of the local clock (backwards clock or restored
	// backup) no longer rejects the whole state: it is clamped to `now`, so the
	// comments, priorities and the stopped flag survive and a paused torrent
	// stays paused.
	TorrentLocalState future;
	future.stopped = true;
	future.priorities = { 2, 1 };
	future.comments.SetEnabled (true);
	comment.time = now + 5000;
	assert (future.comments.Add (comment));
	const auto futureEncoded = EncodeTorrentLocalState (future, error);
	assert (!futureEncoded.empty () && error.empty ());
	TorrentLocalState restored;
	assert (DecodeTorrentLocalState (futureEncoded, now, restored, error) && error.empty ());
	assert (restored.stopped && restored.priorities == future.priorities);
	auto clamped = restored.comments.Snapshot ();
	assert (clamped.size () == 1 && clamped[0].time == now && clamped[0].owner == comment.owner);
	// Moderation state is preserved and the clamped entry is a normal entry.
	assert (restored.comments.HideId (clamped[0].localId) && restored.comments.HiddenCount () == 1);
	// With a clock beyond the stored timestamp nothing is clamped.
	TorrentLocalState futureClock;
	assert (DecodeTorrentLocalState (futureEncoded, now + 6000, futureClock, error) && error.empty ());
	assert (futureClock.comments.Snapshot ().front ().time == now + 5000);
	future.comments.SetEnabled (false);
	// Disabling comments must retain local moderation and privacy settings.
	auto disabled = EncodeTorrentLocalState (future, error);
	assert (DecodeTorrentLocalState (disabled, now + 1, future, error));
	assert (!future.comments.IsEnabled () && future.comments.Snapshot ().size () == 1);

	TorrentLocalState maximum;
	maximum.priorities.assign (MAX_NUM_TORRENT_FILES, 2);
	auto maximalPriorities = EncodeTorrentLocalState (maximum, error);
	assert (!maximalPriorities.empty ());
	assert (DecodeTorrentLocalState (maximalPriorities, now, maximum, error));
	assert (maximum.priorities.size () == MAX_NUM_TORRENT_FILES);
	maximum.priorities.push_back (1);
	assert (EncodeTorrentLocalState (maximum, error).empty () && !error.empty ());
	maximum.priorities = { 3 };
	assert (EncodeTorrentLocalState (maximum, error).empty () && !error.empty ());
	maximum.priorities.clear ();
	maximum.comments.SetEnabled (true);
	comment.time = std::numeric_limits<uint64_t>::max ();
	assert (maximum.comments.Add (comment));
	assert (EncodeTorrentLocalState (maximum, error).empty () && !error.empty ());

	TorrentLocalState empty;
	auto emptyEncoded = EncodeTorrentLocalState (empty, error);
	assert (!emptyEncoded.empty () && error.empty ());
	assert (DecodeTorrentLocalState (emptyEncoded, now, decoded, error));
	assert (!decoded.stopped && decoded.priorities.empty () && !decoded.comments.IsEnabled ());
	assert (decoded.comments.Snapshot ().empty ());
	return 0;
}
#else
int main ()
{
	return 0;
}
#endif
