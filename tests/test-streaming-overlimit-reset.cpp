/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test streaming overlimit reset.
 *
 * Checks that a SYN refused by the concurrent-stream cap or by a full pending
 * backlog is answered with a signed RESET (never silence), and that the RESET
 * builder used for that answer produces a packet.
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

	explicit Fixture (const i2p::util::Mapping & params)
	{
		auto keys = i2p::data::PrivateKeys::CreateRandomKeys (
			i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
		owner = std::make_shared<i2p::client::ClientDestination> (service, keys, false, &params);
		destination = std::make_shared<i2p::stream::StreamingDestination> (owner);
	}

	Fixture ()
		: Fixture (i2p::util::Mapping ())
	{
	}
};
} // namespace

int main ()
{
	using i2p::stream::MAX_PENDING_INCOMING_BACKLOG;
	using i2p::stream::StreamingDestination;

	// Concurrent stream cap: two registered streams exceed a cap of one.
	{
		i2p::util::Mapping params;
		params.Insert (i2p::client::I2CP_PARAM_STREAMING_MAX_CONCURRENT_STREAMS, "1");
		Fixture f (params);
		auto & destination = *f.destination;

		destination.CreateStreamForTesting ();
		destination.CreateStreamForTesting ();
		assert (destination.GetNumStreams () == 2);

		const uint32_t receiveStreamID = 0x0BADF00D;
		assert (destination.AdmitSynForTesting (receiveStreamID) == StreamingDestination::SynAdmission::eOverLimit);

		// The rejection is answered, not swallowed.
		assert (destination.GetNumRejectionResetsForTesting () == 0);
		assert (destination.BuildRejectionResetForTesting (receiveStreamID));
		assert (destination.GetNumRejectionResetsForTesting () == 1);

		// The refused connection is remembered, so the peer's next SYN is
		// refused as an already-closed connection instead of consuming another
		// signature verification and RESET round trip.
		destination.RememberRecentlyClosedForTesting (receiveStreamID);
		assert (
			destination.AdmitSynForTesting (receiveStreamID) == StreamingDestination::SynAdmission::eRecentlyClosed);

		// Teardown releases the registered streams.
		destination.StopForTesting ();
		assert (destination.GetNumStreams () == 0);
	}

	// Pending backlog: with no acceptor armed, the backlog fills and further
	// SYNs are refused with a RESET.
	{
		Fixture f;
		auto & destination = *f.destination;
		assert (!destination.IsAcceptorSet ());

		size_t queued = 0;
		while (destination.OfferIncomingStreamForTesting (
			std::make_shared<i2p::stream::Stream> (f.service, f.destination)))
			++queued;
		assert (queued == MAX_PENDING_INCOMING_BACKLOG);
		assert (destination.GetNumPendingIncomingForTesting () == MAX_PENDING_INCOMING_BACKLOG);

		const uint32_t receiveStreamID = 0x0BADBEEF;
		assert (destination.AdmitSynForTesting (receiveStreamID) == StreamingDestination::SynAdmission::eBacklogFull);
		assert (destination.BuildRejectionResetForTesting (receiveStreamID));
		assert (destination.GetNumRejectionResetsForTesting () == 1);

		// The backlog did not grow and no stream was registered for the
		// refused SYN.
		assert (destination.GetNumPendingIncomingForTesting () == MAX_PENDING_INCOMING_BACKLOG);
		assert (destination.GetNumStreams () == 0);

		// Teardown releases the backlog without an io thread.
		destination.StopForTesting ();
		assert (destination.GetNumPendingIncomingForTesting () == 0);
	}

	return 0;
}
