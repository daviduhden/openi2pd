/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Torrent tracker dispatch, registry and DHT privacy contracts.
 *
 * Pins the announce policy that the torrent tunnel and the public DHT rely on,
 * without a service, socket, timer, thread or sleep:
 *  - a dispatched request never overwrites the silent-tracker failure ladder,
 *  - the advertised announce interval stays floored at 15 minutes,
 *  - tracker registry slots are reference counted, reused lowest-free-first and
 *    capped at MAX_NUM_TRACKERS without renumbering a live entry,
 *  - a free (unreferenced, non-common) slot is not live and cannot match,
 *  - tracker announces are single-flight per (torrent, slot) and bounded,
 *  - the private info-hash set admits, deduplicates, erases and evicts oldest.
 *
 * Not covered here, because it needs the tunnel service thread or a live KRPC
 * peer: the HTTP announce deadline timer and its stream close/cancel paths, the
 * DHT peer/token purge performed by TorrentsDHT::MarkPrivate()/Forget(), and
 * inbound get_peers/announce_peer refusal. Those remain manual-CI integration
 * work; this file only exercises the pure policy seams and the registry API
 * that is synchronous by construction.
 */

#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "Torrents.h"
#include "TorrentsDHT.h"
#include "TorrentsTunnel.h"
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

Torrent::InfoHash MakeHash (uint32_t value)
{
	Torrent::InfoHash hash { };
	hash[0] = static_cast<uint8_t> (value >> 24);
	hash[1] = static_cast<uint8_t> (value >> 16);
	hash[2] = static_cast<uint8_t> (value >> 8);
	hash[3] = static_cast<uint8_t> (value);
	// Non-zero tail: hash 0 must not collide with a default-initialised array.
	hash[hash.size () - 1] = 0xA5;
	return hash;
}

void TestAdvertisedIntervalFloor ()
{
	assert (MIN_TRACKER_ANNOUNCE_INTERVAL == 900);
	assert (MIN_TRACKER_REQUESTS_INTERVAL == 15 * 1000);

	const uint64_t now = 5'000'000;
	// A tracker that advertises 30 s is still polled at the 15-minute floor.
	assert (NextTrackerDispatchTime (now, false, 0, 30 * 1000, MIN_TRACKER_REQUESTS_INTERVAL, 0)
		== now + static_cast<uint64_t> (MIN_TRACKER_ANNOUNCE_INTERVAL) * 1000);
	// A hostile zero/negative interval is a floor, never a spin.
	assert (NextTrackerDispatchTime (now, false, 0, 0, MIN_TRACKER_REQUESTS_INTERVAL, 0)
		== now + 900 * 1000);
	// A larger advertised interval is honoured, as before this change.
	assert (NextTrackerDispatchTime (now, false, 0, 3600 * 1000, MIN_TRACKER_REQUESTS_INTERVAL, 0)
		== now + 3600 * 1000);
	// Jitter is added on top of a healthy interval only.
	assert (NextTrackerDispatchTime (now, false, 0, 900 * 1000, 15000, 2999) == now + 900 * 1000 + 2999);
}

void TestSilentTrackerKeepsFailureLadder ()
{
	// A tracker that advertised an hour and then went silent.
	Torrent torrent (MakeHash (0x5C));
	torrent.ParseTrackerResponse (0, Dict ({ { "interval", Integer (3600) } }));
	assert (torrent.GetTrackerError (0).empty ());
	assert (torrent.GetInterval (0) == 3600 * 1000);

	// The retry tick reports the failure exactly like
	// HandleTrackerRequestsCheckTimer() does (guarded by an empty error), and
	// then dispatches a request. The dispatch must not replace the ladder retry
	// with the advertised interval.
	const uint64_t now = i2p::util::GetMonotonicMilliseconds ();
	torrent.ReportTrackerFailure (0, "No response");
	assert (!torrent.GetTrackerError (0).empty ());
	const uint64_t ladderRetry = torrent.GetNextTrackerRequestTime (0);
	assert (ladderRetry > now);

	const uint64_t next = NextTrackerDispatchTime (
		now, !torrent.GetTrackerError (0).empty (), ladderRetry, torrent.GetInterval (0),
		torrent.GetMinInterval (0), 0);
	assert (next == ladderRetry);
	assert (next < now + static_cast<uint64_t> (torrent.GetInterval (0)));
	assert (next - now <= static_cast<uint64_t> (MIN_TRACKER_REQUESTS_INTERVAL) + 1000);

	// Repeated failures escalate the ladder, and the dispatch decision follows
	// the escalated value up to the 30-minute cap.
	for (int i = 0; i < 8; i++) torrent.ReportTrackerFailure (0, "No response");
	const uint64_t escalatedRetry = torrent.GetNextTrackerRequestTime (0);
	const uint64_t escalatedNow = i2p::util::GetMonotonicMilliseconds ();
	const uint64_t escalatedNext = NextTrackerDispatchTime (
		escalatedNow, true, escalatedRetry, torrent.GetInterval (0), torrent.GetMinInterval (0), 0);
	assert (escalatedNext == escalatedRetry);
	assert (escalatedNext - escalatedNow <= static_cast<uint64_t> (MAX_TRACKER_FAILURE_BACKOFF) * 1000);
	assert (escalatedNext - escalatedNow > static_cast<uint64_t> (MIN_TRACKER_REQUESTS_INTERVAL));

	// Jitter may not push a retry past the failure cap.
	const uint64_t capped = NextTrackerDispatchTime (now, true,
		now + static_cast<uint64_t> (MAX_TRACKER_FAILURE_BACKOFF) * 1000, torrent.GetInterval (0),
		torrent.GetMinInterval (0), 2999);
	assert (capped == now + static_cast<uint64_t> (MAX_TRACKER_FAILURE_BACKOFF) * 1000);

	// An already-expired retry time still waits a full ladder floor instead of
	// re-announcing immediately on the next tick.
	assert (NextTrackerDispatchTime (now, true, now - 1, 3600 * 1000, 15000, 0)
		== now + static_cast<uint64_t> (MIN_TRACKER_REQUESTS_INTERVAL));

	// A successful response clears the error, so the next decision is the
	// advertised interval again.
	torrent.ParseTrackerResponse (0, Dict ({ { "interval", Integer (1800) } }));
	assert (torrent.GetTrackerError (0).empty ());
	assert (NextTrackerDispatchTime (now, false, 0, torrent.GetInterval (0), torrent.GetMinInterval (0), 0)
		== now + 1800 * 1000);
}

void TestSlotReuseAndReferenceCounting ()
{
	TrackerSlots slots;

	// Two registrations append two slots (the caller initialises them, exactly
	// like TorrentsTunnel::ResolveTrackerSlot()).
	auto first = AdmitTrackerSlot (slots, std::nullopt);
	assert (first.trackerID && *first.trackerID == 0 && !first.existing);
	slots.emplace_back (TrackerSlotState { });
	AcquireTrackerSlotReference (slots, 0);
	auto second = AdmitTrackerSlot (slots, std::nullopt);
	assert (second.trackerID && *second.trackerID == 1 && !second.existing);
	slots.emplace_back (TrackerSlotState { });
	AcquireTrackerSlotReference (slots, 1);
	assert (slots.size () == 2 && slots[0].references == 1 && slots[1].references == 1);

	// An identical announce URL resolves to its existing slot: no new slot is
	// consumed and the reference count carries the second membership.
	auto duplicate = AdmitTrackerSlot (slots, std::optional<size_t> { 0 });
	assert (duplicate.trackerID && *duplicate.trackerID == 0 && duplicate.existing);
	AcquireTrackerSlotReference (slots, 0);
	assert (slots.size () == 2 && slots[0].references == 2);

	// One holder leaving keeps the slot live and matchable.
	assert (ReleaseTrackerSlotReference (slots, 0) == TrackerSlotRelease::Retained);
	assert (IsTrackerSlotLive (slots, 0));
	assert (slots[0].references == 1);

	// The last holder frees it: the slot must be ignored by membership, so the
	// production caller clears its announce URL at this point.
	assert (ReleaseTrackerSlotReference (slots, 0) == TrackerSlotRelease::Freed);
	assert (!IsTrackerSlotLive (slots, 0));
	// A second release must not underflow the count.
	assert (ReleaseTrackerSlotReference (slots, 0) == TrackerSlotRelease::Invalid);
	// Releasing an out-of-range slot is rejected, never indexed.
	assert (ReleaseTrackerSlotReference (slots, 99) == TrackerSlotRelease::Invalid);

	// A new announce URL reuses the lowest free slot instead of appending, and
	// keeps the slot stable for every other live entry.
	assert (FindReusableTrackerSlot (slots) == std::optional<size_t> { 0 });
	auto reused = AdmitTrackerSlot (slots, std::nullopt);
	assert (reused.trackerID && *reused.trackerID == 0 && !reused.existing);
	slots[0] = TrackerSlotState { }; // re-initialised: no stale common flag/refcount
	AcquireTrackerSlotReference (slots, 0);
	assert (slots.size () == 2 && IsTrackerSlotLive (slots, 0) && IsTrackerSlotLive (slots, 1));
}

void TestCommonSlotsAreNeverRecycled ()
{
	TrackerSlots slots (1, TrackerSlotState { 0, true });
	assert (IsTrackerSlotLive (slots, 0)); // configured common trackers live forever
	assert (!FindReusableTrackerSlot (slots));

	// A common slot cannot be freed by a reference drop ...
	AcquireTrackerSlotReference (slots, 0);
	assert (ReleaseTrackerSlotReference (slots, 0) == TrackerSlotRelease::Retained);
	assert (IsTrackerSlotLive (slots, 0));
	// ... so a new URL appends after it instead of taking its index.
	auto admitted = AdmitTrackerSlot (slots, std::nullopt);
	assert (admitted.trackerID && *admitted.trackerID == 1 && !admitted.existing);
	assert (slots[0].isCommon && !slots[0].references);
}

void TestRegistryExhaustion ()
{
	assert (MAX_NUM_TRACKERS == 512);
	TrackerSlots slots (MAX_NUM_TRACKERS, TrackerSlotState { 1, false });
	assert (!FindReusableTrackerSlot (slots));

	// Every slot live: a new announce URL is refused, never appended past the cap
	// and never allowed to evict a registered tracker.
	const auto rejected = AdmitTrackerSlot (slots, std::nullopt);
	assert (!rejected.trackerID && !rejected.existing);

	// One free slot is enough to admit again, at the lowest free index.
	slots[17].references = 0;
	assert (FindReusableTrackerSlot (slots) == std::optional<size_t> { 17 });
	const auto admitted = AdmitTrackerSlot (slots, std::nullopt);
	assert (admitted.trackerID && *admitted.trackerID == 17 && !admitted.existing);

	// A table of configured common trackers is full for a new URL too.
	TrackerSlots common (MAX_NUM_TRACKERS, TrackerSlotState { 0, true });
	assert (!FindReusableTrackerSlot (common));
	assert (!AdmitTrackerSlot (common, std::nullopt).trackerID);
}

void TestTrackerRegistryApi ()
{
	// The registry is synchronous without a service: construction and AddTracker
	// only touch the slot table, so the real API can be pinned here.
	TorrentsTunnel tunnel ("tracker-registry-test", nullptr, "/unused-authority", std::string_view { }, false);
	assert (tunnel.GetNumTrackers () == 0);

	// Fill the registry through the public API: every distinct I2P announce URL
	// takes the next appended index until the cap is reached.
	for (size_t i = 0; i < MAX_NUM_TRACKERS; i++)
	{
		const std::string announce = "http://tracker" + std::to_string (i) + ".i2p/announce";
		assert (tunnel.AddTracker (announce, false) == static_cast<int> (i));
	}
	assert (tunnel.GetNumTrackers () == MAX_NUM_TRACKERS);
	assert (tunnel.GetTrackerAnnounce (7) == "http://tracker7.i2p/announce");

	// An identical announce URL resolves to its existing slot and only raises the
	// reference count: the table does not grow.
	assert (tunnel.AddTracker ("http://tracker7.i2p/announce", false) == 7);
	assert (tunnel.GetNumTrackers () == MAX_NUM_TRACKERS);

	// The table is full of live (referenced) slots, so a new URL is refused with
	// -1 instead of evicting a registered tracker or growing past the cap.
	assert (tunnel.AddTracker ("http://tracker-overflow.i2p/announce", false) == -1);
	assert (tunnel.GetNumTrackers () == MAX_NUM_TRACKERS);

	// Non-I2P and control-character announces are refused before any slot is
	// allocated; out-of-range ids read back as an empty announce.
	assert (tunnel.AddTracker ("http://clearnet.example.com/announce", false) == -1);
	assert (tunnel.AddTracker ("http://bad\nhost.i2p/announce", false) == -1);
	assert (tunnel.AddTracker (std::string_view { }, false) == -1);
	assert (tunnel.GetTrackerAnnounce (MAX_NUM_TRACKERS).empty ());
	assert (tunnel.GetTrackerAnnounce (static_cast<size_t> (-1)).empty ());
}

void TestInflightAdmission ()
{
	assert (MAX_INFLIGHT_TRACKER_REQUESTS == 16);
	using Registry = TrackerInflightRegistry<int>;
	Registry inflight;
	const auto infoHash = MakeHash (0x11);
	const auto otherHash = MakeHash (0x22);

	const TrackerRequestKey key { infoHash, 3 };
	assert (inflight.TryAcquire (key, 1) == Registry::Admission::Accepted);
	assert (inflight.Size () == 1 && inflight.Contains (key));
	// A duplicate is refused and never replaces the admitted payload.
	assert (inflight.TryAcquire (key, 2) == Registry::Admission::Duplicate);
	assert (inflight.Find (key) && *inflight.Find (key) == 1);
	assert (inflight.Release (key));
	assert (!inflight.Contains (key) && inflight.Size () == 0);
	assert (!inflight.Release (key)); // idempotent release

	// The cap is exact: sixteen keys across torrents, then refusal.
	for (size_t i = 0; i < MAX_INFLIGHT_TRACKER_REQUESTS; i++)
		assert (inflight.TryAcquire (TrackerRequestKey { infoHash, i }) == Registry::Admission::Accepted);
	assert (inflight.Size () == MAX_INFLIGHT_TRACKER_REQUESTS);
	assert (inflight.TryAcquire (TrackerRequestKey { otherHash, 0 }) == Registry::Admission::Full);
	assert (inflight.Size () == MAX_INFLIGHT_TRACKER_REQUESTS);

	// A torrent-scoped release drops only that torrent's keys and reports them,
	// so the caller can retire its own timers and streams.
	const auto released = inflight.ReleaseTorrent (infoHash);
	assert (released.size () == MAX_INFLIGHT_TRACKER_REQUESTS);
	assert (inflight.Size () == 0);
	// Releasing a torrent without requests is a no-op.
	assert (inflight.ReleaseTorrent (otherHash).empty ());

	inflight.TryAcquire (TrackerRequestKey { otherHash, 1 }, 9);
	inflight.TryAcquire (TrackerRequestKey { infoHash, 1 }, 9);
	assert (inflight.ReleaseAll ().size () == 2 && inflight.Size () == 0);
}

void TestPrivateInfoHashSet ()
{
	assert (PrivateInfoHashSet::MaxHashes == MAX_DHT_TORRENTS);
	PrivateInfoHashSet hashes;
	const auto privateHash = MakeHash (0x33);

	assert (hashes.Insert (privateHash) == PrivateInfoHashSet::Admission::Inserted);
	assert (hashes.Contains (privateHash) && hashes.Size () == 1);
	// Re-admitting the same hash is a no-op, not a second entry.
	assert (hashes.Insert (privateHash) == PrivateInfoHashSet::Admission::AlreadyKnown);
	assert (hashes.Size () == 1);
	assert (hashes.Erase (privateHash) && !hashes.Contains (privateHash) && hashes.Size () == 0);
	assert (!hashes.Erase (privateHash));

	// Admission is bounded: filling the set evicts exactly the oldest entry and
	// keeps the newest, so the size never exceeds the cap.
	for (size_t i = 0; i < PrivateInfoHashSet::MaxHashes; i++)
		assert (hashes.Insert (MakeHash (static_cast<uint32_t> (i))) == PrivateInfoHashSet::Admission::Inserted);
	assert (hashes.Size () == PrivateInfoHashSet::MaxHashes);
	const auto oldest = MakeHash (0);
	const auto newest = MakeHash (static_cast<uint32_t> (PrivateInfoHashSet::MaxHashes));
	assert (hashes.Contains (oldest) && !hashes.Contains (newest));
	assert (hashes.Insert (newest) == PrivateInfoHashSet::Admission::EvictedOldest);
	assert (hashes.Size () == PrivateInfoHashSet::MaxHashes);
	assert (!hashes.Contains (oldest) && hashes.Contains (newest));
}

void TestDHTPrivacyApi ()
{
	TorrentsTunnel tunnel ("dht-privacy-test", nullptr, "/unused-authority", std::string_view { }, false);
	TorrentsDHT dht (tunnel, 4242, std::string_view { });
	const auto privateHash = MakeHash (0x44);
	const auto publicHash = MakeHash (0x55);

	// A private hash is refused by every DHT entry point once marked. Here the
	// served peer map and the token table are empty, so the purge itself is not
	// observable without a live KRPC peer; the admission and the retained marker
	// are pinned below and the purge stays a manual-CI integration check.
	dht.MarkPrivate (privateHash);
	assert (dht.IsLocallyPrivate (privateHash));
	assert (!dht.IsLocallyPrivate (publicHash));
	assert (dht.GetNumPrivateInfoHashes () == 1);
	assert (dht.GetNumTorrents () == 0 && dht.GetNumIncomingTokens () == 0);
	// MarkPrivate is idempotent for the bounded set.
	dht.MarkPrivate (privateHash);
	assert (dht.GetNumPrivateInfoHashes () == 1);

	// Forget() drops the DHT state of a removed torrent but keeps the private
	// marker: a removed private hash must not become servable again.
	dht.Forget (privateHash);
	assert (dht.IsLocallyPrivate (privateHash));
	dht.Forget (publicHash); // no state, no crash
	assert (dht.GetNumTorrents () == 0);
}

} // namespace

int main ()
{
	TestAdvertisedIntervalFloor ();
	TestSilentTrackerKeepsFailureLadder ();
	TestSlotReuseAndReferenceCounting ();
	TestCommonSlotsAreNeverRecycled ();
	TestRegistryExhaustion ();
	TestTrackerRegistryApi ();
	TestInflightAdmission ();
	TestPrivateInfoHashSet ();
	TestDHTPrivacyApi ();
	return 0;
}

#else

int main ()
{
	return 0;
}

#endif
