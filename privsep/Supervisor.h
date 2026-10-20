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

#include <string>
#include <string_view>
#include "Worker.h"

namespace i2p::privsep
{
/**
 * Entry point for the supervisor process. The core owns the other endpoint
 * of the control socketpair. The supervisor owns the log worker process and
 * forwards structured records; it never opens the log file, so it has no
 * persistent-write capability of its own.
 */
int RunSupervisor (int fd);
// Prepared by the daemon before runtime pledge. The immutable root bounds all
// later worker delegation; this supervisor never has persistent-write access.
bool PrepareTorrentStorageSupervisor (const std::string & root, std::string & error);
// rootFd stays local: only relative path and fstat identity travel to broker.
// OpenBSD pledge sendfd/recvfd explicitly exclude directory descriptors.
i2p::expected<Channel, std::string> SpawnSupervisedTorrentStorage (
 int rootFd, std::string_view rootPath);
int RunTorrentStorageSupervisor (int fd);
}

#endif
