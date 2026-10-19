/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test sam stream lifecycle.
 *
 * Exercises SAM session state: bounded synchronized accept-queue transfer, FIFO and concurrent producers and, over loopback UDP only, serialized datagram sends with handler-owned buffers.
 *
 */

#include <atomic>
#include <cassert>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio.hpp>

#include "SAM.h"

namespace
{
using namespace i2p::client;

struct TestSession : SAMSession
{
	explicit TestSession (SAMBridge & bridge)
		: SAMSession (bridge, "test", SAMSessionType::eSAMSessionTypeStream)
	{
	}

	std::shared_ptr<ClientDestination> GetLocalDestination () override { return nullptr; }
	void StopLocalDestination () override { }
};

bool WaitFor (const std::function<bool ()> & predicate, std::chrono::milliseconds timeout)
{
	auto deadline = std::chrono::steady_clock::now () + timeout;
	while (std::chrono::steady_clock::now () < deadline)
	{
		if (predicate ()) return true;
		std::this_thread::sleep_for (std::chrono::milliseconds (5));
	}
	return predicate ();
}
} // namespace

int main ()
{
	// The control buffer must hold the largest SAM RAW SEND frame (32 KiB
	// payload plus a base64 destination/command), and the receive buffer must
	// hold a 32 KiB datagram plus its header.
	static_assert (SAM_SOCKET_BUFFER_SIZE > 32 * 1024 + 1024);
	static_assert (SAM_STREAM_BUFFER_SIZE > 32 * 1024 + 1024);
	static_assert (SAM_STREAM_MAX_SEND_BUFFER_SIZE >= 32 * 1024);

	// Sockets below only construct their (unbound) TCP objects; the bridge is
	// not started until the loopback send test.
	SAMBridge bridge ("127.0.0.1", 0, 0, true);
	TestSession session (bridge);

	std::vector<std::shared_ptr<SAMSocket>> sockets;
	for (size_t i = 0; i < 60; i++) sockets.emplace_back (std::make_shared<SAMSocket> (bridge));

	const uint64_t now = i2p::util::GetSecondsSinceEpoch ();
	for (size_t i = 0; i < SAM_SESSION_MAX_ACCEPT_QUEUE_SIZE; i++) assert (session.EnqueueAccept (sockets[i], now));
	assert (!session.EnqueueAccept (sockets[SAM_SESSION_MAX_ACCEPT_QUEUE_SIZE], now));
	assert (session.GetAcceptQueueSize () == SAM_SESSION_MAX_ACCEPT_QUEUE_SIZE);

	// Java keeps queued acceptors waiting indefinitely. Advancing time must
	// not expire or drop them.
	std::list<std::shared_ptr<SAMSocket>> dropped;
	for (size_t i = 0; i < SAM_SESSION_MAX_ACCEPT_QUEUE_SIZE; i++)
		assert (session.TakeNextAccept (now + 10 * 60, dropped) == sockets[i]);
	assert (dropped.empty ());
	assert (session.GetAcceptQueueSize () == 0);

	// FIFO transfer of a live entry.
	assert (session.EnqueueAccept (sockets[0], now));
	assert (session.EnqueueAccept (sockets[1], now));
	auto first = session.TakeNextAccept (now + 1, dropped);
	assert (first == sockets[0]);
	assert (dropped.empty ());
	assert (session.GetAcceptQueueSize () == 1);
	assert (session.TakeNextAccept (now + 1, dropped) == sockets[1]);
	assert (dropped.empty ());
	assert (session.GetAcceptQueueSize () == 0);

	// Concurrent producers and one consumer. Capacity is above the total
	// number of entries, so every enqueue must succeed.
	{
		std::atomic<bool> start { false };
		std::atomic<size_t> consumed { 0 };
		std::vector<std::thread> producers;
		for (int t = 0; t < 4; t++)
			producers.emplace_back (
				[t, &session, &sockets, &start, now] ()
				{
					while (!start.load ()) std::this_thread::yield ();
					for (size_t i = 0; i < 10; i++) assert (session.EnqueueAccept (sockets[10 + t * 10 + i], now));
				});
		start.store (true);
		while (consumed.load () < 40)
		{
			auto socket = session.TakeNextAccept (now + 1, dropped);
			if (socket)
				consumed.fetch_add (1);
			else
				std::this_thread::yield ();
		}
		for (auto & producer : producers) producer.join ();
		assert (session.GetAcceptQueueSize () == 0);
	}

	// Loopback UDP send: two buffers must arrive concatenated, and the queue
	// must keep the copied bytes alive after the caller's strings are gone.
	bridge.Start ();
	{
		boost::asio::ip::udp::socket receiver (
			bridge.GetService (), boost::asio::ip::udp::endpoint (boost::asio::ip::make_address ("127.0.0.1"), 0));
		auto endpoint = receiver.local_endpoint ();
		auto received = std::make_shared<std::promise<std::vector<uint8_t>>> ();
		auto future = received->get_future ();
		std::vector<uint8_t> buffer (64);
		receiver.async_receive (boost::asio::buffer (buffer),
			[received, buffer = std::move (buffer)] (const boost::system::error_code & ec, std::size_t len) mutable
			{
				if (ec)
					received->set_value ({ });
				else
				{
					buffer.resize (len);
					received->set_value (std::move (buffer));
				}
			});

		{
			std::string firstPart = "SAM-";
			std::string secondPart = "DATAGRAM";
			bridge.SendTo (
				{ { firstPart.data (), firstPart.size () }, { secondPart.data (), secondPart.size () } }, endpoint);
		}

		assert (future.wait_for (std::chrono::seconds (2)) == std::future_status::ready);
		auto payload = future.get ();
		assert (std::string (payload.begin (), payload.end ()) == "SAM-DATAGRAM");

		// Several sends in a row must be serialized and all reach the peer.
		std::atomic<int> remaining { 8 };
		auto second = std::make_shared<std::promise<void>> ();
		auto secondFuture = second->get_future ();
		std::function<void ()> receiveNext = [&, second] ()
		{
			receiver.async_receive (boost::asio::buffer (buffer),
				[&, second] (const boost::system::error_code & ec, std::size_t len)
				{
					assert (!ec);
					assert (len > 0);
					if (remaining.fetch_sub (1) == 1)
						second->set_value ();
					else
						receiveNext ();
				});
		};
		receiveNext ();
		for (int i = 0; i < 8; i++)
		{
			std::string text = "queued-" + std::to_string (i);
			bridge.SendTo ({ { text.data (), text.size () } }, endpoint);
		}
		assert (secondFuture.wait_for (std::chrono::seconds (2)) == std::future_status::ready);
	}
	bridge.Stop ();

	return 0;
}
