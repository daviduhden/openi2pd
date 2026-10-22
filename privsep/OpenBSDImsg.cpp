/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: OpenBSDImsg.
 *
 * Implements the OpenBSD imsg transport with non-blocking imsg buffers,
 * poll-driven bounded send and receive, descriptor passing and getpeereid
 * credentials, while retaining the shared frame codec and size checks.
 * Non-OpenBSD targets return an explicit unavailability error.
 *
 */

#include "OpenBSDImsg.h"

#ifndef _WIN32

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <new>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

namespace i2p::privsep
{
namespace
{
#if defined(__OpenBSD__)
	using SteadyClock = std::chrono::steady_clock;

	bool IsInfiniteTimeout (int timeoutMs)
	{
		return timeoutMs < 0;
	}

	SteadyClock::time_point MakeDeadline (int timeoutMs)
	{
		return SteadyClock::now () + std::chrono::milliseconds (timeoutMs);
	}

	int PollTimeout (bool infinite, SteadyClock::time_point deadline)
	{
		if (infinite) return -1;
		const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds> (deadline - SteadyClock::now ());
		if (remaining.count () <= 0) return 0;
		return static_cast<int> (std::min<int64_t> (remaining.count (), std::numeric_limits<int>::max ()));
	}

	bool DeadlineExpired (SteadyClock::time_point deadline)
	{
		return SteadyClock::now () >= deadline;
	}
#endif
}

ImsgChannel::~ImsgChannel ()
{
	Close ();
}

ImsgChannel::ImsgChannel (ImsgChannel && other) noexcept
	: m_Fd (other.m_Fd)
#ifdef __OpenBSD__
	, m_Buffer (other.m_Buffer)
#endif
{
	other.m_Fd = -1;
#ifdef __OpenBSD__
	other.m_Buffer = nullptr;
#endif
}

ImsgChannel & ImsgChannel::operator= (ImsgChannel && other) noexcept
{
	if (this != &other)
	{
		Close ();
		m_Fd = other.m_Fd;
#ifdef __OpenBSD__
		m_Buffer = other.m_Buffer;
#endif
		other.m_Fd = -1;
#ifdef __OpenBSD__
		other.m_Buffer = nullptr;
#endif
	}
	return *this;
}

void ImsgChannel::Close ()
{
#ifdef __OpenBSD__
	if (m_Buffer)
	{
		imsgbuf_clear (m_Buffer);
		delete m_Buffer;
		m_Buffer = nullptr;
	}
#endif
	if (m_Fd >= 0)
	{
		::close (m_Fd);
		m_Fd = -1;
	}
}

i2p::expected<ImsgChannel, std::string> ImsgChannel::Adopt (int fd)
{
#if defined(__OpenBSD__)
	if (fd < 0) return i2p::unexpected ("invalid imsg descriptor");
	ImsgChannel channel;
	channel.m_Fd = fd;
	channel.m_Buffer = new (std::nothrow)::imsgbuf ();
	if (!channel.m_Buffer)
	{
		channel.Close ();
		return i2p::unexpected ("cannot allocate imsgbuf");
	}
	if (imsgbuf_init (channel.m_Buffer, fd) != 0)
	{
		const int error = errno;
		channel.Close ();
		return i2p::unexpected (std::string ("imsgbuf_init: ") + strerror (error));
	}
	imsgbuf_allow_fdpass (channel.m_Buffer);
	// Bounded Send/Receive callers poll the descriptor themselves. The imsg
	// helper must not block a worker indefinitely when its peer stops reading.
	const int flags = fcntl (fd, F_GETFL);
	if (flags < 0 || fcntl (fd, F_SETFL, flags | O_NONBLOCK) < 0)
	{
		const int error = errno;
		channel.Close ();
		return i2p::unexpected (std::string ("fcntl(O_NONBLOCK): ") + strerror (error));
	}
	return channel;
#else
	(void)fd;
	return i2p::unexpected ("imsg is available only on OpenBSD");
#endif
}

i2p::expected<ImsgChannel, std::string> ImsgChannel::AdoptValidated (int fd, uint32_t expectedUid)
{
#if defined(__OpenBSD__)
	if (fd < 0) return i2p::unexpected ("invalid imsg descriptor");
	struct stat info { };
	int socketType = 0;
	socklen_t socketTypeLength = sizeof (socketType);
	if (::fstat (fd, &info) != 0 || !S_ISSOCK (info.st_mode)
		|| ::getsockopt (fd, SOL_SOCKET, SO_TYPE, &socketType, &socketTypeLength) != 0
		|| (socketType != SOCK_STREAM && socketType != SOCK_SEQPACKET))
	{
		::close (fd);
		return i2p::unexpected ("imsg descriptor is not a validated stream socket");
	}
	const int flags = ::fcntl (fd, F_GETFD);
	if (flags < 0 || ::fcntl (fd, F_SETFD, flags | FD_CLOEXEC) < 0)
	{
		::close (fd);
		return i2p::unexpected (std::string ("fcntl(FD_CLOEXEC): ") + strerror (errno));
	}
	uid_t uid = 0;
	gid_t gid = 0;
	if (::getpeereid (fd, &uid, &gid) != 0 || static_cast<uint32_t> (uid) != expectedUid)
	{
		::close (fd);
		return i2p::unexpected ("imsg peer uid is not authorized");
	}
	return Adopt (fd); // imsgbuf_init enables fd passing; fd ownership passes here.
#else
	(void)fd;
	(void)expectedUid;
	return i2p::unexpected ("imsg is available only on OpenBSD");
#endif
}

i2p::expected<void, std::string> ImsgChannel::Send (const Message & message, int fd, int timeoutMs)
{
#if defined(__OpenBSD__)
	if (!IsOpen () || !m_Buffer) return i2p::unexpected ("imsg channel is closed");
	auto encoded = Encode (message);
	if (!encoded) return i2p::unexpected (std::string (encoded.error ().text));
	if (encoded->size () + IMSG_HEADER_SIZE > MAX_IMSGSIZE) return i2p::unexpected ("frame exceeds MAX_IMSGSIZE");
	if (imsg_compose (m_Buffer, 0, message.requestId, getpid (), fd, encoded->data (), encoded->size ()) == -1)
		return i2p::unexpected (std::string ("imsg_compose: ") + strerror (errno));

	const bool infinite = IsInfiniteTimeout (timeoutMs);
	const auto deadline = MakeDeadline (timeoutMs);
	while (imsgbuf_queuelen (m_Buffer) > 0)
	{
		pollfd pfd { m_Fd, POLLOUT, 0 };
		int polled;
		do polled = ::poll (&pfd, 1, PollTimeout (infinite, deadline));
		while (polled < 0 && errno == EINTR && (infinite || !DeadlineExpired (deadline)));
		if (polled < 0) return i2p::unexpected (std::string ("imsg poll: ") + strerror (errno));
		if (polled == 0)
		{
			Close (); // a queued frame must not be retried as a second mutation
			return i2p::unexpected ("imsg send timeout");
		}
		if (pfd.revents & POLLNVAL) return i2p::unexpected ("imsg poll: invalid descriptor");
		if (pfd.revents & (POLLERR | POLLHUP)) return i2p::unexpected ("imsg poll: peer closed the channel");
		if (imsgbuf_write (m_Buffer) == -1)
		{
			if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ENOBUFS)
			{
				if (!infinite && DeadlineExpired (deadline))
				{
					Close ();
					return i2p::unexpected ("imsg send timeout");
				}
				continue;
			}
			return i2p::unexpected (std::string ("imsgbuf_write: ") + strerror (errno));
		}
	}
	return { };
#else
	(void)message;
	(void)fd;
	(void)timeoutMs;
	return i2p::unexpected ("imsg is available only on OpenBSD");
#endif
}

i2p::expected<std::optional<ReceivedMessage>, std::string> ImsgChannel::Receive (int timeoutMs)
{
#if defined(__OpenBSD__)
	if (!IsOpen () || !m_Buffer) return i2p::unexpected ("imsg channel is closed");
	const bool infinite = IsInfiniteTimeout (timeoutMs);
	const auto deadline = MakeDeadline (timeoutMs);
	while (true)
	{
		// imsgbuf_read() drains the socket and may append several complete
		// messages to the user buffer. Always consume that buffer first;
		// otherwise a second frame is stranded until new bytes arrive.
		struct imsg message { };
		int got = imsgbuf_get (m_Buffer, &message);
		if (got == 0)
		{
			pollfd pfd { m_Fd, POLLIN, 0 };
			int polled;
			do polled = ::poll (&pfd, 1, PollTimeout (infinite, deadline));
			while (polled < 0 && errno == EINTR && (infinite || !DeadlineExpired (deadline)));
			if (polled < 0) return i2p::unexpected (std::string ("imsg poll: ") + strerror (errno));
			if (polled == 0) return std::optional<ReceivedMessage> { };
			if (pfd.revents & POLLNVAL) return i2p::unexpected ("imsg poll: invalid descriptor");
			if ((pfd.revents & (POLLERR | POLLHUP)) && !(pfd.revents & POLLIN))
				return i2p::unexpected ("peer closed the imsg channel");

			const int readResult = imsgbuf_read (m_Buffer);
			if (readResult == 0) return i2p::unexpected ("peer closed the imsg channel");
			if (readResult < 0)
			{
				if (errno == EAGAIN || errno == EWOULDBLOCK)
				{
					if (!infinite && DeadlineExpired (deadline)) return std::optional<ReceivedMessage> { };
					continue;
				}
				return i2p::unexpected (std::string ("imsgbuf_read: ") + strerror (errno));
			}
			continue; // re-check the user buffer before polling again
		}
		if (got < 0) return i2p::unexpected (std::string ("imsgbuf_get: ") + strerror (errno));

		const size_t length = imsg_get_len (&message);
		std::string payload (length, '\0');
		if (length > 0 && imsg_get_data (&message, payload.data (), length) != 0)
		{
			imsg_free (&message);
			return i2p::unexpected ("invalid imsg payload");
		}
		int receivedFd = imsg_get_fd (&message);
		imsg_free (&message);
		if (receivedFd >= 0)
		{
			const int flags = fcntl (receivedFd, F_GETFD);
			if (flags < 0 || fcntl (receivedFd, F_SETFD, flags | FD_CLOEXEC) < 0)
			{
				::close (receivedFd);
				return i2p::unexpected (std::string ("fcntl(received FD_CLOEXEC): ") + strerror (errno));
			}
		}
		auto decoded = Decode (payload);
		if (!decoded)
		{
			if (receivedFd >= 0) ::close (receivedFd);
			return i2p::unexpected (std::string ("protocol: ") + std::string (decoded.error ().text));
		}
		return std::optional<ReceivedMessage> (ReceivedMessage { std::move (*decoded), receivedFd });
	}
#else
	(void)timeoutMs;
	return i2p::unexpected ("imsg is available only on OpenBSD");
#endif
}

i2p::expected<PeerCredentials, std::string> ImsgChannel::Credentials () const
{
	if (!IsOpen ()) return i2p::unexpected ("imsg channel is closed");
#if defined(__OpenBSD__)
	uid_t uid = 0;
	gid_t gid = 0;
	if (getpeereid (m_Fd, &uid, &gid) != 0) return i2p::unexpected (std::string ("getpeereid: ") + strerror (errno));
	return PeerCredentials { static_cast<uint32_t> (uid), static_cast<uint32_t> (gid), 0 };
#else
	return i2p::unexpected ("getpeereid is available only on BSD");
#endif
}

i2p::expected<uint32_t, std::string> ImsgChannel::PeerUid () const
{
	auto credentials = Credentials ();
	if (!credentials) return i2p::unexpected (credentials.error ());
	return credentials->uid;
}
}

#endif
