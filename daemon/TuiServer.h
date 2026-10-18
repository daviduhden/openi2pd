/*
* Copyright (c) 2013-2026, The PurpleI2P Project
* Copyright (c) 2026, David Uhden Collado
* SPDX-License-Identifier: BSD-3-Clause
*
* This file is part of openi2pd and licensed under BSD-3-Clause
*
* See full license text in LICENSE file at top of project tree
*/

#ifndef TUI_SERVER_H__
#define TUI_SERVER_H__

#ifndef _WIN32

#include <atomic>
#include <memory>
#include <string>
#include <sys/types.h>
#include <thread>

namespace i2p
{
namespace tui
{
	/**
	 * Local administrative channel for i2pd-tui.
	 *
	 * The router owns the listening socket and all snapshot collection. The
	 * TUI is a separate process and can disappear, hang or misbehave without
	 * affecting the daemon: every request is bounded, handled sequentially
	 * and written with a send timeout. Only a peer with the daemon's effective
	 * UID is accepted, on top of a 0600 socket in the data directory.
	 */
	class Server
	{
	public:
		explicit Server (std::string socketPath);
		~Server ();

		Server (const Server &) = delete;
		Server & operator= (const Server &) = delete;

		bool Start ();
		void Stop ();

	private:
		void Run ();
		void HandleClient (int clientFd);
		bool IsTrustedPeer (int clientFd) const;

		std::string m_Path;
		int m_ListenFd { -1 };
		dev_t m_Device { 0 };
		ino_t m_Inode { 0 };
		bool m_HasIdentity { false };
		std::thread m_Thread;
		std::atomic<bool> m_Stop { false };
	};
}
}

#endif // _WIN32

#endif // TUI_SERVER_H__
