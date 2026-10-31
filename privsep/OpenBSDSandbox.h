/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: OpenBSDSandbox.
 *
 * Declares the OpenBSD role-based policy model: per-role pledge
 * promises, explicit unveil rules, the router core's own
 * startup/runtime pledge windows and the single validated
 * `openbsd.pledge_file` parser. Builders reject malformed paths and
 * never add implicit broad rules.
 */

#ifndef I2PD_PRIVSEP_OPENBSD_SANDBOX_H__
#define I2PD_PRIVSEP_OPENBSD_SANDBOX_H__

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Expected.h"

namespace i2p::privsep
{
/**
 * Worker roles for the OpenBSD native sandbox. These are capability
 * classes, not a claim that every backend has already been moved out of
 * the router core. The policy builder is deliberately independent of
 * the IPC message codec so a worker can apply the policy before
 * entering its service loop.
 * Log, Storage, StorageBroker and Supervisor match processes that exist
 * today (the log worker, the data/router/torrent storage workers and
 * the pre-pledge broker). Network, Crypto and Admin are
 * policy-builder-only: no process claims those roles yet, so a
 * reviewable policy row is not a backend.
 */
enum class OpenBSDWorkerRole : uint8_t
{
	Supervisor = 0,
	Log,
	Storage,
	Network,
	Crypto,
	Admin,
	/**
	 * Pre-pledge torrent-storage broker. It forks/execs delegated storage
	 * workers and forwards exactly one channel descriptor per worker with
	 * sendfd. It never receives a descriptor, so it holds neither recvfd
	 * nor unix; an unexpected descriptor is a fatal frame fault
	 * (fail-closed).
	 */
	StorageBroker
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
	// Configured storage workers need only their inherited channel, no new
	// UNIX connections or descriptor reception after bootstrap.
	bool preopenedStorageIpc = false;

	/**
	 * Absolute path of the executable the supervisor/broker roles may
	 * execve.
	 * Supervisor and StorageBroker policies add exactly one unveil rule
	 * for this path with "x" and nothing else, so a compromised sandbox
	 * cannot start an arbitrary image. The value is resolved by the caller
	 * before the policy is built and is reused for every replacement
	 * spawn.
	 */
	std::string executablePath;

	/**
	 * The spawn helper drops supplementary groups, gid and uid in the
	 * forked child before execve. Process-creation roles need the "id"
	 * promise for that single call sequence; it is never part of a leaf
	 * worker policy.
	 */
	bool dropCredentials = false;

	/** Absolute data/certificate roots used by the storage policy. */
	std::string dataDir;
	std::string certsDir;

	/** Log-worker configuration. */
	std::string logPath;
	bool logToSyslog = false;
	bool preopenedLog = false;

	/** Network worker may request mcast only for explicit UPnP support. */
	bool upnp = false;
	/** Future transport worker receives sockets over SCM_RIGHTS. */
	bool socketHandoff = false;
	/** Worker needs route/interface data for MTU/address probing. */
	bool routeInfo = false;
	/**
	 * Proxy-less deployments may disable DNS; default preserves current
	 * policy. */
	bool dnsLookup = true;

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

	/** True for inet/dns/mcast/route-style network promises. */
	bool HasExternalNetwork () const;
	/** True for wpath/cpath/fattr. */
	bool HasPersistentWrite () const;
	/** True for proc/exec. */
	bool HasProcessCreation () const;
	/**
	 * Local UNIX-socket IPC only; this is not external network
	 * capability. */
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
 * pledge(2) is always called with a null execpromises argument, pinned
 * by `OPENBSD_PLEDGE_EXECPROMISES`: an exec'ed image therefore starts
 * without any pledge promise, so every exec'ed worker must install its
 * own unveil rules and pledge before it serves a frame. This code never
 * relies on pledge or unveil state surviving execve, and it never sets
 * execpromises.
 * @return An error when any rule or the pledge itself fails, and on a
 *         non-OpenBSD build. A caller must abort the worker; continuing
 *         would mean serving frames with no sandbox at all.
 */
i2p::expected<void, std::string> ApplyOpenBSDPolicy (const OpenBSDPolicy & policy);

/**
 * Pin, not a configuration knob: the execpromises argument of every
 * pledge(2) call in this component. A non-null value would let a child
 * inherit promises that no policy builder validated.
 */
inline constexpr const char * OPENBSD_PLEDGE_EXECPROMISES = nullptr;

/** Startup or runtime phase of the router core's own pledge. */
enum class OpenBSDPledgePhase : uint8_t
{
	/**
	 * Before the first fork/exec and before unveil rules are installed.
	 * This window is the widest set the core ever holds; it is installed
	 * once and can only be reduced afterwards.
	 */
	Startup,
	/** After every worker is spawned and the unveil list is locked. */
	Runtime
};

/**
 * Which optional promises a pledge window needs. The router core is not
 * a worker: these flags describe the daemon's own startup/runtime
 * requirements.
 */
struct OpenBSDPledgeWindow
{
	/** The core installs unveil rules here during startup. */
	bool unveil = false;
	/** Multicast discovery is enabled for UPnP (both windows). */
	bool upnp = false;
	/** The core forks and execs workers in this window (startup only). */
	bool execWorkers = false;
	/**
	 * The core reaps/signals an owned supervisor without exec (runtime
	 * only). */
	bool reapChildren = false;
	/** The core receives worker channels/roots by descriptor (recvfd). */
	bool descriptorReception = false;
	/**
	 * A forked child drops uid/gid before execve (startup only, needs
	 * id). */
	bool credentialDrop = false;
};

/**
 * The fixed promise list for one phase, in canonical order.
 *
 * The runtime list is always a subset of the startup list: pledge(2)
 * can only remove promises, so a runtime-only promise is silently
 * ignored by the kernel and the first use of that subsystem aborts the
 * core. `IsPledgePromiseSubset` pins the relationship and both call
 * sites use these builders.
 */
std::vector<std::string> OpenBSDPledgePromiseList (OpenBSDPledgePhase phase, const OpenBSDPledgeWindow & window);
/**
 * Join `OpenBSDPledgePromiseList` into the space-separated pledge
 * argument. */
std::string BuildOpenBSDPledgePromises (OpenBSDPledgePhase phase, const OpenBSDPledgeWindow & window);

/** True when every promise in `subset` is present in `superset`. */
bool IsPledgePromiseSubset (std::string_view subset, std::string_view superset);

/**
 * Parse and validate an operator `openbsd.pledge_file`.
 *
 * Rules, all of them deliberately fail-closed:
 *  - the file is bounded (64 KiB) and so is every token;
 *  - only space/tab-separated promise names are accepted;
 * - comments and blank lines are rejected, so a commented-out promise
 * can never look active;
 *  - unknown names are rejected;
 * - a token that is not already in @a allowedPromises is rejected: a
 * policy file may select from the validated base set, never widen it;
 * - the runtime phase additionally rejects process, identity and
 * privilege promises (`proc`, `exec`, `id`, ...), even when the base
 * set contains them, because the core must not be able to re-acquire
 * process creation; - duplicates are ignored.
 *
 * The returned list contains the accepted tokens without duplicates.
 * Errors are bounded, non-secret diagnostics suitable for a startup
 * message.
 */
i2p::expected<std::vector<std::string>, std::string> ParseOpenBSDPledgeFile (
	const std::string & path, std::span<const std::string_view> allowedPromises, OpenBSDPledgePhase phase);

/**
 * Open a directory for descriptor passing (SCM_RIGHTS). The descriptor
 * is close-on-exec, non-symlink and checked to be a directory. The
 * worker still applies the storage policy; this helper only makes the
 * pre-opened object explicit.
 */
i2p::expected<int, std::string> OpenPreopenedDirectory (std::string_view path);

/**
 * Validate a descriptor received through SCM_RIGHTS before it is used.
 * It must be close-on-exec and either a regular file or a directory.
 */
i2p::expected<void, std::string> ValidatePreopenedDescriptor (int fd, bool directory);

/**
 * Validate a socket received through SCM_RIGHTS before it is used by a
 * network/transport worker: close-on-exec, expected SOCK_* type and,
 * for a listening stream socket, SO_ACCEPTCONN.
 */
i2p::expected<void, std::string> ValidatePreopenedSocket (int fd, int expectedType, bool requireListening);

/**
 * Compile-time/runtime policy checks used by diagnostics and CI. They
 * inspect the policy text and never weaken or apply a policy
 * themselves.
 */
bool OpenBSDPolicyAllowsExternalNetwork (const OpenBSDPolicy & policy);
bool OpenBSDPolicyAllowsPersistentWrite (const OpenBSDPolicy & policy);
}

#endif
