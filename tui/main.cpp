/*
* Copyright (c) 2013-2026, The PurpleI2P Project
*
* This file is part of Open I2Pd and licensed under BSD3
*
* See full license text in LICENSE file at top of project tree
*/

#include "TuiClient.h"

#ifndef _WIN32

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <climits>
#include <clocale>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <signal.h>
#include <wchar.h>
#include <ncurses.h>

#include "Log.h"
#include "TuiProtocol.h"
#include "version.h"

namespace
{
	using namespace std::chrono_literals;

	volatile std::sig_atomic_t g_Resize = 0;
	volatile std::sig_atomic_t g_Stop = 0;

	void HandleResize (int) { g_Resize = 1; }
	void HandleStop (int) { g_Stop = 1; }

	enum class Tab
	{
		Status = 0,
		Peers,
		Tunnels,
		Torrents,
		NetDb,
		Logs,
		Help
	};

	constexpr std::array<std::string_view, 7> TAB_NAMES = {
		"Status", "Peers", "Tunnels", "Torrents", "NetDb", "Logs", "Help"
	};

	enum class TorrentSort
	{
		Name = 0,
		Status,
		Progress,
		Down,
		Up,
		Peers
	};

	enum class TorrentDetailTab
	{
		General = 0,
		Files,
		Peers,
		Trackers
	};

	enum class PromptKind
	{
		None = 0,
		Filter,
		AddMagnet,
		AddFile,
		ConfirmRemove,
		ConfirmRemoveFiles
	};

	struct PeerRow
	{
		std::string hash;
		long attempts = 0;
		long sessions = 0;
		long transports = 0;
		bool highBandwidth = false;
	};

	struct TunnelRow
	{
		std::string id;
		std::string direction;
		std::string state;
		std::string hops;
		std::string created;
	};

	struct LogRow
	{
		uint64_t sequence = 0;
		long timestamp = 0;
		int level = 0;
		std::string text;
	};

	struct TorrentRow
	{
		int id = -1;
		std::string name;
		std::string status;
		std::string error;
		std::string hash;
		std::string magnet;
		uint64_t total = 0;
		uint64_t have = 0;
		uint64_t downloaded = 0;
		uint64_t uploaded = 0;
		uint64_t downRate = 0;
		uint64_t upRate = 0;
		int peers = 0;
		int seeders = 0;
		int leechers = 0;
		double percent = -1.0;
		std::optional<uint64_t> eta;
	};

	struct TorrentFileRow
	{
		std::string name;
		uint64_t length = 0;
		uint64_t completed = 0;
	};

	struct TorrentPeerRow
	{
		std::string ident;
		std::string client;
		std::string flags;
		double progress = 0.0;
		uint64_t downRate = 0;
		uint64_t upRate = 0;
	};

	struct TorrentTrackerRow
	{
		std::string announce;
		int peers = 0;
		int seeders = 0;
		int leechers = 0;
		std::string lastResult;
		uint64_t lastAnnounce = 0;
		uint64_t nextAnnounce = 0;
	};

	struct TorrentDetail
	{
		bool loaded = false;
		TorrentRow info;
		std::vector<TorrentFileRow> files;
		std::vector<TorrentPeerRow> peers;
		std::vector<TorrentTrackerRow> trackers;
	};

	struct TorrentTunnelRow
	{
		int index = -1;
		std::string name;
		int torrents = 0;
	};

	struct TorrentSessionInfo
	{
		uint64_t downloaded = 0;
		uint64_t uploaded = 0;
		int active = 0;
		int paused = 0;
	};

	struct TorrentDhtInfo
	{
		bool supported = false;
		int numNodes = 0;
		int numBuckets = 0;
		int numTorrents = 0;
		int numHeardFrom = 0;
		int numFailedPeers = 0;
	};

	struct TorrentLayout
	{
		bool showSize = false;
		bool showDown = false;
		bool showUp = false;
		bool showPeers = false;
		bool showRatio = false;
		bool showError = false;
		int name = 0;
	};

	constexpr int TORRENT_STATUS_COLS = 9;
	constexpr int TORRENT_SIZE_COLS = 9;
	constexpr int TORRENT_PROGRESS_COLS = 7;
	constexpr int TORRENT_DOWN_COLS = 9;
	constexpr int TORRENT_UP_COLS = 9;
	constexpr int TORRENT_PEERS_COLS = 11;
	constexpr int TORRENT_RATIO_COLS = 11;
	constexpr int TORRENT_ERROR_COLS = 14;

	std::vector<std::string_view> Split (std::string_view value, char separator)
	{
		std::vector<std::string_view> parts;
		size_t start = 0;
		while (true)
		{
			auto pos = value.find (separator, start);
			if (pos == std::string_view::npos)
			{
				parts.push_back (value.substr (start));
				break;
			}
			parts.push_back (value.substr (start, pos - start));
			start = pos + 1;
		}
		return parts;
	}

	std::string Trim (std::string_view value)
	{
		while (!value.empty () && (value.front () == ' ' || value.front () == '\t'))
			value.remove_prefix (1);
		while (!value.empty () && (value.back () == ' ' || value.back () == '\t' || value.back () == '\r'))
			value.remove_suffix (1);
		return std::string (value);
	}

	long ToLong (std::string_view value)
	{
		long result = 0;
		for (char ch : value)
		{
			if (ch < '0' || ch > '9') return 0;
			if (result > (LONG_MAX - (ch - '0')) / 10) return 0;
			result = result * 10 + (ch - '0');
		}
		return result;
	}

	uint64_t ToU64 (std::string_view value, uint64_t fallback = 0)
	{
		if (value.empty ()) return fallback;
		uint64_t result = 0;
		auto parsed = std::from_chars (value.data (), value.data () + value.size (), result);
		if (parsed.ec != std::errc () || parsed.ptr != value.data () + value.size ())
			return fallback;
		return result;
	}

	int ToInt (std::string_view value, int fallback = 0)
	{
		if (value.empty ()) return fallback;
		int64_t result = 0;
		auto parsed = std::from_chars (value.data (), value.data () + value.size (), result);
		if (parsed.ec != std::errc () || parsed.ptr != value.data () + value.size ())
			return fallback;
		if (result < 0) return fallback;
		if (result > INT_MAX) return INT_MAX;
		return static_cast<int> (result);
	}

	double ToDouble (std::string_view value, double fallback = 0.0)
	{
		if (value.empty ()) return fallback;
		double result = 0.0;
		auto parsed = std::from_chars (value.data (), value.data () + value.size (), result);
		if (parsed.ec != std::errc () || parsed.ptr != value.data () + value.size ())
			return fallback;
		return result;
	}

	std::string ToLowerAscii (std::string_view value)
	{
		std::string result (value);
		for (char& ch : result)
			if (ch >= 'A' && ch <= 'Z') ch = static_cast<char> (ch - 'A' + 'a');
		return result;
	}

	std::string BoundUtf8 (std::string_view value, size_t limit)
	{
		if (value.size () <= limit) return std::string (value);
		size_t cut = limit;
		while (cut > 0 && cut < value.size () && (static_cast<unsigned char> (value[cut]) & 0xC0) == 0x80)
			--cut;
		return std::string (value.substr (0, cut));
	}

	struct Utf8Character
	{
		size_t length = 1;
		int width = 1;
	};

	Utf8Character NextCharacter (std::string_view text, size_t offset)
	{
		if (offset >= text.size ()) return {1, 0};
		wchar_t character = 0;
		std::mbstate_t state{};
		auto consumed = std::mbrtowc (&character, text.data () + offset, text.size () - offset, &state);
		if (consumed == static_cast<size_t> (-1) || consumed == static_cast<size_t> (-2) || consumed == 0)
			return {1, 1};
		int width = ::wcwidth (character);
		if (width < 0) width = 1;
		return {consumed, width};
	}

	int DisplayWidth (std::string_view text)
	{
		int width = 0;
		size_t offset = 0;
		while (offset < text.size ())
		{
			auto character = NextCharacter (text, offset);
			width += character.width;
			offset += character.length;
		}
		return width;
	}

	std::string SanitizeText (std::string_view text)
	{
		std::string result;
		result.reserve (text.size ());
		for (char ch : text)
		{
			auto byte = static_cast<unsigned char> (ch);
			if (ch == '\n' || ch == '\r' || ch == '\t') result.push_back (' ');
			else if (byte < 0x20 || byte == 0x7F) result.push_back ('?');
			else result.push_back (ch);
		}
		return result;
	}

	std::string TruncateUtf8 (std::string_view text, int maxWidth, bool ellipsis = true)
	{
		if (maxWidth <= 0 || text.empty ()) return {};
		if (DisplayWidth (text) <= maxWidth) return std::string (text);

		std::string suffix;
		if (ellipsis && maxWidth >= 3) suffix = "...";
		int budget = maxWidth - DisplayWidth (suffix);
		if (budget < 0)
		{
			suffix.clear ();
			budget = maxWidth;
		}

		std::string result;
		int used = 0;
		size_t offset = 0;
		while (offset < text.size ())
		{
			auto character = NextCharacter (text, offset);
			if (used + character.width > budget) break;
			result.append (text.substr (offset, character.length));
			used += character.width;
			offset += character.length;
		}
		result += suffix;
		return result;
	}

	std::string FitUtf8 (std::string_view text, int columns, bool rightAlign = false)
	{
		if (columns <= 0) return {};
		auto clean = SanitizeText (text);
		auto cut = TruncateUtf8 (clean, columns, true);
		int padding = columns - DisplayWidth (cut);
		if (padding < 0) padding = 0;
		if (rightAlign)
			return std::string (static_cast<size_t> (padding), ' ') + cut;
		return cut + std::string (static_cast<size_t> (padding), ' ');
	}

	std::string TailUtf8 (std::string_view text, int maxWidth)
	{
		if (maxWidth <= 0 || text.empty ()) return {};
		struct Piece { size_t offset; int width; };
		std::vector<Piece> pieces;
		int total = 0;
		size_t offset = 0;
		while (offset < text.size ())
		{
			auto character = NextCharacter (text, offset);
			pieces.push_back ({offset, character.width});
			total += character.width;
			offset += character.length;
		}
		size_t first = 0;
		while (first < pieces.size () && total > maxWidth)
		{
			total -= pieces[first].width;
			++first;
		}
		if (first >= pieces.size ()) return {};
		return std::string (text.substr (pieces[first].offset));
	}

	std::vector<std::string> WrapUtf8 (std::string_view text, int maxWidth)
	{
		std::vector<std::string> lines;
		if (maxWidth <= 0) return lines;
		std::string line;
		int used = 0;
		size_t offset = 0;
		while (offset < text.size ())
		{
			auto character = NextCharacter (text, offset);
			if (used > 0 && used + character.width > maxWidth)
			{
				lines.push_back (line);
				line.clear ();
				used = 0;
				if (lines.size () >= 32) break;
			}
			line.append (text.substr (offset, character.length));
			used += character.width;
			offset += character.length;
		}
		if (!line.empty ()) lines.push_back (line);
		if (lines.empty ()) lines.emplace_back ();
		return lines;
	}

	void RemoveLastUtf8 (std::string& value)
	{
		if (value.empty ()) return;
		value.pop_back ();
		while (!value.empty () && (static_cast<unsigned char> (value.back ()) & 0xC0) == 0x80)
			value.pop_back ();
	}

	std::string HumanBytes (uint64_t value)
	{
		static constexpr std::array<const char*, 6> units = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
		double amount = static_cast<double> (value);
		size_t unit = 0;
		while (amount >= 1024.0 && unit + 1 < units.size ())
		{
			amount /= 1024.0;
			++unit;
		}
		char buffer[64];
		snprintf (buffer, sizeof (buffer), "%.2f %s", amount, units[unit]);
		return buffer;
	}

	std::string HumanRate (uint64_t bytesPerSecond)
	{
		return HumanBytes (bytesPerSecond) + "/s";
	}

	std::string HumanRateShort (uint64_t bytesPerSecond)
	{
		static constexpr std::array<const char*, 6> units = {"B/s", "K/s", "M/s", "G/s", "T/s", "P/s"};
		double amount = static_cast<double> (bytesPerSecond);
		size_t unit = 0;
		while (amount >= 1024.0 && unit + 1 < units.size ())
		{
			amount /= 1024.0;
			++unit;
		}
		char buffer[32];
		if (amount >= 100.0)
			snprintf (buffer, sizeof (buffer), "%.0f%s", amount, units[unit]);
		else if (amount >= 10.0)
			snprintf (buffer, sizeof (buffer), "%.1f%s", amount, units[unit]);
		else
			snprintf (buffer, sizeof (buffer), "%.2f%s", amount, units[unit]);
		return buffer;
	}

	std::string HumanDuration (uint64_t seconds)
	{
		uint64_t days = seconds / 86400;
		uint64_t hours = (seconds % 86400) / 3600;
		uint64_t minutes = (seconds % 3600) / 60;
		uint64_t secs = seconds % 60;
		char buffer[64];
		if (days)
			snprintf (buffer, sizeof (buffer), "%llud %02lluh", static_cast<unsigned long long> (days),
				static_cast<unsigned long long> (hours));
		else if (hours)
			snprintf (buffer, sizeof (buffer), "%lluh %02llum", static_cast<unsigned long long> (hours),
				static_cast<unsigned long long> (minutes));
		else if (minutes)
			snprintf (buffer, sizeof (buffer), "%llum %02llus", static_cast<unsigned long long> (minutes),
				static_cast<unsigned long long> (secs));
		else
			snprintf (buffer, sizeof (buffer), "%llus", static_cast<unsigned long long> (secs));
		return buffer;
	}

	std::string HumanUptime (long seconds)
	{
		if (seconds < 0) seconds = 0;
		const long days = seconds / 86400;
		const long hours = (seconds % 86400) / 3600;
		const long minutes = (seconds % 3600) / 60;
		const long secs = seconds % 60;
		char buffer[64];
		if (days)
			snprintf (buffer, sizeof (buffer), "%ldd %02ld:%02ld:%02ld", days, hours, minutes, secs);
		else
			snprintf (buffer, sizeof (buffer), "%02ld:%02ld:%02ld", hours, minutes, secs);
		return buffer;
	}

	std::string Timestamp (long epoch)
	{
		std::time_t time = static_cast<std::time_t> (epoch);
		std::tm tm{};
		if (!localtime_r (&time, &tm)) return "--:--:--";
		char buffer[16];
		if (!strftime (buffer, sizeof (buffer), "%H:%M:%S", &tm)) return "--:--:--";
		return buffer;
	}

	std::string Timestamp64 (uint64_t epoch)
	{
		if (!epoch || epoch > static_cast<uint64_t> (LONG_MAX)) return "--:--:--";
		return Timestamp (static_cast<long> (epoch));
	}

	std::string_view LevelName (int level)
	{
		switch (level)
		{
			case eLogCritical: return "CRIT";
			case eLogError: return "ERROR";
			case eLogWarning: return "WARN";
			case eLogInfo: return "INFO";
			case eLogDebug: return "DEBUG";
			default: return "NONE";
		}
	}

	int LevelAttribute (int level)
	{
		switch (level)
		{
			case eLogCritical:
			case eLogError: return COLOR_PAIR (2);
			case eLogWarning: return COLOR_PAIR (3);
			case eLogDebug: return COLOR_PAIR (4);
			default: return 0;
		}
	}

	std::map<std::string, std::string> ParseFieldMap (const std::string& payload)
	{
		std::map<std::string, std::string> fields;
		size_t count = 0;
		for (auto line : Split (payload, '\n'))
		{
			if (line.empty ()) continue;
			if (++count > 8192) break;
			auto equals = line.find ('=');
			if (equals == std::string_view::npos || equals == 0) continue;
			auto name = line.substr (0, equals);
			if (name.size () > i2p::tui::MAX_FIELD_NAME) continue;
			fields[std::string (name)] = BoundUtf8 (i2p::tui::UnescapeValue (line.substr (equals + 1)), 8192);
		}
		return fields;
	}

	const std::string& MapValue (const std::map<std::string, std::string>& fields, std::string_view name)
	{
		static const std::string empty;
		auto it = fields.find (std::string (name));
		return it == fields.end () ? empty : it->second;
	}

	bool SplitIndexedKey (std::string_view key, std::string_view prefix, long& index, std::string_view& field)
	{
		if (!key.starts_with (prefix)) return false;
		auto rest = key.substr (prefix.size ());
		auto dot = rest.find ('.');
		if (dot == std::string_view::npos || dot == 0) return false;
		auto digits = rest.substr (0, dot);
		if (digits.size () > 9) return false;
		long value = 0;
		for (char ch : digits)
		{
			if (ch < '0' || ch > '9') return false;
			value = value * 10 + (ch - '0');
			if (value > INT_MAX) return false;
		}
		index = value;
		field = rest.substr (dot + 1);
		return !field.empty ();
	}

	void AssignTorrentField (TorrentRow& row, std::string_view field, const std::string& value)
	{
		if (field == "id") row.id = ToInt (value, -1);
		else if (field == "name") row.name = BoundUtf8 (value, 4096);
		else if (field == "status") row.status = BoundUtf8 (value, 128);
		else if (field == "error") row.error = BoundUtf8 (value, 2048);
		else if (field == "hash") row.hash = BoundUtf8 (value, 256);
		else if (field == "magnet") row.magnet = BoundUtf8 (value, 2048);
		else if (field == "total") row.total = ToU64 (value);
		else if (field == "have") row.have = ToU64 (value);
		else if (field == "downloaded") row.downloaded = ToU64 (value);
		else if (field == "uploaded") row.uploaded = ToU64 (value);
		else if (field == "downRate") row.downRate = ToU64 (value);
		else if (field == "upRate") row.upRate = ToU64 (value);
		else if (field == "peers") row.peers = ToInt (value);
		else if (field == "seeders") row.seeders = ToInt (value);
		else if (field == "leechers") row.leechers = ToInt (value);
		else if (field == "percent") row.percent = ToDouble (value, row.percent);
		else if (field == "eta")
		{
			auto eta = ToU64 (value);
			if (eta) row.eta = eta;
			else row.eta.reset ();
		}
	}

	void FinalizeTorrent (TorrentRow& row)
	{
		if (row.id < 0) row.id = -1;
		if (row.status.empty ()) row.status = "unknown";
		if (row.percent < 0.0)
		{
			if (row.total)
				row.percent = 100.0 * static_cast<double> (row.have) / static_cast<double> (row.total);
			else
				row.percent = 0.0;
		}
		if (row.percent < 0.0) row.percent = 0.0;
		if (row.percent > 100.0) row.percent = 100.0;
		if (row.eta && !*row.eta) row.eta.reset ();
	}

	std::vector<TorrentRow> BuildTorrentRows (const std::map<std::string, std::string>& fields)
	{
		std::map<long, TorrentRow> rows;
		for (const auto& [key, value] : fields)
		{
			long index = 0;
			std::string_view field;
			if (!SplitIndexedKey (key, "torrent.", index, field)) continue;
			auto it = rows.find (index);
			if (it == rows.end ())
			{
				if (rows.size () >= 1000) continue;
				it = rows.emplace (index, TorrentRow{}).first;
			}
			AssignTorrentField (it->second, field, value);
		}

		std::vector<TorrentRow> result;
		std::set<int> seen;
		for (auto& [index, row] : rows)
		{
			(void) index;
			if (row.id < 0) continue;
			if (!seen.insert (row.id).second) continue;
			FinalizeTorrent (row);
			result.push_back (std::move (row));
			if (result.size () >= 1000) break;
		}
		return result;
	}

	TorrentDetail BuildTorrentDetail (const std::map<std::string, std::string>& fields, int fallbackId)
	{
		TorrentDetail detail;
		detail.info.id = -1;
		detail.info.percent = -1.0;
		std::map<long, TorrentFileRow> files;
		std::map<long, TorrentPeerRow> peers;
		std::map<long, TorrentTrackerRow> trackers;

		for (const auto& [key, value] : fields)
		{
			if (key.starts_with ("detail."))
			{
				AssignTorrentField (detail.info, std::string_view (key).substr (7), value);
				continue;
			}

			long index = 0;
			std::string_view field;
			if (SplitIndexedKey (key, "file.", index, field))
			{
				auto it = files.find (index);
				if (it == files.end ())
				{
					if (files.size () >= 500) continue;
					it = files.emplace (index, TorrentFileRow{}).first;
				}
				auto& file = it->second;
				if (field == "name") file.name = BoundUtf8 (value, 2048);
				else if (field == "length") file.length = ToU64 (value);
				else if (field == "completed") file.completed = ToU64 (value);
			}
			else if (SplitIndexedKey (key, "peer.", index, field))
			{
				auto it = peers.find (index);
				if (it == peers.end ())
				{
					if (peers.size () >= 500) continue;
					it = peers.emplace (index, TorrentPeerRow{}).first;
				}
				auto& peer = it->second;
				if (field == "ident") peer.ident = BoundUtf8 (value, 512);
				else if (field == "client") peer.client = BoundUtf8 (value, 128);
				else if (field == "flags") peer.flags = BoundUtf8 (value, 64);
				else if (field == "progress") peer.progress = ToDouble (value);
				else if (field == "downRate") peer.downRate = ToU64 (value);
				else if (field == "upRate") peer.upRate = ToU64 (value);
			}
			else if (SplitIndexedKey (key, "tracker.", index, field))
			{
				auto it = trackers.find (index);
				if (it == trackers.end ())
				{
					if (trackers.size () >= 500) continue;
					it = trackers.emplace (index, TorrentTrackerRow{}).first;
				}
				auto& tracker = it->second;
				if (field == "announce") tracker.announce = BoundUtf8 (value, 2048);
				else if (field == "peers") tracker.peers = ToInt (value);
				else if (field == "seeders") tracker.seeders = ToInt (value);
				else if (field == "leechers") tracker.leechers = ToInt (value);
				else if (field == "lastResult") tracker.lastResult = BoundUtf8 (value, 512);
				else if (field == "lastAnnounce") tracker.lastAnnounce = ToU64 (value);
				else if (field == "nextAnnounce") tracker.nextAnnounce = ToU64 (value);
			}
		}

		if (detail.info.id < 0) detail.info.id = fallbackId;
		if (detail.info.id < 0) detail.info.id = 0;
		FinalizeTorrent (detail.info);
		detail.loaded = !fields.empty ();
		for (auto& [index, file] : files)
		{
			(void) index;
			detail.files.push_back (std::move (file));
			if (detail.files.size () >= 500) break;
		}
		for (auto& [index, peer] : peers)
		{
			(void) index;
			detail.peers.push_back (std::move (peer));
			if (detail.peers.size () >= 500) break;
		}
		for (auto& [index, tracker] : trackers)
		{
			(void) index;
			detail.trackers.push_back (std::move (tracker));
			if (detail.trackers.size () >= 500) break;
		}
		return detail;
	}

	TorrentLayout ComputeTorrentLayout (int width)
	{
		TorrentLayout layout;
		layout.showSize = width >= 60;
		layout.showDown = width >= 52;
		layout.showUp = width >= 70;
		layout.showPeers = width >= 64;
		layout.showRatio = width >= 84;
		layout.showError = width >= 96;

		auto calculate = [&] ()
		{
			int fixed = TORRENT_STATUS_COLS + TORRENT_PROGRESS_COLS;
			if (layout.showSize) fixed += TORRENT_SIZE_COLS;
			if (layout.showDown) fixed += TORRENT_DOWN_COLS;
			if (layout.showUp) fixed += TORRENT_UP_COLS;
			if (layout.showPeers) fixed += TORRENT_PEERS_COLS;
			if (layout.showRatio) fixed += TORRENT_RATIO_COLS;
			if (layout.showError) fixed += TORRENT_ERROR_COLS;
			layout.name = width - 2 - fixed;
		};
		calculate ();

		while (layout.name < 12)
		{
			if (layout.showError) layout.showError = false;
			else if (layout.showRatio) layout.showRatio = false;
			else if (layout.showUp) layout.showUp = false;
			else if (layout.showSize) layout.showSize = false;
			else if (layout.showDown) layout.showDown = false;
			else if (layout.showPeers) layout.showPeers = false;
			else break;
			calculate ();
		}
		if (layout.name < 4) layout.name = 4;
		return layout;
	}

	std::string SortName (TorrentSort sort)
	{
		switch (sort)
		{
			case TorrentSort::Name: return "name";
			case TorrentSort::Status: return "status";
			case TorrentSort::Progress: return "progress";
			case TorrentSort::Down: return "down";
			case TorrentSort::Up: return "up";
			case TorrentSort::Peers: return "peers";
		}
		return "name";
	}

	bool IsStoppedStatus (std::string_view status)
	{
		auto lower = ToLowerAscii (status);
		return lower.find ("stop") != std::string::npos || lower.find ("pause") != std::string::npos ||
			lower.find ("inactive") != std::string::npos || lower.find ("idle") != std::string::npos;
	}

	bool IsActiveStatus (std::string_view status)
	{
		auto lower = ToLowerAscii (status);
		return lower.find ("start") != std::string::npos || lower.find ("active") != std::string::npos ||
			lower.find ("download") != std::string::npos || lower.find ("seed") != std::string::npos ||
			lower.find ("check") != std::string::npos || lower.find ("announc") != std::string::npos;
	}

	std::string RatioText (const TorrentRow& torrent)
	{
		double ratio = torrent.downloaded ?
			static_cast<double> (torrent.uploaded) / static_cast<double> (torrent.downloaded) : 0.0;
		char buffer[32];
		snprintf (buffer, sizeof (buffer), "%.2f", ratio);
		return buffer;
	}

	std::string RatioEtaText (const TorrentRow& torrent)
	{
		std::string text = RatioText (torrent);
		if (torrent.percent >= 99.95) text += " done";
		else if (torrent.eta) text += " " + HumanDuration (*torrent.eta);
		return text;
	}

	class App
	{
		public:

			explicit App (i2p::tui::ClientOptions options):
				m_Options (std::move (options)), m_Client (m_Options.socketPath)
			{
				m_NextConnect = std::chrono::steady_clock::now ();
				m_NextRefresh = m_NextConnect;
				m_NextTorrentTunnels = m_NextConnect;
				m_NextTorrentList = m_NextConnect;
				m_NextTorrentSession = m_NextConnect;
				m_NextTorrentDht = m_NextConnect;
				m_NextTorrentDetail = m_NextConnect;
			}

			int Run ();

		private:

			void ConnectIfNeeded ();
			void Refresh ();
			bool Request (std::string_view command, std::string& payload);
			void ParseState (const std::string& payload);
			void ParsePeers (const std::string& payload);
			void ParseTunnels (const std::string& payload);
			void ParseDestinations (const std::string& payload);
			void ParseLogs (const std::string& payload);
			const std::string& Field (std::string_view name) const;

			void RefreshTorrents (std::chrono::steady_clock::time_point now);
			void ParseTorrentTunnels (const std::string& payload);
			void ParseTorrents (const std::string& payload);
			void ParseTorrentSession (const std::string& payload);
			void ParseTorrentDht (const std::string& payload);
			void ParseTorrentDetail (const std::string& payload);
			void ParseCommandResult (const std::string& payload);
			void SendTorrentCommand (std::string_view command);
			void ScheduleTorrentRefresh (bool includeTunnels);
			void ResetTorrentAuxState ();
			void RebuildTorrentView (int preferredId);
			int CurrentSelectedId () const;
			const TorrentRow* SelectedTorrent () const;
			const TorrentTunnelRow* ActiveTunnel () const;
			bool RequireTorrentTunnel ();
			void SetStatus (std::string message, bool error);
			void EnterTorrentDetails ();
			void LeaveTorrentDetails ();
			void BeginPrompt (PromptKind kind, std::string label, std::string initial = {});
			bool HandlePromptKey (int key);
			void CommitPrompt ();
			void CancelPrompt ();
			void ToggleSelected ();
			void ReannounceSelected ();
			void RemoveSelected (bool files);
			void CycleTorrentSort ();
			void ToggleTorrentSortDirection ();
			void CycleTorrentTunnel (int delta);
			void EnsureTorrentSelectionVisible (int visible);
			int TorrentPageRows () const;

			void Draw ();
			void DrawHeader (int width);
			void DrawFooter (int height, int width);
			void DrawStatus (int top, int height, int width);
			void DrawPeers (int top, int height, int width);
			void DrawTunnels (int top, int height, int width);
			void DrawNetDb (int top, int height, int width);
			void DrawLogs (int top, int height, int width);
			void DrawHelp (int top, int height, int width);
			void DrawTorrentContext (int width);
			void DrawTorrents (int top, int height, int width);
			void DrawTorrentDetails (int top, int height, int width);
			void DrawTorrentStatusLine (int height, int width);
			void BuildDetailGeneral (std::vector<std::string>& lines, const TorrentRow& info, int width) const;
			void BuildDetailFiles (std::vector<std::string>& lines, const TorrentDetail& detail, int width) const;
			void BuildDetailPeers (std::vector<std::string>& lines, const TorrentDetail& detail, int width) const;
			void BuildDetailTrackers (std::vector<std::string>& lines, const TorrentDetail& detail, int width) const;
			void PutLine (int row, const std::string& text, int width, int attributes = 0);

			void HandleKey (int key);

		private:

			i2p::tui::ClientOptions m_Options;
			i2p::tui::Client m_Client;
			bool m_Connected = false;
			std::string m_Error;
			std::chrono::steady_clock::time_point m_NextConnect;
			std::chrono::steady_clock::time_point m_NextRefresh;
			std::chrono::steady_clock::time_point m_NextLogs;

			Tab m_Tab = Tab::Status;
			std::map<std::string, std::string> m_State;
			std::vector<PeerRow> m_Peers;
			std::vector<TunnelRow> m_Tunnels;
			std::vector<std::string> m_Destinations;
			std::deque<LogRow> m_Logs;
			uint64_t m_LastLogSequence = 0;
			int m_LogLevel = eLogDebug;
			int m_Scroll = 0;
			int m_HelpScroll = 0;
			bool m_Dirty = true;

			std::vector<TorrentTunnelRow> m_TorrentTunnels;
			int m_TorrentTunnel = -1;
			std::string m_TorrentTunnelName;
			std::vector<TorrentRow> m_Torrents;
			std::vector<size_t> m_TorrentView;
			int m_TorrentSelection = 0;
			int m_TorrentScroll = 0;
			TorrentSort m_TorrentSort = TorrentSort::Name;
			bool m_TorrentSortDescending = false;
			std::string m_TorrentFilter;
			bool m_TorrentsLoaded = false;
			TorrentSessionInfo m_TorrentSession;
			bool m_TorrentSessionLoaded = false;
			TorrentDhtInfo m_TorrentDht;
			bool m_TorrentDhtLoaded = false;
			bool m_TorrentDetails = false;
			TorrentDetail m_TorrentDetail;
			TorrentDetailTab m_TorrentDetailTab = TorrentDetailTab::General;
			int m_TorrentDetailScroll = 0;

			std::chrono::steady_clock::time_point m_NextTorrentTunnels;
			std::chrono::steady_clock::time_point m_NextTorrentList;
			std::chrono::steady_clock::time_point m_NextTorrentSession;
			std::chrono::steady_clock::time_point m_NextTorrentDht;
			std::chrono::steady_clock::time_point m_NextTorrentDetail;

			PromptKind m_Prompt = PromptKind::None;
			std::string m_PromptLabel;
			std::string m_InputBuffer;
			int m_PendingTorrentId = -1;
			int m_PendingTorrentTunnel = -1;
			std::string m_PendingTorrentName;

			std::string m_StatusMessage;
			bool m_StatusError = false;
			std::chrono::steady_clock::time_point m_StatusExpiry;
	};

	int App::Run ()
	{
		while (!g_Stop)
		{
			if (g_Resize)
			{
				g_Resize = 0;
				m_Dirty = true;
			}
			ConnectIfNeeded ();
			// Draw before issuing snapshot requests: a slow daemon may block the
			// synchronous client for up to its socket timeout, but the screen
			// already reflects the last complete snapshot.
			if (m_Dirty) Draw ();
			Refresh ();

			int key = getch ();
			if (key == KEY_RESIZE)
			{
				m_Dirty = true;
				continue;
			}
			if (key != ERR) HandleKey (key);
		}
		return 0;
	}

	void App::ConnectIfNeeded ()
	{
		if (m_Connected) return;
		auto now = std::chrono::steady_clock::now ();
		if (now < m_NextConnect) return;
		m_NextConnect = now + 2s;

		auto connected = m_Client.Connect ();
		if (!connected)
		{
			m_Error = connected.error ();
			m_Dirty = true;
			return;
		}
		m_Connected = true;
		m_Error.clear ();
		m_NextRefresh = now;
		ResetTorrentAuxState ();
		ScheduleTorrentRefresh (true);
		SetStatus ("connected to daemon", false);
		m_Dirty = true;
	}

	bool App::Request (std::string_view command, std::string& payload)
	{
		auto response = m_Client.Request (command);
		if (!response)
		{
			const auto& error = response.error ();
			if (error.kind == i2p::tui::RequestErrorKind::Remote)
			{
				// A valid remote error leaves the socket usable; show it in the
				// status line instead of dropping the connection.
				SetStatus (error.message, true);
				return false;
			}
			m_Error = error.message;
			m_Connected = false;
			m_Client.Disconnect ();
			m_NextConnect = std::chrono::steady_clock::now () + 2s;
			m_Dirty = true;
			return false;
		}
		payload = std::move (*response);
		return true;
	}

	void App::Refresh ()
	{
		if (!m_Connected) return;
		auto now = std::chrono::steady_clock::now ();

		if (now >= m_NextRefresh)
		{
			m_NextRefresh = now + 2s;
			std::string payload;
			if (Request ("STATE", payload)) ParseState (payload);

			if (m_Tab == Tab::Peers && Request ("PEERS 200", payload))
				ParsePeers (payload);
			else if (m_Tab == Tab::Tunnels && Request ("TUNNELS", payload))
				ParseTunnels (payload);
			else if (m_Tab == Tab::NetDb && Request ("DESTINATIONS", payload))
				ParseDestinations (payload);

			if (m_Tab == Tab::Logs && now >= m_NextLogs)
			{
				m_NextLogs = now + 1s;
				std::string command = "LOGS " + std::to_string (m_LastLogSequence) + " 200";
				if (Request (command, payload)) ParseLogs (payload);
			}
		}

		if (m_Tab == Tab::Torrents) RefreshTorrents (now);
	}

	void App::RefreshTorrents (std::chrono::steady_clock::time_point now)
	{
		std::string payload;

		if (now >= m_NextTorrentTunnels)
		{
			m_NextTorrentTunnels = now + 5s;
			if (Request ("TORRENT-TUNNELS", payload)) ParseTorrentTunnels (payload);
			return;
		}
		if (m_TorrentTunnel < 0) return;

		if (now >= m_NextTorrentList)
		{
			m_NextTorrentList = now + 2s;
			std::string command = "TORRENTS " + std::to_string (m_TorrentTunnel) + " " + std::to_string (i2p::tui::MAX_TORRENT_RECORDS_PER_REQUEST);
			if (Request (command, payload)) ParseTorrents (payload);
			return;
		}
		if (now >= m_NextTorrentSession)
		{
			m_NextTorrentSession = now + 5s;
			std::string command = "TORRENT-SESSION " + std::to_string (m_TorrentTunnel);
			if (Request (command, payload)) ParseTorrentSession (payload);
			return;
		}
		if (now >= m_NextTorrentDht)
		{
			m_NextTorrentDht = now + 5s;
			std::string command = "TORRENT-DHT " + std::to_string (m_TorrentTunnel);
			if (Request (command, payload)) ParseTorrentDht (payload);
			return;
		}
		if (m_TorrentDetails && now >= m_NextTorrentDetail)
		{
			auto selected = SelectedTorrent ();
			int id = selected ? selected->id : m_TorrentDetail.info.id;
			if (id < 0) return;
			m_NextTorrentDetail = now + 2s;
			std::string command = "TORRENT-DETAIL " + std::to_string (m_TorrentTunnel) + " " + std::to_string (id);
			if (Request (command, payload)) ParseTorrentDetail (payload);
		}
	}

	void App::ParseState (const std::string& payload)
	{
		m_State.clear ();
		for (auto line : Split (payload, '\n'))
		{
			if (line.empty ()) continue;
			auto equals = line.find ('=');
			if (equals == std::string_view::npos) continue;
			auto name = std::string (line.substr (0, equals));
			if (name.size () > i2p::tui::MAX_FIELD_NAME) continue;
			m_State[name] = i2p::tui::UnescapeValue (line.substr (equals + 1));
		}
	}

	void App::ParsePeers (const std::string& payload)
	{
		m_Peers.clear ();
		for (auto line : Split (payload, '\n'))
		{
			if (!line.starts_with ("peer=")) continue;
			auto value = i2p::tui::UnescapeValue (line.substr (5));
			auto parts = Split (value, '|');
			if (parts.size () != 5) continue;
			PeerRow row;
			row.hash = std::string (parts[0]);
			row.attempts = ToLong (parts[1]);
			row.sessions = ToLong (parts[2]);
			row.transports = ToLong (parts[3]);
			row.highBandwidth = parts[4] == "1";
			m_Peers.push_back (std::move (row));
		}
	}

	void App::ParseTunnels (const std::string& payload)
	{
		m_Tunnels.clear ();
		for (auto line : Split (payload, '\n'))
		{
			if (!line.starts_with ("tunnel=")) continue;
			auto value = i2p::tui::UnescapeValue (line.substr (7));
			auto parts = Split (value, '|');
			if (parts.size () != 5) continue;
			m_Tunnels.push_back (TunnelRow{std::string (parts[0]), std::string (parts[1]),
				std::string (parts[2]), std::string (parts[3]), std::string (parts[4])});
		}
	}

	void App::ParseDestinations (const std::string& payload)
	{
		m_Destinations.clear ();
		for (auto line : Split (payload, '\n'))
		{
			if (!line.starts_with ("destination=")) continue;
			auto value = i2p::tui::UnescapeValue (line.substr (12));
			auto separator = value.find ('|');
			if (separator != std::string::npos) value.resize (separator);
			m_Destinations.push_back (std::move (value));
		}
	}

	void App::ParseLogs (const std::string& payload)
	{
		for (auto line : Split (payload, '\n'))
		{
			if (!line.starts_with ("log=")) continue;
			auto value = i2p::tui::UnescapeValue (line.substr (4));
			auto parts = Split (value, '|');
			if (parts.size () < 4) continue;
			LogRow row;
			row.sequence = static_cast<uint64_t> (ToLong (parts[0]));
			if (row.sequence <= m_LastLogSequence) continue;
			row.timestamp = ToLong (parts[1]);
			row.level = static_cast<int> (ToLong (parts[2]));
			// The message may contain '|'; only the first three are separators.
			auto textStart = value.find ('|', value.find ('|', value.find ('|') + 1) + 1);
			row.text = textStart == std::string::npos ? std::string (parts[3]) : value.substr (textStart + 1);
			m_LastLogSequence = row.sequence;
			m_Logs.push_back (std::move (row));
			if (m_Logs.size () > 2000) m_Logs.pop_front ();
		}
	}

	const std::string& App::Field (std::string_view name) const
	{
		static const std::string empty;
		auto it = m_State.find (std::string (name));
		return it == m_State.end () ? empty : it->second;
	}

	void App::ParseTorrentTunnels (const std::string& payload)
	{
		auto fields = ParseFieldMap (payload);
		std::map<long, TorrentTunnelRow> rows;
		for (const auto& [key, value] : fields)
		{
			long index = 0;
			std::string_view field;
			if (!SplitIndexedKey (key, "torrent.tunnel.", index, field)) continue;
			auto it = rows.find (index);
			if (it == rows.end ())
			{
				if (rows.size () >= 256) continue;
				it = rows.emplace (index, TorrentTunnelRow{}).first;
			}
			auto& row = it->second;
			if (row.index < 0) row.index = static_cast<int> (index);
			if (field == "name") row.name = BoundUtf8 (value, 512);
			else if (field == "torrents") row.torrents = ToInt (value);
			else if (field == "index") row.index = ToInt (value, row.index);
		}

		std::vector<TorrentTunnelRow> tunnels;
		for (auto& [index, row] : rows)
		{
			(void) index;
			if (row.index < 0) continue;
			if (row.name.empty ()) row.name = "tunnel " + std::to_string (row.index);
			tunnels.push_back (std::move (row));
			if (tunnels.size () >= 256) break;
		}
		std::sort (tunnels.begin (), tunnels.end (), [] (const TorrentTunnelRow& left, const TorrentTunnelRow& right)
		{
			if (left.index != right.index) return left.index < right.index;
			return left.name < right.name;
		});

		int previous = m_TorrentTunnel;
		auto previousName = m_TorrentTunnelName;
		m_TorrentTunnels = std::move (tunnels);
		bool found = false;
		for (const auto& tunnel : m_TorrentTunnels)
		{
			// The index is only a position in the sorted snapshot: after a
			// tunnel is added or removed it can name a different tunnel. Keep
			// the previous selection only if both index and name still match.
			if (tunnel.index == previous && tunnel.name == previousName)
			{
				found = true;
				break;
			}
		}
		m_TorrentTunnel = found ? previous : (m_TorrentTunnels.empty () ? -1 : m_TorrentTunnels.front ().index);
		m_TorrentTunnelName.clear ();
		for (const auto& tunnel : m_TorrentTunnels)
		{
			if (tunnel.index == m_TorrentTunnel)
			{
				m_TorrentTunnelName = tunnel.name;
				break;
			}
		}

		bool clear = !found || (m_TorrentTunnels.empty () && !m_Torrents.empty ());
		if (clear)
		{
			m_Torrents.clear ();
			m_TorrentView.clear ();
			m_TorrentsLoaded = false;
			m_TorrentDetails = false;
			m_TorrentDetail = TorrentDetail{};
			m_TorrentSelection = 0;
			m_TorrentScroll = 0;
			ResetTorrentAuxState ();
			auto now = std::chrono::steady_clock::now ();
			m_NextTorrentList = now;
			m_NextTorrentSession = now;
			m_NextTorrentDht = now;
			m_NextTorrentDetail = now;
		}
		m_Dirty = true;
	}

	void App::ParseTorrents (const std::string& payload)
	{
		int preferred = CurrentSelectedId ();
		if (preferred < 0 && m_TorrentDetails) preferred = m_TorrentDetail.info.id;

		auto fields = ParseFieldMap (payload);
		m_Torrents = BuildTorrentRows (fields);
		m_TorrentsLoaded = true;
		RebuildTorrentView (preferred);

		if (m_TorrentDetails)
		{
			bool present = false;
			for (const auto& torrent : m_Torrents)
			{
				if (torrent.id == m_TorrentDetail.info.id)
				{
					present = true;
					break;
				}
			}
			if (!present)
			{
				m_TorrentDetails = false;
				m_TorrentDetail = TorrentDetail{};
				SetStatus ("selected torrent is no longer present", true);
			}
		}
		m_Dirty = true;
	}

	void App::ParseTorrentSession (const std::string& payload)
	{
		auto fields = ParseFieldMap (payload);
		m_TorrentSession.downloaded = ToU64 (MapValue (fields, "session.downloaded"));
		m_TorrentSession.uploaded = ToU64 (MapValue (fields, "session.uploaded"));
		m_TorrentSession.active = ToInt (MapValue (fields, "session.active"));
		m_TorrentSession.paused = ToInt (MapValue (fields, "session.paused"));
		m_TorrentSessionLoaded = true;
		m_Dirty = true;
	}

	void App::ParseTorrentDht (const std::string& payload)
	{
		auto fields = ParseFieldMap (payload);
		auto supported = MapValue (fields, "dht.supported");
		m_TorrentDht.supported = supported == "1" || ToLowerAscii (supported) == "true" ||
			ToLowerAscii (supported) == "yes";
		m_TorrentDht.numNodes = ToInt (MapValue (fields, "dht.numNodes"));
		m_TorrentDht.numBuckets = ToInt (MapValue (fields, "dht.numBuckets"));
		m_TorrentDht.numTorrents = ToInt (MapValue (fields, "dht.numTorrents"));
		m_TorrentDht.numHeardFrom = ToInt (MapValue (fields, "dht.numHeardFrom"));
		m_TorrentDht.numFailedPeers = ToInt (MapValue (fields, "dht.numFailedPeers"));
		m_TorrentDhtLoaded = true;
		m_Dirty = true;
	}

	void App::ParseTorrentDetail (const std::string& payload)
	{
		int fallbackId = CurrentSelectedId ();
		if (fallbackId < 0) fallbackId = m_TorrentDetail.info.id;
		m_TorrentDetail = BuildTorrentDetail (ParseFieldMap (payload), fallbackId);
		m_TorrentDetailScroll = std::max (0, m_TorrentDetailScroll);
		m_Dirty = true;
	}

	void App::ParseCommandResult (const std::string& payload)
	{
		auto fields = ParseFieldMap (payload);
		auto result = ToLowerAscii (MapValue (fields, "result"));
		auto message = MapValue (fields, "message");
		bool error = result == "error";
		if (message.empty ())
			message = error ? "daemon rejected the command" : "command accepted";
		SetStatus (message, error);
	}

	void App::SendTorrentCommand (std::string_view command)
	{
		if (!m_Connected)
		{
			SetStatus ("daemon is not connected", true);
			return;
		}
		if (command.size () > 4000)
		{
			SetStatus ("command is too long", true);
			return;
		}
		std::string payload;
		if (!Request (command, payload)) return;
		ParseCommandResult (payload);
		ScheduleTorrentRefresh (false);
	}

	void App::ScheduleTorrentRefresh (bool includeTunnels)
	{
		auto now = std::chrono::steady_clock::now ();
		if (includeTunnels) m_NextTorrentTunnels = now;
		m_NextTorrentList = now;
		m_NextTorrentSession = now;
		m_NextTorrentDht = now;
		m_NextTorrentDetail = now;
	}

	void App::ResetTorrentAuxState ()
	{
		// Session and DHT counters belong to one tunnel (and one daemon
		// instance). Never keep showing the previous values while the new
		// snapshot is pending or if it fails.
		m_TorrentSession = TorrentSessionInfo{};
		m_TorrentSessionLoaded = false;
		m_TorrentDht = TorrentDhtInfo{};
		m_TorrentDhtLoaded = false;
	}

	int App::CurrentSelectedId () const
	{
		if (m_TorrentSelection < 0 || m_TorrentSelection >= static_cast<int> (m_TorrentView.size ()))
			return -1;
		size_t index = m_TorrentView[m_TorrentSelection];
		if (index >= m_Torrents.size ()) return -1;
		return m_Torrents[index].id;
	}

	const TorrentRow* App::SelectedTorrent () const
	{
		if (m_TorrentSelection < 0 || m_TorrentSelection >= static_cast<int> (m_TorrentView.size ()))
			return nullptr;
		size_t index = m_TorrentView[m_TorrentSelection];
		if (index >= m_Torrents.size ()) return nullptr;
		return &m_Torrents[index];
	}

	const TorrentTunnelRow* App::ActiveTunnel () const
	{
		for (const auto& tunnel : m_TorrentTunnels)
			if (tunnel.index == m_TorrentTunnel) return &tunnel;
		return nullptr;
	}

	bool App::RequireTorrentTunnel ()
	{
		if (!m_Connected)
		{
			SetStatus ("daemon is not connected", true);
			return false;
		}
		if (m_TorrentTunnel < 0 || !ActiveTunnel ())
		{
			SetStatus ("no torrent tunnel is available", true);
			return false;
		}
		return true;
	}

	void App::SetStatus (std::string message, bool error)
	{
		m_StatusMessage = BoundUtf8 (SanitizeText (message), 1024);
		m_StatusError = error;
		m_StatusExpiry = std::chrono::steady_clock::now () + (error ? 20s : 8s);
		m_Dirty = true;
	}

	void App::RebuildTorrentView (int preferredId)
	{
		auto filter = ToLowerAscii (m_TorrentFilter);
		m_TorrentView.clear ();
		for (size_t i = 0; i < m_Torrents.size (); ++i)
		{
			const auto& torrent = m_Torrents[i];
			if (!filter.empty ())
			{
				auto name = ToLowerAscii (torrent.name);
				auto hash = ToLowerAscii (torrent.hash);
				if (name.find (filter) == std::string::npos && hash.find (filter) == std::string::npos)
					continue;
			}
			m_TorrentView.push_back (i);
		}

		auto less = [&] (size_t leftIndex, size_t rightIndex)
		{
			const auto& left = m_Torrents[leftIndex];
			const auto& right = m_Torrents[rightIndex];
			int comparison = 0;
			switch (m_TorrentSort)
			{
				case TorrentSort::Name:
					comparison = ToLowerAscii (left.name).compare (ToLowerAscii (right.name));
					break;
				case TorrentSort::Status:
					comparison = ToLowerAscii (left.status).compare (ToLowerAscii (right.status));
					break;
				case TorrentSort::Progress:
					comparison = left.percent < right.percent ? -1 : (left.percent > right.percent ? 1 : 0);
					break;
				case TorrentSort::Down:
					comparison = left.downRate < right.downRate ? -1 : (left.downRate > right.downRate ? 1 : 0);
					break;
				case TorrentSort::Up:
					comparison = left.upRate < right.upRate ? -1 : (left.upRate > right.upRate ? 1 : 0);
					break;
				case TorrentSort::Peers:
					comparison = left.peers < right.peers ? -1 : (left.peers > right.peers ? 1 : 0);
					break;
			}
			if (comparison == 0) comparison = left.id < right.id ? -1 : (left.id > right.id ? 1 : 0);
			return m_TorrentSortDescending ? comparison > 0 : comparison < 0;
		};
		std::stable_sort (m_TorrentView.begin (), m_TorrentView.end (), less);

		int selection = 0;
		if (preferredId >= 0)
		{
			for (size_t i = 0; i < m_TorrentView.size (); ++i)
			{
				if (m_Torrents[m_TorrentView[i]].id == preferredId)
				{
					selection = static_cast<int> (i);
					break;
				}
			}
		}
		if (m_TorrentView.empty ()) selection = 0;
		else selection = std::clamp (selection, 0, static_cast<int> (m_TorrentView.size ()) - 1);
		m_TorrentSelection = selection;
		m_TorrentScroll = std::clamp (m_TorrentScroll, 0, std::max (0, static_cast<int> (m_TorrentView.size ()) - 1));
		if (m_TorrentSelection < m_TorrentScroll) m_TorrentScroll = m_TorrentSelection;
	}

	void App::EnsureTorrentSelectionVisible (int visible)
	{
		if (m_TorrentView.empty () || visible <= 0) return;
		m_TorrentSelection = std::clamp (m_TorrentSelection, 0, static_cast<int> (m_TorrentView.size ()) - 1);
		if (m_TorrentSelection < m_TorrentScroll) m_TorrentScroll = m_TorrentSelection;
		if (m_TorrentSelection >= m_TorrentScroll + visible)
			m_TorrentScroll = m_TorrentSelection - visible + 1;
		int maximum = std::max (0, static_cast<int> (m_TorrentView.size ()) - visible);
		m_TorrentScroll = std::clamp (m_TorrentScroll, 0, maximum);
	}

	int App::TorrentPageRows () const
	{
		int height = 0;
		int width = 0;
		getmaxyx (stdscr, height, width);
		(void) width;
		int content = std::max (0, height - 4);
		return std::max (1, content - 1);
	}

	void App::EnterTorrentDetails ()
	{
		auto torrent = SelectedTorrent ();
		if (!torrent)
		{
			SetStatus ("no torrent selected", true);
			return;
		}
		if (!RequireTorrentTunnel ()) return;
		m_TorrentDetails = true;
		m_TorrentDetail = TorrentDetail{};
		m_TorrentDetail.info = *torrent;
		m_TorrentDetail.loaded = false;
		m_TorrentDetailTab = TorrentDetailTab::General;
		m_TorrentDetailScroll = 0;
		m_NextTorrentDetail = std::chrono::steady_clock::now ();
		m_Dirty = true;
	}

	void App::LeaveTorrentDetails ()
	{
		m_TorrentDetails = false;
		m_TorrentDetail = TorrentDetail{};
		m_Dirty = true;
	}

	void App::BeginPrompt (PromptKind kind, std::string label, std::string initial)
	{
		m_Prompt = kind;
		m_PromptLabel = std::move (label);
		m_InputBuffer = std::move (initial);
		if (m_InputBuffer.size () > 2048) m_InputBuffer.resize (2048);
		m_Dirty = true;
	}

	bool App::HandlePromptKey (int key)
	{
		if (key == 27)
		{
			CancelPrompt ();
			return true;
		}
		if (key == '\n' || key == '\r' || key == KEY_ENTER)
		{
			CommitPrompt ();
			return true;
		}
		if (key == KEY_BACKSPACE || key == 127 || key == 8)
		{
			RemoveLastUtf8 (m_InputBuffer);
			m_Dirty = true;
			return true;
		}
		if (key == KEY_DC)
		{
			m_InputBuffer.clear ();
			m_Dirty = true;
			return true;
		}
		if (key >= 32 && key < 256 && key != 127)
		{
			if (m_InputBuffer.size () < 2048)
				m_InputBuffer.push_back (static_cast<char> (key));
			m_Dirty = true;
			return true;
		}
		// Modal input swallows navigation and action keys.
		return true;
	}

	void App::CommitPrompt ()
	{
		auto kind = m_Prompt;
		auto input = m_InputBuffer;
		m_Prompt = PromptKind::None;
		m_InputBuffer.clear ();
		m_Dirty = true;

		if (kind == PromptKind::Filter)
		{
			m_TorrentFilter = BoundUtf8 (Trim (input), 256);
			int preferred = CurrentSelectedId ();
			RebuildTorrentView (preferred);
			m_TorrentScroll = 0;
			SetStatus (m_TorrentFilter.empty () ? "filter cleared" : "filter: " + m_TorrentFilter, false);
			return;
		}

		if (kind == PromptKind::AddMagnet)
		{
			auto magnet = Trim (input);
			if (magnet.empty ())
			{
				SetStatus ("no magnet supplied", true);
				return;
			}
			if (magnet.size () > 2048)
			{
				SetStatus ("magnet is too long", true);
				return;
			}
			if (magnet.find ('\n') != std::string::npos || magnet.find ('\r') != std::string::npos)
			{
				SetStatus ("magnet contains a line break", true);
				return;
			}
			if (!RequireTorrentTunnel ()) return;
			SendTorrentCommand ("TORRENT-ADD-MAGNET " + std::to_string (m_TorrentTunnel) + " " + magnet);
			return;
		}

		if (kind == PromptKind::AddFile)
		{
			if (input.empty () || Trim (input).empty ())
			{
				SetStatus ("no .torrent path supplied", true);
				return;
			}
			if (input.size () > 2048)
			{
				SetStatus ("path is too long", true);
				return;
			}
			if (input.find ('\n') != std::string::npos || input.find ('\r') != std::string::npos)
			{
				SetStatus ("path contains a line break", true);
				return;
			}
			if (!RequireTorrentTunnel ()) return;
			// The daemon validates and reads the file; the TUI never opens it.
			SendTorrentCommand ("TORRENT-ADD-FILE " + std::to_string (m_TorrentTunnel) + " " + input);
			return;
		}

		if (kind == PromptKind::ConfirmRemove || kind == PromptKind::ConfirmRemoveFiles)
		{
			auto answer = ToLowerAscii (Trim (input));
			bool confirmed = kind == PromptKind::ConfirmRemoveFiles ? answer == "yes" :
				(answer == "y" || answer == "yes");
			if (!confirmed)
			{
				SetStatus ("removal cancelled", false);
				m_PendingTorrentId = -1;
				m_PendingTorrentTunnel = -1;
				m_PendingTorrentName.clear ();
				return;
			}
			if (!RequireTorrentTunnel ())
			{
				m_PendingTorrentId = -1;
				m_PendingTorrentTunnel = -1;
				m_PendingTorrentName.clear ();
				return;
			}
			bool present = false;
			if (m_PendingTorrentId >= 0 && m_PendingTorrentTunnel == m_TorrentTunnel)
			{
				for (const auto& torrent : m_Torrents)
				{
					// The id alone is not enough: a reloaded session can reuse
					// ids. Match the name captured when the prompt was opened.
					if (torrent.id == m_PendingTorrentId && torrent.name == m_PendingTorrentName)
					{
						present = true;
						break;
					}
				}
			}
			if (!present)
			{
				SetStatus ("selected torrent changed; removal cancelled", true);
				m_PendingTorrentId = -1;
				m_PendingTorrentTunnel = -1;
				m_PendingTorrentName.clear ();
				return;
			}
			std::string command = "TORRENT-REMOVE " + std::to_string (m_TorrentTunnel) + " " +
				std::to_string (m_PendingTorrentId);
			if (kind == PromptKind::ConfirmRemoveFiles) command += " files";
			m_PendingTorrentId = -1;
			m_PendingTorrentTunnel = -1;
			m_PendingTorrentName.clear ();
			SendTorrentCommand (command);
			return;
		}
	}

	void App::CancelPrompt ()
	{
		m_Prompt = PromptKind::None;
		m_InputBuffer.clear ();
		m_PendingTorrentId = -1;
		m_PendingTorrentTunnel = -1;
		m_PendingTorrentName.clear ();
		SetStatus ("cancelled", false);
	}

	void App::ToggleSelected ()
	{
		auto torrent = SelectedTorrent ();
		if (!torrent)
		{
			SetStatus ("no torrent selected", true);
			return;
		}
		if (!RequireTorrentTunnel ()) return;
		bool stop = IsActiveStatus (torrent->status) && !IsStoppedStatus (torrent->status);
		std::string command = std::string (stop ? "TORRENT-STOP " : "TORRENT-START ") +
			std::to_string (m_TorrentTunnel) + " " + std::to_string (torrent->id);
		SendTorrentCommand (command);
	}

	void App::ReannounceSelected ()
	{
		auto torrent = SelectedTorrent ();
		if (!torrent)
		{
			SetStatus ("no torrent selected", true);
			return;
		}
		if (!RequireTorrentTunnel ()) return;
		SendTorrentCommand ("TORRENT-REANNOUNCE " + std::to_string (m_TorrentTunnel) + " " +
			std::to_string (torrent->id));
	}

	void App::RemoveSelected (bool files)
	{
		auto torrent = SelectedTorrent ();
		if (!torrent)
		{
			SetStatus ("no torrent selected", true);
			return;
		}
		if (!RequireTorrentTunnel ()) return;
		m_PendingTorrentId = torrent->id;
		m_PendingTorrentTunnel = m_TorrentTunnel;
		m_PendingTorrentName = torrent->name;
		auto name = TruncateUtf8 (SanitizeText (torrent->name), 60);
		if (files)
			BeginPrompt (PromptKind::ConfirmRemoveFiles,
				"Type YES to remove \"" + name + "\" and local data: ");
		else
			BeginPrompt (PromptKind::ConfirmRemove, "Remove \"" + name + "\"? [y/N] ");
	}

	void App::CycleTorrentSort ()
	{
		switch (m_TorrentSort)
		{
			case TorrentSort::Name:
				m_TorrentSort = TorrentSort::Status;
				m_TorrentSortDescending = false;
				break;
			case TorrentSort::Status:
				m_TorrentSort = TorrentSort::Progress;
				m_TorrentSortDescending = true;
				break;
			case TorrentSort::Progress:
				m_TorrentSort = TorrentSort::Down;
				m_TorrentSortDescending = true;
				break;
			case TorrentSort::Down:
				m_TorrentSort = TorrentSort::Up;
				m_TorrentSortDescending = true;
				break;
			case TorrentSort::Up:
				m_TorrentSort = TorrentSort::Peers;
				m_TorrentSortDescending = true;
				break;
			case TorrentSort::Peers:
				m_TorrentSort = TorrentSort::Name;
				m_TorrentSortDescending = false;
				break;
		}
		int preferred = CurrentSelectedId ();
		RebuildTorrentView (preferred);
		SetStatus ("sort: " + SortName (m_TorrentSort) + (m_TorrentSortDescending ? " desc" : " asc"), false);
	}

	void App::ToggleTorrentSortDirection ()
	{
		m_TorrentSortDescending = !m_TorrentSortDescending;
		int preferred = CurrentSelectedId ();
		RebuildTorrentView (preferred);
		SetStatus ("sort: " + SortName (m_TorrentSort) + (m_TorrentSortDescending ? " desc" : " asc"), false);
	}

	void App::CycleTorrentTunnel (int delta)
	{
		if (m_TorrentTunnels.empty ())
		{
			m_TorrentTunnel = -1;
			m_TorrentTunnelName.clear ();
			SetStatus ("no torrent tunnel is available", true);
			return;
		}
		int position = 0;
		for (size_t i = 0; i < m_TorrentTunnels.size (); ++i)
		{
			if (m_TorrentTunnels[i].index == m_TorrentTunnel)
			{
				position = static_cast<int> (i);
				break;
			}
		}
		int count = static_cast<int> (m_TorrentTunnels.size ());
		position = (position + delta % count + count) % count;
		m_TorrentTunnel = m_TorrentTunnels[position].index;
		m_TorrentTunnelName = m_TorrentTunnels[position].name;
		m_Torrents.clear ();
		m_TorrentView.clear ();
		m_TorrentsLoaded = false;
		m_TorrentDetails = false;
		m_TorrentDetail = TorrentDetail{};
		m_TorrentSelection = 0;
		m_TorrentScroll = 0;
		ResetTorrentAuxState ();
		m_NextTorrentList = std::chrono::steady_clock::now ();
		m_NextTorrentSession = m_NextTorrentList;
		m_NextTorrentDht = m_NextTorrentList;
		m_NextTorrentDetail = m_NextTorrentList;
		SetStatus ("tunnel: " + m_TorrentTunnels[position].name, false);
	}

	void App::PutLine (int row, const std::string& text, int width, int attributes)
	{
		move (row, 0);
		clrtoeol ();
		if (attributes) attrset (attributes);
		auto safe = TruncateUtf8 (SanitizeText (text), width, true);
		if (!safe.empty ())
			addnstr (safe.c_str (), static_cast<int> (safe.size ()));
		if (attributes) attrset (A_NORMAL);
	}

	void App::DrawHeader (int width)
	{
		attrset (A_REVERSE | A_BOLD);
		PutLine (0, " Open I2Pd TUI ", width);
		attrset (A_NORMAL);

		int column = 18;
		for (size_t i = 0; i < TAB_NAMES.size (); ++i)
		{
			if (column >= width) break;
			std::string label = " " + std::to_string (i + 1) + ":" + std::string (TAB_NAMES[i]) + " ";
			if (static_cast<size_t> (m_Tab) == i) attrset (A_REVERSE | A_BOLD);
			move (0, column);
			addnstr (label.c_str (), width - column);
			if (static_cast<size_t> (m_Tab) == i) attrset (A_NORMAL);
			column += static_cast<int> (label.size ());
		}
	}

	void App::DrawFooter (int height, int width)
	{
		std::string line = " ";
		line += m_Connected ? "connected" : "disconnected";
		if (!m_Error.empty ()) line += " | " + m_Error;
		line += " | socket " + m_Options.socketPath;
		if (m_Tab == Tab::Torrents)
			line += " | a/A add p start/stop R reannounce d/D remove x/z all / search s sort ? help";
		else
			line += " | q quit, Tab/1-7 switch, r refresh, ? help";
		attrset (A_REVERSE);
		PutLine (height - 1, line, width);
		attrset (A_NORMAL);
	}

	void App::DrawTorrentContext (int width)
	{
		std::string line = " Torrents: ";
		if (!m_Connected)
		{
			line += "daemon disconnected, reconnecting";
		}
		else if (m_TorrentTunnels.empty ())
		{
			line += "no torrent service tunnel";
		}
		else
		{
			auto tunnel = ActiveTunnel ();
			size_t position = 0;
			for (size_t i = 0; i < m_TorrentTunnels.size (); ++i)
				if (m_TorrentTunnels[i].index == m_TorrentTunnel) position = i;
			line += "tunnel " + (tunnel ? tunnel->name : std::string ("?")) + " [" +
				std::to_string (m_TorrentTunnel) + "] " + std::to_string (position + 1) + "/" +
				std::to_string (m_TorrentTunnels.size ());
			line += " | sort " + SortName (m_TorrentSort) + (m_TorrentSortDescending ? " desc" : " asc");
			if (!m_TorrentFilter.empty ())
				line += " | filter " + TruncateUtf8 (m_TorrentFilter, 40);
			line += " | ";
			if (m_TorrentSessionLoaded)
				line += "session down " + HumanBytes (m_TorrentSession.downloaded) + " up " +
					HumanBytes (m_TorrentSession.uploaded) + " active " +
					std::to_string (m_TorrentSession.active) + " paused " +
					std::to_string (m_TorrentSession.paused);
			else
				line += "session loading";
			line += " | DHT ";
			if (!m_TorrentDhtLoaded)
				line += "loading";
			else if (!m_TorrentDht.supported)
				line += "unsupported";
			else
				line += std::to_string (m_TorrentDht.numNodes) + " nodes, " +
					std::to_string (m_TorrentDht.numBuckets) + " buckets, " +
					std::to_string (m_TorrentDht.numTorrents) + " torrents";
		}
		PutLine (1, line, width, A_BOLD);
	}

	void App::DrawStatus (int top, int height, int width)
	{
		int row = top;
		auto show = [&] (std::string_view label, std::string_view value)
		{
			if (row >= top + height) return;
			move (row, 1);
			attrset (A_BOLD);
			addnstr (std::string (label).c_str (), 24);
			attrset (A_NORMAL);
			auto text = TruncateUtf8 (SanitizeText (value), width > 26 ? width - 26 : 0);
			if (!text.empty ()) addnstr (text.c_str (), static_cast<int> (text.size ()));
			++row;
		};

		show ("Router identity", Field ("router.hash"));
		show ("Version", Field ("router.version"));
		show ("Uptime", HumanUptime (ToLong (Field ("router.uptime"))));
		show ("Network status (IPv4)", Field ("network.status4") + " " + Field ("network.error4"));
		show ("Network status (IPv6)", Field ("network.status6") + " " + Field ("network.error6"));
		show ("Capabilities", Field ("router.caps"));
		show ("Floodfill", Field ("router.floodfill") == "1" ? "yes" : "no");
		show ("NAT / testing", std::string ("v4 ") + (Field ("network.testing4") == "1" ? "testing" : "ok") +
			", v6 " + (Field ("network.testing6") == "1" ? "testing" : "ok"));
		show ("NTCP2 / SSU2", std::string (Field ("transport.ntcp2") == "1" ? "NTCP2" : "-") + " / " +
			(Field ("transport.ssu2") == "1" ? "SSU2" : "-"));
		show ("Bandwidth in / out", HumanRate (ToU64 (Field ("bandwidth.in"))) + " / " +
			HumanRate (ToU64 (Field ("bandwidth.out"))));
		show ("Transferred", HumanBytes (ToU64 (Field ("traffic.received"))) + " received, " +
			HumanBytes (ToU64 (Field ("traffic.sent"))) + " sent");
		show ("Peers / routers", Field ("peers.count") + " / " + Field ("netdb.routers"));
		show ("Floodfills / leasesets", Field ("netdb.floodfills") + " / " + Field ("netdb.leasesets"));
		show ("Tunnels in / out / transit", Field ("tunnels.inbound") + " / " + Field ("tunnels.outbound") +
			" / " + Field ("tunnels.transit"));
		show ("Tunnel creation success", Field ("tunnels.success") + "%");
		show ("Reseeding", Field ("netdb.reseeding") == "1" ? "active" : "idle");
		show ("Max bandwidth", HumanBytes (ToLong (Field ("router.bandwidth_limit")) * 1024) + "/s");
	}

	void App::DrawPeers (int top, int height, int width)
	{
		int row = top;
		std::string header = "  Peer hash                                      Sess Attempts Transports High";
		PutLine (row++, header, width, A_BOLD);
		if (m_Peers.empty ()) PutLine (row, "  no known peers", width);
		int visible = std::max (0, height - 1);
		int start = std::max (0, static_cast<int> (m_Peers.size ()) - visible - m_Scroll);
		if (start < 0) start = 0;
		for (int i = start; i < static_cast<int> (m_Peers.size ()) && row < top + height; ++i)
		{
			const auto& peer = m_Peers[i];
			char buffer[160];
			char transports[16] = "-";
			if (peer.transports)
				snprintf (transports, sizeof (transports), "%ld", peer.transports);
			snprintf (buffer, sizeof (buffer), "  %-46.46s %4ld %8ld %10s %4s",
				peer.hash.c_str (), peer.sessions, peer.attempts, transports,
				peer.highBandwidth ? "yes" : "");
			PutLine (row++, buffer, width);
		}
	}

	void App::DrawTunnels (int top, int height, int width)
	{
		int row = top;
		PutLine (row++, "  Tunnel ID   Dir      State         Hops  Created", width, A_BOLD);
		if (m_Tunnels.empty ()) PutLine (row, "  no tunnels", width);
		int visible = std::max (0, height - 1);
		int start = std::max (0, static_cast<int> (m_Tunnels.size ()) - visible - m_Scroll);
		if (start < 0) start = 0;
		for (int i = start; i < static_cast<int> (m_Tunnels.size ()) && row < top + height; ++i)
		{
			const auto& tunnel = m_Tunnels[i];
			char buffer[160];
			snprintf (buffer, sizeof (buffer), "  %-10.10s %-8.8s %-13.13s %5.5s %s",
				tunnel.id.c_str (), tunnel.direction.c_str (), tunnel.state.c_str (),
				tunnel.hops.c_str (), Timestamp (ToLong (tunnel.created)).c_str ());
			PutLine (row++, buffer, width);
		}
	}

	void App::DrawNetDb (int top, int height, int width)
	{
		int row = top;
		auto show = [&] (std::string_view label, const std::string& value)
		{
			if (row >= top + height) return;
			PutLine (row++, "  " + FitUtf8 (label, 24) + value, width);
		};
		show ("Routers", Field ("netdb.routers"));
		show ("Floodfills", Field ("netdb.floodfills"));
		show ("LeaseSets", Field ("netdb.leasesets"));
		show ("Reseeding", Field ("netdb.reseeding") == "1" ? "active" : "idle");
		show ("Floodfill mode", Field ("router.floodfill") == "1" ? "enabled" : "disabled");
		show ("Hidden mode", Field ("router.hidden") == "1" ? "enabled" : "disabled");
		show ("Family", Field ("router.family").empty () ? "none" : Field ("router.family"));
		show ("Connected peers", Field ("peers.count"));
		show ("Tunnel build queue", Field ("tunnels.build_queue"));
		if (!m_Destinations.empty ())
		{
			if (row < top + height) PutLine (row++, "", width);
			if (row < top + height) PutLine (row++, "  Local destinations (b32):", width, A_BOLD);
			for (const auto& destination : m_Destinations)
			{
				if (row >= top + height) break;
				PutLine (row++, "  " + destination, width);
			}
		}
	}

	void App::DrawLogs (int top, int height, int width)
	{
		std::vector<const LogRow*> visible;
		for (const auto& record : m_Logs)
			if (record.level <= m_LogLevel)
				visible.push_back (&record);
		int lines = std::max (0, height - 1);
		int start = std::max (0, static_cast<int> (visible.size ()) - lines - m_Scroll);
		int row = top;
		PutLine (row++, "  Time      Level  Message (l cycles filter: debug/info/warn/error/critical)", width, A_BOLD);
		if (visible.empty ())
		{
			PutLine (row, "  no log records at this level", width);
			return;
		}
		for (int i = start; i < static_cast<int> (visible.size ()) && row < top + height; ++i)
		{
			const auto& record = *visible[i];
			move (row, 0);
			clrtoeol ();
			std::string prefix = "  " + Timestamp (record.timestamp) + " " +
				std::string (LevelName (record.level)) + "  ";
			auto safePrefix = TruncateUtf8 (prefix, width, false);
			addnstr (safePrefix.c_str (), static_cast<int> (safePrefix.size ()));
			int remaining = width - DisplayWidth (safePrefix);
			auto message = TruncateUtf8 (SanitizeText (record.text), std::max (0, remaining), true);
			attrset (LevelAttribute (record.level));
			if (!message.empty ()) addnstr (message.c_str (), static_cast<int> (message.size ()));
			attrset (A_NORMAL);
			++row;
		}
	}

	void App::DrawHelp (int top, int height, int width)
	{
		static constexpr std::array<std::string_view, 35> lines = {
			"  Keys - global",
			"  1..7, Tab, Left/Right   switch view (Torrents is 4)",
			"  q                       quit the TUI (router keeps running)",
			"  r                       refresh immediately",
			"  ?                       this help",
			"",
			"  Torrents (4)",
			"  Up/Down, PgUp/PgDn      select a torrent",
			"  Home/End                first/last torrent",
			"  Enter                   open torrent details",
			"  Esc                     back, or cancel a prompt",
			"  a / A                   add magnet / add .torrent file path",
			"  p                       start or stop the selected torrent",
			"  R                       reannounce the selected torrent",
			"  Ctrl-R                  reannounce all torrents",
			"  d / D                   remove / remove with local data",
			"  D                       requires typing YES",
			"  x / z                   start all / stop all",
			"  s / S                   cycle sort / reverse sort direction",
			"  /                       filter by torrent name or hash",
			"  c                       clear the filter",
			"  [ / ]                   previous/next torrent tunnel",
			"",
			"  Torrent details",
			"  Left/Right, Tab         general/files/peers/trackers",
			"  Up/Down, PgUp/PgDn      scroll detail lines",
			"  Home/End                first/last detail line",
			"  Esc                     return to the torrent table",
			"",
			"  Notes",
			"  Actions are sent to the daemon over the local UNIX socket.",
			"  The TUI never opens torrent files, peers or trackers directly.",
			"  Removal with local data is a separate, typed confirmation.",
			"  Sort order and filter are shown above the table.",
			"  Long UTF-8 values are truncated without splitting characters."
		};
		int total = static_cast<int> (lines.size ());
		int visible = std::max (0, height);
		int maximum = std::max (0, total - visible);
		m_HelpScroll = std::clamp (m_HelpScroll, 0, maximum);
		int row = top;
		for (int i = m_HelpScroll; i < total && row < top + height; ++i)
			PutLine (row++, std::string (lines[i]), width);
	}

	void App::DrawTorrents (int top, int height, int width)
	{
		if (!m_Connected)
		{
			PutLine (top, "  daemon is disconnected; torrent state is unavailable", width);
			PutLine (top + 1, "  reconnecting automatically - see the footer for details", width);
			return;
		}
		if (m_TorrentTunnels.empty ())
		{
			PutLine (top, "  No torrent service tunnel is available on this router.", width, A_BOLD);
			PutLine (top + 1, "  Start a torrent client tunnel in i2pd, then press r.", width);
			PutLine (top + 2, "  Torrent administration does not open any files or network links here.", width);
			return;
		}

		auto layout = ComputeTorrentLayout (width);
		auto column = [] (std::string_view text, int columns, bool right = false)
		{
			return FitUtf8 (text, columns, right);
		};

		std::string header = "  ";
		header += column ("Name", layout.name);
		header += column ("Status", TORRENT_STATUS_COLS);
		if (layout.showSize) header += column ("Size", TORRENT_SIZE_COLS, true);
		header += column ("Prog", TORRENT_PROGRESS_COLS, true);
		if (layout.showDown) header += column ("Down", TORRENT_DOWN_COLS, true);
		if (layout.showUp) header += column ("Up", TORRENT_UP_COLS, true);
		if (layout.showPeers) header += column ("Peers S/L", TORRENT_PEERS_COLS);
		if (layout.showRatio) header += column ("Ratio/ETA", TORRENT_RATIO_COLS);
		if (layout.showError) header += column ("Error", TORRENT_ERROR_COLS);
		PutLine (top, header, width, A_BOLD);

		if (!m_TorrentsLoaded)
		{
			PutLine (top + 1, "  loading torrent snapshot...", width);
			return;
		}
		if (m_Torrents.empty ())
		{
			PutLine (top + 1, "  no torrents in this tunnel", width);
			PutLine (top + 2, "  press a to add a magnet, A to add a .torrent by path", width);
			return;
		}
		if (m_TorrentView.empty ())
		{
			PutLine (top + 1, "  no torrent matches the current filter", width);
			PutLine (top + 2, "  press / to change the filter or c to clear it", width);
			return;
		}

		int visible = std::max (0, height - 1);
		EnsureTorrentSelectionVisible (visible);
		int row = top + 1;
		for (int i = m_TorrentScroll; i < static_cast<int> (m_TorrentView.size ()) && row < top + height; ++i)
		{
			const auto& torrent = m_Torrents[m_TorrentView[i]];
			bool selected = i == m_TorrentSelection;
			std::string line = selected ? "> " : "  ";
			line += column (torrent.name, layout.name);
			line += column (torrent.status, TORRENT_STATUS_COLS);
			if (layout.showSize) line += column (HumanBytes (torrent.total), TORRENT_SIZE_COLS, true);
			char progress[32];
			snprintf (progress, sizeof (progress), "%.1f%%", torrent.percent);
			line += column (progress, TORRENT_PROGRESS_COLS, true);
			if (layout.showDown) line += column (HumanRateShort (torrent.downRate), TORRENT_DOWN_COLS, true);
			if (layout.showUp) line += column (HumanRateShort (torrent.upRate), TORRENT_UP_COLS, true);
			if (layout.showPeers)
			{
				char peers[64];
				snprintf (peers, sizeof (peers), "%d %d/%d", torrent.peers, torrent.seeders, torrent.leechers);
				line += column (peers, TORRENT_PEERS_COLS);
			}
			if (layout.showRatio) line += column (RatioEtaText (torrent), TORRENT_RATIO_COLS);
			if (layout.showError)
			{
				std::string error = torrent.error.empty () ? std::string () : "! " + torrent.error;
				line += column (error, TORRENT_ERROR_COLS);
			}
			PutLine (row++, line, width, selected ? A_REVERSE : 0);
		}
	}

	void App::BuildDetailGeneral (std::vector<std::string>& lines, const TorrentRow& info, int width) const
	{
		auto add = [&] (std::string_view label, std::string_view value)
		{
			std::string prefix = "  " + std::string (label) + ": ";
			int available = std::max (8, width - static_cast<int> (prefix.size ()));
			auto text = SanitizeText (value);
			bool clipped = false;
			if (text.size () > 4096)
			{
				text = BoundUtf8 (text, 4096);
				clipped = true;
			}
			auto wrapped = WrapUtf8 (text, available);
			if (wrapped.empty ()) wrapped.emplace_back ();
			for (size_t i = 0; i < wrapped.size (); ++i)
			{
				if (i == 0) lines.push_back (prefix + wrapped[i]);
				else lines.push_back (std::string (prefix.size (), ' ') + wrapped[i]);
			}
			if (clipped) lines.push_back (std::string (prefix.size (), ' ') + "...");
		};

		lines.push_back ("  General");
		add ("Name", info.name);
		add ("ID", std::to_string (info.id));
		add ("Status", info.status);
		add ("Hash", info.hash.empty () ? "unknown" : info.hash);
		add ("Magnet", info.magnet.empty () ? "not reported" : info.magnet);
		char percent[32];
		snprintf (percent, sizeof (percent), "%.1f%%", info.percent);
		add ("Size", HumanBytes (info.total) + " (" + percent + " complete)");
		add ("Have", HumanBytes (info.have));
		add ("Downloaded / uploaded", HumanBytes (info.downloaded) + " / " + HumanBytes (info.uploaded));
		add ("Rate down / up", HumanRate (info.downRate) + " / " + HumanRate (info.upRate));
		add ("Peers", std::to_string (info.peers) + " (seeders " + std::to_string (info.seeders) +
			", leechers " + std::to_string (info.leechers) + ")");
		add ("Ratio", RatioText (info));
		add ("ETA", info.eta ? HumanDuration (*info.eta) : "not reported");
		add ("Error", info.error.empty () ? "none" : info.error);
		add ("Detail", m_TorrentDetail.loaded ? "loaded from TORRENT-DETAIL" : "waiting for daemon snapshot");
		lines.push_back ("");
		lines.push_back ("  Session");
		if (m_TorrentSessionLoaded)
		{
			add ("Downloaded / uploaded", HumanBytes (m_TorrentSession.downloaded) + " / " +
				HumanBytes (m_TorrentSession.uploaded));
			add ("Active / paused", std::to_string (m_TorrentSession.active) + " / " +
				std::to_string (m_TorrentSession.paused));
		}
		else
			add ("State", "not loaded yet");
		lines.push_back ("");
		lines.push_back ("  DHT");
		if (!m_TorrentDhtLoaded)
			add ("State", "not loaded yet");
		else if (!m_TorrentDht.supported)
			add ("State", "not supported by this daemon");
		else
		{
			add ("Nodes / buckets", std::to_string (m_TorrentDht.numNodes) + " / " +
				std::to_string (m_TorrentDht.numBuckets));
			add ("Torrents / heard from", std::to_string (m_TorrentDht.numTorrents) + " / " +
				std::to_string (m_TorrentDht.numHeardFrom));
			add ("Failed peers", std::to_string (m_TorrentDht.numFailedPeers));
		}
		lines.push_back ("");
		lines.push_back ("  Esc returns to the torrent table. Left/Right changes detail tab.");
	}

	void App::BuildDetailFiles (std::vector<std::string>& lines, const TorrentDetail& detail, int width) const
	{
		int nameColumns = std::max (8, width - 2 - 9 - 9 - 7);
		lines.push_back ("  " + FitUtf8 ("File", nameColumns) + FitUtf8 ("Size", 9, true) +
			FitUtf8 ("Done", 9, true) + FitUtf8 ("Prog", 7, true));
		if (detail.files.empty ())
		{
			lines.push_back ("  no file information reported");
			return;
		}
		for (const auto& file : detail.files)
		{
			double percent = file.length ?
				100.0 * static_cast<double> (file.completed) / static_cast<double> (file.length) : 0.0;
			if (percent > 100.0) percent = 100.0;
			char progress[32];
			snprintf (progress, sizeof (progress), "%.1f%%", percent);
			lines.push_back ("  " + FitUtf8 (file.name, nameColumns) + FitUtf8 (HumanBytes (file.length), 9, true) +
				FitUtf8 (HumanBytes (file.completed), 9, true) + FitUtf8 (progress, 7, true));
		}
	}

	void App::BuildDetailPeers (std::vector<std::string>& lines, const TorrentDetail& detail, int width) const
	{
		bool showClient = width >= 72;
		bool showFlags = width >= 60;
		bool showRates = width >= 84;
		int fixed = 2 + 7;
		if (showClient) fixed += 14;
		if (showFlags) fixed += 7;
		if (showRates) fixed += 18;
		int identColumns = std::max (8, width - fixed);
		std::string header = "  " + FitUtf8 ("Peer", identColumns);
		if (showClient) header += FitUtf8 ("Client", 14);
		if (showFlags) header += FitUtf8 ("Flags", 7);
		header += FitUtf8 ("Prog", 7, true);
		if (showRates) header += FitUtf8 ("Down", 9, true) + FitUtf8 ("Up", 9, true);
		lines.push_back (header);
		if (detail.peers.empty ())
		{
			lines.push_back ("  no peer information reported");
			return;
		}
		for (const auto& peer : detail.peers)
		{
			char progress[32];
			snprintf (progress, sizeof (progress), "%.1f%%", std::clamp (peer.progress, 0.0, 100.0));
			std::string line = "  " + FitUtf8 (peer.ident, identColumns);
			if (showClient) line += FitUtf8 (peer.client, 14);
			if (showFlags) line += FitUtf8 (peer.flags, 7);
			line += FitUtf8 (progress, 7, true);
			if (showRates) line += FitUtf8 (HumanRateShort (peer.downRate), 9, true) +
				FitUtf8 (HumanRateShort (peer.upRate), 9, true);
			lines.push_back (line);
		}
	}

	void App::BuildDetailTrackers (std::vector<std::string>& lines, const TorrentDetail& detail, int width) const
	{
		lines.push_back ("  Trackers");
		if (detail.trackers.empty ())
		{
			lines.push_back ("  no tracker information reported");
			return;
		}
		for (const auto& tracker : detail.trackers)
		{
			lines.push_back ("  " + TruncateUtf8 (SanitizeText (tracker.announce), std::max (8, width - 2), true));
			lines.push_back ("    peers " + std::to_string (tracker.peers) + " (seeders " +
				std::to_string (tracker.seeders) + ", leechers " + std::to_string (tracker.leechers) + ")" +
				" | result: " + TruncateUtf8 (SanitizeText (tracker.lastResult.empty () ? "none" :
					tracker.lastResult), std::max (8, width - 48), true));
			lines.push_back ("    last announce " + Timestamp64 (tracker.lastAnnounce) +
				", next announce " + Timestamp64 (tracker.nextAnnounce));
		}
	}

	void App::DrawTorrentDetails (int top, int height, int width)
	{
		const TorrentRow* selected = SelectedTorrent ();
		const TorrentRow* info = selected;
		if (m_TorrentDetail.loaded && selected && m_TorrentDetail.info.id == selected->id)
			info = &m_TorrentDetail.info;
		if (!info)
		{
			PutLine (top, "  torrent is no longer available; press Esc", width);
			return;
		}

		int row = top;
		PutLine (row++, "  " + FitUtf8 (info->name, std::max (1, width - 2)), width, A_BOLD);

		static constexpr std::array<std::string_view, 4> tabNames = {"General", "Files", "Peers", "Trackers"};
		std::string tabs = "  ";
		for (size_t i = 0; i < tabNames.size (); ++i)
		{
			if (static_cast<size_t> (m_TorrentDetailTab) == i)
				tabs += "[" + std::string (tabNames[i]) + "] ";
			else
				tabs += " " + std::string (tabNames[i]) + "  ";
		}
		tabs += " (Left/Right, Tab)";
		PutLine (row++, tabs, width);

		std::vector<std::string> lines;
		switch (m_TorrentDetailTab)
		{
			case TorrentDetailTab::General: BuildDetailGeneral (lines, *info, width); break;
			case TorrentDetailTab::Files: BuildDetailFiles (lines, m_TorrentDetail, width); break;
			case TorrentDetailTab::Peers: BuildDetailPeers (lines, m_TorrentDetail, width); break;
			case TorrentDetailTab::Trackers: BuildDetailTrackers (lines, m_TorrentDetail, width); break;
		}

		int visible = std::max (0, top + height - row);
		int maximum = std::max (0, static_cast<int> (lines.size ()) - visible);
		m_TorrentDetailScroll = std::clamp (m_TorrentDetailScroll, 0, maximum);
		for (int i = m_TorrentDetailScroll; i < static_cast<int> (lines.size ()) && row < top + height; ++i)
			PutLine (row++, lines[i], width);
	}

	void App::DrawTorrentStatusLine (int height, int width)
	{
		int row = height - 2;
		if (row < 1) return;
		if (m_Prompt != PromptKind::None)
		{
			int labelColumns = std::max (8, width / 2);
			auto label = TruncateUtf8 (SanitizeText (m_PromptLabel), labelColumns, true);
			int available = std::max (0, width - DisplayWidth (label) - 1);
			auto input = TailUtf8 (m_InputBuffer, available);
			std::string line = " " + label + input;
			attrset (A_REVERSE | A_BOLD);
			PutLine (row, line, width);
			attrset (A_NORMAL);
			int column = std::min (width - 1, DisplayWidth (line));
			curs_set (1);
			move (row, std::max (0, column));
			return;
		}

		curs_set (0);
		if (!m_StatusMessage.empty ())
		{
			if (std::chrono::steady_clock::now () >= m_StatusExpiry)
			{
				m_StatusMessage.clear ();
				m_StatusError = false;
				return;
			}
			std::string line = " " + std::string (m_StatusError ? "! " : "> ") + m_StatusMessage;
			PutLine (row, line, width, m_StatusError ? (COLOR_PAIR (2) | A_BOLD) : A_BOLD);
		}
	}

	void App::Draw ()
	{
		int height = 0, width = 0;
		getmaxyx (stdscr, height, width);
		if (height < 6 || width < 40)
		{
			erase ();
			curs_set (0);
			mvaddnstr (0, 0, "Terminal too small", std::max (0, width));
			refresh ();
			m_Dirty = false;
			return;
		}

		erase ();
		DrawHeader (width);
		if (m_Tab == Tab::Torrents) DrawTorrentContext (width);
		DrawFooter (height, width);
		const int top = 2;
		const int contentHeight = height - 4;
		switch (m_Tab)
		{
			case Tab::Status: DrawStatus (top, contentHeight, width); break;
			case Tab::Peers: DrawPeers (top, contentHeight, width); break;
			case Tab::Tunnels: DrawTunnels (top, contentHeight, width); break;
			case Tab::Torrents:
				if (m_TorrentDetails) DrawTorrentDetails (top, contentHeight, width);
				else DrawTorrents (top, contentHeight, width);
				break;
			case Tab::NetDb: DrawNetDb (top, contentHeight, width); break;
			case Tab::Logs: DrawLogs (top, contentHeight, width); break;
			case Tab::Help: DrawHelp (top, contentHeight, width); break;
		}
		DrawTorrentStatusLine (height, width);
		refresh ();
		m_Dirty = false;
	}

	void App::HandleKey (int key)
	{
		if (m_Prompt != PromptKind::None)
		{
			HandlePromptKey (key);
			return;
		}

		if (m_Tab == Tab::Torrents)
		{
			if (m_TorrentDetails)
			{
				switch (key)
				{
					case 27:
						LeaveTorrentDetails ();
						return;
					case KEY_LEFT:
					case KEY_BTAB:
					{
						int tab = static_cast<int> (m_TorrentDetailTab);
						m_TorrentDetailTab = static_cast<TorrentDetailTab> ((tab + 3) % 4);
						m_TorrentDetailScroll = 0;
						m_Dirty = true;
						return;
					}
					case KEY_RIGHT:
					case '\t':
					{
						int tab = static_cast<int> (m_TorrentDetailTab);
						m_TorrentDetailTab = static_cast<TorrentDetailTab> ((tab + 1) % 4);
						m_TorrentDetailScroll = 0;
						m_Dirty = true;
						return;
					}
					case KEY_UP:
						if (m_TorrentDetailScroll > 0) --m_TorrentDetailScroll;
						m_Dirty = true;
						return;
					case KEY_DOWN:
						++m_TorrentDetailScroll;
						m_Dirty = true;
						return;
					case KEY_PPAGE:
						m_TorrentDetailScroll = std::max (0, m_TorrentDetailScroll - 10);
						m_Dirty = true;
						return;
					case KEY_NPAGE:
						m_TorrentDetailScroll += 10;
						m_Dirty = true;
						return;
					case KEY_HOME:
						m_TorrentDetailScroll = 0;
						m_Dirty = true;
						return;
					case KEY_END:
						m_TorrentDetailScroll = INT_MAX;
						m_Dirty = true;
						return;
					default:
						break;
				}
			}
			else
			{
				int page = TorrentPageRows ();
				switch (key)
				{
					case KEY_UP:
						if (m_TorrentSelection > 0) --m_TorrentSelection;
						EnsureTorrentSelectionVisible (page);
						m_Dirty = true;
						return;
					case KEY_DOWN:
						if (m_TorrentSelection + 1 < static_cast<int> (m_TorrentView.size ())) ++m_TorrentSelection;
						EnsureTorrentSelectionVisible (page);
						m_Dirty = true;
						return;
					case KEY_PPAGE:
						m_TorrentSelection = std::max (0, m_TorrentSelection - page);
						EnsureTorrentSelectionVisible (page);
						m_Dirty = true;
						return;
					case KEY_NPAGE:
						m_TorrentSelection = std::min (static_cast<int> (m_TorrentView.size ()) - 1,
							m_TorrentSelection + page);
						EnsureTorrentSelectionVisible (page);
						m_Dirty = true;
						return;
					case KEY_HOME:
						m_TorrentSelection = 0;
						EnsureTorrentSelectionVisible (page);
						m_Dirty = true;
						return;
					case KEY_END:
						m_TorrentSelection = std::max (0, static_cast<int> (m_TorrentView.size ()) - 1);
						EnsureTorrentSelectionVisible (page);
						m_Dirty = true;
						return;
					case '\n':
					case '\r':
					case KEY_ENTER:
						EnterTorrentDetails ();
						return;
					case '/':
						BeginPrompt (PromptKind::Filter, "filter name/hash: ", m_TorrentFilter);
						return;
					case 's':
						CycleTorrentSort ();
						return;
					case 'S':
						ToggleTorrentSortDirection ();
						return;
					case 'c':
					case 'C':
						m_TorrentFilter.clear ();
						RebuildTorrentView (CurrentSelectedId ());
						m_TorrentScroll = 0;
						SetStatus ("filter cleared", false);
						return;
					case '[':
						CycleTorrentTunnel (-1);
						return;
					case ']':
						CycleTorrentTunnel (1);
						return;
					case 'a':
						BeginPrompt (PromptKind::AddMagnet, "add magnet URI: ");
						return;
					case 'A':
						BeginPrompt (PromptKind::AddFile, "add .torrent path: ");
						return;
					case 'p':
					case 'P':
						ToggleSelected ();
						return;
					case 'R':
						ReannounceSelected ();
						return;
					case 'd':
						RemoveSelected (false);
						return;
					case 'D':
						RemoveSelected (true);
						return;
					case 'x':
					case 'X':
						if (RequireTorrentTunnel ())
							SendTorrentCommand ("TORRENT-START-ALL " + std::to_string (m_TorrentTunnel));
						return;
					case 'z':
					case 'Z':
						if (RequireTorrentTunnel ())
							SendTorrentCommand ("TORRENT-STOP-ALL " + std::to_string (m_TorrentTunnel));
						return;
					case 18: // Ctrl-R
						if (RequireTorrentTunnel ())
							SendTorrentCommand ("TORRENT-REANNOUNCE-ALL " + std::to_string (m_TorrentTunnel));
						return;
					default:
						break;
				}
			}
		}

		switch (key)
		{
			case 'q':
			case 'Q':
				g_Stop = 1;
				return;
			case '1': m_Tab = Tab::Status; m_Scroll = 0; break;
			case '2': m_Tab = Tab::Peers; m_Scroll = 0; break;
			case '3': m_Tab = Tab::Tunnels; m_Scroll = 0; break;
			case '4': m_Tab = Tab::Torrents; m_Scroll = 0; ScheduleTorrentRefresh (true); break;
			case '5': m_Tab = Tab::NetDb; m_Scroll = 0; break;
			case '6': m_Tab = Tab::Logs; m_Scroll = 0; break;
			case '7':
			case '?': m_Tab = Tab::Help; m_Scroll = 0; m_HelpScroll = 0; break;
			case '\t':
			{
				auto next = (static_cast<int> (m_Tab) + 1) % static_cast<int> (TAB_NAMES.size ());
				m_Tab = static_cast<Tab> (next);
				m_Scroll = 0;
				if (m_Tab == Tab::Torrents) ScheduleTorrentRefresh (true);
				break;
			}
			case KEY_RIGHT:
			{
				auto next = (static_cast<int> (m_Tab) + 1) % static_cast<int> (TAB_NAMES.size ());
				m_Tab = static_cast<Tab> (next);
				m_Scroll = 0;
				if (m_Tab == Tab::Torrents) ScheduleTorrentRefresh (true);
				break;
			}
			case KEY_LEFT:
			{
				auto previous = (static_cast<int> (m_Tab) + static_cast<int> (TAB_NAMES.size ()) - 1) %
					static_cast<int> (TAB_NAMES.size ());
				m_Tab = static_cast<Tab> (previous);
				m_Scroll = 0;
				if (m_Tab == Tab::Torrents) ScheduleTorrentRefresh (true);
				break;
			}
			case 'r':
				m_NextRefresh = std::chrono::steady_clock::now ();
				if (m_Tab == Tab::Torrents) ScheduleTorrentRefresh (true);
				m_Dirty = true;
				return;
			case 'l':
			case 'L':
				if (m_LogLevel > eLogCritical) --m_LogLevel;
				else m_LogLevel = eLogDebug;
				break;
			case KEY_UP:
				if (m_Tab == Tab::Help)
				{
					if (m_HelpScroll > 0) --m_HelpScroll;
				}
				else ++m_Scroll;
				m_Dirty = true;
				return;
			case KEY_DOWN:
				if (m_Tab == Tab::Help) ++m_HelpScroll;
				else if (m_Scroll > 0) --m_Scroll;
				m_Dirty = true;
				return;
			case KEY_PPAGE:
				if (m_Tab == Tab::Help) m_HelpScroll = std::max (0, m_HelpScroll - 10);
				else m_Scroll += 10;
				m_Dirty = true;
				return;
			case KEY_NPAGE:
				if (m_Tab == Tab::Help) m_HelpScroll += 10;
				else if (m_Scroll > 10) m_Scroll -= 10;
				else m_Scroll = 0;
				m_Dirty = true;
				return;
			case KEY_HOME:
				if (m_Tab == Tab::Help) m_HelpScroll = 0;
				else m_Scroll = 0;
				m_Dirty = true;
				return;
			default: return;
		}
		m_Dirty = true;
	}
}

int main (int argc, char* argv[])
{
	auto options = i2p::tui::ParseOptions (argc, argv);
	if (!options)
	{
		std::fprintf (stderr, "i2pd-tui: %s\n", options.error ().c_str ());
		return EXIT_FAILURE;
	}
	if (options->showHelp)
	{
		std::printf (
			"i2pd-tui " VERSION " - local console for the Open I2Pd router\n"
			"\n"
			"Usage: i2pd-tui [--socket PATH] [--datadir PATH] [--conf PATH] [--service]\n"
			"\n"
			"  --socket PATH   UNIX socket exposed by the daemon (tui.socket)\n"
			"  --datadir PATH  data directory (default: ~/.i2pd)\n"
			"  --conf PATH     i2pd.conf used to read [tui] socket\n"
			"  --service       use the system service data directory\n"
			"  --help          show this help\n"
			"  --version       show the version\n");
		return EXIT_SUCCESS;
	}
	if (options->showVersion)
	{
		std::printf ("i2pd-tui %s\n", VERSION);
		return EXIT_SUCCESS;
	}

	auto resolved = i2p::tui::ResolveSocket (*options);
	if (!resolved)
	{
		std::fprintf (stderr, "i2pd-tui: %s\n", resolved.error ().c_str ());
		return EXIT_FAILURE;
	}

	setlocale (LC_ALL, "");
	struct sigaction resizeAction{};
	resizeAction.sa_handler = HandleResize;
	sigemptyset (&resizeAction.sa_mask);
	sigaction (SIGWINCH, &resizeAction, nullptr);
	struct sigaction stopAction{};
	stopAction.sa_handler = HandleStop;
	sigemptyset (&stopAction.sa_mask);
	sigaction (SIGINT, &stopAction, nullptr);
	sigaction (SIGTERM, &stopAction, nullptr);

	initscr ();
	cbreak ();
	noecho ();
	keypad (stdscr, TRUE);
	curs_set (0);
	timeout (200);
	if (has_colors ())
	{
		start_color ();
		use_default_colors ();
		init_pair (1, COLOR_CYAN, -1);
		init_pair (2, COLOR_RED, -1);
		init_pair (3, COLOR_YELLOW, -1);
		init_pair (4, COLOR_BLUE, -1);
	}

	App app (*resolved);
	int result = app.Run ();
	endwin ();
	return result;
}

#endif // _WIN32
