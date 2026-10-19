/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: bounded local torrent administration state codec.
 * Encodes and atomically decodes versioned stopped/comment/priority
 * snapshots; this module performs no filesystem or network operations.
 */

#ifndef NO_TORRENTS
#include "TorrentsState.h"
#include "Torrents.h"
#include <algorithm>
#include <limits>
#include <utility>

namespace i2p::torrents
{
namespace
{
	bool ValidPriorities (const std::vector<uint8_t> & priorities)
	{
		return priorities.size () <= MAX_NUM_TORRENT_FILES
			&& std::all_of (priorities.begin (), priorities.end (), [] (uint8_t value) { return value <= 2; });
	}

	bool IntegerField (
		const std::map<std::string_view, std::string_view> & fields, std::string_view name, int64_t & value)
	{
		auto found = fields.find (name);
		if (found == fields.end ()) return false;
		auto [parsed, size] = ExtractInteger (found->second);
		if (!size || size != found->second.size ()) return false;
		value = parsed;
		return true;
	}
}

std::string EncodeTorrentLocalState (const TorrentLocalState & state, std::string & error)
{
	error.clear ();
	if (!ValidPriorities (state.priorities))
	{
		error = "invalid local torrent file priorities";
		return { };
	}
	auto comments = EncodeTorrentCommentState (state.comments);
	TorrentCommentStore checkedComments;
	if (comments.empty ()
		|| !DecodeTorrentCommentState (comments, std::numeric_limits<uint64_t>::max (), checkedComments))
	{
		error = "invalid local torrent comments";
		return { };
	}
	std::string priorities;
	priorities.reserve (state.priorities.size ());
	for (auto value : state.priorities) priorities.push_back (static_cast<char> (value));
	// CreateByteString("") is the empty string, and CreateDictionary omits
	// empty values. The format requires the priorities key even when no file
	// override is present, so encode the empty byte string explicitly.
	auto encodedPriorities = priorities.empty () ? std::string ("0:") : CreateByteString (priorities);
	auto encoded = CreateDictionary ({ { "comments", comments }, { "priorities", encodedPriorities },
		{ "stopped", CreateInteger (state.stopped) }, { "version", CreateInteger (1) } });
	if (encoded.size () > MAX_TORRENT_LOCAL_STATE_BYTES)
	{
		error = "local torrent state exceeds size limit";
		return { };
	}
	return encoded;
}

bool DecodeTorrentLocalState (std::string_view encoded, uint64_t now, TorrentLocalState & state, std::string & error)
{
	error.clear ();
	auto fail = [&error] (const char * reason)
	{
		error = reason;
		return false;
	};
	if (encoded.empty () || encoded.size () > MAX_TORRENT_LOCAL_STATE_BYTES)
		return fail ("invalid local torrent state size");
	auto [fields, consumed] = ExtractDictionary (encoded);
	if (!consumed || consumed != encoded.size () || fields.size () != 4 || !fields.count ("comments")
		|| !fields.count ("priorities") || !fields.count ("stopped") || !fields.count ("version"))
		return fail ("invalid local torrent state dictionary");
	int64_t version = 0, stopped = -1;
	if (!IntegerField (fields, "version", version) || version != 1)
		return fail ("unsupported local torrent state version");
	if (!IntegerField (fields, "stopped", stopped) || (stopped != 0 && stopped != 1))
		return fail ("invalid local torrent stopped flag");
	auto [priorities, length] = ExtractByteString (fields.at ("priorities"));
	if (!length || length != fields.at ("priorities").size () || priorities.size () > MAX_NUM_TORRENT_FILES)
		return fail ("invalid local torrent priority count");
	TorrentLocalState candidate;
	candidate.stopped = stopped == 1;
	candidate.priorities.reserve (priorities.size ());
	for (unsigned char value : priorities)
	{
		if (value > 2) return fail ("invalid local torrent file priority");
		candidate.priorities.push_back (value);
	}
	if (!DecodeTorrentCommentState (fields.at ("comments"), now, candidate.comments))
		return fail ("invalid local torrent comment state");
	state = std::move (candidate);
	return true;
}
}
#endif
