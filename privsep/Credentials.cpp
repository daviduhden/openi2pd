/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Credentials.
 *
 * Resolves the worker service identity once, before any sandbox is installed,
 * publishes it for the spawn helpers and drops to it in the forked child.
 * Only fixed, non-secret diagnostics leave this translation unit: an account
 * name, a password field, a home directory or a group list is never echoed,
 * formatted, logged or retained.
 *
 */

#include "Credentials.h"

#ifndef _WIN32

#include <array>
#include <cerrno>
#include <cstddef>
#include <grp.h>
#include <mutex>
#include <pwd.h>
#include <string_view>
#include <sys/types.h>
#include <unistd.h>

namespace i2p::privsep
{
namespace
{
	/** Account and group names are bounded well below any NSS record size. */
	constexpr size_t MAX_IDENTITY_NAME_SIZE = 256;
	/**
	 * Fixed NSS scratch buffer. The first attempt uses half of it and one
	 * ERANGE retry uses all of it, so a hostile or broken account database
	 * cannot make the lookup grow without a limit.
	 */
	constexpr size_t LOOKUP_BUFFER_SIZE = 16384;

	/**
	 * Reject NUL, an embedded control character and an oversized name. A
	 * truncated account name would silently resolve to a different identity.
	 */
	bool IsValidIdentityName (std::string_view name)
	{
		if (name.empty () || name.size () >= MAX_IDENTITY_NAME_SIZE) return false;
		for (auto character : name)
		{
			const auto value = static_cast<unsigned char> (character);
			if (value == 0 || value < 0x20 || value == 0x7f) return false;
		}
		return true;
	}

	/** Run one bounded NSS lookup (getpwnam_r/getgrnam_r share this shape). */
	template <typename Lookup> int LookupBounded (Lookup && lookup)
	{
		std::array<char, LOOKUP_BUFFER_SIZE> buffer { };
		const int error = lookup (buffer.data (), buffer.size () / 2);
		if (error != ERANGE) return error;
		return lookup (buffer.data (), buffer.size ());
	}
}

i2p::expected<WorkerCredentials, std::string> ResolveWorkerCredentials (
	std::string_view user, std::string_view group)
{
	WorkerCredentials credentials;
	credentials.uid = static_cast<uint32_t> (::geteuid ());
	credentials.gid = static_cast<uint32_t> (::getegid ());
	credentials.configured = false;
	// No override: keep the effective identity of the daemon. Such a daemon
	// cannot raise or lower privilege and needs no account database lookup.
	if (user.empty () && group.empty ()) return credentials;
	// A group without a user would change the group while keeping the daemon's
	// uid, which is never a coherent service identity.
	if (user.empty ()) return i2p::unexpected (std::string ("privsep.group requires privsep.user"));
	if (!IsValidIdentityName (user))
		return i2p::unexpected (std::string ("privsep.user is not a valid account name"));
	if (!group.empty () && !IsValidIdentityName (group))
		return i2p::unexpected (std::string ("privsep.group is not a valid group name"));

	const std::string userName (user);
	struct passwd userEntry { };
	struct passwd * userResult = nullptr;
	const int userError = LookupBounded ([&] (char * buffer, size_t size)
	{
		userResult = nullptr;
		return ::getpwnam_r (userName.c_str (), &userEntry, buffer, size, &userResult);
	});
	if (userError != 0) return i2p::unexpected (std::string ("cannot resolve privsep.user"));
	if (!userResult) return i2p::unexpected (std::string ("privsep.user does not exist"));
	// A worker must never run as the superuser: every sandbox bypass would then
	// be a full privilege compromise.
	if (userEntry.pw_uid == 0)
		return i2p::unexpected (std::string ("privsep.user must not be the superuser"));
	credentials.uid = static_cast<uint32_t> (userEntry.pw_uid);
	credentials.gid = static_cast<uint32_t> (userEntry.pw_gid);

	if (!group.empty ())
	{
		const std::string groupName (group);
		struct group groupEntry { };
		struct group * groupResult = nullptr;
		const int groupError = LookupBounded ([&] (char * buffer, size_t size)
		{
			groupResult = nullptr;
			return ::getgrnam_r (groupName.c_str (), &groupEntry, buffer, size, &groupResult);
		});
		if (groupError != 0) return i2p::unexpected (std::string ("cannot resolve privsep.group"));
		if (!groupResult) return i2p::unexpected (std::string ("privsep.group does not exist"));
		credentials.gid = static_cast<uint32_t> (groupEntry.gr_gid);
	}
	credentials.configured = true;
	return credentials;
}

namespace
{
	std::mutex g_CredentialsMutex;
	WorkerCredentials g_Credentials;
	bool g_CredentialsInitialized = false;
}

void SetWorkerCredentials (const WorkerCredentials & credentials)
{
	std::lock_guard lock (g_CredentialsMutex);
	g_Credentials = credentials;
	g_CredentialsInitialized = true;
}

const WorkerCredentials & GetWorkerCredentials ()
{
	// The declaration returns a reference, so every thread keeps its own
	// snapshot: a concurrent Set cannot torn-read the struct and the returned
	// reference stays valid for the caller's expression. The value is
	// process-wide and installed once, before the first spawn.
	thread_local WorkerCredentials snapshot;
	{
		std::lock_guard lock (g_CredentialsMutex);
		if (!g_CredentialsInitialized)
		{
			// Unconfigured default: the daemon's own effective identity. It is
			// sampled on first use so no static initializer calls geteuid().
			g_Credentials.configured = false;
			g_Credentials.uid = static_cast<uint32_t> (::geteuid ());
			g_Credentials.gid = static_cast<uint32_t> (::getegid ());
			g_CredentialsInitialized = true;
		}
		snapshot = g_Credentials;
	}
	return snapshot;
}

uint32_t WorkerUid ()
{
	return GetWorkerCredentials ().uid;
}

bool IsAllowedParentUid (uint32_t peerUid)
{
	const uint32_t self = static_cast<uint32_t> (::geteuid ());
	if (peerUid == self) return true;
	// A root core that keeps its authority and delegates only the worker role
	// is a valid parent for an unprivileged worker.
	return self != 0 && peerUid == 0;
}

bool DropWorkerCredentialsInChild (const WorkerCredentials & credentials)
{
	if (!credentials.configured) return true;
	// Already at the requested identity with no supplementary groups: there is
	// nothing to drop. Skipping the calls keeps a worker that is already past
	// its pledge (the supervisor) working, because setgroups needs superuser
	// authority and, on OpenBSD, every one of these calls needs the id promise.
	if (static_cast<uint32_t> (::geteuid ()) == credentials.uid
		&& static_cast<uint32_t> (::getegid ()) == credentials.gid && ::getgroups (0, nullptr) == 0)
		return true;
	// setgroups(2) is restricted to the superuser. A child that is already
	// unprivileged cannot clear supplementary groups; the verification below
	// still fails closed when any remain.
	if (::geteuid () == 0 && ::setgroups (0, nullptr) != 0) return false;
	// gid before uid: dropping the uid first would remove the authority that
	// the group change needs.
	if (::setgid (static_cast<gid_t> (credentials.gid)) != 0) return false;
	if (::setuid (static_cast<uid_t> (credentials.uid)) != 0) return false;
	if (static_cast<uint32_t> (::getuid ()) != credentials.uid) return false;
	if (static_cast<uint32_t> (::geteuid ()) != credentials.uid) return false;
	if (static_cast<uint32_t> (::getgid ()) != credentials.gid) return false;
	if (static_cast<uint32_t> (::getegid ()) != credentials.gid) return false;
	// getgroups(0, nullptr) returns the count, so any value other than zero
	// (including -1 for a lookup failure) is a mismatch.
	if (::getgroups (0, nullptr) != 0) return false;
	return true;
}

std::string DescribeWorkerCredentials (const WorkerCredentials & credentials)
{
	// Identity numbers only: no name, no password field, no home directory and
	// no supplementary group list.
	return "uid=" + std::to_string (credentials.uid) + " gid=" + std::to_string (credentials.gid);
}
}

#else // _WIN32

#include <string_view>

namespace i2p::privsep
{
i2p::expected<WorkerCredentials, std::string> ResolveWorkerCredentials (
	std::string_view, std::string_view)
{
	return i2p::unexpected (std::string ("worker credentials are not supported on this platform"));
}

void SetWorkerCredentials (const WorkerCredentials &)
{
}

const WorkerCredentials & GetWorkerCredentials ()
{
	static const WorkerCredentials credentials;
	return credentials;
}

uint32_t WorkerUid ()
{
	return 0;
}

bool IsAllowedParentUid (uint32_t)
{
	return false;
}

bool DropWorkerCredentialsInChild (const WorkerCredentials &)
{
	// There is no worker process to drop into on this platform.
	return false;
}

std::string DescribeWorkerCredentials (const WorkerCredentials &)
{
	return { };
}
}

#endif
