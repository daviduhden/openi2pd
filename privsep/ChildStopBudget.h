/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 */

#ifndef I2PD_PRIVSEP_CHILD_STOP_BUDGET_H
#define I2PD_PRIVSEP_CHILD_STOP_BUDGET_H

#include <algorithm>
#include <chrono>

namespace i2p::privsep
{
/*
 * Absolute POSIX child-stop phase boundaries, measured once at entry.
 *
 * Reserve up to 500 ms each for TERM and final KILL reaping, with the
 * remainder available for advisory shutdown and graceful exit. Small
 * budgets partition proportionally. Nonpositive inputs retain the
 * historical default of two seconds. Scheduler and kernel stalls can
 * exceed a wall-time bound; no phase starts a new relative wait budget.
 */
struct ChildStopBudget
{
	using Clock = std::chrono::steady_clock;
	Clock::time_point send;
	Clock::time_point graceful;
	Clock::time_point terminate;
	Clock::time_point reap;

	ChildStopBudget (Clock::time_point started, int timeoutMs)
	{
		const auto total = std::chrono::milliseconds (
			timeoutMs > 0 ? timeoutMs : 2000);
		const auto reserve = std::min (std::chrono::milliseconds (500),
			total / 3);
		reap = started + total;
		terminate = reap - reserve;
		graceful = terminate - reserve;
		send = std::min (graceful,
			started + std::chrono::milliseconds (500));
	}
};
}

#endif
