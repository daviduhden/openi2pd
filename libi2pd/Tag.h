/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Tag.
 *
 * Fixed-size, byte-comparable tag type used for garlic and session
 * identifiers, with Base32/Base64 conversion and CSPRNG-backed randomization
 * that zeroes the buffer if the provider fails.
 *
 */

#ifndef TAG_H__
#define TAG_H__

#include <boost/static_assert.hpp>
#include <string.h>
#include <openssl/rand.h>
#include <string>
#include <string_view>
#include <compare>
#include "Base.h"

namespace i2p
{
namespace data
{
	template <size_t sz> class Tag
	{
		BOOST_STATIC_ASSERT_MSG (sz % 8 == 0, "Tag size must be multiple of 8 bytes");

	public:
		static constexpr size_t len = sz;

		Tag () = default;
		Tag (const uint8_t * buf) { memcpy (m_Buf, buf, sz); }

		bool operator== (const Tag & other) const { return !memcmp (m_Buf, other.m_Buf, sz); }
		bool operator!= (const Tag & other) const { return !(*this == other); }
		bool operator< (const Tag & other) const { return memcmp (m_Buf, other.m_Buf, sz) < 0; }
		// without it std::pair and std::tuple pick the built-in comparison of
		// the pointer this converts to, and equal tags come out different
		std::strong_ordering operator<=> (const Tag & other) const { return memcmp (m_Buf, other.m_Buf, sz) <=> 0; }

		uint8_t * operator() () { return m_Buf; }
		const uint8_t * operator() () const { return m_Buf; }

		operator uint8_t *() { return m_Buf; }
		operator const uint8_t *() const { return m_Buf; }

		const uint8_t * data () const { return m_Buf; }
		const uint64_t * GetLL () const { return ll; }

		bool IsZero () const
		{
			for (size_t i = 0; i < sz / 8; ++i)
				if (ll[i]) return false;
			return true;
		}

		void Fill (uint8_t c) { memset (m_Buf, c, sz); }

		bool Randomize ()
		{
			// Clear first so a partially written buffer is never mistaken for
			// a freshly generated tag if the provider fails.
			memset (m_Buf, 0, sz);
			if (RAND_bytes (m_Buf, sz) == 1) return true;
			memset (m_Buf, 0, sz);
			return false;
		}

		std::string ToBase64 (size_t length = sz) const { return i2p::data::ByteStreamToBase64 (m_Buf, length); }

		std::string ToBase32 (size_t length = sz) const { return i2p::data::ByteStreamToBase32 (m_Buf, length); }

		size_t FromBase32 (std::string_view s) { return i2p::data::Base32ToByteStream (s, m_Buf, sz); }

		size_t FromBase64 (std::string_view s) { return i2p::data::Base64ToByteStream (s, m_Buf, sz); }

		uint8_t GetBit (int i) const
		{
			int pos = i >> 3; // /8
			if (pos >= (int)sz) return 0;
			return m_Buf[pos] & (0x80 >> (i & 0x07));
		}

	private:
		union // 8 bytes aligned
		{
			uint8_t m_Buf[sz];
			uint64_t ll[sz / 8];
		};
	};
} // data
} // i2p

namespace std
{
// hash for std::unordered_map
template <size_t sz> struct hash<i2p::data::Tag<sz>>
{
	size_t operator() (const i2p::data::Tag<sz> & s) const { return s.GetLL ()[0]; }
};
}

#endif /* TAG_H__ */
