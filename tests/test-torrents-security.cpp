/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test torrents security.
 *
 * Adversarial torrent checks: 64-bit size limits, piece-block bounds
 * and stopped-torrent rejection, peer framing, corrupt pieces and
 * resume bitfields, metadata copy avoidance, and DHT routing-table
 * ownership.
 *
 */

#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
#include <boost/json.hpp>

#include "Torrents.h"
#include "TorrentsControl.h"
#include "TorrentsDHT.h"
#include "TorrentsTunnel.h"
#include "TorrentsRPC.h"
#include "TorrentsState.h"
#include "Modern.h"

#ifndef NO_TORRENTS

using namespace i2p::torrents;

namespace i2p::torrents
{
struct TorrentRecoveryTestAccess
{
	static void Load (TorrentsTunnel & tunnel)
	{
		tunnel.m_StartupRecoveryPending.store (true);
		tunnel.LoadInitialTorrents ();
	}
};

struct TorrentInboundAcceptTestAccess
{
	static auto Begin (TorrentsTunnel & tunnel)
	{
		// Exercise accept generations without starting disk workers.
		{
			std::lock_guard<std::mutex> publication (tunnel.m_InboundAcceptMutex);
			tunnel.m_InboundAcceptStopped = false;
		}
		tunnel.m_Stopping.store (false);
		return tunnel.CreateInboundAcceptor ();
	}
	static void BeginDials (TorrentsTunnel & tunnel)
	{
		{
			std::lock_guard<std::mutex> publication (tunnel.m_InboundAcceptMutex);
			tunnel.m_InboundAcceptStopped = false;
		}
		tunnel.m_Stopping = false;
		tunnel.StartPeerDials ();
	}
	static auto Dial (TorrentsTunnel & tunnel, std::shared_ptr<Torrent> torrent)
	{
		return tunnel.CreatePeerDialCompletion (torrent, i2p::data::IdentHash { }, ePeerConnectionOriginTracker);
	}
	static size_t PendingDials (TorrentsTunnel & tunnel) { return tunnel.m_PendingDials.size (); }
	static void RetireDials (TorrentsTunnel & tunnel) { tunnel.m_PendingDials.clear (); }
	static auto DialAdmissionLock (TorrentsTunnel & tunnel)
	{
		return std::unique_lock<std::mutex> (tunnel.m_PeerDialGeneration->mutex);
	}
	static void ReplaceDialOwner (TorrentsTunnel & tunnel, const std::shared_ptr<Torrent> & torrent)
	{
		assert (tunnel.m_PendingDials.size () == 1);
		tunnel.m_PendingDials.begin ()->second.torrent = torrent;
	}
	static void ExhaustDials (TorrentsTunnel & tunnel)
	{
		tunnel.m_NextDialID = std::numeric_limits<uint64_t>::max ();
	}
	static auto AdmissionLock (TorrentsTunnel & tunnel)
	{
		return std::unique_lock<std::mutex> (tunnel.m_InboundAcceptGeneration->mutex);
	}
	static void Revoke (TorrentsTunnel & tunnel) { tunnel.RevokeInboundAccepts (); }
	static auto Register (TorrentsTunnel & tunnel) { return tunnel.CreateInboundAcceptor (); }
	static size_t Handlers (TorrentsTunnel & tunnel)
	{
		return tunnel.CountPeerHandlers ();
	}
};

struct TorrentDiscoveryTestAccess
{
	static void Prepare (TorrentsTunnel & tunnel, const std::shared_ptr<Torrent> & torrent,
		bool ready = false, bool loaded = true)
	{
		tunnel.m_Stopping = false;
		tunnel.m_StartupRecoveryPending = false;
		tunnel.m_Torrents.clear ();
		tunnel.m_TorrentsByID.clear ();
		tunnel.m_AdminState.clear ();
		tunnel.m_Torrents.emplace (torrent->GetInfoHash (), torrent);
		auto & state = tunnel.m_AdminState[torrent->GetInfoHash ()];
		state.loaded = loaded;
		state.initialDiscoveryPending = true;
		state.initialDiscoveryReady = ready;
		state.initialDiscoveryOwner = torrent;
	}
	static void Ready (TorrentsTunnel & tunnel, const std::shared_ptr<Torrent> & torrent)
	{
		tunnel.m_AdminState.at (torrent->GetInfoHash ()).initialDiscoveryReady = true;
	}
	static void Loaded (TorrentsTunnel & tunnel, const std::shared_ptr<Torrent> & torrent)
	{
		tunnel.m_AdminState.at (torrent->GetInfoHash ()).loaded = true;
	}
	static void PendingWrite (TorrentsTunnel & tunnel, const std::shared_ptr<Torrent> & torrent, bool pending)
	{
		tunnel.m_AdminState.at (torrent->GetInfoHash ()).pending = pending;
	}
	static bool Dispatch (TorrentsTunnel & tunnel, const std::shared_ptr<Torrent> & torrent,
		const std::function<void ()> & trackers, const std::function<void ()> & dht)
	{
		return tunnel.DispatchInitialDiscovery (torrent, trackers, dht);
	}
	static void Replace (TorrentsTunnel & tunnel, const std::shared_ptr<Torrent> & torrent)
	{
		tunnel.m_Torrents[torrent->GetInfoHash ()] = torrent;
	}
	static bool PendingInitial (TorrentsTunnel & tunnel, const std::shared_ptr<Torrent> & torrent)
	{
		return tunnel.m_AdminState.at (torrent->GetInfoHash ()).initialDiscoveryPending;
	}
	static void Activity (TorrentsTunnel & tunnel, bool stopped, bool recovering)
	{
		tunnel.m_Stopping = stopped;
		tunnel.m_StartupRecoveryPending = recovering;
	}
	static void Finish (TorrentsTunnel & tunnel)
	{
		tunnel.m_Stopping = true;
		tunnel.m_Torrents.clear ();
		tunnel.m_AdminState.clear ();
	}
};
}

// The size constant must be computed in 64 bits even on ILP32, where
// the product would otherwise wrap to zero.
static_assert (MAX_TORRENT_LENGTH == (uint64_t { 256 } << 40), "limit must stay at 256 TiB");
static_assert (MAX_TORRENT_LENGTH > std::numeric_limits<uint32_t>::max (), "limit must exceed 32-bit size_t");

namespace
{
template<class Predicate>
void WaitForRPC (Predicate predicate)
{
	const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (5);
	while (!predicate ())
	{
		assert (std::chrono::steady_clock::now () < deadline);
		std::this_thread::yield ();
	}
}

std::string ReadClosedRPC (boost::asio::ip::tcp::socket & socket)
{
	socket.non_blocking (true);
	std::string response;
	WaitForRPC ([&] ()
		{
			char buffer[1024];
			boost::system::error_code error;
			auto count = socket.read_some (boost::asio::buffer (buffer), error);
			response.append (buffer, count);
			assert (response.size () < 65536);
			if (error == boost::asio::error::would_block || error == boost::asio::error::try_again) return false;
			if (!error) return false;
			assert (error == boost::asio::error::eof || error == boost::asio::error::connection_reset);
			return true;
		});
	return response;
}

void TestRPCAdmissionAndRestart ()
{
	boost::asio::io_context clientService;
	TorrentsRPCServer server ("127.0.0.1", 0);
	server.AddTunnel ("", nullptr, true, "user", "private-marker");
	assert (server.GetAuth ("/rpc").first && server.GetAuth ("/rpc/").first);
	server.AddTunnel ("", nullptr, false);
	assert (!server.GetAuth ("/rpc").first && server.GetAuth ("/rpc/").second.empty ());
	const auto endpoint = server.GetEndpoint ();
	server.Start ();
	server.Start (); // repeated start must not create another accept chain
	std::vector<boost::asio::ip::tcp::socket> clients;
	for (size_t i = 0; i < TorrentsRPCServer::MAX_SESSIONS; ++i)
	{
		clients.emplace_back (clientService);
		clients.back ().connect (endpoint);
		// Count publication, rather than a sleep, synchronizes admission.
		WaitForRPC ([&] { return server.GetSessionCount () == i + 1; });
	}
	boost::asio::ip::tcp::socket excess (clientService);
	excess.connect (endpoint);
	assert (ReadClosedRPC (excess).empty ());
	assert (server.GetSessionCount () == TorrentsRPCServer::MAX_SESSIONS);
	server.Stop ();
	assert (server.GetSessionCount () == 0);
	for (auto & client : clients) assert (ReadClosedRPC (client).empty ());
	server.Stop ();
	server.Start ();
	boost::asio::ip::tcp::socket restarted (clientService);
	restarted.connect (endpoint);
	const std::string request = "GET /rpc HTTP/1.1\r\nHost: localhost\r\n\r\n";
	boost::asio::write (restarted, boost::asio::buffer (request));
	assert (ReadClosedRPC (restarted).find ("405") != std::string::npos);
	WaitForRPC ([&] { return server.GetSessionCount () == 0; });
	server.Stop ();

	boost::asio::ip::tcp::socket orphan (clientService);
	{
		TorrentsRPCServer scoped ("127.0.0.1", 0);
		scoped.Start ();
		orphan.connect (scoped.GetEndpoint ());
		WaitForRPC ([&] { return scoped.GetSessionCount () == 1; });
	} // destruction must close accepted sockets and queued completions
	assert (ReadClosedRPC (orphan).empty ());
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

std::string MakeSingleFileTorrent ()
{
	std::string hashes (SHA_DIGEST_LENGTH, 'A');
	return Dict ({ { "announce", Bytes ("http://tracker.i2p/announce") },
		{ "info",
			Dict ({ { "length", "i16384e" }, { "name", Bytes ("file.bin") }, { "piece length", "i16384e" },
				{ "pieces", Bytes (hashes) } }) } });
}

std::string StringList (std::initializer_list<std::string_view> items)
{
	std::string result ("l");
	for (const auto & item : items) result += Bytes (item);
	result += "e";
	return result;
}

std::string MakeMultiFileTorrent (std::string_view name, std::initializer_list<std::string_view> pathComponents,
	size_t fileLength = 16384, size_t numPieces = 1)
{
	std::string hashes (numPieces * SHA_DIGEST_LENGTH, 'A');
	auto file
		= Dict ({ { "length", "i" + std::to_string (fileLength) + "e" }, { "path", StringList (pathComponents) } });
	return Dict ({ { "info",
		Dict ({ { "files", "l" + file + "e" }, { "name", Bytes (name) }, { "piece length", "i16384e" },
			{ "pieces", Bytes (hashes) } }) } });
}

std::filesystem::path MakeTempDir ()
{
	auto base = std::filesystem::temp_directory_path ();
	std::error_code ec;
	for (unsigned i = 0; i < 1000; i++)
	{
		auto candidate = base / ("i2pd-torrent-security-" + std::to_string (i));
		if (std::filesystem::create_directory (candidate, ec)) return candidate;
		ec.clear ();
	}
	assert (false);
	return { };
}

struct TempDir
{
	std::filesystem::path path;
	~TempDir ()
	{
		std::error_code ec;
		std::filesystem::remove_all (path, ec);
	}
};

void TestI2PTrackerHostValidation ()
{
	assert (IsI2PTrackerHost ("tracker.i2p"));
	assert (IsI2PTrackerHost ("tracker.example.i2p"));
	assert (IsI2PTrackerHost ("TRACKER.I2P")); // host names are case-insensitive
	assert (!IsI2PTrackerHost ("tracker.example"));
	assert (!IsI2PTrackerHost ("tracker.i2p.example"));
	assert (!IsI2PTrackerHost ("tracker.i2p."));
	assert (!IsI2PTrackerHost (".i2p"));
	assert (!IsI2PTrackerHost ("tracker..i2p"));
	assert (!IsI2PTrackerHost ("tracker.i2p/path"));
	assert (!IsI2PTrackerHost ("tracker.i2p:80"));
	assert (!IsI2PTrackerHost ("user@tracker.i2p"));
	assert (!IsI2PTrackerHost (std::string ("tracker.i2p\0evil", 16)));
}

// A peer-selected offset/length must never be able to index outside the
// piece block map or wrap the bounds check.
void TestPieceBlockBounds ()
{
	uint8_t hash[SHA_DIGEST_LENGTH] = { };
	{
		Piece piece (16384, hash);
		auto [offset, len] = piece.GetNextBlockToRequest ();
		assert (offset == 0 && len == 16384);
		std::vector<uint8_t> block (len, 0x42);
		piece.BlockReceived (block.data (), block.size (), offset);
		assert (piece.IsComplete ());
	}
	{
		Piece piece (16384, hash);
		auto [offset, len] = piece.GetNextBlockToRequest ();
		assert (len == 16384);
		uint8_t block[8] = { };
		// Offset beyond the piece would wrap `offset + len` on a 32-bit
		// size_t in the old code.
		piece.BlockReceived (block, sizeof (block), std::numeric_limits<size_t>::max () - 4);
		assert (!piece.IsComplete ());
		// Length beyond the piece and an offset exactly at the end.
		piece.BlockReceived (block, len + 1, offset);
		assert (!piece.IsComplete ());
		piece.BlockReceived (block, 1, piece.GetSize ());
		assert (!piece.IsComplete ());
		// A NULL source must be ignored rather than dereferenced.
		piece.BlockReceived (nullptr, 1, 0);
		assert (!piece.IsComplete ());
		// A one-byte response for a requested 16 KiB block must not mark the
		// whole block available (which would expose uninitialised heap).
		uint8_t partial[1] = { 0x42 };
		piece.BlockReceived (partial, sizeof (partial), offset);
		assert (!piece.HasBlock (offset));
		assert (!piece.IsComplete ());
	}
}

// A block is accepted only while the torrent is running and only when
// the peer-supplied offset/length is a non-empty, in-range slice of the
// actual piece. The helper uses the piece size, not the nominal piece
// length, so the short final piece is handled without accepting bytes
// beyond it.
void TestCanAcceptPieceBlock ()
{
	// 20000 bytes at a 16 KiB piece length produces a 16384-byte first
	// piece
	// and a 3616-byte final piece.
	Torrent torrent (MakeMultiFileTorrent ("block-bounds", { "file.bin" }, 20000, 2));
	assert (torrent.GetError () == eTorrentErrorNoError);
	assert (torrent.GetNumPieces () == 2);
	assert (torrent.GetPiece (0).GetSize () == 16384);
	assert (torrent.GetPiece (1).GetSize () == 3616);
	assert (!torrent.IsStopped ());

	// Running: valid full and final-piece blocks are accepted.
	assert (torrent.CanAcceptPieceBlock (0, 0, 16384));
	assert (torrent.CanAcceptPieceBlock (0, 0, 1));
	assert (torrent.CanAcceptPieceBlock (1, 0, 3616));
	assert (torrent.CanAcceptPieceBlock (1, 3615, 1));

	// Running: empty, out-of-index, cross-boundary and wrapping ranges are
	// rejected by the same helper used by HandlePieceMsg.
	assert (!torrent.CanAcceptPieceBlock (0, 0, 0));
	assert (!torrent.CanAcceptPieceBlock (2, 0, 1));
	assert (!torrent.CanAcceptPieceBlock (0, 0, 16385));
	assert (!torrent.CanAcceptPieceBlock (0, 16384, 1));
	assert (!torrent.CanAcceptPieceBlock (0, 16383, 2));
	assert (!torrent.CanAcceptPieceBlock (1, 0, 3617));
	assert (!torrent.CanAcceptPieceBlock (1, 3616, 1));
	assert (!torrent.CanAcceptPieceBlock (0, std::numeric_limits<size_t>::max () - 4, 8));

	// Stopped: even a perfectly bounded block is rejected, and resuming
	// restores the bounds-only decision.
	torrent.SetStopped (true);
	assert (torrent.IsStopped ());
	assert (!torrent.CanAcceptPieceBlock (0, 0, 16384));
	assert (!torrent.CanAcceptPieceBlock (1, 0, 3616));
	torrent.SetStopped (false);
	assert (!torrent.IsStopped ());
	assert (torrent.CanAcceptPieceBlock (0, 0, 16384));
}

// A 65535-byte payload plus its 4-byte prefix does not fit in the
// receive buffer. It must be rejected before it is stored as
// m_NextMsgLength, which would otherwise wait forever for bytes that
// cannot be buffered.
void TestPeerMessageFraming ()
{
	static_assert (MAX_PEER_MSG_LEN + 4 == PEER_CONNECTION_RECEIVE_BUFFER_SIZE);
	assert (IsPeerMessageLengthAllowed (0)); // keep-alive
	assert (IsPeerMessageLengthAllowed (MAX_PEER_MSG_LEN));
	assert (!IsPeerMessageLengthAllowed (MAX_PEER_MSG_LEN + 1));
	assert (!IsPeerMessageLengthAllowed (65535));
	assert (!IsPeerMessageLengthAllowed (std::numeric_limits<uint32_t>::max ()));
}

// A complete piece whose bytes do not match the metainfo hash must be
// turned back into a Missing block map, not left complete with corrupt
// data. Reset() of a complete piece has the same contract so it can be
// downloaded again.
void TestPieceRequestRelease ()
{
	std::vector<uint8_t> block (2 * 16384, 0x33);
	uint8_t hash[SHA_DIGEST_LENGTH];
	SHA1 (block.data (), block.size (), hash);
	Piece piece (block.size (), hash);
	auto first = piece.GetNextBlockToRequest ();
	auto second = piece.GetNextBlockToRequest ();
	assert (std::get<0> (first) == 0 && std::get<0> (second) == 16384);
	const auto epoch = piece.GetBlockEpoch ();
	piece.ReleaseBlock (std::get<0> (first), epoch);
	auto released = piece.GetNextBlockToRequest ();
	assert (std::get<0> (released) == 0);
	piece.ReleaseBlock (std::get<0> (released), epoch);
	piece.ReleaseBlock (std::get<0> (second), epoch);
	assert (!piece.IsRequested ());
}

void TestPieceCorruptionAndReset ()
{
	std::vector<uint8_t> good (16384, 0x42);
	uint8_t hash[SHA_DIGEST_LENGTH];
	SHA1 (good.data (), good.size (), hash);

	{
		Piece piece (16384, hash);
		auto [offset, len] = piece.GetNextBlockToRequest ();
		assert (offset == 0 && len == good.size ());
		std::vector<uint8_t> wrong (len, 0x43);
		piece.BlockReceived (wrong.data (), wrong.size (), offset);
		assert (piece.IsComplete ());
		assert (!piece.VerifyHash ());
		assert (!piece.IsVerified ());

		piece.MarkCorrupt ();
		assert (!piece.IsComplete ());
		assert (!piece.HasBlock (0));
		auto [nextOffset, nextLen] = piece.GetNextBlockToRequest ();
		assert (nextLen == good.size ()); // downloadable again
		piece.BlockReceived (good.data (), good.size (), nextOffset);
		assert (piece.IsComplete ());
		assert (piece.VerifyHash ());
		assert (piece.IsVerified ());

		// Eviction keeps completion and the verification record (the disk
		// bytes were verified); Reset() forgets both.
		piece.ReleaseData ();
		assert (piece.IsComplete ());
		assert (!piece.HasData ());
		assert (piece.IsVerified ());
	}

	{
		Piece piece (16384, hash);
		auto [offset, len] = piece.GetNextBlockToRequest ();
		piece.BlockReceived (good.data (), good.size (), offset);
		assert (piece.IsComplete ());
		assert (piece.VerifyHash ());
		piece.Reset ();
		assert (!piece.IsComplete ());
		assert (!piece.IsVerified ());
		assert (!piece.HasBlock (0));
		auto [nextOffset, nextLen] = piece.GetNextBlockToRequest ();
		assert (nextLen == good.size ());
		(void)nextOffset;
	}
}

// A resume bitfield is unverified input. Applying it may keep complete
// pieces,
// but it must not make AreAllPiecesVerified() true, and an individual
// corrupt
// piece must remain recoverable.
void TestCorruptResumeBitfield ()
{
	Torrent torrent (MakeMultiFileTorrent ("resume-corrupt", { "file.bin" }, 32768, 2));
	assert (torrent.GetError () == eTorrentErrorNoError);
	std::vector<uint8_t> allOnes (1, 0xC0);
	assert (torrent.ApplyBitfield (allOnes));
	assert (!torrent.IsComplete ()); // Torrent-level completion is separate
	assert (!torrent.AreAllPiecesVerified ());

	auto [bitfield, have] = torrent.CreateBitfield ();
	// The tri-state is compared the way the engine itself reads it
	// (Torrent::SaveTorrentResumeFile): `indeterminate()` plus an explicit
	// boolean conversion. A tribool `!= false` yields another tribool, so
	// an indeterminate value would silently satisfy a `!= false` test. The
	// two conditions are split so a failure names the state that was seen.
	assert (!boost::logic::indeterminate (have));
	assert (static_cast<bool> (have));
	// Two pieces occupy the two high bits; unused tail bits stay zero.
	assert (bitfield.size () == 1 && bitfield[0] == 0xC0);

	torrent.GetPiece (0).MarkCorrupt ();
	assert (!torrent.GetPiece (0).IsComplete ());
	assert (!torrent.AreAllPiecesVerified ());
	auto [repaired, repairedHave] = torrent.CreateBitfield ();
	assert (repaired.size () == 1);
	assert (repaired[0] == 0x40); // first piece is Missing again
	assert (boost::logic::indeterminate (repairedHave));
}

// GetInfo() used to return a fresh copy of up to 20 MiB per ut_metadata
// request. It must expose the stored metadata by reference.
void TestInfoIsNotCopied ()
{
	Torrent torrent (MakeSingleFileTorrent ());
	assert (torrent.GetError () == eTorrentErrorNoError);
	const auto & first = torrent.GetInfo ();
	const auto & second = torrent.GetInfo ();
	assert (&first == &second);
	assert (!first.empty ());

	// Bitfields from a peer are indexed only up to the local piece count.
	torrent.SetComplete ();
	boost::dynamic_bitset<> oversized (torrent.GetNumPieces () + 8);
	oversized.set ();
	assert (!torrent.HasIncompletePieces (oversized));
	torrent.ApplyPeerRemoteBitfield (oversized);
}

void TestRoutingTableOwnership ()
{
	NodeID ourNode { };
	RoutingTable table (ourNode);
	assert (table.GetNumBuckets () == 1);

	auto addNode = [&table] (uint8_t first, uint8_t last)
	{
		NodeID id { };
		id[0] = first;
		id[19] = last;
		i2p::data::IdentHash peer { };
		peer[0] = first;
		// One node ID per destination reference: the table caps a peer at two
		// nodes, so the bucket-split fixture needs a distinct peer per node.
		peer[1] = last;
		return table.AddNode (std::make_shared<Node> (id, peer, 0));
	};

	// Force a split of the first bucket. All nodes sort above the middle
	// ID and move into the new bucket while our own node stays below it.
	for (uint8_t i = 0; i < MAX_BUCKET_CAPACITY; i++) assert (addNode (0x80, i));
	assert (table.GetNumBuckets () == 1);
	assert (addNode (0x80, static_cast<uint8_t> (MAX_BUCKET_CAPACITY + 1)) == nullptr);
	assert (table.GetNumBuckets () == 2);
	assert (table.GetNumNodes () == MAX_BUCKET_CAPACITY);

	// Expiring every node must leave a valid, empty head bucket behind.
	table.DeleteExpiredNodes (std::numeric_limits<uint64_t>::max ());
	assert (table.GetNumNodes () == 0);
	assert (table.GetNumBuckets () >= 1);
}

void TestDHTRequestBounds ()
{
	RequestInfo info (nullptr, 1);
	info.target.fill (0);
	for (size_t i = 0; i < MAX_DHT_REQUEST_NODES + 32; i++)
	{
		NodeID id { };
		id[0] = (uint8_t)(i >> 8);
		id[1] = (uint8_t)i;
		i2p::data::IdentHash peer { };
		info.AddNode (std::make_shared<Node> (id, peer, 0));
	}
	assert (info.nodesToRequest.size () <= MAX_DHT_REQUEST_NODES);
}

void TestDHTPeerBounds ()
{
	DHTTorrent torrent;
	size_t added = 0;
	for (size_t i = 0; i < MAX_DHT_PEERS_PER_TORRENT + 32; i++)
	{
		i2p::data::IdentHash peer { };
		peer[0] = (uint8_t)(i >> 8);
		peer[1] = (uint8_t)i;
		if (torrent.AddPeer (peer)) added++;
	}
	assert (added == MAX_DHT_PEERS_PER_TORRENT);
	// Re-adding an existing peer refreshes it but does not grow the map.
	i2p::data::IdentHash peer { };
	assert (!torrent.AddPeer (peer));
	// One reserved slot lets the rendezvous identity publish itself even
	// when remote announcers have filled the normal cap.
	i2p::data::IdentHash local { };
	local[31] = 0x7f;
	assert (torrent.AddPeer (local, false, true));
	i2p::data::IdentHash extra { };
	extra[31] = 0x7e;
	assert (!torrent.AddPeer (extra));
	assert (torrent.RemovePeer (local));
	assert (!torrent.RemovePeer (local));
}

void TestTorrentFileIO ()
{
	TempDir dir;
	dir.path = MakeTempDir ();
	// TorrentFile opens an existing ".part" with in|out; the session
	// creates it before any piece is written.
	{
		std::ofstream part (dir.path / "piece.bin.part", std::ofstream::binary);
		assert (part);
	}
	auto file = std::make_shared<TorrentFile> (dir.path / "piece.bin", 16);
	uint8_t written[16];
	for (size_t i = 0; i < sizeof (written); i++) written[i] = (uint8_t)i;
	assert (file->Save (0, written, sizeof (written)));

	uint8_t loaded[16] = { };
	assert (file->Load (0, loaded, sizeof (loaded)));
	assert (!memcmp (written, loaded, sizeof (written)));

	// Reading past the reserved length must be reported as failure instead
	// of silently succeeding with partially initialised data.
	uint8_t extra = 0;
	assert (!file->Load (16, &extra, 1));
	// Offset/length combinations must be bounded without wrapping.
	assert (!file->Load (15, loaded, 2));
	assert (!file->Save (15, written, 2));
	assert (!file->Load (std::numeric_limits<size_t>::max () - 4, &extra, 8));
	assert (!file->Save (std::numeric_limits<size_t>::max () - 4, written, 8));
	// A NULL buffer with a non-zero length is rejected rather than passed
	// to the kernel.
	assert (!file->Load (0, nullptr, 1));
	assert (!file->Save (0, nullptr, 1));

	// A truncated ".part" must make Load fail, not return uninitialised
	// bytes.
	auto truncated = std::make_shared<TorrentFile> (dir.path / "truncated.bin", 16);
	{
		std::ofstream part (dir.path / "truncated.bin.part", std::ofstream::binary);
		part.write ("abc", 3);
	}
	memset (loaded, 0, sizeof (loaded));
	assert (!truncated->Load (0, loaded, sizeof (loaded)));

	// A valid file of the reserved length is still readable.
	{
		std::ofstream part (dir.path / "complete.bin.part", std::ofstream::binary);
		part.write ((const char *)written, sizeof (written));
	}
	auto complete = std::make_shared<TorrentFile> (dir.path / "complete.bin", 16);
	assert (complete->Load (0, loaded, sizeof (loaded)));
	assert (!memcmp (written, loaded, sizeof (written)));

	// A complete piece whose backing file cannot be loaded must be rebuilt
	// as Missing rather than silently remaining complete.
	{
		uint8_t zeroHash[SHA_DIGEST_LENGTH] = { };
		Piece missingPiece (16, zeroHash);
		missingPiece.Complete ();
		assert (missingPiece.IsComplete ());
		auto missing = std::make_shared<TorrentFile> (dir.path / "missing.bin", 16);
		PieceFileFragment fragment (missing, 0, 0, 16);
		assert (!missingPiece.Load (std::move (fragment)));
		missingPiece.MarkCorrupt ();
		assert (!missingPiece.IsComplete ());
		assert (!missingPiece.HasBlock (0));
	}
}

// Path components from metainfo must never escape the torrent
// directory. The parser rejects "." / "..", absolute components and NUL
// before a path is constructed.
void TestUnsafeTorrentPaths ()
{
	// A non-string component must invalidate the entire list rather than
	// be skipped by the generic bencode parser and change the intended
	// path.
	const auto invalidFile = Dict ({ { "length", "i16384e" }, { "path", "l3:subi1e8:file.bine" } });
	const auto invalidMeta = Dict ({ { "info",
		Dict ({ { "files", "l" + invalidFile + "e" }, { "name", Bytes ("root") }, { "piece length", "i16384e" },
			{ "pieces", Bytes (std::string (SHA_DIGEST_LENGTH, 'A')) } }) } });
	Torrent invalidComponent (invalidMeta);
	assert (invalidComponent.GetError () == eTorrentErrorMalformedMetaInfo);

	Torrent traversal (MakeMultiFileTorrent ("root", { "..", "escape.bin" }));
	assert (traversal.GetError () == eTorrentErrorMalformedMetaInfo);

	Torrent relativeTraversal (MakeMultiFileTorrent ("root", { "sub", "..", "escape.bin" }));
	assert (relativeTraversal.GetError () == eTorrentErrorMalformedMetaInfo);

	Torrent absoluteComponent (MakeMultiFileTorrent ("root", { "/abs", "escape.bin" }));
	assert (absoluteComponent.GetError () == eTorrentErrorMalformedMetaInfo);

	Torrent backslashComponent (MakeMultiFileTorrent ("root", { "..\\escape.bin" }));
	// Separators inside a component are neutralised, so this is not
	// traversal; it must not produce an error but also must not create a
	// directory.
	assert (backslashComponent.GetError () == eTorrentErrorNoError);
	assert (!backslashComponent.GetFiles ().empty ());

	const std::string embeddedNul ("bad\0name", 8);
	Torrent nulComponent (MakeMultiFileTorrent ("root", { embeddedNul }));
	assert (nulComponent.GetError () == eTorrentErrorMalformedMetaInfo);

	Torrent absoluteName (MakeMultiFileTorrent ("/root", { "file.bin" }));
	assert (absoluteName.GetError () == eTorrentErrorMalformedMetaInfo);

	Torrent valid (MakeMultiFileTorrent ("root", { "sub", "file.bin" }));
	assert (valid.GetError () == eTorrentErrorNoError);
	assert (!valid.GetFiles ().empty ());
}

// Win32 resolves a DOS device name whatever extension follows and after
// stripping the trailing spaces and dots of the part before the first
// dot. A peer-supplied metainfo component may not name one, because the
// write would leave the download root. The predicate is checked here on
// every platform so the Windows name rule has a regression that runs in
// the POSIX lanes too.
void TestReservedDeviceNames ()
{
	assert (IsWindowsReservedDeviceName ("CON"));
	assert (IsWindowsReservedDeviceName ("con"));
	assert (IsWindowsReservedDeviceName ("NUL"));
	assert (IsWindowsReservedDeviceName ("com1"));
	assert (IsWindowsReservedDeviceName ("LPT9"));
	assert (IsWindowsReservedDeviceName ("CON.txt"));
	assert (IsWindowsReservedDeviceName ("aux.tar.gz"));
	// Win32 strips the trailing spaces and dots before resolving the
	// name, so these address the same devices as the plain spellings.
	assert (IsWindowsReservedDeviceName ("CON .txt"));
	assert (IsWindowsReservedDeviceName ("AUX..txt"));
	assert (IsWindowsReservedDeviceName ("NUL  .bin"));
	assert (IsWindowsReservedDeviceName ("PRN..."));
	// Ordinary names, and names that merely start with a device prefix.
	assert (!IsWindowsReservedDeviceName ("conifer.bin"));
	assert (!IsWindowsReservedDeviceName ("console"));
	assert (!IsWindowsReservedDeviceName ("COM10"));
	assert (!IsWindowsReservedDeviceName ("LPT"));
	assert (!IsWindowsReservedDeviceName ("file.bin"));
	assert (!IsWindowsReservedDeviceName (""));
	// A metainfo component that only becomes a device after AdjustName
	// neutralises a separator is not one; the raw component decides.
	assert (!IsWindowsReservedDeviceName ("CON|x"));
	assert (!IsWindowsReservedDeviceName ("CON:"));
}

void TestStrictBencodedStringList ()
{
	const auto valid = ParseStringList ("l0:1:xe");
	assert (valid.second == 7 && valid.first.size () == 2);
	assert (valid.first[0].empty () && valid.first[1] == "x");
	for (std::string_view bad : { "li1ee", "l1:ai1ee", "ll1:aee", "ld1:a1:bee", "l1:a" })
	{
		auto parsed = ParseStringList (bad);
		assert (!parsed.second && parsed.first.empty ());
	}
}

// Lexical containment and symlink rejection used by the session before
// it creates, opens or removes files.
void TestPathContainment ()
{
	assert (!IsSafeRelativePath (std::filesystem::path ("../escape")));
	assert (!IsSafeRelativePath (std::filesystem::path ("a/../b")));
	assert (!IsSafeRelativePath (std::filesystem::path ("/absolute")));
	assert (!IsSafeRelativePath (std::filesystem::path (".")));
	assert (!IsSafeRelativePath (std::filesystem::path ()));
	assert (IsSafeRelativePath (std::filesystem::path ("a/b")));
	assert (IsSafeRelativePath (std::filesystem::path ("file.bin")));

	// This is the network-thread check: it must be purely lexical and
	// reject any path that leaves the configured root before storage is
	// asked to touch the filesystem.
	assert (IsPathLexicallyWithinRoot (std::filesystem::path ("root"), std::filesystem::path ("root/file.bin")));
	assert (IsPathLexicallyWithinRoot (std::filesystem::path ("root"), std::filesystem::path ("root/sub/file")));
	assert (!IsPathLexicallyWithinRoot (std::filesystem::path ("root"), std::filesystem::path ("root/../escape")));
	assert (!IsPathLexicallyWithinRoot (std::filesystem::path ("root"), std::filesystem::path ("other/file")));

	TempDir dir;
	dir.path = MakeTempDir ();
	auto root = dir.path / "root";
	auto outside = dir.path / "outside";
	std::error_code ec;
	std::filesystem::create_directories (root / "sub", ec);
	assert (!ec);
	std::filesystem::create_directories (outside, ec);
	assert (!ec);

	// Download paths are built from sanitized components; an absolute or
	// traversal path must be rejected before it is anchored.
	{
		TorrentFile traversal ("../escape.bin", 16);
		assert (!traversal.UpdateFullPath (root));
		assert (traversal.GetFullFilePath ().empty ());
		TorrentFile absolute ("/escape.bin", 16);
		assert (!absolute.UpdateFullPath (root));
		TorrentFile validRelative ("sub/file.bin", 16);
		assert (validRelative.UpdateFullPath (root));
		assert (validRelative.GetFullFilePath () == root / "sub" / "file.bin");
	}

	assert (IsPathWithinRoot (root, root / "sub" / "file.bin", false));
	assert (!IsPathWithinRoot (root, root / ".." / "outside" / "file.bin", false));
	assert (!IsPathWithinRoot (root, outside / "file.bin", false));
	assert (IsPathWithinRoot (root, root / "missing" / "file.bin", false)); // creation path

	// Anchored creation traverses existing components without following
	// links and rejects a path that is not safe and relative.
	assert (CreateDirectoriesWithinRootNoFollow (root, std::filesystem::path ("sub")));
	assert (CreateFileWithSizeNoFollow (root, std::filesystem::path ("sub/created.bin"), 4));
	assert (std::filesystem::file_size (root / "sub" / "created.bin") == 4);
	assert (!CreateDirectoriesWithinRootNoFollow (root, std::filesystem::path ("../escape")));
	assert (!CreateFileWithSizeNoFollow (root, std::filesystem::path ("../escape.bin"), 4));

#ifndef _WIN32
	auto outsideFile = outside / "file.bin";
	{
		std::ofstream f (outsideFile, std::ofstream::binary);
		f << "outside";
	}
	ec.clear ();
	std::filesystem::create_directory_symlink (outside, root / "link", ec);
	if (!ec)
	{
		assert (std::filesystem::is_symlink (std::filesystem::symlink_status (root / "link")));
		assert (!IsPathWithinRoot (root, root / "link" / "file.bin", false));
		assert (!CreateDirectoriesWithinRootNoFollow (root, std::filesystem::path ("link/newdir")));
		assert (!CreateFileWithSizeNoFollow (root, std::filesystem::path ("link/new.bin"), 4));
	}
	ec.clear ();
	std::filesystem::create_symlink (outsideFile, root / "file-link", ec);
	if (!ec) assert (!IsPathWithinRoot (root, root / "file-link", false));
#endif
}

#ifndef _WIN32
// A swapped part file or intermediate directory must never redirect
// reads or writes outside the torrent root. The victim file must stay
// byte-identical.
void TestTorrentFileSymlinkRejection ()
{
	TempDir dir;
	dir.path = MakeTempDir ();
	auto victim = dir.path / "victim.bin";
	const std::string secret ("SECRET-DATA");
	{
		std::ofstream f (victim, std::ofstream::binary);
		f.write (secret.data (), secret.size ());
	}

	uint8_t block[16] = { };
	std::error_code ec;
	std::filesystem::create_symlink (victim, dir.path / "leaf.bin.part", ec);
	if (!ec)
	{
		auto leaf = std::make_shared<TorrentFile> (dir.path / "leaf.bin", sizeof (block));
		leaf->SetRootDir (dir.path);
		assert (!leaf->Save (0, block, sizeof (block)));
		assert (!leaf->Load (0, block, sizeof (block)));
	}

	auto outside = dir.path / "outside";
	std::filesystem::create_directories (outside / "sub", ec);
	assert (!ec);
	{
		std::ofstream f (outside / "sub" / "nested.bin.part", std::ofstream::binary);
		f.write ((const char *)block, sizeof (block));
	}
	ec.clear ();
	// Use a nested target so the leaf's parent itself is not the symlink.
	std::filesystem::create_directory_symlink (outside, dir.path / "linkdir", ec);
	if (!ec)
	{
		auto nested = std::make_shared<TorrentFile> (dir.path / "linkdir" / "sub" / "nested.bin", sizeof (block));
		nested->SetRootDir (dir.path);
		assert (!nested->Save (0, block, sizeof (block)));
		assert (!nested->Load (0, block, sizeof (block)));
	}

	// Nothing above may have modified the victim or the outside file.
	{
		std::ifstream f (victim, std::ifstream::binary);
		std::string content ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char> ());
		assert (content == secret);
	}
	assert (std::filesystem::file_size (victim) == secret.size ());
	assert (std::filesystem::file_size (outside / "sub" / "nested.bin.part") == sizeof (block));
}
#endif

// Resume files are derived data but their write path is still attacker
// reachable through the torrent directory. A symlinked ".resume" must
// not truncate its target, and reads must be bounded.
void TestRemovedTorrentPersistence ()
{
	TempDir dir { MakeTempDir () };
	Torrent torrent (MakeMultiFileTorrent ("removed", { "file.bin" }, 32768, 2));
	torrent.SetFullPath (dir.path / "removed");
	torrent.GetPiece (0).Complete ();
	unsigned writes = 0;
	torrent.SetResumePersistence (
		[&writes] (std::span<const uint8_t>, bool)
		{
			++writes;
			return true;
		});
	torrent.SaveTorrentResumeFile ();
	assert (writes == 1);
	torrent.MarkRemoved ();
	torrent.MarkRemoved (); // revocation is irreversible and idempotent
	torrent.SetStopped (false);
	assert (torrent.IsRemoved () && !torrent.CanAcceptPieceBlock (1, 0, 1));
	torrent.SaveTorrentResumeFile ();
	assert (writes == 1); // delegated writes are revoked too
	torrent.SetResumePersistence ({ });
	torrent.SaveTorrentResumeFile ();
	assert (!std::filesystem::exists (dir.path / "removed.resume"));
}

void TestInboundAcceptAuthority ()
{
	boost::asio::io_context service;
	auto keys = i2p::data::PrivateKeys::CreateRandomKeys (
		i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
	auto destination = std::make_shared<i2p::client::ClientDestination> (service, keys, false);
	auto streaming = std::make_shared<i2p::stream::StreamingDestination> (destination);
	auto tunnel = std::make_shared<TorrentsTunnel> ("accept-authority", destination, "", "", false);
	auto copied = TorrentInboundAcceptTestAccess::Begin (*tunnel);
	{
		// Revocation shares the admission lock. No sleeps or timer race
		// decide when the currently admitted operation releases that lock.
		auto admitted = TorrentInboundAcceptTestAccess::AdmissionLock (*tunnel);
		std::promise<void> entering;
		auto entered = entering.get_future ();
		std::jthread revoker ([&]
			{
				entering.set_value ();
				TorrentInboundAcceptTestAccess::Revoke (*tunnel);
			});
		entered.wait ();
		admitted.unlock ();
		revoker.join ();
		// Stop may still be queued and m_Stopping false, but registration
		// must already be closed by its synchronous revocation phase.
		assert (!TorrentInboundAcceptTestAccess::Register (*tunnel));
	}
	auto stop = [&]
	{
		boost::asio::post (service, [tunnel] { tunnel->Stop (); });
		service.restart ();
		service.poll ();
	};
	auto assertRejected = [&] (const auto & acceptor)
	{
		auto stream = std::make_shared<i2p::stream::Stream> (service, streaming);
		acceptor (stream);
		service.restart ();
		service.poll ();
		assert (stream->GetStatus () == i2p::stream::eStreamStatusTerminated);
	};

	// A callback extracted before deregistration must reject after stop.
	stop ();
	assertRejected (copied);
	assert (TorrentInboundAcceptTestAccess::Handlers (*tunnel) == 0);

	// Restart uses a distinct authority even though m_Stopping is false.
	auto current = TorrentInboundAcceptTestAccess::Begin (*tunnel);
	assertRejected (copied);
	auto live = std::make_shared<i2p::stream::Stream> (service, streaming);
	current (live);
	assert (TorrentInboundAcceptTestAccess::Handlers (*tunnel) == 1);
	stop ();
	assert (TorrentInboundAcceptTestAccess::Handlers (*tunnel) == 0);
	assertRejected (current);

	// Retaining either extracted callback must not retain the tunnel.
	std::weak_ptr<TorrentsTunnel> released = tunnel;
	tunnel.reset ();
	assert (released.expired ());
	assertRejected (current);
}

void TestPeerDialAuthority ()
{
	boost::asio::io_context service;
	auto keys = i2p::data::PrivateKeys::CreateRandomKeys (
		i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
	auto destination = std::make_shared<i2p::client::ClientDestination> (service, keys, false);
	auto streaming = std::make_shared<i2p::stream::StreamingDestination> (destination);
	auto tunnel = std::make_shared<TorrentsTunnel> ("dial-authority", destination, "", "", false);
	auto torrent = std::make_shared<Torrent> (MakeSingleFileTorrent ());
	using Access = TorrentInboundAcceptTestAccess;
	Access::BeginDials (*tunnel); // No inbound acceptor is registered.
	auto old = Access::Dial (*tunnel, torrent);
	assert (old && Access::PendingDials (*tunnel) == 1);
	auto reject = [&] (const auto & complete)
	{
		auto stream = std::make_shared<i2p::stream::Stream> (service, streaming);
		complete (stream);
		if (service.stopped ()) service.restart ();
		service.poll ();
		assert (stream->GetStatus () == i2p::stream::eStreamStatusTerminated);
	};
	{
		auto admitted = Access::DialAdmissionLock (*tunnel);
		std::promise<void> entering;
		auto entered = entering.get_future ();
		std::jthread revoker ([&]
		{
			entering.set_value ();
			Access::Revoke (*tunnel);
		});
		entered.wait ();
		admitted.unlock ();
		revoker.join ();
	}
	assert (!Access::Dial (*tunnel, torrent));
	Access::RetireDials (*tunnel);
	Access::BeginDials (*tunnel);
	auto current = Access::Dial (*tunnel, torrent);
	assert (current && Access::PendingDials (*tunnel) == 1);
	reject (old); // An old generation cannot retire the new pending dial.
	assert (Access::PendingDials (*tunnel) == 1 && Access::Handlers (*tunnel) == 0);
	auto replacement = std::make_shared<Torrent> (MakeSingleFileTorrent ());
	Access::ReplaceDialOwner (*tunnel, replacement);
	reject (current); // A different torrent instance owns this reservation.
	assert (Access::PendingDials (*tunnel) == 1);
	Access::RetireDials (*tunnel);
	reject (current); // An expired slot cannot publish a late success.
	assert (Access::Handlers (*tunnel) == 0);

	// Exhaustion never reuses zero or retired IDs, even after restart.
	Access::ExhaustDials (*tunnel);
	auto last = Access::Dial (*tunnel, torrent);
	assert (last);
	last (nullptr);
	assert (Access::PendingDials (*tunnel) == 0);
	assert (!Access::Dial (*tunnel, torrent));
	Access::Revoke (*tunnel);
	Access::BeginDials (*tunnel);
	assert (!Access::Dial (*tunnel, torrent));
	Access::Revoke (*tunnel);
	boost::asio::post (service, [tunnel] { tunnel->Stop (); });
	if (service.stopped ()) service.restart ();
	service.poll ();
	std::weak_ptr<TorrentsTunnel> released = tunnel;
	tunnel.reset ();
	assert (released.expired ());
	reject (old); // Retained dial callbacks borrow the tunnel.
}

void TestInitialDiscoveryAuthority ()
{
	boost::asio::io_context service;
	auto keys = i2p::data::PrivateKeys::CreateRandomKeys (
		i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
	auto destination = std::make_shared<i2p::client::ClientDestination> (service, keys, false);
	auto tunnel = std::make_shared<TorrentsTunnel> ("discovery-authority", destination, "", "", false);
	auto torrent = std::make_shared<Torrent> (MakeSingleFileTorrent ());
	assert (torrent->IsValid ());
	int trackers = 0, dht = 0;
	auto discover = [&]
	{
		return TorrentDiscoveryTestAccess::Dispatch (*tunnel, torrent, [&] { ++trackers; }, [&] { ++dht; });
	};

	// Neither a pending persistence write nor unloaded pause policy
	// grants network authority. Verification is a separate barrier.
	TorrentDiscoveryTestAccess::Prepare (*tunnel, torrent, false, false);
	assert (!discover ());
	TorrentDiscoveryTestAccess::Ready (*tunnel, torrent);
	assert (!discover ());
	TorrentDiscoveryTestAccess::Loaded (*tunnel, torrent);
	TorrentDiscoveryTestAccess::PendingWrite (*tunnel, torrent, true);
	assert (!discover ());
	TorrentDiscoveryTestAccess::PendingWrite (*tunnel, torrent, false);
	torrent->RequestLocalVerification (false);
	assert (!discover ());
	assert (torrent->GetNextPieceToVerify () == -1);
	assert (discover ());
	assert (trackers == 1 && dht == 1);
	assert (!discover ()); // insertion authority is consumed once

	TorrentDiscoveryTestAccess::Prepare (*tunnel, torrent, true);
	TorrentDiscoveryTestAccess::Activity (*tunnel, true, false);
	assert (!discover ());
	TorrentDiscoveryTestAccess::Activity (*tunnel, false, true);
	assert (!discover ());
	TorrentDiscoveryTestAccess::Activity (*tunnel, false, false);
	torrent->SetStopped (true);
	assert (!discover ());
	torrent->SetStopped (false);
	torrent->SetError (eTorrentErrorFilesystem);
	assert (!discover ());
	torrent->SetError (eTorrentErrorNoError);
	auto replacement = std::make_shared<Torrent> (MakeSingleFileTorrent ());
	TorrentDiscoveryTestAccess::Replace (*tunnel, replacement);
	assert (!discover ());
	assert (!TorrentDiscoveryTestAccess::Dispatch (*tunnel, replacement, {}, {}));
	assert (trackers == 1 && dht == 1);

	// Private metadata may contact its configured tracker, never DHT.
	const auto privateInfo = Dict ({ { "length", "i16384e" }, { "name", Bytes ("private.bin") },
		{ "piece length", "i16384e" }, { "pieces", Bytes (std::string (SHA_DIGEST_LENGTH, 'A')) },
		{ "private", "i1e" } });
	torrent = std::make_shared<Torrent> (Dict ({ { "info", privateInfo } }));
	assert (torrent->IsValid () && torrent->IsPrivate ());
	TorrentDiscoveryTestAccess::Prepare (*tunnel, torrent, true);
	assert (discover ());
	assert (trackers == 2 && dht == 1);

	// Retirement during tracker dispatch must suppress the DHT action.
	torrent = std::make_shared<Torrent> (MakeSingleFileTorrent ());
	TorrentDiscoveryTestAccess::Prepare (*tunnel, torrent, true);
	assert (TorrentDiscoveryTestAccess::Dispatch (*tunnel, torrent,
		[&] { ++trackers; torrent->MarkRemoved (); }, [&] { ++dht; }));
	assert (trackers == 3 && dht == 1);
	assert (!discover ());
	TorrentDiscoveryTestAccess::Finish (*tunnel);
}

void TestMagnetDiscoveryWaitsForDurability ()
{
	for (bool writable : { false, true })
	{
		TempDir dir { MakeTempDir () };
		auto root = dir.path / "root";
		if (writable) std::filesystem::create_directory (root);
		else { std::ofstream blocked (root); blocked << "not a directory"; }
		boost::asio::io_context service;
		auto keys = i2p::data::PrivateKeys::CreateRandomKeys (
			i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
		auto destination = std::make_shared<i2p::client::ClientDestination> (service, keys, false);
		auto tunnel = std::make_shared<TorrentsTunnel> ("magnet-discovery", destination, root.string (), "", false);
		const std::string magnet = "magnet:?xt=urn:btih:00112233445566778899aabbccddeeff00112233";
		auto [torrent, id] = tunnel->AddMagnet (magnet);
		assert (torrent && id);
		assert (TorrentDiscoveryTestAccess::PendingInitial (*tunnel, torrent));
		int premature = 0;
		assert (!TorrentDiscoveryTestAccess::Dispatch (*tunnel, torrent,
			[&] { ++premature; }, [&] { ++premature; }));
		// Coalesce a changed immutable record behind the first write. The
		// first acknowledgement cannot authorize the newer generation.
		assert (tunnel->AddMagnet (magnet + "&dn=updated").first == torrent);
		tunnel->GetDiskIOService ().restart ();
		tunnel->GetDiskIOService ().poll ();
		service.restart ();
		service.poll ();
		assert (TorrentDiscoveryTestAccess::PendingInitial (*tunnel, torrent));
		assert (premature == 0);
		if (writable)
		{
			tunnel->GetDiskIOService ().restart ();
			tunnel->GetDiskIOService ().poll ();
			service.restart ();
			service.poll ();
			assert (!TorrentDiscoveryTestAccess::PendingInitial (*tunnel, torrent));
			auto relative = std::filesystem::path (".openi2pd-state")
				/ (ToHexString (torrent->GetInfoHash ()) + ".magnet");
			std::vector<uint8_t> bytes;
			std::string error;
			assert (ReadFileBoundedNoFollow (root, relative, MAX_TORRENT_MAGNET_STATE_BYTES, bytes, error));
			TorrentMagnetState durable;
			assert (DecodeTorrentMagnetState (
				std::string_view (reinterpret_cast<const char *> (bytes.data ()), bytes.size ()), durable, error));
			assert (durable.name == "updated");
		}
		else
			assert (!TorrentDiscoveryTestAccess::Dispatch (*tunnel, torrent, {}, {}));
		TorrentDiscoveryTestAccess::Finish (*tunnel);
	}
}

void TestMagnetRecovery ()
{
	for (bool malformed : { false, true })
	{
		TempDir dir { MakeTempDir () };
		const auto content = MakeMultiFileTorrent ("crash-window", { "file.bin" });
		Torrent metainfo (content);
		assert (metainfo.IsValid ());
		const auto hash = ToHexString (metainfo.GetInfoHash ());
		std::filesystem::create_directories (dir.path / ".openi2pd-state");
		{
			std::ofstream file (dir.path / "crash-window.torrent", std::ios::binary);
			file << content;
		}
		std::string error;
		TorrentMagnetState record { metainfo.GetInfoHash (), "display only", { }, true };
		const auto recovery = EncodeTorrentMagnetState (record, error);
		assert (!recovery.empty ());
		{
			std::ofstream file (dir.path / ".openi2pd-state" / (hash + ".magnet"), std::ios::binary);
			file << (malformed ? "unsupported record" : recovery);
		}
		boost::asio::io_context service;
		auto keys = i2p::data::PrivateKeys::CreateRandomKeys (
			i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ELGAMAL, true);
		auto destination = std::make_shared<i2p::client::ClientDestination> (service, keys, false);
		auto tunnel = std::make_shared<TorrentsTunnel> ("recovery", destination, dir.path.string (), "", false);
		TorrentRecoveryTestAccess::Load (*tunnel);
		// No listeners, router, peers or background executor are started.
		for (unsigned pass = 0; pass < 16; ++pass)
		{
			tunnel->GetDiskIOService ().restart ();
			tunnel->GetDiskIOService ().poll ();
			service.restart ();
			service.poll ();
		}
		auto restored = tunnel->FindTorrent (metainfo.GetInfoHash ());
		assert (restored && restored->IsValid () && restored->IsStopped ());
		const auto statePath = dir.path / ".openi2pd-state" / (hash + ".state");
		std::ifstream saved (statePath, std::ios::binary);
		const std::string bytes ((std::istreambuf_iterator<char> (saved)), {});
		TorrentLocalState state;
		assert (DecodeTorrentLocalState (bytes, i2p::util::GetSecondsSinceEpoch (), state, error));
		assert (state.stopped);
		// Both durable replacements complete before fallback retirement.
		assert (!std::filesystem::exists (dir.path / ".openi2pd-state" / (hash + ".magnet")));
	}
	TempDir dir { MakeTempDir () };
	TorrentMagnetState pending { { }, "display-only", { "http://first.i2p/a" }, true };
	pending.infoHash[0] = 0xab;
	const auto hash = ToHexString (pending.infoHash);
	const auto path = dir.path / ".openi2pd-state" / (hash + ".magnet");
	std::filesystem::create_directories (path.parent_path ());
	std::string error;
	{
		std::ofstream saved (path, std::ios::binary);
		saved << EncodeTorrentMagnetState (pending, error);
	}
	// A display-name collision must not grant deletion authority.
	{
		std::ofstream sentinel (dir.path / pending.name);
		sentinel << "retain";
	}
	boost::asio::io_context service;
	auto keys = i2p::data::PrivateKeys::CreateRandomKeys (
		i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ELGAMAL, true);
	auto destination = std::make_shared<i2p::client::ClientDestination> (service, keys, false);
	auto tunnel = std::make_shared<TorrentsTunnel> ("pending", destination, dir.path.string (), "", false);
	auto pump = [&]
	{
		for (unsigned pass = 0; pass < 16; ++pass)
		{
			tunnel->GetDiskIOService ().restart ();
			tunnel->GetDiskIOService ().poll ();
			service.restart ();
			service.poll ();
		}
	};
	TorrentRecoveryTestAccess::Load (*tunnel);
	pump ();
	auto restored = tunnel->FindTorrent (pending.infoHash);
	assert (restored && !restored->IsValid () && restored->IsStopped ());
	auto [duplicate, id] = tunnel->AddMagnet (
		"magnet:?xt=urn:btih:" + hash + "&tr=http%3A%2F%2Fsecond.i2p%2Fa");
	assert (duplicate == restored && id > 0 && restored->IsStopped ());
	pump ();
	std::ifstream saved (path, std::ios::binary);
	const std::string bytes ((std::istreambuf_iterator<char> (saved)), {});
	TorrentMagnetState merged;
	assert (DecodeTorrentMagnetState (bytes, merged, error));
	assert (merged.stopped && merged.trackers.size () == 2);
	saved.close ();
	assert (tunnel->RemoveTorrent (id, true));
	pump ();
	assert (!std::filesystem::exists (path));
	assert (std::filesystem::exists (dir.path / pending.name));
}

void TestRPCMetadataFields ()
{
	TempDir dir { MakeTempDir () };
	boost::asio::io_context service;
	auto keys = i2p::data::PrivateKeys::CreateRandomKeys (
		i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ELGAMAL, true);
	auto destination = std::make_shared<i2p::client::ClientDestination> (service, keys, false);
	auto tunnel = std::make_shared<TorrentsTunnel> ("rpc-meta", destination, dir.path.string (), "", false);
	auto [torrent, id] = tunnel->AddTorrent (MakeMultiFileTorrent ("meta", { "file.bin" }, 32768, 2));
	auto [emptyTorrent, emptyId] = tunnel->AddTorrent (MakeMultiFileTorrent ("empty", { "file.bin" }, 32768, 2));
	assert (torrent && emptyTorrent && id > 0 && emptyId > 0);
	auto invalidContent = MakeMultiFileTorrent ("invalid-text", { "file.bin" }, 32768, 2);
	invalidContent.insert (1, Bytes ("comment") + Bytes (std::string (1, '\xff')));
	auto [invalidTorrent, invalidId] = tunnel->AddTorrent (invalidContent);
	assert (invalidTorrent && invalidId > 0);
	assert (invalidTorrent->GetMetaComment () == std::string (1, '\xff'));
	const std::string comment = "A quoted \"metainfo\" comment\nsecond line";
	const std::string creator = "fixture\\creator";
	assert (torrent->SetMetaComment (comment));
	assert (torrent->SetCreatedBy (creator));
	// Internal error identities remain generic local errors on the RPC
	// boundary; no new numeric wire value is introduced.
	torrent->SetError (eTorrentErrorRandomSource);
	auto work = boost::asio::make_work_guard (service);
	// Adding metadata queues owner-retaining disk work. Run that domain
	// too: an unstarted member queue otherwise keeps the tunnel alive.
	auto & diskService = tunnel->GetDiskIOService ();
	auto diskWork = boost::asio::make_work_guard (diskService);
	std::jthread disk ([&] { diskService.run (); });
	std::jthread engine ([&] { service.run (); });
	std::weak_ptr<TorrentsTunnel> released = tunnel;
	auto stopEngine = i2p::modern::ScopeExit ([&]
		{
			// Keep both domains available until final persistence finishes.
			tunnel->Stop ();
			diskWork.reset ();
			disk.join ();
			destination->Stop ();
			service.stop ();
			engine.join ();
			tunnel.reset ();
			assert (released.expired ());
		});
	TorrentsRPCServer server ("127.0.0.1", 0);
	server.AddTunnel ("", tunnel);
	server.Start ();
	boost::asio::io_context clientService;
	boost::asio::ip::tcp::socket client (clientService);
	client.connect (server.GetEndpoint ());
	const auto snapshot = tunnel->GetControlSnapshot (std::chrono::seconds (2));
	assert (snapshot.has_value ());
	const auto reported = std::find_if (snapshot->torrents.begin (), snapshot->torrents.end (),
		[id] (const auto & info) { return info.id == id; });
	assert (reported != snapshot->torrents.end ());
	assert (reported->error == torrent->GetErrorStr ());
	const std::string body = "{\"method\":\"torrent-get\",\"arguments\":{\"fields\":[\"comment\",\"creator\",\"error\",\"errorString\"],\"ids\":["
		+ std::to_string (id) + "," + std::to_string (emptyId) + "," + std::to_string (invalidId) + "]}}";
	const std::string request = "POST /rpc HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/json\r\nContent-Length: "
		+ std::to_string (body.size ()) + "\r\n\r\n" + body;
	boost::asio::write (client, boost::asio::buffer (request));
	const auto response = ReadClosedRPC (client);
	const auto split = response.find ("\r\n\r\n");
	assert (split != std::string::npos);
	const auto json = boost::json::parse (response.substr (split + 4));
	const auto & entries = json.as_object ().at ("arguments").as_object ().at ("torrents").as_array ();
	assert (entries.size () == 3);
	for (const auto & entry : entries)
	{
		const auto returnedId = entry.as_object ().at ("id").as_int64 ();
		const bool populated = returnedId == id;
		assert (populated || returnedId == emptyId || returnedId == invalidId);
		assert (entry.as_object ().at ("error").as_int64 () == (populated ? 3 : 0));
		const auto & errorText = entry.as_object ().at ("errorString").as_string ();
		assert (std::string_view (errorText.data (), errorText.size ())
			== (populated ? torrent->GetErrorStr () : std::string_view { }));
		const auto & returnedComment = entry.as_object ().at ("comment").as_string ();
		const auto & returnedCreator = entry.as_object ().at ("creator").as_string ();
		assert (std::string_view (returnedComment.data (), returnedComment.size ())
			== (populated ? comment : std::string { }));
		assert (std::string_view (returnedCreator.data (), returnedCreator.size ())
			== (populated ? creator : std::string { }));
	}
	server.Stop ();
}

void TestRemovalQueueAndReadd ()
{
	TempDir dir { MakeTempDir () };
	boost::asio::io_context service;
	auto keys = i2p::data::PrivateKeys::CreateRandomKeys (
		i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ELGAMAL, true);
	auto destination = std::make_shared<i2p::client::ClientDestination> (service, keys, false);
	auto tunnel = std::make_shared<TorrentsTunnel> ("remove-test", destination, dir.path.string (), "", false);
	const auto content = MakeMultiFileTorrent ("queued", { "file.bin" }, 32768, 2);
	auto [torrent, id] = tunnel->AddTorrent (content);
	assert (torrent && id > 0);
	const auto state = std::filesystem::path (".openi2pd-state") / (ToHexString (torrent->GetInfoHash ()) + ".state");
	std::filesystem::create_directories (dir.path / state.parent_path ());
	{
		std::ofstream file (dir.path / state);
		file << "old settings";
	}
	std::filesystem::create_directories (dir.path / "queued");
	{
		std::ofstream file (dir.path / "queued" / "file.bin");
		file << "retained";
	}
	assert (tunnel->RemoveTorrent (id, false));
	assert (torrent->IsRemoved ());
	assert (!tunnel->AddTorrent (content).first); // cleanup not dispatched yet
	// Drive both domains explicitly, without threads, sleeps, a router or
	// peers.
	auto pump = [&]
	{
		for (unsigned pass = 0; pass < 8; ++pass)
		{
			service.restart ();
			service.poll ();
			tunnel->GetDiskIOService ().restart ();
			tunnel->GetDiskIOService ().poll ();
		}
	};
	pump ();
	assert (!std::filesystem::exists (dir.path / state));
	assert (!std::filesystem::exists (dir.path / "queued.torrent"));
	assert (std::filesystem::exists (dir.path / "queued" / "file.bin"));
	auto [replacement, replacementID] = tunnel->AddTorrent (content);
	assert (replacement && replacement != torrent && replacementID > 0 && !replacement->IsRemoved ());
	// Simulate a disk transaction that passed authorization before
	// removal.
	// Revocation must not wait for this running transaction, and its late
	// write must still precede the serialized cleanup, never resurrect
	// state.
	boost::asio::post (tunnel->GetDiskIOService (),
		[&]
		{
			assert (tunnel->RemoveTorrent (replacementID, false));
			assert (!tunnel->AddTorrent (content).first);
			std::ofstream file (dir.path / state);
			file << "late old transaction";
		});
	pump ();
	assert (!std::filesystem::exists (dir.path / state));
	auto [last, lastID] = tunnel->AddTorrent (content);
	assert (last && lastID > 0);
	assert (tunnel->RemoveTorrent (lastID, false));
	// A nonempty directory at the exact metadata filename must fail
	// closed;
	// cleanup must never recurse into it or permit a replacement
	// afterward.
	std::filesystem::create_directory (dir.path / state);
	{
		std::ofstream file (dir.path / state / "keep");
		file << "keep";
	}
	pump ();
	assert (std::filesystem::exists (dir.path / state / "keep"));
	assert (!tunnel->AddTorrent (content).first);
	tunnel.reset ();
}

void TestMetadataRemovalNoFollow ()
{
	TempDir dir { MakeTempDir () }, outside { MakeTempDir () };
	std::string error;
	assert (RemoveFileNoFollow (dir.path, "missing/file.state", error));
	assert (!RemoveFileNoFollow (dir.path, "../escape.state", error));
	std::filesystem::create_directory (dir.path / "directory.state");
	assert (!RemoveFileNoFollow (dir.path, "directory.state", error));
	{
		std::ofstream file (dir.path / "regular.state");
		file << "state";
	}
	assert (RemoveFileNoFollow (dir.path, "regular.state", error));
	assert (!std::filesystem::exists (dir.path / "regular.state"));
#ifndef _WIN32
	{
		std::ofstream file (outside.path / "keep.state");
		file << "keep";
	}
	std::filesystem::create_directory_symlink (outside.path, dir.path / "linked");
	assert (!RemoveFileNoFollow (dir.path, "linked/keep.state", error));
	assert (std::filesystem::exists (outside.path / "keep.state"));
	std::filesystem::create_symlink (outside.path / "keep.state", dir.path / "leaf.state");
	assert (RemoveFileNoFollow (dir.path, "leaf.state", error));
	assert (std::filesystem::exists (outside.path / "keep.state"));
#endif
}

void TestResumeWriteAndRead ()
{
	TempDir dir;
	dir.path = MakeTempDir ();

	const std::string secret ("KEEP");
	auto victim = dir.path / "victim.txt";
	{
		std::ofstream f (victim, std::ofstream::binary);
		f.write (secret.data (), secret.size ());
	}

	Torrent torrent (MakeMultiFileTorrent ("resume-root", { "file.bin" }, 32768, 2));
	assert (torrent.GetError () == eTorrentErrorNoError);
	torrent.SetFullPath (dir.path / "resume-root");
	// One complete and one incomplete piece makes the bitfield
	// indeterminate,
	// which is the state that is persisted to the resume file.
	torrent.GetPiece (0).Complete ();

	auto resumePath = dir.path / "resume-root.resume";
	std::error_code ec;
	std::filesystem::remove (resumePath, ec);
	std::filesystem::create_symlink (victim, resumePath, ec);
	if (!ec)
	{
		torrent.SaveTorrentResumeFile ();
		assert (std::filesystem::is_symlink (std::filesystem::symlink_status (resumePath)));
	}
	std::filesystem::remove (resumePath, ec);
	torrent.SaveTorrentResumeFile ();
	assert (std::filesystem::is_regular_file (std::filesystem::symlink_status (resumePath)));
	assert (std::filesystem::file_size (resumePath) == 1); // 2 pieces -> one byte

	// Nothing above may have modified the victim.
	{
		std::ifstream f (victim, std::ifstream::binary);
		std::string content ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char> ());
		assert (content == secret);
	}

	std::vector<uint8_t> content;
	std::string error;
	// Reading through a symlinked resume path must not expose the target.
	ec.clear ();
	std::filesystem::remove (resumePath, ec);
	std::filesystem::create_symlink (victim, resumePath, ec);
	if (!ec)
	{
		assert (!ReadFileBoundedNoFollow (resumePath, 64, content, error));
		std::filesystem::remove (resumePath, ec);
	}
	// Empty and oversized/truncated resume payloads are rejected before
	// any
	// bitfield is applied.
	{
		std::ofstream f (resumePath, std::ofstream::binary | std::ofstream::trunc);
	}
	assert (!ReadFileBoundedNoFollow (resumePath, 1, content, error));
	{
		std::ofstream f (resumePath, std::ofstream::binary | std::ofstream::trunc);
		f.put ('\x80');
		f.put ('\x00');
	}
	assert (!ReadFileBoundedNoFollow (resumePath, 1, content, error));
	assert (ReadFileBoundedNoFollow (resumePath, 2, content, error));
	assert (content.size () == 2);
}
}

int main ()
{
	TestInboundAcceptAuthority ();
	TestPeerDialAuthority ();
	TestInitialDiscoveryAuthority ();
	TestMagnetDiscoveryWaitsForDurability ();
	TestMagnetRecovery ();
	TestRPCAdmissionAndRestart ();
	TestRPCMetadataFields ();
	TestI2PTrackerHostValidation ();
	TestPieceBlockBounds ();
	TestCanAcceptPieceBlock ();
	TestPeerMessageFraming ();
	TestPieceRequestRelease ();
	TestPieceCorruptionAndReset ();
	TestCorruptResumeBitfield ();
	TestInfoIsNotCopied ();
	TestRoutingTableOwnership ();
	TestDHTRequestBounds ();
	TestDHTPeerBounds ();
	TestTorrentFileIO ();
	TestUnsafeTorrentPaths ();
	TestStrictBencodedStringList ();
	TestReservedDeviceNames ();
	TestPathContainment ();
#ifndef _WIN32
	TestTorrentFileSymlinkRejection ();
#endif
	TestResumeWriteAndRead ();
	TestRemovedTorrentPersistence ();
	TestRemovalQueueAndReadd ();
	TestMetadataRemovalNoFollow ();
	return 0;
}

#else

int main ()
{
	return 0;
}

#endif
