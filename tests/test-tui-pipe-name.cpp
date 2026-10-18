/*
 * Copyright (c) 2013-2026, The PurpleI2P Project
 * Copyright (c) 2026, David Uhden Collado
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Portable tests for the Windows named-pipe name derivation.
 *
 * TuiWin32Ipc.cpp only uses the C++ standard library, so this test runs on
 * Unix too. It validates the one-way endpoint normalisation and the stable
 * pipe name; it does not create a pipe or inspect an ACL.
 */

#include <cassert>
#include <string>
#include <string_view>

#include "TuiWin32Ipc.h"

using namespace i2p::tui;

static void TestNormalization ()
{
	assert (NormalizeWin32Endpoint ("") == "");
	assert (NormalizeWin32Endpoint ("   ") == "");
	assert (NormalizeWin32Endpoint ("  C:\\I2PD///i2pd-tui.sock  ") == "c:\\i2pd\\i2pd-tui.sock");
	assert (NormalizeWin32Endpoint ("/var/i2pd/i2pd-tui.sock") == "\\var\\i2pd\\i2pd-tui.sock");
	assert (NormalizeWin32Endpoint ("C:\\\\Data\\\\i2pd\\\\") == "c:\\data\\i2pd");

	// Only ASCII is case-folded; non-ASCII bytes are preserved.
	const std::string utf8 = "C:\\donn\xC3\xA9"
							 "es\\i2pd";
	assert (NormalizeWin32Endpoint (utf8)
		== "c:\\donn\xC3\xA9"
		   "es\\i2pd");
}

static void TestPipeName ()
{
	constexpr std::wstring_view prefix = L"\\\\.\\pipe\\openi2pd-tui-";

	const std::wstring first = MakeWin32PipeName ("C:\\Data\\i2pd");
	assert (first.size () == prefix.size () + 16);
	assert (first.compare (0, prefix.size (), prefix) == 0);
	for (size_t i = prefix.size (); i < first.size (); ++i)
		assert ((first[i] >= L'0' && first[i] <= L'9') || (first[i] >= L'a' && first[i] <= L'f'));

	// Case, separator spelling and trailing separators must not fork the
	// name used by the daemon and the client.
	assert (MakeWin32PipeName ("c:/data/I2PD/") == first);
	assert (MakeWin32PipeName ("C:\\\\Data\\\\i2pd") == first);
	assert (MakeWin32PipeName ("C:\\Data\\i2pd\\") == first);

	assert (MakeWin32PipeName ("C:\\Data\\other") != first);
	assert (MakeWin32PipeName ("") != first);
	assert (MakeWin32PipeName ("") == MakeWin32PipeName ("  "));

	// A caller-provided full pipe path is hashed, never opened verbatim.
	const std::wstring arbitrary = MakeWin32PipeName ("\\\\.\\pipe\\custom");
	assert (arbitrary.rfind (prefix, 0) == 0);
	assert (arbitrary.find (L"custom") == std::wstring::npos);
	assert (arbitrary.size () < 256);
}

int main ()
{
	TestNormalization ();
	TestPipeName ();
	return 0;
}
