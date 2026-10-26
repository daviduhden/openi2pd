/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: atomic router-key and RouterInfo storage process.
 *
 * Only four fixed filenames are writable (router.keys, router.info,
 * ntcp2.keys, ssu2.keys); a write request carries no path field, so the worker
 * cannot be pointed at another object. The worker has no network capability.
 * Each write stages into one fixed same-directory slot, fsyncs it, renames it
 * over the target and fsyncs the directory, and refuses a symlink, hardlink,
 * directory or foreign-owned target at every step.
 *
 * The client is single-flight and generation-fenced: a reply is applied only
 * when both its generation and its request id match the outstanding request, so
 * a reply from a replaced worker cannot be mistaken for the current one. A lost
 * reply leaves the write's outcome unknown; it is reported as a failure and is
 * never replayed. Restarts use the shared bounded ladder and fail closed without
 * a core-side filesystem fallback. On OpenBSD the worker cannot be re-exec'ed
 * after the runtime pledge, so a lost worker stays terminal until the daemon
 * restarts.
 */
#include "RouterStorage.h"
#include "Credentials.h"
#include "DataStorage.h"
#include "Worker.h"
#include "LinuxSandbox.h"
#include "OpenBSDSandbox.h"
#include <mutex>
#include <optional>
#include <limits>
#include <utility>
#include <cerrno>
#include <cstdio>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace i2p::privsep
{
namespace
{
	struct Fd
	{
		int value;
		~Fd ()
		{
			if (value >= 0) ::close (value);
		}
		int Release () noexcept { return std::exchange (value, -1); }
	};
	const char * FileName (uint8_t id)
	{
		switch (static_cast<i2p::data::RouterPersistentFile> (id))
		{
		case i2p::data::RouterPersistentFile::RouterKeys: return "router.keys";
		case i2p::data::RouterPersistentFile::RouterInfo: return "router.info";
		case i2p::data::RouterPersistentFile::NTCP2Keys: return "ntcp2.keys";
		case i2p::data::RouterPersistentFile::SSU2Keys: return "ssu2.keys";
		}
		return nullptr;
	}
	bool AtomicWrite (int root, uint8_t id, std::string_view bytes)
	{
		const char * name = FileName (id);
		if (!name || bytes.empty () || bytes.size () > 32768) return false;
		const std::string temporary = std::string (".openi2pd-") + name + ".new";
		struct stat current { };
		if (::fstatat (root, name, &current, AT_SYMLINK_NOFOLLOW) == 0)
		{
			if (!S_ISREG (current.st_mode) || current.st_uid != ::geteuid () || current.st_nlink != 1) return false;
		}
		else if (errno != ENOENT)
			return false;
		// A fixed staging slot bounds leftovers. Only remove an owned regular slot;
		// never open/truncate an existing inode that might have gained another link.
		if (::fstatat (root, temporary.c_str (), &current, AT_SYMLINK_NOFOLLOW) == 0)
		{
			if (!S_ISREG (current.st_mode) || current.st_uid != ::geteuid () || current.st_nlink != 1
				|| ::unlinkat (root, temporary.c_str (), 0))
				return false;
		}
		else if (errno != ENOENT)
			return false;
		Fd output { ::openat (root, temporary.c_str (), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600) };
		if (output.value < 0) return false;
		size_t done = 0;
		while (done < bytes.size ())
		{
			ssize_t count = ::write (output.value, bytes.data () + done, bytes.size () - done);
			if (count < 0 && errno == EINTR) continue;
			if (count <= 0)
			{
				::unlinkat (root, temporary.c_str (), 0);
				return false;
			}
			done += static_cast<size_t> (count);
		}
		if (::fsync (output.value) || ::renameat (root, temporary.c_str (), root, name))
		{
			::unlinkat (root, temporary.c_str (), 0);
			return false;
		}
		return ::fsync (root) == 0;
	}
}
struct RouterStorageClient::Impl
{
	enum class State : uint8_t
	{
		Stopped,
		Ready,
		Restarting,
		Failed
	};

	std::mutex mutex;
	ChildProcess child;
	/** Validated root capability retained so a restart reuses the same
	 *  directory object instead of re-resolving an attacker-controlled path. */
	std::string rootPath;
	uint64_t device = 0;
	uint64_t inode = 0;
	int rootFd = -1;
	uint32_t next = 0;
	/** Bumped by every successful Start/configuration. Request ids are fenced
	 *  per generation so a reply from a replaced worker can never be applied. */
	uint32_t generation = 0;
	State state = State::Stopped;
	StorageClientRestart restart;
	bool restartPermitted = true;
	bool idExhausted = false;

	Impl ()
	{
#if defined(__OpenBSD__)
		// The core has no fork/exec authority after the runtime pledge, and no
		// broker path exists for this role yet. A lost worker is terminal
		// until the daemon restarts; writes fail closed instead of falling
		// back to a local key/RouterInfo file.
		restartPermitted = false;
#endif
	}

	void CloseWorker ()
	{
		child.ChannelFd ().Close ();
#if !defined(__OpenBSD__)
		child.Stop (500);
#else
		child.Disown ();
#endif
	}

	bool Configure (std::string & error)
	{
		CloseWorker ();
		if (rootFd < 0)
		{
			error = "router storage root capability is closed";
			return false;
		}
		const uint32_t nextGeneration = generation + 1;
#if defined(__OpenBSD__)
		auto worker = SpawnDetachedWorkerWithRoot ("router-storage-inherited", rootFd);
		if (!worker)
		{
			error = worker.error ();
			return false;
		}
		child = ChildProcess::Adopt (-1, std::move (*worker));
		const int passed = -1;
#else
		auto worker = SpawnWorker ("router-storage", { });
		if (!worker)
		{
			error = worker.error ();
			return false;
		}
		child = std::move (*worker);
		const int passed = rootFd;
#endif
		PayloadWriter setup;
		if (!setup.WritePath (rootPath))
		{
			error = "router storage root path exceeds the IPC bound";
			return false;
		}
		setup.WriteU64BE (device);
		setup.WriteU64BE (inode);
		auto & channel = child.ChannelFd ();
		channel.SetEnforcementPolicy (EnforcementPolicy::Endpoint);
		const auto id = ++next;
		if (!channel.SendWithFd ({ MessageType::ConfigureRouterStorage, id, setup.Data () }, passed, 5000))
		{
			error = "router storage worker is unreachable";
			return false;
		}
		auto reply = channel.ReceiveWithFd (5000);
		if (!reply || !*reply)
		{
			error = "router storage worker did not answer";
			return false;
		}
		auto & frame = **reply;
		if (frame.fd >= 0) ::close (frame.fd);
		const bool accepted = frame.fd < 0 && frame.message.requestId == id
			&& (frame.message.type == MessageType::RouterStorageReply || frame.message.type == MessageType::Error)
			&& (frame.message.type == MessageType::Error || frame.message.payload == std::string (1, '\0'));
		if (!accepted)
		{
			error = "router storage sandbox/setup failed";
			return false;
		}
		if (frame.message.type == MessageType::Error)
		{
			error = "router storage worker refused the configuration";
			return false;
		}
		generation = nextGeneration;
		restart.Reset (std::chrono::steady_clock::now ());
		return true;
	}

	void MarkRestart (std::string_view reason)
	{
		CloseWorker ();
		if (restartPermitted && restart.RecordFailure (std::chrono::steady_clock::now ()))
		{
			state = State::Restarting;
			return;
		}
		state = State::Failed;
		// A bounded diagnostic without any key material or artifact name.
		std::fprintf (stderr, "privsep router storage: client disabled (%.*s)\n", static_cast<int> (reason.size ()),
			reason.data ());
	}

	bool EnsureReady ()
	{
		if (state == State::Ready) return true;
		if (!restartPermitted || state != State::Restarting || !restart.CanAttempt (std::chrono::steady_clock::now ()))
			return false;
		std::string error;
		if (Configure (error))
		{
			state = State::Ready;
			return true;
		}
		if (restart.RecordFailure (std::chrono::steady_clock::now ()))
		{
			state = State::Restarting;
			return false;
		}
		state = State::Failed;
		std::fprintf (stderr, "privsep router storage: restart limit reached\n");
		return false;
	}

	bool Exchange (MessageType type, std::string payload)
	{
		auto & channel = child.ChannelFd ();
		// Request ids are fenced per generation: a reply is accepted only for
		// the generation that issued it and for the request still in flight.
		// Ids are monotonic across restarts, so an abandoned request can never
		// collide with a newer one.
		const auto requestGeneration = generation;
		const auto id = ++next;
		if (!channel.Send ({ type, id, std::move (payload) }, 5000))
		{
			MarkRestart ("send failed");
			return false;
		}
		auto result = channel.ReceiveWithFd (5000);
		if (!result || !*result)
		{
			MarkRestart ("worker did not answer");
			return false;
		}
		auto & reply = **result;
		// A worker-level write rejection (status '\1', for example ENOSPC or a
		// symlink/ownership check) is a failed operation, not a broken channel.
		const bool valid = reply.fd < 0 && reply.message.type == MessageType::RouterStorageReply
			&& reply.message.requestId == id && requestGeneration == generation
			&& (reply.message.payload == std::string (1, '\0') || reply.message.payload == std::string (1, '\1'));
		if (reply.fd >= 0) ::close (reply.fd);
		if (!valid)
		{
			// A malformed frame cannot be attributed to a request: the worker
			// is replaced rather than trusted for a second answer.
			MarkRestart ("malformed reply");
			return false;
		}
		return reply.message.payload == std::string (1, '\0');
	}
};
RouterStorageClient::RouterStorageClient ()
	: m_Impl (std::make_unique<Impl> ())
{
}
RouterStorageClient::~RouterStorageClient ()
{
	Stop ();
}
void RouterStorageClient::Stop ()
{
	std::lock_guard lock (m_Impl->mutex);
	m_Impl->state = Impl::State::Stopped;
	// Detached OpenBSD workers are owned only through channel EOF: no kill or
	// waitpid after runtime pledge. Linux owns/reaps its regular child.
	m_Impl->CloseWorker ();
	if (m_Impl->rootFd >= 0)
	{
		::close (m_Impl->rootFd);
		m_Impl->rootFd = -1;
	}
	m_Impl->restart.Reset (std::chrono::steady_clock::now ());
}
bool RouterStorageClient::Start (const std::filesystem::path & root, std::string & error)
{
	std::lock_guard lock (m_Impl->mutex);
	error.clear ();
	const uint32_t workerUid = WorkerUid ();
	if (m_Impl->state == Impl::State::Ready || workerUid == 0)
	{
		error = "router storage requires an unprivileged worker identity";
		return false;
	}
	std::error_code ec;
	auto absolute = std::filesystem::canonical (root, ec);
	if (ec || absolute.string ().size () > 4096)
	{
		error = "invalid router storage root";
		return false;
	}
	// Keep O_RDONLY rather than O_PATH: AtomicWrite performs fsync() on the
	// directory descriptor, which an O_PATH descriptor cannot satisfy.
	Fd directory { ::open (absolute.c_str (), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC) };
	struct stat status { };
	if (directory.value < 0 || ::fstat (directory.value, &status) || status.st_uid != workerUid
		|| (status.st_mode & 0022))
	{
		error = "unsafe router storage root";
		return false;
	}
	if (m_Impl->rootFd >= 0) ::close (m_Impl->rootFd);
	m_Impl->rootFd = directory.Release ();
	m_Impl->rootPath = absolute.string ();
	m_Impl->device = static_cast<uint64_t> (status.st_dev);
	m_Impl->inode = static_cast<uint64_t> (status.st_ino);
	m_Impl->restart.Reset (std::chrono::steady_clock::now ());
	if (!m_Impl->Configure (error))
	{
		m_Impl->state = Impl::State::Stopped;
		m_Impl->CloseWorker ();
		return false;
	}
	m_Impl->state = Impl::State::Ready;
	return true;
}
bool RouterStorageClient::Write (i2p::data::RouterPersistentFile file, std::span<const uint8_t> bytes)
{
	std::lock_guard lock (m_Impl->mutex);
	if (!FileName (static_cast<uint8_t> (file)) || bytes.empty () || bytes.size () > 32768) return false;
	if (m_Impl->state == Impl::State::Stopped || m_Impl->state == Impl::State::Failed) return false;
	// A backoff delay or a failed respawn is a Busy refusal. It must not be
	// reported as a completed write and must not fall back to a local file.
	if (!m_Impl->EnsureReady ()) return false;
	if (m_Impl->next == std::numeric_limits<uint32_t>::max ())
	{
		// Exhaustion disables new requests but keeps the live channel usable;
		// the alternative (killing the client) would also strand later reads.
		if (!m_Impl->idExhausted)
		{
			m_Impl->idExhausted = true;
			std::fprintf (stderr, "privsep router storage: request id space exhausted\n");
		}
		return false;
	}
	std::string request (1, static_cast<char> (file));
	request.append (reinterpret_cast<const char *> (bytes.data ()), bytes.size ());
	return m_Impl->Exchange (MessageType::RouterStorageRequest, std::move (request));
}
int RunRouterStorageWorker (int fd, int inheritedRoot)
{
	Channel channel = Channel::Adopt (fd);
	channel.SetEnforcementPolicy (EnforcementPolicy::Endpoint);
#if defined(__linux__)
	// The socket/capability boundary does not need the delegated root and is
	// installed before the first Configure frame is parsed. This runs in the
	// worker, never in the socket-owning core.
	if (auto lockdown = ApplyLinuxIpcLockdown (GetLinuxSandboxProfile (WorkerClass::Storage)); !lockdown) return 1;
#endif
	auto first = channel.ReceiveWithFd (5000);
	if (!first || !*first) return 1;
	auto & config = **first;
	Fd root { inheritedRoot >= 0 ? inheritedRoot : config.fd };
	if (inheritedRoot >= 0 && config.fd >= 0)
	{
		::close (config.fd);
		return 1;
	}
#if defined(__OpenBSD__)
	if (inheritedRoot != 4 || config.fd >= 0) return 1;
#endif
	auto peer = channel.PeerUid ();
	struct stat status { };
	if (config.message.type != MessageType::ConfigureRouterStorage || !config.message.requestId || !peer
		|| !IsAllowedParentUid (*peer) || ::geteuid () == 0 || root.value < 0 || ::fstat (root.value, &status)
		|| !S_ISDIR (status.st_mode) || status.st_uid != ::geteuid () || (status.st_mode & 0022))
		return 1;
	PayloadReader setup (config.message.payload);
	auto path = setup.ReadString ();
	auto expectedDevice = setup.ReadU64BE ();
	auto expectedInode = setup.ReadU64BE ();
	if (!path || !expectedDevice || !expectedInode || !setup.Done () || path->empty () || path->front () != '/'
		|| path->size () > MAX_PATH_SIZE || path->find ('\0') != std::string_view::npos
		|| static_cast<uint64_t> (status.st_dev) != *expectedDevice
		|| static_cast<uint64_t> (status.st_ino) != *expectedInode)
		return 1;
	if (root.value <= 3)
	{
		int replacement = ::fcntl (root.value, F_DUPFD_CLOEXEC, 4);
		if (replacement < 0) return 1;
		::close (root.value);
		root.value = replacement;
	}
	::close (0);
	::close (1);
	::close (2);
#if defined(__linux__) && !defined(__ANDROID__)
	auto profile = GetLinuxSandboxProfile (WorkerClass::Storage);
	profile.restrictFilesystemReads = true;
	profile.writablePaths = { "/proc/self/fd/" + std::to_string (root.value) };
	if (!ApplyLinuxSandbox (profile)) return 1;
#elif defined(__OpenBSD__)
	OpenBSDPolicyConfig policyConfig;
	policyConfig.preopenedStorageIpc = true;
	policyConfig.dataDir = std::string (*path);
	auto policy = BuildOpenBSDPolicy (OpenBSDWorkerRole::Storage, policyConfig);
	if (!policy || !ApplyOpenBSDPolicy (*policy)) return 1;
#else
	return 1;
#endif
	if (!channel.Send ({ MessageType::RouterStorageReply, config.message.requestId, std::string (1, '\0') }, 5000))
		return 1;
	uint32_t lastRequest = 0;
	unsigned loopViolations = 0;
	std::optional<uint32_t> cancelledRequest;
	while (true)
	{
		auto incoming = channel.ReceiveWithFd (30000);
		if (!incoming) return 1;
		if (!*incoming) continue;
		auto & frame = **incoming;
		if (frame.fd >= 0)
		{
			::close (frame.fd);
			return 1;
		}
		const auto & request = frame.message;
		unsigned & violations = loopViolations;
		auto reject = [&] (ErrorCode code, std::string_view detail)
		{
			// A known type this endpoint may not receive, or a malformed
			// request, is answered once and then bounded: the channel stays
			// usable for a legitimate retry but a flooding peer is dropped.
			if (++violations > Channel::MAX_PROTOCOL_VIOLATIONS) return false;
			return channel
				.Send ({ MessageType::Error, request.requestId, EncodeError (request.type, code, detail) }, 5000)
				.has_value ();
		};
		if (!AllowsOperation (EndpointRole::RouterStorage, request.type))
		{
			if (!reject (ErrorCode::NotAuthorized, "operation is not allowed on the router storage endpoint")) return 1;
			continue;
		}
		if (request.type == MessageType::Ping)
		{
			if (!request.payload.empty ()) return 1;
			if (!channel.Send ({ MessageType::Pong, request.requestId, { } }, 5000)) return 1;
			continue;
		}
		if (request.type == MessageType::Shutdown)
		{
			if (!request.payload.empty ()) return 1;
			return 0;
		}
		if (request.type == MessageType::Cancel)
		{
			auto cancelled = DecodeCancel (request.payload);
			if (!cancelled)
			{
				if (!reject (ErrorCode::BadField, "invalid cancel request")) return 1;
				continue;
			}
			cancelledRequest = *cancelled;
			continue;
		}
		if (request.type != MessageType::RouterStorageRequest || !request.requestId || request.payload.size () < 2
			|| request.payload.size () > 32769)
		{
			if (!reject (ErrorCode::BadField, "invalid router storage request")) return 1;
			continue;
		}
		if (cancelledRequest && request.requestId == *cancelledRequest)
		{
			cancelledRequest.reset ();
			if (!channel.Send ({ MessageType::RouterStorageReply, request.requestId, std::string (1, '\1') }, 5000))
				return 1;
			continue;
		}
		if (request.requestId <= lastRequest)
		{
			if (!channel.Send ({ MessageType::RouterStorageReply, request.requestId, std::string (1, '\1') }, 5000))
				return 1;
			continue;
		}
		lastRequest = request.requestId;
		bool success = AtomicWrite (
			root.value, static_cast<uint8_t> (request.payload.front ()), std::string_view (request.payload).substr (1));
		if (!channel.Send (
				{ MessageType::RouterStorageReply, request.requestId, std::string (1, success ? '\0' : '\1') }, 5000))
			return 1;
	}
}
}
