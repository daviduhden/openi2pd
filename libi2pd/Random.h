/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Random.
 *
 * Small bounded helpers over OpenSSL's CSPRNG. They replace
 * process-global rand()/srand() so security-relevant ports, padding,
 * jitter and peer choices do not depend on deterministic libc state.
 *
 */

#ifndef I2PD_RANDOM_H__
#define I2PD_RANDOM_H__

#include <cstdint>
#include <limits>
#include <openssl/rand.h>

namespace i2p
{
namespace crypto
{
	inline uint32_t RandomUInt32 ()
	{
		uint32_t value = 0;
		if (RAND_bytes (reinterpret_cast<unsigned char *> (&value), sizeof (value)) != 1) return 0;
		return value;
	}

	inline uint32_t RandomBelow (uint32_t bound)
	{
		if (bound < 2) return 0;
		// Reject the short top interval to avoid modulo bias.
		const uint32_t threshold = (uint32_t { 0 } - bound) % bound;
		uint32_t value;
		do value = RandomUInt32 ();
		while (value < threshold);
		return value % bound;
	}
}
}

#endif
