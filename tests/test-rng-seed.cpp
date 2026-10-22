/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test RNG seed.
 *
 * Checks that GetRngSeed() no longer returns the raw monotonic clock and
 * produces independent seeds for protocol-visible choices. Prepared for
 * manual CI; no local build is implied.
 *
 */

#include <cassert>
#include <cstdint>
#include <set>

#include "util.h"

int main ()
{
	std::set<uint_fast32_t> seeds;
	for (int i = 0; i < 512; ++i)
		seeds.insert (i2p::util::GetRngSeed ());
	// A clock-seeded generator produced identical values when two callers
	// sampled the same microsecond. CSPRNG-derived seeds must not collapse.
	assert (seeds.size () == 512);
	return 0;
}
