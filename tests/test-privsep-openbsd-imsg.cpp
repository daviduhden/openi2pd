/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test privsep openbsd imsg.
 *
 * Checks the OpenBSD imsg transport against the versioned frame allowlist and payload decoding, including rejection of unknown types.
 *
 */

#include <cassert>
#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "privsep/OpenBSDImsg.h"

using namespace i2p::privsep;

#if !defined(__OpenBSD__)
int main ()
{
	return 0;
}
#else
int main ()
{
	int fds[2] = { -1, -1 };
	assert (socketpair (AF_UNIX, SOCK_STREAM, 0, fds) == 0);
	auto left = ImsgChannel::AdoptValidated (fds[0], geteuid ());
	auto right = ImsgChannel::Adopt (fds[1]);
	assert (left.has_value ());
	assert (right.has_value ());

	auto uid = left->PeerUid ();
	assert (uid.has_value ());
	assert (*uid == geteuid ());

	// A validated adoption rejects a wrong peer uid and a non-socket.
	{
		int wrong[2] = { -1, -1 };
		assert (socketpair (AF_UNIX, SOCK_STREAM, 0, wrong) == 0);
		assert (!ImsgChannel::AdoptValidated (wrong[0], geteuid () + 1).has_value ());
		close (wrong[1]);
		const int regular = open ("/dev/null", O_RDONLY | O_CLOEXEC);
		assert (regular >= 0);
		assert (!ImsgChannel::AdoptValidated (regular, geteuid ()).has_value ());
	}

	// A failed oversized compose keeps caller descriptor ownership; storage
	// callers must be able to close their own descriptor exactly once.
	{
		int local[2] = { -1, -1 };
		assert (socketpair (AF_UNIX, SOCK_STREAM, 0, local) == 0);
		auto channel = ImsgChannel::AdoptValidated (local[0], geteuid ());
		assert (channel.has_value ());
		const int fd = open ("/dev/null", O_RDONLY | O_CLOEXEC);
		assert (fd >= 0);
		std::string payload (MAX_IMSGSIZE + 1, 'x');
		auto sent = channel->Send (Message { MessageType::LogRecord, 7, payload }, fd);
		assert (!sent.has_value ());
		assert (fcntl (fd, F_GETFD) >= 0);
		close (fd);
		close (local[1]);
		channel->Close ();
	}

	assert (left->Send (Message { MessageType::Ping, 7, "hello" }).has_value ());
	auto received = right->Receive (1000);
	assert (received.has_value ());
	assert (received->has_value ());
	assert ((*received)->message.type == MessageType::Ping);
	assert ((*received)->message.requestId == 7);
	assert ((*received)->message.payload == "hello");
	assert ((*received)->fd < 0);

	// The imsg transport does not bypass the versioned frame's type allowlist.
	assert (left->Send (Message { static_cast<MessageType> (0xffff), 9, { } }).has_value ());
	auto malformed = right->Receive (1000);
	assert (!malformed.has_value ());

	// imsgbuf_read may buffer several complete messages in one read. They must
	// all be consumed from the user buffer before polling again.
	assert (left->Send (Message { MessageType::Ping, 20, { } }).has_value ());
	assert (left->Send (Message { MessageType::Ping, 21, { } }).has_value ());
	assert (left->Send (Message { MessageType::Ping, 22, { } }).has_value ());
	for (uint32_t id : { 20u, 21u, 22u })
	{
		auto buffered = right->Receive (200);
		assert (buffered.has_value ());
		assert (buffered->has_value ());
		assert ((*buffered)->message.type == MessageType::Ping);
		assert ((*buffered)->message.requestId == id);
	}

	const int fd = open ("/dev/null", O_RDONLY | O_CLOEXEC);
	assert (fd >= 0);
	// imsg_compose(3) closes a successfully composed descriptor in the sender.
	assert (left->Send (Message { MessageType::Shutdown, 8, { } }, fd).has_value ());
	auto withFd = right->Receive (1000);
	assert (withFd.has_value ());
	assert (withFd->has_value ());
	assert ((*withFd)->message.type == MessageType::Shutdown);
	assert ((*withFd)->fd >= 0);
	struct stat info { };
	assert (fstat ((*withFd)->fd, &info) == 0);
	assert (S_ISCHR (info.st_mode));
	close ((*withFd)->fd);
	return 0;
}
#endif
