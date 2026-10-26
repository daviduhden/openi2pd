/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test data-storage worker.
 *
 * Exercises the isolated artifact worker end to end against a temporary root:
 * canonical path layout, mode 0600, replacement, removal, rejection of
 * symlink/hardlink/directory/foreign targets, staging-slot cleanup and
 * restart. Skipped when the kernel cannot provide the storage sandbox or the
 * process is privileged. Prepared for manual CI only.
 */

#include <array>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "Base.h"
#include "privsep/DataStorage.h"
#include "privsep/LinuxSandbox.h"
#include "privsep/RunWorker.h"

using namespace i2p::privsep;
using i2p::data::StorageArtifact;

namespace
{
std::string Identity (uint8_t seed)
{
	std::array<uint8_t, 32> identity { };
	for (size_t i = 0; i < identity.size (); ++i) identity[i] = static_cast<uint8_t> (seed * 7 + i);
	return i2p::data::ByteStreamToBase64 (identity.data (), identity.size ());
}

std::string Base32Identity (uint8_t seed)
{
	std::array<uint8_t, 32> identity { };
	for (size_t i = 0; i < identity.size (); ++i) identity[i] = static_cast<uint8_t> (seed * 11 + i);
	return i2p::data::ByteStreamToBase32 (identity.data (), identity.size ());
}

std::vector<uint8_t> Bytes (uint8_t seed, size_t size)
{
	std::vector<uint8_t> bytes (size);
	for (size_t i = 0; i < size; ++i) bytes[i] = static_cast<uint8_t> (seed + i);
	return bytes;
}

bool ReadBack (const std::filesystem::path & path, std::vector<uint8_t> & bytes)
{
	std::ifstream input (path, std::ios::binary);
	if (!input) return false;
	bytes.assign (std::istreambuf_iterator<char> (input), std::istreambuf_iterator<char> ());
	return true;
}
}

#include "privsep-test-executable.h"

int main (int argc, char ** argv)
{
	i2p::privsep::PinTestExecutable (argv[0]);
	if (argc == 4 && std::string_view (argv[1]) == "--privsep-worker")
		return i2p::privsep::RunWorker (argv[2], argv[3]);
#if defined(__linux__) && !defined(__ANDROID__)
	if (::geteuid () == 0) return 77;
	const auto support = i2p::privsep::ProbeLinuxIsolation ();
	if (!support.filesystem || !support.seccompFilter) return 77;
	const auto root = std::filesystem::temp_directory_path () / ("openi2pd-data-storage-" + std::to_string (::getpid ()));
	std::filesystem::create_directory (root);
	struct Cleanup
	{
		std::filesystem::path path;
		~Cleanup ()
		{
			std::error_code ec;
			std::filesystem::remove_all (path, ec);
		}
	} cleanup { root };

	DataStorageClient client;
	std::string error;
	assert (client.Start (root, error));
	assert (client.Generation () == 1);

	const auto infoName = Identity (1);
	const auto profileName = Identity (2);
	const auto destinationName = Base32Identity (3) + ".3";

	// Every kind lands on the path the core already reads, with mode 0600 and
	// exactly the submitted bytes.
	struct Expectation
	{
		StorageArtifact kind;
		std::string name;
	};
	for (const auto & expectation : { Expectation { StorageArtifact::RouterInfoCache, infoName },
			 Expectation { StorageArtifact::PeerProfile, profileName },
			 Expectation { StorageArtifact::DestinationKeys, destinationName } })
	{
		const auto bytes = Bytes (static_cast<uint8_t> (expectation.name.size ()), 96);
		assert (client.Write (expectation.kind, expectation.name, bytes));
		assert (client.LastStatus () == DataStorageStatus::Ok);
		const auto path = root / ArtifactRelativePath (expectation.kind, expectation.name);
		assert (std::filesystem::exists (path));
		struct stat status { };
		assert (::stat (path.c_str (), &status) == 0);
		assert ((status.st_mode & 0777) == 0600);
		assert (status.st_nlink == 1);
		std::vector<uint8_t> read;
		assert (ReadBack (path, read));
		assert (read == bytes);
	}

	// Replacement keeps one file and the newest bytes.
	{
		const auto bytes = Bytes (9, 128);
		assert (client.Write (StorageArtifact::RouterInfoCache, infoName, bytes));
		const auto path = root / ArtifactRelativePath (StorageArtifact::RouterInfoCache, infoName);
		std::vector<uint8_t> read;
		assert (ReadBack (path, read));
		assert (read == bytes);
		// No staging slot survives a successful write.
		const auto directory = path.parent_path ();
		for (const auto & entry : std::filesystem::directory_iterator (directory))
			assert (entry.path ().filename ().string ().find (".openi2pd-artifact-") == std::string::npos);
	}

	// Non-canonical names, unknown kinds and empty payloads are refused by the
	// client before they reach the worker.
	{
		assert (!client.Write (StorageArtifact::RouterInfoCache, "not-a-name", Bytes (1, 4)));
		assert (!client.Write (StorageArtifact::RouterInfoCache, "../" + infoName, Bytes (1, 4)));
		assert (!client.Write (StorageArtifact::PeerProfile, infoName + "/x", Bytes (1, 4)));
		assert (!client.Write (StorageArtifact::RouterInfoCache, infoName, { }));
		assert (!client.Write (static_cast<StorageArtifact> (9), infoName, Bytes (1, 4)));
		assert (!client.Write (
			StorageArtifact::RouterInfoCache, infoName, Bytes (1, MAX_ARTIFACT_PAYLOAD_SIZE + 1)));
		assert (client.LastStatus () == DataStorageStatus::Rejected);
		// A rejection must not poison the live worker.
		assert (client.Write (StorageArtifact::RouterInfoCache, infoName, Bytes (3, 16)));
	}

	// Symlink, hardlink and directory targets are refused; the outside object
	// is never touched and the worker stays usable.
	{
		const auto path = root / ArtifactRelativePath (StorageArtifact::RouterInfoCache, infoName);
		std::filesystem::remove (path);
		std::filesystem::create_symlink ("/etc/passwd", path);
		assert (!client.Write (StorageArtifact::RouterInfoCache, infoName, Bytes (4, 16)));
		assert (std::filesystem::is_symlink (path));
		std::filesystem::remove (path);

		const auto outside = root / "outside.bin";
		{
			std::ofstream out (outside, std::ios::binary);
			out.write ("outside", 7);
		}
		std::filesystem::create_hard_link (outside, path);
		assert (!client.Write (StorageArtifact::RouterInfoCache, infoName, Bytes (5, 16)));
		std::vector<uint8_t> read;
		assert (ReadBack (outside, read));
		assert (std::string (read.begin (), read.end ()) == "outside");
		std::filesystem::remove (path);
		std::filesystem::remove (outside);

		std::filesystem::create_directory (path);
		assert (!client.Write (StorageArtifact::RouterInfoCache, infoName, Bytes (6, 16)));
		std::filesystem::remove (path);

		assert (client.Write (StorageArtifact::RouterInfoCache, infoName, Bytes (7, 16)));
	}

	// A stale staging slot left by a killed worker is cleaned up by the next
	// write of the same artifact family, without touching the target.
	{
		const auto path = root / ArtifactRelativePath (StorageArtifact::PeerProfile, profileName);
		const auto directory = path.parent_path ();
		const auto stale = directory / ".openi2pd-artifact-3";
		{
			std::ofstream out (stale, std::ios::binary);
			out.write ("stale", 5);
		}
		assert (client.Write (StorageArtifact::PeerProfile, profileName, Bytes (8, 64)));
		assert (!std::filesystem::exists (stale));
	}

	// Removal is idempotent and only deletes the artifact.
	{
		assert (client.Remove (StorageArtifact::DestinationKeys, destinationName));
		const auto path = root / ArtifactRelativePath (StorageArtifact::DestinationKeys, destinationName);
		assert (!std::filesystem::exists (path));
		assert (client.Remove (StorageArtifact::DestinationKeys, destinationName));
		// A directory at the target is not removed by the artifact path.
		std::filesystem::create_directory (path);
		assert (!client.Remove (StorageArtifact::DestinationKeys, destinationName));
		assert (std::filesystem::is_directory (path));
		std::filesystem::remove (path);
	}

	// Restart: requests from the previous generation are gone, the generation
	// advances and writes keep working.
	{
		const auto generation = client.Generation ();
		client.Stop ();
		assert (!client.Write (StorageArtifact::RouterInfoCache, infoName, Bytes (2, 8)));
		assert (client.LastStatus () == DataStorageStatus::Rejected);
		assert (client.Start (root, error));
		assert (client.Generation () == generation + 1);
		assert (client.Write (StorageArtifact::RouterInfoCache, infoName, Bytes (2, 8)));
	}

	// A second client on the same root is a fresh generation but the same
	// on-disk artifact set.
	{
		client.Stop ();
		DataStorageClient second;
		assert (second.Start (root, error));
		assert (second.Generation () == 1);
		std::vector<uint8_t> read;
		assert (ReadBack (root / ArtifactRelativePath (StorageArtifact::RouterInfoCache, infoName), read));
		assert (read == Bytes (2, 8));
		second.Stop ();
	}
#endif
	return 0;
}
