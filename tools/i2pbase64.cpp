/*
 * Copyright (c) 2013-2026, The PurpleI2P Project
 * Copyright (c) 2026, David Uhden Collado
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <string_view>
#include <unistd.h>

#include "Base.h"

namespace
{
	constexpr size_t CHUNK_SIZE = 4096;

	int printHelp (const char * exe, int exitcode)
	{
		std::cout << "usage: " << exe << " [-d] [filename]" << std::endl;
		return exitcode;
	}

	bool WriteAll (int fd, const uint8_t * data, size_t size)
	{
		while (size)
		{
			const auto written = ::write (fd, data, size);
			if (written < 0)
			{
				if (errno == EINTR) continue;
				return false;
			}
			if (!written) return false;
			data += written;
			size -= static_cast<size_t> (written);
		}
		return true;
	}

	int Decode (int infile, int outfile)
	{
		std::array<char, CHUNK_SIZE> inbuf{};
		std::array<uint8_t, CHUNK_SIZE> outbuf{};
		std::string pending;
		while (true)
		{
			const auto readSize = ::read (infile, inbuf.data (), inbuf.size ());
			if (readSize < 0)
			{
				if (errno == EINTR) continue;
				return 1;
			}
			if (readSize == 0) break;
			for (ssize_t i = 0; i < readSize; ++i)
				if (inbuf[i] != '\n' && inbuf[i] != '\r' && inbuf[i] != ' ' && inbuf[i] != '\t')
					pending.push_back (inbuf[i]);

			const size_t complete = pending.size () - pending.size () % 4;
			if (complete)
			{
				const auto decoded = i2p::data::Base64ToByteStream (
					std::string_view (pending).substr (0, complete), outbuf);
				if (!decoded || !WriteAll (outfile, outbuf.data (), decoded)) return 1;
				pending.erase (0, complete);
			}
			if (pending.size () > 4)
			{
				std::cerr << "invalid base64 input" << std::endl;
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

	int Encode (int infile, int outfile)
	{
		std::array<uint8_t, CHUNK_SIZE * 3> inbuf{};
		while (true)
		{
			const auto readSize = ::read (infile, inbuf.data (), inbuf.size ());
			if (readSize < 0)
			{
				if (errno == EINTR) continue;
				return 1;
			}
			if (readSize == 0) break;
			const auto encoded = i2p::data::ByteStreamToBase64 (inbuf.data (), static_cast<size_t> (readSize));
			if (!WriteAll (outfile, reinterpret_cast<const uint8_t *> (encoded.data ()), encoded.size ())) return 1;
		}
		return 0;
	}
}

int main (int argc, char * argv[])
{
	int opt;
	bool decode = false;
	int infile = STDIN_FILENO;
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
	if (optind < argc)
	{
		infile = ::open (argv[optind], O_RDONLY);
		if (infile == -1)
		{
			std::perror (argv[optind]);
			return 1;
		}
	}

	const int result = decode ? Decode (infile, STDOUT_FILENO) : Encode (infile, STDOUT_FILENO);
	if (infile != STDIN_FILENO) ::close (infile);
	return result;
}
