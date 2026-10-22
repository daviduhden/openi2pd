/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: torrent payload storage capabilities.
 * Decouples piece IO from filesystem and Android document-provider authority.
 */
#ifndef I2PD_TORRENT_PAYLOAD_STORAGE_H
#define I2PD_TORRENT_PAYLOAD_STORAGE_H
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <limits>
#include "TorrentStorageWorker.h"

namespace i2p::torrents
{
// Implementations own their IPC/broker lifetime. A handle is local to this
// backend, never an OS descriptor. Every operation is exact-length: partial IO,
// revocation, cancellation and unsupported seek return false. Implementations
// must serialize shared transports, bound waits and never fall back to paths.
// Open is administrative and accepts only validated root-relative names;
// payload messages received from peers cannot grant document authority.
class TorrentPayloadStorage
{
public:
	static constexpr size_t MaxTransfer = 16 * 1024;
	virtual ~TorrentPayloadStorage () = default;
	// Document providers use stable names; filesystem backends retain .part staging.
	virtual bool UsesFinalNames () const { return false; }
	virtual uint64_t Open (const std::filesystem::path & relative, uint64_t length, bool create, std::string & error)
		= 0;
	virtual bool Read (uint64_t handle, uint64_t offset, std::span<uint8_t> bytes, std::string & error) = 0;
	virtual bool Write (uint64_t handle, uint64_t offset, std::span<const uint8_t> bytes, std::string & error) = 0;
	virtual bool Close (uint64_t handle, std::string & error) = 0;
	// Exact registered payload only; unsupported backends must never fall back
	// to filesystem paths for document-provider storage.
	virtual bool Remove (const std::filesystem::path &, uint64_t, std::string & error)
	{
		error = "Payload deletion is not supported by this backend";
		return false;
	}
};

// Reuses the real isolated worker rather than introducing another IO engine.
class WorkerPayloadStorage final : public TorrentPayloadStorage
{
public:
	explicit WorkerPayloadStorage (std::shared_ptr<TorrentStorageWorker> worker)
		: m_Worker (std::move (worker))
	{
	}
	uint64_t Open (const std::filesystem::path & relative, uint64_t length, bool create, std::string & error) override
	{
		return m_Worker ? m_Worker->Register (relative, length, error, create) : 0;
	}
	bool Read (uint64_t handle, uint64_t offset, std::span<uint8_t> bytes, std::string & error) override
	{
		return Valid (handle, bytes.size (), error)
			&& m_Worker->Read (static_cast<uint32_t> (handle), offset, bytes, error);
	}
	bool Write (uint64_t handle, uint64_t offset, std::span<const uint8_t> bytes, std::string & error) override
	{
		return Valid (handle, bytes.size (), error)
			&& m_Worker->Write (static_cast<uint32_t> (handle), offset, bytes, error);
	}
	bool Close (uint64_t handle, std::string & error) override
	{
		return Valid (handle, 0, error) && m_Worker->Release (static_cast<uint32_t> (handle), error);
	}

private:
	bool Valid (uint64_t handle, size_t length, std::string & error) const
	{
		if (m_Worker && handle && handle <= std::numeric_limits<uint32_t>::max () && length <= MaxTransfer) return true;
		error = "Invalid storage capability or transfer size";
		return false;
	}
	std::shared_ptr<TorrentStorageWorker> m_Worker;
};
}
#endif
