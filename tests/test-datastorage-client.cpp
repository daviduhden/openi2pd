/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test data-storage client restart behaviour.
 *
 * Simulates worker death with a real child: the failing write is
 * reported and never replayed, the retry is scheduled by the injected
 * clock (250 ms then 500 ms), the replacement worker gets a new
 * generation, stale replies are dropped, and request-id exhaustion is a
 * Busy refusal that keeps the client usable. Skipped when the kernel
 * cannot provide the storage sandbox or the process is privileged.
 * Prepared for manual CI only.
 */

#include <array>
#include <cassert>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <sys/stat.h>
#include <thread>
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
	for (size_t i = 0; i < identity.size (); ++i) identity[i] = static_cast<uint8_t> (seed * 13 + i);
	return i2p::data::ByteStreamToBase64 (identity.data (), identity.size ());
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

/**
 * Wait until a request observes the dead worker; never advances the
 * clock.
 */
bool WaitForFailure (DataStorageClient & client, StorageArtifact kind, std::string_view name)
{
	for (int attempt = 0; attempt < 100; ++attempt)
	{
		if (!client.Write (kind, name, Bytes (9, 8))) return true;
		std::this_thread::sleep_for (std::chrono::milliseconds (10));
	}
	return false;
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
	const auto root
		= std::filesystem::temp_directory_path () / ("openi2pd-data-client-" + std::to_string (::getpid ()));
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

	auto now = std::chrono::steady_clock::now ();
	DataStorageClient client;
	client.SetClockForTesting ([&now] { return now; });
	std::string error;
	assert (client.Start (root, error));
	assert (client.Generation () == 1);
	assert (client.WorkerPid () > 0);

	const auto name = Identity (1);
	const auto path = root / ArtifactRelativePath (StorageArtifact::RouterInfoCache, name);
	const auto baseline = Bytes (1, 32);
	assert (client.Write (StorageArtifact::RouterInfoCache, name, baseline));
	assert (client.LastStatus () == DataStorageStatus::Ok);

	// Worker death: the in-flight write fails and is never replayed, so
	// the artifact still holds the last durable content.
	const long firstWorker = client.WorkerPid ();
	assert (firstWorker > 0);
	assert (::kill (static_cast<pid_t> (firstWorker), SIGKILL) == 0);
	assert (WaitForFailure (client, StorageArtifact::RouterInfoCache, name));
	assert (client.LastStatus () == DataStorageStatus::Busy);
	{
		std::vector<uint8_t> read;
		assert (ReadBack (path, read));
		assert (read == baseline);
	}
	const auto firstFailure = now;
	assert (client.Generation () == 1);
	// The client reaps the dead child and owns no worker while it is
	// restarting.
	assert (client.WorkerPid () <= 0);

	// Before the 250 ms backoff expires no replacement is spawned and the
	// refusal is Busy, not a silent success.
	now = firstFailure + std::chrono::milliseconds (100);
	assert (!client.Write (StorageArtifact::RouterInfoCache, name, Bytes (2, 16)));
	assert (client.LastStatus () == DataStorageStatus::Busy);
	assert (client.Generation () == 1);
	assert (client.WorkerPid () <= 0);
	{
		std::vector<uint8_t> read;
		assert (ReadBack (path, read));
		assert (read == baseline);
	}

	// At the deadline the client respawns, reconfigures with generation 2
	// and serves the new request; the abandoned one is gone.
	now = firstFailure + std::chrono::milliseconds (250);
	const auto secondContent = Bytes (3, 24);
	assert (client.Write (StorageArtifact::RouterInfoCache, name, secondContent));
	assert (client.LastStatus () == DataStorageStatus::Ok);
	assert (client.Generation () == 2);
	assert (client.WorkerPid () > 0);
	{
		std::vector<uint8_t> read;
		assert (ReadBack (path, read));
		assert (read == secondContent);
	}

	// A second failure backs off twice as long: the retry is refused at
	// 250 ms and allowed at 500 ms, again with a new generation.
	const long secondWorker = client.WorkerPid ();
	assert (secondWorker > 0);
	assert (::kill (static_cast<pid_t> (secondWorker), SIGKILL) == 0);
	assert (WaitForFailure (client, StorageArtifact::RouterInfoCache, name));
	const auto secondFailure = now;
	now = secondFailure + std::chrono::milliseconds (250);
	assert (!client.Write (StorageArtifact::RouterInfoCache, name, Bytes (4, 16)));
	assert (client.LastStatus () == DataStorageStatus::Busy);
	assert (client.Generation () == 2);
	now = secondFailure + std::chrono::milliseconds (500);
	assert (client.Write (StorageArtifact::RouterInfoCache, name, Bytes (5, 16)));
	assert (client.Generation () == 3);

	// Stale-generation replies are dropped, so a reply from an older
	// worker
	// can never be applied to the current request.
	assert (IsCurrentDataStorageReply (3, 42, 3, 42));
	assert (!IsCurrentDataStorageReply (2, 42, 3, 42));
	assert (!IsCurrentDataStorageReply (3, 41, 3, 42));

	// Request-id exhaustion is a local Busy refusal: the client stays
	// usable
	// and a later request with an id the worker has not seen still
	// succeeds.
	client.SetRequestIdForTesting (std::numeric_limits<uint32_t>::max ());
	assert (!client.Write (StorageArtifact::RouterInfoCache, name, Bytes (6, 8)));
	assert (client.LastStatus () == DataStorageStatus::Busy);
	assert (client.WorkerPid () > 0);
	assert (client.Generation () == 3);
	client.SetRequestIdForTesting (100);
	const auto lastContent = Bytes (7, 12);
	assert (client.Write (StorageArtifact::RouterInfoCache, name, lastContent));
	assert (client.LastStatus () == DataStorageStatus::Ok);
	{
		std::vector<uint8_t> read;
		assert (ReadBack (path, read));
		assert (read == lastContent);
	}

	// A stopped client refuses without attempting to respawn.
	client.Stop ();
	const auto stoppedGeneration = client.Generation ();
	now += std::chrono::seconds (60);
	assert (!client.Write (StorageArtifact::RouterInfoCache, name, Bytes (8, 8)));
	assert (client.LastStatus () == DataStorageStatus::Rejected);
	assert (client.Generation () == stoppedGeneration);
	assert (client.WorkerPid () <= 0);
#endif
	return 0;
}
