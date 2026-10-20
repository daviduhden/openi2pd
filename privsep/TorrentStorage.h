/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: typed torrent storage worker protocol.
 * Only setup and registration carry paths; payload IO uses bounded handles.
 */
#ifndef I2PD_PRIVSEP_TORRENT_STORAGE_H
#define I2PD_PRIVSEP_TORRENT_STORAGE_H
#include <cstddef>
#include <cstdint>
#include <string_view>
namespace i2p::privsep
{
inline constexpr size_t TorrentStorageBlock = 16 * 1024;
inline constexpr uint32_t TorrentStorageHandles = 4096;
inline constexpr uint64_t TorrentStorageFileLimit = uint64_t {1} << 40;
enum class StorageOperation : uint8_t { Register = 1, Read, Write, Release, Promote, Remove, RemoveDirectory, MetadataBegin, MetadataChunk, MetadataCommit, MetadataAbort, MetadataRemove, MetadataOpen, AuthorizeDHTMetadata };
bool SafeStoragePath (std::string_view path);
int RunTorrentStorageWorker (int fd, int inheritedRoot = -1);
}
#endif
