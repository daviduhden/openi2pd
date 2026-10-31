/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test tools getopt.
 *
 * Host-side regression for the Windows/MSVC getopt compatibility shim.
 * It is built with I2PD_TOOLS_GETOPT_TEST on POSIX so the same
 * implementation that MSVC uses is exercised without a Windows runner.
 * It does not touch the network, files or the router.
 */

#include <cassert>
#include <cstdio>
#include <cstring>

#include "common/getopt.h"

namespace
{
/**
 * Reset the shim's process-wide parser state between cases.
 *
 * `optarg`, `optind`, `optopt` and `opterr` are globals, so each case
 * must start from this state or it inherits the previous case's
 * position. `opterr = 0` keeps expected diagnostics out of the test
 * output. The `index` parameter only resets the long-option index that
 * `getopt_long` writes.
 * The shim also keeps an internal position inside the current argument,
 * which this helper cannot reach: a case is only isolated when it
 * consumes every option it starts, as the cases below do. A case that
 * abandoned a clustered argument such as `-ab` after the first letter
 * would poison the next one.
 * Cases pass writable `char` arrays because the implementation permutes
 * `argv` and hands out pointers into it through `optarg`.
 */
void Reset (int & index)
{
	optarg = nullptr;
	optind = 1;
	optopt = 0;
	opterr = 0;
	index = 0;
}
}

int main ()
{
	// Short options with an attached operand and a trailing positional.
	{
		char arg0[] = "tool";
		char arg1[] = "-a";
		char arg2[] = "-bvalue";
		char arg3[] = "operand";
		char * argv[] = { arg0, arg1, arg2, arg3, nullptr };
		int index = 0;
		Reset (index);
		assert (getopt (4, argv, "ab:") == 'a');
		assert (optarg == nullptr);
		assert (getopt (4, argv, "ab:") == 'b');
		assert (optarg && std::strcmp (optarg, "value") == 0);
		assert (getopt (4, argv, "ab:") == -1);
		assert (optind == 3);
	}

	// A separate operand is consumed by a required argument.
	{
		char arg0[] = "tool";
		char arg1[] = "-b";
		char arg2[] = "--help";
		char * argv[] = { arg0, arg1, arg2, nullptr };
		int index = 0;
		Reset (index);
		assert (getopt (3, argv, "b:") == 'b');
		assert (optarg && std::strcmp (optarg, "--help") == 0);
	}

	// Unknown option reports '?' and sets optopt.
	{
		char arg0[] = "tool";
		char arg1[] = "-z";
		char * argv[] = { arg0, arg1, nullptr };
		int index = 0;
		Reset (index);
		assert (getopt (2, argv, "ab:") == '?');
		assert (optopt == 'z');
	}

	// Bare -- terminates option processing and advances past it.
	{
		char arg0[] = "tool";
		char arg1[] = "--";
		char arg2[] = "-a";
		char * argv[] = { arg0, arg1, arg2, nullptr };
		int index = 0;
		Reset (index);
		assert (getopt (3, argv, "a") == -1);
		assert (optind == 2);
	}

	// Long option with an equals operand, then a flagless long option.
	{
		struct option options[] = { { "threads", required_argument, nullptr, 't' },
			{ "help", no_argument, nullptr, 'h' }, { nullptr, 0, nullptr, 0 } };
		char arg0[] = "tool";
		char arg1[] = "--threads=4";
		char arg2[] = "--help";
		char * argv[] = { arg0, arg1, arg2, nullptr };
		int index = 0;
		Reset (index);
		assert (getopt_long (3, argv, "t:h", options, &index) == 't');
		assert (optarg && std::strcmp (optarg, "4") == 0);
		assert (index == 0);
		assert (getopt_long (3, argv, "t:h", options, &index) == 'h');
		assert (index == 1);
		assert (getopt_long (3, argv, "t:h", options, &index) == -1);
	}

	// A long option that requires an argument and has none reports '?'.
	{
		struct option options[] = { { "threads", required_argument, nullptr, 't' }, { nullptr, 0, nullptr, 0 } };
		char arg0[] = "tool";
		char arg1[] = "--threads";
		char * argv[] = { arg0, arg1, nullptr };
		int index = 0;
		Reset (index);
		assert (getopt_long (2, argv, "t:", options, &index) == '?');
	}

	std::puts ("tools-getopt: all checks passed");
	return 0;
}
