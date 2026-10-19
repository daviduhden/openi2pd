/*
 * Copyright (c) 2026, David Uhden Collado
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * OpenBSD imsg adapter smoke test, including one SCM_RIGHTS descriptor and
 * peer credentials. It is compiled and executed only by the manual OpenBSD
 * CI job; local review does not claim a runtime result.
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
	auto left = ImsgChannel::Adopt (fds[0]);
	auto right = ImsgChannel::Adopt (fds[1]);
	assert (left.has_value ());
	assert (right.has_value ());

	auto uid = left->PeerUid ();
	assert (uid.has_value ());
	assert (*uid == geteuid ());

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
