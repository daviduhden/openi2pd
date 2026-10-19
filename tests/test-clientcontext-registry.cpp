/*
 * Copyright (c) 2026, David Uhden Collado
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Regression coverage for ClientContext's destination registry. Readers use
 * the locked snapshots while the owner adds, replaces and deletes entries.
 * No router, tunnel or external network is started; destinations are local
 * objects on an in-process io_context.
 */

#include <atomic>
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

	// Replacing the key is visible to both lookup styles and keeps the same
	// destination object.
	i2p::data::IdentHash replacement;
	replacement.Fill (0xA5);
	assert (context.ReplaceLocalDestinationHash (hashA, replacement));
	assert (context.FindLocalDestination (hashA) == nullptr);
	assert (context.FindLocalDestination (replacement) == destinationA);
	assert (!Contains (context.GetDestinationsList (), hashA));
	assert (Contains (context.GetDestinationsList (), replacement));

	// Concurrent readers must always see a complete snapshot, and a deleted
	// destination must disappear atomically.
	{
		std::atomic<bool> stop { false };
		std::atomic<int> snapshots { 0 };
		std::vector<std::thread> readers;
		for (int i = 0; i < 4; i++)
			readers.emplace_back (
				[&]
				{
					while (!stop.load ())
					{
						auto list = context.GetDestinationsList ();
						for (const auto & item : list) assert (item.second);
						assert (Contains (list, replacement));
						snapshots.fetch_add (1);
					}
				});

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
