/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test modern vocabulary.
 *
 * Exercises the portable C++23 Modern.h seams (concepts, enum conversion,
 * overload composition, scope guard and byte-span views) without network,
 * filesystem or crypto operations. The test is prepared for manual CI.
 */

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "Modern.h"

namespace
{
enum class Example : std::uint8_t
{
	zero = 0,
	two = 2
};
}

int main ()
{
	static_assert (i2p::modern::Enum<Example>);
	static_assert (!i2p::modern::Enum<int>);
	static_assert (i2p::modern::ToUnderlying (Example::two) == 2);

	static_assert (i2p::modern::ByteLike<std::byte>);
	static_assert (i2p::modern::ByteLike<std::uint8_t>);
	static_assert (i2p::modern::ByteLike<unsigned char>);
	static_assert (!i2p::modern::ByteLike<std::uint16_t>);
	static_assert (i2p::modern::StringViewLike<const char *>);
	static_assert (i2p::modern::StringViewLike<std::string_view>);

	std::array<std::uint8_t, 4> bytes { 0x10, 0x20, 0x30, 0x40 };
	auto view = i2p::modern::AsByteSpan (bytes);
	assert (view.size () == bytes.size ());
	assert (view.data () == reinterpret_cast<const std::byte *> (bytes.data ()));
	assert (std::to_integer<std::uint8_t> (view[2]) == 0x30);

	std::vector<unsigned char> dynamicBytes { 1, 2, 3 };
	auto dynamicView = i2p::modern::AsByteSpan (dynamicBytes);
	assert (dynamicView.size () == dynamicBytes.size ());

	int scopeCalls = 0;
	{
		i2p::modern::ScopeExit guard ([&scopeCalls] { ++scopeCalls; });
	}
	assert (scopeCalls == 1);

	{
		i2p::modern::ScopeExit guard ([&scopeCalls] { ++scopeCalls; });
		guard.dismiss ();
	}
	assert (scopeCalls == 1);

	{
		i2p::modern::ScopeExit first ([&scopeCalls] { ++scopeCalls; });
		auto second = std::move (first);
	}
	assert (scopeCalls == 2);

	bool visitedInt = false;
	bool visitedString = false;
	std::variant<int, std::string> value { 42 };
	std::visit (i2p::modern::Overloaded {
					[&visitedInt] (int) { visitedInt = true; },
					[&visitedString] (const std::string &) { visitedString = true; },
				},
		value);
	assert (visitedInt && !visitedString);

	return 0;
}
