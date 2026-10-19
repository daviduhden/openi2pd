/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TuiWin32Ipc.
 *
 * Normalises configured local endpoints and hashes them with 64-bit FNV-1a to
 * produce the named-pipe path mounted under \\.\pipe\openi2pd-tui-. ASCII case
 * is folded and separators are collapsed, so equivalent endpoint spellings
 * yield the same bounded pipe name on both sides.
 *
 */

#include "TuiWin32Ipc.h"

#include <cstdint>
#include <iomanip>
#include <sstream>

namespace i2p
{
namespace tui
{
	namespace
	{
		constexpr uint64_t FNV1A_OFFSET_BASIS = 14695981039346656037ULL;
		constexpr uint64_t FNV1A_PRIME = 1099511628211ULL;
	}

	std::string NormalizeWin32Endpoint (std::string_view endpoint)
	{
		while (!endpoint.empty () && (endpoint.front () == ' ' || endpoint.front () == '\t'))
			endpoint.remove_prefix (1);
		while (!endpoint.empty () && (endpoint.back () == ' ' || endpoint.back () == '\t')) endpoint.remove_suffix (1);

		std::string normalized;
		normalized.reserve (endpoint.size ());
		bool previousSeparator = false;
		for (char ch : endpoint)
		{
			if (ch == '/') ch = '\\';
			if (ch == '\\')
			{
				// Collapse repeated separators and drop trailing ones. Both
				// sides apply the same rule, so path spelling does not fork
				// the pipe name.
				if (previousSeparator) continue;
				previousSeparator = true;
			}
			else
				previousSeparator = false;
			if (ch >= 'A' && ch <= 'Z') ch = static_cast<char> (ch - 'A' + 'a');
			normalized.push_back (ch);
		}
		while (!normalized.empty () && normalized.back () == '\\') normalized.pop_back ();
		return normalized;
	}

	std::wstring MakeWin32PipeName (std::string_view endpoint)
	{
		const std::string normalized = NormalizeWin32Endpoint (endpoint);
		uint64_t hash = FNV1A_OFFSET_BASIS;
		for (unsigned char ch : normalized)
		{
			hash ^= ch;
			hash *= FNV1A_PRIME;
		}

		std::wostringstream name;
		name << L"\\\\.\\pipe\\openi2pd-tui-" << std::hex << std::nouppercase << std::setw (16) << std::setfill (L'0')
			 << hash;
		return name.str ();
	}
}
}
