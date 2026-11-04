/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test log lifecycle.
 *
 * Exercises logger start/stop/reopen, sink replacement and destruction
 * while the writer thread runs, requiring records to be drained and the
 * output file to close before removal.
 *
 */

#include <atomic>
#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#else
#define NOMINMAX
#include <aclapi.h>
#include <windows.h>
#endif

#include "Log.h"
#include "Modern.h"

namespace
{
void Append (i2p::log::Log & log, const std::string & text, LogLevel level = eLogInfo)
{
	auto msg = std::make_shared<i2p::log::LogMsg> (level, 1234567890, std::string (text));
	log.Append (msg);
}

size_t CountLines (const std::string & text)
{
	size_t lines = 0;
	for (char ch : text)
		if (ch == '\n') ++lines;
	return lines;
}

std::string ReadFile (const std::filesystem::path & path)
{
	std::ifstream file (path);
	return std::string (std::istreambuf_iterator<char> (file), std::istreambuf_iterator<char> ());
}

std::filesystem::path UniquePath (std::string_view prefix)
{
	const auto unique = std::chrono::steady_clock::now ().time_since_epoch ().count ()
		^ static_cast<long long> (std::random_device { }());
	return std::filesystem::temp_directory_path () / (std::string (prefix) + std::to_string (unique) + ".log");
}

#ifdef _WIN32
bool HasPrivateWindowsDacl (const std::filesystem::path & path)
{
	HANDLE handle = CreateFileW (path.c_str (), READ_CONTROL, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
	if (handle == INVALID_HANDLE_VALUE) return false;
	const auto closeHandle = i2p::modern::ScopeExit ([&] { CloseHandle (handle); });

	PACL dacl = nullptr;
	PSECURITY_DESCRIPTOR descriptor = nullptr;
	if (GetSecurityInfo (handle, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &dacl, nullptr,
			&descriptor)
		!= ERROR_SUCCESS)
		return false;
	const auto freeDescriptor = i2p::modern::ScopeExit ([&] { LocalFree (descriptor); });
	SECURITY_DESCRIPTOR_CONTROL control = 0;
	DWORD revision = 0;
	if (!GetSecurityDescriptorControl (descriptor, &control, &revision) || !(control & SE_DACL_PROTECTED) || !dacl)
		return false;
	ACL_SIZE_INFORMATION aclInfo { };
	if (!GetAclInformation (dacl, &aclInfo, sizeof (aclInfo), AclSizeInformation) || aclInfo.AceCount != 2) return false;

	HANDLE token = nullptr;
	if (!OpenProcessToken (GetCurrentProcess (), TOKEN_QUERY, &token)) return false;
	const auto closeToken = i2p::modern::ScopeExit ([&] { CloseHandle (token); });
	DWORD tokenSize = 0;
	GetTokenInformation (token, TokenUser, nullptr, 0, &tokenSize);
	if (GetLastError () != ERROR_INSUFFICIENT_BUFFER || !tokenSize) return false;
	std::vector<BYTE> tokenBuffer (tokenSize);
	if (!GetTokenInformation (token, TokenUser, tokenBuffer.data (), tokenSize, &tokenSize)) return false;
	const PSID userSid = reinterpret_cast<TOKEN_USER *> (tokenBuffer.data ())->User.Sid;
	std::array<BYTE, SECURITY_MAX_SID_SIZE> systemBuffer { };
	DWORD systemSize = static_cast<DWORD> (systemBuffer.size ());
	if (!CreateWellKnownSid (WinLocalSystemSid, nullptr, systemBuffer.data (), &systemSize)) return false;
	bool hasUser = false, hasSystem = false;
	for (DWORD index = 0; index < aclInfo.AceCount; ++index)
	{
		LPVOID rawAce = nullptr;
		if (!GetAce (dacl, index, &rawAce)) return false;
		const auto * ace = static_cast<const ACCESS_ALLOWED_ACE *> (rawAce);
		if (ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE || ace->Mask != FILE_ALL_ACCESS) return false;
		const PSID sid = const_cast<DWORD *> (&ace->SidStart);
		if (EqualSid (sid, userSid))
			hasUser = true;
		else if (EqualSid (sid, systemBuffer.data ()))
			hasSystem = true;
		else
			return false;
	}
	return hasUser && hasSystem;
}
#endif
}

int main ()
{
	// Repeated Start()/Stop() must not replace a joinable thread or lose
	// the records queued in each cycle.
	{
		auto out = std::make_shared<std::ostringstream> ();
		i2p::log::Log log;
		log.SendTo (out);
		assert (log.GetLogType () == eLogStream);

		for (int round = 0; round < 3; ++round)
		{
			log.Start ();
			log.Start (); // idempotent while already running
			Append (log, "round-" + std::to_string (round));
			log.Stop ();
			log.Stop (); // idempotent after the writer has been joined
		}

		const std::string contents = out->str ();
		assert (CountLines (contents) == 3);
		assert (contents.find ("round-0") != std::string::npos);
		assert (contents.find ("round-2") != std::string::npos);
	}

	// Failed initialization resets a stopped logger to stdout and releases
	// the configured stream. The operation must refuse to replace a live
	// destination.
	{
		i2p::log::Log log;
		auto out = std::make_shared<std::ostringstream> ();
		std::weak_ptr<std::ostringstream> weakOut = out;
		log.SendTo (out);
		out.reset ();
		assert (!weakOut.expired ());
		log.Start ();
		const bool resetWhileRunning = log.ResetDestination ();
		assert (!resetWhileRunning);
		log.Stop ();
		const bool resetStopped = log.ResetDestination ();
		assert (resetStopped);
		assert (log.GetLogType () == eLogStdout);
		assert (weakOut.expired ());
	}

	// Reset also closes a file destination before the logger object is
	// destroyed.
	{
		const auto unique = std::chrono::steady_clock::now ().time_since_epoch ().count ()
			^ static_cast<long long> (std::random_device { }());
		const auto path
			= std::filesystem::temp_directory_path () / ("openi2pd-log-reset-" + std::to_string (unique) + ".log");
		i2p::log::Log log;
		log.SendTo (path.string ());
		assert (log.GetLogType () == eLogFile);
		const bool reset = log.ResetDestination ();
		assert (reset);
		assert (log.GetLogType () == eLogStdout);
		std::error_code error;
		const bool removed = std::filesystem::remove (path, error);
		assert (removed && !error);
	}

	// A path whose final component is a symlink must be rejected without
	// replacing the target or installing the linked file as the log sink.
	{
		const auto target = UniquePath ("openi2pd-log-link-target-");
		const auto link = UniquePath ("openi2pd-log-link-");
		{
			std::ofstream output (target);
			output << "preserve-target\n";
		}
		std::error_code error;
		std::filesystem::create_symlink (target, link, error);
#ifndef _WIN32
		assert (!error);
#endif
		if (!error)
		{
			i2p::log::Log log;
			log.SendTo (link.string ());
			assert (log.GetLogType () == eLogStdout);
			assert (ReadFile (target) == "preserve-target\n");
		}
		std::filesystem::remove (link, error);
		std::filesystem::remove (target, error);
	}

#ifndef _WIN32
	// A symlink in a parent component must also be rejected; no directory
	// traversal step may resolve through a redirected path.
	{
		const auto directory = UniquePath ("openi2pd-log-parent-target-");
		const auto link = UniquePath ("openi2pd-log-parent-link-");
		std::error_code error;
		std::filesystem::create_directory (directory, error);
		assert (!error);
		std::filesystem::create_directory_symlink (directory, link, error);
		assert (!error);
		i2p::log::Log log;
		log.SendTo ((link / "redirected.log").string ());
		assert (log.GetLogType () == eLogStdout);
		assert (!std::filesystem::exists (directory / "redirected.log"));
		std::filesystem::remove (link, error);
		std::filesystem::remove (directory, error);
	}
#endif

	// Hard-linked files are rejected so a log path cannot mutate another
	// pathname through the same inode.
	{
		const auto target = UniquePath ("openi2pd-log-hardlink-target-");
		const auto link = UniquePath ("openi2pd-log-hardlink-");
		{
			std::ofstream output (target);
			output << "preserve-hardlink\n";
		}
		std::error_code error;
		std::filesystem::create_hard_link (target, link, error);
		assert (!error);
		i2p::log::Log log;
		log.SendTo (link.string ());
		assert (log.GetLogType () == eLogStdout);
		assert (ReadFile (target) == "preserve-hardlink\n");
		std::filesystem::remove (link, error);
		std::filesystem::remove (target, error);
	}

	// The opened descriptor, not a pathname re-open, is tightened to private
	// mode for both newly created and pre-existing files.
	{
		const auto newPath = UniquePath ("openi2pd-log-private-new-");
		{
			i2p::log::Log log;
			log.SendTo (newPath.string ());
#ifdef _WIN32
			assert (HasPrivateWindowsDacl (newPath));
#else
			struct stat created { };
			assert (stat (newPath.c_str (), &created) == 0);
			assert ((created.st_mode & 0777) == 0600);
#endif
		}
		std::error_code error;
		std::filesystem::remove (newPath, error);

		const auto path = UniquePath ("openi2pd-log-private-");
		{
			std::ofstream output (path);
			output << "existing\n";
		}
#ifndef _WIN32
		assert (chmod (path.c_str (), 0644) == 0);
#endif
		i2p::log::Log log;
		log.SendTo (path.string ());
#ifndef _WIN32
		struct stat info { };
		assert (stat (path.c_str (), &info) == 0);
		assert ((info.st_mode & 0777) == 0600);
		log.Reopen ();
		assert (stat (path.c_str (), &info) == 0);
		assert ((info.st_mode & 0777) == 0600);
#else
		assert (HasPrivateWindowsDacl (path));
#endif
		std::filesystem::remove (path, error);
	}

	// Sink installation and replacement are observed by the writer thread
	// without leaving a callback pointing at a replaced object.
	{
		i2p::log::Log log;
		std::atomic<int> sinkCount { 0 };
		std::mutex sinkMutex;
		std::string lastText;
		log.SetSink (
			[&] (LogLevel, std::time_t, std::string_view text)
			{
				++sinkCount;
				std::lock_guard<std::mutex> l (sinkMutex);
				lastText.assign (text);
			});

		log.Start ();
		Append (log, "sink-one");
		Append (log, "sink-two");
		log.Stop ();
		assert (sinkCount.load () == 2);
		assert (lastText == "sink-two");

		log.SetSink (nullptr);
		auto out = std::make_shared<std::ostringstream> ();
		log.SendTo (out);
		log.Start ();
		Append (log, "local-after-sink");
		log.Stop ();
		assert (out->str ().find ("local-after-sink") != std::string::npos);
	}

	// Each fresh writer must wait for its own publication even when the
	// previous run's start signal was true. No sleep authorizes the next
	// generation, and each destination must receive its own final record.
	{
		i2p::log::Log log;
		for (int generation = 0; generation < 8; ++generation)
		{
			auto out = std::make_shared<std::ostringstream> ();
			log.SendTo (out);
			log.Start ();
			const auto record = "generation-" + std::to_string (generation);
			Append (log, record);
			log.Stop ();
			assert (CountLines (out->str ()) == 1);
			assert (out->str ().find (record) != std::string::npos);
		}
	}

	// A logger destroyed without an explicit Stop() must drain, flush and
	// join instead of terminating on a joinable thread.
	{
		auto out = std::make_shared<std::ostringstream> ();
		{
			i2p::log::Log log;
			log.SendTo (out);
			log.Start ();
			Append (log, "destructor-drain");
		}
		assert (out->str ().find ("destructor-drain") != std::string::npos);
	}

	// An unwritable/stalled sink must not grow the queue without bound.
	// The writer is intentionally not started, so every accepted record
	// stays in the queue and records beyond the fixed quota are counted
	// and dropped.
	{
		i2p::log::Log log;
		for (size_t i = 0; i < i2p::log::LOG_QUEUE_MAX_RECORDS; ++i) Append (log, "queued-" + std::to_string (i));
		assert (log.GetDroppedCount () == 0);
		for (int i = 0; i < 5; ++i) Append (log, "dropped-" + std::to_string (i));
		assert (log.GetDroppedCount () == 5);
	}

	// Reopen() is called from the signal-handling thread while the writer
	// is active. Every record must reach the same file and no stream swap
	// may deadlock against the writer.
	{
		const auto path = UniquePath ("openi2pd-log-lifecycle-");

		std::string contents;
		{
			i2p::log::Log log;
			log.SendTo (path.string ());
			assert (log.GetLogType () == eLogFile);
			log.Start ();

			std::thread reopener (
				[&log]
				{
					for (int i = 0; i < 50; ++i) log.Reopen ();
				});
			for (int i = 0; i < 200; ++i) Append (log, "reopen-line-" + std::to_string (i));
			reopener.join ();
			log.Stop ();

			contents = ReadFile (path);
		}
		assert (CountLines (contents) == 200);
		assert (contents.find ("reopen-line-0") != std::string::npos);
		assert (contents.find ("reopen-line-199") != std::string::npos);

		// The logger (and its file stream) has been destroyed before removal,
		// which also works on platforms that lock open files.
		std::error_code ignored;
		std::filesystem::remove (path, ignored);
	}

	// Reopen follows a rotated ordinary file by path, but each file remains
	// an independent append target and no stream swap loses queued records.
	{
		const auto path = UniquePath ("openi2pd-log-rotate-");
		auto rotated = path;
		rotated += ".1";
		{
			i2p::log::Log log;
			log.SendTo (path.string ());
			log.Start ();
			Append (log, "before-rotation");
			log.Stop ();
			std::error_code error;
			std::filesystem::rename (path, rotated, error);
			assert (!error);
			{
				std::ofstream replacement (path);
				replacement << "replacement-header\n";
			}
			log.Reopen ();
			log.Start ();
			Append (log, "after-rotation");
			log.Stop ();
		}
		const auto oldContents = ReadFile (rotated);
		const auto newContents = ReadFile (path);
		assert (oldContents.find ("before-rotation") != std::string::npos);
		assert (oldContents.find ("after-rotation") == std::string::npos);
		assert (newContents.find ("replacement-header") != std::string::npos);
		assert (newContents.find ("after-rotation") != std::string::npos);
		std::error_code error;
		std::filesystem::remove (path, error);
		std::filesystem::remove (rotated, error);
	}

	return 0;
}
