/*
* Copyright (c) 2013-2026, The PurpleI2P Project
* Copyright (c) 2026, David Uhden Collado
* SPDX-License-Identifier: BSD-3-Clause
*
* This file is part of openi2pd and licensed under BSD-3-Clause
*
* See full license text in LICENSE file at top of project tree
*/

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include "Terminal.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <windows.h>

namespace i2p
{
namespace tui
{
	namespace terminal
	{
		namespace
		{
			struct Cell
			{
				std::string text;
				int attributes = ATTR_NORMAL;
				int width = 1;
				bool continuation = false;
			};

			struct PairSpec
			{
				int foreground = COLOR_DEFAULT;
				int background = COLOR_DEFAULT;
				bool configured = false;
			};

			HANDLE g_Output = INVALID_HANDLE_VALUE;
			HANDLE g_Input = INVALID_HANDLE_VALUE;
			DWORD g_SavedOutputMode = 0;
			DWORD g_SavedInputMode = 0;
			bool g_OutputModeSaved = false;
			bool g_InputModeSaved = false;
			bool g_Initialized = false;
			bool g_AltScreen = false;
			const char * g_Error = nullptr;

			int g_Height = 0;
			int g_Width = 0;
			std::vector<Cell> g_Front;
			std::vector<Cell> g_Back;

			int g_Attribute = ATTR_NORMAL;
			int g_CursorRow = 0;
			int g_CursorColumn = 0;
			int g_WantedRow = 0;
			int g_WantedColumn = 0;
			int g_PhysicalRow = 0;
			int g_PhysicalColumn = -1;
			bool g_CursorVisible = false;
			bool g_CursorVisibilityEmitted = false;

			int g_ColorCount = 256;
			int g_ColorPairCount = MAX_PAIR + 1;
			std::array<PairSpec, MAX_PAIR + 1> g_Pairs { };

			std::deque<int> g_PendingKeys;
			wchar_t g_HighSurrogate = 0;

			Cell BlankCell ()
			{
				Cell cell;
				cell.text = " ";
				cell.width = 1;
				return cell;
			}

			bool WriteUtf8 (std::string_view text)
			{
				if (!g_Output || g_Output == INVALID_HANDLE_VALUE || text.empty ()) return true;
				const int length
					= MultiByteToWideChar (CP_UTF8, 0, text.data (), static_cast<int> (text.size ()), nullptr, 0);
				if (length <= 0) return false;
				std::wstring wide (static_cast<std::size_t> (length), L'\0');
				if (!MultiByteToWideChar (
						CP_UTF8, 0, text.data (), static_cast<int> (text.size ()), wide.data (), length))
					return false;
				std::size_t offset = 0;
				while (offset < wide.size ())
				{
					const DWORD chunk = static_cast<DWORD> (std::min<std::size_t> (wide.size () - offset, 32768));
					DWORD written = 0;
					if (!WriteConsoleW (g_Output, wide.data () + offset, chunk, &written, nullptr) || written == 0)
						return false;
					offset += written;
				}
				return true;
			}

			bool UpdateSize ()
			{
				CONSOLE_SCREEN_BUFFER_INFO info { };
				if (!GetConsoleScreenBufferInfo (g_Output, &info)) return false;
				int width = info.srWindow.Right - info.srWindow.Left + 1;
				int height = info.srWindow.Bottom - info.srWindow.Top + 1;
				if (width < 1) width = 1;
				if (height < 1) height = 1;
				if (width == g_Width && height == g_Height) return true;
				g_Width = width;
				g_Height = height;
				try
				{
					g_Front.assign (static_cast<std::size_t> (width) * height, BlankCell ());
					g_Back = g_Front;
				}
				catch (...)
				{
					g_Width = 0;
					g_Height = 0;
					g_Front.clear ();
					g_Back.clear ();
					return false;
				}
				g_CursorRow = std::clamp (g_CursorRow, 0, g_Height - 1);
				g_CursorColumn = std::clamp (g_CursorColumn, 0, g_Width - 1);
				g_WantedRow = std::clamp (g_WantedRow, 0, g_Height - 1);
				g_WantedColumn = std::clamp (g_WantedColumn, 0, g_Width - 1);
				g_PhysicalRow = 0;
				g_PhysicalColumn = -1;
				return true;
			}

			void RestoreConsole ()
			{
				if (g_AltScreen)
					WriteUtf8 ("\x1b[0m\x1b[?25h\x1b[?7h\x1b[?1049l");
				else if (g_OutputModeSaved)
					WriteUtf8 ("\x1b[0m\x1b[?25h\x1b[?7h");
				g_AltScreen = false;
				if (g_InputModeSaved && g_Input != INVALID_HANDLE_VALUE)
				{
					SetConsoleMode (g_Input, g_SavedInputMode);
					g_InputModeSaved = false;
				}
				if (g_OutputModeSaved && g_Output != INVALID_HANDLE_VALUE)
				{
					SetConsoleMode (g_Output, g_SavedOutputMode);
					g_OutputModeSaved = false;
				}
				g_Initialized = false;
				g_Front.clear ();
				g_Back.clear ();
				g_Height = 0;
				g_Width = 0;
			}

			std::string CursorTo (int row, int column)
			{
				return "\x1b[" + std::to_string (row + 1) + ";" + std::to_string (column + 1) + "H";
			}

			bool IsBlankTail (int row, int column)
			{
				const int rowOffset = row * g_Width;
				for (int current = column; current < g_Width; ++current)
				{
					const Cell & cell = g_Back[rowOffset + current];
					if (cell.continuation || cell.width != 1 || cell.attributes != ATTR_NORMAL || cell.text != " ")
						return false;
				}
				return true;
			}

			void AppendColor (std::string & sequence, int index, bool foreground)
			{
				if (index < 0) return;
				if (g_ColorCount >= 256)
				{
					sequence += foreground ? ";38;5;" : ";48;5;";
					sequence += std::to_string (index & 0xFF);
				}
				else if (g_ColorCount >= 16)
				{
					const int value = index & 15;
					int code = 0;
					if (foreground)
						code = value >= 8 ? 90 + (value - 8) : 30 + value;
					else
						code = value >= 8 ? 100 + (value - 8) : 40 + value;
					sequence += ";" + std::to_string (code);
				}
				else if (g_ColorCount >= 8)
				{
					const int code = (foreground ? 30 : 40) + (index & 7);
					sequence += ";" + std::to_string (code);
				}
			}

			std::string BuildSgr (int attributes)
			{
				std::string sequence = "\x1b[0";
				if (attributes & ATTR_BOLD) sequence += ";1";
				if (attributes & ATTR_DIM) sequence += ";2";
				if (attributes & ATTR_UNDERLINE) sequence += ";4";
				if (attributes & ATTR_REVERSE) sequence += ";7";
				const int pair = (attributes >> PAIR_SHIFT) & PAIR_MASK;
				if (pair > 0 && pair <= MAX_PAIR && g_Pairs[pair].configured)
				{
					AppendColor (sequence, g_Pairs[pair].foreground, true);
					AppendColor (sequence, g_Pairs[pair].background, false);
				}
				sequence += "m";
				return sequence;
			}

			bool DecodeNext (std::string_view text, std::size_t offset, std::uint32_t & codePoint, std::size_t & length)
			{
				const auto byte = static_cast<unsigned char> (text[offset]);
				if (byte < 0x80)
				{
					codePoint = byte;
					length = 1;
					return true;
				}
				std::size_t needed = 0;
				std::uint32_t value = 0;
				if ((byte & 0xE0) == 0xC0)
				{
					needed = 1;
					value = byte & 0x1F;
				}
				else if ((byte & 0xF0) == 0xE0)
				{
					needed = 2;
					value = byte & 0x0F;
				}
				else if ((byte & 0xF8) == 0xF0)
				{
					needed = 3;
					value = byte & 0x07;
				}
				else
				{
					codePoint = 0xFFFD;
					length = 1;
					return true;
				}
				if (offset + needed >= text.size ())
				{
					codePoint = 0xFFFD;
					length = 1;
					return true;
				}
				for (std::size_t i = 1; i <= needed; ++i)
				{
					const auto next = static_cast<unsigned char> (text[offset + i]);
					if ((next & 0xC0) != 0x80)
					{
						codePoint = 0xFFFD;
						length = 1;
						return true;
					}
					value = (value << 6) | (next & 0x3F);
				}
				codePoint = value;
				length = needed + 1;
				return true;
			}

			std::string EncodeUtf8 (std::uint32_t codePoint)
			{
				std::string result;
				if (codePoint < 0x80)
					result.push_back (static_cast<char> (codePoint));
				else if (codePoint < 0x800)
				{
					result.push_back (static_cast<char> (0xC0 | (codePoint >> 6)));
					result.push_back (static_cast<char> (0x80 | (codePoint & 0x3F)));
				}
				else if (codePoint < 0x10000)
				{
					result.push_back (static_cast<char> (0xE0 | (codePoint >> 12)));
					result.push_back (static_cast<char> (0x80 | ((codePoint >> 6) & 0x3F)));
					result.push_back (static_cast<char> (0x80 | (codePoint & 0x3F)));
				}
				else
				{
					result.push_back (static_cast<char> (0xF0 | (codePoint >> 18)));
					result.push_back (static_cast<char> (0x80 | ((codePoint >> 12) & 0x3F)));
					result.push_back (static_cast<char> (0x80 | ((codePoint >> 6) & 0x3F)));
					result.push_back (static_cast<char> (0x80 | (codePoint & 0x3F)));
				}
				return result;
			}

			int CodePointWidth (std::uint32_t codePoint)
			{
				if ((codePoint >= 0x0300 && codePoint <= 0x036F) || (codePoint >= 0x1AB0 && codePoint <= 0x1AFF)
					|| (codePoint >= 0x1DC0 && codePoint <= 0x1DFF) || (codePoint >= 0x20D0 && codePoint <= 0x20FF)
					|| (codePoint >= 0xFE00 && codePoint <= 0xFE0F) || (codePoint >= 0xFE20 && codePoint <= 0xFE2F))
					return 0;
				if ((codePoint >= 0x1100 && codePoint <= 0x115F) || codePoint == 0x2329 || codePoint == 0x232A
					|| (codePoint >= 0x2E80 && codePoint <= 0xA4CF && codePoint != 0x303F)
					|| (codePoint >= 0xAC00 && codePoint <= 0xD7A3) || (codePoint >= 0xF900 && codePoint <= 0xFAFF)
					|| (codePoint >= 0xFE10 && codePoint <= 0xFE19) || (codePoint >= 0xFE30 && codePoint <= 0xFE6F)
					|| (codePoint >= 0xFF00 && codePoint <= 0xFF60) || (codePoint >= 0xFFE0 && codePoint <= 0xFFE6)
					|| (codePoint >= 0x1F300 && codePoint <= 0x1FAFF) || (codePoint >= 0x20000 && codePoint <= 0x3FFFD))
					return 2;
				return 1;
			}

			int QueueUtf8 (std::uint32_t codePoint)
			{
				const std::string encoded = EncodeUtf8 (codePoint);
				for (std::size_t i = 1; i < encoded.size (); ++i)
					g_PendingKeys.push_back (static_cast<unsigned char> (encoded[i]));
				return static_cast<unsigned char> (encoded[0]);
			}

			int TranslateKey (const KEY_EVENT_RECORD & key)
			{
				const DWORD state = key.dwControlKeyState;
				const bool shift = (state & SHIFT_PRESSED) != 0;
				const bool control = (state & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
				const bool alt = (state & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0;
				const WORD virtualKey = key.wVirtualKeyCode;

				switch (virtualKey)
				{
				case VK_UP: return KEY_UP;
				case VK_DOWN: return KEY_DOWN;
				case VK_LEFT: return KEY_LEFT;
				case VK_RIGHT: return KEY_RIGHT;
				case VK_HOME: return KEY_HOME;
				case VK_END: return KEY_END;
				case VK_PRIOR: return KEY_PPAGE;
				case VK_NEXT: return KEY_NPAGE;
				case VK_INSERT: return KEY_IC;
				case VK_DELETE: return KEY_DC;
				case VK_BACK: return KEY_BACKSPACE;
				case VK_RETURN: return KEY_ENTER;
				case VK_ESCAPE: return 27;
				case VK_TAB: return shift ? KEY_BTAB : '\t';
				case VK_F1: return KEY_F1;
				case VK_F2: return KEY_F2;
				case VK_F3: return KEY_F3;
				case VK_F4: return KEY_F4;
				case VK_F5: return KEY_F5;
				case VK_F6: return KEY_F6;
				case VK_F7: return KEY_F7;
				case VK_F8: return KEY_F8;
				case VK_F9: return KEY_F9;
				case VK_F10: return KEY_F10;
				case VK_F11: return KEY_F11;
				case VK_F12: return KEY_F12;
				default: break;
				}

				// Ctrl-letter arrives as the matching C0 byte.  Ctrl-C is
				// therefore 3 and is handled by the shared main loop.
				if (control && !alt && virtualKey >= 'A' && virtualKey <= 'Z')
				{
					g_HighSurrogate = 0;
					return static_cast<int> (virtualKey - 'A' + 1);
				}

				const wchar_t character = key.uChar.UnicodeChar;
				if (character >= 0xD800 && character <= 0xDBFF)
				{
					g_HighSurrogate = character;
					return KEY_NONE;
				}
				if (character >= 0xDC00 && character <= 0xDFFF)
				{
					if (!g_HighSurrogate) return KEY_NONE;
					const std::uint32_t codePoint = 0x10000
						+ ((static_cast<std::uint32_t> (g_HighSurrogate) - 0xD800) << 10)
						+ (static_cast<std::uint32_t> (character) - 0xDC00);
					g_HighSurrogate = 0;
					return QueueUtf8 (codePoint);
				}
				g_HighSurrogate = 0;
				if (character == 0) return KEY_NONE;
				if (character < 0x80) return static_cast<int> (character);
				return QueueUtf8 (static_cast<std::uint32_t> (character));
			}
		}

		bool Init ()
		{
			g_Error = nullptr;
			if (g_Initialized) return true;

			g_Output = GetStdHandle (STD_OUTPUT_HANDLE);
			g_Input = GetStdHandle (STD_INPUT_HANDLE);
			if (!g_Output || g_Output == INVALID_HANDLE_VALUE)
			{
				g_Error = "standard output is not available";
				return false;
			}
			if (!g_Input || g_Input == INVALID_HANDLE_VALUE)
			{
				g_Error = "standard input is not available";
				return false;
			}
			if (!GetConsoleMode (g_Output, &g_SavedOutputMode))
			{
				g_Error = "standard output is redirected; a console is required";
				g_Output = INVALID_HANDLE_VALUE;
				return false;
			}
			g_OutputModeSaved = true;
			if (!GetConsoleMode (g_Input, &g_SavedInputMode))
			{
				g_Error = "standard input is redirected; a console is required";
				RestoreConsole ();
				return false;
			}
			g_InputModeSaved = true;

			// Do not allocate a console: a redirected or detached process
			// must fail instead of taking over the parent's terminal.
			if (!SetConsoleMode (
					g_Output, g_SavedOutputMode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING))
			{
				g_Error = "the console does not support virtual terminal processing";
				RestoreConsole ();
				return false;
			}

			DWORD inputMode = g_SavedInputMode;
			inputMode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT | ENABLE_QUICK_EDIT_MODE);
			inputMode |= ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT;
			if (!SetConsoleMode (g_Input, inputMode))
			{
				g_Error = "unable to configure console input";
				RestoreConsole ();
				return false;
			}

			if (!UpdateSize ())
			{
				g_Error = "unable to size the console screen buffer";
				RestoreConsole ();
				return false;
			}

			g_ColorCount = 256;
			g_ColorPairCount = MAX_PAIR + 1;
			g_Pairs.fill (PairSpec { });
			g_Attribute = ATTR_NORMAL;
			g_CursorRow = 0;
			g_CursorColumn = 0;
			g_WantedRow = 0;
			g_WantedColumn = 0;
			g_PhysicalRow = 0;
			g_PhysicalColumn = -1;
			g_CursorVisible = false;
			g_CursorVisibilityEmitted = false;
			g_PendingKeys.clear ();
			g_HighSurrogate = 0;

			g_AltScreen = true;
			static constexpr std::string_view enterSequence = "\x1b[?1049h\x1b[?7l\x1b[?25l\x1b[2J\x1b[H";
			if (!WriteUtf8 (enterSequence))
			{
				g_Error = "unable to write to the console";
				RestoreConsole ();
				return false;
			}

			g_Initialized = true;
			return true;
		}

		void Shutdown ()
		{
			if (!g_Initialized && !g_AltScreen && !g_OutputModeSaved && !g_InputModeSaved) return;
			RestoreConsole ();
		}

		const char * LastError ()
		{
			return g_Error;
		}

		void Dimensions (int & height, int & width)
		{
			height = 0;
			width = 0;
			if (!g_Initialized) return;
			if (!UpdateSize ())
			{
				height = g_Height;
				width = g_Width;
				return;
			}
			height = g_Height;
			width = g_Width;
		}

		void Clear ()
		{
			if (!g_Initialized) return;
			g_Back.assign (static_cast<std::size_t> (g_Width) * g_Height, BlankCell ());
		}

		void Move (int row, int column)
		{
			if (!g_Initialized || g_Width <= 0 || g_Height <= 0) return;
			g_CursorRow = std::clamp (row, 0, g_Height - 1);
			g_CursorColumn = std::clamp (column, 0, g_Width - 1);
			g_WantedRow = g_CursorRow;
			g_WantedColumn = g_CursorColumn;
		}

		void Put (std::string_view text, int limit)
		{
			if (!g_Initialized || text.empty () || g_Width <= 0 || g_Height <= 0) return;
			std::size_t offset = 0;
			while (offset < text.size ())
			{
				if (limit >= 0 && offset >= static_cast<std::size_t> (limit)) break;
				std::uint32_t codePoint = 0;
				std::size_t length = 1;
				DecodeNext (text, offset, codePoint, length);
				offset += length;

				// Never pass control bytes from router data into the VT
				// stream.  SanitizeText() already does this upstream; this
				// is the terminal-level backstop against escape injection.
				if (codePoint < 0x20 || codePoint == 0x7F) codePoint = '?';

				const int width = CodePointWidth (codePoint);
				const std::string encoded = EncodeUtf8 (codePoint);
				if (width == 0)
				{
					if (g_CursorColumn <= 0 || g_CursorRow >= g_Height) continue;
					int previous = g_CursorColumn - 1;
					const int rowOffset = g_CursorRow * g_Width;
					while (previous >= 0 && g_Back[rowOffset + previous].continuation) --previous;
					if (previous < 0) continue;
					g_Back[rowOffset + previous].text += encoded;
					continue;
				}

				if (g_CursorRow >= g_Height) break;
				if (g_CursorColumn >= g_Width)
				{
					g_CursorColumn = 0;
					++g_CursorRow;
				}
				if (width == 2 && g_CursorColumn + 1 >= g_Width)
				{
					g_CursorColumn = 0;
					++g_CursorRow;
				}
				if (g_CursorRow >= g_Height) break;

				const int rowOffset = g_CursorRow * g_Width;
				Cell cell;
				cell.text = encoded;
				cell.attributes = g_Attribute;
				cell.width = width;
				cell.continuation = false;
				g_Back[rowOffset + g_CursorColumn] = std::move (cell);
				if (width == 2)
				{
					Cell continuation;
					continuation.attributes = g_Attribute;
					continuation.width = 0;
					continuation.continuation = true;
					g_Back[rowOffset + g_CursorColumn + 1] = std::move (continuation);
				}
				g_CursorColumn += width;
			}
			g_WantedRow = std::clamp (g_CursorRow, 0, g_Height - 1);
			g_WantedColumn = std::clamp (g_CursorColumn, 0, g_Width - 1);
		}

		void ClearToEol ()
		{
			if (!g_Initialized || g_Width <= 0) return;
			const int rowOffset = g_CursorRow * g_Width;
			for (int column = std::clamp (g_CursorColumn, 0, g_Width - 1); column < g_Width; ++column)
				g_Back[rowOffset + column] = BlankCell ();
		}

		void SetAttribute (int attributes)
		{
			g_Attribute = attributes;
		}

		void Refresh ()
		{
			if (!g_Initialized) return;
			std::string output;
			int currentAttributes = -1;
			int physicalRow = g_PhysicalRow;
			int physicalColumn = g_PhysicalColumn;
			bool cursorVisibility = g_CursorVisibilityEmitted;

			for (int row = 0; row < g_Height; ++row)
			{
				const int rowOffset = row * g_Width;
				for (int column = 0; column < g_Width; ++column)
				{
					const Cell & next = g_Back[rowOffset + column];
					const Cell & previous = g_Front[rowOffset + column];
					if (next.continuation) continue;
					if (previous.continuation == next.continuation && previous.text == next.text
						&& previous.attributes == next.attributes && previous.width == next.width)
						continue;

					if (physicalRow != row || physicalColumn != column)
					{
						output += CursorTo (row, column);
						physicalRow = row;
						physicalColumn = column;
					}
					if (IsBlankTail (row, column))
					{
						// Clear-to-end-of-line is cheaper than a run of
						// spaces and keeps the buffered diff exact: every
						// remaining cell in this row is already blank.
						if (currentAttributes != ATTR_NORMAL)
						{
							output += BuildSgr (ATTR_NORMAL);
							currentAttributes = ATTR_NORMAL;
						}
						output += "\x1b[K";
						physicalColumn = -1;
						break;
					}
					if (currentAttributes != next.attributes)
					{
						output += BuildSgr (next.attributes);
						currentAttributes = next.attributes;
					}
					output += next.text;
					physicalColumn += next.width;
					if (physicalColumn >= g_Width) physicalColumn = -1;
				}
			}

			if (g_CursorVisible)
			{
				const int wantedRow = std::clamp (g_WantedRow, 0, std::max (0, g_Height - 1));
				const int wantedColumn = std::clamp (g_WantedColumn, 0, std::max (0, g_Width - 1));
				if (physicalRow != wantedRow || physicalColumn != wantedColumn)
				{
					output += CursorTo (wantedRow, wantedColumn);
					physicalRow = wantedRow;
					physicalColumn = wantedColumn;
				}
			}

			if (cursorVisibility != g_CursorVisible)
			{
				output += g_CursorVisible ? "\x1b[?25h" : "\x1b[?25l";
				cursorVisibility = g_CursorVisible;
			}
			if (output.empty ()) return;
			if (!WriteUtf8 (output))
			{
				g_Error = "failed to write to the console";
				return;
			}
			g_Front = g_Back;
			g_PhysicalRow = physicalRow;
			g_PhysicalColumn = physicalColumn;
			g_CursorVisibilityEmitted = cursorVisibility;
		}

		void ShowCursor (bool visible)
		{
			g_CursorVisible = visible;
		}

		int ReadKey (int timeoutMilliseconds)
		{
			if (!g_Initialized) return KEY_NONE;
			if (!g_PendingKeys.empty ())
			{
				const int key = g_PendingKeys.front ();
				g_PendingKeys.pop_front ();
				return key;
			}

			const ULONGLONG start = GetTickCount64 ();
			const ULONGLONG deadline
				= timeoutMilliseconds < 0 ? 0 : start + static_cast<ULONGLONG> (timeoutMilliseconds);
			int remaining = timeoutMilliseconds;

			for (;;)
			{
				const DWORD wait = remaining < 0 ? INFINITE : static_cast<DWORD> (remaining);
				const DWORD result = WaitForSingleObject (g_Input, wait);
				if (result == WAIT_TIMEOUT) return KEY_NONE;
				if (result != WAIT_OBJECT_0) return KEY_NONE;

				INPUT_RECORD record { };
				DWORD count = 0;
				if (!ReadConsoleInputW (g_Input, &record, 1, &count) || count == 0) return KEY_NONE;
				if (record.EventType == WINDOW_BUFFER_SIZE_EVENT)
				{
					UpdateSize ();
					return KEY_RESIZE;
				}
				if (record.EventType == KEY_EVENT)
				{
					const KEY_EVENT_RECORD & key = record.Event.KeyEvent;
					if (key.bKeyDown)
					{
						const int translated = TranslateKey (key);
						if (translated != KEY_NONE) return translated;
					}
				}

				if (remaining >= 0)
				{
					const ULONGLONG now = GetTickCount64 ();
					remaining = now >= deadline ? 0 : static_cast<int> (deadline - now);
				}
			}
		}

		bool HasColors ()
		{
			return g_Initialized && g_ColorCount >= 8;
		}

		bool StartColor ()
		{
			return g_Initialized && g_ColorCount >= 8;
		}

		int ColorCount ()
		{
			return g_Initialized ? g_ColorCount : 0;
		}

		int ColorPairCount ()
		{
			return g_Initialized ? g_ColorPairCount : 0;
		}

		bool UseDefaultColors ()
		{
			return g_Initialized;
		}

		bool InitPair (int pair, int foreground, int background)
		{
			if (!g_Initialized || pair <= 0 || pair > MAX_PAIR) return false;
			if (foreground < COLOR_DEFAULT || foreground > 255) return false;
			if (background < COLOR_DEFAULT || background > 255) return false;
			g_Pairs[pair].foreground = foreground;
			g_Pairs[pair].background = background;
			g_Pairs[pair].configured = true;
			return true;
		}

		bool InitExtendedPair (int pair, int foreground, int background)
		{
			// VT SGR can address the existing 256-colour palette but does
			// not define palette entries, so direct RGB pairs are not used.
			(void)pair;
			(void)foreground;
			(void)background;
			return false;
		}
	}
}
}

#endif // _WIN32
