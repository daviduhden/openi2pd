/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: i2pbase64.
 *
 * Encodes stdin or one file to I2P base64, or decodes with -d, writing
 * to stdout. Decoding ignores ASCII whitespace, processes bounded
 * chunks and rejects truncated or malformed input.
 *
 */

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>

#include "common/getopt.h"
#include "Base.h"

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace
{
constexpr size_t CHUNK_SIZE = 4096;

/// @brief Print the usage synopsis to an explicit stream.
/// `-h` writes it to stdout; a usage error writes it to stderr, so a
/// failing invocation never leaves a synopsis on stdout.
void printUsage (const char * exe, std::ostream & out)
{
	out << "usage: " << exe << " [-d] [filename]" << std::endl;
}

void SetBinaryStdio ()
{
#ifdef _WIN32
	_setmode (_fileno (stdin), _O_BINARY);
	_setmode (_fileno (stdout), _O_BINARY);
#endif
}

bool WriteAll (std::ostream & output, std::span<const uint8_t> data)
{
	if (data.empty ()) return true;
	output.write (reinterpret_cast<const char *> (data.data ()), static_cast<std::streamsize> (data.size ()));
	return static_cast<bool> (output);
}

bool WriteAll (std::ostream & output, std::string_view data)
{
	if (data.empty ()) return true;
	output.write (data.data (), static_cast<std::streamsize> (data.size ()));
	return static_cast<bool> (output);
}

/// @brief Streaming base64 decoder for I2P base64 on stdin or one file.
///
/// Decoding is chunked, so `=padding` can arrive in a later read than
/// the data it terminates. Whole four-character quads are decoded one
/// at a time, because the core decoder only accepts padding at the end
/// of its input: handing it a buffer that already contains a second
/// quartet would hide the padding position from this state machine and
/// collapse a distinct protocol violation into the generic alphabet
/// diagnostic. Once a decoded quad contained `=`, the `sawPadding`
/// latch rejects every later non-whitespace byte, in the same chunk or
/// in a later one, with `invalid base64 input: data after padding`, so
/// a second encoded value cannot be appended to the same stream. A
/// trailing partial quad without preceding padding is reported as
/// truncated rather than padded implicitly.
///
/// Whitespace (space, tab, CR, LF, VT, FF) is ignored anywhere;
/// anything else outside the alphabet is an error from the core
/// decoder.
/// @return 0 on success, 1 on invalid/truncated input or a write/read
/// error.
/// @post Diagnostics go to stderr; decoded bytes are the only stdout
/// output.
int Decode (std::istream & input, std::ostream & output)
{
	std::array<char, CHUNK_SIZE> inbuf { };
	std::array<uint8_t, CHUNK_SIZE> outbuf { };
	std::string pending;
	bool sawPadding = false;
	while (true)
	{
		input.read (inbuf.data (), static_cast<std::streamsize> (inbuf.size ()));
		const auto readSize = input.gcount ();
		if (readSize > 0)
		{
			for (std::streamsize i = 0; i < readSize; ++i)
			{
				const char ch = inbuf[static_cast<size_t> (i)];
				if (ch == '\n' || ch == '\r' || ch == ' ' || ch == '\t' || ch == '\v' || ch == '\f') continue;
				if (sawPadding)
				{
					// '=' terminates the encoded stream. Any non-whitespace byte
					// after it is invalid even when it arrives in a later chunk.
					std::cerr << "invalid base64 input: data after padding" << std::endl;
					return 1;
				}
				pending.push_back (ch);
			}

			const size_t complete = pending.size () - pending.size () % 4;
			if (complete)
			{
				// Decode quartet by quartet: Base64ToByteStream accepts
				// padding only at the end of its input, so a buffer that
				// holds the padded quartet and a following one would fail
				// with the generic alphabet diagnostic instead of naming
				// the actual violation.
				size_t decodedBytes = 0;
				for (size_t offset = 0; offset < complete; offset += 4)
				{
					if (sawPadding)
					{
						// A whole quartet after the padded one is a second
						// encoded value appended to the same stream.
						std::cerr << "invalid base64 input: data after padding" << std::endl;
						return 1;
					}
					const std::string_view quartet = std::string_view (pending).substr (offset, 4);
					const auto decoded
						= i2p::data::Base64ToByteStream (quartet, std::span<uint8_t> (outbuf).subspan (decodedBytes));
					if (!decoded)
					{
						std::cerr << "invalid base64 input" << std::endl;
						return 1;
					}
					decodedBytes += decoded;
					sawPadding = quartet.find ('=') != std::string_view::npos;
				}
				// A complete quartet needs at most three output bytes, so
				// one chunk of quartets always fits outbuf.
				if (!WriteAll (output, std::span<const uint8_t> (outbuf).first (decodedBytes))) return 1;
				pending.erase (0, complete);
			}
		}
		if (input.bad ())
		{
			std::cerr << "read error" << std::endl;
			return 1;
		}
		if (input.eof ()) break;
		if (!input)
		{
			std::cerr << "read error" << std::endl;
			return 1;
		}
	}
	if (!pending.empty ())
	{
		// Padding already terminated the value, so a partial quartet after
		// it is a stray byte, not an incomplete stream.
		if (sawPadding)
		{
			std::cerr << "invalid base64 input: data after padding" << std::endl;
			return 1;
		}
		std::cerr << "truncated base64 input" << std::endl;
		return 1;
	}
	return 0;
}

int Encode (std::istream & input, std::ostream & output)
{
	std::array<uint8_t, CHUNK_SIZE * 3> inbuf { };
	while (true)
	{
		input.read (reinterpret_cast<char *> (inbuf.data ()), static_cast<std::streamsize> (inbuf.size ()));
		const auto readSize = input.gcount ();
		if (readSize > 0)
		{
			const std::span<const uint8_t> chunk
				= std::span<const uint8_t> (inbuf).first (static_cast<size_t> (readSize));
			const auto encoded = i2p::data::ByteStreamToBase64 (chunk);
			if (!WriteAll (output, std::string_view (encoded))) return 1;
		}
		if (input.bad ())
		{
			std::cerr << "read error" << std::endl;
			return 1;
		}
		if (input.eof ()) break;
		if (!input)
		{
			std::cerr << "read error" << std::endl;
			return 1;
		}
	}
	return 0;
}
}

int main (int argc, char * argv[])
{
	SetBinaryStdio ();
	int opt;
	bool decode = false;
	while ((opt = getopt (argc, argv, "dh")) != -1)
	{
		switch (opt)
		{
		case 'h': printUsage (argv[0], std::cout); return 0;
		case 'd': decode = true; break;
		default: printUsage (argv[0], std::cerr); return 1;
		}
	}

	if (argc - optind > 1)
	{
		printUsage (argv[0], std::cerr);
		return 1;
	}
	std::ifstream file;
	std::istream * input = &std::cin;
	if (optind < argc)
	{
		file.open (argv[optind], std::ios::binary);
		if (!file)
		{
			std::perror (argv[optind]);
			return 1;
		}
		input = &file;
	}

	return decode ? Decode (*input, std::cout) : Encode (*input, std::cout);
}
