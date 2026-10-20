/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: delegated DHT state persistence regression.
 * Preserves compact NodeInfo bytes and fails closed on invalid storage input.
 */
#include "TorrentsDHT.h"
#include "TorrentsTunnel.h"
#include "TorrentStorageWorker.h"
#include "../privsep/RunWorker.h"
#include <cassert>
#include <cstring>
#include <fstream>
#if defined(__linux__) && !defined(__ANDROID__)
#include "../privsep/LinuxSandbox.h"
#include <unistd.h>
#endif
int main (int argc, char ** argv)
{
 if (argc == 4 && std::string_view (argv[1]) == "--privsep-worker")
  return i2p::privsep::RunWorker (argv[2], argv[3]);
#ifndef NO_TORRENTS
 using namespace i2p::torrents;
 TorrentsTunnel tunnel ("persistence-test", nullptr, "/unused-authority", "", false);
 i2p::data::IdentHash peer;
 std::memset (peer (), 0x42, peer.len);
 NodeID id {};
 std::memcpy (id.data (), peer (), 6);
 id[4] ^= 0x12; id[5] ^= 0x34;
 const auto node = Node (id, peer, 0x1234).GetNodeInfo ();
 std::vector<uint8_t> source (node.begin (), node.end ()), saved;
 size_t reads = 0, writes = 0;
 TorrentsDHT dht (tunnel, 1234, "preserved.dht");
 dht.SetPersistence ([&] (const std::filesystem::path & path, std::vector<uint8_t> & bytes, std::string &)
  { assert (path.filename () == "preserved.dht"); bytes = source; ++reads; return true; },
  [&] (const std::filesystem::path & path, std::span<const uint8_t> bytes, std::string &)
  { assert (path.filename () == "preserved.dht"); saved.assign (bytes.begin (), bytes.end ()); ++writes; return true; });
 dht.Start ();
 assert (reads == 1 && dht.GetNumNodes () == 1);
 dht.Stop ();
 assert (writes == 1 && saved == source);
 // Partial records must not replace the live routing state or node identity.
 source.push_back (0);
 dht.Start ();
 assert (dht.GetNumNodes () == 1);
 dht.Stop ();
 assert (saved.size () == node.size ());
#if defined(__linux__) && !defined(__ANDROID__)
 if (::geteuid () == 0) return 77;
 const auto support = i2p::privsep::ProbeLinuxIsolation ();
 if (!support.filesystem || !support.seccompFilter) return 77;
 auto root = std::filesystem::temp_directory_path () / ("openi2pd-dht-persistence-" + std::to_string (::getpid ()));
 assert (std::filesystem::create_directory (root));
 struct Cleanup { std::filesystem::path root; ~Cleanup () { std::error_code ec; std::filesystem::remove_all (root, ec); } } cleanup { root };
 TorrentStorageWorker worker;
 std::string error;
 assert (worker.Start (root, error));
 std::vector<uint8_t> data (64 * 1024 + 3);
 for (size_t n = 0; n < data.size (); ++n) data[n] = n % 251;
 assert (!worker.ReplaceMetadata ("legacy-cache", data, error));
 assert (worker.AuthorizeDHTMetadata ("legacy-cache", error));
 assert (worker.AuthorizeDHTMetadata ("legacy-cache", error));
 assert (worker.ReplaceMetadata ("legacy-cache", data, error));
 std::vector<uint8_t> legacy;
 assert (worker.ReadMetadata ("legacy-cache", legacy, error) && legacy == data);
 assert (!worker.AuthorizeDHTMetadata ("../outside", error));
 assert (!worker.AuthorizeDHTMetadata ("nested/cache", error));
 assert (!worker.AuthorizeDHTMetadata (".openi2pd-pending-0", error));
 assert (!worker.AuthorizeDHTMetadata (".openi2pd-state", error));
 for (unsigned n = 1; n < 16; ++n)
  assert (worker.AuthorizeDHTMetadata ("legacy-cache-" + std::to_string (n), error));
 assert (!worker.AuthorizeDHTMetadata ("one-too-many", error));
 assert (worker.ReplaceMetadata ("preserved.dht", data, error));
 std::vector<uint8_t> read;
 assert (worker.ReadMetadata ("preserved.dht", read, error) && read == data);
 assert (worker.ReplaceMetadata ("preserved.dht", {}, error));
 assert (worker.ReadMetadata ("preserved.dht", read, error) && read.empty ());
 assert (!worker.ReadMetadata ("missing.dht", read, error));
 assert (!worker.ReadMetadata ("../preserved.dht", read, error));
 assert (!worker.ReadMetadata ("nested/preserved.dht", read, error));
 std::filesystem::create_symlink (root / "preserved.dht", root / "link.dht");
 assert (!worker.ReadMetadata ("link.dht", read, error));
 std::filesystem::create_hard_link (root / "preserved.dht", root / "hard.dht");
 assert (!worker.ReadMetadata ("hard.dht", read, error));
 std::filesystem::remove (root / "hard.dht");
 { std::ofstream large (root / "large.dht", std::ios::binary); large.seekp (8 * 1024 * 1024); large.put ('x'); }
 assert (!worker.ReadMetadata ("large.dht", read, error));
 worker.Stop ();
 assert (!worker.ReadMetadata ("preserved.dht", read, error));
#endif
#endif
 return 0;
}
