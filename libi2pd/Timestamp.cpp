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
#include <algorithm>
#include <array>
#include <cstdio>
#include <inttypes.h>
#include <string.h>
#include <chrono>
#include <future>
#include <span>
#include <utility>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>
#include "Config.h"
#include "Log.h"
#include "Random.h"
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

	uint64_t MakeNTPTransmitTimestamp (uint64_t unixSeconds, uint32_t fraction)
	{
		return ((unixSeconds + NTP_UNIX_EPOCH_DELTA) << 32) | static_cast<uint64_t> (fraction);
	}

	void BuildNTPRequest (std::span<uint8_t> packet, uint64_t transmitTimestamp)
	{
		if (packet.size () < NTP_PACKET_SIZE) return;
		std::fill (packet.begin (), packet.begin () + NTP_PACKET_SIZE, static_cast<uint8_t> (0));
		// LI 0 (no warning), VN 3 (RFC 4330), mode 3 (client).
		packet[0] = static_cast<uint8_t> ((0 << 6) | (NTP_MIN_VERSION << 3) | 3);
		htobe64buf (packet.data () + 40, transmitTimestamp);
	}

	NTPReplyError ValidateNTPReply (
		std::span<const uint8_t> packet, uint64_t expectedOrigin, uint32_t & serverSeconds)
	{
		serverSeconds = 0;
		if (packet.size () < NTP_PACKET_SIZE) return NTPReplyError::Size;
		const auto leap = static_cast<uint8_t> ((packet[0] >> 6) & 0x03);
		const auto version = static_cast<uint8_t> ((packet[0] >> 3) & 0x07);
		const auto mode = static_cast<uint8_t> (packet[0] & 0x07);
		if (mode != 4) return NTPReplyError::Mode; // server reply, never a request
		if (version < NTP_MIN_VERSION || version > NTP_MAX_VERSION) return NTPReplyError::Version;
		// Leap 3 means the server itself is unsynchronized: the reply carries
		// no authoritative time and must not be applied.
		if (leap > 2) return NTPReplyError::Leap;
		// Stratum 0 is a kiss-o'-death or unspecified clock; 16..255 is reserved
		// or unsynchronized.
		if (packet[1] == 0 || packet[1] > 15) return NTPReplyError::Stratum;
		const uint32_t rootDelay = bufbe32toh (packet.data () + 4) >> 16;
		if (rootDelay > NTP_MAX_ROOT_DELAY_SECONDS) return NTPReplyError::RootDelay;
		const uint32_t rootDispersion = bufbe32toh (packet.data () + 8) >> 16;
		if (rootDispersion > NTP_MAX_ROOT_DISPERSION_SECONDS) return NTPReplyError::RootDispersion;
		// The origin field must echo the transmit timestamp of our request, so
		// an off-path or delayed reply is not accepted as an answer to it.
		if (bufbe64toh (packet.data () + 24) != expectedOrigin) return NTPReplyError::Origin;
		const uint64_t transmit = bufbe64toh (packet.data () + 32);
		const uint64_t ntpSeconds = transmit >> 32;
		if (ntpSeconds == 0) return NTPReplyError::TransmitTime;
		// NTP counts from 1900 in a 32-bit second field: values below the Unix
		// epoch delta belong to era 1 (after 2036).
		const uint64_t unixSeconds = ntpSeconds >= NTP_UNIX_EPOCH_DELTA
			? ntpSeconds - NTP_UNIX_EPOCH_DELTA
			: ntpSeconds + (uint64_t { 1 } << 32) - NTP_UNIX_EPOCH_DELTA;
		if (unixSeconds > 0xFFFFFFFFULL) return NTPReplyError::TransmitTime;
		serverSeconds = static_cast<uint32_t> (unixSeconds);
		return NTPReplyError::None;
	}

	void NTPOffsetAgreement::Reset ()
	{
		m_Samples = { };
		m_Count = 0;
	}

	std::optional<int64_t> NTPOffsetAgreement::Add (int64_t offsetSeconds, uint64_t nowSeconds)
	{
		if (offsetSeconds <= NTP_MAX_SINGLE_PACKET_OFFSET && offsetSeconds >= -NTP_MAX_SINGLE_PACKET_OFFSET)
			return offsetSeconds;
		// Drop expired samples: they cannot corroborate a current offset.
		size_t kept = 0;
		for (size_t i = 0; i < m_Count; ++i)
		{
			if (nowSeconds > m_Samples[i].at
				&& nowSeconds - m_Samples[i].at > NTP_AGREEMENT_SAMPLE_LIFETIME)
				continue;
			m_Samples[kept++] = m_Samples[i];
		}
		m_Count = kept;
		if (m_Count < m_Samples.size ()) m_Samples[m_Count++] = { offsetSeconds, nowSeconds };
		if (m_Count < NTP_AGREEMENT_SAMPLES) return std::nullopt;
		int64_t lowest = m_Samples[0].offset;
		int64_t highest = m_Samples[0].offset;
		int64_t sum = 0;
		for (size_t i = 0; i < m_Count; ++i)
		{
			lowest = std::min (lowest, m_Samples[i].offset);
			highest = std::max (highest, m_Samples[i].offset);
			sum += m_Samples[i].offset;
		}
		if (highest - lowest > NTP_AGREEMENT_WINDOW)
		{
			// Keep collecting samples but never apply a split vote: the oldest
			// sample is discarded so a stale value cannot pin the decision.
			m_Samples[0] = m_Samples[m_Count - 1];
			m_Count = 1;
			return std::nullopt;
		}
		// Apply the mean of the agreeing samples (rounded towards zero).
		return sum / static_cast<int64_t> (m_Count);
	}

	// Returns true when an offset was applied to the shared clock.
	static bool SyncTimeWithNTP (
		const std::string & address, const std::atomic<bool> & isRunning, NTPOffsetAgreement & agreement)
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
				return false;
			}

			boost::asio::ip::udp::socket socket (service);
			socket.open (ep.protocol (), ec);
			if (!ec)
			{
				std::array<uint8_t, NTP_PACKET_SIZE> request { };
				// A random fraction makes the echoed origin timestamp an
				// unpredictable nonce for this request.
				const uint64_t transmit = MakeNTPTransmitTimestamp (
					GetLocalSecondsSinceEpoch (), i2p::crypto::RandomUInt32 ());
				BuildNTPRequest (request, transmit);
				std::array<uint8_t, NTP_PACKET_SIZE> buf { };
				size_t len = 0;
				boost::asio::ip::udp::endpoint sender;
				try
				{
					socket.send_to (boost::asio::buffer (request), ep);
					// Poll in short slices so Stop() is not delayed by the full
					// ten-second NTP timeout. 100 * 100 ms == 10 s.
					int i = 0;
					while (!socket.available () && i < 100 && isRunning.load (std::memory_order_relaxed))
					{
						std::this_thread::sleep_for (std::chrono::milliseconds (100));
						i++;
					}
					if (isRunning.load (std::memory_order_relaxed) && socket.available ())
					{
						len = socket.receive_from (boost::asio::buffer (buf), sender);
						// The reply must come from the server this request was
						// sent to; receive_from() rewrites its endpoint argument.
						if (sender.address () != ep.address () || sender.port () != ep.port ())
						{
							LogPrint (eLogWarning, "Timestamp: Ignoring an NTP reply from an unexpected source");
							return false;
						}
					}
				}
				catch (std::exception & e)
				{
					LogPrint (eLogError, "Timestamp: NTP error: ", e.what ());
				}
				if (len)
				{
					uint32_t serverSeconds = 0;
					const auto validation = ValidateNTPReply (
						std::span<const uint8_t> (buf.data (), len), transmit, serverSeconds);
					if (validation != NTPReplyError::None)
					{
						LogPrint (eLogWarning, "Timestamp: Ignoring an invalid NTP reply (reason ",
							static_cast<int> (validation), ")");
						return false;
					}
					const auto ourTs = GetLocalSecondsSinceEpoch ();
					const int64_t offset = static_cast<int64_t> (serverSeconds) - static_cast<int64_t> (ourTs);
					const auto agreed = agreement.Add (offset, ourTs);
					if (!agreed)
					{
						LogPrint (eLogInfo, "Timestamp: NTP reply from ", address, " not applied; waiting for agreement");
						return false;
					}
					// The clock is never moved from a single unauthenticated
					// packet when the difference is large.
					g_TimeOffset.store (*agreed, std::memory_order_relaxed);
					LogPrint (eLogInfo, "Timestamp: ", address, " time offset from system time is ",
						g_TimeOffset.load (std::memory_order_relaxed), " seconds");
					return true;
				}
			}
			else
				LogPrint (eLogError, "Timestamp: Couldn't open UDP socket");
		}
		else
			LogPrint (eLogError, "Timestamp: Couldn't resolve address ", address);
		return false;
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
		{
			// A large correction needs several independent replies that agree.
			// Query a random, distinct subset and stop as soon as a reply has
			// been applied, so one cycle never re-uses one server's answer.
			m_Agreement.Reset ();
			const size_t attempts = std::min (m_NTPServersList.size (), NTP_AGREEMENT_SAMPLES);
			const size_t start = m_NTPServersList.size () > 1
				? i2p::crypto::RandomBelow (static_cast<uint32_t> (m_NTPServersList.size ()))
				: 0;
			for (size_t i = 0; i < attempts && m_IsRunning.load (std::memory_order_relaxed); ++i)
			{
				const auto & server = m_NTPServersList[(start + i) % m_NTPServersList.size ()];
				if (SyncTimeWithNTP (server, m_IsRunning, m_Agreement)) break;
			}
		}
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
		// Epoch minutes fit uint32_t until well beyond the protocol lifetime.
		return static_cast<uint32_t> (
			GetLocalMinutesSinceEpoch () + g_TimeOffset.load (std::memory_order_relaxed) / 60);
	}

	uint32_t GetHoursSinceEpoch ()
	{
		return static_cast<uint32_t> (
			GetLocalHoursSinceEpoch () + g_TimeOffset.load (std::memory_order_relaxed) / 3600);
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
		int year = tm.tm_year + 1900;
		if (year < 0 || year > 9999) year = 0;
		date[0] = static_cast<char> ('0' + year / 1000);
		date[1] = static_cast<char> ('0' + (year / 100) % 10);
		date[2] = static_cast<char> ('0' + (year / 10) % 10);
		date[3] = static_cast<char> ('0' + year % 10);
		date[4] = static_cast<char> ('0' + (tm.tm_mon + 1) / 10);
		date[5] = static_cast<char> ('0' + (tm.tm_mon + 1) % 10);
		date[6] = static_cast<char> ('0' + tm.tm_mday / 10);
		date[7] = static_cast<char> ('0' + tm.tm_mday % 10);
		date[8] = 0;
	}

	void AdjustTimeOffset (int64_t offset)
	{
		g_TimeOffset.fetch_add (offset, std::memory_order_relaxed);
	}
}
}
