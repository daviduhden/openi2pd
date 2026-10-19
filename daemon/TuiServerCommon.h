/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TuiServerCommon.
 *
 * Declares the transport-independent TUI request dispatcher and its result
 * type. Both the UNIX socket and Windows named-pipe servers must pass every
 * request line through this entry point, including embedded torrent import.
 *
 */

#ifndef TUI_SERVER_COMMON_H__
#define TUI_SERVER_COMMON_H__

#include <string>
#include <string_view>
#include <span>
#include <cstdint>

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
	 *
	 * All exceptions are translated into an Internal error frame here. The
	 * transports must nevertheless wrap their own per-client buffering and
	 * send loop in catch(...): a bad_alloc in transport state must close one
	 * client, not unwind through std::thread and terminate the router.
	 */
	DispatchResult Dispatch (std::string_view line);
	// Embedded administration only: caller must establish the same local
	// application authority as Dispatch. Bytes are metainfo, never a path.
	DispatchResult ImportTorrent (uint32_t tunnelIndex, std::span<const uint8_t> content);
}
}

#endif // TUI_SERVER_COMMON_H__
