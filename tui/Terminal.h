/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Terminal.
 *
 * Defines the platform-neutral terminal boundary shared by i2pd-tui:
 * integer attributes, fixed key codes and UTF-8 input/output
 * operations. Unix backs it with ncursesw and Windows with the Win32
 * console and VT sequences, so presentation code never depends on
 * either API.
 */

#ifndef OPENI2PD_TUI_TERMINAL_H
#define OPENI2PD_TUI_TERMINAL_H

#include <string_view>

/*
 * Minimal terminal boundary for the shared i2pd-tui presentation code.
 *
 * Unix wraps ncursesw; Windows uses the Win32 console API with VT/ANSI
 * output.  Attributes stay plain ints so the theme and the application
 * do not depend on ncurses types.  The layout is:
 *
 *   bits 0..7   text style (bold, dim, underline, reverse)
 *   bits 16..   colour pair number, see ColorPair()
 *
 * Keys are fixed values outside the byte range.  Printable input is
 * returned as one UTF-8 byte at a time, matching what the application
 * already appends to its prompt buffer.
 */

namespace i2p
{
namespace tui
{
	namespace terminal
	{
		/*
		 * Fixed key codes live outside the byte range because a printable
		 * key is returned as the raw UTF-8 byte that the console appends to
		 * its prompt buffer. The two value spaces must not collide, so a
		 * backend must never remap ordinary input into this range. Ctrl-C
		 * keeps the historical value 3 and is the only control byte the main
		 * loop treats as a command; both backends synthesise it (ncurses
		 * passes the byte, Win32 also maps VK_CANCEL) rather than expecting
		 * the terminal to send a signal.
		 */
		constexpr int KEY_NONE = -1;
		constexpr int KEY_CTRL_C = 3;
		constexpr int KEY_RESIZE = 0x100;
		constexpr int KEY_UP = 0x101;
		constexpr int KEY_DOWN = 0x102;
		constexpr int KEY_LEFT = 0x103;
		constexpr int KEY_RIGHT = 0x104;
		constexpr int KEY_HOME = 0x105;
		constexpr int KEY_END = 0x106;
		constexpr int KEY_PPAGE = 0x107;
		constexpr int KEY_NPAGE = 0x108;
		constexpr int KEY_ENTER = 0x109;
		constexpr int KEY_BACKSPACE = 0x10A;
		constexpr int KEY_DC = 0x10B;
		constexpr int KEY_IC = 0x10C;
		constexpr int KEY_BTAB = 0x10D;
		constexpr int KEY_F1 = 0x120;
		constexpr int KEY_F2 = 0x121;
		constexpr int KEY_F3 = 0x122;
		constexpr int KEY_F4 = 0x123;
		constexpr int KEY_F5 = 0x124;
		constexpr int KEY_F6 = 0x125;
		constexpr int KEY_F7 = 0x126;
		constexpr int KEY_F8 = 0x127;
		constexpr int KEY_F9 = 0x128;
		constexpr int KEY_F10 = 0x129;
		constexpr int KEY_F11 = 0x12A;
		constexpr int KEY_F12 = 0x12B;

		constexpr int ATTR_NORMAL = 0;
		constexpr int ATTR_BOLD = 1 << 0;
		constexpr int ATTR_DIM = 1 << 1;
		constexpr int ATTR_UNDERLINE = 1 << 2;
		constexpr int ATTR_REVERSE = 1 << 3;

		constexpr int COLOR_BLACK = 0;
		constexpr int COLOR_RED = 1;
		constexpr int COLOR_GREEN = 2;
		constexpr int COLOR_YELLOW = 3;
		constexpr int COLOR_BLUE = 4;
		constexpr int COLOR_MAGENTA = 5;
		constexpr int COLOR_CYAN = 6;
		constexpr int COLOR_WHITE = 7;
		constexpr int COLOR_DEFAULT = -1;

		constexpr int PAIR_SHIFT = 16;
		constexpr int PAIR_MASK = 0xFF;
		constexpr int MAX_PAIR = 255;

		/*
		 * Pack a pair number into the attribute word. Pair 0 is the terminal
		 * default and is reserved, never allocated; the 8-bit mask follows
		 * the width of the pair field in ncurses A_COLOR. A backend whose
		 * pair space is smaller rejects the number, which is why the caller
		 * (Theme) owns allocation and treats a failure as the next
		 * degradation step.
		 */
		constexpr int ColorPair (int pair)
		{
			return (pair & PAIR_MASK) << PAIR_SHIFT;
		}

		/*
		 * Enter terminal mode.  Returns false when there is no usable
		 * console (for example redirected standard handles on Windows) or
		 * when the terminal cannot provide the required capabilities.
		 * LastError() then describes the failure.
		 *
		 * Init is idempotent and owns the process-wide console state: on
		 * POSIX the ncursesw screen and keypad mode, on Windows the saved
		 * console modes and the alternate screen.  The locale must already
		 * provide a multibyte LC_CTYPE, because Put decodes UTF-8 to measure
		 * columns.  Shutdown is the only operation that restores that state
		 * and must run on every exit path, including an error path after a
		 * partially successful Init.
		 */
		bool Init ();
		void Shutdown ();
		const char * LastError ();

		void Dimensions (int & height, int & width);
		void Clear ();
		void Move (int row, int column);
		/*
		 * Write text at the cursor.  `limit` is a terminal-column budget, not
		 * a byte count: the text is decoded so a wide CJK glyph counts as two
		 * cells and a combining mark as zero, and a sequence is never split.
		 * A negative limit means "whole string".  Bytes that the locale
		 * cannot decode are consumed one at a time so the cursor still
		 * advances, instead of letting ncursesw swallow the rest of the row.
		 *
		 * Input must already be sanitized (SanitizeText turns tabs and line
		 * breaks into spaces). A control character that wcwidth() reports as
		 * -1 is charged one column here although a terminal may advance a tab
		 * to the next tab stop, so the backend does not expand tabs itself
		 * and a caller that bypasses sanitizing can drift the column budget.
		 */
		void Put (std::string_view text, int limit = -1);
		void ClearToEol ();
		void SetAttribute (int attributes);
		void Refresh ();
		void ShowCursor (bool visible);

		/*
		 * Wait for one key or resize event.  A negative timeout waits
		 * forever, zero polls once.  Returns KEY_NONE on timeout.
		 */
		int ReadKey (int timeoutMilliseconds);

		bool HasColors ();
		bool StartColor ();
		int ColorCount ();
		int ColorPairCount ();
		bool UseDefaultColors ();
		/*
		 * Allocate one colour pair.  Pair 0 is reserved for the terminal
		 * default and is rejected here; the caller (Theme) owns the number
		 * allocation so a failed allocation is a normal, non-fatal result
		 * that selects the next degradation step.  InitExtendedPair needs a
		 * direct-colour build and returns false where it is unavailable.
		 */
		bool InitPair (int pair, int foreground, int background);
		bool InitExtendedPair (int pair, int foreground, int background);
	}
}
}

#endif // OPENI2PD_TUI_TERMINAL_H
