/*
* Copyright (c) 2013-2024, The PurpleI2P Project
* Copyright (c) 2026, David Uhden Collado
* SPDX-License-Identifier: BSD-3-Clause
*
* This file is part of openi2pd and licensed under BSD-3-Clause
*
* See full license text in LICENSE file at top of project tree
*/

#ifndef LIBI2PD_CPU_H
#define LIBI2PD_CPU_H

#if defined(_M_AMD64) || defined(__x86_64__) || defined(_M_IX86) || defined(__i386__)
#	define IS_X86 1
#	if defined(_M_AMD64) || defined(__x86_64__)
#		define IS_X86_64 1
#	else
#		define IS_X86_64 0
#	endif
#else
#	define IS_X86 0
#	define IS_X86_64 0
#endif

#endif
