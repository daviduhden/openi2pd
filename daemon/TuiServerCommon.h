/*
* Copyright (c) 2013-2026, The PurpleI2P Project
* Copyright (c) 2026, David Uhden Collado
* SPDX-License-Identifier: BSD-3-Clause
*
* This file is part of openi2pd and licensed under BSD-3-Clause
*
* See full license text in LICENSE file at top of project tree
*/

#ifndef TUI_SERVER_COMMON_H__
#define TUI_SERVER_COMMON_H__

#include <string>
#include <string_view>

namespace i2p
{
namespace tui
{
	/**
	 * Result of handling one request line.
	 *
	 * The response is already framed and length-checked by the protocol
	 * helpers; transports only have to write it verbatim. A true
	 * closeConnection means the peer asked to end the session (QUIT).
	 */
	struct DispatchResult
	{
		std::string response;
		bool closeConnection = false;
	};

	/**
	 * Validate and execute one newline-free request line.
	 *
	 * This is the only protocol entry point. Both the UNIX-domain socket
	 * and the Windows named-pipe transports must call it; neither may build
	 * snapshots or execute commands itself.
	 */
	DispatchResult Dispatch (std::string_view line);
}
}

#endif // TUI_SERVER_COMMON_H__
