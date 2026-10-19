/*
 * Copyright (c) 2026, David Uhden Collado
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Local UNIX-socket regression test for the TUI transport.
 *
 * It exercises the real server and client framing code: partial request
 * lines, pipelined requests, the request-line bound, remote errors that keep
 * the connection usable, QUIT teardown and socket removal. It does not start
 * router subsystems, so only commands that do not read router state (PING,
 * QUIT and malformed/unknown requests) are used.
 */

#ifndef _WIN32

#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

#include "TuiProtocol.h"
#include "daemon/TuiServer.h"
#include "tui/TuiClient.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace
{
bool SendAll (int fd, std::string_view data)
{
	while (!data.empty ())
	{
		auto sent = ::send (fd, data.data (), data.size (), MSG_NOSIGNAL);
		if (sent < 0)
		{
			if (errno == EINTR) continue;
			return false;
		}
		if (!sent) return false;
		data.remove_prefix (static_cast<size_t> (sent));
	}
	return true;
}

bool ReceiveExact (int fd, char * data, size_t size)
{
	while (size)
	{
		auto received = ::recv (fd, data, size, 0);
		if (received < 0)
		{
			if (errno == EINTR) continue;
			return false;
		}
		if (!received) return false;
		data += received;
		size -= static_cast<size_t> (received);
	}
	return true;
}

bool ReadFrame (int fd, i2p::tui::ResponseHeader & header, std::string & payload)
{
	std::string line;
	line.reserve (128);
	while (line.size () < 128)
	{
		char ch = 0;
		auto received = ::recv (fd, &ch, 1, 0);
		if (received < 0)
		{
			if (errno == EINTR) continue;
			return false;
		}
		if (!received) return false;
		if (ch == '\n') break;
		line.push_back (ch);
	}
	if (line.empty () || line.back () == '\r') return false;

	auto parsed = i2p::tui::ParseResponseHeader (line);
	if (!parsed) return false;
	payload.assign (parsed->payloadSize, '\0');
	if (!payload.empty () && !ReceiveExact (fd, payload.data (), payload.size ())) return false;
	header = *parsed;
	return true;
}

int ConnectRaw (const std::string & path)
{
	int fd = ::socket (AF_UNIX, SOCK_STREAM, 0);
	assert (fd >= 0);

	timeval timeout { 3, 0 };
	setsockopt (fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof (timeout));
	setsockopt (fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof (timeout));

	sockaddr_un address { };
	address.sun_family = AF_UNIX;
	assert (path.size () < sizeof (address.sun_path));
	std::memcpy (address.sun_path, path.c_str (), path.size () + 1);
	if (::connect (fd, reinterpret_cast<sockaddr *> (&address), sizeof (address)) != 0)
	{
		::close (fd);
		return -1;
	}
	return fd;
}

void TestClientRoundTrip (const std::string & path)
{
	i2p::tui::Client client (path);
	auto connected = client.Connect ();
	assert (connected.has_value ());
	assert (client.IsConnected ());

	auto ping = client.Request ("PING");
	assert (ping.has_value ());
	assert (ping->starts_with ("pong="));

	// Read-only directory views must dispatch without requiring a running
	// router subsystem. The process-global tables are empty in this test.
	auto connections = client.Request ("CONNECTIONS 5");
	assert (connections.has_value ());
	assert (connections->find ("connections.count=") != std::string::npos);
	auto routers = client.Request ("NETDB-ROUTERS 5");
	assert (routers.has_value ());
	assert (routers->find ("netdb.routers.listed=") != std::string::npos);
	auto leaseSets = client.Request ("NETDB-LEASESETS 5");
	assert (leaseSets.has_value ());
	assert (leaseSets->find ("netdb.leasesets.listed=") != std::string::npos);

	// The only writable administrative command used here updates an atomic
	// logger level; it does not touch router state or the filesystem.
	auto admin = client.Request ("ADMIN LOGLEVEL info");
	assert (admin.has_value ());
	assert (admin->find ("result=ok") != std::string::npos);

	// A well-formed remote error keeps the connection usable.
	auto unknown = client.Request ("BOGUS");
	assert (!unknown.has_value ());
	assert (unknown.error ().kind == i2p::tui::RequestErrorKind::Remote);
	assert (client.IsConnected ());

	auto second = client.Request ("PING");
	assert (second.has_value ());
	assert (second->starts_with ("pong="));

	// The server replies before closing on QUIT.
	auto quit = client.Request ("QUIT");
	assert (quit.has_value ());
	assert (quit->empty ());
	client.Disconnect ();
}

void TestRawFraming (const std::string & path)
{
	int fd = ConnectRaw (path);
	assert (fd >= 0);

	// A request line may arrive in several writes.
	assert (SendAll (fd, "I2PDTUI/1 PI"));
	std::this_thread::sleep_for (std::chrono::milliseconds (20));
	assert (SendAll (fd, "NG\n"));

	i2p::tui::ResponseHeader header { };
	std::string payload;
	assert (ReadFrame (fd, header, payload));
	assert (header.status == i2p::tui::ResponseStatus::Ok);
	assert (payload.starts_with ("pong="));

	// Two complete requests in one write must produce two responses in
	// order, without losing the second one in the accumulator.
	assert (SendAll (fd, "I2PDTUI/1 PING\nI2PDTUI/1 PING\n"));
	assert (ReadFrame (fd, header, payload));
	assert (header.status == i2p::tui::ResponseStatus::Ok);
	assert (payload.starts_with ("pong="));
	assert (ReadFrame (fd, header, payload));
	assert (header.status == i2p::tui::ResponseStatus::Ok);
	assert (payload.starts_with ("pong="));

	assert (SendAll (fd, "I2PDTUI/1 QUIT\n"));
	assert (ReadFrame (fd, header, payload));
	assert (header.status == i2p::tui::ResponseStatus::Ok);
	assert (payload.empty ());
	char extra = 0;
	assert (::recv (fd, &extra, 1, 0) == 0);
	::close (fd);
}

void TestOverlongLine (const std::string & path)
{
	int fd = ConnectRaw (path);
	assert (fd >= 0);

	// One byte over the line limit must be rejected before dispatch and
	// must close the connection rather than leaving a desynchronised
	// accumulator.
	std::string overlong (i2p::tui::MAX_REQUEST_LINE + 2, 'A');
	assert (SendAll (fd, overlong));

	i2p::tui::ResponseHeader header { };
	std::string payload;
	assert (ReadFrame (fd, header, payload));
	assert (header.status == i2p::tui::ResponseStatus::Error);
	assert (header.errorCode == i2p::tui::ErrorCode::MalformedRequest);
	assert (!payload.empty ());

	char extra = 0;
	assert (::recv (fd, &extra, 1, 0) == 0);
	::close (fd);
}

void TestAbortedClient (const std::string & path)
{
	int fd = ConnectRaw (path);
	assert (fd >= 0);

	// Send a partial request, then abort with RST. The server may fail while
	// reading or while writing the error frame; that client-local failure
	// must not terminate the accept loop.
	assert (SendAll (fd, "I2PDTUI/1 PI"));
	struct linger abortive { };
	abortive.l_onoff = 1;
	abortive.l_linger = 0;
	setsockopt (fd, SOL_SOCKET, SO_LINGER, &abortive, sizeof (abortive));
	::close (fd);

	std::this_thread::sleep_for (std::chrono::milliseconds (50));

	// The next client must be served normally.
	i2p::tui::Client client (path);
	auto connected = client.Connect ();
	assert (connected.has_value ());
	auto ping = client.Request ("PING");
	assert (ping.has_value ());
	assert (ping->starts_with ("pong="));
	client.Disconnect ();

	// A complete request followed by an abortive close makes the response
	// write fail. The server must contain that failure per connection.
	fd = ConnectRaw (path);
	assert (fd >= 0);
	assert (SendAll (fd, "I2PDTUI/1 PING\n"));
	setsockopt (fd, SOL_SOCKET, SO_LINGER, &abortive, sizeof (abortive));
	::close (fd);
	std::this_thread::sleep_for (std::chrono::milliseconds (50));

	i2p::tui::Client second (path);
	assert (second.Connect ().has_value ());
	auto secondPing = second.Request ("PING");
	assert (secondPing.has_value ());
	second.Disconnect ();
}

void TestBoundedStop (const std::string & directory)
{
	const std::string path = directory + "/stop.sock";
	i2p::tui::Server server (path);
	assert (server.Start ());

	// Keep one connection open with an incomplete line: Stop() must not wait
	// for the old five-second receive timeout.
	int fd = ConnectRaw (path);
	assert (fd >= 0);
	assert (SendAll (fd, "I2PDTUI/1 PI"));
	std::this_thread::sleep_for (std::chrono::milliseconds (50));

	const auto started = std::chrono::steady_clock::now ();
	server.Stop ();
	const auto elapsed = std::chrono::steady_clock::now () - started;
	assert (elapsed < std::chrono::seconds (3));

	::close (fd);
	struct stat info { };
	assert (lstat (path.c_str (), &info) != 0 && errno == ENOENT);
}
}

int main ()
{
	char directory[] = "/tmp/openi2pd-tui-ipc-XXXXXX";
	assert (mkdtemp (directory) != nullptr);

	const std::string socketPath = std::string (directory) + "/tui.sock";
	{
		i2p::tui::Server server (socketPath);
		assert (server.Start ());

		struct stat info { };
		assert (lstat (socketPath.c_str (), &info) == 0);
		assert (S_ISSOCK (info.st_mode));
		assert ((info.st_mode & 0777) == 0600);

		TestClientRoundTrip (socketPath);
		TestRawFraming (socketPath);
		TestOverlongLine (socketPath);
		TestAbortedClient (socketPath);

		server.Stop ();
		assert (lstat (socketPath.c_str (), &info) != 0 && errno == ENOENT);
	}

	TestBoundedStop (directory);

	assert (rmdir (directory) == 0);
	return 0;
}

#endif // _WIN32
