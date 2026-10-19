/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test http url decode.
 *
 * Checks percent-decoding of UTF-8 and embedded NUL bytes, including the option that preserves or decodes %00.
 *
 */

#include <cassert>
#include "HTTP.h"

using namespace i2p::http;

int main ()
{
	std::string in ("/%D1%81%D1%82%D1%80%D0%B0%D0%BD%D0%B8%D1%86%D0%B0/");
	std::string out = UrlDecode (in);

	assert (strcmp (out.c_str (), "/\xd1\x81\xd1\x82\xd1\x80\xd0\xb0\xd0\xbd\xd0\xb8\xd1\x86\xd0\xb0/") == 0);

	in = "/%00/";
	out = UrlDecode (in, false);
	assert (strcmp (out.c_str (), "/%00/") == 0);
	out = UrlDecode (in, true);
	assert (strcmp (out.c_str (), "/\0/") == 0);

	return 0;
}
