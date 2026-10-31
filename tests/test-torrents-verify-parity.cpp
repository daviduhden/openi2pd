/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Torrent resume verification, tracker peer parsing and
 * peer-id parity regression tests.
 *
 * Covers the I2PSnark-parity fixes that can be exercised without a live
 * network: resume bitfields and pre-existing final-named data are
 * hashed before availability is published, tracker responses accept
 * both compact and dict-based I2P peer lists with per-entry rejection
 * and a peer cap, min-interval/tracker-id handling, percent-encoded
 * query values and per-torrent random peer ids. Connection limits, PEX
 * scheduling and private swarm announce selection require a running
 * tunnel and are not covered here.
 *
 */

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "Torrents.h"
#include "TorrentsControl.h"
#include "Base.h"

#ifndef NO_TORRENTS

namespace
{
using namespace i2p::torrents;

// Minimal in-memory payload capability: it keeps the test off the
// filesystem and exercises exactly the TorrentFilePath/Piece path the
// storage worker uses.
class MemoryStorage final : public TorrentPayloadStorage
{
public:
	explicit MemoryStorage (bool finalNames)
		: m_FinalNames (finalNames)
	{
	}
	bool UsesFinalNames () const override { return m_FinalNames; }

	std::map<std::string, std::vector<uint8_t>> files;
	struct Handle
	{
		std::string name;
		bool writable;
	};
	std::map<uint64_t, Handle> handles;
	uint64_t next = 1;

	uint64_t Open (const std::filesystem::path & relative, uint64_t length, bool create, std::string &) override
	{
		assert (IsSafeRelativePath (relative));
		auto name = relative.generic_string ();
		if (!files.contains (name))
		{
			if (!create) return 0;
			files[name].resize (length);
		}
		if (files[name].size () != length) return 0;
		auto id = next++;
		handles.emplace (id, Handle { name, create });
		return id;
	}
	bool Read (uint64_t handle, uint64_t offset, std::span<uint8_t> bytes, std::string &) override
	{
		assert (bytes.size () <= MaxTransfer);
		if (!handles.contains (handle)) return false;
		auto & file = files[handles.at (handle).name];
		if (offset > file.size () || bytes.size () > file.size () - offset) return false;
		std::copy_n (file.data () + offset, bytes.size (), bytes.data ());
		return true;
	}
	bool Write (uint64_t handle, uint64_t offset, std::span<const uint8_t> bytes, std::string &) override
	{
		assert (bytes.size () <= MaxTransfer);
		if (!handles.contains (handle) || !handles.at (handle).writable) return false;
		auto & file = files[handles.at (handle).name];
		if (offset > file.size () || bytes.size () > file.size () - offset) return false;
		std::copy (bytes.begin (), bytes.end (), file.begin () + offset);
		return true;
	}
	bool Close (uint64_t handle, std::string &) override { return handles.erase (handle) == 1; }

private:
	bool m_FinalNames;
};

constexpr size_t PIECE_LENGTH = 16384;

struct Fixture
{
	std::shared_ptr<Torrent> torrent;
	std::shared_ptr<MemoryStorage> backend;
	std::vector<uint8_t> data;
	std::filesystem::path root { "torrent-verify-root" };
};

// Two 16 KiB files (one piece each) whose on-disk bytes hash to the
// piece hashes in the metainfo, so verification can actually succeed.
Fixture MakeVerifiedTorrent (std::string_view name, bool finalNames)
{
	Fixture fixture;
	fixture.data.resize (2 * PIECE_LENGTH);
	for (size_t i = 0; i < fixture.data.size (); ++i) fixture.data[i] = static_cast<uint8_t> ((i * 31 + 7) & 0xFF);
	std::array<uint8_t, SHA_DIGEST_LENGTH> first { }, second { };
	SHA1 (fixture.data.data (), PIECE_LENGTH, first.data ());
	SHA1 (fixture.data.data () + PIECE_LENGTH, PIECE_LENGTH, second.data ());
	fixture.torrent = Torrent::CreateV1 (
		name, { { "a.bin", PIECE_LENGTH }, { "b.bin", PIECE_LENGTH } }, PIECE_LENGTH, { first, second });
	assert (fixture.torrent && fixture.torrent->GetNumPieces () == 2);
	fixture.backend = std::make_shared<MemoryStorage> (finalNames);
	size_t offset = 0;
	for (const auto & file : fixture.torrent->GetFiles ())
	{
		const auto length = file->GetFileLength ();
		assert (file->UpdateFullPath (fixture.root));
		// Final-named storage keeps no .part staging; resume storage does.
		file->SetIsPart (!finalNames);
		file->SetPayloadStorage (fixture.backend, fixture.root);
		assert (file->Save (0, fixture.data.data () + offset, length));
		offset += length;
	}
	assert (offset == fixture.data.size ());
	return fixture;
}

// Runs the bounded local-verification pass exactly as the tunnel's
// storage service does: one piece at a time, hash from disk, then
// either finish it or mark it corrupt. Returns the number of pieces
// that failed.
size_t RunLocalVerification (Torrent & torrent)
{
	size_t failed = 0;
	for (;;)
	{
		const int index = torrent.GetNextPieceToVerify ();
		if (index < 0) break;
		auto & piece = torrent.GetPiece (index);
		assert (piece.IsVerificationPending ());
		bool loaded = true;
		for (auto & fragment : torrent.GetPieceFileFragments (index))
		{
			if (!piece.Load (std::move (fragment)))
			{
				loaded = false;
				break;
			}
		}
		if (!loaded || !piece.VerifyHash ())
		{
			++failed;
			torrent.InvalidatePiece (static_cast<size_t> (index));
			continue;
		}
		torrent.FinishPieceVerification (static_cast<size_t> (index));
		piece.ReleaseData ();
	}
	return failed;
}

std::string Bytes (std::string_view value)
{
	return std::to_string (value.size ()) + ":" + std::string (value);
}

std::string Dict (std::initializer_list<std::pair<std::string, std::string>> items)
{
	std::string result ("d");
	for (const auto & [key, value] : items)
	{
		result += Bytes (key);
		result += value;
	}
	result += "e";
	return result;
}

std::string CompactPeers (std::initializer_list<uint8_t> firstBytes)
{
	std::string peers;
	for (const uint8_t first : firstBytes)
	{
		std::string hash (i2p::data::IdentHash::len, '\0');
		hash[0] = static_cast<char> (first);
		hash[i2p::data::IdentHash::len - 1] = static_cast<char> (0x7F);
		peers += hash;
	}
	return CreateByteString (peers);
}

std::string PeerEntry (std::string_view encodedHash)
{
	return Dict ({ { "ip", Bytes (encodedHash) }, { "port", "i6881e" } });
}

std::string EncodeHash (const std::string & hash)
{
	return i2p::data::ByteStreamToBase64 (reinterpret_cast<const uint8_t *> (hash.data ()), hash.size ());
}
} // namespace

// A resume bitfield restores complete-but-unverified pieces. They must
// be excluded from the published bitfield and from serving until the
// storage service has hashed them, and a failed hash must make the
// piece downloadable again instead of stalling promotion forever.
static void TestResumeVerificationGate ()
{
	auto fixture = MakeVerifiedTorrent ("resume-verify", false);
	auto & torrent = *fixture.torrent;

	assert (torrent.GetError () == eTorrentErrorNoError);
	// Exact length and zero padding are part of the resume contract. A
	// short,
	// oversized or spare-bit bitfield is discarded without mutating
	// pieces.
	assert (!torrent.ApplyBitfield ({ }));
	assert (!torrent.ApplyBitfield ({ 0xC0, 0x00 }));
	assert (!torrent.ApplyBitfield ({ 0xC1 }));
	assert (torrent.CreateBitfield ().first == std::vector<uint8_t> ({ 0x00 }));
	assert (torrent.ApplyBitfield ({ 0xC0 }));
	// Applying a resume bitfield alone must not count as verified data.
	assert (!torrent.AreAllPiecesVerified ());
	{
		auto [bitfield, have] = torrent.CreateBitfield ();
		assert (bitfield.size () == 1 && bitfield[0] == 0xC0);
		(void)have;
	}

	torrent.RequestLocalVerification (false);
	assert (torrent.IsLocalVerificationRequested ());
	assert (torrent.GetPiece (0).IsVerificationPending () && torrent.GetPiece (1).IsVerificationPending ());
	// Pending pieces are neither advertised nor served.
	{
		auto [bitfield, have] = torrent.CreateBitfield ();
		assert (bitfield.size () == 1 && bitfield[0] == 0x00);
		assert (!boost::logic::indeterminate (have) && !have);
	}
	assert (torrent.GetPiece (0).IsComplete () && !torrent.GetPiece (0).IsVerified ());
	assert (torrent.GetPiece (0).IsSaving ());
	assert (!torrent.GetPiece (0).HasBlock (0));
	std::vector<uint8_t> block;
	assert (!torrent.GetPiece (0).CopyBlock (0, 16, block));

	// One verification chain at a time, one piece per step.
	assert (torrent.BeginLocalVerification ());
	assert (!torrent.BeginLocalVerification ());
	assert (torrent.GetNextPieceToVerify () == 0);
	// Corrupt the second file before the pass reaches it.
	auto & files = fixture.backend->files;
	assert (files.size () == 2);
	auto secondFile = std::next (files.begin ());
	assert (secondFile->first.find ("b.bin") != std::string::npos);
	secondFile->second[0] ^= 0xFF;
	const size_t failed = RunLocalVerification (torrent);
	assert (failed == 1);
	torrent.EndLocalVerification ();

	// The good piece is verified and published; the corrupt one is
	// Missing.
	assert (torrent.GetPiece (0).IsVerified () && !torrent.GetPiece (0).IsVerificationPending ());
	assert (!torrent.GetPiece (1).IsComplete () && !torrent.GetPiece (1).IsVerificationPending ());
	assert (!torrent.AreAllPiecesVerified ());
	assert (torrent.GetLeft () == PIECE_LENGTH);
	{
		auto [bitfield, have] = torrent.CreateBitfield ();
		assert (bitfield.size () == 1 && bitfield[0] == 0x80);
		assert (boost::logic::indeterminate (have));
	}
	// The walk ends and clears the request so the status timer stops.
	assert (!torrent.IsLocalVerificationRequested ());
}

// A restart can find one final-named file and one absent file. Only
// pieces
// fully backed by existing files are verified; the missing range
// remains
// downloadable instead of causing a whole-torrent re-download.
static void TestMixedExistingFiles ()
{
	auto fixture = MakeVerifiedTorrent ("mixed-existing", true);
	auto & torrent = *fixture.torrent;
	assert (fixture.backend->files.erase ("b.bin") == 1);
	std::vector<bool> present { true, false };
	assert (torrent.RequestLocalVerificationForExistingFiles (present) == 1);
	assert (torrent.GetPiece (0).IsVerificationPending ());
	assert (!torrent.GetPiece (1).IsVerificationPending ());
	assert (torrent.BeginLocalVerification ());
	assert (RunLocalVerification (torrent) == 0);
	torrent.EndLocalVerification ();
	assert (torrent.GetPiece (0).IsVerified ());
	assert (!torrent.GetPiece (1).IsComplete ());
	assert (torrent.GetSelectedVerifiedBytes () == PIECE_LENGTH);
	assert (torrent.GetLeft () == PIECE_LENGTH);
}

// Data that already exists under its final name must be hashed, never
// trusted
// by filename, and must become fully verified so promotion can proceed.
static void TestExistingFinalDataIsHashed ()
{
	auto fixture = MakeVerifiedTorrent ("final-names", true);
	auto & torrent = *fixture.torrent;
	assert (torrent.GetNumPieces () == 2);

	// This is what the old code did for a directory of final-named files.
	torrent.SetComplete ();
	assert (torrent.IsComplete ());
	assert (!torrent.AreAllPiecesVerified ());

	torrent.RequestLocalVerification (true);
	assert (!torrent.IsComplete ()); // completion is no longer proven
	assert (torrent.IsLocalVerificationRequested ());
	assert (torrent.GetPiece (0).IsVerificationPending () && torrent.GetPiece (1).IsVerificationPending ());
	{
		auto [bitfield, have] = torrent.CreateBitfield ();
		assert (bitfield.size () == 1 && bitfield[0] == 0x00);
		assert (!boost::logic::indeterminate (have) && !have);
	}

	assert (torrent.BeginLocalVerification ());
	assert (RunLocalVerification (torrent) == 0);
	torrent.EndLocalVerification ();
	assert (torrent.AreAllPiecesVerified ());
	assert (!torrent.IsComplete ()); // promotion stays a tunnel decision
	assert (torrent.GetLeft () == 0);
	{
		auto [bitfield, have] = torrent.CreateBitfield ();
		assert (bitfield.size () == 1 && bitfield[0] == 0xC0);
		assert (!boost::logic::indeterminate (have) && have);
	}
}

// Tracker responses: compact peers and list-of-dicts must both work,
// each
// malformed entry is rejected on its own, and the peer count is capped.
static void TestTrackerPeerForms ()
{
	Torrent::InfoHash infoHash { };
	infoHash[0] = 0x11;
	Torrent torrent (infoHash);

	// Compact form: two 32-byte destination hashes.
	torrent.ParseTrackerResponse (0, Dict ({ { "interval", "i1800e" }, { "peers", CompactPeers ({ 0x01, 0x02 }) } }));
	assert (torrent.GetNumPeers (0) == 2);
	assert (torrent.GetInterval (0) == 1800 * 1000);

	// List-of-dicts (I2P base64 "ip"): one valid pair plus a malformed
	// base64
	// value, a dictionary without "ip" and a truncated encoded hash. Each
	// bad
	// entry is skipped individually.
	std::string raw0 (i2p::data::IdentHash::len, '\0');
	std::string raw1 (i2p::data::IdentHash::len, '\0');
	raw0[0] = 0x21;
	raw0.back () = 0x11;
	raw1[0] = 0x22;
	raw1.back () = 0x12;
	const auto encoded0 = EncodeHash (raw0);
	const auto encoded1 = EncodeHash (raw1);
	const std::string peers = "l" + PeerEntry (encoded0) + PeerEntry (encoded1) + PeerEntry ("not-base64!!")
		+ Dict ({ { "port", "i6881e" } }) + PeerEntry (encoded1.substr (0, encoded1.size () - 4)) + "e";
	torrent.ParseTrackerResponse (1, Dict ({ { "peers", peers } }));
	assert (torrent.GetNumPeers (1) == 2);

	// A malformed list is rejected without keeping partial peer state.
	torrent.ParseTrackerResponse (1, Dict ({ { "peers", "l" + PeerEntry (encoded0) } }));
	assert (torrent.GetNumPeers (1) == 0);

	// Peer cap: entries above the engine bound are ignored, not buffered.
	std::string many = "l";
	for (size_t i = 0; i < MAX_PEERS_PER_TRACKER_RESPONSE + 25; ++i)
	{
		std::string hash (i2p::data::IdentHash::len, '\0');
		hash[0] = static_cast<char> (1 + (i % 250));
		hash[1] = static_cast<char> (i / 250);
		many += PeerEntry (EncodeHash (hash));
	}
	many += "e";
	torrent.ParseTrackerResponse (2, Dict ({ { "peers", many } }));
	assert (torrent.GetNumPeers (2) == static_cast<int> (MAX_PEERS_PER_TRACKER_RESPONSE));

	// The datagram tracker path obeys the same bound and skips zero
	// hashes.
	std::vector<uint8_t> hashes (i2p::data::IdentHash::len * 3, 0);
	torrent.HandleDatagramTrackerResponse (
		3, 900, hashes.data (), hashes.size (), -1, std::numeric_limits<int>::max ());
	assert (torrent.GetNumPeers (3) == 0); // all zero destinations are skipped
	assert (torrent.GetNumSeeders (3) == 0 && torrent.GetNumLeechers (3) > 0);
}

// Minimum interval and the opaque tracker id must be honoured, and an
// oversized body must be refused before parsing.
static void TestTrackerIntervalAndTrackerID ()
{
	Torrent::InfoHash infoHash { };
	infoHash[0] = 0x33;
	Torrent torrent (infoHash);

	torrent.ParseTrackerResponse (0,
		Dict ({ { "complete", "i7e" }, { "incomplete", "i3e" }, { "interval", "i60e" }, { "min interval", "i600e" },
			{ "tracker id", Bytes ("opaque-id") } }));
	assert (torrent.GetMinInterval (0) == 600 * 1000);
	assert (torrent.GetInterval (0) == MIN_TRACKER_ANNOUNCE_INTERVAL * 1000); // 15-minute floor
	assert (torrent.GetTrackerID (0) == "opaque-id");
	assert (torrent.GetNumSeeders (0) == 7 && torrent.GetNumLeechers (0) == 3);

	// An unusable interval stays clamped into the supported range.
	torrent.ParseTrackerResponse (1, Dict ({ { "interval", "i-5e" }, { "min interval", "i-5e" } }));
	assert (torrent.GetInterval (1) >= MIN_TRACKER_REQUESTS_INTERVAL);
	assert (torrent.GetMinInterval (1) >= MIN_TRACKER_REQUESTS_INTERVAL);
	assert (torrent.GetTrackerID (1).empty ());

	// An over-long tracker id is dropped instead of being echoed back.
	const std::string longID (MAX_TRACKER_ID_LENGTH + 1, 'x');
	torrent.ParseTrackerResponse (2, Dict ({ { "tracker id", Bytes (longID) } }));
	assert (torrent.GetTrackerID (2).empty ());

	// Oversized bodies are rejected without touching previously known
	// peers.
	torrent.ParseTrackerResponse (0, Dict ({ { "peers", CompactPeers ({ 0x01 }) } }));
	assert (torrent.GetNumPeers (0) == 1);
	const std::string oversized (MAX_TRACKER_RESPONSE_BODY + 1, 'x');
	torrent.ParseTrackerResponse (0, Dict ({ { "peers", Bytes (oversized) } }));
	assert (torrent.GetNumPeers (0) == 1);
	assert (!torrent.GetTrackerError (0).empty ());

	// A non-bencode HTTP 200 body is a failure, not a successful announce.
	torrent.ParseTrackerResponse (0, "<html>not bencode</html>");
	assert (!torrent.GetTrackerError (0).empty ());
	assert (torrent.GetNumPeers (0) == 1); // previous good list retained

	// Transport/parse failures use the bounded retry ladder and a
	// successful response without an interval re-bases the announce to the
	// 15-minute floor.
	const int before = torrent.GetInterval (3);
	torrent.ReportTrackerFailure (3, "request failed");
	assert (torrent.GetInterval (3) >= before);
	assert (!torrent.GetTrackerError (3).empty ());
	torrent.ParseTrackerResponse (3, Dict ({ { "peers", CompactPeers ({ 0x11 }) } }));
	assert (torrent.GetInterval (3) == MIN_TRACKER_ANNOUNCE_INTERVAL * 1000);
	assert (torrent.GetTrackerError (3).empty ());

	// A tracker's peer list expires after its bounded freshness window.
	torrent.ParseTrackerResponse (4, Dict ({ { "interval", "i60e" }, { "peers", CompactPeers ({ 0x12 }) } }));
	assert (torrent.GetNumPeers (4) == 1);
	torrent.ExpireTrackerPeers (torrent.GetLastTrackerUpdateTime (4) + 2 * 3600 + 1);
	assert (torrent.GetNumPeers (4) == 0);
}

// Query values (peer id, tracker id) must be percent-encoded because
// URL::create_query() does not encode parameters.
static void TestQueryValueEncoding ()
{
	const std::string peerID ("-I2PD-0123456789abcd", 20);
	assert (PercentEncodeQueryValue (peerID) == peerID); // unreserved bytes survive
	assert (PercentEncodeQueryValue (std::string_view ("a b&c=d/e?f%g", 13)) == "a%20b%26c%3Dd%2Fe%3Ff%25g");
	const std::string binary ("\x00\x01\x7F\xFF", 4);
	assert (PercentEncodeQueryValue (binary) == "%00%01%7F%FF");
	assert (PercentEncodeQueryValue ("").empty ());
	assert (PercentEncodeQueryValue ("~-._").size () == 4); // unreserved per RFC 3986
}

// Peer ids are per torrent and random: two torrents with the same info
// hash
// must not share one, and neither may be the info hash or identity
// hash.
static void TestPerTorrentPeerID ()
{
	auto first = MakeVerifiedTorrent ("peer-id", false).torrent;
	auto second = std::make_shared<Torrent> (first->GetInfoHash ());
	assert (first->GetInfoHash () == second->GetInfoHash ());
	const auto & id = first->GetPeerID ();
	assert (id.size () == 20);
	assert (std::memcmp (id.data (), "-I2PD-", 6) == 0);
	assert (std::memcmp (id.data (), second->GetPeerID ().data (), id.size ()) != 0);
	// The id is stable for the lifetime of the torrent.
	const auto copy = first->GetPeerID ();
	assert (std::memcmp (copy.data (), first->GetPeerID ().data (), id.size ()) == 0);
	assert (std::memcmp (id.data (), first->GetInfoHash ().data (), id.size ()) != 0);
	for (size_t i = 6; i < id.size (); ++i) assert (std::isalnum (id[i]) != 0);
}

// Connection limits must stay internally consistent: dials are the
// smaller bound and a session may hold more peers than a single
// torrent.
static void TestConnectionLimitRelations ()
{
	static_assert (MAX_DIALS_PER_TORRENT < MAX_PEERS_PER_TORRENT, "dials are bounded below the peer limit");
	static_assert (MAX_TOTAL_DIALS >= MAX_DIALS_PER_TORRENT, "global dial limit covers one torrent");
	static_assert (MAX_TOTAL_PEERS > MAX_PEERS_PER_TORRENT, "global limit exceeds the per-torrent limit");
	static_assert (
		MAX_PEERS_PER_TORRENT <= static_cast<size_t> (MAX_CONTROL_PEERS), "control snapshots can list a full torrent");
	static_assert (MAX_PEX_PEERS_PER_MESSAGE < MAX_PEX_TRACKED_PEERS, "one message is a subset of the tracked set");

	auto fixture = MakeVerifiedTorrent ("limits", false);
	assert (fixture.torrent->HasConnectionRoom ());
	// With no connections there is nothing to rotate and nothing to add.
	assert (!fixture.torrent->EvictIdleConnection ());
	assert (!fixture.torrent->AddConnection (nullptr));
	assert (fixture.torrent->GetConnections ().empty ());

	TorrentInfo info { };
	assert (info.maxPeers == 0 && info.peers == 0);
	TorrentsSnapshot snapshot { };
	assert (snapshot.maxPeersPerTorrent == 0 && snapshot.maxTotalPeers == 0);
	// The engine-internal recheck command keeps its own enum value.
	assert (TorrentCommandKind::Recheck != TorrentCommandKind::AddFile);
}

int main ()
{
	TestResumeVerificationGate ();
	TestMixedExistingFiles ();
	TestExistingFinalDataIsHashed ();
	TestTrackerPeerForms ();
	TestTrackerIntervalAndTrackerID ();
	TestQueryValueEncoding ();
	TestPerTorrentPeerID ();
	TestConnectionLimitRelations ();
	return 0;
}

#else
int main ()
{
	return 0;
}
#endif
