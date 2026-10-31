/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Credentials.
 *
 * Declares the service identity used by every privilege-separated
 * worker. Names are resolved once, before any pledge/unveil policy is
 * installed and before the first worker is spawned. The forked child
 * drops supplementary groups, gid and uid before execve; the exec'ed
 * worker verifies the identity recorded in its spawn hello. No
 * OpenBSD-specific call is made here, so the same contract is
 * reviewable on every platform.
 */

#ifndef I2PD_PRIVSEP_CREDENTIALS_H__
#define I2PD_PRIVSEP_CREDENTIALS_H__

#include <cstdint>
#include <string>
#include <string_view>

#include "Expected.h"

namespace i2p::privsep
{
/**
 * Fixed service identity of every spawned worker.
 *
 * The pair is non-secret and process-wide: it is resolved once before
 * the startup sandbox and the first spawn, and no later NSS lookup can
 * change it. On Windows the struct is inert (`configured` stays false
 * and `WorkerUid()` is 0) because the Windows backend identifies a
 * worker by its authenticated pipe peer token instead of a POSIX
 * uid/gid; a caller that needs a real worker identity, such as the
 * storage clients, must therefore fail closed there rather than treat 0
 * as a valid uid.
 */
struct WorkerCredentials
{
	/**
	 * True only when `privsep.user` was resolved; false keeps the
	 * daemon's own effective identity. */
	bool configured = false;
	uint32_t uid = 0;
	uint32_t gid = 0;
};

/**
 * Resolve `user`/`group` before the startup sandbox is applied.
 *
 * An empty user and group keep the current effective identity
 * (unconfigured). A configured user must exist, must not be uid 0, and
 * its primary group is used when `group` is empty. A configured group
 * must exist. The result is a fixed uid/gid pair; later lookups or NSS
 * changes cannot alter it.
 */
i2p::expected<WorkerCredentials, std::string> ResolveWorkerCredentials (std::string_view user, std::string_view group);

/**
 * Install the process-wide worker identity. Must precede the first
 * spawn. */
void SetWorkerCredentials (const WorkerCredentials & credentials);
const WorkerCredentials & GetWorkerCredentials ();

/**
 * Effective uid a spawned worker must have. In the core this is the
 * configured `privsep.user` (or the core's own euid when unconfigured);
 * after `RunWorker` it is the identity recorded in the spawn hello.
 * Never 0 for a started worker.
 */
uint32_t WorkerUid ();

/**
 * True when a channel peer may be the daemon that spawned this worker:
 * the same effective uid, or uid 0 for an unprivileged worker whose
 * root core retained its authority and only drops credentials in the
 * child. A channel is an anonymous socketpair, so a root peer can only
 * be the spawner.
 */
bool IsAllowedParentUid (uint32_t peerUid);

/**
 * Drop to the configured identity in a forked child, immediately before
 * execve. Clears supplementary groups, sets gid then uid, and verifies
 * the real/effective ids plus an empty supplementary set.
 * Async-signal-safe: only syscalls, no allocation, no logging. Returns
 * false on any mismatch; the caller must `_exit` without exec.
 *
 * On Windows there is no forked child to drop into and this function
 * always returns false: a caller must treat it as "the spawn is not
 * available here", never as "no drop was needed".
 */
bool DropWorkerCredentialsInChild (const WorkerCredentials & credentials);

/**
 * Describe the identity without leaking a password or a home directory.
 * Used in bounded startup diagnostics.
 */
std::string DescribeWorkerCredentials (const WorkerCredentials & credentials);
}

#endif
