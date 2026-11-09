/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TerminalText.
 *
 * Portable UTF-8 unit decoding, encoding, column measurement and
 * column-budget fitting shared by the Windows console backend and its
 * compile-free regression. It carries no platform API, so the column
 * contract documented in Terminal.h can be tested on any host.
 *
 */

#ifndef OPENI2PD_TUI_TERMINAL_TEXT_H
#define OPENI2PD_TUI_TERMINAL_TEXT_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace i2p
{
namespace tui
{
	namespace terminal
	{
		/*
		 * Terminal-column policy for UTF-8 text.
		 *
		 * Terminal.h defines `Put(text, limit)` as a column budget and
		 * requires a sequence never to be split. The ncurses backend
		 * measures that with the locale (mbrtowc/wcwidth); the Windows
		 * backend decodes UTF-8 itself, so the policy it applies lives
		 * here and can be verified without a console.
		 *
		 * The decoder is deliberately strict: an overlong form, an
		 * encoded UTF-16 surrogate, a code point above U+10FFFF, a bad
		 * continuation byte and a truncated tail all become exactly one
		 * U+FFFD unit of length one and width one.
		 *
		 * One unit per rejected byte keeps the caller's cursor advancing,
		 * matching ncurses behavior for an undecodable byte. It also keeps
		 * hostile sequences out of the VT stream and cell buffer, where
		 * the console might reinterpret them.
		 */
		namespace text
		{
			/// Unicode replacement character, emitted for malformed input.
			inline constexpr std::uint32_t REPLACEMENT_CHARACTER = 0xFFFD;

			/// One decoded UTF-8 unit.
			struct Unit
			{
				std::uint32_t codePoint = REPLACEMENT_CHARACTER;
				std::size_t length = 1;
				int width = 1;
			};

			/*
			 * Column width of a code point.
			 *
			 * A fixed table rather than wcwidth() because the Windows
			 * console has no locale-backed width function for the
			 * virtual-terminal stream it receives. Zero-width combining
			 * ranges count zero columns, East Asian wide/fullwidth ranges
			 * and emoji count two, and everything else counts one: an
			 * unknown glyph is charged one column, matching the fallback
			 * documented in Terminal.h.
			 *
			 * @param codePoint decoded code point.
			 * @return 0, 1 or 2 columns.
			 */
			inline int Width (std::uint32_t codePoint)
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

			/*
			 * Decode one UTF-8 unit at `offset`.
			 *
			 * Overlong forms (C0/C1 and their three/four-byte
			 * equivalents), encoded surrogates (ED A0..BF), values above
			 * U+10FFFF (F4 90 and above), stray continuation bytes and a
			 * truncated tail are rejected as one U+FFFD unit.
			 *
			 * @param text UTF-8 text; must outlive the call.
			 * @param offset start byte; an out-of-range offset yields the
			 * replacement unit.
			 * @return decoded unit, never an invalid code point.
			 */
			inline Unit Decode (std::string_view text, std::size_t offset)
			{
				Unit unit;
				if (offset >= text.size ()) return unit;

				const auto lead = static_cast<unsigned char> (text[offset]);
				if (lead < 0x80)
				{
					unit.codePoint = lead;
					return unit;
				}

				std::size_t needed = 0;
				std::uint32_t value = 0;
				// The second byte carries the shortest-form and scalar
				// constraints; the remaining continuations are 80..BF.
				unsigned char secondMinimum = 0x80;
				unsigned char secondMaximum = 0xBF;
				if (lead >= 0xC2 && lead <= 0xDF)
				{
					needed = 1;
					value = lead & 0x1F;
				}
				else if (lead >= 0xE0 && lead <= 0xEF)
				{
					needed = 2;
					value = lead & 0x0F;
					if (lead == 0xE0)
						secondMinimum = 0xA0; // overlong below U+0800
					else if (lead == 0xED)
						secondMaximum = 0x9F; // encoded surrogates
				}
				else if (lead >= 0xF0 && lead <= 0xF4)
				{
					needed = 3;
					value = lead & 0x07;
					if (lead == 0xF0)
						secondMinimum = 0x90; // overlong below U+10000
					else if (lead == 0xF4)
						secondMaximum = 0x8F; // above U+10FFFF
				}
				else
					return unit; // C0/C1, F5..FF or a stray continuation

				if (offset + needed >= text.size ()) return unit;
				for (std::size_t index = 1; index <= needed; ++index)
				{
					const auto next = static_cast<unsigned char> (text[offset + index]);
					if (index == 1)
					{
						if (next < secondMinimum || next > secondMaximum) return unit;
					}
					else if ((next & 0xC0) != 0x80)
						return unit;
					value = (value << 6) | (next & 0x3F);
				}

				unit.codePoint = value;
				unit.length = needed + 1;
				unit.width = Width (value);
				return unit;
			}

			/*
			 * Encode one code point as UTF-8.
			 *
			 * A surrogate or a value above U+10FFFF cannot be encoded;
			 * it becomes U+FFFD so the result is always well-formed UTF-8
			 * and never contains a five-byte form.
			 *
			 * @param codePoint code point to encode.
			 * @return 1 to 4 UTF-8 bytes.
			 */
			inline std::string Encode (std::uint32_t codePoint)
			{
				if (codePoint > 0x10FFFF || (codePoint >= 0xD800 && codePoint <= 0xDFFF))
					codePoint = REPLACEMENT_CHARACTER;
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

			/*
			 * Whether a byte range contains only complete UTF-8 scalars.
			 *
			 * @param text Input bytes; empty input is valid.
			 * @return False for invalid, overlong or truncated encodings.
			 */
			inline bool IsValidUtf8 (std::string_view text)
			{
				std::size_t offset = 0;
				while (offset < text.size ())
				{
					const auto unit = Decode (text, offset);
					if (unit.length == 1 && static_cast<unsigned char> (text[offset]) >= 0x80)
						return false;
					offset += unit.length;
				}
				return true;
			}

			/*
			 * Remove the final UTF-8 scalar without retaining its lead byte.
			 *
			 * Malformed suffixes lose only their final byte, so unrelated
			 * preceding text is preserved and repeated deletion progresses.
			 *
			 * Combining marks are separate scalars, not whole graphemes.
			 *
			 * @param value Mutable UTF-8 input; empty input is a no-op.
			 */
			inline void EraseLastUnit (std::string & value)
			{
				if (value.empty ()) return;
				std::size_t first = value.size () - 1;
				while (first > 0 && value.size () - first < 4
					&& (static_cast<unsigned char> (value[first]) & 0xC0) == 0x80)
					--first;
				const auto unit = Decode (value, first);
				if (first + unit.length == value.size ())
					value.erase (first);
				else
					value.pop_back ();
			}

			/*
			 * Length in bytes of the prefix that fits a column budget.
			 *
			 * Mirrors the ncurses backend: whole sequences only, a wide
			 * glyph that would exceed the budget is not written at all,
			 * and a negative budget means "whole string". A malformed
			 * byte occupies one column, so rejected input still advances
			 * the cursor instead of stalling the row.
			 * Trailing combining marks remain with their fitting base even
			 * at the exact budget; a zero budget always returns no bytes.
			 * @param text UTF-8 text; must outlive the call.
			 * @param limit column budget, or a negative value for none.
			 * @return byte length of the fitting prefix (0..text.size()).
			 */
			inline std::size_t FitLength (std::string_view text, int limit)
			{
				if (limit == 0) return 0;
				int columns = 0;
				std::size_t offset = 0;
				while (offset < text.size ())
				{
					const Unit unit = Decode (text, offset);
					if (limit >= 0 && columns + unit.width > limit) break;
					columns += unit.width;
					offset += unit.length;
				}
				return offset;
			}
		}
	}
}
}

#endif // OPENI2PD_TUI_TERMINAL_TEXT_H
