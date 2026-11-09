/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * Exercise readiness ownership without listeners or live peers. Friend
 * access delivers actual timer transactions deterministically; canceled
 * executor completions still drain through the production handler.
 */

#include <cassert>
#include <barrier>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

#include "I2PService.h"
#include "Streaming.h"

namespace i2p::client
{
struct I2PServiceReadinessTestAccess
{
	static auto Snapshot (I2PService & service)
	{
		auto generation = service.GetReadyGeneration ();
		assert (generation);
		std::lock_guard lock (generation->mutex);
		return std::pair (generation, generation->wait);
	}
	static size_t Pending (I2PService & service)
	{
		auto generation = service.GetReadyGeneration ();
		if (!generation) return 0;
		std::lock_guard lock (generation->mutex);
		return generation->callbacks.size ();
	}
	static uint64_t LastDeadline (I2PService & service)
	{
		auto generation = service.GetReadyGeneration ();
		std::lock_guard lock (generation->mutex);
		return generation->callbacks.back ().second;
	}
	template<class Ticket> static void Cancel (const Ticket & ticket)
	{
		std::lock_guard lock (ticket.first->mutex);
		ticket.first->timer.cancel ();
	}
	template<class Ticket> static void Deliver (const Ticket & ticket,
		const boost::system::error_code & error)
	{
		assert (ticket.second);
		I2PService::HandleReadyCheckTimer (error, ticket.first, ticket.second);
	}
	static auto Guard (I2PService & service, StreamRequestComplete complete)
	{
		return I2PService::GuardStreamCompletion (service.GetReadyGeneration (), std::move (complete));
	}
	static auto IdleSnapshot (I2PService & service)
	{
		std::lock_guard lock (service.m_IdleMutex);
		return service.m_IdleWait;
	}
	static auto IdleDeadline (boost::asio::steady_timer::time_point now,
		uint64_t idleTime)
	{
		return I2PService::IdleDeadline (now, idleTime);
	}
	static void RearmIdle (I2PService & service)
	{
		service.ScheduleIdleCheckTimer ();
	}
	template<class Ticket> static void DeliverIdle (I2PService & service,
		const Ticket & ticket, const boost::system::error_code & error)
	{
		service.HandleIdleCheckTimer (error, ticket);
	}
};
}

namespace
{
using Access = i2p::client::I2PServiceReadinessTestAccess;
using Error = boost::system::error_code;

struct Fixture
{
	boost::asio::io_context executor;
	std::shared_ptr<i2p::client::ClientDestination> destination;
	std::shared_ptr<i2p::client::I2PService> owner;

	Fixture ()
	{
		auto keys = i2p::data::PrivateKeys::CreateRandomKeys (
			i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519,
			i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
		destination = std::make_shared<i2p::client::ClientDestination> (executor, keys, false);
		owner = std::make_shared<i2p::client::I2PService> (destination);
	}
	~Fixture ()
	{
		if (owner) owner->Stop ();
		owner.reset ();
		Drain ();
	}
	void Drain ()
	{
		executor.restart ();
		executor.poll ();
	}
	void Dispatch ()
	{
		auto ticket = Access::Snapshot (*owner);
		Access::Cancel (ticket);
		Access::Deliver (ticket, boost::asio::error::timed_out);
		Drain ();
	}
};

void TestReentrantRegistrationAndStaleWait ()
{
	Fixture fixture;
	int first = 0, second = 0;
	// Construction, without Start(), preserves the pre-start API.
	fixture.owner->AddReadyCallback ([&] (const Error & error)
		{
			assert (error == boost::asio::error::timed_out);
			++first;
			fixture.owner->AddReadyCallback ([&] (const Error & nested)
				{ assert (nested == boost::asio::error::timed_out); ++second; });
		});
	fixture.Dispatch ();
	assert (first == 1 && second == 0);
	assert (Access::Pending (*fixture.owner) == 1);
	// Draining the canceled first wait must not cancel the rearmed wait.
	fixture.Dispatch ();
	assert (first == 1 && second == 1);
}

void TestStopRestartAndReentrantStop ()
{
	Fixture fixture;
	int retired = 0, current = 0;
	fixture.owner->AddReadyCallback ([&] (const Error & error)
		{ assert (error == boost::asio::error::operation_aborted); ++retired; });
	auto old = Access::Snapshot (*fixture.owner);
	fixture.owner->Stop ();
	fixture.owner->Start ();
	fixture.owner->AddReadyCallback ([&] (const Error & error)
		{ assert (error == boost::asio::error::timed_out); ++current; });
	Access::Deliver (old, boost::asio::error::timed_out);
	fixture.Drain ();
	assert (retired == 1 && current == 0);
	assert (Access::Pending (*fixture.owner) == 1);
	fixture.Dispatch ();
	assert (current == 1);

	fixture.owner->AddReadyCallback ([&] (const Error &)
		{ fixture.owner->Stop (); fixture.owner->Start (); });
	fixture.owner->AddReadyCallback ([&] (const Error & error)
		{ assert (error == boost::asio::error::operation_aborted); ++retired; });
	fixture.Dispatch ();
	assert (retired == 2);
}

void TestClearAndLateStreamCompletion ()
{
	Fixture fixture;
	int calls = 0;
	auto completion = Access::Guard (*fixture.owner,
		[&] (auto stream) { assert (!stream); ++calls; });
	fixture.owner->ClearHandlers ();
	auto streaming = std::make_shared<i2p::stream::StreamingDestination> (fixture.destination);
	auto stream = std::make_shared<i2p::stream::Stream> (fixture.executor, streaming);
	completion (stream);
	fixture.Drain ();
	assert (calls == 1);
	assert (stream->GetStatus () == i2p::stream::eStreamStatusTerminated);

	fixture.owner->Stop ();
	fixture.owner->ClearHandlers (); // Clearing must not reopen a stop.
	fixture.owner->AddReadyCallback ([&] (const Error & error)
		{ assert (error == boost::asio::error::operation_aborted); ++calls; });
	fixture.Drain ();
	assert (calls == 2 && Access::Pending (*fixture.owner) == 0);

	fixture.owner->Start ();
	auto destroyed = Access::Guard (*fixture.owner,
		[&] (auto result) { assert (!result); ++calls; });
	std::weak_ptr<i2p::client::I2PService> released = fixture.owner;
	fixture.owner.reset ();
	assert (released.expired ());
	auto late = std::make_shared<i2p::stream::Stream> (fixture.executor, streaming);
	destroyed (late);
	fixture.Drain ();
	assert (calls == 3);
	assert (late->GetStatus () == i2p::stream::eStreamStatusTerminated);
}

void TestQueueBoundAndTimeoutSaturation ()
{
	Fixture fixture;
	fixture.owner->SetConnectTimeout ((std::numeric_limits<uint64_t>::max) ());
	size_t aborted = 0, busy = 0;
	for (size_t index = 0; index < i2p::client::MAX_SERVICE_READY_CALLBACKS; ++index)
		fixture.owner->AddReadyCallback ([&] (const Error & error)
			{ assert (error == boost::asio::error::operation_aborted); ++aborted; });
	assert (Access::LastDeadline (*fixture.owner) == (std::numeric_limits<uint64_t>::max) ());
	fixture.owner->AddReadyCallback ([&] (const Error & error)
		{ assert (error == boost::asio::error::no_buffer_space); ++busy; });
	assert (busy == 1); // Refusal cannot grow the executor's work queue.
	fixture.Drain ();
	assert (busy == 1 && aborted == 0);
	assert (Access::Pending (*fixture.owner) == i2p::client::MAX_SERVICE_READY_CALLBACKS);
	fixture.owner->Stop ();
	fixture.Drain ();
	assert (aborted == i2p::client::MAX_SERVICE_READY_CALLBACKS);
}

void TestThrowingCallbackDoesNotAbandonPeers ()
{
	Fixture fixture;
	int completed = 0;
	fixture.owner->AddReadyCallback ([] (const Error &) { throw std::runtime_error ("fixture"); });
	fixture.owner->AddReadyCallback ([&] (const Error &) { ++completed; });
	fixture.Dispatch ();
	assert (completed == 1);
}

void TestPendingTimerDoesNotRetainOwnerOrDestination ()
{
	Fixture fixture;
	int aborted = 0;
	fixture.owner->AddReadyCallback ([&] (const Error & error)
		{ assert (error == boost::asio::error::operation_aborted); ++aborted; });
	std::weak_ptr<i2p::client::I2PService> owner = fixture.owner;
	std::weak_ptr<i2p::client::ClientDestination> destination = fixture.destination;
	fixture.owner.reset ();
	fixture.destination.reset ();
	assert (owner.expired () && destination.expired ());
	assert (aborted == 0); // Cancellation is queued, not invoked in teardown.
	fixture.Drain ();
	assert (aborted == 1);
}

void TestOwnedExecutorDestructionWithPendingCancellation ()
{
	auto keys = i2p::data::PrivateKeys::CreateRandomKeys (
		i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519,
		i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
	// Do not start the destination: destruction shuts down its executor
	// without a worker thread or final poll. Queued callbacks are
	// discarded with that executor.
	auto destination = std::make_shared<i2p::client::RunnableClientDestination> (keys, false);
	auto service = std::make_shared<i2p::client::I2PService> (destination);
	service->SetCloseIdleTime (i2p::client::I2P_SERVICE_MIN_CLOSE_IDLE_TIME);
	service->Start ();
	int invoked = 0;
	service->AddReadyCallback ([&] (const Error &) { ++invoked; });
	std::weak_ptr<i2p::client::I2PService> releasedService = service;
	std::weak_ptr<i2p::client::RunnableClientDestination> releasedDestination = destination;
	service.reset ();
	destination.reset ();
	assert (releasedService.expired () && releasedDestination.expired ());
	assert (invoked == 0);
}

void TestIdleWaitRevocationAndRestart ()
{
	Fixture fixture;
	fixture.owner->SetCloseIdleTime (i2p::client::I2P_SERVICE_MIN_CLOSE_IDLE_TIME);
	fixture.owner->Start ();
	auto first = Access::IdleSnapshot (*fixture.owner);
	assert (first);
	Access::RearmIdle (*fixture.owner);
	auto second = Access::IdleSnapshot (*fixture.owner);
	assert (second && second != first);
	Access::DeliverIdle (*fixture.owner, first, { });
	assert (Access::IdleSnapshot (*fixture.owner) == second);
	Access::DeliverIdle (*fixture.owner, second,
		boost::asio::error::operation_aborted);
	assert (Access::IdleSnapshot (*fixture.owner) == second);
	fixture.owner->Stop ();
	Access::DeliverIdle (*fixture.owner, second, { });
	assert (!Access::IdleSnapshot (*fixture.owner));
	assert (!fixture.destination->IsIdling ());
	fixture.owner->Start ();
	auto restarted = Access::IdleSnapshot (*fixture.owner);
	assert (restarted && restarted != second);
	Access::DeliverIdle (*fixture.owner, second, { });
	assert (Access::IdleSnapshot (*fixture.owner) == restarted);
	assert (!fixture.destination->IsIdling ());
}

void TestIdleTimerDoesNotRetainOwnerOrDestination ()
{
	Fixture fixture;
	fixture.owner->SetCloseIdleTime (i2p::client::I2P_SERVICE_MIN_CLOSE_IDLE_TIME);
	fixture.owner->Start ();
	assert (Access::IdleSnapshot (*fixture.owner));
	std::weak_ptr<i2p::client::I2PService> owner = fixture.owner;
	std::weak_ptr<i2p::client::ClientDestination> destination = fixture.destination;
	fixture.owner.reset ();
	fixture.destination.reset ();
	assert (owner.expired () && destination.expired ());
	fixture.Drain ();
}

void TestIdleDeadlineSaturation ()
{
	using Timer = boost::asio::steady_timer;
	using std::chrono::milliseconds;
	const auto epoch = Timer::time_point { };
	assert (Access::IdleDeadline (epoch, 60000) == epoch + milliseconds (30000));
	assert (Access::IdleDeadline (epoch, 0) == epoch);
	const auto nearEnd = Timer::time_point::max () - milliseconds (1);
	assert (Access::IdleDeadline (nearEnd, 60000) == Timer::time_point::max ());
	assert (Access::IdleDeadline (nearEnd,
		std::numeric_limits<uint64_t>::max ()) == Timer::time_point::max ());
}

void TestFreshActivityRearmsIdleAuthority ()
{
	Fixture fixture;
	fixture.owner->SetCloseIdleTime (i2p::client::I2P_SERVICE_MIN_CLOSE_IDLE_TIME);
	fixture.owner->Start ();
	auto retired = Access::IdleSnapshot (*fixture.owner);
	fixture.destination->SetIsIdling (true);
	fixture.owner->UpdateLastActivityTime ();
	assert (!fixture.destination->IsIdling ());
	auto current = Access::IdleSnapshot (*fixture.owner);
	assert (current && current != retired);
	Access::DeliverIdle (*fixture.owner, retired, { });
	assert (!fixture.destination->IsIdling ());
	assert (Access::IdleSnapshot (*fixture.owner) == current);
}

void TestDestinationIdleStateAcrossThreads ()
{
	Fixture fixture;
	// These production APIs are used by the service and tunnel pool.
	// The barrier establishes concurrent authority without timing sleeps.
	std::barrier ready (2);
	unsigned observations = 0;
	std::jthread writer ([&]
		{
			ready.arrive_and_wait ();
			for (unsigned index = 0; index < 10000; ++index)
				fixture.destination->SetIsIdling (index % 2 != 0);
			fixture.destination->SetIsIdling (true);
		});
	std::jthread reader ([&]
		{
			ready.arrive_and_wait ();
			for (unsigned index = 0; index < 10000; ++index)
				observations += fixture.destination->IsIdling () ? 1 : 0;
		});
	writer.join ();
	reader.join ();
	assert (observations <= 10000);
	assert (fixture.destination->IsIdling ());
}
}

int main ()
{
	TestReentrantRegistrationAndStaleWait ();
	TestStopRestartAndReentrantStop ();
	TestClearAndLateStreamCompletion ();
	TestQueueBoundAndTimeoutSaturation ();
	TestThrowingCallbackDoesNotAbandonPeers ();
	TestPendingTimerDoesNotRetainOwnerOrDestination ();
	TestOwnedExecutorDestructionWithPendingCancellation ();
	TestIdleWaitRevocationAndRestart ();
	TestIdleTimerDoesNotRetainOwnerOrDestination ();
	TestIdleDeadlineSaturation ();
	TestFreshActivityRearmsIdleAuthority ();
	TestDestinationIdleStateAcrossThreads ();
	return 0;
}
