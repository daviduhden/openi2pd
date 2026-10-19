/*
* Copyright (c) 2026, David Uhden Collado
* SPDX-License-Identifier: BSD-3-Clause
*
* This file is part of openi2pd and licensed under BSD-3-Clause
*
* See full license text in LICENSE file at top of project tree
*/

#ifndef TUI_SERVER_H__
#define TUI_SERVER_H__

#include <memory>
#include <string>

namespace i2p
{
namespace tui
{
	/**
	 * Local administrative channel for i2pd-tui.
	 *
	 * The router owns the listening endpoint and all snapshot collection.
	 * The TUI is a separate process and can disappear, hang or misbehave
	 * without affecting the daemon: every request is bounded, handled
	 * sequentially and written with a send timeout.
	 *
	 * On UNIX the endpoint is a 0600 AF_UNIX socket and only a peer with the
	 * daemon's effective UID is accepted. On Windows the endpoint is mapped
	 * to a named pipe whose explicit DACL admits only the daemon user,
	 * SYSTEM and Administrators; remote clients are rejected. In both cases
	 * every request is validated by the shared TuiServerCommon dispatcher.
	 *
	 * The transport state is private to the platform implementation so this
	 * header does not expose file descriptors or HANDLE values.
	 */
	class Server
	{
	public:
		explicit Server (std::string endpoint);
		~Server ();

		Server (const Server &) = delete;
		Server & operator= (const Server &) = delete;

		bool Start ();
		void Stop ();

	private:
		class Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}
}

#endif // TUI_SERVER_H__
