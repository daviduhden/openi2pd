/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: getopt.
 *
 * Windows/MSVC implementation of the getopt and getopt_long subset used by the
 * tools, preserving their historical short and long command-line forms. On
 * POSIX the translation unit is empty and the system &lt;getopt.h&gt; is used.
 *
 */

#if defined(_WIN32) || defined(I2PD_TOOLS_GETOPT_TEST)

#include "getopt.h"

#include <cstring>
#include <iostream>

extern "C"
{
	char * optarg = nullptr;
	int optind = 1;
	int opterr = 1;
	int optopt = 0;
}

namespace
{
int g_OptionPosition = 1;

void ReportUnknown (const char * argv0, int option)
{
	if (opterr) std::cerr << argv0 << ": invalid option -- '" << static_cast<char> (option) << "'\n";
}
}

/// @brief Parse one short option and advance the shared cursor.
///
/// The compatibility shim reproduces the subset the maintained tools rely on:
/// clustered short options, `-xVALUE` and `-x VALUE` forms, `--` termination
/// and `?` for an unknown option, with `opterr` suppressing the diagnostic.
/// `optind`/`optarg` are process-wide like the POSIX pair, so this must not be
/// called concurrently.
///
/// @param optstring Short option letters; a trailing ':' means one argument.
/// @return The option letter, '?' (or ':' when optstring starts with ':' for a
///         missing argument), or -1 when option processing is finished.
extern "C" int getopt (int argc, char * const argv[], const char * optstring)
{
	optarg = nullptr;
	if (!argv || !optstring || optind >= argc) return -1;

	const char * argument = argv[optind];
	if (!argument || argument[0] != '-' || argument[1] == '\0') return -1;
	if (argument[1] == '-')
	{
		// A bare -- terminates option processing. Long options are handled by
		// getopt_long(), which sees the argument before delegating here.
		if (argument[2] == '\0') ++optind;
		return -1;
	}
	if (g_OptionPosition <= 0) g_OptionPosition = 1;
	const char option = argument[g_OptionPosition];
	if (option == '\0')
	{
		++optind;
		g_OptionPosition = 1;
		return getopt (argc, argv, optstring);
	}
	++g_OptionPosition;

	const char * spec = std::strchr (optstring, option);
	if (!spec)
	{
		optopt = static_cast<unsigned char> (option);
		ReportUnknown (argv[0], optopt);
		if (argument[g_OptionPosition] == '\0')
		{
			++optind;
			g_OptionPosition = 1;
		}
		return '?';
	}

	if (spec[1] == ':')
	{
		if (argument[g_OptionPosition] != '\0')
		{
			optarg = const_cast<char *> (argument + g_OptionPosition);
			++optind;
			g_OptionPosition = 1;
		}
		else
		{
			++optind;
			g_OptionPosition = 1;
			if (optind >= argc)
			{
				if (opterr) std::cerr << argv[0] << ": option requires an argument -- '" << option << "'\n";
				optopt = static_cast<unsigned char> (option);
				return optstring[0] == ':' ? ':' : '?';
			}
			optarg = argv[optind++];
		}
	}
	else if (argument[g_OptionPosition] == '\0')
	{
		++optind;
		g_OptionPosition = 1;
	}
	return static_cast<unsigned char> (option);
}

/// @brief Parse a long option, delegating anything that is not `--name` to getopt.
///
/// Matching is exact (no GNU abbreviation): a documented CLI must not silently
/// accept a truncated spelling. `--name=value` and `--name value` are both
/// accepted for required_argument; a value given to a no_argument option is an
/// error. `optstring` remains the fallback for the tools that mix both forms,
/// and a bare `--` terminates processing.
///
/// @param optionIndex Receives the matched long-option index, or -1.
/// @return The option value, 0 when `flag` was set, '?' on error, -1 at end.
extern "C" int getopt_long (
	int argc, char * const argv[], const char * optstring, const struct option * longOptions, int * optionIndex)
{
	optarg = nullptr;
	if (optionIndex) *optionIndex = -1;
	if (!argv || optind >= argc) return -1;

	const char * argument = argv[optind];
	if (argument && argument[0] == '-' && argument[1] == '-' && argument[2] == '\0')
	{
		++optind;
		return -1;
	}
	if (!argument || argument[0] != '-' || argument[1] != '-' || argument[2] == '\0')
		return getopt (argc, argv, optstring);

	const char * name = argument + 2;
	const char * equals = std::strchr (name, '=');
	const size_t nameLength = equals ? static_cast<size_t> (equals - name) : std::strlen (name);
	if (!longOptions || nameLength == 0)
	{
		if (opterr) std::cerr << argv[0] << ": unrecognized option '" << argument << "'\n";
		++optind;
		return '?';
	}

	for (int index = 0; longOptions[index].name; ++index)
	{
		const auto & entry = longOptions[index];
		if (std::strlen (entry.name) != nameLength || std::strncmp (entry.name, name, nameLength) != 0) continue;
		if (optionIndex) *optionIndex = index;

		const char * value = equals ? equals + 1 : nullptr;
		if (entry.has_arg == required_argument)
		{
			if (!value)
			{
				++optind;
				if (optind >= argc)
				{
					if (opterr) std::cerr << argv[0] << ": option '--" << entry.name << "' requires an argument\n";
					return '?';
				}
				value = argv[optind];
			}
		}
		else if (entry.has_arg == no_argument && value)
		{
			if (opterr) std::cerr << argv[0] << ": option '--" << entry.name << "' does not take an argument\n";
			++optind;
			return '?';
		}

		optarg = const_cast<char *> (value);
		++optind;
		if (entry.flag)
		{
			*entry.flag = entry.val;
			return 0;
		}
		return entry.val;
	}

	if (opterr) std::cerr << argv[0] << ": unrecognized option '" << argument << "'\n";
	++optind;
	return '?';
}

#endif
