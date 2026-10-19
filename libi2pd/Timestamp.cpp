/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Timestamp.
 *
 * Implements wall-clock and monotonic time accessors, offset-adjusted UTC date
 * formatting, and the NTP client loop that resolves the configured servers and
 * updates the shared offset atomically.
 *
 */

#include <time.h>
#include <cstdio>
#include <inttypes.h>
#include <string.h>
#include <chrono>
#include <future>
#include <utility>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>
#include "Config.h"
#include "Log.h"
#include "RouterContext.h"
#include "I2PEndian.h"
#include "Timestamp.h"
#include "util.h"

namespace i2p
{
namespace util
{
	static uint64_t GetLocalMillisecondsSinceEpoch ()
	{
		return std::chrono::duration_cast<std::chrono::milliseconds> (
			std::chrono::system_clock::now ().time_since_epoch ())
			.count ();
	}

	static uint64_t GetLocalSecondsSinceEpoch ()
	{
		return std::chrono::duration_cast<std::chrono::seconds> (std::chrono::system_clock::now ().time_since_epoch ())
			.count ();
	}

	static uint32_t GetLocalMinutesSinceEpoch ()
	{
		return std::chrono::duration_cast<std::chrono::minutes> (std::chrono::system_clock::now ().time_since_epoch ())
			.count ();
	}

	static uint32_t GetLocalHoursSinceEpoch ()
	{
		return std::chrono::duration_cast<std::chrono::hours> (std::chrono::system_clock::now ().time_since_epoch ())
			.count ();
	}

	static std::atomic<int64_t> g_TimeOffset { 0 }; // in seconds

	static void SyncTimeWithNTP (const std::string & address, const std::atomic<bool> & isRunning)
	{
		LogPrint (eLogInfo, "Timestamp: NTP request to ", address);
		boost::asio::io_context service;
		boost::system::error_code ec;
		auto endpoints = boost::asio::ip::udp::resolver (service).resolve (address, "ntp", ec);
		if (!ec)
		{
			bool found = false;
			boost::asio::ip::udp::endpoint ep;
			for (const auto & it : endpoints)
			{
				ep = it;
				if (!ep.address ().is_unspecified ())
				{
					if (ep.address ().is_v4 ())
					{
						if (i2p::context.SupportsV4 ()) found = true;
					}
					else if (ep.address ().is_v6 ())
					{
						if (i2p::util::net::IsYggdrasilAddress (ep.address ()))
						{
							if (i2p::context.SupportsMesh ()) found = true;
						}
						else if (i2p::context.SupportsV6 ())
							found = true;
					}
				}
				if (found) break;
			}
			if (!found)
			{
				LogPrint (eLogError, "Timestamp: can't find compatible address for ", address);
				return;
			}

			boost::asio::ip::udp::socket socket (service);
			socket.open (ep.protocol (), ec);
			if (!ec)
			{
				uint8_t buf[48]; // 48 bytes NTP request/response
				memset (buf, 0, 48);
				htobe32buf (buf, (3 << 27) | (3 << 24)); // RFC 4330
				size_t len = 0;
				try
				{
					socket.send_to (boost::asio::buffer (buf, 48), ep);
					// Poll in short slices so Stop() is not delayed by the full
					// ten-second NTP timeout. 100 * 100 ms == 10 s.
					int i = 0;
					while (!socket.available () && i < 100 && isRunning.load (std::memory_order_relaxed))
					{
						std::this_thread::sleep_for (std::chrono::milliseconds (100));
						i++;
					}
					if (isRunning.load (std::memory_order_relaxed) && socket.available ())
						len = socket.receive_from (boost::asio::buffer (buf, 48), ep);
				}
				catch (std::exception & e)
				{
					LogPrint (eLogError, "Timestamp: NTP error: ", e.what ());
				}
				if (len >= 8)
				{
					auto ourTs = GetLocalSecondsSinceEpoch ();
					uint32_t ts = bufbe32toh (buf + 32);
					if (ts > 2208988800U) ts -= 2208988800U; // 1/1/1970 from 1/1/1900
					g_TimeOffset.store (
						static_cast<int64_t> (ts) - static_cast<int64_t> (ourTs), std::memory_order_relaxed);
					LogPrint (eLogInfo, "Timestamp: ", address, " time offset from system time is ",
						g_TimeOffset.load (std::memory_order_relaxed), " seconds");
				}
			}
			else
				LogPrint (eLogError, "Timestamp: Couldn't open UDP socket");
		}
		else
			LogPrint (eLogError, "Timestamp: Couldn't resolve address ", address);
	}

	NTPTimeSync::NTPTimeSync ()
		: m_IsRunning (false)
		, m_Timer (m_Service)
	{
		i2p::config::GetOption ("nettime.ntpsyncinterval", m_SyncInterval);
		std::string ntpservers;
		i2p::config::GetOption ("nettime.ntpservers", ntpservers);
		boost::split (m_NTPServersList, ntpservers, boost::is_any_of (","), boost::token_compress_on);
	}

	NTPTimeSync::~NTPTimeSync ()
	{
		Stop ();
	}

	void NTPTimeSync::Start ()
	{
		std::lock_guard<std::mutex> l (m_LifecycleMutex);
		bool expected = false;
		if (!m_IsRunning.compare_exchange_strong (expected, true))
		{
			LogPrint (eLogDebug, "Timestamp: NTP time sync already running");
			return; // no-op if already running
		}
		if (m_NTPServersList.empty ())
		{
			m_IsRunning.store (false, std::memory_order_relaxed);
			LogPrint (eLogWarning, "Timestamp: No NTP server found");
			return;
		}
		// A previous Stop() left the io_context stopped. Restart it before
		// posting work or starting a new thread.
		m_Service.restart ();
		try
		{
			m_Thread = std::make_unique<std::thread> (std::bind (&NTPTimeSync::Run, this));
		}
		catch (...)
		{
			m_IsRunning.store (false, std::memory_order_relaxed);
			throw;
		}
		LogPrint (eLogInfo, "Timestamp: NTP time sync starting");
		boost::asio::post (m_Service, std::bind (&NTPTimeSync::Sync, this));
	}

	void NTPTimeSync::Stop ()
	{
		std::lock_guard<std::mutex> l (m_LifecycleMutex);
		const bool wasRunning = m_IsRunning.exchange (false, std::memory_order_relaxed);
		if (!wasRunning && !m_Thread) return;
		LogPrint (eLogInfo, "Timestamp: NTP time sync stopping");
		// Cancel the timer on its own io_context. Cross-thread cancel() is
		// not used: the handler and the timer belong to that executor.
		boost::asio::post (m_Service, [this] () { m_Timer.cancel (); });
		// Also stop the service so Run() returns even if the cancellation
		// handler cannot be dispatched first.
		m_Service.stop ();
		if (m_Thread)
		{
			if (m_Thread->joinable ()) m_Thread->join ();
			m_Thread.reset ();
		}
		// Fallback: no handler can run now, so cancelling the pending wait
		// here cannot race. This prevents a stale one-hour wait from firing
		// after a later Start().
		m_Timer.cancel ();
	}

	void NTPTimeSync::Run ()
	{
		i2p::util::SetThreadName ("Timesync");

		while (m_IsRunning.load (std::memory_order_relaxed))
		{
			try
			{
				m_Service.run ();
			}
			catch (std::exception & ex)
			{
				LogPrint (eLogError, "Timestamp: NTP time sync exception: ", ex.what ());
			}
		}
	}

	void NTPTimeSync::Sync ()
	{
		if (!m_IsRunning.load (std::memory_order_relaxed)) return;
		if (m_NTPServersList.size () > 0)
			SyncTimeWithNTP (m_NTPServersList[rand () % m_NTPServersList.size ()], m_IsRunning);
		else
			m_IsRunning.store (false, std::memory_order_relaxed);

		if (m_IsRunning.load (std::memory_order_relaxed))
		{
			m_Timer.expires_after (std::chrono::hours (m_SyncInterval));
			m_Timer.async_wait (
				[this] (const boost::system::error_code & ecode)
				{
					if (ecode != boost::asio::error::operation_aborted) Sync ();
				});
		}
	}

	uint64_t GetMillisecondsSinceEpoch ()
	{
		return GetLocalMillisecondsSinceEpoch () + g_TimeOffset.load (std::memory_order_relaxed) * 1000;
	}

	uint64_t GetSecondsSinceEpoch ()
	{
		return GetLocalSecondsSinceEpoch () + g_TimeOffset.load (std::memory_order_relaxed);
	}

	uint32_t GetMinutesSinceEpoch ()
	{
		return GetLocalMinutesSinceEpoch () + g_TimeOffset.load (std::memory_order_relaxed) / 60;
	}

	uint32_t GetHoursSinceEpoch ()
	{
		return GetLocalHoursSinceEpoch () + g_TimeOffset.load (std::memory_order_relaxed) / 3600;
	}

	uint64_t GetMonotonicMicroseconds ()
	{
		return std::chrono::duration_cast<std::chrono::microseconds> (
			std::chrono::steady_clock::now ().time_since_epoch ())
			.count ();
	}

	uint64_t GetMonotonicMilliseconds ()
	{
		return std::chrono::duration_cast<std::chrono::milliseconds> (
			std::chrono::steady_clock::now ().time_since_epoch ())
			.count ();
	}

	uint64_t GetMonotonicSeconds ()
	{
		return std::chrono::duration_cast<std::chrono::seconds> (std::chrono::steady_clock::now ().time_since_epoch ())
			.count ();
	}

	void GetCurrentDate (char * date)
	{
		GetDateString (GetSecondsSinceEpoch (), date);
	}

	void GetNextDayDate (char * date)
	{
		GetDateString (GetSecondsSinceEpoch () + 24 * 60 * 60, date);
	}

	void GetDateString (uint64_t timestamp, char * date)
	{
		using clock = std::chrono::system_clock;
		auto t = clock::to_time_t (clock::time_point (std::chrono::seconds (timestamp)));
		struct tm tm;
#ifdef _WIN32
		if (gmtime_s (&tm, &t) != 0)
		{
			date[0] = 0;
			return;
		}
#else
		if (!gmtime_r (&t, &tm))
		{
			date[0] = 0;
			return;
		}
#endif
		std::snprintf (date, 9, "%04i%02i%02i", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
	}

	void AdjustTimeOffset (int64_t offset)
	{
		g_TimeOffset.fetch_add (offset, std::memory_order_relaxed);
	}
}
}
