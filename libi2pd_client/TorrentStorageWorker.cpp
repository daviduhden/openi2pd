/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: torrent storage RPC client. Serializes one bounded
 * transaction; a timeout poisons the channel so late replies cannot be
 * mistaken for later requests. Unsupported platforms fail closed.
 */
#include "TorrentStorageWorker.h"
#include <algorithm>
#include <chrono>
#include <limits>
#include <mutex>
#if (defined(__linux__) || defined(__OpenBSD__)) && !defined(__ANDROID__) && !defined(I2PD_NO_PRIVSEP)
#include "../privsep/Worker.h"
#include "../privsep/Credentials.h"
#include "../privsep/Supervisor.h"
#include "../privsep/TorrentStorage.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#define I2PD_HAS_TORRENT_STORAGE_WORKER 1
#endif
namespace i2p::torrents
{
struct TorrentStorageWorker::Impl
{
	std::mutex mutex;
#ifdef I2PD_HAS_TORRENT_STORAGE_WORKER
	i2p::privsep::ChildProcess process;
	uint32_t next = 0;
	bool ready = false;
	// One request/response transaction on the storage channel. Callers
	// serialize through `mutex`, so at most one frame is in flight. Both
	// the send and the receive carry a 5 s deadline; a transport failure
	// retires the worker instead of leaving a late reply that could be
	// matched to the next request. A typed Error reply with the same
	// request id is an operation rejection and keeps the channel usable,
	// while a reply with a descriptor, a foreign id or an unrecognized
	// shape is treated as corruption and retires the worker.
	bool Exchange (
		i2p::privsep::MessageType type, std::string payload, int fd, std::string & result, std::string & error)
	{
		using namespace i2p::privsep;
		error.clear ();
		if (next == std::numeric_limits<uint32_t>::max ())
		{
			error = "storage request IDs exhausted";
			return false;
		}
		const auto id = ++next;
		auto sent = process.ChannelFd ().SendWithFd ({ type, id, std::move (payload) }, fd, 5000);
		if (!sent)
		{
			ready = false;
			process.Stop (100);
			error = sent.error ();
			return false;
		}
		auto received = process.ChannelFd ().ReceiveWithFd (5000);
		if (!received || !*received)
		{
			ready = false;
			process.Stop (100);
			error = "storage worker disconnected or timed out";
			return false;
		}
		auto & reply = **received;
		if (reply.fd >= 0) ::close (reply.fd);
		if (reply.message.type == MessageType::Error && reply.message.requestId == id && reply.fd < 0)
		{
			auto decoded = DecodeError (reply.message.payload);
			if (!decoded)
			{
				ready = false;
				process.Stop (100);
				error = "invalid storage error response";
				return false;
			}
			// A typed operation rejection is not a broken channel.
			error = std::get<2> (*decoded);
			return false;
		}
		if (reply.fd >= 0 || reply.message.type != MessageType::TorrentStorageReply || reply.message.requestId != id
			|| reply.message.payload.empty ()
			|| (reply.message.payload.front () != 0 && reply.message.payload.front () != 1))
		{
			ready = false;
			process.Stop (100);
			error = "invalid storage response";
			return false;
		}
		if (reply.message.payload.front ())
		{
			error = reply.message.payload.substr (1);
			return false;
		}
		result = reply.message.payload.substr (1);
		return true;
	}
#endif
};
TorrentStorageWorker::TorrentStorageWorker ()
	: m_Impl (std::make_unique<Impl> ())
{
}
TorrentStorageWorker::~TorrentStorageWorker ()
{
	Stop ();
}
void TorrentStorageWorker::Stop ()
{
	std::lock_guard lock (m_Impl->mutex);
#ifdef I2PD_HAS_TORRENT_STORAGE_WORKER
	m_Impl->ready = false;
	m_Impl->process.Stop (1000);
#endif
}
bool TorrentStorageWorker::Start (const std::filesystem::path & root, std::string & error)
{
	std::lock_guard lock (m_Impl->mutex);
	error.clear ();
#if defined(I2PD_HAS_TORRENT_STORAGE_WORKER)
	using namespace i2p::privsep;
	if (m_Impl->ready)
	{
		error = "storage worker already running";
		return false;
	}
	const uint32_t workerUid = WorkerUid ();
	if (workerUid == 0)
	{
		error = "storage worker requires an unprivileged worker identity";
		return false;
	}
	std::error_code ec;
	auto absolute = std::filesystem::canonical (root, ec);
	if (ec || absolute.native ().size () > 4096)
	{
		error = "invalid authorized storage root";
		return false;
	}
	// O_RDONLY rather than O_PATH: the worker fsyncs parent directories
	// after metadata/promote operations, which an O_PATH descriptor cannot
	// satisfy.
	int fd = ::open (absolute.c_str (), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if (fd < 0)
	{
		error = "cannot open authorized storage root";
		return false;
	}
	struct stat rootStatus { };
	if (::fstat (fd, &rootStatus) || !S_ISDIR (rootStatus.st_mode) || rootStatus.st_uid != workerUid
		|| (rootStatus.st_mode & 0022))
	{
		::close (fd);
		error = "unsafe authorized storage root";
		return false;
	}
#if defined(__OpenBSD__)
	// No fork/exec in the pledged core. The pre-started supervisor
	// validates the directory capability and configures the child before
	// handing over its IPC.
	auto channel = SpawnSupervisedTorrentStorage (fd, absolute.string ());
	::close (fd);
	if (!channel)
	{
		error = channel.error ();
		return false;
	}
	m_Impl->process = ChildProcess::Adopt (-1, std::move (*channel));
	m_Impl->process.ChannelFd ().SetEnforcementPolicy (EnforcementPolicy::Endpoint);
	m_Impl->ready = true;
	return true;
#else
	auto child = SpawnWorker ("torrent-storage", { });
	if (!child)
	{
		::close (fd);
		error = child.error ();
		return false;
	}
	m_Impl->process = std::move (*child);
	PayloadWriter setup;
	if (!setup.WritePath (absolute.string ()))
	{
		::close (fd);
		error = "storage root path exceeds the IPC bound";
		return false;
	}
	setup.WriteU64BE (static_cast<uint64_t> (rootStatus.st_dev));
	setup.WriteU64BE (static_cast<uint64_t> (rootStatus.st_ino));
	std::string result;
	const bool ok = m_Impl->Exchange (MessageType::ConfigureTorrentStorage, setup.Data (), fd, result, error);
	::close (fd);
	m_Impl->ready = ok;
	return ok;
#endif
#else
	(void)root;
	error = "isolated torrent storage backend unavailable on this platform";
	return false;
#endif
}
uint32_t TorrentStorageWorker::Register (
	const std::filesystem::path & relative, uint64_t length, std::string & error, bool create)
{
	std::lock_guard lock (m_Impl->mutex);
	error.clear ();
#ifdef I2PD_HAS_TORRENT_STORAGE_WORKER
	using namespace i2p::privsep;
	auto path = relative.generic_string ();
	if (!m_Impl->ready || !SafeStoragePath (path) || length > TorrentStorageFileLimit)
	{
		error = "invalid storage registration or worker unavailable";
		return 0;
	}
	PayloadWriter request;
	request.WriteU8 (static_cast<uint8_t> (StorageOperation::Register));
	if (!request.WritePath (path))
	{
		error = "storage path exceeds the IPC bound";
		return 0;
	}
	request.WriteU64BE (length);
	request.WriteU8 (create ? 1 : 0);
	std::string reply;
	if (!m_Impl->Exchange (MessageType::TorrentStorageRequest, request.Data (), -1, reply, error)) return 0;
	PayloadReader reader (reply);
	auto id = reader.ReadU32 ();
	if (!id || !*id || !reader.Done ())
	{
		error = "invalid storage handle";
		return 0;
	}
	return *id;
#else
	(void)relative;
	(void)length;
	(void)create;
	error = "isolated torrent storage unavailable";
	return 0;
#endif
}
bool TorrentStorageWorker::Read (uint32_t handle, uint64_t offset, std::span<uint8_t> bytes, std::string & error)
{
	std::lock_guard lock (m_Impl->mutex);
	error.clear ();
#ifdef I2PD_HAS_TORRENT_STORAGE_WORKER
	using namespace i2p::privsep;
	if (!m_Impl->ready || !handle || bytes.size () > MaxTransfer)
	{
		error = "invalid storage read";
		return false;
	}
	PayloadWriter request;
	request.WriteU8 (static_cast<uint8_t> (StorageOperation::Read));
	request.WriteU32 (handle);
	request.WriteU64BE (offset);
	request.WriteU32 (bytes.size ());
	std::string reply;
	if (!m_Impl->Exchange (MessageType::TorrentStorageRequest, request.Data (), -1, reply, error)) return false;
	if (reply.size () != bytes.size ())
	{
		error = "invalid storage read length";
		return false;
	}
	std::copy (reply.begin (), reply.end (), bytes.begin ());
	return true;
#else
	(void)handle;
	(void)offset;
	(void)bytes;
	error = "isolated torrent storage unavailable";
	return false;
#endif
}
bool TorrentStorageWorker::Write (uint32_t handle, uint64_t offset, std::span<const uint8_t> bytes, std::string & error)
{
	std::lock_guard lock (m_Impl->mutex);
	error.clear ();
#ifdef I2PD_HAS_TORRENT_STORAGE_WORKER
	using namespace i2p::privsep;
	if (!m_Impl->ready || !handle || bytes.size () > MaxTransfer)
	{
		error = "invalid storage write";
		return false;
	}
	PayloadWriter header;
	header.WriteU8 (static_cast<uint8_t> (StorageOperation::Write));
	header.WriteU32 (handle);
	header.WriteU64BE (offset);
	header.WriteU32 (bytes.size ());
	std::string request = header.Data ();
	if (!bytes.empty ()) request.append (reinterpret_cast<const char *> (bytes.data ()), bytes.size ());
	std::string reply;
	return m_Impl->Exchange (MessageType::TorrentStorageRequest, std::move (request), -1, reply, error)
		&& reply.empty ();
#else
	(void)handle;
	(void)offset;
	(void)bytes;
	error = "isolated torrent storage unavailable";
	return false;
#endif
}
bool TorrentStorageWorker::Release (uint32_t handle, std::string & error)
{
	std::lock_guard lock (m_Impl->mutex);
	error.clear ();
#ifdef I2PD_HAS_TORRENT_STORAGE_WORKER
	using namespace i2p::privsep;
	if (!m_Impl->ready || !handle)
	{
		error = "invalid storage handle";
		return false;
	}
	PayloadWriter request;
	request.WriteU8 (static_cast<uint8_t> (StorageOperation::Release));
	request.WriteU32 (handle);
	std::string reply;
	return m_Impl->Exchange (MessageType::TorrentStorageRequest, request.Data (), -1, reply, error) && reply.empty ();
#else
	(void)handle;
	error = "isolated torrent storage unavailable";
	return false;
#endif
}
bool TorrentStorageWorker::Promote (
	const std::filesystem::path & source, const std::filesystem::path & target, uint64_t length, std::string & error)
{
	std::lock_guard lock (m_Impl->mutex);
	error.clear ();
#ifdef I2PD_HAS_TORRENT_STORAGE_WORKER
	using namespace i2p::privsep;
	auto from = source.generic_string (), to = target.generic_string ();
	if (!m_Impl->ready || !SafeStoragePath (from) || !SafeStoragePath (to) || length > TorrentStorageFileLimit)
	{
		error = "invalid storage promotion";
		return false;
	}
	PayloadWriter request;
	request.WriteU8 (static_cast<uint8_t> (StorageOperation::Promote));
	if (!request.WritePath (from) || !request.WritePath (to))
	{
		error = "storage path exceeds the IPC bound";
		return false;
	}
	request.WriteU64BE (length);
	std::string reply;
	return m_Impl->Exchange (MessageType::TorrentStorageRequest, request.Data (), -1, reply, error) && reply.empty ();
#else
	(void)source;
	(void)target;
	(void)length;
	error = "isolated torrent storage unavailable";
	return false;
#endif
}
bool TorrentStorageWorker::Remove (const std::filesystem::path & relative, uint64_t length, std::string & error)
{
	std::lock_guard lock (m_Impl->mutex);
	error.clear ();
#ifdef I2PD_HAS_TORRENT_STORAGE_WORKER
	using namespace i2p::privsep;
	auto path = relative.generic_string ();
	if (!m_Impl->ready || !SafeStoragePath (path) || length > TorrentStorageFileLimit)
	{
		error = "invalid storage removal";
		return false;
	}
	PayloadWriter request;
	request.WriteU8 (static_cast<uint8_t> (StorageOperation::Remove));
	if (!request.WritePath (path))
	{
		error = "storage path exceeds the IPC bound";
		return false;
	}
	request.WriteU64BE (length);
	std::string reply;
	return m_Impl->Exchange (MessageType::TorrentStorageRequest, request.Data (), -1, reply, error) && reply.empty ();
#else
	(void)relative;
	(void)length;
	error = "isolated torrent storage unavailable";
	return false;
#endif
}
bool TorrentStorageWorker::RemoveEmptyDirectory (const std::filesystem::path & relative, std::string & error)
{
	std::lock_guard lock (m_Impl->mutex);
	error.clear ();
#ifdef I2PD_HAS_TORRENT_STORAGE_WORKER
	using namespace i2p::privsep;
	auto path = relative.generic_string ();
	if (!m_Impl->ready || !SafeStoragePath (path))
	{
		error = "invalid storage directory removal";
		return false;
	}
	PayloadWriter request;
	request.WriteU8 (static_cast<uint8_t> (StorageOperation::RemoveDirectory));
	if (!request.WritePath (path))
	{
		error = "storage path exceeds the IPC bound";
		return false;
	}
	std::string reply;
	return m_Impl->Exchange (MessageType::TorrentStorageRequest, request.Data (), -1, reply, error) && reply.empty ();
#else
	(void)relative;
	error = "isolated torrent storage unavailable";
	return false;
#endif
}

bool TorrentStorageWorker::AuthorizeDHTMetadata (const std::filesystem::path & relative, std::string & error)
{
	std::lock_guard lock (m_Impl->mutex);
	error.clear ();
#ifdef I2PD_HAS_TORRENT_STORAGE_WORKER
	using namespace i2p::privsep;
	const auto path = relative.generic_string ();
	if (!m_Impl->ready || !SafeStoragePath (path) || path.find ('/') != std::string::npos
		|| path.starts_with (".openi2pd") || path.starts_with (".i2pd"))
	{
		error = "invalid DHT metadata authority";
		return false;
	}
	PayloadWriter request;
	request.WriteU8 (static_cast<uint8_t> (StorageOperation::AuthorizeDHTMetadata));
	if (!request.WritePath (path))
	{
		error = "storage path exceeds the IPC bound";
		return false;
	}
	std::string reply;
	return m_Impl->Exchange (MessageType::TorrentStorageRequest, request.Data (), -1, reply, error) && reply.empty ();
#else
	(void)relative;
	error = "isolated torrent storage unavailable";
	return false;
#endif
}
bool TorrentStorageWorker::ReadMetadata (
	const std::filesystem::path & relative, std::vector<uint8_t> & bytes, std::string & error)
{
	std::lock_guard lock (m_Impl->mutex);
	error.clear ();
	bytes.clear ();
#ifdef I2PD_HAS_TORRENT_STORAGE_WORKER
	using namespace i2p::privsep;
	const auto path = relative.generic_string ();
	if (!m_Impl->ready || !SafeStoragePath (path))
	{
		error = "invalid metadata read";
		return false;
	}
	PayloadWriter request;
	request.WriteU8 (static_cast<uint8_t> (StorageOperation::MetadataOpen));
	if (!request.WritePath (path))
	{
		error = "storage path exceeds the IPC bound";
		return false;
	}
	std::string reply;
	if (!m_Impl->Exchange (MessageType::TorrentStorageRequest, request.Data (), -1, reply, error)) return false;
	PayloadReader reader (reply);
	const auto handle = reader.ReadU32 ();
	const auto size = reader.ReadU64BE ();
	if (!handle || !*handle || !size || *size > 8 * 1024 * 1024 || !reader.Done ())
	{
		m_Impl->ready = false;
		m_Impl->process.Stop (100);
		error = "invalid metadata registration";
		return false;
	}
	std::vector<uint8_t> result (static_cast<size_t> (*size));
	const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (30);
	bool ok = true;
	for (size_t offset = 0; ok && offset < result.size ();)
	{
		if (std::chrono::steady_clock::now () >= deadline)
		{
			error = "metadata read deadline exceeded";
			ok = false;
			break;
		}
		const auto count = std::min (MaxTransfer, result.size () - offset);
		PayloadWriter read;
		read.WriteU8 (static_cast<uint8_t> (StorageOperation::Read));
		read.WriteU32 (*handle);
		read.WriteU64BE (offset);
		read.WriteU32 (static_cast<uint32_t> (count));
		ok = m_Impl->Exchange (MessageType::TorrentStorageRequest, read.Data (), -1, reply, error);
		if (ok && reply.size () != count)
		{
			error = "invalid metadata read length";
			ok = false;
		}
		if (ok) std::copy (reply.begin (), reply.end (), result.begin () + offset);
		offset += count;
	}
	if (m_Impl->ready)
	{
		const auto original = error;
		PayloadWriter release;
		release.WriteU8 (static_cast<uint8_t> (StorageOperation::Release));
		release.WriteU32 (*handle);
		const bool released = m_Impl->Exchange (MessageType::TorrentStorageRequest, release.Data (), -1, reply, error)
			&& reply.empty ();
		if (!ok) error = original;
		ok = ok && released;
	}
	if (ok) bytes = std::move (result);
	return ok;
#else
	(void)relative;
	error = "isolated torrent storage unavailable";
	return false;
#endif
}
bool TorrentStorageWorker::ReplaceMetadata (
	const std::filesystem::path & relative, std::span<const uint8_t> bytes, std::string & error)
{
	std::lock_guard lock (m_Impl->mutex);
	error.clear ();
#ifdef I2PD_HAS_TORRENT_STORAGE_WORKER
	using namespace i2p::privsep;
	auto path = relative.generic_string ();
	if (!m_Impl->ready || !SafeStoragePath (path) || bytes.size () > 8 * 1024 * 1024)
	{
		error = "invalid metadata replacement";
		return false;
	}
	std::string reply;
	auto exchange = [&] (std::string payload)
	{
		return m_Impl->Exchange (MessageType::TorrentStorageRequest, std::move (payload), -1, reply, error)
			&& reply.empty ();
	};
	PayloadWriter begin;
	begin.WriteU8 (static_cast<uint8_t> (StorageOperation::MetadataBegin));
	if (!begin.WritePath (path))
	{
		error = "metadata path exceeds the IPC bound";
		return false;
	}
	begin.WriteU64BE (bytes.size ());
	if (!exchange (begin.Data ())) return false;
	const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (30);
	bool ok = true;
	for (size_t offset = 0; ok && offset < bytes.size ();)
	{
		if (std::chrono::steady_clock::now () >= deadline)
		{
			error = "metadata transaction deadline exceeded";
			ok = false;
			break;
		}
		auto length = std::min (MaxTransfer, bytes.size () - offset);
		PayloadWriter chunk;
		chunk.WriteU8 (static_cast<uint8_t> (StorageOperation::MetadataChunk));
		chunk.WriteU64BE (offset);
		std::string payload = chunk.Data ();
		payload.append (reinterpret_cast<const char *> (bytes.data () + offset), length);
		ok = exchange (std::move (payload));
		offset += length;
	}
	if (ok && std::chrono::steady_clock::now () >= deadline)
	{
		error = "metadata transaction deadline exceeded";
		ok = false;
	}
	if (ok)
	{
		PayloadWriter commit;
		commit.WriteU8 (static_cast<uint8_t> (StorageOperation::MetadataCommit));
		ok = exchange (commit.Data ());
	}
	if (!ok && m_Impl->ready)
	{
		auto original = error;
		PayloadWriter abort;
		abort.WriteU8 (static_cast<uint8_t> (StorageOperation::MetadataAbort));
		exchange (abort.Data ());
		error = std::move (original);
	}
	return ok;
#else
	(void)relative;
	(void)bytes;
	error = "isolated torrent storage unavailable";
	return false;
#endif
}
bool TorrentStorageWorker::RemoveMetadata (const std::filesystem::path & relative, std::string & error)
{
	std::lock_guard lock (m_Impl->mutex);
	error.clear ();
#ifdef I2PD_HAS_TORRENT_STORAGE_WORKER
	using namespace i2p::privsep;
	auto path = relative.generic_string ();
	if (!m_Impl->ready || !SafeStoragePath (path))
	{
		error = "invalid metadata removal";
		return false;
	}
	PayloadWriter request;
	request.WriteU8 (static_cast<uint8_t> (StorageOperation::MetadataRemove));
	if (!request.WritePath (path))
	{
		error = "metadata path exceeds the IPC bound";
		return false;
	}
	std::string reply;
	return m_Impl->Exchange (MessageType::TorrentStorageRequest, request.Data (), -1, reply, error) && reply.empty ();
#else
	(void)relative;
	error = "isolated torrent storage unavailable";
	return false;
#endif
}

}
