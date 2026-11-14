/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test clientcontext registry.
 *
 * Exercises ClientContext destination registration, replacement and
 * deletion against concurrent snapshot readers, requiring complete
 * consistent views without starting a router or network.
 *
 */

#include <atomic>
#include <barrier>
#include <cassert>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include <boost/asio.hpp>

#include "ClientContext.h"
#include "Destination.h"
#include "Identity.h"

namespace
{
using namespace i2p::client;

bool Contains (const std::vector<std::pair<i2p::data::IdentHash, std::shared_ptr<ClientDestination>>> & list,
	const i2p::data::IdentHash & hash)
{
	for (const auto & item : list)
		if (item.first == hash) return true;
	return false;
}
} // namespace

int main ()
{
	boost::asio::io_context service;
	auto work = boost::asio::make_work_guard (service);
	std::thread serviceThread ([&service] { service.run (); });

	ClientContext context;

	// Concurrent creators of one identity must all receive the started
	// registry winner, never an unregistered losing candidate.
	{
		auto keys = i2p::data::PrivateKeys::CreateRandomKeys (
			i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
		std::vector<std::shared_ptr<ClientDestination>> results (8);
		std::vector<std::thread> creators;
		std::barrier ready (8);
		for (size_t index = 0; index < results.size (); ++index)
			creators.emplace_back (
				[&, index]
				{
					ready.arrive_and_wait ();
					results[index] = context.CreateNewLocalDestination (service, keys, false);
				});
		for (auto & creator : creators) creator.join ();
		const auto winner = context.FindLocalDestination (keys.GetPublic ()->GetIdentHash ());
		assert (winner);
		assert (winner->GetStreamingDestination ());
		for (const auto & result : results) assert (result == winner);
		assert (context.GetDestinationsList ().size () == 1);
		context.DeleteLocalDestination (winner);
		assert (context.GetDestinationsList ().empty ());
	}

	auto keysA = i2p::data::PrivateKeys::CreateRandomKeys (
		i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ELGAMAL, true);
	auto keysB = i2p::data::PrivateKeys::CreateRandomKeys (
		i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ELGAMAL, true);
	auto destinationA = context.CreateNewLocalDestination (service, keysA, false);
	auto destinationB = context.CreateNewLocalDestination (service, keysB, false);
	auto hashA = destinationA->GetIdentHash ();
	auto hashB = destinationB->GetIdentHash ();
	assert (!(hashA == hashB));

	assert (context.FindLocalDestination (hashA) == destinationA);
	assert (context.FindLocalDestination (hashB) == destinationB);
	assert (Contains (context.GetDestinationsList (), hashA));
	assert (Contains (context.GetDestinationsList (), hashB));

	// A conflicting replacement must preserve both registrations. The
	// identity replacement is idempotent when the key already matches.
	assert (!context.ReplaceLocalDestinationHash (hashA, hashB));
	assert (context.FindLocalDestination (hashA) == destinationA);
	assert (context.FindLocalDestination (hashB) == destinationB);
	assert (context.ReplaceLocalDestinationHash (hashA, hashA));

	// Replacing the key is visible to both lookup styles and keeps the
	// same destination object.
	i2p::data::IdentHash replacement;
	replacement.Fill (0xA5);
	assert (context.ReplaceLocalDestinationHash (hashA, replacement));
	assert (context.FindLocalDestination (hashA) == nullptr);
	assert (context.FindLocalDestination (replacement) == destinationA);
	assert (!Contains (context.GetDestinationsList (), hashA));
	assert (Contains (context.GetDestinationsList (), replacement));

	// Concurrent readers must always see a complete snapshot, and a
	// deleted destination must disappear atomically.
	{
		std::atomic<bool> stop { false };
		std::atomic<int> snapshots { 0 };
		std::vector<std::thread> readers;
		std::barrier started (5);
		for (int i = 0; i < 4; i++)
			readers.emplace_back (
				[&]
				{
					// Take one snapshot before the main thread mutates the
					// registry. Thread creation alone does not guarantee that
					// this reader has been scheduled.
					{
						auto list = context.GetDestinationsList ();
						for (const auto & item : list) assert (item.second);
						assert (Contains (list, replacement));
						snapshots.fetch_add (1);
					}
					started.arrive_and_wait ();
					while (!stop.load ())
					{
						auto list = context.GetDestinationsList ();
						for (const auto & item : list) assert (item.second);
						assert (Contains (list, replacement));
						snapshots.fetch_add (1);
					}
				});

		// Every reader has observed a complete snapshot before deletion and
		// the subsequent stop. This proves execution without relying on a
		// scheduler delay or an arbitrary sleep.
		started.arrive_and_wait ();
		context.DeleteLocalDestination (destinationB);
		assert (context.FindLocalDestination (hashB) == nullptr);
		assert (!Contains (context.GetDestinationsList (), hashB));

		stop.store (true);
		for (auto & reader : readers) reader.join ();
		assert (snapshots.load () > 0);
	}

	context.DeleteLocalDestination (destinationA);
	assert (context.FindLocalDestination (replacement) == nullptr);
	assert (context.GetDestinationsList ().empty ());

	service.stop ();
	serviceThread.join ();
	return 0;
}
