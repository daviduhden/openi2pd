/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: OpenBSD supervised torrent storage regression.
 * Exercises pre-pledge startup, delegated roots and worker replacement
 * while
 * the controlling process has neither proc nor exec promises. Manual CI
 * only.
 */
#include "../privsep/Supervisor.h"
#include "../privsep/RunWorker.h"
#include "../libi2pd_client/TorrentStorageWorker.h"
#include <array>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <fcntl.h>
#include <unistd.h>
#include "privsep-test-executable.h"

int main (int argc, char ** argv)
{
	i2p::privsep::PinTestExecutable (argv[0]);
	if (argc == 4 && std::string_view (argv[1]) == "--privsep-worker")
		return i2p::privsep::RunWorker (argv[2], argv[3]);
#if defined(__OpenBSD__)
	if (::geteuid () == 0) return 77; // Production explicitly refuses root.
	auto executable = std::filesystem::canonical (argv[0]).string ();
	assert (::setenv ("I2PD_EXECUTABLE", executable.c_str (), 1) == 0);
	char temporary[] = "/tmp/openi2pd-storage-openbsd-XXXXXX";
	const char * directory = ::mkdtemp (temporary);
	assert (directory);
	const std::filesystem::path root (directory);
	const auto outside = root.parent_path () / (root.filename ().string () + "-outside");
	std::filesystem::create_directory (outside);
	struct Cleanup
	{
		std::filesystem::path root, outside;
		~Cleanup ()
		{
			std::error_code ec;
			std::filesystem::remove_all (root, ec);
			std::filesystem::remove_all (outside, ec);
		}
	} cleanup { root, outside };
	std::filesystem::create_directory (root / "swarm");
	std::filesystem::permissions (root / "swarm", std::filesystem::perms::owner_all);
	std::filesystem::create_directory_symlink (outside, root / "redirect");
	std::string error;
	assert (i2p::privsep::PrepareTorrentStorageSupervisor (root.string (), error));
	// A fork/exec by the core from here would abort. Descriptor passing
	// grants only the local IPC operations necessary to obtain an
	// already-confined child.
	assert (::pledge ("stdio rpath wpath cpath fattr unix recvfd", nullptr) == 0);
	i2p::torrents::TorrentStorageWorker worker;
	assert (worker.Start (root / "swarm", error));
	const auto handle = worker.Register ("a/first.bin", 8, error, true);
	assert (handle);
	std::array<uint8_t, 4> data { 4, 3, 2, 1 }, result { };
	assert (worker.Write (handle, 2, data, error));
	assert (worker.Read (handle, 2, result, error) && result == data);
	assert (!worker.Write (handle, 7, data, error));
	assert (!worker.Register ("../escape", 4, error, true));
	worker.Stop ();
	// Replacement is an explicit administrative action; acknowledged
	// persisted
	// bytes are retained, and outstanding writes are never silently
	// replayed.
	assert (worker.Start (root / "swarm", error));
	const auto readOnly = worker.Register ("a/first.bin", 8, error);
	assert (readOnly && worker.Read (readOnly, 2, result, error) && result == data);
	assert (!worker.Write (readOnly, 0, data, error));
	worker.Stop ();
	// Deliberate channel loss: supervisor owns/reaps the child and permits
	// a new
	// child without allowing process creation in the core.
	int rootFd = ::open ((root / "swarm").c_str (), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	assert (rootFd >= 0);
	auto channel = i2p::privsep::SpawnSupervisedTorrentStorage (rootFd, (root / "swarm").string ());
	::close (rootFd);
	assert (channel);
	assert (channel->Send ({ i2p::privsep::MessageType::Shutdown, 0, { } }, 5000));
	auto ended = channel->Receive (5000);
	assert (!ended); // EOF must not be mistaken for an empty successful reply.
	channel->Close ();
	assert (worker.Start (root / "swarm", error));
	worker.Stop ();
	// An outside-root request becomes a forbidden relative traversal. Only
	// identity integers are transmitted; directories never travel in
	// SCM_RIGHTS. A per-request rejection must leave the broker and
	// existing workers alive.
	int outsideFd = ::open (outside.c_str (), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	assert (outsideFd >= 0);
	auto rejected = i2p::privsep::SpawnSupervisedTorrentStorage (outsideFd, outside.string ());
	::close (outsideFd);
	assert (!rejected);
	// A path that resolves through a symlink must not become a delegated
	// root.
	// The worker walks the broker root with O_NOFOLLOW before applying
	// policy;
	// the broker reports a typed failure and stays alive.
	int swarmFd = ::open ((root / "swarm").c_str (), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	assert (swarmFd >= 0);
	auto mismatched = i2p::privsep::SpawnSupervisedTorrentStorage (swarmFd, (root / "redirect").string ());
	::close (swarmFd);
	assert (!mismatched);
	// The broker is still usable and a legitimate root can be delegated
	// again.
	assert (worker.Start (root / "swarm", error));
	worker.Stop ();
	return 0;
#else
	return 77;
#endif
}
