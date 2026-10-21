/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test SOCKS address buffer.
 *
 * Regression for zero-length and maximum-length SOCKS5 domain names. The old
 * buffer wrapped its uint8_t length on a zero-length name and wrote past the
 * 255-byte union member.
 */

#include <iostream>
#include <string>

#include "SOCKSAddress.h"

namespace
{
bool Check (bool condition, const char * message)
{
	if (!condition) std::cerr << "socks-address: " << message << std::endl;
	return condition;
}
}

int main ()
{
	i2p::proxy::SOCKSDnsAddress address {};

	if (!Check (address.GetString ().empty (), "zero-initialized address is empty"))
		return 1;
	if (!Check (address.push_back ('a'), "single byte append"))
		return 1;
	if (!Check (address.GetString () == "a", "single byte length"))
		return 1;

	i2p::proxy::SOCKSDnsAddress maximum {};
	const std::string maximumText (i2p::proxy::SOCKS_MAX_HOSTNAME_SIZE, 'x');
	if (!Check (maximum.SetString (maximumText), "maximum-length name accepted"))
		return 1;
	if (!Check (maximum.GetString ().size () == i2p::proxy::SOCKS_MAX_HOSTNAME_SIZE, "maximum length retained"))
		return 1;
	if (!Check (!maximum.push_back ('x'), "append beyond capacity rejected"))
		return 1;

	const std::string tooLong (i2p::proxy::SOCKS_MAX_HOSTNAME_SIZE + 1, 'x');
	i2p::proxy::SOCKSDnsAddress overflow {};
	if (!Check (!overflow.SetString (tooLong), "oversized name rejected"))
		return 1;

	return 0;
}
