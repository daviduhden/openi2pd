/*
* Copyright (c) 2026, David Uhden Collado
* SPDX-License-Identifier: BSD-3-Clause
*
* This file is part of openi2pd and licensed under BSD-3-Clause
*
* See full license text in LICENSE file at top of project tree
*/

#include "TuiServer.h"

#ifndef _WIN32

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <utility>

#include "Log.h"
#include "TuiProtocol.h"
#include "TuiServerCommon.h"
#include "util.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0
#endif

namespace i2p
{
namespace tui
{
	namespace
	{
		using SteadyClock = std::chrono::steady_clock;

		// Bound every per-client operation as well as the total number of
		// requests a single connection may issue. The protocol is unchanged:
		// a client that exceeds a budget is closed, at most after one final
		// error frame.
		constexpr int CLIENT_POLL_MS = 200;
		constexpr auto CLIENT_IDLE_TIMEOUT = std::chrono::seconds (5);
		constexpr size_t MAX_REQUESTS_PER_CONNECTION = 4096;
		constexpr auto MAX_CONNECTION_DURATION = std::chrono::minutes (5);
	}

	class Server::Impl
	{
	public:
		explicit Impl (std::string socketPath)
			: m_Path (std::move (socketPath))
		{
		}

		~Impl () { Stop (); }

		Impl (const Impl &) = delete;
		Impl & operator= (const Impl &) = delete;

		bool Start ();
		void Stop ();

	private:
		void Run ();
		void HandleClient (int clientFd);
		bool SendAll (int fd, std::string_view data, SteadyClock::time_point deadline);
		bool IsTrustedPeer (int clientFd) const;

		std::string m_Path;
		int m_ListenFd { -1 };
		dev_t m_Device { 0 };
		ino_t m_Inode { 0 };
		bool m_HasIdentity { false };
		std::thread m_Thread;
		std::atomic<bool> m_Stop { false };
	};

	bool Server::Impl::Start ()
	{
		if (m_ListenFd >= 0) return true;
		m_HasIdentity = false;
		if (m_Path.empty ())
		{
			LogPrint (eLogError, "TUI: Empty socket path");
			return false;
		}
		if (m_Path.size () >= sizeof (sockaddr_un { }.sun_path))
		{
			LogPrint (eLogError, "TUI: Socket path is too long: ", m_Path);
			return false;
		}

		// Refuse to unlink anything that is not a stale socket owned by us.
		// lstat/unlink/bind is not atomic. If an attacker with write access
		// to the parent directory wins the race, bind() fails with EADDRINUSE
		// and this function returns without unlinking the replacement. The
		// inode/dev identity recorded below is also what Stop() verifies
		// before removing the path, so a replaced socket is never deleted.
		struct stat st { };
		if (lstat (m_Path.c_str (), &st) == 0)
		{
			if (!S_ISSOCK (st.st_mode) || st.st_uid != geteuid ())
			{
				LogPrint (eLogError, "TUI: Refusing to replace ", m_Path, ": not an owned socket");
				return false;
			}
			if (unlink (m_Path.c_str ()) != 0 && errno != ENOENT)
			{
				LogPrint (eLogError, "TUI: Can't remove stale socket ", m_Path, ": ", strerror (errno));
				return false;
			}
		}
		else if (errno != ENOENT)
		{
			LogPrint (eLogError, "TUI: Can't inspect ", m_Path, ": ", strerror (errno));
			return false;
		}

		int fd = ::socket (AF_UNIX, SOCK_STREAM, 0);
		if (fd < 0)
		{
			LogPrint (eLogError, "TUI: Can't create socket: ", strerror (errno));
			return false;
		}
		int flags = fcntl (fd, F_GETFD);
		if (flags >= 0) fcntl (fd, F_SETFD, flags | FD_CLOEXEC);
		// accept() is only called after poll() reports a pending connection,
		// but a client that resets before accept can still block a blocking
		// listener. Keep the listener non-blocking so Stop() is bounded.
		flags = fcntl (fd, F_GETFL);
		if (flags >= 0) fcntl (fd, F_SETFL, flags | O_NONBLOCK);

		sockaddr_un address { };
		address.sun_family = AF_UNIX;
		std::memcpy (address.sun_path, m_Path.c_str (), m_Path.size () + 1);

		// Do not touch the process-wide umask: another thread may be creating
		// files concurrently. The path is chmod'ed immediately after bind.
		// The short pre-chmod window does not grant access on its own,
		// because IsTrustedPeer() rejects every uid but the daemon's.
		if (bind (fd, reinterpret_cast<sockaddr *> (&address), sizeof (address)) != 0)
		{
			LogPrint (eLogError, "TUI: Can't bind ", m_Path, ": ", strerror (errno));
			close (fd);
			return false;
		}
		if (chmod (m_Path.c_str (), S_IRUSR | S_IWUSR) != 0)
		{
			LogPrint (eLogError, "TUI: Can't restrict permissions on ", m_Path, ": ", strerror (errno));
			close (fd);
			unlink (m_Path.c_str ());
			return false;
		}
		if (listen (fd, 4) != 0)
		{
			LogPrint (eLogError, "TUI: Can't listen on ", m_Path, ": ", strerror (errno));
			close (fd);
			unlink (m_Path.c_str ());
			return false;
		}
		if (lstat (m_Path.c_str (), &st) == 0)
		{
			m_Device = st.st_dev;
			m_Inode = st.st_ino;
			m_HasIdentity = true;
		}

		m_ListenFd = fd;
		m_Stop.store (false, std::memory_order_relaxed);
		m_Thread = std::thread ([this] { Run (); });
		LogPrint (eLogInfo, "TUI: Listening on ", m_Path);
		return true;
	}

	void Server::Impl::Stop ()
	{
		m_Stop.store (true, std::memory_order_relaxed);
		if (m_Thread.joinable ()) m_Thread.join ();
		if (m_ListenFd >= 0)
		{
			close (m_ListenFd);
			m_ListenFd = -1;
		}
		if (!m_Path.empty ())
		{
			// Only remove the socket if it is still the one we created.
			struct stat st { };
			if (m_HasIdentity && lstat (m_Path.c_str (), &st) == 0 && S_ISSOCK (st.st_mode) && st.st_dev == m_Device
				&& st.st_ino == m_Inode)
				unlink (m_Path.c_str ());
			m_HasIdentity = false;
		}
	}

	void Server::Impl::Run ()
	{
		i2p::util::SetThreadName ("i2pd-tui-ipc");
		while (!m_Stop.load (std::memory_order_relaxed))
		{
			pollfd pfd { m_ListenFd, POLLIN, 0 };
			int result = poll (&pfd, 1, 200);
			if (result < 0)
			{
				if (errno == EINTR) continue;
				LogPrint (eLogError, "TUI: poll: ", strerror (errno));
				return;
			}
			if (result == 0 || !(pfd.revents & POLLIN)) continue;

			int client = accept (m_ListenFd, nullptr, nullptr);
			if (client < 0)
			{
				if (errno != EINTR && errno != EAGAIN) LogPrint (eLogWarning, "TUI: accept: ", strerror (errno));
				continue;
			}
			int clientFlags = fcntl (client, F_GETFD);
			if (clientFlags >= 0) fcntl (client, F_SETFD, clientFlags | FD_CLOEXEC);
			HandleClient (client);
			close (client);
		}
	}

	bool Server::Impl::IsTrustedPeer (int clientFd) const
	{
#if defined(__linux__)
		struct ucred credentials { };
		socklen_t length = sizeof (credentials);
		if (getsockopt (clientFd, SOL_SOCKET, SO_PEERCRED, &credentials, &length) != 0) return false;
		return credentials.uid == geteuid ();
#elif defined(__OpenBSD__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__APPLE__)
		uid_t uid = 0;
		gid_t gid = 0;
		if (getpeereid (clientFd, &uid, &gid) != 0) return false;
		return uid == geteuid ();
#else
		// No portable peer credential API here. Fail closed: the TUI exposes
		// router state and torrent control, so an unauthenticated transport
		// must not be accepted merely because the socket happens to be 0600.
		(void)clientFd;
		return false;
#endif
	}

	bool Server::Impl::SendAll (int fd, std::string_view data, SteadyClock::time_point deadline)
	{
		while (!data.empty ())
		{
			if (m_Stop.load (std::memory_order_relaxed)) return false;
			const auto now = SteadyClock::now ();
			if (now >= deadline) return false;
			const auto remaining
				= std::chrono::duration_cast<std::chrono::milliseconds> (deadline - now).count ();
			if (remaining <= 0) return false;

			pollfd pfd { fd, POLLOUT, 0 };
			int result;
			do result = ::poll (&pfd, 1, static_cast<int> (std::min<int64_t> (remaining, CLIENT_POLL_MS)));
			while (result < 0 && errno == EINTR);
			if (result < 0) return false;
			if (result == 0) continue;
			if (pfd.revents & POLLNVAL) return false;
			if (pfd.revents & (POLLERR | POLLHUP)) return false;
			if (!(pfd.revents & POLLOUT)) continue;

			auto sent = ::send (fd, data.data (), data.size (), MSG_NOSIGNAL | MSG_DONTWAIT);
			if (sent < 0)
			{
				if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
				return false;
			}
			if (!sent) return false;
			data.remove_prefix (static_cast<size_t> (sent));
		}
		return true;
	}

	void Server::Impl::HandleClient (int clientFd)
	{
		// A per-client allocation failure (bad_alloc from the request
		// accumulator or a snapshot) must not unwind into std::thread and
		// terminate the router. Dispatch() already contains subsystem
		// exceptions; this guard covers the transport itself.
		try
		{
			if (!IsTrustedPeer (clientFd))
			{
				LogPrint (eLogWarning, "TUI: Rejected connection from a different user");
				return;
			}

			timeval receiveTimeout { 5, 0 };
			timeval sendTimeout { 5, 0 };
			setsockopt (clientFd, SOL_SOCKET, SO_RCVTIMEO, &receiveTimeout, sizeof (receiveTimeout));
			setsockopt (clientFd, SOL_SOCKET, SO_SNDTIMEO, &sendTimeout, sizeof (sendTimeout));

			const auto connectionDeadline = SteadyClock::now () + MAX_CONNECTION_DURATION;
			auto lastActivity = SteadyClock::now ();
			size_t requests = 0;
			std::string pending;
			pending.reserve (MAX_REQUEST_LINE);
			std::array<char, 1024> buffer { };
			while (!m_Stop.load (std::memory_order_relaxed))
			{
				const auto now = SteadyClock::now ();
				if (now >= connectionDeadline) return;
				if (now - lastActivity >= CLIENT_IDLE_TIMEOUT) return;

				const auto idleRemaining
					= std::chrono::duration_cast<std::chrono::milliseconds> (lastActivity + CLIENT_IDLE_TIMEOUT - now)
						  .count ();
				const auto connectionRemaining
					= std::chrono::duration_cast<std::chrono::milliseconds> (connectionDeadline - now).count ();
				const auto waitMs = std::min<int64_t> (
					{ CLIENT_POLL_MS, std::max<int64_t> (idleRemaining, 1), std::max<int64_t> (connectionRemaining, 1) });

				pollfd pfd { clientFd, POLLIN, 0 };
				int ready;
				do ready = ::poll (&pfd, 1, static_cast<int> (waitMs));
				while (ready < 0 && errno == EINTR);
				if (ready < 0) return;
				if (ready == 0) continue;
				if (pfd.revents & POLLNVAL) return;
				if ((pfd.revents & (POLLERR | POLLHUP)) && !(pfd.revents & POLLIN)) return;
				if (!(pfd.revents & POLLIN)) continue;

				auto received = ::recv (clientFd, buffer.data (), buffer.size (), MSG_DONTWAIT);
				if (received < 0)
				{
					if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
					return;
				}
				if (received == 0) return;
				lastActivity = SteadyClock::now ();

				pending.append (buffer.data (), static_cast<size_t> (received));
				if (pending.size () > MAX_REQUEST_LINE + 1)
				{
					SendAll (clientFd, MakeErrorResponse (ErrorCode::MalformedRequest, "request too long"),
						connectionDeadline);
					return;
				}

				size_t newline = 0;
				while ((newline = pending.find ('\n')) != std::string::npos)
				{
					if (m_Stop.load (std::memory_order_relaxed)) return;
					if (SteadyClock::now () >= connectionDeadline) return;
					if (++requests > MAX_REQUESTS_PER_CONNECTION)
					{
						SendAll (clientFd, MakeErrorResponse (ErrorCode::Internal, "connection request limit reached"),
							connectionDeadline);
						return;
					}
					std::string_view line (pending.data (), newline);
					if (!line.empty () && line.back () == '\r') line.remove_suffix (1);
					auto result = Dispatch (line);
					if (!SendAll (clientFd, result.response, connectionDeadline)) return;
					pending.erase (0, newline + 1);
					if (result.closeConnection) return;
				}
			}
		}
		catch (...)
		{
			// The client is closed by Run(). Never log from here: a second
			// allocation failure while formatting would terminate anyway.
		}
	}

	Server::Server (std::string endpoint)
		: m_Impl (std::make_unique<Impl> (std::move (endpoint)))
	{
	}

	Server::~Server ()
	{
		Stop ();
	}

	bool Server::Start ()
	{
		return m_Impl && m_Impl->Start ();
	}

	void Server::Stop ()
	{
		if (m_Impl) m_Impl->Stop ();
	}
}
}

#endif // _WIN32
