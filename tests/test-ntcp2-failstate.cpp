/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test ntcp2 failstate.
 *
 * Pins the socket-free parts of the NTCP2 data-phase failure policy:
 * the bounded AEAD-failure drain arithmetic, the ordering rule that
 * nothing may be written before the drain completes or times out, and
 * the termination reasons the session must use (4, 14 and 7). No
 * socket, thread or sleep is used.
 *
 */

#include <cassert>
#include <cstdint>

#include "NTCP2.h"

namespace
{
using i2p::transport::NTCP2FailureDrain;

void CheckDrainArithmetic ()
{
	using namespace i2p::transport;
	static_assert (NTCP2_AEAD_FAILURE_DRAIN_MIN_SIZE == 18, "spec/ntcp2 drain window starts at 18 bytes");
	static_assert (NTCP2_AEAD_FAILURE_DRAIN_MAX_SIZE == 1041, "18 + 1023 is the largest drain");
	static_assert (NTCP2_AEAD_FAILURE_DRAIN_MAX_SIZE - NTCP2_AEAD_FAILURE_DRAIN_MIN_SIZE + 1 == 1024,
		"the drain window must cover exactly the 1024 residual values");
	static_assert (NTCP2_AEAD_FAILURE_DRAIN_TIMEOUT == 10, "the drain deadline is 10 seconds");
	// The intra-frame deadline must stay below the 120 s idle termination
	// so an established session cannot be closed as idle before this
	// deadline fires.
	static_assert (NTCP2_INTRA_FRAME_READ_TIMEOUT > 0);
	static_assert (NTCP2_INTRA_FRAME_READ_TIMEOUT < NTCP2_TERMINATION_TIMEOUT);

	// Uniform in [18, 1041]: every residual maps into the window, the
	// extreme values are reachable, and nothing depends on the width of
	// the RNG value.
	static_assert (NTCP2GetAEADFailureDrainSize (0) == 18);
	static_assert (NTCP2GetAEADFailureDrainSize (1) == 19);
	static_assert (NTCP2GetAEADFailureDrainSize (1023) == 1041);
	static_assert (NTCP2GetAEADFailureDrainSize (1024) == 18); // one full window later

	bool seen[NTCP2_AEAD_FAILURE_DRAIN_MAX_SIZE + 1] = { };
	for (uint32_t rng = 0; rng < 1024; ++rng)
	{
		const size_t size = NTCP2GetAEADFailureDrainSize (rng);
		assert (size >= NTCP2_AEAD_FAILURE_DRAIN_MIN_SIZE);
		assert (size <= NTCP2_AEAD_FAILURE_DRAIN_MAX_SIZE);
		assert (!seen[size]);
		seen[size] = true;
	}
	for (size_t size = NTCP2_AEAD_FAILURE_DRAIN_MIN_SIZE; size <= NTCP2_AEAD_FAILURE_DRAIN_MAX_SIZE; ++size)
		assert (seen[size]);

	// Large RNG values must not overflow into an unbounded drain: the
	// request stays inside the fixed buffer.
	const uint32_t extremes[] = { 1024u, 0xFFFFu, 0x00800000u, 0xFFFFFFFFu };
	for (uint32_t rng : extremes)
	{
		const size_t size = NTCP2GetAEADFailureDrainSize (rng);
		assert (size >= NTCP2_AEAD_FAILURE_DRAIN_MIN_SIZE);
		assert (size <= NTCP2_AEAD_FAILURE_DRAIN_MAX_SIZE);
	}
}

void CheckTerminationReasons ()
{
	using namespace i2p::transport;
	// Values are the NTCP2 reason codes from spec/ntcp2 Termination block.
	static_assert (eNTCP2NormalClose == 0);
	static_assert (eNTCP2TerminationReceived == 1);
	static_assert (eNTCP2IdleTimeout == 2);
	static_assert (eNTCP2RouterShutdown == 3);
	static_assert (eNTCP2DataPhaseAEADFailure == 4);
	static_assert (eNTCP2ClockSkew == 7);
	static_assert (eNTCP2PayloadFormatError == 10);
	static_assert (eNTCP2IntraFrameReadTimeout == 14);
	static_assert (eNTCP2Banned == 17);
}

void CheckDrainOrdering ()
{
	using namespace i2p::transport;

	// A fresh state may write.
	NTCP2FailureDrain drain;
	assert (drain.CanWrite ());
	assert (!drain.IsDraining ());

	// Only the bounded window is accepted; an out-of-range request must
	// fail closed instead of allocating or reading an arbitrary amount.
	assert (!drain.Begin (0));
	assert (!drain.Begin (NTCP2_AEAD_FAILURE_DRAIN_MIN_SIZE - 1));
	assert (!drain.Begin (NTCP2_AEAD_FAILURE_DRAIN_MAX_SIZE + 1));
	assert (!drain.IsDraining ());
	assert (drain.CanWrite ());

	// While the drain is outstanding nothing may be written, partial
	// progress
	// does not authorize a write, and a second failure cannot restart it.
	const size_t drained = 100;
	assert (drain.Begin (drained));
	assert (drain.IsDraining ());
	assert (!drain.CanWrite ());
	assert (drain.GetRemaining () == drained);
	assert (!drain.Begin (drained));
	assert (!drain.Consume (drained - 1));
	assert (drain.IsDraining ());
	assert (!drain.CanWrite ());
	assert (drain.GetRemaining () == 1);

	// Completion allows exactly one Termination write.
	assert (drain.Consume (1));
	assert (!drain.IsDraining ());
	assert (drain.CanWrite ());
	assert (drain.GetRemaining () == 0);
	// Consuming again neither reopens nor resets the drained state.
	assert (!drain.Consume (1));
	assert (drain.CanWrite ());

	// Overshooting the requested count also completes the drain.
	assert (drain.Begin (NTCP2_AEAD_FAILURE_DRAIN_MAX_SIZE));
	assert (!drain.CanWrite ());
	assert (drain.Consume (NTCP2_AEAD_FAILURE_DRAIN_MAX_SIZE));
	assert (drain.CanWrite ());

	// The deadline ends the drain with the same write permission and drops
	// the remaining count, so a peer that stops sending is handled
	// identically.
	assert (drain.Begin (drained));
	assert (drain.Expire ());
	assert (!drain.IsDraining ());
	assert (drain.CanWrite ());
	assert (drain.GetRemaining () == 0);
	assert (!drain.Expire ()); // idempotent, and never a second write permission

	// Terminate() resets the state; the closed session, not the drain, is
	// what blocks further writes afterwards.
	assert (drain.Begin (drained));
	drain.Reset ();
	assert (!drain.IsDraining ());
	assert (drain.CanWrite ());
}

void CheckRouterInfoFloodBound ()
{
	// Java NTCP2Payload only forwards a request-flood RouterInfo below 3
	// KiB.
	static_assert (i2p::transport::NTCP2_ROUTER_INFO_MAX_FLOOD_SIZE == 3 * 1024);
}
}

int main ()
{
	CheckDrainArithmetic ();
	CheckTerminationReasons ();
	CheckDrainOrdering ();
	CheckRouterInfoFloodBound ();
	return 0;
}
