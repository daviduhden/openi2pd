/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test bounded string copy.
 *
 * Exercises the fixed-buffer CopyString helper used for platform
 * interface
 * names and other C-style buffers. The test is prepared for manual CI.
 */

#include <cstring>
#include <iostream>
#include <string>
#include <string_view>

#include "util.h"

namespace
{
bool Check (bool condition, const char * message)
{
	if (!condition) std::cerr << "copy-string: " << message << std::endl;
	return condition;
}
}

int main ()
{
	char buffer[8];
	std::memset (buffer, 'X', sizeof (buffer));

	if (!Check (i2p::util::CopyString (nullptr, 0, "abc") == 3, "null/zero capacity must report length")) return 1;
	if (!Check (i2p::util::CopyString (buffer, 0, "abc") == 3 && buffer[0] == 'X', "zero capacity must not write"))
		return 1;
	if (!Check (i2p::util::CopyString (buffer, sizeof (buffer), "abc") == 3 && std::strcmp (buffer, "abc") == 0,
			"fits and terminates"))
		return 1;
	if (!Check (
			i2p::util::CopyString (buffer, sizeof (buffer), "0123456789") == 10 && std::strcmp (buffer, "0123456") == 0,
			"truncates to capacity minus terminator"))
		return 1;
	if (!Check (i2p::util::CopyString (buffer, 1, "abc") == 3 && buffer[0] == '\0',
			"capacity one writes only the terminator"))
		return 1;

	const std::string binary { 'a', '\0', 'b' };
	if (!Check (i2p::util::CopyString (buffer, sizeof (buffer), binary) == 3 && buffer[0] == 'a' && buffer[1] == '\0'
				&& buffer[2] == 'b',
			"preserves embedded bytes"))
		return 1;

	return 0;
}
