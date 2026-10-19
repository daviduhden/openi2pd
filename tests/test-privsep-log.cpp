/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test privsep log.
 *
 * Exercises the isolated log worker: bounded text sanitization, descriptor-based configuration with exactly one regular file, malformed records, level bounds and bounded child shutdown.
 *
 */

#include <cassert>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#ifdef __linux__
#include <dirent.h>
#include <sys/syscall.h>
#endif

#include "privsep/Channel.h"
#include "privsep/LogWorker.h"
#include "privsep/Protocol.h"
#include "privsep/Worker.h"

using namespace i2p::privsep;

namespace
{
bool WaitForExit (pid_t pid, int & status, int timeoutMs)
{
	const auto deadline = std::chrono::steady_clock::now () + std::chrono::milliseconds (timeoutMs);
	while (std::chrono::steady_clock::now () < deadline)
	{
		const pid_t result = waitpid (pid, &status, WNOHANG);
		if (result == pid) return true;
		if (result < 0 && errno == EINTR) continue;
		if (result < 0) return false;
		std::this_thread::sleep_for (std::chrono::milliseconds (10));
	}
	kill (pid, SIGKILL);
	waitpid (pid, &status, 0);
	return false;
}

/**
	 * Fork one worker, run the parent-side exchange and wait for exit. The
	 * worker is a plain fork of the test process so the credential check is
	 * exercised for the current uid (cross-uid rejection needs root).
	 */
template <typename Exchange> int RunScenario (Exchange exchange, int expectedExit)
{
	auto pair = Channel::CreatePair ();
	assert (pair.has_value ());
	pid_t pid = fork ();
	assert (pid >= 0);
	if (pid == 0)
	{
		pair->first.Close ();
		// _exit avoids running the pair destructor after RunLogWorker has
		// already closed the adopted descriptor.
		_exit (RunLogWorker (pair->second.Fd ()));
	}
	pair->second.Close ();
	exchange (pair->first);
	pair->first.Close ();

	int status = 0;
	assert (WaitForExit (pid, status, 5000));
	assert (WIFEXITED (status));
	if (expectedExit == 0)
		assert (WEXITSTATUS (status) == 0);
	else
		assert (WEXITSTATUS (status) != 0);
	return WEXITSTATUS (status);
}

std::string ReadFile (const std::string & path)
{
	std::ifstream file (path);
	return std::string (std::istreambuf_iterator<char> (file), std::istreambuf_iterator<char> ());
}

int CountChar (const std::string & text, char needle)
{
	int count = 0;
	for (char ch : text)
		if (ch == needle) ++count;
	return count;
}

bool WaitForFile (const std::string & path, int timeoutMs)
{
	const auto deadline = std::chrono::steady_clock::now () + std::chrono::milliseconds (timeoutMs);
	while (std::chrono::steady_clock::now () < deadline)
	{
		struct stat info { };
		if (stat (path.c_str (), &info) == 0) return true;
		std::this_thread::sleep_for (std::chrono::milliseconds (10));
	}
	return false;
}

bool WaitForContent (const std::string & path, const std::string & needle, int timeoutMs)
{
	const auto deadline = std::chrono::steady_clock::now () + std::chrono::milliseconds (timeoutMs);
	while (std::chrono::steady_clock::now () < deadline)
	{
		if (ReadFile (path).find (needle) != std::string::npos) return true;
		std::this_thread::sleep_for (std::chrono::milliseconds (10));
	}
	return false;
}

int OpenLogDescriptor (const std::string & path)
{
	const int fd = open (path.c_str (), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
	assert (fd >= 0);
	return fd;
}

#ifdef __linux__
/**
 * Count descriptors owned by this process. The directory descriptor used for
 * the scan is included in every measurement. Returns -1 without /proc so a
 * non-procfs Linux build skips the comparison instead of failing.
 */
int CountOpenFds ()
{
	DIR * dir = opendir ("/proc/self/fd");
	if (!dir) return -1;
	int count = 0;
	while (struct dirent * entry = readdir (dir))
	{
		if (entry->d_name[0] != '.') ++count;
	}
	closedir (dir);
	return count;
}
#endif

void TestLogTextSanitization ()
{
	std::string text = "line1\nline2\r";
	text.push_back ('\x1b');
	text += "\ttab";
	text.push_back ('\0');
	text += "nul";

	const std::string sanitized = SanitizeLogText (text);
	assert (sanitized.size () == text.size ());
	assert (sanitized.find ('\n') == std::string::npos);
	assert (sanitized.find ('\r') == std::string::npos);
	assert (sanitized.find ('\x1b') == std::string::npos);
	assert (sanitized.find ('\0') == std::string::npos);
	assert (sanitized.find ('\t') != std::string::npos);

	// The same bound is applied before the text reaches a file or syslog.
	const std::string oversized (MAX_TEXT_SIZE * 2, 'a');
	assert (SanitizeLogText (oversized).size () == MAX_TEXT_SIZE);
}

void TestBoundedChildStop ()
{
	// A child that ignores SIGTERM must not make the production wait helper
	// perform an unbounded waitpid. The helper is header-inline so this test
	// exercises the exact logic ChildProcess::WaitForExit uses without
	// linking the daemon-only Worker.cpp.
	{
		pid_t pid = fork ();
		assert (pid >= 0);
		if (pid == 0)
		{
			std::signal (SIGTERM, SIG_IGN);
			for (;;) pause ();
		}

		const auto started = std::chrono::steady_clock::now ();
		int pidFd = -1;
#if defined(__linux__) && defined(SYS_pidfd_open)
		pidFd = static_cast<int> (syscall (SYS_pidfd_open, pid, 0));
#endif
		assert (!WaitForChildExitBounded (pid, pidFd, 100));
		const auto elapsed = std::chrono::steady_clock::now () - started;
		assert (elapsed < std::chrono::seconds (3));

		kill (pid, SIGKILL);
		assert (WaitForChildExitBounded (pid, pidFd, 2000));
		if (pidFd >= 0) close (pidFd);
		assert (kill (pid, 0) == -1);
		assert (errno == ESRCH);
	}

	// The fast path must reap an already-exited child without sleeping for
	// the full timeout.
	{
		pid_t pid = fork ();
		assert (pid >= 0);
		if (pid == 0) _exit (0);

		const auto started = std::chrono::steady_clock::now ();
		assert (WaitForChildExitBounded (pid, -1, 2000));
		const auto elapsed = std::chrono::steady_clock::now () - started;
		assert (elapsed < std::chrono::seconds (2));
		assert (kill (pid, 0) == -1);
		assert (errno == ESRCH);
	}
}
}

int main ()
{
	char dirTemplate[] = "/tmp/i2pd-privsep-log-XXXXXX";
	char * dir = mkdtemp (dirTemplate);
	assert (dir);

	// Baseline: configure, record, clean shutdown, file mode 0600.
	{
		const std::string path = std::string (dir) + "/router.log";
		RunScenario (
			[&path] (Channel & channel)
			{
				assert (channel.Send (Message { MessageType::ConfigureLog, 1, EncodeLogConfig (false, path, "test") })
						.has_value ());
				assert (channel
						.Send (Message { MessageType::LogRecord, 2, EncodeLogRecord (4, 12345, "hello from worker") })
						.has_value ());
				assert (channel.Send (Message { MessageType::Shutdown, 3, { } }).has_value ());
			},
			0);

		const std::string contents = ReadFile (path);
		assert (contents.find ("hello from worker") != std::string::npos);
		assert (CountChar (contents, '\n') == 1);
		struct stat info { };
		assert (stat (path.c_str (), &info) == 0);
		assert ((info.st_mode & 0777) == 0600);
		unlink (path.c_str ());
	}

	// ConfigureLogFd is the production path used by LogSink: the parent opens
	// the sink and transfers exactly one descriptor. The worker must adopt it,
	// and the parent must keep (and close) its own copy.
	{
		const std::string path = std::string (dir) + "/preopened.log";
		RunScenario (
			[&path] (Channel & channel)
			{
				const int fd = OpenLogDescriptor (path);
				assert (channel
						.SendWithFd (
							Message { MessageType::ConfigureLogFd, 1, EncodeLogConfig (false, path, "test") }, fd)
						.has_value ());
				// SCM_RIGHTS duplicates the descriptor; sending does not move
				// ownership away from the parent.
				assert (fcntl (fd, F_GETFD) >= 0);
				close (fd);
				assert (channel.Send (Message { MessageType::LogRecord, 2, EncodeLogRecord (4, 1, "preopened record") })
						.has_value ());
				assert (channel.Send (Message { MessageType::Shutdown, 3, { } }).has_value ());
			},
			0);

		const std::string contents = ReadFile (path);
		assert (contents.find ("preopened record") != std::string::npos);
		struct stat info { };
		assert (stat (path.c_str (), &info) == 0);
		assert ((info.st_mode & 0777) == 0600);
		unlink (path.c_str ());
	}

	// A record before configuration is a protocol error.
	{
		const std::string path = std::string (dir) + "/early.log";
		RunScenario (
			[&path] (Channel & channel)
			{
				assert (channel.Send (Message { MessageType::LogRecord, 1, EncodeLogRecord (4, 1, "too early") })
						.has_value ());
			},
			1);
		unlink (path.c_str ());
	}

	// Configuration is accepted exactly once.
	{
		const std::string path = std::string (dir) + "/duplicate.log";
		RunScenario (
			[&path] (Channel & channel)
			{
				const Message configure { MessageType::ConfigureLog, 1, EncodeLogConfig (false, path, "test") };
				assert (channel.Send (configure).has_value ());
				// Wait until the worker has opened the file, which proves the
				// first configuration was processed before the duplicate is sent.
				assert (WaitForFile (path, 1000));
				assert (channel.Send (configure).has_value ());
			},
			1);
		unlink (path.c_str ());
	}

	// Malformed record payloads are rejected.
	{
		const std::string path = std::string (dir) + "/malformed.log";
		RunScenario (
			[&path] (Channel & channel)
			{
				assert (channel.Send (Message { MessageType::ConfigureLog, 1, EncodeLogConfig (false, path, "test") })
						.has_value ());
				PayloadWriter writer;
				writer.WriteU8 (4); // level only: missing timestamp and text
				assert (channel.Send (Message { MessageType::LogRecord, 2, writer.Data () }).has_value ());
			},
			1);
		unlink (path.c_str ());
	}

	// Descriptor policy: ConfigureLogFd requires exactly one regular-file
	// descriptor for a file sink, legacy ConfigureLog must not carry one, and
	// a non-file sink must not carry one either. Every rejected descriptor is
	// closed by the worker before it exits. The post-switch "unconsumed
	// descriptor" guard in RunLogWorker is defensive: every current message
	// type either consumes or rejects its descriptor, so these cases exercise
	// the per-type checks rather than the fallback.
	{
		// 1. Descriptor mode without a descriptor.
		const std::string path = std::string (dir) + "/missing-fd.log";
		RunScenario (
			[&path] (Channel & channel)
			{
				assert (channel.Send (Message { MessageType::ConfigureLogFd, 1, EncodeLogConfig (false, path, "test") })
						.has_value ());
			},
			1);
		unlink (path.c_str ());
	}
	{
		// 2. Legacy path mode with a descriptor.
		const std::string path = std::string (dir) + "/legacy-fd.log";
		RunScenario (
			[&path] (Channel & channel)
			{
				const int fd = OpenLogDescriptor (path);
				assert (channel
						.SendWithFd (
							Message { MessageType::ConfigureLog, 1, EncodeLogConfig (false, path, "test") }, fd)
						.has_value ());
				close (fd);
			},
			1);
		unlink (path.c_str ());
	}
	{
		// 3. Descriptor mode with a non-regular descriptor: fstat must reject
		// a character device before it is adopted as the log stream.
		const std::string path = std::string (dir) + "/char-device.log";
		RunScenario (
			[&path] (Channel & channel)
			{
				const int fd = open ("/dev/null", O_RDWR | O_CLOEXEC);
				assert (fd >= 0);
				assert (channel
						.SendWithFd (
							Message { MessageType::ConfigureLogFd, 1, EncodeLogConfig (false, path, "test") }, fd)
						.has_value ());
				close (fd);
			},
			1);
		unlink (path.c_str ());
	}
	{
		// 4. Descriptor mode for a non-file sink (syslog) must reject the
		// descriptor instead of silently ignoring it.
		RunScenario (
			[] (Channel & channel)
			{
				const int fd = open ("/dev/null", O_RDWR | O_CLOEXEC);
				assert (fd >= 0);
				assert (channel
						.SendWithFd (Message { MessageType::ConfigureLogFd, 1, EncodeLogConfig (true, "", "test") }, fd)
						.has_value ());
				close (fd);
			},
			1);
	}

	// Out-of-range levels are rejected by the worker.
	{
		const std::string path = std::string (dir) + "/level.log";
		RunScenario (
			[&path] (Channel & channel)
			{
				assert (channel.Send (Message { MessageType::ConfigureLog, 1, EncodeLogConfig (false, path, "test") })
						.has_value ());
				PayloadWriter writer;
				writer.WriteU8 (6);
				writer.WriteU64BE (1);
				writer.WriteString ("bad level");
				assert (channel.Send (Message { MessageType::LogRecord, 2, writer.Data () }).has_value ());
			},
			1);
		unlink (path.c_str ());
	}

	// A descriptor attached to a log record is a protocol violation: records
	// carry no descriptors, and the worker must close and reject it.
	{
		const std::string path = std::string (dir) + "/record-fd.log";
		RunScenario (
			[&path] (Channel & channel)
			{
				assert (channel.Send (Message { MessageType::ConfigureLog, 1, EncodeLogConfig (false, path, "test") })
						.has_value ());
				assert (WaitForFile (path, 1000));
				const int fd = open ("/dev/null", O_RDONLY | O_CLOEXEC);
				assert (fd >= 0);
				assert (channel
						.SendWithFd (Message { MessageType::LogRecord, 2, EncodeLogRecord (4, 1, "fd on record") }, fd)
						.has_value ());
				close (fd);
			},
			1);
		// The rejected record must not have been written.
		assert (ReadFile (path).find ("fd on record") == std::string::npos);
		unlink (path.c_str ());
	}

	// Oversized text is truncated at the protocol bound; control characters
	// (including newline) are flattened so one record cannot inject lines.
	{
		const std::string path = std::string (dir) + "/oversized.log";
		RunScenario (
			[&path] (Channel & channel)
			{
				assert (channel.Send (Message { MessageType::ConfigureLog, 1, EncodeLogConfig (false, path, "test") })
						.has_value ());
				std::string text (MAX_TEXT_SIZE * 2, 'a');
				text[10] = '\n';
				text[11] = '\x1b';
				text += "TRAILING-MARKER";
				assert (
					channel.Send (Message { MessageType::LogRecord, 2, EncodeLogRecord (4, 1, text) }).has_value ());
				assert (channel.Send (Message { MessageType::Shutdown, 3, { } }).has_value ());
			},
			0);

		const std::string contents = ReadFile (path);
		assert (CountChar (contents, '\n') == 1); // only the record terminator
		assert (contents.find ("TRAILING-MARKER") == std::string::npos);
		assert (contents.find ('\x1b') == std::string::npos);
		assert (contents.size () < MAX_TEXT_SIZE + 64);
		unlink (path.c_str ());
	}

	// A NUL byte in the configured path is rejected before it can truncate a
	// C path argument in the worker.
	{
		RunScenario (
			[] (Channel & channel)
			{
				PayloadWriter writer;
				writer.WriteU8 (0);
				writer.WriteString (std::string ("/tmp/bad\0name", 13));
				writer.WriteString ("test");
				assert (channel.Send (Message { MessageType::ConfigureLog, 1, writer.Data () }).has_value ());
			},
			1);
	}

	// Control messages are safe before configuration: Ping gets a Pong and
	// Shutdown before Configure is a clean exit rather than a protocol error.
	{
		RunScenario (
			[] (Channel & channel)
			{
				assert (channel.Send (Message { MessageType::Ping, 77, { } }).has_value ());
				auto pong = channel.Receive (1000);
				assert (pong.has_value ());
				assert (pong->has_value ());
				assert ((*pong)->type == MessageType::Pong);
				assert ((*pong)->requestId == 77);
				assert (channel.Send (Message { MessageType::Shutdown, 78, { } }).has_value ());
			},
			0);
	}

	// A control message that arrives with an unexpected descriptor must still
	// consume and close it, answer the probe and keep the channel usable.
	{
		RunScenario (
			[] (Channel & channel)
			{
				const int fd = open ("/dev/null", O_RDONLY | O_CLOEXEC);
				assert (fd >= 0);
				assert (channel.SendWithFd (Message { MessageType::Ping, 201, { } }, fd).has_value ());
				close (fd);
				auto pong = channel.Receive (1000);
				assert (pong.has_value ());
				assert (pong->has_value ());
				assert ((*pong)->type == MessageType::Pong);
				assert ((*pong)->requestId == 201);
				assert (channel.Send (Message { MessageType::Shutdown, 202, { } }).has_value ());
			},
			0);
	}

	// Reattach: the sink keeps its configuration and can replace a dead
	// worker's channel without constructing a second sink object. This is the
	// primitive the bounded restart monitor uses.
	{
		const std::string path = std::string (dir) + "/reattach.log";
		auto first = Channel::CreatePair ();
		assert (first.has_value ());
		pid_t firstPid = fork ();
		assert (firstPid >= 0);
		if (firstPid == 0)
		{
			first->first.Close ();
			_exit (RunLogWorker (first->second.Fd ()));
		}
		first->second.Close ();

		{
#ifdef __linux__
			// Configure() opens the log file, transfers a duplicate and must
			// close its own copy. A leak or a double close changes the count.
			const int descriptorsBeforeConfigure = CountOpenFds ();
#endif
			LogSink sink (std::move (first->first));
			assert (sink.Configure (false, path, "test").has_value ());
#ifdef __linux__
			if (descriptorsBeforeConfigure >= 0) assert (CountOpenFds () == descriptorsBeforeConfigure);
#endif
			assert (!sink.Configure (false, path, "test").has_value ()); // once
			sink.Write (4, 1, "first worker");
			assert (WaitForContent (path, "first worker", 1000));

			// Simulate an unexpected worker death; the sink write must close
			// the channel rather than falling back to a local file.
			kill (firstPid, SIGKILL);
			int firstStatus = 0;
			assert (WaitForExit (firstPid, firstStatus, 5000));
			sink.Write (4, 2, "lost record");
			assert (!sink.IsOpen ());

			auto second = Channel::CreatePair ();
			assert (second.has_value ());
			pid_t secondPid = fork ();
			assert (secondPid >= 0);
			if (secondPid == 0)
			{
				second->first.Close ();
				_exit (RunLogWorker (second->second.Fd ()));
			}
			second->second.Close ();
			assert (sink.Reattach (std::move (second->first)).has_value ());
			assert (sink.IsOpen ());
			sink.Write (4, 3, "second worker");
			assert (sink.Shutdown ().has_value ());

			int secondStatus = 0;
			assert (WaitForExit (secondPid, secondStatus, 5000));
			assert (WIFEXITED (secondStatus));
			assert (WEXITSTATUS (secondStatus) == 0);
		}

		const std::string contents = ReadFile (path);
		assert (contents.find ("first worker") != std::string::npos);
		assert (contents.find ("second worker") != std::string::npos);
		assert (contents.find ("lost record") == std::string::npos);
		unlink (path.c_str ());
	}

	// Peer credentials are readable and match the worker's expected uid.
	{
		auto pair = Channel::CreatePair ();
		assert (pair.has_value ());
		auto uid = pair->first.PeerUid ();
		assert (uid.has_value ());
		assert (*uid == geteuid ());
	}

	TestLogTextSanitization ();
	TestBoundedChildStop ();

	rmdir (dir);
	return 0;
}
