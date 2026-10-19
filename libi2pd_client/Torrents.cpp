/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Torrents.
 *
 * Implements BEP 3 metainfo parsing and creation, piece verification and
 * block scheduling, peer-wire messages with BEP 6/9/10/11 extensions, resume
 * state and tracker response handling. Filesystem access stays beneath the
 * torrent root through no-follow bounded helpers, while shared piece state is
 * mutex-protected across the network and disk workers.
 *
 */

#ifndef NO_TORRENTS

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <charconv>
#include <sstream>
#include <algorithm>
#include <cerrno>
#include <climits>
#include <functional>
#include <iterator>
#include <limits>
#include <set>
#include <openssl/rand.h>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/hex.hpp>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include "Log.h"
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
			return _wopen (fullPath.c_str (), flags, _S_IREAD | _S_IWRITE);
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
		if (RAND_bytes (random.data (), random.size ()) != 1) return fail ("cannot randomize temporary name");
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

	// Tracker-provided counters travel as signed 64-bit bencode integers. Clamp
	// them instead of truncating an out-of-range value into int.
	static int ClampTrackerCount (int64_t value)
	{
		return (int)std::clamp<int64_t> (value, 0, std::numeric_limits<int>::max ());
	}

	std::pair<std::string_view, size_t> ExtractByteString (std::string_view buf)
	{
		// A byte string starts with a decimal length without sign, leading zeroes
		// or embedded whitespace, followed by ':' and exactly that many bytes.
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
		size_t len = ParseList (buf,
			[&strings] (std::string_view str) -> size_t
			{
				auto [s, l] = ExtractByteString (str);
				if (l) strings.push_back (s);
				return l;
			});
		return { strings, len };
	}

	std::string CreateByteString (std::string_view str)
	{
		if (str.empty ()) return "";
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
		std::stringstream s;
		s << 'd';
		for (const auto & [name, value] : items)
			if (!name.empty () && !value.empty ())
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

	//------------------------------------

	bool TorrentFile::Save (size_t offset, const uint8_t * buf, size_t len)
	{
		// A peer-controlled offset/length may never index outside this file or
		// wrap the bounds check, even if the session layout is corrupted.
		if (offset > m_FileLength || len > m_FileLength - offset || (len && !buf))
		{
			LogPrint (eLogError, "Torrents: Refusing out-of-range write at ", offset, " length ", len, " to ",
				m_FullFilePath);
			return false;
		}
		Open (true);
		if (m_File < 0) return false;
		if (!len) return true;
		if (!WriteAt (m_File, offset, buf, len))
		{
			LogPrint (eLogError, "Torrents: Failed to write ", len, " bytes at ", offset, " to ", m_FullFilePath);
			return false;
		}
		return true;
	}

	bool TorrentFile::Load (size_t offset, uint8_t * buf, size_t len)
	{
		if (offset > m_FileLength || len > m_FileLength - offset || (len && !buf))
		{
			LogPrint (eLogError, "Torrents: Refusing out-of-range read at ", offset, " length ", len, " from ",
				m_FullFilePath);
			return false;
		}
		Open (false);
		if (m_File < 0) return false;
		if (!len) return true;
		// A short file or short read is a failure: never hand partially
		// initialised data to the piece hash check.
		if (!ReadAt (m_File, offset, buf, len))
		{
			LogPrint (eLogError, "Torrents: Failed to read ", len, " bytes at ", offset, " from ", m_FullFilePath);
			return false;
		}
		return true;
	}

	void TorrentFile::Complete ()
	{
		m_IsPart = false;
		Close ();
	}

	void TorrentFile::Open (bool create)
	{
		if (m_File >= 0) return;
		auto filePath = m_FullFilePath;
		if (m_IsPart) filePath += ".part";

		// Prefer opening relative to the trusted root so every component below
		// it is traversed with O_NOFOLLOW. If a root was supplied, never fall
		// back to a plain path open on failure: that would reintroduce the
		// symlink traversal the anchored open just rejected.
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
		}
		else
		{
			const auto access = m_IsPart ? (create ? FileAccess::eReadWriteCreate : FileAccess::eReadWrite)
										 : (create ? FileAccess::eReadWrite : FileAccess::eRead);
			m_File = OpenNoFollow (filePath, access);
		}
		if (m_File >= 0 && !IsRegularFileDescriptor (m_File))
		{
			CloseFile (m_File);
			m_File = -1;
			errno = EINVAL;
		}
		if (m_File < 0) LogPrint (eLogError, "Torrents: Can't open file ", filePath, " : ", std::strerror (errno));
		m_LastAccessTime = i2p::util::GetMonotonicSeconds ();
	}

	void TorrentFile::Close ()
	{
		CloseFile (m_File);
		m_File = -1;
	}

	bool TorrentFile::UpdateFullPath (const std::filesystem::path & rootDir)
	{
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
		, m_LastActivityTimestamp (0)
		, m_NumPeers (0)
	{
		memcpy (m_Hash, other.m_Hash, SHA_DIGEST_LENGTH);
		std::lock_guard<std::mutex> l (other.m_Mutex);
		m_Data = other.m_Data;
		other.m_Data = nullptr;
		m_Blocks = std::move (other.m_Blocks);
		m_IsSending = other.m_IsSending;
		m_IsLoading = other.m_IsLoading;
		m_IsRequested = other.m_IsRequested;
		m_IsVerified = other.m_IsVerified;
		m_LastActivityTimestamp = other.m_LastActivityTimestamp;
		m_NumPeers = other.m_NumPeers;
		other.m_IsSending = false;
		other.m_IsLoading = false;
		other.m_IsRequested = false;
		other.m_IsVerified = false;
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

	void Piece::Complete ()
	{
		std::lock_guard<std::mutex> l (m_Mutex);
		m_Blocks.reset ();
		m_IsRequested = false;
		// Completion alone (a resume bitfield or a pre-existing final file)
		// is not proof that the bytes match the hash. Only VerifyHash() sets
		// this flag.
		m_IsVerified = false;
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
		SHA1 (m_Data, m_Size, digest);
		m_IsVerified = !memcmp (m_Hash, digest, SHA_DIGEST_LENGTH);
		return m_IsVerified;
	}

	bool Piece::IsVerified () const
	{
		std::lock_guard<std::mutex> l (m_Mutex);
		return m_IsVerified;
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
		int numBlocks = d.quot;
		if (d.rem > 0) numBlocks++;
		return numBlocks;
	}

	void Piece::BlockReceived (const uint8_t * block, size_t len, size_t offset)
	{
		std::lock_guard<std::mutex> l (m_Mutex);
		// Check the subtraction form so a hostile offset cannot wrap the
		// addition and pass a bounds check on 32-bit size_t.
		if (!len || !block || !m_Blocks || offset > m_Size || len > m_Size - offset) return;
		// A requested block is exactly one aligned 16 KiB range (or the final
		// partial range). Rejecting shorter/misaligned peer payloads prevents
		// marking a whole block Available after one byte and later serving
		// uninitialised bytes from the rest of the block.
		if (offset % REQUEST_BLOCK_SIZE != 0) return;
		const size_t expectedLen = std::min (REQUEST_BLOCK_SIZE, m_Size - offset);
		if (len != expectedLen)
		{
			LogPrint (eLogWarning, "Torrents: Partial/misaligned block (", offset, ",", len, ")");
			return;
		}
		size_t blockIndex = offset / REQUEST_BLOCK_SIZE;
		if (blockIndex >= m_Blocks->size ()) return;
		if ((*m_Blocks)[blockIndex] == BlockStatus::Requested)
		{
			if (!m_Data) NewDataBufferLocked ();
			memcpy (m_Data + offset, block, len);
			m_IsVerified = false; // the bytes changed since any earlier hash check
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
		}
		else
		{
			if ((*m_Blocks)[blockIndex] == BlockStatus::Available)
				LogPrint (eLogWarning, "Torrents: Duplicated piece block ", blockIndex);
			else
				LogPrint (eLogWarning, "Torrents: Late or unsolicited piece block ", blockIndex);
		}
	}

	void Piece::Dump (PieceFileFragment && fragment)
	{
		std::lock_guard<std::mutex> l (m_Mutex);
		if (!m_IsVerified)
		{
			LogPrint (eLogError, "Torrents: Refusing to write an unverified piece");
			return;
		}
		m_IsSending = true;
		if (m_Data && fragment.fragmentOffset <= m_Size && fragment.fragmentSize <= m_Size - fragment.fragmentOffset)
		{
			if (fragment.file->Save (fragment.fileOffset, m_Data + fragment.fragmentOffset, fragment.fragmentSize))
				LogPrint (eLogDebug, "Torrents: Saved bytes ", fragment.fileOffset, " - ",
					fragment.fileOffset + fragment.fragmentSize - 1, " to ", fragment.file->GetFullFilePath ());
		}
		m_IsSending = false;
	}

	bool Piece::Load (PieceFileFragment && fragment)
	{
		std::lock_guard<std::mutex> l (m_Mutex);
		if (fragment.fragmentOffset > m_Size || fragment.fragmentSize > m_Size - fragment.fragmentOffset) return false;
		if (!m_Data) m_Data = new uint8_t[m_Size]();
		bool success
			= fragment.file->Load (fragment.fileOffset, m_Data + fragment.fragmentOffset, fragment.fragmentSize);
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
		return m_Data != nullptr;
	}

	bool Piece::CopyBlock (size_t offset, size_t len, std::vector<uint8_t> & out) const
	{
		std::lock_guard<std::mutex> l (m_Mutex);
		// Unverified data may come from a resume bitfield or a previous
		// partial load; never serve it to a peer. The caller falls back to
		// the disk load path, which hashes the whole piece first.
		if (!m_IsVerified) return false;
		if (!len || !m_Data || offset > m_Size || len > m_Size - offset) return false;
		if (!IsAvailableLocked ((int)(offset / REQUEST_BLOCK_SIZE))
			|| !IsAvailableLocked ((int)((offset + len - 1) / REQUEST_BLOCK_SIZE)))
			return false;
		out.assign (m_Data + offset, m_Data + offset + len);
		return true;
	}

	void Piece::NewDataBufferLocked ()
	{
		auto old = m_Data;
		m_Data = new uint8_t[m_Size]();
		delete[] old;
	}

	void Piece::DeleteDataBufferLocked ()
	{
		delete[] m_Data;
		m_Data = nullptr;
	}

	bool Piece::HasBlock (size_t offset) const
	{
		std::lock_guard<std::mutex> l (m_Mutex);
		if (offset >= m_Size) return false;
		return IsAvailableLocked (offset / REQUEST_BLOCK_SIZE);
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
		m_IsRequested = false;
		if (!m_Blocks) return;
		for (auto & it : *m_Blocks)
			if (it == BlockStatus::Requested) it = BlockStatus::Missing;
	}

	void Piece::MarkCorruptLocked ()
	{
		m_Blocks = std::make_unique<std::vector<BlockStatus>> (GetNumBlocks (m_Size), BlockStatus::Missing);
		DeleteDataBufferLocked ();
		m_IsRequested = false;
		m_IsVerified = false;
	}

	void Piece::MarkCorrupt ()
	{
		std::lock_guard<std::mutex> l (m_Mutex);
		MarkCorruptLocked ();
	}

	void Piece::ReleaseDataLocked ()
	{
		if (!m_Data) return;
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
				return 0;
			});
		// Reject trailing data after the top-level dictionary. Accepting it makes
		// the parsed torrent and its info hash ambiguous to other implementations.
		if (!len || len != buf.size ()) m_Error = eTorrentErrorMalformedMetaInfo;
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
		// The number of hashes must exactly match the number of pieces implied by
		// the total length and the piece length. Reject missing/extra hashes.
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
					if (l && !version) // no pieces yet, might be v2
						version = value;
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
		// Every supported (v1/hybrid) metainfo requires a non-empty root name.
		// Without it the download path would be the torrents directory itself
		// and the torrent would have no usable file entry.
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
		if (m_Error) m_IsStopped = true;
		return len;
	}

	std::string Torrent::CreateTorrentFileContent () const
	{
		if (m_Info.empty ()) return "";
		std::vector<std::string> tiers;
		for (const auto & tier : m_TrackerTiers)
		{
			std::vector<std::string> urls;
			for (const auto & url : tier) urls.push_back (CreateByteString (url));
			tiers.push_back (CreateList (urls));
		}
		if (tiers.empty () && !m_HasExplicitTrackers)
			return CreateDictionary ({ { "announce", CreateByteString (m_Announce) },
				{ "info", std::string_view ((const char *)m_Info.data (), m_Info.size ()) } });
		return CreateDictionary (
			{ { "announce", CreateByteString (m_Announce) }, { "announce-list", CreateList (tiers) },
				{ "info", std::string_view ((const char *)m_Info.data (), m_Info.size ()) } });
	}

	void Torrent::SetCommentsEnabled (bool enabled)
	{
		m_Comments.SetEnabled (enabled);
		for (auto & connection : GetConnections ()) connection->RefreshComments ();
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
		if (name.empty () || AdjustName (name) != name || files.empty () || files.size () > MAX_NUM_TORRENT_FILES
			|| pieceLength < MIN_PIECE_LENGTH || pieceLength > MAX_PIECE_LENGTH || pieceHashes.empty ()
			|| pieceHashes.size () > MAX_NUM_TORRENT_PIECES)
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
		// Keep owned encoded strings alive while CreateDictionary consumes views.
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
		if (!result->SetTrackerTiers (trackers)) return nullptr;
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
		// Absolute paths and Windows drive/UNC prefixes must never be accepted as
		// a component. Separators inside a component are replaced, not interpreted.
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
							if (length <= 0 || (uint64_t)length > MAX_TORRENT_LENGTH
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
				if (!len || !hasPath || !hasLength || fileLength == 0 || filePath.empty ())
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
		std::string infoHash;
		for (auto it : m_InfoHash)
		{
			char str[4];
			snprintf (str, 4, "%%%02x", it);
			infoHash.append (str);
		}
		return infoHash;
	}

	size_t Torrent::GetLeft () const
	{
		if (IsComplete ()) return 0;
		size_t completed = 0;
		for (const auto & it : m_Pieces)
			if (it.IsComplete ()) completed += it.GetSize ();
		return m_Length > completed ? m_Length - completed : 0;
	}

	void Torrent::ParseTrackerResponse (size_t trackerID, std::string_view buf)
	{
		CheckTrackerStatsSize (trackerID);
		std::get<6> (m_TrackerStats[trackerID]) = ""; // clear error
		ParseDictionary (buf,
			[this, trackerID] (std::string_view key, std::string_view buf) -> size_t
			{
				if (key == "interval")
				{
					auto [value, l] = ExtractInteger (buf);
					if (l)
					{
						auto interval = std::clamp<int64_t> (value, MIN_TRACKER_REQUESTS_INTERVAL / 1000,
											MAX_TRACKER_REQUESTS_INTERVAL / 1000)
							* 1000; // in milliseconds
						std::get<1> (m_TrackerStats[trackerID]) = (int)interval;
						std::get<2> (m_TrackerStats[trackerID])
							= i2p::util::GetMonotonicMilliseconds () + interval; // reset next request
					}
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
					LogPrint (eLogError, "Torrents: Tracker error: ", reason);
					std::get<6> (m_TrackerStats[trackerID]) = reason;
					// double interval if tracker failure
					int interval = std::clamp (std::get<1> (m_TrackerStats[trackerID]) * 2,
						MIN_TRACKER_REQUESTS_INTERVAL, MAX_TRACKER_REQUESTS_INTERVAL);
					std::get<1> (m_TrackerStats[trackerID]) = interval;
					std::get<2> (m_TrackerStats[trackerID]) = i2p::util::GetMonotonicMilliseconds () + interval;
					return l;
				}
				return 0;
			});
		std::get<5> (m_TrackerStats[trackerID]) = i2p::util::GetSecondsSinceEpoch ();
	}

	size_t Torrent::ParsePeers (size_t trackerID, std::string_view buf)
	{
		auto & peers = std::get<0> (m_TrackerStats[trackerID]);
		peers.clear ();
		auto [hashes, len] = ExtractByteString (buf);
		while (hashes.length () >= i2p::data::IdentHash::len)
		{
			peers.emplace (
				i2p::data::IdentHash ((const uint8_t *)hashes.substr (0, i2p::data::IdentHash::len).data ()));
			hashes = hashes.substr (i2p::data::IdentHash::len);
		}
		return len;
	}

	void Torrent::HandleDatagramTrackerResponse (
		size_t trackerID, uint32_t interval, const uint8_t * hashes, size_t hashesLen, int numSeeders, int numLeechers)
	{
		CheckTrackerStatsSize (trackerID);
		auto & [peers, trackerRequestInterval, nextRequestTime, seeders, leechers, lastUpdateTime, error]
			= m_TrackerStats[trackerID];
		error = "";
		trackerRequestInterval
			= std::clamp ((int)interval, MIN_TRACKER_REQUESTS_INTERVAL / 1000, MAX_TRACKER_REQUESTS_INTERVAL / 1000)
			* 1000; // milliseconds
		nextRequestTime = i2p::util::GetMonotonicMilliseconds () + trackerRequestInterval;
		seeders = numSeeders;
		leechers = numLeechers;
		lastUpdateTime = i2p::util::GetSecondsSinceEpoch ();
		peers.clear ();
		size_t offset = 0;
		while (offset + i2p::data::IdentHash::len <= hashesLen)
		{
			i2p::data::IdentHash ident (hashes + offset);
			if (ident.IsZero ()) break;
			peers.emplace (std::move (ident));
			offset += i2p::data::IdentHash::len;
		}
	}

	void Torrent::SetTrackerError (size_t trackerID, std::string_view error)
	{
		CheckTrackerStatsSize (trackerID);
		std::get<6> (m_TrackerStats[trackerID]) = error;
	}

	void Torrent::CheckTrackerStatsSize (size_t trackerID)
	{
		if (trackerID >= m_TrackerStats.size ())
			m_TrackerStats.resize (trackerID + 1,
				TrackerStats { { }, MIN_TRACKER_REQUESTS_INTERVAL, 0, 0, 0, i2p::util::GetSecondsSinceEpoch (), "" });
	}

	void Torrent::SetInterval (size_t trackerID, int interval)
	{
		CheckTrackerStatsSize (trackerID);
		std::get<1> (m_TrackerStats[trackerID]) = interval;
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
				if (m_Pieces[idx].IsComplete ())
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

	bool Torrent::ApplyBitfield (const std::vector<uint8_t> & bitfield)
	{
		bool complete = true;
		size_t numPieces = m_Pieces.size ();
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
			if (!it.IsComplete () || !it.IsVerified ()) return false;
		return !m_Pieces.empty ();
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
		for (auto & connection : GetConnections ()) connection->RefreshFileSelection ();
		return true;
	}

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
			// try another piece if not current piece or no more blocks in current piece
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
				uint32_t ind = std::get<0> (it);
				auto [offset, len] = m_Pieces[ind].GetNextBlockToRequest ();
				if (len > 0) return { ind, offset, len };
			}
		}
		return { 0, 0, 0 };
	}

	bool Torrent::UpdateStatus (uint64_t ts)
	{
		GetConnections (); // cleanup expired connections
		if (!m_Length) return false; // non ready magnet
		bool complete = true;
		for (auto & it : m_Pieces)
		{
			bool pieceComplete = it.IsComplete ();
			if (!pieceComplete) complete = false;
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
		if (m_FullPath.empty ()) return;
		auto [bitfield, have] = CreateBitfield ();
		if (!have) return; // empty
		std::filesystem::path resumeFilePath = m_FullPath;
		resumeFilePath += ".resume";
		if (have && m_IsComplete.load (std::memory_order_acquire)) // all pieces complete and the torrent was promoted
		{
			// delete resume file
			std::error_code ec;
			std::filesystem::remove (resumeFilePath, ec);
			if (ec) LogPrint (eLogError, "Torrents: Can't delete resume file ", resumeFilePath, " : ", ec.message ());
		}
		else
		{
			// The resume file is derived data but is still opened without
			// following a symlink leaf, so a swapped path cannot truncate an
			// unrelated file.
			if (!WriteFileNoFollow (resumeFilePath, bitfield.data (), bitfield.size ()))
				LogPrint (eLogError, "Torrents: Can't write resume file ", resumeFilePath);
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
				file->SetIsPart (!IsComplete ());
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
			auto filesIT = m_Files.begin ();
			size_t currentSize = 0, currentCompletedSize = 0;
			for (const auto & piece : m_Pieces)
			{
				if (currentSize + m_PieceLength < (*filesIT)->GetFileLength ())
				{
					currentSize += m_PieceLength;
					if (piece.IsComplete ()) currentCompletedSize += m_PieceLength;
				}
				else
				{
					size_t leftoverSize = (*filesIT)->GetFileLength () - currentSize;
					if (piece.IsComplete ()) currentCompletedSize += leftoverSize;
					completed.push_back (currentCompletedSize);
					currentSize = m_PieceLength - leftoverSize;
					filesIT++;
					while (filesIT != m_Files.end () && (*filesIT)->GetFileLength () <= currentSize)
					{
						completed.push_back (piece.IsComplete () ? (*filesIT)->GetFileLength () : 0);
						currentSize -= (*filesIT)->GetFileLength ();
						filesIT++;
					}
					currentCompletedSize = piece.IsComplete () ? currentSize : 0;
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
				TrackerStats { { }, MIN_TRACKER_REQUESTS_INTERVAL, 0, 0, 0, i2p::util::GetSecondsSinceEpoch (), "" });
		std::get<2> (m_TrackerStats[trackerID]) = ts;
	}

	TorrentStatus Torrent::GetStatus () const
	{
		if (m_IsStopped) return eTorrentStatusStopped;
		if (m_IsComplete.load (std::memory_order_acquire)) return eTorrentStatusSeeding;
		return eTorrentStatusDownloading;
	}

	bool Torrent::AddConnection (std::shared_ptr<PeerConnection> conn)
	{
		if (!conn) return false;
		auto remoteIdentHash = conn->GetRemoteIdentHash ();
		if (!remoteIdentHash) return false;
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
			if (m_LastRequestedPieceIndex >= 0) // pending requests by us
			{
				auto & piece = m_Torrent->GetPiece (m_LastRequestedPieceIndex);
				if (piece.IsRequested ()) piece.ClearAllRequests (); // piece can be requested by other connections
			}
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
			[s = shared_from_this ()] (const boost::system::error_code & ecode, size_t bytes_transferred)
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
		if (m_IsEstablished)
		{
			if (ts > m_LastReceiveTime + PEER_KEEP_ALIVE_TIMEOUT)
			{
				LogPrint (eLogInfo, "Torrent: Peer timeout expired");
				Close (); // Terminate shouldn't be called from IterateHandler directly
			}
			else if (ts > m_LastSendTime + PEER_KEEP_SEND_INTERVAL)
			{
				m_NumRequests = 0; // if we need to send keep-alive, all pending requests are invalid now
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
		// Reject the frame before storing m_NextMsgLength. Checking the payload
		// alone accepted 65535, whose 4-byte prefix does not fit in the receive
		// buffer and left the connection waiting for bytes that could never
		// arrive.
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
			case eMessageTypeNotInterested: m_IsRemoteInterested = false; break;
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
		memset (buf + 48, '0', 20);
		if (GetTorrentsTunnel ())
		{
			const auto & peerID = GetTorrentsTunnel ()->GetPeerID ();
			size_t len = peerID.length ();
			if (len > 20) len = 20;
			memcpy (buf + 48, peerID.data (), len);
		}
		WriteToStream (buf, HANDSHAKE_MSG_LENGTH);
		m_IsHandshakeSent = true;
	}

	void PeerConnection::HandleHaveMsg (const uint8_t * buf, size_t len)
	{
		if (len < 4) return;
		if (m_RemoteBitfield.empty ()) // bitfield was not received before because was empty
			m_RemoteBitfield.resize (m_Torrent->GetNumPieces ());
		uint32_t index = bufbe32toh (buf);
		if (index < m_RemoteBitfield.size ())
		{
			m_RemoteBitfield.set (index);
			if (m_Torrent && !m_Torrent->IsComplete ())
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
					if (!m_IsInterested && !m_Torrent->GetPiece (idx).IsComplete ()) m_IsInterested = true;
				}
				bit >>= 1;
				idx++;
			}
		}
		if (m_IsInterested)
			SendInterestedMsg ();
		else if (m_RemoteBitfield.all ()) // remote is seeding
			Terminate (); // we don't need this connection
	}

	void PeerConnection::SendBitfieldMsg (const uint8_t * bitfield, size_t bitfieldLen)
	{
		std::vector<uint8_t> sendBuffer (bitfieldLen + 5);
		htobe32buf (sendBuffer.data (), bitfieldLen + 1); // length
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
		if (len && index < m_Torrent->GetNumPieces ())
		{
			Piece & piece = m_Torrent->GetPiece (index);
			auto pieceSize = piece.GetSize ();
			if (offset > pieceSize || len > pieceSize - offset)
			{
				LogPrint (
					eLogWarning, "Torrents: Piece block (", index, ",", offset, ",", len, ") is outside the piece");
			}
			else
			{
				piece.BlockReceived (buf + 8, len, offset);
				if (piece.IsComplete ())
				{
					if (piece.VerifyHash ())
					{
						boost::asio::post (GetTorrentsTunnel ()->GetDiskIOService (),
							[index, torrent = m_Torrent] () mutable
							{
								auto fragments = torrent->GetPieceFileFragments (index);
								Piece & piece = torrent->GetPiece (index);
								for (auto & it : fragments) piece.Dump (std::move (it));
								torrent->SaveTorrentResumeFile ();
							});
						// send have
						auto conns = m_Torrent->GetConnections ();
						for (auto it : conns) it->SendHaveMsg (index);
					}
					else
					{
						LogPrint (eLogWarning, "Torrents: Received piece hash mismatch");
						piece.InvalidateAllBlocks ();
					}
				}
			}
		}
		if (m_NumRequests > 0) m_NumRequests--;
		if (m_NumRequests <= m_MaxNumRequests * 2 / 3) RequestNextBlocks ();
		// update stats
		m_Downloaded += REQUEST_BLOCK_SIZE;
		m_Torrent->AddDownloaded (REQUEST_BLOCK_SIZE);
		auto ts = i2p::util::GetMonotonicMilliseconds ();
		if (m_LastBlockDownloadTimestamp)
		{
			m_ReceivedSinceLastTimestamp += REQUEST_BLOCK_SIZE;
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
		std::vector<uint8_t> sendBuffer (len + 8 + 5);
		htobe32buf (sendBuffer.data (), len + 8 + 1); // length
		sendBuffer[4] = eMessageTypePiece; // msg ID
		htobe32buf (sendBuffer.data () + 5, index);
		htobe32buf (sendBuffer.data () + 9, offset);
		memcpy (sendBuffer.data () + 13, data, len);
		LogPrint (eLogDebug, "Torrents: Sending piece index ", index, " offset ", offset, " length ", len);
		m_NumPieces++;
		m_Stream->AsyncSend (sendBuffer.data (), sendBuffer.size (),
			[s = shared_from_this ()] (const boost::system::error_code & ecode, size_t bytes_transferred)
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
						s->m_SentSinceLastTimestamp += REQUEST_BLOCK_SIZE;
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
		m_LastSendTime = i2p::util::GetMonotonicSeconds ();
		m_Uploaded += REQUEST_BLOCK_SIZE;
		m_Torrent->AddUploaded (len);
	}

	void PeerConnection::HandleRejectRequestMsg (const uint8_t * buf, size_t len)
	{
		if (len < 8 || !m_Torrent) return;
		uint32_t index = bufbe32toh (buf);
		uint32_t offset = bufbe32toh (buf + 4);
		LogPrint (eLogDebug, "Torrents: Reject request msg received index ", index, " offset ", offset);
		if (m_NumRequests > 0) m_NumRequests--;
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
								piece.MarkCorrupt ();
							}
							if (loaded)
								boost::asio::post (s->GetTorrentsTunnel ()->GetService (),
									[requestedBlock = std::move (requestBlock), s] ()
									{
										if (!s->SendRequestedBlock (requestedBlock))
										{
											LogPrint (eLogError, "Torrents: Couldn't send block from loaded piece");
											std::apply (std::bind_front (&PeerConnection::SendRejectRequestMsg, s),
												requestedBlock);
										}
									});
							else
							{
								LogPrint (eLogError, "Torrents: Failed to load piece ", index);
								if (s->m_IsFast)
									boost::asio::post (s->GetTorrentsTunnel ()->GetService (),
										[requestedBlock = std::move (requestBlock), s] ()
										{
											std::apply (std::bind_front (&PeerConnection::SendRejectRequestMsg, s),
												requestedBlock);
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
		m_NumRequests = 0;
		if (m_Torrent && m_LastRequestedPieceIndex >= 0)
			m_Torrent->GetPiece (m_LastRequestedPieceIndex).ClearAllRequests ();
		m_LastRequestedPieceIndex = -1;
	}

	void PeerConnection::HandleSuggestPieceMsg (const uint8_t * buf, size_t len)
	{
		if (len < 4) return;
		uint32_t index = bufbe32toh (buf);
		LogPrint (eLogDebug, "Torrents: suggest piece msg received ", index);
		if (IsPieceAvailable (index)) m_SuggestedPieceIndex = index;
	}

	void PeerConnection::HandleAllowedFastMsg (const uint8_t * buf, size_t len)
	{
		LogPrint (eLogDebug, "Torrents: allowed fast msg received");
		// ignore for now
	}

	void PeerConnection::HandleExtendedMsg (const uint8_t * buf, size_t len)
	{
		if (len < 1) return;
		if (!buf[0]) // Handshake
		{
			ParseDictionary (std::string_view ((const char *)(buf + 1), len - 1),
				[this] (std::string_view key, std::string_view buf) -> size_t
				{
					if (key == "m")
					{
						return ParseDictionary (buf,
							[this] (std::string_view msg, std::string_view msgID) -> size_t
							{
								auto [id, l] = ExtractInteger (msgID);
								if (l) AddExtendedMsgHandler (msg, id);
								return l;
							});
					}
					else if (key == "metadata_size")
					{
						auto [s, l] = ExtractInteger (buf);
						if (l)
						{
							if (s < 0 || (size_t)s > MAX_NUM_TORRENT_PIECES * SHA_DIGEST_LENGTH)
							{
								LogPrint (eLogError, "Torrents: Invalid metadata_size ", s);
								s = 0;
							}
							m_RemoteMetadataSize = s;
						}
						return l;
					}
					else if (key == "reqq")
					{
						auto [q, l] = ExtractInteger (buf);
						if (l) m_MaxNumRequests = std::clamp ((size_t)q, MIN_NUM_REQUESTS, MAX_NUM_REQUESTS);
						return l;
					}
					else if (key == "v")
					{
						auto [v, l] = ExtractByteString (buf);
						if (l) m_RemoteName = v;
						return l;
					}
					return 0;
				});
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
			// BEP11
			if (m_RemoteMsgIDI2PPEX && m_Stream && m_Stream->IsIncoming ()) NotifyPEXPeers ();
			// BEP5
			if (m_RemoteMsgIDI2PDHT && GetTorrentsTunnel ()->SupportsDHT ()) SendDHTPortAdvertisement ();
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
		if (msgID < 0 || msgID > 255) return;
		if (extensionName == COMMENT_EXTENSION_NAME)
		{
			m_ExtendedMessageHandlers.emplace (4, &PeerConnection::HandleCommentExtension);
			m_RemoteMsgIDComment = static_cast<uint8_t> (msgID);
			return;
		}
		if (extensionName == EXTENSION_NAME_UT_METADATA)
		{
			m_ExtendedMessageHandlers.emplace (EXTENSION_MSGID_UT_METADATA, &PeerConnection::HandleUtMetadataExtension);
			m_RemoteMsgIDUtMetadata = msgID;
		}
		else if (extensionName == EXTENSION_NAME_I2P_PEX)
		{
			m_ExtendedMessageHandlers.emplace (EXTENSION_MSGID_I2P_PEX, &PeerConnection::HandleI2PPEXExtension);
			m_RemoteMsgIDI2PPEX = msgID;
		}
		else if (extensionName == EXTENSION_NAME_I2P_DHT)
		{
			m_ExtendedMessageHandlers.emplace (EXTENSION_MSGID_I2P_DHT, &PeerConnection::HandleI2PDHTExtension);
			m_RemoteMsgIDI2PDHT = msgID;
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
			auto comment = CreateInteger (comments ? 4 : 0);
			std::vector<std::pair<std::string_view, std::string_view>> extensions;
			if (!m_Torrent->IsPrivate ())
			{
				if (GetTorrentsTunnel ()->SupportsDHT ()) extensions.emplace_back (EXTENSION_NAME_I2P_DHT, dht);
				extensions.emplace_back (EXTENSION_NAME_I2P_PEX, pex);
			}
			extensions.emplace_back (COMMENT_EXTENSION_NAME, comment);
			extensions.emplace_back (EXTENSION_NAME_UT_METADATA, metadata);
			str = CreateDictionary ({ { "m", CreateDictionary (extensions) },
				{ "metadata_size", CreateInteger (m_Torrent->GetInfo ().size ()) },
				{ "reqq", CreateInteger (MAX_INCOMING_REQUESTS_QUEUE_SIZE) }, { "v", CreateByteString ("i2pd") } });
			payload = str;
		}
		std::vector<uint8_t> sendBuffer (payload.length () + data.length () + 1 + 5);
		htobe32buf (sendBuffer.data (), payload.length () + data.length () + 1 + 1); // length
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
		auto now = i2p::util::GetMonotonicSeconds ();
		if (m_LastCommentRequest && now - m_LastCommentRequest < 600) return;
		m_LastCommentRequest = now;
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
			for (auto & comment : message->comments) m_Torrent->GetCommentStore ().Add (std::move (comment));
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
		std::unordered_set<i2p::data::IdentHash> newPeers;
		auto parsed = ParseDictionary (std::string_view ((const char *)buf, len),
			[&newPeers] (std::string_view key, std::string_view buf) -> size_t
			{
				if (key == "added")
				{
					auto [idents, l] = ExtractByteString (buf);
					if (l && !(idents.size () & 0x1F)) // multiple of 32
						while (idents.length () >= i2p::data::IdentHash::len)
						{
							newPeers.emplace (i2p::data::IdentHash (
								(const uint8_t *)idents.substr (0, i2p::data::IdentHash::len).data ()));
							idents = idents.substr (i2p::data::IdentHash::len);
						};
					return l;
				}
				return 0;
			});
		if (parsed != len) return;
		if (!newPeers.empty ())
		{
			LogPrint (eLogDebug, "Torrents: I2P_PEX ", newPeers.size (), " new peers received");
			GetTorrentsTunnel ()->ConnectToNewPeers (m_Torrent, newPeers, ePeerConnectionOriginPEX);
		}
	}

	void PeerConnection::HandleI2PDHTExtension (const uint8_t * buf, size_t len)
	{
		if (!m_Torrent || m_Torrent->IsPrivate ()) return;
		uint16_t port = 0; // rport is always port + 1
		auto parsed = ParseDictionary (std::string_view ((const char *)buf, len),
			[&port] (std::string_view key, std::string_view buf) -> size_t
			{
				if (key == "port")
				{
					auto [value, l] = ExtractInteger (buf);
					if (l && value > 0 && value < 65535) port = value;
					return l;
				}
				return 0;
			});
		if (parsed != len) return;
		if (port)
		{
			auto ident = GetRemoteIdentHash ();
			if (ident) GetTorrentsTunnel ()->SendDHTPingQuery (*ident, port);
		}
	}

	void PeerConnection::NotifyPEXPeers ()
	{
		if (!m_Torrent || m_Torrent->IsPrivate ()) return;
		auto conns = m_Torrent->GetConnections ();
		if (conns.size () > 1) // including us
		{
			std::string addedPayload;
			auto remoteIdent = GetRemoteIdentHash ();
			if (remoteIdent)
				addedPayload = CreateDictionary ({ { "added",
					CreateByteString (
						std::string_view ((const char *)remoteIdent->data (), i2p::data::IdentHash::len)) } });
			std::vector<uint8_t> hashes;
			for (auto it : conns)
			{
				auto ident = it->GetRemoteIdentHash ();
				if (ident && ident != remoteIdent)
				{
					if (!addedPayload.empty () && it->m_RemoteMsgIDI2PPEX) // connection suuprts PEX
						it->SendExtendedMsg (it->m_RemoteMsgIDI2PPEX, addedPayload);
					hashes.insert (hashes.end (), ident->data (), ident->data () + i2p::data::IdentHash::len);
				}
			}
			if (!hashes.empty ())
				SendExtendedMsg (m_RemoteMsgIDI2PPEX,
					CreateDictionary ({ { "added",
						CreateByteString (std::string_view ((const char *)hashes.data (), hashes.size ())) } }));
		}
	}

	void PeerConnection::SendDHTPortAdvertisement ()
	{
		if (!m_Torrent || m_Torrent->IsPrivate ()) return;
		auto [port, rport] = GetTorrentsTunnel ()->GetDHTPorts ();
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
			if (m_IsInterested)
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
		while (m_NumRequests < m_MaxNumRequests)
		{
			auto nextBlock = GetNextBlockToRequest ();
			if (!nextBlock) break;
			auto [index, offset, len] = *nextBlock;
			FillRequestMsg (buf + bufOffset, index, offset, len);
			bufOffset += REQUEST_MSG_LENGTH;
			m_LastRequestedPieceIndex = index;
			m_NumRequests++;
		}
		if (bufOffset > 0) WriteToStream (buf, bufOffset);
		return bufOffset > 0;
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
