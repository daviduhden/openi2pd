/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 */

#include <cassert>
#include <chrono>
#include <limits>
#include <initializer_list>

#include "privsep/ChildStopBudget.h"

int main ()
{
	using namespace std::chrono;
	using i2p::privsep::ChildStopBudget;
	const auto started = ChildStopBudget::Clock::time_point { };
	for (const int timeout : { -1, 0, 1, 2, 3, 100, 2000,
		std::numeric_limits<int>::max () })
	{
		const ChildStopBudget budget (started, timeout);
		assert (started <= budget.send);
		assert (budget.send <= budget.graceful);
		assert (budget.graceful <= budget.terminate);
		assert (budget.terminate <= budget.reap);
		assert (budget.reap - started == milliseconds (
			timeout > 0 ? timeout : 2000));
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
