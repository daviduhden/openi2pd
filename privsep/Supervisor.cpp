/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Supervisor.
 *
 * Implements the privilege-separated supervisor: verifies the control peer's
 * UID, applies its platform sandbox and starts or restarts a log worker while
 * forwarding decoded records. It proxies log configuration without opening
 * the file, answers pings and exits on shutdown or malformed frames.
 *
 */

#include "Supervisor.h"

#ifndef _WIN32

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
 if (storageSupervisor) { error = "storage supervisor already prepared"; return false; }
 if (::geteuid () == 0) { error = "storage supervisor requires unprivileged identity"; return false; }
 std::error_code ec;
 const auto absolute = std::filesystem::canonical (root, ec);
 if (ec) { error = "invalid storage supervisor root"; return false; }
 auto spawned = SpawnDetachedWorker ("torrent-storage-supervisor");
 if (!spawned) { error = spawned.error (); return false; }
 PayloadWriter config; config.WriteString (absolute.string ()); config.WriteString (ExecutablePath ());
 auto sent = spawned->Send ({ MessageType::ConfigureTorrentStorage, 1, config.Data () }, 5000);
 if (!sent) { error = sent.error (); return false; }
 auto reply = spawned->Receive (5000);
 if (!reply || !*reply || (**reply).type != MessageType::TorrentStorageSupervisorReply
   || (**reply).requestId != 1 || !(**reply).payload.empty ())
 { error = "storage supervisor initialization failed"; return false; }
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
 struct stat expected {};
 if (rootFd < 0 || ::fstat (rootFd, &expected) || !S_ISDIR (expected.st_mode))
  return i2p::unexpected (std::string ("invalid local root identity"));
 const auto relative = std::filesystem::path (rootPath).lexically_relative (storageSupervisorRoot).generic_string ();
 PayloadWriter request; request.WriteString (relative);
 request.WriteU64BE (static_cast<uint64_t> (expected.st_dev));
 request.WriteU64BE (static_cast<uint64_t> (expected.st_ino));
 auto & channel = *storageSupervisor;
 auto sent = channel.Send ({ MessageType::SpawnTorrentStorage, id, request.Data () }, 5000);
 if (!sent) { storageSupervisor.reset (); return i2p::unexpected (sent.error ()); }
 auto received = channel.ReceiveWithFd (5000);
 if (!received || !*received)
 { storageSupervisor.reset (); return i2p::unexpected (std::string ("storage supervisor disconnected")); }
 auto & reply = **received;
 if (reply.message.type != MessageType::TorrentStorageSupervisorReply || reply.message.requestId != id
   || !reply.message.payload.empty () || reply.fd < 0)
 {
  if (reply.fd >= 0) ::close (reply.fd);
  storageSupervisor.reset ();
  return i2p::unexpected (std::string ("storage supervisor rejected worker capability"));
 }
 return Channel::Adopt (reply.fd);
}

int RunTorrentStorageSupervisor (int fd)
{
#if !defined(__OpenBSD__)
 (void)fd; return 1; // This broker is used only by the pre-pledge OpenBSD path.
#else
 Channel control = Channel::Adopt (fd);
 auto uid = control.PeerUid ();
 if (!uid || *uid != ::geteuid () || ::geteuid () == 0) return 1;
 // An inherited terminal/log descriptor might itself be network-capable.
 ::close (0); ::close (1); ::close (2);
 auto received = control.ReceiveWithFd (5000);
 if (!received || !*received) return 1;
 auto & initial = **received;
 struct RootOwner { int fd; ~RootOwner () { if (fd >= 0) ::close (fd); } } unexpected { initial.fd };
 struct stat rootInfo {};
 PayloadReader setup (initial.message.payload);
 auto configuredRoot = setup.ReadString ();
 auto configuredExecutable = setup.ReadString ();
 if (initial.message.type != MessageType::ConfigureTorrentStorage || initial.message.requestId != 1
   || initial.fd >= 0
   || !configuredRoot || !configuredExecutable || !setup.Done () || configuredRoot->empty () || configuredRoot->front () != '/'
   || configuredRoot->find ('\0') != std::string_view::npos) return 1;
 const std::string rootPath (*configuredRoot);
 // Trusted bootstrap only: open before confinement, and never accept another
 // absolute authority once this immutable root capability has been installed.
 RootOwner root { ::open (rootPath.c_str (), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC) };
 if (root.fd < 0 || ::fstat (root.fd, &rootInfo) || !S_ISDIR (rootInfo.st_mode)
   || rootInfo.st_uid != ::geteuid () || (rootInfo.st_mode & 0022)) return 1;
 const std::string executable (*configuredExecutable); // Frozen by trusted pre-pledge startup.
 if (executable.empty () || executable.front () != '/' || executable.find ('\0') != std::string::npos
   || ::access (executable.c_str (), X_OK)) return 1;
 // No inet/dns/wpath/cpath/fattr. rpath is needed only to compare delegated
 // directory identities through the immutable root FD. Each exec-ed child
 // installs its own storage-only policy before processing file requests.
 if (::unveil (rootPath.c_str (), "r") || ::unveil (executable.c_str (), "x")
   || ::unveil (nullptr, nullptr)
   || ::pledge ("stdio rpath unix proc exec sendfd recvfd", nullptr)) return 1;
 if (!control.Send ({ MessageType::TorrentStorageSupervisorReply, 1, {} }, 5000)) return 1;
 std::vector<ChildProcess> children;
 uint32_t launches = 0, lastRequest = 1;
 while (true)
 {
  for (auto it = children.begin (); it != children.end ();)
   if (!it->IsRunning ()) it = children.erase (it); else ++it;
  auto next = control.ReceiveWithFd (500);
  if (!next) return 1; // RAII stops/reaps every owned worker.
  if (!*next) continue;
  auto & request = **next;
  RootOwner unexpectedRequestFd { request.fd };
  if (request.message.type == MessageType::Shutdown && request.fd < 0) return 0;
  if (request.message.type != MessageType::SpawnTorrentStorage || request.message.requestId <= lastRequest
    || request.fd >= 0 || children.size () >= 16 || launches >= 128) return 1;
  lastRequest = request.message.requestId;
  PayloadReader payload (request.message.payload);
  auto path = payload.ReadString ();
  auto expectedDevice = payload.ReadU64BE (); auto expectedInode = payload.ReadU64BE ();
  if (!path || !expectedDevice || !expectedInode || !payload.Done ()) return 1;
  const std::string relative (*path);
  if (relative != "." && !SafeStoragePath (relative)) return 1;
  // Walk beneath the pre-authorized root without following symlinks. Expected
  // device/inode values detect replacement between the client's open and ours;
  // they never grant authority outside this root.
  RootOwner checked { ::dup (root.fd) };
  size_t start = 0;
  while (relative != "." && start < relative.size ())
  {
   auto slash = relative.find ('/', start);
   auto part = relative.substr (start, slash == std::string::npos ? relative.size () - start : slash - start);
   int child = ::openat (checked.fd, part.c_str (), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
   if (child < 0) return 1;
   ::close (checked.fd); checked.fd = child;
   if (slash == std::string::npos) break;
   start = slash + 1;
  }
  struct stat actual {};
  if (::fstat (checked.fd, &actual) || static_cast<uint64_t> (actual.st_dev) != *expectedDevice
    || static_cast<uint64_t> (actual.st_ino) != *expectedInode
    || actual.st_uid != ::geteuid () || !S_ISDIR (actual.st_mode) || (actual.st_mode & 0022)) return 1;
  auto child = SpawnWorkerWithRoot ("torrent-storage-inherited", executable, checked.fd);
  if (!child) return 1;
  ++launches;
  // Bind the worker to the checked capability here, before releasing its
  // channel. The client cannot substitute another root after authorization.
  PayloadWriter workerSetup; workerSetup.WriteString (relative == "." ? rootPath : rootPath + "/" + relative);
  if (!child->ChannelFd ().Send ({ MessageType::ConfigureTorrentStorage, 1, workerSetup.Data () }, 5000)) return 1;
  auto ready = child->ChannelFd ().Receive (5000);
  if (!ready || !*ready || (**ready).type != MessageType::TorrentStorageReply
    || (**ready).requestId != 1 || (**ready).payload != std::string (1, '\0')) return 1;
  if (!control.SendWithFd ({ MessageType::TorrentStorageSupervisorReply, lastRequest, {} }, child->ChannelFd ().Fd (), 5000)) return 1;
  child->ChannelFd ().Close (); // Do not keep the client endpoint alive.
  children.emplace_back (std::move (*child));
 }
#endif
}

int RunSupervisor (int fd)
{
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
	// The supervisor may fork/exec a replacement log worker later; the filter
	// therefore permits process creation and AF_UNIX socketpairs. It denies
	// AF_INET/AF_INET6, drops capabilities and (where available) restricts
	// pathname writes without blocking exec of the child.
	auto sandboxProfile = GetLinuxSandboxProfile (WorkerClass::Supervisor);
	sandboxProfile.requireLandlock = false;
	auto sandbox = ApplyLinuxSandbox (sandboxProfile);
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

	while (true)
	{
		if (!worker || !worker->IsRunning () || !sink || !sink->IsOpen ())
		{
			if (worker) worker->Stop (500);
			sink.reset ();
			std::this_thread::sleep_for (std::chrono::milliseconds (250));
			if (!startWorker ()) return 1;
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
		if (message.type == MessageType::Shutdown) break;
		if (message.type == MessageType::Ping)
		{
			(void)control.Send (Message { MessageType::Pong, message.requestId, { } }, 500);
			continue;
		}
		if (message.type != MessageType::LogRecord)
		{
			std::cerr << "privsep supervisor: unsupported message type" << std::endl;
			break;
		}
		auto record = DecodeLogRecord (message.payload);
		if (!record)
		{
			std::cerr << "privsep supervisor: malformed log record" << std::endl;
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
	return 0;
}
}

#endif
