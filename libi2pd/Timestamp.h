/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Timestamp.
 *
 * Declares epoch and monotonic clock accessors, UTC date formatting, the
 * router-wide clock offset and NTPTimeSync, which periodically queries the
 * configured NTP servers from a background thread.
 *
 */

#ifndef TIMESTAMP_H__
#define TIMESTAMP_H__

#include <memory>
#include <atomic>
#include <mutex>
#include <inttypes.h>
#include <thread>
#include <vector>
#include <string>
#include <utility>
#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp> // for boost 1.89

namespace i2p
{
namespace util
{
	uint64_t GetMillisecondsSinceEpoch ();
	uint64_t GetSecondsSinceEpoch ();
	uint32_t GetMinutesSinceEpoch ();
	uint32_t GetHoursSinceEpoch ();

	uint64_t GetMonotonicMicroseconds ();
	uint64_t GetMonotonicMilliseconds ();
	uint64_t GetMonotonicSeconds ();

	void GetCurrentDate (char * date); // returns UTC date as YYYYMMDD string, 9 bytes
	void GetNextDayDate (char * date); // returns next UTC day as YYYYMMDD string, 9 bytes
	void GetDateString (
		uint64_t timestamp, char * date); // timestamp is seconds since epoch, returns date as YYYYMMDD string, 9 bytes
	void AdjustTimeOffset (int64_t offset); // in seconds from current

	class NTPTimeSync
	{
	public:
		NTPTimeSync ();
		~NTPTimeSync ();

		void Start ();
		void Stop ();

	private:
		friend struct NTPTimeSyncTestAccess;

		void Run ();
		void Sync ();

	private:
		std::atomic<bool> m_IsRunning;
		std::mutex m_LifecycleMutex;
		std::unique_ptr<std::thread> m_Thread;
		boost::asio::io_context m_Service;
		boost::asio::steady_timer m_Timer;
		int m_SyncInterval = 1;
		std::vector<std::string> m_NTPServersList;
	};
}
}

#endif
