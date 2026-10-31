/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test bob command.
 *
 * Compile-free regression for the BOB command channel policy: the
 * bounded line reader rejects a newline-free flood without unbounded
 * growth and keeps a pipelined line queued, an invalid port operand
 * becomes a protocol error instead of a throwing conversion, the
 * concurrent-session cap is enforced, and the private-key reply stays
 * disabled unless the option is explicit. No socket, destination or
 * tunnel is created.
 *
 */

#include <atomic>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "BOB.h"

namespace
{
int failures = 0;

void Check (bool condition, const std::string & what)
{
	if (condition) return;
	std::cerr << "FAIL: " << what << std::endl;
	++failures;
}
}

int main ()
{
	using namespace i2p::client;

	// --- bounded line reader
	// ----------------------------------------------
	{
		Check (BOB_MAX_COMMAND_LINE == 4096, "command line bound is 4 KiB");

		BOBCommandLineReader reader;
		Check (reader.Feed ("hel") == BOBCommandLineReader::Result::Incomplete, "partial line is incomplete");
		Check (!reader.HasCompleteLine (), "partial line is not complete");
		Check (reader.Feed ("lo\n") == BOBCommandLineReader::Result::Complete, "newline completes the line");
		Check (reader.HasCompleteLine (), "line is complete");
		Check (reader.TakeLine () == "hello", "line is returned without the newline");
		Check (!reader.HasCompleteLine (), "line is consumed");
		Check (reader.GetPendingSize () == 0, "nothing is left after the line");

		// A CRLF peer loses only the CR.
		Check (reader.Feed ("quit\r\n") == BOBCommandLineReader::Result::Complete, "CRLF line completes");
		Check (reader.TakeLine () == "quit", "CR is stripped");

		// A pipelined line stays queued for the next command.
		Check (reader.Feed ("one\ntwo\n") == BOBCommandLineReader::Result::Complete, "pipelined batch completes");
		Check (reader.TakeLine () == "one", "first pipelined line");
		Check (reader.HasCompleteLine (), "second pipelined line is queued");
		Check (reader.TakeLine () == "two", "second pipelined line");
		Check (!reader.HasCompleteLine (), "pipelined batch drained");

		// A line at the exact limit is accepted; one byte more is not.
		{
			BOBCommandLineReader exact;
			std::string line (BOB_MAX_COMMAND_LINE, 'a');
			Check (exact.Feed (line) == BOBCommandLineReader::Result::Incomplete, "limit-sized line is incomplete");
			Check (exact.GetPendingSize () <= BOB_MAX_COMMAND_LINE + 1, "pending size stays bounded");
			Check (exact.Feed ("\n") == BOBCommandLineReader::Result::Complete, "limit-sized line completes");
			Check (exact.TakeLine ().size () == BOB_MAX_COMMAND_LINE, "limit-sized line is intact");
		}
		{
			BOBCommandLineReader overflow;
			std::string line (BOB_MAX_COMMAND_LINE + 1, 'a');
			const auto result = overflow.Feed (line);
			Check (result == BOBCommandLineReader::Result::Overflow, "over-long line overflows");
			Check (overflow.GetPendingSize () <= BOB_MAX_COMMAND_LINE + 1, "overflow stores no excess");
		}

		// A newline-free flood must be rejected without growth.
		{
			BOBCommandLineReader flood;
			std::string chunk (256, 'x');
			bool overflowed = false;
			for (int i = 0; i < 64 && !overflowed; ++i)
				overflowed = flood.Feed (chunk) == BOBCommandLineReader::Result::Overflow;
			Check (overflowed, "newline-free flood overflows");
			Check (flood.GetPendingSize () <= BOB_MAX_COMMAND_LINE + 1, "flood buffer is bounded");
		}
	}

	// --- port operand
	// ------------------------------------------------------
	{
		uint16_t port = 0;
		Check (ParseBOBPort ("", port) == BOBPortResult::Empty, "empty port is rejected");
		Check (ParseBOBPort ("abc", port) == BOBPortResult::Invalid, "non-numeric port is rejected");
		Check (ParseBOBPort ("-1", port) == BOBPortResult::Invalid, "negative port is rejected");
		Check (ParseBOBPort ("12x", port) == BOBPortResult::Invalid, "trailing bytes are rejected");
		Check (ParseBOBPort ("65536", port) == BOBPortResult::OutOfRange, "port above 65535 is rejected");
		Check (ParseBOBPort ("999999", port) == BOBPortResult::OutOfRange, "long port is rejected");
		Check (ParseBOBPort ("0", port) == BOBPortResult::Ok && port == 0, "port zero is accepted");
		Check (ParseBOBPort ("65535", port) == BOBPortResult::Ok && port == 65535, "maximum port is accepted");
		Check (ParseBOBPort ("8080", port) == BOBPortResult::Ok && port == 8080, "ordinary port is accepted");
	}

	// --- session admission
	// -------------------------------------------------
	{
		Check (BOB_MAX_CONCURRENT_SESSIONS >= 1, "session cap is positive");
		BOBAdmissionState state;
		size_t admitted = 0;
		while (TryAcquireBOBSession (state))
		{
			++admitted;
			if (admitted > BOB_MAX_CONCURRENT_SESSIONS + 2) break; // never spin forever
		}
		Check (admitted == BOB_MAX_CONCURRENT_SESSIONS, "session cap is enforced");
		Check (!TryAcquireBOBSession (state), "no slot beyond the cap");
		ReleaseBOBSession (state);
		Check (TryAcquireBOBSession (state), "released slot is reusable");
		ReleaseBOBSession (state);
		// Releasing more than acquired must not underflow.
		for (size_t i = 0; i < admitted + 4; ++i) ReleaseBOBSession (state);
		Check (state.active.load () == 0, "release never underflows");
		Check (TryAcquireBOBSession (state), "state is reusable after over-release");
		ReleaseBOBSession (state);

		// A concurrent acquire/release cycle stays consistent.
		BOBAdmissionState shared;
		std::vector<std::thread> workers;
		std::atomic<size_t> succeeded { 0 };
		for (int i = 0; i < 4; ++i)
			workers.emplace_back (
				[&shared, &succeeded]
				{
					for (int round = 0; round < 1000; ++round)
					{
						if (!TryAcquireBOBSession (shared)) continue;
						succeeded.fetch_add (1, std::memory_order_relaxed);
						ReleaseBOBSession (shared);
					}
				});
		for (auto & worker : workers) worker.join ();
		Check (succeeded.load () > 0, "contended admission still succeeds");
		Check (shared.active.load () == 0, "contended admission returns to zero");
	}

	// --- getkeys opt-in
	// ----------------------------------------------------
	{
		Check (!BOB_DEFAULT_ALLOW_GETKEYS, "getkeys is denied by default");
		Check (!ResolveBOBAllowGetKeys (false, false), "absent option denies getkeys");
		Check (!ResolveBOBAllowGetKeys (false, true), "absent option denies even a true value");
		Check (!ResolveBOBAllowGetKeys (true, false), "false option denies getkeys");
		Check (ResolveBOBAllowGetKeys (true, true), "explicit true allows getkeys");
		Check (BOB_OPTION_ALLOW_GETKEYS == "bob.allowgetkeys", "option name is stable");
	}

	if (failures)
	{
		std::cerr << failures << " BOB command checks failed" << std::endl;
		return 1;
	}
	std::cout << "BOB command checks passed" << std::endl;
	return 0;
}
