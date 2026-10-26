/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TorrentsComments.
 *
 * Encodes and decodes bounded ut_comment request/response messages using
 * peer-wire age timestamps, and maintains the per-torrent store with
 * normalized deduplication, hidden-entry retention, anonymous and
 * pseudonymous visible quotas and rating aggregation. Malformed frames are
 * rejected; an invalid entry is skipped without discarding a valid entry from
 * the same peer reply. The persistence decoder clamps a future timestamp to
 * the local clock rather than discarding the whole state.
 *
 */

#ifndef NO_TORRENTS
#include "TorrentsComments.h"
#include "Torrents.h"
#include <algorithm>
#include <limits>
#include <set>
#include <utility>

namespace i2p::torrents
{
namespace
{
	// Validate UTF-8 and return Java-compatible UTF-16 code units, without
	// accepting surrogate encodings, overlong encodings or terminal controls.
	size_t TextUnits (std::string_view text)
	{
		size_t units = 0;
		for (size_t i = 0; i < text.size ();)
		{
			auto ch = static_cast<unsigned char> (text[i++]);
			uint32_t cp = ch;
			size_t extra = 0;
			if (ch >= 0xC2 && ch <= 0xDF)
			{
				cp = ch & 31;
				extra = 1;
			}
			else if (ch >= 0xE0 && ch <= 0xEF)
			{
				cp = ch & 15;
				extra = 2;
			}
			else if (ch >= 0xF0 && ch <= 0xF4)
			{
				cp = ch & 7;
				extra = 3;
			}
			else if (ch >= 0x80)
				return static_cast<size_t> (-1);
			if (extra > text.size () - i) return static_cast<size_t> (-1);
			for (size_t j = 0; j < extra; ++j)
			{
				auto next = static_cast<unsigned char> (text[i++]);
				if ((next & 0xC0) != 0x80) return static_cast<size_t> (-1);
				cp = (cp << 6) | (next & 63);
			}
			if ((extra == 1 && cp < 0x80) || (extra == 2 && cp < 0x800) || (extra == 3 && cp < 0x10000) || cp > 0x10FFFF
				|| (cp >= 0xD800 && cp <= 0xDFFF) || cp < 0x20 || (cp >= 0x7F && cp <= 0x9F))
				return static_cast<size_t> (-1);
			units += cp >= 0x10000 ? 2 : 1;
			if (units > MAX_COMMENT_TEXT_UNITS * 4) return static_cast<size_t> (-1);
		}
		return units;
	}
	bool ValidText (std::string_view text, size_t maxUnits)
	{
		const auto units = TextUnits (text);
		return units != static_cast<size_t> (-1) && units <= maxUnits;
	}
	bool ValidComment (const TorrentComment & comment)
	{
		return comment.rating <= 5 && ValidText (comment.owner, MAX_COMMENT_OWNER_UNITS)
			&& ValidText (comment.text, MAX_COMMENT_TEXT_UNITS);
	}
	bool IsAsciiWhitespace (char ch)
	{
		return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\v' || ch == '\f';
	}
	// Deduplication key for a stored owner/text. The stored value is never
	// rewritten; only the comparison ignores presentation-only differences, so
	// trailing/leading whitespace, collapsed whitespace runs and zero-width
	// characters cannot be used to replay the same comment past the dedupe and
	// per-owner/anonymous quotas.
	std::string CommentDedupeKey (std::string_view value)
	{
		std::string key;
		key.reserve (value.size ());
		bool pendingSpace = false;
		for (size_t i = 0; i < value.size ();)
		{
			const auto ch = static_cast<unsigned char> (value[i]);
			// U+200B/U+200C/U+200D and U+FEFF are invisible in every renderer and
			// have no legitimate use as a comment separator.
			if (ch == 0xE2 && i + 2 < value.size () && static_cast<unsigned char> (value[i + 1]) == 0x80
				&& (static_cast<unsigned char> (value[i + 2]) == 0x8B
					|| static_cast<unsigned char> (value[i + 2]) == 0x8C
					|| static_cast<unsigned char> (value[i + 2]) == 0x8D))
			{
				i += 3;
				continue;
			}
			if (ch == 0xEF && i + 2 < value.size () && value.compare (i, 3, "\xEF\xBB\xBF") == 0)
			{
				i += 3;
				continue;
			}
			++i;
			if (IsAsciiWhitespace (static_cast<char> (ch)))
			{
				// Trimming is implicit: a space is emitted only once real content
				// follows it, so neither leading nor trailing whitespace reaches
				// the key.
				if (!key.empty ()) pendingSpace = true;
				continue;
			}
			if (pendingSpace)
			{
				key.push_back (' ');
				pendingSpace = false;
			}
			key.push_back (static_cast<char> (ch));
		}
		return key;
	}
	bool Integer (const std::map<std::string_view, std::string_view> & fields, std::string_view key, int64_t & result)
	{
		auto found = fields.find (key);
		if (found == fields.end ()) return false;
		auto [value, len] = ExtractInteger (found->second);
		if (!len || len != found->second.size ()) return false;
		result = value;
		return true;
	}
}

std::optional<TorrentCommentMessage> DecodeTorrentComments (std::string_view encoded, uint64_t now)
{
	if (encoded.size () > MAX_COMMENT_MESSAGE_BYTES) return std::nullopt;
	auto [fields, len] = ExtractDictionary (encoded);
	int64_t type = -1;
	if (!len || len != encoded.size () || !Integer (fields, "msg_type", type)) return std::nullopt;
	TorrentCommentMessage message;
	if (type == 0)
	{
		message.request = true;
		int64_t count = MAX_COMMENT_MESSAGE_COUNT;
		if (fields.count ("num") && !Integer (fields, "num", count)) return std::nullopt;
		if (count < 0) return std::nullopt;
		// A Java peer may request up to CommentSet.MAX_SIZE (256); the reply
		// encoder enforces the 16 KiB wire budget independently.
		message.requested = std::min<uint64_t> (count, MAX_COMMENT_COUNT);
		if (auto it = fields.find ("filter"); it != fields.end ())
		{
			// The filter is an optimisation hint for the requester's own cache
			// and is not authenticated. Java sends 64 zero bytes, and a peer may
			// send any 0..64-byte hint; content is ignored either way. Only a
			// hint longer than the documented size or malformed bencode makes the
			// request itself invalid.
			auto [filter, n] = ExtractByteString (it->second);
			if (!n || n != it->second.size () || filter.size () > 64) return std::nullopt;
		}
		return message;
	}
	if (type != 1 || !fields.count ("comments")) return std::nullopt;
	auto list = fields.at ("comments");
	auto consumed = ParseList (list,
		[&] (std::string_view entry)
		{
			auto [values, size] = ExtractDictionary (entry);
			if (!size) return size; // structural: stop the list
			// Java may return up to MAX_COMMENT_COUNT entries and can include
			// a legacy entry that native considers invalid. Ignore that entry
			// rather than discarding every valid comment in the reply.
			if (message.comments.size () >= MAX_COMMENT_COUNT) return size;
			TorrentComment comment;
			int64_t rating = -1, age = -1;
			if (!Integer (values, "like", rating) || !Integer (values, "timestamp", age)) return size;
			for (auto key : { "owner", "text" })
			{
				auto it = values.find (key);
				if (it == values.end ()) return size;
				auto [text, n] = ExtractByteString (it->second);
				if (!n || n != it->second.size ()) return size;
				std::string & target = key == std::string_view ("owner") ? comment.owner : comment.text;
				target.assign (text);
				// Java normalizes CR/LF/TAB in comments to a single space. TAB is
				// a text separator in Java's CommentSet and a terminal control
				// here, so it is sanitized before validation. Other C0/DEL/C1
				// controls stay rejected by ValidText.
				for (char & ch : target)
					if (ch == '\r' || ch == '\n' || ch == '\t') ch = ' ';
			}
			// Java clamps ratings to 0..5 and negative ages to "now".
			comment.rating = static_cast<uint8_t> (std::clamp<int64_t> (rating, 0, 5));
			age = std::clamp<int64_t> (age, 0, std::numeric_limits<int32_t>::max ());
			comment.time = static_cast<uint64_t> (age) > now ? 0 : now - age;
			if (ValidComment (comment)) message.comments.push_back (std::move (comment));
			return size;
		});
	if (!consumed || consumed != list.size ()) return std::nullopt;
	return message;
}

std::string EncodeTorrentCommentRequest (size_t count)
{
	return CreateDictionary ({ { "filter", CreateByteString (std::string (64, '\0')) },
		{ "msg_type", CreateInteger (0) }, { "num", CreateInteger (std::min (count, MAX_COMMENT_MESSAGE_COUNT)) } });
}

std::string EncodeTorrentComments (const std::vector<TorrentComment> & comments, uint64_t now, size_t count)
{
	std::vector<const TorrentComment *> selected;
	selected.reserve (comments.size ());
	for (const auto & comment : comments)
		if (!comment.hidden && ValidComment (comment)) selected.push_back (&comment);
	// Java publishes the newest N, not the first N inserted.
	std::sort (selected.begin (), selected.end (),
		[] (const TorrentComment * left, const TorrentComment * right)
		{
			if (left->time != right->time) return left->time > right->time;
			return left->localId > right->localId;
		});
	count = std::min (count, MAX_COMMENT_COUNT);
	if (selected.size () > count) selected.resize (count);
	std::vector<std::string> entries;
	entries.reserve (selected.size ());
	for (const auto * comment : selected)
	{
		auto age = now > comment->time ? now - comment->time : 0;
		age = std::min<uint64_t> (age, std::numeric_limits<int32_t>::max ());
		entries.push_back (CreateDictionary (
			{ { "like", CreateInteger (comment->rating) }, { "owner", CreateByteString (comment->owner) },
				{ "text", CreateByteString (comment->text) }, { "timestamp", CreateInteger (age) } }));
	}
	auto encode = [&] ()
	{ return CreateDictionary ({ { "comments", CreateList (entries) }, { "msg_type", CreateInteger (1) } }); };
	std::string encoded = encode ();
	// Java disconnects on an extension payload above 16482 bytes. Drop the
	// oldest selected entries until the frame fits rather than sending a
	// message the peer will reject.
	while (encoded.size () > MAX_COMMENT_MESSAGE_BYTES && !entries.empty ())
	{
		entries.pop_back ();
		encoded = encode ();
	}
	return encoded;
}

bool TorrentCommentStore::IsDuplicate (const TorrentComment & comment) const
{
	const auto owner = CommentDedupeKey (comment.owner);
	const auto text = CommentDedupeKey (comment.text);
	for (const auto & existing : m_Comments)
		if (CommentDedupeKey (existing.owner) == owner && CommentDedupeKey (existing.text) == text) return true;
	return false;
}

bool TorrentCommentStore::Add (TorrentComment comment)
{
	if (!m_Enabled || !ValidComment (comment)
		|| m_NextId >= static_cast<uint64_t> (std::numeric_limits<int64_t>::max ()))
		return false;
	// Content deduplication is independent of the rating and compares a
	// normalized key, so the same pseudonymous text cannot be replayed with
	// ratings 0..5 or with whitespace/zero-width padding. Hidden entries stay
	// in the comparison; only local moderation can release a text again.
	if (IsDuplicate (comment)) return false;
	const auto textUnits = TextUnits (comment.text);
	if (textUnits == static_cast<size_t> (-1)) return false;
	// Bound one pseudonym's share of the visible store, and give anonymous
	// entries their own smaller budget: an empty owner has no stable label, so
	// it must not escape the per-pseudonym quota. Hidden entries remain for
	// dedupe but do not count toward either budget.
	if (comment.owner.empty ())
	{
		size_t anonymousEntries = 0;
		for (const auto & existing : m_Comments)
			if (!existing.hidden && existing.owner.empty ()
				&& ++anonymousEntries >= MAX_COMMENT_ANONYMOUS_VISIBLE)
				return false;
	}
	else
	{
		size_t ownerEntries = 0;
		for (const auto & existing : m_Comments)
			if (!existing.hidden && existing.owner == comment.owner && ++ownerEntries >= MAX_COMMENT_PER_OWNER)
				return false;
	}
	// Evict the oldest foreign entry rather than becoming permanently full.
	// Local and hidden entries are never victims: moderation state and the
	// operator's own comments outlive peer-supplied spam.
	while (m_Comments.size () >= MAX_COMMENT_COUNT || m_TextUnits > MAX_COMMENT_TEXT_TOTAL_UNITS
		|| textUnits > MAX_COMMENT_TEXT_TOTAL_UNITS - m_TextUnits)
	{
		auto victim = m_Comments.end ();
		for (auto it = m_Comments.begin (); it != m_Comments.end (); ++it)
			if (!it->local && !it->hidden)
			{
				victim = it;
				break;
			}
		if (victim == m_Comments.end ()) return false;
		m_TextUnits -= std::min (m_TextUnits, TextUnits (victim->text));
		m_Comments.erase (victim);
	}
	comment.hidden = false;
	comment.localId = m_NextId++;
	m_TextUnits += textUnits;
	m_Comments.push_back (std::move (comment));
	TrimHidden ();
	++m_Revision;
	return true;
}

bool TorrentCommentStore::Hide (size_t localIndex)
{
	if (localIndex >= m_Comments.size ()) return false;
	bool changed = false;
	if (!m_Comments[localIndex].hidden)
	{
		const auto units = TextUnits (m_Comments[localIndex].text);
		if (units != static_cast<size_t> (-1)) m_TextUnits -= std::min (m_TextUnits, units);
		changed = true;
	}
	m_Comments[localIndex].hidden = true;
	// Trimming can drop another hidden entry, which is a content change even
	// when this entry was already hidden.
	if (TrimHidden ()) changed = true;
	if (changed) ++m_Revision;
	return true;
}

bool TorrentCommentStore::HideId (uint64_t id)
{
	for (auto & comment : m_Comments)
		if (comment.localId == id)
		{
			bool changed = false;
			if (!comment.hidden)
			{
				const auto units = TextUnits (comment.text);
				if (units != static_cast<size_t> (-1)) m_TextUnits -= std::min (m_TextUnits, units);
				changed = true;
			}
			comment.hidden = true;
			if (TrimHidden ()) changed = true;
			if (changed) ++m_Revision;
			return true;
		}
	return false;
}

bool TorrentCommentStore::UnhideId (uint64_t id)
{
	for (auto & comment : m_Comments)
		if (comment.localId == id)
		{
			if (comment.hidden)
			{
				const auto units = TextUnits (comment.text);
				// Hidden text is excluded from the visible budget. Refuse an
				// unhide that would make the persisted state invalid instead
				// of wedging every later settings write.
				if (units == static_cast<size_t> (-1) || m_TextUnits > MAX_COMMENT_TEXT_TOTAL_UNITS
					|| units > MAX_COMMENT_TEXT_TOTAL_UNITS - m_TextUnits)
					return false;
				comment.hidden = false;
				m_TextUnits += units;
				++m_Revision;
			}
			return true;
		}
	return false;
}

bool TorrentCommentStore::Restore (bool enabled, const std::vector<TorrentComment> & comments)
{
	if (comments.size () > MAX_COMMENT_COUNT) return false;
	size_t units = 0;
	uint64_t next = 1;
	std::set<uint64_t> ids;
	for (const auto & comment : comments)
	{
		const auto textUnits = TextUnits (comment.text);
		if (!ValidComment (comment)
			|| (!comment.hidden
				&& (units > MAX_COMMENT_TEXT_TOTAL_UNITS || textUnits > MAX_COMMENT_TEXT_TOTAL_UNITS - units))
			|| !comment.localId || comment.localId >= static_cast<uint64_t> (std::numeric_limits<int64_t>::max ())
			|| !ids.insert (comment.localId).second)
			return false;
		if (!comment.hidden) units += textUnits;
		next = std::max (next, comment.localId + 1);
	}
	m_Comments = comments;
	m_TextUnits = units;
	m_NextId = next;
	m_Enabled = enabled;
	TrimHidden ();
	++m_Revision;
	return true;
}

bool TorrentCommentStore::TrimHidden ()
{
	size_t hidden = 0;
	for (const auto & comment : m_Comments)
		if (comment.hidden) ++hidden;
	bool trimmed = false;
	while (hidden > MAX_COMMENT_HIDDEN_COUNT)
	{
		auto victim = m_Comments.end ();
		for (auto it = m_Comments.begin (); it != m_Comments.end (); ++it)
			if (it->hidden)
			{
				victim = it;
				break;
			}
		if (victim == m_Comments.end ()) break;
		m_Comments.erase (victim);
		--hidden;
		trimmed = true;
	}
	return trimmed;
}

size_t TorrentCommentStore::VisibleCount () const
{
	size_t count = 0;
	for (const auto & comment : m_Comments)
		if (!comment.hidden) ++count;
	return count;
}

size_t TorrentCommentStore::HiddenCount () const
{
	size_t count = 0;
	for (const auto & comment : m_Comments)
		if (comment.hidden) ++count;
	return count;
}

size_t TorrentCommentStore::LocalCount () const
{
	size_t count = 0;
	for (const auto & comment : m_Comments)
		if (comment.local) ++count;
	return count;
}

namespace
{
	// A pseudonym cannot inflate the aggregate by posting many rated texts.
	// Anonymous entries have no stable label and each count separately.
	template <typename Callback> void ForEachRating (const std::vector<TorrentComment> & comments, Callback callback)
	{
		std::set<std::string> seenOwners;
		for (const auto & comment : comments)
			if (!comment.hidden && comment.rating)
			{
				if (!comment.owner.empty () && !seenOwners.insert (comment.owner).second) continue;
				callback (comment.rating);
			}
	}
}

size_t TorrentCommentStore::RatingCount () const
{
	size_t count = 0;
	if (m_Enabled) ForEachRating (m_Comments, [&count] (uint8_t) { ++count; });
	return count;
}

double TorrentCommentStore::AverageRating () const
{
	size_t total = 0, count = 0;
	if (m_Enabled)
		ForEachRating (m_Comments,
			[&total, &count] (uint8_t rating)
			{
				total += rating;
				++count;
			});
	return count ? static_cast<double> (total) / count : 0;
}

std::string EncodeTorrentCommentState (const TorrentCommentStore & store)
{
	std::vector<std::string> entries;
	for (const auto & comment : store.Snapshot ())
	{
		if (comment.time > static_cast<uint64_t> (std::numeric_limits<int64_t>::max ())) return { };
		entries.push_back (CreateDictionary ({ { "hidden", CreateInteger (comment.hidden) },
			{ "id", CreateInteger (comment.localId) }, { "like", CreateInteger (comment.rating) },
			{ "local", CreateInteger (comment.local) }, { "owner", CreateByteString (comment.owner) },
			{ "text", CreateByteString (comment.text) }, { "time", CreateInteger (comment.time) } }));
	}
	return CreateDictionary ({ { "comments", CreateList (entries) }, { "enabled", CreateInteger (store.IsEnabled ()) },
		{ "version", CreateInteger (1) } });
}

bool DecodeTorrentCommentState (std::string_view encoded, uint64_t now, TorrentCommentStore & store)
{
	if (encoded.size () > 128 * 1024) return false;
	auto [fields, size] = ExtractDictionary (encoded);
	int64_t version = 0, enabled = -1;
	if (!size || size != encoded.size () || !Integer (fields, "version", version) || version != 1
		|| !Integer (fields, "enabled", enabled) || (enabled != 0 && enabled != 1) || !fields.count ("comments"))
		return false;
	std::vector<TorrentComment> comments;
	bool valid = true;
	auto list = fields.at ("comments");
	auto consumed = ParseList (list,
		[&] (std::string_view entry)
		{
			auto [values, len] = ExtractDictionary (entry);
			if (!len || comments.size () >= MAX_COMMENT_COUNT)
			{
				valid = false;
				return len;
			}
			int64_t id, rating, local, hidden, time;
			if (!Integer (values, "id", id) || id <= 0 || !Integer (values, "like", rating) || rating < 0 || rating > 5
				|| !Integer (values, "local", local) || (local != 0 && local != 1)
				|| !Integer (values, "hidden", hidden) || (hidden != 0 && hidden != 1)
				|| !Integer (values, "time", time) || time < 0)
			{
				valid = false;
				return len;
			}
			TorrentComment comment;
			comment.localId = id;
			comment.rating = rating;
			comment.local = local;
			comment.hidden = hidden;
			// A timestamp ahead of the local clock (restored backup, backwards
			// clock) is clamped to `now` instead of rejecting the whole state:
			// losing every comment, priority and the stopped flag because of one
			// future stamp would silently resume a paused torrent.
			comment.time = static_cast<uint64_t> (time) > now ? now : static_cast<uint64_t> (time);
			for (auto key : { "owner", "text" })
			{
				auto it = values.find (key);
				if (it == values.end ())
				{
					valid = false;
					return len;
				}
				auto [text, n] = ExtractByteString (it->second);
				if (!n || n != it->second.size ())
				{
					valid = false;
					return len;
				}
				(key == std::string_view ("owner") ? comment.owner : comment.text).assign (text);
			}
			comments.push_back (std::move (comment));
			return len;
		});
	return valid && consumed == list.size () && consumed && store.Restore (enabled, comments);
}

std::vector<TorrentComment> TorrentCommentStore::Visible () const
{
	std::vector<TorrentComment> result;
	if (m_Enabled)
		for (const auto & comment : m_Comments)
			if (!comment.hidden) result.push_back (comment);
	return result;
}
}
#endif
