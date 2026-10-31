/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Base.
 *
 * Declares the Base64 and Base32 codecs used for I2P addresses and
 * signatures. Provides I2P's URL-safe Base64 alphabet, span-based
 * buffer helpers and encoded-size calculation.
 *
 */

#ifndef BASE_H__
#define BASE_H__

#include <inttypes.h>
#include <string>
#include <string_view>
#include <span>
#include <cstdlib>

namespace i2p
{
namespace data
{
	std::string ByteStreamToBase64 (const uint8_t * InBuffer, size_t InCount);
	/**
	 * Span overload of ByteStreamToBase64. An empty span encodes to an
	 * empty string.
	 */
	std::string ByteStreamToBase64 (std::span<const uint8_t> InBuffer);
	size_t Base64ToByteStream (std::string_view base64Str, std::span<uint8_t> outBuffer);
	inline size_t Base64ToByteStream (std::string_view base64Str, uint8_t * outBuffer, size_t len)
	{
		return Base64ToByteStream (base64Str, std::span<uint8_t> (outBuffer, outBuffer ? len : 0));
	}

	const char * GetBase32SubstitutionTable ();
	const char * GetBase64SubstitutionTable ();
	constexpr bool IsBase64 (char ch)
	{
		return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-'
			|| ch == '~';
	}

	size_t Base32ToByteStream (std::string_view base32Str, uint8_t * outBuf, size_t outLen);
	std::string ByteStreamToBase32 (const uint8_t * inBuf, size_t len);
	constexpr bool IsBase32 (char ch)
	{
		return (ch >= 'a' && ch <= 'z') || (ch >= '2' && ch <= '7');
	}

	/**
	 * Compute the size for a buffer to contain encoded base64 given that
	 * the size of the input is input_size bytes
	 */
	constexpr size_t Base64EncodingBufferSize (size_t input_size)
	{
		return (input_size / 3 + (input_size % 3 ? 1 : 0)) * 4;
	}

} // data
} // i2p

#endif
