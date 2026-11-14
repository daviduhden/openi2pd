/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * Exercises SSU2 socket admission during concurrent send and reopen.
 */
#include <array>
#include <barrier>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include "SSU2.h"

namespace i2p::transport
{
struct SSU2SocketTestAccess
{
	static void OpenDirect (SSU2Server & server)
	{
		server.OpenSocket ({ boost::asio::ip::address_v4::loopback (), 0 });
	}
	static void Close (SSU2Server & server) { server.CloseSocket (server.m_SocketV4); }
	static bool OpenProxy (SSU2Server & server)
	{
		boost::system::error_code error;
		return server.OpenProxySocket (error);
	}
	static bool Nonblocking (SSU2Server & server)
	{
		std::lock_guard lock (server.m_SocketMutex);
		return server.m_SocketV4.non_blocking ();
	}
	static void PublishRelay (SSU2Server & server, const boost::asio::ip::udp::endpoint & endpoint)
	{
		std::lock_guard lock (server.m_ProxyRelayEndpointMutex);
		server.m_ProxyRelayEndpoint = std::make_unique<boost::asio::ip::udp::endpoint> (endpoint);
	}
	static void ReceiveClosed (SSU2Server & server) { server.Receive (server.m_SocketV4); }
};
}

namespace
{
using i2p::transport::SSU2Server;
using i2p::transport::SSU2SocketTestAccess;

void TestReopenDuringSend (bool throughProxy)
{
	boost::asio::io_context service;
	boost::asio::ip::udp::socket receiver (
		service, boost::asio::ip::udp::endpoint (boost::asio::ip::address_v4::loopback (), 0));
	const auto destination = receiver.local_endpoint ();
	SSU2Server server;
	SSU2SocketTestAccess::OpenDirect (server);
	assert (SSU2SocketTestAccess::Nonblocking (server));
	assert (server.GetPort (true));
	if (throughProxy)
	{
		assert (server.SetProxy ("127.0.0.1", destination.port ()));
		SSU2SocketTestAccess::PublishRelay (server, destination);
	}

	constexpr size_t iterations = 128;
	std::barrier phase (2);
	std::thread reopen (
		[&]
		{
			for (size_t i = 0; i < iterations; ++i)
			{
				phase.arrive_and_wait ();
				SSU2SocketTestAccess::Close (server);
				assert (SSU2SocketTestAccess::OpenProxy (server));
				phase.arrive_and_wait ();
			}
		});
	const std::array<uint8_t, 16> header { };
	const std::array<uint8_t, 8> payload { };
	for (size_t i = 0; i < iterations; ++i)
	{
		phase.arrive_and_wait ();
		server.Send (header.data (), header.size (), payload.data (), payload.size (), destination);
		server.Send (header.data (), header.size (), header.data (), header.size (), payload.data (), payload.size (),
			destination);
		(void)server.GetPort (true);
		(void)server.IsSupported (destination.address ());
		phase.arrive_and_wait ();
	}
	reopen.join ();
	assert (SSU2SocketTestAccess::Nonblocking (server));
	SSU2SocketTestAccess::Close (server);
	assert (!server.IsSupported (destination.address ()));
	assert (!server.GetPort (true));
	// A stopped socket does not admit a new borrowing receive operation.
	SSU2SocketTestAccess::ReceiveClosed (server);
}
}

int main ()
{
	TestReopenDuringSend (false);
	TestReopenDuringSend (true);
}
