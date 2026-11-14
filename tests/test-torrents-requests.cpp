/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * Observe real peer request-retirement frames without a live stream.
 * The private write-boundary observer copies submitted bytes; request
 * leases and piece epochs use the production connection methods.
 */

#include <array>
#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <new>
#include <algorithm>
#include <span>
#include <utility>
#include <vector>
#include "Torrents.h"

#ifndef NO_TORRENTS
namespace i2p::torrents
{
struct PeerRequestLedgerTestAccess
{
	static void Prepare (PeerConnection & peer, bool fast, std::function<void (std::span<const uint8_t>)> observer)
	{
		peer.m_IsEstablished = true;
		peer.m_IsFast = fast;
		peer.m_IsChoked = true; // isolate retirement from automatic refilling
		peer.m_RemoteBitfield.resize (peer.m_Torrent->GetNumPieces ());
		peer.m_RemoteBitfield.set ();
		peer.m_WriteObserverForTesting = std::move (observer);
	}
	static void Queue (PeerConnection & peer, uint32_t index, uint32_t offset, uint32_t length, uint64_t now)
	{
		assert (std::in_range<int> (index) && index < peer.m_Torrent->GetNumPieces ());
		peer.m_OutgoingRequests.push_back (
			{ index, offset, length, peer.m_Torrent->GetPiece (static_cast<int> (index)).GetBlockEpoch (), now });
		++peer.m_NumRequests;
	}
	static bool Expire (PeerConnection & peer, uint64_t now) { return peer.ReleaseExpiredOutgoingRequests (now); }
	static size_t Count (const PeerConnection & peer) { return peer.m_NumRequests; }
	static bool Cancelled (const PeerConnection & peer)
	{
		return peer.m_OutgoingRequests.size () == 1 && peer.m_OutgoingRequests.front ().cancelled;
	}
	static bool TimedOut (const PeerConnection & peer) { return peer.m_RequestRetirementTimedOut; }
	static void Release (PeerConnection & peer, bool notify) { peer.ReleaseOutgoingRequests (notify); }
	static void Choke (PeerConnection & peer) { peer.HandleChokeMsg (); }
	static bool Refill (PeerConnection & peer)
	{
		peer.m_IsChoked = false;
		const bool requested = peer.RequestNextBlocks ();
		peer.m_IsChoked = true;
		return requested;
	}
	inline static unsigned admissionsBeforeFailure = 0;
	static void AdmissionCheckpoint ()
	{
		if (!admissionsBeforeFailure) throw std::bad_alloc ();
		--admissionsBeforeFailure;
	}
	static void FailAdmission (PeerConnection & peer, unsigned after)
	{
		admissionsBeforeFailure = after;
		peer.m_BeforeRequestAdmissionForTesting = &AdmissionCheckpoint;
		try
		{
			Refill (peer);
		}
		catch (...)
		{
			peer.m_BeforeRequestAdmissionForTesting = nullptr;
			peer.m_IsChoked = true;
			throw;
		}
		peer.m_BeforeRequestAdmissionForTesting = nullptr;
	}
	inline static Piece * invalidateOnAdmission = nullptr;
	static void InvalidationCheckpoint ()
	{
		assert (invalidateOnAdmission);
		invalidateOnAdmission->ClearAllRequests ();
		assert (invalidateOnAdmission->AcquireBlockRequest ());
		throw std::bad_alloc ();
	}
	static void InvalidateDuringAdmission (PeerConnection & peer, Piece & piece)
	{
		invalidateOnAdmission = &piece;
		peer.m_BeforeRequestAdmissionForTesting = &InvalidationCheckpoint;
		try
		{
			Refill (peer);
		}
		catch (...)
		{
			peer.m_BeforeRequestAdmissionForTesting = nullptr;
			invalidateOnAdmission = nullptr;
			peer.m_IsChoked = true;
			throw;
		}
		peer.m_BeforeRequestAdmissionForTesting = nullptr;
		invalidateOnAdmission = nullptr;
	}
	static void Closing (PeerConnection & peer) { peer.m_UploadClosing.store (true); }
	static void Unestablished (PeerConnection & peer) { peer.m_IsEstablished = false; }
	static void Reject (PeerConnection & peer, uint32_t length, size_t wireLength = 12)
	{
		std::array<uint8_t, 13> payload { };
		htobe32buf (payload.data (), 0);
		htobe32buf (payload.data () + 4, 0);
		htobe32buf (payload.data () + 8, length);
		peer.HandleRejectRequestMsg (payload.data (), wireLength);
	}
	static void DeliverCancelled (PeerConnection & peer, size_t length = 7)
	{
		std::vector<uint8_t> payload (8 + length);
		peer.HandlePieceMsg (payload.data (), payload.size ());
	}
};
}

namespace
{
using namespace i2p::torrents;
constexpr uint64_t sentAt = 1000;
constexpr uint64_t deadline = sentAt + PEER_REQUEST_TIMEOUT;
constexpr uint32_t blockLength = 7;

struct Fixture
{
	std::shared_ptr<Torrent> torrent;
	std::vector<std::vector<uint8_t>> frames;
	std::shared_ptr<PeerConnection> peerOwner;
	PeerConnection & peer;

	explicit Fixture (bool fast, size_t bytes = blockLength)
		: torrent (CreateTorrent (bytes))
		, peerOwner (std::make_shared<PeerConnection> (nullptr, nullptr, torrent))
		, peer (*peerOwner)
	{
		torrent->SetStopped (false);
		torrent->GetPiece (0).MarkCorrupt ();
		PeerRequestLedgerTestAccess::Prepare (peer, fast, [this] (std::span<const uint8_t> frameBytes)
			{ frames.emplace_back (frameBytes.begin (), frameBytes.end ()); });
		const auto [offset, length] = torrent->GetPiece (0).GetNextBlockToRequest ();
		assert (offset == 0 && length == std::min<size_t> (bytes, REQUEST_BLOCK_SIZE));
		PeerRequestLedgerTestAccess::Queue (peer, 0, 0, static_cast<uint32_t> (length), sentAt);
	}
	Fixture (const Fixture &) = delete;
	Fixture & operator= (const Fixture &) = delete;
	Fixture (Fixture &&) = delete;
	Fixture & operator= (Fixture &&) = delete;

	static std::shared_ptr<Torrent> CreateTorrent (size_t length)
	{
		std::vector<uint8_t> bytes (length);
		std::vector<Torrent::InfoHash> hashes;
		for (size_t offset = 0; offset < length; offset += MIN_PIECE_LENGTH)
		{
			Torrent::InfoHash hash;
			assert (SHA1 (bytes.data () + offset, std::min<size_t> (length - offset, MIN_PIECE_LENGTH), hash.data ()));
			hashes.push_back (hash);
		}
		auto created = Torrent::CreateV1 ("requests", { { "", length } }, MIN_PIECE_LENGTH, hashes);
		assert (created && created->GetNumPieces () == hashes.size ());
		return created;
	}

	void CheckCancel () const
	{
		assert (frames.size () == 1);
		const auto & frame = frames.front ();
		assert (frame.size () == CANCEL_MSG_LENGTH && frame.size () == 17);
		assert (bufbe32toh (frame.data ()) == 13 && frame[4] == eMessageTypeCancel);
		assert (bufbe32toh (frame.data () + 5) == 0);
		assert (bufbe32toh (frame.data () + 9) == 0);
		assert (bufbe32toh (frame.data () + 13) == blockLength);
	}
};

void TestLegacyExpiryAndSilence ()
{
	Fixture fixture (false);
	assert (!PeerRequestLedgerTestAccess::Expire (fixture.peer, deadline - 1));
	assert (fixture.frames.empty ());
	assert (PeerRequestLedgerTestAccess::Expire (fixture.peer, deadline));
	fixture.CheckCancel ();
	assert (!PeerRequestLedgerTestAccess::Count (fixture.peer));
	assert (!PeerRequestLedgerTestAccess::Expire (fixture.peer, deadline + 1));
	fixture.CheckCancel ();

	for (unsigned state = 0; state < 4; ++state)
	{
		Fixture silent (false);
		if (state == 0) silent.torrent->SetStopped (true);
		if (state == 1) silent.torrent->MarkRemoved ();
		if (state == 2) PeerRequestLedgerTestAccess::Closing (silent.peer);
		if (state == 3) PeerRequestLedgerTestAccess::Unestablished (silent.peer);
		PeerRequestLedgerTestAccess::Expire (silent.peer, deadline);
		assert (silent.frames.empty () && !PeerRequestLedgerTestAccess::Count (silent.peer));
	}
	Fixture disconnected (false);
	PeerRequestLedgerTestAccess::Release (disconnected.peer, false);
	assert (disconnected.frames.empty () && !PeerRequestLedgerTestAccess::Count (disconnected.peer));
}

void TestFastCancellationAuthority ()
{
	Fixture fixture (true);
	assert (PeerRequestLedgerTestAccess::Expire (fixture.peer, deadline));
	fixture.CheckCancel ();
	assert (PeerRequestLedgerTestAccess::Count (fixture.peer) == 1);
	assert (PeerRequestLedgerTestAccess::Cancelled (fixture.peer));
	assert (!PeerRequestLedgerTestAccess::Refill (fixture.peer)); // no tuple reuse
	assert (PeerRequestLedgerTestAccess::Count (fixture.peer) == 1);
	PeerRequestLedgerTestAccess::Expire (fixture.peer, deadline + 1);
	fixture.CheckCancel (); // no duplicate CANCEL
	PeerRequestLedgerTestAccess::Expire (fixture.peer, deadline + PEER_REQUEST_TIMEOUT - 1);
	assert (!PeerRequestLedgerTestAccess::TimedOut (fixture.peer));
	PeerRequestLedgerTestAccess::Expire (fixture.peer, deadline + PEER_REQUEST_TIMEOUT);
	assert (PeerRequestLedgerTestAccess::TimedOut (fixture.peer));
}

void TestExactRejectAndCancelledPiece ()
{
	Fixture active (true);
	for (const size_t length : { size_t { 0 }, size_t { blockLength - 1 }, size_t { blockLength + 1 } })
	{
		PeerRequestLedgerTestAccess::DeliverCancelled (active.peer, length);
		assert (PeerRequestLedgerTestAccess::Count (active.peer) == 1);
		assert (!active.torrent->GetPiece (0).GetNextBlockToRequest ().second);
		assert (!active.torrent->GetPiece (0).HasBlock (0));
		assert (!active.peer.GetDownloaded () && !active.torrent->GetDownloaded ());
	}
	PeerRequestLedgerTestAccess::Reject (active.peer, blockLength);
	assert (!PeerRequestLedgerTestAccess::Count (active.peer) && active.frames.empty ());
	assert (active.torrent->GetPiece (0).GetNextBlockToRequest ().second == blockLength);

	Fixture fixture (true);
	PeerRequestLedgerTestAccess::Reject (fixture.peer, blockLength, 8);
	PeerRequestLedgerTestAccess::Reject (fixture.peer, blockLength, 13);
	PeerRequestLedgerTestAccess::Reject (fixture.peer, blockLength + 1);
	assert (PeerRequestLedgerTestAccess::Count (fixture.peer) == 1 && fixture.frames.empty ());
	PeerRequestLedgerTestAccess::Expire (fixture.peer, deadline);
	const auto successor = fixture.torrent->GetPiece (0).GetNextBlockToRequest ();
	assert (successor.first == 0 && successor.second == blockLength);
	PeerRequestLedgerTestAccess::Reject (fixture.peer, blockLength);
	assert (!PeerRequestLedgerTestAccess::Count (fixture.peer));
	assert (!fixture.torrent->GetPiece (0).GetNextBlockToRequest ().second); // successor retained
	fixture.CheckCancel ();

	Fixture piece (true);
	PeerRequestLedgerTestAccess::Expire (piece.peer, deadline);
	assert (piece.torrent->GetPiece (0).GetNextBlockToRequest ().second == blockLength);
	for (const size_t length : { size_t { 0 }, size_t { blockLength - 1 }, size_t { blockLength + 1 } })
	{
		PeerRequestLedgerTestAccess::DeliverCancelled (piece.peer, length);
		assert (PeerRequestLedgerTestAccess::Count (piece.peer) == 1);
		assert (PeerRequestLedgerTestAccess::Cancelled (piece.peer));
		assert (!piece.torrent->GetPiece (0).GetNextBlockToRequest ().second);
		assert (!piece.torrent->GetPiece (0).HasBlock (0));
		assert (!piece.peer.GetDownloaded () && !piece.torrent->GetDownloaded ());
	}
	PeerRequestLedgerTestAccess::DeliverCancelled (piece.peer);
	assert (!PeerRequestLedgerTestAccess::Count (piece.peer));
	assert (!piece.torrent->GetPiece (0).HasBlock (0));
	assert (!piece.torrent->GetPiece (0).GetNextBlockToRequest ().second);
	assert (!piece.peer.GetDownloaded () && !piece.torrent->GetDownloaded ());

	Fixture legacy (false);
	PeerRequestLedgerTestAccess::Reject (legacy.peer, blockLength);
	assert (PeerRequestLedgerTestAccess::Count (legacy.peer) == 1); // not negotiated
}

void TestAdmissionFailureRetiresLeases ()
{
	// First-node failure and a later failure in a multi-frame batch both
	// release every selected block before any wire publication.
	for (unsigned after : { 0u, 1u })
	{
		Fixture fixture (false, 2 * MIN_PIECE_LENGTH);
		PeerRequestLedgerTestAccess::Release (fixture.peer, false);
		const auto previousPiece = fixture.peer.GetLastRequestedPieceIndex ();
		bool failed = false;
		try
		{
			PeerRequestLedgerTestAccess::FailAdmission (fixture.peer, after);
		}
		catch (const std::bad_alloc &)
		{
			failed = true;
		}
		assert (failed && fixture.frames.empty ());
		assert (!PeerRequestLedgerTestAccess::Count (fixture.peer));
		assert (fixture.peer.GetLastRequestedPieceIndex () == previousPiece);
		for (size_t index = 0; index < fixture.torrent->GetNumPieces (); ++index)
			assert (fixture.torrent->GetPiece (static_cast<int> (index)).GetNextBlockToRequest ().second
				== REQUEST_BLOCK_SIZE);
	}
}

void TestSelectionEpochSurvivesInvalidation ()
{
	Fixture fixture (false);
	PeerRequestLedgerTestAccess::Release (fixture.peer, false);
	auto & piece = fixture.torrent->GetPiece (0);
	const auto oldEpoch = piece.GetBlockEpoch ();
	bool failed = false;
	try
	{
		PeerRequestLedgerTestAccess::InvalidateDuringAdmission (fixture.peer, piece);
	}
	catch (const std::bad_alloc &)
	{
		failed = true;
	}
	assert (failed && piece.GetBlockEpoch () != oldEpoch);
	assert (fixture.frames.empty () && !PeerRequestLedgerTestAccess::Count (fixture.peer));
	// The failed old selection must not release the successor admitted
	// by the checkpoint after the epoch changed.
	assert (!piece.AcquireBlockRequest ());
	piece.ReleaseBlock (0, piece.GetBlockEpoch ());
	assert (piece.AcquireBlockRequest ());
}

void TestChokeAndExplicitRetirement ()
{
	Fixture fast (true);
	PeerRequestLedgerTestAccess::Choke (fast.peer);
	assert (PeerRequestLedgerTestAccess::Count (fast.peer) == 1 && fast.frames.empty ());
	PeerRequestLedgerTestAccess::Release (fast.peer, true);
	fast.CheckCancel ();
	PeerRequestLedgerTestAccess::Release (fast.peer, true);
	fast.CheckCancel ();
	PeerRequestLedgerTestAccess::Release (fast.peer, false);
	assert (!PeerRequestLedgerTestAccess::Count (fast.peer));

	Fixture legacy (false);
	PeerRequestLedgerTestAccess::Choke (legacy.peer);
	assert (!PeerRequestLedgerTestAccess::Count (legacy.peer) && legacy.frames.empty ());
	Fixture explicitLegacy (false);
	PeerRequestLedgerTestAccess::Release (explicitLegacy.peer, true);
	explicitLegacy.CheckCancel ();
}
}

int main ()
{
	TestLegacyExpiryAndSilence ();
	TestFastCancellationAuthority ();
	TestExactRejectAndCancelledPiece ();
	TestChokeAndExplicitRetirement ();
	TestAdmissionFailureRetiresLeases ();
	TestSelectionEpochSurvivesInvalidation ();
	return 0;
}
#else
int main ()
{
	return 0;
}
#endif
