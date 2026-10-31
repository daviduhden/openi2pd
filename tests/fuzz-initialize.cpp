/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: fuzz initialize.
 *
 * Shared libFuzzer initialization. The harness does not start the async
 * log worker, so parser diagnostics accumulate in its unbounded queue
 * queue. Disable logging before input processing to prevent exhaustion.
 *
 */

#include "Log.h"

extern "C" int LLVMFuzzerInitialize (int *, char ***)
{
	i2p::log::Logger ().SetLogLevel ("none");
	return 0;
}
