/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: network-denied torrent storage process.
 * Opens descendants by descriptor and serves serialized bounded IO requests.
 */
#include "TorrentStorage.h"
#include "Channel.h"
#include "LinuxSandbox.h"
#include "OpenBSDSandbox.h"
#include <set>
#include <limits>
#include <map>
#ifndef _WIN32
#include <cerrno>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace i2p::privsep
{
bool SafeStoragePath (std::string_view path)
{
 if (path.empty () || path.size () > 4096 || path.front () == '/' || path.back () == '/') return false;
 size_t start = 0;
 while (start < path.size ())
 {
  auto end = path.find ('/', start);
  if (end == std::string_view::npos) end = path.size ();
  auto part = path.substr (start, end - start);
  if (part.empty () || part == "." || part == ".." || part.size () > 255) return false;
  for (unsigned char ch : part) if (ch < 32 || ch == 127 || ch == '\\' || ch == ':') return false;
  start = end + 1;
 }
 return true;
}
#ifndef _WIN32
namespace
{
 struct Fd { int fd; ~Fd () { if (fd >= 0) ::close (fd); } };
 struct File { int fd; uint64_t length; bool writable; };
 // Walks only existing owned directories. The returned descriptor pins the
 // parent even if its pathname changes; final operations never follow links.
 int OpenParent (int root, std::string_view path, std::string & leaf)
 {
  if (!SafeStoragePath (path)) return -1;
  Fd parent { ::dup (root) };
  if (parent.fd < 0) return -1;
  size_t start = 0;
  while (true)
  {
   auto slash = path.find ('/', start);
   std::string part (path.substr (start, slash == std::string_view::npos ? path.size () - start : slash - start));
   if (slash == std::string_view::npos)
   { leaf = std::move (part); int result = parent.fd; parent.fd = -1; return result; }
   Fd child { ::openat (parent.fd, part.c_str (), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC) };
   struct stat status {};
   if (child.fd < 0 || ::fstat (child.fd, &status) || status.st_uid != ::geteuid () || (status.st_mode & 0022)) return -1;
   ::close (parent.fd); parent.fd = child.fd; child.fd = -1; start = slash + 1;
  }
 }
 bool SameFile (const struct stat & left, const struct stat & right)
 { return left.st_dev == right.st_dev && left.st_ino == right.st_ino; }
 bool ValidPayload (const struct stat & status, uint64_t length)
 {
  return S_ISREG (status.st_mode) && status.st_uid == ::geteuid () && status.st_size >= 0
   && static_cast<uint64_t> (status.st_size) == length;
 }
 bool PromotePayload (int root, std::string_view source, std::string_view target, uint64_t length)
 {
  if (source == target) return false;
  std::string from, to;
  Fd sourceParent { OpenParent (root, source, from) };
  int sourceError = errno;
  Fd targetParent { OpenParent (root, target, to) };
  if (targetParent.fd < 0) return false;
  if (sourceParent.fd < 0)
  {
   if (sourceError != ENOENT) return false;
   struct stat completed {};
   return ::fstatat (targetParent.fd, to.c_str (), &completed, AT_SYMLINK_NOFOLLOW) == 0
    && ValidPayload (completed, length) && completed.st_nlink == 1;
  }
  Fd file { ::openat (sourceParent.fd, from.c_str (), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC) };
  struct stat original {}, current {}, destination {};
  if (file.fd < 0 && errno == ENOENT)
   return ::fstatat (targetParent.fd, to.c_str (), &destination, AT_SYMLINK_NOFOLLOW) == 0
    && ValidPayload (destination, length) && destination.st_nlink == 1;
  if (file.fd < 0 || ::fstat (file.fd, &original) || !ValidPayload (original, length)
    || original.st_nlink < 1 || original.st_nlink > 2 || ::fsync (file.fd)) return false;
  // linkat has atomic no-replace semantics on both Linux and OpenBSD. An
  // interrupted link/unlink transaction may be resumed only for this same inode.
  if (::linkat (sourceParent.fd, from.c_str (), targetParent.fd, to.c_str (), 0) && errno != EEXIST) return false;
  if (::fstatat (targetParent.fd, to.c_str (), &destination, AT_SYMLINK_NOFOLLOW)
    || !SameFile (original, destination) || !ValidPayload (destination, length)
    || destination.st_nlink != 2
    || ::fstatat (sourceParent.fd, from.c_str (), &current, AT_SYMLINK_NOFOLLOW)
    || !SameFile (original, current)) return false;
  // Commit the destination entry before removing staging, then sync removal.
  if (::fsync (targetParent.fd) || ::unlinkat (sourceParent.fd, from.c_str (), 0)) return false;
  return ::fsync (sourceParent.fd) == 0;
 }
 bool RemovePayload (int root, std::string_view path, uint64_t length, bool directory)
 {
  std::string leaf; Fd parent { OpenParent (root, path, leaf) };
  if (parent.fd < 0) return errno == ENOENT;
  struct stat status {};
  if (::fstatat (parent.fd, leaf.c_str (), &status, AT_SYMLINK_NOFOLLOW)) return errno == ENOENT;
  if (status.st_uid != ::geteuid ()) return false;
  if (directory ? !S_ISDIR (status.st_mode) : (!ValidPayload (status, length) || status.st_nlink != 1)) return false;
  // Directories must be empty. Never traverse recursively on a delete request.
  return ::unlinkat (parent.fd, leaf.c_str (), directory ? AT_REMOVEDIR : 0) == 0 && ::fsync (parent.fd) == 0;
 }
 constexpr uint64_t MetadataLimit = 8 * 1024 * 1024;
 bool SafeMetadata (std::string_view path)
 {
  if (!SafeStoragePath (path)) return false;
  constexpr std::string_view prefix = ".openi2pd-state/";
  if (path.starts_with (prefix))
  {
   auto leaf = path.substr (prefix.size ());
   if (leaf.size () != 46 || !leaf.ends_with (".state")) return false;
   for (auto ch : leaf.substr (0, 40)) if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return false;
   return true;
  }
  return path.find ('/') == std::string_view::npos
   && ((path.size () > 8 && path.ends_with (".torrent")) || (path.size () > 7 && path.ends_with (".resume")) || (path.size () > 4 && path.ends_with (".dht")));
 }
 struct PendingMetadata
 {
  int parent = -1, file = -1;
  std::string target, temporary;
  uint64_t length = 0, written = 0;
  ~PendingMetadata () { Clear (); }
  void Clear ()
  {
   if (file >= 0) ::close (file);
   if (parent >= 0)
   {
    if (!temporary.empty ()) ::unlinkat (parent, temporary.c_str (), 0);
    ::close (parent);
   }
   file = parent = -1; temporary.clear (); target.clear (); length = written = 0;
  }
  bool SafeTarget () const
  {
   struct stat status {};
   if (::fstatat (parent, target.c_str (), &status, AT_SYMLINK_NOFOLLOW)) return errno == ENOENT;
   return S_ISREG (status.st_mode) && status.st_uid == ::geteuid () && status.st_nlink == 1;
  }
  bool Begin (int root, std::string_view path, uint64_t size, bool authorizedDHT = false)
  {
   Clear ();
   if ((!SafeMetadata (path) && !authorizedDHT) || size > MetadataLimit) return false;
   if (path.starts_with (".openi2pd-state/"))
    if (::mkdirat (root, ".openi2pd-state", 0700) && errno != EEXIST) return false;
   parent = OpenParent (root, path, target);
   if (parent < 0 || !SafeTarget ()) { Clear (); return false; }
   // A killed worker can leave staging behind. A fixed eight-slot namespace
   // bounds orphan storage to 64 MiB per metadata directory, independent of
   // restart count; never overwrite an unknown or concurrently active slot.
   for (unsigned slot = 0; slot < 8; ++slot)
   {
    temporary = ".openi2pd-pending-" + std::to_string (slot);
    file = ::openat (parent, temporary.c_str (), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (file >= 0 || errno != EEXIST) break;
   }
   if (file < 0) { temporary.clear (); Clear (); return false; }
   length = size; return true;
  }
  bool Chunk (uint64_t offset, std::string_view bytes)
  {
   if (file < 0 || offset != written || bytes.size () > TorrentStorageBlock || bytes.size () > length - written) return false;
   size_t done = 0;
   while (done < bytes.size ())
   {
    auto count = ::write (file, bytes.data () + done, bytes.size () - done);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) return false;
    done += static_cast<size_t> (count);
   }
   written += done; return true;
  }
  bool Commit ()
  {
   bool ok = file >= 0 && written == length && ::fsync (file) == 0 && SafeTarget ()
    && ::renameat (parent, temporary.c_str (), parent, target.c_str ()) == 0;
   if (ok) { temporary.clear (); ok = ::fsync (parent) == 0; }
   Clear (); return ok;
  }
 };
 bool RemoveMetadataFile (int root, std::string_view path)
 {
  if (!SafeMetadata (path)) return false;
  std::string leaf; Fd parent { OpenParent (root, path, leaf) };
  if (parent.fd < 0) return errno == ENOENT;
  struct stat status {};
  if (::fstatat (parent.fd, leaf.c_str (), &status, AT_SYMLINK_NOFOLLOW)) return errno == ENOENT;
  if (!S_ISREG (status.st_mode) || status.st_uid != ::geteuid () || status.st_nlink != 1
    || status.st_size < 0 || static_cast<uint64_t> (status.st_size) > MetadataLimit) return false;
  return ::unlinkat (parent.fd, leaf.c_str (), 0) == 0 && ::fsync (parent.fd) == 0;
 }
 int OpenPayload (int root, std::string_view path, uint64_t length, bool create)
 {
  if (!SafeStoragePath (path)) return -1;
  Fd parent { ::dup (root) };
  if (parent.fd < 0) return -1;
  size_t start = 0;
  while (true)
  {
   auto slash = path.find ('/', start);
   std::string part (path.substr (start, slash == std::string_view::npos ? path.size () - start : slash - start));
   if (slash == std::string_view::npos)
   {
    Fd file { ::openat (parent.fd, part.c_str (), (create ? O_RDWR | O_CREAT : O_RDONLY) | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0600) };
    struct stat status {};
    if (file.fd < 0 || ::fstat (file.fd, &status) || !S_ISREG (status.st_mode) || status.st_nlink != 1
      || status.st_uid != ::geteuid () || status.st_size < 0 || static_cast<uint64_t> (status.st_size) > length
      || (!create && static_cast<uint64_t> (status.st_size) != length))
     return -1;
    // Existing contents are retained; sparse extension does not mark pieces verified.
    if (create && static_cast<uint64_t> (status.st_size) < length && ::ftruncate (file.fd, static_cast<off_t> (length))) return -1;
    int result = file.fd; file.fd = -1; return result;
   }
   if (create && ::mkdirat (parent.fd, part.c_str (), 0700) && errno != EEXIST) return -1;
   int child = ::openat (parent.fd, part.c_str (), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
   if (child < 0) return -1;
   struct stat directory {};
   if (::fstat (child, &directory) || directory.st_uid != ::geteuid () || (directory.st_mode & 0022))
   { ::close (child); return -1; }
   ::close (parent.fd); parent.fd = child; start = slash + 1;
  }
 }
}
int RunTorrentStorageWorker (int fd, int inheritedRoot)
{
 Channel channel = Channel::Adopt (fd);
 auto first = channel.ReceiveWithFd (5000);
 if (!first || !*first) return 1;
 auto & config = **first;
 const bool supervised = config.message.type == MessageType::ConfigureTorrentStorageSupervised;
#if defined(__OpenBSD__)
 // The supervisor inherited the broker data root through exec, not sendfd
 // (which permits only sockets, pipes and regular files under pledge).
 if (!supervised || inheritedRoot != 4 || config.fd >= 0)
 { if (config.fd >= 0) ::close (config.fd); return 1; }
#elif defined(__linux__) && !defined(__ANDROID__)
 // Linux delivers the already-opened delegated directory as one SCM_RIGHTS
 // descriptor and must not use the OpenBSD broker configuration.
 if (supervised || inheritedRoot >= 0 || config.fd < 0) return 1;
#endif
 Fd root { inheritedRoot >= 0 ? inheritedRoot : config.fd };
 if (inheritedRoot >= 0 && config.fd >= 0) { ::close (config.fd); return 1; }
 if (config.message.requestId == 0 || root.fd < 0
   || (!supervised && config.message.type != MessageType::ConfigureTorrentStorage)) return 1;
 struct stat status {};
 if (::fstat (root.fd, &status) || !S_ISDIR (status.st_mode) || ::geteuid () == 0
   || status.st_uid != ::geteuid () || (status.st_mode & 0022)) return 1;
 auto peer = channel.PeerUid ();
 if (!peer || *peer != ::geteuid ()) return 1;
 if (root.fd <= 3)
 {
  int relocated = ::fcntl (root.fd, F_DUPFD_CLOEXEC, 4);
  if (relocated < 0) return 1;
  ::close (root.fd); root.fd = relocated;
 }
 PayloadReader setup (config.message.payload);
 auto rootPath = setup.ReadString ();
 if (!rootPath || rootPath->empty () || rootPath->front () != '/'
   || rootPath->size () > MAX_PATH_SIZE || rootPath->find ('\0') != std::string_view::npos) return 1;
 if (supervised)
 {
  // The broker passed its immutable data root at fd4. Walk the delegated
  // relative path here, before applying the policy, and replace fd4 with the
  // validated subdirectory. The broker itself must not openat() after pledge.
  auto relative = setup.ReadString ();
  auto expectedDevice = setup.ReadU64BE ();
  auto expectedInode = setup.ReadU64BE ();
  if (!relative || !expectedDevice || !expectedInode || !setup.Done ()) return 1;
  if (*relative != "." && !SafeStoragePath (*relative)) return 1;
  Fd checked { ::dup (root.fd) };
  if (checked.fd < 0) return 1;
  if (*relative != ".")
  {
   std::string leaf;
   Fd parent { OpenParent (checked.fd, *relative, leaf) };
   if (parent.fd < 0) return 1;
   int child = ::openat (parent.fd, leaf.c_str (), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
   if (child < 0) return 1;
   ::close (checked.fd);
   checked.fd = child;
  }
  struct stat delegated {};
  if (::fstat (checked.fd, &delegated) || !S_ISDIR (delegated.st_mode)
    || delegated.st_uid != ::geteuid () || (delegated.st_mode & 0022)
    || static_cast<uint64_t> (delegated.st_dev) != *expectedDevice
    || static_cast<uint64_t> (delegated.st_ino) != *expectedInode) return 1;
  // The pathname passed to unveil must still resolve to the capability we
  // just walked. Any replacement between the broker and this check fails
  // closed before the worker exposes a path-based policy.
  Fd named { ::open (std::string (*rootPath).c_str (), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC) };
  struct stat namedStatus {};
  if (named.fd < 0 || ::fstat (named.fd, &namedStatus)
    || namedStatus.st_dev != delegated.st_dev || namedStatus.st_ino != delegated.st_ino
    || namedStatus.st_uid != delegated.st_uid || (namedStatus.st_mode & 0022)) return 1;
  ::close (root.fd);
  root.fd = checked.fd;
  checked.fd = -1;
 }
 else
 {
  auto expectedDevice = setup.ReadU64BE ();
  auto expectedInode = setup.ReadU64BE ();
  if (!expectedDevice || !expectedInode || !setup.Done ()
    || static_cast<uint64_t> (status.st_dev) != *expectedDevice
    || static_cast<uint64_t> (status.st_ino) != *expectedInode) return 1;
 }
 // Ensure the validated root survives the stdio cleanup below even when
 // dup()/openat() reused descriptors 0..3 during the pre-policy walk.
 if (root.fd <= 3)
 {
  int relocated = ::fcntl (root.fd, F_DUPFD_CLOEXEC, 4);
  if (relocated < 0) return 1;
  ::close (root.fd); root.fd = relocated;
 }
 // Never retain inherited terminal/log descriptors: they could be sockets.
 ::close (0); ::close (1); ::close (2);
#if defined(__linux__) && !defined(__ANDROID__)
 auto profile = GetLinuxSandboxProfile (WorkerClass::Storage);
 profile.restrictFilesystemReads = true;
 // Bind Landlock to the actual delegated directory, not a replaceable pathname.
 profile.writablePaths = { "/proc/self/fd/" + std::to_string (root.fd) };
 if (!ApplyLinuxSandbox (profile)) return 1;
#elif defined(__OpenBSD__)
 // Descriptor traversal remains the authority; unveil also limits pathname IO.
 OpenBSDPolicyConfig policyConfig;
 policyConfig.preopenedStorageIpc = true;
 policyConfig.dataDir = std::string (*rootPath);
 auto policy = BuildOpenBSDPolicy (OpenBSDWorkerRole::Storage, policyConfig);
 if (!policy || !ApplyOpenBSDPolicy (*policy)) return 1;
#else
 return 1; // No sandbox backend: never silently run an unconfined writer.
#endif
 if (!channel.Send ({ MessageType::TorrentStorageReply, config.message.requestId, std::string (1, '\0') }, 5000)) return 1;
 PendingMetadata metadata;
 std::set<std::string, std::less<>> authorizedDHT;
 std::map<uint32_t, File> files;
 uint32_t next = 0;
 uint32_t lastRequest = 0;
 struct Cleanup { std::map<uint32_t, File> & files; ~Cleanup () { for (auto & [id, file] : files) ::close (file.fd); } } cleanup { files };
 while (true)
 {
  auto received = channel.ReceiveWithFd (30000);
  if (!received) return 1;
  if (!*received) continue;
  auto & frame = **received;
  if (frame.fd >= 0) { ::close (frame.fd); return 1; }
  const auto & request = frame.message;
  if (!AllowsOperation (EndpointRole::TorrentStorage, request.type)) return 1;
  if (request.type == MessageType::Ping)
  {
   if (!request.payload.empty ()) return 1;
   if (!channel.Send ({ MessageType::Pong, request.requestId, {} }, 5000)) return 1;
   continue;
  }
  if (request.type == MessageType::Shutdown)
  {
   if (!request.payload.empty ()) return 1;
   return 0;
  }
  if (request.type != MessageType::TorrentStorageRequest || !request.requestId) return 1;
  // The worker is the security boundary: a duplicate or replayed request
  // must not re-execute a mutation. The reply is an operation failure so the
  // channel remains usable for a later legitimate request.
  if (request.requestId <= lastRequest)
  {
   if (!channel.Send ({ MessageType::Error, request.requestId,
     EncodeError (MessageType::TorrentStorageRequest, ErrorCode::BadField, "stale or replayed request id") }, 5000)) return 1;
   continue;
  }
  lastRequest = request.requestId;
  PayloadReader reader (request.payload);
  auto operation = reader.ReadU8 ();
  bool success = false;
  std::string result;
  if (operation && *operation == static_cast<uint8_t> (StorageOperation::Register))
  {
   auto path = reader.ReadString (); auto length = reader.ReadU64BE (); auto create = reader.ReadU8 ();
   if (path && length && create && *create <= 1 && reader.Done () && *length <= TorrentStorageFileLimit
     && *length <= static_cast<uint64_t> (std::numeric_limits<off_t>::max ())
     && files.size () < TorrentStorageHandles && next != std::numeric_limits<uint32_t>::max ())
   {
    int opened = OpenPayload (root.fd, *path, *length, *create != 0);
    if (opened >= 0)
    {
     auto id = ++next; files.emplace (id, File { opened, *length, *create != 0 });
     PayloadWriter writer; writer.WriteU32 (id); result = writer.Data (); success = true;
    }
   }
  }
  else if (operation && *operation == static_cast<uint8_t> (StorageOperation::AuthorizeDHTMetadata))
  {
   auto path = reader.ReadString ();
   // This operation is sent only from trusted tunnel configuration, never
   // from peer filenames. It grants one flat DHT state leaf, not a directory.
   if (path && reader.Done () && SafeStoragePath (*path) && path->find ('/') == std::string_view::npos
     && !path->starts_with (".openi2pd") && !path->starts_with (".i2pd")
     && (authorizedDHT.contains (*path) || authorizedDHT.size () < 16))
   { authorizedDHT.emplace (*path); success = true; }
  }
  else if (operation && *operation == static_cast<uint8_t> (StorageOperation::MetadataOpen))
  {
   auto path = reader.ReadString ();
   if (path && reader.Done () && (SafeMetadata (*path) || authorizedDHT.contains (*path)) && files.size () < TorrentStorageHandles
     && next != std::numeric_limits<uint32_t>::max ())
   {
    std::string leaf; Fd parent { OpenParent (root.fd, *path, leaf) };
    Fd opened { parent.fd >= 0 ? ::openat (parent.fd, leaf.c_str (), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC) : -1 };
    struct stat status {};
    if (opened.fd >= 0 && !::fstat (opened.fd, &status) && S_ISREG (status.st_mode)
      && status.st_uid == ::geteuid () && status.st_nlink == 1 && status.st_size >= 0
      && static_cast<uint64_t> (status.st_size) <= MetadataLimit)
    {
     const auto id = ++next;
     files.emplace (id, File { opened.fd, static_cast<uint64_t> (status.st_size), false }); opened.fd = -1;
     PayloadWriter writer; writer.WriteU32 (id); writer.WriteU64BE (static_cast<uint64_t> (status.st_size));
     result = writer.Data (); success = true;
    }
   }
  }
  else if (operation && *operation >= static_cast<uint8_t> (StorageOperation::MetadataBegin)
    && *operation <= static_cast<uint8_t> (StorageOperation::MetadataRemove))
  {
   if (*operation == static_cast<uint8_t> (StorageOperation::MetadataBegin))
   {
    auto path = reader.ReadString (); auto length = reader.ReadU64BE ();
    if (path && length && reader.Done ()) success = metadata.Begin (root.fd, *path, *length, authorizedDHT.contains (*path));
   }
   else if (*operation == static_cast<uint8_t> (StorageOperation::MetadataChunk))
   {
    auto offset = reader.ReadU64BE ();
    constexpr size_t header = 1 + 8;
    if (offset && request.payload.size () >= header)
     success = metadata.Chunk (*offset, std::string_view (request.payload).substr (header));
   }
   else if (*operation == static_cast<uint8_t> (StorageOperation::MetadataCommit) && reader.Done ()) success = metadata.Commit ();
   else if (*operation == static_cast<uint8_t> (StorageOperation::MetadataAbort) && reader.Done ()) { metadata.Clear (); success = true; }
   else if (*operation == static_cast<uint8_t> (StorageOperation::MetadataRemove))
   { auto path = reader.ReadString (); if (path && reader.Done ()) success = RemoveMetadataFile (root.fd, *path); }
   if (!success) metadata.Clear ();
  }
  else if (operation && (*operation == static_cast<uint8_t> (StorageOperation::Promote)
    || *operation == static_cast<uint8_t> (StorageOperation::Remove)
    || *operation == static_cast<uint8_t> (StorageOperation::RemoveDirectory)))
  {
   auto path = reader.ReadString ();
   if (*operation == static_cast<uint8_t> (StorageOperation::Promote))
   {
    auto target = reader.ReadString (); auto length = reader.ReadU64BE ();
    if (path && target && length && *length <= TorrentStorageFileLimit && reader.Done ())
     success = PromotePayload (root.fd, *path, *target, *length);
   }
   else if (*operation == static_cast<uint8_t> (StorageOperation::Remove))
   {
    auto length = reader.ReadU64BE ();
    if (path && length && *length <= TorrentStorageFileLimit && reader.Done ())
     success = RemovePayload (root.fd, *path, *length, false);
   }
   else if (path && reader.Done ()) success = RemovePayload (root.fd, *path, 0, true);
  }
  else if (operation)
  {
   auto id = reader.ReadU32 ();
   auto found = id ? files.find (*id) : files.end ();
   if (found != files.end ())
   {
    if (*operation == static_cast<uint8_t> (StorageOperation::Release) && reader.Done ())
    { ::close (found->second.fd); files.erase (found); success = true; }
    else
    {
     auto offset = reader.ReadU64BE (); auto length = reader.ReadU32 ();
     const bool writing = *operation == static_cast<uint8_t> (StorageOperation::Write);
     const bool reading = *operation == static_cast<uint8_t> (StorageOperation::Read);
     // Data follows the fixed 17-byte request header; never allocate from unchecked lengths.
     constexpr size_t header = 1 + 4 + 8 + 4;
     if (offset && length && *length <= TorrentStorageBlock && *offset <= found->second.length
       && *length <= found->second.length - *offset
       && ((reading && reader.Done ())
         || (writing && found->second.writable && request.payload.size () == header + *length)))
     {
      result.resize (reading ? *length : 0);
      size_t done = 0;
      while (done < *length)
      {
       ssize_t count = writing
        ? ::pwrite (found->second.fd, request.payload.data () + header + done, *length - done, static_cast<off_t> (*offset + done))
        : ::pread (found->second.fd, result.data () + done, *length - done, static_cast<off_t> (*offset + done));
       if (count < 0 && errno == EINTR) continue;
       if (count <= 0) break;
       done += static_cast<size_t> (count);
      }
      success = done == *length;
      // A successful write acknowledgement is durable, not merely copied into page cache.
      if (success && writing && ::fsync (found->second.fd)) success = false;
     }
    }
   }
  }
  if (!success)
  {
   if (!channel.Send ({ MessageType::Error, request.requestId,
     EncodeError (MessageType::TorrentStorageRequest, ErrorCode::Internal, "storage operation rejected or failed") }, 5000)) return 1;
   continue;
  }
  result.insert (result.begin (), '\0');
  if (!channel.Send ({ MessageType::TorrentStorageReply, request.requestId, std::move (result) }, 5000)) return 1;
 }
}
#else
int RunTorrentStorageWorker (int, int) { return 1; }
#endif
}
