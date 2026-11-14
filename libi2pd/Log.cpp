/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Log.
 *
 * Implements the asynchronous logger: level filtering, timestamp and
 * level formatting, ANSI colouring, and output to stdout, a file/stream
 * or syslog on a dedicated worker thread. A bounded, sanitised
 * recent-record ring serves read-only TUI queries, and start/stop
 * drains, flushes and joins the writer.
 *
 */

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <time.h>
#include <vector>

#ifdef _WIN32
#include <aclapi.h>
#include <sddl.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "Log.h"
#include "Modern.h"
#include "util.h"

//for std::transform
#include <algorithm>

namespace i2p
{
namespace log
{
	namespace
	{
		class NativeLogFile
		{
		public:
#ifdef _WIN32
			explicit NativeLogFile (HANDLE handle = INVALID_HANDLE_VALUE)
				: m_Handle (handle)
			{
			}
			NativeLogFile (NativeLogFile && other) noexcept
				: m_Handle (std::exchange (other.m_Handle, INVALID_HANDLE_VALUE))
			{
			}
			NativeLogFile & operator= (NativeLogFile && other) noexcept
			{
				if (this != &other)
				{
					Reset ();
					m_Handle = std::exchange (other.m_Handle, INVALID_HANDLE_VALUE);
				}
				return *this;
			}
			~NativeLogFile () { Reset (); }
			HANDLE Get () const { return m_Handle; }
			void Reset ()
			{
				if (m_Handle != INVALID_HANDLE_VALUE) CloseHandle (std::exchange (m_Handle, INVALID_HANDLE_VALUE));
			}

		private:
			HANDLE m_Handle;
#else
			explicit NativeLogFile (int fd = -1)
				: m_Fd (fd)
			{
			}
			NativeLogFile (NativeLogFile && other) noexcept
				: m_Fd (std::exchange (other.m_Fd, -1))
			{
			}
			NativeLogFile & operator= (NativeLogFile && other) noexcept
			{
				if (this != &other)
				{
					Reset ();
					m_Fd = std::exchange (other.m_Fd, -1);
				}
				return *this;
			}
			~NativeLogFile () { Reset (); }
			int Get () const { return m_Fd; }
			void Reset ()
			{
				if (m_Fd >= 0) close (std::exchange (m_Fd, -1));
			}

		private:
			int m_Fd;
#endif
			NativeLogFile (const NativeLogFile &) = delete;
			NativeLogFile & operator= (const NativeLogFile &) = delete;
		};

		class AppendLogBuffer final : public std::streambuf
		{
		public:
			explicit AppendLogBuffer (NativeLogFile file)
				: m_File (std::move (file))
			{
			}

		protected:
			std::streamsize xsputn (const char * data, std::streamsize size) override
			{
				if (size <= 0) return 0;
				std::streamsize written = 0;
				while (written < size)
				{
#ifdef _WIN32
					const auto remaining = static_cast<uint64_t> (size - written);
					const DWORD count = static_cast<DWORD> (std::min<uint64_t> (remaining, MAXDWORD));
					DWORD actual = 0;
					if (!WriteFile (m_File.Get (), data + written, count, &actual, nullptr) || !actual) break;
					written += static_cast<std::streamsize> (actual);
#else
					const auto remaining = static_cast<uint64_t> (size - written);
					const auto count = static_cast<size_t> (
						std::min<uint64_t> (remaining, static_cast<uint64_t> (std::numeric_limits<ssize_t>::max ())));
					const ssize_t actual = write (m_File.Get (), data + written, count);
					if (actual < 0 && errno == EINTR) continue;
					if (actual <= 0) break;
					written += static_cast<std::streamsize> (actual);
#endif
				}
				return written;
			}

			int_type overflow (int_type value) override
			{
				if (traits_type::eq_int_type (value, traits_type::eof ())) return traits_type::not_eof (value);
				const char ch = traits_type::to_char_type (value);
				return xsputn (&ch, 1) == 1 ? value : traits_type::eof ();
			}

			int sync () override { return 0; }

		private:
			NativeLogFile m_File;
		};

		class AppendLogStream final : public std::ostream
		{
		public:
			explicit AppendLogStream (NativeLogFile file)
				: std::ostream (nullptr)
				, m_Buffer (std::move (file))
			{
				rdbuf (&m_Buffer);
			}
			~AppendLogStream ()
			{
				flush ();
				rdbuf (nullptr);
			}

		private:
			AppendLogBuffer m_Buffer;
		};

#ifdef _WIN32
		bool CurrentUserSid (std::vector<BYTE> & buffer, PSID & sid)
		{
			HANDLE token = nullptr;
			if (!OpenProcessToken (GetCurrentProcess (), TOKEN_QUERY, &token)) return false;
			const auto closeToken = i2p::modern::ScopeExit ([&] { CloseHandle (token); });
			DWORD size = 0;
			GetTokenInformation (token, TokenUser, nullptr, 0, &size);
			if (GetLastError () != ERROR_INSUFFICIENT_BUFFER || !size) return false;
			buffer.resize (size);
			if (!GetTokenInformation (token, TokenUser, buffer.data (), size, &size)) return false;
			sid = reinterpret_cast<TOKEN_USER *> (buffer.data ())->User.Sid;
			return IsValidSid (sid) != FALSE;
		}

		bool BuildPrivateLogDacl (PSECURITY_DESCRIPTOR & descriptor, PACL & dacl)
		{
			std::vector<BYTE> userBuffer;
			PSID userSid = nullptr;
			if (!CurrentUserSid (userBuffer, userSid)) return false;
			LPWSTR sidText = nullptr;
			if (!ConvertSidToStringSidW (userSid, &sidText)) return false;
			const auto freeSidText = i2p::modern::ScopeExit ([&] { LocalFree (sidText); });
			const std::wstring sddl = L"D:P(A;;FA;;;" + std::wstring (sidText) + L")(A;;FA;;;SY)";
			if (!ConvertStringSecurityDescriptorToSecurityDescriptorW (
					sddl.c_str (), SDDL_REVISION_1, &descriptor, nullptr))
				return false;
			BOOL present = FALSE, defaulted = FALSE;
			if (!GetSecurityDescriptorDacl (descriptor, &present, &dacl, &defaulted) || !present || !dacl)
			{
				LocalFree (descriptor);
				descriptor = nullptr;
				return false;
			}
			return true;
		}

		bool IsPrivateLogOwner (HANDLE handle, PSID expectedOwner)
		{
			PSID owner = nullptr;
			PSECURITY_DESCRIPTOR descriptor = nullptr;
			const DWORD result = GetSecurityInfo (
				handle, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, &owner, nullptr, nullptr, nullptr, &descriptor);
			const bool valid = result == ERROR_SUCCESS && owner && EqualSid (owner, expectedOwner);
			if (descriptor) LocalFree (descriptor);
			return valid;
		}

		bool IsRegularLogFile (HANDLE handle, BY_HANDLE_FILE_INFORMATION & info)
		{
			return GetFileType (handle) == FILE_TYPE_DISK && GetFileInformationByHandle (handle, &info)
				&& !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
				&& info.nNumberOfLinks == 1;
		}

		bool OpenLogDirectoryNoReparse (const std::filesystem::path & path, NativeLogFile & directory)
		{
			HANDLE handle = CreateFileW (path.c_str (), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
				nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
			if (handle == INVALID_HANDLE_VALUE) return false;
			NativeLogFile candidate (handle);
			BY_HANDLE_FILE_INFORMATION info { };
			if (!GetFileInformationByHandle (candidate.Get (), &info)
				|| !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				|| (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
				return false;
			directory = std::move (candidate);
			return true;
		}

		bool SameFile (const BY_HANDLE_FILE_INFORMATION & left, const BY_HANDLE_FILE_INFORMATION & right)
		{
			return left.dwVolumeSerialNumber == right.dwVolumeSerialNumber
				&& left.nFileIndexHigh == right.nFileIndexHigh && left.nFileIndexLow == right.nFileIndexLow;
		}
#endif

		std::shared_ptr<std::ostream> OpenPrivateAppendLog (const std::string & path, std::string & error)
		{
			if (path.empty () || path.find ('\0') != std::string::npos)
			{
				error = "invalid path";
				return nullptr;
			}
			std::error_code pathError;
			auto target = std::filesystem::absolute (std::filesystem::path (path), pathError).lexically_normal ();
			if (pathError || target.empty () || target.filename ().empty ())
			{
				error = "invalid path";
				return nullptr;
			}
#ifdef _WIN32
			std::vector<NativeLogFile> parentHandles;
			NativeLogFile directory;
			auto current = target.root_path ();
			if (current.empty () || !OpenLogDirectoryNoReparse (current, directory))
			{
				error = "can't securely open parent directory";
				return nullptr;
			}
			parentHandles.push_back (std::move (directory));
			for (const auto & component : target.parent_path ().relative_path ())
			{
				if (component.empty () || component == L".") continue;
				current /= component;
				NativeLogFile next;
				if (!OpenLogDirectoryNoReparse (current, next))
				{
					error = "parent path contains a non-directory or reparse point";
					return nullptr;
				}
				parentHandles.push_back (std::move (next));
			}

			PSECURITY_DESCRIPTOR descriptor = nullptr;
			PACL dacl = nullptr;
			if (!BuildPrivateLogDacl (descriptor, dacl))
			{
				error = "can't build private file security";
				return nullptr;
			}
			const auto freeDescriptor = i2p::modern::ScopeExit ([&] { LocalFree (descriptor); });
			SECURITY_ATTRIBUTES attributes { sizeof (SECURITY_ATTRIBUTES), descriptor, FALSE };
			HANDLE handle
				= CreateFileW (target.c_str (), FILE_APPEND_DATA | FILE_READ_ATTRIBUTES | READ_CONTROL | WRITE_DAC,
					FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &attributes, OPEN_ALWAYS,
					FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
			if (handle == INVALID_HANDLE_VALUE)
			{
				error = "can't open file without following reparse points";
				return nullptr;
			}
			NativeLogFile file (handle);
			std::vector<BYTE> userBuffer;
			PSID userSid = nullptr;
			BY_HANDLE_FILE_INFORMATION info { };
			if (!CurrentUserSid (userBuffer, userSid) || !IsRegularLogFile (file.Get (), info)
				|| !IsPrivateLogOwner (file.Get (), userSid))
			{
				error = "file is not a private regular file owned by this user";
				return nullptr;
			}
			HANDLE namedHandle = CreateFileW (target.c_str (), FILE_READ_ATTRIBUTES,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
				FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
			NativeLogFile named (namedHandle);
			BY_HANDLE_FILE_INFORMATION namedInfo { };
			if (namedHandle == INVALID_HANDLE_VALUE || !GetFileInformationByHandle (named.Get (), &namedInfo)
				|| (namedInfo.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || !SameFile (info, namedInfo))
			{
				error = "file path changed during secure open";
				return nullptr;
			}
			if (SetSecurityInfo (file.Get (), SE_FILE_OBJECT,
					DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, dacl, nullptr)
				!= ERROR_SUCCESS)
			{
				error = "can't apply private file security";
				return nullptr;
			}
			return std::make_shared<AppendLogStream> (std::move (file));
#else
			int directoryFlags = O_RDONLY;
#ifdef O_DIRECTORY
			directoryFlags |= O_DIRECTORY;
#else
			error = "directory no-follow support is unavailable";
			return nullptr;
#endif
#ifdef O_NOFOLLOW
			directoryFlags |= O_NOFOLLOW;
#else
			error = "path no-follow support is unavailable";
			return nullptr;
#endif
#ifdef O_CLOEXEC
			directoryFlags |= O_CLOEXEC;
#endif
			const auto root = target.root_path ();
			int directory = open (root.c_str (), directoryFlags);
			if (directory < 0)
			{
				error = "can't securely open root directory";
				return nullptr;
			}
			NativeLogFile parent (directory);
			for (const auto & component : target.parent_path ().relative_path ())
			{
				if (component.empty () || component == ".") continue;
				const auto name = component.string ();
				const int next = openat (parent.Get (), name.c_str (), directoryFlags);
				if (next < 0)
				{
					error = "parent path contains a non-directory or symlink";
					return nullptr;
				}
				parent = NativeLogFile (next);
			}
			int fileFlags = O_WRONLY | O_CREAT | O_APPEND | O_NONBLOCK;
#ifdef O_NOCTTY
			fileFlags |= O_NOCTTY;
#endif
#ifdef O_CLOEXEC
			fileFlags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
			fileFlags |= O_NOFOLLOW;
#endif
			const auto leaf = target.filename ().string ();
			const int fd = openat (parent.Get (), leaf.c_str (), fileFlags, S_IRUSR | S_IWUSR);
			if (fd < 0)
			{
				error = "can't open file without following symlinks";
				return nullptr;
			}
			NativeLogFile file (fd);
			struct stat descriptor { };
			struct stat named { };
			if (fstat (fd, &descriptor) != 0 || !S_ISREG (descriptor.st_mode) || descriptor.st_uid != geteuid ()
				|| descriptor.st_nlink != 1)
			{
				error = "file is not a private regular file owned by this user";
				return nullptr;
			}
			if (fstatat (parent.Get (), leaf.c_str (), &named, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISREG (named.st_mode)
				|| named.st_dev != descriptor.st_dev || named.st_ino != descriptor.st_ino)
			{
				error = "file path changed during secure open";
				return nullptr;
			}
			if (fchmod (fd, S_IRUSR | S_IWUSR) != 0)
			{
				error = "can't apply private file mode";
				return nullptr;
			}
			return std::make_shared<AppendLogStream> (std::move (file));
#endif
		}
	} // namespace

	/*
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

	/*
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
	/*
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
		, m_StartRequested (false)
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
		{
			// The previous writer was joined before this generation starts.
			// Re-arm publication: a stale true value lets the new writer
			// drain and exit before Start publishes its running authority.
			std::lock_guard<std::mutex> start (m_StartMutex);
			m_StartRequested = false;
		}
		// Create the thread first, then publish the running flag. Run() waits
		// on m_StartCondition so it cannot observe a false flag and exit
		// early.
		m_Thread = std::make_unique<std::thread> (std::bind (&Log::Run, this));
		{
			std::lock_guard<std::mutex> start (m_StartMutex);
			m_StartRequested = true;
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

	bool Log::ResetDestination ()
	{
		// Keep the same lifecycle -> destination lock order as
		// Start()/Stop(). The sink and stream may own external resources, so
		// release their last references after dropping m_LogMutex.
		std::lock_guard<std::mutex> lifecycle (m_LifecycleMutex);
		if (m_Thread || m_IsRunning.load (std::memory_order_acquire)) return false;

		std::shared_ptr<std::ostream> previousStream;
		SinkFunction previousSink;
#ifndef _WIN32
		bool closeSyslog = false;
#endif
		{
			std::lock_guard<std::mutex> destination (m_LogMutex);
#ifndef _WIN32
			closeSyslog = m_Destination == eLogSyslog;
#endif
			previousStream = std::move (m_LogStream);
			previousSink = std::move (m_Sink);
			m_Destination = eLogStdout;
			m_Logfile.clear ();
			m_HasColors = true;
			m_TimeFormat = "%H:%M:%S";
		}
#ifndef _WIN32
		if (closeSyslog) closelog ();
#endif
		(void)previousStream;
		(void)previousSink;
		return true;
	}

	std::string str_tolower (std::string s)
	{
		std::transform (s.begin (), s.end (), s.begin (),
			// static_cast&lt;int(*)(int)&gt;(std::tolower)      // wrong
			// [](int c){ return std::tolower(c); }        // wrong
			// [](char c){ return std::tolower(c); }       // wrong
			[] (unsigned char c) { return std::tolower (c); } // correct
		);
		return s;
	}

	void Log::SetLogLevel (const std::string & level_)
	{
		static constexpr std::array<std::pair<std::string_view, LogLevel>, 6> levels { {
			{ "none", eLogNone },
			{ "critical", eLogCritical },
			{ "error", eLogError },
			{ "warn", eLogWarning },
			{ "info", eLogInfo },
			{ "debug", eLogDebug },
		} };
		const std::string level = str_tolower (level_);
		const auto found = std::ranges::find_if (
			levels, [&level] (const auto & entry) { return entry.first == std::string_view (level); });
		if (found == levels.end ())
		{
			LogPrint (eLogCritical, "Log: Unknown loglevel: ", level);
			return;
		}
		m_MinLevel.store (found->second, std::memory_order_relaxed);
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
		m_LastDateTime[0] = 0;
		if (!format.empty () && !strftime (m_LastDateTime, sizeof (m_LastDateTime), format.c_str (), &caltime))
		{
			std::snprintf (m_LastDateTime, sizeof (m_LastDateTime), "%lld", static_cast<long long> (t));
		}
		m_LastDateTime[sizeof (m_LastDateTime) - 1] = 0;
		m_LastTimestamp = t;
		return m_LastDateTime;
	}

	/*
	 * @brief Write one record to the current destination on the writer
	 *        thread.
	 * @note This function better to be run in separate thread due to
	 *       disk i/o. Unfortunately, with current startup process with
	 *       late fork() this will give us nothing but pain. Maybe later.
	 *       See in NetDb as example.
	 *
	 * The destination, sink and stream are snapshotted under m_LogMutex
	 * and used after the lock is dropped, so a concurrent
	 * SendTo()/Reopen() only replaces a pointer and never waits for a slow
	 * disk. A custom sink takes the record verbatim; the file and stdout
	 * paths add the timestamp, thread id and level.
	 *
	 * This layer performs no redaction and no secret filtering. The text
	 * is a persistence boundary: producers must never pass a password,
	 * private key, seed, token or credential-bearing operand, and the
	 * privilege-separated log worker's redaction pass is a safety net for
	 * untrusted text, not a licence to log a secret.
	 */
	void Log::Process (std::shared_ptr<LogMsg> msg)
	{
		if (!msg) return;

		// Snapshot the destination under the lock, then release it before any
		// formatting or I/O. Reopen() can swap the stream concurrently
		// without waiting on the sink or a slow log device.
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
			// A Stop() racing with thread creation must not leave Run() waiting
			// forever for a running flag that was already cleared. The request
			// bit stays true within this run; a later Start re-arms it only
			// after this writer has been joined.
			m_StartCondition.wait (l, [this] () { return m_StartRequested; });
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

	/*
	 * @brief Enqueue a record and update the bounded recent-record ring.
	 *
	 * Two independent bounds apply. The queue holds at most
	 * LOG_QUEUE_MAX_RECORDS (8192) entries; beyond that the record is
	 * dropped and only a counter is incremented, so a stalled writer
	 * degrades into lossy logging instead of unbounded memory growth. The
	 * TUI ring keeps the newest LOG_RECENT_CAPACITY (512) records with at
	 * most LOG_RECENT_TEXT_LIMIT bytes of text each.
	 *
	 * The ring copy is additionally flattened: control bytes below 0x20
	 * except TAB are replaced with spaces so a hostile log line cannot
	 * inject terminal escapes into an administrative client. The queued
	 * text keeps its bytes (only the hard size cap applies), which is why
	 * producers must still keep secrets out of the message.
	 */
	void Log::Append (std::shared_ptr<i2p::log::LogMsg> & msg)
	{
		std::string diagnosticText;
		if (msg)
		{
			if (msg->text.size () > LOG_QUEUE_MAX_TEXT) msg->text.resize (LOG_QUEUE_MAX_TEXT);
			// Make a bounded copy before handing the message to the writer.
			// Control characters are flattened so a malformed log line cannot
			// inject terminal escapes into an administrative client.
			diagnosticText = msg->text.substr (0, LOG_RECENT_TEXT_LIMIT);
			for (auto & ch : diagnosticText)
				if (static_cast<unsigned char> (ch) < 0x20 && ch != '\t') ch = ' ';
			std::string ringText = diagnosticText;
			{
				std::lock_guard<std::mutex> l (m_RecentMutex);
				m_Recent.push_back (LogRecord { ++m_RecentSequence, msg->timestamp, msg->level, std::move (ringText) });
				while (m_Recent.size () > LOG_RECENT_CAPACITY) m_Recent.pop_front ();
			}
		}
		// The test diagnostic sink runs before the queue so a later abort
		// still leaves the record in the CI log. It is copied under the
		// lock and invoked after releasing it: the callback is not allowed
		// to call back into Log, but it must not be able to deadlock the
		// logging path either.
		SinkFunction diagnostic;
		if (m_HasSynchronousSink.load (std::memory_order_relaxed))
		{
			std::lock_guard<std::mutex> l (m_LogMutex);
			diagnostic = m_SynchronousSink;
		}
		if (diagnostic && msg)
		{
			try
			{
				diagnostic (msg->level, msg->timestamp, diagnosticText);
			}
			catch (...)
			{
				// A diagnostic sink must never be able to fail logging.
			}
		}
		if (!m_Queue.PutIfBelow (msg, LOG_QUEUE_MAX_RECORDS)) m_QueuedDropped.fetch_add (1, std::memory_order_relaxed);
	}

	void Log::SetSynchronousSink (SinkFunction sink)
	{
		const bool installed = static_cast<bool> (sink);
		{
			std::lock_guard<std::mutex> l (m_LogMutex);
			m_SynchronousSink = std::move (sink);
		}
		m_HasSynchronousSink.store (installed, std::memory_order_relaxed);
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
		std::string error;
		auto os = OpenPrivateAppendLog (path, error);
		if (!os)
		{
			LogPrint (eLogCritical, "Log: Can't securely open file: ", error);
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
		// concurrent SendTo() is never overwritten by this signal-thread
		// path.
		std::string error;
		auto os = OpenPrivateAppendLog (logfile, error);
		if (!os)
		{
			LogPrint (eLogCritical, "Log: Can't securely reopen file: ", error);
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
