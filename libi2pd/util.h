/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: util.
 *
 * Provides allocation pools (single-threaded and mutex-protected), asio worker
 * service wrappers, RAII helpers, the Mapping option codec and network utilities
 * such as MTU discovery, interface lookup and local/reserved/Yggdrasil range
 * checks.
 *
 */

#ifndef UTIL_H
#define UTIL_H

#include <inttypes.h>
#include <atomic>
#include <string>
#include <map>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <boost/asio.hpp>
#include "AddressFormat.h"

namespace i2p
{
namespace util
{

	template <class T> class MemoryPool
	{
		//BOOST_STATIC_ASSERT_MSG(sizeof(T) >= sizeof(void*), "size cannot be less that general pointer size");

	public:
		MemoryPool ()
			: m_Head (nullptr)
			, m_Size (0)
		{
		}
		~MemoryPool () { CleanUp (); }

		void CleanUp ()
		{
			// detach the list first: while it is being freed m_Head would point
			// to already deleted memory, and Acquire or Release from another
			// thread reads or writes it
			auto head = m_Head;
			m_Head = nullptr;
			m_Size = 0;
			CleanUp (head);
		}

		template <typename... TArgs> T * Acquire (TArgs &&... args)
		{
			if (!m_Head)
				return new T (std::forward<TArgs> (args)...);
			else
			{
				auto tmp = m_Head;
				m_Head = static_cast<T *> (*(void **)m_Head); // next
				--m_Size;
				return new (tmp) T (std::forward<TArgs> (args)...);
			}
		}

		void Release (T * t)
		{
			if (!t) return;
			t->~T ();
			// The pool is a cache, not an unbounded allocator. A long-lived
			// parser must not retain every discarded RouterInfo/Buffer.
			if (m_Size >= MAX_CACHED)
			{
				::operator delete ((void *)t);
				return;
			}
			Splice (t);
			++m_Size;
		}

		/**
		 * Typed pool deleter. Replaces the former std::function/std::bind
		 * erasure: the object still borrows the pool, which must outlive it.
		 */
		struct Deleter
		{
			MemoryPool * pool;
			void operator() (T * value) const noexcept { pool->Release (value); }
		};

		template <typename... TArgs> std::unique_ptr<T, Deleter> AcquireUnique (TArgs &&... args)
		{
			return std::unique_ptr<T, Deleter> (Acquire (std::forward<TArgs> (args)...), Deleter { this });
		}

		template <typename... TArgs> std::shared_ptr<T> AcquireShared (TArgs &&... args)
		{
			return std::shared_ptr<T> (Acquire (std::forward<TArgs> (args)...), Deleter { this });
		}

	protected:
		void CleanUp (T * head)
		{
			while (head)
			{
				auto tmp = head;
				head = static_cast<T *> (*(void **)head); // next
				// Release() already ran ~T() before placing the object on the
				// free list; destroying it again would be a double destruction.
				::operator delete ((void *)tmp);
			}
		}

		void Splice (T * t)
		{
			*(void **)t = m_Head; // next
			m_Head = t;
		}

		void Splice (T * first, T * last)
		{
			*(void **)last = m_Head; // next
			m_Head = first;
		}

	protected:
		static constexpr size_t MAX_CACHED = 256;
		T * m_Head;
		size_t m_Size;
	};

	template <class T> class MemoryPoolMt : private MemoryPool<T>
	{
	public:
		MemoryPoolMt () { }
		template <typename... TArgs> T * AcquireMt (TArgs &&... args)
		{
			T * ret = nullptr;
			{
				std::lock_guard<std::mutex> l (m_Mutex);
				if (this->m_Head) // not empty
					ret = this->Acquire (std::forward<TArgs> (args)...);
			}
			if (!ret) ret = new T (std::forward<TArgs> (args)...);
			return ret;
		}

		void ReleaseMt (T * t)
		{
			if (!t) return;
			std::lock_guard<std::mutex> l (m_Mutex);
			this->Release (t);
		}

		void ReleaseMt (T ** arr, size_t num)
		{
			if (!arr || !num) return;
			// Bulk releases are uncommon; free them directly rather than
			// bypassing the bounded single-object cache accounting.
			for (size_t i = 0; i < num; i++)
			{
				T * t = arr[i];
				if (!t) continue;
				t->~T ();
				::operator delete ((void *)t);
			}
		}

		template <template <typename, typename...> class C, typename... R> void ReleaseMt (const C<T *, R...> & c)
		{
			for (auto & it : c)
			{
				T * t = it;
				if (!t) continue;
				t->~T ();
				::operator delete ((void *)t);
			}
		}

		/** Thread-safe typed pool deleter; the pool must outlive the object. */
		struct Deleter
		{
			MemoryPoolMt * pool;
			void operator() (T * value) const noexcept { pool->ReleaseMt (value); }
		};

		template <typename... TArgs> std::shared_ptr<T> AcquireSharedMt (TArgs &&... args)
		{
			return std::shared_ptr<T> (AcquireMt (std::forward<TArgs> (args)...), Deleter { this });
		}

		void CleanUpMt ()
		{
			T * head;
			{
				std::lock_guard<std::mutex> l (m_Mutex);
				head = this->m_Head;
				this->m_Head = nullptr;
				this->m_Size = 0;
			}
			if (head) this->CleanUp (head);
		}

	private:
		std::mutex m_Mutex;
	};

	class RunnableService
	{
	protected:
		RunnableService (const std::string & name)
			: m_Name (name)
			, m_IsRunning (false)
		{
		}
		// The base destructor stops and joins the worker. Derived services
		// must not leave a joinable thread behind; this is the explicit safe
		// contract for objects whose Stop() was never called.
		virtual ~RunnableService ()
		{
			if (m_Thread && m_Thread->joinable () && m_Thread->get_id () == std::this_thread::get_id ())
			{
				// Destruction from the service's own handler: detaching avoids
				// std::terminate and is the only safe option here.
				m_Thread->detach ();
				m_Thread.reset ();
			}
			else
				StopIOService ();
		}

		auto & GetIOService () { return m_Service; }
		bool IsRunning () const { return m_IsRunning.load (std::memory_order_relaxed); }

		void StartIOService ();
		void StopIOService (bool stopService = true);

		void SetName (std::string_view name);

	private:
		void Run ();

	private:
		std::string m_Name;
		std::atomic<bool> m_IsRunning;
		std::mutex m_LifecycleMutex;
		std::unique_ptr<std::thread> m_Thread;
		boost::asio::io_context m_Service;
	};

	class RunnableServiceWithWork : public RunnableService
	{
	protected:
		RunnableServiceWithWork (const std::string & name)
			: RunnableService (name)
			, m_Work (boost::asio::make_work_guard (GetIOService ()))
		{
		}

		void StopWorkAndFinishTasks (); // let all outstanding tasks finish and terminate

	private:
		boost::asio::executor_work_guard<boost::asio::io_context::executor_type> m_Work;
	};

	void SetThreadName (const char * name);

	class Mapping
	{
	public:
		Mapping () = default;
		Mapping (const Mapping &) = default;
		Mapping (Mapping &&) = default;
		Mapping (std::initializer_list<std::pair<const std::string, std::string>> options)
			: m_Options (options)
		{
		}

		size_t FromBuffer (const uint8_t * buf, size_t len);
		size_t FromBuffer (size_t size, const uint8_t * buf, size_t len); //without 2 bytes size
		size_t ToBuffer (uint8_t * buf, size_t len) const;

		std::string_view operator[] (std::string_view param) const;
		bool Insert (std::string_view param, std::string_view value);
		bool Contains (std::string_view param) const;
		void CleanUp ();
		bool IsEmpty () const { return m_Options.empty (); }

		static std::string_view ExtractString (const uint8_t * buf, size_t len);
		static size_t WriteString (std::string_view str, uint8_t * buf, size_t len);
		static size_t WriteOption (std::string_view param, std::string_view value, uint8_t * buf, size_t len);

		template <typename T> bool Get (std::string_view param, T & value) const
		{
			auto s = (*this)[param];
			if (s.empty ()) return false;
			auto res = std::from_chars (s.data (), s.data () + s.size (), value);
			return res.ec == std::errc ();
		}
		bool Get (std::string_view param, bool & value) const
		{
			auto s = (*this)[param];
			if (s.empty ()) return false;
			return GetBoolParamValue (s, value);
		}
		template <typename T> bool Put (std::string_view param, T value)
		{
			return Insert (param, std::to_string (value));
		}

	private:
		static bool GetBoolParamValue (std::string_view s, bool & value);

	private:
		std::map<std::string, std::string, std::less<>> m_Options;

	public:
		const decltype (m_Options) & GetOptions () const { return m_Options; }
	};

	uint_fast32_t GetRngSeed ();

	// Bounded, NUL-terminating copy for fixed platform buffers. Returns the
	// required source length (like strlcpy) without reading past the view.
	inline std::size_t CopyString (char * destination, std::size_t capacity, std::string_view source) noexcept
	{
		const std::size_t required = source.size ();
		if (!destination || capacity == 0) return required;
		const std::size_t copied = required < capacity - 1 ? required : capacity - 1;
		if (copied) std::memcpy (destination, source.data (), copied);
		destination[copied] = '\0';
		return required;
	}

	namespace net
	{
		int GetMTU (const boost::asio::ip::address & localAddress);
		int GetMaxMTU (const boost::asio::ip::address_v6 & localAddress); // check tunnel broker for ipv6 address
		const boost::asio::ip::address GetInterfaceAddress (const std::string & ifname, bool ipv6 = false);
		boost::asio::ip::address_v6 GetYggdrasilAddress ();
		boost::asio::ip::address_v6 GetClearnetIPV6Address ();
		bool IsLocalAddress (const boost::asio::ip::address & addr);
		bool IsInReservedRange (const boost::asio::ip::address & host);
		bool IsYggdrasilAddress (const boost::asio::ip::address & addr);
		bool IsPortInReservedRange (const uint16_t port) noexcept;
	}
}
}

#endif
