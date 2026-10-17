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
			bool syslog;
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
			if (!config.path.empty () && unveil (config.path.c_str (), "rwc") == -1)
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
			localtime_r (&timestamp, &tm);
			char date[32] = "??:??:??";
			strftime (date, sizeof (date), "%Y-%m-%d %H:%M:%S", &tm);
			std::string line = date;
			line += " [";
			line += levels[level <= 5 ? level : 0];
			line += "] ";
			for (char ch : text.substr (0, MAX_LINE))
				line.push_back ((static_cast<unsigned char> (ch) < 0x20 && ch != '\t') ? ' ' : ch);
			return line;
		}
	}

	std::expected<void, std::string> LogSink::Configure (bool useSyslog, std::string_view path,
		std::string_view tag)
	{
		Message message{MessageType::ConfigureLog, 0, EncodeLogConfig (useSyslog, path, tag)};
		return m_Channel.Send (message);
	}

	void LogSink::Write (uint8_t level, std::time_t timestamp, std::string_view text)
	{
		if (!IsLevel (level) || !m_Channel.IsOpen ()) return;
		Message message{MessageType::LogRecord, 0,
			EncodeLogRecord (level, static_cast<int64_t> (timestamp), text)};
		auto result = m_Channel.Send (message);
		if (!result) m_Channel.Close (); // drop logs; never fall back to a local file
	}

	std::expected<void, std::string> LogSink::Shutdown ()
	{
		if (!m_Channel.IsOpen ()) return {};
		Message message{MessageType::Shutdown, 0, {}};
		auto result = m_Channel.Send (message, 1000);
		m_Channel.Close ();
		return result;
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
		int records = 0;
		while (true)
		{
			auto received = channel.Receive (1000);
			if (!received)
			{
				std::cerr << "privsep log: " << received.error () << std::endl;
				return 1;
			}
			if (!received->has_value ()) continue;
			const Message& message = **received;
			if (!config.configured)
			{
				if (message.type != MessageType::ConfigureLog)
				{
					std::cerr << "privsep log: configuration required first" << std::endl;
					return 1;
				}
				auto parsed = DecodeLogConfig (message.payload);
				if (!parsed)
				{
					std::cerr << "privsep log: invalid configuration" << std::endl;
					return 1;
				}
				auto [useSyslog, path, tag] = *parsed;
				config = {useSyslog, std::move (path), std::move (tag), true};
				if (!ApplyIsolation (config)) return 1;
				if (config.syslog) openlog (config.tag.c_str (), LOG_PID, LOG_DAEMON);
				if (!config.syslog && !config.path.empty ())
				{
					umask (0077); // logs may contain peer addresses and metadata
					file.open (config.path, std::ios::out | std::ios::app);
					if (!file)
					{
						std::cerr << "privsep log: cannot open log file" << std::endl;
						return 1;
					}
				}
				continue;
			}

			switch (message.type)
			{
				case MessageType::LogRecord:
				{
					auto parsed = DecodeLogRecord (message.payload);
					if (!parsed)
					{
						std::cerr << "privsep log: malformed record" << std::endl;
						return 1;
					}
					auto [level, timestamp, text] = *parsed;
					if (!IsLevel (level)) return 1;
					if (++records > 1000000) records = 0; // bounded statistic only
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
					}
					else
					{
						std::cout << line << '\n';
						std::cout.flush ();
					}
					break;
				}
				case MessageType::Ping:
				{
					Message pong{MessageType::Pong, message.requestId, {}};
					if (!channel.Send (pong)) return 1;
					break;
				}
				case MessageType::Shutdown:
					file.flush ();
					if (config.syslog) closelog ();
					return 0;
				default:
					std::cerr << "privsep log: unexpected message" << std::endl;
					return 1;
			}
		}
	}
}

#endif
