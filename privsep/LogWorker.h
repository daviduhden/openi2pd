/*
* Copyright (c) 2026, The PurpleI2P Project
* Copyright (c) 2026, David Uhden Collado
* SPDX-License-Identifier: BSD-3-Clause
*
* This file is part of openi2pd and licensed under BSD-3-Clause
*/

#ifndef I2PD_PRIVSEP_LOG_WORKER_H__
#define I2PD_PRIVSEP_LOG_WORKER_H__

#include <cstdint>
#include <ctime>
#include <mutex>
#include <string>
#include <string_view>

#include "Channel.h"

namespace i2p::privsep
{
/**
	 * Parent-side log sink. It only serializes structured records to the log
	 * worker; the router process never opens or writes the log file itself.
	 *
	 * The sink object stays alive for the router lifetime so the logger's sink
	 * callback never points at a replaced object. The in-process restart
	 * monitor swaps the underlying channel with Reattach() instead.
	 */
class LogSink
{
public:
	explicit LogSink (Channel channel)
		: m_Channel (std::move (channel))
	{
	}

	/** Send the one-time configuration and remember it for restarts. */
	i2p::expected<void, std::string> Configure (bool useSyslog, std::string_view path, std::string_view tag);
	/** Attach a freshly spawned worker after re-sending the configuration. */
	i2p::expected<void, std::string> Reattach (Channel channel);
	void Write (uint8_t level, std::time_t timestamp, std::string_view text);
	i2p::expected<void, std::string> Shutdown ();
	bool IsOpen () const;
	bool IsShutdown () const;

private:
	mutable std::mutex m_Mutex;
	Channel m_Channel;
	bool m_Configured = false;
	bool m_Shutdown = false;
	bool m_Syslog = false;
	std::string m_Path;
	std::string m_Tag;
};

/** Entry point for the isolated log worker process. */
int RunLogWorker (int fd);
}

#endif
