/*
 * Copyright (c) 2013-2026, The PurpleI2P Project
 * Copyright (c) 2026, David Uhden Collado
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef I2PD_TOOLS_COMMON_FILE_HPP
#define I2PD_TOOLS_COMMON_FILE_HPP

#include <cstdint>
#include "Expected.h"
#include <cerrno>
#include <fstream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <openssl/crypto.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif

namespace i2p::tools
{
inline constexpr size_t MAX_TOOL_FILE_SIZE = 16 * 1024 * 1024; // 16 MiB

/**
	 * Read a complete file with an explicit size cap. Returns a value instead
	 * of throwing or leaving a partially initialized buffer on short reads.
	 */
inline i2p::expected<std::vector<uint8_t>, std::string> ReadFile (
	const std::string & path, size_t maxSize = MAX_TOOL_FILE_SIZE)
{
	std::ifstream in (path, std::ios::binary | std::ios::ate);
	if (!in) return i2p::unexpected ("cannot open " + path);

	const auto end = in.tellg ();
	if (end < 0) return i2p::unexpected ("cannot determine size of " + path);
	const auto size = static_cast<uint64_t> (end);
	if (size > maxSize) return i2p::unexpected ("file is too large: " + path);
	if (size > static_cast<uint64_t> (std::numeric_limits<std::streamsize>::max ()))
		return i2p::unexpected ("file is too large: " + path);

	std::vector<uint8_t> data (static_cast<size_t> (size));
	in.seekg (0, std::ios::beg);
	if (!data.empty ())
	{
		in.read (reinterpret_cast<char *> (data.data ()), static_cast<std::streamsize> (data.size ()));
		if (!in || static_cast<size_t> (in.gcount ()) != data.size ())
			return i2p::unexpected ("short read from " + path);
	}
	return data;
}

inline bool WriteFile (const std::string & path, std::span<const uint8_t> data)
{
	if (data.size () > static_cast<size_t> (std::numeric_limits<std::streamsize>::max ())) return false;
#ifdef _WIN32
	{
		std::ofstream out (path, std::ios::binary | std::ios::trunc);
		if (!out) return false;
		if (!data.empty ())
			out.write (reinterpret_cast<const char *> (data.data ()), static_cast<std::streamsize> (data.size ()));
		out.close ();
		if (!out) return false;
	}
	// Best effort: key outputs should not inherit a world-readable mode.
	_chmod (path.c_str (), _S_IREAD | _S_IWRITE);
	return true;
#else
	// Key and vanity outputs are private by default. O_NOFOLLOW avoids
	// redirecting a write through a planted symlink; the caller still treats
	// an existing non-regular leaf as a normal write failure.
	int fd = ::open (path.c_str (), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0) return false;
	(void)::fchmod (fd, S_IRUSR | S_IWUSR);
	const uint8_t * cursor = data.data ();
	size_t remaining = data.size ();
	bool ok = true;
	while (remaining)
	{
		ssize_t written;
		do
			written = ::write (fd, cursor, remaining);
		while (written < 0 && errno == EINTR);
		if (written <= 0)
		{
			ok = false;
			break;
		}
		cursor += written;
		remaining -= static_cast<size_t> (written);
	}
	int savedErrno = errno;
	::close (fd);
	errno = savedErrno;
	return ok;
#endif
}

inline bool WriteFile (const std::string & path, const uint8_t * data, size_t size)
{
	if (!data && size) return false;
	return WriteFile (path, std::span<const uint8_t> (data, data ? size : 0));
}

/** Best-effort erasure of private key material. */
inline void Wipe (std::vector<uint8_t> & data)
{
	if (!data.empty ()) OPENSSL_cleanse (data.data (), data.size ());
	data.clear ();
}
}

#endif
