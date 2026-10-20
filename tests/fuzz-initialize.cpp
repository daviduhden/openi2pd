/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: fuzz initialize.
 *
 * Common libFuzzer initialization. The harness never starts the asynchronous
 * log worker, so parser diagnostics would otherwise accumulate in its
 * unbounded pre-start queue for every malformed input and turn a valid run
 * into an out-of-memory failure. Logging is therefore disabled before the
 * first input is processed.
 *
 */

#include "Log.h"

extern "C" int LLVMFuzzerInitialize (int *, char ***)
{
	i2p::log::Logger ().SetLogLevel ("none");
	return 0;
}
