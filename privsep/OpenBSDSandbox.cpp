/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: OpenBSDSandbox.
 *
 * Builds least-privilege pledge and unveil policies per worker role, merges
 * duplicate path rules by unioning their permissions, then applies unveil
 * (locking the list) before pledge. Also builds the router core's startup and
 * runtime pledge windows, parses the operator pledge file, and reports whether
 * a policy grants network, persistent-write or process-creation capability.
 *
 */

#include "OpenBSDSandbox.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

namespace i2p::privsep
{
namespace
{
	bool HasPromise (std::string_view list, std::string_view name)
	{
		size_t pos = 0;
		while (pos < list.size ())
		{
			while (pos < list.size () && list[pos] == ' ') ++pos;
			const size_t begin = pos;
			while (pos < list.size () && list[pos] != ' ') ++pos;
			if (list.substr (begin, pos - begin) == name) return true;
		}
		return false;
	}

	void AppendPromise (std::string & value, std::string_view promise)
	{
		if (HasPromise (value, promise)) return;
		if (!value.empty ()) value.push_back (' ');
		value.append (promise);
	}

	bool AddUnveil (OpenBSDPolicy & policy, std::string_view path, std::string_view permissions)
	{
		if (path.empty ()) return true;
		if (path.front () != '/') return false; // configuration paths must be resolved before the sandbox
		if (path.find ('\0') != std::string_view::npos || permissions.find ('\0') != std::string_view::npos)
			return false;
		for (auto & rule : policy.unveil)
		{
			if (rule.path != std::string (path)) continue;
			// Repeated rules only add permissions. Never let a later rule
			// replace an earlier, more restrictive rule with a weaker one.
			for (char permission : permissions)
				if (rule.permissions.find (permission) == std::string::npos) rule.permissions.push_back (permission);
			return true;
		}
		policy.unveil.push_back ({ std::string (path), std::string (permissions) });
		return true;
	}

	i2p::unexpected<std::string> PolicyError (std::string_view message)
	{
		return i2p::unexpected (std::string (message));
	}

	/** Promise names accepted by the supported OpenBSD release (pledge(2)). */
	constexpr std::string_view KNOWN_PLEDGE_PROMISES[]
		= { "audio", "bpf", "chown", "cpath", "disklabel", "dns", "dpath", "drm", "error", "exec", "fattr", "flock",
			  "getpw", "id", "inet", "mcast", "pf", "proc", "prot_exec", "ps", "recvfd", "route", "rpath", "sendfd",
			  "settime", "stdio", "tape", "tty", "unix", "unveil", "video", "vminfo", "vmm", "wpath", "wroute" };

	/**
	 * Promises a runtime policy file may never add, even when the runtime base
	 * set already contains one of them. They either re-open process creation
	 * (proc/exec), change process identity (id/chown), or grant privileged
	 * kernel interfaces the router never needs.
	 */
	constexpr std::string_view RUNTIME_FORBIDDEN_PLEDGE_PROMISES[] = { "proc", "exec", "id", "chown", "dpath",
		"prot_exec", "pf", "bpf", "vmm", "tty", "audio", "video", "tape", "disklabel", "drm", "wroute", "settime" };

	constexpr size_t MAX_PLEDGE_FILE_BYTES = 64 * 1024;
	constexpr size_t MAX_PLEDGE_TOKEN_LENGTH = 32;
	constexpr size_t MAX_PLEDGE_TOKENS = 64;

	bool IsKnownPledgePromise (std::string_view name)
	{
		for (const auto known : KNOWN_PLEDGE_PROMISES)
			if (name == known) return true;
		return false;
	}

	bool IsRuntimeForbiddenPledge (std::string_view name)
	{
		for (const auto forbidden : RUNTIME_FORBIDDEN_PLEDGE_PROMISES)
			if (name == forbidden) return true;
		return false;
	}

	bool ContainsPromise (std::span<const std::string_view> promises, std::string_view name)
	{
		for (const auto promise : promises)
			if (promise == name) return true;
		return false;
	}

	std::string_view TrimPledgeText (std::string_view text)
	{
		const auto first = text.find_first_not_of (" \t\r");
		if (first == std::string_view::npos) return { };
		const auto last = text.find_last_not_of (" \t\r");
		return text.substr (first, last - first + 1);
	}
}

const char * OpenBSDWorkerRoleName (OpenBSDWorkerRole role)
{
	switch (role)
	{
	case OpenBSDWorkerRole::Supervisor: return "supervisor";
	case OpenBSDWorkerRole::Log: return "log";
	case OpenBSDWorkerRole::Storage: return "storage";
	case OpenBSDWorkerRole::Network: return "network";
	case OpenBSDWorkerRole::Crypto: return "crypto";
	case OpenBSDWorkerRole::Admin: return "admin";
	case OpenBSDWorkerRole::StorageBroker: return "storage-broker";
	}
	return "unknown";
}

bool OpenBSDPolicy::HasExternalNetwork () const
{
	return HasPromise (pledge, "inet") || HasPromise (pledge, "dns") || HasPromise (pledge, "mcast")
		|| HasPromise (pledge, "route") || HasPromise (pledge, "wroute") || HasPromise (pledge, "bpf");
}

bool OpenBSDPolicy::HasPersistentWrite () const
{
	return HasPromise (pledge, "wpath") || HasPromise (pledge, "cpath") || HasPromise (pledge, "fattr");
}

bool OpenBSDPolicy::HasProcessCreation () const
{
	return HasPromise (pledge, "proc") || HasPromise (pledge, "exec");
}

bool OpenBSDPolicy::HasLocalIpc () const
{
	return HasPromise (pledge, "unix");
}

i2p::expected<OpenBSDPolicy, std::string> BuildOpenBSDPolicy (
	OpenBSDWorkerRole role, const OpenBSDPolicyConfig & config)
{
	OpenBSDPolicy policy;
	policy.role = role;
	policy.useUnveil = config.useUnveil;
	policy.usePledge = config.usePledge;

	switch (role)
	{
	case OpenBSDWorkerRole::Supervisor:
		// The supervisor forks/execs replacement log workers, so it needs
		// proc/exec. pledge(2) is called with null execpromises, which means
		// the exec'ed image starts without any pledge; every worker therefore
		// installs its own unveil rules and pledge immediately after execve
		// and before it serves a frame.
		//
		// unveil is forced on for this role even when the operator disabled it:
		// an unlocked unveil list would let the supervisor execve an arbitrary
		// binary (the exec promise does not need rpath). Exactly one rule is
		// added - the frozen executable path with "x" - and nothing else, so
		// the supervisor can neither read/write a path nor exec a second image.
		policy.useUnveil = true;
		if (config.executablePath.empty () || config.executablePath.front () != '/'
			|| config.executablePath.find ('\0') != std::string::npos)
			return PolicyError ("invalid supervisor executable unveil rule");
		if (!AddUnveil (policy, config.executablePath, "x"))
			return PolicyError ("invalid supervisor executable unveil rule");
		AppendPromise (policy.pledge, "stdio");
		AppendPromise (policy.pledge, "proc");
		AppendPromise (policy.pledge, "exec");
		// No "unix": stdio already permits socketpair(2), which is all the
		// supervisor needs to build a worker channel. "unix" would additionally
		// allow connect/bind at any pathname without rpath or cpath.
		// "id" is present only when a worker identity is configured: the forked
		// child drops supplementary groups, gid and uid immediately before
		// execve, in this process, so the promise is required here and nowhere
		// else.
		if (config.dropCredentials) AppendPromise (policy.pledge, "id");
		break;

	case OpenBSDWorkerRole::StorageBroker:
		// The broker is spawned before the router runtime pledge, forwards one
		// channel descriptor per worker with sendfd, and never receives one.
		// recvfd is deliberately absent: a setup/replacement frame that carries
		// an unexpected descriptor fails at recvmsg before the payload is
		// parsed, which is the fail-closed outcome for this role. "unix" is
		// absent for the same reason as the supervisor: socketpair(2) is part
		// of stdio and the broker never resolves a pathname.
		policy.useUnveil = true;
		if (config.executablePath.empty () || config.executablePath.front () != '/'
			|| config.executablePath.find ('\0') != std::string::npos)
			return PolicyError ("invalid storage broker executable unveil rule");
		if (!AddUnveil (policy, config.executablePath, "x"))
			return PolicyError ("invalid storage broker executable unveil rule");
		AppendPromise (policy.pledge, "stdio");
		AppendPromise (policy.pledge, "proc");
		AppendPromise (policy.pledge, "exec");
		AppendPromise (policy.pledge, "sendfd");
		// Same reason as the supervisor: the forked child drops uid/gid before
		// execve, so a configured worker identity needs the id promise.
		if (config.dropCredentials) AppendPromise (policy.pledge, "id");
		break;

	case OpenBSDWorkerRole::Log:
		AppendPromise (policy.pledge, "stdio");
		AppendPromise (policy.pledge, "unix");
		if (config.logToSyslog)
		{
			// syslog(3) connects to /dev/log. The core's unveil state is not
			// inherited across execve, so the log worker needs its own rule.
			if (config.useUnveil && !AddUnveil (policy, "/dev/log", "w"))
				return PolicyError ("invalid /dev/log unveil rule");
		}
		else if (config.logPath.empty ())
		{
			// Foreground stdout sink: no filesystem access; the channel still
			// needs the local UNIX-socket promise.
		}
		else if (config.preopenedLog)
		{
			// The sender opened the file and passes one descriptor before the
			// sandbox is installed. Do not grant wpath/cpath/fattr or a rule
			// for the log path; descriptor reception is not needed afterwards.
			AppendPromise (policy.pledge, "rpath");
			if (config.useUnveil && !AddUnveil (policy, "/etc/localtime", "r"))
				return PolicyError ("invalid localtime unveil rule");
		}
		else
		{
			AppendPromise (policy.pledge, "rpath");
			AppendPromise (policy.pledge, "wpath");
			AppendPromise (policy.pledge, "cpath");
			AppendPromise (policy.pledge, "fattr");
			if (config.useUnveil)
			{
				if (!AddUnveil (policy, "/etc/localtime", "r")) return PolicyError ("invalid localtime unveil rule");
				if (!AddUnveil (policy, config.logPath, "rwc")) return PolicyError ("invalid log unveil rule");
			}
		}
		break;

	case OpenBSDWorkerRole::Storage:
		// Filesystem and local IPC only. There is deliberately no inet/dns
		// promise and no generic path operation: the backend maps typed
		// resources to these roots.
		AppendPromise (policy.pledge, "stdio");
		if (!config.preopenedStorageIpc) AppendPromise (policy.pledge, "unix");
		AppendPromise (policy.pledge, "rpath");
		AppendPromise (policy.pledge, "wpath");
		AppendPromise (policy.pledge, "cpath");
		// Storage code never chmods/chflags or takes a lock; do not grant
		// fattr/flock to the untrusted-input parsing process.
		// Storage roots and other bounded objects may be delivered as one
		// pre-opened descriptor. recvfd is local descriptor reception only;
		// it does not grant external network or pathname write access.
		if (!config.preopenedStorageIpc) AppendPromise (policy.pledge, "recvfd");
		if (config.useUnveil)
		{
			if (config.dataDir.empty () || config.dataDir.front () != '/'
				|| config.dataDir.find ('\0') != std::string::npos)
				return PolicyError ("invalid storage data-directory unveil rule");
			if (!AddUnveil (policy, config.dataDir, "rwc"))
				return PolicyError ("invalid storage data-directory unveil rule");
			if (!AddUnveil (policy, config.certsDir, "r"))
				return PolicyError ("invalid certificate-directory unveil rule");
			for (const auto & path : config.extraReadPaths)
				if (!AddUnveil (policy, path, "r")) return PolicyError ("invalid storage read unveil rule");
			for (const auto & path : config.extraWritePaths)
				if (!AddUnveil (policy, path, "rwc")) return PolicyError ("invalid storage write unveil rule");
		}
		break;

	case OpenBSDWorkerRole::Network:
		// External networking is intentionally paired with no pathname write
		// capability. mcast is opt-in and used only for UPnP discovery.
		AppendPromise (policy.pledge, "stdio");
		AppendPromise (policy.pledge, "unix");
		AppendPromise (policy.pledge, "inet");
		if (config.dnsLookup) AppendPromise (policy.pledge, "dns");
		if (config.socketHandoff) AppendPromise (policy.pledge, "recvfd");
		if (config.routeInfo) AppendPromise (policy.pledge, "route");
		if (config.upnp) AppendPromise (policy.pledge, "mcast");
		break;

	case OpenBSDWorkerRole::Crypto:
		// Long-term keys must be loaded before this policy is installed, or
		// supplied through a pre-opened descriptor. There is no network and no
		// pathname write capability. Operations use bounded key handles.
		AppendPromise (policy.pledge, "stdio");
		AppendPromise (policy.pledge, "unix");
		break;

	case OpenBSDWorkerRole::Admin:
		// Administrative reads/allowlisted control only. No external network
		// and no persistent writes in this process.
		AppendPromise (policy.pledge, "stdio");
		AppendPromise (policy.pledge, "unix");
		break;
	}

	return policy;
}

i2p::expected<void, std::string> ApplyOpenBSDPolicy (const OpenBSDPolicy & policy)
{
#if defined(__OpenBSD__)
	if (policy.useUnveil)
	{
		for (const auto & rule : policy.unveil)
		{
			if (rule.path.empty () || rule.permissions.empty ()) return i2p::unexpected ("empty unveil rule");
			if (unveil (rule.path.c_str (), rule.permissions.c_str ()) == -1)
				return i2p::unexpected ("unveil " + rule.path + ": " + std::string (strerror (errno)));
		}
		if (unveil (nullptr, nullptr) == -1) return i2p::unexpected (std::string ("unveil lock: ") + strerror (errno));
	}
	// OPENBSD_PLEDGE_EXECPROMISES is always null: an exec'ed image starts with
	// no promise at all and must install its own unveil+pledge. Never replace
	// this with a promise string; the value is pinned in the header and in the
	// statically checked policy test.
	if (policy.usePledge && pledge (policy.pledge.c_str (), OPENBSD_PLEDGE_EXECPROMISES) == -1)
		return i2p::unexpected ("pledge " + policy.pledge + ": " + std::string (strerror (errno)));
	return { };
#else
	(void)policy;
	return i2p::unexpected ("OpenBSD sandbox is not available on this platform");
#endif
}

i2p::expected<int, std::string> OpenPreopenedDirectory (std::string_view path)
{
	if (path.empty () || path.front () != '/') return i2p::unexpected ("pre-opened directory path must be absolute");
	int flags = O_RDONLY | O_CLOEXEC;
#ifdef O_DIRECTORY
	flags |= O_DIRECTORY;
#endif
#ifdef O_NOFOLLOW
	flags |= O_NOFOLLOW;
#endif
	std::string copy (path);
	const int fd = ::open (copy.c_str (), flags);
	if (fd < 0) return i2p::unexpected ("open pre-opened directory: " + std::string (strerror (errno)));
	struct stat info { };
	if (fstat (fd, &info) != 0 || !S_ISDIR (info.st_mode))
	{
		const int error = errno;
		::close (fd);
		return i2p::unexpected ("pre-opened directory is not a directory: " + std::string (strerror (error)));
	}
	return fd;
}

i2p::expected<void, std::string> ValidatePreopenedDescriptor (int fd, bool directory)
{
	if (fd < 0) return i2p::unexpected ("invalid pre-opened descriptor");
	const int flags = fcntl (fd, F_GETFD);
	if (flags < 0) return i2p::unexpected ("fcntl(F_GETFD): " + std::string (strerror (errno)));
	if ((flags & FD_CLOEXEC) == 0) return i2p::unexpected ("pre-opened descriptor is not close-on-exec");
	struct stat info { };
	if (fstat (fd, &info) != 0)
		return i2p::unexpected ("fstat pre-opened descriptor: " + std::string (strerror (errno)));
	if (directory && !S_ISDIR (info.st_mode)) return i2p::unexpected ("pre-opened descriptor is not a directory");
	if (!directory && !S_ISREG (info.st_mode)) return i2p::unexpected ("pre-opened descriptor is not a regular file");
	return { };
}

i2p::expected<void, std::string> ValidatePreopenedSocket (int fd, int expectedType, bool requireListening)
{
	struct stat status { };
	int socketType = 0;
	socklen_t socketTypeLength = sizeof (socketType);
	if (fd < 0 || ::fstat (fd, &status) != 0 || !S_ISSOCK (status.st_mode)
		|| ::getsockopt (fd, SOL_SOCKET, SO_TYPE, &socketType, &socketTypeLength) != 0 || socketType != expectedType)
		return i2p::unexpected ("pre-opened descriptor is not the expected socket type");
	const int flags = ::fcntl (fd, F_GETFD);
	if (flags < 0 || !(flags & FD_CLOEXEC)) return i2p::unexpected ("pre-opened socket is not close-on-exec");
	if (requireListening)
	{
#ifdef SO_ACCEPTCONN
		int accepting = 0;
		socklen_t acceptingLength = sizeof (accepting);
		if (::getsockopt (fd, SOL_SOCKET, SO_ACCEPTCONN, &accepting, &acceptingLength) != 0 || !accepting)
			return i2p::unexpected ("pre-opened stream socket is not listening");
#else
		return i2p::unexpected ("SO_ACCEPTCONN is unavailable");
#endif
	}
	return { };
}

bool OpenBSDPolicyAllowsExternalNetwork (const OpenBSDPolicy & policy)
{
	return policy.HasExternalNetwork ();
}

bool OpenBSDPolicyAllowsPersistentWrite (const OpenBSDPolicy & policy)
{
	return policy.HasPersistentWrite ();
}

std::vector<std::string> OpenBSDPledgePromiseList (OpenBSDPledgePhase phase, const OpenBSDPledgeWindow & window)
{
	std::vector<std::string> promises;
	auto add = [&promises] (const char * promise)
	{
		for (const auto & existing : promises)
			if (existing == promise) return;
		promises.emplace_back (promise);
	};

	// Both phases: the router core always needs its own queue, sockets and
	// already-open descriptors, its data/certificate reads and writes, its
	// signing-key chmod, the transports, the resolver and local IPC (worker
	// channels and /dev/log).
	add ("stdio"); // queue, sockets, signals and already-open descriptors
	add ("rpath"); // read router data, certificates and configuration
	add ("wpath"); // write router data and logs
	add ("cpath"); // create and remove files, sockets and the pidfile
	add ("fattr"); // chmod/chflags when saving keys and restricting files
	add ("inet"); // NTCP2/SSU2, HTTP proxies and reseed
	add ("dns"); // resolver support
	add ("unix"); // local IPC and /dev/log
	add ("flock"); // pidfile lockf(3)
	if (window.descriptorReception)
		// Bounded worker channels/roots are handed back by a supervised
		// storage worker or broker; descriptor reception is not external
		// network and grants no pathname authority.
		add ("recvfd");

	if (phase == OpenBSDPledgePhase::Startup)
	{
		// Fork/exec of the supervisor, log, router-storage, data-storage and
		// torrent-storage processes happens only inside this window. proc is
		// also needed to reap them.
		if (window.execWorkers || window.reapChildren) add ("proc");
		if (window.execWorkers) add ("exec");
		// unveil(2) itself requires the unveil promise.
		if (window.unveil) add ("unveil");
		// miniupnpc multicast discovery.
		if (window.upnp) add ("mcast");
		// A forked child drops supplementary groups, gid and uid before
		// execve; only the startup window may carry the identity promise.
		if (window.credentialDrop) add ("id");
	}
	else
	{
		// The runtime window keeps proc only for reaping/signalling an owned
		// supervisor. It never has exec, id, chown, unveil or any other
		// capability the core no longer needs after startup.
		if (window.reapChildren) add ("proc");
		if (window.upnp) add ("mcast");
	}
	return promises;
}

std::string BuildOpenBSDPledgePromises (OpenBSDPledgePhase phase, const OpenBSDPledgeWindow & window)
{
	std::string promises;
	for (const auto & promise : OpenBSDPledgePromiseList (phase, window))
	{
		if (!promises.empty ()) promises.push_back (' ');
		promises += promise;
	}
	return promises;
}

bool IsPledgePromiseSubset (std::string_view subset, std::string_view superset)
{
	size_t pos = 0;
	while (pos < subset.size ())
	{
		while (pos < subset.size () && subset[pos] == ' ') ++pos;
		const size_t begin = pos;
		while (pos < subset.size () && subset[pos] != ' ') ++pos;
		if (begin == pos) continue;
		if (!HasPromise (superset, subset.substr (begin, pos - begin))) return false;
	}
	return true;
}

i2p::expected<std::vector<std::string>, std::string> ParseOpenBSDPledgeFile (
	const std::string & path, std::span<const std::string_view> allowedPromises, OpenBSDPledgePhase phase)
{
	if (path.empty () || path.find ('\0') != std::string::npos)
		return i2p::unexpected ("empty or invalid pledge policy path");
	std::ifstream file (path);
	if (!file) return i2p::unexpected ("cannot open pledge policy file");
	const std::string_view phaseName = phase == OpenBSDPledgePhase::Startup ? "startup" : "runtime";
	std::vector<std::string> accepted;
	std::string line;
	size_t consumed = 0;
	size_t lineNumber = 0;
	while (std::getline (file, line))
	{
		++lineNumber;
		if (line.size () > MAX_PLEDGE_FILE_BYTES || consumed + line.size () > MAX_PLEDGE_FILE_BYTES)
			return i2p::unexpected ("pledge policy file exceeds the size bound");
		consumed += line.size ();
		if (line.find ('#') != std::string::npos)
			return i2p::unexpected ("comment on line " + std::to_string (lineNumber) + " of the pledge policy file");
		const auto trimmed = TrimPledgeText (line);
		if (trimmed.empty ())
			return i2p::unexpected ("blank line " + std::to_string (lineNumber) + " in the pledge policy file");
		size_t pos = 0;
		while (pos < trimmed.size ())
		{
			while (pos < trimmed.size () && (trimmed[pos] == ' ' || trimmed[pos] == '\t')) ++pos;
			const size_t begin = pos;
			while (pos < trimmed.size () && trimmed[pos] != ' ' && trimmed[pos] != '\t') ++pos;
			if (begin == pos) continue;
			const auto token = trimmed.substr (begin, pos - begin);
			if (token.size () > MAX_PLEDGE_TOKEN_LENGTH)
				return i2p::unexpected ("oversized token in the pledge policy file");
			if (!IsKnownPledgePromise (token))
				return i2p::unexpected ("unknown pledge promise \"" + std::string (token) + "\"");
			if (phase == OpenBSDPledgePhase::Runtime && IsRuntimeForbiddenPledge (token))
				return i2p::unexpected ("runtime pledge policy may not add \"" + std::string (token) + "\"");
			if (!ContainsPromise (allowedPromises, token))
				return i2p::unexpected ("pledge promise \"" + std::string (token) + "\" is not part of the "
					+ std::string (phaseName) + " base set");
			if (accepted.size () >= MAX_PLEDGE_TOKENS)
				return i2p::unexpected ("too many promises in the pledge policy file");
			// Duplicates are harmless and ignored.
			const std::string promise (token);
			if (std::find (accepted.begin (), accepted.end (), promise) == accepted.end ())
				accepted.push_back (promise);
		}
	}
	if (file.bad ()) return i2p::unexpected ("cannot read the pledge policy file");
	return accepted;
}
}
