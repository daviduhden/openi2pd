/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test torrents comments.
 *
 * Checks the I2P torrent comment wire codec: request/response fields, relative timestamps, size and count limits, trailing data, and rejection or filtering of control and overlong UTF-8 text.
 *
 */

#include <cassert>
#include <string>
#include "TorrentsComments.h"
#include "Torrents.h"
#ifndef NO_TORRENTS
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
	// timestamp is an age, not an epoch. Missing required response fields fail.
	assert (!DecodeTorrentComments (CreateDictionary ({ { "msg_type", CreateInteger (1) } }), now));
	assert (!DecodeTorrentComments (encoded + "garbage", now));
	assert (!DecodeTorrentComments (std::string (MAX_COMMENT_MESSAGE_BYTES + 1, 'a'), now));
	assert (!DecodeTorrentComments (
		CreateDictionary ({ { "filter", CreateByteString ("short") }, { "msg_type", CreateInteger (0) } }), now));
	auto invalid = original;
	invalid.text = "\033[31mterminal";
	assert (DecodeTorrentComments (EncodeTorrentComments ({ invalid }, now), now)->comments.empty ());
	invalid.text = std::string ("\xC0\xAF", 2); // overlong UTF-8
	assert (DecodeTorrentComments (EncodeTorrentComments ({ invalid }, now), now)->comments.empty ());
	// Independent Java wire field vector: timestamp 10 means age ten seconds.
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

	// Invalid entries are skipped without discarding the valid ones, and Java's
	// larger reply count is accepted.
	TorrentComment good { "good", "keep me", 2, now, false };
	const auto badEntry = CreateDictionary ({ { "like", CreateInteger (0) }, { "owner", CreateByteString ("bad") },
		{ "text", CreateByteString ("\x1b") }, { "timestamp", CreateInteger (0) } });
	const auto goodEntry = CreateDictionary ({ { "like", CreateInteger (2) }, { "owner", CreateByteString ("good") },
		{ "text", CreateByteString ("keep me") }, { "timestamp", CreateInteger (0) } });
	auto mixedReply = CreateDictionary (
		{ { "comments", CreateList ({ badEntry, goodEntry }) }, { "msg_type", CreateInteger (1) } });
	auto mixed = DecodeTorrentComments (mixedReply, now);
	assert (mixed && mixed->comments.size () == 1 && mixed->comments[0].owner == good.owner);

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

	// Newest comments are published first, even if older entries fill the store.
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

	// Hide/unhide must survive a state round-trip without changing the stable
	// local ID or resurrecting a duplicate.
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
	assert (visibleRoundTrip.Visible ().size () == 1
		&& visibleRoundTrip.Snapshot ().front ().localId == persistedId);
	store.SetEnabled (false);
	assert (store.Visible ().empty ());
	TorrentCommentStore disabled;
	assert (DecodeTorrentCommentState (EncodeTorrentCommentState (store), now, disabled));
	assert (!disabled.IsEnabled () && disabled.Snapshot ().size () == 1);
	return 0;
}
#else
int main ()
{
	return 0;
}
#endif
