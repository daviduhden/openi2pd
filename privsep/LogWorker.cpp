/*
* Copyright (c) 2026, The PurpleI2P Project
*
* This file is part of Open I2Pd and licensed under BSD3
*/

#include "LogWorker.h"

#ifndef _WIN32

#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>
#include <sys/stat.h>

#ifndef _WIN32
#include <syslog.h>
#endif

#if defined(__linux__)
#include <sys/prctl.h>
#endif

namespace i2p::privsep
{
	namespace
	{
		struct LogConfig
		{
			bool syslog = false;
			std::string path;
			std::string tag;
			bool configured = false;
		};

		bool IsLevel (uint8_t level)
		{
			return level <= 5; // eLogNone..eLogDebug
		}

#ifdef __OpenBSD__
		bool ApplyIsolation (const LogConfig& config)
		{
			if (config.syslog)
			{
				if (pledge ("stdio unix", nullptr) == -1)
				{
					std::cerr << "privsep log: pledge: " << strerror (errno) << std::endl;
					return false;
				}
				return true;
			}
			if (config.path.empty ())
			{
				// Foreground stdout sink: no filesystem access at all.
				if (pledge ("stdio", nullptr) == -1)
				{
					std::cerr << "privsep log: pledge: " << strerror (errno) << std::endl;
					return false;
				}
				return true;
			}
			// localtime_r(3) reads the zone file; allow that one read-only
			// path so timestamps do not silently degrade under unveil.
			if (unveil ("/etc/localtime", "r") == -1)
			{
				std::cerr << "privsep log: unveil /etc/localtime: " << strerror (errno) << std::endl;
				return false;
			}
			if (unveil (config.path.c_str (), "rwc") == -1)
			{
				std::cerr << "privsep log: unveil: " << strerror (errno) << std::endl;
				return false;
			}
			if (unveil (nullptr, nullptr) == -1)
			{
				std::cerr << "privsep log: unveil lock: " << strerror (errno) << std::endl;
				return false;
			}
			if (pledge ("stdio rpath wpath cpath fattr", nullptr) == -1)
			{
				std::cerr << "privsep log: pledge: " << strerror (errno) << std::endl;
				return false;
			}
			return true;
		}
#else
		bool ApplyIsolation (const LogConfig&)
		{
#if defined(__linux__) && defined(PR_SET_NO_NEW_PRIVS)
			if (prctl (PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0)
			{
				std::cerr << "privsep log: no_new_privs: " << strerror (errno) << std::endl;
				return false;
			}
#endif
			return true;
		}
#endif

		std::string FormatRecord (uint8_t level, std::time_t timestamp, std::string_view text)
		{
			static constexpr const char *levels[] = {"none", "critical", "error", "warn", "info", "debug"};
			static constexpr size_t MAX_LINE = 8192;
			std::tm tm{};
			char date[32] = "??:??:??";
			if (localtime_r (&timestamp, &tm))
				strftime (date, sizeof (date), "%Y-%m-%d %H:%M:%S", &tm);
			std::string line = date;
			line += " [";
			line += levels[level <= 5 ? level : 0];
			line += "] ";
			for (char ch : text.substr (0, MAX_LINE))
			{
				const auto byte = static_cast<unsigned char> (ch);
				line.push_back ((byte < 0x20 && ch != '\t') || byte == 0x7f ? ' ' : ch);
			}
			return line;
		}
	}

	std::expected<void, std::string> LogSink::Configure (bool useSyslog, std::string_view path,
		std::string_view tag)
	{
		std::lock_guard<std::mutex> lock (m_Mutex);
		if (m_Configured) return std::unexpected ("log sink already configured");
		if (path.find ('\0') != std::string_view::npos ||
			tag.find ('\0') != std::string_view::npos)
			return std::unexpected ("NUL in log configuration");
		Message message{MessageType::ConfigureLog, 0, EncodeLogConfig (useSyslog, path, tag)};
		auto sent = m_Channel.Send (message);
		if (!sent)
		{
			m_Channel.Close ();
			return sent;
		}
		m_Syslog = useSyslog;
		m_Path.assign (path.substr (0, MAX_TEXT_SIZE));
		m_Tag.assign (tag.substr (0, MAX_TEXT_SIZE));
		m_Configured = true;
		return {};
	}

	std::expected<void, std::string> LogSink::Reattach (Channel channel)
	{
		std::lock_guard<std::mutex> lock (m_Mutex);
		if (!m_Configured) return std::unexpected ("log sink is not configured");
		if (m_Shutdown) return std::unexpected ("log sink is shut down");
		m_Channel.Close ();
		if (!channel.IsOpen ()) return std::unexpected ("invalid replacement channel");
		Message message{MessageType::ConfigureLog, 0,
			EncodeLogConfig (m_Syslog, m_Path, m_Tag)};
		auto sent = channel.Send (message);
		if (!sent)
		{
			channel.Close ();
			return sent;
		}
		m_Channel = std::move (channel);
		return {};
	}

	void LogSink::Write (uint8_t level, std::time_t timestamp, std::string_view text)
	{
		std::lock_guard<std::mutex> lock (m_Mutex);
		if (m_Shutdown || !IsLevel (level) || !m_Channel.IsOpen ()) return;
		Message message{MessageType::LogRecord, 0,
			EncodeLogRecord (level, static_cast<int64_t> (timestamp), text)};
		// A bounded send keeps a wedged worker from stalling the logger
		// thread for the full default timeout on every record.
		auto result = m_Channel.Send (message, 1000);
		if (!result) m_Channel.Close (); // drop logs; never fall back to a local file
	}

	std::expected<void, std::string> LogSink::Shutdown ()
	{
		std::lock_guard<std::mutex> lock (m_Mutex);
		m_Shutdown = true;
		if (!m_Channel.IsOpen ()) return {};
		Message message{MessageType::Shutdown, 0, {}};
		auto result = m_Channel.Send (message, 1000);
		m_Channel.Close ();
		return result;
	}

	bool LogSink::IsOpen () const
	{
		std::lock_guard<std::mutex> lock (m_Mutex);
		return !m_Shutdown && m_Channel.IsOpen ();
	}

	bool LogSink::IsShutdown () const
	{
		std::lock_guard<std::mutex> lock (m_Mutex);
		return m_Shutdown;
	}

	int RunLogWorker (int fd)
	{
		Channel channel = Channel::Adopt (fd);
		auto peer = channel.PeerUid ();
		if (!peer || *peer != geteuid ())
		{
			std::cerr << "privsep log: rejecting peer with unexpected uid" << std::endl;
			return 1;
		}

		LogConfig config;
		std::ofstream file;
		while (true)
		{
			auto received = channel.Receive (1000);
			if (!received)
			{
				// Includes EOF: the parent closed the channel or died. Exit
				// instead of polling a closed socket forever.
				std::cerr << "privsep log: " << received.error () << std::endl;
				return 1;
			}
			if (!received->has_value ()) continue;
			const Message& message = **received;

			switch (message.type)
			{
				case MessageType::ConfigureLog:
				{
					if (config.configured)
					{
						std::cerr << "privsep log: duplicate configuration" << std::endl;
						return 1;
					}
					auto parsed = DecodeLogConfig (message.payload);
					if (!parsed)
					{
						std::cerr << "privsep log: invalid configuration" << std::endl;
						return 1;
					}
					auto [useSyslog, path, tag] = *parsed;
					// DecodeLogConfig already rejects NUL; repeat the check so
					// a future codec change cannot turn it into path truncation.
					if (path.find ('\0') != std::string::npos ||
						tag.find ('\0') != std::string::npos)
					{
						std::cerr << "privsep log: NUL in configuration" << std::endl;
						return 1;
					}
					config = {useSyslog, std::move (path), std::move (tag), true};
					if (!ApplyIsolation (config)) return 1;
					if (config.syslog) openlog (config.tag.c_str (), LOG_PID, LOG_DAEMON);
					if (!config.syslog && !config.path.empty ())
					{
						umask (0077); // logs may contain peer addresses and metadata
						file.open (config.path, std::ios::out | std::ios::app);
						if (!file.is_open ())
						{
							std::cerr << "privsep log: cannot open log file" << std::endl;
							return 1;
						}
					}
					break;
				}
				case MessageType::LogRecord:
				{
					if (!config.configured)
					{
						std::cerr << "privsep log: configuration required first" << std::endl;
						return 1;
					}
					auto parsed = DecodeLogRecord (message.payload);
					if (!parsed)
					{
						std::cerr << "privsep log: malformed record" << std::endl;
						return 1;
					}
					auto [level, timestamp, text] = *parsed;
					if (!IsLevel (level))
					{
						std::cerr << "privsep log: invalid level" << std::endl;
						return 1;
					}
					if (config.syslog)
					{
						syslog (level == 1 ? LOG_CRIT : level == 2 ? LOG_ERR : level == 3 ? LOG_WARNING : level == 4 ? LOG_INFO : LOG_DEBUG,
							"%s", text.c_str ());
						break;
					}
					auto line = FormatRecord (level, static_cast<std::time_t> (timestamp), text);
					if (!config.path.empty ())
					{
						file << line << '\n';
						file.flush ();
						if (!file)
						{
							// Disk full/EIO must be observable to the parent's
							// restart policy, not silently dropped forever.
							std::cerr << "privsep log: write failed" << std::endl;
							return 1;
						}
					}
					else
					{
						std::cout << line << '\n';
						std::cout.flush ();
						if (!std::cout)
						{
							std::cerr << "privsep log: stdout write failed" << std::endl;
							return 1;
						}
					}
					break;
				}
				case MessageType::Ping:
				{
					// Control messages are safe before configuration; this lets
					// a supervisor probe a freshly spawned worker.
					Message pong{MessageType::Pong, message.requestId, {}};
					if (!channel.Send (pong)) return 1;
					break;
				}
				case MessageType::Shutdown:
					if (config.syslog) closelog ();
					else if (file.is_open ())
					{
						file.flush ();
						if (!file)
						{
							std::cerr << "privsep log: final flush failed" << std::endl;
							return 1;
						}
					}
					return 0;
				default:
					std::cerr << "privsep log: unexpected message" << std::endl;
					return 1;
			}
		}
	}
}

#endif
