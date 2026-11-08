/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: typed torrent storage worker protocol.
 *
 * Declares the operation vocabulary, the fixed resource bounds and the
 * worker entry point for the network-denied torrent payload process.
 * The worker receives its delegated root as a capability: an inherited
 * descriptor on OpenBSD or one SCM_RIGHTS descriptor on Linux. It
 * checks the caller's device/inode identity before sandboxing.
 *
 * Read/Write/Release name worker-assigned handles. Registration,
 * promotion, removal and metadata operations carry names that are
 * resolved relative to the validated root. An existing handle cannot
 * be redirected by changing a later request's pathname.
 *
 * The worker is an endpoint of the shared framed protocol
 * (privsep/Protocol.h): it enforces the per-type descriptor policy,
 * refuses a replayed or non-monotonic request id instead of
 * re-executing a mutation, and treats `Cancel` as advisory only. This
 * header deliberately says nothing about router networking or torrent
 * session state; those stay in the core.
 */
#ifndef I2PD_PRIVSEP_TORRENT_STORAGE_H
#define I2PD_PRIVSEP_TORRENT_STORAGE_H
#include <cstddef>
#include <cstdint>
#include <string_view>
namespace i2p::privsep
{
/**
 * Largest single payload block accepted by Read/Write and by one
 * metadata chunk. It bounds both the request frame and the
 * per-operation work, so a peer cannot ask the worker to copy an
 * arbitrarily large buffer in one step.
 */
inline constexpr size_t TorrentStorageBlock = 16 * 1024;
/**
 * Maximum number of payload/metadata handles registered at once. The
 * handle table is the worker's only long-lived per-peer state; reaching
 * the limit is a typed per-operation refusal (the channel stays
 * usable), not a disconnect.
 */
inline constexpr uint32_t TorrentStorageHandles = 4096;
/**
 * Declared-size ceiling for one registered payload file (1 TiB). It is
 * a sanity bound on the registration claim, not an allocation: the
 * registered length, checked against the opened descriptor, is what
 * bounds every later offset.
 */
inline constexpr uint64_t TorrentStorageFileLimit = uint64_t { 1 } << 40;
/**
 * Wire operation ids carried in the first payload byte of a
 * TorrentStorageRequest. Ids are part of the worker protocol; add a new
 * one at the end and update the worker dispatch and its tests together.
 *
 * Requests execute sequentially in one worker loop. Read/Write offsets
 * are bounded by the handle's registered size; a successful Write reply
 * follows fsync. Failure is not rollback: a partial write or a rename
 * followed by directory-fsync failure may already have changed storage.
 * Do not retry an ambiguous mutation as though it had never executed.
 */
enum class StorageOperation : uint8_t
{
	/**
	 * Open (create=1) or reopen (create=0) a payload path; reply
	 * carries the handle. */
	Register = 1,
	/**
	 * Read one block at an offset of a registered handle; reply carries
	 * the bytes. */
	Read,
	/**
	 * Write one block at an offset; only a handle opened writable
	 * accepts this. */
	Write,
	/**
	 * Close and forget one handle. Repeating it fails but does not
	 * poison the channel. */
	Release,
	/**
	 * Atomically publish a fully written staging file as the final
	 * payload name. */
	Promote,
	/**
	 * Delete one payload file after checking its owner, size and link
	 * count. */
	Remove,
	/** Remove one empty payload directory; never recursive. */
	RemoveDirectory,
	/**
	 * Start a bounded staging write of a metainfo/resume/DHT state
	 * file. A new begin discards the previous pending staging write.
	 */
	MetadataBegin,
	/** Append the next sequential chunk of the pending metadata file. */
	MetadataChunk,
	/**
	 * fsync and atomically rename the pending metadata file onto its
	 * target. */
	MetadataCommit,
	/** Discard the pending metadata file and its staging slot. */
	MetadataAbort,
	/**
	 * Delete a metainfo/resume/DHT state file under the metadata name
	 * rules. */
	MetadataRemove,
	/**
	 * Open an existing metadata file read-only and return its handle
	 * and size. */
	MetadataOpen,
	/**
	 * Allow one flat DHT state leaf name that the general metadata suffix
	 * rules would otherwise refuse. It is sent only from trusted tunnel
	 * configuration, never from a peer-supplied filename, and is capped
	 * per worker.
	 */
	AuthorizeDHTMetadata
};
/**
 * Check root-relative path syntax without accessing the filesystem.
 * Reject absolute paths, empty/dot components, trailing separators,
 * control bytes, backslashes and colons. Limits are measured in bytes:
 * 4096 for the whole path and 255 for each component.
 *
 * This does not validate UTF-8, file type, ownership or containment.
 * The worker must walk from its root descriptor without following links
 * and apply operation-specific checks before using an accepted path.
 */
bool SafeStoragePath (std::string_view path);
/**
 * Run a blocking storage endpoint on its dedicated worker process.
 * On supported POSIX platforms, adopt fd as the owned IPC channel.
 * The OpenBSD bootstrap additionally supplies inheritedRoot; Linux
 * receives the root with configuration.
 * The worker closes standard descriptors before applying its policy.
 *
 * Return 0 on an authorized Shutdown and 1 on bootstrap, channel or
 * sandbox failure. A rejected operation can leave the channel usable.
 * Unsupported platforms fail instead of running an unconfined writer.
 * Callers must not use this entry point on a router event-loop thread.
 */
int RunTorrentStorageWorker (int fd, int inheritedRoot = -1);
}
#endif
