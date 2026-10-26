/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Siphash.
 *
 * Portable, independent implementation of the SipHash-2-4 construction with
 * 64- and 128-bit output. It provides the fallback for keyed peer-ordering
 * hashes and NTCP2 length obfuscation when OpenSSL is built without SipHash
 * support.
 *
 */

#ifndef SIPHASH_H
#define SIPHASH_H

#include <bit>
#include <cstddef>
#include <cstdint>
#include "Crypto.h"

#if !OPENSSL_SIPHASH
namespace i2p
{
namespace crypto
{
	namespace siphash
	{
		constexpr int crounds = 2;
		constexpr int drounds = 4;

		inline uint64_t rotl (uint64_t value, int bits)
		{
			return std::rotl (value, bits);
		}

		inline void u32to8le (uint32_t value, uint8_t * output)
		{
			for (unsigned i = 0; i < 4; ++i) output[i] = static_cast<uint8_t> (value >> (i * 8));
		}

		inline void u64to8le (uint64_t value, uint8_t * output)
		{
			for (unsigned i = 0; i < 8; ++i) output[i] = static_cast<uint8_t> (value >> (i * 8));
		}

		inline uint64_t u8to64le (const uint8_t * input)
		{
			uint64_t value = 0;
			for (unsigned i = 0; i < 8; ++i) value |= static_cast<uint64_t> (input[i]) << (i * 8);
			return value;
		}

		inline void round (uint64_t & v0, uint64_t & v1, uint64_t & v2, uint64_t & v3)
		{
			v0 += v1;
			v1 = rotl (v1, 13);
			v1 ^= v0;
			v0 = rotl (v0, 32);

			v2 += v3;
			v3 = rotl (v3, 16);
			v3 ^= v2;

			v0 += v3;
			v3 = rotl (v3, 21);
			v3 ^= v0;

			v2 += v1;
			v1 = rotl (v1, 17);
			v1 ^= v2;
			v2 = rotl (v2, 32);
		}
	}

	/**
	 * SipHash-2-4 (Aumasson/Bernstein), 64- or 128-bit output.
	 *
	 * This is the portable fallback used when the crypto provider is built
	 * without SipHash; the OpenSSL-backed path in Crypto.cpp is preferred when
	 * available and both must produce identical bytes for the same key.
	 *
	 * @param hash Output buffer of exactly hashsz bytes.
	 * @param input Message bytes; the length is taken from inputSize and never
	 * from a terminator, so binary input is supported.
	 * @param key Exactly 16 bytes (two little-endian 64-bit words). The key is
	 * a secret: it is never logged and callers must not reuse one key across
	 * unrelated protocols.
	 * @tparam hashsz 8 or 16 only. The 128-bit form is the two-pass variant
	 * required by the 16-byte peer-ordering digest; NTCP2's AEAD-length tag is
	 * the 8-byte form. Other values are not part of the contract even though
	 * the template accepts them.
	 * @note This is a keyed hash, not a MAC-verify primitive: callers must
	 * compare results with a constant-time comparison.
	 */
	template <std::size_t hashsz>
	inline void Siphash (uint8_t * hash, const uint8_t * input, std::size_t inputSize, const uint8_t * key)
	{
		uint64_t v0 = 0x736f6d6570736575ULL;
		uint64_t v1 = 0x646f72616e646f6dULL;
		uint64_t v2 = 0x6c7967656e657261ULL;
		uint64_t v3 = 0x7465646279746573ULL;

		const uint64_t k0 = siphash::u8to64le (key);
		const uint64_t k1 = siphash::u8to64le (key + 8);

		v3 ^= k1;
		v2 ^= k0;
		v1 ^= k1;
		v0 ^= k0;

		if (hashsz == 16) v1 ^= 0xee;

		// Consume every complete eight-byte block. The tail is built
		// separately so an input buffer that overlaps the output remains
		// readable until all words have been mixed.
		const std::size_t blockCount = inputSize / sizeof (uint64_t);
		const uint8_t * cursor = input;
		for (std::size_t block = 0; block < blockCount; ++block)
		{
			const uint64_t message = siphash::u8to64le (cursor);
			v3 ^= message;
			for (int i = 0; i < siphash::crounds; ++i) siphash::round (v0, v1, v2, v3);
			v0 ^= message;
			cursor += sizeof (uint64_t);
		}

		uint64_t tail = static_cast<uint64_t> (inputSize) << 56;
		const std::size_t tailSize = inputSize & 7u;
		for (std::size_t i = 0; i < tailSize; ++i) tail |= static_cast<uint64_t> (cursor[i]) << (i * 8);

		v3 ^= tail;
		for (int i = 0; i < siphash::crounds; ++i) siphash::round (v0, v1, v2, v3);
		v0 ^= tail;

		if (hashsz == 16)
			v2 ^= 0xee;
		else
			v2 ^= 0xff;

		for (int i = 0; i < siphash::drounds; ++i) siphash::round (v0, v1, v2, v3);

		const uint64_t first = v0 ^ v1 ^ v2 ^ v3;
		siphash::u64to8le (first, hash);

		// SipHash-2-4 with 128-bit output extends the same state with a
		// second finalization pass. Other template values retain the legacy
		// behavior of writing the extension as well; callers use 8 or 16.
		if (hashsz == 8) return;

		v1 ^= 0xdd;

		for (int i = 0; i < siphash::drounds; ++i) siphash::round (v0, v1, v2, v3);

		const uint64_t second = v0 ^ v1 ^ v2 ^ v3;
		siphash::u64to8le (second, hash + 8);
	}
}
}
#endif

#endif
