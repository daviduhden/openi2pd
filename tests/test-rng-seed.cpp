/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test RNG seed.
 *
 * Pins seed-source success, boundary and failure behavior without a
 * process-global provider hook. Provider samples must not collapse to a
 * constant; legitimate 32-bit collisions remain permitted.
 *
 */

#include <cassert>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <set>
#include <span>
#include <stdexcept>

#include "util.h"
#include "Random.h"

namespace
{
void TestCheckedRandomBytes ()
{
	using namespace i2p::crypto;
	static_assert (!detail::RandomByteSource<decltype ([] (std::span<uint8_t>) { return -1; })>);
	static_assert (!detail::RandomWordSource<decltype ([] { return false; })>);
	constexpr auto limit = static_cast<std::size_t> (std::numeric_limits<int>::max ());
	static_assert (detail::RandomByteCountSupported (limit));
	static_assert (!detail::RandomByteCountSupported (limit + 1));
	int calls = 0;
	detail::FillRandomBytesWith ({ },
		[&calls] (std::span<uint8_t>)
		{
			++calls;
			return false;
		});
	assert (calls == 0);
	FillRandomBytes ({ });
	std::array<uint8_t, 8> bytes;
	bytes.fill (0xff);
	detail::FillRandomBytesWith (bytes,
		[] (std::span<uint8_t> output)
		{
			std::fill (output.begin (), output.end (), uint8_t { 0 });
			return true;
		});
	assert (std::ranges::all_of (bytes, [] (uint8_t value) { return value == 0; }));
	detail::FillRandomBytesWith (bytes,
		[] (std::span<uint8_t> output)
		{
			uint8_t value = 0;
			for (auto & byte : output) byte = ++value;
			return true;
		});
	assert (bytes.front () == 1 && bytes.back () == 8);
	for (const bool throws : { false, true })
	{
		bytes.fill (0xff);
		bool rejected = false;
		try
		{
			detail::FillRandomBytesWith (bytes,
				[throws] (std::span<uint8_t> output)
				{
					output.front () = 0x42;
					if (throws) throw std::logic_error ("fixture source failure");
					return false;
				});
		}
		catch (const std::runtime_error &)
		{
			assert (!throws);
			rejected = true;
		}
		catch (const std::logic_error &)
		{
			assert (throws);
			rejected = true;
		}
		assert (rejected);
		assert (std::ranges::all_of (bytes, [] (uint8_t value) { return value == 0; }));
	}
}

void TestCheckedRandomBounds ()
{
	using i2p::crypto::detail::RandomBelowWith;
	int calls = 0;
	auto zero = [&calls] () -> uint32_t
	{
		++calls;
		return 0;
	};
	assert (RandomBelowWith (0, zero) == 0);
	assert (RandomBelowWith (1, zero) == 0);
	assert (calls == 0);
	assert (RandomBelowWith (2, zero) == 0);
	assert (calls == 1); // Valid zero must not be mistaken for failure.
	calls = 0;
	assert (RandomBelowWith (3, [&calls] () -> uint32_t { return ++calls == 1 ? 0 : 7; }) == 1);
	assert (calls == 2);
	calls = 0;
	bool rejected = false;
	try
	{
		(void)RandomBelowWith (3,
			[&calls] () -> uint32_t
			{
				++calls;
				uint32_t value = 0;
				i2p::crypto::detail::FillRandomBytesWith (
					std::span { reinterpret_cast<uint8_t *> (&value), sizeof (value) },
					[] (std::span<uint8_t>) { return false; });
				return value;
			});
	}
	catch (const std::runtime_error &)
	{
		rejected = true;
	}
	assert (rejected && calls == 1);
}
}

int main ()
{
	TestCheckedRandomBytes ();
	TestCheckedRandomBounds ();
	using i2p::util::detail::RequireRandomSeed;
	int calls = 0;
	const auto zero = RequireRandomSeed (
		[&calls] (std::span<std::byte> bytes)
		{
			++calls;
			assert (bytes.size () == sizeof (uint32_t));
			std::fill (bytes.begin (), bytes.end (), std::byte { 0 });
			return true;
		});
	assert (zero == 0 && calls == 1);
	const auto maximum = RequireRandomSeed (
		[] (std::span<std::byte> bytes)
		{
			std::fill (bytes.begin (), bytes.end (), std::byte { 0xff });
			return true;
		});
	assert (maximum == std::numeric_limits<uint32_t>::max ());
	constexpr uint32_t pattern = 0x12345678u;
	const auto mixed = RequireRandomSeed (
		[pattern] (std::span<std::byte> bytes)
		{
			std::memcpy (bytes.data (), &pattern, sizeof (pattern));
			return true;
		});
	assert (mixed == pattern);
	for (const bool partialWrite : { false, true })
	{
		bool rejected = false;
		try
		{
			(void)RequireRandomSeed (
				[partialWrite] (std::span<std::byte> bytes)
				{
					if (partialWrite) bytes.front () = std::byte { 0x7f };
					return false;
				});
		}
		catch (const std::runtime_error &)
		{
			rejected = true;
		}
		assert (rejected);
	}
	bool sourceException = false;
	try
	{
		(void)RequireRandomSeed (
			[] (std::span<std::byte>) -> bool { throw std::logic_error ("fixture source failure"); });
	}
	catch (const std::logic_error &)
	{
		sourceException = true;
	}
	assert (sourceException);

	std::set<uint_fast32_t> seeds;
	for (int i = 0; i < 512; ++i) seeds.insert (i2p::util::GetRngSeed ());
	// This detects a constant provider result without requiring impossible
	// collision freedom from a uniformly sampled 32-bit space.
	assert (seeds.size () > 1);
	return 0;
}
