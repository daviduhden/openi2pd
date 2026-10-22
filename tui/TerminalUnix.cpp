/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TerminalUnix.
 *
 * Implements the terminal boundary with ncursesw: initialises cbreak,
 * no-echo keypad input, maps ncurses keys to stable shared codes and exposes
 * colour-pair and timeout-based read operations. It is compiled only on
 * non-Windows targets and translates resize and keypad input for the shared
 * TUI loop.
 *
 */

#ifndef _WIN32

#include "Terminal.h"

#include <algorithm>
#include <cwchar>
#include <string>

#include <ncurses.h>

// Terminal.h deliberately uses stable, platform-neutral key values.  ncurses
// also exposes KEY_* names as macros; capture the native values before
// undefining them so TranslateKey can compare getch() results without making
// the shared identifiers macro-dependent.
namespace
{
constexpr int NC_KEY_RESIZE = KEY_RESIZE;
constexpr int NC_KEY_UP = KEY_UP;
constexpr int NC_KEY_DOWN = KEY_DOWN;
constexpr int NC_KEY_LEFT = KEY_LEFT;
constexpr int NC_KEY_RIGHT = KEY_RIGHT;
constexpr int NC_KEY_HOME = KEY_HOME;
constexpr int NC_KEY_END = KEY_END;
constexpr int NC_KEY_PPAGE = KEY_PPAGE;
constexpr int NC_KEY_NPAGE = KEY_NPAGE;
constexpr int NC_KEY_ENTER = KEY_ENTER;
constexpr int NC_KEY_BACKSPACE = KEY_BACKSPACE;
constexpr int NC_KEY_DC = KEY_DC;
constexpr int NC_KEY_IC = KEY_IC;
constexpr int NC_KEY_BTAB = KEY_BTAB;
constexpr int NC_KEY_F1 = KEY_F (1);
constexpr int NC_KEY_F2 = KEY_F (2);
constexpr int NC_KEY_F3 = KEY_F (3);
constexpr int NC_KEY_F4 = KEY_F (4);
constexpr int NC_KEY_F5 = KEY_F (5);
constexpr int NC_KEY_F6 = KEY_F (6);
constexpr int NC_KEY_F7 = KEY_F (7);
constexpr int NC_KEY_F8 = KEY_F (8);
constexpr int NC_KEY_F9 = KEY_F (9);
constexpr int NC_KEY_F10 = KEY_F (10);
constexpr int NC_KEY_F11 = KEY_F (11);
constexpr int NC_KEY_F12 = KEY_F (12);
}
#undef KEY_RESIZE
#undef KEY_UP
#undef KEY_DOWN
#undef KEY_LEFT
#undef KEY_RIGHT
#undef KEY_HOME
#undef KEY_END
#undef KEY_PPAGE
#undef KEY_NPAGE
#undef KEY_ENTER
#undef KEY_BACKSPACE
#undef KEY_DC
#undef KEY_IC
#undef KEY_BTAB
#undef KEY_F

namespace i2p
{
namespace tui
{
	namespace terminal
	{
		namespace
		{
			bool g_Initialized = false;
			const char * g_Error = nullptr;

			int MapAttributes (int attributes)
			{
				int result = A_NORMAL;
				if (attributes & ATTR_BOLD) result |= A_BOLD;
				if (attributes & ATTR_DIM) result |= A_DIM;
				if (attributes & ATTR_UNDERLINE) result |= A_UNDERLINE;
				if (attributes & ATTR_REVERSE) result |= A_REVERSE;
				const int pair = (attributes >> PAIR_SHIFT) & PAIR_MASK;
				if (pair != 0) result |= COLOR_PAIR (pair);
				return result;
			}

			int TranslateKey (int key)
			{
				switch (key)
				{
				case ERR: return KEY_NONE;
				case NC_KEY_RESIZE: return KEY_RESIZE;
				case NC_KEY_UP: return KEY_UP;
				case NC_KEY_DOWN: return KEY_DOWN;
				case NC_KEY_LEFT: return KEY_LEFT;
				case NC_KEY_RIGHT: return KEY_RIGHT;
				case NC_KEY_HOME: return KEY_HOME;
				case NC_KEY_END: return KEY_END;
				case NC_KEY_PPAGE: return KEY_PPAGE;
				case NC_KEY_NPAGE: return KEY_NPAGE;
				case NC_KEY_ENTER: return KEY_ENTER;
				case NC_KEY_BACKSPACE: return KEY_BACKSPACE;
				case NC_KEY_DC: return KEY_DC;
				case NC_KEY_IC: return KEY_IC;
				case NC_KEY_BTAB: return KEY_BTAB;
				case NC_KEY_F1: return KEY_F1;
				case NC_KEY_F2: return KEY_F2;
				case NC_KEY_F3: return KEY_F3;
				case NC_KEY_F4: return KEY_F4;
				case NC_KEY_F5: return KEY_F5;
				case NC_KEY_F6: return KEY_F6;
				case NC_KEY_F7: return KEY_F7;
				case NC_KEY_F8: return KEY_F8;
				case NC_KEY_F9: return KEY_F9;
				case NC_KEY_F10: return KEY_F10;
				case NC_KEY_F11: return KEY_F11;
				case NC_KEY_F12: return KEY_F12;
				default: break;
				}
				// Printable input and Ctrl combinations stay as bytes.  The
				// application already treats Ctrl-C (3) as a stop key.
				if (key >= 0 && key <= 0xFF) return key;
				return KEY_NONE;
			}
		}

		bool Init ()
		{
			g_Error = nullptr;
			if (g_Initialized) return true;
			if (initscr () == nullptr)
			{
				g_Error = "unable to initialize the ncurses terminal";
				return false;
			}
			cbreak ();
			noecho ();
			keypad (stdscr, TRUE);
			curs_set (0);
			timeout (200);
			g_Initialized = true;
			return true;
		}

		void Shutdown ()
		{
			if (!g_Initialized) return;
			curs_set (1);
			endwin ();
			g_Initialized = false;
		}

		const char * LastError ()
		{
			return g_Error;
		}

		void Dimensions (int & height, int & width)
		{
			height = 0;
			width = 0;
			if (!g_Initialized || stdscr == nullptr) return;
			getmaxyx (stdscr, height, width);
		}

		void Clear ()
		{
			if (g_Initialized) erase ();
		}

		void Move (int row, int column)
		{
			if (g_Initialized) move (row, column);
		}

		void Put (std::string_view text, int limit)
		{
			if (!g_Initialized || text.empty ()) return;
			// limit is a terminal-column budget, not a byte count. Decode each
			// UTF-8 sequence so a wide CJK glyph or a combining mark cannot be
			// split or consume the wrong number of cells.
			std::string copy;
			int columns = 0;
			size_t offset = 0;
			while (offset < text.size () && (limit < 0 || columns < limit))
			{
				std::mbstate_t state { };
				wchar_t character = 0;
				const size_t consumed = std::mbrtowc (&character, text.data () + offset, text.size () - offset, &state);
				size_t bytes = consumed;
				int width = 1;
				if (consumed == static_cast<size_t> (-1) || consumed == static_cast<size_t> (-2) || consumed == 0)
					bytes = 1;
				else
				{
					width = ::wcwidth (character);
					if (width < 0) width = 1;
				}
				if (limit >= 0 && columns + width > limit) break;
				copy.append (text.substr (offset, bytes));
				columns += width;
				offset += bytes;
			}
			if (!copy.empty ()) addnstr (copy.c_str (), static_cast<int> (copy.size ()));
		}

		void ClearToEol ()
		{
			if (g_Initialized) clrtoeol ();
		}

		void SetAttribute (int attributes)
		{
			if (g_Initialized) attrset (MapAttributes (attributes));
		}

		void Refresh ()
		{
			if (g_Initialized) refresh ();
		}

		void ShowCursor (bool visible)
		{
			if (g_Initialized) curs_set (visible ? 1 : 0);
		}

		int ReadKey (int timeoutMilliseconds)
		{
			if (!g_Initialized) return KEY_NONE;
			timeout (timeoutMilliseconds);
			return TranslateKey (getch ());
		}

		bool HasColors ()
		{
			return g_Initialized && has_colors () == TRUE;
		}

		bool StartColor ()
		{
			return g_Initialized && start_color () != ERR;
		}

		int ColorCount ()
		{
			return g_Initialized ? COLORS : 0;
		}

		int ColorPairCount ()
		{
			return g_Initialized ? COLOR_PAIRS : 0;
		}

		bool UseDefaultColors ()
		{
			return g_Initialized && use_default_colors () != ERR;
		}

		bool InitPair (int pair, int foreground, int background)
		{
			if (!g_Initialized || pair <= 0 || pair >= COLOR_PAIRS) return false;
			return init_pair (
					   static_cast<short> (pair), static_cast<short> (foreground), static_cast<short> (background))
				!= ERR;
		}

		bool InitExtendedPair (int pair, int foreground, int background)
		{
#if defined(NCURSES_EXT_COLORS) && defined(NCURSES_EXT_FUNCS)
			if (!g_Initialized || pair <= 0) return false;
			return init_extended_pair (pair, foreground, background) == OK;
#else
			(void)pair;
			(void)foreground;
			(void)background;
			return false;
#endif
		}
	}
}
}

#endif // _WIN32
