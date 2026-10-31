/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Win32 shutdown deadline arithmetic.
 *
 */

#include <cassert>
#include <cstdint>
#include <limits>

#include "Win32/GracefulShutdown.h"

int main ()
{
	using i2p::win32::GetGracefulShutdownRemainingMilliseconds;
	using i2p::win32::GRACEFUL_SHUTDOWN_TIMEOUT_MS;

	constexpr uint32_t now = 1000U;
	assert (GetGracefulShutdownRemainingMilliseconds (now + GRACEFUL_SHUTDOWN_TIMEOUT_MS, now)
		== GRACEFUL_SHUTDOWN_TIMEOUT_MS);
	assert (GetGracefulShutdownRemainingMilliseconds (now - 1U, now) == 0U);
	assert (GetGracefulShutdownRemainingMilliseconds (now + GRACEFUL_SHUTDOWN_TIMEOUT_MS + 1U, now) == 0U);

	constexpr uint32_t wrapNow = std::numeric_limits<uint32_t>::max () - 4000U;
	constexpr uint32_t wrapDeadline = wrapNow + 8000U;
	assert (GetGracefulShutdownRemainingMilliseconds (wrapDeadline, wrapNow) == 8000U);
	assert (GetGracefulShutdownRemainingMilliseconds (wrapDeadline, wrapDeadline + 1U) == 0U);
	return 0;
}
