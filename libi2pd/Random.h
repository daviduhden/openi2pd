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
#include <cstddef>
#include <concepts>
#include <functional>
#include <limits>
#include <span>
#include <stdexcept>
#include <openssl/crypto.h>
#include <openssl/rand.h>

namespace i2p
{
namespace crypto
{
	namespace detail
	{
		constexpr bool RandomByteCountSupported (std::size_t size) noexcept
		{
			return size <= static_cast<std::size_t> (std::numeric_limits<int>::max ());
		}

		template <typename Fill>
		concept RandomByteSource = requires (Fill & source, std::span<uint8_t> bytes) {
			{ std::invoke (source, bytes) } -> std::same_as<bool>;
		};
		template <typename Next>
		concept RandomWordSource = requires (Next & next) {
			{ std::invoke (next) } -> std::same_as<uint32_t>;
		};

		/*
		 * Checked per-call source adapter. Failure never publishes partial
		 * output; the entire destination is cleansed before unwinding.
		 * @param bytes Writable output, or an empty span for a no-op.
		 * @param source Trusted callable filling all bytes before true.
		 * The callable is synchronous and must not retain the output span.
		 * @throws std::length_error If the byte count exceeds RAND_bytes.
		 * @throws std::runtime_error If the source reports failure.
		 * Source exceptions propagate after cleansing the destination.
		 */
		template <RandomByteSource Fill> void FillRandomBytesWith (std::span<uint8_t> bytes, Fill && source)
		{
			if (bytes.empty ()) return;
			if (!RandomByteCountSupported (bytes.size ()))
				throw std::length_error ("CSPRNG byte count exceeds provider limit");
			bool ready = false;
			try
			{
				ready = std::invoke (source, bytes);
			}
			catch (...)
			{
				OPENSSL_cleanse (bytes.data (), bytes.size ());
				throw;
			}
			if (!ready)
			{
				OPENSSL_cleanse (bytes.data (), bytes.size ());
				throw std::runtime_error ("CSPRNG output unavailable");
			}
		}

		/*
		 * Apply unbiased rejection to a checked 32-bit source.
		 * @param bound Exclusive upper bound; zero and one return zero.
		 * @param next Trusted source; exceptions abort rejection immediately.
		 * @return A value below bound, or zero when no choice is needed.
		 */
		template <RandomWordSource Next> uint32_t RandomBelowWith (uint32_t bound, Next && next)
		{
			if (bound < 2) return 0;
			// Reject the short top interval to avoid modulo bias.
			const uint32_t threshold = (uint32_t { 0 } - bound) % bound;
			uint32_t value;
			do value = std::invoke (next);
			while (value < threshold);
			return value % bound;
		}
	}

	/*
	 * Fill output from the provider, or abort without usable partial data.
	 * @param bytes Writable output; empty spans do not invoke the provider.
	 * @throws std::length_error Before writing counts above INT_MAX.
	 * @throws std::runtime_error After cleansing a failed provider output.
	 * An oversized destination remains unchanged; no source was invoked.
	 */
	inline void FillRandomBytes (std::span<uint8_t> bytes)
	{
		detail::FillRandomBytesWith (bytes, [] (std::span<uint8_t> output)
			{ return RAND_bytes (output.data (), static_cast<int> (output.size ())) == 1; });
	}

	/// Return a checked provider word, including zero. Throws on failure.
	inline uint32_t RandomUInt32 ()
	{
		uint32_t value = 0;
		FillRandomBytes (std::span { reinterpret_cast<uint8_t *> (&value), sizeof (value) });
		return value;
	}

	/// Return an unbiased bounded choice; provider failures propagate.
	/// Bounds below two need no randomness and return zero.
	inline uint32_t RandomBelow (uint32_t bound)
	{
		return detail::RandomBelowWith (bound, [] { return RandomUInt32 (); });
	}
}
}

#endif
