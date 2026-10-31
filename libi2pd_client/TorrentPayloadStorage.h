/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: torrent payload storage capabilities. Decouples piece IO
 * from filesystem and Android document-provider authority.
 */
#ifndef I2PD_TORRENT_PAYLOAD_STORAGE_H
#define I2PD_TORRENT_PAYLOAD_STORAGE_H
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <limits>
#include <utility>
#include "TorrentStorageWorker.h"

namespace i2p::torrents
{
/// Capability interface between torrent piece IO and a worker or SAF
/// broker. Implementations own their IPC/broker lifetime. Handles are
/// backend-local tokens, never OS descriptors; they must not be used
/// with another backend. Operations report success only for complete
/// IO. Implementations serialize shared transports, bound waits and do
/// not fall back to paths after failure. Open is administrative and
/// accepts only validated root-relative names; peer payload messages
/// cannot grant document authority.
class TorrentPayloadStorage
{
public:
	static constexpr size_t MaxTransfer = 16 * 1024;
	virtual ~TorrentPayloadStorage () = default;
	/// Document providers use final names; filesystem backends retain
	/// .part staging.
	virtual bool UsesFinalNames () const { return false; }
	/// Open an authorized relative name with the declared file length.
	/// @return A nonzero backend-local handle, or 0 on failure; @a error
	///         may describe the cause.
	virtual uint64_t Open (const std::filesystem::path & relative, uint64_t length, bool create, std::string & error)
		= 0;
	/// Read the entire requested span or return false; the caller discards
	/// a partial buffer on failure.
	virtual bool Read (uint64_t handle, uint64_t offset, std::span<uint8_t> bytes, std::string & error) = 0;
	/// Write the entire span or return false. Failure is not a rollback
	/// guarantee.
	virtual bool Write (uint64_t handle, uint64_t offset, std::span<const uint8_t> bytes, std::string & error) = 0;
	/// Release the handle acquired by Open().
	virtual bool Close (uint64_t handle, std::string & error) = 0;
	// Exact registered payload only; unsupported backends must never fall
	// back to filesystem paths for document-provider storage.
	virtual bool Remove (const std::filesystem::path &, uint64_t, std::string & error)
	{
		error = "Payload deletion is not supported by this backend";
		return false;
	}
};

/// Adapter retaining shared ownership of the existing isolated worker.
/// It validates the handle width and per-request transfer limit before
/// IPC.
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
