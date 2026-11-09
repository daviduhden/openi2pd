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
	// Explicit false keeps the fallback testable on native-capable hosts
	// without changing feature macros or other translation units' layouts.
#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
	inline constexpr bool HasNativeAtomicSharedPtr = true;
#else
	inline constexpr bool HasNativeAtomicSharedPtr = false;
#endif
	template <typename T, bool Native = HasNativeAtomicSharedPtr> class AtomicSharedPtr;

	/// Atomic publication of shared ownership, with a mutex fallback when
	/// the standard-library specialization is unavailable. Loading keeps
	/// the object alive but does not synchronize later mutation of the
	/// pointed-to object. The storage must outlive concurrent calls and
	/// reentrant deleters.
#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
	template <typename T> class AtomicSharedPtr<T, true>
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
		/// Match pointer and ownership; failure replaces expected.
		bool CompareExchangeStrong (std::shared_ptr<T> & expected, std::shared_ptr<T> desired,
			std::memory_order success = std::memory_order_acq_rel,
			std::memory_order failure = std::memory_order_acquire)
		{
			return m_Value.compare_exchange_strong (expected, std::move (desired), success, failure);
		}
		AtomicSharedPtr & operator= (std::shared_ptr<T> value)
		{
			Store (std::move (value));
			return *this;
		}

	private:
		std::atomic<std::shared_ptr<T>> m_Value;
	};
#endif
	template <typename T> class AtomicSharedPtr<T, false>
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
			{
				std::lock_guard<std::mutex> lock (m_Mutex);
				m_Value.swap (value);
			}
			// The retired object's destructor can reenter this store.
			// Release its final owner only after the mutex is unlocked.
		}
		std::shared_ptr<T> Exchange (std::shared_ptr<T> value, std::memory_order = std::memory_order_acq_rel)
		{
			{
				std::lock_guard<std::mutex> lock (m_Mutex);
				m_Value.swap (value);
			}
			return value;
		}
		/*
		 * Match both the stored pointer and its control block. As with the
		 * standard specialization, a failed comparison updates expected.
		 * Memory orders must be valid for standard compare-exchange;
		 * mutex synchronization provides stronger ordering on this path.
		 */
		bool CompareExchangeStrong (std::shared_ptr<T> & expected, std::shared_ptr<T> desired,
			std::memory_order = std::memory_order_acq_rel,
			std::memory_order = std::memory_order_acquire)
		{
			std::shared_ptr<T> observed;
			bool matched;
			{
				std::lock_guard<std::mutex> lock (m_Mutex);
				matched = m_Value.get () == expected.get ()
					&& !m_Value.owner_before (expected) && !expected.owner_before (m_Value);
				if (matched) m_Value.swap (desired);
				else observed = m_Value;
			}
			// Both retired values and replaced expected owners can execute
			// arbitrary destructors; neither is released under the lock.
			if (!matched) expected.swap (observed);
			return matched;
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
	// Preserve construction with a deduced value type after introducing
	// the backend selector on the primary template.
	template <typename T> AtomicSharedPtr (std::shared_ptr<T>) -> AtomicSharedPtr<T>;
}
}

#endif
