/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test tui pipe name.
 *
 * Checks portable Windows named-pipe name derivation: one-way endpoint normalization, ASCII case folding and a stable hashed pipe name so daemon and client agree; no pipe is created.
 *
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
