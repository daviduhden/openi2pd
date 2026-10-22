/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: OpenBSDSandbox.
 *
 * Builds least-privilege pledge and unveil policies per worker role, merges
 * duplicate path rules by unioning their permissions, then applies unveil
 * (locking the list) before pledge. Helpers open and validate pre-opened
 * directories and descriptors and report whether a policy grants network,
 * persistent-write or process-creation capability.
 *
 */

#include "OpenBSDSandbox.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
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
		// The supervisor must be able to fork/exec a replacement log worker.
		// It still has no network and no pathname write/read capability. Do
		// not lock unveil here: the supervisor must exec the executable path
		// resolved before this policy is installed. pledge already denies
		// pathname reads/writes.
		policy.useUnveil = false;
		AppendPromise (policy.pledge, "stdio");
		AppendPromise (policy.pledge, "unix");
		AppendPromise (policy.pledge, "proc");
		AppendPromise (policy.pledge, "exec");
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
	if (policy.usePledge && pledge (policy.pledge.c_str (), nullptr) == -1)
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
}
