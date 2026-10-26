/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: FS.
 *
 * Implements data-directory detection and filesystem helpers: Windows AppData
 * or service paths, OpenBSD /var/i2pd, Android external storage and Unix home
 * fallbacks, plus directory/file creation, rename and removal. Hashed storage
 * maps identities to sanitised leaf names and walks the tree, while private
 * key files are created owner-readable and owner-writable only.
 *
 */

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <fstream>
#include <mutex>
#include <system_error>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <sddl.h>
#include <shlobj.h>
#include <codecvt>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include "Base.h"
#include "FS.h"
#include "Log.h"
#include "Garlic.h"
#include "Modern.h"

#include <filesystem>
namespace fs_lib = std::filesystem;

namespace i2p
{
namespace fs
{
	static std::mutex g_AtomicWriteMutex;

#ifdef _WIN32
	bool BuildPrivateFileSecurity (PSECURITY_DESCRIPTOR & descriptor)
	{
		HANDLE token = nullptr;
		if (!OpenProcessToken (GetCurrentProcess (), TOKEN_QUERY, &token)) return false;
		auto closeToken = i2p::modern::ScopeExit ([&] { CloseHandle (token); });
		DWORD size = 0;
		GetTokenInformation (token, TokenUser, nullptr, 0, &size);
		if (GetLastError () != ERROR_INSUFFICIENT_BUFFER || !size) return false;
		std::vector<BYTE> buffer (size);
		if (!GetTokenInformation (token, TokenUser, buffer.data (), size, &size)) return false;
		auto * user = reinterpret_cast<TOKEN_USER *> (buffer.data ());
		LPWSTR sid = nullptr;
		if (!ConvertSidToStringSidW (user->User.Sid, &sid)) return false;
		auto freeSid = i2p::modern::ScopeExit ([&] { LocalFree (sid); });
		const std::wstring sddl = L"D:P(A;;FA;;;" + std::wstring (sid) + L")(A;;FA;;;SY)";
		if (!ConvertStringSecurityDescriptorToSecurityDescriptorW (
				sddl.c_str (), SDDL_REVISION_1, &descriptor, nullptr))
			return false;
		BOOL present = FALSE, defaulted = FALSE;
		PACL dacl = nullptr;
		SECURITY_DESCRIPTOR_CONTROL control = 0;
		DWORD revision = 0;
		return GetSecurityDescriptorDacl (descriptor, &present, &dacl, &defaulted) && present && dacl
			&& GetSecurityDescriptorControl (descriptor, &control, &revision) && (control & SE_DACL_PROTECTED);
	}
#endif
	std::string appName = "i2pd";
	std::string dataDir = "";
	std::string certsDir = "";
#ifdef _WIN32
	std::string dirSep = "\\";
#else
	std::string dirSep = "/";
#endif

	const std::string & GetAppName ()
	{
		return appName;
	}

	void SetAppName (const std::string & name)
	{
		appName = name;
	}

	const std::string & GetDataDir ()
	{
		return dataDir;
	}

	const std::string & GetCertsDir ()
	{
		return certsDir;
	}

	const std::string GetUTF8DataDir ()
	{
#ifdef _WIN32
		int size = MultiByteToWideChar (CP_ACP, 0, dataDir.c_str (), static_cast<int> (dataDir.size ()), nullptr, 0);
		std::wstring utf16Str (size, L'\0');
		MultiByteToWideChar (CP_ACP, 0, dataDir.c_str (), static_cast<int> (dataDir.size ()), &utf16Str[0], size);
		int utf8Size = WideCharToMultiByte (
			CP_UTF8, 0, utf16Str.c_str (), static_cast<int> (utf16Str.size ()), nullptr, 0, nullptr, nullptr);
		std::string utf8Str (utf8Size, '\0');
		WideCharToMultiByte (CP_UTF8, 0, utf16Str.c_str (), static_cast<int> (utf16Str.size ()), &utf8Str[0], utf8Size,
			nullptr, nullptr);
		return utf8Str;
#else
		return dataDir; // linux, osx, android uses UTF-8 by default
#endif
	}

	void DetectDataDir (const std::string & cmdline_param, bool isService)
	{
		// with 'datadir' option
		if (cmdline_param != "")
		{
			dataDir = cmdline_param;
			return;
		}

#if !defined(ANDROID)
		// with 'service' option
		if (isService)
		{
#ifdef _WIN32
			wchar_t commonAppData[MAX_PATH];
			if (SHGetFolderPathW (NULL, CSIDL_COMMON_APPDATA, NULL, 0, commonAppData) != S_OK)
			{
#ifdef WIN32_APP
				MessageBox (
					NULL, TEXT ("Unable to get common AppData path!"), TEXT ("openi2pd: error"), MB_ICONERROR | MB_OK);
#else
				fprintf (stderr, "Error: Unable to get common AppData path!");
#endif
				exit (1);
			}
			else
				dataDir = (fs_lib::path (commonAppData) / appName).string ();
#elif defined(__OpenBSD__)
			dataDir = "/var/" + appName;
#else
			dataDir = "/var/lib/" + appName;
#endif
			return;
		}
#endif

		// detect directory as usual
#ifdef _WIN32
		wchar_t localAppData[MAX_PATH];

		// check executable directory first
		if (!GetModuleFileNameW (NULL, localAppData, MAX_PATH))
		{
#ifdef WIN32_APP
			MessageBox (NULL, TEXT ("Unable to get application path!"), TEXT ("openi2pd: error"), MB_ICONERROR | MB_OK);
#else
			fprintf (stderr, "Error: Unable to get application path!");
#endif
			exit (1);
		}
		else
		{
			auto execPath = fs_lib::path (localAppData).parent_path ();

			// if config file exists in .exe's folder use it
			if (fs_lib::exists (execPath / "i2pd.conf")) // TODO: magic string
				dataDir = execPath.string ();
			else // otherwise %appdata%
			{
				if (SHGetFolderPathW (NULL, CSIDL_APPDATA, NULL, 0, localAppData) != S_OK)
				{
#ifdef WIN32_APP
					MessageBox (
						NULL, TEXT ("Unable to get AppData path!"), TEXT ("openi2pd: error"), MB_ICONERROR | MB_OK);
#else
					fprintf (stderr, "Error: Unable to get AppData path!");
#endif
					exit (1);
				}
				else
					dataDir = (fs_lib::path (localAppData) / appName).string ();
			}
		}
		return;
#else /* other unix */
#if defined(ANDROID)
		const char * ext = getenv ("EXTERNAL_STORAGE");
		if (!ext) ext = "/sdcard";
		if (fs_lib::exists (ext))
		{
			dataDir = std::string (ext) + "/" + appName;
			return;
		}
#endif // ANDROID
		// use /home/user/.i2pd or /tmp/i2pd
		char * home = getenv ("HOME");
		if (home != NULL && strlen (home) > 0)
		{
			dataDir = std::string (home) + "/." + appName;
		}
		else
		{
			dataDir = "/tmp/" + appName;
		}
		return;
#endif
	}

	void SetCertsDir (const std::string & cmdline_certsdir)
	{
		if (cmdline_certsdir != "")
		{
			if (cmdline_certsdir[cmdline_certsdir.length () - 1] == '/')
				certsDir = cmdline_certsdir.substr (0, cmdline_certsdir.size () - 1); // strip trailing slash
			else
				certsDir = cmdline_certsdir;
		}
		else
		{
			certsDir = i2p::fs::DataDirPath ("certificates");
		}
		return;
	}

	bool Init ()
	{
		if (!fs_lib::exists (dataDir)) fs_lib::create_directory (dataDir);

		std::string destinations = DataDirPath ("destinations");
		if (!fs_lib::exists (destinations)) fs_lib::create_directory (destinations);

		std::string tags = DataDirPath ("tags");
		if (!fs_lib::exists (tags))
			fs_lib::create_directory (tags);
		else
			i2p::garlic::CleanUpTagsFiles ();

		return true;
	}

	bool ReadDir (const std::string & path, std::vector<std::string> & files)
	{
		if (!fs_lib::exists (path)) return false;
		fs_lib::directory_iterator it (path);
		fs_lib::directory_iterator end;

		for (; it != end; it++)
		{
			if (!fs_lib::is_regular_file (it->status ())) continue;
			files.push_back (it->path ().string ());
		}

		return true;
	}

	bool Exists (const std::string & path)
	{
		return fs_lib::exists (path);
	}

	uint32_t GetLastUpdateTime (const std::string & path)
	{
		if (!fs_lib::exists (path)) return 0;
		std::error_code ec;
		auto t = std::filesystem::last_write_time (path, ec);
		if (ec) return 0;
#if __cpp_lib_chrono >= 201907L // std::chrono::clock_cast (OpenBSD libc++ gap)
		const auto sctp = std::chrono::clock_cast<std::chrono::system_clock> (t);
#else
		const auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration> (
			t - decltype (t)::clock::now () + std::chrono::system_clock::now ());
#endif
		return static_cast<uint32_t> (std::chrono::system_clock::to_time_t (sctp));
	}

	bool Remove (const std::string & path)
	{
		if (!fs_lib::exists (path)) return false;
		return fs_lib::remove (path);
	}

	bool Rename (std::string_view from, std::string_view to)
	{
		try
		{
			fs_lib::rename (from, to);
		}
		catch (std::exception & ex)
		{
			LogPrint (eLogError, "FS: Can't rename from ", from, " to ", to, " : ", ex.what ());
			return false;
		}
		return true;
	}

	/**
	 * @brief Durable, atomic replacement of one file.
	 * @param path target path; the parent directory supplies the temporary file.
	 * @param contents full new contents (binary safe).
	 * @param privateWrite create/replace with owner-only permissions and refuse a
	 *        directory or reparse target on Windows.
	 * @return false when the replacement did not commit.
	 *
	 * The sequence is the same on every platform: write a unique temporary file in
	 * the target directory, flush it to stable storage, close it, then rename it
	 * over the target, and finally flush the directory. Nothing is ever written
	 * into the target name, so a crash leaves either the old file or the new one,
	 * never a truncated mixture, and a concurrent reader never sees a partial
	 * document. The temporary name includes the PID and a process-wide counter, and
	 * POSIX creation uses O_EXCL|O_NOFOLLOW so a pre-planted file or symlink cannot
	 * be followed or overwritten; on POSIX the directory is opened with O_DIRECTORY
	 * and O_NOFOLLOW and all operations use *at() calls relative to it. A name
	 * collision is retried with the next counter value instead of reusing a
	 * temporary that may already belong to another writer.
	 *
	 * Notes for callers:
	 * - The whole write is serialised by one process-wide mutex, so a slow disk
	 *   delays every other atomic writer in this process. That is intentional: it
	 *   keeps the temporary-name sequence and the store renames ordered.
	 * - On POSIX a successful rename followed by a failed directory fsync reports
	 *   false even though the new contents are visible. The durability of that
	 *   replacement is therefore ambiguous, and a caller that tracks dirty state
	 *   must keep it rather than assume the update was lost.
	 * - Private mode is applied with fchmod() to the already-open temporary before
	 *   the rename, so the secret is never chmod-ed after it became visible under
	 *   its final name.
	 */
	bool AtomicWriteImpl (std::string_view path, std::string_view contents, bool privateWrite)
	{
		std::lock_guard writerLock (g_AtomicWriteMutex);
		static std::atomic<uint64_t> sequence { 0 };
		const fs_lib::path target { std::string (path) };
		const auto parent = target.parent_path ();
		if (target.empty () || target.filename ().empty ()) return false;
#ifdef _WIN32
		PSECURITY_DESCRIPTOR privateDescriptor = nullptr;
		auto freeDescriptor = i2p::modern::ScopeExit (
			[&]
			{
				if (privateDescriptor) LocalFree (privateDescriptor);
			});
		SECURITY_ATTRIBUTES privateAttributes { sizeof (SECURITY_ATTRIBUTES), nullptr, FALSE };
		if (privateWrite)
		{
			if (!BuildPrivateFileSecurity (privateDescriptor))
			{
				LogPrint (eLogError, "FS: Can't build private file DACL");
				return false;
			}
			privateAttributes.lpSecurityDescriptor = privateDescriptor;
			const DWORD attributes = GetFileAttributesW (target.c_str ());
			if (attributes != INVALID_FILE_ATTRIBUTES
				&& (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
			{
				LogPrint (eLogError, "FS: Refusing private replacement of directory/reparse target ", target.string ());
				return false;
			}
		}
#else
		(void)privateWrite;
#endif
		bool written = false;
		for (unsigned attempt = 0; attempt < 16 && !written; ++attempt)
		{
			auto temporary = target;
#ifdef _WIN32
			const auto process = GetCurrentProcessId ();
#else
			const auto process = static_cast<uint64_t> (getpid ());
#endif
			temporary += ".tmp-" + std::to_string (process) + "-" + std::to_string (++sequence);
#ifdef _WIN32
			HANDLE file
				= CreateFileW (temporary.c_str (), GENERIC_WRITE, 0, privateWrite ? &privateAttributes : nullptr,
					CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY | (privateWrite ? FILE_FLAG_OPEN_REPARSE_POINT : 0), nullptr);
			if (file == INVALID_HANDLE_VALUE)
			{
				if (GetLastError () == ERROR_FILE_EXISTS) continue;
				LogPrint (eLogError, "FS: Can't create temporary file ", temporary.string ());
				return false;
			}
			bool tempCommitted = false;
			auto cleanupTemp = i2p::modern::ScopeExit (
				[&]
				{
					if (!tempCommitted) DeleteFileW (temporary.c_str ());
				});
			auto closeFile = i2p::modern::ScopeExit (
				[&]
				{
					if (file != INVALID_HANDLE_VALUE) CloseHandle (file);
				});
			written = true;
			size_t offset = 0;
			while (offset < contents.size ())
			{
				const auto count = static_cast<DWORD> (std::min<size_t> (contents.size () - offset, MAXDWORD));
				DWORD actual = 0;
				if (!WriteFile (file, contents.data () + offset, count, &actual, nullptr) || !actual)
				{
					written = false;
					break;
				}
				offset += actual;
			}
			if (written && !FlushFileBuffers (file)) written = false;
			if (!CloseHandle (file))
				written = false;
			else
				file = INVALID_HANDLE_VALUE;
			if (written
				&& !MoveFileExW (
					temporary.c_str (), target.c_str (), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
				written = false;
			else if (written)
				tempCommitted = true;
#else
			int flags = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_NOFOLLOW
			flags |= O_NOFOLLOW;
#endif
#ifdef O_CLOEXEC
			flags |= O_CLOEXEC;
#endif
			int directoryFlags = O_RDONLY;
#ifdef O_DIRECTORY
			directoryFlags |= O_DIRECTORY;
#endif
#ifdef O_NOFOLLOW
			directoryFlags |= O_NOFOLLOW;
#endif
#ifdef O_CLOEXEC
			directoryFlags |= O_CLOEXEC;
#endif
			const auto directory = parent.empty () ? fs_lib::path (".") : parent;
			const int dirfd = ::open (directory.c_str (), directoryFlags);
			if (dirfd < 0)
			{
				LogPrint (eLogError, "FS: Can't securely open destination directory ", directory.string (), ": ",
					strerror (errno));
				return false;
			}
			auto closeDirectory = i2p::modern::ScopeExit ([&] { ::close (dirfd); });
			struct stat directoryStatus;
			if (::fstat (dirfd, &directoryStatus) != 0 || !S_ISDIR (directoryStatus.st_mode))
			{
				LogPrint (eLogError, "FS: Destination parent is not a directory: ", directory.string ());
				return false;
			}
			const auto temporaryName = temporary.filename ().string ();
			const auto targetName = target.filename ().string ();
			bool tempCreated = false;
			bool tempCommitted = false;
			auto cleanupTemp = i2p::modern::ScopeExit (
				[&]
				{
					if (tempCreated && !tempCommitted) ::unlinkat (dirfd, temporaryName.c_str (), 0);
				});
			const int fd = ::openat (dirfd, temporaryName.c_str (), flags, S_IRUSR | S_IWUSR);
			if (fd < 0)
			{
				const int error = errno;
				if (error == EEXIST) continue;
				LogPrint (eLogError, "FS: Can't create temporary file ", temporary.string (), ": ", strerror (error));
				return false;
			}
			tempCreated = true;
			bool fileOpen = true;
			auto closeFile = i2p::modern::ScopeExit (
				[&]
				{
					if (fileOpen) ::close (fd);
				});
			written = true;
			size_t offset = 0;
			while (offset < contents.size ())
			{
				const auto count = ::write (fd, contents.data () + offset, contents.size () - offset);
				if (count < 0 && errno == EINTR) continue;
				if (count <= 0)
				{
					written = false;
					break;
				}
				offset += static_cast<size_t> (count);
			}
			if (written && ::fsync (fd) != 0) written = false;
			if (written && privateWrite && ::fchmod (fd, S_IRUSR | S_IWUSR) != 0) written = false;
			if (::close (fd) != 0) written = false;
			fileOpen = false;
			if (written && ::renameat (dirfd, temporaryName.c_str (), dirfd, targetName.c_str ()) != 0)
				written = false;
			else if (written)
				tempCommitted = true;
			if (written)
			{
				if (::fsync (dirfd) != 0)
				{
					written = false;
					LogPrint (eLogError, "FS: Replacement committed but directory durability is uncertain for ",
						target.string ());
				}
			}
#endif
			if (!written)
			{
				LogPrint (eLogError, "FS: Durable atomic replacement failed for ", target.string ());
				return false;
			}
		}
		if (!written) LogPrint (eLogError, "FS: Unable to allocate temporary file for ", target.string ());
		return written;
	}

	bool AtomicWrite (std::string_view path, std::string_view contents)
	{
		return AtomicWriteImpl (path, contents, false);
	}

	bool AtomicWritePrivate (std::string_view path, std::string_view contents)
	{
		return AtomicWriteImpl (path, contents, true);
	}

	bool CreateDirectory (const std::string & path)
	{
		if (fs_lib::exists (path) && fs_lib::is_directory (fs_lib::status (path))) return true;
		return fs_lib::create_directory (path);
	}

	/**
	 * @brief Create a private-mode file for a caller that writes it itself.
	 * @param path file to create or append to.
	 * @return the same path, so it can be used inline in an ofstream constructor.
	 *
	 * This is the legacy helper for callers that need a stream rather than a
	 * complete buffer. It creates the file with the process umask and then applies
	 * owner read/write permissions afterwards, which leaves a window in which the
	 * file is more permissive than intended and follows a pre-existing symlink.
	 * New secret output should use AtomicWritePrivate() instead, which creates a
	 * unique no-follow temporary and applies the mode before the rename. A failure
	 * to restrict the mode is only logged, so a caller handling key material must
	 * not rely on this function alone.
	 */
	std::filesystem::path CreatePrivateFile (const std::filesystem::path & path)
	{
		std::ofstream f (path, std::ofstream::out | std::ofstream::app);
		std::error_code ec;
		std::filesystem::permissions (path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
			std::filesystem::perm_options::replace, ec);
		if (ec) LogPrint (eLogWarning, "FS: Can't restrict access to ", path.string (), ": ", ec.message ());
		return path;
	}

	void HashedStorage::SetPlace (const std::string & path)
	{
		root = path + i2p::fs::dirSep + name;
	}

	bool HashedStorage::Init (const char * chars, size_t count)
	{
		try
		{
			if (!fs_lib::exists (root)) fs_lib::create_directories (root);

			for (size_t i = 0; i < count; i++)
			{
				auto p = root + i2p::fs::dirSep + prefix1 + chars[i];
				if (fs_lib::exists (p)) continue;
				if (fs_lib::create_directory (p)) continue; /* ^ throws exception on failure */
				return false;
			}
		}
		catch (std::exception & ex)
		{
			LogPrint (eLogCritical, "FS: Can't init storage ", root, ": ", ex.what ());
			ThrowFatal ("Can't init FS storage ", root, ": ", ex.what ());
			return false;
		}
		return true;
	}

	/**
	 * @brief Map an identity string to a sharded storage path.
	 * @param ident encoded identity (base64 or base32, already validated by the
	 *        caller); the first character selects the shard directory.
	 * @return relative path below the store root.
	 *
	 * Only the two path separators are replaced, so this function is a name
	 * normaliser, not a sanitiser: the caller must pass a canonical encoded
	 * identity and must never pass a peer-controlled file name, because "..", NUL
	 * or drive colons would otherwise reach the filesystem. The result keeps the
	 * store's prefix/suffix conventions so an unrelated regular file is never
	 * matched by expiry cleanup.
	 */
	std::string HashedStorage::Path (const std::string & ident) const
	{
		std::string safe_ident = ident;
		std::replace (safe_ident.begin (), safe_ident.end (), '/', '-');
		std::replace (safe_ident.begin (), safe_ident.end (), '\\', '-');

		std::stringstream t ("");
		t << this->root << i2p::fs::dirSep;
		t << prefix1 << safe_ident[0] << i2p::fs::dirSep;
		t << prefix2 << safe_ident << "." << suffix;

		return t.str ();
	}

	void HashedStorage::Remove (const std::string & ident)
	{
		std::string path = Path (ident);
		if (!fs_lib::exists (path)) return;
		fs_lib::remove (path);
	}

	void HashedStorage::Traverse (std::vector<std::string> & files)
	{
		Iterate ([&files] (const std::string & fname) { files.push_back (fname); });
	}

	void HashedStorage::Iterate (FilenameVisitor v)
	{
		fs_lib::path p (root);
		fs_lib::recursive_directory_iterator it (p);
		fs_lib::recursive_directory_iterator end;

		for (; it != end; it++)
		{
			if (!fs_lib::is_regular_file (it->status ())) continue;
			const std::string & t = it->path ().string ();
			v (t);
		}
	}
} // fs
} // i2p
