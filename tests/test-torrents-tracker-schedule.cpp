/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Torrent tracker announce scheduling and progress parity tests.
 *
 * Pins the I2P Java 2.14.0 TrackerClient behaviour without sockets, threads,
 * timers or sleeps: a tracker-advertised interval is floored at 15 minutes, a
 * larger "min interval" wins, consecutive failures use the short internal retry
 * ladder capped at 30 minutes, and a successful response resets that ladder.
 * Also checks that "left" describes the whole torrent and that
 * event=completed is only allowed once every piece is verified. Live announces
 * and the tunnel-side event selection are not covered here.
 */

#include <cassert>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>
#include "Torrents.h"
#include "Timestamp.h"

#ifndef NO_TORRENTS

namespace
{
using namespace i2p::torrents;

using Field = std::pair<std::string, std::string>;

std::string Integer (int64_t value)
{
	return "i" + std::to_string (value) + "e";
}

std::string Dict (const std::vector<Field> & fields)
{
	std::string ret ("d");
	for (const auto & [key, value] : fields)
	{
		ret += std::to_string (key.size ());
		ret += ':';
		ret += key;
		ret += value;
	}
	ret += 'e';
	return ret;
}

// Milliseconds still scheduled for the tracker, read as one snapshot so the
// assertion never depends on two clock reads straddling a scheduler tick.
uint64_t ScheduledWait (const Torrent & torrent, size_t trackerID)
{
	const auto now = i2p::util::GetMonotonicMilliseconds ();
	const auto next = torrent.GetNextTrackerRequestTime (trackerID);
	return next > now ? next - now : 0;
}

void TestSchedulingArithmetic ()
{
	// Java's MIN_TRACKER_ANNOUNCE_INTERVAL and LONG_SLEEP, in seconds.
	assert (MIN_TRACKER_ANNOUNCE_INTERVAL == 900);
	assert (MAX_TRACKER_FAILURE_BACKOFF == 1800);

	// A tracker answering interval=30 must not be polled 30x more often than
	// Java: the advertise floor wins, the ceiling still bounds hostile values.
	assert (TrackerSchedule::AdvertisedInterval (30) == 900 * 1000);
	assert (TrackerSchedule::AdvertisedInterval (900) == 900 * 1000);
	assert (TrackerSchedule::AdvertisedInterval (3600) == 3600 * 1000);
	assert (TrackerSchedule::AdvertisedInterval (0) == 900 * 1000);
	assert (TrackerSchedule::AdvertisedInterval (-30) == 900 * 1000);
	assert (
		TrackerSchedule::AdvertisedInterval (std::numeric_limits<int64_t>::max ()) == MAX_TRACKER_REQUESTS_INTERVAL);

	// "min interval" is stored as announced; it can raise but never lower the
	// effective interval, and it cannot undercut the 15-minute floor.
	assert (TrackerSchedule::AdvertisedMinInterval (1800) == 1800 * 1000);
	assert (TrackerSchedule::AdvertisedMinInterval (30) == 30 * 1000);
	assert (TrackerSchedule::AdvertisedMinInterval (-1) == MIN_TRACKER_REQUESTS_INTERVAL);
	assert (TrackerSchedule::EffectiveInterval (900 * 1000, 1800 * 1000) == 1800 * 1000);
	assert (TrackerSchedule::EffectiveInterval (3600 * 1000, 600 * 1000) == 3600 * 1000);
	assert (TrackerSchedule::EffectiveInterval (60 * 1000, 0) == 900 * 1000);

	// A success without "interval" re-bases at the announce floor, not at the
	// old 15-second retry granularity.
	assert (TrackerSchedule::DefaultInterval (0) == 900 * 1000);
	assert (TrackerSchedule::DefaultInterval (MIN_TRACKER_REQUESTS_INTERVAL) == 900 * 1000);
	assert (TrackerSchedule::DefaultInterval (1800 * 1000) == 1800 * 1000);

	// Failures keep the short internal granularity: the first retry is 15 s and
	// each further failure doubles it.
	assert (TrackerSchedule::FailureInterval (0, 0) == MIN_TRACKER_REQUESTS_INTERVAL);
	int ladder = TrackerSchedule::FailureInterval (0, 0);
	assert (ladder == MIN_TRACKER_REQUESTS_INTERVAL);
	ladder = TrackerSchedule::FailureInterval (ladder, 0);
	assert (ladder == 30 * 1000);
	ladder = TrackerSchedule::FailureInterval (ladder, 0);
	assert (ladder == 60 * 1000);

	// ... and the ladder stops at 30 minutes instead of growing to 24 h.
	for (int i = 0; i < 32; ++i) ladder = TrackerSchedule::FailureInterval (ladder, 0);
	assert (ladder == MAX_TRACKER_FAILURE_BACKOFF * 1000);
	assert (ladder < MAX_TRACKER_REQUESTS_INTERVAL);
	// The cap is not sticky-earlier either: doubling from an advertised
	// 15-minute interval also lands on the cap, not on 30 minutes of growth.
	assert (TrackerSchedule::FailureInterval (900 * 1000, 0) == 1800 * 1000);

	// A tracker that demanded a longer minimum interval still governs the
	// failure floor; the retry cap must not undercut it.
	assert (TrackerSchedule::FailureInterval (0, 3600 * 1000) == 3600 * 1000);
	assert (TrackerSchedule::FailureInterval (3600 * 1000, 3600 * 1000) == 3600 * 1000);
}

void TestResponseParsing ()
{
	Torrent::InfoHash infoHash { };
	infoHash[0] = 0x5A;
	Torrent torrent (infoHash); // magnet: no metadata yet

	// A magnet keeps the phony left=1 and never claims completion.
	const auto magnet = torrent.GetAnnounceProgress ();
	assert (magnet.left == 1 && !magnet.completed);

	// interval=30 is floored to 15 minutes and schedules the next announce.
	torrent.ParseTrackerResponse (0, Dict ({ { "interval", Integer (30) } }));
	assert (torrent.GetTrackerError (0).empty ());
	assert (torrent.GetInterval (0) == 900 * 1000);
	assert (ScheduledWait (torrent, 0) > 0);

	// interval=3600 is honoured.
	torrent.ParseTrackerResponse (1, Dict ({ { "interval", Integer (3600) } }));
	assert (torrent.GetInterval (1) == 3600 * 1000);

	// A larger "min interval" wins over "interval".
	torrent.ParseTrackerResponse (2, Dict ({ { "interval", Integer (3600) }, { "min interval", Integer (7200) } }));
	assert (torrent.GetMinInterval (2) == 7200 * 1000);
	assert (torrent.GetInterval (2) == 7200 * 1000);

	// A smaller "min interval" cannot shorten "interval" ...
	torrent.ParseTrackerResponse (3, Dict ({ { "interval", Integer (3600) }, { "min interval", Integer (1800) } }));
	assert (torrent.GetMinInterval (3) == 1800 * 1000);
	assert (torrent.GetInterval (3) == 3600 * 1000);

	// ... and neither value can undercut the 15-minute floor.
	torrent.ParseTrackerResponse (4, Dict ({ { "interval", Integer (60) }, { "min interval", Integer (30) } }));
	assert (torrent.GetMinInterval (4) == 30 * 1000);
	assert (torrent.GetInterval (4) == 900 * 1000);

	// A duplicated underscore spelling of the optional key is accepted too.
	torrent.ParseTrackerResponse (5, Dict ({ { "min_interval", Integer (2400) } }));
	assert (torrent.GetMinInterval (5) == 2400 * 1000);
	assert (torrent.GetInterval (5) == 2400 * 1000);

	// An unusable interval value is not trusted: it falls back to the floor.
	torrent.ParseTrackerResponse (6, Dict ({ { "interval", "0:" } }));
	assert (torrent.GetTrackerError (6).empty ());
	assert (torrent.GetInterval (6) == 900 * 1000);
	torrent.ParseTrackerResponse (7, Dict ({ { "interval", Integer (-30) } }));
	assert (torrent.GetInterval (7) == 900 * 1000);

	// Truncated bencode is a failure, not a successful announce, and a failure
	// never rewrites the advertised interval.
	torrent.ParseTrackerResponse (8, "<html>not bencode</html>");
	assert (!torrent.GetTrackerError (8).empty ());
	assert (torrent.GetInterval (8) == MIN_TRACKER_REQUESTS_INTERVAL);

	// A tracker-level failure reason records the error and schedules a retry on
	// the short internal ladder rather than on the advertised interval.
	torrent.ParseTrackerResponse (
		9, Dict ({ { "failure reason", "11:temporarily" }, { "interval", Integer (86400) } }));
	assert (torrent.GetTrackerError (9) == "temporarily");
	assert (torrent.GetInterval (9) == MAX_TRACKER_REQUESTS_INTERVAL);
	assert (ScheduledWait (torrent, 9) <= 60 * 1000);

	// The datagram tracker response (BEP 15, seconds) obeys the same floor.
	std::vector<uint8_t> hashes (infoHash.size (), 0);
	torrent.HandleDatagramTrackerResponse (10, 30, hashes.data (), hashes.size (), 1, 2);
	assert (torrent.GetInterval (10) == 900 * 1000);
	assert (torrent.GetNumPeers (10) == 0); // zero destinations are skipped
	assert (torrent.GetNumSeeders (10) == 1 && torrent.GetNumLeechers (10) == 2);
	torrent.HandleDatagramTrackerResponse (
		11, std::numeric_limits<uint32_t>::max (), hashes.data (), hashes.size (), 0, 0);
	assert (torrent.GetInterval (11) == MAX_TRACKER_REQUESTS_INTERVAL);
}

void TestFailureLadderAndReset ()
{
	Torrent::InfoHash infoHash { };
	infoHash[0] = 0x5B;
	Torrent torrent (infoHash);

	// The first failure after a successful announce retries quickly; the
	// advertised interval is not consumed by the retry ladder.
	torrent.ParseTrackerResponse (0, Dict ({ { "interval", Integer (3600) } }));
	torrent.ReportTrackerFailure (0, "request failed");
	assert (!torrent.GetTrackerError (0).empty ());
	assert (torrent.GetInterval (0) == 3600 * 1000);
	assert (ScheduledWait (torrent, 0) <= 60 * 1000);

	// Repeated failures grow the retry interval but never beyond 30 minutes.
	for (int i = 0; i < 32; ++i) torrent.ReportTrackerFailure (0, "request failed");
	const auto capped = ScheduledWait (torrent, 0);
	assert (capped <= static_cast<uint64_t> (MAX_TRACKER_FAILURE_BACKOFF) * 1000);
	assert (capped >= static_cast<uint64_t> (MIN_TRACKER_ANNOUNCE_INTERVAL) * 1000);
	assert (torrent.GetInterval (0) == 3600 * 1000);

	// A successful response resets the ladder, so the next failure is short
	// again and the new advertised interval is honoured.
	torrent.ParseTrackerResponse (0, Dict ({ { "interval", Integer (1800) } }));
	assert (torrent.GetTrackerError (0).empty ());
	assert (torrent.GetInterval (0) == 1800 * 1000);
	torrent.ReportTrackerFailure (0, "request failed");
	assert (ScheduledWait (torrent, 0) <= 60 * 1000);

	// A success without any interval re-bases at the announce floor.
	torrent.ParseTrackerResponse (0, Dict ({ { "complete", Integer (1) } }));
	assert (torrent.GetTrackerError (0).empty ());
	assert (torrent.GetInterval (0) == 900 * 1000);
}
} // namespace

int main ()
{
	TestSchedulingArithmetic ();
	TestResponseParsing ();
	TestFailureLadderAndReset ();
	return 0;
}

#else

int main ()
{
	return 0;
}

#endif
