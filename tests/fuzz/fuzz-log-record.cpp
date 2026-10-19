/*
 * Copyright (c) 2026, David Uhden Collado
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * libFuzzer target for the bounded administrative log snapshot. Append()
 * is called from arbitrary router threads, so the recent-record copy must
 * never retain control characters or exceed its documented size.
 */

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>

#include "Log.h"

extern "C" int LLVMFuzzerTestOneInput (const uint8_t * data, size_t size)
{
	if (!data && size != 0) return 0;

	const LogLevel level = static_cast<LogLevel> (size == 0 ? 0 : data[0] % eNumLogLevels);
	std::string text;
	if (size != 0) text.assign (reinterpret_cast<const char *> (data), size);

	i2p::log::Log log;
	auto msg = std::make_shared<i2p::log::LogMsg> (level, 0, std::move (text));
	log.Append (msg);

	const auto records = log.GetRecent (0, 8);
	if (records.size () != 1) std::abort ();
	const auto & record = records.front ();
	if (record.text.size () > i2p::log::LOG_RECENT_TEXT_LIMIT) std::abort ();
	for (char ch : record.text)
	{
		const auto byte = static_cast<unsigned char> (ch);
		if (byte < 0x20 && ch != '\t') std::abort ();
	}

	// A second call with a larger "after" sequence must not return the record.
	if (!log.GetRecent (record.sequence, 8).empty ()) std::abort ();
	return 0;
}
