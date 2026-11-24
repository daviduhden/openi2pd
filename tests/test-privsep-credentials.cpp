/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test privsep worker credentials.
 *
 * Checks the worker service identity contract on Linux and OpenBSD:
 * bounded account and group resolution, refusal of the superuser,
 * refusal of a group without a user, bounded rejection of malformed
 * names, the fixed non-secret diagnostic, and the forked credential
 * drop (empty supplementary groups, real/effective ids equal to the
 * requested pair). The default cases use the current non-root identity
 * or a nonexistent name. The OpenBSD VM additionally selects the
 * privileged drop mode with an explicitly provisioned non-root account.
 * Prepared for manual CI only; this file must not be executed locally.
 */

#if !defined(__OpenBSD__) && !defined(__linux__)

#include <cstdio>

int main ()
{
	std::fprintf (stderr, "test-privsep-credentials: Unix only, skipping\n");
	return 77;
}

#else

#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <grp.h>
#include <pwd.h>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "privsep/Credentials.h"
#include "capability-skip.h"

using namespace i2p::privsep;

namespace
{
std::string NonexistentName (const char * prefix)
{
	return std::string (prefix) + "-openi2pd-" + std::to_string (::getpid ()) + "-" + std::to_string (::geteuid ());
}

void CheckCredentialDrop (const WorkerCredentials & credentials, bool shouldDrop, bool seedRootGroup = false)
{
	const pid_t pid = ::fork ();
	assert (pid >= 0);
	if (pid == 0)
	{
		if (seedRootGroup)
		{
			const gid_t rootGroup = 0;
			if (::setgroups (1, &rootGroup) != 0 || ::getgroups (0, nullptr) <= 0) _exit (3);
		}
		const bool dropped = DropWorkerCredentialsInChild (credentials);
		if (dropped != shouldDrop) _exit (1);
		if (dropped
			&& (static_cast<uint32_t> (::getuid ()) != credentials.uid
				|| static_cast<uint32_t> (::geteuid ()) != credentials.uid
				|| static_cast<uint32_t> (::getgid ()) != credentials.gid
				|| static_cast<uint32_t> (::getegid ()) != credentials.gid || ::getgroups (0, nullptr) != 0))
			_exit (2);
		_exit (0);
	}
	int status = 0;
	assert (::waitpid (pid, &status, 0) == pid);
	assert (WIFEXITED (status) && WEXITSTATUS (status) == 0);
}
}

int main (int argc, char ** argv)
{
	if (argc == 3 && std::string_view (argv[1]) == "--verify-privileged-drop")
	{
		assert (::getuid () == 0 && ::geteuid () == 0);
		const auto target = ResolveWorkerCredentials (argv[2], "");
		assert (target && target->configured && target->uid != 0 && target->gid != 0);
		CheckCredentialDrop (*target, true, true);
		return 0;
	}
	assert (argc == 1);
	// A root test runner would exercise the privilege-raising path and can
	// never reach the unprivileged expectations below.
	if (::geteuid () == 0) return i2p::test::SkipCapability ("worker credentials require a non-root runner");
	// Skip rather than guessing when the account database cannot describe
	// the identity the test itself runs as.
	const struct passwd * self = ::getpwuid (::geteuid ());
	if (!self || !self->pw_name || !*self->pw_name)
		return i2p::test::SkipCapability ("runner user is absent from the account database");
	const struct group * selfGroup = ::getgrgid (::getegid ());
	if (!selfGroup || !selfGroup->gr_name || !*selfGroup->gr_name)
		return i2p::test::SkipCapability ("runner group is absent from the account database");
	const std::string user (self->pw_name);
	const std::string selfGroupName (selfGroup->gr_name);

	// No override keeps the daemon's own effective identity and is
	// explicitly unconfigured, so no privilege call is attempted at all.
	{
		const auto resolved = ResolveWorkerCredentials ("", "");
		assert (resolved.has_value ());
		assert (!resolved->configured);
		assert (resolved->uid == static_cast<uint32_t> (::geteuid ()));
		assert (resolved->gid == static_cast<uint32_t> (::getegid ()));
	}

	// The current account resolves to itself and its primary group is used
	// when
	// no group is configured.
	{
		const auto resolved = ResolveWorkerCredentials (user, "");
		assert (resolved.has_value ());
		assert (resolved->configured);
		assert (resolved->uid == static_cast<uint32_t> (self->pw_uid));
		assert (resolved->gid == static_cast<uint32_t> (self->pw_gid));
	}

	// An explicit group replaces the primary group of the account.
	{
		const auto resolved = ResolveWorkerCredentials (user, selfGroupName);
		assert (resolved.has_value ());
		assert (resolved->configured);
		assert (resolved->uid == static_cast<uint32_t> (self->pw_uid));
		assert (resolved->gid == static_cast<uint32_t> (selfGroup->gr_gid));
	}

	// Unknown names, a group without a user and malformed names fail
	// closed.
	// The diagnostics are fixed strings: the account name never appears.
	{
		const std::string missing = NonexistentName ("account");
		const auto unknownUser = ResolveWorkerCredentials (missing, "");
		assert (!unknownUser.has_value ());
		assert (unknownUser.error ().find (missing) == std::string::npos);
		const auto unknownGroup = ResolveWorkerCredentials (user, missing);
		assert (!unknownGroup.has_value ());
		assert (unknownGroup.error ().find (missing) == std::string::npos);
		const auto groupOnly = ResolveWorkerCredentials ("", selfGroupName);
		assert (!groupOnly.has_value ());
		assert (!ResolveWorkerCredentials (std::string ("ro\0ot", 5), "").has_value ());
		assert (!ResolveWorkerCredentials (std::string ("ro\tot"), "").has_value ());
		assert (!ResolveWorkerCredentials (std::string (4096, 'a'), "").has_value ());
		assert (!ResolveWorkerCredentials (user, std::string (4096, 'a')).has_value ());
	}

	// A name that resolves to uid 0 must be refused; skip when the account
	// database does not expose a root name at all.
	{
		const struct passwd * superuser = ::getpwuid (0);
		if (superuser && superuser->pw_name && *superuser->pw_name)
		{
			const auto resolved = ResolveWorkerCredentials (superuser->pw_name, "");
			assert (!resolved.has_value ());
		}
	}

	// The diagnostic carries the identity numbers and nothing else.
	{
		const WorkerCredentials credentials { true, 1234, 5678 };
		assert (DescribeWorkerCredentials (credentials) == "uid=1234 gid=5678");
	}

	// A different identity cannot be adopted by an unprivileged process:
	// the setuid must fail and the drop must report it instead of
	// continuing.
	{
		const uint32_t foreignUid = static_cast<uint32_t> (::geteuid ()) + 1;
		if (foreignUid != 0)
		{
			const WorkerCredentials foreign { true, foreignUid, static_cast<uint32_t> (::getegid ()) };
			CheckCredentialDrop (foreign, false);
		}
	}

	// The drop verifies the real/effective ids and an empty supplementary
	// set. A non-root caller with supplementary groups cannot clear them,
	// and must therefore see a failure even for its own identity.
	const int supplementaryGroups = ::getgroups (0, nullptr);
	assert (supplementaryGroups >= 0);
	const WorkerCredentials credentials { true, static_cast<uint32_t> (::geteuid ()),
		static_cast<uint32_t> (::getegid ()) };
	CheckCredentialDrop (credentials, supplementaryGroups == 0);
	return 0;
}

#endif
