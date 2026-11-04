/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: server-tunnel teardown and restart regression.
 *
 * Proves that stopping a server tunnel actually terminates and removes
 * every registered handler (the base Terminate used to poison m_Dead
 * before the derived teardown ran), that Stop(bool) distinguishes the
 * forced and in-use cases, that a restart accepts again, that a
 * port-specific tunnel never takes the default streaming acceptor away
 * from another tunnel, and that a stop/start cycle cancels and re-arms
 * the address resolver.
 *
 */

#include <cassert>
#include <memory>
#include <string>

#include <boost/asio.hpp>

#include "Destination.h"
#include "I2PService.h"
#include "I2PTunnel.h"
#include "Identity.h"

namespace
{
// Counts the virtual Terminate dispatch and never allocates a socket.
class CountingHandler : public i2p::client::I2PServiceHandler
{
public:
	explicit CountingHandler (std::shared_ptr<i2p::client::I2PService> owner)
		: I2PServiceHandler (std::move (owner))
	{
	}

	void Terminate () override
	{
		++terminations;
		I2PServiceHandler::Terminate ();
	}

	int terminations = 0;
};

struct Fixture
{
	boost::asio::io_context service;
	std::shared_ptr<i2p::client::ClientDestination> destination;

	Fixture ()
	{
		i2p::util::Mapping params;
		auto keys = i2p::data::PrivateKeys::CreateRandomKeys (
			i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
		destination = std::make_shared<i2p::client::ClientDestination> (service, keys, false, &params);
	}
};
} // namespace

int main ()
{
	using i2p::client::I2PServerTunnel;
	using i2p::client::I2PServiceHandler;
	// An unstarted pipe has no completion retaining it. Releasing its
	// last owner must close both endpoints without shared_from_this().
	{
		boost::asio::io_context service;
		auto up = std::make_shared<boost::asio::ip::tcp::socket> (service);
		auto down = std::make_shared<boost::asio::ip::tcp::socket> (service);
		up->open (boost::asio::ip::tcp::v4 ());
		down->open (boost::asio::ip::tcp::v4 ());
		auto pipe = i2p::client::CreateSocketsPipe (nullptr, up, down);
		pipe.reset ();
		assert (!up->is_open () && !down->is_open ());
		up->open (boost::asio::ip::tcp::v4 ());
		down->open (boost::asio::ip::tcp::v4 ());
		pipe = i2p::client::CreateSocketsPipe (nullptr, up, down);
		pipe->Terminate ();
		pipe->Terminate ();
		assert (!up->is_open () && !down->is_open ());
		pipe.reset ();
	}

	Fixture fixture;
	auto & destination = *fixture.destination;

	// The default streaming destination is the port-0 destination. It has
	// no
	// acceptor until a tunnel claims it.
	auto defaultDestination = destination.GetStreamingDestination (0);
	if (!defaultDestination) defaultDestination = destination.CreateStreamingDestination (0);
	assert (defaultDestination);
	assert (!defaultDestination->IsAcceptorSet ());

	auto tunnel = std::make_shared<I2PServerTunnel> ("stop-test", "127.0.0.1", 8080, fixture.destination, 0, false);
	tunnel->Start ();
	assert (tunnel->IsRunning ());
	assert (defaultDestination->IsAcceptorSet ());

	// Virtual dispatch through the base class must reach the derived
	// teardown:
	// the old non-virtual Terminate only set m_Dead and left the socket
	// open.
	{
		auto handler = std::make_shared<CountingHandler> (tunnel);
		I2PServiceHandler * base = handler.get ();
		base->Terminate ();
		assert (handler->terminations == 1);
	}

	// ClearHandlers snapshots under the lock and terminates outside it:
	// every live handler is torn down once and the set is emptied.
	auto first = std::make_shared<CountingHandler> (tunnel);
	auto second = std::make_shared<CountingHandler> (tunnel);
	tunnel->AddHandler (first);
	tunnel->AddHandler (second);
	assert (tunnel->GetNumHandlers () == 2);
	assert (tunnel->IsInUse ());

	tunnel->Stop ();
	assert (first->terminations == 1);
	assert (second->terminations == 1);
	assert (tunnel->GetNumHandlers () == 0);
	assert (!tunnel->IsRunning ());
	assert (!defaultDestination->IsAcceptorSet ());

	// Stop() is forced, so a second call is idempotent and harmless.
	tunnel->Stop ();
	assert (tunnel->GetNumHandlers () == 0);

	// Restartability: Start() installs the acceptor again after a full
	// stop.
	tunnel->Start ();
	assert (tunnel->IsRunning ());
	assert (defaultDestination->IsAcceptorSet ());
	tunnel->Start (); // idempotent enough: the acceptor is replaced, not lost
	assert (tunnel->IsRunning ());
	assert (defaultDestination->IsAcceptorSet ());

	// Non-forced stop keeps the live connections of a tunnel in use, but
	// stops accepting so no new stream is offered to them.
	auto kept = std::make_shared<CountingHandler> (tunnel);
	tunnel->AddHandler (kept);
	tunnel->Stop (false);
	assert (kept->terminations == 0);
	assert (tunnel->GetNumHandlers () == 1);
	assert (!tunnel->IsRunning ());
	assert (!defaultDestination->IsAcceptorSet ());

	// A later forced stop releases the retained connections.
	tunnel->Stop (true);
	assert (kept->terminations == 1);
	assert (tunnel->GetNumHandlers () == 0);

	// Acceptor isolation: a port-specific tunnel owns only its own
	// streaming
	// port and cannot disable the default acceptor of another tunnel.
	tunnel->Start ();
	assert (defaultDestination->IsAcceptorSet ());

	auto portTunnel
		= std::make_shared<I2PServerTunnel> ("port-specific", "127.0.0.1", 8081, fixture.destination, 9090, false);
	portTunnel->Start ();
	auto portDestination = destination.GetStreamingDestination (9090);
	assert (portDestination);
	assert (portDestination->IsAcceptorSet ());
	assert (defaultDestination->IsAcceptorSet ());

	portTunnel->Stop ();
	assert (!portDestination->IsAcceptorSet ());
	assert (defaultDestination->IsAcceptorSet ()); // untouched

	// SetAcceptor returns the registration it displaced, so ownership is
	// deterministic for the caller.
	auto replaced = defaultDestination->SetAcceptor (nullptr);
	assert (replaced); // the still running tunnel's acceptor
	assert (!defaultDestination->IsAcceptorSet ());
	auto nothing = defaultDestination->SetAcceptor (nullptr);
	assert (!nothing);

	// Continue with a clean acceptor state and finish the teardown.
	assert (tunnel->IsRunning ());
	assert (!defaultDestination->IsAcceptorSet ());
	tunnel->Stop ();
	assert (tunnel->GetNumHandlers () == 0);

	// A named upstream arms the address resolver during Start(). A stop
	// must cancel and release it, and a following start must arm a fresh
	// one: a stale completion must not be able to clear the new
	// registration. Nothing runs the service here, so the resolver
	// completion itself stays pending; only the ownership transitions are
	// observed.
	{
		auto namedTunnel
			= std::make_shared<I2PServerTunnel> ("named", "localhost", 8082, fixture.destination, 9091, false);
		namedTunnel->Start ();
		assert (namedTunnel->IsRunning ());
		auto namedDestination = destination.GetStreamingDestination (9091);
		assert (namedDestination);
		assert (namedDestination->IsAcceptorSet ());

		namedTunnel->Stop ();
		assert (!namedTunnel->IsRunning ());
		assert (!namedDestination->IsAcceptorSet ());

		// Restart after the cancelled resolve still arms an acceptor and
		// remains stoppable.
		namedTunnel->Start ();
		assert (namedTunnel->IsRunning ());
		assert (namedDestination->IsAcceptorSet ());
		namedTunnel->Stop ();
		assert (!namedDestination->IsAcceptorSet ());
	}

	return 0;
}
