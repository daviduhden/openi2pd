/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test i18n formatting.
 *
 * Exercises the bounded, checked formatting helper used by tr()/ntr().
 */

#include <iostream>
#include <string>

#include "I18N.h"

namespace
{
bool Check (bool condition, const char * message)
{
	if (!condition) std::cerr << "i18n-format: " << message << std::endl;
	return condition;
}
}

int main ()
{
	using i2p::i18n::detail::FormatPrintf;

	if (!Check (FormatPrintf ("").empty (), "empty format")) return 1;
	if (!Check (FormatPrintf ("%s", "").empty (), "empty string argument")) return 1;
	if (!Check (FormatPrintf ("%d%%", 100) == "100%", "percent escape")) return 1;
	if (!Check (FormatPrintf ("%s", "100%") == "100%", "percent in argument")) return 1;

	const std::string large (70000, 'a');
	if (!Check (FormatPrintf ("%s", large.c_str ()) == large, "large string")) return 1;

	return 0;
}
