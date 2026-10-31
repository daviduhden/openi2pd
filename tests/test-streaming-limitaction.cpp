/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: streaming admission (limitAction / access filter)
 * regression.
 *
 * Installs the server-tunnel admission predicate on two streaming
 * destinations and proves that a denied peer is decided before any
 * stream is offered to the acceptor: `drop` keeps the wire silent,
 * `reset` produces one signed RESET, and a predicate installed for one
 * port never affects another.
 *
 */

#include <cassert>
#include <cstdint>
#include <memory>

#include <boost/asio.hpp>

#include "Destination.h"
#include "I2PTunnelAccessFilter.h"
#include "Streaming.h"

namespace
{
using i2p::stream::StreamAdmission;
using i2p::stream::StreamingDestination;

struct Fixture
{
	boost::asio::io_context service;
	std::shared_ptr<i2p::client::ClientDestination> owner;
	std::shared_ptr<StreamingDestination> defaultDestination;
	std::shared_ptr<StreamingDestination> portDestination;

	Fixture ()
	{
		i2p::util::Mapping params;
		auto keys = i2p::data::PrivateKeys::CreateRandomKeys (
			i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
		owner = std::make_shared<i2p::client::ClientDestination> (service, keys, false, &params);
		defaultDestination = std::make_shared<StreamingDestination> (owner);
		portDestination = std::make_shared<StreamingDestination> (owner, 5555);
	}
};
} // namespace

int main ()
{
	Fixture fixture;
	auto & defaultDestination = *fixture.defaultDestination;
	auto & portDestination = *fixture.portDestination;
	assert (defaultDestination.GetLocalPort () == 0);
	assert (portDestination.GetLocalPort () == 5555);

	// Without a predicate every SYN is admitted and the historical RESET
	// action
	// stays in place for the streaming limits.
	i2p::data::IdentHash peer;
	peer.Fill (0x42);
	assert (!defaultDestination.IsAdmissionPredicateSet ());
	assert (defaultDestination.EvaluateAdmissionForTesting (peer) == StreamAdmission::eAllow);
	assert (defaultDestination.GetRejectionAction () == StreamAdmission::eReset);

	const auto denied = [] (const i2p::data::IdentHash & peer, uint16_t port) -> StreamAdmission
	{
		(void)peer;
		// Only the port-specific predicate refuses, so the same identity is
		// allowed on the default destination.
		return port == 5555 ? StreamAdmission::eDrop : StreamAdmission::eAllow;
	};
	portDestination.SetAdmissionPredicate (denied);
	assert (portDestination.IsAdmissionPredicateSet ());
	assert (!defaultDestination.IsAdmissionPredicateSet ());

	assert (portDestination.EvaluateAdmissionForTesting (peer) == StreamAdmission::eDrop);
	assert (defaultDestination.EvaluateAdmissionForTesting (peer) == StreamAdmission::eAllow);

	// Per-port isolation: a second predicate on another destination must
	// not change the first one.
	const auto resetAll
		= [] (const i2p::data::IdentHash &, uint16_t) -> StreamAdmission { return StreamAdmission::eReset; };
	auto otherDestination = std::make_shared<StreamingDestination> (fixture.owner, 6666);
	otherDestination->SetAdmissionPredicate (resetAll);
	assert (portDestination.EvaluateAdmissionForTesting (peer) == StreamAdmission::eDrop);
	assert (otherDestination->EvaluateAdmissionForTesting (peer) == StreamAdmission::eReset);
	assert (defaultDestination.EvaluateAdmissionForTesting (peer) == StreamAdmission::eAllow);

	// A dropped admission is silent: no RESET is built, the connection is
	// not remembered as recently closed and no stream reaches the acceptor
	// queue.
	{
		const uint32_t receiveStreamID = 0x0BADF00D;
		assert (portDestination.GetNumRejectionResetsForTesting () == 0);
		assert (portDestination.ApplyAdmissionForTesting (receiveStreamID, peer) == StreamAdmission::eDrop);
		assert (portDestination.GetNumRejectionResetsForTesting () == 0);
		assert (portDestination.GetNumPendingIncomingForTesting () == 0);
		assert (portDestination.GetNumStreams () == 0);
		// A retransmitted SYN is not remembered, so it is dropped again
		// rather
		// than answered with a RESET.
		assert (portDestination.AdmitSynForTesting (receiveStreamID) == StreamingDestination::SynAdmission::eAccept);
	}

	// A reset admission answers with exactly one signed RESET and
	// remembers the connection as closed so a retransmission cannot reopen
	// it.
	{
		const uint32_t receiveStreamID = 0x0BADBEEF;
		assert (otherDestination->GetNumRejectionResetsForTesting () == 0);
		assert (otherDestination->ApplyAdmissionForTesting (receiveStreamID, peer) == StreamAdmission::eReset);
		assert (otherDestination->GetNumRejectionResetsForTesting () == 1);
		assert (otherDestination->GetNumPendingIncomingForTesting () == 0);
		assert (otherDestination->GetNumStreams () == 0);
		assert (otherDestination->AdmitSynForTesting (receiveStreamID)
			== StreamingDestination::SynAdmission::eRecentlyClosed);
	}

	// limitAction maps onto the rejection action used for streaming
	// limits:
	// `drop` is silent, the default resets.
	portDestination.SetRejectionAction (StreamAdmission::eDrop);
	assert (portDestination.GetRejectionAction () == StreamAdmission::eDrop);
	{
		const uint32_t receiveStreamID = 0x0BAD1234;
		assert (portDestination.ApplyRejectionActionForTesting (receiveStreamID) == StreamAdmission::eDrop);
		assert (portDestination.GetNumRejectionResetsForTesting () == 0);
		assert (portDestination.AdmitSynForTesting (receiveStreamID) == StreamingDestination::SynAdmission::eAccept);
	}
	portDestination.SetRejectionAction (StreamAdmission::eReset);
	{
		const uint32_t receiveStreamID = 0x0BAD5678;
		assert (portDestination.ApplyRejectionActionForTesting (receiveStreamID) == StreamAdmission::eReset);
		assert (portDestination.GetNumRejectionResetsForTesting () == 1);
		assert (portDestination.AdmitSynForTesting (receiveStreamID)
			== StreamingDestination::SynAdmission::eRecentlyClosed);
	}

	// Removing the predicate restores the allow-all behaviour.
	portDestination.ResetAdmissionPredicate ();
	assert (!portDestination.IsAdmissionPredicateSet ());
	assert (portDestination.EvaluateAdmissionForTesting (peer) == StreamAdmission::eAllow);

	// An access-filter denial flows through the same seam: a deny-all
	// definition refuses the peer.
	{
		auto denyAll = std::make_shared<i2p::client::TunnelAccessFilter> (i2p::client::FilterDefinition::DenyAll ());
		defaultDestination.SetAdmissionPredicate (
			[denyAll] (const i2p::data::IdentHash & peer, uint16_t) -> StreamAdmission
			{ return denyAll->Evaluate (peer, 1000).allow ? StreamAdmission::eAllow : StreamAdmission::eDrop; });
		assert (defaultDestination.EvaluateAdmissionForTesting (peer) == StreamAdmission::eDrop);
	}
	defaultDestination.ResetAdmissionPredicate ();

	// Teardown without an io thread releases the internal state.
	defaultDestination.StopForTesting ();
	otherDestination->StopForTesting ();
	return 0;
}
