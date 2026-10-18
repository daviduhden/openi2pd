#ifndef I2PD_TOOLS_COMMON_FILE_HPP
#define I2PD_TOOLS_COMMON_FILE_HPP

#include <cstdint>
#include "Expected.h"
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include <openssl/crypto.h>

namespace i2p::tools
{
	inline constexpr size_t MAX_TOOL_FILE_SIZE = 16 * 1024 * 1024; // 16 MiB

	/**
	 * Read a complete file with an explicit size cap. Returns a value instead
	 * of throwing or leaving a partially initialized buffer on short reads.
	 */
	inline i2p::expected<std::vector<uint8_t>, std::string> ReadFile (
		const std::string& path, size_t maxSize = MAX_TOOL_FILE_SIZE)
	{
		std::ifstream in (path, std::ios::binary | std::ios::ate);
		if (!in) return i2p::unexpected ("cannot open " + path);

		const auto end = in.tellg ();
		if (end < 0) return i2p::unexpected ("cannot determine size of " + path);
		const auto size = static_cast<uint64_t> (end);
		if (size > maxSize) return i2p::unexpected ("file is too large: " + path);

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

	inline bool WriteFile (const std::string& path, const uint8_t * data, size_t size)
	{
		std::ofstream out (path, std::ios::binary | std::ios::trunc);
		if (!out) return false;
		if (size)
			out.write (reinterpret_cast<const char *> (data), static_cast<std::streamsize> (size));
		return static_cast<bool> (out);
	}

	/** Best-effort erasure of private key material. */
	inline void Wipe (std::vector<uint8_t>& data)
	{
		if (!data.empty ()) OPENSSL_cleanse (data.data (), data.size ());
		data.clear ();
	}
}

#endif
