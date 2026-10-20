/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: isolated torrent storage regression.
 * Exercises descriptor-rooted IO, handle/range validation and denied networking.
 * This executable is intended only for manually dispatched CI.
 */
#include "../libi2pd_client/TorrentStorageWorker.h"
#include "../libi2pd_client/Torrents.h"
#include "../privsep/TorrentStorage.h"
#include "../privsep/RunWorker.h"
#include <array>
#include <cassert>
#include <filesystem>
#include <string>
#if defined(__linux__) && !defined(__ANDROID__)
#include "../privsep/LinuxSandbox.h"
#include <fcntl.h>
#include <cerrno>
#include <sys/syscall.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
int main (int argc, char ** argv)
{
 if (argc == 4 && std::string_view (argv[1]) == "--privsep-worker")
  return i2p::privsep::RunWorker (argv[2], argv[3]);
 using namespace i2p::privsep;
 assert (SafeStoragePath ("payload/files/part.bin"));
 for (auto path : { "", "/etc/passwd", "../outside", "a/../outside", "a//b", "a/./b", "a/", "a\\b", "C:escape" })
  assert (!SafeStoragePath (path));
 assert (!SafeStoragePath (std::string ("a\0b", 3)));
#if defined(__linux__) && !defined(__ANDROID__)
 if (::geteuid () == 0) return 77; // The production worker deliberately refuses root.
 auto root = std::filesystem::temp_directory_path () / ("openi2pd-storage-worker-" + std::to_string (::getpid ()));
 std::filesystem::create_directory (root);
 struct Cleanup { std::filesystem::path path; ~Cleanup () { std::error_code ec; std::filesystem::remove_all (path, ec); } } cleanup { root };
 i2p::torrents::TorrentStorageWorker worker;
 std::string error;
 auto support = ProbeLinuxIsolation ();
 if (!support.filesystem || !support.seccompFilter) return 77; // Explicitly unsupported kernels only.
 assert (worker.Start (root, error));
 auto handle = worker.Register ("nested/piece.bin", 8, error, true);
 assert (handle);
 assert (!worker.Register ("missing/new.bin", 8, error));
 assert (!std::filesystem::exists (root / "missing"));
 assert (!worker.Register ("nested/piece.bin", 9, error));
 assert (std::filesystem::file_size (root / "nested/piece.bin") == 8);
 auto reader = worker.Register ("nested/piece.bin", 8, error);
 assert (reader);
 std::array<uint8_t, 4> sent { 1, 2, 3, 4 }, received {};
 assert (!worker.Write (reader, 2, sent, error));
 assert (worker.Write (handle, 2, sent, error));
 assert (worker.Read (reader, 2, received, error) && sent == received);
 assert (worker.Release (reader, error));
 assert (worker.Read (handle, 2, received, error) && sent == received);
#ifndef NO_TORRENTS
 {
  i2p::torrents::TorrentFile file (root / "nested/piece.bin", 8);
  file.SetRootDir (root);
  file.SetIsPart (false);
  auto backend = std::shared_ptr<i2p::torrents::TorrentStorageWorker> (&worker, [] (auto *) {});
  file.SetStorageWorker (backend, root);
  assert (file.Load (2, received.data (), received.size ()) && received == sent);
  assert (file.Save (0, sent.data (), sent.size ()));
  assert (file.Load (0, received.data (), received.size ()) && received == sent);
 }
#endif

 assert (!worker.Write (handle, 6, sent, error));
 assert (!worker.Read (handle, UINT64_MAX, received, error));
 assert (!worker.Read (handle + 1, 0, received, error));
 assert (!worker.Register ("../escape", 1, error));
 std::filesystem::create_symlink (root.parent_path (), root / "redirect");
 assert (!worker.Register ("redirect/escape", 1, error, true));
 // An existing hardlink must not allow mutation of an inode outside the root.
 auto outside = root.parent_path () / (root.filename ().string () + "-outside");
 { auto fd = ::open (outside.c_str (), O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC, 0600); assert (fd >= 0); ::close (fd); }
 std::filesystem::create_hard_link (outside, root / "hardlink");
 assert (!worker.Register ("hardlink", 1, error, true));
 std::filesystem::remove (outside);
 assert (worker.Release (handle, error));
 assert (!worker.Read (handle, 0, received, error));
 worker.Stop ();
 assert (!worker.Read (handle, 0, received, error));
 auto pid = ::fork (); assert (pid >= 0);
 if (!pid)
 {
  auto profile = GetLinuxSandboxProfile (WorkerClass::Storage);
  profile.restrictFilesystemReads = true;
  profile.writablePaths = { root.string () };
  if (!ApplyLinuxSandbox (profile)) ::_exit (1);
  errno = 0;
  auto forbidden = ::open ("/etc/passwd", O_RDONLY | O_CLOEXEC);
  if (forbidden >= 0) { ::close (forbidden); ::_exit (1); }
  if (errno != EACCES) ::_exit (1);
  auto socket = ::socket (AF_INET, SOCK_STREAM, 0);
  if (socket >= 0) { ::close (socket); ::_exit (1); }
  socket = ::socket (AF_INET6, SOCK_DGRAM, 0);
  if (socket >= 0) { ::close (socket); ::_exit (1); }
#ifdef SYS_io_uring_setup
  errno = 0;
  if (::syscall (SYS_io_uring_setup, 1, nullptr) != -1 || errno != EACCES) ::_exit (1);
#endif
#ifdef SYS_pidfd_getfd
  errno = 0;
  if (::syscall (SYS_pidfd_getfd, -1, -1, 0) != -1 || errno != EACCES) ::_exit (1);
#endif
#if defined(__x86_64__)
  errno = 0;
  if (::syscall (SYS_socket | 0x40000000U, AF_INET, SOCK_STREAM, 0) != -1 || errno != EACCES) ::_exit (1);
#endif
  ::_exit (0);
 }
 int status = 0; assert (::waitpid (pid, &status, 0) == pid);
 assert (WIFEXITED (status) && WEXITSTATUS (status) == 0);
 return 0;
#else
 return 77;
#endif
}
