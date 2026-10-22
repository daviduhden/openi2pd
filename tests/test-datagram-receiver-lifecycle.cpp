/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test datagram receiver lifecycle.
 *
 * Covers identity-bearing receiver reset, wildcard fallback compatibility,
 * draining callbacks before owner teardown, and reset from inside a callback.
 *
 */

#include <cassert>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <boost/asio.hpp>

#include "Datagram.h"
#include "Destination.h"

namespace
{
std::vector<uint8_t> MakeEmptyStoredGzipMember ()
{
	// DatagramDestination inflates every incoming protocol. This minimal
	// stored empty gzip member uses the parser's documented zero-block form.
	return { 0x1f, 0x8b, 0x08, 0, 0, 0, 0, 0, 0, 0xff, 0x01, 0, 0, 0xff, 0xff, 0, 0, 0, 0, 0, 0, 0, 0 };
}

std::shared_ptr<i2p::datagram::DatagramDestination> MakeDatagramDestination ()
{
	// Create the destination directly: storing it through
	// ClientDestination::CreateDatagramDestination would form the owner's
	// intentional destination ownership cycle in this isolated fixture.
	auto keys = i2p::data::PrivateKeys::CreateRandomKeys (
		i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
	static boost::asio::io_context service;
	auto owner = std::make_shared<i2p::client::ClientDestination> (service, keys, false);
	return std::make_shared<i2p::datagram::DatagramDestination> (owner, false, i2p::datagram::eDatagramV1);
}

void DispatchRaw (const std::shared_ptr<i2p::datagram::DatagramDestination> & destination, uint16_t port)
{
	auto payload = MakeEmptyStoredGzipMember ();
	destination->HandleDataMessagePayload (
		0, port, payload.data (), payload.size (), i2p::client::PROTOCOL_TYPE_RAW, nullptr);
}

void TestRegistrationIdentityAndFallback ()
{
	auto destination = MakeDatagramDestination ();
	int fallbackCalls = 0, replacementCalls = 0, secondPortCalls = 0;
	auto first = destination->SetRawReceiver (
		[&] (uint8_t, uint16_t, uint16_t, const uint8_t *, size_t) { ++fallbackCalls; }, 41);
	auto second = destination->SetRawReceiver (
		[&] (uint8_t, uint16_t, uint16_t, const uint8_t *, size_t) { ++secondPortCalls; }, 42);
	auto replacement = destination->SetRawReceiver (
		[&] (uint8_t, uint16_t, uint16_t, const uint8_t *, size_t) { ++replacementCalls; }, 41);

	// Port keys are last-wins; the initial port-specific receiver remains the
	// wildcard fallback until an explicit wildcard registration replaces it.
	DispatchRaw (destination, 41);
	DispatchRaw (destination, 42);
	DispatchRaw (destination, 99);
	assert (replacementCalls == 1);
	assert (secondPortCalls == 1);
	assert (fallbackCalls == 1);

	// A stale owner's token cannot remove a replacement at the same port or
	// the replacement's independent lifetime.
	assert (first.Reset ());
	DispatchRaw (destination, 41);
	assert (replacementCalls == 2);
	replacement.Reset ();
	DispatchRaw (destination, 41);
	assert (replacementCalls == 2);
	second.Reset ();
}

void TestResetDrainsInFlightCallback ()
{
	auto destination = MakeDatagramDestination ();
	std::mutex mutex;
	std::condition_variable changed;
	bool entered = false, release = false;
	auto registration = destination->SetRawReceiver (
		[&] (uint8_t, uint16_t, uint16_t, const uint8_t *, size_t)
		{
			std::unique_lock<std::mutex> lock (mutex);
			entered = true;
			changed.notify_all ();
			changed.wait (lock, [&] { return release; });
		},
		73);

	std::thread dispatch ([&] { DispatchRaw (destination, 73); });
	{
		std::unique_lock<std::mutex> lock (mutex);
		changed.wait (lock, [&] { return entered; });
	}
	std::atomic<bool> resetStarted { false }, resetFinished { false }, resetDrained { false };
	std::thread reset (
		[&]
		{
			resetStarted.store (true, std::memory_order_release);
			resetDrained.store (registration.Reset (), std::memory_order_release);
			resetFinished.store (true, std::memory_order_release);
		});
	while (!resetStarted.load (std::memory_order_acquire)) std::this_thread::yield ();
	std::this_thread::sleep_for (std::chrono::milliseconds (10));
	assert (!resetFinished.load (std::memory_order_acquire));
	{
		std::lock_guard<std::mutex> lock (mutex);
		release = true;
	}
	changed.notify_all ();
	dispatch.join ();
	reset.join ();
	assert (resetFinished.load (std::memory_order_acquire));
	assert (resetDrained.load (std::memory_order_acquire));
	DispatchRaw (destination, 73);
}

void TestSelfResetDoesNotDeadlock ()
{
	auto destination = MakeDatagramDestination ();
	i2p::datagram::DatagramDestination::ReceiverRegistration registration;
	bool invoked = false;
	registration = destination->SetRawReceiver (
		[&] (uint8_t, uint16_t, uint16_t, const uint8_t *, size_t)
		{
			invoked = true;
			assert (!registration.Reset ());
		},
		74);
	DispatchRaw (destination, 74);
	assert (invoked);
	invoked = false;
	DispatchRaw (destination, 74);
	assert (!invoked);
}

void TestConcurrentSelfResetDoesNotDeadlock ()
{
	using Callback = std::function<void ()>;
	using Slot = i2p::datagram::detail::ReceiverSlot<Callback>;
	std::atomic<unsigned> entered { 0 };
	Slot * slotPtr = nullptr;
	auto slot = std::make_shared<Slot> (
		[&]
		{
			entered.fetch_add (1, std::memory_order_acq_rel);
			while (entered.load (std::memory_order_acquire) < 2) std::this_thread::yield ();
			slotPtr->DeactivateAndWait ();
		});
	slotPtr = slot.get ();
	std::thread first ([&] { slot->Invoke (); });
	std::thread second ([&] { slot->Invoke (); });
	first.join ();
	second.join ();
	assert (entered.load (std::memory_order_acquire) == 2);
	assert (!slot->Invoke ());
}

void TestCrossSlotSelfResetDoesNotDeadlock ()
{
	using Callback = std::function<void ()>;
	using Slot = i2p::datagram::detail::ReceiverSlot<Callback>;
	std::atomic<unsigned> entered { 0 };
	std::shared_ptr<Slot> first, second;
	auto resetOther = [&] (const std::shared_ptr<Slot> & other)
	{
		entered.fetch_add (1, std::memory_order_acq_rel);
		while (entered.load (std::memory_order_acquire) < 2) std::this_thread::yield ();
		assert (!other->DeactivateAndWait ());
	};
	first = std::make_shared<Slot> ([&] { resetOther (second); });
	second = std::make_shared<Slot> ([&] { resetOther (first); });
	std::thread a ([&] { first->Invoke (); });
	std::thread b ([&] { second->Invoke (); });
	a.join ();
	b.join ();
	assert (entered.load (std::memory_order_acquire) == 2);
}

void TestEachDispatchUsesCallbackSnapshot ()
{
	using Callback = std::function<void ()>;
	using Slot = i2p::datagram::detail::ReceiverSlot<Callback>;
	std::vector<unsigned> observations;
	auto slot = std::make_shared<Slot> ([calls = 0U, &observations] () mutable { observations.push_back (++calls); });
	slot->Invoke ();
	slot->Invoke ();
	assert (observations.size () == 2);
	assert (observations[0] == 1 && observations[1] == 1);
}

void TestReentrantCallbackDestructionOutsideRegistryLock ()
{
	auto destination = MakeDatagramDestination ();
	struct Reentrant
	{
		std::shared_ptr<i2p::datagram::DatagramDestination> destination;
		~Reentrant ()
		{
			destination->SetRawReceiver ([] (uint8_t, uint16_t, uint16_t, const uint8_t *, size_t) { }, 89);
		}
	};
	auto captured = std::make_shared<Reentrant> ();
	captured->destination = destination;
	// Retain only the registration/map references after this scope. Replacing
	// both map and fallback entries releases the callback outside the lock.
	auto original
		= destination->SetRawReceiver ([captured] (uint8_t, uint16_t, uint16_t, const uint8_t *, size_t) { }, 89);
	captured.reset ();
	auto portReplacement
		= destination->SetRawReceiver ([] (uint8_t, uint16_t, uint16_t, const uint8_t *, size_t) { }, 89);
	auto wildcardReplacement
		= destination->SetRawReceiver ([] (uint8_t, uint16_t, uint16_t, const uint8_t *, size_t) { });
	original.Reset ();
	portReplacement.Reset ();
	wildcardReplacement.Reset ();
}
}

int main ()
{
	TestRegistrationIdentityAndFallback ();
	TestResetDrainsInFlightCallback ();
	TestSelfResetDoesNotDeadlock ();
	TestConcurrentSelfResetDoesNotDeadlock ();
	TestCrossSlotSelfResetDoesNotDeadlock ();
	TestEachDispatchUsesCallbackSnapshot ();
	TestReentrantCallbackDestructionOutsideRegistryLock ();
	return 0;
}
