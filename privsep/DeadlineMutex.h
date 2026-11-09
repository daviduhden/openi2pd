/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 */

#ifndef I2PD_PRIVSEP_DEADLINE_MUTEX_H
#define I2PD_PRIVSEP_DEADLINE_MUTEX_H

#include <chrono>
#include <condition_variable>
#include <mutex>

namespace i2p::privsep
{
/**
 * Nonrecursive transaction admission with a steady-clock deadline.
 *
 * Implements BasicLockable and the steady try_lock_until operation used
 * by unique_lock. The caller owns admission until unlock; destruction
 * requires no owners or waiters. As with a standard mutex, recursive
 * locking and unlocking without ownership violate the contract.
 *
 * The private mutex protects only admission state, never the caller's
 * transaction or destructors. Condition-variable waits release it while
 * sleeping. This avoids timed_mutex clock-lock feature dependencies in
 * toolchains whose pthread and C++ library capabilities differ.
 */
class DeadlineMutex
{
public:
	void lock ()
	{
		std::unique_lock lock (m_StateMutex);
		m_Changed.wait (lock, [this] { return !m_Held; });
		m_Held = true;
	}

	bool try_lock ()
	{
		std::lock_guard lock (m_StateMutex);
		if (m_Held) return false;
		m_Held = true;
		return true;
	}

	bool try_lock_until (std::chrono::steady_clock::time_point deadline)
	{
		std::unique_lock lock (m_StateMutex);
		if (!m_Changed.wait_until (lock, deadline,
			[this] { return !m_Held; })) return false;
		m_Held = true;
		return true;
	}

	void unlock ()
	{
		{
			std::lock_guard lock (m_StateMutex);
			m_Held = false;
		}
		m_Changed.notify_one ();
	}

private:
	std::mutex m_StateMutex;
	std::condition_variable m_Changed;
	bool m_Held = false;
};
}

#endif
