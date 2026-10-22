/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: LogWorker.
 *
 * Implements LogSink configuration and reattachment plus the log worker: the
 * sink sends configuration (optionally with a pre-opened descriptor) and
 * records, while the worker applies its role sandbox, sanitises records and
 * writes to one O_NOFOLLOW file or syslog. Control characters are flattened
 * so a record cannot forge extra physical log lines.
 *
 */

#include "LogWorker.h"
#include "Worker.h"
#include "LinuxSandbox.h"
#include "OpenBSDSandbox.h"

#ifndef _WIN32

#include <cerrno>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <unistd.h>
#include <sys/stat.h>

#ifndef _WIN32
#include <syslog.h>
#endif

namespace i2p::privsep
{
std::string SanitizeLogText (std::string_view text)
{
	// Keep one record bounded and flatten every byte that could start a new
	// physical line or emit a terminal control sequence. Tab is preserved.
	static constexpr size_t MAX_LINE = MAX_TEXT_SIZE;
	std::string line;
	line.reserve (std::min (text.size (), MAX_LINE));
	for (char ch : text.substr (0, MAX_LINE))
	{
		const auto byte = static_cast<unsigned char> (ch);
		line.push_back ((byte < 0x20 && ch != '\t') || byte == 0x7f ? ' ' : ch);
	}
	return line;
}

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

	int OpenLogFile (std::string_view path)
	{
		std::string copy (path);
		int flags = O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC;
#ifdef O_NOFOLLOW
		flags |= O_NOFOLLOW;
#endif
		return open (copy.c_str (), flags, 0600);
	}

#ifdef __OpenBSD__
	bool ApplyIsolation (const LogConfig & config, bool preopenedFile)
	{
		OpenBSDPolicyConfig policyConfig;
		policyConfig.logPath = config.path;
		policyConfig.logToSyslog = config.syslog;
		policyConfig.preopenedLog = preopenedFile;
		auto policy = BuildOpenBSDPolicy (OpenBSDWorkerRole::Log, policyConfig);
		if (!policy)
		{
			std::cerr << "privsep log: invalid OpenBSD policy: " << policy.error () << std::endl;
			return false;
		}
		auto applied = ApplyOpenBSDPolicy (*policy);
		if (!applied)
		{
			std::cerr << "privsep log: OpenBSD sandbox: " << applied.error () << std::endl;
			return false;
		}
		return true;
	}
#else
	bool ApplyIsolation (const LogConfig & config, bool preopenedFile)
	{
#if defined(__linux__)
		LinuxSandboxProfile profile = GetLinuxSandboxProfile (WorkerClass::Log);
		if (!config.syslog && !config.path.empty () && config.path.front () == '/')
		{
			// The worker owns exactly one log file. The path was opened before
			// this call (pre-opened or legacy mode), so Landlock can restrict
			// writes to that file rather than to its whole parent directory.
			profile.allowPersistentWrites = true;
			profile.writablePaths = { config.path };
		}
		// A log worker does not need Landlock on old kernels when it only has
		// a pre-opened descriptor or stdout; seccomp still denies AF_INET.
		profile.requireLandlock = false;
		auto applied = ApplyLinuxSandbox (profile);
		if (!applied)
		{
			std::cerr << "privsep log: Linux sandbox: " << applied.error () << std::endl;
			return false;
		}
#endif
		(void)config;
		(void)preopenedFile;
		return true;
	}
#endif

	std::string FormatRecord (uint8_t level, std::time_t timestamp, std::string_view text)
	{
		static constexpr const char * levels[] = { "none", "critical", "error", "warn", "info", "debug" };
		std::tm tm { };
		char date[32] = "??:??:??";
		if (localtime_r (&timestamp, &tm)) strftime (date, sizeof (date), "%Y-%m-%d %H:%M:%S", &tm);
		std::string line = date;
		line += " [";
		line += levels[level <= 5 ? level : 0];
		line += "] ";
		line += SanitizeLogText (text);
		return line;
	}
}

i2p::expected<void, std::string> LogSink::WaitForPong (Channel & channel, uint32_t expected, int timeoutMs)
{
	auto deadline = std::chrono::steady_clock::now () + std::chrono::milliseconds (timeoutMs);
	while (std::chrono::steady_clock::now () < deadline)
	{
		const auto remaining
			= std::chrono::duration_cast<std::chrono::milliseconds> (deadline - std::chrono::steady_clock::now ())
				  .count ();
		if (remaining <= 0) break;
		auto received = channel.Receive (static_cast<int> (std::max<int64_t> (remaining, 1)));
		if (!received) return i2p::unexpected (received.error ());
		if (!*received) continue;
		const auto & message = **received;
		if (message.type == MessageType::Pong && message.requestId == expected) return { };
		if (message.type == MessageType::Pong) continue;
		return i2p::unexpected ("unexpected response while waiting for log worker readiness");
	}
	return i2p::unexpected ("log worker did not acknowledge configuration");
}

i2p::expected<void, std::string> LogSink::ConfigureChannel (
	Channel & channel, MessageType type, bool useSyslog, std::string_view path, std::string_view tag, int fd)
{
	auto encoded = EncodeLogConfig (useSyslog, path, tag);
	if (!encoded)
		return i2p::unexpected (std::string ("invalid log configuration: ") + std::string (encoded.error ().text));
	const uint32_t configureId = ++m_NextRequest;
	Message message { type, configureId, *encoded };
	auto sent = fd >= 0 ? channel.SendWithFd (message, fd) : channel.Send (message);
	if (!sent) return sent;
	// Configure is ordered before Ping on SOCK_SEQPACKET/message pipes. The
	// matching Pong therefore proves that the worker decoded and attached the
	// configuration before startup reports success.
	const uint32_t pingId = ++m_NextRequest;
	if (!channel.Send (Message { MessageType::Ping, pingId, { } }, 5000))
		return i2p::unexpected ("cannot probe configured log worker");
	return WaitForPong (channel, pingId, 5000);
}

i2p::expected<void, std::string> LogSink::Configure (bool useSyslog, std::string_view path, std::string_view tag)
{
	std::lock_guard<std::mutex> lock (m_Mutex);
	if (m_Configured) return i2p::unexpected ("log sink already configured");
	int logFd = -1;
	if (!useSyslog && !path.empty ())
	{
		logFd = OpenLogFile (path);
		if (logFd < 0) return i2p::unexpected (std::string ("open log file: ") + strerror (errno));
	}
	auto configured = ConfigureChannel (m_Channel, MessageType::ConfigureLogFd, useSyslog, path, tag, logFd);
	if (logFd >= 0) close (logFd);
	if (!configured)
	{
		m_Channel.Close ();
		return configured;
	}
	m_Syslog = useSyslog;
	m_Path.assign (path);
	m_Tag.assign (tag.substr (0, MAX_TEXT_SIZE));
	m_Configured = true;
	return { };
}

i2p::expected<void, std::string> LogSink::ConfigureProxy (bool useSyslog, std::string_view path, std::string_view tag)
{
	std::lock_guard<std::mutex> lock (m_Mutex);
	if (m_Configured) return i2p::unexpected ("log sink already configured");
	auto configured = ConfigureChannel (m_Channel, MessageType::ConfigureLog, useSyslog, path, tag, -1);
	if (!configured)
	{
		m_Channel.Close ();
		return configured;
	}
	m_Syslog = useSyslog;
	m_Path.assign (path);
	m_Tag.assign (tag.substr (0, MAX_TEXT_SIZE));
	m_Configured = true;
	return { };
}

i2p::expected<void, std::string> LogSink::Reattach (Channel channel)
{
	std::lock_guard<std::mutex> lock (m_Mutex);
	if (!m_Configured) return i2p::unexpected ("log sink is not configured");
	if (m_Shutdown) return i2p::unexpected ("log sink is shut down");
	int logFd = -1;
	if (!m_Syslog && !m_Path.empty ())
	{
		logFd = OpenLogFile (m_Path);
		if (logFd < 0) return i2p::unexpected (std::string ("open log file: ") + strerror (errno));
	}
	if (!channel.IsOpen ())
	{
		if (logFd >= 0) close (logFd);
		return i2p::unexpected ("invalid replacement channel");
	}
	auto configured = ConfigureChannel (channel, MessageType::ConfigureLogFd, m_Syslog, m_Path, m_Tag, logFd);
	if (logFd >= 0) close (logFd);
	if (!configured)
	{
		channel.Close ();
		return configured;
	}
	m_Channel.Close ();
	m_Channel = std::move (channel);
	return { };
}

i2p::expected<void, std::string> LogSink::ReattachProxy (Channel channel)
{
	std::lock_guard<std::mutex> lock (m_Mutex);
	if (!m_Configured) return i2p::unexpected ("log sink is not configured");
	if (m_Shutdown) return i2p::unexpected ("log sink is shut down");
	if (!channel.IsOpen ()) return i2p::unexpected ("invalid replacement channel");
	auto configured = ConfigureChannel (channel, MessageType::ConfigureLog, m_Syslog, m_Path, m_Tag, -1);
	if (!configured)
	{
		channel.Close ();
		return configured;
	}
	m_Channel.Close ();
	m_Channel = std::move (channel);
	return { };
}

void LogSink::Write (uint8_t level, std::time_t timestamp, std::string_view text)
{
	std::lock_guard<std::mutex> lock (m_Mutex);
	if (m_Shutdown || !IsLevel (level)) return;
	if (!m_Channel.IsOpen ())
	{
		++m_Dropped;
		return;
	}
	Message message { MessageType::LogRecord, 0, EncodeLogRecord (level, static_cast<int64_t> (timestamp), text) };
	// A bounded send keeps a wedged worker from stalling the logger
	// thread for the full default timeout on every record.
	auto result = m_Channel.Send (message, 1000);
	if (!result)
	{
		++m_Dropped;
		m_Channel.Close (); // drop logs; never fall back to a local file
	}
}

uint64_t LogSink::DroppedCount () const
{
	std::lock_guard<std::mutex> lock (m_Mutex);
	return m_Dropped;
}

i2p::expected<void, std::string> LogSink::Shutdown ()
{
	std::lock_guard<std::mutex> lock (m_Mutex);
	m_Shutdown = true;
	if (!m_Channel.IsOpen ()) return { };
	Message message { MessageType::Shutdown, 0, { } };
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
	SanitizeInheritedStdio ();
	Channel channel = Channel::Adopt (fd);
	auto peer = channel.PeerUid ();
	if (!peer || *peer != geteuid ())
	{
		std::cerr << "privsep log: rejecting peer with unexpected uid" << std::endl;
		return 1;
	}

	LogConfig config;
	FILE * file = nullptr;
	while (true)
	{
		auto received = channel.ReceiveWithFd (1000);
		if (!received)
		{
			// Includes EOF: the parent closed the channel or died. Exit
			// instead of polling a closed socket forever.
			std::cerr << "privsep log: " << received.error () << std::endl;
			if (file) fclose (file);
			return 1;
		}
		if (!received->has_value ()) continue;
		const Message & message = (*received)->message;
		int messageFd = (*received)->fd;
		auto closeMessageFd = [&messageFd] ()
		{
			if (messageFd >= 0)
			{
				close (messageFd);
				messageFd = -1;
			}
		};

		if (!AllowsOperation (EndpointRole::Log, message.type))
		{
			closeMessageFd ();
			std::cerr << "privsep log: operation is not allowed on this endpoint" << std::endl;
			return 1;
		}
		switch (message.type)
		{
		case MessageType::ConfigureLog:
		case MessageType::ConfigureLogFd:
		{
			if (config.configured)
			{
				std::cerr << "privsep log: duplicate configuration" << std::endl;
				closeMessageFd ();
				return 1;
			}
			auto parsed = DecodeLogConfig (message.payload);
			if (!parsed)
			{
				std::cerr << "privsep log: invalid configuration" << std::endl;
				closeMessageFd ();
				return 1;
			}
			auto [useSyslog, path, tag] = *parsed;
			// DecodeLogConfig already rejects NUL; repeat the check so
			// a future codec change cannot turn it into path truncation.
			if (path.find ('\0') != std::string::npos || tag.find ('\0') != std::string::npos)
			{
				std::cerr << "privsep log: NUL in configuration" << std::endl;
				closeMessageFd ();
				return 1;
			}
			const bool fdMode = message.type == MessageType::ConfigureLogFd;
			int legacyFd = -1;
			if (fdMode)
			{
				if (!useSyslog && !path.empty ())
				{
					auto descriptor = ValidatePreopenedDescriptor (messageFd, false);
					if (!descriptor)
					{
						std::cerr << "privsep log: invalid pre-opened log descriptor: " << descriptor.error ()
								  << std::endl;
						closeMessageFd ();
						return 1;
					}
					// Restrict the mode before ApplyIsolation(): the OpenBSD
					// preopened policy deliberately omits the fattr promise,
					// so fchmod must not run after pledge.
					if (fchmod (messageFd, S_IRUSR | S_IWUSR) != 0)
					{
						std::cerr << "privsep log: cannot restrict pre-opened log descriptor: " << strerror (errno)
								  << std::endl;
						closeMessageFd ();
						return 1;
					}
				}
				else if (messageFd >= 0)
				{
					std::cerr << "privsep log: unexpected descriptor for non-file sink" << std::endl;
					closeMessageFd ();
					return 1;
				}
			}
			else if (messageFd >= 0)
			{
				std::cerr << "privsep log: legacy configuration cannot carry a descriptor" << std::endl;
				closeMessageFd ();
				return 1;
			}
			else if (!useSyslog && !path.empty ())
			{
				// Open the legacy path before the sandbox so the worker can
				// adopt a descriptor without retaining pathname write access.
				// The configured path is still O_NOFOLLOW/O_CLOEXEC/0600.
				legacyFd = OpenLogFile (path);
				if (legacyFd < 0)
				{
					std::cerr << "privsep log: cannot open log file: " << strerror (errno) << std::endl;
					closeMessageFd ();
					return 1;
				}
				if (fchmod (legacyFd, S_IRUSR | S_IWUSR) != 0)
				{
					std::cerr << "privsep log: cannot restrict log file: " << strerror (errno) << std::endl;
					close (legacyFd);
					closeMessageFd ();
					return 1;
				}
			}
			config = { useSyslog, std::move (path), std::move (tag), true };
			const bool preopenedFile = !config.syslog && !config.path.empty () && (fdMode || legacyFd >= 0);
			if (!ApplyIsolation (config, preopenedFile))
			{
				if (legacyFd >= 0) close (legacyFd);
				closeMessageFd ();
				return 1;
			}
			if (config.syslog) openlog (config.tag.c_str (), LOG_PID, LOG_DAEMON);
			if (!config.syslog && !config.path.empty ())
			{
				const int fileFd = messageFd >= 0 ? messageFd : legacyFd;
				file = fdopen (fileFd, "a");
				if (file)
				{
					if (fileFd == messageFd) messageFd = -1;
					if (fileFd == legacyFd) legacyFd = -1;
				}
				else
				{
					if (fileFd >= 0) close (fileFd);
					if (fileFd == messageFd) messageFd = -1;
					if (fileFd == legacyFd) legacyFd = -1;
				}
				if (!file)
				{
					std::cerr << "privsep log: cannot attach log stream" << std::endl;
					closeMessageFd ();
					return 1;
				}
			}
			break;
		}
		case MessageType::LogRecord:
		{
			if (messageFd >= 0)
			{
				std::cerr << "privsep log: unexpected descriptor on log record" << std::endl;
				closeMessageFd ();
				return 1;
			}
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
				// syslog(3) does not neutralise embedded newlines or terminal
				// escapes; a malicious/compromised router thread could forge
				// extra log entries. Apply exactly the file-sink sanitisation.
				const std::string sanitized = SanitizeLogText (text);
				syslog (level == 1	 ? LOG_CRIT
						: level == 2 ? LOG_ERR
						: level == 3 ? LOG_WARNING
						: level == 4 ? LOG_INFO
									 : LOG_DEBUG,
					"%s", sanitized.c_str ());
				break;
			}
			auto line = FormatRecord (level, static_cast<std::time_t> (timestamp), text);
			if (file)
			{
				const std::string record = line + '\n';
				if (fwrite (record.data (), 1, record.size (), file) != record.size () || fflush (file) != 0
					|| ferror (file))
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
			if (messageFd >= 0 || !message.payload.empty ())
			{
				closeMessageFd ();
				std::cerr << "privsep log: invalid ping" << std::endl;
				return 1;
			}
			// Control messages are safe before configuration; this lets
			// a supervisor prove that a freshly configured worker is live.
			Message pong { MessageType::Pong, message.requestId, { } };
			if (!channel.Send (pong)) return 1;
			break;
		}
		case MessageType::Shutdown:
			if (messageFd >= 0 || !message.payload.empty ())
			{
				closeMessageFd ();
				std::cerr << "privsep log: invalid shutdown" << std::endl;
				return 1;
			}
			closeMessageFd ();
			if (config.syslog)
				closelog ();
			else if (file)
			{
				if (fclose (file) != 0)
				{
					std::cerr << "privsep log: final flush failed" << std::endl;
					return 1;
				}
				file = nullptr;
			}
			return 0;
		default:
			closeMessageFd ();
			std::cerr << "privsep log: unexpected message" << std::endl;
			return 1;
		}
		// Any descriptor not explicitly consumed above is a protocol error.
		if (messageFd >= 0)
		{
			closeMessageFd ();
			std::cerr << "privsep log: unconsumed descriptor" << std::endl;
			return 1;
		}
	}
}
}

#endif
