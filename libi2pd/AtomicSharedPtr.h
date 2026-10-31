/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: AtomicSharedPtr.
 *
 * Portable atomic shared-pointer storage. libc++ did not expose the
 * C++20 std::atomic&lt;std::shared_ptr&lt;T&gt;&gt; specialization on
 * every supported target; the mutex fallback keeps those builds correct
 * without deprecated free functions.
 *
 */

#ifndef I2PD_ATOMIC_SHARED_PTR_H__
#define I2PD_ATOMIC_SHARED_PTR_H__

#include <atomic>
#include <memory>
#include <mutex>
#include <utility>

namespace i2p
{
namespace util
{
	/// Atomic publication of shared ownership, with a mutex fallback when
	/// the standard-library specialization is unavailable. Loading keeps
	/// the object alive but does not synchronize later mutation of the
	/// pointed-to object.
#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
	template <typename T> class AtomicSharedPtr
	{
	public:
		AtomicSharedPtr () = default;
		explicit AtomicSharedPtr (std::shared_ptr<T> value)
			: m_Value (std::move (value))
		{
		}

		std::shared_ptr<T> Load (std::memory_order order = std::memory_order_acquire) const
		{
			return m_Value.load (order);
		}
		void Store (std::shared_ptr<T> value, std::memory_order order = std::memory_order_release)
		{
			m_Value.store (std::move (value), order);
		}
		std::shared_ptr<T> Exchange (std::shared_ptr<T> value, std::memory_order order = std::memory_order_acq_rel)
		{
			return m_Value.exchange (std::move (value), order);
		}
		AtomicSharedPtr & operator= (std::shared_ptr<T> value)
		{
			Store (std::move (value));
			return *this;
		}

	private:
		std::atomic<std::shared_ptr<T>> m_Value;
	};
#else
	template <typename T> class AtomicSharedPtr
	{
	public:
		AtomicSharedPtr () = default;
		explicit AtomicSharedPtr (std::shared_ptr<T> value)
			: m_Value (std::move (value))
		{
		}

		std::shared_ptr<T> Load (std::memory_order = std::memory_order_acquire) const
		{
			std::lock_guard<std::mutex> lock (m_Mutex);
			return m_Value;
		}
		void Store (std::shared_ptr<T> value, std::memory_order = std::memory_order_release)
		{
			std::lock_guard<std::mutex> lock (m_Mutex);
			m_Value = std::move (value);
		}
		std::shared_ptr<T> Exchange (std::shared_ptr<T> value, std::memory_order = std::memory_order_acq_rel)
		{
			std::lock_guard<std::mutex> lock (m_Mutex);
			auto previous = std::move (m_Value);
			m_Value = std::move (value);
			return previous;
		}
		AtomicSharedPtr & operator= (std::shared_ptr<T> value)
		{
			Store (std::move (value));
			return *this;
		}

	private:
		mutable std::mutex m_Mutex;
		mutable std::shared_ptr<T> m_Value;
	};
#endif
}
}

#endif
