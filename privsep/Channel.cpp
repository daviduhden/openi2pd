/*
* Copyright (c) 2026, The PurpleI2P Project
* Copyright (c) 2026, David Uhden Collado
* SPDX-License-Identifier: BSD-3-Clause
*
* This file is part of openi2pd and licensed under BSD-3-Clause
*/

#include "Channel.h"

#ifndef _WIN32

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/un.h>
#endif

namespace i2p::privsep
{
	Channel::~Channel ()
	{
		Close ();
	}

	Channel& Channel::operator= (Channel&& other) noexcept
	{
		if (this != &other)
		{
			Close ();
			m_Fd = other.m_Fd;
			other.m_Fd = -1;
		}
		return *this;
	}

	void Channel::Close ()
	{
		if (m_Fd >= 0)
		{
			::close (m_Fd);
			m_Fd = -1;
		}
	}

	i2p::expected<std::pair<Channel, Channel>, std::string> Channel::CreatePair ()
	{
		int fds[2] = {-1, -1};
		int type = SOCK_SEQPACKET;
#ifdef SOCK_CLOEXEC
		type |= SOCK_CLOEXEC;
#endif
		if (::socketpair (AF_UNIX, type, 0, fds) != 0)
		{
#ifdef SOCK_CLOEXEC
			if (errno == EINVAL)
			{
				if (::socketpair (AF_UNIX, SOCK_SEQPACKET, 0, fds) != 0)
					return i2p::unexpected (std::string ("socketpair(SOCK_SEQPACKET): ") + strerror (errno));
			}
			else
#endif
				return i2p::unexpected (std::string ("socketpair(SOCK_SEQPACKET): ") + strerror (errno));
		}
		for (int fd : fds)
		{
			int flags = fcntl (fd, F_GETFD);
			if (flags < 0 || fcntl (fd, F_SETFD, flags | FD_CLOEXEC) < 0)
			{
				// Never hand out a descriptor that might leak through exec.
				const int error = errno;
				::close (fds[0]);
				::close (fds[1]);
				return i2p::unexpected (std::string ("fcntl(FD_CLOEXEC): ") + strerror (error));
			}
		}
		return std::pair<Channel, Channel> (Channel (fds[0]), Channel (fds[1]));
	}

	i2p::expected<void, std::string> Channel::Send (const Message& message, int timeoutMs)
	{
		if (!IsOpen ()) return i2p::unexpected ("channel is closed");
		auto encoded = Encode (message);
		if (!encoded) return i2p::unexpected (std::string (encoded.error ().text));

		pollfd pfd{m_Fd, POLLOUT, 0};
		int result;
		do result = ::poll (&pfd, 1, timeoutMs); while (result < 0 && errno == EINTR);
		if (result < 0)
		{
			const int error = errno;
			return i2p::unexpected (std::string ("poll: ") + strerror (error));
		}
		if (result == 0) return i2p::unexpected ("send timeout");
		if (pfd.revents & POLLNVAL) return i2p::unexpected ("poll: invalid descriptor");

		ssize_t sent;
		do sent = ::send (m_Fd, encoded->data (), encoded->size (), MSG_NOSIGNAL);
		while (sent < 0 && errno == EINTR);
		if (sent < 0)
		{
			const int error = errno;
			return i2p::unexpected (std::string ("send: ") + strerror (error));
		}
		// SOCK_SEQPACKET is all-or-nothing. Treat anything else as an error
		// rather than waiting for a continuation that cannot exist.
		if (sent != static_cast<ssize_t> (encoded->size ()))
			return i2p::unexpected ("send: short write");
		return {};
	}

	i2p::expected<std::optional<Message>, std::string> Channel::Receive (int timeoutMs)
	{
		if (!IsOpen ()) return i2p::unexpected ("channel is closed");
		pollfd pfd{m_Fd, POLLIN, 0};
		int result;
		do result = ::poll (&pfd, 1, timeoutMs); while (result < 0 && errno == EINTR);
		if (result == 0) return std::optional<Message>{};
		if (result < 0)
		{
			const int error = errno;
			return i2p::unexpected (std::string ("poll: ") + strerror (error));
		}
		if (pfd.revents & POLLNVAL) return i2p::unexpected ("poll: invalid descriptor");

		// Deliberately one buffer slot larger than a maximum frame so a
		// truncated SOCK_SEQPACKET record can never look like a valid frame.
		std::string frame (HEADER_SIZE + MAX_MESSAGE_SIZE, '\0');
		struct iovec iov{frame.data (), frame.size ()};
		struct msghdr header{};
		header.msg_iov = &iov;
		header.msg_iovlen = 1;
		ssize_t received;
		do received = ::recvmsg (m_Fd, &header, 0);
		while (received < 0 && errno == EINTR);
		if (received == 0) return i2p::unexpected ("peer closed the channel");
		if (received < 0)
		{
			const int error = errno;
			return i2p::unexpected (std::string ("recv: ") + strerror (error));
		}
		if (header.msg_flags & MSG_TRUNC)
			return i2p::unexpected ("oversized frame");
		frame.resize (static_cast<size_t> (received));
		auto message = Decode (frame);
		if (!message) return i2p::unexpected (std::string ("protocol: ") + std::string (message.error ().text));
		return std::optional<Message> (std::move (*message));
	}

	i2p::expected<uint32_t, std::string> Channel::PeerUid () const
	{
		if (!IsOpen ()) return i2p::unexpected ("channel is closed");
#if defined(__linux__)
		struct ucred cred{};
		socklen_t length = sizeof (cred);
		if (getsockopt (m_Fd, SOL_SOCKET, SO_PEERCRED, &cred, &length) != 0)
			return i2p::unexpected (std::string ("SO_PEERCRED: ") + strerror (errno));
		return cred.uid;
#elif defined(__OpenBSD__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__APPLE__)
		uid_t uid = 0;
		gid_t gid = 0;
		if (getpeereid (m_Fd, &uid, &gid) != 0)
			return i2p::unexpected (std::string ("getpeereid: ") + strerror (errno));
		return uid;
#else
		return i2p::unexpected ("peer credentials are not available on this platform");
#endif
	}
}

#endif
