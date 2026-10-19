/*
* Copyright (c) 2026, David Uhden Collado
* SPDX-License-Identifier: BSD-3-Clause
*
* This file is part of openi2pd and licensed under BSD-3-Clause
*/

#ifndef I2PD_PRIVSEP_SUPERVISOR_H__
#define I2PD_PRIVSEP_SUPERVISOR_H__

namespace i2p::privsep
{
/**
 * Entry point for the supervisor process. The core owns the other endpoint
 * of the control socketpair. The supervisor owns the log worker process and
 * forwards structured records; it never opens the log file, so it has no
 * persistent-write capability of its own.
 */
int RunSupervisor (int fd);
}

#endif
