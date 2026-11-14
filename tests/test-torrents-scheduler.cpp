/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Torrent transfer-scheduling, accounting and progress tests.
 *
 * Pins the socket-free rules behind the peer scheduler: outbound
 * request
 * expiry, epoch-stale block release, actual-length block accounting,
 * verified-completion progress and idle rate decay, plus the bounded
 * best-effort corrupt-peer counters.
 *
 * Everything here is pure arithmetic, metadata or piece state. No
 * stream, io_context, timer, thread or filesystem payload is involved,
 * so the connection-level call sites (ReleaseExpiredOutgoingRequests,
 * UpdateIdleRates, the disk write error path and the hash-mismatch
 * charge) are exercised only through the helpers they use. Those call
 * sites still need a live peer/tunnel run and remain statically
 * reviewed only.
 */

#include <cassert>
#include <chrono>
#include <cstdint>
#include <algorithm>
#include <stdexcept>
#include <set>
#include <string>
#include <vector>
#include "Torrents.h"

#ifndef NO_TORRENTS

namespace
{
using namespace i2p::torrents;

i2p::data::IdentHash MakePeer (uint32_t id)
{
	// Distinct, deterministic identities without a random source.
	i2p::data::IdentHash peer { };
	peer[0] = static_cast<uint8_t> (id >> 24);
	peer[1] = static_cast<uint8_t> (id >> 16);
	peer[2] = static_cast<uint8_t> (id >> 8);
	peer[3] = static_cast<uint8_t> (id);
	return peer;
}

Torrent::InfoHash HashBytes (const char * data, size_t len)
{
	Torrent::InfoHash hash;
	SHA1 (reinterpret_cast<const uint8_t *> (data), len, hash.data ());
	return hash;
}

class MemoryPayload final : public TorrentPayloadStorage
{
public:
	std::string bytes;
	size_t reads = 0;
	bool shortRead = false, throwRead = false;
	std::function<void ()> onRead;
	bool UsesFinalNames () const override { return true; }
	uint64_t Open (const std::filesystem::path &, uint64_t length, bool, std::string &) override
	{
		return length == bytes.size () ? 1 : 0;
	}
	bool Read (uint64_t handle, uint64_t offset, std::span<uint8_t> out, std::string &) override
	{
		++reads;
		if (throwRead) throw std::runtime_error ("injected provider failure");
		if (handle != 1 || offset > bytes.size () || out.size () > bytes.size () - offset) return false;
		const auto count = shortRead ? out.size () / 2 : out.size ();
		std::copy_n (bytes.begin () + static_cast<size_t> (offset), count, out.begin ());
		if (onRead) onRead ();
		return !shortRead;
	}
	bool Write (uint64_t, uint64_t, std::span<const uint8_t>, std::string &) override { return false; }
	bool Close (uint64_t, std::string &) override { return true; }
};

void TestUploadLoadQuota ()
{
	{
		UploadLoadLedger ledger, other;
		const RequestedBlock block { 1, 0, 1 };
		auto ticket = ledger.Acquire (block, 7, 1);
		assert (ticket && ticket->epoch == 7 && ticket->bytes == 1);
		assert (!ledger.Acquire (block, 8, 1));
		assert (!other.Release (ticket));
		assert (!ledger.Cancel ({ 1, 0, 2 })); // CANCEL length must match.
		assert (ledger.Cancel (block) && !ledger.Cancel (block));
		assert (ledger.IsCancelled (block) && ledger.Size () == 1);
		assert (!ledger.Acquire (block, 8, 1)); // Cancel does not free quota.
		for (uint32_t index = 2; index <= MAX_INCOMING_REQUESTS_QUEUE_SIZE; ++index)
			assert (ledger.Acquire ({ index, 0, 1 }, 7, 1));
		assert (!ledger.Acquire ({ 99, 0, 1 }, 7, 1));
		assert (ledger.Release (ticket) && !ledger.Release (ticket));
		auto replacement = ledger.Acquire (block, 8, 1);
		assert (replacement && !ledger.Release (ticket));
		ledger.CancelAll ();
		assert (replacement->cancelled.load () && ledger.Size () == MAX_INCOMING_REQUESTS_QUEUE_SIZE);
		ledger.Close ();
		assert (ledger.Release (replacement));
		assert (!ledger.Acquire ({ 99, 0, 1 }, 8, 1)); // Shutdown never reopens admission.
	}
	{
		// Registry retirement and even owner destruction do not release
		// global bytes while a queued callback still owns the old ticket.
		auto ledger = std::make_unique<UploadLoadLedger> ();
		auto held = ledger->Acquire ({ 1, 0, 1 }, 1, UploadLoadLedger::MaxGlobalBytes);
		assert (held);
		UploadLoadLedger next;
		assert (!next.Acquire ({ 2, 0, 1 }, 1, 1));
		assert (ledger->Release (held));
		assert (!next.Acquire ({ 2, 0, 1 }, 1, 1));
		ledger.reset ();
		assert (!next.Acquire ({ 2, 0, 1 }, 1, 1));
		held.reset ();
		auto abandoned = next.Acquire ({ 2, 0, 1 }, 1, UploadLoadLedger::MaxGlobalBytes);
		assert (abandoned);
	}
	{
		UploadLoadLedger::Ticket abandoned;
		{
			UploadLoadLedger retiring;
			abandoned = retiring.Acquire ({ 3, 0, 1 }, 1, UploadLoadLedger::MaxGlobalBytes);
			assert (abandoned);
		}
		assert (abandoned->cancelled.load ());
		UploadLoadLedger next;
		assert (!next.Acquire ({ 4, 0, 1 }, 1, 1));
		abandoned.reset ();
		assert (next.Acquire ({ 4, 0, 1 }, 1, 1));
	}
	{
		std::vector<std::unique_ptr<UploadLoadLedger>> ledgers;
		for (size_t index = 0; index < UploadLoadLedger::MaxGlobalLoads; ++index)
		{
			if (index % MAX_INCOMING_REQUESTS_QUEUE_SIZE == 0)
				ledgers.push_back (std::make_unique<UploadLoadLedger> ());
			assert (ledgers.back ()->Acquire ({ static_cast<uint32_t> (index), 0, 1 }, 1, 1));
		}
		UploadLoadLedger overflow;
		assert (!overflow.Acquire ({ 0, 0, 1 }, 1, 1));
		ledgers.clear ();
		assert (overflow.Acquire ({ 0, 0, 1 }, 1, 1));
	}
}

void TestUploadScratchPublication ()
{
	auto storage = std::make_shared<MemoryPayload> ();
	storage->bytes.assign (8192, 'u');
	const auto hash = HashBytes (storage->bytes.data (), storage->bytes.size ());
	Piece piece (storage->bytes.size (), hash.data ());
	piece.Complete ();
	auto file = std::make_shared<TorrentFile> ("/virtual/upload", storage->bytes.size ());
	file->SetPayloadStorage (storage, "/virtual");
	const std::vector<PieceFileFragment> fragments { { file, 0, 0, 4096 }, { file, 4096, 4096, 4096 } };
	std::vector<uint8_t> block;
	std::atomic<bool> cancelled { false };
	const auto epoch = piece.GetBlockEpoch ();
	auto read = [&] (std::span<const PieceFileFragment> input)
	{ return piece.ReadVerifiedBlock (input, 0, 4096, epoch, cancelled, block); };
	const std::vector<PieceFileFragment> shortCoverage { { file, 0, 0, 4096 } };
	const std::vector<PieceFileFragment> gap { { file, 0, 0, 4096 }, { file, 4096, 4097, 4095 } };
	const std::vector<PieceFileFragment> overlap { { file, 0, 0, 4096 }, { file, 4096, 4095, 4097 } };
	assert (!read (shortCoverage) && !read (gap) && !read (overlap));
	assert (storage->reads == 0 && !piece.HasData () && !piece.IsVerified ());
	storage->shortRead = true;
	assert (!read (fragments) && block.empty () && !piece.HasData () && !piece.IsVerified ());
	storage->shortRead = false;
	storage->throwRead = true;
	assert (!read (fragments) && !piece.HasData ());
	storage->throwRead = false;
	storage->bytes[0] = 'x';
	assert (!read (fragments) && !piece.HasData () && !piece.IsVerified ());
	storage->bytes[0] = 'u';
	storage->onRead = [&] { cancelled.store (true); };
	assert (!read (fragments) && !piece.HasData () && !piece.IsVerified ());
	storage->onRead = { };
	cancelled.store (false);
	assert (read (fragments) && piece.HasData () && piece.IsVerified ());
	assert (block == std::vector<uint8_t> (4096, 'u'));
	const auto cachedReads = storage->reads;
	assert (read (fragments) && storage->reads == cachedReads); // No per-block rehash/IO.
	assert (!read (gap) && piece.IsVerified ()); // Bad input preserves the cache.
	cancelled.store (true);
	assert (!read (fragments) && piece.IsVerified ());
	cancelled.store (false);
	piece.ReleaseData ();
	storage->onRead = [&] { piece.MarkPendingVerification (); };
	assert (!read (fragments) && !piece.HasData () && !piece.IsVerified ());
}

void TestUploadSlotPolicy ()
{
	std::vector<UploadSlotPolicy::Peer> peers;
	for (uint32_t index = 1; index <= 16; ++index) peers.push_back (MakePeer (index));
	UploadSlotPolicy policy;
	assert (policy.Update ({ }, 0).empty ());
	auto slots = policy.Update (peers, 100);
	assert (slots.size () == UploadSlotPolicy::MaxSlots);
	for (uint32_t index = 1; index <= 8; ++index) assert (policy.Selected (MakePeer (index)));
	assert (!policy.Selected (MakePeer (9)));
	std::reverse (peers.begin (), peers.end ()); // Registry iteration order is irrelevant.
	policy.Update (peers, 129);
	assert (policy.Selected (MakePeer (1)) && !policy.Selected (MakePeer (9)));
	policy.Update (peers, 130);
	for (uint32_t index = 9; index <= 16; ++index) assert (policy.Selected (MakePeer (index)));
	assert (!policy.Selected (MakePeer (1)));
	policy.Update (peers, 160);
	assert (policy.Selected (MakePeer (1)) && !policy.Selected (MakePeer (9)));

	// Interest loss/removal frees a slot immediately without restarting
	// every incumbent. New peers cannot reroll the cohort before expiry.
	std::erase (peers, MakePeer (1));
	std::erase (peers, MakePeer (2));
	policy.Update (peers, 161);
	assert (!policy.Selected (MakePeer (1)) && !policy.Selected (MakePeer (2)));
	for (uint32_t index = 3; index <= 10; ++index) assert (policy.Selected (MakePeer (index)));
	peers.push_back (MakePeer (17));
	policy.Update (peers, 162);
	assert (!policy.Selected (MakePeer (17)) && policy.Selected (MakePeer (3)));
	assert (policy.Update (peers, 189).size () == UploadSlotPolicy::MaxSlots);
	assert (policy.Selected (MakePeer (3)));
	policy.Update (peers, 190);
	assert (policy.Selected (MakePeer (17)));

	// A backward clock sample rebases the next rotation, never underflows.
	policy.Reset ();
	policy.Update (peers, 100);
	auto sample = policy.Update (peers, 80);
	std::vector<UploadSlotPolicy::Peer> cohort (sample.begin (), sample.end ());
	auto same = policy.Update (peers, 109);
	assert (std::equal (cohort.begin (), cohort.end (), same.begin (), same.end ()));
	policy.Update (peers, 110);
	assert (!policy.Selected (cohort.back ()));

	// Authenticated identity deduplication does not create extra slots.
	std::vector<UploadSlotPolicy::Peer> duplicates (MAX_PEERS_PER_TORRENT, MakePeer (7));
	assert (policy.Update (duplicates, 200).size () == 1);
	assert (policy.Selected (MakePeer (7)));
	duplicates.push_back (MakePeer (8));
	assert (policy.Update (duplicates, 201).empty ()); // Oversized input fails closed.
	assert (!policy.Selected (MakePeer (7)));
	assert (policy.Update ({ }, 202).empty ());

	// Stable full capacity eventually serves every eligible identity.
	peers.clear ();
	for (uint32_t index = 1; index <= MAX_PEERS_PER_TORRENT; ++index) peers.push_back (MakePeer (index));
	std::set<UploadSlotPolicy::Peer> seen;
	for (uint64_t round = 0; round < MAX_PEERS_PER_TORRENT / UploadSlotPolicy::MaxSlots; ++round)
	{
		auto selected = policy.Update (peers, round * UploadSlotPolicy::RotationSeconds);
		assert (selected.size () == UploadSlotPolicy::MaxSlots);
		seen.insert (selected.begin (), selected.end ());
	}
	assert (seen.size () == MAX_PEERS_PER_TORRENT);
	policy.Reset ();
	assert (policy.Update (peers, 1).front () == MakePeer (1));

	// Choke revocation acknowledges each still-pending Fast request once.
	// Previously canceled jobs retain quota but owe no second rejection.
	UploadLoadLedger ledger;
	const RequestedBlock first { 1, 0, 1 }, second { 2, 0, 1 };
	assert (ledger.Acquire (first, 1, 1) && ledger.Acquire (second, 1, 1));
	assert (ledger.Cancel (first));
	const auto revoked = ledger.CancelForChoke ();
	assert (revoked == std::vector<RequestedBlock> ({ second }));
	assert (ledger.Size () == 2 && ledger.CancelForChoke ().empty ());
	assert (!ledger.Cancel (second));
	assert (ledger.Acquire ({ 3, 0, 1 }, 1, 1)); // A later unchoke can admit work.
}
} // namespace

int main ()
{
	TestUploadLoadQuota ();
	TestUploadScratchPublication ();
	TestUploadSlotPolicy ();
	// --- request expiry boundaries
	// -----------------------------------------
	// requestedAt is monotonic seconds; the boundary is inclusive.
	constexpr uint64_t requestedAt = 1000;
	assert (!ShouldExpireOutgoingRequest (0, requestedAt + 1000, PEER_REQUEST_TIMEOUT)); // unknown send time
	assert (!ShouldExpireOutgoingRequest (requestedAt, requestedAt, 0)); // no deadline configured
	assert (!ShouldExpireOutgoingRequest (requestedAt, requestedAt + PEER_REQUEST_TIMEOUT - 1, PEER_REQUEST_TIMEOUT));
	assert (ShouldExpireOutgoingRequest (requestedAt, requestedAt + PEER_REQUEST_TIMEOUT, PEER_REQUEST_TIMEOUT));
	assert (ShouldExpireOutgoingRequest (requestedAt, requestedAt + PEER_REQUEST_TIMEOUT + 1, PEER_REQUEST_TIMEOUT));
	// A clock that appears to move backwards must never expire a fresh
	// request.
	assert (!ShouldExpireOutgoingRequest (requestedAt, requestedAt - 1, PEER_REQUEST_TIMEOUT));
	// The per-request deadline has to fire before the blanket keep-alive
	// release, otherwise the stalled-block case is never reached.
	assert (PEER_REQUEST_TIMEOUT < PEER_KEEP_SEND_INTERVAL);
	assert (PEER_REQUEST_TIMEOUT > 0);

	// --- idle rate decay
	// ----------------------------------------------------
	assert (DecayPeerRate (0, 0) == 0);
	assert (DecayPeerRate (0, 10 * PEER_RATE_DECAY_INTERVAL) == 0);
	assert (DecayPeerRate (1000, 0) == 1000);
	assert (DecayPeerRate (1000, PEER_RATE_DECAY_INTERVAL - 1) == 1000);
	assert (DecayPeerRate (1000, PEER_RATE_DECAY_INTERVAL) == 500);
	assert (DecayPeerRate (1000, 2 * PEER_RATE_DECAY_INTERVAL - 1) == 500);
	// Two idle windows zero the rate instead of keeping the last burst.
	assert (DecayPeerRate (1000, 2 * PEER_RATE_DECAY_INTERVAL) == 0);
	assert (DecayPeerRate (1000, 10 * PEER_RATE_DECAY_INTERVAL) == 0);
	// Integer halving has a floor; it must not wrap.
	assert (DecayPeerRate (1, PEER_RATE_DECAY_INTERVAL) == 0);
	// Compare durations, not the numeric seconds and milliseconds.
	assert (
		std::chrono::seconds (PEER_RATE_DECAY_INTERVAL) > std::chrono::milliseconds (BANDWIDTH_RATE_SAMPLING_INTERVAL));

	// --- verified-completion rule
	// ------------------------------------------
	assert (IsPieceProgressComplete (true, true, false));
	assert (!IsPieceProgressComplete (true, false, false)); // downloaded, not hashed
	assert (!IsPieceProgressComplete (true, true, true)); // write still in flight
	assert (!IsPieceProgressComplete (false, true, false));
	assert (!IsPieceProgressComplete (false, false, false));

	// --- actual-length block accounting
	// ------------------------------------ A single 8192-byte file becomes
	// one 8192-byte piece, whose only block is shorter than the nominal
	// block size. That wire length is what a peer and the torrent must be
	// charged.
	std::string small (8192, 'x');
	auto single = Torrent::CreateV1 (
		"single", { { "", small.size () } }, MIN_PIECE_LENGTH, { HashBytes (small.data (), small.size ()) });
	assert (single && single->GetNumPieces () == 1);

	Piece & lastPiece = single->GetPiece (0);
	lastPiece.MarkCorrupt (); // reset to a never-downloaded block map
	auto [finalOffset, finalLength] = lastPiece.GetNextBlockToRequest ();
	assert (finalOffset == 0 && finalLength == small.size ());
	assert (finalLength < REQUEST_BLOCK_SIZE);
	assert (BlockTransferBytes (finalLength) == small.size ());
	assert (BlockTransferBytes (finalLength) != REQUEST_BLOCK_SIZE);
	// A full block is still accounted as itself.
	assert (BlockTransferBytes (REQUEST_BLOCK_SIZE) == REQUEST_BLOCK_SIZE);

	// --- epoch-stale release is a no-op
	// ------------------------------------ A recheck/invalidation bumps
	// the piece epoch. An expiry that carries the old epoch must not free
	// a block that the new epoch requested.
	const auto staleEpoch = lastPiece.GetBlockEpoch ();
	lastPiece.MarkCorrupt ();
	auto [againOffset, againLength] = lastPiece.GetNextBlockToRequest ();
	assert (againOffset == 0 && againLength == small.size ());
	assert (lastPiece.GetBlockEpoch () != staleEpoch);
	lastPiece.ReleaseBlock (againOffset, staleEpoch);
	auto [heldOffset, heldLength] = lastPiece.GetNextBlockToRequest ();
	assert (heldOffset == 0 && heldLength == 0); // still Requested under the current epoch
	// The current epoch does release it, so recovery is possible.
	lastPiece.ReleaseBlock (againOffset, lastPiece.GetBlockEpoch ());
	auto [releasedOffset, releasedLength] = lastPiece.GetNextBlockToRequest ();
	assert (releasedOffset == 0 && releasedLength == small.size ());
	assert (
		lastPiece.BlockReceived (reinterpret_cast<const uint8_t *> (small.data ()), releasedLength, releasedOffset));
	assert (lastPiece.VerifyHash ());

	// --- per-file progress with empty, shared and saving pieces
	// ------------ a[8192] + empty[0] + b[16384] + c[8192] over two
	// 16384-byte pieces, so every file boundary case (empty file, piece
	// shared by two files) appears.
	std::string payload;
	payload.reserve (32768);
	payload.append (8192, 'a');
	payload.append (16384, 'b');
	payload.append (8192, 'c');
	auto torrent = Torrent::CreateV1 ("scheduler", { { "a", 8192 }, { "empty", 0 }, { "b", 16384 }, { "c", 8192 } },
		MIN_PIECE_LENGTH,
		{ HashBytes (payload.data (), MIN_PIECE_LENGTH),
			HashBytes (payload.data () + MIN_PIECE_LENGTH, MIN_PIECE_LENGTH) });
	assert (torrent && torrent->GetNumPieces () == 2 && torrent->GetFiles ().size () == 4);

	// Freshly created pieces are incomplete and never verified, so no file
	// may claim progress yet (this is the 100%-while-withheld regression).
	assert (!torrent->GetPiece (0).IsComplete () && !torrent->GetPiece (0).IsVerified ());
	assert (torrent->GetFilesCompleted () == std::vector<size_t> ({ 0, 0, 0, 0 }));
	assert (!torrent->IsFileComplete (0) && !torrent->IsFileComplete (2));
	// A zero-length file needs no piece and is complete by definition.
	assert (torrent->IsFileComplete (1) && torrent->GetFilesCompleted ()[1] == 0);

	Piece & first = torrent->GetPiece (0);
	first.MarkCorrupt ();
	auto [firstOffset, firstLength] = first.GetNextBlockToRequest ();
	assert (firstOffset == 0 && firstLength == MIN_PIECE_LENGTH);
	assert (first.BlockReceived (reinterpret_cast<const uint8_t *> (payload.data ()), firstLength, firstOffset));
	// Present but unhashed: still no progress.
	assert (first.IsComplete () && !first.IsVerified ());
	assert (torrent->GetFilesCompleted () == std::vector<size_t> ({ 0, 0, 0, 0 }));
	assert (first.VerifyHash ());
	// Piece 0 holds all of a and the first half of b; the empty file is 0
	// and c has not started.
	assert (torrent->GetFilesCompleted () == std::vector<size_t> ({ 8192, 0, 8192, 0 }));
	assert (torrent->IsFileComplete (0)); // fully covered by a verified piece
	assert (torrent->IsFileComplete (1)); // zero-length file needs no piece
	assert (!torrent->IsFileComplete (2) && !torrent->IsFileComplete (3));

	Piece & second = torrent->GetPiece (1);
	second.MarkCorrupt ();
	auto [secondOffset, secondLength] = second.GetNextBlockToRequest ();
	assert (secondOffset == 0 && secondLength == MIN_PIECE_LENGTH);
	assert (second.BlockReceived (
		reinterpret_cast<const uint8_t *> (payload.data () + MIN_PIECE_LENGTH), secondLength, secondOffset));
	// Complete but unverified again changes nothing.
	assert (torrent->GetFilesCompleted () == std::vector<size_t> ({ 8192, 0, 8192, 0 }));
	assert (second.VerifyHash ());
	assert (torrent->GetFilesCompleted () == std::vector<size_t> ({ 8192, 0, 16384, 8192 }));
	assert (torrent->IsFileComplete (2) && torrent->IsFileComplete (3));
	assert (torrent->AreAllPiecesVerified ());

	// A write still in flight withholds the same progress again.
	second.SetIsSaving (true);
	assert (torrent->GetFilesCompleted () == std::vector<size_t> ({ 8192, 0, 8192, 0 }));
	assert (!torrent->IsFileComplete (2));
	second.SetIsSaving (false);
	assert (torrent->GetFilesCompleted () == std::vector<size_t> ({ 8192, 0, 16384, 8192 }));

	// --- bounded best-effort corrupt-peer counters
	// -------------------------
	assert (Torrent::PEER_PENALTY_THRESHOLD == 3);
	assert (Torrent::MAX_PENALIZED_PEERS == 64);
	const auto bad = MakePeer (1);
	assert (torrent->GetPeerPieceFailures (bad) == 0 && !torrent->IsPeerPenalized (bad));
	assert (torrent->ChargePieceFailure (bad) == 1 && !torrent->IsPeerPenalized (bad));
	assert (torrent->ChargePieceFailure (bad) == 2 && !torrent->IsPeerPenalized (bad));
	assert (torrent->ChargePieceFailure (bad) == 3 && torrent->IsPeerPenalized (bad));
	// A penalized peer keeps counting; only the disconnect threshold is
	// new.
	assert (torrent->ChargePieceFailure (bad) == 4 && torrent->IsPeerPenalized (bad));
	const auto other = MakePeer (2);
	assert (torrent->ChargePieceFailure (other) == 1 && !torrent->IsPeerPenalized (other));
	assert (torrent->GetNumPieceFailurePeers () == 2);

	uint32_t nextPeerId = 100;
	while (torrent->GetNumPieceFailurePeers () < Torrent::MAX_PENALIZED_PEERS)
		assert (torrent->ChargePieceFailure (MakePeer (nextPeerId++)) == 1);
	assert (torrent->GetNumPieceFailurePeers () == Torrent::MAX_PENALIZED_PEERS);
	// The table is bounded and never evicts: a new peer is simply not
	// tracked.
	assert (torrent->ChargePieceFailure (MakePeer (nextPeerId++)) == 0);
	assert (torrent->GetNumPieceFailurePeers () == Torrent::MAX_PENALIZED_PEERS);
	assert (!torrent->IsPeerPenalized (MakePeer (nextPeerId)));
	// Already tracked peers still accumulate at capacity.
	assert (torrent->ChargePieceFailure (bad) == 5);

	return 0;
}

#else

int main ()
{
	return 0;
}

#endif
