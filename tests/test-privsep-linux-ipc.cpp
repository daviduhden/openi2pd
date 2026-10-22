/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test privsep linux ipc.
 *
 * Linux IPC checks for the private worker channel: SO_PEERCRED, mandatory SCM_CREDENTIALS, single-descriptor SCM_RIGHTS transfer, CLOEXEC and recovery after rejected frames.
 *
 */

#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <dirent.h>
#include <iostream>
#include <fcntl.h>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>

#include "privsep/Channel.h"

using namespace i2p::privsep;

#if !defined(__linux__)
int main ()
{
	return 0;
}
#else
namespace
{
/**
 * Count the descriptors owned by this process. The directory descriptor used
 * for the scan is included in every measurement, so equal values mean equal
 * ownership. Returns -1 when /proc is unavailable; callers then skip the
 * comparison instead of failing on a non-procfs Linux environment.
 */
int CountOpenFds ()
{
	DIR * dir = opendir ("/proc/self/fd");
	if (!dir) return -1;
	int count = 0;
	while (struct dirent * entry = readdir (dir))
	{
		if (entry->d_name[0] != '.') ++count;
	}
	closedir (dir);
	return count;
}

int OpenDescriptor (const char * path, int flags)
{
	const int fd = open (path, flags | O_CLOEXEC);
	assert (fd >= 0);
	return fd;
}

std::string BuildHeader (uint16_t type, uint32_t requestId, uint32_t length)
{
	PayloadWriter writer;
	writer.WriteU32 (PROTOCOL_MAGIC);
	writer.WriteU16 (PROTOCOL_VERSION);
	writer.WriteU16 (type);
	writer.WriteU32 (requestId);
	writer.WriteU32 (length);
	return writer.Data ();
}

/**
 * Send a raw record, optionally with SCM_RIGHTS descriptors. This bypasses
 * Encode() on purpose: the receiver is the component under test.
 */
bool SendRawWithFds (int socketFd, std::string_view frame, const int * descriptors, size_t count)
{
	assert (count <= 4u);
	struct iovec iov { const_cast<char *> (frame.data ()), frame.size () };
	struct msghdr header { };
	header.msg_iov = &iov;
	header.msg_iovlen = 1;
	alignas (struct cmsghdr) char control[CMSG_SPACE (sizeof (int) * 4)] { };
	if (count > 0)
	{
		header.msg_control = control;
		header.msg_controllen = CMSG_SPACE (sizeof (int) * count);
		auto * cmsg = CMSG_FIRSTHDR (&header);
		cmsg->cmsg_level = SOL_SOCKET;
		cmsg->cmsg_type = SCM_RIGHTS;
		cmsg->cmsg_len = CMSG_LEN (sizeof (int) * count);
		memcpy (CMSG_DATA (cmsg), descriptors, sizeof (int) * count);
	}
	ssize_t sent;
	do sent = ::sendmsg (socketFd, &header, MSG_NOSIGNAL);
	while (sent < 0 && errno == EINTR);
	return sent == static_cast<ssize_t> (frame.size ());
}

bool SendRaw (int socketFd, std::string_view frame)
{
	return SendRawWithFds (socketFd, frame, nullptr, 0);
}

/**
 * A rejected record must be fully consumed. The next valid frame has to
 * arrive intact, otherwise a framing error would desynchronise the channel.
 */
void ExpectReusable (Channel & sender, Channel & receiver, uint32_t requestId)
{
	assert (sender.Send (Message { MessageType::Ping, requestId, { } }).has_value ());
	auto received = receiver.Receive (1000);
	assert (received.has_value ());
	assert (received->has_value ());
	assert ((*received)->type == MessageType::Ping);
	assert ((*received)->requestId == requestId);
}

void TestPeerCredentials ()
{
	auto pair = Channel::CreatePair ();
	assert (pair.has_value ());
	auto credentials = pair->first.Credentials ();
	assert (credentials.has_value ());
	assert (credentials->uid == geteuid ());
	assert (credentials->gid == getegid ());
	assert (credentials->pid == static_cast<uint32_t> (getpid ()));

	auto uid = pair->first.PeerUid ();
	assert (uid.has_value ());
	assert (*uid == geteuid ());

	// Both ends describe the same process; SO_PEERCRED must not disagree.
	auto peer = pair->second.Credentials ();
	assert (peer.has_value ());
	assert (peer->uid == credentials->uid);
	assert (peer->gid == credentials->gid);

	// Credentials are unavailable once the local descriptor is gone, rather
	// than silently returning a cached (stale) identity.
	pair->first.Close ();
	assert (!pair->first.Credentials ().has_value ());
	assert (!pair->first.PeerUid ().has_value ());
}

void TestEmptyRecordIsNotEof ()
{
	auto pair = Channel::CreatePair ();
	assert (pair.has_value ());

	// A zero-length SOCK_SEQPACKET record is not EOF. It must be consumed
	// without tearing down the channel or leaking an attached descriptor.
	const int before = CountOpenFds ();
	assert (SendRaw (pair->first.Fd (), ""));
	auto empty = pair->second.ReceiveWithFd (50);
	assert (empty.has_value ());
	assert (!empty->has_value ());
	ExpectReusable (pair->first, pair->second, 201);

	const int fd = OpenDescriptor ("/dev/null", O_RDONLY);
	assert (SendRawWithFds (pair->first.Fd (), "", &fd, 1));
	close (fd);
	auto emptyWithFd = pair->second.ReceiveWithFd (50);
	assert (emptyWithFd.has_value ());
	assert (!emptyWithFd->has_value ());
	if (before >= 0) assert (CountOpenFds () == before);
	ExpectReusable (pair->first, pair->second, 202);

	// A peer that floods empty records must be bounded, not allowed to spin
	// the receive loop for the caller's whole timeout.
	for (int i = 0; i < 9; ++i) assert (SendRaw (pair->first.Fd (), ""));
	auto flood = pair->second.ReceiveWithFd (500);
	assert (!flood.has_value ());
	assert (flood.error ().find ("empty-record flood") != std::string::npos);
}

void TestAdoptValidatedRejectsNonChannels ()
{
	auto pair = Channel::CreatePair ();
	assert (pair.has_value ());
	const uint32_t uid = geteuid ();

	// A regular file, a pipe and a stream socket are never an IPC channel.
	const int regular = OpenDescriptor ("/dev/null", O_RDONLY);
	assert (!Channel::AdoptValidated (regular, uid).has_value ());

	int pipeFds[2] = { -1, -1 };
	assert (::pipe (pipeFds) == 0);
	const int before = CountOpenFds ();
	assert (!Channel::AdoptValidated (pipeFds[0], uid).has_value ());
	if (before >= 0) assert (CountOpenFds () == before - 1); // rejected fd was closed
	::close (pipeFds[1]);

	int streamFds[2] = { -1, -1 };
	assert (::socketpair (AF_UNIX, SOCK_STREAM, 0, streamFds) == 0);
	assert (!Channel::AdoptValidated (streamFds[0], uid).has_value ());
	::close (streamFds[1]);

	// A valid endpoint is adopted with CLOEXEC and the expected peer UID.
	auto validated = Channel::AdoptValidated (pair->first.Release (), uid);
	assert (validated.has_value ());
	assert (fcntl (validated->Fd (), F_GETFD) >= 0);
	assert ((fcntl (validated->Fd (), F_GETFD) & FD_CLOEXEC) != 0);
}

void TestAdoptValidatedEnablesPasscred ()
{
	int fds[2] = { -1, -1 };
	assert (::socketpair (AF_UNIX, SOCK_SEQPACKET, 0, fds) == 0);
	auto receiver = Channel::AdoptValidated (fds[1], geteuid ());
	assert (receiver.has_value ());
	fds[1] = -1;
	Channel sender = Channel::Adopt (fds[0]);
	fds[0] = -1;
	assert (sender.Send (Message { MessageType::Ping, 301, { } }).has_value ());
	auto received = receiver->ReceiveWithFd (1000);
	assert (received.has_value ());
	assert (received->has_value ());
	assert ((*received)->message.requestId == 301);
	sender.Close ();
	receiver->Close ();
}

void TestMissingCredentialsRejected ()
{
	// SO_PASSCRED is what makes SCM_CREDENTIALS mandatory. A socketpair that
	// never enabled it models a peer that sends without credentials: the
	// channel must reject the record instead of accepting unauthenticated
	// input. This is the only credential-negative case reachable without
	// root, because the kernel refuses to let an unprivileged sender forge a
	// different uid/gid in SCM_CREDENTIALS.
	int fds[2] = { -1, -1 };
	assert (::socketpair (AF_UNIX, SOCK_SEQPACKET, 0, fds) == 0);
	Channel receiver = Channel::Adopt (fds[1]);
	fds[1] = -1; // owned by receiver from here on

	auto frame = Encode (Message { MessageType::Ping, 5, { } });
	assert (frame.has_value ());
	assert (SendRaw (fds[0], *frame));

	const int before = CountOpenFds ();
	auto received = receiver.ReceiveWithFd (1000);
	assert (!received.has_value ());
	assert (received.error ().find ("SCM_CREDENTIALS") != std::string::npos);
	if (before >= 0) assert (CountOpenFds () == before);

	::close (fds[0]);
}

void TestDescriptorTransfer ()
{
	auto pair = Channel::CreatePair ();
	assert (pair.has_value ());

	const int before = CountOpenFds ();
	const int fd = OpenDescriptor ("/dev/null", O_RDONLY);
	assert (pair->first.SendWithFd (Message { MessageType::Ping, 11, { } }, fd).has_value ());
	// SCM_RIGHTS duplicates the sender's descriptor; sending does not
	// transfer ownership of the local copy.
	assert (fcntl (fd, F_GETFD) >= 0);
	close (fd);

	auto received = pair->second.ReceiveWithFd (1000);
	assert (received.has_value ());
	assert (received->has_value ());
	assert ((*received)->message.type == MessageType::Ping);
	assert ((*received)->message.requestId == 11);
	assert ((*received)->fd >= 0);
	// The receiver must set FD_CLOEXEC regardless of MSG_CMSG_CLOEXEC support.
	const int descriptorFlags = fcntl ((*received)->fd, F_GETFD);
	assert (descriptorFlags >= 0);
	assert ((descriptorFlags & FD_CLOEXEC) != 0);
	struct stat info { };
	assert (fstat ((*received)->fd, &info) == 0);
	assert (S_ISCHR (info.st_mode));
	close ((*received)->fd);
	if (before >= 0) assert (CountOpenFds () == before);

	// A descriptor is never silently consumed by the plain Receive API.
	const int beforePlain = CountOpenFds ();
	const int secondFd = OpenDescriptor ("/dev/null", O_RDONLY);
	assert (pair->first.SendWithFd (Message { MessageType::Ping, 12, { } }, secondFd).has_value ());
	close (secondFd);
	auto rejected = pair->second.Receive (1000);
	assert (!rejected.has_value ());
	assert (rejected.error ().find ("unexpected SCM_RIGHTS") != std::string::npos);
	// The rejected duplicate must be closed, not leaked.
	if (beforePlain >= 0) assert (CountOpenFds () == beforePlain);

	// The channel remains usable after the rejected descriptor frame.
	ExpectReusable (pair->first, pair->second, 13);
}

void TestMultipleDescriptorRejection ()
{
	auto pair = Channel::CreatePair ();
	assert (pair.has_value ());

	// The public API authorises at most one descriptor per operation. A peer
	// that smuggles two must have both duplicates closed by the receiver.
	int descriptors[2] = { OpenDescriptor ("/dev/null", O_RDONLY), OpenDescriptor ("/dev/null", O_RDONLY) };
	auto frame = Encode (Message { MessageType::Ping, 21, { } });
	assert (frame.has_value ());

	const int before = CountOpenFds ();
	assert (SendRawWithFds (pair->first.Fd (), *frame, descriptors, 2));
	auto rejected = pair->second.ReceiveWithFd (1000);
	assert (!rejected.has_value ());
	// Both received copies must be closed: the sender's two originals are
	// still counted on both sides of the measurement.
	if (before >= 0) assert (CountOpenFds () == before);
	assert (fcntl (descriptors[0], F_GETFD) >= 0);
	assert (fcntl (descriptors[1], F_GETFD) >= 0);
	close (descriptors[0]);
	close (descriptors[1]);

	// Rejecting an ancillary-data record does not poison the channel.
	ExpectReusable (pair->first, pair->second, 22);
}

void TestDescriptorWithMalformedFrame ()
{
	auto pair = Channel::CreatePair ();
	assert (pair.has_value ());

	// The descriptor arrives before Decode() runs. If the frame is invalid,
	// the received duplicate must still be closed; a leak here would survive
	// every malformed-frame flood.
	auto frame = Encode (Message { MessageType::Ping, 41, { } });
	assert (frame.has_value ());
	std::string malformed = *frame;
	malformed[6] = static_cast<char> (0xff); // type becomes 0xffff
	malformed[7] = static_cast<char> (0xff);

	const int fd = OpenDescriptor ("/dev/null", O_RDONLY);
	const int before = CountOpenFds ();
	assert (SendRawWithFds (pair->first.Fd (), malformed, &fd, 1));
	auto rejected = pair->second.ReceiveWithFd (1000);
	assert (!rejected.has_value ());
	if (before >= 0) assert (CountOpenFds () == before);
	// The sender's original is untouched.
	assert (fcntl (fd, F_GETFD) >= 0);
	close (fd);

	ExpectReusable (pair->first, pair->second, 42);
}

void TestMalformedFrames ()
{
	auto pair = Channel::CreatePair ();
	assert (pair.has_value ());
	Channel & sender = pair->first;
	Channel & receiver = pair->second;

	// A maximum frame must be sendable even when the host lowered
	// net.core.wmem_default. The kernel clamps the request to wmem_max, which
	// is still far above one 64 KiB record.
	int sendBuffer = 1 << 20;
	assert (setsockopt (sender.Fd (), SOL_SOCKET, SO_SNDBUF, &sendBuffer, sizeof (sendBuffer)) == 0);

	// A truncated header is its own record on SOCK_SEQPACKET. It must be
	// rejected and must not be merged into the following frame.
	{
		auto valid = Encode (Message { MessageType::LogRecord, 1, "abc" });
		assert (valid.has_value ());
		assert (SendRaw (sender.Fd (), std::string_view (*valid).substr (0, HEADER_SIZE - 1)));
		auto received = receiver.Receive (1000);
		assert (!received.has_value ());
		ExpectReusable (sender, receiver, 101);
	}

	// A declared payload longer than the record really is a framing error
	// (not a size error) and is consumed before the next message.
	{
		auto truncated = Encode (Message { MessageType::LogRecord, 2, "abc" });
		assert (truncated.has_value ());
		assert (SendRaw (sender.Fd (), std::string_view (*truncated).substr (0, truncated->size () - 1)));
		auto received = receiver.Receive (1000);
		assert (!received.has_value ());
		ExpectReusable (sender, receiver, 102);
	}

	// Unknown message types are rejected before payload dispatch.
	{
		assert (SendRaw (sender.Fd (), BuildHeader (7, 3, 0)));
		auto received = receiver.Receive (1000);
		assert (!received.has_value ());
		assert (received.error ().find ("unknown type") != std::string::npos);
		ExpectReusable (sender, receiver, 103);

		assert (SendRaw (sender.Fd (), BuildHeader (0xffff, 4, 0)));
		auto unknown = receiver.Receive (1000);
		assert (!unknown.has_value ());
		assert (unknown.error ().find ("unknown type") != std::string::npos);
		ExpectReusable (sender, receiver, 104);
	}

	// A declared length above the frame limit is a size error even when the
	// record itself is short.
	{
		assert (SendRaw (sender.Fd (), BuildHeader (2, 5, static_cast<uint32_t> (MAX_MESSAGE_SIZE))));
		auto received = receiver.Receive (1000);
		assert (!received.has_value ());
		assert (received.error ().find ("too large") != std::string::npos);
		ExpectReusable (sender, receiver, 105);
	}

	// A record larger than the receive buffer is truncated by the kernel
	// (MSG_TRUNC) and rejected as a whole; the excess bytes must not be
	// delivered as a second record.
	{
		std::string oversized = BuildHeader (2, 6, static_cast<uint32_t> (MAX_MESSAGE_SIZE - HEADER_SIZE));
		oversized.append (MAX_MESSAGE_SIZE - HEADER_SIZE + 128, 'x');
		assert (SendRaw (sender.Fd (), oversized));
		auto received = receiver.Receive (1000);
		assert (!received.has_value ());
		ExpectReusable (sender, receiver, 106);
	}

	// A record that is one byte above the frame limit still fits the
	// receive-buffer headroom, so truncation cannot mask it. The declared
	// length mismatch must be classified as a framing error.
	{
		std::string slightlyOversized = BuildHeader (2, 8, static_cast<uint32_t> (MAX_MESSAGE_SIZE - HEADER_SIZE));
		slightlyOversized.append (MAX_MESSAGE_SIZE - HEADER_SIZE, 'y');
		slightlyOversized.push_back ('z');
		assert (slightlyOversized.size () == MAX_MESSAGE_SIZE + 1);
		assert (SendRaw (sender.Fd (), slightlyOversized));
		auto received = receiver.Receive (1000);
		assert (!received.has_value ());
		assert (received.error ().find ("length mismatch") != std::string::npos);
		ExpectReusable (sender, receiver, 108);
	}

	// An exact maximum frame still fits the receive buffer, and the next
	// frame is read cleanly (no off-by-one in the headroom).
	{
		auto maximum
			= Encode (Message { MessageType::LogRecord, 7, std::string (MAX_MESSAGE_SIZE - HEADER_SIZE, 'm') });
		assert (maximum.has_value ());
		assert (maximum->size () == MAX_MESSAGE_SIZE);
		assert (SendRaw (sender.Fd (), *maximum));
		auto received = receiver.Receive (1000);
		assert (received.has_value ());
		assert (received->has_value ());
		assert ((*received)->requestId == 7);
		assert ((*received)->payload.size () == MAX_MESSAGE_SIZE - HEADER_SIZE);
		// The whole payload must round-trip; a stale or short receive buffer
		// would leave zeroes or the previous record here.
		assert ((*received)->payload == std::string (MAX_MESSAGE_SIZE - HEADER_SIZE, 'm'));
		ExpectReusable (sender, receiver, 107);
	}
}

void TestSendValidation ()
{
	auto pair = Channel::CreatePair ();
	assert (pair.has_value ());

	// A payload above the frame limit is rejected before any record is
	// queued; the next send must not inherit a partial header.
	auto tooLarge = pair->first.Send (Message { MessageType::LogRecord, 31, std::string (MAX_MESSAGE_SIZE, 'x') });
	assert (!tooLarge.has_value ());
	assert (tooLarge.error ().find ("too large") != std::string::npos);
	ExpectReusable (pair->first, pair->second, 32);

	// A closed descriptor number is rejected before sendmsg: nothing is
	// transmitted and the channel remains usable.
	const int fd = OpenDescriptor ("/dev/null", O_RDONLY);
	close (fd);
	auto invalidFd = pair->first.SendWithFd (Message { MessageType::Ping, 33, { } }, fd);
	assert (!invalidFd.has_value ());
	assert (invalidFd.error ().find ("invalid descriptor") != std::string::npos);
	ExpectReusable (pair->first, pair->second, 34);
}

void TestChannelOwnership ()
{
	const int before = CountOpenFds ();
	{
		auto first = Channel::CreatePair ();
		assert (first.has_value ());
		Channel target (std::move (first->first));
		const int replacedFd = target.Fd ();
		assert (replacedFd >= 0);

		// Move assignment must close the previous descriptor exactly once.
		auto second = Channel::CreatePair ();
		assert (second.has_value ());
		target = std::move (second->first);
		assert (fcntl (replacedFd, F_GETFD) == -1);
		assert (errno == EBADF);
		assert (target.IsOpen ());
		assert (second->first.Fd () == -1);
		second->first.Close (); // closing a moved-from channel is a no-op

		// Close is idempotent and must not double-close a reused number.
		const int closedFd = target.Fd ();
		assert (closedFd >= 0);
		target.Close ();
		target.Close ();
		assert (!target.IsOpen ());
		assert (fcntl (closedFd, F_GETFD) == -1);
		assert (errno == EBADF);
	}
	if (before >= 0) assert (CountOpenFds () == before);
}

void TestClosedChannel ()
{
	auto pair = Channel::CreatePair ();
	assert (pair.has_value ());
	pair->first.Close ();
	auto sent = pair->first.SendWithFd (Message { MessageType::Ping, 1, { } }, -1);
	assert (!sent.has_value ());

	// Peer close is an error, never an empty timeout: a worker that treats
	// EOF as a timeout would spin forever.
	auto received = pair->second.ReceiveWithFd (100);
	assert (!received.has_value ());
	assert (received.error ().find ("closed") != std::string::npos);

	// Repeated reads keep reporting the same terminal condition instead of
	// blocking or resurrecting the channel.
	for (int attempt = 0; attempt < 3; ++attempt)
	{
		auto again = pair->second.Receive (100);
		assert (!again.has_value ());
		assert (again.error ().find ("closed") != std::string::npos);
	}

	// A locally closed channel reports a local error rather than a timeout.
	auto local = Channel::CreatePair ();
	assert (local.has_value ());
	local->second.Close ();
	auto localReceive = local->second.ReceiveWithFd (100);
	assert (!localReceive.has_value ());
	assert (localReceive.error ().find ("closed") != std::string::npos);
	assert (!local->second.Send (Message { MessageType::Ping, 2, { } }).has_value ());
}

void TestSendDeadlineAfterDrain ()
{
	// A poll(POLLOUT) result can become stale before sendmsg() runs: another
	// writer, or the consumer side of the advertised space, can take the slot
	// in between. The timeout must still hold, and freeing exactly one record
	// slot must let a bounded send proceed without spinning or blocking.
	auto pair = Channel::CreatePair ();
	assert (pair.has_value ());
	int sendBuffer = 4096;
	assert (setsockopt (pair->first.Fd (), SOL_SOCKET, SO_SNDBUF, &sendBuffer, sizeof (sendBuffer)) == 0);

	const std::string payload (512 - HEADER_SIZE, 'r');
	int queued = 0;
	for (; queued < 1000; ++queued)
	{
		auto frame = Encode (Message { MessageType::LogRecord, static_cast<uint32_t> (queued), payload });
		assert (frame.has_value ());
		const ssize_t sent = ::send (pair->first.Fd (), frame->data (), frame->size (), MSG_DONTWAIT | MSG_NOSIGNAL);
		if (sent < 0)
		{
			assert (errno == EAGAIN || errno == EWOULDBLOCK);
			break;
		}
		assert (sent == static_cast<ssize_t> (frame->size ()));
	}
	assert (queued > 0);
	assert (queued < 1000);

	// Release one slot, leaving residual queue space smaller than the whole
	// socket buffer. A sender that ignores EAGAIN after a stale poll would
	// block here instead of respecting its deadline.
	auto first = pair->second.Receive (1000);
	assert (first.has_value ());
	assert (first->has_value ());
	assert ((*first)->requestId == 0);
	first->reset ();

	const auto started = std::chrono::steady_clock::now ();
	const std::string resumedPayload (64, 's');
	auto sent = pair->first.Send (Message { MessageType::LogRecord, 1000, resumedPayload }, 1000);
	const auto elapsed = std::chrono::steady_clock::now () - started;
	if (!sent)
		std::cerr << "post-drain send failed after " << queued << " queued frames: " << sent.error () << std::endl;
	assert (sent.has_value ());
	assert (elapsed < std::chrono::seconds (2));

	for (int index = 1; index < queued; ++index)
	{
		auto received = pair->second.Receive (1000);
		assert (received.has_value ());
		assert (received->has_value ());
		assert ((*received)->requestId == static_cast<uint32_t> (index));
		assert ((*received)->payload == payload);
	}
	auto final = pair->second.Receive (1000);
	assert (final.has_value ());
	assert (final->has_value ());
	assert ((*final)->requestId == 1000);
	assert ((*final)->payload == resumedPayload);

	// An empty queue is still governed by the same absolute deadline.
	const auto emptyStarted = std::chrono::steady_clock::now ();
	auto empty = pair->second.Receive (50);
	const auto emptyElapsed = std::chrono::steady_clock::now () - emptyStarted;
	assert (empty.has_value ());
	assert (!empty->has_value ());
	assert (emptyElapsed < std::chrono::seconds (2));
}

void TestReceiveDeadline ()
{
	auto pair = Channel::CreatePair ();
	assert (pair.has_value ());

	const auto started = std::chrono::steady_clock::now ();
	auto empty = pair->second.ReceiveWithFd (50);
	const auto elapsed = std::chrono::steady_clock::now () - started;
	assert (empty.has_value ());
	assert (!empty->has_value ());
	assert (elapsed < std::chrono::seconds (2));

	// A zero timeout performs one non-blocking readiness check.
	const auto zeroStarted = std::chrono::steady_clock::now ();
	auto zero = pair->second.ReceiveWithFd (0);
	const auto zeroElapsed = std::chrono::steady_clock::now () - zeroStarted;
	assert (zero.has_value ());
	assert (!zero->has_value ());
	assert (zeroElapsed < std::chrono::seconds (1));
}

void TestSendQueueSaturation ()
{
	// Simulate a wedged peer without any network: shrink the sender's socket
	// buffer, queue records until the kernel refuses more, then check that a
	// bounded Channel::Send fails promptly instead of blocking forever. This
	// models backpressure; it does not model a worker that is slow to parse.
	auto pair = Channel::CreatePair ();
	assert (pair.has_value ());
	int sendBuffer = 4096;
	assert (setsockopt (pair->first.Fd (), SOL_SOCKET, SO_SNDBUF, &sendBuffer, sizeof (sendBuffer)) == 0);

	const std::string payload (512 - HEADER_SIZE, 'q');
	int queued = 0;
	for (; queued < 1000; ++queued)
	{
		auto frame = Encode (Message { MessageType::LogRecord, static_cast<uint32_t> (queued), payload });
		assert (frame.has_value ());
		const ssize_t sent = ::send (pair->first.Fd (), frame->data (), frame->size (), MSG_DONTWAIT | MSG_NOSIGNAL);
		if (sent < 0)
		{
			assert (errno == EAGAIN || errno == EWOULDBLOCK);
			break;
		}
		assert (sent == static_cast<ssize_t> (frame->size ()));
	}
	assert (queued > 0);
	assert (queued < 1000);

	const auto started = std::chrono::steady_clock::now ();
	auto blocked = pair->first.Send (Message { MessageType::LogRecord, 999, payload }, 50);
	const auto elapsed = std::chrono::steady_clock::now () - started;
	assert (!blocked.has_value ());
	// Either poll times out or sendmsg returns EAGAIN; both are bounded.
	assert (elapsed < std::chrono::seconds (2));

	// Drain exactly the records that were accepted. Every record must decode
	// and arrive in order, proving the failed send did not corrupt framing.
	for (int index = 0; index < queued; ++index)
	{
		auto received = pair->second.Receive (1000);
		assert (received.has_value ());
		assert (received->has_value ());
		assert ((*received)->requestId == static_cast<uint32_t> (index));
		// The failed send must not have left a partial/duplicated record.
		assert ((*received)->payload == payload);
	}

	// Space is available again; the channel is not permanently wedged.
	assert (pair->first.Send (Message { MessageType::Ping, 1000, { } }, 1000).has_value ());
	auto final = pair->second.Receive (1000);
	assert (final.has_value ());
	assert (final->has_value ());
	assert ((*final)->requestId == 1000);
}
}

int main ()
{
	TestPeerCredentials ();
	TestEmptyRecordIsNotEof ();
	TestAdoptValidatedRejectsNonChannels ();
	TestAdoptValidatedEnablesPasscred ();
	TestMissingCredentialsRejected ();
	TestDescriptorTransfer ();
	TestMultipleDescriptorRejection ();
	TestDescriptorWithMalformedFrame ();
	TestMalformedFrames ();
	TestSendValidation ();
	TestChannelOwnership ();
	TestClosedChannel ();
	TestSendDeadlineAfterDrain ();
	TestReceiveDeadline ();
	TestSendQueueSaturation ();
	return 0;
}
#endif
