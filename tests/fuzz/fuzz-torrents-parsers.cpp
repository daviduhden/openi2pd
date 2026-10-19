/*
* Copyright (c) 2026, David Uhden Collado
* SPDX-License-Identifier: BSD-3-Clause
*
* This file is part of openi2pd and licensed under BSD-3-Clause
*
* See full license text in LICENSE file at top of project tree
*/

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "Torrents.h"
#include "TorrentsControl.h"
#include "TorrentsDHT.h"

#ifndef NO_TORRENTS

extern "C" int LLVMFuzzerTestOneInput (const uint8_t * data, size_t size)
{
	std::string_view input (reinterpret_cast<const char *> (data), size);

	// Bencode primitives and the metainfo parser share these entry points.
	(void)i2p::torrents::ExtractByteString (input);
	(void)i2p::torrents::ExtractInteger (input);
	{
		auto dictionary = i2p::torrents::ExtractDictionary (input);
		(void)dictionary;
	}
	(void)i2p::torrents::ParseDictionary (input);
	(void)i2p::torrents::ParseList (input);
	{
		auto strings = i2p::torrents::ParseStringList (input);
		(void)strings;
	}

	static_assert (i2p::torrents::MAX_TORRENT_LENGTH == (uint64_t { 256 } << 40),
		"MAX_TORRENT_LENGTH must be a 64-bit constant");
	static_assert (i2p::torrents::MAX_PEER_MSG_LEN + 4 == i2p::torrents::PEER_CONNECTION_RECEIVE_BUFFER_SIZE,
		"peer frame must fit the receive buffer");

	// The 4-byte peer-wire length prefix must be rejected before it can be
	// stored as a frame length. The maximum representable length and 65535 in
	// particular do not fit together with the prefix.
	{
		uint32_t wireLen = 0;
		if (size >= 4)
			wireLen = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8)
				| (uint32_t)data[3];
		const bool allowed = i2p::torrents::IsPeerMessageLengthAllowed (wireLen);
		if (wireLen == 0xFFFF || wireLen > i2p::torrents::MAX_PEER_MSG_LEN) assert (!allowed);
		assert (i2p::torrents::IsPeerMessageLengthAllowed (i2p::torrents::MAX_PEER_MSG_LEN));
	}

	// Bound the work for giant corpora; the RPC path uses the same limit.
	if (size <= i2p::torrents::MAX_CONTROL_FILE_BYTES)
	{
		i2p::torrents::Torrent torrent (input);
		if (torrent.GetError () == i2p::torrents::eTorrentErrorNoError)
		{
			(void)torrent.CreateTorrentFileContent ();
			(void)torrent.GetLeft ();
			auto [bitfield, have] = torrent.CreateBitfield ();
			(void)have;

			// Bitfield accounting must match the piece count exactly and the
			// unused tail bits of the last byte must stay zero.
			const size_t numPieces = torrent.GetNumPieces ();
			const size_t expectedSize = (numPieces + 7) / 8;
			assert (bitfield.size () == expectedSize);
			size_t counted = 0;
			uint64_t pieceTotal = 0;
			for (size_t i = 0; i < numPieces; i++)
			{
				const auto & piece = torrent.GetPiece (static_cast<int> (i));
				pieceTotal += piece.GetSize ();
				const bool bit = (bitfield[i / 8] & (0x80 >> (i % 8))) != 0;
				if (bit) counted++;
				assert (bit == piece.IsComplete ());
			}
			assert (counted <= numPieces);
			assert (pieceTotal == torrent.GetLength ());
			if (expectedSize)
			{
				for (size_t i = numPieces; i < expectedSize * 8; i++)
					assert ((bitfield[i / 8] & (0x80 >> (i % 8))) == 0);
			}

			// A resume-style bitfield is unverified input. Applying it may
			// mark pieces complete, but it must not claim verification.
			std::vector<uint8_t> claimed (expectedSize, 0xFF);
			(void)torrent.ApplyBitfield (claimed);
			if (numPieces) assert (!torrent.AreAllPiecesVerified ());
			if (numPieces)
			{
				torrent.GetPiece (0).MarkCorrupt ();
				assert (!torrent.GetPiece (0).IsComplete ());
				auto [rebuilt, rebuiltHave] = torrent.CreateBitfield ();
				(void)rebuiltHave;
				assert (rebuilt.size () == expectedSize);
				assert ((rebuilt[0] & 0x80) == 0);
			}
		}
	}

	// Exercise the peer-wire block path with attacker-controlled offset and
	// length. The Piece bounds checks must reject everything outside the piece.
	static constexpr uint8_t zeroHash[SHA_DIGEST_LENGTH] = { };
	i2p::torrents::Piece piece (16384, zeroHash);
	(void)piece.GetNextBlockToRequest ();
	size_t offset = 0;
	if (size >= 2) offset = ((size_t)data[0] << 8) | data[1];
	piece.BlockReceived (data, size, offset);
	if (piece.IsComplete () && !piece.VerifyHash ())
	{
		piece.MarkCorrupt ();
		assert (!piece.IsComplete ());
		auto [nextOffset, nextLen] = piece.GetNextBlockToRequest ();
		(void)nextOffset;
		assert (nextLen == 16384);
	}

	// DHT node records are fixed-size and parsed from untrusted datagrams.
	if (size >= sizeof (i2p::torrents::NodeInfo))
	{
		i2p::torrents::NodeInfo nodeInfo;
		memcpy (nodeInfo.data (), data, nodeInfo.size ());
		i2p::torrents::Node node (nodeInfo);
		(void)node.VerifyID ();
		(void)node.GetNodeInfo ();
	}

	// A torrent hash extracted from the input must not be able to grow a
	// DHTTorrent without bound.
	if (size >= i2p::data::IdentHash::len)
	{
		i2p::torrents::DHTTorrent dhtTorrent;
		for (size_t i = 0; i + i2p::data::IdentHash::len <= size; i += i2p::data::IdentHash::len)
		{
			i2p::data::IdentHash peer (data + i);
			dhtTorrent.AddPeer (peer);
		}
		(void)dhtTorrent.GetBEncodedPeers ();
	}

	return 0;
}

#else

extern "C" int LLVMFuzzerTestOneInput (const uint8_t *, size_t)
{
	return 0;
}

#endif
