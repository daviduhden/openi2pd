/*
* Copyright (c) 2026, The PurpleI2P Project
* Copyright (c) 2026, David Uhden Collado
* SPDX-License-Identifier: BSD-3-Clause
*
* This file is part of openi2pd and licensed under BSD-3-Clause
*/

#ifndef I2PD_PRIVSEP_CHANNEL_H__
#define I2PD_PRIVSEP_CHANNEL_H__

#include "Expected.h"
#include <optional>
#include <string>
#include <utility>

#include "Protocol.h"

namespace i2p::privsep
{
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
	i2p::expected<std::optional<Message>, std::string> Receive (int timeoutMs = 1000);
	i2p::expected<uint32_t, std::string> PeerUid () const;

private:
	int m_Fd = -1;
};
}

#endif
