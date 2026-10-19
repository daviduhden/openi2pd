/*
 * Copyright (c) 2026, David Uhden Collado
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Portable checks for the shared terminal key/attribute contract in
 * tui/Terminal.h. This does not initialise ncurses or a Win32 console and
 * therefore does not validate platform rendering.
 */

#include <cassert>
#include <cstddef>

#include "Terminal.h"

using namespace i2p::tui::terminal;

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

int main ()
{
	TestKeys ();
	TestAttributes ();
	return 0;
}
