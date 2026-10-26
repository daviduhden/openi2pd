/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test privsep openbsd policy.
 *
 * OpenBSD policy and negative-capability checks: per-role pledge/unveil
 * separation, the supervisor's x-only executable unveil rule, the broker's
 * sendfd-only descriptor contract, the startup/runtime promise relationship,
 * the single pledge-file parser and preopened descriptor validation. Forked
 * children prove that storage cannot use the network or fork, that the
 * supervisor cannot exec an arbitrary image, and that unveil cannot be
 * re-opened after the lock.
 *
 */

#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <signal.h>
#include <span>
#include <stdlib.h>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "privsep/OpenBSDSandbox.h"
#if defined(__OpenBSD__)
#include "privsep/Supervisor.h"
#endif

using namespace i2p::privsep;

#if !defined(__OpenBSD__)
int main ()
{
	// The sandbox primitives exist only on OpenBSD; CTest treats 77 as SKIP.
	return 77;
}
#else
namespace
{
/** A fixed absolute path under an existing directory; the leaf need not exist. */
constexpr const char * PROBE_EXECUTABLE = "/tmp/openi2pd-supervisor-probe";

OpenBSDPolicyConfig BaseConfig ()
{
	OpenBSDPolicyConfig config;
	config.useUnveil = false;
	config.usePledge = true;
	config.executablePath = PROBE_EXECUTABLE;
	return config;
}

bool HasPromiseText (std::string_view list, std::string_view name)
{
	size_t pos = 0;
	while (pos < list.size ())
	{
		while (pos < list.size () && list[pos] == ' ') ++pos;
		const size_t begin = pos;
		while (pos < list.size () && list[pos] != ' ') ++pos;
		if (begin != pos && list.substr (begin, pos - begin) == name) return true;
	}
	return false;
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
	// The policy was applied (the child got past the exit codes above) and the
	// forbidden operation was a pledge violation: an uncatchable SIGABRT.
	assert (WIFSIGNALED (status));
	assert (WTERMSIG (status) == SIGABRT);
}

/** Run @a body in a child that applied @a policy; assert a clean exit. */
template <typename Body> void ExpectPolicyExit (const OpenBSDPolicy & policy, Body body)
{
	const pid_t pid = fork ();
	assert (pid >= 0);
	if (pid == 0)
	{
		if (!ApplyOpenBSDPolicy (policy)) _exit (2);
		_exit (body ());
	}
	int status = 0;
	pid_t reaped;
	do reaped = waitpid (pid, &status, 0);
	while (reaped < 0 && errno == EINTR);
	assert (reaped == pid);
	assert (WIFEXITED (status));
	assert (WEXITSTATUS (status) == 0);
}

std::string ResolveSelf (const char * argv0)
{
	std::string candidate = argv0 ? argv0 : "";
	if (candidate.empty ()) return candidate;
	if (candidate.front () != '/')
	{
		char cwd[PATH_MAX];
		if (!getcwd (cwd, sizeof (cwd))) return candidate;
		candidate = std::string (cwd) + "/" + candidate;
	}
	char resolved[PATH_MAX];
	if (realpath (candidate.c_str (), resolved)) return resolved;
	return candidate;
}

void TestPolicySeparation ()
{
	auto storage = BuildOpenBSDPolicy (OpenBSDWorkerRole::Storage, BaseConfig ());
	assert (storage.has_value ());
	assert (!storage->HasExternalNetwork ());
	assert (storage->HasPersistentWrite ());
	assert (!storage->HasProcessCreation ());
	assert (storage->HasLocalIpc ());
	// The storage role must never carry an external-network promise, even
	// through a future configuration flag.
	for (const char * promise : { "inet", "dns", "mcast", "route" })
		assert (storage->pledge.find (promise) == std::string::npos);
	auto missingRoot = BaseConfig ();
	missingRoot.useUnveil = true;
	missingRoot.dataDir.clear ();
	assert (!BuildOpenBSDPolicy (OpenBSDWorkerRole::Storage, missingRoot).has_value ());
	auto inheritedConfig = BaseConfig ();
	inheritedConfig.preopenedStorageIpc = true;
	auto inheritedStorage = BuildOpenBSDPolicy (OpenBSDWorkerRole::Storage, inheritedConfig);
	assert (inheritedStorage && !inheritedStorage->HasExternalNetwork ());
	assert (inheritedStorage->HasPersistentWrite () && !inheritedStorage->HasProcessCreation ());
	assert (!inheritedStorage->HasLocalIpc ()); // No new UNIX endpoints/fd reception.

	auto network = BuildOpenBSDPolicy (OpenBSDWorkerRole::Network, BaseConfig ());
	assert (network.has_value ());
	assert (network->HasExternalNetwork ());
	assert (!network->HasPersistentWrite ());
	assert (!network->HasProcessCreation ());
	assert (network->HasLocalIpc ());
	assert (network->pledge.find ("recvfd") == std::string::npos);
	OpenBSDPolicyConfig handoff = BaseConfig ();
	handoff.socketHandoff = true;
	handoff.dnsLookup = false;
	handoff.routeInfo = true;
	auto networkHandoff = BuildOpenBSDPolicy (OpenBSDWorkerRole::Network, handoff);
	assert (networkHandoff.has_value ());
	assert (networkHandoff->pledge.find ("recvfd") != std::string::npos);
	assert (networkHandoff->pledge.find ("route") != std::string::npos);
	assert (networkHandoff->pledge.find ("dns") == std::string::npos);

	auto supervisor = BuildOpenBSDPolicy (OpenBSDWorkerRole::Supervisor, BaseConfig ());
	assert (supervisor.has_value ());
	assert (!supervisor->HasExternalNetwork ());
	assert (!supervisor->HasPersistentWrite ());
	assert (supervisor->HasProcessCreation ());
	// "unix" would allow AF_UNIX connect/bind at any pathname without rpath or
	// cpath; stdio already provides socketpair(2) for the worker channel.
	assert (!supervisor->HasLocalIpc ());
	assert (supervisor->pledge == "stdio proc exec");
	// Unveil is forced on for this role and carries exactly one "x" rule for the
	// frozen executable path, so no second image can be exec'ed.
	assert (supervisor->useUnveil);
	assert (supervisor->unveil.size () == 1);
	assert (supervisor->unveil[0].path == PROBE_EXECUTABLE);
	assert (supervisor->unveil[0].permissions == "x");

	auto broker = BuildOpenBSDPolicy (OpenBSDWorkerRole::StorageBroker, BaseConfig ());
	assert (broker.has_value ());
	assert (broker->pledge == "stdio proc exec sendfd");
	assert (!broker->HasLocalIpc ());
	assert (broker->HasProcessCreation ());
	assert (!broker->HasExternalNetwork ());
	assert (!broker->HasPersistentWrite ());
	assert (broker->pledge.find ("recvfd") == std::string::npos);
	assert (broker->useUnveil && broker->unveil.size () == 1);
	assert (broker->unveil[0].path == PROBE_EXECUTABLE && broker->unveil[0].permissions == "x");

	// A configured worker identity adds "id" only to the two process-creation
	// roles: the forked child drops supplementary groups, gid and uid there
	// immediately before execve. Leaf workers never gain it.
	OpenBSDPolicyConfig credentialed = BaseConfig ();
	credentialed.dropCredentials = true;
	auto supervisorDrop = BuildOpenBSDPolicy (OpenBSDWorkerRole::Supervisor, credentialed);
	assert (supervisorDrop.has_value () && supervisorDrop->pledge == "stdio proc exec id");
	auto brokerDrop = BuildOpenBSDPolicy (OpenBSDWorkerRole::StorageBroker, credentialed);
	assert (brokerDrop.has_value () && brokerDrop->pledge == "stdio proc exec sendfd id");
	auto storageDrop = BuildOpenBSDPolicy (OpenBSDWorkerRole::Storage, credentialed);
	assert (storageDrop.has_value () && !HasPromiseText (storageDrop->pledge, "id"));

	auto crypto = BuildOpenBSDPolicy (OpenBSDWorkerRole::Crypto, BaseConfig ());
	assert (crypto.has_value ());
	assert (!crypto->HasExternalNetwork ());
	assert (!crypto->HasPersistentWrite ());
	assert (!crypto->HasProcessCreation ());

	auto admin = BuildOpenBSDPolicy (OpenBSDWorkerRole::Admin, BaseConfig ());
	assert (admin.has_value ());
	assert (!admin->HasExternalNetwork ());
	assert (!admin->HasPersistentWrite ());
	assert (!admin->HasProcessCreation ());

	// The single cross-role invariant: external network and persistent writes
	// never coexist in one process, for every role this builder can produce.
	const OpenBSDWorkerRole roles[] = { OpenBSDWorkerRole::Supervisor, OpenBSDWorkerRole::Log,
		OpenBSDWorkerRole::Storage, OpenBSDWorkerRole::Network, OpenBSDWorkerRole::Crypto,
		OpenBSDWorkerRole::Admin, OpenBSDWorkerRole::StorageBroker };
	for (const auto role : roles)
	{
		OpenBSDPolicyConfig config = BaseConfig ();
		config.dataDir = "/var/i2pd";
		config.certsDir = "/var/i2pd/certificates";
		auto policy = BuildOpenBSDPolicy (role, config);
		assert (policy.has_value ());
		assert (!(policy->HasExternalNetwork () && policy->HasPersistentWrite ()));
		// Only the two process-creation roles may hold proc/exec.
		if (role != OpenBSDWorkerRole::Supervisor && role != OpenBSDWorkerRole::StorageBroker)
			assert (!policy->HasProcessCreation ());
	}

	// A log worker that owns its file has no process-creation or network power.
	OpenBSDPolicyConfig logConfig;
	logConfig.useUnveil = true;
	logConfig.logToSyslog = true;
	auto syslogPolicy = BuildOpenBSDPolicy (OpenBSDWorkerRole::Log, logConfig);
	assert (syslogPolicy.has_value ());
	assert (!syslogPolicy->HasExternalNetwork ());
	assert (!syslogPolicy->HasPersistentWrite ());
	assert (!syslogPolicy->HasProcessCreation ());
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

	// The executable path is the only thing the supervisor and broker unveil;
	// an empty, relative or NUL-carrying value fails closed at build time.
	auto rejectConfig = BaseConfig ();
	rejectConfig.executablePath.clear ();
	assert (!BuildOpenBSDPolicy (OpenBSDWorkerRole::Supervisor, rejectConfig).has_value ());
	assert (!BuildOpenBSDPolicy (OpenBSDWorkerRole::StorageBroker, rejectConfig).has_value ());
	rejectConfig.executablePath = "i2pd";
	assert (!BuildOpenBSDPolicy (OpenBSDWorkerRole::Supervisor, rejectConfig).has_value ());
	assert (!BuildOpenBSDPolicy (OpenBSDWorkerRole::StorageBroker, rejectConfig).has_value ());
	rejectConfig.executablePath = std::string ("/usr/local/bin/") + '\0' + "i2pd";
	assert (!BuildOpenBSDPolicy (OpenBSDWorkerRole::Supervisor, rejectConfig).has_value ());
	assert (!BuildOpenBSDPolicy (OpenBSDWorkerRole::StorageBroker, rejectConfig).has_value ());
}

void TestPledgeWindows ()
{
	// The execpromises argument is pinned to null: an exec'ed image starts
	// without any promise and installs its own unveil+pledge.
	static_assert (OPENBSD_PLEDGE_EXECPROMISES == nullptr,
		"pledge(2) must never be called with execpromises");
	assert (!IsPledgePromiseSubset ("recvfd", "stdio proc exec"));
	assert (IsPledgePromiseSubset ("stdio exec", "stdio proc exec"));
	assert (IsPledgePromiseSubset ("", "stdio"));

	for (unsigned mask = 0; mask < 64; ++mask)
	{
		OpenBSDPledgeWindow window;
		window.unveil = (mask & 1) != 0;
		window.upnp = (mask & 2) != 0;
		window.execWorkers = (mask & 4) != 0;
		window.reapChildren = (mask & 8) != 0;
		window.descriptorReception = (mask & 16) != 0;
		window.credentialDrop = (mask & 32) != 0;
		const auto startup = BuildOpenBSDPledgePromises (OpenBSDPledgePhase::Startup, window);
		const auto runtime = BuildOpenBSDPledgePromises (OpenBSDPledgePhase::Runtime, window);
		// A promise can only be dropped: the runtime set is always a subset.
		assert (IsPledgePromiseSubset (runtime, startup));
		// The runtime window never regains process creation, identity changes,
		// unveil or descriptor sending.
		assert (!HasPromiseText (runtime, "exec"));
		assert (!HasPromiseText (runtime, "id"));
		assert (!HasPromiseText (runtime, "unveil"));
		assert (!HasPromiseText (runtime, "sendfd"));
		if (window.reapChildren)
			assert (HasPromiseText (runtime, "proc"));
		else
			assert (!HasPromiseText (runtime, "proc"));
		if (window.descriptorReception)
			assert (HasPromiseText (runtime, "recvfd"));
		else
			assert (!HasPromiseText (runtime, "recvfd"));
		if (window.unveil) assert (HasPromiseText (startup, "unveil"));
		if (window.upnp)
			assert (HasPromiseText (startup, "mcast") && HasPromiseText (runtime, "mcast"));
		if (window.credentialDrop) assert (HasPromiseText (startup, "id"));
		if (window.execWorkers)
			assert (HasPromiseText (startup, "proc") && HasPromiseText (startup, "exec"));
	}

	// The daemon's exact windows: startup is the widest, runtime keeps recvfd
	// for the supervised storage handoff and proc only for the log supervisor.
	OpenBSDPledgeWindow startup;
	startup.unveil = true;
	startup.execWorkers = true;
	startup.descriptorReception = true;
	startup.credentialDrop = true;
	const auto startupPromises = BuildOpenBSDPledgePromises (OpenBSDPledgePhase::Startup, startup);
	assert (HasPromiseText (startupPromises, "recvfd"));
	assert (HasPromiseText (startupPromises, "exec"));
	assert (HasPromiseText (startupPromises, "id"));
	OpenBSDPledgeWindow runtime;
	runtime.descriptorReception = true;
	runtime.reapChildren = true;
	const auto runtimePromises = BuildOpenBSDPledgePromises (OpenBSDPledgePhase::Runtime, runtime);
	assert (HasPromiseText (runtimePromises, "recvfd"));
	assert (HasPromiseText (runtimePromises, "proc"));
	assert (IsPledgePromiseSubset (runtimePromises, startupPromises));
}

std::string WriteTempPolicy (const std::string & content)
{
	char name[] = "/tmp/openi2pd-pledge-XXXXXX";
	const int fd = mkstemp (name);
	assert (fd >= 0);
	size_t written = 0;
	while (written < content.size ())
	{
		const ssize_t count = write (fd, content.data () + written, content.size () - written);
		if (count <= 0) break;
		written += static_cast<size_t> (count);
	}
	assert (written == content.size ());
	close (fd);
	return name;
}

std::vector<std::string_view> Views (const std::vector<std::string> & values)
{
	std::vector<std::string_view> views;
	views.reserve (values.size ());
	for (const auto & value : values) views.emplace_back (value);
	return views;
}

void TestPledgeFileParser ()
{
	OpenBSDPledgeWindow runtimeWindow;
	runtimeWindow.reapChildren = true;
	runtimeWindow.descriptorReception = true;
	const auto runtimeBase = OpenBSDPledgePromiseList (OpenBSDPledgePhase::Runtime, runtimeWindow);
	const auto runtimeAllowed = Views (runtimeBase);
	OpenBSDPledgeWindow startupWindow;
	startupWindow.unveil = true;
	startupWindow.execWorkers = true;
	startupWindow.descriptorReception = true;
	const auto startupBase = OpenBSDPledgePromiseList (OpenBSDPledgePhase::Startup, startupWindow);
	const auto startupAllowed = Views (startupBase);

	auto check = [] (const std::string & content, std::span<const std::string_view> allowed, OpenBSDPledgePhase phase)
	{
		const auto path = WriteTempPolicy (content);
		auto result = ParseOpenBSDPledgeFile (path, allowed, phase);
		unlink (path.c_str ());
		return result;
	};

	// Selecting from the base set is accepted; duplicates collapse.
	auto selected = check ("stdio\nrecvfd\n", runtimeAllowed, OpenBSDPledgePhase::Runtime);
	assert (selected.has_value ());
	assert (selected->size () == 2);
	assert ((*selected)[0] == "stdio" && (*selected)[1] == "recvfd");
	auto duplicated = check ("stdio\nstdio stdio\n", runtimeAllowed, OpenBSDPledgePhase::Runtime);
	assert (duplicated.has_value () && duplicated->size () == 1);

	// Comments, blank lines, unknown names and oversized tokens are refused so
	// a malformed file can never be read as a silently different policy.
	assert (!check ("# stdio\n", runtimeAllowed, OpenBSDPledgePhase::Runtime).has_value ());
	assert (!check ("\n", runtimeAllowed, OpenBSDPledgePhase::Runtime).has_value ());
	assert (!check ("  \n", runtimeAllowed, OpenBSDPledgePhase::Runtime).has_value ());
	assert (!check ("stdio # trailing\n", runtimeAllowed, OpenBSDPledgePhase::Runtime).has_value ());
	assert (!check ("frobnicate\n", runtimeAllowed, OpenBSDPledgePhase::Runtime).has_value ());
	assert (!check (std::string (40, 'x') + "\n", runtimeAllowed, OpenBSDPledgePhase::Runtime).has_value ());

	// A runtime file may not add process/identity/privilege promises even when
	// the runtime base set contains proc for reaping the log supervisor.
	assert (HasPromiseText (BuildOpenBSDPledgePromises (OpenBSDPledgePhase::Runtime, runtimeWindow), "proc"));
	assert (!check ("proc\n", runtimeAllowed, OpenBSDPledgePhase::Runtime).has_value ());
	assert (!check ("exec\n", startupAllowed, OpenBSDPledgePhase::Runtime).has_value ());
	assert (!check ("id\n", startupAllowed, OpenBSDPledgePhase::Runtime).has_value ());
	assert (!check ("settime\n", startupAllowed, OpenBSDPledgePhase::Runtime).has_value ());

	// A startup file may not add anything outside the startup base set either.
	assert (!check ("mcast\n", startupAllowed, OpenBSDPledgePhase::Startup).has_value ());
	auto startupSelected = check ("exec unveil\n", startupAllowed, OpenBSDPledgePhase::Startup);
	assert (startupSelected.has_value () && startupSelected->size () == 2);

	// A missing file is an error, not an empty policy.
	auto missing = ParseOpenBSDPledgeFile ("/var/empty/openi2pd-pledge-missing", startupAllowed,
		OpenBSDPledgePhase::Startup);
	assert (!missing.has_value ());
}

void TestPreopenedSocketValidation ()
{
	const int datagram = socket (AF_INET, SOCK_DGRAM, 0);
	assert (datagram >= 0);
	assert (fcntl (datagram, F_SETFD, FD_CLOEXEC) == 0);
	assert (ValidatePreopenedSocket (datagram, SOCK_DGRAM, false).has_value ());
	assert (!ValidatePreopenedSocket (datagram, SOCK_STREAM, false).has_value ());
	close (datagram);

	const int stream = socket (AF_INET, SOCK_STREAM, 0);
	assert (stream >= 0);
	assert (fcntl (stream, F_SETFD, FD_CLOEXEC) == 0);
	assert (!ValidatePreopenedSocket (stream, SOCK_STREAM, true).has_value ());
	struct sockaddr_in address { };
	address.sin_len = sizeof (address);
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
	address.sin_port = 0;
	assert (bind (stream, reinterpret_cast<struct sockaddr *> (&address), sizeof (address)) == 0);
	assert (listen (stream, 1) == 0);
	assert (ValidatePreopenedSocket (stream, SOCK_STREAM, true).has_value ());
	close (stream);
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

void TestStorageCannotFork ()
{
	ExpectPledgeViolation (OpenBSDWorkerRole::Storage, [] { (void)fork (); });
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

/**
 * The supervisor holds proc/exec but unveils exactly one path with "x" and
 * nothing else. Executing any other image therefore fails with ENOENT (unveil
 * reports ENOENT for a path with no qualifying permission) while the frozen
 * executable path still runs. The exec'ed probe writes no stamp file: "x" does
 * not grant write or read access.
 */
void TestSupervisorExecUnveil (const std::string & executable)
{
	if (executable.empty () || executable.front () != '/') return;
	OpenBSDPolicyConfig config = BaseConfig ();
	config.executablePath = executable;
	auto policy = BuildOpenBSDPolicy (OpenBSDWorkerRole::Supervisor, config);
	assert (policy.has_value ());
	ExpectPolicyExit (*policy,
		[&executable] () -> int
		{
			char * const deniedArgv[] = { const_cast<char *> ("/bin/sh"), nullptr };
			char * const deniedEnv[] = { nullptr };
			execve ("/bin/sh", deniedArgv, deniedEnv);
			if (errno != ENOENT) return 4;
			// The unveiled image itself is executable. The probe argument makes
			// the re-executed test binary exit immediately.
			char * const allowedArgv[] = { const_cast<char *> (executable.c_str ()),
				const_cast<char *> ("--unveil-exec-probe"), nullptr };
			char * const allowedEnv[] = { nullptr };
			execve (executable.c_str (), allowedArgv, allowedEnv);
			return 5;
		});
}

/** The supervisor pledge has no unveil promise: re-opening unveil aborts. */
void TestSupervisorCannotReopenUnveil ()
{
	ExpectPledgeViolation (OpenBSDWorkerRole::Supervisor, [] { (void)unveil ("/etc", "r"); });
}

/** After the explicit lock, a second unveil call is EPERM. */
void TestUnveilLockIsFinal ()
{
	OpenBSDPolicy policy;
	policy.useUnveil = true;
	policy.usePledge = true;
	policy.pledge = "stdio unveil";
	policy.unveil.push_back ({ "/tmp", "r" });
	ExpectPolicyExit (policy,
		[] () -> int
		{
			if (unveil ("/etc", "r") == 0) return 3;
			if (errno != EPERM) return 4;
			return 0;
		});
	// The log worker's own policy has no unveil promise at all, which is the
	// stronger form of the same guarantee: it cannot widen its rules after
	// ApplyOpenBSDPolicy returned, because any unveil call is a violation.
	ExpectPledgeViolation (OpenBSDWorkerRole::Log, [] { (void)unveil ("/etc", "r"); });
}

/** The in-process spawn gate is per-process and cannot be cleared. */
void TestProcessCreationGate ()
{
	assert (!IsProcessCreationForbidden ());
	ForbidProcessCreation ();
	assert (IsProcessCreationForbidden ());
}
}

int main (int argc, char * argv[])
{
	// Re-executed by TestSupervisorExecUnveil through the unveiled path.
	if (argc == 2 && std::string_view (argv[1]) == "--unveil-exec-probe") return 0;

	const std::string self = ResolveSelf (argc > 0 ? argv[0] : nullptr);
	TestPolicySeparation ();
	TestPledgeWindows ();
	TestPledgeFileParser ();
	TestPreopenedDescriptorValidation ();
	TestPreopenedSocketValidation ();
	TestStorageCannotUseExternalNetwork ();
	TestStorageCannotFork ();
	TestNetworkCannotWritePersistentFile ();
	TestSupervisorExecUnveil (self);
	TestSupervisorCannotReopenUnveil ();
	TestUnveilLockIsFinal ();
	TestProcessCreationGate ();
	return 0;
}
#endif
