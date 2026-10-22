/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test privsep linux sandbox.
 *
 * Runs forked children under the Linux worker profiles to verify Landlock/capability separation: storage cannot use the network, network cannot write persistent files, and log/supervisor policies stay minimal.
 *
 */

#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <linux/capability.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

#include "privsep/LinuxSandbox.h"

using namespace i2p::privsep;

#if !defined(__linux__)
int main ()
{
	return 0;
}
#else
namespace
{
struct ChildResult
{
	int status = -1;
};

ChildResult RunChild (const LinuxSandboxProfile & profile, int (*body) ())
{
	pid_t pid = fork ();
	assert (pid >= 0);
	if (pid == 0)
	{
		auto applied = ApplyLinuxSandbox (profile);
		if (!applied) _exit (77);
		_exit (body ());
	}
	int status = 0;
	assert (waitpid (pid, &status, 0) == pid);
	return ChildResult { status };
}

int ExpectStorageDeniesNetwork ()
{
	errno = 0;
	const int fd = socket (AF_INET, SOCK_STREAM, 0);
	if (fd >= 0)
	{
		close (fd);
		return 2;
	}
	return (errno == EACCES || errno == EPERM) ? 0 : 3;
}

int ExpectStorageAllowsWritablePath (const std::string & file)
{
	const int fd = open (file.c_str (), O_WRONLY | O_CREAT | O_CLOEXEC, 0600);
	if (fd < 0) return 2;
	if (write (fd, "x", 1) != 1)
	{
		close (fd);
		return 3;
	}
	close (fd);
	return 0;
}

int ExpectStorageDeniesOutsideWrite (const std::string & file)
{
	errno = 0;
	const int fd = open (file.c_str (), O_WRONLY | O_CREAT | O_CLOEXEC, 0600);
	if (fd >= 0)
	{
		close (fd);
		return 2;
	}
	return (errno == EACCES || errno == EPERM) ? 0 : 3;
}

int ExpectNetworkDeniesWrite (const std::string & file)
{
	errno = 0;
	const int fd = open (file.c_str (), O_WRONLY | O_CREAT | O_CLOEXEC, 0600);
	if (fd >= 0)
	{
		close (fd);
		return 2;
	}
	return (errno == EACCES || errno == EPERM) ? 0 : 3;
}

int ExpectNoEffectiveCapabilities ()
{
	struct __user_cap_header_struct header { _LINUX_CAPABILITY_VERSION_3, 0 };
	struct __user_cap_data_struct data[2] { };
	if (syscall (SYS_capget, &header, data) != 0) return 2;
	if (data[0].effective != 0 || data[0].permitted != 0 || data[1].effective != 0 || data[1].permitted != 0) return 3;
	return 0;
}

int ExpectNetworkCanCreateSocket ()
{
	const int fd = socket (AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) return 2;
	close (fd);
	return 0;
}

int ExpectStorageDeniesProcessMemory ()
{
#if defined(SYS_process_vm_readv)
	struct iovec local { };
	struct iovec remote { };
	errno = 0;
	const long result = syscall (SYS_process_vm_readv, getppid (), &local, 1, &remote, 1, 0);
	if (result >= 0) return 2;
	return (errno == EACCES || errno == EPERM) ? 0 : 3;
#else
	return 0;
#endif
}

int ExpectStorageDeniesPidfdOpen ()
{
#if defined(SYS_pidfd_open)
	errno = 0;
	const long result = syscall (SYS_pidfd_open, getpid (), 0);
	if (result >= 0)
	{
		close (static_cast<int> (result));
		return 2;
	}
	return (errno == EACCES || errno == EPERM) ? 0 : 3;
#else
	return 0;
#endif
}

int ExpectStorageDeniesSocketpair ()
{
	errno = 0;
	int fds[2] = { -1, -1 };
	if (socketpair (AF_UNIX, SOCK_SEQPACKET, 0, fds) == 0)
	{
		close (fds[0]);
		close (fds[1]);
		return 2;
	}
	return (errno == EACCES || errno == EPERM) ? 0 : 3;
}

int ExpectLogDeniesInetAllowsUnix ()
{
	errno = 0;
	const int inet = socket (AF_INET, SOCK_STREAM, 0);
	if (inet >= 0)
	{
		close (inet);
		return 2;
	}
	if (errno != EACCES && errno != EPERM) return 3;
	const int unixSocket = socket (AF_UNIX, SOCK_DGRAM, 0);
	if (unixSocket < 0) return 4;
	close (unixSocket);
	return 0;
}

int ExpectSupervisorAllowsUnixSocketpair ()
{
	int fds[2] = { -1, -1 };
	if (socketpair (AF_UNIX, SOCK_SEQPACKET, 0, fds) != 0) return 2;
	close (fds[0]);
	close (fds[1]);
	return 0;
}

int ExpectSupervisorBootstrapPreservesDelegation ()
{
	// The supervisor must drop capabilities without installing an inherited
	// Landlock/seccomp/netns domain: exec'ed storage and network workers can
	// only become stricter, so a supervisor restriction here would silently
	// break their delegated capability.
	if (ExpectNoEffectiveCapabilities () != 0) return 2;
	if (ExpectSupervisorAllowsUnixSocketpair () != 0) return 3;
	if (ExpectNetworkCanCreateSocket () != 0) return 4;
	// A write-deny Landlock domain would also be inherited; verify a new
	// temporary file can still be created for a delegated child.
	char path[] = "/tmp/openi2pd-supervisor-bootstrap-XXXXXX";
	const int fd = mkstemp (path);
	if (fd < 0) return 5;
	close (fd);
	unlink (path);
	return 0;
}

ChildResult RunSupervisorBootstrapChild ()
{
	pid_t pid = fork ();
	assert (pid >= 0);
	if (pid == 0)
	{
		auto applied = ApplyLinuxSupervisorSandbox ();
		if (!applied) _exit (77);
		_exit (ExpectSupervisorBootstrapPreservesDelegation ());
	}
	int status = 0;
	assert (waitpid (pid, &status, 0) == pid);
	return ChildResult { status };
}

void RequireExitZero (ChildResult result, const char * name)
{
	// A 77 from the supervisor-bootstrap child is an unsupported-kernel
	// failure, not a skip: the parent already made the explicit filesystem
	// capability probe and this helper is only used for the negative policy
	// checks that must run when the kernel supports them.
	assert (WIFEXITED (result.status));
	assert (WEXITSTATUS (result.status) == 0);
	(void)name;
}
}

int main ()
{
	auto storage = GetLinuxSandboxProfile (WorkerClass::Storage);
	assert (!storage.allowNetwork);
	assert (storage.allowPersistentWrites);
	assert (storage.denySocketCreation);

	auto network = GetLinuxSandboxProfile (WorkerClass::Network);
	assert (network.allowNetwork);
	assert (!network.allowPersistentWrites);
	assert (!network.denySocketCreation);

	auto log = GetLinuxSandboxProfile (WorkerClass::Log);
	assert (!log.allowNetwork);
	assert (log.denyInetSockets);
	assert (!log.denySocketCreation);

	auto supervisor = GetLinuxSandboxProfile (WorkerClass::Supervisor);
	assert (!supervisor.allowNetwork);
	assert (!supervisor.allowPersistentWrites);
	assert (supervisor.denyInetSockets);
	assert (!supervisor.denySocketCreation);

	// The policy constructor must reject a contradictory profile.
	LinuxSandboxProfile contradictory;
	contradictory.allowNetwork = true;
	contradictory.allowPersistentWrites = true;
	auto rejected = ApplyLinuxSandbox (contradictory);
	assert (!rejected.has_value ());

	// The supervisor bootstrap is capability-only. Its exec'ed children apply
	// their own role policy, so it must not install an inherited domain.
	RequireExitZero (RunSupervisorBootstrapChild (), "supervisor bootstrap");

	const auto support = ProbeLinuxIsolation ();
	if (!support.filesystem)
	{
		// The static policy assertions above ran, but the negative Landlock
		// enforcement body cannot. Report SKIP so CI does not mistake this for
		// a completed sandbox test.
		return 77;
	}

	char directory[] = "/tmp/openi2pd-linux-sandbox-XXXXXX";
	char * created = mkdtemp (directory);
	assert (created);
	const std::string writable = std::string (created) + "/inside";
	// Keep the denied path outside the delegated directory; a sibling under
	// `created` would correctly be writable and would make the negative test
	// meaningless.
	const std::string outside = std::string (created) + "-outside";

	LinuxSandboxProfile storageProfile = storage;
	storageProfile.writablePaths = { created };
	// The two checks are separate child processes because a successful
	// Landlock restriction cannot be undone in the same process.
	RequireExitZero (RunChild (storageProfile, &ExpectStorageDeniesNetwork), "storage network");
	RequireExitZero (RunChild (storageProfile, &ExpectStorageDeniesSocketpair), "storage socketpair");
	RequireExitZero (RunChild (storageProfile, &ExpectStorageDeniesProcessMemory), "storage process memory");
	RequireExitZero (RunChild (storageProfile, &ExpectStorageDeniesPidfdOpen), "storage pidfd open");
	RequireExitZero (RunChild (storageProfile, &ExpectNoEffectiveCapabilities), "storage capabilities");
	RequireExitZero (RunChild (storageProfile, [] () { return 0; }), "storage start");

	LinuxSandboxProfile logProfile = log;
	logProfile.requireLandlock = false;
	RequireExitZero (RunChild (logProfile, &ExpectLogDeniesInetAllowsUnix), "log inet/unix");

	LinuxSandboxProfile supervisorProfile = supervisor;
	supervisorProfile.requireLandlock = false;
	RequireExitZero (RunChild (supervisorProfile, &ExpectSupervisorAllowsUnixSocketpair), "supervisor socketpair");

	// A writable directory is allowed for storage; write into it in a child.
	LinuxSandboxProfile storageWrite = storage;
	storageWrite.writablePaths = { created };
	pid_t writePid = fork ();
	assert (writePid >= 0);
	if (writePid == 0)
	{
		auto applied = ApplyLinuxSandbox (storageWrite);
		if (!applied) _exit (77);
		_exit (ExpectStorageAllowsWritablePath (writable));
	}
	int writeStatus = 0;
	assert (waitpid (writePid, &writeStatus, 0) == writePid);
	RequireExitZero (ChildResult { writeStatus }, "storage write");
	unlink (writable.c_str ());

	pid_t outsidePid = fork ();
	assert (outsidePid >= 0);
	if (outsidePid == 0)
	{
		auto applied = ApplyLinuxSandbox (storageProfile);
		if (!applied) _exit (77);
		_exit (ExpectStorageDeniesOutsideWrite (outside));
	}
	int outsideStatus = 0;
	assert (waitpid (outsidePid, &outsideStatus, 0) == outsidePid);
	RequireExitZero (ChildResult { outsideStatus }, "storage outside write");
	unlink (outside.c_str ());

	LinuxSandboxProfile networkProfile = network;
	pid_t networkWritePid = fork ();
	assert (networkWritePid >= 0);
	if (networkWritePid == 0)
	{
		auto applied = ApplyLinuxSandbox (networkProfile);
		if (!applied) _exit (77);
		_exit (ExpectNetworkDeniesWrite (outside));
	}
	int networkWriteStatus = 0;
	assert (waitpid (networkWritePid, &networkWriteStatus, 0) == networkWritePid);
	RequireExitZero (ChildResult { networkWriteStatus }, "network write");
	unlink (outside.c_str ());

	RequireExitZero (RunChild (networkProfile, &ExpectNetworkCanCreateSocket), "network socket");
	rmdir (created);
	return 0;
}
#endif
