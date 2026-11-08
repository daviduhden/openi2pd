/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test tui terminal.
 *
 * Verifies the shared terminal key/attribute contract: special keys
 * stay above printable bytes, and attribute, color-pair and color bits
 * do not overlap; ncurses/Win32 rendering is not exercised.
 *
 */

#include <cassert>
#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>

#include "Terminal.h"
#include "TerminalText.h"
#include "Theme.h"

using namespace i2p::tui::terminal;

static void TestColorFgbgParsing ()
{
	using i2p::tui::theme_detail::IsLightBackground;
	using i2p::tui::theme_detail::ParseColorFgbgBackground;

	const auto light = ParseColorFgbgBackground ("0;15");
	assert (light && *light == 15);
	assert (IsLightBackground (*light));

	const auto dark = ParseColorFgbgBackground ("15;0");
	assert (dark && *dark == 0);
	assert (!IsLightBackground (*dark));

	const auto sequence = ParseColorFgbgBackground ("1;2:3;15");
	assert (sequence && *sequence == 15);
	const auto lastPair = ParseColorFgbgBackground ("0;15:15;0");
	assert (lastPair && *lastPair == 0);
	const auto highestIndex = ParseColorFgbgBackground ("0;255");
	assert (highestIndex && *highestIndex == 255);

	const std::string_view malformed[] = {
		"", "15", ";15", "0;", "0;;15", "0;15;2", "0;15:", ":0;15", "0;15:2", "0;15::2;3",
		"fg0;15", "0;15tail", " 0;15", "0;15 ", "256;15", "0;256", "999999999999999999999999;15"
	};
	for (const auto value : malformed) assert (!ParseColorFgbgBackground (value));
}

static void TestKeys ()
{
	static_assert (KEY_NONE < 0);
	static_assert (KEY_CTRL_C == 3);

	constexpr int keys[] = { KEY_RESIZE, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_HOME, KEY_END, KEY_PPAGE, KEY_NPAGE,
		KEY_ENTER, KEY_BACKSPACE, KEY_DC, KEY_IC, KEY_BTAB, KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7,
		KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12 };
	constexpr size_t count = sizeof (keys) / sizeof (keys[0]);
	for (size_t i = 0; i < count; ++i)
	{
		// Special keys must not collide with a printable byte.
		assert (keys[i] > 0xFF);
		for (size_t j = i + 1; j < count; ++j) assert (keys[i] != keys[j]);
	}
	assert (KEY_F12 > KEY_F1);
}

static void TestAttributes ()
{
	static_assert (ATTR_NORMAL == 0);
	static_assert (ATTR_BOLD == (1 << 0));
	static_assert (ATTR_DIM == (1 << 1));
	static_assert (ATTR_UNDERLINE == (1 << 2));
	static_assert (ATTR_REVERSE == (1 << 3));

	assert ((ATTR_BOLD & ATTR_DIM) == 0);
	assert ((ATTR_BOLD | ATTR_DIM) != ATTR_BOLD);
	assert ((ATTR_BOLD | ATTR_DIM | ATTR_UNDERLINE | ATTR_REVERSE) == 0x0F);

	static_assert (PAIR_SHIFT == 16);
	static_assert (PAIR_MASK == 0xFF);
	static_assert (MAX_PAIR == 255);

	assert (ColorPair (0) == 0);
	assert (ColorPair (1) == (1 << PAIR_SHIFT));
	assert (ColorPair (MAX_PAIR) == (MAX_PAIR << PAIR_SHIFT));
	// Only the low byte is part of the pair field.
	assert (ColorPair (MAX_PAIR + 1) == ColorPair (0));
	assert (ColorPair (-1) == ColorPair (MAX_PAIR));

	// Attributes and colours occupy disjoint bits.
	assert ((ATTR_BOLD & ColorPair (1)) == 0);
	assert ((ATTR_REVERSE | ColorPair (2)) == ((1 << 3) | (2 << PAIR_SHIFT)));

	static_assert (COLOR_DEFAULT == -1);
	assert (COLOR_BLACK == 0 && COLOR_RED == 1 && COLOR_GREEN == 2 && COLOR_YELLOW == 3);
	assert (COLOR_BLUE == 4 && COLOR_MAGENTA == 5 && COLOR_CYAN == 6 && COLOR_WHITE == 7);
}

static void TestTextDecoding ()
{
	// The Windows backend owns its own UTF-8 decode because the VT stream
	// has no locale-backed width function; these cases pin the policy,
	// including the strict rejection of forms that must never reach the
	// console or the cell buffer.
	{
		const auto unit = text::Decode ("A", 0);
		assert (unit.codePoint == 'A' && unit.length == 1 && unit.width == 1);
	}
	{
		// U+041D CYRILLIC CAPITAL LETTER EN: two bytes, one column.
		const auto unit = text::Decode ("\xD0\x9D", 0);
		assert (unit.codePoint == 0x041D && unit.length == 2 && unit.width == 1);
	}
	{
		// U+4E2D CJK: three bytes, two columns.
		const auto unit = text::Decode ("\xE4\xB8\xAD", 0);
		assert (unit.codePoint == 0x4E2D && unit.length == 3 && unit.width == 2);
	}
	{
		// U+1F600: four bytes, two columns.
		const auto unit = text::Decode ("\xF0\x9F\x98\x80", 0);
		assert (unit.codePoint == 0x1F600 && unit.length == 4 && unit.width == 2);
	}

	// Malformed input is exactly one replacement unit of width one, so the
	// caller's cursor still advances by one column.
	const std::string_view malformed[] = {
		"\xC0\xAF", // overlong solidus
		"\xC1\xBF", // overlong, two-byte encoding of 0x7F
		"\xE0\x80\xAF", // overlong, three-byte
		"\xF0\x80\x80\xAF", // overlong, four-byte
		"\xED\xA0\x80", // encoded surrogate U+D800
		"\xED\xBF\xBF", // encoded surrogate U+DFFF
		"\xF4\x90\x80\x80", // above U+10FFFF
		"\xF5\x80\x80\x80", // invalid lead byte
		"\xE4\xB8", // truncated tail
		"\xE4" "a", // bad continuation
		"\x80", // stray continuation byte
		"\xFF", // never a lead byte
	};
	for (const auto sample : malformed)
	{
		const auto unit = text::Decode (sample, 0);
		assert (unit.codePoint == text::REPLACEMENT_CHARACTER);
		assert (unit.length == 1);
		assert (unit.width == 1);
	}

	// An offset outside the text cannot read past the end.
	{
		const auto unit = text::Decode ("abc", 3);
		assert (unit.codePoint == text::REPLACEMENT_CHARACTER && unit.length == 1);
	}

	// Width is the column contract shared with Put().
	assert (text::Width ('A') == 1);
	assert (text::Width (0x0301) == 0); // combining acute accent
	assert (text::Width (0x20DD) == 0); // combining enclosing circle
	assert (text::Width (0x041D) == 1);
	assert (text::Width (0x4E2D) == 2);
	assert (text::Width (0xFF21) == 2); // fullwidth A
	assert (text::Width (0x1F600) == 2);
	assert (text::Width (text::REPLACEMENT_CHARACTER) == 1);
}

static void TestTextEncoding ()
{
	// Encoding is total: a value that has no UTF-8 form becomes the
	// replacement character instead of a four-byte sequence whose lead
	// byte would exceed 0xF4.
	assert (text::Encode ('A') == "A");
	assert (text::Encode (0x041D) == "\xD0\x9D");
	assert (text::Encode (0x4E2D) == "\xE4\xB8\xAD");
	assert (text::Encode (0x1F600) == "\xF0\x9F\x98\x80");
	assert (text::Encode (0xD800) == "\xEF\xBF\xBD");
	assert (text::Encode (0xDFFF) == "\xEF\xBF\xBD");
	assert (text::Encode (0x110000) == "\xEF\xBF\xBD");
	assert (text::Encode (0xFFFFFFFF) == "\xEF\xBF\xBD");

	// Every encode/decode round trip keeps the code point and its width.
	const std::uint32_t scalars[] = { 'A', 0x7F, 0x80, 0x7FF, 0x800, 0x041D, 0x4E2D, 0xFF21, 0xFFFD, 0x10000, 0x1F600,
		0x10FFFF };
	for (const auto scalar : scalars)
	{
		const std::string encoded = text::Encode (scalar);
		assert (!encoded.empty () && encoded.size () <= 4);
		const auto unit = text::Decode (encoded, 0);
		assert (unit.codePoint == scalar);
		assert (unit.length == encoded.size ());
		assert (unit.width == text::Width (scalar));
	}
}

static void TestColumnBudget ()
{
	// Put()'s limit is a terminal-column budget, not a byte count. The
	// Windows previously used byte length, truncating translated labels
	// early and charging three columns for a two-column CJK glyph.
	assert (text::FitLength ("abc", 3) == 3);
	assert (text::FitLength ("abc", 2) == 2);
	assert (text::FitLength ("abc", 0) == 0);
	assert (text::FitLength ("abc", -1) == 3);
	assert (text::FitLength ("", 4) == 0);

	// Two Cyrillic capitals: four bytes, two columns.
	const std::string_view cyrillic = "\xD0\x9D\xD0\xB0";
	assert (text::FitLength (cyrillic, 2) == 4);
	assert (text::FitLength (cyrillic, 1) == 2);
	assert (text::FitLength (cyrillic, 3) == 4);

	// Two wide glyphs: six bytes, four columns. A wide glyph is never
	// split across the budget.
	const std::string_view wide = "\xE4\xB8\xAD\xE4\xB8\xAD";
	assert (text::FitLength (wide, 0) == 0);
	assert (text::FitLength (wide, 1) == 0);
	assert (text::FitLength (wide, 2) == 3);
	assert (text::FitLength (wide, 3) == 3);
	assert (text::FitLength (wide, 4) == 6);

	// A combining mark costs no column and stays attached to a fitting
	// base at the exact budget. A zero budget must still emit no bytes.
	const std::string_view combining = "e\xCC\x81";
	assert (text::FitLength (combining, 0) == 0);
	assert (text::FitLength (combining, 1) == 3);
	assert (text::FitLength (combining, 2) == 3);

	// A malformed byte occupies one column, so a hostile sequence cannot
	// stall the row by costing zero.
	assert (text::FitLength ("\xC0\xAF", 1) == 1);
	assert (text::FitLength ("\xC0\xAF", 0) == 0);

	// The invariant behind the fix: the fitting prefix never exceeds the
	// budget when measured in columns, and it stops only where the next
	// unit would not fit.
	const std::string_view samples[] = { "openi2pd TUI", cyrillic, wide, combining,
		"Espa\xC3\xB1ol", "T\xC3\xBCrk\xC3\xA7" "e", "\xE4\xB8\xAD" "abc" };
	for (const auto sample : samples)
	{
		for (int limit = 0; limit <= 8; ++limit)
		{
			const auto length = text::FitLength (sample, limit);
			assert (length <= sample.size ());
			int columns = 0;
			std::size_t offset = 0;
			while (offset < length)
			{
				const auto unit = text::Decode (sample, offset);
				assert (offset + unit.length <= length);
				columns += unit.width;
				offset += unit.length;
			}
			assert (columns <= limit);
			if (length < sample.size ())
			{
				// Except for an explicitly empty budget, the next unit must
				// exceed the available columns; combining marks stay attached.
				const auto next = text::Decode (sample, length);
				if (limit == 0)
					assert (length == 0);
				else
					assert (columns + next.width > limit);
			}
		}
	}
}

static void TestUtf8Backspace ()
{
	const std::string_view scalars[] = {
		"A", "\xC3\xB1", "\xD0\xAF", "\xE4\xB8\xAD", "\xF0\x9F\x98\x80", "\xEF\xBF\xBD"
	};
	for (const auto scalar : scalars)
	{
		assert (text::IsValidUtf8 (scalar));
		std::string input = "prefix" + std::string (scalar);
		text::EraseLastUnit (input);
		assert (input == "prefix");
	}
	std::string combined = "e\xCC\x81";
	assert (text::IsValidUtf8 (combined));
	text::EraseLastUnit (combined);
	assert (combined == "e");
	text::EraseLastUnit (combined);
	assert (combined.empty ());
	assert (text::IsValidUtf8 (combined));
	text::EraseLastUnit (combined);
	assert (combined.empty ());

	// Invalid/truncated input cannot consume an unrelated valid prefix.
	for (const std::string_view suffix : { "\x80", "\xE4\xB8", "\xED\xA0\x80", "\xF4\x90\x80\x80" })
	{
		assert (!text::IsValidUtf8 (suffix));
		std::string input = "prefix" + std::string (suffix);
		for (size_t remaining = suffix.size (); remaining > 0; --remaining)
		{
			const auto previous = input.size ();
			text::EraseLastUnit (input);
			assert (input.size () == previous - 1);
		}
		assert (input == "prefix");
	}
}

int main ()
{
	TestColorFgbgParsing ();
	TestKeys ();
	TestAttributes ();
	TestTextDecoding ();
	TestTextEncoding ();
	TestColumnBudget ();
	TestUtf8Backspace ();
	return 0;
}
