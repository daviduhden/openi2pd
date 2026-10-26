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

#include <array>
#include <memory>
#include <atomic>
#include <mutex>
#include <optional>
#include <span>
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
	/// Shift the router-wide clock offset in seconds.
	/// The offset is applied by every GetSecondsSinceEpoch()/GetMillisecondsSinceEpoch()
	/// caller, so it changes signed descriptor timestamps and lease expiry for the
	/// whole process. Only the NTP sync path or an explicit operator action may
	/// move it, and a large jump is applied only after independent agreement.
	void AdjustTimeOffset (int64_t offset); // in seconds from current

	/*
	 * SNTP request/reply validation.
	 *
	 * An unauthenticated UDP reply must never move the router clock on its
	 * own: the source endpoint, mode, version, leap indicator, stratum, root
	 * delay/dispersion and the echoed origin timestamp are all checked before
	 * the transmit timestamp is trusted.
	 */
	constexpr size_t NTP_PACKET_SIZE = 48;
	constexpr uint32_t NTP_UNIX_EPOCH_DELTA = 2208988800U; // 1900-01-01 .. 1970-01-01
	constexpr uint8_t NTP_MIN_VERSION = 3;
	constexpr uint8_t NTP_MAX_VERSION = 4;
	constexpr uint32_t NTP_MAX_ROOT_DELAY_SECONDS = 8;
	constexpr uint32_t NTP_MAX_ROOT_DISPERSION_SECONDS = 16;
	// An offset this small is applied from one validated reply.
	constexpr int64_t NTP_MAX_SINGLE_PACKET_OFFSET = 1; // seconds
	// A larger offset needs this many replies that agree within the window.
	constexpr int64_t NTP_AGREEMENT_WINDOW = 2; // seconds
	constexpr size_t NTP_AGREEMENT_SAMPLES = 3;
	constexpr uint64_t NTP_AGREEMENT_SAMPLE_LIFETIME = 300; // seconds

	// Encodes the client transmit timestamp (seconds since 1900, 32-bit fraction).
	uint64_t MakeNTPTransmitTimestamp (uint64_t unixSeconds, uint32_t fraction);
	// Fills a 48-byte client request (LI 0, VN 3, mode 3, origin in transmit).
	void BuildNTPRequest (std::span<uint8_t> packet, uint64_t transmitTimestamp);

	enum class NTPReplyError
	{
		None = 0,
		Size,
		Mode,
		Version,
		Leap,
		Stratum,
		RootDelay,
		RootDispersion,
		Origin,
		TransmitTime
	};

	// Validates one reply and extracts the server time as Unix seconds.
	NTPReplyError ValidateNTPReply (std::span<const uint8_t> packet, uint64_t expectedOrigin, uint32_t & serverSeconds);

	/*
	 * Majority agreement for clock offsets. Small offsets are returned from a
	 * single sample; a larger jump requires NTP_AGREEMENT_SAMPLES samples whose
	 * spread is inside NTP_AGREEMENT_WINDOW. Samples older than
	 * NTP_AGREEMENT_SAMPLE_LIFETIME are discarded.
	 */
	class NTPOffsetAgreement
	{
	public:
		void Reset ();
		std::optional<int64_t> Add (int64_t offsetSeconds, uint64_t nowSeconds);
		size_t GetSampleCount () const { return m_Count; }

	private:
		struct Sample
		{
			int64_t offset;
			uint64_t at;
		};

		std::array<Sample, NTP_AGREEMENT_SAMPLES> m_Samples { };
		size_t m_Count { 0 };
	};

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
		// Samples collected within one sync cycle; reset before each cycle.
		NTPOffsetAgreement m_Agreement;
	};
}
}

#endif
