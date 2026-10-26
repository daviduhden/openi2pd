/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: reseed netid URL composition regression.
 *
 * Proposal 147 appends the router network ID to a reseed URL. A `netid`
 * parameter is matched as a complete query parameter (never as a raw
 * substring), the fragment is stripped and the composition is idempotent.
 * Compile-free logic only: no socket, no thread.
 *
 */

#include <cassert>
#include <string>

#include "Reseed.h"

int main ()
{
	using i2p::data::AppendReseedNetId;

	// No query: the parameter opens the query.
	assert (AppendReseedNetId ("https://example.invalid/i2pseeds.su3", 2)
		== "https://example.invalid/i2pseeds.su3?netid=2");

	// Existing query: the parameter is appended.
	assert (AppendReseedNetId ("https://example.invalid/i2pseeds.su3?x=1", 2)
		== "https://example.invalid/i2pseeds.su3?x=1&netid=2");

	// A real netid parameter is never duplicated.
	assert (AppendReseedNetId ("https://example.invalid/i2pseeds.su3?netid=7", 2)
		== "https://example.invalid/i2pseeds.su3?netid=7");
	assert (AppendReseedNetId ("https://example.invalid/i2pseeds.su3?x=1&netid=7", 2)
		== "https://example.invalid/i2pseeds.su3?x=1&netid=7");
	assert (AppendReseedNetId ("https://example.invalid/i2pseeds.su3?netid", 2)
		== "https://example.invalid/i2pseeds.su3?netid");

	// A spoofed match in another parameter name or value is not a netid.
	assert (AppendReseedNetId ("https://example.invalid/i2pseeds.su3?xnetid=9", 2)
		== "https://example.invalid/i2pseeds.su3?xnetid=9&netid=2");
	assert (AppendReseedNetId ("https://example.invalid/i2pseeds.su3?x=netid=9", 2)
		== "https://example.invalid/i2pseeds.su3?x=netid=9&netid=2");
	assert (AppendReseedNetId ("https://example.invalid/i2pseeds.su3?xnetid=9&y=2", 2)
		== "https://example.invalid/i2pseeds.su3?xnetid=9&y=2&netid=2");

	// The fragment is never sent to the server and must not swallow the
	// parameter.
	assert (AppendReseedNetId ("https://example.invalid/i2pseeds.su3#fragment", 2)
		== "https://example.invalid/i2pseeds.su3?netid=2");
	assert (AppendReseedNetId ("https://example.invalid/i2pseeds.su3?x=1#fragment", 2)
		== "https://example.invalid/i2pseeds.su3?x=1&netid=2");
	assert (AppendReseedNetId ("https://example.invalid/i2pseeds.su3#fragment?netid=9", 2)
		== "https://example.invalid/i2pseeds.su3?netid=2");

	// A trailing separator is reused instead of producing "?&".
	assert (AppendReseedNetId ("https://example.invalid/i2pseeds.su3?", 2)
		== "https://example.invalid/i2pseeds.su3?netid=2");
	assert (AppendReseedNetId ("https://example.invalid/i2pseeds.su3?x=1&", 2)
		== "https://example.invalid/i2pseeds.su3?x=1&netid=2");

	// Idempotent: a second pass cannot append a duplicate parameter.
	for (const char * url : { "https://example.invalid/i2pseeds.su3",
			 "https://example.invalid/i2pseeds.su3?x=1", "https://example.invalid/i2pseeds.su3#f",
			 "https://example.invalid/i2pseeds.su3?x=netid=9" })
	{
		const auto once = AppendReseedNetId (url, 2);
		assert (AppendReseedNetId (once, 2) == once);
		assert (once.find ("netid=2") != std::string::npos);
	}

	// Network IDs other than the default keep their own value.
	assert (AppendReseedNetId ("https://example.invalid/i2pseeds.su3", 0)
		== "https://example.invalid/i2pseeds.su3?netid=0");
	assert (AppendReseedNetId ("https://example.invalid/i2pseeds.su3", 42)
		== "https://example.invalid/i2pseeds.su3?netid=42");

	return 0;
}
