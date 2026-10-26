/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: typed torrent storage worker protocol.
 *
 * Declares the operation vocabulary, the fixed resource bounds and the worker
 * entry point for the network-denied torrent payload process. Only setup and
 * registration carry a path, and even those are relative to a delegated root
 * that the worker receives as a capability (an inherited descriptor on OpenBSD,
 * one SCM_RIGHTS descriptor on Linux) and validates against the caller's
 * device/inode identity before it applies its sandbox. Every later payload
 * operation names a worker-assigned handle, so a peer cannot re-point an open
 * file by sending a different path.
 *
 * The worker is an endpoint of the shared framed protocol
 * (privsep/Protocol.h): it enforces the per-type descriptor policy, refuses a
 * replayed or non-monotonic request id instead of re-executing a mutation, and
 * treats `Cancel` as advisory only. This header deliberately says nothing about
 * router networking or torrent session state; those stay in the core.
 */
#ifndef I2PD_PRIVSEP_TORRENT_STORAGE_H
#define I2PD_PRIVSEP_TORRENT_STORAGE_H
#include <cstddef>
#include <cstdint>
#include <string_view>
namespace i2p::privsep
{
/**
 * Largest single payload block accepted by Read/Write and by one metadata
 * chunk. It bounds both the request frame and the per-operation work, so a peer
 * cannot ask the worker to copy an arbitrarily large buffer in one step.
 */
inline constexpr size_t TorrentStorageBlock = 16 * 1024;
/**
 * Maximum number of payload/metadata handles registered at once. The handle
 * table is the worker's only long-lived per-peer state; reaching the limit is a
 * typed per-operation refusal (the channel stays usable), not a disconnect.
 */
inline constexpr uint32_t TorrentStorageHandles = 4096;
/**
 * Declared-size ceiling for one registered payload file (1 TiB). It is a sanity
 * bound on the registration claim, not an allocation: the registered length,
 * checked against the opened descriptor, is what bounds every later offset.
 */
inline constexpr uint64_t TorrentStorageFileLimit = uint64_t { 1 } << 40;
/**
 * Wire operation ids carried in the first payload byte of a
 * TorrentStorageRequest. Ids are part of the worker protocol; add a new one at
 * the end and update the worker dispatch and its tests together.
 */
enum class StorageOperation : uint8_t
{
	/** Open (create=1) or reopen (create=0) a payload path; reply carries the handle. */
	Register = 1,
	/** Read one block at an offset of a registered handle; reply carries the bytes. */
	Read,
	/** Write one block at an offset; only a handle opened writable accepts this. */
	Write,
	/** Close and forget one handle. Repeating it fails but does not poison the channel. */
	Release,
	/** Atomically publish a fully written staging file as the final payload name. */
	Promote,
	/** Delete one payload file after checking its owner, size and link count. */
	Remove,
	/** Remove one empty payload directory; never recursive. */
	RemoveDirectory,
	/** Start a bounded staging write of a metainfo/resume/DHT state file. */
	MetadataBegin,
	/** Append the next sequential chunk of the pending metadata file. */
	MetadataChunk,
	/** fsync and atomically rename the pending metadata file onto its target. */
	MetadataCommit,
	/** Discard the pending metadata file and its staging slot. */
	MetadataAbort,
	/** Delete a metainfo/resume/DHT state file under the metadata name rules. */
	MetadataRemove,
	/** Open an existing metadata file read-only and return its handle and size. */
	MetadataOpen,
	/**
	 * Allow one flat DHT state leaf name that the general metadata suffix rules
	 * would otherwise refuse. It is sent only from trusted tunnel configuration,
	 * never from a peer-supplied filename, and is capped per worker.
	 */
	AuthorizeDHTMetadata
};
/// Validate the lexical form of a root-relative worker path. This does not
/// open the path or prove containment; the worker must traverse from its root
/// descriptor without following links before acting on it.
bool SafeStoragePath (std::string_view path);
/// Serve typed operations after validating the delegated root and applying the
/// platform policy. An inherited root is used for the OpenBSD bootstrap path.
int RunTorrentStorageWorker (int fd, int inheritedRoot = -1);
}
#endif
