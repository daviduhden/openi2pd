/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * Force-included by MSVC test targets before any source header.
 * Release test assertions must evaluate their operands too: fixtures
 * perform setup and verify required operations inside assert().
 */

#ifndef I2PD_TEST_ASSERTIONS_H
#define I2PD_TEST_ASSERTIONS_H

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

#endif
