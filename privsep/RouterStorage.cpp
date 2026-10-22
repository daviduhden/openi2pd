/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: atomic router-key and RouterInfo storage process.
 * Only four fixed filenames are writable. The worker has no network capability.
 */
#include "RouterStorage.h"
#include "Worker.h"
#include "LinuxSandbox.h"
#include "OpenBSDSandbox.h"
#include <mutex>
#include <limits>
#include <cerrno>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace i2p::privsep
{
namespace
{
struct Fd { int value; ~Fd () { if (value >= 0) ::close (value); } };
const char * FileName (uint8_t id)
{
 switch (static_cast<i2p::data::RouterPersistentFile> (id))
 {
 case i2p::data::RouterPersistentFile::RouterKeys: return "router.keys";
 case i2p::data::RouterPersistentFile::RouterInfo: return "router.info";
 case i2p::data::RouterPersistentFile::NTCP2Keys: return "ntcp2.keys";
 case i2p::data::RouterPersistentFile::SSU2Keys: return "ssu2.keys";
 }
 return nullptr;
}
bool AtomicWrite (int root, uint8_t id, std::string_view bytes)
{
 const char * name = FileName (id);
 if (!name || bytes.empty () || bytes.size () > 32768) return false;
 const std::string temporary = std::string (".openi2pd-") + name + ".new";
 struct stat current {};
 if (::fstatat (root, name, &current, AT_SYMLINK_NOFOLLOW) == 0)
 {
  if (!S_ISREG (current.st_mode) || current.st_uid != ::geteuid () || current.st_nlink != 1) return false;
 }
 else if (errno != ENOENT) return false;
 // A fixed staging slot bounds leftovers. Only remove an owned regular slot;
 // never open/truncate an existing inode that might have gained another link.
 if (::fstatat (root, temporary.c_str (), &current, AT_SYMLINK_NOFOLLOW) == 0)
 {
  if (!S_ISREG (current.st_mode) || current.st_uid != ::geteuid () || current.st_nlink != 1
    || ::unlinkat (root, temporary.c_str (), 0)) return false;
 }
 else if (errno != ENOENT) return false;
 Fd output { ::openat (root, temporary.c_str (), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600) };
 if (output.value < 0) return false;
 size_t done = 0;
 while (done < bytes.size ())
 {
  ssize_t count = ::write (output.value, bytes.data () + done, bytes.size () - done);
  if (count < 0 && errno == EINTR) continue;
  if (count <= 0) { ::unlinkat (root, temporary.c_str (), 0); return false; }
  done += static_cast<size_t> (count);
 }
 if (::fsync (output.value) || ::renameat (root, temporary.c_str (), root, name))
 { ::unlinkat (root, temporary.c_str (), 0); return false; }
 return ::fsync (root) == 0;
}
}
struct RouterStorageClient::Impl
{
 std::mutex mutex;
 ChildProcess child;
 uint32_t next = 0;
 bool ready = false;
 bool Exchange (MessageType type, std::string payload, int fd)
 {
  if (next == std::numeric_limits<uint32_t>::max ()) { ready = false; return false; }
  const auto id = ++next;
  auto & channel = child.ChannelFd ();
  if (!channel.SendWithFd ({type, id, std::move (payload)}, fd, 5000))
  { ready = false; channel.Close (); return false; }
  auto result = channel.ReceiveWithFd (5000);
  if (!result || !*result) { ready = false; channel.Close (); return false; }
  auto & reply = **result;
  // A worker-level write rejection (status '\1', for example ENOSPC or a
  // symlink/ownership check) is a failed operation, not a broken channel.
  // Only a malformed frame/request ID poisons the client permanently.
  const bool valid = reply.fd < 0 && reply.message.type == MessageType::RouterStorageReply
   && reply.message.requestId == id && (reply.message.payload == std::string (1, '\0')
     || reply.message.payload == std::string (1, '\1'));
  if (reply.fd >= 0) ::close (reply.fd);
  if (!valid) { ready = false; channel.Close (); return false; }
  return reply.message.payload == std::string (1, '\0');
 }
};
RouterStorageClient::RouterStorageClient () : m_Impl (std::make_unique<Impl> ()) {}
RouterStorageClient::~RouterStorageClient () { Stop (); }
void RouterStorageClient::Stop ()
{
 std::lock_guard lock (m_Impl->mutex);
 m_Impl->ready = false;
 // Detached OpenBSD workers are owned only through channel EOF: no kill or
 // waitpid after runtime pledge. Linux owns/reaps its regular child.
 m_Impl->child.ChannelFd ().Close ();
#if !defined(__OpenBSD__)
 m_Impl->child.Stop (1000);
#endif
}
bool RouterStorageClient::Start (const std::filesystem::path & root, std::string & error)
{
 std::lock_guard lock (m_Impl->mutex);
 error.clear ();
 if (m_Impl->ready || ::geteuid () == 0) { error = "router storage requires an unprivileged identity"; return false; }
 std::error_code ec;
 auto absolute = std::filesystem::canonical (root, ec);
 if (ec || absolute.string ().size () > 4096) { error = "invalid router storage root"; return false; }
 // Keep O_RDONLY rather than O_PATH: AtomicWrite performs fsync() on the
 // directory descriptor, which an O_PATH descriptor cannot satisfy.
 Fd directory { ::open (absolute.c_str (), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC) };
 struct stat status {};
 if (directory.value < 0 || ::fstat (directory.value, &status) || status.st_uid != ::geteuid () || (status.st_mode & 0022))
 { error = "unsafe router storage root"; return false; }
#if defined(__OpenBSD__)
 auto worker = SpawnDetachedWorkerWithRoot ("router-storage-inherited", directory.value);
 if (!worker) { error = worker.error (); return false; }
 m_Impl->child = ChildProcess::Adopt (-1, std::move (*worker));
 const int passed = -1;
#else
 auto worker = SpawnWorker ("router-storage", {});
 if (!worker) { error = worker.error (); return false; }
 m_Impl->child = std::move (*worker);
 const int passed = directory.value;
#endif
 PayloadWriter setup;
 if (!setup.WritePath (absolute.string ())) { error = "router storage root path exceeds the IPC bound"; return false; }
 setup.WriteU64BE (static_cast<uint64_t> (status.st_dev));
 setup.WriteU64BE (static_cast<uint64_t> (status.st_ino));
 m_Impl->ready = m_Impl->Exchange (MessageType::ConfigureRouterStorage, setup.Data (), passed);
 if (!m_Impl->ready) error = "router storage sandbox/setup failed";
 return m_Impl->ready;
}
bool RouterStorageClient::Write (i2p::data::RouterPersistentFile file, std::span<const uint8_t> bytes)
{
 std::lock_guard lock (m_Impl->mutex);
 if (!m_Impl->ready || !FileName (static_cast<uint8_t> (file)) || bytes.empty () || bytes.size () > 32768) return false;
 std::string request (1, static_cast<char> (file));
 request.append (reinterpret_cast<const char *> (bytes.data ()), bytes.size ());
 return m_Impl->Exchange (MessageType::RouterStorageRequest, std::move (request), -1);
}
int RunRouterStorageWorker (int fd, int inheritedRoot)
{
 Channel channel = Channel::Adopt (fd);
 auto first = channel.ReceiveWithFd (5000);
 if (!first || !*first) return 1;
 auto & config = **first;
 Fd root { inheritedRoot >= 0 ? inheritedRoot : config.fd };
 if (inheritedRoot >= 0 && config.fd >= 0) { ::close (config.fd); return 1; }
#if defined(__OpenBSD__)
 if (inheritedRoot != 4 || config.fd >= 0) return 1;
#endif
 auto peer = channel.PeerUid ();
 struct stat status {};
 if (config.message.type != MessageType::ConfigureRouterStorage || !config.message.requestId
   || !peer || *peer != ::geteuid () || ::geteuid () == 0 || root.value < 0
   || ::fstat (root.value, &status) || !S_ISDIR (status.st_mode)
   || status.st_uid != ::geteuid () || (status.st_mode & 0022)) return 1;
 PayloadReader setup (config.message.payload);
 auto path = setup.ReadString ();
 auto expectedDevice = setup.ReadU64BE ();
 auto expectedInode = setup.ReadU64BE ();
 if (!path || !expectedDevice || !expectedInode || !setup.Done () || path->empty () || path->front () != '/'
   || path->size () > MAX_PATH_SIZE || path->find ('\0') != std::string_view::npos
   || static_cast<uint64_t> (status.st_dev) != *expectedDevice
   || static_cast<uint64_t> (status.st_ino) != *expectedInode) return 1;
 if (root.value <= 3)
 {
  int replacement = ::fcntl (root.value, F_DUPFD_CLOEXEC, 4);
  if (replacement < 0) return 1;
  ::close (root.value); root.value = replacement;
 }
 ::close (0); ::close (1); ::close (2);
#if defined(__linux__) && !defined(__ANDROID__)
 auto profile = GetLinuxSandboxProfile (WorkerClass::Storage);
 profile.restrictFilesystemReads = true;
 profile.writablePaths = {"/proc/self/fd/" + std::to_string (root.value)};
 if (!ApplyLinuxSandbox (profile)) return 1;
#elif defined(__OpenBSD__)
 OpenBSDPolicyConfig policyConfig;
 policyConfig.preopenedStorageIpc = true; policyConfig.dataDir = std::string (*path);
 auto policy = BuildOpenBSDPolicy (OpenBSDWorkerRole::Storage, policyConfig);
 if (!policy || !ApplyOpenBSDPolicy (*policy)) return 1;
#else
 return 1;
#endif
 if (!channel.Send ({MessageType::RouterStorageReply, config.message.requestId, std::string (1, '\0')}, 5000)) return 1;
 uint32_t lastRequest = 0;
 while (true)
 {
  auto incoming = channel.ReceiveWithFd (30000);
  if (!incoming) return 1;
  if (!*incoming) continue;
  auto & frame = **incoming;
  if (frame.fd >= 0) { ::close (frame.fd); return 1; }
  const auto & request = frame.message;
  if (!AllowsOperation (EndpointRole::RouterStorage, request.type)) return 1;
  if (request.type == MessageType::Ping)
  {
   if (!request.payload.empty ()) return 1;
   if (!channel.Send ({MessageType::Pong, request.requestId, {}}, 5000)) return 1;
   continue;
  }
  if (request.type == MessageType::Shutdown)
  {
   if (!request.payload.empty ()) return 1;
   return 0;
  }
  if (request.type != MessageType::RouterStorageRequest || !request.requestId || request.payload.size () < 2 || request.payload.size () > 32769) return 1;
  if (request.requestId <= lastRequest)
  {
   if (!channel.Send ({MessageType::RouterStorageReply, request.requestId, std::string (1, '\1')}, 5000)) return 1;
   continue;
  }
  lastRequest = request.requestId;
  bool success = AtomicWrite (root.value, static_cast<uint8_t> (request.payload.front ()), std::string_view (request.payload).substr (1));
  if (!channel.Send ({MessageType::RouterStorageReply, request.requestId, std::string (1, success ? '\0' : '\1')}, 5000)) return 1;
 }
}
}
