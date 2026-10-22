/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test router profile storage.
 *
 * Exercises isolated profile persistence, immutable storage-root authority,
 * and serialization between profile replacement and obsolete-file cleanup.
 *
 */

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "FS.h"
#include "Profiling.h"

int main ()
{
	const auto root = std::filesystem::temp_directory_path ()
		/ ("openi2pd-profile-storage-"
			+ std::to_string (std::chrono::steady_clock::now ().time_since_epoch ().count ()));
	std::filesystem::create_directories (root);
	const auto abandonedStage = root / "peerProfiles" / "pA" / "profile-stale.txt.tmp-previous";
	std::filesystem::create_directories (abandonedStage.parent_path ());
	std::ofstream (abandonedStage) << "interrupted write";
	i2p::data::InitProfilesStorage (root.string ());
	assert (std::filesystem::exists (abandonedStage));

	i2p::data::IdentHash ident;
	ident.Fill (0x6D);
	i2p::data::RouterProfile profile;
	profile.Connected ();
	profile.Duplicated ();
	assert (profile.Save (ident));

	std::vector<std::filesystem::path> files;
	for (const auto & entry : std::filesystem::recursive_directory_iterator (root))
		if (entry.is_regular_file () && entry.path ().extension () == ".txt") files.push_back (entry.path ());
	assert (files.size () == 1);
	const auto profilePath = files.front ();
	std::string contents;
	{
		std::ifstream saved (profilePath);
		contents.assign (std::istreambuf_iterator<char> (saved), std::istreambuf_iterator<char> ());
	}
	assert (contents.find ("lastupdatetimestamp=") != std::string::npos);
	assert (contents.find ("connected=") != std::string::npos);
	assert (contents.find ("duplicated=") != std::string::npos);

	i2p::data::RouterProfile loaded;
	loaded.Load (ident);
	assert (loaded.IsReal ());
	assert (loaded.IsDuplicated ());
	assert (!loaded.IsUpdated ());

	const auto alternateRoot = root.string () + "-alternate";
	bool rejectedRootChange = false;
	try
	{
		i2p::data::InitProfilesStorage (alternateRoot);
	}
	catch (const std::runtime_error &)
	{
		rejectedRootChange = true;
	}
	assert (rejectedRootChange);
	i2p::data::InitProfilesStorage ((root / ".").string ());
	assert (profile.Save (ident));
	assert (std::filesystem::exists (profilePath));

#ifndef _WIN32
	const auto shard = profilePath.parent_path ();
	const auto movedShard = root / "held-shard";
	const auto outside = root / "outside";
	std::filesystem::create_directories (outside);
	std::filesystem::rename (shard, movedShard);
	std::filesystem::create_directory_symlink (outside, shard);
	assert (!i2p::fs::AtomicWrite (profilePath.string (), "must not follow shard symlink"));
	assert (!std::filesystem::exists (outside / profilePath.filename ()));
	std::filesystem::remove (shard);
	std::filesystem::rename (movedShard, shard);
#endif

	const auto staleTime = std::filesystem::file_time_type::clock::now ()
		- std::chrono::seconds (i2p::data::PEER_PROFILE_EXPIRATION_TIMEOUT + 60);
	// Cleanup must neither claim unknown leaves nor follow profile symlinks.
	const auto unrelated = profilePath.parent_path () / "administrator-notes.txt";
	const auto malformedName = profilePath.parent_path () / "profile-invalid.txt";
	const auto wrongShard = root / "peerProfiles" / "pA" / profilePath.filename ();
	std::ofstream (unrelated) << "keep";
	std::ofstream (malformedName) << "keep";
	std::ofstream (wrongShard) << "keep";
	for (const auto & retained : { abandonedStage, unrelated, malformedName, wrongShard })
		std::filesystem::last_write_time (retained, staleTime);
	i2p::data::IdentHash expiredIdent;
	expiredIdent.Fill (0x42);
	i2p::data::RouterProfile expired;
	expired.Connected ();
	assert (expired.Save (expiredIdent));
	const auto expiredName = "profile-" + expiredIdent.ToBase64 () + ".txt";
	const auto expiredPath = root / "peerProfiles" / ("p" + expiredIdent.ToBase64 ().substr (0, 1)) / expiredName;
	std::filesystem::last_write_time (expiredPath, staleTime);
#ifndef _WIN32
	i2p::data::IdentHash linkIdent;
	linkIdent.Fill (0x43);
	const auto linkPath = root / "peerProfiles" / ("p" + linkIdent.ToBase64 ().substr (0, 1))
		/ ("profile-" + linkIdent.ToBase64 () + ".txt");
	std::filesystem::create_symlink (unrelated, linkPath);
#endif
	std::filesystem::last_write_time (profilePath, staleTime);
	std::jthread saver ([&] { assert (profile.Save (ident)); });
	auto cleanup = i2p::data::DeleteObsoleteProfiles ();
	saver.join ();
	if (cleanup.valid ()) cleanup.get ();
	assert (std::filesystem::exists (profilePath));
	assert (!std::filesystem::exists (expiredPath));
	for (const auto & retained : { abandonedStage, unrelated, malformedName, wrongShard })
		assert (std::filesystem::exists (retained));
#ifndef _WIN32
	assert (std::filesystem::is_symlink (linkPath));
#endif

	assert (i2p::fs::AtomicWrite (profilePath.string (), "malformed [ profile"));
	i2p::data::RouterProfile dirty;
	dirty.Connected ();
	dirty.Load (ident);
	assert (dirty.IsUpdated ());

	std::filesystem::remove_all (root);
	return 0;
}
