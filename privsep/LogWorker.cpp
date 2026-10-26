/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: LogWorker.
 *
 * Implements LogSink configuration and reattachment plus the log worker: the
 * sink sends configuration (optionally with a pre-opened descriptor) and
 * records, while the worker applies its role sandbox, redacts secrets,
 * sanitises records and writes to one O_NOFOLLOW file or syslog. Control
 * characters are neutralised so a record cannot forge extra physical log lines
 * and the file sink stops appending at a fixed byte budget.
 *
 */

#include "LogWorker.h"
#include "Credentials.h"
#include "Worker.h"
#include "LinuxSandbox.h"
#include "OpenBSDSandbox.h"

#ifndef _WIN32

#include <array>
#include <cerrno>
#include <algorithm>
#include <chrono>
#include <cstddef>
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
namespace
{
	/**
	 * Validate one UTF-8 sequence at @a pos and report its length plus code
	 * point. Returns 0 for invalid input: a bad lead or continuation byte, a
	 * truncated sequence, an overlong form, a surrogate or a value above
	 * U+10FFFF. Never reads past the end of @a text.
	 */
	size_t DecodeUtf8 (std::string_view text, size_t pos, uint32_t & codePoint)
	{
		const auto lead = static_cast<unsigned char> (text[pos]);
		if (lead < 0x80)
		{
			codePoint = lead;
			return 1;
		}
		size_t length = 0;
		uint32_t value = 0;
		if ((lead & 0xe0) == 0xc0)
		{
			length = 2;
			value = lead & 0x1f;
		}
		else if ((lead & 0xf0) == 0xe0)
		{
			length = 3;
			value = lead & 0x0f;
		}
		else if ((lead & 0xf8) == 0xf0)
		{
			length = 4;
			value = lead & 0x07;
		}
		else
		{
			return 0;
		}
		if (pos + length > text.size ()) return 0;
		for (size_t index = 1; index < length; ++index)
		{
			const auto byte = static_cast<unsigned char> (text[pos + index]);
			if ((byte & 0xc0) != 0x80) return 0;
			value = (value << 6) | (byte & 0x3f);
		}
		static constexpr uint32_t MINIMUM[5] = { 0, 0, 0x80, 0x800, 0x10000 };
		if (value < MINIMUM[length] || value > 0x10ffff) return 0;
		if (value >= 0xd800 && value <= 0xdfff) return 0;
		codePoint = value;
		return length;
	}

	/**
	 * Code points that must not reach a file, syslog or a terminal verbatim.
	 * Tab is the only C0 control kept; C1 contains the single-byte CSI (0x9B)
	 * that some terminals still honour inside an otherwise UTF-8 stream.
	 */
	bool IsUnsafeLogCodePoint (uint32_t codePoint)
	{
		if (codePoint == '\t') return false;
		if (codePoint < 0x20) return true;
		if (codePoint == 0x7f) return true;
		return codePoint >= 0x80 && codePoint <= 0x9f;
	}
}

std::string SanitizeLogText (std::string_view text)
{
	// The marker is inside the bound so one sanitised fragment never exceeds
	// MAX_TEXT_SIZE and a truncated record is visibly truncated.
	const std::string_view marker = LOG_TRUNCATION_MARKER;
	const size_t payloadBudget = MAX_TEXT_SIZE > marker.size () ? MAX_TEXT_SIZE - marker.size () : 0;
	const std::string_view bounded = text.substr (0, std::min (text.size (), MAX_TEXT_SIZE));
	bool truncated = bounded.size () < text.size ();
	std::string line;
	line.reserve (std::min (bounded.size (), payloadBudget));
	size_t pos = 0;
	while (pos < bounded.size ())
	{
		uint32_t codePoint = 0;
		const size_t length = DecodeUtf8 (bounded, pos, codePoint);
		if (length == 0)
		{
			// Invalid UTF-8, or a sequence cut by the text bound: emit one
			// replacement byte and never a partial sequence.
			if (line.size () + 1 > payloadBudget)
			{
				truncated = true;
				break;
			}
			line.push_back ('?');
			++pos;
			continue;
		}
		if (IsUnsafeLogCodePoint (codePoint))
		{
			if (line.size () + 1 > payloadBudget)
			{
				truncated = true;
				break;
			}
			line.push_back (' ');
		}
		else
		{
			if (line.size () + length > payloadBudget)
			{
				truncated = true;
				break;
			}
			line.append (bounded.substr (pos, length));
		}
		pos += length;
	}
	if (truncated) line.append (marker);
	return line;
}

namespace
{
	char LowerAscii (char ch)
	{
		return ch >= 'A' && ch <= 'Z' ? static_cast<char> (ch - 'A' + 'a') : ch;
	}

	bool EqualsAsciiIgnoreCase (std::string_view text, std::string_view needle)
	{
		if (text.size () != needle.size ()) return false;
		for (size_t index = 0; index < needle.size (); ++index)
		{
			if (LowerAscii (text[index]) != LowerAscii (needle[index])) return false;
		}
		return true;
	}

	/** A key must start a token: the preceding byte must not be alphanumeric. */
	bool IsKeyBoundary (std::string_view text, size_t pos)
	{
		if (pos == 0) return true;
		const auto previous = static_cast<unsigned char> (text[pos - 1]);
		const bool alphanumeric = (previous >= '0' && previous <= '9') || (previous >= 'a' && previous <= 'z')
			|| (previous >= 'A' && previous <= 'Z');
		return !alphanumeric;
	}

	bool IsValueTerminator (char ch)
	{
		return ch == ' ' || ch == '\t' || ch == ',' || ch == ';' || ch == '}' || ch == ']' || ch == ')';
	}

	/** A quoted value ends after its closing quote; an unquoted one at a separator. */
	size_t FindValueEnd (std::string_view text, size_t start)
	{
		if (start >= text.size ()) return start;
		const char quote = text[start];
		if (quote == '"' || quote == '\'')
		{
			for (size_t index = start + 1; index < text.size (); ++index)
			{
				if (text[index] == quote) return index + 1;
			}
			return text.size ();
		}
		size_t index = start;
		while (index < text.size () && !IsValueTerminator (text[index])) ++index;
		return index;
	}
}

std::string RedactLogSecrets (std::string_view text)
{
	// The whole sensitive-name set is compiled in: no configuration file, no
	// network input and no operator can change what counts as a secret name.
	static constexpr std::array<std::string_view, 6> SENSITIVE_KEYS { "privatekey", "private key", "passphrase",
		"password", "secret", "token" };
	std::string out;
	out.reserve (text.size ());
	size_t redactions = 0;
	size_t pos = 0;
	while (pos < text.size ())
	{
		if (redactions < MAX_LOG_REDACTIONS && IsKeyBoundary (text, pos))
		{
			bool matched = false;
			size_t valueStart = 0;
			for (const auto key : SENSITIVE_KEYS)
			{
				if (text.size () - pos < key.size ()) continue;
				if (!EqualsAsciiIgnoreCase (text.substr (pos, key.size ()), key)) continue;
				size_t cursor = pos + key.size ();
				while (cursor < text.size () && (text[cursor] == ' ' || text[cursor] == '\t')) ++cursor;
				if (cursor >= text.size () || (text[cursor] != '=' && text[cursor] != ':')) continue;
				++cursor;
				while (cursor < text.size () && (text[cursor] == ' ' || text[cursor] == '\t')) ++cursor;
				valueStart = cursor;
				matched = true;
				break;
			}
			if (matched)
			{
				out.append (text.substr (pos, valueStart - pos));
				out.append (LOG_REDACTED_VALUE);
				pos = FindValueEnd (text, valueStart);
				++redactions;
				continue;
			}
		}
		out.push_back (text[pos++]);
	}
	return out;
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

	/**
	 * Bind a pre-opened log descriptor to the configured path. Without this
	 * check the sender could hand over any same-UID writable regular file and
	 * turn the log worker into a write primitive for it. The comparison is
	 * dev/inode identity plus owner and link count, so a replaced path, a
	 * symlink or a hard link is rejected before the sandbox is applied.
	 */
	i2p::expected<void, std::string> ValidateLogDescriptorIdentity (int fd, const std::string & path)
	{
		struct stat descriptor { };
		struct stat named { };
		if (::fstat (fd, &descriptor) != 0)
			return i2p::unexpected ("fstat log descriptor: " + std::string (strerror (errno)));
		if (::lstat (path.c_str (), &named) != 0)
			return i2p::unexpected ("stat configured log path: " + std::string (strerror (errno)));
		if (!S_ISREG (named.st_mode)) return i2p::unexpected ("configured log path is not a regular file");
		if (descriptor.st_dev != named.st_dev || descriptor.st_ino != named.st_ino)
			return i2p::unexpected ("pre-opened descriptor is not the configured log file");
		if (descriptor.st_uid != ::geteuid () || descriptor.st_nlink != 1)
			return i2p::unexpected ("pre-opened log descriptor has an unexpected owner or link count");
		return { };
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

	/**
	 * The single text pipeline: redact first (it can grow the text) and
	 * sanitise last, so the fragment written to the sink is bounded and never
	 * ends inside a multi-byte sequence.
	 */
	std::string PrepareRecordText (std::string_view text)
	{
		return SanitizeLogText (RedactLogSecrets (text));
	}

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
		line += PrepareRecordText (text);
		return line;
	}

	/** Bytes already in the attached sink, or 0 when it cannot be measured. */
	uint64_t CurrentSinkSize (FILE * file)
	{
		struct stat info { };
		const int fd = file ? fileno (file) : -1;
		if (fd < 0 || fstat (fd, &info) != 0 || info.st_size < 0) return 0;
		return static_cast<uint64_t> (info.st_size);
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
	if (!m_Channel || !m_Channel->IsOpen ()) return i2p::unexpected ("log sink channel is not open");
	int logFd = -1;
	if (!useSyslog && !path.empty ())
	{
		logFd = OpenLogFile (path);
		if (logFd < 0) return i2p::unexpected (std::string ("open log file: ") + strerror (errno));
	}
	auto configured = ConfigureChannel (*m_Channel, MessageType::ConfigureLogFd, useSyslog, path, tag, logFd);
	if (logFd >= 0) close (logFd);
	if (!configured)
	{
		if (m_Channel) m_Channel->Close ();
		return configured;
	}
	m_Syslog = useSyslog;
	m_Path.assign (path);
	m_Tag.assign (tag.substr (0, MAX_TEXT_SIZE));
	m_ChannelFailed = false;
	m_Configured = true;
	return { };
}

i2p::expected<void, std::string> LogSink::ConfigureProxy (bool useSyslog, std::string_view path, std::string_view tag)
{
	std::lock_guard<std::mutex> lock (m_Mutex);
	if (m_Configured) return i2p::unexpected ("log sink already configured");
	if (!m_Channel || !m_Channel->IsOpen ()) return i2p::unexpected ("log sink channel is not open");
	auto configured = ConfigureChannel (*m_Channel, MessageType::ConfigureLog, useSyslog, path, tag, -1);
	if (!configured)
	{
		if (m_Channel) m_Channel->Close ();
		return configured;
	}
	m_Syslog = useSyslog;
	m_Path.assign (path);
	m_Tag.assign (tag.substr (0, MAX_TEXT_SIZE));
	m_ChannelFailed = false;
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
	if (m_Channel) m_Channel->Close ();
	m_Channel = std::make_shared<Channel> (std::move (channel));
	m_ChannelFailed = false;
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
	if (m_Channel) m_Channel->Close ();
	m_Channel = std::make_shared<Channel> (std::move (channel));
	m_ChannelFailed = false;
	return { };
}

void LogSink::CountDroppedLocked (bool workerUnavailable)
{
	++m_Dropped;
	if (workerUnavailable) ++m_WorkerDropped;
}

void LogSink::Write (uint8_t level, std::time_t timestamp, std::string_view text)
{
	if (!IsLevel (level)) return;
	std::shared_ptr<Channel> channel;
	{
		std::lock_guard<std::mutex> lock (m_Mutex);
		if (m_Shutdown) return;
		channel = m_Channel;
		if (m_ChannelFailed || !channel || !channel->IsOpen ())
		{
			CountDroppedLocked (true);
			return;
		}
	}
	Message message { MessageType::LogRecord, 0, EncodeLogRecord (level, static_cast<int64_t> (timestamp), text) };
	// The send is deliberately outside m_Mutex: a wedged worker must cost one
	// bounded slow record, never the router's logger thread and every other
	// sink reader for up to the send timeout.
	const auto sent = channel->Send (message, 1000);
	if (sent) return;
	std::lock_guard<std::mutex> lock (m_Mutex);
	CountDroppedLocked (true); // drop logs; never fall back to a local file
	if (m_Channel == channel)
	{
		// Detach without closing: a send still in flight on another thread keeps
		// the object, and therefore its descriptor number, alive until it
		// returns. Closing here could hand the number to an unrelated open()
		// underneath that send.
		m_ChannelFailed = true;
		m_Channel.reset ();
	}
}

uint64_t LogSink::DroppedCount () const
{
	std::lock_guard<std::mutex> lock (m_Mutex);
	return m_Dropped;
}

uint64_t LogSink::GetWorkerDroppedCount () const
{
	std::lock_guard<std::mutex> lock (m_Mutex);
	return m_WorkerDropped;
}

i2p::expected<void, std::string> LogSink::Shutdown ()
{
	std::lock_guard<std::mutex> lock (m_Mutex);
	m_Shutdown = true;
	if (!m_Channel || !m_Channel->IsOpen ()) return { };
	Message message { MessageType::Shutdown, 0, { } };
	auto result = m_Channel->Send (message, 1000);
	m_Channel->Close ();
	m_Channel.reset ();
	return result;
}

bool LogSink::IsOpen () const
{
	std::lock_guard<std::mutex> lock (m_Mutex);
	return !m_Shutdown && !m_ChannelFailed && m_Channel && m_Channel->IsOpen ();
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
	channel.SetEnforcementPolicy (EnforcementPolicy::Endpoint);
#if defined(__linux__)
	// Restrict the log role before it reads its configuration frame.
	if (auto lockdown = ApplyLinuxIpcLockdown (GetLinuxSandboxProfile (WorkerClass::Log)); !lockdown)
		return 1;
#endif
	auto peer = channel.PeerUid ();
	if (!peer || !IsAllowedParentUid (*peer))
	{
		std::cerr << "privsep log: rejecting peer with unexpected uid" << std::endl;
		return 1;
	}

	LogConfig config;
	FILE * file = nullptr;
	// Bytes already appended to the file sink. The budget is a compile-time
	// policy: the configuration frame has no spare field and adding one would
	// change the wire format for every existing worker.
	uint64_t sinkBytes = 0;
	bool sinkCapped = false;
	unsigned violations = 0;
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
			if (++violations > Channel::MAX_PROTOCOL_VIOLATIONS) return 1;
			if (!channel.Send ({ MessageType::Error, message.requestId,
					   EncodeError (message.type, ErrorCode::NotAuthorized, "operation is not allowed on the log endpoint") },
					500))
				return 1;
			continue;
		}
		switch (message.type)
		{
		case MessageType::ConfigureLog:
		case MessageType::ConfigureLogFd:
		{
			if (config.configured)
			{
				// A second configure is a bounded refusal, not a teardown:
				// the original sink stays valid for the peer that owns it.
				closeMessageFd ();
				if (++violations > Channel::MAX_PROTOCOL_VIOLATIONS) return 1;
				if (!channel.Send ({ MessageType::Error, message.requestId,
						   EncodeError (message.type, ErrorCode::BadField, "duplicate log configuration") },
						500))
					return 1;
				continue;
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
					auto identity = ValidateLogDescriptorIdentity (messageFd, path);
					if (!identity)
					{
						std::cerr << "privsep log: " << identity.error () << std::endl;
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
				sinkBytes = CurrentSinkSize (file);
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
				// extra log entries. Apply exactly the file-sink pipeline
				// (redaction and sanitisation) so both sinks agree.
				const std::string sanitized = PrepareRecordText (text);
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
				if (!FitsLogSinkBudget (sinkBytes, record.size ()))
				{
					// The sink reached its byte budget. Stop appending and say so
					// once, on stderr: the worker keeps draining records so the
					// channel never wedges, and nothing rotates, truncates or
					// rewrites the existing content behind the operator's back.
					if (!sinkCapped)
					{
						sinkCapped = true;
						std::cerr << "privsep log: log sink byte budget reached; further records are discarded"
								  << std::endl;
					}
					break;
				}
				if (fwrite (record.data (), 1, record.size (), file) != record.size () || fflush (file) != 0
					|| ferror (file))
				{
					// Disk full/EIO must be observable to the parent's
					// restart policy, not silently dropped forever.
					std::cerr << "privsep log: write failed" << std::endl;
					return 1;
				}
				sinkBytes += record.size ();
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
