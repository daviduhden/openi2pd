/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test RunnableService lifecycle.
 *
 * Exercises self-stop from a posted handler and a normal Start/Stop cycle
 * without a router, network socket or project daemon.
 */

#include <cassert>
#include <chrono>
#include <future>
#include <thread>

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
	// The self-stop detaches the run loop by design; give it a bounded chance
	// to observe the cleared running flag before the service is destroyed.
	std::this_thread::sleep_for (std::chrono::milliseconds (100));
	service.Stop ();
	assert (!service.Running ());
	return 0;
}
