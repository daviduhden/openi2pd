/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TuiWin32Ipc.
 *
 * Derives the Windows named-pipe path shared by the daemon and i2pd-tui from
 * the configured local endpoint. The endpoint is normalised and hashed with
 * FNV-1a into \\.\pipe\openi2pd-tui-<16 hex digits>, so path spelling cannot
 * change the pipe and arbitrary pipe names cannot be selected.
 *
 */

#ifndef TUI_WIN32_IPC_H__
#define TUI_WIN32_IPC_H__

#include <string>
#include <string_view>

namespace i2p
{
namespace tui
{
	/**
	 * Map a configured local endpoint to a stable Windows named-pipe path.
	 *
	 * The daemon and i2pd-tui are separate processes and do not share a
	 * runtime object, so both derive the pipe from the same configured
	 * value: `tui.socket` when set, otherwise <datadir>/i2pd-tui.sock.
	 *
	 * The derivation is deliberately one-way:
	 *
	 *   - separators are normalised to `\` and ASCII is folded to lower case
	 *     so equivalent path spellings yield the same pipe;
	 *   - the normalised bytes are hashed with 64-bit FNV-1a;
	 *   - the result is `\\.\pipe\openi2pd-tui-<16 lowercase hex digits>`.
	 *
	 * No endpoint byte becomes part of the pipe name, which avoids invalid
	 * pipe characters and keeps the name bounded. A full `\\.\pipe\...`
	 * value is still hashed, never opened verbatim; callers cannot select an
	 * arbitrary existing pipe by choosing `--socket`.
	 */
	std::wstring MakeWin32PipeName (std::string_view endpoint);

	/** Normalised UTF-8 form used by MakeWin32PipeName (diagnostics/tests). */
	std::string NormalizeWin32Endpoint (std::string_view endpoint);
}
}

#endif // TUI_WIN32_IPC_H__
