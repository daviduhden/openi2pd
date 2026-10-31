/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test streaming accept race.
 *
 * Checks the acceptor registration list: arming, single-registration
 * removal,
 * reset of every armed registration and the pending-incoming hand-off
 * never
 * strand a stream or leave the destination with a phantom acceptor.
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

	// A stream offered to an armed one-shot registration is handed over
	// immediately and the registration is consumed.
	auto firstState = std::make_shared<SyncAcceptState> ();
	auto firstToken = destination.StartSyncAcceptForTesting (firstState);
	assert (firstToken != 0);
	auto firstStream = f.MakeStream ();
	assert (destination.OfferIncomingStreamForTesting (firstStream));
	assert (firstState->GetStream () == firstStream);
	assert (destination.GetNumArmedAcceptorsForTesting () == 0);
	assert (destination.GetNumPendingIncomingForTesting () == 0);
	assert (destination.GetNumStreams () == 0); // fabricated stream stays unregistered

	// ResetAcceptor completes every armed registration with nullptr and
	// leaves no acceptor behind, so no waiter is left stranded by another
	// waiter.
	int firstCalls = 0, secondCalls = 0, nullCalls = 0;
	destination.AcceptOnce (
		[&] (std::shared_ptr<Stream> s)
		{
			++firstCalls;
			if (!s) ++nullCalls;
		});
	destination.AcceptOnce (
		[&] (std::shared_ptr<Stream> s)
		{
			++secondCalls;
			if (!s) ++nullCalls;
		});
	assert (destination.IsAcceptorSet ());
	assert (destination.GetNumArmedAcceptorsForTesting () == 2);
	destination.ResetAcceptor ();
	assert (firstCalls == 1 && secondCalls == 1);
	assert (nullCalls == 2);
	assert (!destination.IsAcceptorSet ());
	assert (destination.GetNumArmedAcceptorsForTesting () == 0);

	// With no acceptor, an incoming stream is queued instead of being
	// lost.
	auto queuedStream = f.MakeStream ();
	assert (destination.OfferIncomingStreamForTesting (queuedStream));
	assert (destination.GetNumPendingIncomingForTesting () == 1);
	assert (destination.GetNumStreams () == 0);

	// Arming an acceptor later drains the backlog.
	auto secondState = std::make_shared<SyncAcceptState> ();
	destination.StartSyncAcceptForTesting (secondState);
	f.service.poll (); // AcceptOnce dispatches the backlog on the service thread
	assert (secondState->GetStream () == queuedStream);
	assert (destination.GetNumPendingIncomingForTesting () == 0);
	assert (destination.GetNumArmedAcceptorsForTesting () == 0);

	// Registrations are served newest first; the older one stays armed and
	// receives the next stream.
	auto olderState = std::make_shared<SyncAcceptState> ();
	auto newerState = std::make_shared<SyncAcceptState> ();
	destination.StartSyncAcceptForTesting (olderState);
	destination.StartSyncAcceptForTesting (newerState);
	auto newerStream = f.MakeStream ();
	auto olderStream = f.MakeStream ();
	assert (destination.OfferIncomingStreamForTesting (newerStream));
	assert (newerState->GetStream () == newerStream);
	assert (olderState->GetStream () == nullptr);
	assert (destination.GetNumArmedAcceptorsForTesting () == 1);
	assert (destination.OfferIncomingStreamForTesting (olderStream));
	assert (olderState->GetStream () == olderStream);
	assert (destination.GetNumArmedAcceptorsForTesting () == 0);
	assert (destination.GetNumPendingIncomingForTesting () == 0);

	// A persistent acceptor serves every stream and is completed on reset.
	int persistentStreams = 0, persistentResets = 0;
	destination.SetAcceptor (
		[&] (std::shared_ptr<Stream> s)
		{
			if (s)
				++persistentStreams;
			else
				++persistentResets;
		});
	f.service.poll ();
	auto persistentStream = f.MakeStream ();
	assert (destination.OfferIncomingStreamForTesting (persistentStream));
	assert (persistentStreams == 1);
	destination.ResetAcceptor ();
	assert (persistentResets == 1);
	assert (!destination.IsAcceptorSet ());
	assert (destination.GetNumStreams () == 0);

	// Removing an unknown or already consumed token is reported, never
	// thrown.
	assert (!destination.RemoveAcceptor (firstToken));
	assert (!destination.RemoveAcceptor (0));
	assert (!destination.RemoveAcceptor (firstToken + 1000));

	f.service.poll ();
	return 0;
}
