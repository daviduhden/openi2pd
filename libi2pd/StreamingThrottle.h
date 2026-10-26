/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: StreamingThrottle.
 *
 * Counts incoming streaming connections per peer and in total over the
 * minute/hour/day windows used by Java's ConnThrottler and ConnectionManager.
 * Check() returns the retry interval so the streaming layer can apply the
 * configured limitAction.
 *
 */

#ifndef STREAMING_THROTTLE_H__
#define STREAMING_THROTTLE_H__

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include "Tag.h"

namespace i2p
{
namespace stream
{
	struct StreamingConnectionLimits
	{
		int perPeerMinute = 0;
		int perPeerHour = 0;
		int perPeerDay = 0;
		int totalMinute = 0;
		int totalHour = 0;
		int totalDay = 0;
	};

	// Java ConnectionManager checks day, hour and minute in that order and
	// consumes an outer window even when a later window rejects the attempt.
	// ConnThrottler increments a counter and clears it periodically; this
	// implementation uses a fixed window per counter (constant memory) rather
	// than an unbounded list of timestamps. The retry values are the
	// ConnectionManager 60/3600/86400-second reasons used by limitAction.
	class StreamingConnectionThrottle
	{
	public:
		void SetLimits (const StreamingConnectionLimits & limits)
		{
			std::lock_guard<std::mutex> lock (m_Mutex);
			m_Limits = limits;
		}

		StreamingConnectionLimits GetLimits () const
		{
			std::lock_guard<std::mutex> lock (m_Mutex);
			return m_Limits;
		}

		// 0 means allowed; otherwise the ConnectionManager retry interval in
		// seconds (60, 3600 or 86400) for the configured limit action.
		int Check (const i2p::data::Tag<32> & peer, uint64_t now)
		{
			std::lock_guard<std::mutex> lock (m_Mutex);
			if (m_Limits.perPeerMinute <= 0 && m_Limits.perPeerHour <= 0 && m_Limits.perPeerDay <= 0
				&& m_Limits.totalMinute <= 0 && m_Limits.totalHour <= 0 && m_Limits.totalDay <= 0)
				return 0;

			// Never fail open for the totals: if per-peer tracking is full, the
			// attempt gets an untracked (always fresh) peer window but still
			// consumes and checks the total budget.
			PeerWindows overflow;
			PeerWindows * windows = &overflow;
			const bool trackPeer
				= m_Limits.perPeerMinute > 0 || m_Limits.perPeerHour > 0 || m_Limits.perPeerDay > 0;
			if (trackPeer)
			{
				auto it = m_Peers.find (peer);
				if (it != m_Peers.end ())
					windows = &it->second;
				else if (m_Peers.size () < MAX_TRACKED_PEERS)
					windows = &m_Peers.emplace (peer, PeerWindows { }).first->second;
			}

			if (IncrementAndOver (windows->day, now, DAY_WINDOW, m_Limits.perPeerDay)) return DAY_WINDOW;
			if (IncrementAndOver (m_TotalDay, now, DAY_WINDOW, m_Limits.totalDay)) return DAY_WINDOW;
			if (IncrementAndOver (windows->hour, now, HOUR_WINDOW, m_Limits.perPeerHour)) return HOUR_WINDOW;
			if (IncrementAndOver (m_TotalHour, now, HOUR_WINDOW, m_Limits.totalHour)) return HOUR_WINDOW;
			if (IncrementAndOver (windows->minute, now, MINUTE_WINDOW, m_Limits.perPeerMinute))
				return MINUTE_WINDOW;
			if (IncrementAndOver (m_TotalMinute, now, MINUTE_WINDOW, m_Limits.totalMinute))
				return MINUTE_WINDOW;
			return 0;
		}

		void Cleanup (uint64_t now)
		{
			std::lock_guard<std::mutex> lock (m_Mutex);
			for (auto it = m_Peers.begin (); it != m_Peers.end ();)
			{
				ResetIfExpired (it->second.day, now, DAY_WINDOW);
				ResetIfExpired (it->second.hour, now, HOUR_WINDOW);
				ResetIfExpired (it->second.minute, now, MINUTE_WINDOW);
				if (it->second.day.count == 0 && it->second.hour.count == 0 && it->second.minute.count == 0)
					it = m_Peers.erase (it);
				else
					++it;
			}
			ResetIfExpired (m_TotalDay, now, DAY_WINDOW);
			ResetIfExpired (m_TotalHour, now, HOUR_WINDOW);
			ResetIfExpired (m_TotalMinute, now, MINUTE_WINDOW);
		}

		size_t GetNumPeers () const
		{
			std::lock_guard<std::mutex> lock (m_Mutex);
			return m_Peers.size ();
		}

		static constexpr uint64_t MINUTE_WINDOW = 60;
		static constexpr uint64_t HOUR_WINDOW = 3600;
		static constexpr uint64_t DAY_WINDOW = 86400;
		// Bound memory under source churn. The total windows stay authoritative
		// when the peer table is full; the timer cleanup eventually frees it.
		static constexpr size_t MAX_TRACKED_PEERS = 4096;

	private:
		struct WindowCounter
		{
			uint64_t start = 0;
			uint32_t count = 0;
		};

		struct PeerWindows
		{
			WindowCounter minute, hour, day;
		};

		static void ResetIfExpired (WindowCounter & counter, uint64_t now, uint64_t window)
		{
			if (counter.count && now >= counter.start + window)
			{
				counter.start = 0;
				counter.count = 0;
			}
		}

		static bool IncrementAndOver (WindowCounter & counter, uint64_t now, uint64_t window, int limit)
		{
			if (limit <= 0) return false;
			// A clock regression starts a fresh window rather than blocking
			// every source until wall time catches up.
			if (!counter.count || now < counter.start || now >= counter.start + window)
			{
				counter.start = now;
				counter.count = 0;
			}
			++counter.count;
			return counter.count > static_cast<uint32_t> (limit);
		}

	private:
		mutable std::mutex m_Mutex;
		std::unordered_map<i2p::data::Tag<32>, PeerWindows> m_Peers;
		WindowCounter m_TotalMinute, m_TotalHour, m_TotalDay;
		StreamingConnectionLimits m_Limits;
	};
}
}

#endif
