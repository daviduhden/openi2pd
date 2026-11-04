/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test i2cp session lifecycle.
 *
 * Exercises I2CP session allocation, collision handling, idempotent
 * termination and self-stop joins on loopback port 0, without a router
 * or external network.
 *
 */

#include <array>
#include <barrier>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>

#include <boost/asio.hpp>

#include "I2CP.h"
#include "Identity.h"

namespace
{
bool WaitUntil (const std::function<bool ()> & predicate, std::chrono::milliseconds timeout)
{
	auto deadline = std::chrono::steady_clock::now () + timeout;
	while (std::chrono::steady_clock::now () < deadline)
	{
		if (predicate ()) return true;
		std::this_thread::sleep_for (std::chrono::milliseconds (10));
	}
	return predicate ();
}

std::shared_ptr<i2p::data::IdentityEx> MakeIdentity ()
{
	auto keys = i2p::data::PrivateKeys::CreateRandomKeys (
		i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ELGAMAL, true);
	return std::make_shared<i2p::data::IdentityEx> (keys.GetPublic ()->GetStandardIdentity ());
}

class TestRunnableI2CPDestination final : public i2p::client::RunnableI2CPDestination
{
public:
	using RunnableI2CPDestination::RunnableI2CPDestination;
	void QueueLeaseSetCreation () { CreateNewLeaseSet ({ }); }
};

void AssertClosed (boost::asio::ip::tcp::socket & socket)
{
	socket.non_blocking (true);
	assert (WaitUntil ([&]
		{
			char byte;
			boost::system::error_code error;
			socket.read_some (boost::asio::buffer (&byte, 1), error);
			if (error == boost::asio::error::would_block || error == boost::asio::error::try_again) return false;
			assert (error == boost::asio::error::eof || error == boost::asio::error::connection_reset);
			return true;
		}, std::chrono::seconds (2)));
}

void TestServerLifecycle ()
{
	using namespace i2p::client;
	boost::asio::io_context clients;
	I2CPServer server ("127.0.0.1", 0, true);
	const auto endpoint = server.GetEndpoint ();
	server.Start ();
	server.Start ();
	boost::asio::ip::tcp::socket silent (clients), partial (clients);
	silent.connect (endpoint);
	partial.connect (endpoint);
	const std::array<uint8_t, 3> prefix { I2CP_PROTOCOL_BYTE, 0, 0 };
	boost::asio::write (partial, boost::asio::buffer (prefix));
	assert (WaitUntil ([&] { return server.GetNumConnections () == 2; }, std::chrono::seconds (2)));
	assert (server.GetSessionsList ().empty ());
	server.Stop ();
	assert (server.GetNumConnections () == 0);
	AssertClosed (silent);
	AssertClosed (partial);
	server.Stop ();
	server.GetService ().restart ();
	assert (server.GetService ().poll () == 0); // all canceled handlers drained
	server.GetService ().stop ();
	server.Start ();

	// Hold the old executor while the controller closes admission. Start
	// must wait for that generation's join and drain before publishing work.
	std::mutex mutex;
	std::condition_variable changed;
	bool entered = false, release = false;
	boost::asio::post (server.GetService (), [&]
		{
			std::unique_lock lock (mutex);
			entered = true;
			changed.notify_all ();
			changed.wait (lock, [&] { return release; });
		});
	{
		std::unique_lock lock (mutex);
		assert (changed.wait_for (lock, std::chrono::seconds (2), [&] { return entered; }));
	}
	// This duplicate owner-thread Stop will execute during the later drain;
	// it must not interrupt that drain or strand its queued cleanup callback.
	boost::asio::post (server.GetService (), [&] { server.Stop (); });
	std::thread stopper ([&] { server.Stop (); });
	assert (WaitUntil ([&] { return server.IsStopping (); }, std::chrono::seconds (2)));
	std::thread starter ([&] { server.Start (); });
	{
		std::lock_guard lock (mutex);
		release = true;
	}
	changed.notify_all ();
	stopper.join ();
	starter.join ();
	assert (!server.IsStopping ());
	boost::asio::ip::tcp::socket restarted (clients);
	restarted.connect (endpoint);
	assert (WaitUntil ([&] { return server.GetNumConnections () == 1; }, std::chrono::seconds (2)));

	// A service-thread shutdown leaves its join to the control thread.
	std::promise<void> stopped;
	auto future = stopped.get_future ();
	boost::asio::post (server.GetService (), [&] { server.Stop (); stopped.set_value (); });
	assert (future.wait_for (std::chrono::seconds (2)) == std::future_status::ready);
	server.Start ();
	assert (server.GetNumConnections () == 0);
	AssertClosed (restarted);
	server.Stop ();

	// A public session snapshot can outlive the facade. Its terminated
	// session must retain the executor until its timer/socket destructors run.
	std::shared_ptr<I2CPSession> retained;
	{
		auto owner = std::make_unique<I2CPServer> ("127.0.0.1", 0, true);
		owner->Start ();
		auto socket = std::make_shared<boost::asio::ip::tcp::socket> (owner->GetService ());
		retained = std::make_shared<I2CPSession> (*owner, socket);
		assert (owner->InsertSession (retained, 0x1234));
		auto snapshot = owner->GetSessionsList ();
		assert (snapshot.size () == 1 && snapshot.front ().second == retained);
		// External publication and retirement share lifetime authority.
		// Race these paths under the sanitizer lanes without fixed sleeps.
		std::barrier retireGate (2);
		std::jthread publisher ([&]
			{
				retireGate.arrive_and_wait ();
				for (unsigned i = 0; i < 64; ++i)
					retained->SendI2CPMessage (I2CP_SET_DATE_MESSAGE, nullptr, 0);
				retained->Stop ();
			});
		retireGate.arrive_and_wait ();
		owner.reset ();
		publisher.join ();
		assert (retained->GetSessionID () == 0xFFFF);
	}
	retained->Stop ();
	retained->SendI2CPMessage (I2CP_SET_DATE_MESSAGE, nullptr, 0);
	retained.reset ();
}
} // namespace

int main ()
{
	using namespace i2p::client;

	I2CPServer server ("127.0.0.1", 0, true);

	// Sentinel and collision handling. The identifier is assigned only
	// when the insertion succeeds.
	{
		auto socket = std::make_shared<boost::asio::ip::tcp::socket> (server.GetService ());
		auto session = std::make_shared<I2CPSession> (server, socket);
		assert (session->GetSessionID () == 0xFFFF);
		assert (!server.InsertSession (session, 0xFFFF));
		assert (session->GetSessionID () == 0xFFFF);
		assert (server.InsertSession (session, 0x1234));
		assert (session->GetSessionID () == 0x1234);
		assert (server.FindSessionByID (0x1234) == session);

		auto otherSocket = std::make_shared<boost::asio::ip::tcp::socket> (server.GetService ());
		auto other = std::make_shared<I2CPSession> (server, otherSocket);
		assert (!server.InsertSession (other, 0x1234));
		assert (other->GetSessionID () == 0xFFFF);
		assert (server.InsertSession (other, 0x4321));
		assert (other->GetSessionID () == 0x4321);

		// Removing the first session must not disturb the second one.
		server.RemoveSession (0x1234);
		assert (server.FindSessionByID (0x1234) == nullptr);
		assert (server.FindSessionByID (0x4321) == other);
		server.RemoveSession (0x4321);
	}

	server.Start ();

	// Idempotent termination, marshalled from the test thread to the
	// service thread, must remove exactly the inserted identifier.
	{
		auto socket = std::make_shared<boost::asio::ip::tcp::socket> (server.GetService ());
		auto session = std::make_shared<I2CPSession> (server, socket);
		assert (server.InsertSession (session, 0x2222));
		uint16_t id = session->GetSessionID ();
		session->Stop ();
		session->Stop ();
		assert (WaitUntil ([&] { return server.FindSessionByID (id) == nullptr; }, std::chrono::seconds (2)));
		assert (session->GetSessionID () == 0xFFFF);
	}

	// A destination stopping itself from its own worker must defer the
	// join to a different thread. Reaching the destructor with a joinable
	// thread would terminate the process.
	{
		auto socket = std::make_shared<boost::asio::ip::tcp::socket> (server.GetService ());
		auto owner = std::make_shared<I2CPSession> (server, socket);
		i2p::util::Mapping params;
		auto destination = std::make_shared<RunnableI2CPDestination> (owner, MakeIdentity (), false, params);
		destination->Start ();
		auto & service = destination->GetService ();
		boost::asio::post (service, [destination] () { destination->Stop (); });
		assert (WaitUntil ([&] { return service.stopped (); }, std::chrono::seconds (2)));
		// Repeated calls after self-stop must not self-join or re-enter stop.
		destination->Stop ();
		destination->Stop ();
		// Drop the caller's ownership and prove the stopped context's timer
		// handlers do not keep the destination alive.
		std::weak_ptr<RunnableI2CPDestination> stoppedDestination = destination;
		destination.reset ();
		assert (WaitUntil ([&] { return stoppedDestination.expired (); }, std::chrono::seconds (30)));
	}

	// A queued LeaseSet callback must not keep a stopped destination alive
	// through the destination-owned io_context. Hold the service thread in a
	// blocker so Stop() can stop the context while the callback remains queued.
	{
		auto socket = std::make_shared<boost::asio::ip::tcp::socket> (server.GetService ());
		auto owner = std::make_shared<I2CPSession> (server, socket);
		i2p::util::Mapping params;
		auto destination = std::make_shared<TestRunnableI2CPDestination> (
			owner, MakeIdentity (), false, params);
		destination->Start ();
		auto & service = destination->GetService ();
		std::mutex mutex;
		std::condition_variable changed;
		bool entered = false;
		bool release = false;
		boost::asio::post (service, [&]
			{
				std::unique_lock<std::mutex> lock (mutex);
				entered = true;
				changed.notify_all ();
				changed.wait (lock, [&] { return release; });
			});
		{
			std::unique_lock<std::mutex> lock (mutex);
			assert (changed.wait_for (lock, std::chrono::seconds (2), [&] { return entered; }));
		}
		destination->QueueLeaseSetCreation ();
		std::weak_ptr<TestRunnableI2CPDestination> weak = destination;
		std::thread stopper ([&] { destination->Stop (); });
		assert (WaitUntil ([&] { return service.stopped (); }, std::chrono::seconds (2)));
		{
			std::lock_guard<std::mutex> lock (mutex);
			release = true;
		}
		changed.notify_all ();
		stopper.join ();
		destination.reset ();
		assert (weak.expired ());
	}

	// The concurrent-connection counter is charged only by HandleAccept: a
	// session built directly never releases an uncharged slot, so repeated
	// termination cannot underflow the counter.
	assert (server.GetNumConnections () == 0);
	{
		auto socket = std::make_shared<boost::asio::ip::tcp::socket> (server.GetService ());
		auto session = std::make_shared<I2CPSession> (server, socket);
		session->Stop ();
		session->Stop ();
	}
	assert (server.GetNumConnections () == 0);
	assert (server.GetAdmissionPolicy ().maxConnections >= 1);
	assert (server.GetAdmissionPolicy ().maxSessions >= 1);

	server.Stop ();
	TestServerLifecycle ();
	return 0;
}
