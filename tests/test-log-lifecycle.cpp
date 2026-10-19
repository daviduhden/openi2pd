/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test log lifecycle.
 *
 * Exercises logger start/stop/reopen, sink replacement and destruction while the writer thread runs, requiring records to be drained and the output file to close before removal.
 *
 */

#include <atomic>
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

#include "Log.h"

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
}

int main ()
{
	// Repeated Start()/Stop() must not replace a joinable thread or lose the
	// records queued in each cycle.
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

	// Reopen() is called from the signal-handling thread while the writer is
	// active. Every record must reach the same file and no stream swap may
	// deadlock against the writer.
	{
		const auto unique = std::chrono::steady_clock::now ().time_since_epoch ().count ()
			^ static_cast<long long> (std::random_device { }());
		const auto path
			= std::filesystem::temp_directory_path () / ("openi2pd-log-lifecycle-" + std::to_string (unique) + ".log");

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

	return 0;
}
