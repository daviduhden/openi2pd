/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: LinuxSandbox.
 *
 * Declares the Linux worker policy classes and the Landlock, seccomp,
 * network-namespace, capability and cgroup-v2 helpers used to confine
 * workers. Profiles separate network, persistent-write, socket-creation and
 * namespace requirements; probing only reports kernel support.
 *
 */

#ifndef I2PD_PRIVSEP_LINUX_SANDBOX_H__
#define I2PD_PRIVSEP_LINUX_SANDBOX_H__

#include <cstdint>
#include <string>
#include <vector>

#include "Expected.h"

namespace i2p::privsep
{
/**
 * Coarse process classes for the Linux worker sandbox. These are policy
 * classes, not a claim that every backend has already been split out of the
 * router core.
 */
enum class WorkerClass : uint8_t
{
	Log,
	Storage,
	Network,
	Crypto,
	Admin,
	Supervisor
};

struct LinuxSandboxProfile
{
	bool allowNetwork = false;
	bool allowPersistentWrites = false;
	/** Restrict regular-file/directory reads to delegated writable roots. */
	bool restrictFilesystemReads = false;
	/** Deny every new socket/socketpair, including AF_UNIX. */
	bool denySocketCreation = false;
	/** Deny AF_INET/AF_INET6 but permit AF_UNIX for IPC/syslog. */
	bool denyInetSockets = false;
	/**
	 * Request a new empty network namespace. Failure is only fatal when
	 * requireNetworkNamespace is true; seccomp/Landlock remain the fallback.
	 */
	bool isolateNetworkNamespace = false;
	bool requireNetworkNamespace = false;
	bool requireLandlock = true;
	// Directories/files for which persistent writes are allowed. Empty means
	// no persistent write access. The paths must be absolute and pre-resolved
	// before the sandbox is applied.
	std::vector<std::string> writablePaths;
};

/**
 * Declared policy of one Linux worker class: the capabilities a role is allowed
 * to retain. It is the contract, not the enforcement; the backend policy for a
 * given platform must never grant more than this profile, and the profile never
 * implies that the role already runs in its own process.
 *
 * Log and Storage are the classes with a live process today; Network, Crypto,
 * Admin and Supervisor describe intended boundaries, and Supervisor is applied
 * only through `ApplyLinuxSupervisorSandbox`, which deliberately installs no
 * Landlock/seccomp/namespace domain.
 */
LinuxSandboxProfile GetLinuxSandboxProfile (WorkerClass workerClass);
/**
 * Install the profile's Landlock domain and seccomp filter.
 *
 * `writablePaths` must be absolute and resolved before this call; an empty list
 * means the process may not write anywhere. A mechanism that the profile marks
 * as required (Landlock by default) fails closed when the kernel cannot provide
 * it, so a caller never silently continues with fewer restrictions. This must
 * run before the first untrusted frame is parsed, and a delegated root the
 * policy must include has to be resolved first.
 *
 * A profile that requests both external networking and persistent writes is
 * rejected: that is the process-level enforcement of the role/capability
 * invariant in privsep/Capabilities.h. The socket filter is installed at most
 * once per process and cannot be relaxed afterwards, so a later profile can
 * only add restrictions.
 */
i2p::expected<void, std::string> ApplyLinuxSandbox (const LinuxSandboxProfile & profile);
// The supervisor must not install Landlock/seccomp/network namespaces: those
// domains are inherited across fork+exec and would prevent every child from
// using its own delegated capability (storage writes, network sockets).
// It still drops capabilities and sets no_new_privs; each exec'ed worker
// applies its own role policy before handling requests.
i2p::expected<void, std::string> ApplyLinuxSupervisorSandbox ();

/**
 * First capability stage in the forked child, **while it is still
 * privileged**: clear the ambient set and drop every bounding-set bit. The
 * effective/permitted sets are deliberately kept so the following
 * `setgroups`/`setgid`/`setuid` calls still have CAP_SETGID/CAP_SETUID.
 */
i2p::expected<void, std::string> PrepareCapabilitiesBeforeCredentialDrop ();
/**
 * Second capability stage, after `setuid`: zero the effective/permitted/
 * inheritable sets and verify with capget. Running this before the credential
 * drop would make setgid/setuid fail; running it only after the drop (the old
 * behaviour) left file capabilities in place.
 */
i2p::expected<void, std::string> ClearCapabilitiesAfterCredentialDrop ();

/**
 * Apply the part of a worker policy that needs no configuration and must be
 * in force before the first untrusted frame is parsed: no-new-privs, an empty
 * capability set and the role's socket-creation/Internet seccomp filter.
 * Landlock (which needs the delegated root) is applied later. The same filter
 * is installed at most once per process.
 */
i2p::expected<void, std::string> ApplyLinuxIpcLockdown (const LinuxSandboxProfile & profile);
/**
 * Assert the post-exec preconditions before a worker serves a frame:
 * `PR_GET_NO_NEW_PRIVS == 1` and an empty capability set (Linux only).
 */
i2p::expected<void, std::string> VerifyWorkerPrivileges ();

/**
 * Optional cgroup v2 limits. The path must name a delegated cgroup directory
 * (not an arbitrary filesystem path). The function writes only the known
 * cgroup control files, with O_NOFOLLOW/O_CLOEXEC, and can move the current
 * process into the cgroup after setting limits.
 *
 * A cgroup is not a substitute for Landlock/seccomp: it is a resource boundary,
 * not a pathname/network capability.
 */
struct LinuxCgroupV2Limits
{
	std::string path;
	uint64_t memoryMax = 0; // 0 leaves memory.max untouched
	uint32_t pidsMax = 0; // 0 leaves pids.max untouched
	bool moveSelf = true;
};

i2p::expected<void, std::string> ApplyLinuxCgroupV2Limits (const LinuxCgroupV2Limits & limits);

/**
 * Evaluation result for diagnostics and tests. It never enables a capability;
 * it only reports which mechanisms the running kernel can enforce. A false
 * field here is not a policy decision either: the caller's profile still marks
 * a mechanism as required or optional, and a required one that is reported
 * unavailable makes the sandbox installation fail closed.
 */
struct LinuxIsolationSupport
{
	long landlockAbi = 0;
	bool filesystem = false;
	bool tcpNetwork = false;
	bool udpNetwork = false;
	bool seccompFilter = false;
	bool capabilities = false;
	bool userNamespaces = false;
	bool networkNamespaces = false;
	bool cgroupV2 = false;
};

LinuxIsolationSupport ProbeLinuxIsolation ();
}

#endif
