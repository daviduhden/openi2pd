/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test privsep worker identity.
 *
 * Checks the per-spawn identity record that binds the exec'ed role and
 * its uid/gid to the process that forked it: the fixed 25-byte layout,
 * codec bounds, a missing/short/duplicated/oversized record, an
 * all-zero nonce, a uid-0 or mismatched identity, a role that disagrees
 * with argv, the pinned standard descriptors, the one-way
 * process-creation barrier and the frozen executable path. A valid
 * record must still serve a Ping. Prepared for manual CI only; this
 * file must not be executed locally.
 */

#include <cassert>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <span>
#include <string>
#include <string_view>
#include <sys/socket.h>
#if defined(__linux__)
#include <sys/prctl.h>
#endif
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "privsep/Channel.h"
#include "privsep/Protocol.h"
#include "privsep/RunWorker.h"
#include "privsep/Supervisor.h"
#include "privsep/Worker.h"

using namespace i2p::privsep;

namespace
{
/**
 * Hello field offsets: [role u8 | nonce 16 | uid u32 BE | gid u32
 * BE].
 */
constexpr size_t UID_OFFSET = 1 + WORKER_HELLO_NONCE_SIZE;
constexpr size_t GID_OFFSET = UID_OFFSET + 4;

std::vector<uint8_t> Nonce (uint8_t seed)
{
	std::vector<uint8_t> nonce (WORKER_HELLO_NONCE_SIZE, 0);
	for (size_t i = 0; i < nonce.size (); ++i) nonce[i] = static_cast<uint8_t> (seed + i);
	return nonce;
}

std::string Hex (const std::vector<uint8_t> & bytes)
{
	static constexpr char digits[] = "0123456789abcdef";
	std::string text;
	for (auto byte : bytes)
	{
		text.push_back (digits[byte >> 4]);
		text.push_back (digits[byte & 0x0f]);
	}
	return text;
}

bool Contains (const std::string & haystack, const std::string & needle)
{
	return !needle.empty () && haystack.find (needle) != std::string::npos;
}

bool ContainsRaw (const std::string & haystack, const std::vector<uint8_t> & needle)
{
	if (needle.size () > haystack.size ()) return false;
	return haystack.find (std::string (needle.begin (), needle.end ())) != std::string::npos;
}

bool WaitForChild (pid_t pid, int & status, int timeoutMs)
{
	const auto deadline = std::chrono::steady_clock::now () + std::chrono::milliseconds (timeoutMs);
	while (std::chrono::steady_clock::now () < deadline)
	{
		const pid_t result = waitpid (pid, &status, WNOHANG);
		if (result == pid) return true;
		if (result < 0 && errno == EINTR) continue;
		if (result < 0) return false;
		std::this_thread::sleep_for (std::chrono::milliseconds (10));
	}
	kill (pid, SIGKILL);
	waitpid (pid, &status, 0);
	return false;
}

std::string ReadAll (int fd)
{
	std::string text;
	char buffer[512];
	while (true)
	{
		const auto count = read (fd, buffer, sizeof (buffer));
		if (count < 0 && errno == EINTR) continue;
		if (count <= 0) break;
		text.append (buffer, static_cast<size_t> (count));
	}
	return text;
}

/**
 * The test binary's own absolute path. It is used both as the frozen
 * spawn path and as the exec target of the hand-started worker
 * scenarios; the production resolver is deliberately not used because
 * it must not search $PATH or fall back to /proc.
 */
std::string SelfExecutable (const char * argv0)
{
	char buffer[4096] = { };
#if defined(__linux__)
	const auto length = readlink ("/proc/self/exe", buffer, sizeof (buffer) - 1);
	if (length > 0 && static_cast<size_t> (length) < sizeof (buffer)) return std::string (buffer, length);
#else
	if (argv0 && std::strchr (argv0, '/'))
	{
		char resolved[4096] = { };
		if (realpath (argv0, resolved)) return resolved;
	}
#endif
	return { };
}

/**
 * Build a raw hello with an arbitrary role/identity, bypassing the
 * codec.
 */
std::vector<uint8_t> RawHello (uint8_t role, const std::vector<uint8_t> & nonce, uint32_t uid, uint32_t gid)
{
	std::vector<uint8_t> record (WORKER_HELLO_SIZE, 0);
	record[0] = role;
	for (size_t i = 0; i < nonce.size () && i < WORKER_HELLO_NONCE_SIZE; ++i) record[1 + i] = nonce[i];
	for (size_t i = 0; i < 4; ++i)
	{
		record[UID_OFFSET + i] = static_cast<uint8_t> (uid >> (24 - 8 * i));
		record[GID_OFFSET + i] = static_cast<uint8_t> (gid >> (24 - 8 * i));
	}
	return record;
}

int RunChildScenario (int (*body) ())
{
	const pid_t pid = fork ();
	assert (pid >= 0);
	if (pid == 0) _exit (body ());
	int status = 0;
	if (!WaitForChild (pid, status, 5000)) return -1;
	if (!WIFEXITED (status)) return -1;
	return WEXITSTATUS (status);
}

struct ScenarioResult
{
	int exitCode = -1;
	std::string error;
};

/**
 * Run the production RunWorker in a child whose descriptor 5 carries @a
 * hello
 * (or no descriptor at all) and whose argv names @a argvRole.
 */
ScenarioResult RunHelloScenario (
	const std::string & self, const char * argvRole, const std::vector<uint8_t> & hello, bool provideHello)
{
	ScenarioResult result;
	auto pair = Channel::CreatePair ();
	assert (pair.has_value ());
	int helloPipe[2] = { -1, -1 };
	if (provideHello)
	{
		assert (pipe (helloPipe) == 0);
		size_t done = 0;
		while (done < hello.size ())
		{
			const auto count = write (helloPipe[1], hello.data () + done, hello.size () - done);
			assert (count > 0);
			done += static_cast<size_t> (count);
		}
	}
	int errorPipe[2] = { -1, -1 };
	assert (pipe (errorPipe) == 0);
	const pid_t pid = fork ();
	assert (pid >= 0);
	if (pid == 0)
	{
		// Make stderr the error pipe first, then move the record above the
		// fixed descriptors: every later dup2 is then alias-safe.
		if (dup2 (errorPipe[1], 2) < 0) _exit (127);
		if (errorPipe[1] != 2) close (errorPipe[1]);
		close (errorPipe[0]);
		int helloRead = helloPipe[0];
		if (helloRead >= 0)
		{
			const int moved = fcntl (helloRead, F_DUPFD_CLOEXEC, 10);
			if (moved < 0) _exit (127);
			close (helloRead);
			helloRead = moved;
		}
		if (helloPipe[1] >= 0) close (helloPipe[1]);
		pair->first.Close ();
#if defined(__linux__)
		// A production worker is exec'ed by the spawn child, which sets this
		// before execve and empties the capability set. Model it here so the
		// RunWorker preflight sees the same contract.
		if (prctl (PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) _exit (127);
#endif
		const int channelFd = pair->second.Fd ();
		if (channelFd != 3)
		{
			if (dup2 (channelFd, 3) < 0) _exit (127);
			close (channelFd);
		}
		const int flags3 = fcntl (3, F_GETFD);
		if (flags3 < 0 || fcntl (3, F_SETFD, flags3 & ~FD_CLOEXEC) < 0) _exit (127);
		if (provideHello && helloRead >= 0)
		{
			if (dup2 (helloRead, 5) < 0) _exit (127);
			if (helloRead != 5) close (helloRead);
			const int flags5 = fcntl (5, F_GETFD);
			if (flags5 < 0 || fcntl (5, F_SETFD, flags5 & ~FD_CLOEXEC) < 0) _exit (127);
		}
		else
			close (5); // no identity record at all
		char * const childArgv[] = { const_cast<char *> ("test-privsep-worker-identity"),
			const_cast<char *> ("--privsep-worker"), const_cast<char *> (argvRole), const_cast<char *> ("3"), nullptr };
		execv (self.c_str (), childArgv);
		_exit (127);
	}
	if (helloPipe[0] >= 0) close (helloPipe[0]);
	if (helloPipe[1] >= 0) close (helloPipe[1]);
	pair->second.Close ();
	close (errorPipe[1]);
	result.error = ReadAll (errorPipe[0]);
	close (errorPipe[0]);
	pair->first.Close ();
	int status = 0;
	assert (WaitForChild (pid, status, 5000));
	result.exitCode = WIFEXITED (status) ? WEXITSTATUS (status) : 128 + WTERMSIG (status);
	return result;
}
}

#include "privsep-test-executable.h"

int main (int argc, char ** argv)
{
	i2p::privsep::PinTestExecutable (argv[0]);
	if (argc == 4 && std::string_view (argv[1]) == "--privsep-worker")
		return i2p::privsep::RunWorker (argv[2], argv[3]);
	if (::geteuid () == 0) return 77;

	// The spawn helpers only use a frozen, validated absolute path.
	const std::string self = SelfExecutable (argc > 0 ? argv[0] : nullptr);
	assert (!self.empty ());
	SetExecutablePath (self);
	const std::string frozen = ExecutablePath ();
	assert (!frozen.empty ());
	assert (frozen.front () == '/');

	// The role table and the hello codec are one contract: every spawnable
	// role has a wire id and a stable name.
	for (auto role : { WorkerRole::Log, WorkerRole::Supervisor, WorkerRole::RouterStorage,
			 WorkerRole::RouterStorageInherited, WorkerRole::TorrentStorage, WorkerRole::TorrentStorageInherited,
			 WorkerRole::TorrentStorageSupervisor, WorkerRole::DataStorage, WorkerRole::DataStorageInherited })
	{
		const auto * name = WorkerRoleName (role);
		assert (name && *name);
		const auto parsed = WorkerRoleFromName (name);
		assert (parsed.has_value () && *parsed == role);
	}
	assert (!WorkerRoleFromName ("").has_value ());
	assert (!WorkerRoleFromName ("unknown").has_value ());
	assert (!WorkerRoleName (static_cast<WorkerRole> (0)));

	const uint32_t uid = static_cast<uint32_t> (geteuid ());
	const uint32_t gid = static_cast<uint32_t> (getegid ());

	// Codec: one role byte, sixteen nonce bytes, a big-endian uid and gid.
	static_assert (WORKER_HELLO_SIZE == 25);
	static_assert (WORKER_HELLO_NONCE_SIZE == 16);
	{
		const auto nonce = Nonce (1);
		auto hello = EncodeWorkerHello (WorkerRole::DataStorage, nonce, uid, gid);
		assert (hello.has_value ());
		assert (hello->size () == WORKER_HELLO_SIZE);
		assert ((*hello)[0] == static_cast<uint8_t> (WorkerRole::DataStorage));
		assert ((*hello)[UID_OFFSET + 3] == static_cast<uint8_t> (uid & 0xff));
		assert ((*hello)[GID_OFFSET + 3] == static_cast<uint8_t> (gid & 0xff));
		const auto decoded = DecodeWorkerHello (*hello);
		assert (decoded.has_value ());
		assert (decoded->role == WorkerRole::DataStorage);
		assert (decoded->uid == uid);
		assert (decoded->gid == gid);
		// Unknown role id, short/long record, all-zero nonce, uid 0 and a
		// zero-length nonce are all refused.
		auto unknown = *hello;
		unknown[0] = 0;
		assert (!DecodeWorkerHello (unknown).has_value ());
		unknown[0] = 200;
		assert (!DecodeWorkerHello (unknown).has_value ());
		assert (!DecodeWorkerHello (std::span<const uint8_t> (hello->data (), hello->size () - 1)).has_value ());
		auto doubled = *hello;
		doubled.insert (doubled.end (), hello->begin (), hello->end ());
		assert (!DecodeWorkerHello (doubled).has_value ());
		std::vector<uint8_t> zero (WORKER_HELLO_SIZE, 0);
		zero[0] = static_cast<uint8_t> (WorkerRole::Log);
		assert (!DecodeWorkerHello (zero).has_value ());
		assert (!DecodeWorkerHello (RawHello (static_cast<uint8_t> (WorkerRole::Log), nonce, 0, gid)).has_value ());
		assert (!EncodeWorkerHello (WorkerRole::Log, nonce, 0, gid).has_value ());
		assert (!EncodeWorkerHello (WorkerRole::Log, std::span<const uint8_t> (), uid, gid).has_value ());
		assert (!EncodeWorkerHello (WorkerRole::Log, nonce.subspan (0, 15), uid, gid).has_value ());
		const auto generated = CreateWorkerHello (WorkerRole::Log, uid, gid);
		assert (generated.has_value ());
		const auto generatedRecord = DecodeWorkerHello (*generated);
		assert (generatedRecord.has_value () && generatedRecord->role == WorkerRole::Log);
		assert (generatedRecord->uid == uid && generatedRecord->gid == gid);
	}

	// A valid record serves a Ping and then shuts down cleanly.
	{
		auto spawned = SpawnWorker ("log", { });
		assert (spawned.has_value ());
		assert (spawned->Pid () > 0);
		auto & channel = spawned->ChannelFd ();
		assert (channel.Send (Message { MessageType::Ping, 7, { } }, 5000).has_value ());
		auto reply = channel.Receive (5000);
		assert (reply.has_value () && reply->has_value ());
		assert ((*reply)->type == MessageType::Pong && (*reply)->requestId == 7);
		spawned->Stop (2000);
	}

	// A role that disagrees with argv, and every malformed record, must
	// exit 1 before any frame is served. A worker pins its standard
	// descriptors, so an inherited stderr pipe must stay empty: neither
	// the record nor its nonce can reach the terminal or a service-manager
	// log pipe.
	{
		const auto nonce = Nonce (0x40);
		auto hello = EncodeWorkerHello (WorkerRole::DataStorage, nonce, uid, gid);
		assert (hello.has_value ());
		const auto wrongRole = RunHelloScenario (frozen, "log", *hello, true);
		assert (wrongRole.exitCode == 1);
		assert (wrongRole.error.empty ());
		assert (!Contains (wrongRole.error, Hex (nonce)));
		assert (!ContainsRaw (wrongRole.error, nonce));
	}
	{
		// Missing descriptor: a hand-started worker selects no role.
		const auto missing = RunHelloScenario (frozen, "log", { }, false);
		assert (missing.exitCode == 1);
		assert (missing.error.empty ());
	}
	{
		// All-zero nonce: structurally invalid, so the record is refused.
		std::vector<uint8_t> zero (WORKER_HELLO_SIZE, 0);
		zero[0] = static_cast<uint8_t> (WorkerRole::Log);
		const auto result = RunHelloScenario (frozen, "log", zero, true);
		assert (result.exitCode == 1);
		assert (result.error.empty ());
	}
	{
		// Truncated record: never partially accepted.
		const auto nonce = Nonce (0x10);
		auto hello = EncodeWorkerHello (WorkerRole::Log, nonce, uid, gid);
		assert (hello.has_value ());
		hello->resize (WORKER_HELLO_SIZE - 1);
		const auto result = RunHelloScenario (frozen, "log", *hello, true);
		assert (result.exitCode == 1);
		assert (!ContainsRaw (result.error, nonce));
	}
	{
		// Duplicated/oversized record: exactly one record is allowed.
		const auto nonce = Nonce (0x80);
		auto hello = EncodeWorkerHello (WorkerRole::Log, nonce, uid, gid);
		assert (hello.has_value ());
		auto doubled = *hello;
		doubled.insert (doubled.end (), hello->begin (), hello->end ());
		const auto duplicate = RunHelloScenario (frozen, "log", doubled, true);
		assert (duplicate.exitCode == 1);
		assert (!ContainsRaw (duplicate.error, nonce));
	}
	{
		// One byte past the record is already oversized.
		const auto nonce = Nonce (0xC0);
		auto hello = EncodeWorkerHello (WorkerRole::Log, nonce, uid, gid);
		assert (hello.has_value ());
		hello->push_back (0);
		const auto oversized = RunHelloScenario (frozen, "log", *hello, true);
		assert (oversized.exitCode == 1);
		assert (!ContainsRaw (oversized.error, nonce));
	}
	{
		// The role byte must be a known role even when argv matches it.
		std::vector<uint8_t> unknown (WORKER_HELLO_SIZE, 0);
		unknown[0] = 250;
		for (size_t i = 1; i < unknown.size (); ++i) unknown[i] = static_cast<uint8_t> (i);
		const auto result = RunHelloScenario (frozen, "log", unknown, true);
		assert (result.exitCode == 1);
	}
	{
		// A uid-0 record is never accepted, even with a known role and a
		// matching nonce: the worker would otherwise run as the superuser.
		const auto nonce = Nonce (0x20);
		const auto rootRecord = RawHello (static_cast<uint8_t> (WorkerRole::Log), nonce, 0, gid);
		const auto result = RunHelloScenario (frozen, "log", rootRecord, true);
		assert (result.exitCode == 1);
		assert (result.error.empty ());
	}
	{
		// A well-formed record that names a different identity must not
		// serve: the spawner did not drop to the identity it claims.
		const auto nonce = Nonce (0x30);
		const auto wrongUid = RawHello (static_cast<uint8_t> (WorkerRole::Log), nonce, uid + 1, gid);
		const auto uidResult = RunHelloScenario (frozen, "log", wrongUid, true);
		assert (uidResult.exitCode == 1);
		assert (uidResult.error.empty ());
		const auto wrongGid = RawHello (static_cast<uint8_t> (WorkerRole::Log), nonce, uid, gid + 1);
		const auto gidResult = RunHelloScenario (frozen, "log", wrongGid, true);
		assert (gidResult.exitCode == 1);
		assert (gidResult.error.empty ());
	}

	// Process creation is a one-way process-wide barrier, so a child
	// process owns the flag: every spawn helper must then fail closed with
	// the same fixed error while the parent's spawn path stays usable.
	{
		assert (!IsProcessCreationForbidden ());
		const int code = RunChildScenario (
			[] () -> int
			{
				ForbidProcessCreation ();
				if (!IsProcessCreationForbidden ()) return 1;
				auto spawned = SpawnWorker ("log", { });
				if (spawned) return 2;
				if (spawned.error ().find ("forbidden") == std::string::npos) return 3;
				auto withRoot = SpawnWorkerWithRoot ("log", { }, 0);
				if (withRoot) return 4;
				if (withRoot.error ().find ("forbidden") == std::string::npos) return 5;
				auto detached = SpawnDetachedWorker ("log");
				if (detached) return 6;
				if (detached.error ().find ("forbidden") == std::string::npos) return 7;
				auto detachedRoot = SpawnDetachedWorkerWithRoot ("log", 0);
				if (detachedRoot) return 8;
				if (detachedRoot.error ().find ("forbidden") == std::string::npos) return 9;
				return 0;
			});
		assert (code == 0);
		assert (!IsProcessCreationForbidden ());
		// The parent still spawns: the barrier never leaked out of the child.
		auto spawned = SpawnWorker ("log", { });
		assert (spawned.has_value ());
		spawned->Stop (2000);
	}

	// An unusable executable path is refused instead of being resolved
	// through
	// $PATH or /proc, and the spawn fails closed with a fixed error.
	{
		const int code = RunChildScenario (
			[] () -> int
			{
				SetExecutablePath ("i2pd");
				if (!ExecutablePath ().empty ()) return 1;
				auto relative = SpawnWorker ("log", { });
				if (relative) return 2;
				if (relative.error ().find ("not frozen") == std::string::npos) return 3;
				SetExecutablePath ("/nonexistent-openi2pd-worker");
				if (!ExecutablePath ().empty ()) return 4;
				SetExecutablePath ("/tmp");
				if (!ExecutablePath ().empty ()) return 5;
				SetExecutablePath ("/$PATH/i2pd");
				if (!ExecutablePath ().empty ()) return 6;
				return 0;
			});
		assert (code == 0);
		assert (!ExecutablePath ().empty ());
	}
	return 0;
}
