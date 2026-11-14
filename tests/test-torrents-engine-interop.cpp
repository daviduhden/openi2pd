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

#include <algorithm>
#include <cassert>
#include <array>
#include <limits>
#include <span>
#include <string>
#include <vector>
#include "Torrents.h"

#ifndef NO_TORRENTS
namespace
{
void TestSharedMetadata ()
{
	using namespace i2p::torrents;
	using Result = MetadataDownload::Result;
	std::vector<uint8_t> info (2 * REQUEST_BLOCK_SIZE + 7);
	for (size_t i = 0; i < info.size (); ++i) info[i] = static_cast<uint8_t> (i * 17 + 3);
	std::array<uint8_t, SHA_DIGEST_LENGTH> hash { };
	assert (SHA1 (info.data (), info.size (), hash.data ()));
	const auto first = std::span<const uint8_t> (info).first (REQUEST_BLOCK_SIZE);
	const auto second = std::span<const uint8_t> (info).subspan (REQUEST_BLOCK_SIZE, REQUEST_BLOCK_SIZE);
	const auto last = std::span<const uint8_t> (info).last (7);
	MetadataDownload download;
	assert (!download.Request (0, info.size (), 0));
	assert (!download.Request (1, 0, 0));
	assert (!download.Request (1, MAX_PEER_METADATA_SIZE + 1, 0));
	assert (download.TakeVerified ().empty ());
	assert (download.Request (1, info.size (), 0) == 0);
	assert (!download.Request (1, info.size (), 0)); // one lease per peer
	assert (download.Request (2, info.size (), 0) == 1);
	assert (!download.Request (3, info.size () + 1, 0)); // size disagreement
	assert (download.Size () == info.size ());
	assert (download.Receive (2, 0, info.size (), first, hash, 1) == Result::Rejected);
	assert (download.Receive (1, 0, info.size () + 1, first, hash, 1) == Result::Rejected);
	assert (download.Receive (1, 0, info.size (), first.first (REQUEST_BLOCK_SIZE - 1), hash, 1) == Result::Rejected);
	assert (download.Receive (1, 0, info.size (), std::span<const uint8_t> (info), hash, 1) == Result::Rejected);
	assert (
		download.Receive (1, std::numeric_limits<size_t>::max (), info.size (), first, hash, 1) == Result::Rejected);
	assert (download.Received () == 0);
	// Different peers can deliver disjoint blocks in either order.
	assert (download.Receive (2, 1, info.size (), second, hash, 1) == Result::Accepted);
	assert (download.Request (2, info.size (), 1) == 2);
	assert (download.Receive (2, 2, info.size (), last.first (6), hash, 2) == Result::Rejected);
	assert (download.Receive (2, 2, info.size (), last, hash, 2) == Result::Accepted);
	assert (download.Receive (2, 2, info.size (), last, hash, 2) == Result::Rejected);
	assert (download.Receive (1, 0, info.size (), first, hash, 2) == Result::Complete);
	assert (!download.Request (3, info.size (), 2));
	assert (download.TakeVerified () == info);
	assert (download.Size () == 0);
	assert (download.TakeVerified ().empty ());

	assert (download.Request (1, info.size (), 0) == 0);
	assert (download.Request (2, info.size (), 0) == 1);
	download.Release (1); // disconnect releases only its own outstanding block
	assert (download.Request (3, info.size (), 1) == 0);
	assert (!download.Reject (1, 1, 1));
	assert (download.Reject (2, 1, 1));
	assert (download.Request (4, info.size (), 2) == 1);
	assert (download.Receive (3, 0, info.size (), first, hash, 31) == Result::Rejected);
	assert (download.Request (5, info.size (), 31) == 0); // expired exactly at deadline
	download.Reset ();
	assert (download.Request (1, info.size (), 0) == 0);
	assert (!download.Request (2, info.size () + 1, 29));
	assert (download.Request (2, info.size () + 1, 30) == 0); // stalled wrong-size candidate retired
	assert (download.Size () == info.size () + 1);
	download.Reset ();
	assert (download.Request (1, info.size (), 0) == 0);
	assert (!download.Request (2, info.size () + 1, 1));
	assert (!download.Request (1, info.size (), 30)); // renewal cannot starve the alternate offer
	assert (download.Request (2, info.size () + 1, 30) == 0);
	download.Reset ();
	assert (download.Request (1, info.size (), 0) == 0);
	assert (!download.Request (2, info.size () + 1, 1));
	assert (!download.Request (1, info.size (), 30));
	assert (download.Request (1, info.size (), 60) == 0); // vanished alternate cannot pin preference forever
	download.Reset ();

	const std::array<uint8_t, 1> byte { 7 };
	std::array<uint8_t, SHA_DIGEST_LENGTH> oneHash { };
	assert (SHA1 (byte.data (), byte.size (), oneHash.data ()));
	const std::array<uint8_t, SHA_DIGEST_LENGTH> wrongHash { };
	uint64_t now = 0, delay = 30;
	for (unsigned failure = 0; failure < 6; ++failure)
	{
		assert (download.Request (1, 1, now) == 0);
		assert (download.Receive (1, 0, 1, byte, wrongHash, now) == Result::HashMismatch);
		assert (download.Size () == 0 && download.TakeVerified ().empty ());
		assert (!download.Request (2, 1, now + delay - 1));
		now += delay;
		delay = std::min<uint64_t> (delay * 2, 300);
	}
	assert (download.Request (2, 1, now) == 0);
	assert (download.Receive (2, 0, 1, byte, oneHash, now) == Result::Complete);
	assert (download.TakeVerified () == std::vector<uint8_t> (byte.begin (), byte.end ()));
	assert (download.Request (1, MAX_PEER_METADATA_SIZE, now) == 0);
	assert (download.Size () == MAX_PEER_METADATA_SIZE);
	download.Reset ();
	Torrent magnet (hash);
	auto owner = magnet.AllocateMetadataOwner ();
	assert (owner && magnet.GetMetadataDownload ().Request (owner, info.size (), 0) == 0);
	magnet.SetStopped (true);
	assert (magnet.GetMetadataDownload ().Size () == 0);
	assert (magnet.AllocateMetadataOwner () > owner); // old connection IDs never reappear
}
}

int main ()
{
	using namespace i2p::torrents;
	TestSharedMetadata ();
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
