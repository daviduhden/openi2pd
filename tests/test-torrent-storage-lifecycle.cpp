/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: storage capability lifecycle regressions.
 * Prepared for manual CI; checks bounded metadata and payload administration.
 */
#include "../libi2pd_client/TorrentStorageWorker.h"
#include "../privsep/RunWorker.h"
#include <array>
#include <cassert>
#include <filesystem>
#include <string>
#include <fstream>
#if defined(__linux__) && !defined(__ANDROID__)
#include "../privsep/LinuxSandbox.h"
#include <unistd.h>
#endif
int main (int argc, char ** argv)
{
 if (argc == 4 && std::string_view (argv[1]) == "--privsep-worker")
  return i2p::privsep::RunWorker (argv[2], argv[3]);
#if defined(__linux__) && !defined(__ANDROID__)
 if (::geteuid () == 0) return 77;
 auto support = i2p::privsep::ProbeLinuxIsolation ();
 if (!support.filesystem || !support.seccompFilter) return 77;
 auto root = std::filesystem::temp_directory_path () / ("openi2pd-storage-lifecycle-" + std::to_string (::getpid ()));
 std::filesystem::create_directory (root);
 struct Cleanup { std::filesystem::path root; ~Cleanup () { std::error_code ec; std::filesystem::remove_all (root, ec); } } cleanup { root };
 i2p::torrents::TorrentStorageWorker worker;
 std::string error;
 assert (worker.Start (root, error));
 std::array<uint8_t, 4> bytes {1, 2, 3, 4};
 auto handle = worker.Register ("nested/payload.part", bytes.size (), error, true);
 assert (handle && worker.Write (handle, 0, bytes, error) && worker.Release (handle, error));
 assert (!worker.Promote ("../outside", "nested/payload", bytes.size (), error));
 assert (!worker.Promote ("nested/payload.part", "nested/payload", 99, error));
 assert (worker.Promote ("nested/payload.part", "nested/payload", bytes.size (), error));
 assert (worker.Promote ("nested/payload.part", "nested/payload", bytes.size (), error));
 assert (!std::filesystem::exists (root / "nested/payload.part"));
 handle = worker.Register ("nested/payload.part", bytes.size (), error, true);
 assert (handle && worker.Release (handle, error));
 assert (!worker.Promote ("nested/payload.part", "nested/payload", bytes.size (), error));
 // Atomic metadata commits are bounded and never permit a general path write.
 std::string state = ".openi2pd-state/" + std::string (40, 'a') + ".state";
 std::string large (48 * 1024, 'x');
 auto data = std::span<const uint8_t> (reinterpret_cast<const uint8_t *> (large.data ()), large.size ());
 assert (worker.ReplaceMetadata ("test.torrent", data, error));
 assert (std::filesystem::file_size (root / "test.torrent") == large.size ());
 assert (worker.ReplaceMetadata ("test.torrent", bytes, error));
 assert (std::filesystem::file_size (root / "test.torrent") == bytes.size ());
 assert (worker.ReplaceMetadata (state, bytes, error));
 assert (!worker.ReplaceMetadata ("arbitrary-key", bytes, error));
 assert (!worker.ReplaceMetadata ("nested/test.torrent", bytes, error));
 assert (!worker.ReplaceMetadata ("../test.resume", bytes, error));
 assert (!worker.ReplaceMetadata (".openi2pd-state/not-a-hash.state", bytes, error));
 std::filesystem::create_symlink (root / "test.torrent", root / "link.resume");
 assert (!worker.ReplaceMetadata ("link.resume", bytes, error));
 assert (!worker.RemoveMetadata ("link.resume", error));
 std::filesystem::create_hard_link (root / "test.torrent", root / "hard.resume");
 assert (!worker.ReplaceMetadata ("hard.resume", bytes, error));
 assert (!worker.RemoveMetadata ("hard.resume", error));
 std::filesystem::remove (root / "hard.resume");
 assert (worker.RemoveMetadata ("test.torrent", error));
 assert (worker.RemoveMetadata ("test.torrent", error));
 assert (worker.RemoveMetadata (state, error));
 // Killed-worker staging cannot grow with restart count. Full reserved slots
 // fail closed and preserve the last committed metadata instead of clobbering.
 assert (worker.ReplaceMetadata ("test.resume", bytes, error));
 for (unsigned slot = 0; slot < 8; ++slot)
 { std::ofstream orphan (root / (".openi2pd-pending-" + std::to_string (slot))); orphan << "incomplete"; }
 assert (!worker.ReplaceMetadata ("test.resume", data, error));
 assert (std::filesystem::file_size (root / "test.resume") == bytes.size ());
 for (unsigned slot = 0; slot < 8; ++slot)
  std::filesystem::remove (root / (".openi2pd-pending-" + std::to_string (slot)));
 assert (worker.ReplaceMetadata ("test.resume", data, error));
 assert (worker.RemoveMetadata ("test.resume", error));
 assert (!worker.RemoveEmptyDirectory ("nested", error));
 assert (!worker.Remove ("nested/payload", 99, error));
 assert (worker.Remove ("nested/payload", bytes.size (), error));
 assert (worker.Remove ("nested/payload", bytes.size (), error));
 assert (worker.Remove ("nested/payload.part", bytes.size (), error));
 assert (worker.RemoveEmptyDirectory ("nested", error));
 worker.Stop ();
 assert (!worker.RemoveMetadata ("test.torrent", error));
 return 0;
#else
 return 77;
#endif
}
