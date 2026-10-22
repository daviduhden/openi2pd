/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Channel.
 *
 * Implements the UNIX seqpacket channel: atomic framed sends and receives
 * with absolute deadlines, peer credential checks and at most one
 * SCM_RIGHTS descriptor per message. Endpoints are close-on-exec and Linux
 * validates credentials on every delivered record.
 *
 */

#include "Channel.h"

#ifndef _WIN32

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/un.h>
#endif

#ifndef MSG_DONTWAIT
// Every supported target provides MSG_DONTWAIT. Keep the build alive on an
// exotic UNIX whose headers omit it; the deadline is then enforced by poll()
// plus the socket's own send/receive timeout only.
#define MSG_DONTWAIT 0
#endif

namespace i2p::privsep
{
namespace
{
	using SteadyClock = std::chrono::steady_clock;

	/** A negative timeout keeps the historical infinite-wait meaning. */
	bool IsInfiniteTimeout (int timeoutMs)
	{
		return timeoutMs < 0;
	}

	SteadyClock::time_point MakeDeadline (int timeoutMs)
	{
		return SteadyClock::now () + std::chrono::milliseconds (timeoutMs);
	}

	bool DeadlineExpired (SteadyClock::time_point deadline)
	{
		return SteadyClock::now () >= deadline;
	}

	int PollTimeout (bool infinite, SteadyClock::time_point deadline)
	{
		if (infinite) return -1;
		const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds> (deadline - SteadyClock::now ());
		if (remaining.count () <= 0) return 0;
		return static_cast<int> (std::min<int64_t> (remaining.count (), std::numeric_limits<int>::max ()));
	}
}

Channel::~Channel ()
{
	Close ();
}

Channel & Channel::operator= (Channel && other) noexcept
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

i2p::expected<Channel, std::string> Channel::AdoptValidated (int fd, uint32_t expectedUid)
{
	if (fd < 0) return i2p::unexpected ("invalid channel descriptor");
	struct stat info { };
	int socketType = 0;
	socklen_t socketTypeLength = sizeof (socketType);
	if (::fstat (fd, &info) != 0 || !S_ISSOCK (info.st_mode)
		|| ::getsockopt (fd, SOL_SOCKET, SO_TYPE, &socketType, &socketTypeLength) != 0
		|| socketType != SOCK_SEQPACKET)
	{
		::close (fd);
		return i2p::unexpected ("adopted descriptor is not an AF_UNIX SOCK_SEQPACKET socket");
	}
#ifdef SO_DOMAIN
	int domain = 0;
	socklen_t domainLength = sizeof (domain);
	if (::getsockopt (fd, SOL_SOCKET, SO_DOMAIN, &domain, &domainLength) != 0 || domain != AF_UNIX)
	{
		::close (fd);
		return i2p::unexpected ("adopted descriptor is not AF_UNIX");
	}
#endif
#ifdef SO_ACCEPTCONN
	int accepting = 0;
	socklen_t acceptingLength = sizeof (accepting);
	if (::getsockopt (fd, SOL_SOCKET, SO_ACCEPTCONN, &accepting, &acceptingLength) == 0 && accepting)
	{
		::close (fd);
		return i2p::unexpected ("adopted descriptor is a listening socket");
	}
#endif
#if defined(__linux__)
	int passcred = 0;
	socklen_t passcredLength = sizeof (passcred);
	if (::getsockopt (fd, SOL_SOCKET, SO_PASSCRED, &passcred, &passcredLength) != 0)
	{
		::close (fd);
		return i2p::unexpected (std::string ("getsockopt(SO_PASSCRED): ") + strerror (errno));
	}
	if (!passcred)
	{
		const int enable = 1;
		if (::setsockopt (fd, SOL_SOCKET, SO_PASSCRED, &enable, sizeof (enable)) != 0)
		{
			::close (fd);
			return i2p::unexpected (std::string ("setsockopt(SO_PASSCRED): ") + strerror (errno));
		}
	}
#endif
	const int flags = ::fcntl (fd, F_GETFD);
	if (flags < 0 || ::fcntl (fd, F_SETFD, flags | FD_CLOEXEC) < 0)
	{
		::close (fd);
		return i2p::unexpected (std::string ("fcntl(FD_CLOEXEC): ") + strerror (errno));
	}
	Channel channel (fd);
	auto uid = channel.PeerUid ();
	if (!uid || *uid != expectedUid)
	{
		channel.Close ();
		return i2p::unexpected ("adopted channel peer has an unexpected uid");
	}
	return channel;
}

i2p::expected<std::pair<Channel, Channel>, std::string> Channel::CreatePair ()
{
	int fds[2] = { -1, -1 };
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
#if defined(__linux__)
		// Ask the kernel to attach SCM_CREDENTIALS to every received message.
		// This complements SO_PEERCRED: the latter identifies the connected
		// peer, while the former is checked per delivered frame.
		int enable = 1;
		if (setsockopt (fd, SOL_SOCKET, SO_PASSCRED, &enable, sizeof (enable)) != 0)
		{
			const int error = errno;
			::close (fds[0]);
			::close (fds[1]);
			return i2p::unexpected (std::string ("setsockopt(SO_PASSCRED): ") + strerror (error));
		}
#endif
	}
	return std::pair<Channel, Channel> (Channel (fds[0]), Channel (fds[1]));
}

i2p::expected<void, std::string> Channel::Send (const Message & message, int timeoutMs)
{
	return SendWithFd (message, -1, timeoutMs);
}

i2p::expected<void, std::string> Channel::SendWithFd (const Message & message, int fd, int timeoutMs)
{
	if (!IsOpen ()) return i2p::unexpected ("channel is closed");
	if (fd >= 0 && fcntl (fd, F_GETFD) < 0)
		return i2p::unexpected (std::string ("invalid descriptor: ") + strerror (errno));
	auto encoded = Encode (message);
	if (!encoded) return i2p::unexpected (std::string (encoded.error ().text));

	// SOCK_SEQPACKET is atomic: one sendmsg either queues the whole record or
	// fails with EAGAIN. A poll(POLLOUT) followed by a blocking sendmsg is NOT
	// bounded, because another writer (or a consumer of the advertised space)
	// can take the free slot between the two calls. Send with MSG_DONTWAIT and
	// retry until one absolute deadline, so the timeout always holds even on
	// BSD-derived sockets that report readiness conservatively.
	const bool infinite = IsInfiniteTimeout (timeoutMs);
	const auto deadline = MakeDeadline (timeoutMs);
	while (true)
	{
		pollfd pfd { m_Fd, POLLOUT, 0 };
		int result;
		do result = ::poll (&pfd, 1, PollTimeout (infinite, deadline));
		while (result < 0 && errno == EINTR && (infinite || !DeadlineExpired (deadline)));
		if (result < 0)
		{
			const int error = errno;
			if (error == EINTR) return i2p::unexpected ("send timeout");
			return i2p::unexpected (std::string ("poll: ") + strerror (error));
		}
		if (result == 0) return i2p::unexpected ("send timeout");
		if (pfd.revents & POLLNVAL) return i2p::unexpected ("poll: invalid descriptor");
		if (pfd.revents & (POLLERR | POLLHUP)) return i2p::unexpected ("sendmsg: peer closed the channel");

		struct iovec iov { encoded->data (), encoded->size () };
		struct msghdr header { };
		header.msg_iov = &iov;
		header.msg_iovlen = 1;
		alignas (struct cmsghdr) char control[CMSG_SPACE (sizeof (int))] { };
		if (fd >= 0)
		{
			header.msg_control = control;
			header.msg_controllen = sizeof (control);
			auto * cmsg = CMSG_FIRSTHDR (&header);
			cmsg->cmsg_level = SOL_SOCKET;
			cmsg->cmsg_type = SCM_RIGHTS;
			cmsg->cmsg_len = CMSG_LEN (sizeof (int));
			memcpy (CMSG_DATA (cmsg), &fd, sizeof (fd));
		}
		ssize_t sent;
		do sent = ::sendmsg (m_Fd, &header, MSG_NOSIGNAL | MSG_DONTWAIT);
		while (sent < 0 && errno == EINTR && (infinite || !DeadlineExpired (deadline)));
		if (sent < 0)
		{
			const int error = errno;
			if (error == EINTR) return i2p::unexpected ("send timeout");
			if (error == EAGAIN || error == EWOULDBLOCK)
			{
				if (!infinite && DeadlineExpired (deadline)) return i2p::unexpected ("send timeout");
				continue; // another writer claimed the slot after poll()
			}
			return i2p::unexpected (std::string ("sendmsg: ") + strerror (error));
		}
		// SOCK_SEQPACKET is all-or-nothing. Treat anything else as an error
		// rather than waiting for a continuation that cannot exist.
		if (sent != static_cast<ssize_t> (encoded->size ())) return i2p::unexpected ("sendmsg: short write");
		return { };
	}
}

i2p::expected<std::optional<Message>, std::string> Channel::Receive (int timeoutMs)
{
	auto received = ReceiveWithFd (timeoutMs);
	if (!received) return i2p::unexpected (received.error ());
	if (!received->has_value ()) return std::optional<Message> { };
	if ((*received)->fd >= 0)
	{
		::close ((*received)->fd);
		return i2p::unexpected ("unexpected SCM_RIGHTS descriptor");
	}
	return std::optional<Message> (std::move ((*received)->message));
}

i2p::expected<std::optional<ReceivedMessage>, std::string> Channel::ReceiveWithFd (int timeoutMs)
{
	if (!IsOpen ()) return i2p::unexpected ("channel is closed");
	// A SOCK_SEQPACKET record is delivered to exactly one reader. Concurrent
	// Receive/ReceiveWithFd callers therefore steal records from each other;
	// every endpoint must have a single reader (see the header contract).
	const bool infinite = IsInfiniteTimeout (timeoutMs);
	const auto deadline = MakeDeadline (timeoutMs);

	// Deliberately one buffer slot larger than a maximum frame so a
	// truncated SOCK_SEQPACKET record can never look like a valid frame.
	std::string frame (HEADER_SIZE + MAX_MESSAGE_SIZE, '\0');
	alignas (struct cmsghdr) char control[256] { };
	int flags = MSG_DONTWAIT;
#ifdef MSG_CMSG_CLOEXEC
	flags |= MSG_CMSG_CLOEXEC;
#endif
	struct msghdr header { };
	ssize_t received = -1;
	short readyEvents = 0;
	unsigned emptyRecords = 0;
	while (true)
	{
		short events = POLLIN;
#ifdef POLLRDHUP
		events |= POLLRDHUP;
#endif
		pollfd pfd { m_Fd, events, 0 };
		int result;
		do result = ::poll (&pfd, 1, PollTimeout (infinite, deadline));
		while (result < 0 && errno == EINTR && (infinite || !DeadlineExpired (deadline)));
		if (result < 0)
		{
			const int error = errno;
			if (error == EINTR) return std::optional<ReceivedMessage> { };
			return i2p::unexpected (std::string ("poll: ") + strerror (error));
		}
		if (result == 0) return std::optional<ReceivedMessage> { };
		if (pfd.revents & POLLNVAL) return i2p::unexpected ("poll: invalid descriptor");
		readyEvents = pfd.revents;
		// Drain buffered records before reporting a hangup: POLLIN and
		// POLLHUP may arrive together when the peer has written and closed.
		if ((readyEvents & (POLLERR | POLLHUP
#ifdef POLLRDHUP
			| POLLRDHUP
#endif
			)) && !(readyEvents & POLLIN))
			return i2p::unexpected ("peer closed the channel");

		struct iovec iov { frame.data (), frame.size () };
		header = { };
		header.msg_iov = &iov;
		header.msg_iovlen = 1;
		header.msg_control = control;
		header.msg_controllen = sizeof (control);
		do received = ::recvmsg (m_Fd, &header, flags);
		while (received < 0 && errno == EINTR && (infinite || !DeadlineExpired (deadline)));
		if (received < 0)
		{
			const int error = errno;
			if (error == EINTR) return std::optional<ReceivedMessage> { };
			if (error == EAGAIN || error == EWOULDBLOCK)
			{
				if (!infinite && DeadlineExpired (deadline)) return std::optional<ReceivedMessage> { };
				continue; // another reader consumed the ready record
			}
			return i2p::unexpected (std::string ("recvmsg: ") + strerror (error));
		}
		break;
	}

	// Parse ancillary data before treating the record as accepted. A
	// truncated or duplicated descriptor set is a protocol violation; close
	// every descriptor the kernel delivered so it cannot leak.
	int receivedFd = -1;
#if defined(__linux__)
	bool receivedCredentials = false;
	struct ucred messageCredentials { };
#endif
	bool ancillaryError = false;
	if (header.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) ancillaryError = true;
	for (auto * cmsg = CMSG_FIRSTHDR (&header); cmsg; cmsg = CMSG_NXTHDR (&header, cmsg))
	{
		if (cmsg->cmsg_level != SOL_SOCKET) continue;
		if (cmsg->cmsg_type == SCM_RIGHTS)
		{
			// The public Channel API permits at most one descriptor per
			// operation. Close every descriptor in an oversized/duplicated
			// control message before rejecting the whole record.
			if (cmsg->cmsg_len < CMSG_LEN (0))
			{
				ancillaryError = true;
				continue;
			}
			const size_t dataLen = cmsg->cmsg_len - CMSG_LEN (0);
			const size_t count = dataLen / sizeof (int);
			if (count != 1 || (dataLen % sizeof (int)) != 0 || receivedFd >= 0)
			{
				for (size_t i = 0; i < count; ++i)
				{
					int descriptor = -1;
					memcpy (&descriptor, CMSG_DATA (cmsg) + i * sizeof (int), sizeof (descriptor));
					if (descriptor >= 0) ::close (descriptor);
				}
				ancillaryError = true;
				continue;
			}
			int descriptor = -1;
			memcpy (&descriptor, CMSG_DATA (cmsg), sizeof (descriptor));
			receivedFd = descriptor;
		}
#if defined(__linux__)
		else if (cmsg->cmsg_type == SCM_CREDENTIALS && cmsg->cmsg_len >= CMSG_LEN (sizeof (struct ucred)))
		{
			memcpy (&messageCredentials, CMSG_DATA (cmsg), sizeof (messageCredentials));
			receivedCredentials = true;
		}
#endif
	}
	if (ancillaryError)
	{
		if (receivedFd >= 0) ::close (receivedFd);
		return i2p::unexpected ("invalid ancillary data");
	}
	if (received == 0)
	{
		if (receivedFd >= 0)
		{
			::close (receivedFd);
			return i2p::unexpected ("descriptor on an empty record");
		}
		// A zero-length record is still a peer send and must satisfy the same
		// per-record credential rule as a data frame.
#if defined(__linux__)
		if (!receivedCredentials) return i2p::unexpected ("missing SCM_CREDENTIALS on an empty record");
		auto emptyPeer = Credentials ();
		if (!emptyPeer) return i2p::unexpected (emptyPeer.error ());
		if (messageCredentials.uid != emptyPeer->uid || messageCredentials.gid != emptyPeer->gid)
			return i2p::unexpected ("empty record failed SCM_CREDENTIALS");
#endif
		// SOCK_SEQPACKET delivers a zero-length datagram as recvmsg()==0,
		// exactly like EOF. Only a hangup/error event makes this terminal.
		if (readyEvents & (POLLERR | POLLHUP
#ifdef POLLRDHUP
			| POLLRDHUP
#endif
			)) return i2p::unexpected ("peer closed the channel");
		if (++emptyRecords > 8) return i2p::unexpected ("empty-record flood");
		continue; // consume the empty record and keep the channel usable
	}
	if (receivedFd >= 0)
	{
		// MSG_CMSG_CLOEXEC is not available on every supported UNIX. Do not
		// rely on the sender; apply FD_CLOEXEC to every received descriptor.
		const int descriptorFlags = fcntl (receivedFd, F_GETFD);
		if (descriptorFlags < 0 || fcntl (receivedFd, F_SETFD, descriptorFlags | FD_CLOEXEC) < 0)
		{
			::close (receivedFd);
			return i2p::unexpected (std::string ("fcntl(received FD_CLOEXEC): ") + strerror (errno));
		}
	}
	frame.resize (static_cast<size_t> (received));
	auto message = Decode (frame);
	if (!message)
	{
		if (receivedFd >= 0) ::close (receivedFd);
		return i2p::unexpected (std::string ("protocol: ") + std::string (message.error ().text));
	}
#if defined(__linux__)
	// SO_PASSCRED makes SCM_CREDENTIALS mandatory on every received record.
	// SO_PEERCRED identifies the socket creator, but its PID is fixed before
	// fork+exec; the worker's SCM_CREDENTIALS PID is the child PID and may
	// legitimately differ. Compare the stable uid/gid, not the PID.
	if (!receivedCredentials)
	{
		if (receivedFd >= 0) ::close (receivedFd);
		return i2p::unexpected ("missing SCM_CREDENTIALS");
	}
	auto peer = Credentials ();
	if (!peer)
	{
		if (receivedFd >= 0) ::close (receivedFd);
		return i2p::unexpected (peer.error ());
	}
	if (messageCredentials.uid != peer->uid || messageCredentials.gid != peer->gid)
	{
		if (receivedFd >= 0) ::close (receivedFd);
		return i2p::unexpected ("SCM_CREDENTIALS do not match peer");
	}
#endif
	return std::optional<ReceivedMessage> (ReceivedMessage { std::move (*message), receivedFd });
}

i2p::expected<PeerCredentials, std::string> Channel::Credentials () const
{
	if (!IsOpen ()) return i2p::unexpected ("channel is closed");
#if defined(__linux__)
	struct ucred cred { };
	socklen_t length = sizeof (cred);
	if (getsockopt (m_Fd, SOL_SOCKET, SO_PEERCRED, &cred, &length) != 0)
		return i2p::unexpected (std::string ("SO_PEERCRED: ") + strerror (errno));
	return PeerCredentials { cred.uid, cred.gid, static_cast<uint32_t> (cred.pid) };
#elif defined(__OpenBSD__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__APPLE__)
	uid_t uid = 0;
	gid_t gid = 0;
	if (getpeereid (m_Fd, &uid, &gid) != 0) return i2p::unexpected (std::string ("getpeereid: ") + strerror (errno));
	return PeerCredentials { static_cast<uint32_t> (uid), static_cast<uint32_t> (gid), 0 };
#else
	return i2p::unexpected ("peer credentials are not available on this platform");
#endif
}

i2p::expected<uint32_t, std::string> Channel::PeerUid () const
{
	auto credentials = Credentials ();
	if (!credentials) return i2p::unexpected (credentials.error ());
	return credentials->uid;
}
}

#endif
