/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: netDb RouterInfo persistence.
 *
 * Checks the cache-file naming policy (exact identity-hash binding, staged
 * leftovers skipped), the atomic no-follow replacement of a cached RouterInfo,
 * and the bounded final persistence pass in NetDb::Stop(). Uses a temporary
 * directory only; no sockets, threads or sleeps.
 *
 */

#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <list>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Base.h"
#include "FS.h"
#include "I2PEndian.h"
#include "Identity.h"
#include "NetDb.hpp"
#include "PersistentStorage.h"
#include "RouterContext.h"
#include "RouterInfo.h"
#include "Timestamp.h"

namespace i2p::data
{
// The friend declared by NetDb; only deterministic storage/admission helpers.
struct NetDbLifecycleTestAccess
{
	static void InitStorage (NetDb & database, const std::string & root)
	{
		std::lock_guard lock (database.m_StorageMutex);
		database.m_Storage.SetPlace (root);
		assert (database.m_Storage.Init (i2p::data::GetBase64SubstitutionTable (), 64));
	}
	static void Insert (NetDb & database, const std::shared_ptr<RouterInfo> & router)
	{
		std::lock_guard lock (database.m_RouterInfosMutex);
		database.m_RouterInfos.emplace (router->GetIdentHash (), router);
	}
	static std::string Path (NetDb & database, const std::string & ident)
	{
		std::lock_guard lock (database.m_StorageMutex);
		return database.m_Storage.Path (ident);
	}
	static void Persist (NetDb & database,
		std::list<std::pair<std::string, std::shared_ptr<RouterInfo::Buffer>>> && update,
		std::list<std::string> && remove)
	{
		database.PersistRouters (std::move (update), std::move (remove));
	}
};
}

namespace
{
using namespace i2p;
using namespace i2p::data;

void AppendBe16 (std::vector<uint8_t> & out, uint16_t value)
{
	uint8_t encoded[2];
	htobe16buf (encoded, value);
	out.insert (out.end (), encoded, encoded + sizeof (encoded));
}

void AppendBe64 (std::vector<uint8_t> & out, uint64_t value)
{
	uint8_t encoded[8];
	htobe64buf (encoded, value);
	out.insert (out.end (), encoded, encoded + sizeof (encoded));
}

void AppendParam (std::string & out, std::string_view key, std::string_view value)
{
	assert (key.size () <= 255 && value.size () <= 255);
	out.push_back (static_cast<char> (key.size ()));
	out.append (key);
	out.push_back ('=');
	out.push_back (static_cast<char> (value.size ()));
	out.append (value);
	out.push_back (';');
}

std::vector<uint8_t> BuildRouterInfo (const PrivateKeys & keys, uint64_t published)
{
	std::vector<uint8_t> body;
	AppendBe64 (body, published);
	body.push_back (0); // zero addresses
	body.push_back (0); // zero peer hashes

	std::string properties;
	AppendParam (properties, "caps", "O");
	AppendParam (properties, "netId", std::to_string (i2p::context.GetNetID ()));
	AppendParam (properties, "router.version", "2.14.0");
	AppendBe16 (body, static_cast<uint16_t> (properties.size ()));
	body.insert (body.end (), properties.begin (), properties.end ());

	std::vector<uint8_t> buffer;
	auto identity = keys.GetPublic ();
	assert (identity);
	const auto identityOffset = buffer.size ();
	buffer.resize (identityOffset + identity->GetFullLen ());
	assert (identity->ToBuffer (buffer.data () + identityOffset, identity->GetFullLen ()) == identity->GetFullLen ());
	buffer.insert (buffer.end (), body.begin (), body.end ());

	std::vector<uint8_t> signature (identity->GetSignatureLen ());
	keys.Sign (buffer.data (), static_cast<int> (buffer.size ()), signature.data ());
	buffer.insert (buffer.end (), signature.begin (), signature.end ());
	return buffer;
}

std::string ReadFile (const std::filesystem::path & path)
{
	std::ifstream input (path, std::ifstream::binary);
	std::string contents;
	contents.assign (std::istreambuf_iterator<char> (input), std::istreambuf_iterator<char> ());
	return contents;
}

std::filesystem::path MakeTempDir (const char * tag)
{
	const auto root = std::filesystem::temp_directory_path ()
		/ (std::string ("openi2pd-netdb-") + tag + "-"
			+ std::to_string (std::chrono::steady_clock::now ().time_since_epoch ().count ()));
	std::filesystem::create_directories (root);
	return root;
}

void TestFileNameBinding ()
{
	std::string ident;
	const char * hash = "AbCdEfGhIjKlMnOpQrStUvWxYz0123456789-~abcd=";

	assert (ClassifyRouterInfoFile (std::string ("/data/netDb/r/") + hash + ".dat", ident) == RouterInfoFileKind::Load);
	assert (ident == hash);

	// Both the retired ".tmp" name and AtomicWrite's unique staging name are
	// leftovers of this storage and may be removed.
	ident.clear ();
	assert (ClassifyRouterInfoFile (std::string ("/data/netDb/r/") + hash + ".dat.tmp", ident)
		== RouterInfoFileKind::Staging);
	assert (ident.empty ());
	assert (ClassifyRouterInfoFile (std::string ("/data/netDb/r/") + hash + ".dat.tmp-4711-3", ident)
		== RouterInfoFileKind::Staging);
	assert (ident.empty ());

	// Windows-style separators are classified the same way.
	assert (ClassifyRouterInfoFile (std::string ("C:\\data\\netDb\\r\\") + hash + ".dat", ident)
		== RouterInfoFileKind::Load);
	assert (ident == hash);

	// Unknown names are never loaded and never deleted by the loader.
	assert (ClassifyRouterInfoFile ("/data/netDb/r/other.dat", ident) == RouterInfoFileKind::Foreign);
	assert (ClassifyRouterInfoFile ("/data/netDb/r/routerInfo-.dat", ident) == RouterInfoFileKind::Foreign);
	assert (ClassifyRouterInfoFile ("/data/netDb/r/routerInfo-.dat.tmp-1-1", ident) == RouterInfoFileKind::Staging);
	assert (ClassifyRouterInfoFile ("/data/netDb/r/routerInfo", ident) == RouterInfoFileKind::Foreign);
	assert (ClassifyRouterInfoFile ("/data/netDb/r/", ident) == RouterInfoFileKind::Foreign);
}

void TestAtomicSaveReplacesWithoutLeftovers ()
{
	const auto root = MakeTempDir ("save");
	const auto target = root / "routerInfo-test.dat";
	const auto staleStage = root / "routerInfo-test.dat.tmp";
	std::ofstream (staleStage, std::ofstream::binary) << "stale";

	std::array<uint8_t, MAX_RI_BUFFER_SIZE> first { };
	for (size_t i = 0; i < first.size (); ++i) first[i] = static_cast<uint8_t> (i);
	auto buffer = std::make_shared<RouterInfo::Buffer> (first.data (), first.size ());
	assert (buffer);
	assert (RouterInfo::SaveToFile (target.string (), buffer));
	assert (ReadFile (target) == std::string (reinterpret_cast<const char *> (first.data ()), first.size ()));

	// A second replacement is atomic and leaves no unique staging file behind.
	auto second = std::make_shared<RouterInfo::Buffer> (first.data (), 512);
	assert (second);
	assert (RouterInfo::SaveToFile (target.string (), second));
	assert (ReadFile (target) == std::string (reinterpret_cast<const char *> (first.data ()), 512));

	// The predictable legacy name is not reused as the staging file: it is
	// neither consumed nor modified.
	assert (ReadFile (staleStage) == "stale");

	for (const auto & entry : std::filesystem::directory_iterator (root))
	{
		const auto name = entry.path ().filename ().string ();
		assert (name == "routerInfo-test.dat" || name == "routerInfo-test.dat.tmp");
	}
	std::filesystem::remove_all (root);
}

void TestStopFlushesUpdatedRouterInfo ()
{
	auto keys
		= PrivateKeys::CreateRandomKeys (SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
	auto ident = keys.GetPublic ()->GetIdentHash ();
	const auto now = i2p::util::GetMillisecondsSinceEpoch ();

	const auto root = MakeTempDir ("stop-flush");
	i2p::fs::DetectDataDir (root.string (), false);
	NetDb database;
	i2p::data::NetDbLifecycleTestAccess::InitStorage (database, root.string ());
	const auto expectedPath = i2p::data::NetDbLifecycleTestAccess::Path (database, ident.ToBase64 ());

	auto buffer = BuildRouterInfo (keys, now - 60 * 1000);
	auto router
		= std::make_shared<RouterInfo> (netdb.NewRouterInfoBuffer (buffer.data (), buffer.size ()), buffer.size ());
	assert (!router->IsUnreachable ());
	assert (router->GetIdentHash () == ident);
	assert (router->IsUpdated ()); // a freshly parsed descriptor is pending
	i2p::data::NetDbLifecycleTestAccess::Insert (database, router);

	// NetDb was never started: Stop() must still flush the accepted descriptor
	// before the maps are cleared, so at most the pending interval is lost.
	database.Stop ();
	assert (std::filesystem::exists (expectedPath));
	assert (ReadFile (expectedPath) == std::string (reinterpret_cast<const char *> (buffer.data ()), buffer.size ()));
	assert (database.GetNumRouters () == 0);

	// The file name is bound to the identity hash and the loader accepts it.
	std::string fileIdent;
	assert (ClassifyRouterInfoFile (expectedPath, fileIdent) == RouterInfoFileKind::Load);
	assert (fileIdent == ident.ToBase64 ());

	// A second Stop() has nothing left to persist and must not recreate files.
	database.Stop ();
	assert (database.GetNumRouters () == 0);
	std::filesystem::remove_all (root);
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

// A cached RouterInfo is persisted and removed through the installed artifact
// writer; no direct cache file appears, and the direct backend keeps the
// previous on-disk name and bytes when nothing is installed.
void TestArtifactRoutedPersistence ()
{
	auto keys
		= PrivateKeys::CreateRandomKeys (SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
	auto ident = keys.GetPublic ()->GetIdentHash ();
	const auto name = ident.ToBase64 ();
	const auto now = i2p::util::GetMillisecondsSinceEpoch ();
	auto buffer = BuildRouterInfo (keys, now - 60 * 1000);

	const auto root = MakeTempDir ("artifact");
	i2p::fs::DetectDataDir (root.string (), false);
	NetDb database;
	i2p::data::NetDbLifecycleTestAccess::InitStorage (database, root.string ());
	const auto cachePath = i2p::data::NetDbLifecycleTestAccess::Path (database, name);

	IdentHash removedIdent;
	removedIdent.Fill (0x17);
	const auto removedName = removedIdent.ToBase64 ();

	RecordedWrite write;
	RecordedRemove remove;
	InstallArtifactStorage (
		[&write] (StorageArtifact kind, std::string_view artifactName, std::span<const uint8_t> bytes)
		{
			write.called = true;
			write.kind = kind;
			write.name.assign (artifactName);
			write.bytes.assign (bytes.begin (), bytes.end ());
			return true;
		},
		[&remove] (StorageArtifact kind, std::string_view artifactName)
		{
			remove.called = true;
			remove.kind = kind;
			remove.name.assign (artifactName);
			return true;
		});
	assert (IsArtifactStorageInstalled ());

	std::list<std::pair<std::string, std::shared_ptr<RouterInfo::Buffer>>> update;
	update.emplace_back (name, std::make_shared<RouterInfo::Buffer> (buffer.data (), buffer.size ()));
	std::list<std::string> removals { removedName };
	i2p::data::NetDbLifecycleTestAccess::Persist (database, std::move (update), std::move (removals));

	assert (write.called);
	assert (write.kind == StorageArtifact::RouterInfoCache);
	assert (write.name == name);
	assert (write.bytes == buffer);
	assert (!std::filesystem::exists (cachePath));
	assert (remove.called);
	assert (remove.kind == StorageArtifact::RouterInfoCache);
	assert (remove.name == removedName);

	ClearArtifactStorage ();
	assert (!IsArtifactStorageInstalled ());

	// The direct backend preserves the cache name, the identity binding and
	// the exact descriptor bytes of the previous writer.
	auto directBuffer = std::make_shared<RouterInfo::Buffer> (buffer.data (), buffer.size ());
	assert (RouterInfo::SaveToArtifact (name, directBuffer));
	assert (std::filesystem::exists (cachePath));
	assert (ReadFile (cachePath) == std::string (reinterpret_cast<const char *> (buffer.data ()), buffer.size ()));
	std::string fileIdent;
	assert (ClassifyRouterInfoFile (cachePath, fileIdent) == RouterInfoFileKind::Load);
	assert (fileIdent == name);
	assert (RemoveArtifact (StorageArtifact::RouterInfoCache, removedName));
	assert (RemoveArtifact (StorageArtifact::RouterInfoCache, name));
	assert (!std::filesystem::exists (cachePath));

	database.Stop ();
	std::filesystem::remove_all (root);
}
}

int main ()
{
	TestFileNameBinding ();
	TestAtomicSaveReplacesWithoutLeftovers ();
	TestStopFlushesUpdatedRouterInfo ();
	TestArtifactRoutedPersistence ();
	return 0;
}
