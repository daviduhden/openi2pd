/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: payload capability regression cases for manual CI.
 * Checks bounded cross-file IO and recovery without filesystem fallback.
 */
#include "Torrents.h"
#include <algorithm>
#include <cassert>
#include <map>
#ifndef NO_TORRENTS
using namespace i2p::torrents;
class MemoryStorage final : public TorrentPayloadStorage
{
public:
 std::map<std::string, std::vector<uint8_t>> files;
 struct Handle { std::string name; bool writable; };
 std::map<uint64_t, Handle> handles;
 uint64_t next = 1;
 size_t calls = 0, opens = 0;
 bool failRead = false, failWrite = false;
 uint64_t Open (const std::filesystem::path & relative, uint64_t length, bool create, std::string &) override
 {
  ++opens;
  assert (IsSafeRelativePath (relative));
  auto name = relative.generic_string ();
  if (!files.contains (name)) { if (!create) return 0; files[name].resize (length); }
  if (files[name].size () != length) return 0;
  auto id = next++;
  handles.emplace (id, Handle {name, create});
  return id;
 }
 bool Read (uint64_t handle, uint64_t offset, std::span<uint8_t> bytes, std::string &) override
 {
  ++calls; assert (bytes.size () <= MaxTransfer);
  if (failRead || !handles.contains (handle)) return false;
  auto & f = files[handles.at (handle).name];
  if (offset > f.size () || bytes.size () > f.size () - offset) return false;
  std::copy_n (f.data () + offset, bytes.size (), bytes.data ()); return true;
 }
 bool Write (uint64_t handle, uint64_t offset, std::span<const uint8_t> bytes, std::string &) override
 {
  ++calls; assert (bytes.size () <= MaxTransfer);
  if (failWrite || !handles.contains (handle) || !handles.at (handle).writable) return false;
  auto & f = files[handles.at (handle).name];
  if (offset > f.size () || bytes.size () > f.size () - offset) return false;
  std::copy (bytes.begin (), bytes.end (), f.begin () + offset); return true;
 }
 bool Close (uint64_t handle, std::string &) override { return handles.erase (handle) == 1; }
};
int main ()
{
 auto backend = std::make_shared<MemoryStorage> ();
 // Virtual paths have no filesystem authority and need never exist on disk.
 auto root = std::filesystem::path ("payload-capability-root");
 auto first = std::make_shared<TorrentFile> (root / "first", 20000);
 auto second = std::make_shared<TorrentFile> (root / "second", 20000);
 first->SetPayloadStorage (backend, root); second->SetPayloadStorage (backend, root);
 first->SetIsPart (false); second->SetIsPart (false);
 std::vector<uint8_t> piece (32768);
 for (size_t i = 0; i < piece.size (); ++i) piece[i] = static_cast<uint8_t> (i * 17);
 assert (first->Save (0, piece.data (), 20000));
 assert (second->Save (0, piece.data () + 20000, piece.size () - 20000));
 assert (backend->calls == 3); // One file spans two bounded broker requests.
 std::array<uint8_t, SHA_DIGEST_LENGTH> digest {};
 SHA1 (piece.data (), piece.size (), digest.data ());
 Piece verified (piece.size (), digest.data ());
 assert (verified.Load ({first, 0, 0, 20000}));
 assert (verified.Load ({second, 0, 20000, piece.size () - 20000}));
 assert (verified.VerifyHash ());
 verified.SetIsSaving (true);
 assert (verified.IsSaving () && !verified.IsVerified ());
 assert (!verified.HasBlock (0));
 std::vector<uint8_t> withheld;
 assert (!verified.CopyBlock (0, 16, withheld));
 verified.ReleaseData (); // Queued writes retain their verified payload buffer.
 assert (verified.HasData ());
 verified.SetIsSaving (false);
 assert (verified.IsVerified ());
 auto metadata = Torrent::CreateV1 ("saving", {{"payload", piece.size ()}}, piece.size (), {digest});
 assert (metadata);
 auto & restored = metadata->GetPiece (0);
 restored.Complete (true);
 assert (restored.IsSaving ());
 assert (restored.Load ({first, 0, 0, 20000}));
 assert (restored.Load ({second, 0, 20000, piece.size () - 20000}));
 assert (restored.VerifyHash () && !metadata->AreAllPiecesVerified ());
 restored.SetIsSaving (false);
 assert (metadata->AreAllPiecesVerified ());
 restored.SetIsSaving (true);
 assert (!metadata->AreAllPiecesVerified ());
 restored.SetIsSaving (false);
 assert (metadata->AreAllPiecesVerified ());
 metadata->SetComplete ();
 assert (metadata->IsComplete ());
 metadata->InvalidatePiece (0);
 assert (!metadata->IsComplete () && !metadata->GetPiece (0).IsComplete ());
 backend->failWrite = true;
 assert (!verified.Dump ({second, 0, 20000, piece.size () - 20000}));
 backend->failWrite = false;
 assert (verified.Dump ({second, 0, 20000, piece.size () - 20000}));
 backend->failRead = true;
 auto before = backend->opens;
 assert (!verified.Load ({first, 0, 0, 20000}));
 assert (!verified.IsVerified ());
 backend->failRead = false;
 assert (verified.Load ({first, 0, 0, 20000}));
 assert (backend->opens > before); // A failed capability is reopened on retry.
 assert (!first->Save (19999, piece.data (), 2));
 assert (!first->Load (SIZE_MAX, piece.data (), 1));
 // Zero-length files must not produce a fragment/open, otherwise a SAF
 // restart would try to acquire a document that was never created.
 auto zeroLayout = Torrent::CreateV1 (
  "zero-length-layout", { { "a.bin", 8192 }, { "empty.bin", 0 }, { "b.bin", 8192 } }, 16384, { digest });
 assert (zeroLayout && zeroLayout->GetNumPieces () == 1);
 auto zeroFragments = zeroLayout->GetPieceFileFragments (0);
 assert (zeroFragments.size () == 2);
 for (const auto & fragment : zeroFragments) assert (fragment.fragmentSize > 0);
 first->Close (); second->Close ();
 assert (backend->handles.empty ());
}

#else
int main () {}
#endif
