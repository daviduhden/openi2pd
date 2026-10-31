/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test log diagnostic sink.
 *
 * Pins the contract that tests/log-support.cpp relies on: a
 * synchronous sink observes every record on the calling thread,
 * receives the same sanitized and size-bounded copy as the
 * administrative ring, does not replace the ring, and can be removed.
 * A failure here would make every other test's CI diagnostics silently
 * empty.
 *
 */

#include <cassert>
#include <cstdint>
#include <ctime>
#include <string>
#include <string_view>
#include <vector>

#include "Log.h"

int main ()
{
	i2p::log::Log & logger = i2p::log::Logger ();

	std::vector<std::string> captured;
	std::vector<LogLevel> levels;
	logger.SetSynchronousSink (
		[&captured, &levels] (LogLevel level, std::time_t, std::string_view text)
		{
			levels.push_back (level);
			captured.emplace_back (text);
		});

	LogPrint (eLogWarning, "diagnostic ", 42, " text");
	assert (levels.size () == 1 && captured.size () == 1);
	assert (levels.front () == eLogWarning);
	assert (captured.front () == "diagnostic 42 text");

	// The sink sees the sanitized copy: C0 controls other than TAB are
	// flattened, and the record is capped like the ring copy.
	LogPrint (eLogError, "line\r\nnext");
	assert (captured.size () == 2);
	assert (captured.back () == "line  next");

	const std::string oversize (4000, 'x');
	LogPrint (eLogInfo, oversize);
	assert (captured.size () == 3);
	assert (captured.back ().size () == i2p::log::LOG_RECENT_TEXT_LIMIT);
	assert (captured.back () == std::string (i2p::log::LOG_RECENT_TEXT_LIMIT, 'x'));

	// Synchronous delivery does not replace the administrative ring.
	auto recent = logger.GetRecent (0, i2p::log::LOG_RECENT_CAPACITY);
	size_t ringMatches = 0;
	for (const auto & record : recent)
		if (record.text == "diagnostic 42 text" && record.level == eLogWarning) ++ringMatches;
	assert (ringMatches == 1);

	// An empty function removes the sink; later records must not reach a
	// callback that no longer exists.
	logger.SetSynchronousSink ({});
	const size_t before = captured.size ();
	LogPrint (eLogCritical, "after removal");
	assert (captured.size () == before);
	// ... but they are still recorded in the ring.
	auto after = logger.GetRecent (0, i2p::log::LOG_RECENT_CAPACITY);
	bool found = false;
	for (const auto & record : after)
		if (record.text == "after removal" && record.level == eLogCritical) found = true;
	assert (found);

	return 0;
}
