/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test streaming accept timeout.
 *
 * Exercises the heap-allocated wait state of StreamingDestination::AcceptStream:
 * a cancelled wait can never be written after the caller returned, and a stream
 * that raced with the cancellation is re-offered instead of being dropped.
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

	std::shared_ptr<i2p::stream::Stream> MakeStream ()
	{
		return std::make_shared<i2p::stream::Stream> (service, destination);
	}
};
} // namespace

int main ()
{
	using i2p::stream::Stream;
	using i2p::stream::SyncAcceptState;

	Fixture f;
	auto & destination = *f.destination;

	// Arm the same registration AcceptStream arms, then take the timeout path
	// synchronously instead of waiting for a real clock.
	auto state = std::make_shared<SyncAcceptState> ();
	auto token = destination.StartSyncAcceptForTesting (state);
	assert (token != 0);
	assert (destination.GetNumArmedAcceptorsForTesting () == 1);
	assert (destination.IsAcceptorSet ());

	assert (state->Cancel ()); // the timeout won: no stream was delivered
	assert (state->GetStream () == nullptr);
	assert (destination.RemoveAcceptor (token));
	assert (destination.GetNumArmedAcceptorsForTesting () == 0);
	assert (!destination.IsAcceptorSet ());

	// A registration preserved past its cancellation must not write the result
	// of the finished wait. The registration owns the wait state through a
	// shared_ptr, so this is a logical no-op and not a use-after-free.
	auto staleState = std::make_shared<SyncAcceptState> ();
	auto staleToken = destination.StartSyncAcceptForTesting (staleState);
	assert (staleState->Cancel ());
	auto lateAcceptor = destination.TakeArmedAcceptorForTesting (staleToken);
	assert (lateAcceptor);
	auto lateStream = f.MakeStream ();
	lateAcceptor (lateStream);
	assert (!staleState->GetStream ());

	// The refused stream is not lost: the stale registration re-offers it on the
	// service thread, where it waits for the next acceptor.
	f.service.poll ();
	assert (destination.GetNumPendingIncomingForTesting () == 1);
	assert (destination.GetNumStreams () == 0);

	// Registration tokens are single use.
	assert (!destination.RemoveAcceptor (token));
	assert (!destination.TakeArmedAcceptorForTesting (token));
	assert (!destination.RemoveAcceptor (staleToken));

	// A delivery attempted after the cancellation is refused rather than
	// written; the caller still owns the stream.
	auto extraStream = f.MakeStream ();
	assert (!state->Deliver (extraStream));
	assert (state->GetStream () == nullptr);
	assert (destination.OfferIncomingStreamForTesting (extraStream));
	assert (destination.GetNumPendingIncomingForTesting () == 2);
	assert (destination.GetNumStreams () == 0);

	// Teardown releases the backlog without an io thread.
	destination.StopForTesting ();
	assert (destination.GetNumPendingIncomingForTesting () == 0);
	assert (!destination.IsAcceptorSet ());
	return 0;
}
