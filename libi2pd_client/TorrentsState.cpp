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
#include "TorrentsControl.h"
#include "HTTP.h"
#include <algorithm>
#include <limits>
#include <unordered_set>
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

std::string EncodeTorrentMagnetState (const TorrentMagnetState & state, std::string & error)
{
	error.clear ();
	if (state.name.empty () || !IsValidTorrentMetaText (state.name, MAX_CONTROL_ARGUMENT)
		|| std::any_of (state.name.begin (), state.name.end (), [] (unsigned char c) { return c < 32 || c == 127; })
		|| state.trackers.size () > MAX_CONTROL_TRACKERS)
	{
		error = "invalid magnet display name or tracker count";
		return { };
	}
	std::vector<std::string> trackers;
	std::unordered_set<std::string> unique;
	for (const auto & hint : state.trackers)
	{
		i2p::http::URL url (hint);
		if (hint.empty () || hint.size () > MAX_CONTROL_ARGUMENT
			|| std::any_of (hint.begin (), hint.end (), [] (unsigned char c) { return c <= 32 || c == 127; })
			|| (url.schema != "http" && url.schema != "udp") || !IsI2PTrackerHost (url.host) || !url.user.empty ()
			|| !url.pass.empty () || url.ipv6 || !unique.insert (url.to_string ()).second)
		{
			error = "invalid magnet tracker hint";
			return { };
		}
		trackers.push_back (CreateByteString (hint));
	}
	auto encoded = CreateDictionary ({ { "hash", CreateByteString (ToHexString (state.infoHash)) },
		{ "name", CreateByteString (state.name) }, { "stopped", CreateInteger (state.stopped) },
		{ "trackers", CreateList (trackers) }, { "version", CreateInteger (1) } });
	if (encoded.size () > MAX_TORRENT_MAGNET_STATE_BYTES)
	{
		error = "magnet state exceeds size limit";
		return { };
	}
	return encoded;
}

bool DecodeTorrentMagnetState (std::string_view encoded, TorrentMagnetState & state, std::string & error)
{
	error.clear ();
	auto fail = [&error] ()
	{
		error = "invalid or unsupported magnet state";
		return false;
	};
	if (encoded.empty () || encoded.size () > MAX_TORRENT_MAGNET_STATE_BYTES) return fail ();
	auto [fields, consumed] = ExtractDictionary (encoded);
	if (consumed != encoded.size () || fields.size () != 5 || !fields.contains ("hash") || !fields.contains ("name")
		|| !fields.contains ("trackers") || !fields.contains ("stopped") || !fields.contains ("version"))
		return fail ();
	int64_t version = 0, stopped = -1;
	if (!IntegerField (fields, "version", version) || version != 1 || !IntegerField (fields, "stopped", stopped)
		|| (stopped != 0 && stopped != 1))
		return fail ();
	auto [hash, hashLength] = ExtractByteString (fields.at ("hash"));
	auto [name, nameLength] = ExtractByteString (fields.at ("name"));
	auto [trackers, trackersLength] = ParseStringList (fields.at ("trackers"));
	if (!hashLength || hashLength != fields.at ("hash").size () || hash.size () != 40 || !nameLength
		|| nameLength != fields.at ("name").size () || !trackersLength
		|| trackersLength != fields.at ("trackers").size ())
		return fail ();
	TorrentMagnetState candidate;
	const auto hex = [] (char c) -> int
	{
		return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
	};
	for (size_t i = 0; i < candidate.infoHash.size (); ++i)
	{
		const int high = hex (hash[2 * i]), low = hex (hash[2 * i + 1]);
		if (high < 0 || low < 0) return fail ();
		candidate.infoHash[i] = static_cast<uint8_t> (high * 16 + low);
	}
	candidate.name = name;
	candidate.stopped = stopped == 1;
	for (auto tracker : trackers) candidate.trackers.emplace_back (tracker);
	if (EncodeTorrentMagnetState (candidate, error).empty ()) return false;
	state = std::move (candidate);
	return true;
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
	// The local format requires the priorities key, including an explicit
	// zero-length byte string when no overrides are present.
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
