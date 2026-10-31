/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test torrents engine interop.
 *
 * Round-trips torrent metadata through creation and parsing, checking
 * tracker tiers, private flag, info-hash stability, multi-file and
 * zero-length entries, path validation and explicit tracker
 * suppression.
 *
 */

#include <cassert>
#include <string>
#include <vector>
#include "Torrents.h"

#ifndef NO_TORRENTS
int main ()
{
	using namespace i2p::torrents;
	assert (CreateByteString ("") == "0:");
	assert (CreateDictionary ({ { "z", "i1e" }, { "a", "0:" } }) == "d1:a0:1:zi1ee");
	assert (CreateDictionary ({ { "a", "i1e" }, { "a", "i2e" } }).empty ());

	std::vector<Torrent::InfoHash> hashes (1);
	const std::string data ("abc");
	SHA1 (reinterpret_cast<const uint8_t *> (data.data ()), data.size (), hashes[0].data ());
	Torrent::TrackerTiers trackers { { "http://first.i2p/announce", "http://backup.i2p/announce" },
		{ "http://second.i2p/announce" } };
	auto torrent = Torrent::CreateV1 ("example", { { { }, data.size () } }, MIN_PIECE_LENGTH, hashes, trackers, true);
	assert (torrent && torrent->IsPrivate () && torrent->GetLength () == 3);
	Torrent restored (torrent->CreateTorrentFileContent ());
	assert (restored.GetError () == eTorrentErrorNoError);
	assert (restored.GetTrackerTiers () == trackers);
	assert (restored.GetInfoHash () == torrent->GetInfoHash ());
	assert (restored.IsPrivate ());
	// Tracker administration changes the outer dictionary only.
	assert (restored.SetTrackerTiers ({ { "http://replacement.i2p/announce" } }));
	Torrent edited (restored.CreateTorrentFileContent ());
	assert (edited.GetInfoHash () == torrent->GetInfoHash ());
	assert (edited.SetTrackerTiers ({ }));
	edited.SetAnnounce ({ });
	Torrent trackerless (edited.CreateTorrentFileContent ());
	assert (trackerless.HasExplicitTrackers () && trackerless.GetTrackerTiers ().empty ());
	assert (trackerless.GetAnnounce ().empty () && trackerless.GetInfoHash () == torrent->GetInfoHash ());
	const auto info
		= std::string (reinterpret_cast<const char *> (torrent->GetInfo ().data ()), torrent->GetInfo ().size ());

	// A primary announce outside a non-empty announce-list remains part of
	// the policy and is normalized into tier 0 without changing the info
	// hash.
	auto splitTrackers = CreateDictionary ({ { "announce", CreateByteString ("http://primary.i2p/a") },
		{ "announce-list", CreateList ({ CreateList ({ CreateByteString ("http://backup.i2p/a") }) }) },
		{ "info", info } });
	Torrent split (splitTrackers);
	assert (split.GetError () == eTorrentErrorNoError);
	assert (split.GetTrackerTiers ().size () == 1);
	assert (split.GetTrackerTiers ().front ().size () == 2);
	assert (split.GetTrackerTiers ().front ().front () == "http://primary.i2p/a");
	assert (split.GetInfoHash () == torrent->GetInfoHash ());

	// A created public torrent with no trackers inherits the tunnel's
	// common
	// endpoints instead of being marked as explicitly trackerless.
	Torrent::TrackerTiers emptyTiers;
	auto trackerlessCreated
		= Torrent::CreateV1 ("trackerless", { { { }, data.size () } }, MIN_PIECE_LENGTH, hashes, emptyTiers);
	assert (trackerlessCreated && !trackerlessCreated->HasExplicitTrackers ());
	assert (trackerlessCreated->GetAnnounce ().empty ());
	assert (!Torrent::CreateV1 (
		"private-trackerless", { { { }, data.size () } }, MIN_PIECE_LENGTH, hashes, emptyTiers, true));

	// SHA-1 spans file boundaries; zero-length entries remain represented.
	auto multi = Torrent::CreateV1 (
		"directory", { { "first", 1 }, { "empty", 0 }, { "sub/second", 2 } }, MIN_PIECE_LENGTH, hashes);
	assert (multi && !multi->IsSingleFile () && multi->GetFiles ().size () == 3);
	assert (!Torrent::CreateV1 ("../escape", { { { }, 3 } }, MIN_PIECE_LENGTH, hashes));
	assert (!Torrent::CreateV1 ("root", { { "../escape", 3 } }, MIN_PIECE_LENGTH, hashes));
	assert (!Torrent::CreateV1 ("root", { { "same", 1 }, { "same", 2 } }, MIN_PIECE_LENGTH, hashes));
	assert (!Torrent::CreateV1 ("root", { { { }, 3 } }, MIN_PIECE_LENGTH, { }));
	assert (!Torrent::CreateV1 ("root", { { { }, MIN_PIECE_LENGTH + 1 } }, MIN_PIECE_LENGTH, hashes));
	Torrent::TrackerTiers excessive (MAX_NUM_TRACKERS + 1);
	assert (!restored.SetTrackerTiers (excessive));
	// A typed field with a syntactically valid but wrong bencode type is
	// rejected.
	Torrent malformed (CreateDictionary ({ { "announce-list", "li1ee" }, { "info", info } }));
	assert (malformed.GetError () == eTorrentErrorMalformedMetaInfo);

	// Tracker/metadata rewrites preserve top-level keys the engine does
	// not interpret. Java's MetaInfo copy constructor has the same
	// guarantee.
	auto withExtras = CreateDictionary ({ { "announce", CreateByteString ("http://tracker.i2p/announce") },
		{ "comment", CreateByteString ("original comment") }, { "created by", CreateByteString ("fixture") },
		{ "creation date", "i1700000000e" }, { "url-list", CreateList ({ CreateByteString ("http://seed.i2p/file") }) },
		{ "info", info } });
	Torrent extras (withExtras);
	assert (extras.GetError () == eTorrentErrorNoError);
	assert (extras.GetMetaComment () == "original comment");
	assert (extras.GetCreatedBy () == "fixture");
	assert (extras.SetMetaComment ("edited comment"));
	assert (extras.SetCreatedBy ("openi2pd"));
	assert (!extras.SetMetaComment (std::string (1025, 'x')));
	assert (!extras.SetCreatedBy (std::string (256, 'x')));
	auto rewritten = extras.CreateTorrentFileContent ();
	assert (!rewritten.empty ());
	assert (rewritten.find ("creation date") != std::string::npos);
	assert (rewritten.find ("url-list") != std::string::npos);
	Torrent reparsed (rewritten);
	assert (reparsed.GetError () == eTorrentErrorNoError);
	assert (reparsed.GetMetaComment () == "edited comment");
	assert (reparsed.GetCreatedBy () == "openi2pd");
	assert (reparsed.GetInfoHash () == torrent->GetInfoHash ());
	return 0;
}
#else
int main ()
{
	return 0;
}
#endif
