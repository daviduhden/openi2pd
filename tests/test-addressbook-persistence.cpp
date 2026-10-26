/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test address-book persistence.
 *
 * Offline checks that every address-book artifact (the identity file, the
 * address index, the hosts.txt dump, the subscription ETag file, deleted.txt
 * and the entry-origin sidecar) is replaced atomically: no staging file is left
 * behind, a symlink pre-placed at the target path is never followed or
 * truncated, the files keep private permissions and an empty index is not
 * materialised. The only filesystem use is an isolated temporary data
 * directory; no destination, resolver, socket, thread, timer or network is
 * started.
 */

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <system_error>

#include "AddressBook.h"
#include "Config.h"
#include "FS.h"
#include "Identity.h"

namespace fs = std::filesystem;

namespace
{
constexpr const char * VICTIM_CONTENT = "keep-this-file-intact\n";

std::string ReadFile (const fs::path & path)
{
	std::ifstream f (path, std::ios::binary);
	assert (f.is_open ());
	return std::string (std::istreambuf_iterator<char> (f), std::istreambuf_iterator<char> ());
}

void WriteFile (const fs::path & path, const std::string & contents)
{
	std::ofstream f (path, std::ios::binary | std::ios::trunc);
	assert (f.is_open ());
	f.write (contents.data (), static_cast<std::streamsize> (contents.size ()));
	f.close ();
	assert (f);
}

#ifndef _WIN32
// A symlink is only meaningful where the test platform allows creating one
// unprivileged; the rest of the checks run everywhere.
void PlaceSymlink (const fs::path & victim, const fs::path & link)
{
	std::error_code error;
	// The path may already hold a regular file from an earlier block.
	fs::remove (link, error);
	error.clear ();
	fs::create_symlink (victim, link, error);
	assert (!error);
}
#endif

i2p::data::PrivateKeys MakeKeys ()
{
	return i2p::data::PrivateKeys::CreateRandomKeys (
		i2p::data::SIGNING_KEY_TYPE_EDDSA_SHA512_ED25519, i2p::data::CRYPTO_KEY_TYPE_ECIES_X25519_AEAD, true);
}

std::string Base64 (const i2p::data::PrivateKeys & keys)
{
	return keys.GetPublic ()->ToBase64 ();
}

void InitialiseConfig (const fs::path & root, const fs::path & hostsFile)
{
	i2p::config::Init ();
	const auto configPath = root / "i2pd.conf";
	{
		std::ofstream file (configPath, std::ios::trunc);
		assert (file);
		// persist.addressbook and addressbook.enabled keep their compiled
		// defaults (enabled); only the hosts.txt dump path is set here.
		file << "addressbook.hostsfile = " << hostsFile.string () << "\n";
		file.close ();
		assert (file);
	}
	i2p::config::ParseConfig (configPath.string ());
	i2p::config::Finalize ();
	bool persist = false;
	assert (i2p::config::GetOption ("persist.addressbook", persist) && persist);
	bool enabled = false;
	assert (i2p::config::GetOption ("addressbook.enabled", enabled) && enabled);
}

// No atomic replacement leaves a ".tmp-*" staging file behind.
size_t CountStagingFiles (const fs::path & directory)
{
	std::error_code error;
	size_t staging = 0;
	for (const auto & entry : fs::directory_iterator (directory, error))
	{
		const auto name = entry.path ().filename ().string ();
		if (name.find (".tmp-") != std::string::npos) staging++;
	}
	return staging;
}
} // namespace

int main ()
{
	const auto root = fs::temp_directory_path ()
		/ ("openi2pd-addressbook-persistence-"
			+ std::to_string (std::chrono::steady_clock::now ().time_since_epoch ().count ()));
	fs::create_directories (root);
	i2p::fs::DetectDataDir (root.string (), false);

	const auto hostsDump = root / "hosts-dump.txt";
	InitialiseConfig (root, hostsDump);

	const auto keys = MakeKeys ();
	const std::string destination = Base64 (keys);
	const std::string b32 = keys.GetPublic ()->GetIdentHash ().ToBase32 ();

	// The filesystem backend derives these paths from the data directory.
	const auto storageRoot = root / "addressbook";
	const auto indexPath = storageRoot / "addresses.csv";
	const auto deletedPath = storageRoot / "deleted.txt";
	const auto sourcesPath = storageRoot / "entry-sources.txt";
	const auto etagsPath = storageRoot / "etags";
	const auto identityPath = storageRoot / ("b" + std::string (1, b32[0])) / (b32 + ".b32");

	// An empty index is not materialised: a clean shutdown must not turn an
	// absent index into an authoritative empty one.
	{
		i2p::client::AddressBook book;
		book.Start ();
		assert (fs::exists (storageRoot));
		assert (!fs::exists (indexPath));
		book.Stop ();
		assert (!fs::exists (indexPath));
		assert (CountStagingFiles (storageRoot) == 0);
	}

	// The index is replaced atomically and a symlink placed at its path is
	// neither read through nor truncated.
	{
#ifndef _WIN32
		// A plausible index that must never be imported through the symlink.
		const auto victim = root / "victim-index.txt";
		const std::string victimIndex = "evil.i2p," + std::string (52, 'A') + "\n";
		WriteFile (victim, victimIndex);
		PlaceSymlink (victim, indexPath);
#endif
		i2p::client::AddressBook book;
		book.Start ();
#ifndef _WIN32
		assert (!book.FindAddress ("evil.i2p"));
#endif
		assert (book.AddAddressChecked ("site.i2p", destination, false) == i2p::client::AddressAddResult::Ok);
		assert (!fs::is_symlink (indexPath));
		const auto contents = ReadFile (indexPath);
		assert (contents == "site.i2p," + b32 + "\n");
		assert (CountStagingFiles (storageRoot) == 0);
#ifndef _WIN32
		// The pre-placed symlink was replaced, not written through.
		assert (ReadFile (victim) == victimIndex);
		const auto permissions = fs::symlink_status (indexPath).permissions ();
		assert ((permissions & (fs::perms::group_all | fs::perms::others_all)) == fs::perms::none);
#endif
		book.Stop ();
		assert (!fs::is_symlink (indexPath));
#ifndef _WIN32
		assert (ReadFile (victim) == victimIndex);
#endif
	}

	// The identity file, the hosts.txt dump, deleted.txt, the ETag file and the
	// entry-origin sidecar are replaced atomically as well.
	{
		i2p::client::AddressBook book;
		book.Start ();
		const auto etagPath = etagsPath / (b32 + ".txt");
#ifndef _WIN32
		const auto victim = root / "victim-artifacts.txt";
		WriteFile (victim, VICTIM_CONTENT);
		PlaceSymlink (victim, identityPath);
		PlaceSymlink (victim, hostsDump);
		PlaceSymlink (victim, deletedPath);
		PlaceSymlink (victim, etagPath);
		PlaceSymlink (victim, sourcesPath);
#endif
		assert (book.AddAddressChecked ("artifact.i2p", destination, false) == i2p::client::AddressAddResult::Ok);
		assert (!fs::is_symlink (identityPath));
		const auto identity = ReadFile (identityPath);
		assert (identity.size () == keys.GetPublic ()->GetFullLen ());
		assert (!fs::is_symlink (hostsDump));
		// The dump is a full hosts.txt view: both stored names share one identity.
		const auto dump = ReadFile (hostsDump);
		assert (dump.find ("artifact.i2p=" + destination + "\n") != std::string::npos);
		assert (dump.find ("site.i2p=" + destination + "\n") != std::string::npos);

		assert (book.BlockAddress ("blocked.i2p") == i2p::client::AddressBookMutationResult::Ok);
		assert (!fs::is_symlink (deletedPath));
		const auto deleted = ReadFile (deletedPath);
		assert (deleted.find ("blocked.i2p\n") != std::string::npos);
		assert (deleted.find ("site.i2p") == std::string::npos);

		book.DownloadComplete (true, keys.GetPublic ()->GetIdentHash (), "\"etag-1\"", "last-modified-1");
		assert (!fs::is_symlink (etagPath));
		assert (ReadFile (etagPath) == "\"etag-1\"\nlast-modified-1\n");
		// The ETag round trip is unchanged.
		std::string etag, lastModified;
		assert (book.GetEtag (keys.GetPublic ()->GetIdentHash (), etag, lastModified));
		assert (etag == "\"etag-1\"" && lastModified == "last-modified-1");

		// A feed merge records the entry origin in the sidecar.
		const auto feedKeys = MakeKeys ();
		std::istringstream update ("fromfeed.i2p=" + Base64 (feedKeys) + "\n");
		assert (book.LoadHostsFromStream (std::move (update), true, "stats.i2p"));
		assert (!fs::is_symlink (sourcesPath));
		const auto sources = ReadFile (sourcesPath);
		assert (sources.find ("fromfeed.i2p\tstats.i2p\n") != std::string::npos);
		assert (sources.find ("artifact.i2p") == std::string::npos);
		assert (!fs::is_symlink (hostsDump));
		assert (ReadFile (hostsDump).find ("fromfeed.i2p=" + Base64 (feedKeys) + "\n") != std::string::npos);
#ifndef _WIN32
		assert (ReadFile (victim) == VICTIM_CONTENT);
		assert (!fs::is_symlink (identityPath));
		assert (!fs::is_symlink (etagPath));
		assert (!fs::is_symlink (deletedPath));
		assert (!fs::is_symlink (sourcesPath));
#endif
		book.Stop ();
		assert (CountStagingFiles (storageRoot) == 0);
#ifndef _WIN32
		assert (ReadFile (victim) == VICTIM_CONTENT);
#endif
	}

	// The reloaded book reads the index and the origin sidecar back: the
	// originating feed may still retract its own name, which proves that the
	// sidecar round trip works without changing the index columns.
	{
		i2p::client::AddressBook book;
		book.Start ();
		assert (book.FindAddress ("artifact.i2p"));
		assert (book.FindAddress ("fromfeed.i2p"));
		auto deleted = book.GetDeletedHosts (10);
		assert (deleted.size () == 1 && deleted[0] == "blocked.i2p");
		const auto feedDestination = Base64 (MakeKeys ());
		std::istringstream retract ("fromfeed.i2p=" + feedDestination + "#!action=delete\n");
		assert (book.LoadHostsFromStream (std::move (retract), true, "stats.i2p"));
		assert (!book.FindAddress ("fromfeed.i2p"));
		// A locally added name stays.
		assert (book.FindAddress ("artifact.i2p"));
		book.Stop ();
	}

	// A different feed cannot retract the reloaded names either.
	{
		i2p::client::AddressBook book;
		book.Start ();
		std::istringstream foreign ("artifact.i2p=" + destination + "#!action=delete\n");
		assert (book.LoadHostsFromStream (std::move (foreign), true, "other.i2p"));
		assert (book.FindAddress ("artifact.i2p"));
		book.Stop ();
	}

	std::error_code ignored;
	fs::remove_all (root, ignored);
	return 0;
}
