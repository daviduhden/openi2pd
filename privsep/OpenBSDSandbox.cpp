/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: OpenBSDSandbox.
 *
 * Builds least-privilege pledge and unveil policies per worker role,
 * merges duplicate path rules by unioning their permissions, then
 * applies unveil (locking the list) before pledge. Also builds the
 * router core's startup and runtime pledge windows, parses the operator
 * pledge file, and reports whether a policy grants network,
 * persistent-write or process-creation capability. A rule path that is
 * not absolute, carries NUL or names the filesystem root is refused:
 * unveiling "/" would grant a whole tree to a role that was built to
 * hold only its delegated one.
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
		// An all-slash path names the filesystem root. Unveiling it would
		// grant the whole tree and turn a least-privilege role into an
		// unsandboxed one, so a caller that resolves a root of "/" fails
		// the build instead of publishing that rule.
		if (path.find_first_not_of ('/') == std::string_view::npos) return false;
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

	/**
	 * Promise names accepted by the supported OpenBSD release
	 * (pledge(2)). "exec" and "error" are deliberately absent; see
	 * FORBIDDEN_PLEDGE_PROMISES.
	 */
	constexpr std::string_view KNOWN_PLEDGE_PROMISES[]
		= { "audio", "bpf", "chown", "cpath", "disklabel", "dns", "dpath", "drm", "fattr", "flock",
			  "getpw", "id", "inet", "mcast", "pf", "proc", "prot_exec", "ps", "recvfd", "route", "rpath", "sendfd",
			  "settime", "stdio", "tape", "tty", "unix", "unveil", "video", "vminfo", "vmm", "wpath", "wroute" };

	/**
	 * Promises no policy may ever carry.
	 *
	 * "exec" would let a restricted process start a new image; every
	 * worker is created before its first pledge. "error" would turn a
	 * policy violation into a recoverable errno and hide an operation
	 * that the sandbox was supposed to refuse.
	 */
	constexpr std::string_view FORBIDDEN_PLEDGE_PROMISES[] = { "exec", "error" };

	/**
	 * Promises a runtime policy file may never add, even when the runtime
	 * base set already contains one of them. They either re-open process
	 * creation (proc/exec), change process identity (id/chown), or grant
	 * privileged kernel interfaces the router never needs.
	 */
	constexpr std::string_view RUNTIME_FORBIDDEN_PLEDGE_PROMISES[] = { "proc", "exec", "error", "id", "chown",
		"dpath", "prot_exec", "pf", "bpf", "vmm", "tty", "audio", "video", "tape", "disklabel", "drm", "wroute",
		"settime" };

	constexpr size_t MAX_PLEDGE_FILE_BYTES = 64 * 1024;
	constexpr size_t MAX_PLEDGE_TOKEN_LENGTH = 32;
	constexpr size_t MAX_PLEDGE_TOKENS = 64;

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

bool IsKnownPledgePromiseName (std::string_view name)
{
	if (name.empty ()) return false;
	for (const auto known : KNOWN_PLEDGE_PROMISES)
		if (name == known) return true;
	return false;
}

bool IsForbiddenPledgePromiseName (std::string_view name)
{
	for (const auto forbidden : FORBIDDEN_PLEDGE_PROMISES)
		if (name == forbidden) return true;
	return false;
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
	return HasPromise (pledge, "proc");
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
		// The supervisor owns pre-created log workers: the active one and
		// one reserve are configured before this policy is installed. It
		// never executes a new image, opens a path, connects a socket or
		// receives a descriptor. "proc" is needed only to waitpid/kill the
		// children it already owns; "stdio" covers the anonymous
		// socketpair channels. No unveil rule is required.
		policy.useUnveil = false; // no path access at all
		AppendPromise (policy.pledge, "stdio");
		AppendPromise (policy.pledge, "proc");
		break;

	case OpenBSDWorkerRole::StorageBroker:
		// The former on-demand broker forked/exec'ed a storage worker after
		// its own sandbox. That requires the forbidden "exec" promise, so
		// the role is refused until an exec-free provisioning protocol
		// exists. Isolated torrent storage consequently fails closed on
		// OpenBSD instead of receiving a broader capability.
		return PolicyError ("storage broker role is disabled: it requires the forbidden exec promise");

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
		// supplied through a pre-opened descriptor. There is no network and
		// no pathname write capability. Operations use bounded key handles.
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

	// Review the generated text before it can reach the kernel: pledge(2)
	// answers EINVAL for an unknown name, which aborts a worker without
	// naming the malformed promise.
	size_t pos = 0;
	while (pos < policy.pledge.size ())
	{
		while (pos < policy.pledge.size () && policy.pledge[pos] == ' ') ++pos;
		const size_t begin = pos;
		while (pos < policy.pledge.size () && policy.pledge[pos] != ' ') ++pos;
		if (begin == pos) continue;
		const std::string_view token = std::string_view (policy.pledge).substr (begin, pos - begin);
		if (IsForbiddenPledgePromiseName (token))
			return PolicyError ("forbidden pledge promise \"" + std::string (token) + "\"");
		if (!IsKnownPledgePromiseName (token))
			return PolicyError ("unknown pledge promise \"" + std::string (token) + "\"");
	}
	return policy;
}

i2p::expected<void, std::string> ApplyOpenBSDPolicy (const OpenBSDPolicy & policy)
{
#if defined(__OpenBSD__)
	if (policy.usePledge)
	{
		// Defence in depth for a hand-assembled policy: never let a
		// forbidden or unknown promise reach the kernel, and never call
		// pledge(""). The builders and pledge-file parser already reject
		// these names.
		if (policy.pledge.empty ()) return i2p::unexpected ("empty pledge policy");
		size_t pos = 0;
		while (pos < policy.pledge.size ())
		{
			while (pos < policy.pledge.size () && policy.pledge[pos] == ' ') ++pos;
			const size_t begin = pos;
			while (pos < policy.pledge.size () && policy.pledge[pos] != ' ') ++pos;
			if (begin == pos) continue;
			const std::string_view token = std::string_view (policy.pledge).substr (begin, pos - begin);
			if (IsForbiddenPledgePromiseName (token))
				return i2p::unexpected ("forbidden pledge promise \"" + std::string (token) + "\"");
			if (!IsKnownPledgePromiseName (token))
				return i2p::unexpected ("unknown pledge promise \"" + std::string (token) + "\"");
		}
	}
	if (policy.useUnveil)
	{
		for (const auto & rule : policy.unveil)
		{
			// A policy object can be assembled by a caller, so the applied
			// boundary re-checks the two rules that make an unveil entry
			// meaningful: a non-empty permission set and an absolute path
			// that is not the filesystem root.
			if (rule.path.empty () || rule.permissions.empty ()) return i2p::unexpected ("empty unveil rule");
			if (rule.path.front () != '/' || rule.path.find_first_not_of ('/') == std::string::npos)
				return i2p::unexpected ("unveil rule is not a path below the filesystem root");
			if (unveil (rule.path.c_str (), rule.permissions.c_str ()) == -1)
				return i2p::unexpected ("unveil " + rule.path + ": " + std::string (strerror (errno)));
		}
		if (unveil (nullptr, nullptr) == -1) return i2p::unexpected (std::string ("unveil lock: ") + strerror (errno));
	}
	// OPENBSD_PLEDGE_EXECPROMISES is always null. No policy grants exec,
	// so a pledged process cannot reach a new image; a pre-created worker
	// installs its own unveil+pledge after fork/exec. Never replace this
	// with a promise string; the value is pinned in the header and in the
	// statically checked policy test.
	if (policy.usePledge && pledge (policy.pledge.c_str (), OPENBSD_PLEDGE_EXECPROMISES) == -1)
		return i2p::unexpected ("pledge " + policy.pledge + ": " + std::string (strerror (errno)));
	return { };
#else
	// Deliberate fail-closed stub: an unavailable sandbox is an error,
	// never a successful no-op, so no caller can silently continue
	// unconfined on a platform that does not implement pledge/unveil.
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

// Validation is performed on the descriptor, not on the path it was
// addressed with, so a rename or replacement between sendfd and this
// call cannot redirect the authority. CLOEXEC is required as well: an
// inherited descriptor that survives execve would bypass the exec'ed
// worker's own descriptor contract.
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
	// signing-key chmod, the transports, the resolver and local IPC
	// (worker channels and /dev/log).
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

	if (window.reapChildren)
		// An owned supervisor is created before the first pledge; proc is
		// needed afterwards only for bounded waitpid/kill. It is present in
		// both windows because pledge can only remove promises.
		add ("proc");
	if (window.upnp) add ("mcast"); // miniupnpc multicast discovery
	if (phase == OpenBSDPledgePhase::Startup && window.unveil)
		// The first pledge is installed after every worker exists. It adds
		// only "unveil", needed to install and lock the rules; the final
		// pledge drops it. No window carries exec, id or another
		// process-creation promise.
		add ("unveil");
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

// Reading the operator policy file needs rpath, so it belongs to the
// startup phase: after the runtime pledge has removed path opening, the
// same call would abort the core instead of returning an error. The
// accepted list is only intersected with the phase base set; nothing
// here can widen a promise.
i2p::expected<std::vector<std::string>, std::string> ParseOpenBSDPledgeFile (
	const std::string & path, std::span<const std::string_view> allowedPromises, OpenBSDPledgePhase phase)
{
	if (path.empty () || path.find ('\0') != std::string::npos)
		return i2p::unexpected ("empty or invalid pledge policy path");
	std::ifstream file (path);
	if (!file) return i2p::unexpected ("cannot open pledge policy file");
	const std::string_view phaseName = phase == OpenBSDPledgePhase::Startup ? "startup" : "runtime";
	// The file is read into one bounded buffer before any line is
	// split. std::getline() would materialise an arbitrarily long first
	// line before the size check, which turns a malformed policy file
	// into unbounded allocation. The extra byte detects an oversized
	// file without reading it whole.
	std::string contents (MAX_PLEDGE_FILE_BYTES + 1, '\0');
	file.read (contents.data (), static_cast<std::streamsize> (contents.size ()));
	const std::streamsize read = file.gcount ();
	if (file.bad ()) return i2p::unexpected ("cannot read the pledge policy file");
	if (read > static_cast<std::streamsize> (MAX_PLEDGE_FILE_BYTES))
		return i2p::unexpected ("pledge policy file exceeds the size bound");
	contents.resize (static_cast<size_t> (read));
	std::vector<std::string> accepted;
	std::string line;
	size_t lineNumber = 0;
	size_t lineOffset = 0;
	while (lineOffset < contents.size ())
	{
		++lineNumber;
		const auto newline = contents.find ('\n', lineOffset);
		const size_t lineEnd = newline == std::string::npos ? contents.size () : newline;
		line.assign (contents, lineOffset, lineEnd - lineOffset);
		lineOffset = newline == std::string::npos ? contents.size () : newline + 1;
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
			if (IsForbiddenPledgePromiseName (token))
				return i2p::unexpected ("forbidden pledge promise \"" + std::string (token) + "\"");
			if (!IsKnownPledgePromiseName (token))
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
	return accepted;
}
}
