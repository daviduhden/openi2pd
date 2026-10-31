/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test streaming recently closed.
 *
 * Checks the bounded recently-closed set that stops a retransmitted SYN
 * from resurrecting a finished incoming connection (Java
 * ConnectionManager _recentlyClosed) and that such a SYN is answered
 * with a RESET.
 *
 */

#include <cassert>
#include <cstdint>
#include <memory>

#include <boost/asio.hpp>

#include "Destination.h"
#include "Streaming.h"

namespace
{
struct Fixture
{
	boost::asio::io_context service;
	std::shared_ptr<i2p::client::ClientDestination> owner;
	std::shared_ptr<i2p::stream::StreamingDestination> destination;

	Fixture ()
	{
		auto keys = i2p::data::PrivateKeys::CreateRandomKeys (
			i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
		owner = std::make_shared<i2p::client::ClientDestination> (service, keys, false);
		destination = std::make_shared<i2p::stream::StreamingDestination> (owner);
	}
};
} // namespace

int main ()
{
	using i2p::stream::MAX_RECENTLY_CLOSED_STREAMS;
	using i2p::stream::StreamingDestination;

	Fixture f;
	auto & destination = *f.destination;

	const uint32_t peerStreamID = 0x11223344;

	// A closed incoming stream is remembered by the peer's stream id.
	auto stream = destination.CreateStreamForTesting ();
	assert (destination.GetNumStreams () == 1);
	const uint32_t localStreamID = stream->GetRecvStreamID ();
	stream->SetStreamIDsForTesting (peerStreamID, localStreamID);
	stream->Terminate (true); // leaves the destination maps
	assert (destination.GetNumStreams () == 0);
	assert (destination.GetNumRecentlyClosedForTesting () == 1);

	// The replayed SYN is dropped by policy: no stream is created, nothing
	// is queued and no acceptor is invoked.
	int accepted = 0;
	auto token = destination.AcceptOnce (
		[&accepted] (std::shared_ptr<i2p::stream::Stream> s)
		{
			if (s) ++accepted;
		});
	assert (destination.AdmitSynForTesting (peerStreamID) == StreamingDestination::SynAdmission::eRecentlyClosed);
	assert (destination.GetNumStreams () == 0);
	assert (destination.GetNumPendingIncomingForTesting () == 0);
	assert (accepted == 0);
	destination.RemoveAcceptor (token);

	// ... and the SYN is answered with a RESET instead of silence, so the
	// peer stops its retransmission ladder.
	const auto before = destination.GetNumRejectionResetsForTesting ();
	assert (destination.BuildRejectionResetForTesting (peerStreamID));
	assert (destination.GetNumRejectionResetsForTesting () == before + 1);

	// Other receive stream ids are unaffected by the remembered one.
	assert (destination.AdmitSynForTesting (0x55667788) == StreamingDestination::SynAdmission::eAccept);

	// The set is bounded: it never grows past MAX_RECENTLY_CLOSED_STREAMS,
	// so a peer that opens and closes many connections cannot grow it
	// without limit. Inserting decays the expired entries first.
	for (uint32_t i = 1; i <= MAX_RECENTLY_CLOSED_STREAMS + 50; ++i)
		destination.RememberRecentlyClosedForTesting (0x10000000 + i);
	assert (destination.GetNumRecentlyClosedForTesting () <= MAX_RECENTLY_CLOSED_STREAMS);
	assert (destination.GetNumRecentlyClosedForTesting () > 0);

	// Oldest entries are evicted first: the most recent id is still
	// refused.
	assert (destination.AdmitSynForTesting (0x10000000 + MAX_RECENTLY_CLOSED_STREAMS + 50)
		== StreamingDestination::SynAdmission::eRecentlyClosed);

	// Teardown forgets the set, so a restarted destination accepts the id
	// again.
	destination.StopForTesting ();
	assert (destination.GetNumRecentlyClosedForTesting () == 0);
	return 0;
}
