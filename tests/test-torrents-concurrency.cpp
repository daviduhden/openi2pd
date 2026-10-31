/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test torrents concurrency.
 *
 * Exercises concurrent piece publication and corruption against
 * readers, plus CreateBitfield/GetLeft/AreAllPiecesVerified
 * consistency. Intended for the manual TSan job and normal CTest.
 *
 */

// Torrent piece publication contract, suitable for a normal CTest run
// and
// for the manual TSan sanitizer job:
//
// The test uses the public Piece/Torrent API and is registered with
// CTest and the manual TSan lane.
//
// No local execution is claimed.

#include <atomic>
#include <cassert>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include <openssl/sha.h>

#include "Torrents.h"

#ifndef NO_TORRENTS

using namespace i2p::torrents;

namespace
{
std::string MinimalTwoPieceTorrent ()
{
	std::string hashes (2 * SHA_DIGEST_LENGTH, 'A');
	// d4:info d6:length i32768e 4:name 8:file.bin
	// 12:piece length i16384e 6:pieces 40:<hashes> e e
	return "d4:infod6:lengthi32768e4:name8:file.bin12:piece lengthi16384e6:pieces40:" + hashes + "ee";
}

// One thread publishes blocks / corrupts / re-requests, another
// observes the same piece. Every mutable field is under Piece::m_Mutex,
// so TSan must not report a race even though the raw data buffer is
// replaced repeatedly.
void TestPiecePublication ()
{
	std::vector<uint8_t> good (16384, 0x42);
	uint8_t hash[SHA_DIGEST_LENGTH];
	SHA1 (good.data (), good.size (), hash);

	Piece piece (16384, hash);
	std::atomic<bool> start { false };
	std::atomic<bool> done { false };

	std::thread writer (
		[&]
		{
			while (!start.load (std::memory_order_acquire)) std::this_thread::yield ();
			for (unsigned i = 0; i < 20000; i++)
			{
				auto [offset, len] = piece.GetNextBlockToRequest ();
				if (len) piece.BlockReceived (good.data (), good.size (), offset);
				if (piece.IsComplete ())
				{
					if (!piece.VerifyHash ()) piece.MarkCorrupt ();
				}
				piece.MarkCorrupt ();
			}
			done.store (true, std::memory_order_release);
		});

	std::thread reader (
		[&]
		{
			while (!start.load (std::memory_order_acquire)) std::this_thread::yield ();
			std::vector<uint8_t> copy;
			while (!done.load (std::memory_order_acquire))
			{
				(void)piece.IsComplete ();
				(void)piece.IsVerified ();
				(void)piece.HasData ();
				(void)piece.HasBlock (0);
				(void)piece.GetLastActivityTimestamp ();
				(void)piece.IsRequested ();
				if (piece.CopyBlock (0, 16384, copy)) assert (copy.size () == 16384);
			}
		});

	start.store (true, std::memory_order_release);
	writer.join ();
	reader.join ();
}

// CreateBitfield/GetLeft/AreAllPiecesVerified run on the service thread
// while
// another thread applies a resume-style bitfield and marks one piece
// corrupt.
void TestTorrentBitfieldConsistency ()
{
	Torrent torrent (MinimalTwoPieceTorrent ());
	assert (torrent.GetError () == eTorrentErrorNoError);
	assert (torrent.GetNumPieces () == 2);

	std::atomic<bool> start { false };
	std::atomic<bool> done { false };
	std::vector<uint8_t> allOnes (1, 0xC0);

	std::thread writer (
		[&]
		{
			while (!start.load (std::memory_order_acquire)) std::this_thread::yield ();
			for (unsigned i = 0; i < 20000; i++)
			{
				(void)torrent.ApplyBitfield (allOnes);
				torrent.GetPiece (0).MarkCorrupt ();
			}
			done.store (true, std::memory_order_release);
		});

	std::thread reader (
		[&]
		{
			while (!start.load (std::memory_order_acquire)) std::this_thread::yield ();
			while (!done.load (std::memory_order_acquire))
			{
				auto [bitfield, have] = torrent.CreateBitfield ();
				(void)have;
				assert (bitfield.size () == 1);
				(void)torrent.GetLeft ();
				(void)torrent.AreAllPiecesVerified ();
				uint64_t total = 0;
				for (int i = 0; i < (int)torrent.GetNumPieces (); i++) total += torrent.GetPiece (i).GetSize ();
				assert (total == torrent.GetLength ());
			}
		});

	start.store (true, std::memory_order_release);
	writer.join ();
	reader.join ();
}
}

int main ()
{
	TestPiecePublication ();
	TestTorrentBitfieldConsistency ();
	return 0;
}

#else

int main ()
{
	return 0;
}

#endif
