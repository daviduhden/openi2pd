/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TorrentsComments.
 *
 * Encodes and decodes bounded ut_comment request/response messages using
 * peer-wire age timestamps, and maintains the per-torrent store with
 * deduplication, hidden-entry retention and rating aggregation. Malformed
 * counts, ratings, sizes and text are rejected before state changes.
 *
 */

#ifndef NO_TORRENTS
#include "TorrentsComments.h"
#include "Torrents.h"
#include <algorithm>
#include <limits>
#include <set>

namespace i2p::torrents
{
namespace
{
	// Validate UTF-8 and limit Java-compatible UTF-16 code units, without
	// accepting surrogate encodings, overlong encodings or terminal controls.
	bool ValidText (std::string_view text, size_t maxUnits)
	{
		if (text.size () > maxUnits * 4) return false;
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
				return false;
			if (extra > text.size () - i) return false;
			for (size_t j = 0; j < extra; ++j)
			{
				auto next = static_cast<unsigned char> (text[i++]);
				if ((next & 0xC0) != 0x80) return false;
				cp = (cp << 6) | (next & 63);
			}
			if ((extra == 1 && cp < 0x80) || (extra == 2 && cp < 0x800) || (extra == 3 && cp < 0x10000) || cp > 0x10FFFF
				|| (cp >= 0xD800 && cp <= 0xDFFF) || cp < 0x20 || (cp >= 0x7F && cp <= 0x9F))
				return false;
			units += cp >= 0x10000 ? 2 : 1;
			if (units > maxUnits) return false;
		}
		return true;
	}
	bool ValidComment (const TorrentComment & comment)
	{
		return comment.rating <= 5 && ValidText (comment.owner, 32) && ValidText (comment.text, 512);
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
		message.requested = std::min<uint64_t> (count, MAX_COMMENT_MESSAGE_COUNT);
		if (auto it = fields.find ("filter"); it != fields.end ())
		{
			auto [filter, n] = ExtractByteString (it->second);
			if (!n || n != it->second.size () || filter.size () != 64) return std::nullopt;
		}
		return message;
	}
	if (type != 1 || !fields.count ("comments")) return std::nullopt;
	bool valid = true;
	auto list = fields.at ("comments");
	auto consumed = ParseList (list,
		[&] (std::string_view entry)
		{
			auto [values, size] = ExtractDictionary (entry);
			if (!size || message.comments.size () >= MAX_COMMENT_MESSAGE_COUNT)
			{
				valid = false;
				return size;
			}
			TorrentComment comment;
			int64_t rating = -1, age = -1;
			if (!Integer (values, "like", rating) || rating < 0 || rating > 5 || !Integer (values, "timestamp", age)
				|| age < 0 || age > std::numeric_limits<int32_t>::max ())
			{
				valid = false;
				return size;
			}
			for (auto key : { "owner", "text" })
			{
				auto it = values.find (key);
				if (it == values.end ())
				{
					valid = false;
					return size;
				}
				auto [text, n] = ExtractByteString (it->second);
				if (!n || n != it->second.size ())
				{
					valid = false;
					return size;
				}
				(key == std::string_view ("owner") ? comment.owner : comment.text).assign (text);
			}
			comment.rating = static_cast<uint8_t> (rating);
			comment.time = static_cast<uint64_t> (age) > now ? 0 : now - age;
			if (!ValidComment (comment))
				valid = false;
			else
				message.comments.push_back (std::move (comment));
			return size;
		});
	if (!valid || !consumed || consumed != list.size ()) return std::nullopt;
	return message;
}

std::string EncodeTorrentCommentRequest (size_t count)
{
	return CreateDictionary ({ { "filter", CreateByteString (std::string (64, '\0')) },
		{ "msg_type", CreateInteger (0) }, { "num", CreateInteger (std::min (count, MAX_COMMENT_MESSAGE_COUNT)) } });
}

std::string EncodeTorrentComments (const std::vector<TorrentComment> & comments, uint64_t now, size_t count)
{
	std::vector<std::string> entries;
	count = std::min (count, MAX_COMMENT_MESSAGE_COUNT);
	for (const auto & comment : comments)
	{
		if (entries.size () >= count) break;
		if (comment.hidden || !ValidComment (comment)) continue;
		auto age = now > comment.time ? now - comment.time : 0;
		age = std::min<uint64_t> (age, std::numeric_limits<int32_t>::max ());
		entries.push_back (CreateDictionary (
			{ { "like", CreateInteger (comment.rating) }, { "owner", CreateByteString (comment.owner) },
				{ "text", CreateByteString (comment.text) }, { "timestamp", CreateInteger (age) } }));
	}
	return CreateDictionary ({ { "comments", CreateList (entries) }, { "msg_type", CreateInteger (1) } });
}

bool TorrentCommentStore::Add (TorrentComment comment)
{
	if (!m_Enabled || !ValidComment (comment)
		|| m_NextId >= static_cast<uint64_t> (std::numeric_limits<int64_t>::max ())
		|| m_Comments.size () >= MAX_COMMENT_COUNT || comment.text.size () > 4096 - m_TextBytes)
		return false;
	for (const auto & existing : m_Comments)
	{
		auto delta = existing.time > comment.time ? existing.time - comment.time : comment.time - existing.time;
		if (delta <= 4 * 3600 && existing.owner == comment.owner && existing.text == comment.text
			&& existing.rating == comment.rating)
			return false;
	}
	comment.hidden = false;
	comment.localId = m_NextId++;
	m_TextBytes += comment.text.size ();
	m_Comments.push_back (std::move (comment));
	++m_Revision;
	return true;
}

bool TorrentCommentStore::Hide (size_t localIndex)
{
	if (localIndex >= m_Comments.size ()) return false;
	if (!m_Comments[localIndex].hidden) ++m_Revision;
	m_Comments[localIndex].hidden = true;
	return true;
}

bool TorrentCommentStore::HideId (uint64_t id)
{
	for (auto & comment : m_Comments)
		if (comment.localId == id)
		{
			if (!comment.hidden) ++m_Revision;
			comment.hidden = true;
			return true;
		}
	return false;
}

bool TorrentCommentStore::Restore (bool enabled, const std::vector<TorrentComment> & comments)
{
	if (comments.size () > MAX_COMMENT_COUNT) return false;
	size_t bytes = 0;
	uint64_t next = 1;
	std::set<uint64_t> ids;
	for (const auto & comment : comments)
	{
		if (!ValidComment (comment) || comment.text.size () > 4096 - bytes || !comment.localId
			|| comment.localId >= static_cast<uint64_t> (std::numeric_limits<int64_t>::max ())
			|| !ids.insert (comment.localId).second)
			return false;
		bytes += comment.text.size ();
		next = std::max (next, comment.localId + 1);
	}
	m_Comments = comments;
	m_TextBytes = bytes;
	m_NextId = next;
	m_Enabled = enabled;
	++m_Revision;
	return true;
}

size_t TorrentCommentStore::RatingCount () const
{
	size_t count = 0;
	if (m_Enabled)
		for (const auto & comment : m_Comments)
			if (!comment.hidden && comment.rating) ++count;
	return count;
}

double TorrentCommentStore::AverageRating () const
{
	size_t total = 0, count = 0;
	if (m_Enabled)
		for (const auto & comment : m_Comments)
			if (!comment.hidden && comment.rating)
			{
				total += comment.rating;
				++count;
			}
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
				|| !Integer (values, "time", time) || time < 0 || static_cast<uint64_t> (time) > now)
			{
				valid = false;
				return len;
			}
			TorrentComment comment;
			comment.localId = id;
			comment.rating = rating;
			comment.local = local;
			comment.hidden = hidden;
			comment.time = time;
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
