/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 */

#include <cassert>
#include <array>
#include <optional>
#include <utility>
#include <chrono>
#include <limits>
#include <initializer_list>

#include "privsep/ChildStopBudget.h"
#ifndef _WIN32
#include "privsep/Worker.h"
#endif

#ifndef _WIN32
void TestDeferredOwnership ()
{
	using Registry = i2p::privsep::detail::ChildRetirementRegistry<2>;
	Registry registry;
	auto first = registry.Reserve ();
	auto second = registry.Reserve ();
	assert (first && second && *first != *second);
	assert (!registry.Reserve ());
	registry.Retire (*first, 101, 201);
	unsigned probes = 0, closed = 0;
	bool exited = false;
	auto probe = [&] (pid_t pid, int fd) noexcept
	{
		assert (pid == 101 && fd == 201);
		++probes;
		return exited;
	};
	auto close = [&] (int fd) noexcept
	{
		assert (fd == 201);
		++closed;
	};
	assert (registry.Drain (probe, close) == 0);
	assert (probes == 1 && closed == 0 && !registry.Reserve ());
	exited = true;
	assert (registry.Drain (probe, close) == 1);
	assert (probes == 2 && closed == 1);
	assert (registry.Drain (probe, close) == 0 && closed == 1);
	auto replacement = registry.Reserve ();
	assert (replacement && *replacement == *first);
	registry.Release (*replacement);
	registry.Release (*second);
	assert (registry.Reserve ());
}

void TestExclusiveRetirementClaim ()
{
	i2p::privsep::detail::ChildRetirementRegistry<1> registry;
	auto slot = registry.Reserve ();
	assert (slot);
	registry.Retire (*slot, 102, -1);
	unsigned probes = 0;
	auto close = [] (int) noexcept { assert (false); };
	auto probe = [&] (pid_t pid, int fd) noexcept
	{
		assert (pid == 102 && fd == -1);
		++probes;
		// Another drain cannot consume the metadata already claimed by
		// this probe, even when it runs before the first one publishes.
		assert (registry.Drain (
					[] (pid_t, int) noexcept
					{
						assert (false);
						return true;
					},
					close)
			== 0);
		assert (!registry.Reserve ());
		return true;
	};
	assert (registry.Drain (probe, close) == 1 && probes == 1);
	assert (registry.Drain (probe, close) == 0 && probes == 1);
	assert (registry.Reserve ());
}

void TestProductionAdmission ()
{
	using i2p::privsep::ChildProcess;
	std::array<std::optional<ChildProcess::Admission>, ChildProcess::MAX_OWNED_CHILDREN> admissions;
	for (auto & admission : admissions)
	{
		admission = ChildProcess::Reserve ();
		assert (admission);
	}
	assert (!ChildProcess::Reserve ());
	{
		auto moved = std::move (*admissions[0]);
		admissions[0].reset ();
		assert (!ChildProcess::Reserve ());
		// A detached IPC owner has no waitpid authority or reserved slot.
		auto detached = ChildProcess::AdoptChannel ({ });
		assert (detached.Pid () == -1);
		assert (!ChildProcess::Reserve ());
	}
	auto replacement = ChildProcess::Reserve ();
	assert (replacement);
	assert (!ChildProcess::Reserve ());
	replacement.reset ();
	for (auto & admission : admissions) admission.reset ();
	assert (i2p::privsep::DrainRetiredChildren () == 0);
}

void TestDisownRetainsReapAuthority ()
{
	using namespace std::chrono;
	using namespace i2p::privsep;
	auto admission = ChildProcess::Reserve ();
	assert (admission);
	const pid_t pid = ::fork ();
	assert (pid >= 0);
	if (pid == 0) ::_exit (0);
	auto child = ChildProcess::Adopt (std::move (*admission), pid, { });
	child.Disown ();
	assert (child.Pid () == -1);
	// A normal child exits immediately; retain the standard stop budget
	// for scheduler delay without introducing a blocking wait or signal.
	const ChildStopBudget budget (steady_clock::now (), 2000);
	size_t reaped = 0;
	while (!reaped && steady_clock::now () < budget.reap)
	{
		reaped += DrainRetiredChildren ();
		if (!reaped) (void)::poll (nullptr, 0, 1);
	}
	assert (reaped == 1);
	int status = 0;
	assert (::waitpid (pid, &status, WNOHANG) == -1 && errno == ECHILD);
	assert (DrainRetiredChildren () == 0);
}

#endif

int main ()
{
#ifndef _WIN32
	TestDeferredOwnership ();
	TestExclusiveRetirementClaim ();
	TestProductionAdmission ();
	TestDisownRetainsReapAuthority ();
#endif
	using namespace std::chrono;
	using i2p::privsep::ChildStopBudget;
	const auto started = ChildStopBudget::Clock::time_point { };
	for (const int timeout : { -1, 0, 1, 2, 3, 100, 2000, std::numeric_limits<int>::max () })
	{
		const ChildStopBudget budget (started, timeout);
		assert (started <= budget.send);
		assert (budget.send <= budget.graceful);
		assert (budget.graceful <= budget.terminate);
		assert (budget.terminate <= budget.reap);
		assert (budget.reap - started == milliseconds (timeout > 0 ? timeout : 2000));
		assert (budget.send - started <= milliseconds (500));
		assert (budget.reap - budget.terminate <= milliseconds (500));
		assert (budget.terminate - budget.graceful <= milliseconds (500));
	}
	const ChildStopBudget normal (started, 2000);
	assert (normal.send == started + milliseconds (500));
	assert (normal.graceful == started + milliseconds (1000));
	assert (normal.terminate == started + milliseconds (1500));
	assert (normal.reap == started + milliseconds (2000));
	// Later phases use stored boundaries even after earlier work stalls.
	const auto stalled = started + seconds (3);
	assert (normal.graceful < stalled && normal.reap < stalled);
}
