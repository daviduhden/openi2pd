/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * Exercises actual SSU2 proxy callback admission without opening a proxy.
 */
#include <algorithm>
#include <array>
#include <cassert>
#include <functional>
#include <chrono>
#include <future>
#include <memory>
#include <thread>
#include <vector>
#include "SSU2.h"
#include "Log.h"
#include "Modern.h"
#include <string>
#include <string_view>
#include <ctime>

namespace i2p::transport
{
struct SSU2ProxyTestAccess
{
	using Attempt = SSU2Server::ProxyAttempt;
	static auto Publish (SSU2Server & server)
	{
		auto attempt = std::make_shared<Attempt> (server.m_ReceiveService.GetService ());
		std::lock_guard lock (server.m_ProxyAuthority->mutex);
		server.m_ProxyAuthority->current = attempt;
		return attempt;
	}
	static auto Prepare (SSU2Server & server, const std::shared_ptr<Attempt> & attempt, std::function<void ()> action,
		bool retry = false)
	{
		return server.MakeProxyHandler (
			attempt, [action = std::move (action)] (SSU2Server &, const auto &, const boost::system::error_code &)
			{ action (); }, retry);
	}
	static void Revoke (SSU2Server & server) { server.RevokeProxyAuthority (); }
	static bool AdmissionIsHeld (SSU2Server & server)
	{
		std::unique_lock lock (server.m_ProxyAuthority->mutex, std::try_to_lock);
		return !lock.owns_lock ();
	}
	static void CheckRetainedAttemptCap (SSU2Server & server)
	{
		std::vector<std::shared_ptr<Attempt>> retained;
		for (auto & slot : server.m_ProxyAttempts)
		{
			auto attempt = std::make_shared<Attempt> (server.m_ReceiveService.GetService ());
			server.RetireProxyAttempt (attempt);
			slot = attempt;
			retained.push_back (std::move (attempt));
		}
		assert (retained.size () == 64);
		assert (server.SetProxy ("127.0.0.1", 1));
		server.ConnectToProxy ();
		assert (!server.m_ProxyAuthority->current);
	}

	static bool Current (SSU2Server & server, const std::shared_ptr<Attempt> & attempt)
	{
		std::lock_guard lock (server.m_ProxyAuthority->mutex);
		return server.m_ProxyAuthority->current == attempt;
	}
	static void RejectMethod (SSU2Server & server, const std::shared_ptr<Attempt> & attempt)
	{
		server.ScheduleProxyHandshakeDeadline (attempt);
		attempt->handshake[0] = SOCKS5_VER;
		attempt->handshake[1] = 0xff;
		auto handler = server.MakeProxyHandler (attempt,
			[] (SSU2Server & owner, const auto & current, const boost::system::error_code & error)
			{ owner.HandleProxyMethodReply (current, error); });
		handler (boost::system::error_code { });
	}
	static void CheckTimerRetirement (SSU2Server & server)
	{
		for (bool handshakeComplete : { false, true })
		{
			auto attempt = Publish (server);
			auto & service = server.m_ReceiveService.GetService ();
			unsigned cancelled = 0;
			auto observe = [&cancelled] (const boost::system::error_code & error)
			{
				assert (error == boost::asio::error::operation_aborted);
				++cancelled;
			};
			attempt->deadline->expires_after (std::chrono::hours (1));
			attempt->retry->expires_after (std::chrono::hours (1));
			attempt->deadline->async_wait (observe);
			attempt->retry->async_wait (observe);
			if (handshakeComplete)
			{
				attempt->handshakeComplete = true;
				server.CancelProxyHandshakeDeadline (attempt);
				assert (!attempt->deadline && attempt->retry);
			}
			server.RetireProxyAttempt (attempt);
			assert (!attempt->socket && !attempt->deadline && !attempt->retry);
			// Observe actual cancelled waits after the production wrappers
			// have retired. No borrowing observer outlives this fixture.
			service.restart ();
			const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (5);
			while (cancelled != 2 && std::chrono::steady_clock::now () < deadline)
				if (!service.run_one_until (deadline)) break;
			assert (cancelled == 2);
		}
	}
	static void CheckFragmentedTransfer (SSU2Server & server)
	{
		auto attempt = Publish (server);
		auto & service = server.m_ReceiveService.GetService ();
		boost::asio::ip::tcp::acceptor listener (
			service, boost::asio::ip::tcp::endpoint (boost::asio::ip::address_v4::loopback (), 0));
		boost::asio::ip::tcp::socket peer (service);
		peer.connect (listener.local_endpoint ());
		listener.accept (*attempt->socket);
		unsigned completions = 0;
		{
			std::lock_guard lock (server.m_ProxyAuthority->mutex);
			server.TransferProxyBytes (attempt, 3, 0, false,
				[&] (SSU2Server &, const auto &, const boost::system::error_code & error, size_t bytes)
				{
					assert (!error && bytes == 3);
					++completions;
				});
		}
		std::array<uint8_t, 3> reply { 5, 0, 42 };
		boost::asio::write (peer, boost::asio::buffer (reply.data (), 1));
		// This fixture has exactly one published read. Observe its first
		// callback before supplying the remaining bytes, proving that the
		// production partial-read continuation actually runs.
		const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (5);
		service.restart ();
		assert (service.run_one_until (deadline) == 1);
		assert (!completions);
		boost::asio::write (peer, boost::asio::buffer (reply.data () + 1, 2));
		while (!completions && std::chrono::steady_clock::now () < deadline)
			if (!service.run_one_until (deadline)) break;
		assert (completions == 1);
		assert (std::equal (reply.begin (), reply.end (), attempt->handshake.begin ()));
	}
	static auto & Service (SSU2Server & server) { return server.m_ReceiveService.GetService (); }
};
}

using namespace i2p::transport;

void TestReplacementAndRetry ()
{
	SSU2Server server;
	unsigned calls = 0;
	auto old = SSU2ProxyTestAccess::Publish (server);
	auto oldCompletion = SSU2ProxyTestAccess::Prepare (server, old, [&] { ++calls; });
	auto current = SSU2ProxyTestAccess::Publish (server);
	assert (old->handshake.data () != current->handshake.data ());
	oldCompletion (boost::system::error_code { });
	oldCompletion (boost::asio::error::operation_aborted);
	assert (!calls && SSU2ProxyTestAccess::Current (server, current));
	auto completion = SSU2ProxyTestAccess::Prepare (server, current, [&] { ++calls; });
	auto retry = SSU2ProxyTestAccess::Prepare (server, current, [&] { ++calls; }, true);
	completion (boost::system::error_code { });
	assert (calls == 1);
	current->retrying = true;
	completion (boost::system::error_code { });
	completion (boost::asio::error::operation_aborted);
	assert (calls == 1);
	retry (boost::system::error_code { });
	assert (calls == 2);
	SSU2ProxyTestAccess::Revoke (server);
	retry (boost::system::error_code { });
	assert (calls == 2);
}

void TestTerminalMethodRetiresDeadline ()
{
	SSU2Server server;
	auto attempt = SSU2ProxyTestAccess::Publish (server);
	SSU2ProxyTestAccess::RejectMethod (server, attempt);
	assert (!SSU2ProxyTestAccess::Current (server, attempt));
	assert (!attempt->retrying && !attempt->socket && !attempt->deadline && !attempt->retry);
	auto & service = SSU2ProxyTestAccess::Service (server);
	service.poll ();
	assert (!SSU2ProxyTestAccess::Current (server, attempt));
}

void TestRevocationDrainsAdmission ()
{
	SSU2Server server;
	auto attempt = SSU2ProxyTestAccess::Publish (server);
	std::promise<void> admitted, release;
	auto released = release.get_future ().share ();
	auto completion = SSU2ProxyTestAccess::Prepare (server, attempt,
		[&]
		{
			admitted.set_value ();
			released.wait ();
		});
	std::thread callback ([&] { completion (boost::system::error_code { }); });
	admitted.get_future ().wait ();
	// The admitted callback demonstrably holds the revocation gate.
	// Thread launch alone cannot prove that Revoke has started waiting.
	assert (SSU2ProxyTestAccess::AdmissionIsHeld (server));
	auto stop = std::async (std::launch::async, [&] { SSU2ProxyTestAccess::Revoke (server); });
	release.set_value ();
	callback.join ();
	stop.get ();
	assert (!SSU2ProxyTestAccess::Current (server, attempt));
	unsigned staleCalls = 0;
	auto stale = SSU2ProxyTestAccess::Prepare (server, attempt, [&] { ++staleCalls; });
	stale (boost::system::error_code { });
	assert (!staleCalls);
}

void TestUndrainedCallbackAfterOwner ()
{
	unsigned calls = 0;
	std::function<void (const boost::system::error_code &)> completion;
	{
		auto server = std::make_unique<SSU2Server> ();
		auto attempt = SSU2ProxyTestAccess::Publish (*server);
		completion = SSU2ProxyTestAccess::Prepare (*server, attempt, [&] { ++calls; });
	}
	// The callback retains inert buffer state, not executor-borrowing
	// socket or timer wrappers. Owner destruction revoked its gate.
	completion (boost::system::error_code { });
	completion (boost::asio::error::operation_aborted);
	assert (!calls);
	completion = { };
}

void TestMalformedProxyDiagnostic ()
{
	std::string diagnostic;
	auto & logger = i2p::log::Logger ();
	logger.SetSynchronousSink (
		[&diagnostic] (LogLevel, std::time_t, std::string_view text) { diagnostic.append (text); });
	auto restore = i2p::modern::ScopeExit ([&logger] { logger.SetSynchronousSink ({ }); });
	SSU2Server server;
	const std::string operand = "opaque-user:opaque-password@invalid";
	assert (!server.SetProxy (operand, 1));
	assert (diagnostic.find ("Invalid proxy address") != std::string::npos);
	assert (diagnostic.find (operand) == std::string::npos);
	assert (diagnostic.find ("opaque-password") == std::string::npos);
}

int main ()
{
	{
		SSU2Server server;
		SSU2ProxyTestAccess::CheckTimerRetirement (server);
	}
	{
		SSU2Server server;
		SSU2ProxyTestAccess::CheckRetainedAttemptCap (server);
	}
	{
		SSU2Server server;
		SSU2ProxyTestAccess::CheckFragmentedTransfer (server);
	}
	TestReplacementAndRetry ();
	TestTerminalMethodRetiresDeadline ();
	TestRevocationDrainsAdmission ();
	TestUndrainedCallbackAfterOwner ();
	TestMalformedProxyDiagnostic ();
}
