/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: SOCKS address buffer.
 *
 * Fixed-capacity SOCKS5 domain-name buffer with explicit rejection
 * instead of length wrap or out-of-bounds writes.
 */

#ifndef SOCKS_ADDRESS_H__
#define SOCKS_ADDRESS_H__

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace i2p
{
namespace proxy
{
	inline constexpr std::size_t SOCKS_MAX_HOSTNAME_SIZE = 255;

	struct SOCKSDnsAddress
	{
		std::uint8_t size;
		char value[SOCKS_MAX_HOSTNAME_SIZE];

		bool SetString (std::string_view str)
		{
			if (str.size () > sizeof (value)) return false;
			size = static_cast<std::uint8_t> (str.size ());
			if (size) std::memcpy (value, str.data (), size);
			return true;
		}

		std::string_view GetString () const { return std::string_view (value, size); }

		bool push_back (char c)
		{
			if (size >= sizeof (value)) return false;
			value[size++] = c;
			return true;
		}
	};
}
}

#endif // SOCKS_ADDRESS_H__
