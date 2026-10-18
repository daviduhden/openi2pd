/*
* Copyright (c) 2026, The PurpleI2P Project
*
* This file is part of Open I2Pd and licensed under BSD3
*/

#ifndef I2PD_PRIVSEP_WORKER_H__
#define I2PD_PRIVSEP_WORKER_H__

#include <string>
#include <sys/types.h>

#include "Channel.h"

namespace i2p::privsep
{
	class ChildProcess
	{
		public:
			ChildProcess () = default;
			~ChildProcess ();
			ChildProcess (ChildProcess&& other) noexcept;
			ChildProcess& operator= (ChildProcess&& other) noexcept;
			ChildProcess (const ChildProcess&) = delete;
			ChildProcess& operator= (const ChildProcess&) = delete;

			bool IsRunning ();
			void Stop (int timeoutMs = 2000);
			pid_t Pid () const { return m_Pid; }
			Channel& ChannelFd () { return m_Channel; }

		private:
			friend i2p::expected<ChildProcess, std::string> SpawnLogWorker ();
			/** Reap without blocking. Returns true when the child no longer exists. */
			bool ReapNoHang ();
			void ClosePidFd ();
			pid_t m_Pid = -1;
			int m_PidFd = -1;
			Channel m_Channel;
	};

	i2p::expected<ChildProcess, std::string> SpawnLogWorker ();
	std::string ExecutablePath ();
}

#endif
