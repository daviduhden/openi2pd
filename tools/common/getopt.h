/*
 * Copyright (c) 2026, David Uhden Collado
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef I2PD_TOOLS_COMMON_GETOPT_H
#define I2PD_TOOLS_COMMON_GETOPT_H

/*
 * Tools keep their historical getopt/getopt_long command lines on Unix.
 * Windows/MSVC has no <getopt.h>; tools/common/getopt.cpp provides the small
 * subset used by the maintained tools. This is deliberately a compatibility
 * layer, not a new CLI grammar.
 */
#ifdef _WIN32

#ifdef __cplusplus
extern "C"
{
#endif

	struct option
	{
		const char * name;
		int has_arg;
		int * flag;
		int val;
	};

#define no_argument 0
#define required_argument 1
#define optional_argument 2

	extern char * optarg;
	extern int optind;
	extern int opterr;
	extern int optopt;

	int getopt (int argc, char * const argv[], const char * optstring);
	int getopt_long (
		int argc, char * const argv[], const char * optstring, const struct option * longOptions, int * optionIndex);

#ifdef __cplusplus
}
#endif

#else

#include <getopt.h>
#include <unistd.h>

#endif

#endif
