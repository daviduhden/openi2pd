/*
* Copyright (c) 2013-2022, The PurpleI2P Project
* Copyright (c) 2026, David Uhden Collado
* SPDX-License-Identifier: BSD-3-Clause
*
* This file is part of openi2pd and licensed under BSD-3-Clause
*
* See full license text in LICENSE file at top of project tree
*/

#include <time.h>

#include "Log.h"
#include "util.h"

//for std::transform
#include <algorithm>

namespace i2p
{
namespace log
{
	/**
	 * @brief Maps our loglevel to their symbolic name
	 */
	static const char * g_LogLevelStr[eNumLogLevels] = {
		"none", // eLogNone
		"critical", // eLogCritical
		"error", // eLogError
		"warn", // eLogWarning
		"info", // eLogInfo
		"debug" // eLogDebug
	};

	/**
	 * @brief Colorize log output -- array of terminal control sequences
	 * @note Using ISO 6429 (ANSI) color sequences
	 */
#ifdef _WIN32
	static const char * LogMsgColors[] = { "", "", "", "", "", "", "" };
#else /* UNIX */
	static const char * LogMsgColors[] = {
		"\033[1;32m", /* none:    green */
		"\033[1;41m", /* critical: red background */
		"\033[1;31m", /* error:   red */
		"\033[1;33m", /* warning: yellow */
		"\033[1;36m", /* info:    cyan */
		"\033[1;34m", /* debug:   blue */
		"\033[0m" /* reset */
	};
#endif

#ifndef _WIN32
	/**
	 * @brief Maps our log levels to syslog one
	 * @return syslog priority LOG_*, as defined in syslog.h
	 */
	static inline int GetSyslogPrio (enum LogLevel l)
	{
		int priority = LOG_DEBUG;
		switch (l)
		{
		case eLogNone: priority = LOG_CRIT; break;
		case eLogCritical: priority = LOG_CRIT; break;
		case eLogError: priority = LOG_ERR; break;
		case eLogWarning: priority = LOG_WARNING; break;
		case eLogInfo: priority = LOG_INFO; break;
		case eLogDebug: priority = LOG_DEBUG; break;
		default: priority = LOG_DEBUG; break;
		}
		return priority;
	}
#endif

	Log::Log ()
		: m_Destination (eLogStdout)
		, m_MinLevel (eLogInfo)
		, m_LogStream (nullptr)
		, m_Logfile ("")
		, m_LastTimestamp (static_cast<std::time_t> (-1))
		, m_LastDateTime { }
		, m_HasColors (true)
		, m_TimeFormat ("%H:%M:%S")
		, m_IsRunning (false)
		, m_Thread (nullptr)
		, m_RecentSequence (0)
	{
	}

	Log::~Log ()
	{
		// A logger that was never explicitly stopped must not leave a
		// joinable thread behind or lose its final buffered records.
		Stop ();
	}

	void Log::Start ()
	{
		std::lock_guard<std::mutex> l (m_LifecycleMutex);
		if (m_Thread || m_IsRunning.load (std::memory_order_relaxed)) return;
		// Create the thread first, then publish the running flag. Run() waits
		// on m_StartCondition so it cannot observe a false flag and exit early.
		m_Thread = std::make_unique<std::thread> (std::bind (&Log::Run, this));
		{
			std::lock_guard<std::mutex> start (m_StartMutex);
			m_IsRunning.store (true, std::memory_order_release);
		}
		m_StartCondition.notify_all ();
	}

	void Log::Stop ()
	{
		std::lock_guard<std::mutex> lifecycle (m_LifecycleMutex);
		{
			std::lock_guard<std::mutex> start (m_StartMutex);
			m_IsRunning.store (false, std::memory_order_relaxed);
		}
		m_StartCondition.notify_all ();
		m_Queue.WakeUp ();
		if (m_Thread)
		{
			if (m_Thread->joinable ()) m_Thread->join ();
			m_Thread.reset ();
		}

		LogType destination;
		{
			std::lock_guard<std::mutex> l (m_LogMutex);
			destination = m_Destination;
		}
		switch (destination)
		{
#ifndef _WIN32
		case eLogSyslog: closelog (); break;
#endif
		case eLogFile:
		case eLogStream: FlushStream (); break;
		default:
			/* do nothing */
			break;
		}
	}

	std::string str_tolower (std::string s)
	{
		std::transform (s.begin (), s.end (), s.begin (),
			// static_cast<int(*)(int)>(std::tolower)      // wrong
			// [](int c){ return std::tolower(c); }        // wrong
			// [](char c){ return std::tolower(c); }       // wrong
			[] (unsigned char c) { return std::tolower (c); } // correct
		);
		return s;
	}

	void Log::SetLogLevel (const std::string & level_)
	{
		std::string level = str_tolower (level_);
		if (level == "none")
		{
			m_MinLevel.store (eLogNone, std::memory_order_relaxed);
		}
		else if (level == "critical")
		{
			m_MinLevel.store (eLogCritical, std::memory_order_relaxed);
		}
		else if (level == "error")
		{
			m_MinLevel.store (eLogError, std::memory_order_relaxed);
		}
		else if (level == "warn")
		{
			m_MinLevel.store (eLogWarning, std::memory_order_relaxed);
		}
		else if (level == "info")
		{
			m_MinLevel.store (eLogInfo, std::memory_order_relaxed);
		}
		else if (level == "debug")
		{
			m_MinLevel.store (eLogDebug, std::memory_order_relaxed);
		}
		else
		{
			LogPrint (eLogCritical, "Log: Unknown loglevel: ", level);
			return;
		}
		LogPrint (eLogInfo, "Log: Logging level set to ", level);
	}

	const char * Log::TimeAsString (std::time_t t)
	{
		std::string format;
		{
			std::lock_guard<std::mutex> l (m_LogMutex);
			format = m_TimeFormat;
		}
		struct tm caltime;
#ifdef _WIN32
		localtime_s (&caltime, &t);
#else
		localtime_r (&t, &caltime);
#endif
		strftime (m_LastDateTime, sizeof (m_LastDateTime), format.c_str (), &caltime);
		m_LastTimestamp = t;
		return m_LastDateTime;
	}

	/**
	 * @note This function better to be run in separate thread due to disk i/o.
	 * Unfortunately, with current startup process with late fork() this
	 * will give us nothing but pain. Maybe later. See in NetDb as example.
	 */
	void Log::Process (std::shared_ptr<LogMsg> msg)
	{
		if (!msg) return;

		// Snapshot the destination under the lock, then release it before any
		// formatting or I/O. Reopen() can swap the stream concurrently without
		// waiting on the sink or a slow log device.
		SinkFunction sink;
		LogType destination;
		std::shared_ptr<std::ostream> stream;
		{
			std::lock_guard<std::mutex> l (m_LogMutex);
			sink = m_Sink;
			destination = m_Destination;
			stream = m_LogStream;
		}
		if (sink)
		{
			sink (msg->level, msg->timestamp, msg->text);
			return;
		}
		std::hash<std::thread::id> hasher;
		unsigned short short_tid;
		short_tid = (short)(hasher (msg->tid) % 1000);
		switch (destination)
		{
#ifndef _WIN32
		case eLogSyslog: syslog (GetSyslogPrio (msg->level), "[%03u] %s", short_tid, msg->text.c_str ()); break;
#endif
		case eLogFile:
		case eLogStream:
			if (stream)
				*stream << TimeAsString (msg->timestamp) << "@" << short_tid << "/" << g_LogLevelStr[msg->level]
						<< " - " << msg->text << std::endl;
			break;
		case eLogStdout:
		default:
			std::cout << TimeAsString (msg->timestamp) << "@" << short_tid << "/" << LogMsgColors[msg->level]
					  << g_LogLevelStr[msg->level] << LogMsgColors[eNumLogLevels] << " - " << msg->text << std::endl;
			break;
		} // switch
	}

	void Log::FlushStream ()
	{
		std::shared_ptr<std::ostream> stream;
		{
			std::lock_guard<std::mutex> l (m_LogMutex);
			stream = m_LogStream;
		}
		if (stream) stream->flush ();
	}

	void Log::Run ()
	{
		i2p::util::SetThreadName ("Logging");

		{
			std::unique_lock<std::mutex> l (m_StartMutex);
			m_StartCondition.wait (l, [this] () { return m_IsRunning.load (std::memory_order_acquire); });
		}

		Reopen ();
		while (m_IsRunning.load (std::memory_order_relaxed))
		{
			std::shared_ptr<LogMsg> msg;
			while ((msg = m_Queue.Get ())) Process (msg);
			FlushStream ();
			// Bounded wait: Stop() may clear m_IsRunning and signal between
			// the check above and Wait(), and a missed wakeup must not hang
			// the join.
			if (m_IsRunning.load (std::memory_order_relaxed)) m_Queue.Wait (1, 0);
		}

		// Stop() wakes the thread after clearing m_IsRunning. Drain and flush
		// messages queued during shutdown before the logger is joined.
		std::shared_ptr<LogMsg> msg;
		while ((msg = m_Queue.Get ())) Process (msg);
		FlushStream ();
	}

	void Log::Append (std::shared_ptr<i2p::log::LogMsg> & msg)
	{
		if (msg)
		{
			// Make a bounded copy before handing the message to the writer.
			// Control characters are flattened so a malformed log line cannot
			// inject terminal escapes into an administrative client.
			std::string text = msg->text.substr (0, LOG_RECENT_TEXT_LIMIT);
			for (auto & ch : text)
				if (static_cast<unsigned char> (ch) < 0x20 && ch != '\t') ch = ' ';
			{
				std::lock_guard<std::mutex> l (m_RecentMutex);
				m_Recent.push_back (LogRecord { ++m_RecentSequence, msg->timestamp, msg->level, std::move (text) });
				while (m_Recent.size () > LOG_RECENT_CAPACITY) m_Recent.pop_front ();
			}
		}
		m_Queue.Put (msg);
	}

	std::vector<LogRecord> Log::GetRecent (uint64_t after, size_t max) const
	{
		std::vector<LogRecord> result;
		std::lock_guard<std::mutex> l (m_RecentMutex);
		result.reserve (std::min (max, m_Recent.size ()));
		for (const auto & record : m_Recent)
		{
			if (record.sequence <= after) continue;
			result.push_back (record);
			if (result.size () >= max) break;
		}
		return result;
	}

	void Log::SendTo (const std::string & path)
	{
		// Open outside the lock: a slow or blocking open must not stall the
		// signal thread while it is reopening the log.
		auto flags = std::ofstream::out | std::ofstream::app;
		auto os = std::make_shared<std::ofstream> (path, flags);
		if (!os->is_open ())
		{
			LogPrint (eLogCritical, "Log: Can't open file ", path);
			return;
		}
		InstallFileStream (path, std::move (os), false);
	}

	void Log::InstallFileStream (
		const std::string & path, std::shared_ptr<std::ostream> stream, bool onlyIfFileDestination)
	{
		std::shared_ptr<std::ostream> previous;
		{
			std::lock_guard<std::mutex> l (m_LogMutex);
			// Reopen() must not override a destination that another thread
			// installed while the replacement file was being opened.
			if (onlyIfFileDestination && (m_Destination != eLogFile || m_Logfile != path)) return;
			previous = std::move (m_LogStream);
			m_HasColors = false;
			m_Logfile = path;
			m_Destination = eLogFile;
			m_LogStream = std::move (stream);
		}
		// previous is destroyed outside the lock so closing a slow log device
		// cannot stall a concurrent Reopen().
	}

	void Log::SendTo (std::shared_ptr<std::ostream> os)
	{
		std::shared_ptr<std::ostream> previous;
		{
			std::lock_guard<std::mutex> l (m_LogMutex);
			previous = std::move (m_LogStream);
			m_HasColors = false;
			m_Destination = eLogStream;
			m_LogStream = std::move (os);
		}
	}

#ifndef _WIN32
	void Log::SendTo (const char * name, int facility)
	{
		if (m_MinLevel.load (std::memory_order_relaxed) == eLogNone) return;
		openlog (name, LOG_CONS | LOG_PID, facility);
		std::shared_ptr<std::ostream> previous;
		{
			std::lock_guard<std::mutex> l (m_LogMutex);
			previous = std::move (m_LogStream);
			m_HasColors = false;
			m_Destination = eLogSyslog;
			m_LogStream = nullptr;
		}
	}
#endif

	void Log::Reopen ()
	{
		std::string logfile;
		{
			std::lock_guard<std::mutex> l (m_LogMutex);
			if (m_Destination != eLogFile) return;
			logfile = m_Logfile;
		}
		if (logfile.empty ()) return;
		// Open before taking the lock, then re-check the destination so a
		// concurrent SendTo() is never overwritten by this signal-thread path.
		auto flags = std::ofstream::out | std::ofstream::app;
		auto os = std::make_shared<std::ofstream> (logfile, flags);
		if (!os->is_open ())
		{
			LogPrint (eLogCritical, "Log: Can't reopen file ", logfile);
			return;
		}
		InstallFileStream (logfile, std::move (os), true);
	}

	Log & Logger ()
	{
		// Intentionally leaked function-local singleton. Log records can be
		// emitted from destructors of other translation units whose order
		// relative to a static Log is unspecified. Keeping the object alive
		// guarantees Logger() remains usable during static destruction.
		static Log * logger = new Log ();
		return *logger;
	}

	static ThrowFunction g_ThrowFunction;
	ThrowFunction GetThrowFunction ()
	{
		return g_ThrowFunction;
	}
	void SetThrowFunction (ThrowFunction f)
	{
		g_ThrowFunction = f;
	}

} // log
} // i2p
