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
#include <chrono>
#include <climits>
#include <clocale>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <signal.h>
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
		NetDb,
		Logs,
		Help
	};

	constexpr std::array<std::string_view, 6> TAB_NAMES = {"Status", "Peers", "Tunnels", "NetDb", "Logs", "Help"};

	struct StateField
	{
		std::string name;
		std::string value;
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

	std::string HumanBytes (long value)
	{
		static constexpr std::array<const char*, 5> units = {"B", "KiB", "MiB", "GiB", "TiB"};
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

	std::string HumanRate (long bytesPerSecond)
	{
		return HumanBytes (bytesPerSecond) + "/s";
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

	class App
	{
		public:

			explicit App (i2p::tui::ClientOptions options):
				m_Options (std::move (options)), m_Client (m_Options.socketPath)
			{
				m_NextConnect = std::chrono::steady_clock::now ();
				m_NextRefresh = m_NextConnect;
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

			void Draw ();
			void DrawHeader (int width);
			void DrawFooter (int height, int width);
			void DrawStatus (int top, int height, int width);
			void DrawPeers (int top, int height, int width);
			void DrawTunnels (int top, int height, int width);
			void DrawNetDb (int top, int height, int width);
			void DrawLogs (int top, int height, int width);
			void DrawHelp (int top, int height, int width);
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
			bool m_Dirty = true;
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
			Refresh ();
			if (m_Dirty) Draw ();

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
		m_Dirty = true;
	}

	bool App::Request (std::string_view command, std::string& payload)
	{
		auto response = m_Client.Request (command);
		if (!response)
		{
			m_Error = response.error ();
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
		if (now < m_NextRefresh) return;
		m_NextRefresh = now + 2s;

		std::string payload;
		if (!Request ("STATE", payload)) return;
		ParseState (payload);

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
		m_Dirty = true;
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

	void App::PutLine (int row, const std::string& text, int width, int attributes)
	{
		move (row, 0);
		clrtoeol ();
		if (attributes) attrset (attributes);
		addnstr (text.c_str (), width);
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
		std::string status;
		if (m_Connected) status = "connected";
		else status = "disconnected";
		std::string line = " " + status;
		if (!m_Error.empty ()) line += " | " + m_Error;
		line += " | socket " + m_Options.socketPath;
		line += " | q quit, Tab/1-5 switch, r refresh, l log level, arrows scroll";
		if (static_cast<int> (line.size ()) >= width)
			line.resize (width > 0 ? width - 1 : 0);
		attrset (A_REVERSE);
		PutLine (height - 1, line, width);
		attrset (A_NORMAL);
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
			addnstr (std::string (value).c_str (), width > 26 ? width - 26 : 0);
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
		show ("Bandwidth in / out", HumanRate (ToLong (Field ("bandwidth.in"))) + " / " +
			HumanRate (ToLong (Field ("bandwidth.out"))));
		show ("Transferred", HumanBytes (ToLong (Field ("traffic.received"))) + " received, " +
			HumanBytes (ToLong (Field ("traffic.sent"))) + " sent");
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
			char buffer[512];
			snprintf (buffer, sizeof (buffer), "  %-24.24s %s", std::string (label).c_str (), value.c_str ());
			PutLine (row++, buffer, width);
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
		for (const auto& row : m_Logs)
			if (row.level <= m_LogLevel)
				visible.push_back (&row);
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
			addnstr (prefix.c_str (), width);
			attrset (LevelAttribute (record.level));
			addnstr (record.text.c_str (), std::max (0, width - static_cast<int> (prefix.size ())));
			attrset (A_NORMAL);
			++row;
		}
	}

	void App::DrawHelp (int top, int height, int width)
	{
		static constexpr std::array<const char*, 10> lines = {
			"  Keys",
			"  1-5, Tab, Left/Right   switch view",
			"  Up/Down, PgUp/PgDn     scroll lists and logs",
			"  l                      cycle log level filter",
			"  r                      refresh immediately",
			"  q                      quit the TUI (the router keeps running)",
			"",
			"  This interface is read-only. It connects to the daemon through a",
			"  versioned UNIX socket and never receives private keys or secrets.",
			"  Closing it has no effect on the router."
		};
		int row = top;
		for (const char* line : lines)
		{
			if (row >= top + height) break;
			PutLine (row++, line, width);
		}
	}

	void App::Draw ()
	{
		int height = 0, width = 0;
		getmaxyx (stdscr, height, width);
		if (height < 6 || width < 40)
		{
			erase ();
			mvaddnstr (0, 0, "Terminal too small", std::max (0, width));
			refresh ();
			m_Dirty = false;
			return;
		}

		erase ();
		DrawHeader (width);
		DrawFooter (height, width);
		const int top = 2;
		const int contentHeight = height - 4;
		switch (m_Tab)
		{
			case Tab::Status: DrawStatus (top, contentHeight, width); break;
			case Tab::Peers: DrawPeers (top, contentHeight, width); break;
			case Tab::Tunnels: DrawTunnels (top, contentHeight, width); break;
			case Tab::NetDb: DrawNetDb (top, contentHeight, width); break;
			case Tab::Logs: DrawLogs (top, contentHeight, width); break;
			case Tab::Help: DrawHelp (top, contentHeight, width); break;
		}
		refresh ();
		m_Dirty = false;
	}

	void App::HandleKey (int key)
	{
		switch (key)
		{
			case 'q':
			case 'Q':
				g_Stop = 1;
				return;
			case '1': m_Tab = Tab::Status; m_Scroll = 0; break;
			case '2': m_Tab = Tab::Peers; m_Scroll = 0; break;
			case '3': m_Tab = Tab::Tunnels; m_Scroll = 0; break;
			case '4': m_Tab = Tab::NetDb; m_Scroll = 0; break;
			case '5': m_Tab = Tab::Logs; m_Scroll = 0; break;
			case '?': m_Tab = Tab::Help; m_Scroll = 0; break;
			case '\t':
			{
				auto next = (static_cast<int> (m_Tab) + 1) % static_cast<int> (TAB_NAMES.size ());
				m_Tab = static_cast<Tab> (next);
				m_Scroll = 0;
				break;
			}
			case KEY_RIGHT:
			{
				auto next = (static_cast<int> (m_Tab) + 1) % static_cast<int> (TAB_NAMES.size ());
				m_Tab = static_cast<Tab> (next);
				m_Scroll = 0;
				break;
			}
			case KEY_LEFT:
			{
				auto previous = (static_cast<int> (m_Tab) + static_cast<int> (TAB_NAMES.size ()) - 1) %
					static_cast<int> (TAB_NAMES.size ());
				m_Tab = static_cast<Tab> (previous);
				m_Scroll = 0;
				break;
			}
			case 'r':
			case 'R':
				m_NextRefresh = std::chrono::steady_clock::now ();
				m_Dirty = true;
				return;
			case 'l':
			case 'L':
				if (m_LogLevel > eLogCritical) --m_LogLevel;
				else m_LogLevel = eLogDebug;
				break;
			case KEY_UP: ++m_Scroll; m_Dirty = true; return;
			case KEY_DOWN: if (m_Scroll > 0) --m_Scroll; m_Dirty = true; return;
			case KEY_PPAGE: m_Scroll += 10; m_Dirty = true; return;
			case KEY_NPAGE: if (m_Scroll > 10) m_Scroll -= 10; else m_Scroll = 0; m_Dirty = true; return;
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
			"i2pd-tui " VERSION " - local read-only console for the Open I2Pd router\n"
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
