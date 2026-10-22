/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Queue.
 *
 * Thread-safe FIFO queue built on std::list, a mutex and a condition variable, with blocking,
 * timed and non-blocking dequeue, bulk splice insertion and queue swapping. Put() is
 * unbounded, so callers that need backpressure use PutIfBelow(); Get() and Peek() return
 * nullptr when empty.
 *
 */

#ifndef QUEUE_H__
#define QUEUE_H__

#include <list>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <functional>
#include <utility>

namespace i2p
{
namespace util
{
	template <typename Element> class Queue
	{
	public:
		void Put (Element e)
		{
			std::unique_lock<std::mutex> l (m_QueueMutex);
			m_Queue.push_back (std::move (e));
			m_NonEmpty.notify_one ();
		}

		/**
		 * Enqueue only while the queue is below an explicit limit. The size
		 * check and insertion are one critical section, so concurrent
		 * producers cannot exceed the limit. Returns false when the record was
		 * rejected; the element is then left untouched.
		 */
		bool PutIfBelow (Element e, size_t limit)
		{
			std::unique_lock<std::mutex> l (m_QueueMutex);
			if (m_Queue.size () >= limit) return false;
			m_Queue.push_back (std::move (e));
			m_NonEmpty.notify_one ();
			return true;
		}

		void Put (std::list<Element> & list)
		{
			if (!list.empty ())
			{
				std::unique_lock<std::mutex> l (m_QueueMutex);
				m_Queue.splice (m_Queue.end (), list);
				m_NonEmpty.notify_one ();
			}
		}

		Element GetNext ()
		{
			std::unique_lock<std::mutex> l (m_QueueMutex);
			auto el = GetNonThreadSafe ();
			if (!el)
			{
				m_NonEmpty.wait (l);
				el = GetNonThreadSafe ();
			}
			return el;
		}

		Element GetNextWithTimeout (int usec)
		{
			std::unique_lock<std::mutex> l (m_QueueMutex);
			auto el = GetNonThreadSafe ();
			if (!el)
			{
				m_NonEmpty.wait_for (l, std::chrono::milliseconds (usec));
				el = GetNonThreadSafe ();
			}
			return el;
		}

		void Wait ()
		{
			std::unique_lock<std::mutex> l (m_QueueMutex);
			m_NonEmpty.wait (l);
		}

		bool Wait (int sec, int usec)
		{
			std::unique_lock<std::mutex> l (m_QueueMutex);
			return m_NonEmpty.wait_for (l, std::chrono::seconds (sec) + std::chrono::milliseconds (usec))
				!= std::cv_status::timeout;
		}

		bool IsEmpty ()
		{
			std::unique_lock<std::mutex> l (m_QueueMutex);
			return m_Queue.empty ();
		}

		size_t GetSize () const
		{
			std::unique_lock<std::mutex> l (m_QueueMutex);
			return m_Queue.size ();
		}

		void WakeUp () { m_NonEmpty.notify_all (); };

		Element Get ()
		{
			std::unique_lock<std::mutex> l (m_QueueMutex);
			return GetNonThreadSafe ();
		}

		Element Peek ()
		{
			std::unique_lock<std::mutex> l (m_QueueMutex);
			return GetNonThreadSafe (true);
		}

		void GetWholeQueue (std::list<Element> & queue)
		{
			if (!queue.empty ())
			{
				std::list<Element> newQueue;
				queue.swap (newQueue);
			}
			{
				std::unique_lock<std::mutex> l (m_QueueMutex);
				m_Queue.swap (queue);
			}
		}

	private:
		Element GetNonThreadSafe (bool peek = false)
		{
			if (!m_Queue.empty ())
			{
				auto el = m_Queue.front ();
				if (!peek) m_Queue.pop_front ();
				return el;
			}
			return nullptr;
		}

	private:
		std::list<Element> m_Queue;
		mutable std::mutex m_QueueMutex;
		std::condition_variable m_NonEmpty;
	};
}
}

#endif
