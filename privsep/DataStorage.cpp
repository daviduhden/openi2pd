/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: hash-named runtime artifact storage.
 *
 * Implements the data-storage worker, which owns the three hash-named
 * artifact directories and has no network capability, plus its
 * generation-fenced client. Writes and removals use the same validated
 * atomic replacement as the fixed-file router storage: an
 * owner/mode/nlink checked target, a bounded fixed staging slot, fsync
 * of the file and its directory, and no pathname resolution through a
 * symlink. Reads stay in the core, so this process never becomes a read
 * oracle for the data directory.
 */

#include "DataStorage.h"
#include "Credentials.h"
#include "Protocol.h"

#ifndef _WIN32

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <limits>
#include <mutex>
#include <optional>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

#include "LinuxSandbox.h"
#include "OpenBSDSandbox.h"
#include "Worker.h"

namespace i2p::privsep
{
namespace
{
	constexpr unsigned STAGING_SLOTS = 8;
	constexpr int REQUEST_TIMEOUT_MS = 5000;

	/**
	 * Canonical name codecs.
	 *
	 * These deliberately do not call the core library: this archive must
	 * stay linkable without it, and the worker must not inherit core
	 * state. The alphabets are the I2P substitutions (base64 with '-' and
	 * '~') and the lower-case base32 alphabet. `test-datastorage-protocol`
	 * pins these helpers against libi2pd's Base codec so the two cannot
	 * drift.
	 */
	constexpr char BASE64_TABLE[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-~";
	constexpr char BASE32_TABLE[] = "abcdefghijklmnopqrstuvwxyz234567";

	constexpr std::array<int8_t, 256> BuildReverse (const char * table, size_t size)
	{
		std::array<int8_t, 256> reverse { };
		reverse.fill (-1);
		for (size_t i = 0; i < size; ++i) reverse[static_cast<uint8_t> (table[i])] = static_cast<int8_t> (i);
		return reverse;
	}
	constexpr auto BASE64_REVERSE = BuildReverse (BASE64_TABLE, 64);
	constexpr auto BASE32_REVERSE = BuildReverse (BASE32_TABLE, 32);

	std::string EncodeBase64 (const uint8_t * in, size_t size)
	{
		// Encode each three-byte group as four six-bit symbols. The final
		// one- or two-byte group uses the I2P alphabet and '=' padding.
		std::string out;
		out.reserve ((size + 2) / 3 * 4);
		size_t pos = 0;
		for (; pos + 2 < size; pos += 3)
		{
			const uint32_t value
				= (static_cast<uint32_t> (in[pos]) << 16) | (static_cast<uint32_t> (in[pos + 1]) << 8) | in[pos + 2];
			out.push_back (BASE64_TABLE[(value >> 18) & 0x3f]);
			out.push_back (BASE64_TABLE[(value >> 12) & 0x3f]);
			out.push_back (BASE64_TABLE[(value >> 6) & 0x3f]);
			out.push_back (BASE64_TABLE[value & 0x3f]);
		}
		if (pos + 1 == size)
		{
			const uint32_t value = static_cast<uint32_t> (in[pos]) << 16;
			out.push_back (BASE64_TABLE[(value >> 18) & 0x3f]);
			out.push_back (BASE64_TABLE[(value >> 12) & 0x3f]);
			out.append ("==");
		}
		else if (pos + 2 == size)
		{
			const uint32_t value = (static_cast<uint32_t> (in[pos]) << 16) | (static_cast<uint32_t> (in[pos + 1]) << 8);
			out.push_back (BASE64_TABLE[(value >> 18) & 0x3f]);
			out.push_back (BASE64_TABLE[(value >> 12) & 0x3f]);
			out.push_back (BASE64_TABLE[(value >> 6) & 0x3f]);
			out.push_back ('=');
		}
		return out;
	}

	/** Decode a padded I2P-base64 string; returns the byte count or -1. */
	int DecodeBase64 (std::string_view text, uint8_t * out, size_t outSize)
	{
		if (text.empty () || text.size () % 4 != 0) return -1;
		size_t padding = 0;
		if (text.ends_with ("=="))
			padding = 2;
		else if (text.back () == '=')
			padding = 1;
		const size_t encoded = text.size () - padding;
		for (size_t i = 0; i < encoded; ++i)
			if (BASE64_REVERSE[static_cast<uint8_t> (text[i])] < 0) return -1;
		for (size_t i = encoded; i < text.size (); ++i)
			if (text[i] != '=') return -1;
		const size_t decoded = text.size () / 4 * 3 - padding;
		if (decoded > outSize) return -1;
		size_t written = 0;
		for (size_t i = 0; i < text.size (); i += 4)
		{
			uint32_t value = 0;
			for (size_t j = 0; j < 4; ++j)
			{
				const auto ch = static_cast<uint8_t> (text[i + j]);
				const int part = ch == '=' ? 0 : BASE64_REVERSE[ch];
				if (part < 0) return -1;
				value = (value << 6) | static_cast<uint32_t> (part);
			}
			if (written < decoded) out[written++] = static_cast<uint8_t> (value >> 16);
			if (written < decoded) out[written++] = static_cast<uint8_t> (value >> 8);
			if (written < decoded) out[written++] = static_cast<uint8_t> (value);
		}
		return static_cast<int> (written);
	}

	std::string EncodeBase32 (const uint8_t * in, size_t size)
	{
		if (!size) return { };
		// Emit five-bit symbols from the input bit stream. A partial final
		// symbol is zero-filled; this name format has no '=' padding.
		std::string out;
		out.reserve ((size * 8 + 4) / 5);
		size_t pos = 1;
		unsigned bits = 8;
		unsigned tmp = in[0];
		while (bits > 0 || pos < size)
		{
			if (bits < 5)
			{
				if (pos < size)
				{
					tmp <<= 8;
					tmp |= in[pos] & 0xff;
					++pos;
					bits += 8;
				}
				else
				{
					tmp <<= (5 - bits);
					bits = 5;
				}
			}
			bits -= 5;
			out.push_back (BASE32_TABLE[(tmp >> bits) & 0x1f]);
		}
		return out;
	}

	/** Decode a lower-case base32 string; returns the byte count or -1. */
	int DecodeBase32 (std::string_view text, uint8_t * out, size_t outSize)
	{
		unsigned tmp = 0, bits = 0;
		size_t written = 0;
		for (auto ch : text)
		{
			const int value = BASE32_REVERSE[static_cast<uint8_t> (ch)];
			if (value < 0) return -1;
			tmp |= static_cast<unsigned> (value);
			bits += 5;
			if (bits >= 8)
			{
				bits -= 8;
				if (written >= outSize) return -1;
				out[written++] = static_cast<uint8_t> ((tmp >> bits) & 0xff);
			}
			tmp <<= 5;
		}
		return static_cast<int> (written);
	}

	/// Owning descriptor for one directory level of an artifact path.
	/// `Release()` transfers the number to the caller that keeps it (an
	/// opened kind or shard directory, or the worker's root), so ownership
	/// moves explicitly instead of a borrowed `int` being copied.
	struct Fd
	{
		int value = -1;
		Fd () = default;
		explicit Fd (int fd)
			: value (fd)
		{
		}
		Fd (const Fd &) = delete;
		Fd & operator= (const Fd &) = delete;
		~Fd ()
		{
			if (value >= 0) ::close (value);
		}
		int Release ()
		{
			const int fd = value;
			value = -1;
			return fd;
		}
	};

	/**
	 * Resolved location of one artifact: the fixed kind directory, the
	 * optional first-character shard the core already uses, and the file
	 * name.
	 */
	struct TargetLayout
	{
		std::string directory;
		std::string shard;
		std::string file;
	};

	/**
	 * Map one canonical artifact name onto the fixed kind directory, the
	 * first-character shard the core's readers already use and the file
	 * name.
	 * The name must already have passed `IsCanonicalArtifactName`: this
	 * helper assumes it contains no separator, NUL or ".." and only copies
	 * it into a component. A non-canonical name, or a kind this worker
	 * does not own, returns false so the request is refused before any
	 * path is opened.
	 */
	bool BuildLayout (i2p::data::StorageArtifact kind, std::string_view name, TargetLayout & out)
	{
		if (!IsCanonicalArtifactName (kind, name)) return false;
		const std::string base (name);
		switch (kind)
		{
		case i2p::data::StorageArtifact::RouterInfoCache:
			out.directory = "netDb";
			out.shard = std::string ("r") + base.front ();
			out.file = "routerInfo-" + base + ".dat";
			return true;
		case i2p::data::StorageArtifact::PeerProfile:
			out.directory = "peerProfiles";
			out.shard = std::string ("p") + base.front ();
			out.file = "profile-" + base + ".txt";
			return true;
		case i2p::data::StorageArtifact::DestinationKeys:
			out.directory = "destinations";
			out.shard.clear ();
			out.file = base + ".dat";
			return true;
		}
		return false;
	}

	/**
	 * Open (or create, mode 0700) one child directory of @a root. The
	 * entry is checked with AT_SYMLINK_NOFOLLOW and then re-checked
	 * through the opened descriptor so a replaced entry between the two
	 * calls is detected.
	 */
	int OpenChildDirectory (int root, std::string_view child)
	{
		if (child.empty () || child.find ('/') != std::string_view::npos || child.find ('\0') != std::string_view::npos)
			return -1;
		const std::string name (child);
		struct stat named { };
		if (::fstatat (root, name.c_str (), &named, AT_SYMLINK_NOFOLLOW) != 0)
		{
			if (errno != ENOENT || ::mkdirat (root, name.c_str (), 0700) != 0) return -1;
			if (::fstatat (root, name.c_str (), &named, AT_SYMLINK_NOFOLLOW) != 0) return -1;
		}
		if (!S_ISDIR (named.st_mode) || named.st_uid != ::geteuid ()) return -1;
		Fd opened { ::openat (root, name.c_str (), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC) };
		if (opened.value < 0) return -1;
		struct stat actual { };
		if (::fstat (opened.value, &actual) || actual.st_dev != named.st_dev || actual.st_ino != named.st_ino
			|| !S_ISDIR (actual.st_mode) || actual.st_uid != ::geteuid ())
			return -1;
		return opened.Release ();
	}

	/**
	 * fstatat with symlinks rejected; returns false on any unexpected
	 * error. */
	bool TargetStatus (int directory, const std::string & file, struct stat & status, bool & exists)
	{
		exists = false;
		if (::fstatat (directory, file.c_str (), &status, AT_SYMLINK_NOFOLLOW) != 0) return errno == ENOENT;
		exists = true;
		return true;
	}

	bool AtomicArtifactWrite (int root, const TargetLayout & layout, std::string_view bytes)
	{
		// An empty payload means "remove" on the wire and an empty artifact
		// has no meaning on disk; refuse rather than create a zero-length
		// file.
		if (bytes.empty () || bytes.size () > MAX_ARTIFACT_PAYLOAD_SIZE) return false;
		Fd kindDirectory { OpenChildDirectory (root, layout.directory) };
		if (kindDirectory.value < 0) return false;
		int directory = kindDirectory.value;
		Fd shardDirectory;
		if (!layout.shard.empty ())
		{
			shardDirectory.value = OpenChildDirectory (directory, layout.shard);
			if (shardDirectory.value < 0) return false;
			directory = shardDirectory.value;
		}
		struct stat target { };
		bool exists = false;
		if (!TargetStatus (directory, layout.file, target, exists)) return false;
		if (exists && !AcceptsArtifactTarget (S_ISREG (target.st_mode), target.st_uid, ::geteuid (), target.st_nlink))
			return false;
		// One request is in flight at a time and this process is single
		// threaded, so no staging slot here can belong to a concurrent
		// request of this worker. Stale slots from a killed predecessor are
		// removed first, after the same owner/regular/single-link check, so
		// leftovers can never accumulate or exhaust the fixed namespace.
		for (unsigned slot = 0; slot < STAGING_SLOTS; ++slot)
		{
			const std::string stale = ".openi2pd-artifact-" + std::to_string (slot);
			struct stat staleStatus { };
			bool staleExists = false;
			if (!TargetStatus (directory, stale, staleStatus, staleExists)) return false;
			if (!staleExists) continue;
			if (!AcceptsArtifactTarget (
					S_ISREG (staleStatus.st_mode), staleStatus.st_uid, ::geteuid (), staleStatus.st_nlink)
				|| ::unlinkat (directory, stale.c_str (), 0) != 0)
				return false;
		}
		std::string staging;
		Fd output;
		for (unsigned slot = 0; slot < STAGING_SLOTS && output.value < 0; ++slot)
		{
			staging = ".openi2pd-artifact-" + std::to_string (slot);
			output.value
				= ::openat (directory, staging.c_str (), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
			if (output.value < 0 && errno != EEXIST) return false;
		}
		if (output.value < 0) return false;
		size_t done = 0;
		while (done < bytes.size ())
		{
			const auto count = ::write (output.value, bytes.data () + done, bytes.size () - done);
			if (count < 0 && errno == EINTR) continue;
			if (count <= 0)
			{
				::unlinkat (directory, staging.c_str (), 0);
				return false;
			}
			done += static_cast<size_t> (count);
		}
		// Re-check the destination after the staging file is durable: a link
		// planted in the meantime must not be replaced.
		struct stat current { };
		bool currentExists = false;
		const bool rechecked = TargetStatus (directory, layout.file, current, currentExists)
			&& (!currentExists
				|| AcceptsArtifactTarget (S_ISREG (current.st_mode), current.st_uid, ::geteuid (), current.st_nlink));
		if (!rechecked || ::fsync (output.value) != 0
			|| ::renameat (directory, staging.c_str (), directory, layout.file.c_str ()) != 0)
		{
			::unlinkat (directory, staging.c_str (), 0);
			return false;
		}
		staging.clear ();
		return ::fsync (directory) == 0;
	}

	bool RemoveArtifact (int root, const TargetLayout & layout)
	{
		Fd kindDirectory { OpenChildDirectory (root, layout.directory) };
		if (kindDirectory.value < 0) return false;
		int directory = kindDirectory.value;
		Fd shardDirectory;
		if (!layout.shard.empty ())
		{
			shardDirectory.value = OpenChildDirectory (directory, layout.shard);
			if (shardDirectory.value < 0) return false;
			directory = shardDirectory.value;
		}
		struct stat target { };
		bool exists = false;
		if (!TargetStatus (directory, layout.file, target, exists)) return false;
		// Removing an absent artifact succeeds: the typed operation is
		// idempotent, and a failed cleanup must not block a later retry.
		if (!exists) return true;
		if (!AcceptsArtifactTarget (S_ISREG (target.st_mode), target.st_uid, ::geteuid (), target.st_nlink))
			return false;
		return ::unlinkat (directory, layout.file.c_str (), 0) == 0 && ::fsync (directory) == 0;
	}

	/**
	 * Send the single status reply of one request. It is used for the
	 * configuration acknowledgement, for the result of an operation and
	 * for a local refusal (Busy/Rejected), so the client always has
	 * exactly one correlated frame to consume. A false result means the
	 * channel itself is unusable and the worker must exit instead of
	 * continuing to serve.
	 */
	bool SendStatus (Channel & channel, uint32_t requestId, DataStorageStatus status, uint8_t kind, uint32_t generation)
	{
		return channel
			.Send ({ MessageType::DataStorageReply, requestId,
					   EncodeDataStorageReply (static_cast<uint8_t> (status), kind, generation) },
				REQUEST_TIMEOUT_MS)
			.has_value ();
	}
}

bool IsKnownArtifactKind (uint8_t kind)
{
	switch (static_cast<i2p::data::StorageArtifact> (kind))
	{
	case i2p::data::StorageArtifact::RouterInfoCache:
	case i2p::data::StorageArtifact::PeerProfile:
	case i2p::data::StorageArtifact::DestinationKeys: return true;
	}
	return false;
}

const char * ArtifactDirectory (i2p::data::StorageArtifact kind)
{
	switch (kind)
	{
	case i2p::data::StorageArtifact::RouterInfoCache: return "netDb";
	case i2p::data::StorageArtifact::PeerProfile: return "peerProfiles";
	case i2p::data::StorageArtifact::DestinationKeys: return "destinations";
	}
	return nullptr;
}

bool IsCanonicalArtifactName (i2p::data::StorageArtifact kind, std::string_view name)
{
	if (name.empty () || name.size () > MAX_ARTIFACT_NAME_SIZE) return false;
	// Cheap structural rejection before any decoding: a separator, a drive
	// or URL colon, a NUL or a parent reference can never name an
	// artifact.
	if (name.find ('\0') != std::string_view::npos || name.find ('/') != std::string_view::npos
		|| name.find ('\\') != std::string_view::npos || name.find (':') != std::string_view::npos
		|| name.find ("..") != std::string_view::npos)
		return false;
	switch (kind)
	{
	case i2p::data::StorageArtifact::RouterInfoCache:
	case i2p::data::StorageArtifact::PeerProfile:
	{
		// Canonical I2P base64 of a 32-byte identity hash: 44 characters,
		// including the single '=' padding that re-encoding produces.
		if (name.size () != 44) return false;
		std::array<uint8_t, 32> decoded { };
		if (DecodeBase64 (name, decoded.data (), decoded.size ()) != static_cast<int> (decoded.size ())) return false;
		return EncodeBase64 (decoded.data (), decoded.size ()) == name;
	}
	case i2p::data::StorageArtifact::DestinationKeys:
	{
		// &lt;52-character lower-case base32 identity&gt;.&lt;decimal key
		// type&gt; Key type 0 is ElGamal, which the core still persists, so
		// the range is 0..65535. Any other spelling (leading zero, sign,
		// suffix) is not canonical and is refused.
		// `libi2pd/PersistentStorage.h` documents the same contract for the
		// core-side validator.
		const auto dot = name.find ('.');
		if (dot != 52 || name.size () < 54 || name.size () > 58) return false;
		std::array<uint8_t, 32> decoded { };
		const auto identity = name.substr (0, 52);
		if (DecodeBase32 (identity, decoded.data (), decoded.size ()) != static_cast<int> (decoded.size ()))
			return false;
		if (EncodeBase32 (decoded.data (), decoded.size ()) != identity) return false;
		const auto digits = name.substr (53);
		if (digits.empty () || (digits.size () > 1 && digits.front () == '0')) return false;
		uint32_t keyType = 0;
		for (auto ch : digits)
		{
			if (ch < '0' || ch > '9') return false;
			keyType = keyType * 10 + static_cast<uint32_t> (ch - '0');
			if (keyType > 65535) return false;
		}
		return true;
	}
	}
	return false;
}

std::string ArtifactRelativePath (i2p::data::StorageArtifact kind, std::string_view name)
{
	TargetLayout layout;
	if (!BuildLayout (kind, name, layout)) return { };
	std::string path = layout.directory;
	if (!layout.shard.empty ()) path += "/" + layout.shard;
	path += "/" + layout.file;
	return path;
}

bool AcceptsArtifactTarget (bool isRegularFile, uint32_t targetUid, uint32_t currentUid, uint32_t linkCount)
{
	return isRegularFile && targetUid == currentUid && linkCount == 1;
}

void StorageClientRestart::Reset (std::chrono::steady_clock::time_point now) noexcept
{
	m_Attempts = 0;
	m_Backoff = MIN_BACKOFF;
	m_WindowStart = now;
	m_NextAttempt = now;
}

bool StorageClientRestart::RecordFailure (std::chrono::steady_clock::time_point now) noexcept
{
	if (m_Attempts == 0 || now - m_WindowStart > WINDOW)
	{
		// A quiet period starts a fresh window so a long-running router is
		// never disabled forever by one old burst of failures.
		m_WindowStart = now;
		m_Attempts = 0;
		m_Backoff = MIN_BACKOFF;
	}
	++m_Attempts;
	m_NextAttempt = now + m_Backoff;
	const auto doubled = std::chrono::duration_cast<std::chrono::milliseconds> (m_Backoff) * 2;
	const auto cap = std::chrono::duration_cast<std::chrono::milliseconds> (MAX_BACKOFF);
	m_Backoff = std::min (doubled, cap);
	return m_Attempts < MAX_ATTEMPTS;
}

bool StorageClientRestart::CanAttempt (std::chrono::steady_clock::time_point now) noexcept
{
	if (now - m_WindowStart > WINDOW)
	{
		m_Attempts = 0;
		m_Backoff = MIN_BACKOFF;
		m_WindowStart = now;
		m_NextAttempt = now;
		return true;
	}
	return m_Attempts < MAX_ATTEMPTS && now >= m_NextAttempt;
}

bool IsCurrentDataStorageReply (
	uint32_t replyGeneration, uint32_t replyRequestId, uint32_t currentGeneration, uint32_t inFlightRequestId)
{
	return replyGeneration == currentGeneration && replyRequestId == inFlightRequestId;
}

/**
 * Data-storage worker body. Ordering is the contract, not an accident:
 * 1. adopt the channel and apply the socket/capability lockdown before
 * the first untrusted frame;
 * 2. accept exactly one configuration frame, from an allowed parent
 * uid, whose dev/inode identity matches the root this worker actually
 * received, so a swapped or forged root is refused;
 * 3. keep the root above the inherited descriptors and create the three
 * fixed 0700 kind directories before the sandbox, so a missing
 * directory is a configuration failure rather than a later per-request
 * rejection; 4. drop stdio and install the storage sandbox
 * (Landlock/seccomp or pledge/unveil) with only that root;
 * 5. acknowledge with the generation, then serve requests, refusing a
 * replayed or out-of-order request id without executing it twice.
 * The worker is single-flight and synchronous; a Cancel only suppresses
 * a request that has not run, and any local or framing fault that is
 * tolerable is answered with a typed Error until the violation budget
 * is spent.
 */
int RunDataStorageWorker (int fd, int inheritedRoot)
{
	Channel channel = Channel::Adopt (fd);
	channel.SetEnforcementPolicy (EnforcementPolicy::Endpoint);
#if defined(__linux__)
	// The socket/capability boundary does not need the delegated root and
	// is installed before the first Configure frame is parsed.
	if (auto lockdown = ApplyLinuxIpcLockdown (GetLinuxSandboxProfile (WorkerClass::Storage)); !lockdown) return 1;
#endif
	auto first = channel.ReceiveWithFd (REQUEST_TIMEOUT_MS);
	if (!first || !*first) return 1;
	auto & config = **first;
	Fd root { inheritedRoot >= 0 ? inheritedRoot : config.fd };
	if (inheritedRoot >= 0 && config.fd >= 0)
	{
		::close (config.fd);
		return 1;
	}
#if defined(__OpenBSD__)
	if (inheritedRoot != 4 || config.fd >= 0) return 1;
#endif
	auto peer = channel.PeerUid ();
	struct stat status { };
	if (config.message.type != MessageType::ConfigureDataStorage || !config.message.requestId || !peer
		|| !IsAllowedParentUid (*peer) || ::geteuid () == 0 || root.value < 0 || ::fstat (root.value, &status)
		|| !S_ISDIR (status.st_mode) || status.st_uid != ::geteuid () || (status.st_mode & 0022))
		return 1;
	auto setup = DecodeConfigureDataStorage (config.message.payload);
	if (!setup || std::get<0> (*setup).empty () || std::get<1> (*setup) != static_cast<uint64_t> (status.st_dev)
		|| std::get<2> (*setup) != static_cast<uint64_t> (status.st_ino))
		return 1;
	const std::string rootPath = std::get<0> (*setup);
	const uint32_t generation = std::get<3> (*setup);
	// Keep the root above the inherited descriptors so a later dup cannot
	// alias it, exactly as the fixed-file router storage worker does.
	if (root.value <= 4)
	{
		const int replacement = ::fcntl (root.value, F_DUPFD_CLOEXEC, 5);
		if (replacement < 0) return 1;
		::close (root.value);
		root.value = replacement;
	}
	// The three kind directories are fixed, owner-only and created before
	// the sandbox so a missing directory is reported as a configuration
	// failure rather than surfacing later as a mysterious per-request
	// rejection.
	for (auto kind : { i2p::data::StorageArtifact::RouterInfoCache, i2p::data::StorageArtifact::PeerProfile,
			 i2p::data::StorageArtifact::DestinationKeys })
	{
		const auto * name = ArtifactDirectory (kind);
		if (!name) return 1;
		Fd directory { OpenChildDirectory (root.value, name) };
		if (directory.value < 0) return 1;
	}
	::close (0);
	::close (1);
	::close (2);
#if defined(__linux__) && !defined(__ANDROID__)
	auto profile = GetLinuxSandboxProfile (WorkerClass::Storage);
	profile.restrictFilesystemReads = true;
	profile.writablePaths = { "/proc/self/fd/" + std::to_string (root.value) };
	if (!ApplyLinuxSandbox (profile)) return 1;
#elif defined(__OpenBSD__)
	OpenBSDPolicyConfig policyConfig;
	// The channel and the inherited root already exist; the worker must
	// not gain new UNIX connections or descriptor reception after
	// bootstrap.
	policyConfig.preopenedStorageIpc = true;
	policyConfig.dataDir = rootPath;
	auto policy = BuildOpenBSDPolicy (OpenBSDWorkerRole::Storage, policyConfig);
	if (!policy || !ApplyOpenBSDPolicy (*policy)) return 1;
#else
	return 1;
#endif
	if (!SendStatus (channel, config.message.requestId, DataStorageStatus::Ok, 0, generation)) return 1;
	uint32_t lastRequest = config.message.requestId;
	unsigned violations = 0;
	std::optional<uint32_t> cancelledRequest;
	while (true)
	{
		auto incoming = channel.ReceiveWithFd (30000);
		if (!incoming) return 1;
		if (!*incoming) continue;
		auto & frame = **incoming;
		if (frame.fd >= 0)
		{
			::close (frame.fd);
			return 1;
		}
		const auto & request = frame.message;
		if (!AllowsOperation (EndpointRole::DataStorage, request.type))
		{
			// A known frame type that this endpoint does not implement is a
			// typed refusal, bounded so a peer that keeps sending one is
			// disconnected instead of being served forever.
			if (!channel.Send ({ MessageType::Error, request.requestId,
								   EncodeError (request.type, ErrorCode::NotAuthorized,
									   "operation is not allowed on the data storage endpoint") },
					REQUEST_TIMEOUT_MS)
				|| ++violations > Channel::MAX_PROTOCOL_VIOLATIONS)
				return 1;
			continue;
		}
		if (request.type == MessageType::Ping)
		{
			if (!request.payload.empty ()) return 1;
			if (!channel.Send ({ MessageType::Pong, request.requestId, { } }, REQUEST_TIMEOUT_MS)) return 1;
			continue;
		}
		if (request.type == MessageType::Shutdown)
		{
			if (!request.payload.empty ()) return 1;
			return 0;
		}
		auto reject = [&] (ErrorCode code, std::string_view detail)
		{
			if (!channel.Send ({ MessageType::Error, request.requestId, EncodeError (request.type, code, detail) },
					REQUEST_TIMEOUT_MS))
				return false;
			// A bounded number of malformed requests is tolerated as a typed
			// refusal; a peer that keeps sending them is disconnected.
			return ++violations <= Channel::MAX_PROTOCOL_VIOLATIONS;
		};
		if (request.type == MessageType::Cancel)
		{
			auto cancelled = DecodeCancel (request.payload);
			if (!cancelled)
			{
				if (!reject (ErrorCode::BadField, "invalid cancel request")) return 1;
				continue;
			}
			// Advisory only: this worker is synchronous, so a cancel that
			// arrives after the write started cannot roll it back. It
			// suppresses a matching request that has not run yet.
			cancelledRequest = *cancelled;
			continue;
		}
		if (request.type != MessageType::DataStorageRequest || !request.requestId)
		{
			if (!reject (ErrorCode::BadField, "invalid data storage operation")) return 1;
			continue;
		}
		if (cancelledRequest && request.requestId == *cancelledRequest)
		{
			cancelledRequest.reset ();
			if (!SendStatus (channel, request.requestId, DataStorageStatus::Busy, 0, generation)) return 1;
			continue;
		}
		auto decoded = DecodeDataStorageRequest (request.payload);
		if (!decoded)
		{
			if (!reject (ErrorCode::BadField, "malformed data storage request")) return 1;
			continue;
		}
		if (!IsKnownArtifactKind (decoded->kind))
		{
			if (!reject (ErrorCode::BadField, "unknown artifact kind")) return 1;
			continue;
		}
		const auto kind = static_cast<i2p::data::StorageArtifact> (decoded->kind);
		TargetLayout layout;
		if (!BuildLayout (kind, decoded->name, layout))
		{
			if (!reject (ErrorCode::BadField, "non-canonical artifact name")) return 1;
			continue;
		}
		if (request.requestId <= lastRequest)
		{
			// A replayed or out-of-order id is refused, never executed twice.
			if (!SendStatus (channel, request.requestId, DataStorageStatus::Busy, decoded->kind, generation)) return 1;
			if (++violations > Channel::MAX_PROTOCOL_VIOLATIONS) return 1;
			continue;
		}
		lastRequest = request.requestId;
		const bool ok = decoded->payload.empty () ? RemoveArtifact (root.value, layout)
												  : AtomicArtifactWrite (root.value, layout, decoded->payload);
		if (!SendStatus (channel, request.requestId, ok ? DataStorageStatus::Ok : DataStorageStatus::Rejected,
				decoded->kind, generation))
			return 1;
	}
}

struct DataStorageClient::Impl
{
	/**
	 * Client-side lifecycle. `Stopped` is the state before `Start()` and
	 * after `Stop()`; `Ready` owns a configured worker; `Restarting` may
	 * respawn once the shared backoff deadline has passed; `Failed` is
	 * terminal for this owner because the attempt window is spent (or
	 * restart is not permitted on this platform), and only an explicit
	 * `Start()` leaves it. A `Failed` client refuses every operation
	 * instead of writing locally.
	 */
	enum class State : uint8_t
	{
		Stopped,
		Ready,
		Restarting,
		Failed
	};

	std::mutex mutex;
	ChildProcess child;
	std::string rootPath;
	uint64_t device = 0;
	uint64_t inode = 0;
	/**
	 * Kept open for the lifetime of a started client so a restart can
	 * reuse the same validated directory capability instead of
	 * re-resolving a path. */
	int rootFd = -1;
	uint32_t nextRequest = 0;
	uint32_t generation = 0;
	State state = State::Stopped;
	StorageClientRestart restart;
	bool restartPermitted = true;
	bool idExhausted = false;
	std::function<std::chrono::steady_clock::time_point ()> clock;
	std::atomic<DataStorageStatus> lastStatus { DataStorageStatus::Rejected };
	std::atomic<uint32_t> publishedGeneration { 0 };

	Impl ()
	{
#if defined(__OpenBSD__)
		// The core cannot fork/exec after the runtime pledge, and this
		// increment does not route a replacement data worker through the
		// storage broker. A lost worker is therefore terminal on OpenBSD:
		// writes fail closed until the daemon restarts. Linux owns its child
		// and can respawn it.
		restartPermitted = false;
#endif
	}

	std::chrono::steady_clock::time_point Now () const { return clock ? clock () : std::chrono::steady_clock::now (); }

	/// Close the channel first, so a worker that only watches for EOF
	/// leaves on its own. On Linux the owned child is then stopped; on
	/// OpenBSD the detached worker is disowned instead, because signalling
	/// it would need proc authority the core no longer holds after its
	/// runtime pledge.
	void CloseWorker ()
	{
		child.ChannelFd ().Close ();
#if !defined(__OpenBSD__)
		child.Stop (500);
#else
		// A detached worker is owned only through channel EOF after the
		// runtime pledge: signalling it would need proc authority the core
		// no longer has, and the worker exits on EOF itself.
		child.Disown ();
#endif
	}

	void MarkRestart (std::string_view reason)
	{
		CloseWorker ();
		const auto now = Now ();
		if (restartPermitted && restart.RecordFailure (now))
		{
			state = State::Restarting;
			return;
		}
		state = State::Failed;
		// One bounded, non-secret diagnostic: the reason is a fixed string
		// and never contains an artifact name, path or key material.
		std::fprintf (stderr, "privsep data storage: client disabled (%.*s)\n", static_cast<int> (reason.size ()),
			reason.data ());
	}

	bool Configure (std::string & error)
	{
		child.ChannelFd ().Close ();
#if !defined(__OpenBSD__)
		child.Stop (500);
#else
		child.Disown ();
#endif
		if (rootFd < 0)
		{
			error = "data storage root capability is closed";
			return false;
		}
		const uint32_t nextGeneration = generation + 1;
#if defined(__OpenBSD__)
		auto worker = SpawnDetachedWorkerWithRoot ("data-storage-inherited", rootFd);
		if (!worker)
		{
			error = worker.error ();
			return false;
		}
		child = ChildProcess::Adopt (-1, std::move (*worker));
		const int passed = -1;
#else
		auto worker = SpawnWorker ("data-storage", { });
		if (!worker)
		{
			error = worker.error ();
			return false;
		}
		child = std::move (*worker);
		const int passed = rootFd;
#endif
		auto setup = EncodeConfigureDataStorage (rootPath, device, inode, nextGeneration);
		if (!setup)
		{
			error = "data storage configuration exceeds the IPC bounds";
			return false;
		}
		const auto id = ++nextRequest;
		child.ChannelFd ().SetEnforcementPolicy (EnforcementPolicy::Endpoint);
		if (!child.ChannelFd ().SendWithFd (
				{ MessageType::ConfigureDataStorage, id, *setup }, passed, REQUEST_TIMEOUT_MS))
		{
			error = "data storage worker is unreachable";
			return false;
		}
		auto reply = child.ChannelFd ().ReceiveWithFd (REQUEST_TIMEOUT_MS);
		if (!reply || !*reply)
		{
			error = "data storage worker did not answer";
			return false;
		}
		auto & frame = **reply;
		if (frame.fd >= 0)
		{
			::close (frame.fd);
			error = "data storage worker sent an unexpected descriptor";
			return false;
		}
		if (frame.message.type == MessageType::Error)
		{
			error = "data storage worker refused the configuration";
			return false;
		}
		auto decoded = DecodeDataStorageReply (frame.message.payload);
		if (frame.message.type != MessageType::DataStorageReply || !decoded || frame.message.requestId != id
			|| std::get<0> (*decoded) != static_cast<uint8_t> (DataStorageStatus::Ok)
			|| std::get<2> (*decoded) != nextGeneration)
		{
			error = "data storage worker returned an invalid configuration reply";
			return false;
		}
		generation = nextGeneration;
		publishedGeneration.store (generation);
		restart.Reset (Now ());
		return true;
	}

	/** Respawn/configure once when the backoff allows it. */
	bool EnsureReady ()
	{
		if (state == State::Ready) return true;
		if (!restartPermitted || state != State::Restarting || !restart.CanAttempt (Now ())) return false;
		std::string error;
		if (Configure (error))
		{
			state = State::Ready;
			return true;
		}
		if (restart.RecordFailure (Now ()))
		{
			state = State::Restarting;
			return false;
		}
		state = State::Failed;
		std::fprintf (stderr, "privsep data storage: restart limit reached (%.*s)\n", static_cast<int> (error.size ()),
			error.data ());
		return false;
	}

	/**
	 * Single-flight write or removal under @ref mutex.
	 *
	 * Validation (kind, canonical name, payload bound) happens before any
	 * worker contact, so a refused request cannot change the worker's
	 * state. A send/receive failure closes the channel and moves the
	 * client into restarting, and becomes a terminal `Failed` once the
	 * shared attempt window is spent; the ambiguous request is never
	 * replayed, so the caller sees false. `LastStatus()` reports Busy for
	 * that case, Rejected for a typed per-request `Error` reply (which
	 * leaves the channel usable) and for a local refusal, and request-id
	 * exhaustion is a local Busy refusal.
	 */
	bool Operate (i2p::data::StorageArtifact kind, std::string_view name, std::span<const uint8_t> bytes, bool remove,
		std::stop_token token)
	{
		std::lock_guard lock (mutex);
		if (token.stop_requested ())
		{
			// Cancelled before anything was sent: nothing to undo.
			lastStatus.store (DataStorageStatus::Busy);
			return false;
		}
		if (!IsKnownArtifactKind (static_cast<uint8_t> (kind)) || !IsCanonicalArtifactName (kind, name)
			|| (!remove && (bytes.empty () || bytes.size () > MAX_ARTIFACT_PAYLOAD_SIZE)))
		{
			lastStatus.store (DataStorageStatus::Rejected);
			return false;
		}
		if (state == State::Stopped || state == State::Failed)
		{
			lastStatus.store (DataStorageStatus::Rejected);
			return false;
		}
		if (!EnsureReady ())
		{
			// Not ready yet: the backoff delay or a failed respawn is a Busy
			// refusal, never a substituted or replayed write.
			lastStatus.store (DataStorageStatus::Busy);
			return false;
		}
		if (nextRequest == std::numeric_limits<uint32_t>::max ())
		{
			if (!idExhausted)
			{
				idExhausted = true;
				std::fprintf (stderr, "privsep data storage: request id space exhausted\n");
			}
			lastStatus.store (DataStorageStatus::Busy);
			return false;
		}
		auto encoded = EncodeDataStorageRequest (static_cast<uint8_t> (kind), name, bytes);
		if (!encoded)
		{
			lastStatus.store (DataStorageStatus::Rejected);
			return false;
		}
		const auto id = ++nextRequest;
		auto & channel = child.ChannelFd ();
		if (!channel.IsOpen () || !channel.Send ({ MessageType::DataStorageRequest, id, *encoded }, REQUEST_TIMEOUT_MS))
		{
			// The request may or may not have been applied. It is never
			// replayed automatically: the caller sees the failure.
			lastStatus.store (DataStorageStatus::Busy);
			MarkRestart ("send failed");
			return false;
		}
		// A cancellation observed after the request was sent is advisory: the
		// worker may already have applied it, so the request is never
		// replayed and the reply is still drained to keep the channel in
		// sync.
		const bool cancelledAfterSend = token.stop_requested ();
		if (cancelledAfterSend) (void)channel.Send ({ MessageType::Cancel, 0, EncodeCancel (id) }, 250);
		auto reply = channel.Receive (REQUEST_TIMEOUT_MS);
		if (!reply || !*reply)
		{
			lastStatus.store (DataStorageStatus::Busy);
			MarkRestart ("worker did not answer");
			return false;
		}
		const auto & message = **reply;
		if (message.type == MessageType::Error)
		{
			// A typed per-request refusal keeps the channel alive: framing was
			// valid, the operation was simply not allowed.
			lastStatus.store (DataStorageStatus::Rejected);
			return false;
		}
		auto decoded = DecodeDataStorageReply (message.payload);
		if (message.type != MessageType::DataStorageReply || !decoded
			|| !IsCurrentDataStorageReply (std::get<2> (*decoded), message.requestId, generation, id))
		{
			// A reply from a replaced worker, or for a request the client has
			// already abandoned, is dropped. The channel stays usable because
			// the frame itself was well formed.
			lastStatus.store (DataStorageStatus::Busy);
			return false;
		}
		const auto status = static_cast<DataStorageStatus> (std::get<0> (*decoded));
		lastStatus.store (cancelledAfterSend ? DataStorageStatus::Busy : status);
		return !cancelledAfterSend && status == DataStorageStatus::Ok;
	}
};

DataStorageClient::DataStorageClient ()
	: m_Impl (std::make_unique<Impl> ())
{
}

DataStorageClient::~DataStorageClient ()
{
	Stop ();
}

void DataStorageClient::Stop ()
{
	std::lock_guard lock (m_Impl->mutex);
	m_Impl->state = Impl::State::Stopped;
	m_Impl->CloseWorker ();
	if (m_Impl->rootFd >= 0)
	{
		::close (m_Impl->rootFd);
		m_Impl->rootFd = -1;
	}
	m_Impl->restart.Reset (m_Impl->Now ());
}

bool DataStorageClient::Start (const std::filesystem::path & root, std::string & error)
{
	std::lock_guard lock (m_Impl->mutex);
	error.clear ();
	if (m_Impl->state == Impl::State::Ready)
	{
		error = "data storage is already started";
		return false;
	}
	const uint32_t workerUid = WorkerUid ();
	if (workerUid == 0)
	{
		error = "data storage requires an unprivileged worker identity";
		return false;
	}
	std::error_code ec;
	const auto absolute = std::filesystem::canonical (root, ec);
	if (ec || absolute.string ().size () > MAX_PATH_SIZE)
	{
		error = "invalid data storage root";
		return false;
	}
	// Keep O_RDONLY rather than O_PATH: the worker fsyncs the directory.
	Fd directory { ::open (absolute.c_str (), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC) };
	struct stat status { };
	if (directory.value < 0 || ::fstat (directory.value, &status) || !S_ISDIR (status.st_mode)
		|| status.st_uid != workerUid || (status.st_mode & 0022))
	{
		error = "unsafe data storage root";
		return false;
	}
	if (m_Impl->rootFd >= 0) ::close (m_Impl->rootFd);
	m_Impl->rootFd = directory.Release ();
	m_Impl->rootPath = absolute.string ();
	m_Impl->device = static_cast<uint64_t> (status.st_dev);
	m_Impl->inode = static_cast<uint64_t> (status.st_ino);
	m_Impl->restart.Reset (m_Impl->Now ());
	if (!m_Impl->Configure (error))
	{
		m_Impl->state = Impl::State::Stopped;
		m_Impl->CloseWorker ();
		return false;
	}
	m_Impl->state = Impl::State::Ready;
	return true;
}

bool DataStorageClient::Write (
	i2p::data::StorageArtifact kind, std::string_view name, std::span<const uint8_t> bytes, std::stop_token token)
{
	return m_Impl->Operate (kind, name, bytes, false, token);
}

bool DataStorageClient::Remove (i2p::data::StorageArtifact kind, std::string_view name, std::stop_token token)
{
	return m_Impl->Operate (kind, name, { }, true, token);
}

DataStorageStatus DataStorageClient::LastStatus () const
{
	return m_Impl->lastStatus.load ();
}

uint32_t DataStorageClient::Generation () const
{
	return m_Impl->publishedGeneration.load ();
}

long DataStorageClient::WorkerPid () const
{
	return static_cast<long> (m_Impl->child.Pid ());
}

void DataStorageClient::SetClockForTesting (std::function<std::chrono::steady_clock::time_point ()> clock)
{
	std::lock_guard lock (m_Impl->mutex);
	m_Impl->clock = std::move (clock);
}

void DataStorageClient::SetRequestIdForTesting (uint32_t nextRequestId)
{
	std::lock_guard lock (m_Impl->mutex);
	m_Impl->nextRequest = nextRequestId;
}
}

#else // _WIN32

namespace i2p::privsep
{
// Fail-closed stubs, not a backend: the data-storage worker needs UNIX
// process and descriptor primitives. Every entry point reports
// "unavailable" so a caller that enables isolated persistence cannot
// silently fall back to writing artifacts from the router process.
bool IsKnownArtifactKind (uint8_t)
{
	return false;
}
const char * ArtifactDirectory (i2p::data::StorageArtifact)
{
	return nullptr;
}
bool IsCanonicalArtifactName (i2p::data::StorageArtifact, std::string_view)
{
	return false;
}
std::string ArtifactRelativePath (i2p::data::StorageArtifact, std::string_view)
{
	return { };
}
bool AcceptsArtifactTarget (bool, uint32_t, uint32_t, uint32_t)
{
	return false;
}
void StorageClientRestart::Reset (std::chrono::steady_clock::time_point now) noexcept
{
	m_Attempts = 0;
	m_Backoff = MIN_BACKOFF;
	m_WindowStart = now;
	m_NextAttempt = now;
}
bool StorageClientRestart::RecordFailure (std::chrono::steady_clock::time_point) noexcept
{
	return false;
}
bool StorageClientRestart::CanAttempt (std::chrono::steady_clock::time_point) noexcept
{
	return false;
}
bool IsCurrentDataStorageReply (uint32_t, uint32_t, uint32_t, uint32_t)
{
	return false;
}
int RunDataStorageWorker (int, int)
{
	return 1;
}
struct DataStorageClient::Impl
{
};
DataStorageClient::DataStorageClient ()
	: m_Impl (std::make_unique<Impl> ())
{
}
DataStorageClient::~DataStorageClient () = default;
void DataStorageClient::Stop () { }
bool DataStorageClient::Start (const std::filesystem::path &, std::string & error)
{
	error = "data storage is unavailable on this platform";
	return false;
}
bool DataStorageClient::Write (i2p::data::StorageArtifact, std::string_view, std::span<const uint8_t>, std::stop_token)
{
	return false;
}
bool DataStorageClient::Remove (i2p::data::StorageArtifact, std::string_view, std::stop_token)
{
	return false;
}
DataStorageStatus DataStorageClient::LastStatus () const
{
	return DataStorageStatus::Rejected;
}
uint32_t DataStorageClient::Generation () const
{
	return 0;
}
long DataStorageClient::WorkerPid () const
{
	return -1;
}
void DataStorageClient::SetClockForTesting (std::function<std::chrono::steady_clock::time_point ()>) { }
void DataStorageClient::SetRequestIdForTesting (uint32_t) { }
}

#endif
