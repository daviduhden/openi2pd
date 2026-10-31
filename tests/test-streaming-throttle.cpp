/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test streaming throttle.
 *
 * Checks the Java ConnThrottler/ConnectionManager-compatible per-peer
 * and total minute/hour/day admission windows, retry intervals,
 * saturation and cleanup used before a streaming SYN creates an
 * incoming stream.
 *
 */

#include <cassert>
#include <cstring>
#include "StreamingThrottle.h"

int main ()
{
	using i2p::stream::StreamingConnectionLimits;
	using i2p::stream::StreamingConnectionThrottle;

	i2p::data::Tag<32> peerA { }, peerB { }, peerC { };
	peerA ()[0] = 1;
	peerB ()[0] = 2;
	peerC ()[0] = 3;

	// Zero limits are unlimited and must not consume any window.
	StreamingConnectionThrottle unlimited;
	StreamingConnectionLimits limits;
	assert (unlimited.Check (peerA, 100) == 0);
	limits.perPeerMinute = 1;
	unlimited.SetLimits (limits);
	assert (unlimited.Check (peerA, 101) == 0);
	assert (unlimited.Check (peerA, 102) == 60);
	assert (unlimited.Check (peerA, 161) == 0); // 60-second window restarted

	// Per-peer minute window: two allowed, third rejected; the retry
	// interval
	// is 60 seconds and the fixed window restarts after it expires.
	StreamingConnectionThrottle minute;
	limits = { };
	limits.perPeerMinute = 2;
	minute.SetLimits (limits);
	assert (minute.Check (peerA, 100) == 0);
	assert (minute.Check (peerA, 101) == 0);
	assert (minute.Check (peerA, 102) == 60);
	assert (minute.Check (peerA, 160) == 0);
	assert (minute.Check (peerA, 161) == 0);
	assert (minute.Check (peerA, 162) == 60);

	// Total minute window applies across peers.
	StreamingConnectionThrottle totalMinute;
	limits = { };
	limits.totalMinute = 2;
	totalMinute.SetLimits (limits);
	assert (totalMinute.Check (peerA, 200) == 0);
	assert (totalMinute.Check (peerB, 200) == 0);
	assert (totalMinute.Check (peerC, 200) == 60);
	assert (totalMinute.Check (peerC, 260) == 0);

	// Java checks day, then hour, then minute, and returns the window
	// length.
	StreamingConnectionThrottle day;
	limits = { };
	limits.perPeerDay = 1;
	day.SetLimits (limits);
	assert (day.Check (peerA, 1000) == 0);
	assert (day.Check (peerA, 1001) == 86400);

	StreamingConnectionThrottle hour;
	limits = { };
	limits.perPeerHour = 1;
	hour.SetLimits (limits);
	assert (hour.Check (peerA, 2000) == 0);
	assert (hour.Check (peerA, 2001) == 3600);

	StreamingConnectionThrottle totalDay;
	limits = { };
	limits.totalDay = 1;
	totalDay.SetLimits (limits);
	assert (totalDay.Check (peerA, 3000) == 0);
	assert (totalDay.Check (peerB, 3000) == 86400);

	// Cleanup releases peers whose windows expired.
	StreamingConnectionThrottle cleanup;
	limits = { };
	limits.perPeerMinute = 5;
	cleanup.SetLimits (limits);
	assert (cleanup.Check (peerA, 4000) == 0);
	assert (cleanup.Check (peerB, 4000) == 0);
	assert (cleanup.GetNumPeers () == 2);
	cleanup.Cleanup (4000 + StreamingConnectionThrottle::DAY_WINDOW + 1);
	assert (cleanup.GetNumPeers () == 0);

	// A saturated peer table must not bypass the total budget. The first
	// attempt is allowed; every later attempt is rejected even when the
	// peer cannot be tracked individually.
	StreamingConnectionThrottle saturated;
	limits = { };
	limits.perPeerDay = 1000000; // track peers but do not reject them individually
	limits.totalMinute = 1;
	saturated.SetLimits (limits);
	for (size_t i = 0; i <= StreamingConnectionThrottle::MAX_TRACKED_PEERS; ++i)
	{
		i2p::data::Tag<32> peer { };
		std::memcpy (peer, &i, sizeof (i));
		if (i == 0)
			assert (saturated.Check (peer, 5000) == 0);
		else
			assert (saturated.Check (peer, 5000) == 60);
	}
	assert (saturated.GetNumPeers () == StreamingConnectionThrottle::MAX_TRACKED_PEERS);

	return 0;
}
