/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Torrent transfer-scheduling, accounting and progress tests.
 *
 * Pins the socket-free rules behind the peer scheduler: outbound request
 * expiry, epoch-stale block release, actual-length block accounting,
 * verified-completion progress and idle rate decay, plus the bounded
 * best-effort corrupt-peer counters.
 *
 * Everything here is pure arithmetic, metadata or piece state. No stream,
 * io_context, timer, thread or filesystem payload is involved, so the
 * connection-level call sites (ReleaseExpiredOutgoingRequests,
 * UpdateIdleRates, the disk write error path and the hash-mismatch charge)
 * are exercised only through the helpers they use. Those call sites still
 * need a live peer/tunnel run and remain statically reviewed only.
 */

#include <cassert>
#include <cstdint>
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
} // namespace

int main ()
{
	// --- request expiry boundaries -----------------------------------------
	// requestedAt is monotonic seconds; the boundary is inclusive.
	constexpr uint64_t requestedAt = 1000;
	assert (!ShouldExpireOutgoingRequest (0, requestedAt + 1000, PEER_REQUEST_TIMEOUT)); // unknown send time
	assert (!ShouldExpireOutgoingRequest (requestedAt, requestedAt, 0)); // no deadline configured
	assert (!ShouldExpireOutgoingRequest (requestedAt, requestedAt + PEER_REQUEST_TIMEOUT - 1, PEER_REQUEST_TIMEOUT));
	assert (ShouldExpireOutgoingRequest (requestedAt, requestedAt + PEER_REQUEST_TIMEOUT, PEER_REQUEST_TIMEOUT));
	assert (ShouldExpireOutgoingRequest (requestedAt, requestedAt + PEER_REQUEST_TIMEOUT + 1, PEER_REQUEST_TIMEOUT));
	// A clock that appears to move backwards must never expire a fresh request.
	assert (!ShouldExpireOutgoingRequest (requestedAt, requestedAt - 1, PEER_REQUEST_TIMEOUT));
	// The per-request deadline has to fire before the blanket keep-alive
	// release, otherwise the stalled-block case is never reached.
	assert (PEER_REQUEST_TIMEOUT < PEER_KEEP_SEND_INTERVAL);
	assert (PEER_REQUEST_TIMEOUT > 0);

	// --- idle rate decay ----------------------------------------------------
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
	// The decay window is a scheduler tick multiple, not the 20 ms sampler.
	assert (PEER_RATE_DECAY_INTERVAL > BANDWIDTH_RATE_SAMPLING_INTERVAL);

	// --- verified-completion rule ------------------------------------------
	assert (IsPieceProgressComplete (true, true, false));
	assert (!IsPieceProgressComplete (true, false, false)); // downloaded, not hashed
	assert (!IsPieceProgressComplete (true, true, true)); // write still in flight
	assert (!IsPieceProgressComplete (false, true, false));
	assert (!IsPieceProgressComplete (false, false, false));

	// --- actual-length block accounting ------------------------------------
	// A single 8192-byte file becomes one 8192-byte piece, whose only block is
	// shorter than the nominal block size. That wire length is what a peer and
	// the torrent must be charged.
	std::string small (8192, 'x');
	auto single = Torrent::CreateV1 ("single", { { "", small.size () } }, MIN_PIECE_LENGTH,
		{ HashBytes (small.data (), small.size ()) });
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

	// --- epoch-stale release is a no-op ------------------------------------
	// A recheck/invalidation bumps the piece epoch. An expiry that carries the
	// old epoch must not free a block that the new epoch requested.
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
	assert (lastPiece.BlockReceived (reinterpret_cast<const uint8_t *> (small.data ()), releasedLength, releasedOffset));
	assert (lastPiece.VerifyHash ());

	// --- per-file progress with empty, shared and saving pieces ------------
	// a[8192] + empty[0] + b[16384] + c[8192] over two 16384-byte pieces, so
	// every file boundary case (empty file, piece shared by two files) appears.
	std::string payload;
	payload.reserve (32768);
	payload.append (8192, 'a');
	payload.append (16384, 'b');
	payload.append (8192, 'c');
	auto torrent = Torrent::CreateV1 ("scheduler", { { "a", 8192 }, { "empty", 0 }, { "b", 16384 }, { "c", 8192 } },
		MIN_PIECE_LENGTH,
		{ HashBytes (payload.data (), MIN_PIECE_LENGTH), HashBytes (payload.data () + MIN_PIECE_LENGTH, MIN_PIECE_LENGTH) });
	assert (torrent && torrent->GetNumPieces () == 2 && torrent->GetFiles ().size () == 4);

	// Freshly created pieces are incomplete and never verified, so no file may
	// claim progress yet (this is the 100%-while-withheld regression).
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
	// Piece 0 holds all of a and the first half of b; the empty file is 0 and
	// c has not started.
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

	// --- bounded best-effort corrupt-peer counters -------------------------
	assert (Torrent::PEER_PENALTY_THRESHOLD == 3);
	assert (Torrent::MAX_PENALIZED_PEERS == 64);
	const auto bad = MakePeer (1);
	assert (torrent->GetPeerPieceFailures (bad) == 0 && !torrent->IsPeerPenalized (bad));
	assert (torrent->ChargePieceFailure (bad) == 1 && !torrent->IsPeerPenalized (bad));
	assert (torrent->ChargePieceFailure (bad) == 2 && !torrent->IsPeerPenalized (bad));
	assert (torrent->ChargePieceFailure (bad) == 3 && torrent->IsPeerPenalized (bad));
	// A penalized peer keeps counting; only the disconnect threshold is new.
	assert (torrent->ChargePieceFailure (bad) == 4 && torrent->IsPeerPenalized (bad));
	const auto other = MakePeer (2);
	assert (torrent->ChargePieceFailure (other) == 1 && !torrent->IsPeerPenalized (other));
	assert (torrent->GetNumPieceFailurePeers () == 2);

	uint32_t nextPeerId = 100;
	while (torrent->GetNumPieceFailurePeers () < Torrent::MAX_PENALIZED_PEERS)
		assert (torrent->ChargePieceFailure (MakePeer (nextPeerId++)) == 1);
	assert (torrent->GetNumPieceFailurePeers () == Torrent::MAX_PENALIZED_PEERS);
	// The table is bounded and never evicts: a new peer is simply not tracked.
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
