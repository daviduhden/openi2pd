/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Supervisor.
 *
 * Implements the privilege-separated supervisor: verifies the control peer's
 * UID, applies its platform sandbox and starts or restarts a log worker while
 * forwarding decoded records. It also implements the pre-pledge OpenBSD
 * torrent-storage broker that inherits the data root as fd4 and delegates
 * bounded storage workers. The log path is never opened here; shutdown or a
 * malformed frame exits fail-closed.
 *
 */

#include "Supervisor.h"

#ifndef _WIN32

#include <algorithm>
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

#include "Channel.h"
#include "LinuxSandbox.h"
#include "LogWorker.h"
#include "OpenBSDSandbox.h"
#include "Protocol.h"
#include "Worker.h"

namespace i2p::privsep
{
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
			auto message = std::move ((*received)->message);
			if ((*received)->fd >= 0) close ((*received)->fd);
			if (message.type == MessageType::Ping)
			{
				if ((*received)->fd >= 0 || !message.payload.empty ())
				{
					std::cerr << "privsep supervisor: invalid ping" << std::endl;
					return false;
				}
				(void)channel.Send (Message { MessageType::Pong, message.requestId, { } }, 500);
				continue;
			}
			if (message.type != MessageType::ConfigureLog && message.type != MessageType::ConfigureLogFd)
			{
				std::cerr << "privsep supervisor: expected ConfigureLog" << std::endl;
				return false;
			}
			if ((*received)->fd >= 0)
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
	std::mutex storageSupervisorMutex;
	std::unique_ptr<Channel> storageSupervisor;
	uint32_t storageSupervisorRequest = 0;
	std::filesystem::path storageSupervisorRoot;
}

bool PrepareTorrentStorageSupervisor (const std::string & root, std::string & error)
{
	std::lock_guard lock (storageSupervisorMutex);
	error.clear ();
	if (storageSupervisor)
	{
		error = "storage supervisor already prepared";
		return false;
	}
	if (::geteuid () == 0)
	{
		error = "storage supervisor requires unprivileged identity";
		return false;
	}
	std::error_code ec;
	const auto absolute = std::filesystem::canonical (root, ec);
	if (ec)
	{
		error = "invalid storage supervisor root";
		return false;
	}
	// Open and validate the immutable root before spawning. The broker receives
	// it as inherited fd4, so it never re-resolves a path after the startup
	// sandbox and cannot be redirected by a path replacement/rename.
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
	if (::fstat (rootFd, &rootStatus) || !S_ISDIR (rootStatus.st_mode) || rootStatus.st_uid != ::geteuid ()
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
	auto sent = spawned->Send ({ MessageType::ConfigureTorrentStorage, 1, config.Data () }, 5000);
	if (!sent)
	{
		error = sent.error ();
		return false;
	}
	auto reply = spawned->Receive (5000);
	if (!reply || !*reply || (**reply).type != MessageType::TorrentStorageSupervisorReply || (**reply).requestId != 1
		|| !(**reply).payload.empty ())
	{
		error = "storage supervisor initialization failed";
		return false;
	}
	storageSupervisor = std::make_unique<Channel> (std::move (*spawned));
	storageSupervisorRequest = 1;
	storageSupervisorRoot = absolute;
	return true;
}

i2p::expected<Channel, std::string> SpawnSupervisedTorrentStorage (int rootFd, std::string_view rootPath)
{
	std::lock_guard lock (storageSupervisorMutex);
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
	auto & channel = *storageSupervisor;
	auto sent = channel.Send ({ MessageType::SpawnTorrentStorage, id, request.Data () }, 5000);
	if (!sent)
	{
		storageSupervisor.reset ();
		return i2p::unexpected (sent.error ());
	}
	std::optional<ReceivedMessage> selected;
	while (true)
	{
		auto received = channel.ReceiveWithFd (5000);
		if (!received)
		{
			storageSupervisor.reset ();
			return i2p::unexpected (std::string ("storage supervisor disconnected"));
		}
		if (!*received)
		{
			// A slow broker is not a dead broker. Keep the channel; the next request
			// drains a late reply by request id before matching.
			return i2p::unexpected (std::string ("storage supervisor busy"));
		}
		auto & candidate = **received;
		if (candidate.message.requestId < id)
		{
			if (candidate.fd >= 0) ::close (candidate.fd);
			continue;
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
		// A per-request rejection must not destroy the broker or other workers.
		return i2p::unexpected (std::string ("storage supervisor rejected request: ") + std::get<2> (*decoded));
	}
	if (reply.message.type != MessageType::TorrentStorageSupervisorReply || reply.message.requestId != id
		|| !reply.message.payload.empty () || reply.fd < 0)
	{
		if (reply.fd >= 0) ::close (reply.fd);
		storageSupervisor.reset ();
		return i2p::unexpected (std::string ("storage supervisor rejected worker capability"));
	}
	auto adopted = Channel::AdoptValidated (reply.fd, static_cast<uint32_t> (::geteuid ()));
	if (!adopted) return i2p::unexpected (adopted.error ());
	return std::move (*adopted);
}

int RunTorrentStorageSupervisor (int fd)
{
#if !defined(__OpenBSD__)
	(void)fd;
	return 1; // This broker is used only by the pre-pledge OpenBSD path.
#else
	Channel control = Channel::Adopt (fd);
	auto uid = control.PeerUid ();
	if (!uid || *uid != ::geteuid () || ::geteuid () == 0) return 1;
	// An inherited terminal/log descriptor might itself be network-capable.
	::close (0);
	::close (1);
	::close (2);
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
	struct RootOwner unexpected { initial.fd };
	PayloadReader setup (initial.message.payload);
	auto configuredRoot = setup.ReadString ();
	// Kept for wire compatibility only. It is never executed: the broker resolves
	// its own executable below and freezes that path before pledge.
	auto configuredExecutable = setup.ReadString ();
	if (initial.message.type != MessageType::ConfigureTorrentStorage || initial.message.requestId != 1
		|| initial.fd >= 0 || !configuredRoot || !configuredExecutable || !setup.Done () || configuredRoot->empty ()
		|| configuredRoot->front () != '/' || configuredRoot->size () > MAX_PATH_SIZE
		|| configuredRoot->find ('\0') != std::string_view::npos)
		return 1;
	const std::string rootPath (*configuredRoot);
	// Never exec a path nominated by the peer. Resolve the broker's own
	// executable before pledge/unveil and freeze it for every replacement.
	const std::string executable = ExecutablePath ();
	if (executable.empty () || executable.front () != '/' || executable.find ('\0') != std::string::npos
		|| ::access (executable.c_str (), X_OK))
		return 1;
	// fd4 is the only directory authority. The configured path is used solely
	// to construct each child's storage policy; it cannot widen that authority.
	// No inet/dns/wpath/cpath/fattr/rpath is needed: the broker never resolves
	// the root path again, and each exec-ed child installs its own policy.
	if (::unveil (executable.c_str (), "x") || ::unveil (nullptr, nullptr)
		|| ::pledge ("stdio unix proc exec sendfd recvfd", nullptr))
		return 1;
	if (!control.Send ({ MessageType::TorrentStorageSupervisorReply, 1, { } }, 5000)) return 1;
	uint32_t lastRequest = 1, violations = 0;
	std::vector<pid_t> detachedWorkers;
	// Forwarded workers are owned by the core after handoff, so the broker
	// cannot observe their liveness. Bound churn with a replenished one-minute
	// launch window rather than a permanent lifetime counter.
	constexpr uint32_t MAX_LAUNCHES_PER_WINDOW = 32;
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
			if (++violations > 16) return false;
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
			// Dropping a replay/duplicate avoids executing it and avoids confusing a
			// live client with a second reply for the same ID.
			if (++violations > 16) return 1;
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
		const std::string relative (*path);
		if (relative != "." && !SafeStoragePath (relative))
		{
			if (!sendError (id, MessageType::SpawnTorrentStorage, ErrorCode::BadField, "unsafe storage path")) return 1;
			continue;
		}
		// The worker inherits the immutable broker root at fd4 and performs the
		// relative walk itself before applying its own policy. The broker must not
		// use openat() here: it has no rpath promise and its unveil list is locked.
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
		auto child = SpawnWorkerWithRoot ("torrent-storage-inherited", executable, root.fd);
		if (!child)
		{
			if (!sendError (id, MessageType::SpawnTorrentStorage, ErrorCode::Internal, "cannot spawn storage worker"))
				return 1;
			continue;
		}
		++launches;
		if (!child->ChannelFd ().Send (
				{ MessageType::ConfigureTorrentStorageSupervised, 1, workerSetup.Data () }, 5000))
		{
			(void)sendError (
				id, MessageType::SpawnTorrentStorage, ErrorCode::Internal, "storage worker configure failed");
			continue;
		}
		auto ready = child->ChannelFd ().Receive (5000);
		if (!ready || !*ready || (**ready).type != MessageType::TorrentStorageReply || (**ready).requestId != 1
			|| (**ready).payload != std::string (1, '\0'))
		{
			(void)sendError (
				id, MessageType::SpawnTorrentStorage, ErrorCode::Internal, "storage worker did not become ready");
			continue;
		}
		if (!control.SendWithFd (
				{ MessageType::TorrentStorageSupervisorReply, id, { } }, child->ChannelFd ().Fd (), 5000))
			return 1;
		child->ChannelFd ().Close (); // Do not keep the client endpoint alive.
		// The core now owns the worker channel. Do not SIGTERM/SIGKILL it when the
		// broker exits: a broker shutdown must not corrupt an in-flight fsync. Keep
		// the PID only for non-blocking reaping.
		const pid_t workerPid = child->Pid ();
		child->Disown ();
		if (workerPid > 0) detachedWorkers.push_back (workerPid);
	}
#endif
}

int RunSupervisor (int fd)
{
	SanitizeInheritedStdio ();
	Channel control = Channel::Adopt (fd);
	auto peer = control.PeerUid ();
	if (!peer || *peer != geteuid ())
	{
		std::cerr << "privsep supervisor: rejecting peer with unexpected uid" << std::endl;
		return 1;
	}

	bool useSyslog = false;
	std::string path, tag;
	if (!ReadLogConfig (control, useSyslog, path, tag)) return 1;

	std::string workerExecutable;
#if defined(__OpenBSD__)
	// Resolve the executable before installing the supervisor sandbox. The
	// supervisor only needs proc/exec for bounded log-worker replacement; it
	// never reads or writes a log path itself and has no external network.
	workerExecutable = ExecutablePath ();
	if (workerExecutable.empty ())
	{
		std::cerr << "privsep supervisor: cannot resolve executable before sandbox" << std::endl;
		return 1;
	}
	OpenBSDPolicyConfig policyConfig;
	// Do not lock unveil here: the supervisor must exec the pre-resolved log
	// worker after applying this policy. pledge("stdio unix proc exec") already
	// denies pathname reads/writes; the child installs its own unveil policy
	// after execve.
	policyConfig.useUnveil = false;
	auto policy = BuildOpenBSDPolicy (OpenBSDWorkerRole::Supervisor, policyConfig);
	if (!policy)
	{
		std::cerr << "privsep supervisor: invalid OpenBSD policy: " << policy.error () << std::endl;
		return 1;
	}
	auto applied = ApplyOpenBSDPolicy (*policy);
	if (!applied)
	{
		std::cerr << "privsep supervisor: OpenBSD sandbox: " << applied.error () << std::endl;
		return 1;
	}
#endif

#if defined(__linux__)
	// Landlock/seccomp/network namespaces are inherited across fork+exec and a
	// child can only become stricter. Installing the supervisor's write-deny
	// or AF_INET-deny domain here would permanently prevent storage/network
	// children from using their delegated capability. Drop capabilities and
	// set no_new_privs; each exec'ed worker applies its own role policy.
	auto sandbox = ApplyLinuxSupervisorSandbox ();
	if (!sandbox)
	{
		std::cerr << "privsep supervisor: Linux sandbox: " << sandbox.error () << std::endl;
		return 1;
	}
#endif

	std::unique_ptr<ChildProcess> worker;
	std::unique_ptr<LogSink> sink;
	auto startWorker = [&] () -> bool
	{
		auto spawned = SpawnLogWorker (workerExecutable);
		if (!spawned)
		{
			std::cerr << "privsep supervisor: cannot spawn log worker: " << spawned.error () << std::endl;
			return false;
		}
		worker = std::make_unique<ChildProcess> (std::move (*spawned));
		auto channel = std::move (worker->ChannelFd ());
		if (!sink)
			sink = std::make_unique<LogSink> (std::move (channel));
		else if (!sink->ReattachProxy (std::move (channel)))
			return false;
		if (!sink->ConfigureProxy (useSyslog, path, tag))
		{
			// ConfigureProxy is only valid once; on restart ReattachProxy
			// already sent the same configuration.
			if (!sink->IsOpen ()) return false;
		}
		return sink->IsOpen ();
	};

	if (!startWorker ()) return 1;
	constexpr unsigned MAX_RESTARTS = 8;
	constexpr auto STABLE_PERIOD = std::chrono::seconds (60);
	unsigned restartAttempts = 0;
	std::chrono::milliseconds restartBackoff (250);
	auto workerStarted = std::chrono::steady_clock::now ();

	bool protocolFailed = false;
	while (true)
	{
		if (!worker || !worker->IsRunning () || !sink || !sink->IsOpen ())
		{
			if (std::chrono::steady_clock::now () - workerStarted >= STABLE_PERIOD)
			{
				restartAttempts = 0;
				restartBackoff = std::chrono::milliseconds (250);
			}
			if (restartAttempts >= MAX_RESTARTS)
			{
				std::cerr << "privsep supervisor: log worker restart limit reached" << std::endl;
				return 1;
			}
			if (worker) worker->Stop (500);
			sink.reset ();
			std::this_thread::sleep_for (restartBackoff);
			restartBackoff = std::min (restartBackoff * 2, std::chrono::milliseconds (30000));
			++restartAttempts;
			if (!startWorker ()) return 1;
			workerStarted = std::chrono::steady_clock::now ();
			continue;
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
			std::cerr << "privsep supervisor: operation is not allowed on this endpoint" << std::endl;
			protocolFailed = true;
			break;
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
		if (!sink->IsOpen ())
		{
			// The next loop starts a replacement and reattaches the sink.
			continue;
		}
	}

	if (sink) (void)sink->Shutdown ();
	if (worker) worker->Stop (500);
	return protocolFailed ? 1 : 0;
}
}

#endif
