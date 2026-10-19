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

	TorrentCommentStore store;
	assert (!store.IsEnabled () && !store.Add (original));
	store.SetEnabled (true);
	assert (store.Add (original));
	assert (!store.Add (original));
	assert (store.Hide (0) && store.Visible ().empty ());
	assert (!store.Add (original)); // hidden spam cannot immediately reappear
	assert (!store.Hide (10));
	assert (store.Snapshot ().size () == 1 && store.Snapshot ()[0].localId != 0);
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
