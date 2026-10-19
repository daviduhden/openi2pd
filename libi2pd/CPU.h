/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: CPU.
 *
 * Defines the IS_X86 and IS_X86_64 preprocessor flags that select x86-specific
 * code paths at compile time.
 *
 */

#ifndef LIBI2PD_CPU_H
#define LIBI2PD_CPU_H

#if defined(_M_AMD64) || defined(__x86_64__) || defined(_M_IX86) || defined(__i386__)
#define IS_X86 1
#if defined(_M_AMD64) || defined(__x86_64__)
#define IS_X86_64 1
#else
#define IS_X86_64 0
#endif
#else
#define IS_X86 0
#define IS_X86_64 0
#endif

#endif
