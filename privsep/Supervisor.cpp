/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Supervisor.
 *
 * Implements the privilege-separated supervisor: verifies the control
 * peer's UID, applies its platform sandbox and starts or restarts a log
 * worker while forwarding decoded records. It also implements the
 * pre-pledge OpenBSD torrent-storage broker that inherits the data root
 * as fd4 and delegates bounded storage workers. The log path is never
 * opened here; shutdown or a malformed frame exits fail-closed.
 *
 */

#include "Supervisor.h"

#ifndef _WIN32

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <mutex>
#include <filesystem>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include "TorrentStorage.h"
#include <unistd.h>

#include "Capabilities.h"
#include "Channel.h"
#include "Credentials.h"
#include "DeadlineMutex.h"
#include "LinuxSandbox.h"
#include "LogWorker.h"
#include "OpenBSDSandbox.h"
#include "Protocol.h"
#include "Worker.h"

namespace i2p::privsep
{
namespace
{
	// The supervisor role must never combine external network access with
	// persistent writes. Capabilities.h is the single authority for that
	// invariant; this is the production consumer of the declaration.
	static_assert (
		IsRoleCapabilitySafe (ProcessRole::Supervisor), "the supervisor capability mask violates the role invariant");
	static_assert (ValidateCapabilityMatrix ().valid, "the capability matrix is inconsistent");

	// Admission gate for worker creation. It is a process-local flag, not
	// a policy builder: a set flag makes every spawn helper fail closed
	// before fork(), independently of the platform sandbox.
	std::atomic<bool> processCreationForbidden { false };
}

void ForbidProcessCreation ()
{
	processCreationForbidden.store (true, std::memory_order_release);
}

bool IsProcessCreationForbidden ()
{
	return processCreationForbidden.load (std::memory_order_acquire);
}

namespace
{
	bool ReadLogConfig (Channel & channel, bool & useSyslog, std::string & path, std::string & tag)
	{
		while (true)
		{
			auto received = channel.ReceiveWithFd (1000);
			if (!received)
			{
				std::cerr << "privsep supervisor: control channel: " << received.error () << std::endl;
				return false;
			}
			if (!received->has_value ()) continue;
			const int descriptor = (*received)->fd;
			auto message = std::move ((*received)->message);
			if (descriptor >= 0) close (descriptor);
			// The registry, not a local switch, decides what this endpoint may
			// receive. The supervisor deliberately never accepts a log
			// descriptor: only the log worker owns that capability.
			if (!AllowsOperation (EndpointRole::Supervisor, message.type))
			{
				std::cerr << "privsep supervisor: operation is not allowed on this endpoint" << std::endl;
				(void)channel.Send ({ MessageType::Error, message.requestId,
										EncodeError (message.type, ErrorCode::NotAuthorized,
											"operation is not allowed on the supervisor endpoint") },
					500);
				continue;
			}
			if (message.type == MessageType::Ping)
			{
				if (descriptor >= 0 || !message.payload.empty ())
				{
					std::cerr << "privsep supervisor: invalid ping" << std::endl;
					return false;
				}
				(void)channel.Send (Message { MessageType::Pong, message.requestId, { } }, 500);
				continue;
			}
			if (message.type != MessageType::ConfigureLog)
			{
				std::cerr << "privsep supervisor: expected ConfigureLog" << std::endl;
				return false;
			}
			if (descriptor >= 0)
			{
				std::cerr << "privsep supervisor: unexpected log descriptor; "
						  << "the log worker owns the file capability" << std::endl;
				return false;
			}
			auto decoded = DecodeLogConfig (message.payload);
			if (!decoded)
			{
				std::cerr << "privsep supervisor: invalid log configuration" << std::endl;
				return false;
			}
			useSyslog = std::get<0> (*decoded);
			path = std::move (std::get<1> (*decoded));
			tag = std::move (std::get<2> (*decoded));
			return true;
		}
	}
}

namespace
{
	DeadlineMutex storageSupervisorMutex;
	std::unique_ptr<Channel> storageSupervisor;
	uint32_t storageSupervisorRequest = 0;
	std::filesystem::path storageSupervisorRoot;
}

bool PrepareTorrentStorageSupervisor (const std::string & root, std::string & error)
{
#if defined(__OpenBSD__)
	// The on-demand broker forked/exec'ed a storage worker after its own
	// sandbox, which requires the forbidden pledge(2) "exec" promise.
	// There is no statically demonstrated exec-free provisioning protocol
	// yet, so isolated torrent storage fails closed instead of receiving a
	// broader privilege. The core warns and continues without it.
	(void)root;
	error = "isolated torrent storage is unavailable on OpenBSD: "
			"on-demand worker creation requires the forbidden exec promise";
	return false;
#else
	std::lock_guard lock (storageSupervisorMutex);
	error.clear ();
	if (storageSupervisor)
	{
		error = "storage supervisor already prepared";
		return false;
	}
	// The broker and every worker it spawns run as the configured worker
	// identity (the forked child drops uid/gid before execve). The core
	// may still be root, so a configured identity replaces the old "never
	// root" test; the broker re-checks its own euid after the drop.
	// Without a configured identity the core must already be unprivileged.
	const auto & credentials = GetWorkerCredentials ();
	if (!credentials.configured && ::geteuid () == 0)
	{
		error = "storage supervisor requires unprivileged identity";
		return false;
	}
	const uid_t workerUid
		= credentials.configured ? static_cast<uid_t> (credentials.uid) : static_cast<uid_t> (::geteuid ());
	std::error_code ec;
	const auto absolute = std::filesystem::canonical (root, ec);
	if (ec)
	{
		error = "invalid storage supervisor root";
		return false;
	}
	// Open and validate the immutable root before spawning. The broker
	// receives it as inherited fd4, so it never re-resolves a path after
	// the startup sandbox and cannot be redirected by a path
	// replacement/rename.
	int rootFd = ::open (absolute.c_str (), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if (rootFd < 0)
	{
		error = "cannot open storage supervisor root";
		return false;
	}
	struct RootGuard
	{
		int fd;
		~RootGuard ()
		{
			if (fd >= 0) ::close (fd);
		}
	} guard { rootFd };
	struct stat rootStatus { };
	// The delegated root must belong to the identity that will walk it,
	// not to the (possibly root) core that opens it, and must not be
	// group/world writable.
	if (::fstat (rootFd, &rootStatus) || !S_ISDIR (rootStatus.st_mode) || rootStatus.st_uid != workerUid
		|| (rootStatus.st_mode & 0022))
	{
		error = "unsafe storage supervisor root";
		return false;
	}
	auto spawned = SpawnDetachedWorkerWithRoot ("torrent-storage-supervisor", rootFd);
	if (!spawned)
	{
		error = spawned.error ();
		return false;
	}
	const std::string executable = ExecutablePath ();
	if (executable.empty ())
	{
		error = "cannot resolve the storage supervisor executable";
		return false;
	}
	PayloadWriter config;
	if (!config.WritePath (absolute.string ()) || !config.WritePath (executable))
	{
		error = "storage supervisor paths exceed the IPC bound";
		return false;
	}
	const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (5);
	auto sent = spawned->SendUntil ({ MessageType::ConfigureTorrentStorage, 1, config.Data () }, deadline);
	if (!sent)
	{
		error = sent.error ();
		return false;
	}
	auto reply = spawned->ReceiveUntil (deadline);
	if (!reply || !*reply || (**reply).type != MessageType::TorrentStorageSupervisorReply || (**reply).requestId != 1
		|| !(**reply).payload.empty ())
	{
		error = "storage supervisor initialization failed";
		return false;
	}
	storageSupervisor = std::make_unique<Channel> (std::move (*spawned));
	storageSupervisor->SetEnforcementPolicy (EnforcementPolicy::Endpoint);
	storageSupervisorRequest = 1;
	storageSupervisorRoot = absolute;
	return true;
#endif
}

i2p::expected<Channel, std::string> SpawnSupervisedTorrentStorage (int rootFd, std::string_view rootPath)
{
	// Admission, send and stale-reply draining consume one transaction.
	const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (5);
	std::unique_lock lock (storageSupervisorMutex, std::defer_lock);
	if (!lock.try_lock_until (deadline))
		return i2p::unexpected (std::string ("storage supervisor busy"));
	if (!storageSupervisor || storageSupervisorRequest == UINT32_MAX)
		return i2p::unexpected (std::string ("storage supervisor unavailable"));
	const auto id = ++storageSupervisorRequest;
	struct stat expected { };
	if (rootFd < 0 || ::fstat (rootFd, &expected) || !S_ISDIR (expected.st_mode))
		return i2p::unexpected (std::string ("invalid local root identity"));
	const auto relative = std::filesystem::path (rootPath).lexically_relative (storageSupervisorRoot).generic_string ();
	PayloadWriter request;
	if (!request.WriteStringBounded (relative, MAX_PATH_SIZE))
		return i2p::unexpected (std::string ("storage root path is too long"));
	request.WriteU64BE (static_cast<uint64_t> (expected.st_dev));
	request.WriteU64BE (static_cast<uint64_t> (expected.st_ino));
	if (std::chrono::steady_clock::now () >= deadline)
		return i2p::unexpected (std::string ("storage supervisor busy"));
	auto & channel = *storageSupervisor;
	auto sent = channel.SendUntil ({ MessageType::SpawnTorrentStorage, id, request.Data () }, deadline);
	if (!sent)
	{
		storageSupervisor.reset ();
		return i2p::unexpected (sent.error ());
	}
	std::optional<ReceivedMessage> selected;
	unsigned drainedStale = 0;
	while (true)
	{
		if (std::chrono::steady_clock::now () >= deadline)
			return i2p::unexpected (std::string ("storage supervisor busy"));
		auto received = channel.ReceiveWithFdUntil (deadline);
		if (!received)
		{
			storageSupervisor.reset ();
			return i2p::unexpected (std::string ("storage supervisor disconnected"));
		}
		if (!*received)
		{
			// A slow broker is not a dead broker. Keep the channel; the next
			// request drains a late reply by request id before matching.
			return i2p::unexpected (std::string ("storage supervisor busy"));
		}
		auto & candidate = **received;
		const auto decision = ClassifySupervisorReply (id, candidate.message.requestId, drainedStale);
		if (decision.kind == SupervisorReplyKind::Stale)
		{
			// A reply for an earlier, already answered request is drained and
			// ignored so a late duplicate cannot be mistaken for this one.
			// The drain itself is bounded: an endless run of them is a fault,
			// not a slow broker, and must not hold this call forever.
			if (candidate.fd >= 0) ::close (candidate.fd);
			++drainedStale;
			continue;
		}
		if (decision.kind == SupervisorReplyKind::OutOfOrder)
		{
			// A future request id, or a run of stale replies past the drain
			// budget, cannot answer this request. It is a bounded protocol
			// fault: discard the frame and its descriptor and reject only
			// this call. Tearing the broker down here would also destroy
			// every worker channel the core already owns.
			(void)decision.code; // BadField for a misordered id, Busy for a stale flood; not a teardown
			if (candidate.fd >= 0) ::close (candidate.fd);
			return i2p::unexpected (std::string (decision.detail));
		}
		selected = std::move (*received);
		break;
	}
	auto & reply = *selected;
	if (reply.message.type == MessageType::Error && reply.message.requestId == id && reply.fd < 0)
	{
		auto decoded = DecodeError (reply.message.payload);
		if (!decoded)
		{
			storageSupervisor.reset ();
			return i2p::unexpected (std::string ("storage supervisor sent a malformed error"));
		}
		// A per-request rejection must not destroy the broker or other
		// workers.
		return i2p::unexpected (std::string ("storage supervisor rejected request: ") + std::get<2> (*decoded));
	}
	if (reply.message.type != MessageType::TorrentStorageSupervisorReply || reply.message.requestId != id
		|| !reply.message.payload.empty () || reply.fd < 0)
	{
		if (reply.fd >= 0) ::close (reply.fd);
		storageSupervisor.reset ();
		return i2p::unexpected (std::string ("storage supervisor rejected worker capability"));
	}
	// The passed socket is the worker's endpoint, so its peer is the
	// worker identity, not the root core that adopted the broker reply.
	auto adopted = Channel::AdoptValidated (reply.fd, WorkerUid ());
	if (!adopted) return i2p::unexpected (adopted.error ());
	return std::move (*adopted);
}

int RunTorrentStorageSupervisor (int fd)
{
#if !defined(__OpenBSD__)
	(void)fd;
	return 1; // This broker is used only by the pre-pledge OpenBSD path.
#else
	// Disabled: the broker needed fork/exec after its own sandbox. The
	// retained implementation below is a design reference for a future
	// exec-free provisioning protocol; it is not compiled or reachable.
	(void)fd;
	return 1;
#if 0
	Channel control = Channel::Adopt (fd);
	control.SetEnforcementPolicy (EnforcementPolicy::Endpoint);
	// The broker runs as the unprivileged worker identity while the core
	// may still be privileged (privilege separation does not drop the
	// core). The peer must therefore be either our own identity or the
	// root that spawned us; a broker that somehow kept euid 0 refuses to
	// run at all.
	auto uid = control.PeerUid ();
	if (!uid || (*uid != ::geteuid () && *uid != 0) || ::geteuid () == 0) return 1;
	// Pin descriptors 0/1/2 to /dev/null instead of closing them. Closing
	// them would let Channel::CreatePair() (and any file it opens)
	// allocate a stdio descriptor, so a worker could inherit a "stdio"
	// slot that is actually the control or worker channel. /dev/null is
	// reachable under the stdio promise through __pledge_open.
	const int devNull = ::open ("/dev/null", O_RDWR);
	if (devNull < 0) return 1;
	for (int stdio = 0; stdio <= 2; ++stdio)
	{
		if (::dup2 (devNull, stdio) < 0)
		{
			if (devNull > 2) ::close (devNull);
			return 1;
		}
	}
	if (devNull > 2) ::close (devNull);
	struct RootOwner
	{
		int fd;
		~RootOwner ()
		{
			if (fd >= 0) ::close (fd);
		}
	} root { 4 };
	struct stat rootInfo { };
	if (::fstat (root.fd, &rootInfo) || !S_ISDIR (rootInfo.st_mode) || rootInfo.st_uid != ::geteuid ()
		|| (rootInfo.st_mode & 0022))
		return 1;
	auto received = control.ReceiveWithFd (5000);
	if (!received || !*received) return 1;
	auto & initial = **received;
	struct RootOwner unexpectedRequestFd { initial.fd };
	PayloadReader setup (initial.message.payload);
	auto configuredRoot = setup.ReadString ();
	// Kept for wire compatibility only. It is never executed: the broker
	// resolves its own executable below and freezes that path before
	// pledge.
	auto configuredExecutable = setup.ReadString ();
	if (initial.message.type != MessageType::ConfigureTorrentStorage || initial.message.requestId != 1
		|| initial.fd >= 0 || !configuredRoot || !configuredExecutable || !setup.Done () || configuredRoot->empty ()
		|| configuredRoot->front () != '/' || configuredRoot->size () > MAX_PATH_SIZE
		|| configuredRoot->find ('\0') != std::string_view::npos)
		return 1;
	const std::string rootPath (*configuredRoot);
	// Historical reference only. The live broker is disabled: it forked
	// and exec'ed a worker after its sandbox, which required the now
	// forbidden pledge promise "exec". Do not enable this block without an
	// exec-free provisioning protocol; the builder already refuses the
	// role and this code is not compiled.
	auto brokerPolicy = BuildOpenBSDPolicy (OpenBSDWorkerRole::StorageBroker, OpenBSDPolicyConfig {});
	if (!brokerPolicy) return 1;
	if (!ApplyOpenBSDPolicy (*brokerPolicy)) return 1;
	if (!control.Send ({ MessageType::TorrentStorageSupervisorReply, 1, { } }, 5000)) return 1;
	uint32_t lastRequest = 1, violations = 0;
	std::vector<pid_t> detachedWorkers;
	// Forwarded workers are owned by the core after handoff, so the broker
	// cannot observe their liveness. Bound churn with a replenished
	// one-minute launch window rather than a permanent lifetime counter.
	// Every attempt is charged, including a child that never becomes ready
	// or is stopped again.
	constexpr uint32_t MAX_LAUNCHES_PER_WINDOW = 32;
	// Per-phase budgets: the configure write and the readiness reply each
	// get their own bounded wait, and a readiness failure stops the child
	// before the next request is served.
	constexpr int CONFIGURE_TIMEOUT_MS = 5000;
	constexpr int READY_TIMEOUT_MS = 5000;
	uint32_t launches = 0;
	auto launchWindow = std::chrono::steady_clock::now ();
	auto sendError = [&control] (uint32_t requestId, MessageType offending, ErrorCode code, std::string_view detail)
	{ return control.Send ({ MessageType::Error, requestId, EncodeError (offending, code, detail) }, 5000); };
	while (true)
	{
		for (auto it = detachedWorkers.begin (); it != detachedWorkers.end ();)
		{
			int childStatus = 0;
			const pid_t reaped = ::waitpid (*it, &childStatus, WNOHANG);
			if (reaped == *it || (reaped < 0 && errno == ECHILD))
				it = detachedWorkers.erase (it);
			else
				++it;
		}
		const auto now = std::chrono::steady_clock::now ();
		if (now - launchWindow >= std::chrono::minutes (1))
		{
			launchWindow = now;
			launches = 0;
		}
		auto next = control.ReceiveWithFd (500);
		if (!next) return 1; // RAII stops/reaps every owned worker.
		if (!*next) continue;
		auto & request = **next;
		RootOwner unexpectedRequestFd { request.fd };
		auto rejectRequest = [&] (std::string_view detail)
		{
			if (++violations > Channel::MAX_PROTOCOL_VIOLATIONS) return false;
			return sendError (request.message.requestId, request.message.type, ErrorCode::BadField, detail)
				.has_value ();
		};
		if (!AllowsOperation (EndpointRole::StorageBroker, request.message.type))
		{
			if (!rejectRequest ("operation is not allowed on the storage broker")) return 1;
			continue;
		}
		if (request.message.type == MessageType::Ping)
		{
			if (request.fd >= 0 || !request.message.payload.empty ())
			{
				if (!rejectRequest ("invalid storage broker ping")) return 1;
				continue;
			}
			if (!control.Send ({ MessageType::Pong, request.message.requestId, { } }, 5000)) return 1;
			continue;
		}
		if (request.message.type == MessageType::Shutdown)
		{
			if (request.fd >= 0 || !request.message.payload.empty ())
			{
				if (!rejectRequest ("invalid storage broker shutdown")) return 1;
				continue;
			}
			return 0;
		}
		if (request.message.type != MessageType::SpawnTorrentStorage || request.fd >= 0)
		{
			if (!rejectRequest ("invalid storage broker operation")) return 1;
			continue;
		}
		if (request.message.requestId <= lastRequest)
		{
			// Dropping a replay/duplicate avoids executing it and avoids
			// confusing a live client with a second reply for the same ID.
			if (++violations > Channel::MAX_PROTOCOL_VIOLATIONS) return 1;
			continue;
		}
		const uint32_t id = request.message.requestId;
		lastRequest = id;
		if (launches >= MAX_LAUNCHES_PER_WINDOW)
		{
			if (!sendError (
					id, MessageType::SpawnTorrentStorage, ErrorCode::Busy, "storage worker launch window exhausted"))
				return 1;
			continue;
		}
		PayloadReader payload (request.message.payload);
		auto path = payload.ReadString ();
		auto expectedDevice = payload.ReadU64BE ();
		auto expectedInode = payload.ReadU64BE ();
		if (!path || !expectedDevice || !expectedInode || !payload.Done ())
		{
			if (!sendError (id, MessageType::SpawnTorrentStorage, ErrorCode::BadField, "malformed storage request"))
				return 1;
			continue;
		}
		// A dedicated `&lt;datadir&gt;/torrents` payload root is required
		// before the "." case can be rejected; today the torrent tunnel may
		// request the whole data directory. Do not widen this fallback to a
		// future router or data-storage role: those already own a narrower
		// root.
		const std::string relative (*path);
		if (relative != "." && !SafeStoragePath (relative))
		{
			if (!sendError (id, MessageType::SpawnTorrentStorage, ErrorCode::BadField, "unsafe storage path")) return 1;
			continue;
		}
		// The worker inherits the immutable broker root at fd4 and performs
		// the relative walk itself before applying its own policy. The broker
		// must not use openat() here: it has no rpath promise and its unveil
		// list is locked.
		PayloadWriter workerSetup;
		const std::string delegatedRoot = relative == "." ? rootPath : rootPath + "/" + relative;
		if (!workerSetup.WritePath (delegatedRoot) || !workerSetup.WriteStringBounded (relative, MAX_PATH_SIZE))
		{
			if (!sendError (
					id, MessageType::SpawnTorrentStorage, ErrorCode::BadField, "storage worker path is too long"))
				return 1;
			continue;
		}
		workerSetup.WriteU64BE (*expectedDevice);
		workerSetup.WriteU64BE (*expectedInode);
		// Charge the attempt before it happens: a fork that fails, a child
		// that never becomes ready and a child this loop stops again all
		// consume the same bounded launch budget.
		++launches;
		auto child = SpawnWorkerWithRoot ("torrent-storage-inherited", executable, root.fd);
		if (!child)
		{
			if (!sendError (id, MessageType::SpawnTorrentStorage, ErrorCode::Internal, "cannot spawn storage worker"))
				return 1;
			continue;
		}
		if (!child->ChannelFd ().Send (
				{ MessageType::ConfigureTorrentStorageSupervised, 1, workerSetup.Data () }, CONFIGURE_TIMEOUT_MS))
		{
			// Stop the configure-phase child before serving the next request so
			// a half-configured worker cannot outlive its failed request.
			child->Stop (CONFIGURE_TIMEOUT_MS);
			(void)sendError (
				id, MessageType::SpawnTorrentStorage, ErrorCode::Internal, "storage worker configure failed");
			continue;
		}
		auto ready = child->ChannelFd ().Receive (READY_TIMEOUT_MS);
		if (!ready || !*ready || (**ready).type != MessageType::TorrentStorageReply || (**ready).requestId != 1
			|| (**ready).payload != std::string (1, '\0'))
		{
			// Readiness is its own phase: the child is stopped before the broker
			// continues, and the per-request typed error keeps the broker alive
			// for every other worker it already handed to the core.
			child->Stop (READY_TIMEOUT_MS);
			(void)sendError (
				id, MessageType::SpawnTorrentStorage, ErrorCode::Internal, "storage worker did not become ready");
			continue;
		}
		if (!control.SendWithFd (
				{ MessageType::TorrentStorageSupervisorReply, id, { } }, child->ChannelFd ().Fd (), 5000))
			return 1;
		child->ChannelFd ().Close (); // Do not keep the client endpoint alive.
		// The core now owns the worker channel. Do not SIGTERM/SIGKILL it
		// when the broker exits: a broker shutdown must not corrupt an
		// in-flight fsync. Keep the PID only for non-blocking reaping.
		const pid_t workerPid = child->Pid ();
		child->Disown ();
		if (workerPid > 0) detachedWorkers.push_back (workerPid);
	}
#endif // disabled on-demand broker
#endif
}

/*
 * Serve the supervisor endpoint: own one log worker, forward framed
 * records and replace that worker under the shared restart ladder.
 *
 * The supervisor never opens the log path itself; the sink descriptor
 * or the syslog destination is the log worker's capability. On OpenBSD
 * the active and one reserve log worker are created and configured
 * before the `stdio proc` pledge; the supervisor then only forwards
 * records and promotes the reserve. It never executes another image,
 * opens a path or receives a descriptor. On Linux no-new-privs and the
 * capability drop are applied for the same inheritance reason as
 * ApplyLinuxSupervisorSandbox(); fork+exec remains the Linux
 * supervision mechanism because no pledge(2) policy is involved.
 * Progress is published only in this process's SupervisorStatus
 * snapshot; no message type was added to expose it, so the core
 * observes the supervisor via its channel and process liveness.
 *
 * @param fd inherited worker channel (descriptor 3 in the exec'ed
 * process).
 * @return 0 on a requested Shutdown, 1 on any protocol, credential or
 * spawn failure; the caller must treat both as the end of this worker.
 */
int RunSupervisor (int fd)
{
	SanitizeInheritedStdio ();
	Channel control = Channel::Adopt (fd);
	control.SetEnforcementPolicy (EnforcementPolicy::Endpoint);
	auto peer = control.PeerUid ();
	if (!peer || !IsAllowedParentUid (*peer))
	{
		std::cerr << "privsep supervisor: rejecting peer with unexpected uid" << std::endl;
		return 1;
	}

	// Configuration is read before the OpenBSD supervisor policy: the
	// active and reserve log workers are created and configured while the
	// supervisor is still unpledged. On Linux it is read only after the
	// supervisor's capability drop.
	bool useSyslog = false;
	std::string path, tag;
#if defined(__OpenBSD__)
	if (!ReadLogConfig (control, useSyslog, path, tag)) return 1;
#endif

	// The supervisor process owns at most the active worker and one
	// pre-created reserve, so the status snapshot has one log entry. It is
	// published under SupervisorStatus' mutex and read only through const
	// getters.
	auto & status = GetSupervisorStatus ();
	constexpr std::string_view LOG_WORKER = "log";
	uint32_t generation = 0;
	LogWorkerRestartBudget budget;
	std::unique_ptr<ChildProcess> worker;
	std::unique_ptr<LogSink> sink;

#if defined(__OpenBSD__)
	// Two workers are created before the sandbox: one active and one
	// reserve. A pledged process may not exec, so the reserve is the only
	// bounded replacement this process can promote.
	std::unique_ptr<ChildProcess> reserveWorker;
	std::unique_ptr<LogSink> reserveSink;
	const std::string workerExecutable = ExecutablePath ();
	if (workerExecutable.empty () || workerExecutable.front () != '/'
		|| workerExecutable.find ('\0') != std::string::npos)
	{
		std::cerr << "privsep supervisor: cannot resolve executable before sandbox" << std::endl;
		return 1;
	}
	/*
	 * Spawn and configure one log worker through a fresh sink. Both the
	 * active and the reserve worker use the same bounded handshake; no
	 * channel exists before this call.
	 */
	auto prepareWorker = [&] (std::unique_ptr<ChildProcess> & process,
		std::unique_ptr<LogSink> & logSink) -> bool
	{
		auto spawned = SpawnLogWorker (workerExecutable);
		if (!spawned)
		{
			std::cerr << "privsep supervisor: cannot spawn log worker: "
					  << spawned.error () << std::endl;
			return false;
		}
		process = std::make_unique<ChildProcess> (std::move (*spawned));
		logSink = std::make_unique<LogSink> (std::move (process->ChannelFd ()));
		auto configured = logSink->ConfigureProxy (useSyslog, path, tag);
		if (!configured || !logSink->IsOpen ())
		{
			std::cerr << "privsep supervisor: cannot configure log worker: "
					  << (configured ? "sink closed" : configured.error ()) << std::endl;
			return false;
		}
		return true;
	};
	++generation;
	if (!prepareWorker (worker, sink))
	{
		if (worker) worker->Stop (500);
		return 1;
	}
	if (!prepareWorker (reserveWorker, reserveSink))
	{
		if (sink) (void)sink->Shutdown ();
		if (worker) worker->Stop (500);
		if (reserveWorker) reserveWorker->Stop (500);
		return 1;
	}
	// Both workers exist. The supervisor now drops to stdio/proc: it can
	// forward records, observe and reap its children, but cannot execute
	// another image, open a path or reach the network.
	OpenBSDPolicyConfig policyConfig;
	auto policy = BuildOpenBSDPolicy (OpenBSDWorkerRole::Supervisor, policyConfig);
	if (!policy)
	{
		std::cerr << "privsep supervisor: invalid OpenBSD policy: " << policy.error () << std::endl;
		if (sink) (void)sink->Shutdown ();
		if (worker) worker->Stop (500);
		if (reserveWorker) reserveWorker->Stop (500);
		return 1;
	}
	auto applied = ApplyOpenBSDPolicy (*policy);
	if (!applied)
	{
		std::cerr << "privsep supervisor: OpenBSD sandbox: " << applied.error () << std::endl;
		if (sink) (void)sink->Shutdown ();
		if (worker) worker->Stop (500);
		if (reserveWorker) reserveWorker->Stop (500);
		return 1;
	}
	ForbidProcessCreation ();
	status.NoteReady (LOG_WORKER, generation, 0);
#else
#if defined(__linux__)
	// Landlock/seccomp/network namespaces are inherited across fork+exec
	// and a child can only become stricter. Installing the supervisor's
	// write-deny or AF_INET-deny domain here would permanently prevent
	// storage/network children from using their delegated capability. Drop
	// capabilities and set no_new_privs; each exec'ed worker applies its
	// own role policy.
	auto sandbox = ApplyLinuxSupervisorSandbox ();
	if (!sandbox)
	{
		std::cerr << "privsep supervisor: Linux sandbox: " << sandbox.error () << std::endl;
		return 1;
	}
#endif
	// Configuration is parsed only after the Linux capability drop and
	// before any worker exists on this platform.
	if (!ReadLogConfig (control, useSyslog, path, tag)) return 1;
	/*
	 * Spawn a replacement worker and configure it through a fresh sink.
	 * Linux has no pledge(2): fork+exec is its supervisor mechanism and is
	 * bounded by the shared restart ladder.
	 */
	auto startWorker = [&] () -> bool
	{
		++generation;
		auto spawned = SpawnLogWorker (ExecutablePath ());
		if (!spawned)
		{
			std::cerr << "privsep supervisor: cannot spawn log worker: " << spawned.error () << std::endl;
			status.NoteFailed (LOG_WORKER, generation, 0, "cannot spawn log worker");
			return false;
		}
		worker = std::make_unique<ChildProcess> (std::move (*spawned));
		sink = std::make_unique<LogSink> (std::move (worker->ChannelFd ()));
		status.NoteStarting (LOG_WORKER, generation, "configuring log worker");
		auto configured = sink->ConfigureProxy (useSyslog, path, tag);
		if (!configured || !sink->IsOpen ())
		{
			std::cerr << "privsep supervisor: cannot configure log worker: "
					  << (configured ? "sink closed" : configured.error ()) << std::endl;
			status.NoteFailed (LOG_WORKER, generation, 0, "cannot configure log worker");
			return false;
		}
		status.NoteReady (LOG_WORKER, generation, budget.Restarts ());
		return true;
	};

	if (!startWorker ())
	{
		if (worker) worker->Stop (500);
		return 1;
	}
#endif
	budget.NoteReady (std::chrono::steady_clock::now ());
	uint64_t publishedDropped = 0;

	bool protocolFailed = false;
	unsigned violations = 0;
	while (true)
	{
#if defined(__OpenBSD__)
		// A reserve that died while idle cannot be replaced without exec.
		// Drop it now so the next active failure is reported as terminal.
		if (reserveWorker
			&& (!reserveWorker->IsRunning () || !reserveSink || !reserveSink->IsOpen ()))
		{
			reserveWorker->Stop (500);
			reserveSink.reset ();
			reserveWorker.reset ();
		}
#endif
		if (!worker || !worker->IsRunning () || !sink || !sink->IsOpen ())
		{
			const auto now = std::chrono::steady_clock::now ();
			// Only a worker that stayed up for STABLE_PERIOD since its own
			// NoteReady resets the budget. The observation time is the
			// failure, so the backoff waits never count as service.
			budget.NoteStable (now);
			if (worker) worker->Stop (500);
			sink.reset ();
			const auto delay = budget.NoteFailure (now);
			if (!delay)
			{
				// Terminal for this process lifetime: report once and stop
				// replacing the worker. Records are dropped; there is no local
				// fallback that would bypass the capability boundary.
				status.NoteFailed (LOG_WORKER, generation, budget.Restarts (), "restart budget exhausted");
				std::cerr << "privsep supervisor: log worker restart budget exhausted ("
						  << LogWorkerRestartBudget::MAX_RESTARTS << " restarts, "
						  << LogWorkerRestartBudget::TotalLadderDelay ().count ()
						  << " ms of backoff); log records are dropped (no local fallback)" << std::endl;
				return 1;
			}
#if defined(__OpenBSD__)
			if (!reserveWorker || !reserveSink)
			{
				// No exec and no reserve: the supervisor cannot create a new
				// log worker. The core observes the closed channel and the
				// runtime status remains Failed; no local fallback is allowed.
				status.NoteFailed (LOG_WORKER, generation, budget.Restarts (),
					"pre-created log reserve exhausted");
				std::cerr << "privsep supervisor: pre-created log reserve exhausted; "
							 "log records are dropped (no exec, no local fallback)" << std::endl;
				return 1;
			}
			// Promote the already-configured reserve. It was created before
			// the pledge and is not re-opened or reconfigured here.
			worker = std::move (reserveWorker);
			sink = std::move (reserveSink);
			++generation;
			status.NoteStarting (LOG_WORKER, generation, "promoting reserve log worker");
			status.NoteReady (LOG_WORKER, generation, budget.Restarts ());
			publishedDropped = 0;
			continue;
#else
			status.NoteRestarting (LOG_WORKER, generation + 1, budget.Restarts (), "log worker exited");
			std::this_thread::sleep_for (*delay);
			if (!startWorker ()) return 1;
			budget.NoteReady (std::chrono::steady_clock::now ());
			publishedDropped = 0;
			continue;
#endif
		}

		auto received = control.Receive (500);
		if (!received)
		{
			std::cerr << "privsep supervisor: control channel closed" << std::endl;
			break;
		}
		if (!received->has_value ()) continue;
		const auto & message = **received;
		if (!AllowsOperation (EndpointRole::Supervisor, message.type))
		{
			// Bounded typed refusal: a wrong-direction frame must not be able
			// to kill the supervisor, and a peer that keeps sending one is
			// dropped once the budget is exhausted.
			if (++violations > Channel::MAX_PROTOCOL_VIOLATIONS)
			{
				protocolFailed = true;
				break;
			}
			if (!control.Send ({ MessageType::Error, message.requestId,
								   EncodeError (message.type, ErrorCode::NotAuthorized,
									   "operation is not allowed on the supervisor endpoint") },
					500))
			{
				protocolFailed = true;
				break;
			}
			continue;
		}
		if (message.type == MessageType::Shutdown)
		{
			if (!message.payload.empty ())
			{
				std::cerr << "privsep supervisor: invalid shutdown" << std::endl;
				protocolFailed = true;
			}
			break;
		}
		if (message.type == MessageType::Ping)
		{
			if (!message.payload.empty ())
			{
				std::cerr << "privsep supervisor: invalid ping" << std::endl;
				protocolFailed = true;
				break;
			}
			(void)control.Send (Message { MessageType::Pong, message.requestId, { } }, 500);
			continue;
		}
		if (message.type != MessageType::LogRecord)
		{
			std::cerr << "privsep supervisor: unsupported message type" << std::endl;
			protocolFailed = true;
			break;
		}
		auto record = DecodeLogRecord (message.payload);
		if (!record)
		{
			std::cerr << "privsep supervisor: malformed log record" << std::endl;
			protocolFailed = true;
			break;
		}
		sink->Write (std::get<0> (*record), static_cast<std::time_t> (std::get<1> (*record)), std::get<2> (*record));
		// Publish the client-side drop counter only when it moved, so the
		// status mutex is not taken for every record.
		const auto dropped = sink->GetWorkerDroppedCount ();
		if (dropped != publishedDropped)
		{
			publishedDropped = dropped;
			status.NoteDroppedRecords (LOG_WORKER, dropped);
		}
		if (!sink->IsOpen ())
		{
			// The next loop starts a replacement and reconfigures a fresh sink.
			continue;
		}
	}

	if (sink) (void)sink->Shutdown ();
	if (worker) worker->Stop (500);
#if defined(__OpenBSD__)
	if (reserveSink) (void)reserveSink->Shutdown ();
	if (reserveWorker) reserveWorker->Stop (500);
#endif
	return protocolFailed ? 1 : 0;
}
}

#endif
