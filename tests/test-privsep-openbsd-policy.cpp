/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test privsep openbsd policy.
 *
 * OpenBSD policy and negative-capability checks: per-role pledge/unveil separation, preopened descriptor validation, and forked children proving storage cannot use the network and network cannot write files.
 *
 */

#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <stdlib.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "privsep/OpenBSDSandbox.h"

using namespace i2p::privsep;

#if !defined(__OpenBSD__)
int main ()
{
	return 0;
}
#else
namespace
{
OpenBSDPolicyConfig BaseConfig ()
{
	OpenBSDPolicyConfig config;
	config.useUnveil = false;
	config.usePledge = true;
	return config;
}

template <typename Action> void ExpectPledgeViolation (OpenBSDWorkerRole role, Action action)
{
	const pid_t pid = fork ();
	assert (pid >= 0);
	if (pid == 0)
	{
		auto policy = BuildOpenBSDPolicy (role, BaseConfig ());
		if (!policy) _exit (2);
		if (!ApplyOpenBSDPolicy (*policy)) _exit (3);
		action ();
		_exit (0);
	}
	int status = 0;
	pid_t reaped;
	do reaped = waitpid (pid, &status, 0);
	while (reaped < 0 && errno == EINTR);
	assert (reaped == pid);
	assert (WIFSIGNALED (status));
}

void TestPolicySeparation ()
{
	auto storage = BuildOpenBSDPolicy (OpenBSDWorkerRole::Storage, BaseConfig ());
	assert (storage.has_value ());
	assert (!storage->HasExternalNetwork ());
	assert (storage->HasPersistentWrite ());
	assert (!storage->HasProcessCreation ());
	assert (storage->HasLocalIpc ());

	auto network = BuildOpenBSDPolicy (OpenBSDWorkerRole::Network, BaseConfig ());
	assert (network.has_value ());
	assert (network->HasExternalNetwork ());
	assert (!network->HasPersistentWrite ());
	assert (!network->HasProcessCreation ());
	assert (network->HasLocalIpc ());

	auto supervisor = BuildOpenBSDPolicy (OpenBSDWorkerRole::Supervisor, BaseConfig ());
	assert (supervisor.has_value ());
	assert (!supervisor->HasExternalNetwork ());
	assert (!supervisor->HasPersistentWrite ());
	assert (supervisor->HasProcessCreation ());
	assert (supervisor->HasLocalIpc ());

	auto crypto = BuildOpenBSDPolicy (OpenBSDWorkerRole::Crypto, BaseConfig ());
	assert (crypto.has_value ());
	assert (!crypto->HasExternalNetwork ());
	assert (!crypto->HasPersistentWrite ());

	auto admin = BuildOpenBSDPolicy (OpenBSDWorkerRole::Admin, BaseConfig ());
	assert (admin.has_value ());
	assert (!admin->HasExternalNetwork ());
	assert (!admin->HasPersistentWrite ());

	// A syslog log worker gets /dev/log but no persistent-write promise; a
	// pre-opened file worker gets read-only path access and no wpath/cpath.
	OpenBSDPolicyConfig logConfig;
	logConfig.useUnveil = true;
	logConfig.logToSyslog = true;
	auto syslogPolicy = BuildOpenBSDPolicy (OpenBSDWorkerRole::Log, logConfig);
	assert (syslogPolicy.has_value ());
	assert (!syslogPolicy->HasExternalNetwork ());
	assert (!syslogPolicy->HasPersistentWrite ());
	assert (syslogPolicy->unveil.size () == 1);
	assert (syslogPolicy->unveil[0].path == "/dev/log");

	logConfig = { };
	logConfig.useUnveil = true;
	logConfig.logPath = "/var/i2pd/i2pd.log";
	logConfig.preopenedLog = true;
	auto fdPolicy = BuildOpenBSDPolicy (OpenBSDWorkerRole::Log, logConfig);
	assert (fdPolicy.has_value ());
	assert (!fdPolicy->HasPersistentWrite ());
	assert (fdPolicy->unveil.size () == 1);
	assert (fdPolicy->unveil[0].path == "/etc/localtime");
}

void TestPreopenedDescriptorValidation ()
{
	auto directory = OpenPreopenedDirectory ("/tmp");
	assert (directory.has_value ());
	assert (ValidatePreopenedDescriptor (*directory, true).has_value ());
	assert (!ValidatePreopenedDescriptor (*directory, false).has_value ());
	close (*directory);

	char name[] = "/tmp/openi2pd-privsep-XXXXXX";
	const int fd = mkstemp (name);
	assert (fd >= 0);
	assert (fcntl (fd, F_SETFD, FD_CLOEXEC) == 0);
	assert (ValidatePreopenedDescriptor (fd, false).has_value ());
	assert (!ValidatePreopenedDescriptor (fd, true).has_value ());
	close (fd);
	unlink (name);
}

void TestStorageCannotUseExternalNetwork ()
{
	ExpectPledgeViolation (OpenBSDWorkerRole::Storage,
		[]
		{
			const int fd = socket (AF_INET, SOCK_STREAM, 0);
			if (fd >= 0) close (fd);
		});
}

void TestNetworkCannotWritePersistentFile ()
{
	ExpectPledgeViolation (OpenBSDWorkerRole::Network,
		[]
		{
			const int fd = open ("/tmp/openi2pd-privsep-network-write", O_WRONLY | O_CREAT | O_TRUNC, 0600);
			if (fd >= 0)
			{
				close (fd);
				unlink ("/tmp/openi2pd-privsep-network-write");
			}
		});
}
}

int main ()
{
	TestPolicySeparation ();
	TestPreopenedDescriptorValidation ();
	TestStorageCannotUseExternalNetwork ();
	TestNetworkCannotWritePersistentFile ();
	return 0;
}
#endif
