/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test i2pcontrol socket.
 *
 * Compile-free regression for the I2PControl AF_UNIX path policy: a
 * pre-existing regular file or directory is never unlinked, an owned stale
 * socket is replaced, the path is restricted to 0600 after bind, the path is
 * removed only while it is still the recorded socket, and a same-user peer is
 * recognised. The different-UID case is skipped unless the process can switch
 * credentials. The TLS service itself is not started.
 *
 */

// I2PControl publishes the AF_UNIX helpers only when Asio reports local
// socket support; include the configuration header before guarding the test.
#include <boost/asio.hpp>

#if defined(BOOST_ASIO_HAS_LOCAL_SOCKETS) && !defined(_WIN32)

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "../daemon/I2PControl.h"

namespace
{
	int failures = 0;

	void Check (bool condition, const std::string & what)
	{
		if (condition) return;
		std::cerr << "FAIL: " << what << std::endl;
		++failures;
	}

	bool Exists (const std::string & path)
	{
		struct stat status { };
		return lstat (path.c_str (), &status) == 0;
	}

	int BindUnixSocket (const std::string & path)
	{
		const int fd = ::socket (AF_UNIX, SOCK_STREAM, 0);
		if (fd < 0) return -1;
		sockaddr_un address { };
		address.sun_family = AF_UNIX;
		if (path.size () >= sizeof (address.sun_path))
		{
			::close (fd);
			return -1;
		}
		std::memcpy (address.sun_path, path.c_str (), path.size () + 1);
		if (::bind (fd, reinterpret_cast<sockaddr *> (&address), sizeof (address)) != 0)
		{
			::close (fd);
			return -1;
		}
		::listen (fd, 1);
		return fd;
	}
}

int main ()
{
	using namespace i2p::client::localcontrol;

	char directory[] = "/tmp/i2pd-control-socket-XXXXXX";
	if (!mkdtemp (directory))
	{
		std::cerr << "FAIL: can't create a temporary directory" << std::endl;
		return 1;
	}
	const std::string base (directory);
	const std::string filePath = base + "/regular";
	const std::string directoryPath = base + "/subdir";
	const std::string socketPath = base + "/control.sock";
	std::string error;

	// A fresh path needs no cleanup.
	Check (PrepareSocketPath (base + "/absent", error), "absent path is accepted");

	// A regular file must never be unlinked, even when it is owned by us.
	{
		FILE * file = std::fopen (filePath.c_str (), "w");
		Check (file != nullptr, "regular file created");
		if (file) std::fclose (file);
		Check (!PrepareSocketPath (filePath, error), "regular file is refused");
		Check (Exists (filePath), "regular file is not unlinked");
	}

	// A directory must never be unlinked either.
	Check (mkdir (directoryPath.c_str (), 0700) == 0, "directory created");
	Check (!PrepareSocketPath (directoryPath, error), "directory is refused");
	Check (Exists (directoryPath), "directory is not unlinked");

	// An owned stale socket is replaced.
	{
		const int fd = BindUnixSocket (socketPath);
		Check (fd >= 0, "stale socket bound");
		if (fd >= 0) ::close (fd);
		if (fd >= 0)
		{
			Check (Exists (socketPath), "stale socket exists");
			Check (PrepareSocketPath (socketPath, error), "owned stale socket is accepted");
			Check (!Exists (socketPath), "owned stale socket is removed");
		}
	}

	// The mode is restricted to owner read/write after bind, and removal is
	// tied to the identity of the socket this process created.
	{
		const int fd = BindUnixSocket (socketPath);
		Check (fd >= 0, "socket bound for mode check");
		if (fd >= 0)
		{
			Check (RestrictSocketPath (socketPath, error), "mode restriction succeeds");
			struct stat status { };
			Check (lstat (socketPath.c_str (), &status) == 0, "socket stat");
			if (S_ISSOCK (status.st_mode))
				Check ((status.st_mode & 07777) == (S_IRUSR | S_IWUSR), "socket mode is 0600");

			const auto identity = IdentifySocketPath (socketPath);
			Check (identity.valid, "socket identity recorded");
			::close (fd);

			// Same path, different socket: the recorded identity must not
			// delete the replacement.
			::unlink (socketPath.c_str ());
			const int replacement = BindUnixSocket (socketPath);
			Check (replacement >= 0, "replacement socket bound");
			if (replacement >= 0)
			{
				Check (!RemoveSocketPath (socketPath, identity), "stale identity does not remove a replacement");
				Check (Exists (socketPath), "replacement socket survives a stale identity");
				const auto replacementIdentity = IdentifySocketPath (socketPath);
				Check (RemoveSocketPath (socketPath, replacementIdentity), "own identity removes the socket");
				Check (!Exists (socketPath), "own socket is removed");
				::close (replacement);
			}

			// An absent path is never reported as removed.
			SocketIdentity absent;
			Check (!RemoveSocketPath (socketPath, absent), "absent path with no identity is not removed");
		}
	}

	// Same-user peer detection on a connected AF_UNIX pair.
	if (HAS_PEER_CREDENTIALS)
	{
		int pair[2] = { -1, -1 };
		if (socketpair (AF_UNIX, SOCK_STREAM, 0, pair) == 0)
		{
			Check (IsSameUserPeer (pair[0]), "own pair is a same-user peer");
			Check (IsSameUserPeer (pair[1]), "both ends are same-user");
			::close (pair[0]);
			::close (pair[1]);
		}
		else
			Check (false, "socketpair");
	}

	// A different UID cannot be simulated without changing credentials.
	if (geteuid () != 0)
		std::cout << "skip: different-UID peer rejection needs root" << std::endl;

	unlink (filePath.c_str ());
	rmdir (directoryPath.c_str ());
	rmdir (directory.c_str ());

	if (failures)
	{
		std::cerr << failures << " I2PControl socket checks failed" << std::endl;
		return 1;
	}
	std::cout << "I2PControl socket checks passed" << std::endl;
	return 0;
}

#else /* no AF_UNIX local-socket support */

#include <iostream>

int main ()
{
	std::cout << "skip: AF_UNIX control socket checks are POSIX-only" << std::endl;
	return 77;
}

#endif /* BOOST_ASIO_HAS_LOCAL_SOCKETS && !_WIN32 */
