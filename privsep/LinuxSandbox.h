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

LinuxSandboxProfile GetLinuxSandboxProfile (WorkerClass workerClass);
i2p::expected<void, std::string> ApplyLinuxSandbox (const LinuxSandboxProfile & profile);

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
 * it only reports which mechanisms the running kernel can enforce.
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
