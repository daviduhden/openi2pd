/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: vanity.
 *
 * Provides the SHA-256 compression primitives, identity/nonce offsets and
 * fixed sizes used by the vanity nonce search. It contains no mutable state
 * and preserves the upstream key format.
 *
 */

#pragma once

// SHA-256 primitives and constants used by the vanity generator. The nonce
// search, the hashing steps and the persisted key format are unchanged from
// the upstream tool; this header deliberately contains no mutable state.

#include <array>
#include <cstddef>
#include <cstdint>

// sha256
#define Ch(x, y, z) ((x & (y ^ z)) ^ z)
#define Maj(x, y, z) ((x & (y | z)) | (y & z))
#define SHR(x, n) (x >> n)
#define ROTR(x, n) ((x >> n) | (x << (32 - n)))
#define S0(x) (ROTR (x, 2) ^ ROTR (x, 13) ^ ROTR (x, 22))
#define S1(x) (ROTR (x, 6) ^ ROTR (x, 11) ^ ROTR (x, 25))
#define s0(x) (ROTR (x, 7) ^ ROTR (x, 18) ^ SHR (x, 3))
#define s1(x) (ROTR (x, 17) ^ ROTR (x, 19) ^ SHR (x, 10))

#define RND(a, b, c, d, e, f, g, h, k)                                                                                 \
	t0 = h + S1 (e) + Ch (e, f, g) + k;                                                                                \
	t1 = S0 (a) + Maj (a, b, c);                                                                                       \
	d += t0;                                                                                                           \
	h = t0 + t1;

#define RNDr(S, W, i, k)                                                                                               \
	RND (S[(64 - i) % 8], S[(65 - i) % 8], S[(66 - i) % 8], S[(67 - i) % 8], S[(68 - i) % 8], S[(69 - i) % 8],         \
		S[(70 - i) % 8], S[(71 - i) % 8], W[i] + k)

namespace i2p::tools::vanity
{
/**
	 * Size of the Ed25519 IdentityEx prefix hashed for the destination
	 * address. The private-key file itself is larger; only this prefix is
	 * searched.
	 */
inline constexpr std::size_t IDENTITY_LEN = 391;

/** Byte mutated by the search: the nonce is carried in the key buffer. */
inline constexpr std::size_t NONCE_OFFSET = 320;

/** A base32 destination hash is 52 characters; 53 bytes with the NUL. */
inline constexpr std::size_t ADDRESS_LEN = 52;

inline constexpr std::array<uint8_t, 64> LAST_BLOCK = {
	0x05, 0x00, 0x04, 0x00, 0x07, 0x00, 0x00, 0x80, // 7 bytes EdDSA certificate
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C,
	0x38 // 3128 bits (391 bytes)
};
}
