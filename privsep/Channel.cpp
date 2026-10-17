/*
* Copyright (c) 2026, The PurpleI2P Project
*
* This file is part of Open I2Pd and licensed under BSD3
*/

#include "Channel.h"

#ifndef _WIN32

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
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

	std::expected<std::pair<Channel, Channel>, std::string> Channel::CreatePair ()
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
					return std::unexpected (std::string ("socketpair(SOCK_SEQPACKET): ") + strerror (errno));
			}
			else
#endif
				return std::unexpected (std::string ("socketpair(SOCK_SEQPACKET): ") + strerror (errno));
		}
		for (int fd : fds)
		{
			int flags = fcntl (fd, F_GETFD);
			if (flags >= 0) fcntl (fd, F_SETFD, flags | FD_CLOEXEC);
		}
		return std::pair<Channel, Channel> (Channel (fds[0]), Channel (fds[1]));
	}

	std::expected<void, std::string> Channel::Send (const Message& message, int timeoutMs)
	{
		if (!IsOpen ()) return std::unexpected ("channel is closed");
		auto encoded = Encode (message);
		if (!encoded) return std::unexpected (std::string (encoded.error ().text));

		pollfd pfd{m_Fd, POLLOUT, 0};
		int result;
		do result = ::poll (&pfd, 1, timeoutMs); while (result < 0 && errno == EINTR);
		if (result <= 0) return std::unexpected (result == 0 ? "send timeout" : "poll failed");

		ssize_t sent;
		do sent = ::send (m_Fd, encoded->data (), encoded->size (), MSG_NOSIGNAL);
		while (sent < 0 && errno == EINTR);
		if (sent != static_cast<ssize_t> (encoded->size ()))
			return std::unexpected (std::string ("send: ") + strerror (errno));
		return {};
	}

	std::expected<std::optional<Message>, std::string> Channel::Receive (int timeoutMs)
	{
		if (!IsOpen ()) return std::unexpected ("channel is closed");
		pollfd pfd{m_Fd, POLLIN, 0};
		int result;
		do result = ::poll (&pfd, 1, timeoutMs); while (result < 0 && errno == EINTR);
		if (result == 0) return std::optional<Message>{};
		if (result < 0) return std::unexpected (std::string ("poll: ") + strerror (errno));

		std::string frame (HEADER_SIZE + MAX_MESSAGE_SIZE, '\0');
		ssize_t received;
		do received = ::recv (m_Fd, frame.data (), frame.size (), 0);
		while (received < 0 && errno == EINTR);
		if (received == 0) return std::optional<Message>{};
		if (received < 0) return std::unexpected (std::string ("recv: ") + strerror (errno));
		frame.resize (static_cast<size_t> (received));
		auto message = Decode (frame);
		if (!message) return std::unexpected (std::string ("protocol: ") + std::string (message.error ().text));
		return std::optional<Message> (std::move (*message));
	}

	std::expected<uint32_t, std::string> Channel::PeerUid () const
	{
#if defined(__linux__)
		struct ucred cred{};
		socklen_t length = sizeof (cred);
		if (getsockopt (m_Fd, SOL_SOCKET, SO_PEERCRED, &cred, &length) != 0)
			return std::unexpected (std::string ("SO_PEERCRED: ") + strerror (errno));
		return cred.uid;
#elif defined(__OpenBSD__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__APPLE__)
		uid_t uid = 0;
		gid_t gid = 0;
		if (getpeereid (m_Fd, &uid, &gid) != 0)
			return std::unexpected (std::string ("getpeereid: ") + strerror (errno));
		return uid;
#else
		return std::unexpected ("peer credentials are not available on this platform");
#endif
	}
}

#endif
