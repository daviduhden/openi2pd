/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Torrents.
 *
 * Implements BEP 3 metainfo parsing and creation, piece verification
 * and block scheduling, peer-wire messages with BEP 6/9/10/11
 * extensions, resume state and tracker response handling. Filesystem
 * access stays beneath the torrent root through no-follow bounded
 * helpers, while shared piece state is mutex-protected across the
 * network and disk workers.
 *
 */

#ifndef NO_TORRENTS

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <charconv>
#include <deque>
#include <sstream>
#include <algorithm>
#include <cerrno>
#include <climits>
#include <system_error>
#include <functional>
#include <iterator>
#include <limits>
#include <set>
#include <utility>
#include <openssl/rand.h>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/hex.hpp>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
// Boost.Asio requires Winsock 2 to be included before windows.h.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include "Log.h"
#include "Base.h"
#include "I2PEndian.h"
#include "Timestamp.h"
#include "TorrentsTunnel.h"
#include "Torrents.h"

namespace i2p
{
namespace torrents
{

	// BEncoded

	constexpr size_t BENCODED_MAX_DEPTH = 10;
	constexpr size_t BENCODED_MAX_DICTIONARY_KEYS = 4096;
	constexpr size_t BENCODED_MAX_LIST_ITEMS = 1024 * 1024;
	constexpr size_t MAX_TORRENT_PATH_COMPONENT_LENGTH = 255;
	constexpr size_t MAX_TORRENT_PATH_COMPONENTS = 256;

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif
#ifndef O_NONBLOCK
#define O_NONBLOCK 0
#endif

	namespace
	{
#ifdef _WIN32
		bool HasReparsePoint (const std::filesystem::path & path)
		{
			auto attributes = GetFileAttributesW (path.c_str ());
			return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT);
		}
#endif

		bool ArePathComponentsEqual (const std::filesystem::path & a, const std::filesystem::path & b)
		{
#ifdef _WIN32
			return boost::iequals (a.native (), b.native ()); // NTFS is case-insensitive
#else
			return a == b;
#endif
		}

		enum class FileAccess
		{
			eRead,
			eReadWrite,
			eReadWriteCreate,
			eCreateExclusive,
			eCreateOrTruncate
		};

		int OpenNoFollowAt (
			const std::filesystem::path & root, const std::filesystem::path & relative, FileAccess access)
		{
			if (!IsSafeRelativePath (relative)) return -1;

#ifdef _WIN32
			auto fullPath = root;
			for (auto it = relative.begin (); it != relative.end (); ++it)
			{
				fullPath /= *it;
				auto attributes = GetFileAttributesW (fullPath.c_str ());
				if (attributes == INVALID_FILE_ATTRIBUTES)
				{
					// Missing intermediate directories must not be created by a
					// file open; a missing leaf is expected before creation.
					if (std::next (it) != relative.end ()) return -1;
				}
				else if (attributes & FILE_ATTRIBUTE_REPARSE_POINT)
					return -1; // never follow junctions or symlinks below the root
			}
			int flags = _O_BINARY | _O_NOINHERIT;
			switch (access)
			{
			case FileAccess::eRead: flags |= _O_RDONLY; break;
			case FileAccess::eReadWrite: flags |= _O_RDWR; break;
			case FileAccess::eReadWriteCreate: flags |= _O_RDWR | _O_CREAT; break;
			case FileAccess::eCreateExclusive: flags |= _O_WRONLY | _O_CREAT | _O_EXCL; break;
			case FileAccess::eCreateOrTruncate: flags |= _O_WRONLY | _O_CREAT | _O_TRUNC; break;
			default: return -1;
			}
			int fd = -1;
			if (_wsopen_s (&fd, fullPath.c_str (), flags, _SH_DENYNO, _S_IREAD | _S_IWRITE) != 0) return -1;
			return fd;
#else
			int directory
				= ::open (root.empty () ? "." : root.c_str (), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
			if (directory < 0) return -1;

			for (auto it = relative.begin (); it != relative.end (); ++it)
			{
				bool isLeaf = std::next (it) == relative.end ();
				int flags;
				if (isLeaf)
				{
					// O_NONBLOCK keeps a planted FIFO from blocking the disk
					// service; regular files ignore it. The descriptor is
					// checked with fstat by the caller.
					flags = O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK;
					switch (access)
					{
					case FileAccess::eRead: flags |= O_RDONLY; break;
					case FileAccess::eReadWrite: flags |= O_RDWR; break;
					case FileAccess::eReadWriteCreate: flags |= O_RDWR | O_CREAT; break;
					case FileAccess::eCreateExclusive: flags |= O_WRONLY | O_CREAT | O_EXCL; break;
					case FileAccess::eCreateOrTruncate: flags |= O_WRONLY | O_CREAT | O_TRUNC; break;
					default:
						::close (directory);
						errno = EINVAL;
						return -1;
					}
				}
				else
					flags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC;

				int fd = ::openat (directory, it->c_str (), flags, 0666);
				int savedErrno = errno;
				::close (directory);
				if (fd < 0)
				{
					errno = savedErrno;
					return -1;
				}
				if (isLeaf) return fd;
				directory = fd;
			}
			// IsSafeRelativePath() rejects empty paths, so the loop always
			// returns the leaf descriptor above. Keep a defensive failure for
			// an unexpected implementation change.
			::close (directory);
			errno = EINVAL;
			return -1;
#endif
		}

		int OpenNoFollow (const std::filesystem::path & path, FileAccess access)
		{
			if (path.empty ()) return -1;
			auto parent = path.parent_path ();
			auto leaf = path.filename ();
			if (leaf.empty ()) return -1;
			return OpenNoFollowAt (parent.empty () ? std::filesystem::path (".") : parent, leaf, access);
		}

		void CloseFile (int fd)
		{
			if (fd < 0) return;
#ifdef _WIN32
			_close (fd);
#else
			::close (fd);
#endif
		}

		long ReadSome (int fd, void * buf, size_t len)
		{
			if (!len) return 0;
#ifdef _WIN32
			return _read (fd, buf, static_cast<unsigned int> (std::min<size_t> (len, INT_MAX)));
#else
			ssize_t result;
			do result = ::read (fd, buf, len);
			while (result < 0 && errno == EINTR);
			return static_cast<long> (result);
#endif
		}

		bool WriteAll (int fd, const void * buf, size_t len)
		{
			auto data = static_cast<const uint8_t *> (buf);
			if (len && !data) return false;
			size_t written = 0;
			while (written < len)
			{
#ifdef _WIN32
				auto n = _write (
					fd, data + written, static_cast<unsigned int> (std::min<size_t> (len - written, INT_MAX)));
#else
				ssize_t n;
				do n = ::write (fd, data + written, len - written);
				while (n < 0 && errno == EINTR);
#endif
				if (n <= 0) return false;
				written += static_cast<size_t> (n);
			}
			return true;
		}

		bool ReadAt (int fd, size_t offset, uint8_t * buf, size_t len)
		{
			if (offset > std::numeric_limits<size_t>::max () - len) return false;
			size_t read = 0;
			while (read < len)
			{
#ifdef _WIN32
				if (_lseeki64 (fd, static_cast<__int64> (offset + read), SEEK_SET) < 0) return false;
				auto n = _read (fd, buf + read, static_cast<unsigned int> (std::min<size_t> (len - read, INT_MAX)));
#else
				ssize_t n;
				do n = ::pread (fd, buf + read, len - read, static_cast<off_t> (offset + read));
				while (n < 0 && errno == EINTR);
#endif
				if (n < 0) return false;
				if (n == 0) return false; // truncated file: never accept a short read
				read += static_cast<size_t> (n);
			}
			return true;
		}

		bool WriteAt (int fd, size_t offset, const uint8_t * buf, size_t len)
		{
			if (offset > std::numeric_limits<size_t>::max () - len) return false;
			size_t written = 0;
			while (written < len)
			{
#ifdef _WIN32
				if (_lseeki64 (fd, static_cast<__int64> (offset + written), SEEK_SET) < 0) return false;
				auto n
					= _write (fd, buf + written, static_cast<unsigned int> (std::min<size_t> (len - written, INT_MAX)));
#else
				ssize_t n;
				do n = ::pwrite (fd, buf + written, len - written, static_cast<off_t> (offset + written));
				while (n < 0 && errno == EINTR);
#endif
				if (n <= 0) return false;
				written += static_cast<size_t> (n);
			}
			return true;
		}

		bool TruncateFile (int fd, size_t size)
		{
#ifdef _WIN32
			return _chsize_s (fd, static_cast<__int64> (size)) == 0;
#else
			return ::ftruncate (fd, static_cast<off_t> (size)) == 0;
#endif
		}

		bool IsRegularFileDescriptor (int fd)
		{
#ifdef _WIN32
			struct _stat64 status;
			return _fstat64 (fd, &status) == 0 && (status.st_mode & _S_IFMT) == _S_IFREG;
#else
			struct stat status;
			return ::fstat (fd, &status) == 0 && S_ISREG (status.st_mode);
#endif
		}
	}

	bool IsSafeRelativePath (const std::filesystem::path & path)
	{
		if (path.empty () || path.is_absolute () || path.has_root_name () || path.has_root_directory ()) return false;
		for (const auto & component : path)
		{
			if (component.empty () || component == "." || component == "..") return false;
			const auto & native = component.native ();
			if (native.find (static_cast<std::filesystem::path::value_type> ('\0')) != native.npos) return false;
		}
		return true;
	}

	bool IsPathLexicallyWithinRoot (const std::filesystem::path & root, const std::filesystem::path & target)
	{
		if (root.empty () || target.empty ()) return false;
		auto relative = target.lexically_normal ().lexically_relative (root.lexically_normal ());
		return !relative.empty () && IsSafeRelativePath (relative);
	}

	bool IsPathWithinRoot (
		const std::filesystem::path & root, const std::filesystem::path & target, bool allowDirectoryLeaf)
	{
		if (root.empty () || target.empty ()) return false;

		std::error_code ec;
		auto absoluteRoot = std::filesystem::absolute (root, ec).lexically_normal ();
		if (ec || absoluteRoot.empty ()) return false;
		auto absoluteTarget = std::filesystem::absolute (target, ec).lexically_normal ();
		if (ec || absoluteTarget.empty ()) return false;

		auto rootStatus = std::filesystem::symlink_status (absoluteRoot, ec);
		if (ec)
		{
			if (ec != std::errc::no_such_file_or_directory) return false;
			ec.clear ();
		}
		else if (std::filesystem::is_symlink (rootStatus))
			return false;
#ifdef _WIN32
		if (HasReparsePoint (absoluteRoot)) return false;
#endif

		auto rootIt = absoluteRoot.begin ();
		auto targetIt = absoluteTarget.begin ();
		for (; rootIt != absoluteRoot.end (); ++rootIt, ++targetIt)
		{
			if (targetIt == absoluteTarget.end () || !ArePathComponentsEqual (*rootIt, *targetIt)) return false;
		}
		if (targetIt == absoluteTarget.end ()) return false; // root itself is not a file target

		auto current = absoluteRoot;
		while (targetIt != absoluteTarget.end ())
		{
			current /= *targetIt;
			auto nextIt = targetIt;
			++nextIt;
			bool isLeaf = nextIt == absoluteTarget.end ();

			auto status = std::filesystem::symlink_status (current, ec);
			if (ec)
			{
				if (ec != std::errc::no_such_file_or_directory) return false;
				ec.clear ();
				// Missing components may be created by us. Their parent has
				// already been checked and every later open/creation rechecks
				// without following symlinks.
			}
			else
			{
				if (std::filesystem::is_symlink (status)) return false;
#ifdef _WIN32
				if (HasReparsePoint (current)) return false;
#endif
				if (isLeaf)
				{
					if (!std::filesystem::is_regular_file (status)
						&& !(allowDirectoryLeaf && std::filesystem::is_directory (status)))
						return false;
				}
				else if (!std::filesystem::is_directory (status))
					return false;
			}
			targetIt = nextIt;
		}
		return true;
	}

	namespace
	{
		bool ReadBoundedFromFd (int fd, size_t maxBytes, std::vector<uint8_t> & content, std::string & error)
		{
			content.clear ();
			std::array<uint8_t, 64 * 1024> chunk;
			size_t total = 0;
			for (;;)
			{
				auto n = ReadSome (fd, chunk.data (), chunk.size ());
				if (n < 0)
				{
					error = "can't read file";
					content.clear ();
					return false;
				}
				if (!n) break;
				if (static_cast<size_t> (n) > maxBytes - total)
				{
					error = "file is too large";
					content.clear ();
					return false;
				}
				content.insert (content.end (), chunk.begin (), chunk.begin () + n);
				total += static_cast<size_t> (n);
			}
			if (!total)
			{
				error = "empty file";
				return false;
			}
			return true;
		}
	}

	bool ReadFileBoundedNoFollow (
		const std::filesystem::path & path, size_t maxBytes, std::vector<uint8_t> & content, std::string & error)
	{
		content.clear ();
		error.clear ();
		int fd = OpenNoFollow (path, FileAccess::eRead);
		if (fd < 0)
		{
			error = "can't open file";
			return false;
		}
		if (!IsRegularFileDescriptor (fd))
		{
			error = "not a regular file";
			CloseFile (fd);
			return false;
		}
		bool ok = ReadBoundedFromFd (fd, maxBytes, content, error);
		CloseFile (fd);
		return ok;
	}

	bool ReadFileBoundedNoFollow (const std::filesystem::path & root, const std::filesystem::path & relativePath,
		size_t maxBytes, std::vector<uint8_t> & content, std::string & error)
	{
		content.clear ();
		error.clear ();
		int fd = OpenNoFollowAt (root, relativePath, FileAccess::eRead);
		if (fd < 0)
		{
			error = "can't open file";
			return false;
		}
		if (!IsRegularFileDescriptor (fd))
		{
			error = "not a regular file";
			CloseFile (fd);
			return false;
		}
		bool ok = ReadBoundedFromFd (fd, maxBytes, content, error);
		CloseFile (fd);
		return ok;
	}

	bool WriteFileNoFollow (const std::filesystem::path & path, const void * data, size_t len)
	{
		int fd = OpenNoFollow (path, FileAccess::eCreateOrTruncate);
		if (fd < 0) return false;
		if (!IsRegularFileDescriptor (fd))
		{
			CloseFile (fd);
			return false;
		}
		bool ok = !len || WriteAll (fd, data, len);
		CloseFile (fd);
		return ok;
	}

	bool WriteFileNoFollow (
		const std::filesystem::path & root, const std::filesystem::path & relativePath, const void * data, size_t len)
	{
		int fd = OpenNoFollowAt (root, relativePath, FileAccess::eCreateOrTruncate);
		if (fd < 0) return false;
		if (!IsRegularFileDescriptor (fd))
		{
			CloseFile (fd);
			return false;
		}
		bool ok = !len || WriteAll (fd, data, len);
		CloseFile (fd);
		return ok;
	}

	bool RemoveFileNoFollow (
		const std::filesystem::path & root, const std::filesystem::path & relativePath, std::string & error)
	{
		error.clear ();
		if (root.empty () || !IsSafeRelativePath (relativePath))
		{
			error = "invalid removal target";
			return false;
		}
#ifdef _WIN32
		auto target = root / relativePath;
		if (!IsPathWithinRoot (root, target, false))
		{
			error = "unsafe removal path";
			return false;
		}
		if (DeleteFileW (target.c_str ())) return true;
		auto code = GetLastError ();
		if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) return true;
		error = "cannot remove file";
		return false;
#else
		int directory = ::open (root.c_str (), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
		if (directory < 0)
		{
			if (errno == ENOENT) return true;
			error = "cannot open removal root";
			return false;
		}
		for (const auto & component : relativePath.parent_path ())
		{
			int next = ::openat (directory, component.c_str (), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
			int code = errno;
			::close (directory);
			if (next < 0)
			{
				if (code == ENOENT) return true;
				error = "unsafe removal parent";
				return false;
			}
			directory = next;
		}
		int result = ::unlinkat (directory, relativePath.filename ().c_str (), 0);
		int code = errno;
		// Missing derived state needs no durability write. A successful
		// unlink is durable only after its containing directory has been
		// synced.
		bool ok = result == 0 ? ::fsync (directory) == 0 : code == ENOENT;
		::close (directory);
		if (!ok) error = "cannot remove file or synchronize removal";
		return ok;
#endif
	}

	bool WriteFileAtomicNoFollow (const std::filesystem::path & root, const std::filesystem::path & relativePath,
		const void * data, size_t len, std::string & error)
	{
		error.clear ();
		auto fail = [&error] (std::string_view why)
		{
			error = why;
			return false;
		};
		if (root.empty () || !IsSafeRelativePath (relativePath) || (len && !data))
			return fail ("invalid atomic write target");
		std::array<uint8_t, 16> random;
		if (RAND_bytes (random.data (), static_cast<int> (random.size ())) != 1) return fail ("cannot randomize temporary name");
		static constexpr char hex[] = "0123456789abcdef";
		std::string temporary = ".openi2pd-";
		for (auto byte : random)
		{
			temporary += hex[byte >> 4];
			temporary += hex[byte & 15];
		}
#ifdef _WIN32
		auto parent = root / relativePath.parent_path ();
		auto target = root / relativePath;
		auto staging = parent / temporary;
		if (!IsPathWithinRoot (root, target, false) || !IsPathWithinRoot (root, staging, false))
			return fail ("unsafe atomic write path");
		auto attributes = GetFileAttributesW (target.c_str ());
		if (attributes != INVALID_FILE_ATTRIBUTES
			&& (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
			return fail ("unsafe existing target");
		if (attributes != INVALID_FILE_ATTRIBUTES)
		{
			HANDLE existing = CreateFileW (target.c_str (), FILE_READ_ATTRIBUTES,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
				FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
			if (existing == INVALID_HANDLE_VALUE) return fail ("cannot inspect existing target");
			BY_HANDLE_FILE_INFORMATION info;
			bool safe = GetFileInformationByHandle (existing, &info) && info.nNumberOfLinks == 1
				&& !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
			CloseHandle (existing);
			if (!safe) return fail ("unsafe linked atomic write target");
		}
		HANDLE file = CreateFileW (staging.c_str (), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
			FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
		if (file == INVALID_HANDLE_VALUE) return fail ("cannot create atomic staging file");
		bool ok = true;
		size_t offset = 0;
		while (offset < len)
		{
			DWORD written = 0;
			auto chunk = static_cast<DWORD> (std::min<size_t> (len - offset, 1024 * 1024));
			if (!WriteFile (file, static_cast<const uint8_t *> (data) + offset, chunk, &written, nullptr) || !written)
			{
				ok = false;
				break;
			}
			offset += written;
		}
		if (ok) ok = FlushFileBuffers (file) != 0;
		if (!CloseHandle (file)) ok = false;
		if (ok)
			ok = IsPathWithinRoot (root, target, false)
				&& MoveFileExW (staging.c_str (), target.c_str (), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
		if (!ok)
		{
			DeleteFileW (staging.c_str ());
			return fail ("atomic file replacement failed");
		}
		return true;
#else
		int directory = ::open (root.c_str (), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
		if (directory < 0) return fail ("cannot open atomic write root");
		for (const auto & component : relativePath.parent_path ())
		{
			auto next = ::openat (directory, component.c_str (), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
			CloseFile (directory);
			if (next < 0) return fail ("cannot traverse atomic write parent");
			directory = next;
		}
		auto leaf = relativePath.filename ().native ();
		auto safeTarget = [&] ()
		{
			struct stat status;
			if (::fstatat (directory, leaf.c_str (), &status, AT_SYMLINK_NOFOLLOW)) return errno == ENOENT;
			return S_ISREG (status.st_mode) && status.st_nlink == 1;
		};
		if (!safeTarget ())
		{
			CloseFile (directory);
			return fail ("unsafe existing atomic write target");
		}
		auto fd = ::openat (directory, temporary.c_str (), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
		if (fd < 0)
		{
			CloseFile (directory);
			return fail ("cannot create atomic staging file");
		}
		bool ok = WriteAll (fd, data, len) && ::fsync (fd) == 0;
		if (::close (fd)) ok = false;
		if (ok) ok = safeTarget () && ::renameat (directory, temporary.c_str (), directory, leaf.c_str ()) == 0;
		if (!ok)
		{
			::unlinkat (directory, temporary.c_str (), 0);
			CloseFile (directory);
			return fail ("atomic file replacement failed");
		}
		ok = ::fsync (directory) == 0;
		CloseFile (directory);
		if (!ok) return fail ("replacement completed but parent sync failed");
		return true;
#endif
	}

	bool CreateFileWithSizeNoFollow (const std::filesystem::path & path, size_t size)
	{
		int fd = OpenNoFollow (path, FileAccess::eCreateExclusive);
		if (fd < 0) return false;
		bool ok = IsRegularFileDescriptor (fd) && TruncateFile (fd, size);
		CloseFile (fd);
		return ok;
	}

	bool CreateFileWithSizeNoFollow (
		const std::filesystem::path & root, const std::filesystem::path & relativePath, size_t size)
	{
		int fd = OpenNoFollowAt (root, relativePath, FileAccess::eCreateExclusive);
		if (fd < 0) return false;
		bool ok = IsRegularFileDescriptor (fd) && TruncateFile (fd, size);
		CloseFile (fd);
		return ok;
	}

	bool CreateDirectoriesWithinRootNoFollow (
		const std::filesystem::path & root, const std::filesystem::path & relativeDirectory)
	{
		if (!relativeDirectory.empty () && relativeDirectory != "." && !IsSafeRelativePath (relativeDirectory))
			return false;

		// The root itself is the trusted configuration value. Create it when
		// missing, but never accept a symlink as the root directory.
		std::error_code ec;
		auto rootStatus = std::filesystem::symlink_status (root, ec);
		if (ec && ec != std::errc::no_such_file_or_directory) return false;
		ec.clear ();
		if (!std::filesystem::is_directory (rootStatus))
		{
			if (std::filesystem::is_symlink (rootStatus)) return false;
			std::filesystem::create_directories (root, ec);
			if (ec) return false;
			rootStatus = std::filesystem::symlink_status (root, ec);
			if (ec || std::filesystem::is_symlink (rootStatus) || !std::filesystem::is_directory (rootStatus))
				return false;
		}
#ifdef _WIN32
		if (HasReparsePoint (root)) return false;
#endif
		if (relativeDirectory.empty () || relativeDirectory == ".") return true;
#ifdef _WIN32
		auto current = root;
		for (const auto & component : relativeDirectory)
		{
			current /= component;
			if (!CreateDirectoryW (current.c_str (), nullptr) && GetLastError () != ERROR_ALREADY_EXISTS) return false;
			auto attributes = GetFileAttributesW (current.c_str ());
			if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)
				|| (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
				return false;
		}
		return true;
#else
		int directory = ::open (root.c_str (), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
		if (directory < 0) return false;
		for (const auto & component : relativeDirectory)
		{
			if (::mkdirat (directory, component.c_str (), 0755) != 0 && errno != EEXIST)
			{
				::close (directory);
				return false;
			}
			int fd = ::openat (directory, component.c_str (), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
			if (fd < 0)
			{
				::close (directory);
				return false;
			}
			::close (directory);
			directory = fd;
		}
		::close (directory);
		return true;
#endif
	}

	// Tracker-provided counters travel as signed 64-bit bencode integers.
	// Clamp them instead of truncating an out-of-range value into int.
	static int ClampTrackerCount (int64_t value)
	{
		return (int)std::clamp<int64_t> (value, 0, std::numeric_limits<int>::max ());
	}

	static bool ValidMetaText (std::string_view text, size_t maxBytes)
	{
		if (text.size () > maxBytes) return false;
		for (size_t i = 0; i < text.size ();)
		{
			auto ch = static_cast<unsigned char> (text[i++]);
			uint32_t cp = ch;
			size_t extra = 0;
			if (ch >= 0xC2 && ch <= 0xDF)
			{
				cp = ch & 0x1F;
				extra = 1;
			}
			else if (ch >= 0xE0 && ch <= 0xEF)
			{
				cp = ch & 0x0F;
				extra = 2;
			}
			else if (ch >= 0xF0 && ch <= 0xF4)
			{
				cp = ch & 0x07;
				extra = 3;
			}
			else if (ch >= 0x80)
				return false;
			if (extra > text.size () - i) return false;
			for (size_t j = 0; j < extra; ++j)
			{
				auto next = static_cast<unsigned char> (text[i++]);
				if ((next & 0xC0) != 0x80) return false;
				cp = (cp << 6) | (next & 0x3F);
			}
			if ((extra == 1 && cp < 0x80) || (extra == 2 && cp < 0x800) || (extra == 3 && cp < 0x10000) || cp > 0x10FFFF
				|| (cp >= 0xD800 && cp <= 0xDFFF))
				return false;
			if (cp < 0x20 && cp != '\t' && cp != '\n') return false;
			if (cp == 0x7F) return false;
		}
		return true;
	}

	std::pair<std::string_view, size_t> ExtractByteString (std::string_view buf)
	{
		// A byte string starts with a decimal length without sign, leading
		// zeroes or embedded whitespace, followed by ':' and exactly that
		// many bytes.
		size_t pos = 0;
		while (pos < buf.size () && buf[pos] >= '0' && buf[pos] <= '9') pos++;
		if (!pos || pos >= buf.size () || buf[pos] != ':') return { std::string_view { }, 0 };
		if (buf[0] == '0' && pos != 1) return { std::string_view { }, 0 }; // no leading zeroes
		size_t len = 0;
		auto res = std::from_chars (buf.data (), buf.data () + pos, len);
		if (res.ec != std::errc () || res.ptr != buf.data () + pos) return { std::string_view { }, 0 };
		if (len > buf.size () - pos - 1) return { std::string_view { }, 0 };
		return { buf.substr (pos + 1, len), pos + 1 + len };
	}

	std::pair<int64_t, size_t> ExtractInteger (std::string_view buf)
	{
		if (buf.size () < 3 || buf[0] != 'i') return { 0, 0 };
		size_t pos = 1;
		bool negative = false;
		if (buf[pos] == '-')
		{
			negative = true;
			pos++;
		}
		size_t digitsStart = pos;
		while (pos < buf.size () && buf[pos] >= '0' && buf[pos] <= '9') pos++;
		if (pos == digitsStart || pos >= buf.size () || buf[pos] != 'e') return { 0, 0 };
		if (buf[digitsStart] == '0' && pos - digitsStart != 1) return { 0, 0 }; // no leading zeroes
		if (negative && pos - digitsStart == 1 && buf[digitsStart] == '0') return { 0, 0 }; // no -0
		int64_t value = 0;
		auto res = std::from_chars (buf.data () + (negative ? digitsStart - 1 : digitsStart), buf.data () + pos, value);
		if (res.ec != std::errc () || res.ptr != buf.data () + pos) return { 0, 0 };
		return { value, pos + 1 };
	}

	static size_t ParseBEncoded (std::string_view buf, size_t depth); // recursive
	size_t ParseDictionary (
		std::string_view buf, std::function<size_t (std::string_view key, std::string_view buf)> handler, size_t depth)
	{
		if (depth > BENCODED_MAX_DEPTH || buf.empty () || buf[0] != 'd') return 0;
		buf = buf.substr (1);
		size_t len = 1;
		std::set<std::string_view> keys;
		while (!buf.empty () && buf[0] != 'e')
		{
			if (keys.size () >= BENCODED_MAX_DICTIONARY_KEYS) return 0;
			auto [key, offset] = ExtractByteString (buf);
			if (!offset) break;
			if (!keys.insert (key).second) return 0; // duplicate dictionary key
			len += offset;
			buf = buf.substr (offset);
			offset = 0;
			if (handler) offset = handler (key, buf);
			if (!offset) offset = ParseBEncoded (buf, depth);
			if (!offset || offset > buf.size ()) break;
			len += offset;
			buf = buf.substr (offset);
		}
		if (buf.empty () || buf[0] != 'e') return 0; // malformed dictionary without terminating 'e'
		len++; // 'e'
		return len;
	}

	std::pair<std::map<std::string_view, std::string_view>, size_t> ExtractDictionary (std::string_view buf)
	{
		std::map<std::string_view, std::string_view> ret;
		size_t len = 0;
		if (!buf.empty () && buf[0] == 'd')
		{
			len++;
			buf = buf.substr (1); // 'd'
			bool tooManyKeys = false;
			while (!buf.empty () && buf[0] != 'e')
			{
				if (ret.size () >= BENCODED_MAX_DICTIONARY_KEYS)
				{
					tooManyKeys = true;
					break;
				}
				auto [key, offset] = ExtractByteString (buf);
				if (!offset) break;
				len += offset;
				buf = buf.substr (offset);
				offset = ParseBEncoded (buf, 1);
				if (!offset || offset > buf.size ()) break;
				if (!ret.emplace (key, buf.substr (0, offset)).second) break; // duplicate key
				len += offset;
				buf = buf.substr (offset);
			}
			if (buf.empty () || buf[0] != 'e' || tooManyKeys)
			{
				ret.clear ();
				len = 0;
			}
			else
				len++; // 'e'
		}
		return { ret, len };
	}

	size_t ParseList (std::string_view buf, std::function<size_t (std::string_view buf)> handler, size_t depth)
	{
		if (depth > BENCODED_MAX_DEPTH || buf.empty () || buf[0] != 'l') return 0;
		buf = buf.substr (1);
		size_t len = 1, numItems = 0;
		while (!buf.empty () && buf[0] != 'e')
		{
			if (++numItems > BENCODED_MAX_LIST_ITEMS) return 0;
			size_t l = 0;
			if (handler) l = handler (buf);
			if (!l) l = ParseBEncoded (buf, depth);
			if (!l || l > buf.size ()) break;
			len += l;
			buf = buf.substr (l);
		}
		if (buf.empty () || buf[0] != 'e') return 0; // malformed list without terminating 'e'
		len++; // 'e'
		return len;
	}

	static size_t ParseBEncoded (std::string_view buf, size_t depth)
	{
		if (buf.empty () || depth > BENCODED_MAX_DEPTH) return 0;
		size_t ret = 0;
		switch (buf[0])
		{
		case 'i': // integer
			return ExtractInteger (buf).second;
			break;
		case 'l': // list
			return ParseList (buf, nullptr, depth + 1);
			break;
		case 'd': // dictionary
			return ParseDictionary (buf, nullptr, depth + 1);
			break;
		default: // byte string
			return ExtractByteString (buf).second;
		}
		return ret;
	}

	std::pair<std::vector<std::string_view>, size_t> ParseStringList (std::string_view buf)
	{
		std::vector<std::string_view> strings;
		bool wrongType = false;
		size_t len = ParseList (buf,
			[&strings, &wrongType] (std::string_view str) -> size_t
			{
				auto [s, l] = ExtractByteString (str);
				if (l)
					strings.push_back (s);
				else
					wrongType = true;
				return l;
			});
		// ParseList treats a zero handler result as a request for generic
		// parsing. Reject that fallback here: metainfo path components and
		// DHT values are byte-string lists, and silently omitting another
		// type would change the meaning of untrusted input.
		if (!len || wrongType) return { { }, 0 };
		return { strings, len };
	}

	std::string CreateByteString (std::string_view str)
	{
		std::string ret (std::to_string (str.length ()));
		ret += ":";
		ret += str;
		return ret;
	}

	std::string CreateInteger (int64_t v)
	{
		std::string ret ("i");
		ret += std::to_string (v);
		ret += "e";
		return ret;
	}

	std::string CreateDictionary (const std::vector<std::pair<std::string_view, std::string_view>> & items)
	{
		std::map<std::string_view, std::string_view> ordered;
		for (const auto & [name, value] : items)
			if (!value.empty () && !ordered.emplace (name, value).second) return { };
		std::stringstream s;
		s << 'd';
		for (const auto & [name, value] : ordered)
		{
			s << CreateByteString (name);
			s << value;
		}
		s << 'e';
		return s.str ();
	}

	std::string CreateList (const std::vector<std::string> & items)
	{
		std::stringstream s;
		s << 'l';
		for (const auto & it : items) s << it;
		s << 'e';
		return s.str ();
	}

	// ------------------------------ Pure peer-extension decoders. No
	// logging, no network, no connection or torrent state: the handlers
	// below and the fuzz/regression tests call the same function, so a
	// decoder fix cannot drift from the wire path.
	// ------------------------------

	PEXDecodeResult DecodePEXAdded (std::string_view dictionary)
	{
		PEXDecodeResult result;
		std::unordered_set<i2p::data::IdentHash> unique;
		auto parsed = ParseDictionary (dictionary,
			[&result, &unique] (std::string_view key, std::string_view value) -> size_t
			{
				// The I2P profile of BEP11 carries exactly one "added" byte
				// string; "added.f" (flags) and "dropped" are not defined here
				// and their values are only structurally validated.
				if (key != "added") return 0;
				auto [idents, len] = ExtractByteString (value);
				if (!len)
				{
					result.malformed = true;
					return 0;
				}
				if (idents.size () % i2p::data::IdentHash::len)
				{
					// A truncated hash list would otherwise drop only the last
					// partial entry and silently accept a corrupt message.
					result.malformed = true;
					return len;
				}
				while (idents.size () >= i2p::data::IdentHash::len && result.peers.size () < MAX_PEX_RECEIVE_PEERS)
				{
					i2p::data::IdentHash peer (reinterpret_cast<const uint8_t *> (idents.data ()));
					idents.remove_prefix (i2p::data::IdentHash::len);
					if (peer.IsZero ()) continue;
					if (unique.insert (peer).second) result.peers.push_back (peer);
				}
				return len;
			});
		// Trailing bytes after the dictionary would be ignored by the parser
		// while the prefix was already applied, so require exact consumption.
		if (!parsed || parsed != dictionary.size ()) result.malformed = true;
		if (result.malformed) result.peers.clear ();
		return result;
	}

	DHTHintResult DecodeDHTHint (std::string_view dictionary)
	{
		DHTHintResult result;
		int64_t port = 0;
		int64_t rport = 0;
		bool hasRPort = false;
		auto parsed = ParseDictionary (dictionary,
			[&port, &rport, &hasRPort] (std::string_view key, std::string_view value) -> size_t
			{
				if (key != "port" && key != "rport") return 0;
				auto [number, len] = ExtractInteger (value);
				if (!len) return 0; // structurally validated, value unusable
				if (key == "port")
					port = number;
				else
				{
					// Record presence even for an out-of-range value: an
					// unrelated rport is tolerated, not a reason to drop a
					// otherwise valid hint.
					rport = number;
					hasRPort = true;
				}
				return len;
			});
		if (!parsed || parsed != dictionary.size ())
		{
			result.malformed = true;
			return result;
		}
		// 1..65534: 65535 and above leave no usable port+1 relationship.
		if (port < 1 || port > 65534) return result;
		result.port = static_cast<uint16_t> (port);
		if (hasRPort)
		{
			// Java uses "port" and ignores "rport"; compare in 64 bits so
			// port + 1 can never wrap at the boundary.
			result.rportMismatch = rport != port + 1;
		}
		return result;
	}

	//------------------------------------

	bool TorrentFile::Save (size_t offset, const uint8_t * buf, size_t len)
	{
		std::lock_guard<std::mutex> lock (m_Mutex);
		// A peer-controlled offset/length may never index outside this file
		// or wrap the bounds check, even if the session layout is corrupted.
		if (offset > m_FileLength || len > m_FileLength - offset || (len && !buf))
		{
			LogPrint (eLogError, "Torrents: Refusing out-of-range write at ", offset, " length ", len, " to ",
				m_FullFilePath);
			return false;
		}
		OpenLocked (true);
		if (m_PayloadStorage ? !m_StorageHandle : m_File < 0) return false;
		if (!len) return true;
		if (m_PayloadStorage)
		{
			std::string error;
			for (size_t done = 0; done < len;)
			{
				auto chunk = std::min (len - done, TorrentPayloadStorage::MaxTransfer);
				if (!m_PayloadStorage->Write (m_StorageHandle, offset + done, { buf + done, chunk }, error))
				{
					LogPrint (eLogError, "Torrents: Payload storage write failed: ", error);
					CloseLocked (); // Retry must reacquire authority after provider/IPC recovery.
					return false;
				}
				done += chunk;
			}
			return true;
		}
		if (!WriteAt (m_File, offset, buf, len))
		{
			LogPrint (eLogError, "Torrents: Failed to write ", len, " bytes at ", offset, " to ", m_FullFilePath);
			return false;
		}
		return true;
	}

	bool TorrentFile::Load (size_t offset, uint8_t * buf, size_t len)
	{
		std::lock_guard<std::mutex> lock (m_Mutex);
		if (offset > m_FileLength || len > m_FileLength - offset || (len && !buf))
		{
			LogPrint (eLogError, "Torrents: Refusing out-of-range read at ", offset, " length ", len, " from ",
				m_FullFilePath);
			return false;
		}
		OpenLocked (false);
		if (m_PayloadStorage ? !m_StorageHandle : m_File < 0) return false;
		if (!len) return true;
		// A short file or short read is a failure: never hand partially
		// initialised data to the piece hash check.
		if (m_PayloadStorage)
		{
			std::string error;
			for (size_t done = 0; done < len;)
			{
				auto chunk = std::min (len - done, TorrentPayloadStorage::MaxTransfer);
				if (!m_PayloadStorage->Read (m_StorageHandle, offset + done, { buf + done, chunk }, error))
				{
					LogPrint (eLogError, "Torrents: Payload storage read failed: ", error);
					CloseLocked ();
					return false;
				}
				done += chunk;
			}
			return true;
		}
		if (!ReadAt (m_File, offset, buf, len))
		{
			LogPrint (eLogError, "Torrents: Failed to read ", len, " bytes at ", offset, " from ", m_FullFilePath);
			return false;
		}
		return true;
	}

	void TorrentFile::Complete ()
	{
		std::lock_guard<std::mutex> lock (m_Mutex);
		m_IsPart = false;
		CloseLocked ();
	}

	void TorrentFile::Open (bool create)
	{
		std::lock_guard<std::mutex> lock (m_Mutex);
		OpenLocked (create);
	}

	void TorrentFile::OpenLocked (bool create)
	{
		// A final-name file is opened read-only for seeding. A later
		// invalidation can force a re-download, which needs a writable
		// descriptor; close and reopen instead of writing through the
		// read-only handle (which would fail forever).
		if (m_File >= 0)
		{
			if (!create || m_FileWritable) return;
			CloseFile (m_File);
			m_File = -1;
			m_FileWritable = false;
		}
		auto filePath = m_FullFilePath;
		if (m_IsPart && (!m_PayloadStorage || !m_PayloadStorage->UsesFinalNames ())) filePath += ".part";
		if (m_PayloadStorage)
		{
			m_LastAccessTime = i2p::util::GetMonotonicSeconds ();
			if (m_StorageHandle && (!create || m_StorageWritable)) return;
			std::string error;
			if (m_StorageHandle)
			{
				m_PayloadStorage->Close (m_StorageHandle, error);
				m_StorageHandle = 0;
			}
			auto relative = filePath.lexically_relative (m_StorageRootDir);
			if (m_StorageRootDir.empty () || !IsSafeRelativePath (relative)) return;
			m_StorageHandle = m_PayloadStorage->Open (relative, m_FileLength, create, error);
			m_StorageWritable = create && m_StorageHandle;
			if (!m_StorageHandle) LogPrint (eLogError, "Torrents: Isolated file registration failed: ", error);
			return; // Never fall back to direct filesystem IO after worker failure.
		}

		// Prefer opening relative to the trusted root so every component
		// below it is traversed with O_NOFOLLOW. If a root was supplied,
		// never fall back to a plain path open on failure: that would
		// reintroduce the symlink traversal the anchored open just rejected.
		if (!m_RootDir.empty ())
		{
			auto relative = filePath.lexically_relative (m_RootDir);
			if (relative.empty () || !IsSafeRelativePath (relative))
			{
				LogPrint (eLogError, "Torrents: Refusing file path outside root ", filePath);
				m_LastAccessTime = i2p::util::GetMonotonicSeconds ();
				return;
			}
			const auto access = m_IsPart ? (create ? FileAccess::eReadWriteCreate : FileAccess::eReadWrite)
										 : (create ? FileAccess::eReadWrite : FileAccess::eRead);
			m_File = OpenNoFollowAt (m_RootDir, relative, access);
			m_FileWritable = m_File >= 0 && access != FileAccess::eRead;
		}
		else
		{
			const auto access = m_IsPart ? (create ? FileAccess::eReadWriteCreate : FileAccess::eReadWrite)
										 : (create ? FileAccess::eReadWrite : FileAccess::eRead);
			m_File = OpenNoFollow (filePath, access);
			m_FileWritable = m_File >= 0 && access != FileAccess::eRead;
		}
		if (m_File >= 0 && !IsRegularFileDescriptor (m_File))
		{
			CloseFile (m_File);
			m_File = -1;
			m_FileWritable = false;
			errno = EINVAL;
		}
		if (m_File < 0) LogPrint (eLogError, "Torrents: Can't open file ", filePath, " : ",
			std::generic_category ().message (errno));
		m_LastAccessTime = i2p::util::GetMonotonicSeconds ();
	}

	void TorrentFile::Close ()
	{
		std::lock_guard<std::mutex> lock (m_Mutex);
		CloseLocked ();
	}

	void TorrentFile::CloseLocked ()
	{
		if (m_StorageHandle && m_PayloadStorage)
		{
			std::string error;
			(void)m_PayloadStorage->Close (m_StorageHandle, error);
			m_StorageHandle = 0;
			m_StorageWritable = false;
		}
		CloseFile (m_File);
		m_File = -1;
		m_FileWritable = false;
	}

	bool TorrentFile::IsOpen () const
	{
		std::lock_guard<std::mutex> lock (m_Mutex);
		return m_File >= 0 || m_StorageHandle != 0;
	}

	bool TorrentFile::ProbePayload ()
	{
		std::lock_guard<std::mutex> lock (m_Mutex);
		if (!m_PayloadStorage || m_StorageRootDir.empty ()) return m_File >= 0;
		auto relative = m_FullFilePath.lexically_relative (m_StorageRootDir);
		if (relative.empty () || !IsSafeRelativePath (relative)) return false;
		std::string error;
		const auto handle = m_PayloadStorage->Open (relative, m_FileLength, false, error);
		if (!handle) return false;
		(void)m_PayloadStorage->Close (handle, error);
		return true;
	}

	bool TorrentFile::UpdateFullPath (const std::filesystem::path & rootDir)
	{
		std::lock_guard<std::mutex> lock (m_Mutex);
		if (!IsSafeRelativePath (m_FullFilePath))
		{
			m_FullFilePath.clear ();
			m_RootDir.clear ();
			return false;
		}
		m_RootDir = rootDir;
		m_FullFilePath = (rootDir / m_FullFilePath).lexically_normal ();
		return true;
	}

	Piece::Piece (size_t size, const uint8_t * hash)
		: m_Size (size)
		, m_Data (nullptr)
		, m_IsSending (false)
		, m_IsLoading (false)
		, m_IsRequested (false)
		, m_IsVerified (false)
		, m_IsVerificationPending (false)
		, m_LastActivityTimestamp (0)
		, m_NumPeers (0)
	{
		memcpy (m_Hash, hash, SHA_DIGEST_LENGTH);
		m_Blocks = std::make_unique<std::vector<BlockStatus>> (GetNumBlocks (size), BlockStatus::Missing);
	}

	Piece::Piece (Piece && other) noexcept
		: m_Size (other.m_Size)
		, m_Data (nullptr)
		, m_IsSending (false)
		, m_IsLoading (false)
		, m_IsRequested (false)
		, m_IsVerified (false)
		, m_IsVerificationPending (false)
		, m_LastActivityTimestamp (0)
		, m_NumPeers (0)
	{
		memcpy (m_Hash, other.m_Hash, SHA_DIGEST_LENGTH);
		std::lock_guard<std::mutex> l (other.m_Mutex);
		m_BlockEpoch = other.m_BlockEpoch;
		m_Data = std::move (other.m_Data);
		m_Blocks = std::move (other.m_Blocks);
		m_IsSending = other.m_IsSending;
		m_IsLoading = other.m_IsLoading;
		m_IsRequested = other.m_IsRequested;
		m_IsVerified = other.m_IsVerified;
		m_IsSaving = other.m_IsSaving;
		m_IsVerificationPending = other.m_IsVerificationPending;
		m_LastActivityTimestamp = other.m_LastActivityTimestamp;
		m_NumPeers = other.m_NumPeers;
		other.m_IsSending = false;
		other.m_IsLoading = false;
		other.m_IsRequested = false;
		other.m_IsVerified = false;
		other.m_IsSaving = false;
		other.m_IsVerificationPending = false;
		other.m_BlockEpoch = 0;
		other.m_LastActivityTimestamp = 0;
		other.m_NumPeers = 0;
	}

	Piece::~Piece ()
	{
		std::lock_guard<std::mutex> l (m_Mutex);
		DeleteDataBufferLocked ();
	}

	bool Piece::IsComplete () const
	{
		std::lock_guard<std::mutex> l (m_Mutex);
		return !m_Blocks;
	}

	void Piece::Complete (bool pendingVerification)
	{
		std::lock_guard<std::mutex> l (m_Mutex);
		m_Blocks.reset ();
		m_IsRequested = false;
		m_IsSaving = pendingVerification;
		// Completion alone (a resume bitfield or a pre-existing final file)
		// is not proof that the bytes match the hash. Only VerifyHash() sets
		// this flag.
		m_IsVerified = false;
		// The bounded verification pass owns the pending flag; a caller that
		// marks the piece complete explicitly also ends that bookkeeping.
		// RequestLocalVerification()/MarkPendingVerification() re-arm it.
		m_IsVerificationPending = false;
	}

	bool Piece::VerifyHash ()
	{
		std::lock_guard<std::mutex> l (m_Mutex);
		if (!m_Data)
		{
			m_IsVerified = false;
			return false;
		}
		uint8_t digest[SHA_DIGEST_LENGTH];
		SHA1 (m_Data.get (), m_Size, digest);
		m_IsVerified = !memcmp (m_Hash, digest, SHA_DIGEST_LENGTH);
		return m_IsVerified;
	}

	bool Piece::IsVerified () const
	{
		std::lock_guard<std::mutex> l (m_Mutex);
		return m_IsVerified && !m_IsSaving && !m_IsVerificationPending;
	}

	void Piece::MarkPendingVerification ()
	{
		std::lock_guard<std::mutex> l (m_Mutex);
		++m_BlockEpoch;
		m_Blocks.reset ();
		m_IsRequested = false;
		m_IsSaving = false;
		m_IsVerified = false;
		m_IsVerificationPending = true;
	}

	bool Piece::IsVerificationPending () const
	{
		std::lock_guard<std::mutex> l (m_Mutex);
		return m_IsVerificationPending;
	}

	void Piece::FinishLocalVerification ()
	{
		std::lock_guard<std::mutex> l (m_Mutex);
		m_IsVerificationPending = false;
	}

	void Piece::SetIsSaving (bool saving)
	{
		std::lock_guard<std::mutex> l (m_Mutex);
		m_IsSaving = saving;
	}

	bool Piece::IsSaving () const
	{
		std::lock_guard<std::mutex> l (m_Mutex);
		// A piece that awaits local hashing must not be published, served or
		// counted as verified, exactly like a write that is still in flight.
		return m_IsSaving || m_IsVerificationPending;
	}

	bool Piece::IsAvailableLocked (int block) const
	{
		if (!m_Blocks) return true;
		if (block < 0 || block >= (int)m_Blocks->size ()) return false;
		return (*m_Blocks)[block] == BlockStatus::Available;
	}

	void Piece::SetIsSending (bool isSending)
	{
		std::lock_guard<std::mutex> l (m_Mutex);
		m_IsSending = isSending;
		if (isSending) m_LastActivityTimestamp = i2p::util::GetMonotonicSeconds ();
	}

	void Piece::SetIsLoading (bool loading)
	{
		std::lock_guard<std::mutex> l (m_Mutex);
		m_IsLoading = loading;
	}

	bool Piece::IsSending () const
	{
		std::lock_guard<std::mutex> l (m_Mutex);
		return m_IsSending;
	}

	bool Piece::IsLoading () const
	{
		std::lock_guard<std::mutex> l (m_Mutex);
		return m_IsLoading;
	}

	uint64_t Piece::GetLastActivityTimestamp () const
	{
		std::lock_guard<std::mutex> l (m_Mutex);
		return m_LastActivityTimestamp;
	}

	bool Piece::IsRequested () const
	{
		std::lock_guard<std::mutex> l (m_Mutex);
		return m_IsRequested;
	}

	size_t Piece::GetNumPeers () const
	{
		std::lock_guard<std::mutex> l (m_Mutex);
		return m_NumPeers;
	}

	void Piece::SetNumPeers (size_t numPeers)
	{
		std::lock_guard<std::mutex> l (m_Mutex);
		m_NumPeers = numPeers;
	}

	size_t Piece::GetNumBlocks (size_t len) const
	{
		auto d = lldiv (len, REQUEST_BLOCK_SIZE);
		// A torrent block count is bounded well below INT_MAX by the metadata
		// and piece-size checks; make the narrowing explicit.
		int numBlocks = static_cast<int> (d.quot);
		if (d.rem > 0) numBlocks++;
		return numBlocks;
	}

	bool Piece::BlockReceived (const uint8_t * block, size_t len, size_t offset)
	{
		std::lock_guard<std::mutex> l (m_Mutex);
		// Check the subtraction form so a hostile offset cannot wrap the
		// addition and pass a bounds check on 32-bit size_t.
		if (!len || !block || !m_Blocks || offset > m_Size || len > m_Size - offset) return false;
		// A requested block is exactly one aligned 16 KiB range (or the final
		// partial range). Rejecting shorter/misaligned peer payloads prevents
		// marking a whole block Available after one byte and later serving
		// uninitialised bytes from the rest of the block.
		if (offset % REQUEST_BLOCK_SIZE != 0) return false;
		const size_t expectedLen = std::min (REQUEST_BLOCK_SIZE, m_Size - offset);
		if (len != expectedLen)
		{
			LogPrint (eLogWarning, "Torrents: Partial/misaligned block (", offset, ",", len, ")");
			return false;
		}
		size_t blockIndex = offset / REQUEST_BLOCK_SIZE;
		if (blockIndex >= m_Blocks->size ()) return false;
		if ((*m_Blocks)[blockIndex] == BlockStatus::Requested)
		{
			if (!m_Data) NewDataBufferLocked ();
			memcpy (m_Data.get () + offset, block, len);
			m_IsVerified = false; // the bytes changed since any earlier hash check
			m_LastActivityTimestamp = i2p::util::GetMonotonicSeconds ();
			(*m_Blocks)[blockIndex] = BlockStatus::Available;
			if (std::find_if (m_Blocks->begin (), m_Blocks->end (),
					[] (BlockStatus status) { return status != BlockStatus::Available; })
				== m_Blocks->end ())
			{
				// all blocks are available
				LogPrint (eLogDebug, "Torrents: piece complete");
				m_Blocks.reset ();
				m_IsRequested = false;
			}
			return true;
		}
		if ((*m_Blocks)[blockIndex] == BlockStatus::Available)
			LogPrint (eLogWarning, "Torrents: Duplicated piece block ", blockIndex);
		else
			LogPrint (eLogWarning, "Torrents: Late or unsolicited piece block ", blockIndex);
		return false;
	}

	bool Piece::Dump (PieceFileFragment && fragment)
	{
		std::lock_guard<std::mutex> l (m_Mutex);
		if (!m_IsVerified || !m_Data || !fragment.file || fragment.fragmentOffset > m_Size
			|| fragment.fragmentSize > m_Size - fragment.fragmentOffset)
		{
			LogPrint (eLogError, "Torrents: Refusing an unverified or invalid piece write");
			return false;
		}
		m_IsSending = true;
		const bool success
			= fragment.file->Save (fragment.fileOffset, m_Data.get () + fragment.fragmentOffset, fragment.fragmentSize);
		m_IsSending = false;
		if (success && fragment.fragmentSize)
			LogPrint (eLogDebug, "Torrents: Saved bytes ", fragment.fileOffset, " - ",
				fragment.fileOffset + fragment.fragmentSize - 1, " to ", fragment.file->GetFullFilePath ());
		return success;
	}

	bool Piece::Load (PieceFileFragment && fragment)
	{
		std::lock_guard<std::mutex> l (m_Mutex);
		if (fragment.fragmentOffset > m_Size || fragment.fragmentSize > m_Size - fragment.fragmentOffset) return false;
		if (!m_Data) m_Data = std::make_unique<uint8_t[]> (m_Size);
		bool success
			= fragment.file->Load (fragment.fileOffset, m_Data.get () + fragment.fragmentOffset, fragment.fragmentSize);
		if (success)
		{
			LogPrint (eLogDebug, "Torrents: Loaded bytes ", fragment.fileOffset, " - ",
				fragment.fileOffset + fragment.fragmentSize - 1, " from ", fragment.file->GetFullFilePath ());
			return true;
		}
		else
		{
			LogPrint (eLogError, "Torrents: Failed to load bytes ", fragment.fileOffset, " - ",
				fragment.fileOffset + fragment.fragmentSize - 1, " from ", fragment.file->GetFullFilePath ());
			// Keep the (partial) buffer for the caller to inspect, but the
			// piece is no longer verified. The caller must MarkCorrupt().
			m_IsVerified = false;
			return false;
		}
	}

	bool Piece::HasData () const
	{
		std::lock_guard<std::mutex> l (m_Mutex);
		return static_cast<bool> (m_Data);
	}

	bool Piece::CopyBlock (size_t offset, size_t len, std::vector<uint8_t> & out) const
	{
		std::lock_guard<std::mutex> l (m_Mutex);
		// Unverified data may come from a resume bitfield or a previous
		// partial load; never serve it to a peer. The caller falls back to
		// the disk load path, which hashes the whole piece first.
		if (!m_IsVerified || m_IsSaving || m_IsVerificationPending) return false;
		if (!len || !m_Data || offset > m_Size || len > m_Size - offset) return false;
		if (!IsAvailableLocked ((int)(offset / REQUEST_BLOCK_SIZE))
			|| !IsAvailableLocked ((int)((offset + len - 1) / REQUEST_BLOCK_SIZE)))
			return false;
		out.assign (m_Data.get () + offset, m_Data.get () + offset + len);
		return true;
	}

	void Piece::NewDataBufferLocked ()
	{
		m_Data = std::make_unique<uint8_t[]> (m_Size);
	}

	void Piece::DeleteDataBufferLocked ()
	{
		m_Data.reset ();
	}

	bool Piece::HasBlock (size_t offset) const
	{
		std::lock_guard<std::mutex> l (m_Mutex);
		if (offset >= m_Size || m_IsSaving || m_IsVerificationPending) return false;
		return IsAvailableLocked (static_cast<int> (offset / REQUEST_BLOCK_SIZE));
	}

	std::pair<size_t, size_t> Piece::GetNextBlockToRequest ()
	{
		std::lock_guard<std::mutex> l (m_Mutex);
		if (m_Blocks)
		{
			size_t ind = 0;
			for (auto & it : *m_Blocks)
			{
				if (it == BlockStatus::Missing)
				{
					it = BlockStatus::Requested;
					auto offset = ind * REQUEST_BLOCK_SIZE;
					m_LastActivityTimestamp = i2p::util::GetMonotonicSeconds ();
					m_IsRequested = true;
					return { offset, (offset + REQUEST_BLOCK_SIZE <= m_Size) ? REQUEST_BLOCK_SIZE : m_Size - offset };
				}
				ind++;
			}
		}
		return { 0, 0 };
	}

	void Piece::ClearAllRequests ()
	{
		std::lock_guard<std::mutex> l (m_Mutex);
		++m_BlockEpoch;
		m_IsRequested = false;
		if (!m_Blocks) return;
		for (auto & it : *m_Blocks)
			if (it == BlockStatus::Requested) it = BlockStatus::Missing;
	}

	void Piece::ReleaseBlock (size_t offset, uint64_t epoch)
	{
		std::lock_guard<std::mutex> l (m_Mutex);
		if (epoch != m_BlockEpoch || !m_Blocks || offset >= m_Size) return;
		const size_t block = offset / REQUEST_BLOCK_SIZE;
		if (block >= m_Blocks->size () || (*m_Blocks)[block] != BlockStatus::Requested) return;
		(*m_Blocks)[block] = BlockStatus::Missing;
		m_IsRequested = std::any_of (
			m_Blocks->begin (), m_Blocks->end (), [] (BlockStatus status) { return status == BlockStatus::Requested; });
	}

	uint64_t Piece::GetBlockEpoch () const
	{
		std::lock_guard<std::mutex> l (m_Mutex);
		return m_BlockEpoch;
	}

	void Piece::MarkCorruptLocked ()
	{
		++m_BlockEpoch;
		m_Blocks = std::make_unique<std::vector<BlockStatus>> (GetNumBlocks (m_Size), BlockStatus::Missing);
		DeleteDataBufferLocked ();
		m_IsRequested = false;
		m_IsVerified = false;
		m_IsSaving = false;
		m_IsVerificationPending = false;
	}

	void Piece::MarkCorrupt ()
	{
		std::lock_guard<std::mutex> l (m_Mutex);
		MarkCorruptLocked ();
	}

	void Piece::ReleaseDataLocked ()
	{
		if (!m_Data || m_IsSaving || m_IsVerificationPending) return;
		DeleteDataBufferLocked ();
		// Verification is a property of the bytes that were written to disk,
		// not of the in-memory cache. Keep the flag so a piece evicted while
		// seeding still counts as verified for completion; a later reload
		// re-hashes the file before it is served again.
		LogPrint (eLogDebug, "Torrents: piece's data deleted");
	}

	void Piece::ReleaseData ()
	{
		std::lock_guard<std::mutex> l (m_Mutex);
		ReleaseDataLocked ();
	}

	void Piece::Reset ()
	{
		std::lock_guard<std::mutex> l (m_Mutex);
		if (m_Blocks)
		{
			++m_BlockEpoch;
			m_IsRequested = false;
			for (auto & it : *m_Blocks)
				if (it == BlockStatus::Requested) it = BlockStatus::Missing;
		}
		else
		{
			// A complete piece whose data is being reset must become
			// downloadable again. Deleting only the buffer would leave the
			// piece marked complete and later promote unverified data.
			MarkCorruptLocked ();
		}
	}

	Torrent::Torrent ()
		: m_AnnounceTrackerID (-1)
		, m_Length (0)
		, m_PieceLength (0)
		, m_IsComplete (false)
		, m_IsStopped (false)
		, m_IsSingleFile (true)
		, m_Uploaded (0)
		, m_Downloaded (0)
		, m_NextUpdateStatusTime (0)
		, m_NextReconnectTime (0)
		, m_NextDHTUpdateTime (0)
		, m_LastDHTAnnounceTime (0)
		, m_Error (eTorrentErrorNoError)
	{
		InitPeerID ();
	}

	void Torrent::InitPeerID ()
	{
		// A per-torrent random peer id: the tunnel-wide destination identity
		// must not be used as a peer id, and two torrents must not share one.
		static constexpr char alphabet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
		constexpr size_t prefixLength = 6; // "-I2PD-"
		constexpr size_t randomLength = 14;
		static_assert (prefixLength + randomLength == std::tuple_size_v<PeerID>, "peer id must stay 20 bytes");
		uint8_t random[randomLength] { };
		if (RAND_bytes (random, static_cast<int> (sizeof (random))) != 1)
		{
			// Never fall back to a predictable identity: derive from the
			// project RNG seed so the id is still per-process unpredictable.
			for (size_t i = 0; i < sizeof (random); ++i)
				random[i] = static_cast<uint8_t> (i2p::util::GetRngSeed () >> ((i % sizeof (uint32_t)) * 8));
		}
		memcpy (m_PeerID.data (), "-I2PD-", prefixLength);
		for (size_t i = 0; i < sizeof (random); ++i)
			m_PeerID[prefixLength + i] = alphabet[random[i] % (sizeof (alphabet) - 1)];
	}

	Torrent::Torrent (std::string_view buf)
		: Torrent ()
	{
		auto len = ParseDictionary (buf,
			[this] (std::string_view key, std::string_view buf) -> size_t
			{
				if (key == "announce")
				{
					auto [announce, l] = ExtractByteString (buf);
					if (l) m_Announce = announce;
					return l;
				}
				else if (key == "announce-list")
				{
					m_HasExplicitTrackers = true;
					size_t count = 0, tierCount = 0;
					auto outerLen = ParseList (buf,
						[this, &count, &tierCount] (std::string_view tier)
						{
							if (++tierCount > MAX_NUM_TRACKERS) m_Error = eTorrentErrorMalformedMetaInfo;
							std::vector<std::string> urls;
							auto len = ParseList (tier,
								[this, &urls, &count] (std::string_view entry)
								{
									auto [url, len] = ExtractByteString (entry);
									if (!len || ++count > MAX_NUM_TRACKERS || url.size () > MAX_TRACKER_ANNOUNCE_LENGTH)
									{
										m_Error = eTorrentErrorMalformedMetaInfo;
										return size_t { 0 };
									}
									if (!url.empty ()) urls.emplace_back (url);
									return len;
								});
							if (!len) m_Error = eTorrentErrorMalformedMetaInfo;
							if (len && !urls.empty () && tierCount <= MAX_NUM_TRACKERS)
								m_TrackerTiers.push_back (std::move (urls));
							return len;
						});
					if (!outerLen) m_Error = eTorrentErrorMalformedMetaInfo;
					return outerLen;
				}
				else if (key == "info")
					return ParseInfo (buf);
				else if (key == "comment")
				{
					auto [comment, l] = ExtractByteString (buf);
					if (!l) return 0;
					if (comment.size () <= 1024)
						m_MetaComment.assign (comment);
					else
						m_MetaInfoComplete = false;
					return l;
				}
				else if (key == "created by")
				{
					auto [createdBy, l] = ExtractByteString (buf);
					if (!l) return 0;
					if (createdBy.size () <= 255)
						m_CreatedBy.assign (createdBy);
					else
						m_MetaInfoComplete = false;
					return l;
				}
				else
				{
					// Preserve creation date, url-list, encoding and unknown
					// top-level keys as raw bencode. Tracker/metadata rewrites
					// must not silently discard data the engine does not use.
					auto rawLen = ParseBEncoded (buf, 1);
					if (!rawLen) return 0;
					size_t total = 0;
					for (const auto & [unused, value] : m_TopLevelExtras) total += value.size ();
					if (m_TopLevelExtras.size () >= 16 || total + rawLen > 16 * 1024)
						m_MetaInfoComplete = false;
					else
						m_TopLevelExtras.emplace_back (std::string (key), std::string (buf.substr (0, rawLen)));
					return rawLen;
				}
			});
		// Reject trailing data after the top-level dictionary. Accepting it
		// makes the parsed torrent and its info hash ambiguous to other
		// implementations.
		if (!len || len != buf.size ()) m_Error = eTorrentErrorMalformedMetaInfo;
		// A primary announce outside a non-empty announce-list is still part
		// of the tracker policy. Preserve it in tier 0 so announce, display
		// and removal all see the same endpoint; the info hash is unchanged.
		if (!m_Error && m_HasExplicitTrackers && !m_Announce.empty ())
		{
			bool present = false;
			size_t count = 0;
			for (const auto & tier : m_TrackerTiers)
				for (const auto & tracker : tier)
				{
					++count;
					if (tracker == m_Announce) present = true;
				}
			if (!present && count < MAX_NUM_TRACKERS)
			{
				if (m_TrackerTiers.empty ())
					m_TrackerTiers.push_back ({ m_Announce });
				else
					m_TrackerTiers.front ().insert (m_TrackerTiers.front ().begin (), m_Announce);
			}
		}
	}

	Torrent::Torrent (const InfoHash & infoHash)
		: Torrent ()
	{
		m_InfoHash = infoHash;
	}

	size_t Torrent::ParsePieces (std::string_view buf)
	{
		auto [hashes, len] = ExtractByteString (buf);
		if (!len)
		{
			m_Error = eTorrentErrorMalformedMetaInfo;
			return 0;
		}
		m_Pieces.clear ();
		if (!m_Length || m_PieceLength < MIN_PIECE_LENGTH || m_PieceLength > MAX_PIECE_LENGTH)
		{
			LogPrint (eLogError, "Torrents: Invalid length or piece length for pieces");
			m_Error = eTorrentErrorMalformedMetaInfo;
			return len;
		}
		// The number of hashes must exactly match the number of pieces
		// implied by the total length and the piece length. Reject
		// missing/extra hashes.
		size_t numPieces = m_Length / m_PieceLength;
		if (m_Length % m_PieceLength) numPieces++;
		const size_t blocksPerPiece = (m_PieceLength + REQUEST_BLOCK_SIZE - 1) / REQUEST_BLOCK_SIZE;
		if (blocksPerPiece && numPieces > MAX_TORRENT_BLOCKS / blocksPerPiece)
		{
			LogPrint (eLogError, "Torrents: Piece/block count exceeds the memory budget");
			m_Error = eTorrentErrorMalformedMetaInfo;
			return len;
		}
		if (!numPieces || numPieces > MAX_NUM_TORRENT_PIECES || hashes.size () % SHA_DIGEST_LENGTH
			|| hashes.size () / SHA_DIGEST_LENGTH != numPieces)
		{
			LogPrint (
				eLogError, "Torrents: Inconsistent pieces length ", hashes.size (), " expected ", numPieces, " pieces");
			m_Error = eTorrentErrorMalformedMetaInfo;
			return len;
		}
		m_Pieces.reserve (numPieces);
		size_t totalLen = 0;
		for (size_t i = 0; i < numPieces; i++)
		{
			// Subtraction form: totalLen + m_PieceLength could wrap on ILP32
			// for a torrent close to the size_t maximum.
			auto l = std::min (m_PieceLength, m_Length - totalLen);
			m_Pieces.emplace_back (
				l, (const uint8_t *)hashes.substr (i * SHA_DIGEST_LENGTH, SHA_DIGEST_LENGTH).data ());
			totalLen += l;
		}
		return len;
	}

	size_t Torrent::ParseInfo (std::string_view buf)
	{
		int version = 0; // unknown
		std::string_view piecesValue;
		bool hasPieces = false, hasFiles = false, hasLength = false;
		size_t len = ParseDictionary (buf,
			[this, &version, &piecesValue, &hasPieces, &hasFiles, &hasLength] (
				std::string_view key, std::string_view buf) -> size_t
			{
				if (key == "length")
				{
					auto [value, l] = ExtractInteger (buf);
					if (!l) return 0;
					hasLength = true;
					// MAX_TORRENT_LENGTH is a 64-bit bound. It must not be
					// truncated into size_t on ILP32, so reject values that
					// cannot be represented by the platform size type.
					if (value < 0 || (uint64_t)value > MAX_TORRENT_LENGTH
						|| (uint64_t)value > (uint64_t)std::numeric_limits<size_t>::max ())
					{
						LogPrint (eLogError, "Torrents: Invalid length ", value);
						m_Error = eTorrentErrorMalformedMetaInfo;
					}
					else
						m_Length = (size_t)value;
					return l;
				}
				else if (key == "name")
				{
					auto [name, l] = ExtractByteString (buf);
					if (l)
					{
						m_Name = AdjustName (name);
						if (m_Name.empty () && !name.empty ())
						{
							LogPrint (eLogError, "Torrents: Unsafe name in torrent: ", name);
							m_Error = eTorrentErrorMalformedMetaInfo;
						}
					}
					return l;
				}
				else if (key == "private")
				{
					auto [value, l] = ExtractInteger (buf);
					if (!l || (value != 0 && value != 1)) m_Error = eTorrentErrorMalformedMetaInfo;
					m_IsPrivate = value == 1;
					return l;
				}
				else if (key == "piece length")
				{
					auto [value, l] = ExtractInteger (buf);
					if (!l) return 0;
					if (value < 0 || (uint64_t)value < MIN_PIECE_LENGTH || (uint64_t)value > MAX_PIECE_LENGTH)
					{
						LogPrint (eLogError, "Torrents: Invalid piece length ", value);
						m_Error = eTorrentErrorMalformedMetaInfo;
					}
					else
						m_PieceLength = (size_t)value;
					return l;
				}
				else if (key == "pieces")
				{
					auto [hashes, l] = ExtractByteString (buf);
					if (!l) return 0;
					version = 1; // v1 or hybrid
					piecesValue = buf;
					hasPieces = true;
					return l;
				}
				else if (key == "files")
				{
					hasFiles = true;
					return ParseFiles (buf);
				}
				else if (key == "meta version")
				{
					auto [value, l] = ExtractInteger (buf);
					if (l && !version && value >= 0 && value <= 2) // no pieces yet, v1/v2
						version = static_cast<int> (value);
					return l;
				}
				return 0;
			});
		if (!len)
		{
			m_Error = eTorrentErrorMalformedMetaInfo;
			return 0;
		}
		// "length" and "files" are mutually exclusive in v1 metadata.
		if (hasFiles && hasLength)
		{
			LogPrint (eLogError, "Torrents: Both length and files are present");
			m_Error = eTorrentErrorMalformedMetaInfo;
		}
		if (hasPieces)
			ParsePieces (piecesValue);
		else if (version == 1)
		{
			LogPrint (eLogError, "Torrents: v1 torrent without pieces");
			m_Error = eTorrentErrorMalformedMetaInfo;
		}
		if (m_Error == eTorrentErrorNoError && version != 1)
		{
			LogPrint (eLogError, "Torrents: Torrent version ", version, " is not supported");
			m_Error = eTorrentErrorNonSupportedVersion;
		}
		// Every supported (v1/hybrid) metainfo requires a non-empty root
		// name. Without it the download path would be the torrents directory
		// itself and the torrent would have no usable file entry.
		if (m_Error == eTorrentErrorNoError && m_Name.empty ())
		{
			LogPrint (eLogError, "Torrents: Missing or empty torrent name");
			m_Error = eTorrentErrorMalformedMetaInfo;
		}
		if (m_Error == eTorrentErrorNoError && m_IsSingleFile && !m_Name.empty ()) // single file
			m_Files.emplace_back (std::make_shared<TorrentFile> (m_Name, m_Length));
		if (m_Error == eTorrentErrorNoError)
		{
			m_FilePriorities.assign (m_Files.size (), 1);
			RebuildPiecePriorities ();
		}
		// save info
		m_Info.resize (len);
		memcpy (m_Info.data (), (const uint8_t *)buf.data (), len);
		// calculate info hash
		SHA1 (m_Info.data (), len, m_InfoHash.data ());
		// Metadata arriving after the fact (magnet/BEP9) can turn the torrent
		// into a private swarm. Candidates learned from public gossip before
		// that point must not survive the transition.
		if (m_IsPrivate) ClearPeerCandidates ();
		if (m_Error) m_IsStopped = true;
		return len;
	}

	std::string Torrent::CreateTorrentFileContent () const
	{
		if (m_Info.empty () || !m_MetaInfoComplete) return "";
		// std::deque keeps earlier values stable while add() appends more, so
		// the string_views stored in fields cannot dangle after a
		// reallocation.
		std::deque<std::string> owned;
		std::vector<std::pair<std::string_view, std::string_view>> fields;
		auto add = [&owned, &fields] (std::string_view key, std::string value)
		{
			owned.push_back (std::move (value));
			fields.emplace_back (key, owned.back ());
		};
		if (!m_Announce.empty ()) add ("announce", CreateByteString (m_Announce));
		std::vector<std::string> tiers;
		for (const auto & tier : m_TrackerTiers)
		{
			std::vector<std::string> urls;
			for (const auto & url : tier) urls.push_back (CreateByteString (url));
			tiers.push_back (CreateList (urls));
		}
		if (!tiers.empty () || m_HasExplicitTrackers) add ("announce-list", CreateList (tiers));
		if (!m_MetaComment.empty ()) add ("comment", CreateByteString (m_MetaComment));
		if (!m_CreatedBy.empty ()) add ("created by", CreateByteString (m_CreatedBy));
		for (const auto & [key, value] : m_TopLevelExtras) fields.emplace_back (key, value);
		fields.emplace_back ("info", std::string_view ((const char *)m_Info.data (), m_Info.size ()));
		return CreateDictionary (fields);
	}

	bool Torrent::SetMetaComment (std::string_view comment)
	{
		if (!m_MetaInfoComplete) return false;
		if (!ValidMetaText (comment, 1024)) return false;
		m_MetaComment.assign (comment);
		return true;
	}

	bool Torrent::SetCreatedBy (std::string_view createdBy)
	{
		if (!m_MetaInfoComplete) return false;
		if (!ValidMetaText (createdBy, 255)) return false;
		m_CreatedBy.assign (createdBy);
		return true;
	}

	void Torrent::SetCommentsEnabled (bool enabled)
	{
		m_Comments.SetEnabled (enabled);
		for (auto & connection : GetConnections ()) connection->RefreshComments ();
	}

	void Torrent::PublishComments ()
	{
		if (!m_Comments.IsEnabled () || IsPrivate () || !IsValid ()) return;
		for (auto & connection : GetConnections ())
			if (connection) connection->RefreshComments ();
	}

	bool Torrent::AdmitCommentEntries (size_t count, uint64_t nowSeconds)
	{
		return m_CommentAdmission.Admit (count, MAX_COMMENT_ENTRIES_PER_TORRENT_WINDOW, nowSeconds);
	}

	bool Torrent::SetTrackerTiers (const TrackerTiers & tiers)
	{
		if (tiers.size () > MAX_NUM_TRACKERS) return false;
		size_t count = 0;
		for (const auto & tier : tiers)
			for (const auto & url : tier)
				if (++count > MAX_NUM_TRACKERS || url.empty () || url.size () > MAX_TRACKER_ANNOUNCE_LENGTH)
					return false;
		m_TrackerTiers = tiers;
		m_HasExplicitTrackers = true;
		return true;
	}

	std::shared_ptr<Torrent> Torrent::CreateV1 (std::string_view name,
		const std::vector<std::pair<std::filesystem::path, size_t>> & files, size_t pieceLength,
		const std::vector<InfoHash> & pieceHashes, const TrackerTiers & trackers, bool privateTorrent)
	{
		const bool hasTracker = std::any_of (
			trackers.begin (), trackers.end (), [] (const std::vector<std::string> & tier) { return !tier.empty (); });
		if (name.empty () || AdjustName (name) != name || files.empty () || files.size () > MAX_NUM_TORRENT_FILES
			|| pieceLength < MIN_PIECE_LENGTH || pieceLength > MAX_PIECE_LENGTH || pieceHashes.empty ()
			|| pieceHashes.size () > MAX_NUM_TORRENT_PIECES || (privateTorrent && !hasTracker))
			return nullptr;
		const bool single = files.size () == 1 && files.front ().first.empty ();
		uint64_t total = 0;
		std::vector<std::string> entries;
		for (const auto & [path, length] : files)
		{
			if (length > MAX_TORRENT_LENGTH - total) return nullptr;
			total += length;
			if (single) continue;
			if (!IsSafeRelativePath (path)) return nullptr;
			std::vector<std::string> components;
			for (const auto & component : path)
			{
				auto text = component.string ();
				if (AdjustName (text) != text) return nullptr;
				components.push_back (CreateByteString (text));
			}
			entries.push_back (
				CreateDictionary ({ { "length", CreateInteger (length) }, { "path", CreateList (components) } }));
		}
		if (!total || total > std::numeric_limits<size_t>::max ()
			|| total / pieceLength + (total % pieceLength != 0) != pieceHashes.size ())
			return nullptr;
		std::string hashes;
		hashes.reserve (pieceHashes.size () * SHA_DIGEST_LENGTH);
		for (const auto & hash : pieceHashes)
			hashes.append (reinterpret_cast<const char *> (hash.data ()), hash.size ());
		// Keep owned encoded strings alive while CreateDictionary consumes
		// views.
		auto encodedFiles = single ? CreateInteger (files.front ().second) : CreateList (entries);
		auto encodedName = CreateByteString (name);
		auto encodedLength = CreateInteger (pieceLength);
		auto encodedHashes = CreateByteString (hashes);
		auto encodedPrivate = CreateInteger (1);
		std::vector<std::pair<std::string_view, std::string_view>> fields { { single ? "length" : "files",
																				encodedFiles },
			{ "name", encodedName }, { "piece length", encodedLength }, { "pieces", encodedHashes } };
		if (privateTorrent) fields.emplace_back ("private", encodedPrivate);
		auto info = CreateDictionary (fields);
		auto result = std::make_shared<Torrent> (CreateDictionary ({ { "info", info } }));
		if (result->GetError () || !result->IsValid ()) return nullptr;
		if (!trackers.empty () && !result->SetTrackerTiers (trackers)) return nullptr;
		for (const auto & tier : result->m_TrackerTiers)
			if (!tier.empty ())
			{
				result->m_Announce = tier.front ();
				break;
			}
		return result;
	}

	std::string Torrent::AdjustName (std::string_view name)
	{
		if (name.empty () || name == "." || name == "..") return "";
		if (name.size () > MAX_TORRENT_PATH_COMPONENT_LENGTH) return "";
		// Absolute paths and Windows drive/UNC prefixes must never be
		// accepted as a component. Separators inside a component are
		// replaced, not interpreted.
		if (name.front () == '/' || name.front () == '\\') return "";
#ifdef _WIN32
		if (name.back () == '.' || name.back () == ' ') return ""; // Windows drops those
#endif
		std::string adjustedName;
		for (char ch : name)
		{
			auto c = (unsigned char)ch;
			if (c < 0x20 || c == 0x7f) return ""; // NUL and control characters
			if (ch == '/' || ch == '\\' || ch == ':' || ch == '<' || ch == '>' || ch == '"' || ch == '|' || ch == '?'
				|| ch == '*')
				adjustedName.push_back ('_');
			else
				adjustedName.push_back (ch);
		}

#ifdef _WIN32
		static constexpr std::array reserved { "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5",
			"COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9" };
		std::string stem (name.substr (0, name.find ('.')));
		boost::to_upper (stem);
		if (std::find (reserved.begin (), reserved.end (), stem) != reserved.end ()) return "";
#endif
		return adjustedName;
	}

	size_t Torrent::ParseFiles (std::string_view buf)
	{
		m_IsSingleFile = false;
		m_Length = 0;
		m_Files.clear ();
		std::set<std::string> filePaths, directoryPaths;
		size_t totalLength = 0;
		auto len = ParseList (buf,
			[this, &filePaths, &directoryPaths, &totalLength] (std::string_view file) -> size_t
			{
				std::filesystem::path filePath;
				size_t fileLength = 0;
				bool hasPath = false, hasLength = false;
				auto len = ParseDictionary (file,
					[this, &filePath, &fileLength, &hasPath, &hasLength] (
						std::string_view key, std::string_view value) -> size_t
					{
						if (key == "path")
						{
							hasPath = true;
							auto [subdirs, l] = ParseStringList (value);
							if (!l) return 0;
							if (subdirs.empty () || subdirs.size () > MAX_TORRENT_PATH_COMPONENTS)
							{
								LogPrint (eLogError, "Torrents: Invalid number of path components ", subdirs.size ());
								m_Error = eTorrentErrorMalformedMetaInfo;
								return l;
							}
							for (const auto & it : subdirs)
							{
								auto name = AdjustName (it);
								if (name.empty ())
								{
									LogPrint (eLogError, "Torrents: Unsafe path component in torrent: ", it);
									filePath.clear ();
									m_Error = eTorrentErrorMalformedMetaInfo;
									return l;
								}
								filePath /= name;
							}
							return l;
						}
						else if (key == "length")
						{
							hasLength = true;
							auto [length, l] = ExtractInteger (value);
							if (!l) return 0;
							if (length < 0 || (uint64_t)length > MAX_TORRENT_LENGTH
								|| (uint64_t)length > (uint64_t)std::numeric_limits<size_t>::max ())
							{
								LogPrint (eLogError, "Torrents: Invalid file length ", length);
								m_Error = eTorrentErrorMalformedMetaInfo;
							}
							else
								fileLength = (size_t)length;
							return l;
						}
						return 0;
					});
				if (!len || !hasPath || !hasLength || filePath.empty ())
				{
					m_Error = eTorrentErrorMalformedMetaInfo;
					return len;
				}
				if (m_Error != eTorrentErrorNoError) return len;
				// Perform the sum and the limit check in 64 bits: on ILP32
				// MAX_TORRENT_LENGTH - totalLength would otherwise be a huge
				// size_t and totalLength += fileLength could wrap.
				if (totalLength > MAX_TORRENT_LENGTH
					|| (uint64_t)fileLength > MAX_TORRENT_LENGTH - (uint64_t)totalLength
					|| fileLength > std::numeric_limits<size_t>::max () - totalLength)
				{
					LogPrint (eLogError, "Torrents: Total torrent length exceeds limit");
					m_Error = eTorrentErrorMalformedMetaInfo;
					return len;
				}
				// Normalise components for collision checks. Windows paths are case
				// insensitive, so fold them there as well.
				std::vector<std::string> components;
				for (const auto & part : filePath)
				{
					std::string component = part.string ();
#ifdef _WIN32
					boost::to_lower (component);
#endif
					components.push_back (std::move (component));
				}
				std::string normalized;
				for (size_t i = 0; i < components.size (); i++)
				{
					if (i) normalized.push_back ('/');
					normalized += components[i];
				}
				bool collision
					= normalized.empty () || filePaths.contains (normalized) || directoryPaths.contains (normalized);
				std::string prefix;
				for (size_t i = 0; !collision && i + 1 < components.size (); i++)
				{
					if (i) prefix.push_back ('/');
					prefix += components[i];
					if (filePaths.contains (prefix)) collision = true; // file used as a directory
				}
				if (collision)
				{
					LogPrint (eLogError, "Torrents: Duplicate or colliding torrent path ", normalized);
					m_Error = eTorrentErrorMalformedMetaInfo;
					return len;
				}
				if (m_Files.size () >= MAX_NUM_TORRENT_FILES)
				{
					LogPrint (eLogError, "Torrents: Too many files in torrent");
					m_Error = eTorrentErrorMalformedMetaInfo;
					return len;
				}
				filePaths.insert (normalized);
				prefix.clear ();
				for (size_t i = 0; i + 1 < components.size (); i++)
				{
					if (i) prefix.push_back ('/');
					prefix += components[i];
					directoryPaths.insert (prefix);
				}
				totalLength += fileLength;
				m_Files.emplace_back (std::make_shared<TorrentFile> (filePath, fileLength));
				return len;
			});
		if (!len)
		{
			m_Error = eTorrentErrorMalformedMetaInfo;
			return 0;
		}
		m_Length = totalLength;
		return len;
	}

	std::string Torrent::GetHexStringInfoHash () const
	{
		static constexpr char hex[] = "0123456789abcdef";
		std::string infoHash;
		infoHash.reserve (m_InfoHash.size () * 3);
		for (auto it : m_InfoHash)
		{
			infoHash.push_back ('%');
			infoHash.push_back (hex[it >> 4]);
			infoHash.push_back (hex[it & 0x0f]);
		}
		return infoHash;
	}

	size_t Torrent::GetLeft () const
	{
		const auto wanted = GetSelectedWantedBytes ();
		const auto verified = GetSelectedVerifiedBytes ();
		return wanted > verified ? wanted - verified : 0;
	}

	size_t Torrent::GetFullLeft () const
	{
		if (IsComplete () && AreAllPiecesVerified ()) return 0;
		size_t completed = 0;
		for (const auto & it : m_Pieces)
			if (it.IsComplete ()) completed += it.GetSize ();
		return m_Length > completed ? m_Length - completed : 0;
	}

	AnnounceProgress Torrent::GetAnnounceProgress () const
	{
		AnnounceProgress progress;
		// BEP 3 "left" is the whole torrent, exactly like Java's
		// TrackerClient. File selection decides what is requested locally,
		// never what the swarm is told: announcing the selected-file
		// remainder would send left=0 and make a partially selected torrent
		// look like a seeder. Without metadata yet (magnet/BEP 9 pending)
		// there is no piece to account for, so the phony left=1 keeps us in
		// the leecher set.
		progress.left = m_Pieces.empty () ? 1 : static_cast<int64_t> (GetFullLeft ());
		// "completed" is a whole-torrent event. It is only truthful once
		// every piece has been hashed against the metainfo, not merely every
		// selected piece. IsComplete()/GetStatus()/TUI progress stay
		// selection-based, so a skip-selected torrent is still shown as
		// seeding locally. A torrent without metadata has no pieces and is
		// never "completed" here; once the metadata is parsed a zero-length
		// payload is still rejected earlier.
		progress.completed = !m_Pieces.empty () && IsValid () && AreAllPiecesVerified ();
		return progress;
	}

	int TrackerSchedule::AdvertisedInterval (int64_t intervalSeconds)
	{
		constexpr int64_t maxSeconds = MAX_TRACKER_REQUESTS_INTERVAL / 1000;
		// Negative, zero and absurd values are clamped, never trusted.
		const int64_t seconds = std::clamp<int64_t> (intervalSeconds, MIN_TRACKER_ANNOUNCE_INTERVAL, maxSeconds);
		return static_cast<int> (seconds * 1000);
	}

	int TrackerSchedule::AdvertisedMinInterval (int64_t minIntervalSeconds)
	{
		// Only the local sanity range applies: a tracker minimum below the
		// announce floor is stored as announced and cannot shorten it.
		constexpr int64_t minSeconds = MIN_TRACKER_REQUESTS_INTERVAL / 1000;
		constexpr int64_t maxSeconds = MAX_TRACKER_REQUESTS_INTERVAL / 1000;
		const int64_t seconds = std::clamp<int64_t> (minIntervalSeconds, minSeconds, maxSeconds);
		return static_cast<int> (seconds * 1000);
	}

	int TrackerSchedule::EffectiveInterval (int interval, int minInterval)
	{
		const int advertised = std::max (interval, minInterval);
		return std::max (advertised, MIN_TRACKER_ANNOUNCE_INTERVAL * 1000);
	}

	int TrackerSchedule::DefaultInterval (int minInterval)
	{
		return EffectiveInterval (MIN_TRACKER_ANNOUNCE_INTERVAL * 1000, minInterval);
	}

	int TrackerSchedule::FailureInterval (int previousFailureInterval, int minInterval)
	{
		const int64_t floor = std::max<int64_t> (MIN_TRACKER_REQUESTS_INTERVAL, minInterval);
		// A tracker that demands a longer minimum interval still governs; the
		// failure cap must never undercut its advertised contract.
		const int64_t cap = std::max<int64_t> (MAX_TRACKER_FAILURE_BACKOFF * 1000LL, minInterval);
		const int64_t next = previousFailureInterval > 0 ? static_cast<int64_t> (previousFailureInterval) * 2 : floor;
		return static_cast<int> (std::min (std::max (next, floor), cap));
	}

	void Torrent::ParseTrackerResponse (size_t trackerID, std::string_view buf)
	{
		CheckTrackerStatsSize (trackerID);
		std::get<6> (m_TrackerStats[trackerID]) = ""; // clear error
		if (buf.size () > MAX_TRACKER_RESPONSE_BODY)
		{
			// A tracker cannot need an unbounded body to describe a bounded
			// peer set. Refuse instead of truncating a bencoded document.
			LogPrint (eLogWarning, "Torrents: Ignoring oversized tracker response of ", buf.size (), " bytes");
			ReportTrackerFailure (trackerID, "oversized response");
			return;
		}
		bool sawInterval = false, failed = false;
		const auto parsed = ParseDictionary (buf,
			[this, trackerID, &sawInterval, &failed] (std::string_view key, std::string_view buf) -> size_t
			{
				if (key == "interval")
				{
					auto [value, l] = ExtractInteger (buf);
					if (l)
					{
						sawInterval = true;
						// Seconds on the wire; floored at 15 minutes like Java's
						// TrackerClient and raised to a larger "min interval".
						SetInterval (trackerID, TrackerSchedule::AdvertisedInterval (value));
						std::get<2> (m_TrackerStats[trackerID])
							= i2p::util::GetMonotonicMilliseconds () + GetInterval (trackerID); // reset next request
					}
					return l;
				}
				else if (key == "min interval" || key == "min_interval")
				{
					auto [value, l] = ExtractInteger (buf);
					if (l)
					{
						SetMinInterval (trackerID, TrackerSchedule::AdvertisedMinInterval (value));
						// A tracker that publishes a larger minimum interval must
						// not be polled faster than that, whichever of the two
						// keys the response listed first.
						auto & interval = std::get<1> (m_TrackerStats[trackerID]);
						const int effective = TrackerSchedule::EffectiveInterval (interval, GetMinInterval (trackerID));
						if (effective != interval)
						{
							interval = effective;
							std::get<2> (m_TrackerStats[trackerID]) = i2p::util::GetMonotonicMilliseconds () + interval;
						}
					}
					return l;
				}
				else if (key == "tracker id")
				{
					auto [id, l] = ExtractByteString (buf);
					// Opaque, echoed back verbatim; drop anything unusable.
					if (l && id.size () <= MAX_TRACKER_ID_LENGTH && !id.empty ()) SetTrackerID (trackerID, id);
					return l;
				}
				else if (key == "peers")
					return ParsePeers (trackerID, buf);
				else if (key == "complete")
				{
					auto [seeders, l] = ExtractInteger (buf);
					if (l) std::get<3> (m_TrackerStats[trackerID]) = ClampTrackerCount (seeders);
					return l;
				}
				else if (key == "incomplete")
				{
					auto [leechers, l] = ExtractInteger (buf);
					if (l) std::get<4> (m_TrackerStats[trackerID]) = ClampTrackerCount (leechers);
					return l;
				}
				else if (key == "failure reason")
				{
					auto [reason, l] = ExtractByteString (buf);
					failed = true;
					// A tracker-level failure is a consecutive failure: retry on
					// the same bounded ladder as a transport failure instead of
					// polling the tracker more often than it asked.
					ReportTrackerFailure (trackerID, reason);
					LogPrint (eLogError, "Torrents: Tracker error: ", std::get<6> (m_TrackerStats[trackerID]));
					return l;
				}
				return 0;
			});
		if (!parsed || parsed != buf.size ())
		{
			ReportTrackerFailure (trackerID, "malformed response");
			return;
		}
		if (failed) return;
		// A successful response ends the consecutive-failure ladder.
		std::get<8> (m_TrackerStats[trackerID]) = 0;
		if (!sawInterval)
		{
			auto & interval = std::get<1> (m_TrackerStats[trackerID]);
			interval = TrackerSchedule::DefaultInterval (GetMinInterval (trackerID));
			std::get<2> (m_TrackerStats[trackerID]) = i2p::util::GetMonotonicMilliseconds () + interval;
		}
		std::get<5> (m_TrackerStats[trackerID]) = i2p::util::GetSecondsSinceEpoch ();
	}

	size_t Torrent::ParsePeers (size_t trackerID, std::string_view buf)
	{
		auto & peers = std::get<0> (m_TrackerStats[trackerID]);
		peers.clear ();
		if (buf.empty ()) return 0;
		if (buf.front () == 'l')
		{
			// BEP 3 list-of-dicts form: an I2P tracker encodes the 32-byte
			// destination hash in "ip" with I2P base64. "port" is meaningless
			// on I2P and is ignored. Each malformed entry is skipped on its
			// own without discarding the rest of the response.
			auto len = ParseList (buf,
				[&peers] (std::string_view entry) -> size_t
				{
					auto [dict, dictLen] = ExtractDictionary (entry);
					if (!dictLen) return 0; // malformed entry: let ParseList skip it
					auto ip = dict.find ("ip");
					if (ip == dict.end ()) return dictLen;
					auto [encoded, encodedLen] = ExtractByteString (ip->second);
					if (!encodedLen) return dictLen;
					std::array<uint8_t, i2p::data::IdentHash::len> hash { };
					if (i2p::data::Base64ToByteStream (encoded, hash.data (), hash.size ()) != hash.size ())
						return dictLen;
					if (peers.size () >= MAX_PEERS_PER_TRACKER_RESPONSE) return dictLen;
					i2p::data::IdentHash ident (hash.data ());
					if (!ident.IsZero ()) peers.emplace (std::move (ident));
					return dictLen;
				});
			if (!len)
			{
				LogPrint (eLogWarning, "Torrents: Malformed tracker peers list");
				peers.clear ();
			}
			return len;
		}
		auto [hashes, len] = ExtractByteString (buf);
		if (!len) return 0;
		size_t count = 0;
		while (hashes.length () >= i2p::data::IdentHash::len && count < MAX_PEERS_PER_TRACKER_RESPONSE)
		{
			i2p::data::IdentHash ident (
				reinterpret_cast<const uint8_t *> (hashes.substr (0, i2p::data::IdentHash::len).data ()));
			if (!ident.IsZero ()) peers.emplace (std::move (ident));
			hashes.remove_prefix (i2p::data::IdentHash::len);
			count++;
		}
		return len;
	}

	std::string Torrent::GetTrackerID (size_t trackerID) const
	{
		auto it = m_TrackerIDs.find (trackerID);
		return it != m_TrackerIDs.end () ? it->second : std::string { };
	}

	void Torrent::SetTrackerID (size_t trackerID, std::string_view id)
	{
		if (id.empty () || id.size () > MAX_TRACKER_ID_LENGTH) return;
		// Bound the map independently of how many tracker ids a hostile
		// response claims to have seen.
		if (!m_TrackerIDs.contains (trackerID) && m_TrackerIDs.size () >= MAX_NUM_TRACKERS) return;
		m_TrackerIDs[trackerID] = id;
	}

	void Torrent::SetMinInterval (size_t trackerID, int interval)
	{
		CheckTrackerStatsSize (trackerID);
		std::get<7> (m_TrackerStats[trackerID])
			= std::clamp (interval, MIN_TRACKER_REQUESTS_INTERVAL, MAX_TRACKER_REQUESTS_INTERVAL);
	}

	void Torrent::HandleDatagramTrackerResponse (
		size_t trackerID, uint32_t interval, const uint8_t * hashes, size_t hashesLen, int numSeeders, int numLeechers)
	{
		CheckTrackerStatsSize (trackerID);
		auto & [peers, trackerRequestInterval, nextRequestTime, seeders, leechers, lastUpdateTime, error, minimum,
			failureInterval] = m_TrackerStats[trackerID];
		error = "";
		// BEP 15 carries the advertised interval in seconds, just like the
		// HTTP response, so the same 15-minute floor and "min interval" rule
		// apply.
		trackerRequestInterval = TrackerSchedule::EffectiveInterval (
			TrackerSchedule::AdvertisedInterval (static_cast<int64_t> (interval)), minimum);
		nextRequestTime = i2p::util::GetMonotonicMilliseconds () + trackerRequestInterval;
		failureInterval = 0; // a successful response ends the retry ladder
		seeders = ClampTrackerCount (numSeeders);
		leechers = ClampTrackerCount (numLeechers);
		lastUpdateTime = i2p::util::GetSecondsSinceEpoch ();
		peers.clear ();
		size_t offset = 0;
		while (offset + i2p::data::IdentHash::len <= hashesLen && peers.size () < MAX_PEERS_PER_TRACKER_RESPONSE)
		{
			i2p::data::IdentHash ident (hashes + offset);
			offset += i2p::data::IdentHash::len;
			if (ident.IsZero ()) continue;
			peers.emplace (std::move (ident));
		}
	}

	void Torrent::SetTrackerError (size_t trackerID, std::string_view error)
	{
		CheckTrackerStatsSize (trackerID);
		// Bound hostile failure strings before they enter the IPC snapshot.
		std::get<6> (m_TrackerStats[trackerID]).assign (error.substr (0, 512));
	}

	void Torrent::ReportTrackerFailure (size_t trackerID, std::string_view reason)
	{
		CheckTrackerStatsSize (trackerID);
		SetTrackerError (trackerID, reason);
		// Retries follow the internal ladder and never the tracker-advertised
		// interval: the first attempt after a failure waits
		// MIN_TRACKER_REQUESTS_INTERVAL (15 s), each further failure doubles
		// it, and it stops at MAX_TRACKER_FAILURE_BACKOFF (30 min, Java
		// LONG_SLEEP).
		const int interval
			= TrackerSchedule::FailureInterval (std::get<8> (m_TrackerStats[trackerID]), GetMinInterval (trackerID));
		std::get<8> (m_TrackerStats[trackerID]) = interval;
		std::get<2> (m_TrackerStats[trackerID]) = i2p::util::GetMonotonicMilliseconds () + interval;
	}

	void Torrent::ExpireTrackerPeers (uint64_t nowEpoch)
	{
		for (auto & [peers, interval, next, seeders, leechers, lastUpdate, error, minimum, failureInterval] :
			m_TrackerStats)
		{
			if (!lastUpdate || nowEpoch < lastUpdate) continue;
			const uint64_t ttl = std::min<uint64_t> (
				std::max<uint64_t> (static_cast<uint64_t> (interval) / 1000 * 2, 30 * 60), 2 * 3600);
			if (nowEpoch - lastUpdate > ttl)
			{
				// Keep lastUpdate/error so a genuinely silent tracker can still
				// escalate to "No response"; only the stale candidates go.
				peers.clear ();
				seeders = 0;
				leechers = 0;
			}
		}
	}

	void Torrent::CheckTrackerStatsSize (size_t trackerID)
	{
		if (trackerID >= m_TrackerStats.size ())
			m_TrackerStats.resize (trackerID + 1,
				TrackerStats { { }, MIN_TRACKER_REQUESTS_INTERVAL, 0, 0, 0, i2p::util::GetSecondsSinceEpoch (), "",
					MIN_TRACKER_REQUESTS_INTERVAL, 0 });
	}

	void Torrent::SetInterval (size_t trackerID, int interval)
	{
		CheckTrackerStatsSize (trackerID);
		// Interval is in milliseconds. The effective value is floored at
		// MIN_TRACKER_ANNOUNCE_INTERVAL and raised to a larger advertised
		// minimum, so no caller can poll a tracker faster than the floor.
		std::get<1> (m_TrackerStats[trackerID])
			= TrackerSchedule::EffectiveInterval (interval, GetMinInterval (trackerID));
	}

	std::pair<std::vector<uint8_t>, boost::logic::tribool> Torrent::CreateBitfield () const
	{
		size_t numPieces = m_Pieces.size ();
		size_t bitfieldSize = numPieces / 8;
		if (numPieces % 8) bitfieldSize++;
		if (!bitfieldSize) return { { }, false }; // magnet, have none
		std::vector<uint8_t> ret (bitfieldSize); // filled with 0
		bool none = true, all = true;
		size_t idx = 0;
		for (size_t i = 0; i < ret.size (); i++) // bytes
		{
			uint8_t bit = 0x80;
			for (int j = 0; j < 8; j++)
			{
				if (idx >= numPieces) break;
				if (m_Pieces[idx].IsComplete () && !m_Pieces[idx].IsSaving ())
				{
					ret[i] |= bit;
					none = false;
				}
				else
					all = false;
				bit >>= 1;
				idx++;
			}
		}
		return { ret,
			all ? boost::logic::tribool (true) : (none ? boost::logic::tribool (false) : boost::logic::indeterminate) };
	}

	bool Torrent::IsBitfieldValid (const std::vector<uint8_t> & bitfield) const
	{
		const size_t numPieces = m_Pieces.size ();
		const size_t expectedSize = (numPieces + 7) / 8;
		if (bitfield.size () != expectedSize) return false;
		if (numPieces % 8)
		{
			const uint8_t validMask = static_cast<uint8_t> (0xFFu << (8 - (numPieces % 8)));
			if (bitfield.back () & static_cast<uint8_t> (~validMask)) return false;
		}
		return true;
	}

	bool Torrent::ApplyBitfield (const std::vector<uint8_t> & bitfield)
	{
		const size_t numPieces = m_Pieces.size ();
		if (!IsBitfieldValid (bitfield)) return false;
		bool complete = true;
		size_t idx = 0;
		for (size_t i = 0; i < bitfield.size (); i++)
		{
			uint8_t bit = 0x80;
			for (int j = 0; j < 8; j++)
			{
				if (idx >= numPieces) break;
				if (bitfield[i] & bit)
					m_Pieces[idx].Complete ();
				else
					complete = false;
				bit >>= 1;
				idx++;
			}
			if (idx >= numPieces) break;
		}
		return complete;
	}

	bool Torrent::AreAllPiecesVerified () const
	{
		for (const auto & it : m_Pieces)
			if (!it.IsComplete () || !it.IsVerified () || it.IsSaving ()) return false;
		return !m_Pieces.empty ();
	}

	bool Torrent::AreAllSelectedPiecesVerified () const
	{
		if (m_PiecePriorities.size () != m_Pieces.size ()) return false;
		for (size_t index = 0; index < m_Pieces.size (); ++index)
		{
			if (!GetPiecePriority (index)) continue;
			const auto & piece = m_Pieces[index];
			if (!piece.IsComplete () || !piece.IsVerified () || piece.IsSaving ()) return false;
		}
		return true;
	}

	bool Torrent::IsFileComplete (size_t index) const
	{
		if (index >= m_Files.size () || !m_PieceLength) return false;
		size_t offset = 0, current = 0;
		for (const auto & file : m_Files)
		{
			const auto length = file->GetFileLength ();
			if (current++ != index)
			{
				offset += length;
				continue;
			}
			if (!length) return true;
			const auto first = offset / m_PieceLength;
			const auto last = (offset + length - 1) / m_PieceLength;
			for (size_t piece = first; piece <= last && piece < m_Pieces.size (); ++piece)
			{
				const auto & candidate = m_Pieces[piece];
				if (!IsPieceProgressComplete (candidate.IsComplete (), candidate.IsVerified (), candidate.IsSaving ()))
					return false;
			}
			return last < m_Pieces.size ();
		}
		return false;
	}

	uint32_t Torrent::ChargePieceFailure (const i2p::data::IdentHash & peer)
	{
		if (auto it = m_PeerPieceFailures.find (peer); it != m_PeerPieceFailures.end ()) return ++it->second;
		// Bounded and non-evicting: a full table silently stops tracking new
		// peers. The alternative (evicting a counter) would let a hostile
		// swarm launder an already charged peer back to zero.
		if (m_PeerPieceFailures.size () >= MAX_PENALIZED_PEERS) return 0;
		m_PeerPieceFailures.emplace (peer, 1);
		return 1;
	}

	uint32_t Torrent::GetPeerPieceFailures (const i2p::data::IdentHash & peer) const
	{
		auto it = m_PeerPieceFailures.find (peer);
		return it != m_PeerPieceFailures.end () ? it->second : 0;
	}

	void Torrent::SetSelectedComplete ()
	{
		size_t index = 0;
		for (const auto & file : m_Files)
		{
			if (index < m_FilePriorities.size () && m_FilePriorities[index]) file->Complete ();
			++index;
		}
		m_IsComplete.store (true, std::memory_order_release);
	}

	void Torrent::RequestLocalVerification (bool markAllPieces, bool includeIncomplete)
	{
		// A resume bitfield or a pre-existing final file only claims which
		// bytes should be on disk. Both are marked pending: they are excluded
		// from CreateBitfield()/HAVE until the storage service has hashed
		// them, and a mismatch turns the piece into Missing again.
		for (auto & piece : m_Pieces)
		{
			// Never clobber a piece whose write (or SAF bootstrap) is still in
			// flight; that writer verifies it before publishing it. Already
			// pending pieces stay pending. An explicit recheck hashes every
			// piece, including an incomplete .part whose bitmap was lost; a
			// resume/final-name pass only claims pieces already complete.
			if (piece.IsSaving ()) continue;
			if (markAllPieces && (includeIncomplete || piece.IsComplete ()))
				piece.MarkPendingVerification ();
			else if (!markAllPieces && piece.IsComplete () && !piece.IsVerified ())
				piece.MarkPendingVerification ();
		}
		m_VerifyCursor = 0;
		m_LocalVerificationRequested.store (true, std::memory_order_release);
		// Completion (and the ability to serve) is no longer proven once the
		// local data is being rechecked; CompleteTorrent() restores it after
		// every selected piece has been hashed again.
		m_IsComplete.store (false, std::memory_order_release);
	}

	size_t Torrent::RequestLocalVerificationForExistingFiles (const std::vector<bool> & presentFiles)
	{
		size_t marked = 0;
		for (size_t index = 0; index < m_Pieces.size (); ++index)
		{
			auto & piece = m_Pieces[index];
			if (piece.IsSaving ()) continue;
			bool backedByExistingFiles = true;
			for (const auto & fragment : GetPieceFileFragments (static_cast<int> (index)))
			{
				auto file = std::find (m_Files.begin (), m_Files.end (), fragment.file);
				if (file == m_Files.end ())
				{
					backedByExistingFiles = false;
					break;
				}
				const auto fileIndex = static_cast<size_t> (std::distance (m_Files.begin (), file));
				if (fileIndex >= presentFiles.size () || !presentFiles[fileIndex])
				{
					backedByExistingFiles = false;
					break;
				}
			}
			if (!backedByExistingFiles) continue;
			piece.MarkPendingVerification ();
			++marked;
		}
		if (marked)
		{
			m_VerifyCursor = 0;
			m_LocalVerificationRequested.store (true, std::memory_order_release);
			m_IsComplete.store (false, std::memory_order_release);
		}
		return marked;
	}

	bool Torrent::BeginLocalVerification ()
	{
		bool expected = false;
		return m_LocalVerificationActive.compare_exchange_strong (
			expected, true, std::memory_order_acq_rel, std::memory_order_acquire);
	}

	int Torrent::GetNextPieceToVerify ()
	{
		const size_t count = m_Pieces.size ();
		for (size_t scanned = 0; scanned < count; ++scanned)
		{
			const size_t index = (m_VerifyCursor + scanned) % count;
			if (m_Pieces[index].IsVerificationPending ())
			{
				m_VerifyCursor = (index + 1) % count;
				return static_cast<int> (index);
			}
		}
		// Nothing left: stop the request so the status-timer safety net does
		// not restart an empty pass on every tick.
		m_VerifyCursor = 0;
		m_LocalVerificationRequested.store (false, std::memory_order_release);
		return -1;
	}

	void Torrent::FinishPieceVerification (size_t index)
	{
		if (index >= m_Pieces.size ()) return;
		m_Pieces[index].FinishLocalVerification ();
	}

	int Torrent::GetFilePriority (size_t index) const
	{
		return index < m_FilePriorities.size () ? m_FilePriorities[index] : -1;
	}

	bool Torrent::SetFilePriority (size_t index, uint8_t priority)
	{
		if (index >= m_FilePriorities.size () || priority > 2) return false;
		m_FilePriorities[index] = priority;
		RebuildPiecePriorities ();
		if (m_IsComplete.load (std::memory_order_acquire) && !IsSelectedComplete ())
			m_IsComplete.store (false, std::memory_order_release);
		for (auto & connection : GetConnections ()) connection->RefreshFileSelection ();
		return true;
	}

	bool Torrent::SetFilePriorities (const std::vector<uint8_t> & priorities)
	{
		if (m_Error != eTorrentErrorNoError || !IsValid () || priorities.size () != m_Files.size ()
			|| std::any_of (priorities.begin (), priorities.end (), [] (uint8_t priority) { return priority > 2; }))
			return false;
		m_FilePriorities = priorities;
		RebuildPiecePriorities ();
		if (m_IsComplete.load (std::memory_order_acquire) && !IsSelectedComplete ())
			m_IsComplete.store (false, std::memory_order_release);
		for (auto & connection : GetConnections ()) connection->RefreshFileSelection ();
		return true;
	}

	// Maps the current file priorities onto piece priorities. A piece that
	// spans several files takes the highest priority of its files, so
	// selecting any boundary file keeps the shared piece requestable; a
	// piece with priority 0 and no data drops outstanding requests instead
	// of downloading silently.
	void Torrent::RebuildPiecePriorities ()
	{
		m_PiecePriorities.assign (m_Pieces.size (), 0);
		if (!m_PieceLength || m_FilePriorities.size () != m_Files.size ()) return;
		size_t offset = 0, index = 0;
		for (const auto & file : m_Files)
		{
			const auto length = file->GetFileLength ();
			const auto priority = m_FilePriorities[index++];
			if (length && priority)
			{
				const auto first = offset / m_PieceLength;
				const auto last = (offset + length - 1) / m_PieceLength;
				for (size_t piece = first; piece <= last && piece < m_PiecePriorities.size (); ++piece)
					m_PiecePriorities[piece] = std::max (m_PiecePriorities[piece], priority);
			}
			offset += length;
		}
		for (size_t piece = 0; piece < m_Pieces.size (); ++piece)
			if (!m_PiecePriorities[piece] && !m_Pieces[piece].IsComplete ()) m_Pieces[piece].ClearAllRequests ();
	}

	uint8_t Torrent::GetPiecePriority (size_t index) const
	{
		return index < m_PiecePriorities.size () ? m_PiecePriorities[index] : 0;
	}

	size_t Torrent::GetSelectedWantedBytes () const
	{
		size_t bytes = 0, index = 0;
		for (const auto & file : m_Files)
		{
			if (index < m_FilePriorities.size () && m_FilePriorities[index]) bytes += file->GetFileLength ();
			++index;
		}
		return bytes;
	}

	size_t Torrent::GetSelectedVerifiedBytes () const
	{
		if (!m_PieceLength) return 0;
		size_t bytes = 0, offset = 0, index = 0;
		for (const auto & file : m_Files)
		{
			const auto end = offset + file->GetFileLength ();
			if (index < m_FilePriorities.size () && m_FilePriorities[index])
			{
				auto position = offset;
				while (position < end)
				{
					auto piece = position / m_PieceLength;
					if (piece >= m_Pieces.size ()) break;
					const auto size = std::min (end - position, m_PieceLength - position % m_PieceLength);
					if (m_Pieces[piece].IsComplete () && m_Pieces[piece].IsVerified ()) bytes += size;
					position += size;
				}
			}
			offset = end;
			++index;
		}
		return bytes;
	}

	bool Torrent::IsSelectedComplete () const
	{
		if (m_Error != eTorrentErrorNoError || !IsValid () || m_PiecePriorities.size () != m_Pieces.size ())
			return false;
		for (size_t index = 0; index < m_Pieces.size (); ++index)
			if (m_PiecePriorities[index] && (!m_Pieces[index].IsComplete () || !m_Pieces[index].IsVerified ()))
				return false;
		return true;
	}

	RequestedBlock Torrent::GetNextBlockToRequest (std::shared_ptr<PeerConnection> conn, bool skipRequested)
	{
		if (conn)
		{
			uint8_t highest = 0;
			for (size_t index = 0; index < m_Pieces.size (); ++index)
				if (!m_Pieces[index].IsComplete () && conn->IsPieceAvailable (index)
					&& (!skipRequested || !m_Pieces[index].IsRequested ()))
					highest = std::max (highest, GetPiecePriority (index));
			if (!highest) return { 0, 0, 0 };
			// Finish an active piece only while it has the highest eligible
			// priority. New pieces retain rarest-first ordering within priority.
			int lastIndex = conn->GetLastRequestedPieceIndex ();
			if (lastIndex >= 0 && static_cast<size_t> (lastIndex) < m_Pieces.size ()
				&& conn->IsPieceAvailable (lastIndex) && GetPiecePriority (lastIndex) >= highest)
			{
				auto [offset, len] = m_Pieces[lastIndex].GetNextBlockToRequest ();
				if (len > 0) return { static_cast<uint32_t> (lastIndex), offset, len };
			}
			// BEP6 suggestions must not override priority or rarity policy.
			conn->ResetSuggestedPieceIndex ();
			// try another piece if not current piece or no more blocks in
			// current piece
			using PieceNumPeers = std::tuple<uint32_t, size_t, uint_fast32_t>; // (index, num peers, random value)
			std::set<PieceNumPeers, std::function<bool (const PieceNumPeers &, const PieceNumPeers &)>>
			sortedByNumPeers (
				[] (const PieceNumPeers & p1, const PieceNumPeers & p2) -> bool
				{
					if (std::get<1> (p1) != std::get<1> (p2)) return std::get<1> (p1) < std::get<1> (p2);
					if (std::get<2> (p1) != std::get<2> (p2)) return std::get<2> (p1) < std::get<2> (p2);
					return std::get<0> (p1) < std::get<0> (p2);
				});
			// sort eligible pieces by num peers
			std::mt19937 rng (i2p::util::GetRngSeed ());
			uint32_t ind = 0;
			for (auto & it : m_Pieces)
			{
				if (!it.IsComplete () && GetPiecePriority (ind) == highest && conn->IsPieceAvailable (ind)
					&& (!skipRequested || !it.IsRequested ()))
					sortedByNumPeers.emplace (ind, it.GetNumPeers (), rng ());
				ind++;
			}
			for (const auto & it : sortedByNumPeers)
			{
				const uint32_t pieceIndex = std::get<0> (it);
				auto [offset, len] = m_Pieces[pieceIndex].GetNextBlockToRequest ();
				if (len > 0) return { pieceIndex, offset, len };
			}
		}
		return { 0, 0, 0 };
	}

	bool Torrent::UpdateStatus (uint64_t ts)
	{
		GetConnections (); // cleanup expired connections
		// Keep the bounded candidate table fresh on the periodic tick:
		// expired and permanently failed candidates must not linger in it.
		ExpirePeerCandidates (ts);
		if (!m_Length) return false; // non ready magnet
		const bool complete = IsSelectedComplete ();
		for (auto & it : m_Pieces)
		{
			bool pieceComplete = it.IsComplete ();
			if (m_IsStopped
				|| (ts > it.GetLastActivityTimestamp () + PIECE_INACTIVITY_TIMEOUT)) // piece was inactive recently
			{
				// Evicting a complete piece must not forget that it is
				// complete; an incomplete piece only loses its outstanding
				// requests. Reset() is reserved for actual corruption and
				// makes even a complete piece downloadable again.
				if (pieceComplete)
					it.ReleaseData ();
				else
					it.Reset ();
			}
		}
		return complete;
	}

	void Torrent::SetComplete ()
	{
		for (auto & it : m_Pieces)
			if (!it.IsComplete ()) it.Complete ();
		for (auto it : m_Files) it->Complete ();
		// Publish completion after the per-piece state is visible.
		m_IsComplete.store (true, std::memory_order_release);
	}

	void Torrent::SaveTorrentResumeFile ()
	{
		if (IsRemoved () || m_FullPath.empty ()) return;
		auto [bitfield, have] = CreateBitfield ();
		const bool indeterminate = boost::logic::indeterminate (have);
		const bool haveValue = !indeterminate && static_cast<bool> (have);
		const bool none = !indeterminate && !haveValue;
		const bool all = !indeterminate && haveValue;
		// A pending verification pass has not decided yet. Do not replace the
		// previous bitmap with a partial view of pending pieces; the
		// end-of-pass save runs after GetNextPieceToVerify() clears the flag.
		if (IsLocalVerificationRequested ()) return;
		if (m_ResumePersistence)
		{
			if (!m_ResumePersistence (std::span<const uint8_t> (bitfield), none || all))
				LogPrint (eLogError, "Torrents: Isolated resume persistence failed");
			return; // No direct fallback after delegated persistence failure.
		}
		std::filesystem::path resumeFilePath = m_FullPath;
		resumeFilePath += ".resume";
		if (none || all)
		{
			// No complete piece, or every piece is complete and verified:
			// a stale resume file must not revive a failed recheck or claim
			// availability after a fully promoted torrent.
			std::string error;
			if (!RemoveFileNoFollow (resumeFilePath.parent_path (), resumeFilePath.filename (), error))
				LogPrint (eLogError, "Torrents: Can't remove resume file ", resumeFilePath, " : ", error);
		}
		else
		{
			// Replace atomically so a crash cannot leave a short bitfield that
			// a later start would misinterpret.
			std::string error;
			if (!WriteFileAtomicNoFollow (resumeFilePath.parent_path (), resumeFilePath.filename (), bitfield.data (),
					bitfield.size (), error))
				LogPrint (eLogError, "Torrents: Can't write resume file ", resumeFilePath, " : ", error);
		}
	}

	void Torrent::StartCountingPeers ()
	{
		for (auto & it : m_Pieces) it.SetNumPeers (0);
	}

	void Torrent::ApplyPeerRemoteBitfield (const boost::dynamic_bitset<> & peerRemoteBitfield)
	{
		size_t ind = peerRemoteBitfield.find_first ();
		while (ind != boost::dynamic_bitset<>::npos && ind < m_Pieces.size ())
		{
			auto & piece = m_Pieces[ind];
			if (!piece.IsComplete ()) piece.SetNumPeers (piece.GetNumPeers () + 1);
			ind = peerRemoteBitfield.find_next (ind);
		}
	}

	bool Torrent::HasIncompletePieces (const boost::dynamic_bitset<> & peerRemoteBitfield) const
	{
		size_t ind = peerRemoteBitfield.find_first ();
		while (ind != boost::dynamic_bitset<>::npos && ind < m_Pieces.size ())
		{
			if (GetPiecePriority (ind) && !m_Pieces[ind].IsComplete ()) return true;
			ind = peerRemoteBitfield.find_next (ind);
		}
		return false;
	}

	bool Torrent::CanAcceptPieceBlock (uint32_t index, size_t offset, size_t len) const
	{
		// Stopping a torrent must stop all inbound payload mutation and the
		// disk writes it would schedule. Empty blocks are not progress and
		// must not touch the block map.
		if (IsRemoved () || m_IsStopped || !len || index >= m_Pieces.size ()) return false;
		const auto pieceSize = m_Pieces[index].GetSize ();
		return offset <= pieceSize && len <= pieceSize - offset;
	}

	std::vector<PieceFileFragment> Torrent::GetPieceFileFragments (int index) const
	{
		if (index < 0 || index >= (int)m_Pieces.size ()) return { };

		std::vector<PieceFileFragment> ret;
		// first file and offset for start of piece
		size_t offset = index * m_PieceLength;
		auto it = m_Files.begin ();
		while (it != m_Files.end ())
		{
			if (offset < (*it)->GetFileLength ()) break;
			offset -= (*it)->GetFileLength ();
			it++;
		}
		if (it != m_Files.end ())
		{
			// split piece by files
			size_t size = m_Pieces[index].GetSize (), fragmentOffset = 0;
			while (size > 0)
			{
				auto file = *it;
				// Empty files carry no bytes. Skipping them avoids opening a
				// non-existent SAF document for a zero-length fragment; POSIX
				// semantics are unchanged because no read/write occurs.
				if (!file->GetFileLength ())
				{
					++it;
					if (it == m_Files.end ()) break;
					continue;
				}
				// Staging is a per-file property set by InitTorrentFiles or by
				// TorrentFile::Complete(). Deriving it from torrent completion
				// made a recheck of a promoted torrent read missing .part files.
				if (offset + size <= file->GetFileLength ())
				{
					// last fragment
					ret.emplace_back (file, offset, fragmentOffset, size);
					size = 0;
				}
				else
				{
					size_t l = file->GetFileLength () - offset;
					ret.emplace_back (file, offset, fragmentOffset, l);
					size -= l;
					fragmentOffset += l;
					offset = 0;
					it++;
					if (it == m_Files.end ()) break;
				}
			}
			if (size > 0) LogPrint (eLogError, "Torrents: Piece ", index, " is beyond files");
		}
		return ret;
	}

	std::vector<size_t> Torrent::GetFilesCompleted () const
	{
		std::vector<size_t> completed;
		if (!m_Files.empty ())
		{
			// A piece is progress only when it is present, hashed and flushed.
			// Counting a complete-but-unverified or still-saving piece made the
			// file (and therefore the UI percentage) report 100% while HAVE and
			// serving still withheld it.
			const auto isCounted = [] (const Piece & piece)
			{ return IsPieceProgressComplete (piece.IsComplete (), piece.IsVerified (), piece.IsSaving ()); };
			auto filesIT = m_Files.begin ();
			size_t currentSize = 0, currentCompletedSize = 0;
			for (const auto & piece : m_Pieces)
			{
				const bool counted = isCounted (piece);
				if (currentSize + m_PieceLength < (*filesIT)->GetFileLength ())
				{
					currentSize += m_PieceLength;
					if (counted) currentCompletedSize += m_PieceLength;
				}
				else
				{
					size_t leftoverSize = (*filesIT)->GetFileLength () - currentSize;
					if (counted) currentCompletedSize += leftoverSize;
					completed.push_back (currentCompletedSize);
					currentSize = m_PieceLength - leftoverSize;
					filesIT++;
					while (filesIT != m_Files.end () && (*filesIT)->GetFileLength () <= currentSize)
					{
						completed.push_back (counted ? (*filesIT)->GetFileLength () : 0);
						currentSize -= (*filesIT)->GetFileLength ();
						filesIT++;
					}
					currentCompletedSize = counted ? currentSize : 0;
					if (filesIT == m_Files.end ()) break;
				}
			}
		}
		return completed;
	}

	std::unordered_set<i2p::data::IdentHash> Torrent::GetNonConnectedPeers ()
	{
		std::unordered_set<i2p::data::IdentHash> ret;
		for (size_t i = 0; i < m_TrackerStats.size (); i++) ret.merge (GetNonConnectedPeers (i));
		return ret;
	}

	std::unordered_set<i2p::data::IdentHash> Torrent::GetNonConnectedPeers (size_t trackerID)
	{
		std::unordered_set<i2p::data::IdentHash> ret;
		if (trackerID < m_TrackerStats.size ())
		{
			const auto & peers = std::get<0> (m_TrackerStats[trackerID]);
			for (const auto & it : peers)
			{
				if (!IsConnectedToPeer (it)) ret.emplace (it);
			}
		}
		return ret;
	}

	uint64_t Torrent::GetNextTrackerRequestTime (size_t trackerID) const
	{
		if (trackerID < m_TrackerStats.size ()) return std::get<2> (m_TrackerStats[trackerID]);
		return 0;
	}

	void Torrent::SetNextTrackerRequestTime (size_t trackerID, uint64_t ts)
	{
		if (trackerID >= m_TrackerStats.size ())
			m_TrackerStats.resize (trackerID + 1,
				TrackerStats { { }, MIN_TRACKER_REQUESTS_INTERVAL, 0, 0, 0, i2p::util::GetSecondsSinceEpoch (), "",
					MIN_TRACKER_REQUESTS_INTERVAL, 0 });
		std::get<2> (m_TrackerStats[trackerID]) = ts;
	}

	TorrentStatus Torrent::GetStatus () const
	{
		if (m_IsStopped) return eTorrentStatusStopped;
		if (m_IsComplete.load (std::memory_order_acquire)) return eTorrentStatusSeeding;
		// Local hashing is not downloading: report it so a client can show
		// that availability is being re-established rather than fetched.
		if (m_LocalVerificationActive.load (std::memory_order_acquire)) return eTorrentStatusVerifyingLocalData;
		return eTorrentStatusDownloading;
	}

	bool Torrent::AddConnection (std::shared_ptr<PeerConnection> conn)
	{
		if (!conn) return false;
		auto remoteIdentHash = conn->GetRemoteIdentHash ();
		if (!remoteIdentHash) return false;
		if (!m_Connections.contains (*remoteIdentHash) && m_Connections.size () >= MAX_PEERS_PER_TORRENT)
		{
			// The per-torrent limit is a hard bound. An authenticated peer may
			// rotate one idle connection, but a transferring or interested
			// peer is never evicted and an attempt to exceed the limit while
			// no idle peer exists is simply refused.
			if (!EvictIdleConnection ())
			{
				LogPrint (eLogDebug, "Torrents: Connection limit ", MAX_PEERS_PER_TORRENT, " reached for ", m_Name);
				return false;
			}
		}
		auto [it, inserted] = m_Connections.emplace (*remoteIdentHash, conn);
		if (!inserted)
		{
			if (it->second.expired ())
			{
				m_Connections.erase (it); // delete not longer existing
				return m_Connections.emplace (*remoteIdentHash, conn).second; // try again
			}
			else
				return false;
		}
		return true;
	}

	bool Torrent::EvictIdleConnection ()
	{
		for (const auto & conn : GetConnections ())
		{
			// Idle means established, choked in both directions and not
			// interested either way. Close() only posts the teardown to the
			// service thread, so the handler is never destroyed here.
			if (!conn || !conn->IsEstablishedAndIdle ()) continue;
			LogPrint (eLogDebug, "Torrents: Rotating an idle peer of ", m_Name, " to respect the connection limit");
			conn->Close ();
			return true;
		}
		return false;
	}

	void Torrent::RemoveConnection (std::shared_ptr<PeerConnection> conn)
	{
		if (!conn) return;
		auto remoteIdentHash = conn->GetRemoteIdentHash ();
		if (!remoteIdentHash) return;
		m_Connections.erase (*remoteIdentHash);
	}

	std::list<std::shared_ptr<PeerConnection>> Torrent::GetConnections ()
	{
		std::list<std::shared_ptr<PeerConnection>> ret;
		auto it = m_Connections.begin ();
		while (it != m_Connections.end ())
		{
			auto conn = it->second.lock ();
			if (conn)
			{
				ret.emplace_back (conn);
				it++;
			}
			else
				it = m_Connections.erase (it);
		}
		return ret;
	}

	bool Torrent::IsConnectedToPeer (const i2p::data::IdentHash & peer)
	{
		auto it = m_Connections.find (peer);
		if (it != m_Connections.end ())
		{
			if (!it->second.expired ()) return true;
			m_Connections.erase (it);
		}
		return false;
	}

	//------------------------------
	// Peer-candidate table (service thread only)
	//------------------------------

	void Torrent::SetStopped (bool stopped)
	{
		m_IsStopped = stopped;
		// A stopped torrent refuses dials, so remembered candidates would
		// only accumulate and survive into the next start.
		if (stopped) ClearPeerCandidates ();
	}

	void Torrent::MarkRemoved ()
	{
		m_Removed.store (true, std::memory_order_release);
		ClearPeerCandidates ();
	}

	PeerCandidate * Torrent::FindPeerCandidate (const i2p::data::IdentHash & peer)
	{
		for (auto & candidate : m_PeerCandidates)
			if (candidate.ident == peer) return &candidate;
		return nullptr;
	}

	void Torrent::RemovePeerCandidate (const i2p::data::IdentHash & peer)
	{
		for (auto it = m_PeerCandidates.begin (); it != m_PeerCandidates.end (); ++it)
		{
			if (it->ident != peer) continue;
			m_PeerCandidates.erase (it);
			return;
		}
	}

	bool Torrent::EvictPeerCandidate ()
	{
		if (m_PeerCandidates.empty ()) return false;
		size_t victim = 0;
		for (size_t i = 1; i < m_PeerCandidates.size (); ++i)
		{
			const auto & candidate = m_PeerCandidates[i];
			const auto & current = m_PeerCandidates[victim];
			const bool candidateDead = candidate.failures >= PEER_CANDIDATE_MAX_FAILURES;
			const bool currentDead = current.failures >= PEER_CANDIDATE_MAX_FAILURES;
			if (candidateDead != currentDead)
			{
				// A permanently failed entry is worthless; drop it first.
				if (candidateDead) victim = i;
				continue;
			}
			if (candidate.added != current.added)
			{
				if (candidate.added < current.added) victim = i; // oldest first
				continue;
			}
			// Deterministic tie-break so eviction never depends on insertion
			// order of equal-age entries from two different messages.
			if (candidate.ident < current.ident) victim = i;
		}
		m_PeerCandidates.erase (m_PeerCandidates.begin () + victim);
		return true;
	}

	bool Torrent::IsPeerConnected (const i2p::data::IdentHash & peer) const
	{
		// const view: never erases an expired entry (the caller may be a
		// const query) and never takes a lock, because the table and the
		// connection map are both service-thread state.
		auto it = m_Connections.find (peer);
		return it != m_Connections.end () && !it->second.expired ();
	}

	bool Torrent::AddPeerCandidate (const i2p::data::IdentHash & peer, PeerConnectionOrigin origin, uint64_t nowSeconds)
	{
		// A zero hash is not a destination; a private swarm must never keep
		// peer-supplied candidates; a stopped/removed torrent has no dials.
		if (peer.IsZero () || !nowSeconds || m_IsStopped || IsRemoved () || m_IsPrivate) return false;
		if (IsPeerConnected (peer)) return false;
		// An existing entry keeps its origin, age and failure history: a
		// re-advertisement must not refresh a candidate's TTL or reset its
		// backoff.
		if (FindPeerCandidate (peer)) return false;
		if (m_PeerCandidates.size () >= MAX_PEER_CANDIDATES)
		{
			ExpirePeerCandidates (nowSeconds);
			if (m_PeerCandidates.size () >= MAX_PEER_CANDIDATES && !EvictPeerCandidate ()) return false;
		}
		m_PeerCandidates.push_back (PeerCandidate { peer, origin, nowSeconds, 0, 0 });
		return true;
	}

	bool Torrent::AdmitPeerCandidates (PeerConnectionOrigin origin, size_t count, uint64_t nowSeconds)
	{
		if (!count) return true;
		PeerCandidateWindow * window = nullptr;
		if (origin == ePeerConnectionOriginPEX)
			window = &m_PexCandidateWindow;
		else if (origin == ePeerConnectionOriginDHT)
			window = &m_DhtCandidateWindow;
		// Tracker and incoming peers are not peer-supplied gossip candidates
		// and do not consume a window.
		if (!window) return true;
		if (!nowSeconds || count > MAX_PEER_CANDIDATES_PER_WINDOW) return false;
		// Fixed window: it restarts exactly at start + PEER_CANDIDATE_WINDOW.
		// A backwards clock does not restart it (that would hand out a second
		// full budget) and the comparison cannot wrap.
		if (!window->start || nowSeconds >= window->start + PEER_CANDIDATE_WINDOW)
		{
			window->start = nowSeconds;
			window->admitted = 0;
		}
		if (count > MAX_PEER_CANDIDATES_PER_WINDOW - window->admitted) return false;
		window->admitted += count;
		return true;
	}

	std::vector<i2p::data::IdentHash> Torrent::GetPeerCandidatesToConnect (uint64_t nowSeconds, size_t max) const
	{
		std::vector<i2p::data::IdentHash> candidates;
		// A private, stopped or removed torrent never dials an
		// unauthenticated peer-offered destination, even if a caller asks for
		// candidates.
		if (!max || m_IsStopped || IsRemoved () || m_IsPrivate) return candidates;
		candidates.reserve (std::min (max, m_PeerCandidates.size ()));
		for (const auto & candidate : m_PeerCandidates)
		{
			if (candidates.size () >= max) break;
			if (IsPeerConnected (candidate.ident)) continue;
			if (!PeerCandidatePolicy::IsUsable (
					candidate.added, candidate.lastAttempt, candidate.failures, nowSeconds, candidate.origin))
				continue;
			candidates.push_back (candidate.ident);
		}
		return candidates;
	}

	void Torrent::MarkPeerCandidateAttempt (const i2p::data::IdentHash & peer, uint64_t nowSeconds)
	{
		if (auto candidate = FindPeerCandidate (peer)) candidate->lastAttempt = nowSeconds;
	}

	void Torrent::MarkPeerCandidateConnected (const i2p::data::IdentHash & peer)
	{
		// The connection table owns the peer from now on. Keeping a pending
		// candidate would hold a table slot while connected and would make
		// the peer dialable again only after its backoff once the connection
		// closes.
		RemovePeerCandidate (peer);
	}

	void Torrent::MarkPeerCandidateFailed (const i2p::data::IdentHash & peer, uint64_t nowSeconds)
	{
		auto candidate = FindPeerCandidate (peer);
		if (!candidate) return;
		if (candidate->failures < PEER_CANDIDATE_MAX_FAILURES) ++candidate->failures;
		candidate->lastAttempt = nowSeconds;
		// The entry is kept after the budget is spent: a re-advertisement
		// must not reset the failure history. It is never dialable again and
		// is dropped by expiration or eviction.
	}

	void Torrent::ExpirePeerCandidates (uint64_t nowSeconds)
	{
		for (auto it = m_PeerCandidates.begin (); it != m_PeerCandidates.end ();)
		{
			// A backwards clock never expires anything: `now - added` cannot
			// wrap into a fresh lifetime and a regression must not destroy the
			// table.
			const bool expired = !it->added
				|| (nowSeconds >= it->added && nowSeconds - it->added >= PeerCandidatePolicy::Ttl (it->origin));
			const bool dead = it->failures >= PEER_CANDIDATE_MAX_FAILURES;
			if (expired || dead)
				it = m_PeerCandidates.erase (it);
			else
				++it;
		}
	}

	PeerConnectionOrigin Torrent::GetPeerCandidateOrigin (const i2p::data::IdentHash & peer) const
	{
		for (const auto & candidate : m_PeerCandidates)
			if (candidate.ident == peer) return candidate.origin;
		return ePeerConnectionOriginPEX;
	}

	void Torrent::ClearPeerCandidates ()
	{
		m_PeerCandidates.clear ();
		// The admission windows belong to the same state: a stopped/removed
		// torrent starts with a full budget again.
		m_PexCandidateWindow = { };
		m_DhtCandidateWindow = { };
	}

	uint64_t Torrent::GetDownloadRate ()
	{
		uint64_t downloadRate = 0;
		auto conns = GetConnections ();
		for (auto it : conns) downloadRate += it->GetDownloadRate ();
		return downloadRate;
	}

	uint64_t Torrent::GetUploadRate ()
	{
		uint64_t uploadRate = 0;
		auto conns = GetConnections ();
		for (auto it : conns) uploadRate += it->GetUploadRate ();
		return uploadRate;
	}

	int Torrent::GetNumDownloadingFromPeers ()
	{
		int numDownloadingFromPeers = 0;
		auto conns = GetConnections ();
		for (auto it : conns)
			if (it->IsDownloading ()) numDownloadingFromPeers++;
		return numDownloadingFromPeers;
	}

	int Torrent::GetNumUploadingToPeers ()
	{
		int numUploadingToPeers = 0;
		auto conns = GetConnections ();
		for (auto it : conns)
			if (it->IsUploading ()) numUploadingToPeers++;
		return numUploadingToPeers;
	}

	PeerConnection::PeerConnection (
		std::shared_ptr<i2p::client::I2PService> owner, std::shared_ptr<i2p::stream::Stream> stream)
		: i2p::client::I2PServiceHandler (owner)
		, m_Stream (stream)
		, m_Origin (ePeerConnectionOriginIncoming)
		, m_ReceiveBufferOffset (0)
		, m_NextMsgLength (0)
		, m_MaxNumRequests (MIN_NUM_REQUESTS)
		, m_IsHandshakeSent (false)
		, m_IsEstablished (false)
		, m_IsChoked (true)
		, m_IsRemoteChoked (true)
		, m_IsInterested (false)
		, m_IsRemoteInterested (false)
		, m_LastReceiveTime (0)
		, m_LastSendTime (0)
		, m_NumRequests (0)
		, m_NumPieces (0)
		, m_LastRequestedPieceIndex (-1)
		, m_RemoteMsgIDUtMetadata (0)
		, m_RemoteMsgIDI2PPEX (0)
		, m_RemoteMsgIDI2PDHT (0)
		, m_RemoteMetadataSize (0)
		, m_IsFast (false)
		, m_SuggestedPieceIndex (-1)
		, m_Downloaded (0)
		, m_Uploaded (0)
	{
		ResetStats ();
	}

	PeerConnection::PeerConnection (std::shared_ptr<i2p::client::I2PService> owner,
		std::shared_ptr<i2p::stream::Stream> stream, std::shared_ptr<Torrent> torrent, PeerConnectionOrigin origin)
		: PeerConnection (owner, stream)
	{
		m_Torrent = torrent;
		m_Origin = origin;
	}

	PeerConnection::~PeerConnection () { }

	void PeerConnection::Terminate ()
	{
		if (Kill ()) return;
		if (m_Torrent)
		{
			ReleaseOutgoingRequests ();
			m_Torrent->RemoveConnection (shared_from_this ());
		}
		if (m_Stream)
		{
			m_Stream->Close ();
			m_Stream = nullptr;
		}
		if (m_HandshakeReceiveTimer)
		{
			m_HandshakeReceiveTimer->cancel ();
			m_HandshakeReceiveTimer = nullptr;
		}
		Done (shared_from_this ());
	}

	void PeerConnection::ResetStats ()
	{
		m_DownloadRate = 0;
		m_UploadRate = 0;
		m_LastBlockDownloadTimestamp = 0;
		m_LastBlockUploadTimestamp = 0;
		m_ReceivedSinceLastTimestamp = 0;
		m_SentSinceLastTimestamp = 0;
	}

	void PeerConnection::TorrentComplete ()
	{
		if (!m_Torrent) return;
		// A peer that never sent a bitfield is not a seed: an empty bitfield
		// is `.all()` as well, so the flag decides instead of the bitset.
		if (IsRemoteSeed ())
		{
			Close ();
			return;
		}
		ResetStats ();
		ReleaseOutgoingRequests ();
		m_LastRequestedPieceIndex = -1;
		if (m_IsInterested)
		{
			m_IsInterested = false;
			SendNotinterestedMsg ();
		}
	}

	void PeerConnection::ScheduleHandshakeReceiveTimer ()
	{
		if (m_HandshakeReceiveTimer)
			m_HandshakeReceiveTimer->cancel ();
		else
			m_HandshakeReceiveTimer = std::make_unique<boost::asio::steady_timer> (GetTorrentsTunnel ()->GetService ());
		m_HandshakeReceiveTimer->expires_after (std::chrono::seconds (HANDSHAKE_RECEIVE_TIMEOUT));
		m_HandshakeReceiveTimer->async_wait (
			[s = shared_from_this ()] (const boost::system::error_code & ecode)
			{
				if (ecode != boost::asio::error::operation_aborted)
				{
					LogPrint (
						eLogInfo, "Torrents: Handshake was not received after ", HANDSHAKE_RECEIVE_TIMEOUT, " seconds");
					s->Terminate ();
				}
				else
					s->m_HandshakeReceiveTimer = nullptr;
			});
	}

	std::shared_ptr<TorrentsTunnel> PeerConnection::GetTorrentsTunnel () const
	{
		return std::static_pointer_cast<TorrentsTunnel> (GetOwner ());
	}

	bool PeerConnection::IsPieceAvailable (size_t ind) const
	{
		if (ind >= m_RemoteBitfield.size ()) return false;
		return m_RemoteBitfield.test (ind);
	}

	void PeerConnection::WriteToStream (const uint8_t * buf, size_t len)
	{
		if (!m_Stream) return;
		LogPrint (eLogDebug, "Torrents: Sending ", len, " bytes");
		m_Stream->AsyncSend (buf, len,
			[s = shared_from_this ()] (const boost::system::error_code & ecode, size_t)
			{
				if (ecode || !s->m_Stream) s->Terminate ();
			});
		m_LastSendTime = i2p::util::GetMonotonicSeconds ();
	}

	void PeerConnection::Connect ()
	{
		if (m_Torrent && m_Torrent->AddConnection (shared_from_this ()))
		{
			SendHandshakeMsg ();
			ScheduleHandshakeReceiveTimer ();
			StreamReceive ();
		}
		else
		{
			LogPrint (eLogWarning, "Torrents: Connection with peer ",
				i2p::data::GetIdentHashAbbreviation (m_Stream->GetRemoteIdentity ()->GetIdentHash ()),
				" already exists");
			Terminate ();
			return;
		}
	}

	void PeerConnection::ReceiveHandshake ()
	{
		LogPrint (eLogDebug, "Torrents: Incoming connection from ",
			m_Stream->GetRemoteIdentity () ? (m_Stream->GetRemoteIdentity ()->GetIdentHash ().ToBase32 () + ".b32.i2p")
										   : "");
		ScheduleHandshakeReceiveTimer ();
		StreamReceive ();
	}

	void PeerConnection::Close ()
	{
		boost::asio::post (GetTorrentsTunnel ()->GetService (), [s = shared_from_this ()] () { s->Terminate (); });
	}

	void PeerConnection::CheckKeepAlive (uint64_t ts)
	{
		if (m_IsEstablished) RequestComments ();
		// Incremental PEX refresh. Private swarms are suppressed inside
		// NotifyPEXPeers(), and the announcement set is bounded.
		if (m_IsEstablished && m_RemoteMsgIDI2PPEX && ts > m_LastPEXTime + PEER_PEX_SEND_INTERVAL) NotifyPEXPeers ();
		if (m_IsEstablished)
		{
			// A rate sampled while blocks moved must not be reported forever
			// after the transfer stopped.
			UpdateIdleRates (ts);
			if (ts > m_LastReceiveTime + PEER_KEEP_ALIVE_TIMEOUT)
			{
				LogPrint (eLogInfo, "Torrent: Peer timeout expired");
				Close (); // Terminate shouldn't be called from IterateHandler directly
			}
			else
			{
				// Per-request deadline. It fires well before the blanket
				// release below, so a peer that keeps sending keep-alives (or
				// other blocks) can no longer pin a stalled block forever.
				ReleaseExpiredOutgoingRequests (ts);
				if (ts > m_LastSendTime + PEER_KEEP_SEND_INTERVAL)
				{
					ReleaseOutgoingRequests (); // pending requests are invalid now
					bool requested = false;
					if (!m_Torrent->IsComplete () && m_Torrent->HasIncompletePieces (m_RemoteBitfield))
					{
						// try to request if we still have blocks to request
						if (!m_IsChoked)
							requested = RequestNextBlocks ();
						else if (!m_IsInterested)
						{
							m_IsInterested = true;
							SendInterestedMsg ();
						}
					}
					if (!requested)
					{
						// send keep-alive
						uint32_t len = 0;
						WriteToStream ((const uint8_t *)&len, 4);
						m_LastSendTime = ts;
					}
				}
			}
		}
	}

	void PeerConnection::UpdateIdleRates (uint64_t nowSeconds)
	{
		// The last accounted block is the only traffic timestamp a rate has.
		// A direction that never moved a block has no rate to decay.
		const auto decay = [nowSeconds] (uint64_t & rate, uint64_t & lastBlockTimestamp, size_t & sinceLastTimestamp)
		{
			if (!rate || !lastBlockTimestamp) return;
			const auto lastSeconds = lastBlockTimestamp / 1000;
			if (nowSeconds <= lastSeconds) return;
			const auto decayed = DecayPeerRate (rate, nowSeconds - lastSeconds);
			if (decayed == rate) return;
			rate = decayed;
			if (!rate)
			{
				// Nothing is left to decay: drop the stale window so the next
				// block starts a fresh sample instead of averaging against a
				// long idle interval, which would report a far too low rate.
				lastBlockTimestamp = 0;
				sinceLastTimestamp = 0;
			}
		};
		decay (m_DownloadRate, m_LastBlockDownloadTimestamp, m_ReceivedSinceLastTimestamp);
		decay (m_UploadRate, m_LastBlockUploadTimestamp, m_SentSinceLastTimestamp);
	}

	void PeerConnection::StreamReceive ()
	{
		if (m_Stream && m_ReceiveBufferOffset < PEER_CONNECTION_RECEIVE_BUFFER_SIZE)
		{
			if (m_Stream->GetStatus () == i2p::stream::eStreamStatusNew
				|| m_Stream->GetStatus () == i2p::stream::eStreamStatusOpen) // regular
			{
				m_Stream->AsyncReceive (boost::asio::buffer (m_ReceiveBuffer + m_ReceiveBufferOffset,
											PEER_CONNECTION_RECEIVE_BUFFER_SIZE - m_ReceiveBufferOffset),
					std::bind (&PeerConnection::HandleStreamReceive, shared_from_this (), std::placeholders::_1,
						std::placeholders::_2),
					PEER_CONNECTION_MAX_IDLE,
					m_NextMsgLength > m_ReceiveBufferOffset ? m_NextMsgLength - m_ReceiveBufferOffset : 0);
			}
			else // closed by peer
			{
				// get remaining data
				auto len = m_Stream->ReadSome (m_ReceiveBuffer + m_ReceiveBufferOffset,
					PEER_CONNECTION_RECEIVE_BUFFER_SIZE - m_ReceiveBufferOffset);
				if (len > 0) // still some data
				{
					m_ReceiveBufferOffset += len;
					HandleReceived ();
				}
				else // no more data*/
					Terminate ();
			}
		}
	}

	void PeerConnection::HandleStreamReceive (const boost::system::error_code & ecode, size_t bytes_transferred)
	{
		if (!m_Stream) return;
		if (ecode)
		{
			if (ecode != boost::asio::error::operation_aborted)
			{
				LogPrint (eLogInfo, "Torrents: Stream read error: ", ecode.message ());
				if (bytes_transferred > 0)
				{
					m_ReceiveBufferOffset += bytes_transferred;
					HandleReceived ();
				}
				else if (ecode == boost::asio::error::timed_out && m_Stream && m_Stream->IsOpen ())
					StreamReceive ();
				else
					Terminate ();
			}
			else
				Terminate ();
		}
		else
		{
			LogPrint (eLogDebug, "Torrents: Received ", bytes_transferred, " bytes");
			m_ReceiveBufferOffset += bytes_transferred;
			HandleReceived ();
			StreamReceive ();
		}
	}

	void PeerConnection::HandleReceived ()
	{
		m_LastReceiveTime = i2p::util::GetMonotonicSeconds ();
		if (m_NextMsgLength > 0 && m_ReceiveBufferOffset < m_NextMsgLength) return; // not enough received
		size_t offset = 0;
		while (size_t len = HandleNextMsg (offset)) offset += len;

		if (offset)
		{
			if (offset < m_ReceiveBufferOffset)
			{
				// move remaining data
				m_ReceiveBufferOffset -= offset;
				memmove (m_ReceiveBuffer, m_ReceiveBuffer + offset, m_ReceiveBufferOffset);
			}
			else
			{
				m_ReceiveBufferOffset = 0;
				m_NextMsgLength = 0;
			}
		}
	}

	size_t PeerConnection::HandleNextMsg (size_t offset)
	{
		if (offset >= m_ReceiveBufferOffset)
		{
			if (offset > m_ReceiveBufferOffset)
				LogPrint (eLogError, "Torrents: Start of message ", offset, " is beyond received buffer ",
					m_ReceiveBufferOffset);
			return 0;
		}
		if (!m_IsEstablished) return HandleHandshakeMsg ();
		// regular messages
		size_t len = m_ReceiveBufferOffset - offset;
		if (len < 4)
		{
			m_NextMsgLength = 0;
			return 0;
		}
		uint32_t msgLen = bufbe32toh (m_ReceiveBuffer + offset);
		// Reject the frame before storing m_NextMsgLength. Checking the
		// payload alone accepted 65535, whose 4-byte prefix does not fit in
		// the receive buffer and left the connection waiting for bytes that
		// could never arrive.
		if (!IsPeerMessageLengthAllowed (msgLen))
		{
			LogPrint (eLogError, "Torrents: Unexpected received message length ", msgLen);
			m_ReceiveBufferOffset = 0;
			m_NextMsgLength = 0;
			Terminate ();
			return 0;
		}
		if (len < (size_t)msgLen + 4)
		{
			m_NextMsgLength = (size_t)msgLen + 4;
			return 0;
		}
		offset += 4;
		if (msgLen >= 1)
		{
			LogPrint (eLogDebug, "Torrents: Received msg type ", (int)m_ReceiveBuffer[offset], " len ", msgLen);
			switch (m_ReceiveBuffer[offset])
			{
			case eMessageTypeRequest: HandleRequestMsg (m_ReceiveBuffer + offset + 1, msgLen - 1); break;
			case eMessageTypeCancel: HandleCancelMsg (m_ReceiveBuffer + offset + 1, msgLen - 1); break;
			case eMessageTypePiece: HandlePieceMsg (m_ReceiveBuffer + offset + 1, msgLen - 1); break;
			case eMessageTypeChoke: HandleChokeMsg (); break;
			case eMessageTypeUnchoke:
				m_IsChoked = false;
				RequestNextBlocks ();
				break;
			case eMessageTypeInterested:
				m_IsRemoteInterested = true;
				if (m_IsRemoteChoked)
				{
					m_IsRemoteChoked = false;
					SendUnchokeMsg ();
				}
				break;
			case eMessageTypeNotInterested:
				m_IsRemoteInterested = false;
				m_UploadRate = 0;
				break;
			case eMessageTypeHave: HandleHaveMsg (m_ReceiveBuffer + offset + 1, msgLen - 1); break;
			case eMessageTypeBitfield: HandleBitfieldMsg (m_ReceiveBuffer + offset + 1, msgLen - 1); break;
			case eMessageTypeHaveAll: HandleHaveAllMsg (); break;
			case eMessageTypeHaveNone: HandleHaveNoneMsg (); break;
			case eMessageTypeExtended: HandleExtendedMsg (m_ReceiveBuffer + offset + 1, msgLen - 1); break;
			case eMessageTypeSuggestPiece: HandleSuggestPieceMsg (m_ReceiveBuffer + offset + 1, msgLen - 1); break;
			case eMessageTypeRejectRequest: HandleRejectRequestMsg (m_ReceiveBuffer + offset + 1, msgLen - 1); break;
			case eMessageTypeAllowedFast: HandleAllowedFastMsg (m_ReceiveBuffer + offset + 1, msgLen - 1); break;
			default:
				LogPrint (eLogWarning, "Torrents: Unexpected message type ", (int)m_ReceiveBuffer[offset], ". Ignored");
			};
		}
		else
			LogPrint (eLogInfo, "Torrents: Keep-alive received");
		return msgLen + 4;
	}

	size_t PeerConnection::HandleHandshakeMsg ()
	{
		LogPrint (eLogDebug, "Torrents: Handshake received");
		if (!m_Stream || m_ReceiveBufferOffset < HANDSHAKE_MSG_LENGTH) return 0;
		if (m_HandshakeReceiveTimer)
		{
			m_HandshakeReceiveTimer->cancel ();
			m_HandshakeReceiveTimer = nullptr;
		}
		if (m_ReceiveBuffer[0] != 19
			|| std::string_view ((const char *)(m_ReceiveBuffer + 1), 19) != "BitTorrent protocol")
		{
			LogPrint (eLogError, "Torrents: Unexpected handshake protocol string");
			Terminate ();
			return 0;
		}
		if (GetTorrentsTunnel ())
		{
			Torrent::InfoHash infoHash;
			memcpy (infoHash.data (), m_ReceiveBuffer + 28, 20);
			auto torrent = GetTorrentsTunnel ()->FindTorrent (infoHash);
			if (!torrent)
			{
				std::string hexHash;
				boost::algorithm::hex (infoHash.begin (), infoHash.end (), std::back_inserter (hexHash));
				LogPrint (eLogWarning, "Torrents: Torrent with InfoHash ", hexHash, " not found");
				Terminate ();
				return 0;
			}
			if (torrent->IsStopped ())
			{
				LogPrint (eLogInfo, "Torrents: Torrent ", torrent->GetName (), " is stopped");
				Terminate ();
				return 0;
			}
			if (m_Torrent)
			{
				// outgoing
				if (m_Torrent->GetInfoHash () != infoHash)
				{
					LogPrint (eLogWarning, "Torrents: InfoHash mistmatch for ", torrent->GetName ());
					Terminate ();
					return 0;
				}
			}
			else
			{
				// incoming
				if (torrent->AddConnection (shared_from_this ()))
					m_Torrent = torrent;
				else
				{
					LogPrint (eLogWarning, "Torrents: Incoming connection with peer ",
						i2p::data::GetIdentHashAbbreviation (m_Stream->GetRemoteIdentity ()->GetIdentHash ()),
						" already exists");
					Terminate ();
					return 0;
				}
			}
		}
		memcpy (m_RemotePeerID.data (), m_ReceiveBuffer + 48, m_RemotePeerID.size ());
		// respond with handshake if incoming
		if (!m_IsHandshakeSent) SendHandshakeMsg ();
		// BEP10
		if (m_ReceiveBuffer[20 + 5] & 0x10) // bit 20 of reserved
			SendExtendedMsg (); // extended handshake if peer supports BEP10
		else if (!m_Torrent->GetLength ()) // we are magnet without info
		{
			LogPrint (eLogInfo, "Torrents: Magnet doesn't have info yet, but BEP10 is not supported by this peer");
			Terminate ();
			return 0;
		}
		// BEP6
		if (m_ReceiveBuffer[20 + 7] & 0x04) // bit 61 of reserved
			m_IsFast = true;
		m_IsEstablished = true;
		// send bitfield, have all or have none
		auto [bitfield, have] = m_Torrent->CreateBitfield ();
		if (!have) // have none
		{
			if (m_IsFast) SendHaveNoneMsg ();
			// otherwise send nothing
		}
		else if (have && m_IsFast) // have all
			SendHaveAllMsg ();
		else
			SendBitfieldMsg (bitfield.data (), bitfield.size ());

		return HANDSHAKE_MSG_LENGTH;
	}

	void PeerConnection::SendHandshakeMsg ()
	{
		if (!m_Torrent || !m_Stream) return;
		uint8_t buf[HANDSHAKE_MSG_LENGTH];
		buf[0] = 19;
		memcpy (buf + 1, "BitTorrent protocol", 19);
		memset (buf + 20, 0, 8); // reserved
		buf[20 + 5] |= 0x10; // bit 20 of reserved, BEP10
		buf[20 + 7] |= 0x04; // bit 61 of reserved, BEP6
		memcpy (buf + 28, m_Torrent->GetInfoHash ().data (), 20);
		// Per-torrent random peer id; the tunnel-wide identity is never used.
		memcpy (buf + 48, m_Torrent->GetPeerID ().data (), 20);
		WriteToStream (buf, HANDSHAKE_MSG_LENGTH);
		m_IsHandshakeSent = true;
	}

	void PeerConnection::HandleHaveMsg (const uint8_t * buf, size_t len)
	{
		if (len < 4) return;
		if (m_RemoteBitfield.empty ()) // bitfield was not received before because was empty
			m_RemoteBitfield.resize (m_Torrent->GetNumPieces ());
		uint32_t index = bufbe32toh (buf);
		if (index < m_RemoteBitfield.size () && m_Torrent && index < m_Torrent->GetNumPieces ())
		{
			m_RemoteBitfield.set (index);
			if (!m_Torrent->IsComplete ())
			{
				Piece & piece = m_Torrent->GetPiece (index);
				if (!piece.IsComplete ())
				{
					// new piece
					if (!m_IsInterested)
					{
						m_IsInterested = true;
						SendInterestedMsg ();
					}
					if (m_LastRequestedPieceIndex < 0 && !m_IsChoked) RequestNextBlocks ();
				}
			}
		}
	}

	void PeerConnection::SendHaveMsg (uint32_t index)
	{
		if (m_IsEstablished)
		{
			uint8_t buf[HAVE_MSG_PAYLOAD_LENGTH + 5];
			htobe32buf (buf, HAVE_MSG_PAYLOAD_LENGTH + 1); // length
			buf[4] = eMessageTypeHave; // msg ID
			htobe32buf (buf + 5, index);
			WriteToStream (buf, HAVE_MSG_PAYLOAD_LENGTH + 5);
		}
	}

	void PeerConnection::HandleBitfieldMsg (const uint8_t * buf, size_t len)
	{
		if (!m_Torrent || !m_Torrent->GetLength ()) return; // we are magnet and don't have torrent info yet
		// A received bitfield (even an all-zero one) is authoritative: it is
		// the only signal that lets later `.all()` checks mean "remote is a
		// seed".
		m_RemoteBitfieldReceived = true;
		m_IsInterested = false;
		size_t numPieces = m_Torrent->GetNumPieces ();
		m_RemoteBitfield.resize (numPieces);
		size_t idx = 0;
		for (size_t i = 0; i < len; i++) // bytes
		{
			uint8_t bit = 0x80;
			for (int j = 0; j < 8; j++)
			{
				if (idx >= numPieces) break;
				if (buf[i] & bit)
				{
					m_RemoteBitfield.set (idx);
					if (!m_IsInterested && !m_Torrent->GetPiece (static_cast<int> (idx)).IsComplete ())
						m_IsInterested = true;
				}
				bit >>= 1;
				idx++;
			}
		}
		if (m_IsInterested)
			SendInterestedMsg ();
		else if (IsRemoteSeed ()) // remote advertised every piece
			Terminate (); // we don't need this connection
	}

	void PeerConnection::SendBitfieldMsg (const uint8_t * bitfield, size_t bitfieldLen)
	{
		std::vector<uint8_t> sendBuffer (bitfieldLen + 5);
		htobe32buf (sendBuffer.data (), static_cast<uint32_t> (bitfieldLen + 1)); // length
		sendBuffer[4] = eMessageTypeBitfield; // msg ID
		memcpy (sendBuffer.data () + 5, bitfield, bitfieldLen);
		WriteToStream (sendBuffer.data (), sendBuffer.size ());
	}

	void PeerConnection::HandleHaveAllMsg ()
	{
		if (!m_Torrent) return;
		size_t numPieces = m_Torrent->GetNumPieces ();
		m_RemoteBitfield.resize (numPieces);
		m_RemoteBitfield.set ();
		m_RemoteBitfieldReceived = true;
		if (!m_Torrent->IsComplete ())
		{
			m_IsInterested = true;
			SendInterestedMsg ();
		}
		else
			Terminate (); // we don't need this connection
	}

	void PeerConnection::SendHaveAllMsg ()
	{
		uint8_t buf[HAVE_ALL_MSG_LENGTH];
		htobe32buf (buf, 1);
		buf[4] = eMessageTypeHaveAll;
		WriteToStream (buf, HAVE_ALL_MSG_LENGTH);
	}

	void PeerConnection::HandleHaveNoneMsg ()
	{
		if (!m_Torrent) return;
		size_t numPieces = m_Torrent->GetNumPieces ();
		m_RemoteBitfield.resize (numPieces);
		m_RemoteBitfield.reset ();
		m_RemoteBitfieldReceived = true;
	}

	void PeerConnection::SendHaveNoneMsg ()
	{
		uint8_t buf[HAVE_NONE_MSG_LENGTH];
		htobe32buf (buf, 1);
		buf[4] = eMessageTypeHaveNone;
		WriteToStream (buf, HAVE_NONE_MSG_LENGTH);
	}

	void PeerConnection::HandlePieceMsg (const uint8_t * buf, size_t len)
	{
		if (len < 8) return;
		uint32_t index = bufbe32toh (buf);
		uint32_t offset = bufbe32toh (buf + 4);
		len -= 8;
		bool applied = false;
		if (len && index < m_Torrent->GetNumPieces ())
		{
			Piece & piece = m_Torrent->GetPiece (index);
			if (!m_Torrent->CanAcceptPieceBlock (index, offset, len))
			{
				// A stopped torrent silently ignores late blocks from an
				// already-established peer. Report malformed ranges only for
				// a running torrent so pausing does not flood the log.
				if (!m_Torrent->IsStopped ())
					LogPrint (
						eLogWarning, "Torrents: Piece block (", index, ",", offset, ",", len, ") is outside the piece");
			}
			else
			{
				applied = piece.BlockReceived (buf + 8, len, offset);
				if (applied && piece.IsComplete () && !piece.IsSaving ())
				{
					piece.SetIsSaving (true);
					if (piece.VerifyHash ())
					{
						// A removed torrent may still be kept alive by a
						// PeerConnection. Only the session map is
						// authoritative: never append payload or rewrite the
						// .resume file for a torrent that is no longer
						// registered (or was replaced at the same infohash).
						if (auto tunnel = GetTorrentsTunnel ())
							boost::asio::post (tunnel->GetDiskIOService (),
								[index, torrent = std::weak_ptr<Torrent> (m_Torrent), tunnel,
									infoHash = m_Torrent->GetInfoHash ()] () mutable
								{
									auto current = torrent.lock ();
									if (!current || current->IsRemoved () || tunnel->FindTorrent (infoHash) != current)
										return;
									auto fragments = current->GetPieceFileFragments (index);
									Piece & piece = current->GetPiece (index);
									bool saved = !fragments.empty ();
									for (auto & it : fragments)
										if (!piece.Dump (std::move (it)))
										{
											saved = false;
											break;
										}
									if (!saved)
									{
										// Withhold the piece as before, but also
										// surface the storage failure: the UI and
										// the announce path must not keep telling
										// the swarm that this torrent is complete
										// and being seeded while payload cannot be
										// written. The piece is Missing again, so
										// normal scheduling re-requests it later
										// (no unbounded retry loop here); a later
										// successful write clears the error again.
										current->SetError (eTorrentErrorFilesystem);
										current->InvalidatePiece (index);
										LogPrint (
											eLogError, "Torrents: Piece storage failed; piece remains unavailable");
										return;
									}
									piece.SetIsSaving (false);
									// A verified write proves the storage
									// backend works again; clear a transient
									// preparation failure so the torrent is not
									// permanently marked and silenced.
									if (current->GetError () == eTorrentErrorFilesystem)
										current->SetError (eTorrentErrorNoError);
									current->SaveTorrentResumeFile ();
									boost::asio::post (tunnel->GetService (),
										[torrent, tunnel, infoHash, index] ()
										{
											auto current = torrent.lock ();
											if (!current || current->IsStopped ()
												|| tunnel->FindTorrent (infoHash) != current)
												return;
											for (auto & connection : current->GetConnections ())
												connection->SendHaveMsg (index);
										});
								});
						else
							m_Torrent->InvalidatePiece (index);
					}
					else
					{
						LogPrint (eLogWarning, "Torrents: Received piece hash mismatch");
						// Best-effort corrupt-peer accounting. Only an applied
						// block that completed the piece reaches this branch, so
						// a late/duplicate block never charges a peer. A shared
						// piece may have been assembled from several peers, so
						// this charges the connection that delivered the block
						// completing it, not every contributor.
						if (const auto remote = GetRemoteIdentHash ())
						{
							const auto failures = m_Torrent->ChargePieceFailure (*remote);
							if (failures == Torrent::PEER_PENALTY_THRESHOLD)
								LogPrint (eLogWarning, "Torrents: Peer ", i2p::data::GetIdentHashAbbreviation (*remote),
									" repeatedly contributed corrupt piece data; disconnecting");
							if (failures >= Torrent::PEER_PENALTY_THRESHOLD) Close ();
						}
						m_Torrent->InvalidatePiece (index);
					}
				}
			}
		}
		ReleaseOutgoingRequest (index, offset);
		if (m_NumRequests <= m_MaxNumRequests * 2 / 3) RequestNextBlocks ();
		if (!applied) return;
		// update stats only for a block that changed the piece state. Charge
		// the bytes actually received: the final block of a piece is shorter
		// than REQUEST_BLOCK_SIZE, so the constant would overstate the
		// totals.
		const auto accounted = BlockTransferBytes (len);
		m_Downloaded += accounted;
		m_Torrent->AddDownloaded (accounted);
		auto ts = i2p::util::GetMonotonicMilliseconds ();
		if (m_LastBlockDownloadTimestamp)
		{
			m_ReceivedSinceLastTimestamp += accounted;
			auto delta = ts - m_LastBlockDownloadTimestamp;
			if (delta >= BANDWIDTH_RATE_SAMPLING_INTERVAL)
			{
				if (m_DownloadRate)
					m_DownloadRate = (m_DownloadRate + m_ReceivedSinceLastTimestamp * 1000 / delta) / 2;
				else
					m_DownloadRate = m_ReceivedSinceLastTimestamp * 1000 / delta;
				m_LastBlockDownloadTimestamp = ts;
				m_ReceivedSinceLastTimestamp = 0;
			}
		}
		else
			m_LastBlockDownloadTimestamp = ts;
	}

	void PeerConnection::SendPieceMsg (uint32_t index, uint32_t offset, const uint8_t * data, size_t len)
	{
		if (!m_Stream) return;
		std::array<uint8_t, PIECE_MSG_LENGTH> header { };
		htobe32buf (header.data (), static_cast<uint32_t> (len + 8 + 1)); // length
		header[4] = eMessageTypePiece; // msg ID
		htobe32buf (header.data () + 5, index);
		htobe32buf (header.data () + 9, offset);
		LogPrint (eLogDebug, "Torrents: Sending piece index ", index, " offset ", offset, " length ", len);
		m_NumPieces++;
		const std::vector<std::pair<const uint8_t *, size_t>> buffers { { header.data (), header.size () },
			{ data, len } };
		auto sendBuffer = std::make_shared<i2p::stream::SendBuffer> (buffers, header.size () + len,
			[s = shared_from_this (), blockLength = len] (
				const boost::system::error_code & ecode, size_t)
			{
				if (s->m_NumPieces > 0) s->m_NumPieces--;
				if (!ecode && s->m_Stream)
				{
					while (!s->m_IncomingRequestsQueue.empty () && s->m_NumPieces < MAX_NUM_PIECES)
					{
						s->SendRequestedBlock (s->m_IncomingRequestsQueue.front ());
						s->m_IncomingRequestsQueue.pop_front ();
					}
					if (s->m_IsRemoteChoked && s->m_IncomingRequestsQueue.size () < 2 * MAX_NUM_PIECES)
					{
						LogPrint (eLogDebug, "Torrents: Unchoke");
						s->m_IsRemoteChoked = false;
						s->SendUnchokeMsg ();
					}
					// update stats
					auto ts = i2p::util::GetMonotonicMilliseconds ();
					if (s->m_LastBlockUploadTimestamp)
					{
						s->m_SentSinceLastTimestamp += blockLength;
						auto delta = ts - s->m_LastBlockUploadTimestamp;
						if (delta >= BANDWIDTH_RATE_SAMPLING_INTERVAL)
						{
							if (s->m_UploadRate)
								s->m_UploadRate = (s->m_UploadRate + s->m_SentSinceLastTimestamp * 1000 / delta) / 2;
							else
								s->m_UploadRate = s->m_SentSinceLastTimestamp * 1000 / delta;
							s->m_LastBlockUploadTimestamp = ts;
							s->m_SentSinceLastTimestamp = 0;
						}
					}
					else
						s->m_LastBlockUploadTimestamp = ts;
				}
				else
					s->Terminate ();
			});
		m_Stream->Send (std::move (sendBuffer));
		m_LastSendTime = i2p::util::GetMonotonicSeconds ();
		// Charge what this block actually carried; the final block of a piece
		// is shorter than REQUEST_BLOCK_SIZE.
		m_Uploaded += BlockTransferBytes (len);
		m_Torrent->AddUploaded (len);
	}

	void PeerConnection::HandleRejectRequestMsg (const uint8_t * buf, size_t len)
	{
		if (len < 8 || !m_Torrent) return;
		uint32_t index = bufbe32toh (buf);
		uint32_t offset = bufbe32toh (buf + 4);
		LogPrint (eLogDebug, "Torrents: Reject request msg received index ", index, " offset ", offset);
		ReleaseOutgoingRequest (index, offset);
		RequestNextBlocks ();
	}

	void PeerConnection::SendRejectRequestMsg (uint32_t index, uint32_t offset, uint32_t len)
	{
		uint8_t buf[REJECT_REQUEST_MSG_LENGTH];
		htobe32buf (buf, REJECT_REQUEST_MSG_PAYLOAD_LENGTH + 1); // msg length
		buf[4] = eMessageTypeRejectRequest; // msg ID
		htobe32buf (buf + 5, index); // index
		htobe32buf (buf + 9, offset); // offset
		htobe32buf (buf + 13, len); // length
		WriteToStream (buf, REJECT_REQUEST_MSG_LENGTH);
	}

	void PeerConnection::HandleCancelMsg (const uint8_t * buf, size_t len)
	{
		if (!m_Torrent) return;
		if (len != CANCEL_MSG_PAYLOAD_LENGTH)
		{
			LogPrint (eLogWarning, "Torrents: Unexpected length of cancel message ", len);
			return;
		}
		const uint32_t index = bufbe32toh (buf);
		const uint32_t offset = bufbe32toh (buf + 4);
		const uint32_t length = bufbe32toh (buf + 8);
		const RequestedBlock cancelled { index, offset, length };
		// Only a request still queued can be cancelled; an in-flight send is
		// already copying the block and must not be interrupted mid-frame.
		// BEP 3 identifies the request by the exact index/offset/length
		// triple.
		auto it = std::find (m_IncomingRequestsQueue.begin (), m_IncomingRequestsQueue.end (), cancelled);
		if (it == m_IncomingRequestsQueue.end ()) return;
		m_IncomingRequestsQueue.erase (it);
		// Reject Request is a BEP 6 Fast Extension message, so it is only
		// sent to a peer that negotiated the extension. Upstream answers
		// unconditionally, which would be a protocol violation here.
		if (m_IsFast) SendRejectRequestMsg (index, offset, length);
	}

	void PeerConnection::HandleRequestMsg (const uint8_t * buf, size_t len)
	{
		if (!m_Torrent) return;
		if (len != REQUEST_MSG_PAYLOAD_LENGTH)
		{
			LogPrint (eLogWarning, "Torrents: Unexpected length of request message ", len);
			return;
		}
		uint32_t index = bufbe32toh (buf);
		uint32_t offset = bufbe32toh (buf + 4);
		uint32_t length = bufbe32toh (buf + 8);
		if (length > REQUEST_BLOCK_SIZE)
		{
			LogPrint (eLogWarning, "Torrents: Requested length is too long ", length);
			return;
		}
		if (index < m_Torrent->GetNumPieces ())
		{
			LogPrint (eLogDebug, "Torrents: Received request index ", index, " offset ", offset, " length ", length);
			Piece & piece = m_Torrent->GetPiece (index);
			auto pieceSize = piece.GetSize ();
			// Reject zero-length and out-of-range requests. Checking both range
			// ends also prevents serving uninitialised bytes that belong to a
			// neighbouring, not yet downloaded block.
			if (length && offset <= pieceSize && length <= pieceSize - offset && piece.HasBlock (offset)
				&& piece.HasBlock (offset + length - 1))
			{
				if (m_NumPieces >= MAX_NUM_PIECES)
				{
					// Queue each (index, offset, length) once. Without this a
					// peer can fill the bounded queue with copies of the same
					// request and starve useful uploads.
					const RequestedBlock request { index, offset, length };
					bool alreadyQueued
						= std::find (m_IncomingRequestsQueue.begin (), m_IncomingRequestsQueue.end (), request)
						!= m_IncomingRequestsQueue.end ();
					if (!alreadyQueued
						&& m_IncomingRequestsQueue.size () + m_NumPieces < MAX_INCOMING_REQUESTS_QUEUE_SIZE)
						m_IncomingRequestsQueue.emplace_back (request);
					else if (!alreadyQueued && m_IsFast)
						SendRejectRequestMsg (index, offset, length);
					else if (!alreadyQueued && !m_IsRemoteChoked)
					{
						LogPrint (eLogDebug, "Torrents: Choke");
						m_IsRemoteChoked = true;
						SendChokeMsg ();
					}
				}
				else if (!SendRequestedBlock ({ index, offset, length })) // block was not sent
				{
					// try to load from file
					boost::asio::post (GetTorrentsTunnel ()->GetDiskIOService (),
						[requestBlock = RequestedBlock { index, offset, length }, torrent = m_Torrent,
							s = shared_from_this ()] () mutable
						{
							if (torrent->IsRemoved ()) return;
							bool loaded = true;
							auto [index, offset, len] = requestBlock;
							Piece & piece = torrent->GetPiece (index);
							piece.SetIsSending (true);
							piece.SetIsLoading (true);
							// Data without a matching verified flag (previous
							// failed load, eviction or corruption) is reloaded
							// and rehashed instead of being served blindly.
							if (!piece.HasData () || !piece.IsVerified ())
							{
								auto fragments = torrent->GetPieceFileFragments (index);
								for (auto & it : fragments)
								{
									if (!piece.Load (std::move (it)))
									{
										loaded = false;
										break;
									}
								}
								if (loaded && !piece.VerifyHash ())
								{
									LogPrint (eLogError, "Torrents: Corrupted piece ", index);
									loaded = false;
								}
							}
							piece.SetIsLoading (false);
							piece.SetIsSending (false);
							if (!loaded)
							{
								// A complete piece whose local data failed to
								// load or hash must not stay complete: rebuild
								// it as Missing so it can be downloaded again.
								torrent->InvalidatePiece (index);
							}
							if (loaded)
								boost::asio::post (s->GetTorrentsTunnel ()->GetService (),
									[requestedBlock = std::move (requestBlock), s] ()
									{
										if (!s->SendRequestedBlock (requestedBlock))
										{
											LogPrint (eLogError, "Torrents: Couldn't send block from loaded piece");
											auto [rejectIndex, rejectOffset, rejectLength] = requestedBlock;
											s->SendRejectRequestMsg (rejectIndex, rejectOffset, rejectLength);
										}
									});
							else
							{
								LogPrint (eLogError, "Torrents: Failed to load piece ", index);
								if (s->m_IsFast)
									boost::asio::post (s->GetTorrentsTunnel ()->GetService (),
										[requestedBlock = std::move (requestBlock), s] ()
										{
											auto [rejectIndex, rejectOffset, rejectLength] = requestedBlock;
											s->SendRejectRequestMsg (rejectIndex, rejectOffset, rejectLength);
										});
							}
						});
				}
			}
			else
			{
				LogPrint (eLogWarning, "Torrents: Requested block (", index, ",", offset, ") is not available");
				if (m_IsFast) SendRejectRequestMsg (index, offset, length);
			}
		}
		else
		{
			LogPrint (eLogWarning, "Torrents: Requested index ", index, "exceeds number of pieces",
				m_Torrent->GetNumPieces ());
			if (m_IsFast) SendRejectRequestMsg (index, offset, length);
		}
	}

	bool PeerConnection::SendRequestedBlock (const RequestedBlock & requestedBlock)
	{
		bool ret = true;
		auto [index, offset, len] = requestedBlock;
		Piece & piece = m_Torrent->GetPiece (index);
		if (piece.IsLoading ())
		{
			LogPrint (eLogDebug, "Torrents: Piece ", index, " is still loading");
			return false;
		}
		piece.SetIsSending (true);
		// CopyBlock validates the whole request under the piece lock and
		// returns a private copy, so the buffer cannot be freed by Reset,
		// MarkCorrupt or the disk thread while SendPieceMsg is using it.
		std::vector<uint8_t> block;
		if (piece.CopyBlock (offset, len, block))
			SendPieceMsg (index, offset, block.data (), block.size ());
		else
			ret = false;
		piece.SetIsSending (false);
		return ret;
	}

	void PeerConnection::SendRequestMsg (uint32_t index, uint32_t offset, uint32_t len)
	{
		uint8_t buf[REQUEST_MSG_LENGTH];
		FillRequestMsg (buf, index, offset, len);
		WriteToStream (buf, REQUEST_MSG_LENGTH);
	}

	size_t PeerConnection::FillRequestMsg (uint8_t * buf, uint32_t index, uint32_t offset, uint32_t len)
	{
		htobe32buf (buf, REQUEST_MSG_PAYLOAD_LENGTH + 1); // msg length
		buf[4] = eMessageTypeRequest; // msg ID
		htobe32buf (buf + 5, index); // index
		htobe32buf (buf + 9, offset); // offset
		htobe32buf (buf + 13, len); // length
		return REQUEST_MSG_LENGTH;
	}

	void PeerConnection::SendInterestedMsg ()
	{
		uint8_t buf[INTERESTED_MSG_LENGTH];
		htobe32buf (buf, 1);
		buf[4] = eMessageTypeInterested;
		WriteToStream (buf, INTERESTED_MSG_LENGTH);
	}

	void PeerConnection::SendNotinterestedMsg ()
	{
		uint8_t buf[NOTINTERESTED_MSG_LENGTH];
		htobe32buf (buf, 1);
		buf[4] = eMessageTypeNotInterested;
		WriteToStream (buf, NOTINTERESTED_MSG_LENGTH);
	}

	void PeerConnection::SendChokeMsg ()
	{
		uint8_t buf[CHOKE_MSG_LENGTH];
		htobe32buf (buf, 1);
		buf[4] = eMessageTypeChoke;
		WriteToStream (buf, CHOKE_MSG_LENGTH);
	}

	void PeerConnection::SendUnchokeMsg ()
	{
		uint8_t buf[UNCHOKE_MSG_LENGTH];
		htobe32buf (buf, 1);
		buf[4] = eMessageTypeUnchoke;
		WriteToStream (buf, UNCHOKE_MSG_LENGTH);
	}

	void PeerConnection::HandleChokeMsg ()
	{
		m_IsChoked = true;
		ReleaseOutgoingRequests ();
		m_LastRequestedPieceIndex = -1;
		m_DownloadRate = 0;
	}

	void PeerConnection::HandleSuggestPieceMsg (const uint8_t * buf, size_t len)
	{
		if (len < 4) return;
		uint32_t index = bufbe32toh (buf);
		LogPrint (eLogDebug, "Torrents: suggest piece msg received ", index);
		if (IsPieceAvailable (index)) m_SuggestedPieceIndex = index;
	}

	void PeerConnection::HandleAllowedFastMsg (const uint8_t *, size_t)
	{
		LogPrint (eLogDebug, "Torrents: allowed fast msg received");
		// ignore for now
	}

	void PeerConnection::HandleExtendedMsg (const uint8_t * buf, size_t len)
	{
		if (len < 1) return;
		if (!buf[0]) // Handshake
		{
			// BEP10 negotiates the extension map once. A repeated ID-0 frame
			// must not re-run the trigger block below: every re-run would
			// request the metadata again, clear the bounded PEX announcement set
			// and advertise the DHT endpoint again, so one frame could amplify
			// four outgoing messages. Write-once extension IDs keep whatever the
			// first handshake installed.
			if (m_ExtendedHandshakeReceived)
			{
				LogPrint (eLogDebug, "Torrents: Ignoring a repeated BEP10 handshake");
				return;
			}
			if (!m_Torrent) return;
			const std::string_view payload ((const char *)buf + 1, len - 1);
			// Parse into locals and apply only after the whole dictionary has
			// been consumed, so a handshake with trailing bytes cannot leave a
			// half-applied negotiation behind.
			struct
			{
				std::vector<std::pair<std::string, int64_t>> extensions;
				int64_t metadataSize = 0;
				int64_t requests = 0;
				std::string_view clientVersion { };
				bool hasMetadataSize = false, hasRequests = false, hasClientVersion = false;
			} handshake;
			auto parsed = ParseDictionary (payload,
				[&handshake] (std::string_view key, std::string_view value) -> size_t
				{
					if (key == "m")
					{
						return ParseDictionary (value,
							[&handshake] (std::string_view extension, std::string_view msgID) -> size_t
							{
								auto [id, l] = ExtractInteger (msgID);
								if (l && handshake.extensions.size () < MAX_EXTENDED_HANDSHAKE_EXTENSIONS)
									handshake.extensions.emplace_back (std::string (extension), id);
								return l;
							});
					}
					if (key == "metadata_size")
					{
						auto [size, l] = ExtractInteger (value);
						if (l)
						{
							handshake.metadataSize = size;
							handshake.hasMetadataSize = true;
						}
						return l;
					}
					if (key == "reqq")
					{
						auto [requests, l] = ExtractInteger (value);
						if (l)
						{
							handshake.requests = requests;
							handshake.hasRequests = true;
						}
						return l;
					}
					if (key == "v")
					{
						auto [version, l] = ExtractByteString (value);
						if (l)
						{
							// A client string is diagnostics only; cap it so a
							// handshake cannot grow per-connection state.
							handshake.clientVersion = version.substr (0, MAX_EXTENDED_CLIENT_VERSION_LENGTH);
							handshake.hasClientVersion = true;
						}
						return l;
					}
					return 0;
				});
			// `len` includes the extended message id; the dictionary starts
			// after it. Anything else is a malformed or padded handshake.
			if (!parsed || parsed != payload.size ())
			{
				LogPrint (eLogWarning, "Torrents: Malformed BEP10 handshake");
				return;
			}
			m_ExtendedHandshakeReceived = true;
			for (const auto & [extension, id] : handshake.extensions) AddExtendedMsgHandler (extension, id);
			if (handshake.hasMetadataSize)
			{
				if (handshake.metadataSize < 0
					|| (size_t)handshake.metadataSize > MAX_NUM_TORRENT_PIECES * SHA_DIGEST_LENGTH)
				{
					LogPrint (eLogError, "Torrents: Invalid metadata_size ", handshake.metadataSize);
					m_RemoteMetadataSize = 0;
				}
				else
					m_RemoteMetadataSize = static_cast<size_t> (handshake.metadataSize);
			}
			if (handshake.hasRequests)
			{
				// Clamp as a signed value before the cast: a negative reqq must
				// not become a huge size_t that is then clamped by accident.
				m_MaxNumRequests = static_cast<size_t> (
					std::clamp<int64_t> (handshake.requests, MIN_NUM_REQUESTS, MAX_NUM_REQUESTS));
			}
			if (handshake.hasClientVersion) m_RemoteName = handshake.clientVersion;
			// trigger extensions
			// BEP9
			if (!m_Torrent->GetLength ()) // magnet without info
			{
				if (m_RemoteMsgIDUtMetadata && m_RemoteMetadataSize) // peer supports BEP9
					// request first piece of info
					RequestUtMetadata ();
				else
				{
					LogPrint (
						eLogInfo, "Torrents: Magnet doesn't have info yet, but BEP9 is not supported by this peer");
					Close ();
				}
			}
			// BEP11: answer both outgoing and incoming peers. The private-swarm
			// suppression and the 120 s per-connection interval live in
			// NotifyPEXPeers(), so the handshake path cannot bypass either.
			if (m_RemoteMsgIDI2PPEX) NotifyPEXPeers ();
			// BEP5: the advertisement checks DHT support and its own interval.
			SendDHTPortAdvertisement ();
			RequestComments ();
		}
		else
		{
			auto it = m_ExtendedMessageHandlers.find (buf[0]);
			if (it != m_ExtendedMessageHandlers.end ())
				(this->*(it->second)) (buf + 1, len - 1);
			else
				LogPrint (eLogInfo, "Torrents: Unexpected extended message type ", (int)buf[0], " received");
		}
	}

	void PeerConnection::AddExtendedMsgHandler (std::string_view extensionName, int64_t msgID)
	{
		// BEP10 reserves 0 for "not supported". Negotiation is write-once per
		// connection so a later handshake cannot redirect our outgoing IDs.
		if (msgID <= 0 || msgID > 255) return;
		if (extensionName == COMMENT_EXTENSION_NAME)
		{
			if (m_RemoteMsgIDComment) return;
			// Our advertised ID is fixed at 4; incoming frames are dispatched by
			// that local ID. The peer's advertised ID is only for frames we
			// send. Registering the peer's value here would let a peer claim the
			// ut_metadata slot and break metadata exchange.
			m_ExtendedMessageHandlers.emplace (4, &PeerConnection::HandleCommentExtension);
			m_RemoteMsgIDComment = static_cast<uint8_t> (msgID);
			return;
		}
		if (extensionName == EXTENSION_NAME_UT_METADATA)
		{
			if (m_RemoteMsgIDUtMetadata) return;
			m_ExtendedMessageHandlers.emplace (EXTENSION_MSGID_UT_METADATA, &PeerConnection::HandleUtMetadataExtension);
			m_RemoteMsgIDUtMetadata = static_cast<uint8_t> (msgID);
		}
		else if (extensionName == EXTENSION_NAME_I2P_PEX)
		{
			if (m_RemoteMsgIDI2PPEX) return;
			m_ExtendedMessageHandlers.emplace (EXTENSION_MSGID_I2P_PEX, &PeerConnection::HandleI2PPEXExtension);
			m_RemoteMsgIDI2PPEX = static_cast<uint8_t> (msgID);
		}
		else if (extensionName == EXTENSION_NAME_I2P_DHT)
		{
			if (m_RemoteMsgIDI2PDHT) return;
			m_ExtendedMessageHandlers.emplace (EXTENSION_MSGID_I2P_DHT, &PeerConnection::HandleI2PDHTExtension);
			m_RemoteMsgIDI2PDHT = static_cast<uint8_t> (msgID);
		}
	}

	void PeerConnection::SendExtendedMsg (uint8_t extendedMsgID, std::string_view payload, std::string_view data)
	{
		std::string str;
		if (!extendedMsgID) // handshake
		{
			auto metadata = CreateInteger (EXTENSION_MSGID_UT_METADATA);
			auto pex = CreateInteger (EXTENSION_MSGID_I2P_PEX);
			auto dht = CreateInteger (EXTENSION_MSGID_I2P_DHT);
			const bool comments
				= m_Torrent->GetCommentStore ().IsEnabled () && m_Torrent->IsValid () && !m_Torrent->IsPrivate ();
			auto comment = CreateInteger (4);
			std::vector<std::pair<std::string_view, std::string_view>> extensions;
			if (!m_Torrent->IsPrivate ())
			{
				if (GetTorrentsTunnel ()->SupportsDHT ()) extensions.emplace_back (EXTENSION_NAME_I2P_DHT, dht);
				extensions.emplace_back (EXTENSION_NAME_I2P_PEX, pex);
			}
			// Omit the key entirely when comments are disabled/private.
			// Advertising ut_comment:0 is not "disabled" in BEP10 and Java
			// treats key presence as capability.
			if (comments) extensions.emplace_back (COMMENT_EXTENSION_NAME, comment);
			// A private torrent with metadata must not offer its info dict.
			// Magnet mode still needs ut_metadata to acquire it.
			const bool advertiseMetadata = !m_Torrent->IsPrivate () || !m_Torrent->IsValid ();
			if (advertiseMetadata) extensions.emplace_back (EXTENSION_NAME_UT_METADATA, metadata);
			auto encodedExtensions = CreateDictionary (extensions);
			auto requests = CreateInteger (MAX_INCOMING_REQUESTS_QUEUE_SIZE);
			auto version = CreateByteString ("i2pd");
			std::vector<std::pair<std::string_view, std::string_view>> fields { { "m", encodedExtensions },
				{ "reqq", requests }, { "v", version } };
			// metadata_size is the exact info-dict length. Emitting it without
			// advertising ut_metadata would leak the size of an info dict this
			// peer is never allowed to request, so both are gated together.
			std::string metadataSize;
			if (advertiseMetadata)
			{
				metadataSize = CreateInteger (m_Torrent->GetInfo ().size ());
				fields.emplace_back ("metadata_size", metadataSize);
			}
			str = CreateDictionary (fields);
			payload = str;
		}
		std::vector<uint8_t> sendBuffer (payload.length () + data.length () + 1 + 5);
		htobe32buf (sendBuffer.data (),
			static_cast<uint32_t> (payload.length () + data.length () + 1 + 1)); // length
		sendBuffer[4] = eMessageTypeExtended; // msg ID
		sendBuffer[5] = extendedMsgID;
		memcpy (sendBuffer.data () + 6, payload.data (), payload.size ());
		if (!data.empty ()) memcpy (sendBuffer.data () + 6 + payload.size (), data.data (), data.size ());
		WriteToStream (sendBuffer.data (), sendBuffer.size ());
	}

	void PeerConnection::RefreshFileSelection ()
	{
		if (!m_IsEstablished || !m_Torrent) return;
		const bool interested = !m_Torrent->IsStopped () && m_Torrent->HasIncompletePieces (m_RemoteBitfield);
		if (interested != m_IsInterested)
		{
			m_IsInterested = interested;
			if (interested)
				SendInterestedMsg ();
			else
				SendNotinterestedMsg ();
		}
		if (interested && !m_IsChoked) RequestNextBlocks ();
	}

	void PeerConnection::RefreshComments ()
	{
		if (!m_IsEstablished || !m_Torrent) return;
		SendExtendedMsg (); // BEP10 subsequent handshake updates support, including ID=0.
		RequestComments ();
	}

	void PeerConnection::RequestComments ()
	{
		if (!m_Torrent || !m_Torrent->IsValid () || m_Torrent->IsPrivate ()
			|| !m_Torrent->GetCommentStore ().IsEnabled () || !m_RemoteMsgIDComment)
			return;
		const auto now = i2p::util::GetMonotonicSeconds ();
		const auto revision = m_Torrent->GetCommentStore ().Revision ();
		// A local add/hide/restore changes the store revision and is worth an
		// immediate refresh, because the periodic cadence alone would usually
		// only re-handshake. RefreshComments() runs once per connection for
		// every local mutation, so the minimum interval coalesces a burst of
		// edits; without a revision change the slower periodic cadence
		// applies.
		const bool revisionChanged = revision != m_LastCommentRequestRevision;
		const auto minInterval = revisionChanged ? COMMENT_REQUEST_MIN_INTERVAL : COMMENT_REQUEST_PERIODIC_INTERVAL;
		if (m_LastCommentRequest && (now < m_LastCommentRequest || now - m_LastCommentRequest < minInterval)) return;
		m_LastCommentRequest = now;
		m_LastCommentRequestRevision = revision;
		SendExtendedMsg (m_RemoteMsgIDComment, EncodeTorrentCommentRequest ());
	}

	void PeerConnection::HandleCommentExtension (const uint8_t * buf, size_t len)
	{
		if (!m_Torrent || !m_Torrent->IsValid () || m_Torrent->IsPrivate ()
			|| !m_Torrent->GetCommentStore ().IsEnabled () || !m_RemoteMsgIDComment)
			return;
		auto now = i2p::util::GetMonotonicSeconds ();
		if (!m_CommentWindow || now - m_CommentWindow >= 60)
		{
			m_CommentWindow = now;
			m_CommentBytes = 0;
			m_CommentMessages = 0;
			// The admitted-entry budget rolls with the same ingress window.
			m_CommentEntryWindow = { };
		}
		if (len > MAX_COMMENT_MESSAGE_BYTES || m_CommentMessages >= 8 || len > 128 * 1024 - m_CommentBytes) return;
		++m_CommentMessages;
		m_CommentBytes += len;
		auto message = DecodeTorrentComments (
			std::string_view (reinterpret_cast<const char *> (buf), len), i2p::util::GetSecondsSinceEpoch ());
		if (!message) return;
		if (message->request)
		{
			if (m_LastCommentReply && now - m_LastCommentReply < 60) return;
			m_LastCommentReply = now;
			auto response = EncodeTorrentComments (
				m_Torrent->GetCommentStore ().Visible (), i2p::util::GetSecondsSinceEpoch (), message->requested);
			if (response.size () <= MAX_COMMENT_MESSAGE_BYTES) SendExtendedMsg (m_RemoteMsgIDComment, response);
		}
		else
		{
			// A single reply may describe up to 256 comments, but accepting all
			// of them at once would let one peer flush the bounded store. Two
			// budgets apply: this connection's entry window and the torrent-wide
			// one, so reconnecting does not reset the effective rate. Only an
			// entry whose normalized (owner,text) is not already stored is
			// charged, so a duplicate replay cannot starve other peers; a later
			// store-side quota rejection keeps the conservative charge.
			auto & store = m_Torrent->GetCommentStore ();
			size_t offered = 0;
			for (auto & comment : message->comments)
			{
				if (offered >= MAX_COMMENT_MESSAGE_COUNT) break;
				if (store.IsDuplicate (comment)) continue;
				if (!m_CommentEntryWindow.Admit (1, MAX_COMMENT_ENTRIES_PER_CONNECTION_WINDOW, now)) break;
				if (!m_Torrent->AdmitCommentEntries (1, now)) break;
				++offered;
				(void)store.Add (std::move (comment));
			}
		}
	}

	void PeerConnection::HandleUtMetadataExtension (const uint8_t * buf, size_t len)
	{
		if (!m_Torrent) return;
		int64_t msgType = -1, piece = -1;
		auto payloadLen = ParseDictionary (std::string_view ((const char *)buf, len),
			[&msgType, &piece] (std::string_view key, std::string_view buf) -> size_t
			{
				if (key == "msg_type")
				{
					auto [value, l] = ExtractInteger (buf);
					if (l) msgType = value;
					return l;
				}
				else if (key == "piece")
				{
					auto [value, l] = ExtractInteger (buf);
					if (l) piece = value;
					return l;
				}
				// ignore total_szie
				return 0;
			});
		if (msgType >= 0 && piece >= 0)
		{
			if (!payloadLen || payloadLen > len) return; // malformed metadata dictionary
			// The peer chooses the piece number. Bound it before multiplying so
			// that a hostile value cannot overflow size_t.
			if ((uint64_t)piece > std::numeric_limits<size_t>::max () / REQUEST_BLOCK_SIZE)
			{
				LogPrint (eLogWarning, "Torrents: ut_metadata piece number ", piece, " is out of range");
				return;
			}
			size_t pieceOffset = (size_t)piece * REQUEST_BLOCK_SIZE;
			switch (msgType)
			{
			case 0: // request
			{
				if (m_Torrent->IsPrivate () && m_Torrent->IsValid ()) break;
				auto & info = m_Torrent->GetInfo ();
				size_t offset = pieceOffset;
				if (offset < info.size ())
				{
					size_t pieceSize = std::min (info.size () - offset, REQUEST_BLOCK_SIZE);
					SendExtendedMsg (m_RemoteMsgIDUtMetadata,
						CreateDictionary ({ { "msg_type", CreateInteger (1) }, { "piece", CreateInteger (piece) },
							{ "total_size", CreateInteger (m_Torrent->GetInfo ().size ()) } }),
						std::string_view ((const char *)info.data () + offset, pieceSize));
				}
				else
					SendExtendedMsg (m_RemoteMsgIDUtMetadata,
						CreateDictionary ({ { "msg_type", CreateInteger (2) }, { "piece", CreateInteger (piece) } }));
				break;
			}
			case 1: // data
			{
				if (m_Torrent->GetLength ())
				{
					// we have info
					if (m_RemoteMetadata.size () < m_RemoteMetadataSize) // response to our request
						Terminate (); // reconnect
					// otherwise unsolicited data, ignore
					break;
				}
				size_t offset = pieceOffset;
				if (offset > m_RemoteMetadataSize) break;
				size_t size = m_RemoteMetadataSize - offset;
				if (size > REQUEST_BLOCK_SIZE) size = REQUEST_BLOCK_SIZE;
				if (payloadLen + size > len) break;
				if (offset == m_RemoteMetadata.size () && size)
				{
					m_RemoteMetadata.resize (m_RemoteMetadata.size () + size);
					memcpy (m_RemoteMetadata.data () + offset, buf + payloadLen, size);
					if (m_RemoteMetadata.size () < m_RemoteMetadataSize)
						// request next piece
						SendExtendedMsg (m_RemoteMsgIDUtMetadata,
							CreateDictionary (
								{ { "msg_type", CreateInteger (0) }, { "piece", CreateInteger (piece + 1) } }));
					else
					{
						// all info received
						LogPrint (eLogDebug, "Torrents: ut_metadata ", m_RemoteMetadataSize, " bytes of info received");
						uint8_t digest[SHA_DIGEST_LENGTH];
						SHA1 (m_RemoteMetadata.data (), m_RemoteMetadata.size (), digest);
						if (!memcmp (m_Torrent->GetInfoHash ().data (), digest, SHA_DIGEST_LENGTH))
							GetTorrentsTunnel ()->UpdateTorrentInfo (m_Torrent,
								std::string_view ((const char *)m_RemoteMetadata.data (), m_RemoteMetadata.size ()));
						else
							LogPrint (eLogError, "Torrents: ut_metadata info doesn't match infoHash");
						Terminate (); // we need to reconnect to receive bitfield
					}
				}
				break;
			}
			case 2: // reject
				LogPrint (eLogError, "Torrents: ut_metadata piece ", piece, " request rejected");
				break;
			default: LogPrint (eLogInfo, "Torrents: ut_metadata msg_type ", msgType, " is not supported");
			}
		}
	}

	void PeerConnection::RequestUtMetadata ()
	{
		SendExtendedMsg (m_RemoteMsgIDUtMetadata,
			CreateDictionary ({ { "msg_type", CreateInteger (0) }, { "piece", CreateInteger (0) } }));
	}

	void PeerConnection::HandleI2PPEXExtension (const uint8_t * buf, size_t len)
	{
		if (!m_Torrent || m_Torrent->IsPrivate ()) return;
		if (m_Torrent->IsComplete ()) return;
		const auto now = i2p::util::GetMonotonicSeconds ();
		if (!m_PexReceiveWindow || now >= m_PexReceiveWindow + PEER_PEX_RECEIVE_WINDOW)
		{
			m_PexReceiveWindow = now;
			m_PexReceiveMessages = 0;
			m_PexReceivePeers = 0;
		}
		if (m_PexReceiveMessages++ >= MAX_PEX_RECEIVE_MESSAGES) return;
		auto decoded = DecodePEXAdded (std::string_view ((const char *)buf, len));
		if (decoded.malformed)
		{
			LogPrint (eLogDebug, "Torrents: Malformed I2P_PEX message ignored");
			return;
		}
		if (decoded.peers.empty ()) return;
		// The decoder already bounds one message; the window bounds the total
		// number of new destinations a peer may contribute per minute.
		std::unordered_set<i2p::data::IdentHash> newPeers;
		for (const auto & peer : decoded.peers)
		{
			if (m_PexReceivePeers >= MAX_PEX_RECEIVE_PEERS) break;
			if (newPeers.insert (peer).second) ++m_PexReceivePeers;
		}
		if (newPeers.empty ()) return;
		LogPrint (eLogDebug, "Torrents: I2P_PEX ", newPeers.size (), " new peers received");
		GetTorrentsTunnel ()->ConnectToNewPeers (m_Torrent, newPeers, ePeerConnectionOriginPEX);
	}

	void PeerConnection::HandleI2PDHTExtension (const uint8_t * buf, size_t len)
	{
		if (!m_Torrent || m_Torrent->IsPrivate ()) return;
		auto decoded = DecodeDHTHint (std::string_view ((const char *)buf, len));
		if (decoded.malformed || !decoded.port) return;
		// Only an established connection with a transport-authenticated
		// remote destination may seed a DHT query: the hint itself is
		// unauthenticated, so it is never allowed to point a ping at some
		// other identity.
		if (!m_IsEstablished) return;
		auto ident = GetRemoteIdentHash ();
		if (!ident || ident->IsZero ()) return;
		if (decoded.rportMismatch)
		{
			// "rport" is informational (Java uses "port"); a stale or unrelated
			// value must not discard an otherwise valid hint.
			LogPrint (eLogDebug, "Torrents: i2p_dht rport does not follow port; using port ", *decoded.port);
		}
		// A connected peer may advertise its DHT endpoint once per throttle
		// window. The port may change after a legitimate restart, but
		// repeated messages must not fill the pending-query table.
		const auto now = i2p::util::GetMonotonicSeconds ();
		if (m_LastDHTHintTime && now < m_LastDHTHintTime + PEER_DHT_HINT_INTERVAL) return;
		m_LastDHTHintTime = now;
		GetTorrentsTunnel ()->SendDHTPingQuery (*ident, *decoded.port);
	}

	void PeerConnection::NotifyPEXPeers ()
	{
		if (!m_Torrent || m_Torrent->IsPrivate () || !m_RemoteMsgIDI2PPEX || !m_Stream) return;
		const auto now = i2p::util::GetMonotonicSeconds ();
		// One announcement per interval per connection. The handshake path
		// used to call this unconditionally: a repeated handshake cleared the
		// bounded "already announced" set and re-sent the message, turning a
		// single frame into unbounded wire amplification.
		if (m_LastPEXTime && now < m_LastPEXTime + PEER_PEX_SEND_INTERVAL) return;
		// The set of destinations already announced on this connection is
		// bounded. When it fills up it is cleared, which restarts the
		// rotation instead of growing for the lifetime of the connection.
		if (m_SentPEXPeers.size () >= MAX_PEX_TRACKED_PEERS) m_SentPEXPeers.clear ();
		std::string hashes;
		size_t count = 0;
		for (const auto & conn : m_Torrent->GetConnections ())
		{
			if (!conn || conn.get () == this) continue;
			if (!conn->IsEstablished ()) continue;
			// A peer that never sent a bitfield is not a known seed: an empty
			// bitfield is `.all()` as well, so `.all()` alone skipped every new
			// peer.
			if (conn->IsRemoteSeed ()) continue;

			auto ident = conn->GetRemoteIdentHash ();
			if (!ident) continue;
			if (!m_SentPEXPeers.insert (*ident).second) continue;
			hashes.append (reinterpret_cast<const char *> (ident->data ()), i2p::data::IdentHash::len);
			if (++count >= MAX_PEX_PEERS_PER_MESSAGE || m_SentPEXPeers.size () >= MAX_PEX_TRACKED_PEERS) break;
		}
		m_LastPEXTime = now;
		if (hashes.empty ()) return;
		SendExtendedMsg (
			m_RemoteMsgIDI2PPEX, CreateDictionary ({ { "added", CreateByteString (std::string_view (hashes)) } }));
	}

	void PeerConnection::SendDHTPortAdvertisement ()
	{
		if (!m_Torrent || m_Torrent->IsPrivate ()) return;
		// Peer advertisement is only meaningful to a peer that negotiated the
		// extension and only from a node that runs a DHT at all.
		if (!m_RemoteMsgIDI2PDHT) return;
		auto tunnel = GetTorrentsTunnel ();
		if (!tunnel || !tunnel->SupportsDHT ()) return;
		// Own per-connection interval: the handshake is not a reliable
		// "once".
		const auto now = i2p::util::GetMonotonicSeconds ();
		if (m_LastDHTAdvertisementTime && now < m_LastDHTAdvertisementTime + PEER_DHT_ADVERTISE_INTERVAL) return;
		m_LastDHTAdvertisementTime = now;
		auto [port, rport] = tunnel->GetDHTPorts ();
		SendExtendedMsg (m_RemoteMsgIDI2PDHT,
			CreateDictionary ({ { "port", CreateInteger (port) }, { "rport", CreateInteger (rport) } }));
	}

	std::optional<RequestedBlock> PeerConnection::GetNextBlockToRequest ()
	{
		auto block = m_Torrent->GetNextBlockToRequest (shared_from_this (), true); // skip already requested pieces
		if (std::get<2> (block) > 0) return block;
		// try to get block from requested by another connection piece
		auto block1 = m_Torrent->GetNextBlockToRequest (shared_from_this (), false);
		if (std::get<2> (block1) > 0) return block1;
		if (m_LastRequestedPieceIndex >= 0)
		{
			m_LastRequestedPieceIndex = -1; // no request
			// Keep interest when this peer still advertises wanted pieces;
			// the block may be temporarily in flight on another connection.
			if (m_IsInterested && !m_Torrent->HasIncompletePieces (m_RemoteBitfield))
			{
				m_IsInterested = false;
				SendNotinterestedMsg ();
			}
		}
		return { };
	}

	bool PeerConnection::RequestNextBlocks ()
	{
		if (m_IsChoked || !m_Torrent || m_Torrent->IsComplete ()) return false;
		if (m_NumRequests >= m_MaxNumRequests) return false;
		if (m_MaxNumRequests > MAX_NUM_REQUESTS)
		{
			LogPrint (eLogError, "Torrents: Max number or requests ", m_MaxNumRequests, " exceeds ", MAX_NUM_REQUESTS);
			m_MaxNumRequests = MAX_NUM_REQUESTS;
			if (m_NumRequests >= m_MaxNumRequests) return false;
		}
		uint8_t buf[REQUEST_MSG_LENGTH * MAX_NUM_REQUESTS];
		size_t bufOffset = 0;
		const auto now = i2p::util::GetMonotonicSeconds ();
		while (m_NumRequests < m_MaxNumRequests)
		{
			auto nextBlock = GetNextBlockToRequest ();
			if (!nextBlock) break;
			auto [index, offset, len] = *nextBlock;
			FillRequestMsg (buf + bufOffset, index, offset, len);
			bufOffset += REQUEST_MSG_LENGTH;
			m_LastRequestedPieceIndex = index;
			m_OutgoingRequests.push_back (OutgoingRequest {
				index, offset, len, m_Torrent->GetPiece (static_cast<int> (index)).GetBlockEpoch (), now });
			m_NumRequests++;
		}
		if (bufOffset > 0) WriteToStream (buf, bufOffset);
		return bufOffset > 0;
	}

	void PeerConnection::ReleaseOutgoingRequest (uint32_t index, uint32_t offset)
	{
		if (!m_Torrent) return;
		auto found = std::find_if (m_OutgoingRequests.begin (), m_OutgoingRequests.end (),
			[index, offset] (const OutgoingRequest & block) { return block.index == index && block.offset == offset; });
		if (found == m_OutgoingRequests.end ()) return;
		const auto epoch = found->epoch;
		m_OutgoingRequests.erase (found);
		if (index < m_Torrent->GetNumPieces ())
			m_Torrent->GetPiece (static_cast<int> (index)).ReleaseBlock (offset, epoch);
		if (m_NumRequests) --m_NumRequests;
	}

	void PeerConnection::ReleaseOutgoingRequests ()
	{
		if (m_Torrent)
			for (const auto & block : m_OutgoingRequests)
				if (block.index < m_Torrent->GetNumPieces ())
					m_Torrent->GetPiece (static_cast<int> (block.index)).ReleaseBlock (block.offset, block.epoch);
		m_OutgoingRequests.clear ();
		m_NumRequests = 0;
	}

	bool PeerConnection::ReleaseExpiredOutgoingRequests (uint64_t now)
	{
		if (!m_Torrent || m_OutgoingRequests.empty ()) return false;
		bool released = false;
		for (auto it = m_OutgoingRequests.begin (); it != m_OutgoingRequests.end ();)
		{
			if (!ShouldExpireOutgoingRequest (it->requestedAt, now, PEER_REQUEST_TIMEOUT))
			{
				++it;
				continue;
			}
			// The epoch stored with the request keeps a stale expiry from
			// resurrecting a block that a recheck or invalidation already reset:
			// ReleaseBlock is a no-op once the piece epoch moved on.
			if (it->index < m_Torrent->GetNumPieces ())
				m_Torrent->GetPiece (static_cast<int> (it->index)).ReleaseBlock (it->offset, it->epoch);
			it = m_OutgoingRequests.erase (it);
			if (m_NumRequests) --m_NumRequests;
			released = true;
		}
		// Leave the block's recovery to the normal scheduler: the released
		// block is Missing again and can be requested here or by another
		// peer. Only the bounded PEER_REQUEST_TIMEOUT can repeat this, so a
		// peer that never answers cannot create an unbounded request loop. A
		// stopped torrent stays silent until it is resumed.
		if (released && !m_Torrent->IsStopped ()) RequestNextBlocks ();
		return released;
	}

	std::optional<i2p::data::IdentHash> PeerConnection::GetRemoteIdentHash () const
	{
		if (m_Stream)
		{
			auto ident = m_Stream->GetRemoteIdentity ();
			if (ident) return ident->GetIdentHash ();
		}
		return { };
	}
}
}

#endif // NO_TORRENTS
