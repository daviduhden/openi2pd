/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * Exercises synchronous stream-result publication across executor and
 * caller threads without a router, tunnel pool or network connection.
 * The destination is deliberately unready, so each asynchronous request
 * completes with nullptr. TSan observes the actual result/predicate seam.
 */

#include <cassert>
#include <future>
#include <memory>
#include <thread>

#include <boost/asio.hpp>

#include "Destination.h"

int main ()
{
	boost::asio::io_context service;
	auto work = boost::asio::make_work_guard (service);
	auto keys = i2p::data::PrivateKeys::CreateRandomKeys (
		i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
	auto destination = std::make_shared<i2p::client::ClientDestination> (service, keys, false);
	i2p::data::IdentHash remote;
	remote.Fill (0);
	std::shared_ptr<const i2p::data::BlindedPublicKey> blinded;
	std::thread executor ([&service] { service.run (); });

	// Exercise both synchronous template instantiations while completions
	// can arrive before or after the caller enters its predicate wait.
	for (int iteration = 0; iteration < 64; ++iteration)
	{
		auto ordinary = destination->CreateStream (remote);
		assert (!ordinary);
		auto encrypted = destination->CreateStream (blinded);
		assert (!encrypted);
	}

	// A completion executor cannot synchronously wait for itself. Both
	// overloads must reject before posting their asynchronous operation.
	// CTest's existing deadline detects a regression without sleep loops.
	std::promise<bool> ownThreadResult;
	auto ownThreadDone = ownThreadResult.get_future ();
	boost::asio::post (service,
		[destination, remote, blinded, &ownThreadResult]
		{
			auto ordinary = destination->CreateStream (remote);
			auto encrypted = destination->CreateStream (blinded);
			ownThreadResult.set_value (!ordinary && !encrypted);
		});
	const bool ownThreadRejected = ownThreadDone.get ();
	assert (ownThreadRejected);

	work.reset ();
	executor.join ();
	return 0;
}
