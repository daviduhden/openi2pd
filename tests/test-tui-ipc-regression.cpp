/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test tui ipc regression.
 *
 * Exercises the UNIX-socket TUI transport with the real server and
 * client framing: partial and pipelined requests, line limits, error
 * recovery, QUIT and bounded shutdown, without router subsystems.
 *
 */

#ifndef _WIN32

#include <cassert>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

#include "TuiProtocol.h"
#include "tui/PosixConnectRetry.h"
#include "daemon/TuiServer.h"
#include "daemon/TuiServerCommon.h"
#include "tui/TuiClient.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace
{
std::atomic<bool> g_BlockAdminExecutor { false };
std::atomic<bool> g_AdminExecutorStarted { false };
std::atomic<bool> g_ReleaseAdminExecutor { false };
std::atomic<unsigned> g_AdminExecutorCount { 0 };

void FakeAdminExecutor (const i2p::tui::AdminCommand & command, i2p::tui::AdminCommandResult & result)
{
	g_AdminExecutorStarted.store (true, std::memory_order_relaxed);
	while (g_BlockAdminExecutor.load (std::memory_order_relaxed)
		&& !g_ReleaseAdminExecutor.load (std::memory_order_relaxed))
		std::this_thread::sleep_for (std::chrono::milliseconds (1));

	result = i2p::tui::AdminCommandResult { };
	result.code = i2p::tui::AdminResultCode::Ok;
	result.value = command.value;
	result.enabled = command.enabled;
	std::snprintf (result.message, sizeof (result.message), "fake admin command executed");
	result.messageLength = std::strlen (result.message);
	g_AdminExecutorCount.fetch_add (1, std::memory_order_relaxed);
}

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

int ListenRaw (const std::string & path)
{
	int fd = ::socket (AF_UNIX, SOCK_STREAM, 0);
	assert (fd >= 0);
	sockaddr_un address { };
	address.sun_family = AF_UNIX;
	assert (path.size () < sizeof (address.sun_path));
	std::memcpy (address.sun_path, path.c_str (), path.size () + 1);
	assert (::bind (fd, reinterpret_cast<sockaddr *> (&address), sizeof (address)) == 0);
	assert (::listen (fd, 1) == 0);
	return fd;
}

void ServeReply (int listener, std::string wire)
{
	int peer = ::accept (listener, nullptr, nullptr);
	assert (peer >= 0);
	char ch = 0;
	for (;;)
	{
		const auto received = ::recv (peer, &ch, 1, 0);
		assert (received == 1);
		if (ch == '\n') break;
	}
	assert (SendAll (peer, wire));
	::close (peer);
}

void TestClientResponseHeaderBounds (const std::string & directory)
{
	const std::string prefix = std::string (i2p::tui::PROTOCOL_NAME) + "/1 OK 1";
	assert (prefix.size () < 128);
	std::string maxHeader = prefix;
	maxHeader.append (128 - maxHeader.size (), ' ');
	const auto parsed = i2p::tui::ParseResponseHeader (maxHeader);
	assert (parsed && parsed->payloadSize == 1);

	const auto exercise = [&directory] (const std::string & name, std::string wire, bool accepted)
	{
		const std::string path = directory + "/" + name;
		const int listener = ListenRaw (path);
		std::thread server (
			[listener, wire = std::move (wire)] () mutable { ServeReply (listener, std::move (wire)); });

		i2p::tui::Client client (path);
		assert (client.Connect ().has_value ());
		const auto result = client.Request ("PING");
		if (accepted)
		{
			assert (result && *result == "x");
			client.Disconnect ();
		}
		else
		{
			assert (!result);
			assert (result.error ().kind == i2p::tui::RequestErrorKind::Protocol);
			assert (!client.IsConnected ());
		}
		server.join ();
		::close (listener);
		assert (::unlink (path.c_str ()) == 0);
	};

	exercise ("max-header.sock", maxHeader + "\nx", true);
	std::string overlong (129, 'A');
	exercise ("overlong-header.sock", overlong + "\nx", false);
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
	auto services = client.Request ("SERVICES 5");
	assert (services.has_value ());
	assert (services->find ("service.count=") != std::string::npos);

	// The only writable administrative command used here updates an atomic
	// logger level; it does not touch router state or the filesystem.
	auto admin = client.Request ("ADMIN LOGLEVEL info");
	assert (admin.has_value ());
	assert (admin->find ("result=ok") != std::string::npos);
	assert (admin->find ("code=ok") != std::string::npos);

	// Every typed router-wide command travels through the fixed-size
	// daemon
	// queue. The test executor only records the variant; no daemon
	// subsystem
	// is linked or changed here.
	for (const auto * command :
		{ "ADMIN RESEED", "ADMIN SHUTDOWN GRACEFUL", "ADMIN SHUTDOWN IMMEDIATE", "ADMIN SHUTDOWN CANCEL",
			"ADMIN RELOAD TUNNELS", "ADMIN BANDWIDTH L", "ADMIN SHARE 50", "ADMIN TRANSIT ON" })
	{
		auto response = client.Request (command);
		assert (response.has_value ());
		assert (response->find ("result=ok") != std::string::npos);
		assert (response->find ("code=ok") != std::string::npos);
	}
	auto bandwidth = client.Request ("ADMIN BANDWIDTH P");
	assert (bandwidth.has_value ());
	assert (bandwidth->find ("value=80") != std::string::npos); // ASCII 'P'

	// A well-formed remote error keeps the connection usable.
	auto unknown = client.Request ("BOGUS");
	assert (!unknown.has_value ());
	assert (unknown.error ().kind == i2p::tui::RequestErrorKind::Remote);
	assert (unknown.error ().code == i2p::tui::ErrorCode::UnknownCommand);
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

	// Send a partial request, then abort with RST. The server may fail
	// while
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

void TestAdminQueueBounds ()
{
	g_AdminExecutorCount.store (0, std::memory_order_relaxed);
	// Running the queue without a daemon proves its fixed capacity and
	// bounded wait: a blocked executor occupies one slot, the remaining
	// slots accept commands that time out as Accepted, and the next submit
	// is rejected as QueueFull without waiting.
	assert (i2p::tui::StartAdminCommandQueue (&FakeAdminExecutor));
	g_BlockAdminExecutor.store (true, std::memory_order_relaxed);
	g_ReleaseAdminExecutor.store (false, std::memory_order_relaxed);
	g_AdminExecutorStarted.store (false, std::memory_order_relaxed);

	i2p::tui::AdminCommand first { i2p::tui::AdminCommandKind::Reseed, 0, false };
	auto firstResult = i2p::tui::SubmitAdminCommand (first, std::chrono::milliseconds (1500));
	assert (firstResult.code == i2p::tui::AdminResultCode::Accepted);

	for (int i = 0; i < 1000 && !g_AdminExecutorStarted.load (std::memory_order_relaxed); ++i)
		std::this_thread::sleep_for (std::chrono::milliseconds (1));
	assert (g_AdminExecutorStarted.load (std::memory_order_relaxed));

	i2p::tui::AdminCommand queued { i2p::tui::AdminCommandKind::Share, 50, false };
	for (size_t i = 1; i < i2p::tui::ADMIN_COMMAND_QUEUE_CAPACITY; ++i)
	{
		auto result = i2p::tui::SubmitAdminCommand (queued, std::chrono::milliseconds (20));
		assert (result.code == i2p::tui::AdminResultCode::Accepted);
	}
	auto full = i2p::tui::SubmitAdminCommand (queued, std::chrono::milliseconds (20));
	assert (full.code == i2p::tui::AdminResultCode::QueueFull);
	assert (std::string_view (full.message).find ("full") != std::string_view::npos);

	g_ReleaseAdminExecutor.store (true, std::memory_order_relaxed);
	// A timed-out IPC waiter must not discard the command itself. The
	// single
	// executor has to run every accepted slot exactly once before
	// shutdown.
	for (int i = 0;
		i < 2000 && g_AdminExecutorCount.load (std::memory_order_relaxed) < i2p::tui::ADMIN_COMMAND_QUEUE_CAPACITY; ++i)
		std::this_thread::sleep_for (std::chrono::milliseconds (1));
	assert (g_AdminExecutorCount.load (std::memory_order_relaxed) == i2p::tui::ADMIN_COMMAND_QUEUE_CAPACITY);
	i2p::tui::StopAdminCommandQueue ();
	g_BlockAdminExecutor.store (false, std::memory_order_relaxed);

	// Stopping makes further submissions fail closed instead of hanging.
	auto stopped = i2p::tui::SubmitAdminCommand (first, std::chrono::milliseconds (20));
	assert (stopped.code == i2p::tui::AdminResultCode::Unavailable);
}

void TestAdminQueueStopReleasesWaiter ()
{
	// A caller waiting for a queued command must be released by the queue
	// stopping, not by its own timeout: the daemon stops the queue during
	// shutdown and the local-IPC thread must not linger behind a command that
	// was already retired.
	g_AdminExecutorCount.store (0, std::memory_order_relaxed);
	g_AdminExecutorStarted.store (false, std::memory_order_relaxed);
	g_ReleaseAdminExecutor.store (false, std::memory_order_relaxed);
	g_BlockAdminExecutor.store (true, std::memory_order_relaxed);
	assert (i2p::tui::StartAdminCommandQueue (&FakeAdminExecutor));

	// Occupy the single executor so the next command has to stay queued.
	i2p::tui::AdminCommand blockingCommand { i2p::tui::AdminCommandKind::Reseed, 0, false };
	auto blockingResult = i2p::tui::SubmitAdminCommand (blockingCommand, std::chrono::milliseconds (100));
	assert (blockingResult.code == i2p::tui::AdminResultCode::Accepted);
	for (int i = 0; i < 2000 && !g_AdminExecutorStarted.load (std::memory_order_relaxed); ++i)
		std::this_thread::sleep_for (std::chrono::milliseconds (1));
	assert (g_AdminExecutorStarted.load (std::memory_order_relaxed));

	i2p::tui::AdminResultCode waiterCode = i2p::tui::AdminResultCode::Ok;
	std::chrono::steady_clock::duration waited { };
	std::thread waiter (
		[&waiterCode, &waited]
		{
			i2p::tui::AdminCommand queuedCommand { i2p::tui::AdminCommandKind::Share, 50, false };
			const auto started = std::chrono::steady_clock::now ();
			// Deliberately far longer than the stop needs: a returned result
			// must come from the stop, never from this timeout expiring.
			auto result = i2p::tui::SubmitAdminCommand (queuedCommand, std::chrono::seconds (30));
			waited = std::chrono::steady_clock::now () - started;
			waiterCode = result.code;
		});
	// Let the waiter observe the running executor and queue its command.
	std::this_thread::sleep_for (std::chrono::milliseconds (50));

	// The stop blocks until the blocked executor is released, so it runs on
	// its own thread while the waiter is observed.
	std::thread stopper ([] { i2p::tui::StopAdminCommandQueue (); });
	waiter.join ();
	assert (waiterCode == i2p::tui::AdminResultCode::Unavailable);
	// Released by the stop, not by the thirty-second timeout: the outcome above
	// already proves the command was retired rather than accepted, and this
	// bound keeps a "wait for the timeout" regression obvious.
	assert (waited < std::chrono::seconds (10));

	g_ReleaseAdminExecutor.store (true, std::memory_order_relaxed);
	stopper.join ();
	g_BlockAdminExecutor.store (false, std::memory_order_relaxed);
}

void TestBoundedStop (const std::string & directory)
{
	const std::string path = directory + "/stop.sock";
	i2p::tui::Server server (path);
	assert (server.Start ());

	// Keep one connection open with an incomplete line: Stop() must not
	// wait for the old five-second receive timeout.
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

void TestConnectIsBounded (const std::string & path)
{
	// Exercise timeout/error policy with a fake clock and connector. Kernel
	// backlog sizing is advisory, so real accept-queue saturation is not a
	// portable way to reach this branch.
	{
		using Clock = std::chrono::steady_clock;
		Clock::time_point now { };
		int attempts = 0;
		const int error = i2p::tui::detail::RetryUnixConnectUntil (
			now + std::chrono::milliseconds (100), [&] { return now; },
			[&]
			{
				++attempts;
				errno = EAGAIN;
				return -1;
			},
			[&] (auto interval) { now += interval; }, std::chrono::milliseconds (20));
		assert (error == ETIMEDOUT);
		assert (attempts == 6);
	}
	{
		using Clock = std::chrono::steady_clock;
		Clock::time_point now { };
		int attempts = 0;
		const int error = i2p::tui::detail::RetryUnixConnectUntil (
			now + std::chrono::seconds (1), [&] { return now; },
			[&]
			{
				if (++attempts < 3)
				{
					errno = EINTR;
					return -1;
				}
				return 0;
			},
			[&] (auto interval) { now += interval; }, std::chrono::milliseconds (20));
		assert (error == 0);
		assert (attempts == 3);
		assert (now == Clock::time_point { });
	}
	{
		using Clock = std::chrono::steady_clock;
		Clock::time_point now { };
		const int error = i2p::tui::detail::RetryUnixConnectUntil (
			now + std::chrono::seconds (1), [&] { return now; },
			[]
			{
				errno = ENOENT;
				return -1;
			},
			[&] (auto interval) { now += interval; }, std::chrono::milliseconds (20));
		assert (error == ENOENT);
		assert (now == Clock::time_point { });
	}

	i2p::tui::Client served (path);
	assert (served.Connect ().has_value ());
	auto pong = served.Request ("PING");
	assert (pong.has_value ());
	assert (pong->starts_with ("pong="));

	// Release the one real client and verify a fresh connection still works.
	served.Disconnect ();

	i2p::tui::Client recovered (path);
	assert (recovered.Connect ().has_value ());
	auto ping = recovered.Request ("PING");
	assert (ping.has_value ());
	assert (ping->starts_with ("pong="));
	recovered.Disconnect ();
}

void TestIdempotentLifecycle (const std::string & directory)
{
	// Start()/Stop() are single-owner lifecycle calls: a repeated Start() must
	// not bind a second listener or replace the worker, a repeated Stop() must
	// stay harmless, and a restart must rebind and serve the same path again.
	const std::string path = directory + "/lifecycle.sock";
	i2p::tui::Server server (path);
	assert (server.Start ());
	assert (server.Start ());

	struct stat info { };
	assert (lstat (path.c_str (), &info) == 0);
	assert (S_ISSOCK (info.st_mode));

	server.Stop ();
	server.Stop ();
	assert (lstat (path.c_str (), &info) != 0 && errno == ENOENT);

	assert (server.Start ());
	i2p::tui::Client client (path);
	assert (client.Connect ().has_value ());
	auto pong = client.Request ("PING");
	assert (pong.has_value ());
	assert (pong->starts_with ("pong="));
	client.Disconnect ();

	server.Stop ();
	assert (lstat (path.c_str (), &info) != 0 && errno == ENOENT);
}
}

int main ()
{
	char directory[] = "/tmp/openi2pd-tui-ipc-XXXXXX";
	assert (mkdtemp (directory) != nullptr);

	TestAdminQueueBounds ();
	TestAdminQueueStopReleasesWaiter ();
	assert (i2p::tui::StartAdminCommandQueue (&FakeAdminExecutor));

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
		TestClientResponseHeaderBounds (directory);
		TestAbortedClient (socketPath);
		TestConnectIsBounded (socketPath);

		server.Stop ();
		assert (lstat (socketPath.c_str (), &info) != 0 && errno == ENOENT);
	}

	i2p::tui::StopAdminCommandQueue ();
	TestBoundedStop (directory);
	TestIdempotentLifecycle (directory);

	assert (rmdir (directory) == 0);
	return 0;
}

#endif // _WIN32
