/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: local torrent state codec regressions.
 * Covers complete-input validation, version and resource bounds,
 * comment moderation, binary priorities and failed-decode atomicity.
 */

#include <cassert>
#include <limits>
#include <string>
#include <utility>
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

	TorrentLocalState future;
	future.comments.SetEnabled (true);
	comment.time = now + 1;
	assert (future.comments.Add (comment));
	rejectWithoutChanges (Envelope (EncodeTorrentCommentState (future.comments), defaults));
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
