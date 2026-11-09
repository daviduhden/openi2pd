/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 */

#ifndef I2PD_TEST_CAPABILITY_SKIP_H
#define I2PD_TEST_CAPABILITY_SKIP_H

#include <cstdio>

namespace i2p::test
{
/* Emit a static, non-secret reason before CTest's capability skip code. */
inline int SkipCapability (const char * reason)
{
	std::fputs ("SKIP: ", stderr);
	std::fputs (reason, stderr);
	std::fputc ('\n', stderr);
	return 77;
}
}

#endif
