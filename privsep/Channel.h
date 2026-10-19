/*
* Copyright (c) 2026, David Uhden Collado
* SPDX-License-Identifier: BSD-3-Clause
*
* This file is part of openi2pd and licensed under BSD-3-Clause
*/

#ifndef I2PD_PRIVSEP_CHANNEL_H__
#define I2PD_PRIVSEP_CHANNEL_H__

#include "Expected.h"
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "Protocol.h"

namespace i2p::privsep
{
struct PeerCredentials
{
	uint32_t uid = 0;
	uint32_t gid = 0;
	uint32_t pid = 0;
};

struct ReceivedMessage
{
	Message message;
	int fd = -1; // SCM_RIGHTS descriptor, owned by the receiver
};

class Channel
{
public:
	Channel () = default;
	explicit Channel (int fd)
		: m_Fd (fd)
	{
	}
	~Channel ();
	Channel (Channel && other) noexcept
		: m_Fd (other.m_Fd)
	{
		other.m_Fd = -1;
	}
	Channel & operator= (Channel && other) noexcept;
	Channel (const Channel &) = delete;
	Channel & operator= (const Channel &) = delete;

	static i2p::expected<std::pair<Channel, Channel>, std::string> CreatePair ();
	static Channel Adopt (int fd) { return Channel (fd); }

	bool IsOpen () const { return m_Fd >= 0; }
	int Fd () const { return m_Fd; }
	void Close ();

	i2p::expected<void, std::string> Send (const Message & message, int timeoutMs = 5000);
	/**
	 * Send one message with at most one SCM_RIGHTS descriptor. The descriptor
	 * is an explicitly authorized side channel; the protocol payload remains
	 * unchanged. Callers must only pass descriptors already covered by the
	 * operation policy (for example a pre-opened log or storage file).
	 *
	 * The send is atomic (SOCK_SEQPACKET) and bounded by @a timeoutMs even if
	 * the peer drains the queue concurrently: the implementation polls with
	 * an absolute deadline and retries EAGAIN using MSG_DONTWAIT. A negative
	 * timeout keeps the historical infinite-wait meaning.
	 */
	i2p::expected<void, std::string> SendWithFd (const Message & message, int fd, int timeoutMs = 5000);
	i2p::expected<std::optional<Message>, std::string> Receive (int timeoutMs = 1000);
	/**
	 * Receive one frame and optionally one descriptor. Any SCM_RIGHTS
	 * descriptor is returned to the caller and must be closed by it. More
	 * than one descriptor is a protocol error. On Linux the per-message
	 * SCM_CREDENTIALS are checked against SO_PEERCRED before the frame is
	 * accepted.
	 *
	 * Concurrency contract: each endpoint must have exactly one reader.
	 * SOCK_SEQPACKET delivers every record to exactly one recvmsg, so a
	 * second concurrent Receive/ReceiveWithFd caller would steal frames from
	 * the first instead of duplicating them. Send is atomic per record, but
	 * callers that require ordering must serialize their writers as well.
	 *
	 * The receive is bounded by @a timeoutMs. A ready record can still be
	 * taken by another reader between poll() and recvmsg(); the resulting
	 * EAGAIN is retried until the absolute deadline rather than blocking.
	 * A negative timeout keeps the historical infinite-wait meaning.
	 */
	i2p::expected<std::optional<ReceivedMessage>, std::string> ReceiveWithFd (int timeoutMs = 1000);
	i2p::expected<uint32_t, std::string> PeerUid () const;
	i2p::expected<PeerCredentials, std::string> Credentials () const;

private:
	int m_Fd = -1;
};
}

#endif
