/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: transport-independent router persistence dispatch.
 */
#include "PersistentStorage.h"
#include "Config.h"
#include "FS.h"
#include "Log.h"
#include "Tag.h"
#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>
#include <string>
namespace i2p::data
{
namespace
{
	std::mutex writerMutex;
	RouterPersistenceWriter installedWriter;

	// An explicitly installed (worker) backend. While it is empty the
	// direct data-directory backend is the effective writer, but only as
	// long as privsep.artifacts is disabled.
	std::mutex artifactMutex;
	ArtifactWriter installedArtifactWriter;
	ArtifactRemover installedArtifactRemover;

	// Per-kind error throttling: at most one line per minute for a kind so
	// a failing worker cannot turn a persistence pass into unbounded log
	// output.
	constexpr uint64_t ARTIFACT_LOG_INTERVAL_MS = 60 * 1000;
	std::array<std::atomic<uint64_t>, 3> artifactLogNotBefore { };

	uint64_t SteadyMilliseconds ()
	{
		return static_cast<uint64_t> (std::chrono::duration_cast<std::chrono::milliseconds> (
			std::chrono::steady_clock::now ().time_since_epoch ())
				.count ());
	}

	size_t ArtifactSlot (StorageArtifact kind)
	{
		switch (kind)
		{
		case StorageArtifact::RouterInfoCache: return 0;
		case StorageArtifact::PeerProfile: return 1;
		case StorageArtifact::DestinationKeys: return 2;
		}
		return 0;
	}

	bool IsKnownArtifact (StorageArtifact kind)
	{
		switch (kind)
		{
		case StorageArtifact::RouterInfoCache:
		case StorageArtifact::PeerProfile:
		case StorageArtifact::DestinationKeys: return true;
		}
		return false;
	}

	void LogArtifactError (StorageArtifact kind, std::string_view message, std::string_view name)
	{
		const auto slot = ArtifactSlot (kind);
		const auto now = SteadyMilliseconds ();
		auto notBefore = artifactLogNotBefore[slot].load (std::memory_order_relaxed);
		while (now >= notBefore)
		{
			if (artifactLogNotBefore[slot].compare_exchange_weak (
					notBefore, now + ARTIFACT_LOG_INTERVAL_MS, std::memory_order_relaxed))
			{
				const char * token = StorageArtifactName (kind);
				if (!token) token = "unknown";
				if (name.empty ())
					LogPrint (eLogError, "Storage: ", token, " ", message);
				else
					LogPrint (eLogError, "Storage: ", token, " ", message, " ", name);
				return;
			}
		}
	}

	bool IsCanonicalIdentityHashBase64 (std::string_view identBase64)
	{
		Tag<32> hash;
		if (hash.FromBase64 (identBase64) != Tag<32>::len) return false;
		return std::string_view (hash.ToBase64 ()) == identBase64;
	}

	bool IsCanonicalIdentityHashBase32 (std::string_view identBase32)
	{
		Tag<32> hash;
		if (hash.FromBase32 (identBase32) != Tag<32>::len) return false;
		return std::string_view (hash.ToBase32 ()) == identBase32;
	}

	// "&lt;52-character lowercase base32 identity&gt;.&lt;decimal
	// CryptoKeyType&gt;". ElGamal is type 0, so zero is a valid key type;
	// leading zeros are not canonical and are rejected.
	bool IsCanonicalDestinationKeyName (std::string_view name)
	{
		const auto separator = name.find ('.');
		if (separator == std::string_view::npos) return false;
		if (name.find ('.', separator + 1) != std::string_view::npos) return false;
		if (!IsCanonicalIdentityHashBase32 (name.substr (0, separator))) return false;
		const auto keyType = name.substr (separator + 1);
		if (keyType.empty () || keyType.size () > 5) return false;
		uint32_t value = 0;
		for (const char digit : keyType)
		{
			if (digit < '0' || digit > '9') return false;
			value = value * 10 + static_cast<uint32_t> (digit - '0');
		}
		if (value > 0xFFFF) return false;
		const auto canonical = std::to_string (value);
		return keyType == std::string_view (canonical);
	}

	// The direct backend resolves the same paths the storage classes used
	// before the worker existed: the shard is the leading alphabet
	// character of the canonical name.
	std::string DirectArtifactPath (StorageArtifact kind, std::string_view name)
	{
		const std::string encoded (name);
		switch (kind)
		{
		case StorageArtifact::RouterInfoCache:
			return i2p::fs::DataDirPath ("netDb", std::string ("r") + encoded[0], "routerInfo-" + encoded + ".dat");
		case StorageArtifact::PeerProfile:
			return i2p::fs::DataDirPath ("peerProfiles", std::string ("p") + encoded[0], "profile-" + encoded + ".txt");
		case StorageArtifact::DestinationKeys: return i2p::fs::DataDirPath ("destinations", encoded + ".dat");
		}
		return { };
	}

	bool DirectWriteArtifact (StorageArtifact kind, std::string_view name, std::span<const uint8_t> bytes)
	{
		const auto path = DirectArtifactPath (kind, name);
		const std::string_view contents (reinterpret_cast<const char *> (bytes.data ()), bytes.size ());
		// Destination keys are secret material: the direct fallback keeps the
		// private mode the worker uses and replaces the file atomically.
		if (kind == StorageArtifact::DestinationKeys) return i2p::fs::AtomicWritePrivate (path, contents);
		return i2p::fs::AtomicWrite (path, contents);
	}

	bool DirectRemoveArtifact (StorageArtifact kind, std::string_view name)
	{
		const auto path = DirectArtifactPath (kind, name);
		if (!i2p::fs::Exists (path)) return true;
		return i2p::fs::Remove (path);
	}
}
const char * RouterPersistentFileName (RouterPersistentFile file)
{
	switch (file)
	{
	case RouterPersistentFile::RouterKeys: return "router.keys";
	case RouterPersistentFile::RouterInfo: return "router.info";
	case RouterPersistentFile::NTCP2Keys: return "ntcp2.keys";
	case RouterPersistentFile::SSU2Keys: return "ssu2.keys";
	}
	return nullptr;
}
void InstallRouterPersistenceWriter (RouterPersistenceWriter writer)
{
	std::lock_guard lock (writerMutex);
	installedWriter = std::move (writer);
}
bool WriteRouterPersistentFile (RouterPersistentFile file, std::span<const uint8_t> bytes)
{
	const auto name = RouterPersistentFileName (file);
	if (!name || bytes.empty () || bytes.size () > 32768) return false;
	RouterPersistenceWriter writer;
	{
		std::lock_guard lock (writerMutex);
		writer = installedWriter;
	}
	if (writer) return writer (file, bytes);
	std::ofstream output (i2p::fs::CreatePrivateFile (i2p::fs::DataDirPath (name)), std::ios::binary | std::ios::out);
	output.write (reinterpret_cast<const char *> (bytes.data ()), bytes.size ());
	output.close ();
	return !output.fail ();
}
const char * StorageArtifactName (StorageArtifact kind)
{
	switch (kind)
	{
	case StorageArtifact::RouterInfoCache: return "routerInfo";
	case StorageArtifact::PeerProfile: return "peerProfile";
	case StorageArtifact::DestinationKeys: return "destinationKeys";
	}
	return nullptr;
}
bool IsValidArtifactName (StorageArtifact kind, std::string_view name)
{
	switch (kind)
	{
	case StorageArtifact::RouterInfoCache:
	case StorageArtifact::PeerProfile: return IsCanonicalIdentityHashBase64 (name);
	case StorageArtifact::DestinationKeys: return IsCanonicalDestinationKeyName (name);
	}
	return false;
}
bool AreArtifactWritesDelegated ()
{
	bool delegated = false;
	i2p::config::GetOption ("privsep.artifacts", delegated);
	return delegated;
}
void InstallArtifactStorage (ArtifactWriter writer, ArtifactRemover remover)
{
	std::lock_guard lock (artifactMutex);
	installedArtifactWriter = std::move (writer);
	installedArtifactRemover = std::move (remover);
}
void ClearArtifactStorage ()
{
	std::lock_guard lock (artifactMutex);
	installedArtifactWriter = nullptr;
	installedArtifactRemover = nullptr;
}
bool IsArtifactStorageInstalled ()
{
	std::lock_guard lock (artifactMutex);
	return static_cast<bool> (installedArtifactWriter);
}
bool WriteArtifact (StorageArtifact kind, std::string_view name, std::span<const uint8_t> bytes)
{
	if (!IsKnownArtifact (kind) || !IsValidArtifactName (kind, name) || bytes.empty ()
		|| bytes.size () > MAX_ARTIFACT_SIZE)
	{
		LogArtifactError (kind, "rejected an invalid artifact request", { });
		return false;
	}
	ArtifactWriter writer;
	{
		std::lock_guard lock (artifactMutex);
		writer = installedArtifactWriter;
	}
	if (writer)
	{
		// The installed writer is the only authority for a delegated
		// artifact. A failure is reported to the caller and never retried on
		// disk.
		if (writer (kind, name, bytes)) return true;
		LogArtifactError (kind, "writer failed for", name);
		return false;
	}
	if (!IsDirectArtifactFallbackAllowed (AreArtifactWritesDelegated ()))
	{
		// privsep.artifacts is enabled but the worker is unavailable: fail
		// closed instead of writing through a path the worker owns.
		LogArtifactError (kind, "has no installed writer; refusing a direct write for", name);
		return false;
	}
	if (DirectWriteArtifact (kind, name, bytes)) return true;
	LogArtifactError (kind, "direct write failed for", name);
	return false;
}
bool RemoveArtifact (StorageArtifact kind, std::string_view name)
{
	if (!IsKnownArtifact (kind) || !IsValidArtifactName (kind, name))
	{
		LogArtifactError (kind, "rejected an invalid artifact request", { });
		return false;
	}
	ArtifactWriter writer;
	ArtifactRemover remover;
	{
		std::lock_guard lock (artifactMutex);
		writer = installedArtifactWriter;
		remover = installedArtifactRemover;
	}
	if (remover)
	{
		if (remover (kind, name)) return true;
		LogArtifactError (kind, "remover failed for", name);
		return false;
	}
	if (writer)
	{
		// The writer owns the artifact but publishes no removal operation: an
		// installed writer must not be bypassed with a filesystem deletion.
		LogArtifactError (kind, "has no installed remover; refusing a direct removal for", name);
		return false;
	}
	if (!IsDirectArtifactFallbackAllowed (AreArtifactWritesDelegated ()))
	{
		LogArtifactError (kind, "has no installed remover; refusing a direct removal for", name);
		return false;
	}
	if (DirectRemoveArtifact (kind, name)) return true;
	LogArtifactError (kind, "direct removal failed for", name);
	return false;
}
}
