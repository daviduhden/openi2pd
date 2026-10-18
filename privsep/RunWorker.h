/*
* Copyright (c) 2026, The PurpleI2P Project
*
* This file is part of Open I2Pd and licensed under BSD3
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
