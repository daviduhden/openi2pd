/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: artifact storage dispatch.
 *
 * Checks the delegated-artifact contract: an installed writer receives
 * the artifact kind, canonical name and exact bytes; a writer failure
 * is returned to the caller and never retried through the filesystem;
 * the direct data-directory backend is used only while
 * privsep.artifacts is disabled; a delegated but uninstalled backend
 * fails closed; and destination key material keeps the exact pub||priv
 * layout the load path expects. Uses a temporary directory only; no
 * sockets, threads or sleeps.
 *
 */

#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Config.h"
#include "CryptoKey.h"
#include "FS.h"
#include "Identity.h"
#include "PersistentStorage.h"

namespace
{
namespace fs = std::filesystem;
using namespace i2p::data;

std::string ReadFile (const fs::path & path)
{
	std::ifstream input (path, std::ifstream::binary);
	std::string contents;
	contents.assign (std::istreambuf_iterator<char> (input), std::istreambuf_iterator<char> ());
	return contents;
}

std::string AsString (const std::vector<uint8_t> & bytes)
{
	return std::string (reinterpret_cast<const char *> (bytes.data ()), bytes.size ());
}

fs::path MakeRoot (const char * tag)
{
	const auto root = fs::temp_directory_path ()
		/ (std::string ("openi2pd-artifact-") + tag + "-"
			+ std::to_string (std::chrono::steady_clock::now ().time_since_epoch ().count ()));
	fs::create_directories (root);
	return root;
}

// The option registry is global and defaults are applied by Finalize(),
// so a second Init()/ParseConfig()/Finalize() round selects a new
// value.
void InitialiseConfig (const fs::path & root, bool artifactsDelegated)
{
	i2p::config::Init ();
	const auto path = root / (artifactsDelegated ? "i2pd-artifacts-on.conf" : "i2pd-artifacts-off.conf");
	{
		std::ofstream file (path, std::ios::trunc);
		assert (file);
		file << "privsep.artifacts = " << (artifactsDelegated ? "true" : "false") << "\n";
		file.close ();
		assert (file);
	}
	i2p::config::ParseConfig (path.string ());
	i2p::config::Finalize ();
	bool value = !artifactsDelegated;
	assert (i2p::config::GetOption ("privsep.artifacts", value));
	assert (value == artifactsDelegated);
	assert (AreArtifactWritesDelegated () == artifactsDelegated);
}

IdentHash MakeIdent (uint8_t fill)
{
	IdentHash ident;
	ident.Fill (fill);
	return ident;
}

fs::path DirectPath (const fs::path & root, StorageArtifact kind, const std::string & name)
{
	switch (kind)
	{
	case StorageArtifact::RouterInfoCache:
		return root / "netDb" / (std::string ("r") + name[0]) / ("routerInfo-" + name + ".dat");
	case StorageArtifact::PeerProfile:
		return root / "peerProfiles" / (std::string ("p") + name[0]) / ("profile-" + name + ".txt");
	case StorageArtifact::DestinationKeys: return root / "destinations" / (name + ".dat");
	}
	return { };
}

// The direct backend never creates shard directories; the daemon's
// storage
// classes do, so the fixture prepares them.
void PrepareDirectRoot (const fs::path & root, StorageArtifact kind, const std::string & name)
{
	fs::create_directories (DirectPath (root, kind, name).parent_path ());
}

struct RecordedWrite
{
	bool called = false;
	StorageArtifact kind = StorageArtifact::RouterInfoCache;
	std::string name;
	std::vector<uint8_t> bytes;
};

struct RecordedRemove
{
	bool called = false;
	StorageArtifact kind = StorageArtifact::RouterInfoCache;
	std::string name;
};

ArtifactWriter RecordingWriter (RecordedWrite & record, bool result)
{
	return [&record, result] (StorageArtifact kind, std::string_view name, std::span<const uint8_t> bytes)
	{
		record.called = true;
		record.kind = kind;
		record.name.assign (name);
		record.bytes.assign (bytes.begin (), bytes.end ());
		return result;
	};
}

ArtifactRemover RecordingRemover (RecordedRemove & record, bool result)
{
	return [&record, result] (StorageArtifact kind, std::string_view name)
	{
		record.called = true;
		record.kind = kind;
		record.name.assign (name);
		return result;
	};
}

// Compile-free statement of the permanent capability rule.
void TestCapabilityInvariant ()
{
	constexpr auto network = CapabilityBit (ProcessCapability::Network);
	constexpr auto artifactWrite = CapabilityBit (ProcessCapability::ArtifactWrite);
	static_assert (network != artifactWrite);
	static_assert (IsCapabilitySetCompatible (0));
	static_assert (IsCapabilitySetCompatible (network));
	static_assert (IsCapabilitySetCompatible (artifactWrite));
	static_assert (!IsCapabilitySetCompatible (network | artifactWrite));
	// The artifact writer is the only write path while the option is
	// enabled.
	static_assert (IsDirectArtifactFallbackAllowed (false));
	static_assert (!IsDirectArtifactFallbackAllowed (true));
	assert (!IsCapabilitySetCompatible (network | artifactWrite));
}

void TestNameFormats ()
{
	const auto ident = MakeIdent (0x5A);
	const auto base64 = ident.ToBase64 ();
	const auto base32 = ident.ToBase32 ();
	assert (base64.size () == 44);
	assert (base32.size () == 52);

	assert (IsValidArtifactName (StorageArtifact::RouterInfoCache, base64));
	assert (IsValidArtifactName (StorageArtifact::PeerProfile, base64));
	assert (!IsValidArtifactName (StorageArtifact::RouterInfoCache, base64.substr (0, 43)));
	assert (!IsValidArtifactName (StorageArtifact::RouterInfoCache, base64 + "="));
	assert (!IsValidArtifactName (StorageArtifact::RouterInfoCache, "../" + base64));
	assert (!IsValidArtifactName (StorageArtifact::RouterInfoCache, ""));
	assert (!IsValidArtifactName (StorageArtifact::PeerProfile, base32 + ".4"));
	assert (!IsValidArtifactName (StorageArtifact::RouterInfoCache, base32 + ".4"));

	assert (IsValidArtifactName (StorageArtifact::DestinationKeys, base32 + ".0"));
	assert (IsValidArtifactName (StorageArtifact::DestinationKeys, base32 + ".4"));
	assert (IsValidArtifactName (StorageArtifact::DestinationKeys, base32 + ".65535"));
	assert (!IsValidArtifactName (StorageArtifact::DestinationKeys, base32));
	assert (!IsValidArtifactName (StorageArtifact::DestinationKeys, base32 + "."));
	assert (!IsValidArtifactName (StorageArtifact::DestinationKeys, base32 + ".04"));
	assert (!IsValidArtifactName (StorageArtifact::DestinationKeys, base32 + ".65536"));
	assert (!IsValidArtifactName (StorageArtifact::DestinationKeys, base32 + ".4/../0"));
	assert (!IsValidArtifactName (StorageArtifact::DestinationKeys, base64 + ".4"));

	const std::string_view router = StorageArtifactName (StorageArtifact::RouterInfoCache);
	const std::string_view profile = StorageArtifactName (StorageArtifact::PeerProfile);
	const std::string_view destinationKeys = StorageArtifactName (StorageArtifact::DestinationKeys);
	assert (!router.empty () && !profile.empty () && !destinationKeys.empty ());
	assert (router != profile && profile != destinationKeys && router != destinationKeys);
}

void TestWriterReceivesArtifact ()
{
	const auto root = MakeRoot ("writer");
	const auto name = MakeIdent (0x31).ToBase64 ();
	InitialiseConfig (root, false);

	// A direct file exists before the delegation is installed, so it
	// proves that a delegated write never touches the data directory.
	PrepareDirectRoot (root, StorageArtifact::PeerProfile, name);
	const auto path = DirectPath (root, StorageArtifact::PeerProfile, name);
	std::ofstream (path) << "previous profile";

	const std::vector<uint8_t> bytes { 'a', 'r', 't', 'i', 'f', 'a', 'c', 't', 0, 1, 2, 3 };
	RecordedWrite write;
	RecordedRemove remove;
	InstallArtifactStorage (RecordingWriter (write, true), RecordingRemover (remove, true));
	assert (IsArtifactStorageInstalled ());

	assert (WriteArtifact (StorageArtifact::PeerProfile, name, bytes));
	assert (write.called);
	assert (write.kind == StorageArtifact::PeerProfile);
	assert (write.name == name);
	assert (write.bytes == bytes);
	assert (ReadFile (path) == "previous profile");

	assert (RemoveArtifact (StorageArtifact::PeerProfile, name));
	assert (remove.called);
	assert (remove.kind == StorageArtifact::PeerProfile);
	assert (remove.name == name);
	assert (fs::exists (path));

	// The same writer serves every artifact kind.
	write = RecordedWrite ();
	assert (WriteArtifact (StorageArtifact::RouterInfoCache, name, bytes));
	assert (write.called && write.kind == StorageArtifact::RouterInfoCache);

	ClearArtifactStorage ();
	assert (!IsArtifactStorageInstalled ());
	fs::remove_all (root);
}

void TestNoFallbackOnWriterFailure ()
{
	const auto root = MakeRoot ("nofallback");
	const auto ident = MakeIdent (0x32);
	const auto name = ident.ToBase64 ();
	const auto keyName = ident.ToBase32 () + ".4";
	InitialiseConfig (root, false);
	PrepareDirectRoot (root, StorageArtifact::RouterInfoCache, name);
	PrepareDirectRoot (root, StorageArtifact::DestinationKeys, keyName);
	const auto directPath = DirectPath (root, StorageArtifact::RouterInfoCache, name);

	// An installed writer that fails is never retried through the
	// filesystem.
	RecordedWrite write;
	RecordedRemove remove;
	InstallArtifactStorage (RecordingWriter (write, false), RecordingRemover (remove, false));
	{
		const std::vector<uint8_t> bytes { 1, 2, 3 };
		assert (!WriteArtifact (StorageArtifact::RouterInfoCache, name, bytes));
		assert (write.called);
		assert (!fs::exists (directPath));
		assert (fs::is_empty (directPath.parent_path ()));
		assert (!RemoveArtifact (StorageArtifact::DestinationKeys, keyName));
		assert (remove.called);
	}

	// Delegated without an installed writer fails closed and writes
	// nothing.
	ClearArtifactStorage ();
	InitialiseConfig (root, true);
	assert (!IsArtifactStorageInstalled ());
	{
		const std::vector<uint8_t> bytes { 4, 5, 6 };
		assert (!WriteArtifact (StorageArtifact::RouterInfoCache, name, bytes));
		assert (!fs::exists (directPath));
		assert (fs::is_empty (directPath.parent_path ()));
		assert (!RemoveArtifact (StorageArtifact::DestinationKeys, keyName));
	}

	// Invalid names and sizes never reach a writer and never touch a path.
	RecordedWrite invalid;
	InstallArtifactStorage (RecordingWriter (invalid, true), RecordingRemover (remove, true));
	{
		const std::vector<uint8_t> bytes { 7 };
		assert (!WriteArtifact (StorageArtifact::RouterInfoCache, "../" + name, bytes));
		assert (!invalid.called);
		assert (!WriteArtifact (StorageArtifact::RouterInfoCache, name, std::span<const uint8_t> ()));
		assert (!invalid.called);
		const std::vector<uint8_t> oversize (MAX_ARTIFACT_SIZE + 1, 0x41);
		assert (!WriteArtifact (StorageArtifact::PeerProfile, name, oversize));
		assert (!invalid.called);
		assert (!WriteArtifact (StorageArtifact::DestinationKeys, name, bytes));
		assert (!invalid.called);
	}

	ClearArtifactStorage ();
	InitialiseConfig (root, false);
	fs::remove_all (root);
}

void TestDirectFallback ()
{
	const auto root = MakeRoot ("direct");
	const auto ident = MakeIdent (0x33);
	const auto name = ident.ToBase64 ();
	const auto keyName = ident.ToBase32 () + ".4";
	InitialiseConfig (root, false);
	ClearArtifactStorage ();
	assert (!IsArtifactStorageInstalled ());
	assert (!AreArtifactWritesDelegated ());

	// RouterInfo cache: the unchanged cache path and exact bytes.
	PrepareDirectRoot (root, StorageArtifact::RouterInfoCache, name);
	const std::vector<uint8_t> cache { 0x00, 0x11, 0x22, 0x33 };
	assert (WriteArtifact (StorageArtifact::RouterInfoCache, name, cache));
	const auto cachePath = DirectPath (root, StorageArtifact::RouterInfoCache, name);
	assert (ReadFile (cachePath) == AsString (cache));
	// A replacement keeps a single file with the new contents.
	const std::vector<uint8_t> replaced { 0x44, 0x55 };
	assert (WriteArtifact (StorageArtifact::RouterInfoCache, name, replaced));
	assert (ReadFile (cachePath) == AsString (replaced));
	assert (RemoveArtifact (StorageArtifact::RouterInfoCache, name));
	assert (!fs::exists (cachePath));
	assert (RemoveArtifact (StorageArtifact::RouterInfoCache, name)); // absent is success

	// Peer profile: INI bytes under profile-<ident>.txt.
	PrepareDirectRoot (root, StorageArtifact::PeerProfile, name);
	const std::string ini = "[participation]\nagreed=3\n";
	const std::vector<uint8_t> profile (ini.begin (), ini.end ());
	const auto profilePath = DirectPath (root, StorageArtifact::PeerProfile, name);
	assert (WriteArtifact (StorageArtifact::PeerProfile, name, profile));
	assert (ReadFile (profilePath) == ini);
	assert (profilePath.filename ().string ().starts_with ("profile-"));
	assert (profilePath.filename ().string ().ends_with (".txt"));
	assert (RemoveArtifact (StorageArtifact::PeerProfile, name));
	assert (!fs::exists (profilePath));

	// Destination keys keep the private file mode on POSIX.
	PrepareDirectRoot (root, StorageArtifact::DestinationKeys, keyName);
	const std::vector<uint8_t> keys (512, 0x5A);
	assert (WriteArtifact (StorageArtifact::DestinationKeys, keyName, keys));
	const auto keyPath = DirectPath (root, StorageArtifact::DestinationKeys, keyName);
	assert (ReadFile (keyPath) == AsString (keys));
#ifndef _WIN32
	const auto permissions = fs::status (keyPath).permissions ();
	assert ((permissions
				& (fs::perms::group_read | fs::perms::group_write | fs::perms::others_read | fs::perms::others_write))
		== fs::perms::none);
#endif
	assert (RemoveArtifact (StorageArtifact::DestinationKeys, keyName));
	assert (!fs::exists (keyPath));

	// No staging leftovers: the atomic writers replace in place.
	size_t leaves = 0;
	for (const auto & entry : fs::recursive_directory_iterator (root))
		if (entry.is_regular_file () && entry.path ().extension () != ".conf") leaves++;
	assert (leaves == 0);
	fs::remove_all (root);
}

// Destination key persistence: "<b32 identity>.<decimal keyType>"
// carrying exactly pub||priv, which is what the unchanged loader in
// Destination.cpp reads back.
void TestDestinationKeyArtifact ()
{
	const auto root = MakeRoot ("destkeys");
	InitialiseConfig (root, false);
	ClearArtifactStorage ();
	i2p::fs::DetectDataDir (root.string (), false);
	fs::create_directories (root / "destinations");

	const auto ident = MakeIdent (0x34);
	const auto base32 = ident.ToBase32 ();
	assert (base32.size () == 52);
	i2p::crypto::LocalEncryptionKey keys (CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
	// A deterministic pair keeps the test independent of the crypto
	// provider:
	// only the storage layout is under test. The size must not be 512,
	// because
	// that length selects the ElGamal pub||priv branch in the loader.
	assert (keys.pub.size () != 512 && keys.pub.size () == keys.priv.size ());
	for (size_t i = 0; i < keys.pub.size (); i++) keys.pub[i] = static_cast<uint8_t> (0x40 + i);
	for (size_t i = 0; i < keys.priv.size (); i++) keys.priv[i] = static_cast<uint8_t> (0x80 + i);

	const auto name = base32 + "." + std::to_string (keys.keyType);
	assert (IsValidArtifactName (StorageArtifact::DestinationKeys, name));
	std::vector<uint8_t> blob = keys.pub;
	blob.insert (blob.end (), keys.priv.begin (), keys.priv.end ());
	assert (WriteArtifact (StorageArtifact::DestinationKeys, name, blob));

	const auto path = DirectPath (root, StorageArtifact::DestinationKeys, name);
	assert (ReadFile (path) == AsString (blob));
#ifndef _WIN32
	// Secret material is written with the private mode the worker uses.
	const auto permissions = fs::status (path).permissions ();
	assert ((permissions
				& (fs::perms::group_read | fs::perms::group_write | fs::perms::others_read | fs::perms::others_write))
		== fs::perms::none);
#endif

	// The loader reads pub and priv at their exact sizes from the same
	// file.
	{
		std::ifstream input (path, std::ifstream::binary);
		std::vector<uint8_t> pub (keys.pub.size ()), priv (keys.priv.size ());
		input.read (reinterpret_cast<char *> (pub.data ()), static_cast<std::streamsize> (pub.size ()));
		input.read (reinterpret_cast<char *> (priv.data ()), static_cast<std::streamsize> (priv.size ()));
		assert (input);
		assert (pub == keys.pub);
		assert (priv == keys.priv);
	}

	// A delegated write sends the same name and bytes to the writer.
	RecordedWrite write;
	RecordedRemove remove;
	InstallArtifactStorage (RecordingWriter (write, true), RecordingRemover (remove, true));
	assert (WriteArtifact (StorageArtifact::DestinationKeys, name, blob));
	assert (write.called);
	assert (write.kind == StorageArtifact::DestinationKeys);
	assert (write.name == name);
	assert (write.bytes == blob);
	ClearArtifactStorage ();

	fs::remove_all (root);
}
}

int main ()
{
	TestCapabilityInvariant ();
	TestNameFormats ();
	TestWriterReceivesArtifact ();
	TestNoFallbackOnWriterFailure ();
	TestDirectFallback ();
	TestDestinationKeyArtifact ();
	return 0;
}
