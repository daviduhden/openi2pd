/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: isolated torrent storage client.
 * Delegates bounded file operations to an exec'ed, network-denied worker.
 */
#ifndef I2PD_TORRENT_STORAGE_WORKER_H
#define I2PD_TORRENT_STORAGE_WORKER_H
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>
namespace i2p::torrents
{
/// Owns one isolated payload-storage channel and serializes its requests.
/// Each exchange has a bounded wait; a transport failure retires the worker.
/// A write whose reply was lost may already have reached storage and is never
/// replayed by this client. Destruction stops the worker.
class TorrentStorageWorker
{
public:
	static constexpr size_t MaxTransfer = 16 * 1024;
	TorrentStorageWorker ();
	~TorrentStorageWorker ();
	TorrentStorageWorker (const TorrentStorageWorker &) = delete;
	TorrentStorageWorker & operator= (const TorrentStorageWorker &) = delete;
	/// Start with an administrator-authorized root owned by the worker identity.
	/// On failure, @a error describes the failure and no path fallback is used.
	bool Start (const std::filesystem::path & root, std::string & error);
	/// Register a root-relative file with an expected length.
	/// Read registrations do not create or resize files; @a create delegates
	/// write authority. Returns a nonzero worker-local handle, or 0 on failure.
	/// Handles cease to be valid when the worker stops.
	uint32_t Register (
		const std::filesystem::path & relative, uint64_t length, std::string & error, bool create = false);
	/// Read exactly the size of @a bytes; one request may not exceed MaxTransfer.
	bool Read (uint32_t handle, uint64_t offset, std::span<uint8_t> bytes, std::string & error);
	/// Write the entire @a bytes span within the registered file bounds; one
	/// request may not exceed MaxTransfer.
	/// @warning Failure after a send can mean that storage was already modified.
	bool Write (uint32_t handle, uint64_t offset, std::span<const uint8_t> bytes, std::string & error);
	/// Release a registered worker handle; 0 is never a valid handle.
	bool Release (uint32_t handle, std::string & error);
	// Administrative payload lifecycle only; no recursive deletion or overwrite.
	bool Promote (const std::filesystem::path & source, const std::filesystem::path & target, uint64_t length,
		std::string & error);
	bool Remove (const std::filesystem::path & relative, uint64_t length, std::string & error);
	bool RemoveEmptyDirectory (const std::filesystem::path & relative, std::string & error);
	/// Authorize one legacy flat DHT state filename from trusted startup configuration.
	bool AuthorizeDHTMetadata (const std::filesystem::path & relative, std::string & error);
	/// Read bounded metadata into @a bytes, publishing it only on complete success.
	bool ReadMetadata (const std::filesystem::path & relative, std::vector<uint8_t> & bytes, std::string & error);
	/// Replace metadata in a bounded chunked transaction; failure does not prove
	/// that a commit sent to the worker was rolled back.
	bool ReplaceMetadata (const std::filesystem::path & relative, std::span<const uint8_t> bytes, std::string & error);
	bool RemoveMetadata (const std::filesystem::path & relative, std::string & error);
	void Stop ();

private:
	struct Impl;
	std::unique_ptr<Impl> m_Impl;
};
}
#endif
