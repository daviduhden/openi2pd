/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test i2cp session lifecycle.
 *
 * Exercises I2CP session allocation, collision handling, idempotent termination and self-stop joins on loopback port 0, without a router or external network.
 *
 */

#include <cassert>
#include <chrono>
#include <functional>
#include <memory>
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
} // namespace

int main ()
{
	using namespace i2p::client;

	I2CPServer server ("127.0.0.1", 0, true);

	// Sentinel and collision handling. The identifier is assigned only when the
	// insertion succeeds.
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

	// Idempotent termination, marshalled from the test thread to the service
	// thread, must remove exactly the inserted identifier.
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

	// A destination stopping itself from its own worker must defer the join to
	// a different thread. Reaching the destructor with a joinable thread would
	// terminate the process.
	{
		auto socket = std::make_shared<boost::asio::ip::tcp::socket> (server.GetService ());
		auto owner = std::make_shared<I2CPSession> (server, socket);
		i2p::util::Mapping params;
		auto destination = std::make_shared<RunnableI2CPDestination> (owner, MakeIdentity (), false, params);
		destination->Start ();
		auto & service = destination->GetService ();
		boost::asio::post (service, [destination] () { destination->Stop (); });
		assert (WaitUntil ([&] { return service.stopped (); }, std::chrono::seconds (2)));
		// The internal self-reference and the joiner thread release the
		// destination once the worker has been joined.
		assert (WaitUntil ([&] { return destination.use_count () == 1; }, std::chrono::seconds (5)));
		// Second and third calls are no-ops and must not self-join.
		destination->Stop ();
		destination->Stop ();
	}

	server.Stop ();
	return 0;
}
