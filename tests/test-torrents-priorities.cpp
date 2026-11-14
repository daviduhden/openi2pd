/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Torrent file selection and boundary-piece regression tests.
 *
 * Also pins the file-selection helpers that promotion and reporting
 * depend on: the fail-closed piece invalidation for a file whose payload
 * is missing, the swarm-availability bitmap merge (clamped to the local
 * piece count), the throttled routine resume update and the exact status
 * wire tokens the TUI classifies. No stream, timer, thread or filesystem
 * payload is involved; the resume write goes through an injected
 * persistence callback.
 */

#include <cassert>
#include <span>
#include <string>
#include <vector>
#include "Torrents.h"
#ifndef NO_TORRENTS
int main ()
{
	using namespace i2p::torrents;
	std::string bytes (MIN_PIECE_LENGTH, 'a');
	Torrent::InfoHash hash;
	SHA1 (reinterpret_cast<const uint8_t *> (bytes.data ()), bytes.size (), hash.data ());
	auto torrent = Torrent::CreateV1 ("selection", { { "a", 8192 }, { "b", 16384 }, { "c", 24576 }, { "empty", 0 } },
		MIN_PIECE_LENGTH, { hash, hash, hash });
	assert (torrent && torrent->GetFilePriorities () == std::vector<uint8_t> ({ 1, 1, 1, 1 }));
	assert (torrent->GetSelectedWantedBytes () == 49152 && !torrent->IsSelectedComplete ());
	assert (!torrent->SetFilePriority (10, 1) && !torrent->SetFilePriority (0, 3));
	assert (torrent->GetFilePriority (10) == -1);
	assert (!torrent->SetFilePriorities ({ 1 }));
	assert (torrent->SetFilePriorities ({ 2, 0, 1, 0 }));
	assert (torrent->GetPiecePriority (0) == 2);
	assert (torrent->GetPiecePriority (1) == 1 && torrent->GetPiecePriority (2) == 1);
	assert (torrent->GetSelectedWantedBytes () == 32768);
	assert (torrent->SetFilePriorities ({ 0, 1, 0, 0 }));
	assert (torrent->GetPiecePriority (0) == 1 && torrent->GetPiecePriority (1) == 1);
	assert (torrent->GetPiecePriority (2) == 0 && torrent->GetSelectedWantedBytes () == 16384);
	boost::dynamic_bitset<> skipped (3);
	skipped.set (2);
	assert (!torrent->HasIncompletePieces (skipped));
	skipped.set (0);
	assert (torrent->HasIncompletePieces (skipped));
	// A resume bit alone is not selected-data verification.
	torrent->ApplyBitfield ({ 0xC0 });
	assert (!torrent->IsSelectedComplete () && torrent->GetSelectedVerifiedBytes () == 0);
	for (int index = 0; index < 2; ++index)
	{
		auto & piece = torrent->GetPiece (index);
		piece.MarkCorrupt ();
		auto [offset, length] = piece.GetNextBlockToRequest ();
		assert (offset == 0 && length == bytes.size ());
		assert (piece.BlockReceived (reinterpret_cast<const uint8_t *> (bytes.data ()), length, offset));
		// A duplicate/unsolicited block for a complete piece changes nothing;
		// it must not force a re-hash or disk rewrite.
		assert (!piece.BlockReceived (reinterpret_cast<const uint8_t *> (bytes.data ()), length, offset));
		assert (piece.IsComplete () && !piece.IsSaving ());
		assert (piece.VerifyHash ());
		assert (torrent->GetSelectedVerifiedBytes () == static_cast<size_t> ((index + 1) * 8192));
	}
	assert (torrent->IsSelectedComplete ());
	assert (torrent->GetLeft () == 0 && torrent->GetFullLeft () == 16384);
	assert (torrent->IsFileComplete (1));
	torrent->SetSelectedComplete ();
	assert (torrent->IsComplete () && torrent->GetStatus () == eTorrentStatusSeeding);
	assert (!torrent->AreAllPiecesVerified () && torrent->GetFullLeft () == 16384);
	assert (!torrent->GetPiece (2).IsComplete () && !torrent->GetPiece (2).IsVerified ());
	// A boundary piece includes skipped bytes and must retain both
	// fragments.
	auto fragments = torrent->GetPieceFileFragments (0);
	assert (fragments.size () == 2 && fragments[0].fragmentSize == 8192 && fragments[1].fragmentSize == 8192);
	assert (torrent->SetFilePriorities ({ 0, 0, 0, 0 }));
	assert (torrent->IsSelectedComplete () && torrent->GetSelectedWantedBytes () == 0);
	assert (torrent->GetSelectedVerifiedBytes () == 0);
	assert (!torrent->AreAllPiecesVerified ());
	assert (torrent->GetLeft () == 0);
	// Tracker progress is whole-torrent based even after selected
	// completion:
	// "left" counts the skipped piece and event=completed is withheld,
	// while
	// the local status above already reports seeding.
	auto announce = torrent->GetAnnounceProgress ();
	assert (announce.left == 16384 && !announce.completed);
	// Once the last piece is present and verified, the same helper reports
	// the whole payload as complete without changing any selection-based
	// value.
	{
		auto & piece = torrent->GetPiece (2);
		piece.MarkCorrupt (); // create the block map of a never-requested piece
		auto [offset, length] = piece.GetNextBlockToRequest ();
		assert (offset == 0 && length == bytes.size ());
		assert (piece.BlockReceived (reinterpret_cast<const uint8_t *> (bytes.data ()), length, offset));
		assert (piece.VerifyHash ());
	}
	auto finished = torrent->GetAnnounceProgress ();
	assert (finished.left == 0 && finished.completed);
	assert (torrent->GetLeft () == 0);

	// --- status wire tokens ------
	// ------------------------------------------- The token table is the
	// single authority for the exact strings the native TUI and the
	// Android client classify; a change here is a protocol change.
	assert (TorrentStatusToken (eTorrentStatusStopped) == "stopped");
	assert (TorrentStatusToken (eTorrentStatusQueuedToVerifyLocalData) == "queued-verify");
	assert (TorrentStatusToken (eTorrentStatusVerifyingLocalData) == "verifying");
	assert (TorrentStatusToken (eTorrentStatusQueuedToDownload) == "queued");
	assert (TorrentStatusToken (eTorrentStatusDownloading) == "downloading");
	assert (TorrentStatusToken (eTorrentStatusQueuedToSeed) == "queued-seed");
	assert (TorrentStatusToken (eTorrentStatusSeeding) == "seeding");
	// An out-of-range value is never rendered as a runnable state.
	assert (TorrentStatusToken (static_cast<TorrentStatus> (99)) == "unknown");

	// --- swarm availability bitmap -------------
	// ------------------------------------ Peers may advertise a piece count
	// that differs from ours. The merge copies only in-range set bits,
	// never resizes the placeholder and leaves spare bits clear so the
	// result is directly encodable as a BEP 3 bitfield.
	{
		auto swarm = Torrent::CreateV1 (
			"availability", { { "", 3 * MIN_PIECE_LENGTH } }, MIN_PIECE_LENGTH, { hash, hash, hash });
		assert (swarm && swarm->GetNumPieces () == 3);
		auto available = swarm->CreateAvailableBitfield ();
		assert (available.size () == 1 && available[0] == 0x00);
		boost::dynamic_bitset<> remote (8); // larger than the local piece set
		remote.set (0);
		remote.set (2);
		remote.set (5); // outside the local piece count
		MergeAvailableBitfield (available, swarm->GetNumPieces (), remote);
		assert (available[0] == 0xA0); // 1010 0000, spare bits clear
		// A second merge is additive and idempotent.
		MergeAvailableBitfield (available, swarm->GetNumPieces (), remote);
		assert (available[0] == 0xA0);
		// A metadata-less torrent and a mismatched placeholder are refused
		// instead of allocating or resizing.
		std::vector<uint8_t> none;
		MergeAvailableBitfield (none, 0, remote);
		assert (none.empty ());
		std::vector<uint8_t> wrong (2, 0x00);
		MergeAvailableBitfield (wrong, 3, remote);
		assert (wrong[0] == 0x00 && wrong[1] == 0x00);
		// Availability is a swarm claim, not local progress.
		assert (!swarm->GetPiece (0).IsComplete () && !swarm->GetPiece (2).IsComplete ());
	}

	// --- missing payload fails closed -------------
	// -------------------------------------- Promotion may only mark a file
	// complete when its payload exists. When neither the .part file nor
	// the final name is present, the covering pieces become downloadable
	// again and completion is withdrawn instead of publishing missing
	// data.
	{
		// Both pieces are hashed from the metainfo digest, so file
		// completion is a real claim before it is withdrawn.
		auto missing = Torrent::CreateV1 ("invalidate",
			{ { "first", MIN_PIECE_LENGTH }, { "second", MIN_PIECE_LENGTH }, { "empty", 0 } }, MIN_PIECE_LENGTH,
			{ hash, hash });
		assert (missing && missing->GetNumPieces () == 2);
		for (int index = 0; index < 2; ++index)
		{
			auto & piece = missing->GetPiece (index);
			piece.MarkCorrupt ();
			auto [offset, length] = piece.GetNextBlockToRequest ();
			assert (offset == 0 && length == bytes.size ());
			assert (piece.BlockReceived (reinterpret_cast<const uint8_t *> (bytes.data ()), length, offset));
			assert (piece.VerifyHash ());
		}
		missing->SetComplete ();
		assert (missing->IsComplete () && missing->IsFileComplete (0) && missing->IsFileComplete (1));
		// A zero-length file has no payload and no covering pieces.
		assert (!missing->InvalidateFilePieces (2));
		assert (!missing->InvalidateFilePieces (7));
		assert (missing->InvalidateFilePieces (0));
		assert (!missing->GetPiece (0).IsComplete ());
		assert (!missing->GetPiece (0).IsVerified ());
		// The neighbour keeps its own complete piece; the shared boundary
		// case is covered by the one-piece-per-file layout above.
		assert (missing->GetPiece (1).IsComplete ());
		assert (!missing->IsComplete ());
		assert (!missing->IsFileComplete (0));
	}

	// --- throttled routine resume persistence --------------
	// -------------------------------- A verified piece must not force one
	// atomic replace plus fsync per piece. The throttle is monotonic, and
	// the explicit save used by stop, removal, completion and shutdown is
	// never throttled.
	{
		auto progress = Torrent::CreateV1 ("resume", { { "", MIN_PIECE_LENGTH } }, MIN_PIECE_LENGTH, { hash });
		assert (progress && progress->GetNumPieces () == 1);
		progress->SetFullPath ("resume-throttle-test");
		unsigned saves = 0;
		bool lastRemove = false;
		progress->SetResumePersistence (
			[&] (std::span<const uint8_t> bitfield, bool remove)
			{
				// No piece is complete yet, so the persisted action is a
				// removal of a stale .resume file.
				assert (bitfield.size () == 1);
				lastRemove = remove;
				++saves;
				return true;
			});
		const uint64_t start = 1000000;
		progress->UpdateTorrentResumeFile (start);
		assert (saves == 1);
		assert (lastRemove);
		progress->UpdateTorrentResumeFile (start + TORRENT_RESUME_UPDATE_INTERVAL - 1);
		assert (saves == 1); // inside the window
		progress->UpdateTorrentResumeFile (start + TORRENT_RESUME_UPDATE_INTERVAL);
		assert (saves == 2);
		// An explicit save bypasses the throttle.
		progress->SaveTorrentResumeFile ();
		assert (saves == 3);
		progress->UpdateTorrentResumeFile (start + 2 * TORRENT_RESUME_UPDATE_INTERVAL);
		assert (saves == 4);
		// A monotonic regression larger than one window re-bases instead of
		// suppressing persistence until the stale deadline.
		progress->UpdateTorrentResumeFile (10);
		assert (saves == 5);
	}
	return 0;
}
#else
int main ()
{
	return 0;
}
#endif
