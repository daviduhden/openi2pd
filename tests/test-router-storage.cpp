/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: fixed-authority router persistence regressions.
 * Prepared for manual CI only; this file must not be executed locally.
 */
#include "../privsep/RouterStorage.h"
#include "../privsep/RunWorker.h"
#include "../privsep/LinuxSandbox.h"
#include <array>
#include <cassert>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
int main (int argc, char ** argv)
{
 if (argc == 4 && std::string_view (argv[1]) == "--privsep-worker")
  return i2p::privsep::RunWorker (argv[2], argv[3]);
#if defined(__linux__) && !defined(__ANDROID__)
 if (::geteuid () == 0) return 77;
 const auto support = i2p::privsep::ProbeLinuxIsolation ();
 if (!support.filesystem || !support.seccompFilter) return 77;
 auto root = std::filesystem::temp_directory_path () / ("openi2pd-router-storage-" + std::to_string (::getpid ()));
 std::filesystem::create_directory (root);
 struct Cleanup { std::filesystem::path path; ~Cleanup () { std::error_code ec; std::filesystem::remove_all (path, ec); } } cleanup {root};
 i2p::privsep::RouterStorageClient client;
 std::string error;
 assert (client.Start (root, error));
 const std::array<uint8_t, 5> bytes {1, 0, 2, 0, 3};
 using File = i2p::data::RouterPersistentFile;
 for (auto file : {File::RouterKeys, File::RouterInfo, File::NTCP2Keys, File::SSU2Keys})
 {
  assert (client.Write (file, bytes));
  auto path = root / i2p::data::RouterPersistentFileName (file);
  struct stat status {};
  assert (::stat (path.c_str (), &status) == 0 && (status.st_mode & 0777) == 0600);
  std::ifstream input (path, std::ios::binary);
  std::array<uint8_t, 5> read {};
  input.read (reinterpret_cast<char *> (read.data ()), read.size ());
  assert (input && read == bytes);
 }
 assert (!client.Write (static_cast<File> (255), bytes));
 assert (!client.Write (File::RouterKeys, {}));
 client.Stop ();
 assert (!client.Write (File::RouterKeys, bytes));
 // A failed transport must not cause core dispatch to write a local key file.
 i2p::data::InstallRouterPersistenceWriter ([] (File, std::span<const uint8_t>) { return false; });
 assert (!i2p::data::WriteRouterPersistentFile (File::RouterKeys, bytes));
 i2p::data::InstallRouterPersistenceWriter ({});
 // Link substitution is rejected; the outside object remains unchanged.
 std::filesystem::remove (root / "router.keys");
 std::filesystem::create_symlink ("/etc/passwd", root / "router.keys");
 assert (client.Start (root, error));
 assert (!client.Write (File::RouterKeys, bytes));
 client.Stop ();
#endif
}
