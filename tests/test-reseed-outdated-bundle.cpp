/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: reseed outdated-bundle regression.
 *
 * A downloaded bundle whose RouterInfos are outdated must be skipped
 * and counted, and must never delete the cached peer set.
 * Deterministic: no socket, no thread, no sleep; the clock and the
 * target netDb are injected.
 *
 */

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Crypto.h"
#include "I2PEndian.h"
#include "Identity.h"
#include "NetDb.hpp"
#include "Reseed.h"
#include "RouterContext.h"
#include "RouterInfo.h"
#include "util.h"

namespace i2p::data
{
// The friend declared by NetDb. The fixture installs already-built
// descriptors
// so a cached but outdated peer can exist at all, and clears them again
// before
// the destructor persistence pass.
struct NetDbLifecycleTestAccess
{
	static void InsertRouterInfo (NetDb & database, const std::shared_ptr<RouterInfo> & router)
	{
		std::lock_guard<std::mutex> lock (database.m_RouterInfosMutex);
		database.m_RouterInfos.emplace (router->GetIdentHash (), router);
	}
	static void ClearRouterInfos (NetDb & database)
	{
		std::lock_guard<std::mutex> lock (database.m_RouterInfosMutex);
		database.m_RouterInfos.clear ();
	}
};
}

namespace
{
using namespace i2p;
using namespace i2p::data;

void AppendLe16 (std::vector<uint8_t> & out, uint16_t value)
{
	out.push_back (static_cast<uint8_t> (value));
	out.push_back (static_cast<uint8_t> (value >> 8));
}

void AppendLe32 (std::vector<uint8_t> & out, uint32_t value)
{
	for (int shift = 0; shift < 32; shift += 8) out.push_back (static_cast<uint8_t> (value >> shift));
}

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

// Minimal signed RouterInfo: no addresses (storable inside the
// 54-minute grace), a valid netId and router.version. Only the
// published timestamp varies between cases.
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

// A stored ZIP archive; the reseed reader only requires the local file
// headers
// and stops at the central directory.
std::vector<uint8_t> BuildZip (const std::vector<std::vector<uint8_t>> & entries)
{
	std::vector<uint8_t> out;
	for (const auto & entry : entries)
	{
		AppendLe32 (out, 0x04034B50); // local file header signature
		AppendLe16 (out, 20); // version needed
		AppendLe16 (out, 0); // flags
		AppendLe16 (out, 0); // compression: stored
		AppendLe16 (out, 0); // modification time
		AppendLe16 (out, 0); // modification date
		AppendLe32 (out, 0); // CRC32 (not checked for stored entries)
		AppendLe32 (out, static_cast<uint32_t> (entry.size ())); // compressed size
		AppendLe32 (out, static_cast<uint32_t> (entry.size ())); // uncompressed size
		AppendLe16 (out, 0); // file name length
		AppendLe16 (out, 0); // extra field length
		out.insert (out.end (), entry.begin (), entry.end ());
	}
	AppendLe32 (out, 0x02014B50); // central directory header signature
	return out;
}

std::string WriteFile (
	const std::filesystem::path & directory, const std::string & name, const std::vector<uint8_t> & bytes)
{
	const auto path = directory / name;
	std::ofstream file (path, std::ios::binary | std::ios::trunc);
	assert (file);
	if (!bytes.empty ())
		file.write (reinterpret_cast<const char *> (bytes.data ()), static_cast<std::streamsize> (bytes.size ()));
	file.close ();
	assert (file);
	return path.string ();
}

PrivateKeys CreateKeys ()
{
	return PrivateKeys::CreateRandomKeys (SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, CRYPTO_KEY_TYPE_ECIES_X25519_AEAD);
}
}

int main ()
{
	using namespace i2p;
	using namespace i2p::data;

	const auto directory = std::filesystem::temp_directory_path () / "openi2pd-reseed-outdated-bundle";
	std::error_code error;
	std::filesystem::remove_all (directory, error);
	std::filesystem::create_directories (directory, error);
	assert (!error);

	const uint64_t now = i2p::util::GetMillisecondsSinceEpoch ();
	const uint64_t stalePublished
		= now - (RESEED_STALE_ROUTER_TIMEOUT + 24ULL * 60 * 60 * 1000); // one day past the window

	// Two cached peers that are already far beyond the freshness window.
	// They
	// are installed directly because the netDb store policy would reject
	// them;
	// the regression is that a later bundle must not delete them.
	auto cachedKeysA = CreateKeys ();
	auto cachedKeysB = CreateKeys ();
	auto cachedA = BuildRouterInfo (cachedKeysA, stalePublished);
	auto cachedB = BuildRouterInfo (cachedKeysB, stalePublished);
	auto cachedRouterA = std::make_shared<RouterInfo> (cachedA.data (), cachedA.size ());
	auto cachedRouterB = std::make_shared<RouterInfo> (cachedB.data (), cachedB.size ());
	assert (cachedRouterA->GetIdentHash () == cachedKeysA.GetPublic ()->GetIdentHash ());
	assert (cachedRouterB->GetIdentHash () == cachedKeysB.GetPublic ()->GetIdentHash ());

	NetDb database;
	using Access = i2p::data::NetDbLifecycleTestAccess;
	Access::InsertRouterInfo (database, cachedRouterA);
	Access::InsertRouterInfo (database, cachedRouterB);
	assert (database.GetNumRouters () == 2);

	// The bundle carries two outdated entries and one fresh entry.
	auto staleKeysA = CreateKeys ();
	auto staleKeysB = CreateKeys ();
	auto freshKeys = CreateKeys ();
	const auto staleA = BuildRouterInfo (staleKeysA, stalePublished);
	const auto staleB = BuildRouterInfo (staleKeysB, stalePublished);
	const auto fresh = BuildRouterInfo (freshKeys, now);

	Reseeder reseeder;
	reseeder.SetNetDbForTesting (&database);
	reseeder.SetClockForTesting ([now] { return now; });

	const auto mixedPath = WriteFile (directory, "mixed.zip", BuildZip ({ staleA, staleB, fresh }));
	const auto mixed = reseeder.ReadZIPFile (mixedPath.c_str ());

	// The outdated entries are skipped and counted; the fresh one is
	// imported.
	assert (mixed.imported == 1);
	assert (mixed.skippedStale == 2);
	assert (!mixed.cancelled);
	assert (mixed.error.empty ());
	// No reset: both cached peers survive next to the imported entry.
	assert (database.GetNumRouters () == 3);
	assert (database.FindRouter (cachedKeysA.GetPublic ()->GetIdentHash ()));
	assert (database.FindRouter (cachedKeysB.GetPublic ()->GetIdentHash ()));
	assert (database.FindRouter (freshKeys.GetPublic ()->GetIdentHash ()));

	// A bundle that only carries outdated entries reports the reason and
	// still leaves every cached peer untouched.
	const auto staleOnlyPath = WriteFile (directory, "stale-only.zip", BuildZip ({ staleA, staleB }));
	const auto staleOnly = reseeder.ReadZIPFile (staleOnlyPath.c_str ());
	assert (staleOnly.imported == 0);
	assert (staleOnly.skippedStale == 2);
	assert (staleOnly.error == "stale bundle");
	assert (database.GetNumRouters () == 3);
	assert (database.FindRouter (cachedKeysA.GetPublic ()->GetIdentHash ()));
	assert (database.FindRouter (cachedKeysB.GetPublic ()->GetIdentHash ()));

	// The injected target was used; the process netDb was never populated.
	assert (i2p::data::netdb.GetNumRouters () == 0);

	// Leave the fixture empty so the destructor persistence pass is
	// skipped.
	Access::ClearRouterInfos (database);
	assert (database.GetNumRouters () == 0);

	std::filesystem::remove_all (directory, error);
	return 0;
}
