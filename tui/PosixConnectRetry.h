/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: bounded POSIX local-socket connect retry policy.
 *
 */

#ifndef I2PD_TUI_POSIX_CONNECT_RETRY_H
#define I2PD_TUI_POSIX_CONNECT_RETRY_H

#include <cerrno>
#include <chrono>

namespace i2p::tui::detail
{
	template <typename TimePoint, typename Now, typename Connect, typename Sleep>
	int RetryUnixConnectUntil (
		TimePoint deadline, Now && now, Connect && connect, Sleep && sleep,
		std::chrono::milliseconds retryInterval)
	{
		for (;;)
		{
			if (connect () == 0) return 0;
			const int error = errno;
			if (error == EINTR) continue;
			// EAGAIN and EWOULDBLOCK are the same value on the supported
			// POSIX targets; do not repeat the comparison.
			if (error != EAGAIN && error != EINPROGRESS && error != EALREADY) return error;
			if (now () >= deadline) return ETIMEDOUT;
			sleep (retryInterval);
		}
	}
}

#endif // I2PD_TUI_POSIX_CONNECT_RETRY_H
