/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: I2PEndian.
 *
 * Endian conversion for I2P's big-endian wire fields, with memcpy-based unaligned buffer
 * access and htobe/be16toh-style macros. POSIX builds use endian(3); Windows uses C++23
 * std::byteswap because its SDK lacks that header, and mixed-endian hosts are rejected at
 * compile time.
 *
 */

#ifndef I2PENDIAN_H__
#define I2PENDIAN_H__
#include <string.h>
#include <inttypes.h>

#ifdef _WIN32
// The Windows SDK has no endian(3) header. C++23 provides the byte-swap
// primitive without compiler-specific intrinsics.
#include <bit>

namespace i2p
{
namespace endian
{
	static_assert (std::endian::native == std::endian::little || std::endian::native == std::endian::big,
		"mixed-endian hosts are not supported");

	template <typename T> constexpr T ToBig (T value)
	{
		if constexpr (std::endian::native == std::endian::big)
			return value;
		else
			return std::byteswap (value);
	}

	template <typename T> constexpr T ToLittle (T value)
	{
		if constexpr (std::endian::native == std::endian::little)
			return value;
		else
			return std::byteswap (value);
	}
} // namespace endian
} // namespace i2p

#define htobe16(x) i2p::endian::ToBig<uint16_t> (x)
#define htole16(x) i2p::endian::ToLittle<uint16_t> (x)
#define be16toh(x) i2p::endian::ToBig<uint16_t> (x)
#define le16toh(x) i2p::endian::ToLittle<uint16_t> (x)

#define htobe32(x) i2p::endian::ToBig<uint32_t> (x)
#define htole32(x) i2p::endian::ToLittle<uint32_t> (x)
#define be32toh(x) i2p::endian::ToBig<uint32_t> (x)
#define le32toh(x) i2p::endian::ToLittle<uint32_t> (x)

#define htobe64(x) i2p::endian::ToBig<uint64_t> (x)
#define htole64(x) i2p::endian::ToLittle<uint64_t> (x)
#define be64toh(x) i2p::endian::ToBig<uint64_t> (x)
#define le64toh(x) i2p::endian::ToLittle<uint64_t> (x)
#else
#include <endian.h>
#endif

inline uint16_t buf16toh (const void * buf)
{
	uint16_t b16;
	memcpy (&b16, buf, sizeof (uint16_t));
	return b16;
}

inline uint32_t buf32toh (const void * buf)
{
	uint32_t b32;
	memcpy (&b32, buf, sizeof (uint32_t));
	return b32;
}

inline uint64_t buf64toh (const void * buf)
{
	uint64_t b64;
	memcpy (&b64, buf, sizeof (uint64_t));
	return b64;
}

inline uint16_t bufbe16toh (const void * buf)
{
	return be16toh (buf16toh (buf));
}

inline uint32_t bufbe32toh (const void * buf)
{
	return be32toh (buf32toh (buf));
}

inline uint64_t bufbe64toh (const void * buf)
{
	return be64toh (buf64toh (buf));
}

inline void htobuf16 (void * buf, uint16_t b16)
{
	memcpy (buf, &b16, sizeof (uint16_t));
}

inline void htobuf32 (void * buf, uint32_t b32)
{
	memcpy (buf, &b32, sizeof (uint32_t));
}

inline void htobuf64 (void * buf, uint64_t b64)
{
	memcpy (buf, &b64, sizeof (uint64_t));
}

inline void htobe16buf (void * buf, uint16_t big16)
{
	htobuf16 (buf, htobe16 (big16));
}

inline void htobe32buf (void * buf, uint32_t big32)
{
	htobuf32 (buf, htobe32 (big32));
}

inline void htobe64buf (void * buf, uint64_t big64)
{
	htobuf64 (buf, htobe64 (big64));
}

inline void htole16buf (void * buf, uint16_t big16)
{
	htobuf16 (buf, htole16 (big16));
}

inline void htole32buf (void * buf, uint32_t big32)
{
	htobuf32 (buf, htole32 (big32));
}

inline void htole64buf (void * buf, uint64_t big64)
{
	htobuf64 (buf, htole64 (big64));
}

inline uint16_t bufle16toh (const void * buf)
{
	return le16toh (buf16toh (buf));
}

inline uint32_t bufle32toh (const void * buf)
{
	return le32toh (buf32toh (buf));
}

inline uint64_t bufle64toh (const void * buf)
{
	return le64toh (buf64toh (buf));
}

#endif // I2PENDIAN_H__
