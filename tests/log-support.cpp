/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test log support.
 *
 * Every C++ test target links this translation unit. A static
 * initializer installs a synchronous diagnostic sink on the product
 * logger, so the records a test causes are written to stderr as they
 * happen instead of staying only in the administrative ring. CTest
 * captures that stream, and an assert() abort therefore still leaves
 * the product's own diagnostics in the CI log.
 *
 * The sink is bounded by the logger's sanitized 512-byte record limit,
 * serialized by a local mutex so interleaved threads cannot split a
 * line, and flushed per record so an abort does not lose it. It never
 * touches the logger mutexes while formatting, so a failing test cannot
 * deadlock the logging path.
 *
 * OPENI2PD_TEST_LOG_LEVEL selects the minimum level (none, critical,
 * error, warn, info or debug; "warning" is accepted as an alias). The
 * default stays at the product's own info level. Fuzz targets do not
 * link this unit: per-input stderr output would dominate their
 * throughput, and their crashes are reported by the sanitizer.
 *
 */

#include "Log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>
#include <string_view>

namespace
{
std::mutex g_EchoMutex;

const char * LevelName (LogLevel level) noexcept
{
	switch (level)
	{
	case eLogCritical: return "critical";
	case eLogError: return "error";
	case eLogWarning: return "warn";
	case eLogInfo: return "info";
	case eLogDebug: return "debug";
	case eLogNone:
	case eNumLogLevels: break;
	}
	return "log";
}

// Writes one record to stderr. The text is the logger's sanitized
// copy: it is already capped at LOG_RECENT_TEXT_LIMIT bytes and has
// C0 controls (except TAB) flattened, so a test cannot inject escape
// sequences into the CI log through a product message.
void Echo (LogLevel level, std::time_t, std::string_view text) noexcept
{
	try
	{
		std::lock_guard<std::mutex> lock (g_EchoMutex);
		std::fputs ("[test-log ", stderr);
		std::fputs (LevelName (level), stderr);
		std::fputs ("] ", stderr);
		std::fwrite (text.data (), 1, text.size (), stderr);
		std::fputc ('\n', stderr);
		std::fflush (stderr);
	}
	catch (...)
	{
		// A diagnostic must never change the test outcome.
	}
}

void ApplyRequestedLevel () noexcept
{
	const char * requested = nullptr;
#if defined(_MSC_VER)
	// MSVC deprecates getenv; own the secure CRT's returned copy.
	char * allocated = nullptr;
	size_t length = 0;
	const auto result = _dupenv_s (&allocated, &length, "OPENI2PD_TEST_LOG_LEVEL");
	const std::unique_ptr<char, decltype (&std::free)> owned { allocated, &std::free };
	if (result != 0) return;
	requested = owned.get ();
#else
	requested = std::getenv ("OPENI2PD_TEST_LOG_LEVEL");
#endif
	if (!requested || !*requested) return;
	// The product spells the warning level "warn"; accept the common
	// "warning" spelling without adding it to the production parser.
	if (std::strcmp (requested, "warning") == 0) requested = "warn";
	try
	{
		i2p::log::Logger ().SetLogLevel (requested);
	}
	catch (...)
	{
		// An invalid level leaves the product default in place.
	}
}

struct Install
{
	Install ()
	{
		// Install the sink before changing the level so the logger's
		// own "Logging level set to ..." line is echoed as well.
		i2p::log::Logger ().SetSynchronousSink (&Echo);
		ApplyRequestedLevel ();
	}
};

// One initializer per test process. Tests are single-translation
// executables created by i2pd_add_test(), so this runs exactly once
// before main().
Install g_Install;
} // namespace
