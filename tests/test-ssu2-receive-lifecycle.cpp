/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * Exercises SSU2 receive-buffer ownership when callbacks are abandoned.
 */
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <exception>
#include <new>
#include <vector>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include "SSU2.h"
#include "Modern.h"

namespace i2p::transport
{
struct SSU2ReceiveTestAccess
{
	struct AllocationFailure final : std::bad_alloc
	{
		std::shared_ptr<const unsigned> token;
		explicit AllocationFailure (std::shared_ptr<const unsigned> value)
			: token (std::move (value))
		{
		}
	};
	using Handler = SSU2Server::ReceiveHandler;
	using Batch = SSU2Server::ReceiveBatch;
	using Checkpoint = SSU2Server::ReceiveCheckpoint;
	inline static Checkpoint failurePoint = Checkpoint::BatchNode;
	inline static std::shared_ptr<const unsigned> failure;
	static void FailAt (Checkpoint point)
	{
		if (point == failurePoint) throw AllocationFailure (failure);
	}
	template <typename Action> static bool FailBefore (SSU2Server & server, Checkpoint point, Action && action)
	{
		failurePoint = point;
		failure = std::make_shared<const unsigned> (0);
		server.m_ReceiveCheckpointForTesting = &FailAt;
		auto clear = i2p::modern::ScopeExit (
			[&server]
			{
				server.m_ReceiveCheckpointForTesting = nullptr;
				failure = nullptr;
			});
		try
		{
			action ();
		}
		catch (const AllocationFailure & injected)
		{
			// Exception copies retain this payload, including on MSVC.
			return injected.token == failure;
		}
		catch (...)
		{
			return false;
		}
		return false;
	}
	static void Append (SSU2Server & server, Batch & batch, Handler::PacketOwner & packet)
	{
		server.AppendReceivedPacket (batch, packet);
	}
	static void Enqueue (SSU2Server & server, Batch & batch) { server.InsertToReceivedPacketsQueue (batch); }
	static std::vector<size_t> QueuedMarkers (SSU2Server & server)
	{
		std::lock_guard lock (server.m_ReceivedPacketsQueueMutex);
		std::vector<size_t> lengths;
		for (auto * packet : server.m_ReceivedPacketsQueue) lengths.push_back (packet->buf[0]);
		return lengths;
	}
	static void DrainWithoutExecution (SSU2Server & server)
	{
		Batch owned;
		std::lock_guard lock (server.m_ReceivedPacketsQueueMutex);
		owned.packets.splice (owned.packets.end (), server.m_ReceivedPacketsQueue);
	}

	static Handler Prepare (SSU2Server & server) { return server.MakeReceiveHandler (server.m_SocketV4); }
	static auto AcquirePacket (SSU2Server & server)
	{
		auto packet = typename Handler::PacketOwner (server.m_PacketsPool.AcquireMt ());
		packet->len = SSU2_MIN_RECEIVED_PACKET_SIZE;
		std::fill_n (packet->buf, packet->len, 0);
		return packet;
	}
	static void PublishPendingRead (SSU2Server & server)
	{
		server.OpenSocket ({ boost::asio::ip::address_v4::loopback (), 0 });
		server.Receive (server.m_SocketV4);
	}
};
}

namespace
{
using i2p::transport::SSU2ReceiveTestAccess;
using i2p::transport::SSU2Server;
using Handler = SSU2ReceiveTestAccess::Handler;
using PacketOwner = Handler::PacketOwner;
static_assert (!std::is_copy_constructible_v<Handler>);
static_assert (std::is_nothrow_move_constructible_v<Handler>);
static_assert (std::is_same_v<PacketOwner::deleter_type, std::default_delete<PacketOwner::element_type>>);

void TestAbandonedHandlerOutlivesOwner ()
{
	std::optional<Handler> retained;
	{
		SSU2Server server;
		auto handler = SSU2ReceiveTestAccess::Prepare (server);
		auto * packet = handler.packet.get ();
		assert (packet);
		retained.emplace (std::move (handler));
		assert (!handler.packet && retained->packet.get () == packet);
	}
	// Destruction must not dereference the now-destroyed server or pool.
	// The default-deleter type contract excludes borrowing pool deleters;
	// ASan/LSan must also observe this retained storage being released.
	assert (retained->packet);
	retained.reset ();
}

void TestCompletedHandlerReturnsPacket ()
{
	SSU2Server server;
	auto handler = SSU2ReceiveTestAccess::Prepare (server);
	auto * packet = handler.packet.get ();
	handler (boost::asio::error::operation_aborted, 0);
	assert (!handler.packet);
	auto reused = SSU2ReceiveTestAccess::AcquirePacket (server);
	assert (reused.get () == packet);
	// A consumed handler owns no packet and cannot release it twice.
	handler (boost::asio::error::operation_aborted, 0);
}

void TestCompletionPublicationFailures ()
{
	{
		SSU2Server server;
		// Neither unrelated failures nor an unarmed path prove injection.
		assert (!SSU2ReceiveTestAccess::FailBefore (
			server, SSU2ReceiveTestAccess::Checkpoint::BatchNode, [] { throw std::bad_alloc (); }));
		assert (!SSU2ReceiveTestAccess::FailBefore (server, SSU2ReceiveTestAccess::Checkpoint::BatchNode, [] { }));
		assert (!SSU2ReceiveTestAccess::FailBefore (server, SSU2ReceiveTestAccess::Checkpoint::BatchNode,
			[] { throw SSU2ReceiveTestAccess::AllocationFailure (std::make_shared<const unsigned> (0)); }));
	}
	for (auto point : { SSU2ReceiveTestAccess::Checkpoint::BatchNode, SSU2ReceiveTestAccess::Checkpoint::QueueWake })
	{
		SSU2Server server;
		auto handler = SSU2ReceiveTestAccess::Prepare (server);
		assert (SSU2ReceiveTestAccess::FailBefore (server, point,
			[&] { handler (boost::system::error_code { }, i2p::transport::SSU2_MIN_RECEIVED_PACKET_SIZE); }));
		assert (!handler.packet);
		assert (SSU2ReceiveTestAccess::QueuedMarkers (server).empty ());
	}
}

void TestBatchNodeFailurePreservesOwners ()
{
	SSU2Server server;
	SSU2ReceiveTestAccess::Batch batch;
	auto first = SSU2ReceiveTestAccess::AcquirePacket (server);
	auto second = SSU2ReceiveTestAccess::AcquirePacket (server);
	assert (SSU2ReceiveTestAccess::FailBefore (server, SSU2ReceiveTestAccess::Checkpoint::BatchNode,
		[&] { SSU2ReceiveTestAccess::Append (server, batch, first); }));
	assert (first && batch.packets.empty ());
	SSU2ReceiveTestAccess::Append (server, batch, first);
	assert (!first && batch.packets.size () == 1);
	assert (SSU2ReceiveTestAccess::FailBefore (server, SSU2ReceiveTestAccess::Checkpoint::BatchNode,
		[&] { SSU2ReceiveTestAccess::Append (server, batch, second); }));
	assert (second && batch.packets.size () == 1);
	// Destruction owns both allocations even after a later node failed.
}

void TestBatchWakeFailureAndRetry ()
{
	SSU2Server server;
	SSU2ReceiveTestAccess::Batch batch;
	for (uint8_t marker : { uint8_t { 11 }, uint8_t { 12 } })
	{
		auto packet = SSU2ReceiveTestAccess::AcquirePacket (server);
		packet->buf[0] = marker;
		SSU2ReceiveTestAccess::Append (server, batch, packet);
	}
	assert (SSU2ReceiveTestAccess::FailBefore (
		server, SSU2ReceiveTestAccess::Checkpoint::QueueWake, [&] { SSU2ReceiveTestAccess::Enqueue (server, batch); }));
	assert (batch.packets.size () == 2);
	assert (SSU2ReceiveTestAccess::QueuedMarkers (server).empty ());
	// Retrying the same owned batch can schedule its wake and publish FIFO.
	SSU2ReceiveTestAccess::Enqueue (server, batch);
	assert (batch.packets.empty ());
	assert ((SSU2ReceiveTestAccess::QueuedMarkers (server) == std::vector<size_t> { 11, 12 }));
	SSU2ReceiveTestAccess::DrainWithoutExecution (server);
}

void TestBatchAdmissionExactCapacity ()
{
	SSU2Server server;
	SSU2ReceiveTestAccess::Batch initial;
	for (size_t i = 0; i + 1 < i2p::transport::SSU2_MAX_RECEIVED_QUEUE_SIZE; ++i)
	{
		auto packet = SSU2ReceiveTestAccess::AcquirePacket (server);
		packet->buf[0] = 0;
		SSU2ReceiveTestAccess::Append (server, initial, packet);
	}
	SSU2ReceiveTestAccess::Enqueue (server, initial);
	assert (initial.packets.empty ());
	SSU2ReceiveTestAccess::Batch incoming;
	for (size_t i = 0; i < 3; ++i)
	{
		auto packet = SSU2ReceiveTestAccess::AcquirePacket (server);
		packet->buf[0] = static_cast<uint8_t> (10 + i);
		SSU2ReceiveTestAccess::Append (server, incoming, packet);
	}
	SSU2ReceiveTestAccess::Enqueue (server, incoming);
	auto queued = SSU2ReceiveTestAccess::QueuedMarkers (server);
	assert (queued.size () == i2p::transport::SSU2_MAX_RECEIVED_QUEUE_SIZE);
	assert (queued.front () == 0 && queued.back () == 10);
	assert (incoming.packets.size () == 2 && incoming.packets.front ()->buf[0] == 11);
	// A full queue leaves the remaining batch owned, without another wake.
	assert (!SSU2ReceiveTestAccess::FailBefore (server, SSU2ReceiveTestAccess::Checkpoint::QueueWake,
		[&] { SSU2ReceiveTestAccess::Enqueue (server, incoming); }));
	assert (incoming.packets.size () == 2);
	SSU2ReceiveTestAccess::DrainWithoutExecution (server);
}

void TestPendingReadWithoutExecutorRestart ()
{
	// No service thread or event loop runs. Closing the socket during
	// destruction leaves Asio to destroy its undispatched owning handler.
	// The canceled receive must not require a later run() to free storage.
	auto server = std::make_unique<SSU2Server> ();
	SSU2ReceiveTestAccess::PublishPendingRead (*server);
	server.reset ();
}
}

int main ()
{
	TestAbandonedHandlerOutlivesOwner ();
	TestCompletedHandlerReturnsPacket ();
	TestPendingReadWithoutExecutorRestart ();
	TestCompletionPublicationFailures ();
	TestBatchNodeFailurePreservesOwners ();
	TestBatchWakeFailureAndRetry ();
	TestBatchAdmissionExactCapacity ();
}
