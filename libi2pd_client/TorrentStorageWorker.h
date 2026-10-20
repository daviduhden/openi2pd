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
class TorrentStorageWorker
{
public:
 static constexpr size_t MaxTransfer = 16 * 1024;
 TorrentStorageWorker ();
 ~TorrentStorageWorker ();
 TorrentStorageWorker (const TorrentStorageWorker &) = delete;
 TorrentStorageWorker & operator= (const TorrentStorageWorker &) = delete;
 // Explicit administrative authority. No peer-provided absolute paths.
 bool Start (const std::filesystem::path & root, std::string & error);
 // Read registrations never create directories/files or change their length.
 // Only explicit create registrations delegate write authority.
 uint32_t Register (const std::filesystem::path & relative, uint64_t length, std::string & error, bool create = false);
 bool Read (uint32_t handle, uint64_t offset, std::span<uint8_t> bytes, std::string & error);
 bool Write (uint32_t handle, uint64_t offset, std::span<const uint8_t> bytes, std::string & error);
 bool Release (uint32_t handle, std::string & error);
 // Administrative payload lifecycle only; no recursive deletion or overwrite.
 bool Promote (const std::filesystem::path & source, const std::filesystem::path & target, uint64_t length, std::string & error);
 bool Remove (const std::filesystem::path & relative, uint64_t length, std::string & error);
 bool RemoveDirectory (const std::filesystem::path & relative, std::string & error);
 // Trusted startup configuration only: authorize one legacy flat DHT state filename.
 bool AuthorizeDHTMetadata (const std::filesystem::path & relative, std::string & error);
 bool ReadMetadata (const std::filesystem::path & relative, std::vector<uint8_t> & bytes, std::string & error);
 bool ReplaceMetadata (const std::filesystem::path & relative, std::span<const uint8_t> bytes, std::string & error);
 bool RemoveMetadata (const std::filesystem::path & relative, std::string & error);
 void Stop ();
private:
 struct Impl;
 std::unique_ptr<Impl> m_Impl;
};
}
#endif
