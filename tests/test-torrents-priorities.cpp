/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Torrent file selection and boundary-piece regression tests.
 */

#include <cassert>
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
		piece.BlockReceived (reinterpret_cast<const uint8_t *> (bytes.data ()), length, offset);
		assert (piece.VerifyHash ());
		assert (torrent->GetSelectedVerifiedBytes () == static_cast<size_t> ((index + 1) * 8192));
	}
	assert (torrent->IsSelectedComplete ());
	assert (!torrent->IsComplete () && !torrent->AreAllPiecesVerified ());
	assert (!torrent->GetPiece (2).IsComplete () && !torrent->GetPiece (2).IsVerified ());
	// A boundary piece includes skipped bytes and must retain both fragments.
	auto fragments = torrent->GetPieceFileFragments (0);
	assert (fragments.size () == 2 && fragments[0].fragmentSize == 8192 && fragments[1].fragmentSize == 8192);
	assert (torrent->SetFilePriorities ({ 0, 0, 0, 0 }));
	assert (torrent->IsSelectedComplete () && torrent->GetSelectedWantedBytes () == 0);
	assert (torrent->GetSelectedVerifiedBytes () == 0);
	assert (!torrent->AreAllPiecesVerified ());
	return 0;
}
#else
int main ()
{
	return 0;
}
#endif
