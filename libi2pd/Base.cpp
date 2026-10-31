/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Base.
 *
 * Implements the I2P base32 and base64 alphabets and their conversions.
 * Base64 decoding requires four-character framing, terminal-only
 * padding and a caller-sized output buffer; base32 emits the canonical
 * lowercase/digit alphabet, so callers can safely compare re-encoded
 * addresses.
 *
 */

#include <array>
#include <cstdint>
#include <stdlib.h>
#include <string.h>

#include "Base.h"

namespace i2p
{
namespace data
{
	static constexpr char T32[32] = {
		'a',
		'b',
		'c',
		'd',
		'e',
		'f',
		'g',
		'h',
		'i',
		'j',
		'k',
		'l',
		'm',
		'n',
		'o',
		'p',
		'q',
		'r',
		's',
		't',
		'u',
		'v',
		'w',
		'x',
		'y',
		'z',
		'2',
		'3',
		'4',
		'5',
		'6',
		'7',
	};

	const char * GetBase32SubstitutionTable ()
	{
		return T32;
	}

	/*
	*
	* BASE64 Substitution Table
	* -------------------------
	*
	* Direct Substitution Table
	*/

	static constexpr char T64[64] = { 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
		'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l',
		'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', '0', '1', '2', '3', '4', '5', '6', '7',
		'8', '9', '-', '~' };

	const char * GetBase64SubstitutionTable ()
	{
		return T64;
	}

	/*
	* Padding
	*/
	static constexpr char P64 = '=';

	/*
	* Reverse Substitution Table (build time). A value of -1 marks a byte
	* that is not part of the alphabet. Building it with consteval removes
	* the previous lazy initialisation, which raced when two threads
	* decoded base64 for the first time concurrently.
	*/
	static consteval std::array<int8_t, 256> BuildReverseTable ()
	{
		std::array<int8_t, 256> table;
		table.fill (-1);
		for (size_t i = 0; i < sizeof (T64); ++i) table[static_cast<uint8_t> (T64[i])] = static_cast<int8_t> (i);
		table[static_cast<uint8_t> (P64)] = 0;
		return table;
	}
	static constexpr auto IT64 = BuildReverseTable ();

	/*
	*
	* ByteStreamToBase64
	* ------------------
	*
	* Converts binary encoded data to BASE64 format.
	*
	*/
	std::string ByteStreamToBase64 ( // base64 encoded string
		const uint8_t * InBuffer, // Input buffer, binary data
		size_t InCount // Number of bytes in the input buffer
	)
	{
		if (!InBuffer) return { };
		return ByteStreamToBase64 (std::span<const uint8_t> (InBuffer, InCount));
	}

	std::string ByteStreamToBase64 ( // base64 encoded string
		std::span<const uint8_t> InBuffer // Input buffer, binary data
	)
	{
		if (InBuffer.empty ()) return { };

		const size_t InCount = InBuffer.size ();
		const size_t n = InCount / 3;
		const size_t m = InCount % 3;
		std::string out;
		out.reserve (m ? 4 * (n + 1) : 4 * n);

		const uint8_t * ps = InBuffer.data ();
		for (size_t i = 0; i < n; ++i)
		{
			const uint8_t b0 = *ps++;
			const uint8_t b1 = *ps++;
			const uint8_t b2 = *ps++;
			out.push_back (T64[b0 >> 2]);
			out.push_back (T64[((b0 & 0x03) << 4) | (b1 >> 4)]);
			out.push_back (T64[((b1 & 0x0f) << 2) | (b2 >> 6)]);
			out.push_back (T64[b2 & 0x3f]);
		}
		if (m == 1)
		{
			const uint8_t b0 = *ps++;
			out.push_back (T64[b0 >> 2]);
			out.push_back (T64[(b0 & 0x03) << 4]);
			out.push_back (P64);
			out.push_back (P64);
		}
		else if (m == 2)
		{
			const uint8_t b0 = *ps++;
			const uint8_t b1 = *ps++;
			out.push_back (T64[b0 >> 2]);
			out.push_back (T64[((b0 & 0x03) << 4) | (b1 >> 4)]);
			out.push_back (T64[(b1 & 0x0f) << 2]);
			out.push_back (P64);
		}
		return out;
	}

	/*
	*
	* Base64ToByteStream
	* ------------------
	*
	* Converts BASE64 encoded string to binary format. If input buffer is
	* not properly padded, buffer of negative length is returned
	*
	*/
	size_t Base64ToByteStream ( // Number of output bytes
		std::string_view base64Str, // BASE64 encoded string
		std::span<uint8_t> outBuffer // destination with explicit size
	)
	{
		// Keep the I2P base64 framing: length must be a multiple of four,
		// padding is only allowed at the end, and every other byte must be
		// in the alphabet. Rejecting invalid input here is important because
		// these strings come from RouterInfo and LeaseSet records received
		// from untrusted peers.
		if (base64Str.empty () || base64Str.size () % 4 != 0) return 0;
		if (base64Str.front () == P64) return 0;

		size_t padding = 0;
		if (base64Str.ends_with ("=="))
			padding = 2;
		else if (base64Str.back () == P64)
			padding = 1;

		const size_t encodedChars = base64Str.size () - padding;
		for (size_t i = 0; i < encodedChars; ++i)
			if (base64Str[i] == P64 || IT64[static_cast<uint8_t> (base64Str[i])] < 0) return 0;
		for (size_t i = encodedChars; i < base64Str.size (); ++i)
			if (base64Str[i] != P64) return 0;

		const size_t outCount = base64Str.size () / 4 * 3 - padding;
		if (outCount > outBuffer.size ()) return 0;

		size_t written = 0;
		for (size_t i = 0; i < base64Str.size (); i += 4)
		{
			const uint32_t v = (static_cast<uint32_t> (IT64[static_cast<uint8_t> (base64Str[i])]) << 18)
				| (static_cast<uint32_t> (IT64[static_cast<uint8_t> (base64Str[i + 1])]) << 12)
				| (static_cast<uint32_t> (IT64[static_cast<uint8_t> (base64Str[i + 2])]) << 6)
				| static_cast<uint32_t> (IT64[static_cast<uint8_t> (base64Str[i + 3])]);

			if (written < outCount) outBuffer[written++] = static_cast<uint8_t> (v >> 16);
			if (written < outCount) outBuffer[written++] = static_cast<uint8_t> (v >> 8);
			if (written < outCount) outBuffer[written++] = static_cast<uint8_t> (v);
		}
		return written;
	}

	size_t Base32ToByteStream (std::string_view base32Str, uint8_t * outBuf, size_t outLen)
	{
		unsigned int tmp = 0, bits = 0;
		size_t ret = 0;
		for (auto ch : base32Str)
		{
			if (ch >= '2' && ch <= '7') // digit
				ch = (ch - '2') + 26; // 26 means a-z
			else if (ch >= 'a' && ch <= 'z')
				ch = ch - 'a'; // a = 0
			else
				return 0; // unexpected character

			tmp |= ch;
			bits += 5;
			if (bits >= 8)
			{
				if (ret >= outLen) return ret;
				outBuf[ret] = static_cast<uint8_t> (tmp >> (bits - 8));
				bits -= 8;
				ret++;
			}
			tmp <<= 5;
		}
		return ret;
	}

	std::string ByteStreamToBase32 (const uint8_t * inBuf, size_t len)
	{
		if (!len) return { };
		if (!inBuf) return { };
		std::string out;
		out.reserve ((len * 8 + 4) / 5);
		size_t pos = 1;
		unsigned int bits = 8, tmp = inBuf[0];
		while (bits > 0 || pos < len)
		{
			if (bits < 5)
			{
				if (pos < len)
				{
					tmp <<= 8;
					tmp |= inBuf[pos] & 0xFF;
					pos++;
					bits += 8;
				}
				else // last byte
				{
					tmp <<= (5 - bits);
					bits = 5;
				}
			}

			bits -= 5;
			int ind = (tmp >> bits) & 0x1F;
			out.push_back (static_cast<char> ((ind < 26) ? (ind + 'a') : ((ind - 26) + '2')));
		}
		return out;
	}
}
}
