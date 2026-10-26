/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: LinuxSandbox.
 *
 * Implements least-privilege Linux sandboxing: no-new-privs, capability
 * drop, optional network namespace, Landlock path and network rules, a seccomp
 * socket filter for AF_INET/AF_INET6 or all socket creation, and optional
 * cgroup-v2 limits. Non-Linux targets return explicit unavailability errors.
 *
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "LinuxSandbox.h"

#ifndef __linux__

namespace i2p::privsep
{
LinuxSandboxProfile GetLinuxSandboxProfile (WorkerClass)
{
	return { };
}

i2p::expected<void, std::string> ApplyLinuxSandbox (const LinuxSandboxProfile &)
{
	return i2p::unexpected ("Linux sandbox is not available on this platform");
}

i2p::expected<void, std::string> ApplyLinuxSupervisorSandbox ()
{
	return i2p::unexpected ("Linux sandbox is not available on this platform");
}

i2p::expected<void, std::string> ApplyLinuxCgroupV2Limits (const LinuxCgroupV2Limits &)
{
	return i2p::unexpected ("cgroup v2 is not available on this platform");
}

i2p::expected<void, std::string> PrepareCapabilitiesBeforeCredentialDrop ()
{
	return i2p::unexpected ("Linux capabilities are not available on this platform");
}

i2p::expected<void, std::string> ClearCapabilitiesAfterCredentialDrop ()
{
	return i2p::unexpected ("Linux capabilities are not available on this platform");
}

i2p::expected<void, std::string> ApplyLinuxIpcLockdown (const LinuxSandboxProfile &)
{
	return i2p::unexpected ("Linux sandbox is not available on this platform");
}

i2p::expected<void, std::string> VerifyWorkerPrivileges ()
{
	return i2p::unexpected ("Linux privileges are not available on this platform");
}

LinuxIsolationSupport ProbeLinuxIsolation ()
{
	return { };
}
}

#else

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <fcntl.h>
#include <string_view>
#include <linux/audit.h>
#include <linux/capability.h>
#include <linux/filter.h>
#if __has_include(<linux/landlock.h>)
#include <linux/landlock.h>
#define I2PD_LANDLOCK_HEADERS_AVAILABLE 1
#endif
#include <linux/magic.h>
#include <linux/seccomp.h>
#include <sched.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/vfs.h>
#include <unistd.h>

#include <vector>

namespace i2p::privsep
{
#ifndef CGROUP2_SUPER_MAGIC
#define CGROUP2_SUPER_MAGIC 0x63677270
#endif
// Landlock ABI 10 adds UDP access rights. Only request the bits exposed by the
// build headers; older distribution headers can otherwise make the kernel
// reject the whole ruleset with EINVAL. Profiles that need UDP confinement
// additionally deny socket creation/Internet sockets through seccomp.
#if defined(LANDLOCK_ACCESS_NET_BIND_UDP) && defined(LANDLOCK_ACCESS_NET_CONNECT_SEND_UDP)
#define I2PD_LANDLOCK_HAS_UDP 1
#endif
namespace
{
	/** A process installs its socket filter at most once (filters stack). */
	bool g_SeccompInstalled = false;
#ifdef I2PD_LANDLOCK_HEADERS_AVAILABLE
	// Linux UAPI linux/landlock.h: ABI 2 adds REFER (bit 13), ABI 3 adds
	// TRUNCATE (bit 14), and ABI 4 appends handled_access_net with TCP bits
	// 0 (bind) and 1 (connect). Keep the exact extensible syscall layout here
	// so older distribution headers do not dictate the running kernel's policy.
	// The kernel accepts a zero extension on older ABIs; masks below include
	// each right only when the probed kernel ABI supports it.
	struct LandlockRulesetAttributes
	{
		uint64_t handled_access_fs = 0;
		uint64_t handled_access_net = 0;
	};
	static_assert (sizeof (LandlockRulesetAttributes) == 16);
	static_assert (offsetof (LandlockRulesetAttributes, handled_access_net) == 8);
	constexpr uint64_t LandlockFsRefer = uint64_t { 1 } << 13;
	constexpr uint64_t LandlockFsTruncate = uint64_t { 1 } << 14;
	constexpr uint64_t LandlockNetBindTcp = uint64_t { 1 } << 0;
	constexpr uint64_t LandlockNetConnectTcp = uint64_t { 1 } << 1;
	static_assert (
		offsetof (landlock_ruleset_attr, handled_access_fs) == offsetof (LandlockRulesetAttributes, handled_access_fs));
#ifdef LANDLOCK_ACCESS_FS_REFER
	static_assert (LandlockFsRefer == LANDLOCK_ACCESS_FS_REFER);
#endif
#ifdef LANDLOCK_ACCESS_FS_TRUNCATE
	static_assert (LandlockFsTruncate == LANDLOCK_ACCESS_FS_TRUNCATE);
#endif
#if defined(LANDLOCK_ACCESS_NET_BIND_TCP) && defined(LANDLOCK_ACCESS_NET_CONNECT_TCP)
	static_assert (LandlockNetBindTcp == LANDLOCK_ACCESS_NET_BIND_TCP);
	static_assert (LandlockNetConnectTcp == LANDLOCK_ACCESS_NET_CONNECT_TCP);
	static_assert (offsetof (landlock_ruleset_attr, handled_access_net)
		== offsetof (LandlockRulesetAttributes, handled_access_net));
#endif

	long LandlockCreateRuleset (void * attr, size_t size, uint32_t flags)
	{
#ifdef SYS_landlock_create_ruleset
		return syscall (SYS_landlock_create_ruleset, attr, size, flags);
#else
		(void)attr;
		(void)size;
		(void)flags;
		errno = ENOSYS;
		return -1;
#endif
	}

	long LandlockAddRule (int ruleset, int ruleType, const void * attr, uint32_t flags)
	{
#ifdef SYS_landlock_add_rule
		return syscall (SYS_landlock_add_rule, ruleset, ruleType, attr, flags);
#else
		(void)ruleset;
		(void)ruleType;
		(void)attr;
		(void)flags;
		errno = ENOSYS;
		return -1;
#endif
	}

	long LandlockRestrictSelf (int ruleset, uint32_t flags)
	{
#ifdef SYS_landlock_restrict_self
		return syscall (SYS_landlock_restrict_self, ruleset, flags);
#else
		(void)ruleset;
		(void)flags;
		errno = ENOSYS;
		return -1;
#endif
	}

	uint64_t FilesystemWriteMask (long abi)
	{
		// Only rights understood by the running ABI are requested. Including
		// an unknown bit makes landlock_create_ruleset fail with EINVAL.
		// EXECUTE is handled but no worker rule grants it: a Landlock-restricted
		// worker must not exec a helper binary. It is excluded from the rule
		// mask below so writable roots do not become executable.
		uint64_t mask = LANDLOCK_ACCESS_FS_EXECUTE | LANDLOCK_ACCESS_FS_WRITE_FILE | LANDLOCK_ACCESS_FS_REMOVE_DIR
			| LANDLOCK_ACCESS_FS_REMOVE_FILE | LANDLOCK_ACCESS_FS_MAKE_CHAR | LANDLOCK_ACCESS_FS_MAKE_DIR
			| LANDLOCK_ACCESS_FS_MAKE_REG | LANDLOCK_ACCESS_FS_MAKE_SOCK | LANDLOCK_ACCESS_FS_MAKE_FIFO
			| LANDLOCK_ACCESS_FS_MAKE_BLOCK | LANDLOCK_ACCESS_FS_MAKE_SYM;
		if (abi >= 2) mask |= LandlockFsRefer;
		if (abi >= 3) mask |= LandlockFsTruncate;
		return mask;
	}

	uint64_t NetworkMask (long abi)
	{
		// TCP was added in ABI 4 and UDP in ABI 10. On an older kernel the
		// caller must either deny socket creation (storage) or explicitly
		// accept that Landlock cannot constrain UDP.
		uint64_t mask = 0;
		if (abi >= 4) mask |= LandlockNetBindTcp | LandlockNetConnectTcp;
#ifdef I2PD_LANDLOCK_HAS_UDP
		if (abi >= 10) mask |= LANDLOCK_ACCESS_NET_BIND_UDP | LANDLOCK_ACCESS_NET_CONNECT_SEND_UDP;
#endif
		return mask;
	}
#endif

	i2p::expected<void, std::string> DropCapabilities ()
	{
		struct __user_cap_header_struct header { _LINUX_CAPABILITY_VERSION_3, 0 };
		struct __user_cap_data_struct data[2] { };
		if (syscall (SYS_capset, &header, data) != 0)
		{
			if (geteuid () == 0) return i2p::unexpected (std::string ("capset: ") + strerror (errno));
			// An unprivileged process has no capability set to drop; EPERM
			// is expected and must not make the worker unusable.
			if (errno != EPERM) return i2p::unexpected (std::string ("capset: ") + strerror (errno));
		}
#ifdef PR_CAPBSET_DROP
		// Clearing the effective/permitted set is not enough for a root worker:
		// remove every capability from the bounding set as well so a future
		// exec/file capability cannot restore it. EPERM is normal for an
		// unprivileged process; EINVAL means the capability number is unknown
		// on this kernel.
		for (int capability = 0; capability <= 63; ++capability)
		{
			if (prctl (PR_CAPBSET_DROP, capability, 0, 0, 0) != 0 && errno != EPERM && errno != EINVAL)
				return i2p::unexpected (std::string ("PR_CAPBSET_DROP: ") + strerror (errno));
		}
#endif
#if defined(PR_CAP_AMBIENT) && defined(PR_CAP_AMBIENT_CLEAR_ALL)
		if (prctl (PR_CAP_AMBIENT, PR_CAP_AMBIENT_CLEAR_ALL, 0, 0, 0) != 0 && errno != EINVAL && errno != EPERM)
			return i2p::unexpected (std::string ("PR_CAP_AMBIENT: ") + strerror (errno));
#endif
		return { };
	}

	unsigned long SeccompArchitecture ()
	{
#if defined(__x86_64__)
		return AUDIT_ARCH_X86_64;
#elif defined(__aarch64__)
		return AUDIT_ARCH_AARCH64;
#else
		return 0;
#endif
	}

	i2p::expected<void, std::string> InstallSocketFilter (bool denyAll)
	{
		const auto arch = SeccompArchitecture ();
		if (!arch) return i2p::unexpected ("seccomp architecture is not supported");
		const uint32_t deny = SECCOMP_RET_ERRNO | (EACCES & SECCOMP_RET_DATA);
		std::vector<sock_filter> filter {
			BPF_STMT (BPF_LD | BPF_W | BPF_ABS, static_cast<uint32_t> (offsetof (struct seccomp_data, arch))),
			BPF_JUMP (BPF_JMP | BPF_JEQ | BPF_K, static_cast<uint32_t> (arch), 1, 0),
			BPF_STMT (BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
			BPF_STMT (BPF_LD | BPF_W | BPF_ABS, static_cast<uint32_t> (offsetof (struct seccomp_data, nr))),
		};
		// x32 shares AUDIT_ARCH_X86_64 but adds a syscall-number bit. Do not
		// let that alternate socket ABI bypass the native-number checks.
#if defined(__x86_64__)
		filter.push_back (BPF_JUMP (BPF_JMP | BPF_JSET | BPF_K, 0x40000000U, 0, 1));
		filter.push_back (BPF_STMT (BPF_RET | BPF_K, deny));
#endif
		// io_uring can create sockets without SYS_socket; descriptor stealing
		// likewise must not restore network authority to a storage process.
#ifdef SYS_io_uring_setup
		filter.push_back (BPF_JUMP (BPF_JMP | BPF_JEQ | BPF_K, SYS_io_uring_setup, 0, 1));
		filter.push_back (BPF_STMT (BPF_RET | BPF_K, deny));
#endif
#ifdef SYS_pidfd_getfd
		filter.push_back (BPF_JUMP (BPF_JMP | BPF_JEQ | BPF_K, SYS_pidfd_getfd, 0, 1));
		filter.push_back (BPF_STMT (BPF_RET | BPF_K, deny));
#endif
#ifdef SYS_ptrace
		filter.push_back (BPF_JUMP (BPF_JMP | BPF_JEQ | BPF_K, SYS_ptrace, 0, 1));
		filter.push_back (BPF_STMT (BPF_RET | BPF_K, deny));
#endif
		auto denySyscall = [&filter] (int syscallNumber)
		{
			filter.push_back (BPF_JUMP (BPF_JMP | BPF_JEQ | BPF_K, static_cast<uint32_t> (syscallNumber), 0, 1));
			filter.push_back (BPF_STMT (BPF_RET | BPF_K, deny));
		};
		// Block the descriptor/process-memory side channels that could undo
		// a storage worker's filesystem/network split against a same-UID core.
#ifdef SYS_process_vm_readv
		denySyscall (SYS_process_vm_readv);
#endif
#ifdef SYS_process_vm_writev
		denySyscall (SYS_process_vm_writev);
#endif
#ifdef SYS_kcmp
		denySyscall (SYS_kcmp);
#endif
#ifdef SYS_pidfd_open
		denySyscall (SYS_pidfd_open);
#endif
#ifdef SYS_pidfd_send_signal
		denySyscall (SYS_pidfd_send_signal);
#endif
#ifdef SYS_open_by_handle_at
		denySyscall (SYS_open_by_handle_at);
#endif
#ifdef SYS_name_to_handle_at
		denySyscall (SYS_name_to_handle_at);
#endif
#ifdef SYS_userfaultfd
		denySyscall (SYS_userfaultfd);
#endif
#ifdef SYS_bpf
		denySyscall (SYS_bpf);
#endif
#ifdef SYS_perf_event_open
		denySyscall (SYS_perf_event_open);
#endif
		if (denyAll)
		{
			filter.push_back (BPF_JUMP (BPF_JMP | BPF_JEQ | BPF_K, SYS_socket, 0, 1));
			filter.push_back (BPF_STMT (BPF_RET | BPF_K, deny));
			filter.push_back (BPF_JUMP (BPF_JMP | BPF_JEQ | BPF_K, SYS_socketpair, 0, 1));
			filter.push_back (BPF_STMT (BPF_RET | BPF_K, deny));
			// A storage worker has no new peer: deny the connection lifecycle on
			// any inherited or later-received socket as well as process creation.
#ifdef SYS_connect
			denySyscall (SYS_connect);
#endif
#ifdef SYS_bind
			denySyscall (SYS_bind);
#endif
#ifdef SYS_listen
			denySyscall (SYS_listen);
#endif
#ifdef SYS_accept
			denySyscall (SYS_accept);
#endif
#ifdef SYS_accept4
			denySyscall (SYS_accept4);
#endif
#ifdef SYS_sendto
			denySyscall (SYS_sendto);
#endif
#ifdef SYS_execve
			denySyscall (SYS_execve);
#endif
#ifdef SYS_execveat
			denySyscall (SYS_execveat);
#endif
#ifdef SYS_fork
			denySyscall (SYS_fork);
#endif
#ifdef SYS_vfork
			denySyscall (SYS_vfork);
#endif
#ifdef SYS_clone
			denySyscall (SYS_clone);
#endif
#ifdef SYS_clone3
			denySyscall (SYS_clone3);
#endif
		}
		else
		{
			// AF_INET/AF_INET6 are rejected; AF_UNIX (IPC/syslog) is allowed.
			// The socketpair syscall is left untouched because it is AF_UNIX.
			filter.push_back (BPF_JUMP (BPF_JMP | BPF_JEQ | BPF_K, SYS_socket, 0, 5));
			filter.push_back (
				BPF_STMT (BPF_LD | BPF_W | BPF_ABS, static_cast<uint32_t> (offsetof (struct seccomp_data, args))));
			filter.push_back (BPF_JUMP (BPF_JMP | BPF_JEQ | BPF_K, AF_INET, 0, 1));
			filter.push_back (BPF_STMT (BPF_RET | BPF_K, deny));
			filter.push_back (BPF_JUMP (BPF_JMP | BPF_JEQ | BPF_K, AF_INET6, 0, 1));
			filter.push_back (BPF_STMT (BPF_RET | BPF_K, deny));
		}
		filter.push_back (BPF_STMT (BPF_RET | BPF_K, SECCOMP_RET_ALLOW));
		struct sock_fprog program { static_cast<unsigned short> (filter.size ()), filter.data () };
		if (prctl (PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program) != 0)
			return i2p::unexpected (std::string ("PR_SET_SECCOMP: ") + strerror (errno));
		g_SeccompInstalled = true;
		return { };
	}

	i2p::expected<void, std::string> ApplyLandlock (const LinuxSandboxProfile & profile)
	{
#ifndef I2PD_LANDLOCK_HEADERS_AVAILABLE
		(void)profile;
		// Missing build headers must never turn a requested boundary into an
		// unenforced success, even when optional kernel probing was requested.
		return i2p::unexpected ("Landlock build headers are unavailable");
#else
		const long abi = LandlockCreateRuleset (nullptr, 0, LANDLOCK_CREATE_RULESET_VERSION);
		if (abi < 0)
		{
			if (profile.requireLandlock)
				return i2p::unexpected (std::string ("landlock_create_ruleset(version): ") + strerror (errno));
			return { };
		}
		LandlockRulesetAttributes ruleset;
		ruleset.handled_access_fs = FilesystemWriteMask (abi);
		if (profile.restrictFilesystemReads)
			ruleset.handled_access_fs |= LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_READ_DIR;
		if (!profile.allowNetwork) ruleset.handled_access_net = NetworkMask (abi);
		if (!ruleset.handled_access_fs && !ruleset.handled_access_net) return { };

		const int rulesetFd = static_cast<int> (LandlockCreateRuleset (&ruleset, sizeof (ruleset), 0));
		if (rulesetFd < 0) return i2p::unexpected (std::string ("landlock_create_ruleset: ") + strerror (errno));

		bool ok = true;
		if (profile.allowPersistentWrites)
		{
			for (const auto & path : profile.writablePaths)
			{
				if (path.empty () || path.front () != '/' || path.find ('\0') != std::string::npos)
				{
					ok = false;
					errno = EINVAL;
					break;
				}
				int pathFd = open (path.c_str (), O_PATH | O_CLOEXEC);
				if (pathFd < 0)
				{
					ok = false;
					break;
				}
				struct stat pathStat { };
				if (fstat (pathFd, &pathStat) != 0)
				{
					ok = false;
					close (pathFd);
					break;
				}
				struct landlock_path_beneath_attr beneath { };
				// Never grant EXECUTE through a writable root.
				beneath.allowed_access = ruleset.handled_access_fs & ~LANDLOCK_ACCESS_FS_EXECUTE;
				if (S_ISREG (pathStat.st_mode))
				{
					// Directory-only rights (make/remove/refer) are invalid for a
					// regular file and make landlock_add_rule fail with EINVAL.
					beneath.allowed_access = LANDLOCK_ACCESS_FS_WRITE_FILE;
					if (profile.restrictFilesystemReads) beneath.allowed_access |= LANDLOCK_ACCESS_FS_READ_FILE;
					if (abi >= 3) beneath.allowed_access |= LandlockFsTruncate;
				}
				beneath.parent_fd = pathFd;
				if (LandlockAddRule (rulesetFd, LANDLOCK_RULE_PATH_BENEATH, &beneath, 0) != 0)
				{
					ok = false;
					close (pathFd);
					break;
				}
				close (pathFd);
			}
		}
		if (ok && LandlockRestrictSelf (rulesetFd, 0) != 0) ok = false;
		const int savedErrno = errno;
		close (rulesetFd);
		if (!ok)
		{
			errno = savedErrno;
			return i2p::unexpected (std::string ("landlock restriction: ") + strerror (errno));
		}
		return { };
#endif
	}
}

LinuxSandboxProfile GetLinuxSandboxProfile (WorkerClass workerClass)
{
	LinuxSandboxProfile profile;
	switch (workerClass)
	{
	case WorkerClass::Log:
		// A file log is expected to use a pre-opened descriptor. Syslog
		// still needs AF_UNIX, but AF_INET/AF_INET6 are never required.
		profile.allowNetwork = false;
		profile.allowPersistentWrites = false;
		profile.denyInetSockets = true;
		break;
	case WorkerClass::Storage:
		// No new socket of any kind, no external network namespace, and
		// writes only to the explicitly delegated paths.
		profile.allowNetwork = false;
		profile.allowPersistentWrites = true;
		profile.denySocketCreation = true;
		// A storage worker reads only the delegated root and the files inside
		// it; do not leave this to each call site.
		profile.restrictFilesystemReads = true;
		// The namespace, when available, is installed by the spawn child while
		// it is still privileged. It is best-effort; seccomp is the boundary.
		profile.isolateNetworkNamespace = false;
		break;
	case WorkerClass::Network:
		// Landlock/seccomp deny persistent writes; sockets are the only
		// external capability this worker is allowed to have.
		profile.allowNetwork = true;
		profile.allowPersistentWrites = false;
		break;
	case WorkerClass::Crypto:
	case WorkerClass::Admin:
	case WorkerClass::Supervisor:
		profile.allowNetwork = false;
		profile.allowPersistentWrites = false;
		profile.isolateNetworkNamespace = true;
		// The supervisor needs a fresh AF_UNIX socketpair for each log
		// child; other roles do not need any socket.
		if (workerClass == WorkerClass::Supervisor)
			profile.denyInetSockets = true;
		else
			profile.denySocketCreation = true;
		break;
	}
	return profile;
}

i2p::expected<void, std::string> PrepareCapabilitiesBeforeCredentialDrop ()
{
	const bool privileged = ::geteuid () == 0;
#if defined(PR_CAP_AMBIENT) && defined(PR_CAP_AMBIENT_CLEAR_ALL)
	if (prctl (PR_CAP_AMBIENT, PR_CAP_AMBIENT_CLEAR_ALL, 0, 0, 0) != 0 && errno != EINVAL && errno != EPERM)
		return i2p::unexpected (std::string ("PR_CAP_AMBIENT_CLEAR_ALL: ") + strerror (errno));
#endif
#ifdef PR_CAPBSET_DROP
	for (int capability = 0; capability <= 63; ++capability)
	{
		if (prctl (PR_CAPBSET_DROP, capability, 0, 0, 0) == 0) continue;
		const int error = errno;
		if (error == EINVAL) continue; // capability number unknown on this kernel
		if (error == EPERM && !privileged) break; // unprivileged: set is already empty
		return i2p::unexpected (std::string ("PR_CAPBSET_DROP: ") + strerror (error));
	}
#endif
	// Effective/permitted are intentionally kept: setgroups/setgid/setuid
	// below still need CAP_SETGID/CAP_SETUID when the core is root.
	return { };
}

i2p::expected<void, std::string> ClearCapabilitiesAfterCredentialDrop ()
{
	struct __user_cap_header_struct header { _LINUX_CAPABILITY_VERSION_3, 0 };
	struct __user_cap_data_struct data[2] { };
	const bool privileged = ::geteuid () == 0;
	if (syscall (SYS_capset, &header, data) != 0 && (privileged || errno != EPERM))
		return i2p::unexpected (std::string ("capset: ") + strerror (errno));
	struct __user_cap_data_struct current[2] { };
	if (syscall (SYS_capget, &header, current) != 0)
		return i2p::unexpected (std::string ("capget: ") + strerror (errno));
	if (current[0].effective || current[0].permitted || current[0].inheritable || current[1].effective
		|| current[1].permitted || current[1].inheritable)
		return i2p::unexpected ("worker capabilities are not empty");
	return { };
}

i2p::expected<void, std::string> VerifyWorkerPrivileges ()
{
	if (prctl (PR_GET_NO_NEW_PRIVS, 0, 0, 0, 0) != 1) return i2p::unexpected ("worker did not set PR_SET_NO_NEW_PRIVS");
	struct __user_cap_header_struct header { _LINUX_CAPABILITY_VERSION_3, 0 };
	struct __user_cap_data_struct data[2] { };
	if (syscall (SYS_capget, &header, data) != 0) return i2p::unexpected (std::string ("capget: ") + strerror (errno));
	if (data[0].effective || data[0].permitted || data[0].inheritable || data[1].effective || data[1].permitted
		|| data[1].inheritable)
		return i2p::unexpected ("worker capabilities are not empty");
	return { };
}

/**
 * Install the pre-configuration half of a worker profile: no-new-privs, an
 * empty capability set and the role's socket/Internet seccomp filter. It is
 * called before the first frame is parsed because it needs no delegated root;
 * Landlock, which does, is installed later by ApplyLinuxSandbox().
 *
 * The two mutual-exclusion checks are policy assertions, not runtime probes:
 * a profile that asks for both network and persistent writes, or for both
 * "all sockets" and "Internet sockets only", is a programming error.
 */
i2p::expected<void, std::string> ApplyLinuxIpcLockdown (const LinuxSandboxProfile & profile)
{
	if (profile.allowNetwork && profile.allowPersistentWrites)
		return i2p::unexpected ("a worker cannot have both network and persistent-write capabilities");
	if (profile.denySocketCreation && profile.denyInetSockets)
		return i2p::unexpected ("a worker cannot both deny all and deny only Internet sockets");
	if (prctl (PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0)
		return i2p::unexpected (std::string ("PR_SET_NO_NEW_PRIVS: ") + strerror (errno));
	if (auto caps = DropCapabilities (); !caps) return caps;
	if ((profile.denySocketCreation || profile.denyInetSockets) && !g_SeccompInstalled)
	{
		auto seccomp = InstallSocketFilter (profile.denySocketCreation);
		if (!seccomp) return seccomp;
	}
	return { };
}

/**
 * Install a complete worker profile. The order is deliberate: no-new-privs
 * first (so nothing later can regain privilege), then the optional empty
 * network namespace, then the capability drop, then Landlock (which needs the
 * delegated paths to exist), then seccomp. A seccomp filter cannot be removed
 * and Landlock can only tighten, so a partially applied profile fails closed by
 * returning an error rather than serving with the remaining steps skipped.
 *
 * A best-effort namespace is not a boundary claim: when unshare(CLONE_NEWNET)
 * fails without requireNetworkNamespace, seccomp remains the enforced network
 * policy for that worker.
 */
i2p::expected<void, std::string> ApplyLinuxSandbox (const LinuxSandboxProfile & profile)
{
	if (profile.allowNetwork && profile.allowPersistentWrites)
		return i2p::unexpected ("a worker cannot have both network and persistent-write capabilities");
	if (profile.denySocketCreation && profile.denyInetSockets)
		return i2p::unexpected ("a worker cannot both deny all and deny only Internet sockets");
	if (prctl (PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0)
		return i2p::unexpected (std::string ("PR_SET_NO_NEW_PRIVS: ") + strerror (errno));
	if (profile.isolateNetworkNamespace)
	{
#ifdef CLONE_NEWNET
		if (unshare (CLONE_NEWNET) != 0 && profile.requireNetworkNamespace)
			return i2p::unexpected (std::string ("unshare(CLONE_NEWNET): ") + strerror (errno));
#else
		if (profile.requireNetworkNamespace) return i2p::unexpected ("CLONE_NEWNET is not available on this kernel");
#endif
	}
	if (auto caps = DropCapabilities (); !caps) return caps;
	if (auto landlock = ApplyLandlock (profile); !landlock) return landlock;
	if ((profile.denySocketCreation || profile.denyInetSockets) && !g_SeccompInstalled)
	{
		auto seccomp = InstallSocketFilter (profile.denySocketCreation);
		if (!seccomp) return seccomp;
	}
	return { };
}

/**
 * Confine a supervisor that will fork/exec workers.
 *
 * Only no-new-privs and the capability drop are applied. Landlock, seccomp
 * socket filters and a network namespace are intentionally absent: they are
 * inherited across fork+exec and cannot be relaxed by the child, so installing
 * a supervisor-level filesystem or socket domain here would permanently deny a
 * storage or network child its delegated capability. Each exec'ed worker applies
 * its own role profile instead.
 */
i2p::expected<void, std::string> ApplyLinuxSupervisorSandbox ()
{
	if (prctl (PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0)
		return i2p::unexpected (std::string ("PR_SET_NO_NEW_PRIVS: ") + strerror (errno));
	return DropCapabilities ();
}

i2p::expected<void, std::string> ApplyLinuxCgroupV2Limits (const LinuxCgroupV2Limits & limits)
{
	if (limits.path.empty () || limits.path.front () != '/' || limits.path.find ('\0') != std::string::npos)
		return i2p::unexpected ("cgroup path must be absolute and NUL-free");
	const int directory = open (limits.path.c_str (), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	if (directory < 0) return i2p::unexpected (std::string ("open cgroup directory: ") + strerror (errno));
	struct statfs filesystem { };
	if (fstatfs (directory, &filesystem) != 0)
	{
		const int error = errno;
		close (directory);
		return i2p::unexpected (std::string ("fstatfs cgroup: ") + strerror (error));
	}
	if (static_cast<unsigned long> (filesystem.f_type) != static_cast<unsigned long> (CGROUP2_SUPER_MAGIC))
	{
		close (directory);
		return i2p::unexpected ("cgroup path is not a cgroup v2 filesystem");
	}

	auto writeControl = [directory] (const char * name, std::string_view value) -> i2p::expected<void, std::string>
	{
		const int fd = openat (directory, name, O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
		if (fd < 0) return i2p::unexpected (std::string ("open cgroup ") + name + ": " + strerror (errno));
		size_t written = 0;
		while (written < value.size ())
		{
			const ssize_t n = write (fd, value.data () + written, value.size () - written);
			if (n < 0 && errno == EINTR) continue;
			if (n <= 0)
			{
				const int error = errno;
				close (fd);
				return i2p::unexpected (std::string ("write cgroup ") + name + ": " + strerror (error));
			}
			written += static_cast<size_t> (n);
		}
		close (fd);
		return { };
	};

	if (limits.memoryMax > 0)
	{
		auto written = writeControl ("memory.max", std::to_string (limits.memoryMax));
		if (!written)
		{
			close (directory);
			return written;
		}
	}
	if (limits.pidsMax > 0)
	{
		auto written = writeControl ("pids.max", std::to_string (limits.pidsMax));
		if (!written)
		{
			close (directory);
			return written;
		}
	}
	if (limits.moveSelf)
	{
		auto moved = writeControl ("cgroup.procs", std::to_string (getpid ()));
		if (!moved)
		{
			close (directory);
			return moved;
		}
	}
	close (directory);
	return { };
}

LinuxIsolationSupport ProbeLinuxIsolation ()
{
	LinuxIsolationSupport support;
#ifdef I2PD_LANDLOCK_HEADERS_AVAILABLE
	const long abi = LandlockCreateRuleset (nullptr, 0, LANDLOCK_CREATE_RULESET_VERSION);
	if (abi >= 0)
	{
		support.landlockAbi = abi;
		support.filesystem = true;
		support.tcpNetwork = abi >= 4;
		support.udpNetwork = abi >= 10;
	}
#endif
#ifdef SECCOMP_MODE_FILTER
	support.seccompFilter = SeccompArchitecture () != 0;
#else
	support.seccompFilter = false;
#endif
	struct __user_cap_header_struct header { _LINUX_CAPABILITY_VERSION_3, 0 };
	struct __user_cap_data_struct data[2] { };
	support.capabilities = syscall (SYS_capget, &header, data) == 0;
	support.userNamespaces = access ("/proc/sys/user/max_user_namespaces", R_OK) == 0;
#ifdef CLONE_NEWNET
	support.networkNamespaces = support.userNamespaces || geteuid () == 0;
#else
	support.networkNamespaces = false;
#endif
	support.cgroupV2 = access ("/sys/fs/cgroup/cgroup.controllers", F_OK) == 0;
	return support;
}
}

#endif
