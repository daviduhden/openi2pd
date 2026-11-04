/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test RunnableService lifecycle.
 *
 * Exercises self-stop from a posted handler and a normal Start/Stop
 * cycle
 * without a router, network socket or project daemon.
 */

#include <cassert>
#include <chrono>
#include <future>
#include <atomic>
#include <stdexcept>

#include "util.h"

namespace
{
class TestService final : public i2p::util::RunnableService
{
public:
	TestService ()
		: RunnableService ("test-service")
	{
	}
	~TestService () override { StopIOService (); }
	void Start () { StartIOService (); }
	void Stop () { StopIOService (); }
	void Drain () { StopIOService (false); }
	auto & Service () { return GetIOService (); }
	bool Running () const { return IsRunning (); }
};
}

int main ()
{
	TestService service;
	service.Start ();
	assert (service.Running ());

	std::promise<void> stopped;
	auto future = stopped.get_future ();
	boost::asio::post (service.Service (),
		[&service, &stopped] ()
		{
			service.Stop (); // must not self-join or throw
			stopped.set_value ();
		});
	assert (future.wait_for (std::chrono::seconds (5)) == std::future_status::ready);
	// External stop joins the retained thread; timing cannot replace it.
	service.Stop ();
	assert (!service.Running ());

	// Run out of ready work between two operations. The first barrier
	// proves the run loop started before the second operation is posted.
	for (int cycle = 0; cycle < 3; ++cycle)
	{
		service.Start ();
		std::promise<void> first, second;
		auto firstDone = first.get_future ();
		auto secondDone = second.get_future ();
		boost::asio::post (service.Service (), [&first] { first.set_value (); });
		assert (firstDone.wait_for (std::chrono::seconds (5)) == std::future_status::ready);
		boost::asio::post (service.Service (), [&second] { second.set_value (); });
		assert (secondDone.wait_for (std::chrono::seconds (5)) == std::future_status::ready);
		service.Stop ();
	}

	// Drain must execute admitted work even if it wins thread startup,
	// and one throwing handler must not discard the following work.
	std::atomic<int> completed { 0 };
	boost::asio::post (service.Service (), [] { throw std::runtime_error ("fixture"); });
	for (int i = 0; i < 32; ++i)
		boost::asio::post (service.Service (), [&completed] { ++completed; });
	service.Start ();
	service.Drain ();
	assert (completed == 32);
	assert (!service.Running ());
	return 0;
}
