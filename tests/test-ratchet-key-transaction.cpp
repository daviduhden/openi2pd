/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * Checks ratchet chain/index/cache transactions across allocation and
 * derivation-boundary exceptions. Faults are per-call checkpoints, not
 * global allocator or provider changes.
 */

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <new>
#include "ECIESX25519AEADRatchetSession.h"

namespace i2p::garlic
{
struct RatchetKeyTransactionTestAccess
{
	using Point = RatchetTagSet::KeyCheckpoint;
	using Key = std::array<uint8_t, 32>;
	struct State
	{
		int next;
		std::array<uint8_t, 64> chain { };
		std::map<int, Key> cached;
		bool operator== (const State &) const = default;
	};
	struct InjectedFailure final : std::bad_alloc
	{
	};
	inline static Point selected = Point::CacheNode;
	inline static unsigned remaining = 0;
	inline static bool reached = false;

	static void Checkpoint (Point point)
	{
		if (point == selected && remaining-- == 0)
		{
			reached = true;
			throw InjectedFailure { };
		}
	}
	static State Capture (const RatchetTagSet & tagset)
	{
		State state { tagset.m_NextSymmKeyIndex, { }, { } };
		// The current chain is initialized only after the first derivation.
		if (state.next) std::memcpy (state.chain.data (), tagset.m_CurrentSymmKeyCK, state.chain.size ());
		for (const auto & [index, value] : tagset.m_ItermediateSymmKeys)
			std::memcpy (state.cached[index].data (), value.data (), Key { }.size ());
		return state;
	}
	static bool Fail (RatchetTagSet & tagset, int index, Key & output, Point point, unsigned after)
	{
		selected = point;
		remaining = after;
		reached = false;
		try
		{
			tagset.GetSymmKeyImpl (index, output.data (), &Checkpoint);
		}
		catch (const InjectedFailure &)
		{
			return reached;
		}
		return false;
	}
};
}

namespace
{
using Access = i2p::garlic::RatchetKeyTransactionTestAccess;

void CheckFailure (bool warm, Access::Point point, unsigned after)
{
	std::array<uint8_t, 32> root, input;
	root.fill (0x11);
	input.fill (0x5a);
	i2p::garlic::RatchetTagSet candidate, uninterrupted;
	assert (candidate.DHInitialize (root.data (), input.data ()));
	assert (uninterrupted.DHInitialize (root.data (), input.data ()));
	Access::Key actual { }, expected { };
	if (warm)
	{
		for (int index : { 0, 3 })
		{
			assert (candidate.GetSymmKey (index, actual.data ()));
			assert (uninterrupted.GetSymmKey (index, expected.data ()));
			assert (actual == expected);
		}
	}
	const auto before = Access::Capture (candidate);
	actual.fill (0xa5);
	const auto sentinel = actual;
	assert (Access::Fail (candidate, 7, actual, point, after));
	assert (actual == sentinel);
	assert (Access::Capture (candidate) == before);
	assert (candidate.GetSymmKey (7, actual.data ()));
	assert (uninterrupted.GetSymmKey (7, expected.data ()));
	// HKDF's void API clears output on provider failure. Equal zeroed
	// results must not make the uninterrupted-reference control pass.
	assert (std::any_of (expected.begin (), expected.end (), [] (uint8_t byte) { return byte != 0; }));
	assert (actual == expected);
	assert (Access::Capture (candidate) == Access::Capture (uninterrupted));
	for (int index : { 6, 1, 4, 2, 5, 0 })
	{
		actual.fill (0x5c);
		expected = actual;
		const bool received = candidate.GetSymmKey (index, actual.data ());
		assert (received == uninterrupted.GetSymmKey (index, expected.data ()));
		assert (actual == expected);
	}
	// Preserve the existing initial-gap treatment of key zero, and the
	// one-shot semantics of all consumed intermediate keys.
	assert (!candidate.GetSymmKey (0, actual.data ()));
	assert (!candidate.GetSymmKey (1, actual.data ()));
}
}

int main ()
{
	for (bool warm : { false, true })
	{
		CheckFailure (warm, Access::Point::CacheNode, 0);
		CheckFailure (warm, Access::Point::CacheNode, 1);
		CheckFailure (warm, Access::Point::CacheReserve, 0);
		CheckFailure (warm, Access::Point::Derive, 0);
		CheckFailure (warm, Access::Point::Derive, 2);
	}
}
