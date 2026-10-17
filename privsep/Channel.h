/*
* Copyright (c) 2026, The PurpleI2P Project
*
* This file is part of Open I2Pd and licensed under BSD3
*/

#ifndef I2PD_PRIVSEP_CHANNEL_H__
#define I2PD_PRIVSEP_CHANNEL_H__

#include <expected>
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
			explicit Channel (int fd): m_Fd (fd) {}
			~Channel ();
			Channel (Channel&& other) noexcept: m_Fd (other.m_Fd) { other.m_Fd = -1; }
			Channel& operator= (Channel&& other) noexcept;
			Channel (const Channel&) = delete;
			Channel& operator= (const Channel&) = delete;

			static std::expected<std::pair<Channel, Channel>, std::string> CreatePair ();
			static Channel Adopt (int fd) { return Channel (fd); }

			bool IsOpen () const { return m_Fd >= 0; }
			int Fd () const { return m_Fd; }
			void Close ();

			std::expected<void, std::string> Send (const Message& message, int timeoutMs = 5000);
			std::expected<std::optional<Message>, std::string> Receive (int timeoutMs = 1000);
			std::expected<uint32_t, std::string> PeerUid () const;

		private:
			int m_Fd = -1;
	};
}

#endif
