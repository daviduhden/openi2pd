/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test torrents comments.
 *
 * Checks the I2P torrent comment wire codec: request/response fields,
 * relative
 * timestamps, size and count limits, trailing data, filter hints, TAB
 * normalization and rejection or filtering of control and overlong
 * UTF-8
 * text; plus the store's normalized deduplication,
 * anonymous/pseudonymous
 * quotas, eviction, revision semantics and the fixed admission windows.
 *
 */

#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "TorrentsComments.h"
#include "Torrents.h"
#ifndef NO_TORRENTS
namespace
{
using namespace i2p::torrents;

// Minimal valid single-file torrent used for the per-torrent admission
// budget. No stream, tunnel, timer or network is created.
std::shared_ptr<Torrent> MakeTorrent ()
{
	std::string bytes (MIN_PIECE_LENGTH, 'a');
	Torrent::InfoHash hash;
	SHA1 (reinterpret_cast<const uint8_t *> (bytes.data ()), bytes.size (), hash.data ());
	auto torrent = Torrent::CreateV1 ("comments", { { "", MIN_PIECE_LENGTH } }, MIN_PIECE_LENGTH, { hash });
	assert (torrent);
	return torrent;
}

// One reply-style list entry, so decoder-level normalization can be
// pinned without going through the encoder (which filters invalid
// entries).
std::string CommentReply (std::string_view owner, std::string_view text)
{
	auto entry = CreateDictionary ({ { "like", CreateInteger (0) }, { "owner", CreateByteString (owner) },
		{ "text", CreateByteString (text) }, { "timestamp", CreateInteger (0) } });
	return CreateDictionary ({ { "comments", CreateList ({ entry }) }, { "msg_type", CreateInteger (1) } });
}
}

int main ()
{
	using namespace i2p::torrents;
	constexpr uint64_t now = 1800000000;
	auto request = DecodeTorrentComments (EncodeTorrentCommentRequest (10000), now);
	assert (request && request->request && request->requested == MAX_COMMENT_MESSAGE_COUNT);
	TorrentComment original { "pseudonym", "Useful torrent", 4, now - 123, false };
	auto encoded = EncodeTorrentComments ({ original }, now);
	auto decoded = DecodeTorrentComments (encoded, now);
	assert (decoded && !decoded->request && decoded->comments.size () == 1);
	assert (decoded->comments[0].time == original.time);
	assert (decoded->comments[0].owner == original.owner && decoded->comments[0].rating == 4);
	// timestamp is an age, not an epoch. Missing required response fields
	// fail.
	assert (!DecodeTorrentComments (CreateDictionary ({ { "msg_type", CreateInteger (1) } }), now));
	assert (!DecodeTorrentComments (encoded + "garbage", now));
	assert (!DecodeTorrentComments (std::string (MAX_COMMENT_MESSAGE_BYTES + 1, 'a'), now));
	// The filter is an ignored optimisation hint: Java sends 64 zero bytes
	// and any 0..64-byte hint is accepted. It never changes the reply, and
	// only an over-long hint or malformed bencode invalidates the request.
	auto requestWithFilter = [] (std::string_view filter)
	{
		return CreateDictionary ({ { "filter", CreateByteString (filter) }, { "msg_type", CreateInteger (0) },
			{ "num", CreateInteger (MAX_COMMENT_MESSAGE_COUNT) } });
	};
	auto shortFilter = DecodeTorrentComments (requestWithFilter ("short"), now);
	assert (shortFilter && shortFilter->request && shortFilter->requested == MAX_COMMENT_MESSAGE_COUNT);
	assert (DecodeTorrentComments (requestWithFilter (std::string ()), now));
	assert (DecodeTorrentComments (requestWithFilter (std::string (64, 'f')), now));
	assert (!DecodeTorrentComments (requestWithFilter (std::string (65, 'f')), now));
	assert (!DecodeTorrentComments (requestWithFilter (std::string (MAX_COMMENT_COUNT, 'f')), now));
	assert (!DecodeTorrentComments (
		CreateDictionary ({ { "filter", CreateInteger (5) }, { "msg_type", CreateInteger (0) } }), now));
	auto invalid = original;
	invalid.text = "\033[31mterminal";
	assert (DecodeTorrentComments (EncodeTorrentComments ({ invalid }, now), now)->comments.empty ());
	invalid.text = std::string ("\xC0\xAF", 2); // overlong UTF-8
	assert (DecodeTorrentComments (EncodeTorrentComments ({ invalid }, now), now)->comments.empty ());
	// Independent Java wire field vector: timestamp 10 means age ten
	// seconds.
	auto javaFields = std::string ("d8:commentsld4:likei5e5:owner1:x4:text1:y9:timestampi10eee8:msg_typei1ee");
	auto javaReply = DecodeTorrentComments (javaFields, now);
	assert (javaReply && javaReply->comments.size () == 1);
	assert (javaReply->comments[0].time == now - 10 && javaReply->comments[0].rating == 5);
	invalid = original;
	invalid.owner = std::string (33, 'x');
	assert (DecodeTorrentComments (EncodeTorrentComments ({ invalid }, now), now)->comments.empty ());
	invalid = original;
	invalid.text = std::string (513, 'x');
	assert (DecodeTorrentComments (EncodeTorrentComments ({ invalid }, now), now)->comments.empty ());

	TorrentComment anonymousRating { "", "", 5, now, false };
	auto ratingOnly = DecodeTorrentComments (EncodeTorrentComments ({ anonymousRating }, now), now);
	assert (ratingOnly && ratingOnly->comments.size () == 1 && ratingOnly->comments.front ().owner.empty ());

	TorrentCommentStore store;
	assert (!store.IsEnabled () && !store.Add (original));
	store.SetEnabled (true);
	assert (store.Add (original));
	assert (!store.Add (original));
	assert (store.Hide (0) && store.Visible ().empty ());
	assert (!store.Add (original)); // hidden spam cannot immediately reappear
	assert (!store.Hide (10));
	assert (store.Snapshot ().size () == 1 && store.Snapshot ()[0].localId != 0);
	const auto hiddenId = store.Snapshot ()[0].localId;
	assert (store.UnhideId (hiddenId) && store.Visible ().size () == 1);
	assert (store.HideId (hiddenId) && store.Visible ().empty ());
	assert (store.UnhideId (hiddenId) && store.Visible ().size () == 1);
	assert (store.HideId (hiddenId)); // state fixture below expects a hidden entry
	auto state = EncodeTorrentCommentState (store);
	TorrentCommentStore restored;
	assert (DecodeTorrentCommentState (state, now, restored));
	assert (restored.IsEnabled () && restored.Visible ().empty ());
	assert (restored.Snapshot ()[0].hidden);
	assert (!restored.Add (original));
	auto local = original;
	local.owner = "other";
	local.local = true;
	assert (restored.Add (local));
	assert (restored.RatingCount () == 1 && restored.AverageRating () == 4.0);
	auto localId = restored.Visible ()[0].localId;
	assert (restored.HideId (localId));
	assert (restored.RatingCount () == 0 && restored.AverageRating () == 0.0);
	assert (!restored.HideId (9999));
	auto revision = restored.Revision ();
	assert (!DecodeTorrentCommentState (state + "trailing", now, restored));
	assert (restored.Revision () == revision);

	// Invalid entries are skipped without discarding the valid ones, and
	// Java's larger reply count is accepted.
	TorrentComment good { "good", "keep me", 2, now, false };
	const auto badEntry = CreateDictionary ({ { "like", CreateInteger (0) }, { "owner", CreateByteString ("bad") },
		{ "text", CreateByteString ("\x1b") }, { "timestamp", CreateInteger (0) } });
	const auto goodEntry = CreateDictionary ({ { "like", CreateInteger (2) }, { "owner", CreateByteString ("good") },
		{ "text", CreateByteString ("keep me") }, { "timestamp", CreateInteger (0) } });
	auto mixedReply = CreateDictionary (
		{ { "comments", CreateList ({ badEntry, goodEntry }) }, { "msg_type", CreateInteger (1) } });
	auto mixed = DecodeTorrentComments (mixedReply, now);
	assert (mixed && mixed->comments.size () == 1 && mixed->comments[0].owner == good.owner);

	// Java separates comment fields with TAB. Native maps CR, LF and TAB
	// to one
	// space before validation, so a Java-style reply stays usable; every
	// other
	// C0/DEL/C1 control remains rejected for terminal safety.
	assert (DecodeTorrentComments (CommentReply ("plain", "plain text"), now)->comments.size () == 1);
	auto tabbed = DecodeTorrentComments (CommentReply ("a\tb\t", "x\ty\tz"), now);
	assert (tabbed && tabbed->comments.size () == 1);
	assert (tabbed->comments[0].owner == "a b " && tabbed->comments[0].text == "x y z");
	auto crlf = DecodeTorrentComments (CommentReply ("a\rb", "x\n\r y"), now);
	assert (crlf && crlf->comments.size () == 1 && crlf->comments[0].text == "x   y");
	assert (DecodeTorrentComments (CommentReply ("negative", "bad \x01"), now)->comments.empty ());
	assert (DecodeTorrentComments (CommentReply ("negative", "bad \x7F"), now)->comments.empty ());
	assert (DecodeTorrentComments (CommentReply ("negative", "bad \xC2\x9B"), now)->comments.empty ());
	assert (DecodeTorrentComments (CommentReply ("negative\xC2\x9B", "text"), now)->comments.empty ());

	// The same pseudonym/text cannot inflate ratings by replaying 0..5.
	TorrentCommentStore rated;
	rated.SetEnabled (true);
	auto firstRating = original;
	firstRating.owner = "same";
	firstRating.text = "same text";
	firstRating.rating = 1;
	assert (rated.Add (firstRating));
	firstRating.rating = 5;
	assert (!rated.Add (firstRating));

	// Newest comments are published first, even if older entries fill the
	// store.
	TorrentCommentStore newest;
	newest.SetEnabled (true);
	for (size_t i = 0; i < 25; ++i)
	{
		TorrentComment c;
		c.owner = "owner" + std::to_string (i);
		c.text = "text" + std::to_string (i);
		c.rating = 1;
		c.time = now - i;
		assert (newest.Add (c));
	}
	auto newestWire = EncodeTorrentComments (newest.Visible (), now, 20);
	auto newestDecoded = DecodeTorrentComments (newestWire, now);
	assert (newestDecoded && newestDecoded->comments.size () == 20);
	assert (newestDecoded->comments.front ().time == now);
	assert (newestDecoded->comments.back ().time == now - 19);

	// Capacity pressure evicts an old foreign entry instead of permanently
	// rejecting every later comment.
	TorrentCommentStore full;
	full.SetEnabled (true);
	for (size_t owner = 0; owner < 8; ++owner)
		for (size_t i = 0; i < MAX_COMMENT_PER_OWNER; ++i)
		{
			TorrentComment c;
			c.owner = "filler" + std::to_string (owner);
			c.text = std::string (64, 'x') + std::to_string (i);
			c.time = now - 1000;
			assert (full.Add (c));
		}
	TorrentComment freshComment;
	freshComment.owner = "fresh";
	freshComment.text = "new";
	freshComment.rating = 1;
	freshComment.time = now;
	assert (full.Add (freshComment));
	assert (full.Snapshot ().size () <= MAX_COMMENT_COUNT);

	// Anonymous rating-only entries do not share one pseudonym quota.
	TorrentCommentStore anonymous;
	anonymous.SetEnabled (true);
	for (size_t i = 0; i < 9; ++i)
	{
		auto c = anonymousRating;
		c.text = "anonymous " + std::to_string (i);
		c.time = now - i;
		assert (anonymous.Add (c));
	}
	assert (anonymous.Snapshot ().size () == 9);

	// Hiding releases visible text budget but keeps the entry for dedupe.
	TorrentCommentStore hiddenBudget;
	hiddenBudget.SetEnabled (true);
	for (size_t i = 0; i < 8; ++i)
	{
		TorrentComment c;
		c.owner = "owner" + std::to_string (i);
		c.text = std::string (512, 'z');
		c.time = now - i;
		assert (hiddenBudget.Add (c));
	}
	const auto hiddenTextId = hiddenBudget.Snapshot ().front ().localId;
	assert (hiddenBudget.HideId (hiddenTextId));
	TorrentComment replacement;
	replacement.owner = "replacement";
	replacement.text = std::string (512, 'y');
	replacement.time = now;
	assert (hiddenBudget.Add (replacement));
	bool hiddenStillPresent = false;
	for (const auto & c : hiddenBudget.Snapshot ())
		if (c.localId == hiddenTextId && c.hidden) hiddenStillPresent = true;
	assert (hiddenStillPresent);
	// The visible budget is full, so unhide must fail rather than create a
	// state that the persistence decoder would reject.
	assert (!hiddenBudget.UnhideId (hiddenTextId));
	TorrentCommentStore budgetRoundTrip;
	assert (DecodeTorrentCommentState (EncodeTorrentCommentState (hiddenBudget), now, budgetRoundTrip));

	// Hide/unhide must survive a state round-trip without changing the
	// stable local ID or resurrecting a duplicate.
	TorrentCommentStore persisted;
	persisted.SetEnabled (true);
	assert (persisted.Add (original));
	const auto persistedId = persisted.Snapshot ().front ().localId;
	assert (persisted.HideId (persistedId));
	TorrentCommentStore hiddenRoundTrip;
	assert (DecodeTorrentCommentState (EncodeTorrentCommentState (persisted), now, hiddenRoundTrip));
	assert (hiddenRoundTrip.Snapshot ().front ().hidden && hiddenRoundTrip.Visible ().empty ());
	assert (hiddenRoundTrip.UnhideId (persistedId) && hiddenRoundTrip.Visible ().size () == 1);
	TorrentCommentStore visibleRoundTrip;
	assert (DecodeTorrentCommentState (EncodeTorrentCommentState (hiddenRoundTrip), now, visibleRoundTrip));
	assert (visibleRoundTrip.Visible ().size () == 1 && visibleRoundTrip.Snapshot ().front ().localId == persistedId);
	store.SetEnabled (false);
	assert (store.Visible ().empty ());
	TorrentCommentStore disabled;
	assert (DecodeTorrentCommentState (EncodeTorrentCommentState (store), now, disabled));
	assert (!disabled.IsEnabled () && disabled.Snapshot ().size () == 1);

	// --- normalized deduplication
	// ----------------------------------------- Whitespace and zero-width
	// padding cannot replay one comment, and the stored text is never
	// rewritten.
	TorrentCommentStore normalized;
	normalized.SetEnabled (true);
	TorrentComment padded { "pseudo", "spam  spam", 1, now, false };
	assert (normalized.Add (padded));
	assert (normalized.Snapshot ().front ().text == "spam  spam");
	auto variant = padded;
	variant.text = " spam spam ";
	assert (!normalized.Add (variant));
	variant.text = "spam\tspam";
	assert (!normalized.Add (variant));
	variant.text = "spam\n\rspam";
	assert (!normalized.Add (variant));
	variant.text = "spam \xE2\x80\x8Bspam"; // U+200B zero-width space
	assert (!normalized.Add (variant));
	variant.text = "\xEF\xBB\xBFspam spam\xE2\x80\x8C"; // BOM + U+200C
	assert (!normalized.Add (variant));
	variant.text = "spam spa\xE2\x80\x8Dm"; // U+200D inside a word
	assert (!normalized.Add (variant));
	variant.text = "spam spam";
	variant.owner = " pseudo\t";
	assert (!normalized.Add (variant));
	// A different normalized text is a different entry, and the rating is
	// not part of the key: another rating for the same content is still a
	// duplicate.
	auto distinct = padded;
	distinct.text = "spam spam spam";
	distinct.rating = 5;
	assert (normalized.Add (distinct));
	auto reRated = padded;
	reRated.rating = 4;
	assert (!normalized.Add (reRated));
	assert (normalized.VisibleCount () == 2 && normalized.HiddenCount () == 0 && normalized.LocalCount () == 0);
	// Hidden entries stay in the dedupe set, so moderation is not undone
	// by a padded replay.
	const auto spamId = normalized.Snapshot ().front ().localId;
	assert (normalized.HideId (spamId));
	variant.text = "spam   spam";
	assert (!normalized.Add (variant));
	assert (normalized.VisibleCount () == 1 && normalized.HiddenCount () == 1);
	assert (normalized.UnhideId (spamId) && normalized.VisibleCount () == 2);

	// The stored-entry counts are plain reads, so a UI snapshot can expose
	// them
	// without iterating a copy: local entries are counted separately.
	TorrentCommentStore counted;
	counted.SetEnabled (true);
	TorrentComment operatorComment { "operator", "local note", 1, now, false };
	operatorComment.local = true;
	assert (counted.Add (operatorComment));
	TorrentComment peerComment { "peer", "peer note", 1, now, false };
	assert (counted.Add (peerComment));
	assert (counted.VisibleCount () == 2 && counted.HiddenCount () == 0 && counted.LocalCount () == 1);
	assert (counted.HideId (counted.Snapshot ().front ().localId));
	assert (counted.VisibleCount () == 1 && counted.HiddenCount () == 1 && counted.LocalCount () == 1);

	// --- anonymous visible cap
	// --------------------------------------------- Anonymous entries have
	// their own visible budget instead of bypassing the per-pseudonym
	// quota, while hidden entries stop counting toward it.
	TorrentCommentStore anon;
	anon.SetEnabled (true);
	for (size_t i = 0; i < MAX_COMMENT_ANONYMOUS_VISIBLE; ++i)
	{
		TorrentComment c;
		c.text = "anonymous " + std::to_string (i);
		c.time = now - i;
		assert (anon.Add (c));
	}
	assert (anon.VisibleCount () == MAX_COMMENT_ANONYMOUS_VISIBLE);
	TorrentComment anonymousOverflow;
	anonymousOverflow.text = "anonymous overflow";
	assert (!anon.Add (anonymousOverflow));
	// An attributed comment is unaffected by the anonymous budget, and an
	// anonymous entry never consumes another pseudonym's quota.
	TorrentComment attributed { "named", "named text", 0, now, false };
	assert (anon.Add (attributed));
	assert (anon.VisibleCount () == MAX_COMMENT_ANONYMOUS_VISIBLE + 1);
	// Hiding an anonymous entry frees exactly one slot; the entry itself
	// is kept for deduplication.
	const auto anonymousId = anon.Snapshot ().front ().localId;
	assert (anon.HideId (anonymousId));
	assert (anon.Add (anonymousOverflow));
	assert (!anon.Add (anonymousOverflow));

	// The global 256-entry limit still evicts an old peer entry rather
	// than
	// rejecting a fresh anonymous comment.
	TorrentCommentStore evicting;
	evicting.SetEnabled (true);
	for (size_t owner = 0; owner < MAX_COMMENT_COUNT / MAX_COMMENT_PER_OWNER; ++owner)
		for (size_t i = 0; i < MAX_COMMENT_PER_OWNER; ++i)
		{
			TorrentComment c;
			c.owner = "e" + std::to_string (owner);
			c.text = "t" + std::to_string (i);
			assert (evicting.Add (c));
		}
	assert (evicting.VisibleCount () == MAX_COMMENT_COUNT);
	const auto evictedId = evicting.Snapshot ().front ().localId;
	TorrentComment freshAnonymous;
	freshAnonymous.text = "fresh anonymous";
	assert (evicting.Add (freshAnonymous));
	assert (evicting.VisibleCount () == MAX_COMMENT_COUNT);
	bool evicted = true;
	for (const auto & c : evicting.Snapshot ())
		if (c.localId == evictedId) evicted = false;
	assert (evicted);

	// --- revision semantics
	// ------------------------------------------------
	// The coalesced admin-state writer compares Revision(), so a real
	// content
	// change must bump it and a no-op hide/unhide must not.
	TorrentCommentStore revisions;
	revisions.SetEnabled (true);
	const auto enabledRevision = revisions.Revision ();
	revisions.SetEnabled (true);
	assert (revisions.Revision () == enabledRevision);
	TorrentComment trackable { "revision", "tracked text", 2, now, false };
	assert (revisions.Add (trackable));
	const auto addedRevision = revisions.Revision ();
	assert (addedRevision != enabledRevision);
	const auto trackedId = revisions.Snapshot ().front ().localId;
	assert (revisions.HideId (trackedId));
	const auto hiddenRevision = revisions.Revision ();
	assert (hiddenRevision != addedRevision);
	assert (revisions.HideId (trackedId) && revisions.Revision () == hiddenRevision); // already hidden
	assert (revisions.Hide (0) && revisions.Revision () == hiddenRevision); // already hidden
	assert (revisions.UnhideId (trackedId));
	const auto visibleRevision = revisions.Revision ();
	assert (visibleRevision != hiddenRevision);
	assert (revisions.UnhideId (trackedId) && revisions.Revision () == visibleRevision); // already visible
	assert (!revisions.UnhideId (9999) && revisions.Revision () == visibleRevision);
	assert (!revisions.Add (trackable) && revisions.Revision () == visibleRevision); // duplicate

	// Trimming hidden state is a content change: at most
	// MAX_COMMENT_HIDDEN_COUNT
	// hidden entries survive and the store revision reflects the restored
	// state.
	std::vector<std::string> hiddenEntries;
	for (size_t i = 0; i < MAX_COMMENT_HIDDEN_COUNT + 1; ++i)
		hiddenEntries.push_back (CreateDictionary ({ { "hidden", CreateInteger (1) },
			{ "id", CreateInteger (static_cast<int64_t> (i) + 1) }, { "like", CreateInteger (0) },
			{ "local", CreateInteger (0) }, { "owner", CreateByteString ("hidden" + std::to_string (i)) },
			{ "text", CreateByteString ("hidden text " + std::to_string (i)) },
			{ "time", CreateInteger (static_cast<int64_t> (now)) } }));
	const auto hiddenHeavyState = CreateDictionary ({ { "comments", CreateList (hiddenEntries) },
		{ "enabled", CreateInteger (1) }, { "version", CreateInteger (1) } });
	TorrentCommentStore trimmed;
	assert (DecodeTorrentCommentState (hiddenHeavyState, now, trimmed));
	assert (trimmed.HiddenCount () == MAX_COMMENT_HIDDEN_COUNT && trimmed.VisibleCount () == 0);
	assert (trimmed.Revision () != 0);
	const auto trimmedRevision = trimmed.Revision ();
	assert (trimmed.Hide (0) && trimmed.Revision () == trimmedRevision); // already hidden, nothing trimmed

	// --- future timestamps
	// -------------------------------------------------- A future stamp is
	// clamped to `now` and the entry is kept, so a backwards clock or a
	// restored backup cannot discard the whole local state.
	TorrentCommentStore futureStore;
	futureStore.SetEnabled (true);
	TorrentComment future { "clock", "skewed", 3, now + 5000, false };
	assert (futureStore.Add (future));
	TorrentCommentStore clamped;
	assert (DecodeTorrentCommentState (EncodeTorrentCommentState (futureStore), now, clamped));
	assert (clamped.Snapshot ().size () == 1 && clamped.Snapshot ().front ().time == now);
	assert (clamped.Snapshot ().front ().owner == future.owner && clamped.Snapshot ().front ().rating == 3);

	// --- fixed admission windows
	// -------------------------------------------
	constexpr size_t connectionCap = MAX_COMMENT_ENTRIES_PER_CONNECTION_WINDOW;
	CommentAdmissionWindow window;
	assert (window.Admit (0, connectionCap, 0)); // a zero count never consumes a window
	assert (window.Admit (0, connectionCap, 1000));
	assert (!window.Admit (1, connectionCap, 0)); // no clock, no admission
	assert (window.Admit (connectionCap, connectionCap, 1000));
	assert (window.start == 1000 && window.admitted == connectionCap);
	assert (!window.Admit (1, connectionCap, 1000));
	assert (!window.Admit (1, connectionCap, 1000 + COMMENT_ENTRY_WINDOW - 1));
	// A backwards clock keeps the current window instead of granting a
	// second full budget, and the fixed window restarts exactly at its
	// boundary.
	assert (!window.Admit (connectionCap, connectionCap, 900));
	assert (window.start == 1000 && window.admitted == connectionCap);
	assert (window.Admit (1, connectionCap, 1000 + COMMENT_ENTRY_WINDOW));
	assert (window.start == 1000 + COMMENT_ENTRY_WINDOW && window.admitted == 1);
	assert (window.Remaining (connectionCap) == connectionCap - 1);
	assert (!window.Admit (connectionCap + 1, connectionCap, 5000)); // single call above cap
	// Two connection windows of 40 cannot flush the 256-entry store; the
	// torrent-wide budget is at most two connection budgets.
	static_assert (MAX_COMMENT_ENTRIES_PER_CONNECTION_WINDOW * 2 <= MAX_COMMENT_ENTRIES_PER_TORRENT_WINDOW,
		"the per-torrent comment budget must cover at most two connection windows");
	static_assert (MAX_COMMENT_ENTRIES_PER_TORRENT_WINDOW * 3 < MAX_COMMENT_COUNT,
		"a reconnecting peer must not flush the store within three windows");

	// --- per-torrent budget
	// -------------------------------------------------
	auto torrent = MakeTorrent ();
	constexpr uint64_t windowBase = 5000;
	assert (torrent->AdmitCommentEntries (0, 0)); // zero count is always free
	assert (!torrent->AdmitCommentEntries (1, 0)); // no clock
	assert (torrent->AdmitCommentEntries (MAX_COMMENT_ENTRIES_PER_TORRENT_WINDOW, windowBase));
	assert (!torrent->AdmitCommentEntries (1, windowBase));
	assert (!torrent->AdmitCommentEntries (1, windowBase + COMMENT_ENTRY_WINDOW - 1));
	assert (!torrent->AdmitCommentEntries (1, windowBase - 1)); // backwards clock
	assert (torrent->AdmitCommentEntries (MAX_COMMENT_ENTRIES_PER_TORRENT_WINDOW, windowBase + COMMENT_ENTRY_WINDOW));
	assert (!torrent->AdmitCommentEntries (1, windowBase + COMMENT_ENTRY_WINDOW));
	return 0;
}
#else
int main ()
{
	return 0;
}
#endif
