/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Supervisor.
 *
 * Declares the supervisor entry point. The supervisor owns the log-worker
 * process, forwards framed log records and never opens the log file itself.
 *
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
