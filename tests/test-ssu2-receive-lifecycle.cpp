/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * Exercises SSU2 receive-buffer ownership when callbacks are abandoned.
 */
#include <cassert>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include "SSU2.h"

namespace i2p::transport
{
struct SSU2ReceiveTestAccess
{
	using Handler = SSU2Server::ReceiveHandler;
	static Handler Prepare (SSU2Server & server) { return server.MakeReceiveHandler (server.m_SocketV4); }
	static auto AcquirePacket (SSU2Server & server)
	{
		return typename Handler::PacketOwner (server.m_PacketsPool.AcquireMt ());
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
}
