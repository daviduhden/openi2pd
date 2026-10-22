/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TorrentsCreator.
 *
 * Hashes file trees into BEP 3 piece hashes with OpenSSL EVP on the storage
 * worker, traversing only beneath the authorized root and rejecting reparse
 * points and symlinked descendants. It produces metadata only and never
 * marks the resulting torrent complete or verified for seeding.
 *
 */

#ifndef NO_TORRENTS
#include "TorrentsCreator.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <limits>
#include <iterator>
#include <memory>
#include <openssl/evp.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace i2p::torrents
{
namespace
{
	struct File
	{
		std::filesystem::path path;
		uint64_t size;
#ifndef _WIN32
		dev_t device;
		ino_t inode;
		time_t modified, changed;
#endif
	};
	struct Descriptor
	{
		int value = -1;
		explicit Descriptor (int fd)
			: value (fd)
		{
		}
		~Descriptor ()
		{
			if (value >= 0)
#ifdef _WIN32
				_close (value);
#else
				::close (value);
#endif
		}
		Descriptor (const Descriptor &) = delete;
		Descriptor & operator= (const Descriptor &) = delete;
	};
#ifndef _WIN32
	int OpenRelative (int root, const std::filesystem::path & path, bool directory = false)
	{
		if (!IsSafeRelativePath (path)) return -1;
		Descriptor current (::dup (root));
		if (current.value < 0) return -1;
		for (auto it = path.begin (); it != path.end (); ++it)
		{
			const bool last = std::next (it) == path.end ();
			auto fd = ::openat (current.value, it->c_str (),
				O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK | ((!last || directory) ? O_DIRECTORY : 0));
			if (fd < 0) return -1;
			::close (current.value);
			current.value = fd;
		}
		auto result = current.value;
		current.value = -1;
		return result;
	}
	bool Collect (int root, const std::filesystem::path & path, size_t depth, std::vector<File> & files,
		size_t & entries, uint64_t & total, const std::function<bool ()> & cancelled)
	{
		if ((cancelled && cancelled ()) || depth > 128 || ++entries > MAX_NUM_TORRENT_FILES * 2) return false;
		Descriptor fd (OpenRelative (root, path));
		struct stat status;
		if (fd.value < 0 || ::fstat (fd.value, &status)) return false;
		if (S_ISREG (status.st_mode))
		{
			if (status.st_size < 0 || static_cast<uint64_t> (status.st_size) > MAX_TORRENT_LENGTH - total
				|| files.size () >= MAX_NUM_TORRENT_FILES)
				return false;
			total += status.st_size;
			files.push_back ({ path, static_cast<uint64_t> (status.st_size), status.st_dev, status.st_ino,
				status.st_mtime, status.st_ctime });
			return true;
		}
		if (!S_ISDIR (status.st_mode)) return false;
		// fdopendir takes ownership. Child opens remain rooted at the trusted
		// anchor, so a renamed/symlinked intermediate path cannot redirect IO.
		struct DirectoryCloser
		{
			void operator() (DIR * directory) const
			{
				if (directory) ::closedir (directory);
			}
		};
		std::unique_ptr<DIR, DirectoryCloser> directory (::fdopendir (fd.value));
		if (!directory) return false;
		fd.value = -1;
		errno = 0;
		while (auto entry = ::readdir (directory.get ()))
		{
			std::string name (entry->d_name);
			if (name == "." || name == "..") continue;
			if (!Collect (root, path / name, depth + 1, files, entries, total, cancelled)) return false;
			errno = 0;
		}
		return errno == 0;
	}
#else
	bool NoReparse (const std::filesystem::path & root, const std::filesystem::path & path)
	{
		auto current = root;
		auto attributes = GetFileAttributesW (current.c_str ());
		if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
		for (const auto & component : path)
		{
			current /= component;
			attributes = GetFileAttributesW (current.c_str ());
			if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
		}
		return true;
	}
	bool Collect (const std::filesystem::path & root, const std::filesystem::path & path, size_t depth,
		std::vector<File> & files, size_t & entries, uint64_t & total, const std::function<bool ()> & cancelled)
	{
		if ((cancelled && cancelled ()) || depth > 128 || ++entries > MAX_NUM_TORRENT_FILES * 2
			|| !NoReparse (root, path))
			return false;
		std::error_code ec;
		auto full = root / path;
		auto status = std::filesystem::symlink_status (full, ec);
		if (ec) return false;
		if (std::filesystem::is_regular_file (status))
		{
			auto size = std::filesystem::file_size (full, ec);
			if (ec || size > MAX_TORRENT_LENGTH - total || files.size () >= MAX_NUM_TORRENT_FILES) return false;
			total += size;
			files.push_back ({ path, size });
			return true;
		}
		if (!std::filesystem::is_directory (status)) return false;
		std::filesystem::directory_iterator iterator (full, ec), end;
		if (ec) return false;
		while (iterator != end)
		{
			if (!Collect (root, path / iterator->path ().filename (), depth + 1, files, entries, total, cancelled))
				return false;
			iterator.increment (ec);
			if (ec) return false;
		}
		return true;
	}
#endif
}

size_t SelectTorrentPieceLength (uint64_t totalBytes, size_t requested)
{
	if (!totalBytes || totalBytes > MAX_TORRENT_LENGTH) return 0;
	if (requested && (requested < MIN_PIECE_LENGTH || requested > MAX_PIECE_LENGTH || (requested & (requested - 1))))
		return 0;
	size_t selected = requested ? requested : 256 * 1024;
	if (!requested)
		while (selected < MAX_PIECE_LENGTH && totalBytes / selected + (totalBytes % selected != 0) > 2048)
			selected *= 2;
	const uint64_t count = totalBytes / selected + (totalBytes % selected != 0);
	const size_t blocks = (selected + REQUEST_BLOCK_SIZE - 1) / REQUEST_BLOCK_SIZE;
	if (count > MAX_NUM_TORRENT_PIECES || count > MAX_TORRENT_BLOCKS / blocks) return 0;
	return selected;
}

std::shared_ptr<Torrent> CreateTorrentFromPath (const std::filesystem::path & root,
	const std::filesystem::path & relativePath, const TorrentCreateOptions & options, std::string & error)
try
{
	error.clear ();
	auto fail = [&error] (std::string_view why) -> std::shared_ptr<Torrent>
	{
		error = why;
		return nullptr;
	};
	if (root.empty () || !IsSafeRelativePath (relativePath))
		return fail ("source must be relative to the authorized root");
	if (options.pieceLength
		&& (options.pieceLength < MIN_PIECE_LENGTH || options.pieceLength > MAX_PIECE_LENGTH
			|| (options.pieceLength & (options.pieceLength - 1))))
		return fail ("invalid piece length");
	std::vector<File> files;
	size_t entries = 0;
	uint64_t total = 0;
#ifndef _WIN32
	Descriptor anchor (::open (root.c_str (), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
	if (anchor.value < 0) return fail ("cannot open authorized root");
	if (!Collect (anchor.value, relativePath, 0, files, entries, total, options.cancelled))
#else
	if (!Collect (root, relativePath, 0, files, entries, total, options.cancelled))
#endif
		return fail (options.cancelled && options.cancelled ()
				? "creation cancelled"
				: "source contains inaccessible, unsafe or excessive entries");
	if (files.empty () || !total || total > std::numeric_limits<size_t>::max ())
		return fail ("empty or excessive source size");
	const auto pieceLength = SelectTorrentPieceLength (total, options.pieceLength);
	if (!pieceLength) return fail ("piece budget exceeded or invalid piece length");
	const auto count = total / pieceLength + (total % pieceLength != 0);
	uint64_t bytesDone = 0;
	size_t filesDone = 0;
	if (options.progress) options.progress (0, total, 0, files.size ());
	std::sort (files.begin (), files.end (),
		[] (const File & a, const File & b) { return a.path.generic_string () < b.path.generic_string (); });
	const bool single = files.size () == 1 && files.front ().path == relativePath;
	std::vector<std::pair<std::filesystem::path, size_t>> layout;
	std::vector<Torrent::InfoHash> hashes;
	hashes.reserve (static_cast<size_t> (count));
	std::unique_ptr<EVP_MD_CTX, decltype (&EVP_MD_CTX_free)> digest (EVP_MD_CTX_new (), EVP_MD_CTX_free);
	if (!digest || EVP_DigestInit_ex (digest.get (), EVP_sha1 (), nullptr) != 1)
		return fail ("cannot initialize piece digest");
	size_t pieceBytes = 0;
	std::array<uint8_t, 64 * 1024> buffer;
	auto finish = [&] ()
	{
		Torrent::InfoHash hash;
		unsigned int length = 0;
		if (EVP_DigestFinal_ex (digest.get (), hash.data (), &length) != 1 || length != hash.size ()) return false;
		hashes.push_back (hash);
		pieceBytes = 0;
		return EVP_DigestInit_ex (digest.get (), EVP_sha1 (), nullptr) == 1;
	};
	for (const auto & file : files)
	{
		layout.emplace_back (single ? std::filesystem::path { } : file.path.lexically_relative (relativePath),
			static_cast<size_t> (file.size));
#ifndef _WIN32
		Descriptor fd (OpenRelative (anchor.value, file.path));
		struct stat before, after;
		if (fd.value < 0 || ::fstat (fd.value, &before) || !S_ISREG (before.st_mode) || before.st_dev != file.device
			|| before.st_ino != file.inode || before.st_size < 0 || static_cast<uint64_t> (before.st_size) != file.size
			|| before.st_mtime != file.modified || before.st_ctime != file.changed)
			return fail ("source changed before hashing");
#else
		if (!NoReparse (root, file.path)) return fail ("unsafe source path");
		Descriptor fd (_wopen ((root / file.path).c_str (), _O_RDONLY | _O_BINARY | _O_NOINHERIT));
		struct _stat64 before, after;
		if (fd.value < 0 || _fstat64 (fd.value, &before) || (before.st_mode & _S_IFMT) != _S_IFREG || before.st_size < 0
			|| static_cast<uint64_t> (before.st_size) != file.size)
			return fail ("source changed before hashing");
#endif
		uint64_t remaining = file.size;
		while (remaining)
		{
			if (options.cancelled && options.cancelled ()) return fail ("creation cancelled");
			auto wanted = static_cast<size_t> (
				std::min<uint64_t> (remaining, std::min (buffer.size (), pieceLength - pieceBytes)));
#ifdef _WIN32
			auto read = _read (fd.value, buffer.data (), static_cast<unsigned int> (wanted));
#else
			ssize_t read;
			do
			{
				read = ::read (fd.value, buffer.data (), wanted);
			} while (read < 0 && errno == EINTR);
#endif
			if (read <= 0) return fail ("source truncated or unreadable while hashing");
			if (EVP_DigestUpdate (digest.get (), buffer.data (), static_cast<size_t> (read)) != 1)
				return fail ("piece digest update failed");
			remaining -= read;
			pieceBytes += read;
			bytesDone += static_cast<uint64_t> (read);
			if (options.progress) options.progress (bytesDone, total, filesDone, files.size ());
			if (pieceBytes == pieceLength && !finish ()) return fail ("piece digest finalization failed");
		}
#ifndef _WIN32
		if (::fstat (fd.value, &after) || after.st_size != before.st_size
#else
		if (_fstat64 (fd.value, &after) || after.st_size != before.st_size
#endif
			|| after.st_mtime != before.st_mtime || after.st_ctime != before.st_ctime)
			return fail ("source changed while hashing");
		++filesDone;
		if (options.progress) options.progress (bytesDone, total, filesDone, files.size ());
	}
	if (pieceBytes && !finish ()) return fail ("last piece digest failed");
	auto torrent = Torrent::CreateV1 (
		relativePath.filename ().string (), layout, pieceLength, hashes, options.trackers, options.privateTorrent);
	if (!torrent) return fail ("source names, tracker list or metadata are invalid");
	return torrent;
}
catch (const std::exception &)
{
	error = "cannot inspect or hash torrent source";
	return nullptr;
}
}
#endif
