/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: fixed-authority router persistence interface.
 * Keeps protocol code independent of worker and platform transports.
 */
#ifndef I2PD_PERSISTENT_STORAGE_H
#define I2PD_PERSISTENT_STORAGE_H
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>
namespace i2p::data
{
enum class RouterPersistentFile : uint8_t
{
	RouterKeys = 1,
	RouterInfo,
	NTCP2Keys,
	SSU2Keys
};
using RouterPersistenceWriter = std::function<bool (RouterPersistentFile, std::span<const uint8_t>)>;
// Install before router initialization. An installed writer failure
// never falls back to a filesystem write in the network process.
// Replacement is synchronized.
void InstallRouterPersistenceWriter (RouterPersistenceWriter writer);
bool WriteRouterPersistentFile (RouterPersistentFile file, std::span<const uint8_t> bytes);
const char * RouterPersistentFileName (RouterPersistentFile file);

/**
 * Bulk router artifacts that are still written by the socket-owning
 * core.
 *
 * The name is the only authority for the destination path; it is
 * canonical and therefore cannot select a directory outside the
 * artifact root: - RouterInfoCache: 44-character base64 identity hash
 * ("netDb/r&lt;shard&gt;/routerInfo-&lt;name&gt;.dat") - PeerProfile:
 * 44-character base64 identity hash
 * ("peerProfiles/p&lt;shard&gt;/profile-&lt;name&gt;.txt") -
 * DestinationKeys: 52-character lowercase base32 identity, then '.',
 * then the decimal CryptoKeyType ("destinations/&lt;name&gt;.dat")
 *
 * The persisted bytes, file names and sizes are identical to the
 * previous direct writers, so a worker-written artifact stays readable
 * by the unchanged load paths.
 */
enum class StorageArtifact : uint8_t
{
	RouterInfoCache = 1,
	PeerProfile = 2,
	DestinationKeys = 3
};
using ArtifactWriter
	= std::function<bool (StorageArtifact kind, std::string_view name, std::span<const uint8_t> bytes)>;
using ArtifactRemover = std::function<bool (StorageArtifact kind, std::string_view name)>;

/**
 * Upper bound for one artifact payload; a cached RouterInfo is at most
 * 4 KiB.
 */
// Must match the data-storage worker's per-frame artifact cap
// (privsep/Protocol.h MAX_ARTIFACT_PAYLOAD_SIZE): the worker rejects a
// larger payload, so accepting it here would only defer the failure.
constexpr size_t MAX_ARTIFACT_SIZE = 16 * 1024;

/**
 * Process capability invariant.
 *
 * A process that owns transport sockets and router keys must never also
 * hold artifact-write authority, and a delegated artifact has exactly
 * one write path: the installed writer. These predicates are
 * compile-time so the rule can be pinned by a regression without a
 * build configuration.
 */
enum class ProcessCapability : uint8_t
{
	Network = 0,
	ArtifactWrite = 1
};
using CapabilityMask = uint32_t;
constexpr CapabilityMask CapabilityBit (ProcessCapability capability)
{
	return CapabilityMask (1) << static_cast<CapabilityMask> (capability);
}
/**
 * true when a capability set does not combine Network and ArtifactWrite
 */
constexpr bool IsCapabilitySetCompatible (CapabilityMask mask)
{
	return (mask & CapabilityBit (ProcessCapability::Network)) == 0
		|| (mask & CapabilityBit (ProcessCapability::ArtifactWrite)) == 0;
}
/**
 * true while the direct data-directory writer may still be used.
 * Enabling privsep.artifacts makes the installed writer the only write
 * path, so a missing or failing worker must never be retried through
 * the filesystem.
 */
constexpr bool IsDirectArtifactFallbackAllowed (bool artifactsDelegated)
{
	return !artifactsDelegated;
}

/**
 * true when "privsep.artifacts" delegates artifact writes to a worker
 */
bool AreArtifactWritesDelegated ();
/**
 * Replace the artifact backend. Install before the storage users start;
 * the caller keeps ownership of the worker lifecycle. Until an explicit
 * writer is installed the direct data-directory backend is the
 * effective writer, which is only used while privsep.artifacts is
 * disabled.
 */
void InstallArtifactStorage (ArtifactWriter writer, ArtifactRemover remover);
/**
 * Drop an installed backend and return to the direct data-directory
 * one.
 */
void ClearArtifactStorage ();
/** true when an explicit (worker) backend is installed */
bool IsArtifactStorageInstalled ();
/**
 * Write one artifact through the installed writer, or through the
 * direct backend while privsep.artifacts is disabled. A failed write
 * leaves the previous file intact (the writer replaces atomically), is
 * logged at most once per minute per kind and returns false to the
 * caller. It is never retried through the filesystem.
 */
bool WriteArtifact (StorageArtifact kind, std::string_view name, std::span<const uint8_t> bytes);
/** Remove one artifact; an already absent artifact is a success. */
bool RemoveArtifact (StorageArtifact kind, std::string_view name);
/**
 * stable log/protocol token for a kind, or nullptr for an unknown value
 */
const char * StorageArtifactName (StorageArtifact kind);
/**
 * true when name is the canonical identifier format required for kind
 */
bool IsValidArtifactName (StorageArtifact kind, std::string_view name);
}
#endif
