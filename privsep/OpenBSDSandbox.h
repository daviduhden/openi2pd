/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: OpenBSDSandbox.
 *
 * Declares the OpenBSD role-based policy model: per-role pledge promises,
 * explicit unveil rules and validation of pre-opened descriptors. Builders
 * reject malformed paths and never add implicit broad rules.
 *
 */

#ifndef I2PD_PRIVSEP_OPENBSD_SANDBOX_H__
#define I2PD_PRIVSEP_OPENBSD_SANDBOX_H__

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "Expected.h"

namespace i2p::privsep
{
/**
 * Worker roles for the OpenBSD native sandbox. These are capability classes,
 * not a claim that every backend has already been moved out of the router
 * core. The policy builder is deliberately independent of the IPC message
 * codec so a worker can apply the policy before entering its service loop.
 */
enum class OpenBSDWorkerRole : uint8_t
{
	Supervisor = 0,
	Log,
	Storage,
	Network,
	Crypto,
	Admin
};

struct OpenBSDUnveilRule
{
	std::string path;
	std::string permissions;
};

struct OpenBSDPolicyConfig
{
	bool useUnveil = true;
	bool usePledge = true;

	/** Absolute data/certificate roots used by the storage policy. */
	std::string dataDir;
	std::string certsDir;

	/** Log-worker configuration. */
	std::string logPath;
	bool logToSyslog = false;
	bool preopenedLog = false;

	/** Network worker may request mcast only for the explicit UPnP feature. */
	bool upnp = false;

	/**
	 * Extra paths are explicit configuration, never an implicit current
	 * directory rule. They are used only for the storage policy.
	 */
	std::vector<std::string> extraReadPaths;
	std::vector<std::string> extraWritePaths;
};

struct OpenBSDPolicy
{
	OpenBSDWorkerRole role = OpenBSDWorkerRole::Admin;
	bool useUnveil = true;
	bool usePledge = true;
	std::string pledge;
	std::vector<OpenBSDUnveilRule> unveil;

	/** True for inet/dns/mcast/route-style external networking promises. */
	bool HasExternalNetwork () const;
	/** True for wpath/cpath/fattr. */
	bool HasPersistentWrite () const;
	/** True for proc/exec. */
	bool HasProcessCreation () const;
	/** Local UNIX-socket IPC only; this is not external network capability. */
	bool HasLocalIpc () const;
};

const char * OpenBSDWorkerRoleName (OpenBSDWorkerRole role);

/**
 * Build a least-privilege policy for a role.
 *
 * The caller supplies only paths it actually owns. `BuildOpenBSDPolicy`
 * rejects NUL/empty rules and never adds an implicit "." or "/" rule.
 */
i2p::expected<OpenBSDPolicy, std::string> BuildOpenBSDPolicy (
	OpenBSDWorkerRole role, const OpenBSDPolicyConfig & config);

/**
 * Apply unveil rules, lock the unveil list and then install pledge.
 *
 * pledge(2) is called with a null execpromises argument. Pledge and unveil
 * state are re-established independently in every exec'ed worker; this code
 * never relies on either surviving execve or on execpromises.
 */
i2p::expected<void, std::string> ApplyOpenBSDPolicy (const OpenBSDPolicy & policy);

/**
 * Open a directory for descriptor passing (SCM_RIGHTS). The descriptor is
 * close-on-exec, non-symlink and checked to be a directory. The worker still
 * applies the storage policy; this helper only makes the pre-opened object
 * explicit.
 */
i2p::expected<int, std::string> OpenPreopenedDirectory (std::string_view path);

/**
 * Validate a descriptor received through SCM_RIGHTS before it is used. It must
 * be close-on-exec and either a regular file or a directory.
 */
i2p::expected<void, std::string> ValidatePreopenedDescriptor (int fd, bool directory);

/**
 * Compile-time/runtime policy checks used by diagnostics and CI. They inspect
 * the policy text and never weaken or apply a policy themselves.
 */
bool OpenBSDPolicyAllowsExternalNetwork (const OpenBSDPolicy & policy);
bool OpenBSDPolicyAllowsPersistentWrite (const OpenBSDPolicy & policy);
}

#endif
