/*
* Copyright (c) 2026, The PurpleI2P Project
*
* This file is part of Open I2Pd and licensed under BSD3
*
* See full license text in LICENSE file at top of project tree
*/

#ifndef NO_TORRENTS

#include "TorrentsControl.h"

#include <fstream>

namespace i2p
{
namespace torrents
{
	namespace
	{
		bool IsUnreservedMagnetChar (char ch)
		{
			auto uch = static_cast<unsigned char> (ch);
			return (uch >= 'A' && uch <= 'Z') || (uch >= 'a' && uch <= 'z') ||
				(uch >= '0' && uch <= '9') || ch == '-' || ch == '.' || ch == '_' || ch == '~';
		}

		std::string PercentEncodeMagnetName (std::string_view name)
		{
			static constexpr char hex[] = "0123456789ABCDEF";
			std::string encoded;
			encoded.reserve (name.size ());
			for (char ch : name)
			{
				if (IsUnreservedMagnetChar (ch))
					encoded.push_back (ch);
				else
				{
					auto uch = static_cast<unsigned char> (ch);
					encoded.push_back ('%');
					encoded.push_back (hex[uch >> 4]);
					encoded.push_back (hex[uch & 0x0F]);
				}
			}
			return encoded;
		}
	}

	std::string ToHexString (std::span<const uint8_t> data)
	{
		static constexpr char hex[] = "0123456789abcdef";
		std::string out;
		out.resize (data.size () * 2);
		for (size_t i = 0; i < data.size (); i++)
		{
			out[i * 2] = hex[data[i] >> 4];
			out[i * 2 + 1] = hex[data[i] & 0x0F];
		}
		return out;
	}

	std::string BuildTorrentMagnet (const std::array<uint8_t, 20>& infoHash, std::string_view name)
	{
		std::string magnet = "magnet:?xt=urn:btih:";
		magnet += ToHexString (std::span<const uint8_t> (infoHash.data (), infoHash.size ()));
		if (!name.empty ())
		{
			magnet += "&dn=";
			magnet += PercentEncodeMagnetName (name);
		}
		return magnet;
	}

	bool ReadTorrentFileBounded (const std::filesystem::path& path, size_t maxBytes,
		std::vector<uint8_t>& content, std::string& error)
	{
		content.clear ();
		error.clear ();
		if (path.empty ())
		{
			error = "empty file path";
			return false;
		}
		if (path.extension () != ".torrent")
		{
			error = "not a .torrent file";
			return false;
		}

		std::error_code ec;
		auto status = std::filesystem::symlink_status (path, ec);
		if (ec)
		{
			error = "can't inspect file";
			return false;
		}
		if (std::filesystem::is_symlink (status) || !std::filesystem::is_regular_file (status))
		{
			error = "not a regular file";
			return false;
		}

		auto size = std::filesystem::file_size (path, ec);
		if (ec)
		{
			error = "can't determine file size";
			return false;
		}
		if (!size)
		{
			error = "empty torrent file";
			return false;
		}
		if (size > maxBytes)
		{
			error = "torrent file is too large";
			return false;
		}

		std::ifstream file (path, std::ifstream::binary);
		if (!file)
		{
			error = "can't open torrent file";
			return false;
		}
		content.resize (static_cast<size_t> (size));
		file.read (reinterpret_cast<char *> (content.data ()), size);
		if (!file || file.gcount () != static_cast<std::streamsize> (size))
		{
			content.clear ();
			error = "can't read torrent file";
			return false;
		}
		return true;
	}
}
}

#endif // NO_TORRENTS
