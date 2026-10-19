/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: RunWorker.
 *
 * Declares the exec'ed worker entry point. The role and inherited channel
 * descriptor arrive as arguments so the post-fork child never needs to
 * mutate the environment.
 *
 */

#ifndef I2PD_PRIVSEP_RUN_WORKER_H__
#define I2PD_PRIVSEP_RUN_WORKER_H__

#include <string_view>

namespace i2p::privsep
{
/**
	 * Entry point for an exec'ed worker process. The channel descriptor is
	 * passed as an argument rather than through the environment so the
	 * post-fork child never needs setenv().
	 */
int RunWorker (std::string_view role, std::string_view fdArgument);
}

#endif
