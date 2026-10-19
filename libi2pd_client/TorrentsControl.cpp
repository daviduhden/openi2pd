/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TorrentsControl.
 *
 * Implements the bounded control-plane helpers: BEP 9 magnet parsing and
 * building, I2P-only tracker host validation, and symlink-safe bounded
 * .torrent file reads.
 *
 */

#ifndef NO_TORRENTS

#include "TorrentsControl.h"

#include <fstream>
#include <algorithm>
#include "HTTP.h"

namespace i2p
{
namespace torrents
{
	namespace
	{
		bool IsUnreservedMagnetChar (char ch)
		{
			auto uch = static_cast<unsigned char> (ch);
			return (uch >= 'A' && uch <= 'Z') || (uch >= 'a' && uch <= 'z') || (uch >= '0' && uch <= '9') || ch == '-'
				|| ch == '.' || ch == '_' || ch == '~';
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

	std::optional<TorrentMagnet> ParseTorrentMagnet (std::string_view uri)
	{
		if (uri.size () > MAX_CONTROL_ARGUMENT || !uri.starts_with ("magnet:?")
			|| uri.find ('#') != std::string_view::npos)
			return std::nullopt;
		const auto hex = [] (char c) -> int
		{
			if (c >= '0' && c <= '9') return c - '0';
			if (c >= 'a' && c <= 'f') return c - 'a' + 10;
			if (c >= 'A' && c <= 'F') return c - 'A' + 10;
			return -1;
		};
		const auto decode = [&hex] (std::string_view text) -> std::optional<std::string>
		{
			std::string out;
			for (size_t i = 0; i < text.size (); ++i)
			{
				unsigned char c = text[i];
				if (c == '%')
				{
					if (text.size () - i < 3 || hex (text[i + 1]) < 0 || hex (text[i + 2]) < 0) return std::nullopt;
					c = (hex (text[i + 1]) << 4) | hex (text[i + 2]);
					i += 2;
				}
				else if (c == '+')
					c = ' ';
				if (c < 0x20 || c == 0x7f) return std::nullopt;
				out.push_back (c);
			}
			return out;
		};
		TorrentMagnet result;
		bool found = false;
		uri.remove_prefix (8);
		while (!uri.empty ())
		{
			const auto amp = uri.find ('&');
			auto part = uri.substr (0, amp);
			uri = amp == std::string_view::npos ? std::string_view { } : uri.substr (amp + 1);
			const auto equal = part.find ('=');
			if (equal == std::string_view::npos) return std::nullopt;
			auto key = decode (part.substr (0, equal));
			auto value = decode (part.substr (equal + 1));
			if (!key || !value) return std::nullopt;
			if (*key == "xt" && value->starts_with ("urn:btih:"))
			{
				std::string_view hash (*value);
				hash.remove_prefix (9);
				std::array<uint8_t, 20> parsed { };
				if (hash.size () == 40)
				{
					for (size_t i = 0; i < parsed.size (); ++i)
					{
						if (hex (hash[2 * i]) < 0 || hex (hash[2 * i + 1]) < 0) return std::nullopt;
						parsed[i] = (hex (hash[2 * i]) << 4) | hex (hash[2 * i + 1]);
					}
				}
				else if (hash.size () == 32)
				{
					uint32_t bits = 0;
					unsigned count = 0;
					size_t offset = 0;
					for (char c : hash)
					{
						if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
						int n = c >= 'A' && c <= 'Z' ? c - 'A' : c >= '2' && c <= '7' ? c - '2' + 26 : -1;
						if (n < 0) return std::nullopt;
						bits = (bits << 5) | n;
						count += 5;
						if (count >= 8)
						{
							count -= 8;
							parsed[offset++] = bits >> count;
						}
					}
				}
				else
					return std::nullopt;
				if (found && parsed != result.infoHash) return std::nullopt;
				result.infoHash = parsed;
				found = true;
			}
			else if (*key == "dn")
				result.name = std::move (*value);
			else if (*key == "tr")
			{
				i2p::http::URL url (*value);
				if (value->find_first_of (" \t\r\n") == std::string::npos
					&& (url.schema == "http" || url.schema == "udp") && IsI2PTrackerHost (url.host) && url.user.empty ()
					&& url.pass.empty () && !url.ipv6
					&& std::find (result.trackers.begin (), result.trackers.end (), *value) == result.trackers.end ())
				{
					if (result.trackers.size () >= MAX_CONTROL_TRACKERS) return std::nullopt;
					result.trackers.push_back (std::move (*value));
				}
			}
		}
		if (!found) return std::nullopt;
		return result;
	}

	bool IsI2PTrackerHost (std::string_view host)
	{
		static constexpr std::string_view suffix { ".i2p" };
		if (host.size () <= suffix.size ()) return false;
		if (host.find ("..") != std::string_view::npos) return false;
		for (char ch : host)
		{
			const auto byte = static_cast<unsigned char> (ch);
			if (byte <= 0x20 || byte >= 0x7F || ch == '/' || ch == '\\' || ch == '@' || ch == ':' || ch == '?'
				|| ch == '#')
				return false;
		}
		for (size_t i = 0; i < suffix.size (); ++i)
		{
			char actual = host[host.size () - suffix.size () + i];
			if (actual >= 'A' && actual <= 'Z') actual = static_cast<char> (actual - 'A' + 'a');
			if (actual != suffix[i]) return false;
		}
		return true;
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

	std::string BuildTorrentMagnet (const std::array<uint8_t, 20> & infoHash, std::string_view name)
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

	bool ReadTorrentFileBounded (
		const std::filesystem::path & path, size_t maxBytes, std::vector<uint8_t> & content, std::string & error)
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
