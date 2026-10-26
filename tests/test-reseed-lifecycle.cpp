/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: reseed job lifecycle regression.
 *
 * The owned reseed job must be single-flight, non-blocking for the caller and
 * cancellable by NetDb::Stop() through its stop token, so an administrative
 * reseed can never freeze an admin/Android thread for minutes. Deterministic:
 * an injected fetcher stalls for at most 100 ms and no socket is opened.
 *
 */

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

#include <boost/program_options/variables_map.hpp>

#include "Config.h"
#include "NetDb.hpp"
#include "Reseed.h"
#include "util.h"

namespace
{
using namespace i2p;
using namespace i2p::data;

// Wait for a predicate with a bounded, CI-friendly budget instead of a fixed
// sleep. Returns false on timeout.
template <typename Predicate> bool WaitFor (Predicate predicate, std::chrono::milliseconds budget)
{
	const auto deadline = std::chrono::steady_clock::now () + budget;
	while (std::chrono::steady_clock::now () < deadline)
	{
		if (predicate ()) return true;
		std::this_thread::sleep_for (std::chrono::milliseconds (2));
	}
	return predicate ();
}

void SetConfigValue (const std::string & name, const bool value)
{
	i2p::config::m_Options[name] = boost::program_options::variable_value (value, false);
}
}

int main ()
{
	using namespace i2p;
	using namespace i2p::data;

	// A stalled fetch that only returns when the stop token is requested or the
	// short bounded wait elapsed. No long sleep, no socket, no live server.
	std::mutex mutex;
	std::condition_variable cv;
	std::atomic<bool> tokenObserved { false };
	std::atomic<int> fetchCount { 0 };

	ReseedTestHooks hooks;
	hooks.overrideSources = true;
	hooks.sources = { "https://reseed.invalid/" };
	hooks.nowMs = [] { return i2p::util::GetMillisecondsSinceEpoch (); };
	hooks.fetch = [&] (const std::string & url, const std::stop_token & token)
	{
		assert (url.find ("i2pseeds.su3") != std::string::npos);
		fetchCount.fetch_add (1, std::memory_order_relaxed);
		std::unique_lock<std::mutex> lock (mutex);
		cv.wait_for (lock, std::chrono::milliseconds (100), [&token] { return token.stop_requested (); });
		tokenObserved.store (token.stop_requested (), std::memory_order_relaxed);
		return std::string { };
	};

	NetDb database;
	database.SetReseedTestHooks (hooks);

	assert (!database.IsReseeding ());
	assert (database.GetReseedState ().phase == ReseedPhase::Idle);

	// The first request starts the owned job and returns immediately; a second
	// request while it runs is refused instead of queued.
	{
		const auto start = std::chrono::steady_clock::now ();
		assert (database.RequestReseed ());
		const auto elapsed = std::chrono::steady_clock::now () - start;
		assert (elapsed < std::chrono::seconds (1));
	}
	assert (database.IsReseeding ());
	assert (!database.RequestReseed ());
	{
		const auto state = database.GetReseedState ();
		assert (state.phase == ReseedPhase::Running);
		assert (state.enabled);
		assert (state.attempts == 1);
		assert (state.succeeded == 0);
		assert (state.lastError.empty ());
		assert (state.lastAttemptMs > 0);
	}

	// The state surface is readable while the job runs and never waits for it.
	{
		const auto start = std::chrono::steady_clock::now ();
		const auto state = database.GetReseedState ();
		assert (std::chrono::steady_clock::now () - start < std::chrono::seconds (1));
		assert (state.phase == ReseedPhase::Running);
	}

	// Let the stalled fetch actually start so cancellation is exercised on an
	// in-flight phase, not only before it.
	{
		const bool started = WaitFor ([&] { return fetchCount.load (std::memory_order_relaxed) > 0; },
			std::chrono::seconds (2));
		assert (started);
	}

	// Stop() must cancel and join the job within a bounded time, and the fetch
	// must have observed the stop token.
	{
		const auto start = std::chrono::steady_clock::now ();
		database.Stop ();
		const auto elapsed = std::chrono::steady_clock::now () - start;
		assert (elapsed < std::chrono::seconds (5));
	}
	assert (!database.IsReseeding ());
	assert (fetchCount.load (std::memory_order_relaxed) >= 1);
	assert (tokenObserved.load (std::memory_order_relaxed));
	{
		const auto state = database.GetReseedState ();
		assert (state.phase == ReseedPhase::Idle);
		assert (state.enabled);
		assert (state.attempts == 1);
		assert (state.lastError.empty ());
	}

	// Disabled reseed: the request is refused, nothing runs and the state says
	// so. `reseed.disable` is the operator switch.
	SetConfigValue ("reseed.disable", true);
	{
		const auto before = fetchCount.load (std::memory_order_relaxed);
		const auto start = std::chrono::steady_clock::now ();
		assert (!database.RequestReseed ());
		assert (std::chrono::steady_clock::now () - start < std::chrono::seconds (1));
		assert (!database.IsReseeding ());
		const auto state = database.GetReseedState ();
		assert (state.phase == ReseedPhase::Disabled);
		assert (!state.enabled);
		assert (state.attempts == 1); // a refused request is not an attempt
		assert (fetchCount.load (std::memory_order_relaxed) == before);
	}

	// Re-enabling is honoured again by the next request.
	SetConfigValue ("reseed.disable", false);
	{
		const auto before = fetchCount.load (std::memory_order_relaxed);
		assert (database.RequestReseed ());
		assert (database.IsReseeding ());
		assert (WaitFor ([&] { return fetchCount.load (std::memory_order_relaxed) > before; },
			std::chrono::seconds (2)));
		database.Stop ();
		assert (!database.IsReseeding ());
	}
	return 0;
}
