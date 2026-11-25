/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Win32 graceful shutdown timing.
 *
 */

#ifndef I2PD_WIN32_GRACEFUL_SHUTDOWN_H
#define I2PD_WIN32_GRACEFUL_SHUTDOWN_H

#include <cstdint>

namespace i2p::win32
{
inline constexpr uint32_t GRACEFUL_SHUTDOWN_TIMEOUT_MS = 10U * 60U * 1000U;

// Both arguments are values from the GetTickCount clock. Unsigned
// subtraction crosses one wrap. For this short deadline, a difference
// larger than the configured interval means the deadline has passed.
constexpr uint32_t GetGracefulShutdownRemainingMilliseconds (uint32_t deadline, uint32_t now) noexcept
{
	const auto remaining = deadline - now;
	return remaining <= GRACEFUL_SHUTDOWN_TIMEOUT_MS ? remaining : 0;
}
}

#endif // I2PD_WIN32_GRACEFUL_SHUTDOWN_H
