/*
* Copyright (c) 2026, The PurpleI2P Project
*
* This file is part of Open I2Pd and licensed under BSD3
*/

#ifndef I2PD_PRIVSEP_LOG_WORKER_H__
#define I2PD_PRIVSEP_LOG_WORKER_H__

#include <cstdint>
#include <ctime>
#include <memory>
#include <string>
#include <string_view>

#include "Channel.h"

namespace i2p::privsep
{
	constexpr const char *LOG_WORKER_ENV_FD = "I2PD_PRIVSEP_FD";

	/**
	 * Parent-side log sink. It only serializes structured records to the log
	 * worker; the router process never opens or writes the log file itself.
	 */
	class LogSink
	{
		public:
			explicit LogSink (Channel channel): m_Channel (std::move (channel)) {}
			std::expected<void, std::string> Configure (bool useSyslog, std::string_view path,
				std::string_view tag);
			void Write (uint8_t level, std::time_t timestamp, std::string_view text);
			std::expected<void, std::string> Shutdown ();
			bool IsOpen () const { return m_Channel.IsOpen (); }

		private:
			Channel m_Channel;
	};

	/** Entry point for the isolated log worker process. */
	int RunLogWorker (int fd);
}

#endif
