/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: i2pbase64.
 *
 * Encodes stdin or one file to I2P base64, or decodes with -d, writing to
 * stdout. Decoding ignores ASCII whitespace, processes bounded chunks and
 * rejects truncated or malformed input.
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

int printHelp (const char * exe, int exitcode)
{
	std::cout << "usage: " << exe << " [-d] [filename]" << std::endl;
	return exitcode;
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
				if (ch == '\n' || ch == '\r' || ch == ' ' || ch == '\t' || ch == '\v' || ch == '\f')
					continue;
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
				const auto decoded
					= i2p::data::Base64ToByteStream (std::string_view (pending).substr (0, complete), outbuf);
				if (!decoded)
				{
					std::cerr << "invalid base64 input" << std::endl;
					return 1;
				}
				if (!WriteAll (output, std::span<const uint8_t> (outbuf).first (decoded))) return 1;
				// Padding is only legal in the final quad; remember it so the
				// next chunk cannot extend the stream.
				sawPadding = pending.substr (0, complete).find ('=') != std::string::npos;
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
		case 'h': return printHelp (argv[0], 0);
		case 'd': decode = true; break;
		default: return printHelp (argv[0], 1);
		}
	}

	if (argc - optind > 1) return printHelp (argv[0], 1);
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
