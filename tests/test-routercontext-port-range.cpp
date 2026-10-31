/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test router port reservation.
 *
 * Pins the reserved TCP/UDP port table used by
 * RouterContext::SelectRandomPort, including the Intel AMT range added
 * upstream in 37090fe0. It is not executed during static review and
 * runs only in manual CI.
 */

#include <cstdint>
#include <iostream>
#include <unordered_set>

#include "util.h"

int main ()
{
	const std::unordered_set<uint16_t> expected { 9119, 9150, 9306, 9312, 9389, 9418, 9535, 9536, 9695, 9800, 9899,
		10000, 10050, 10051, 10110, 10212, 10933, 11001, 11112, 11235, 11371, 12222, 12223, 13075, 13400, 13720, 13721,
		13724, 13782, 13783, 13785, 13786, 15345, 16992, 16993, 16994, 16995, 17224, 17225, 17500, 18104, 19788, 19812,
		19813, 19814, 19999, 20000, 24465, 24554, 26000, 27000, 27001, 27002, 27003, 27004, 27005, 27006, 27007, 27008,
		27009, 28000 };

	for (uint32_t port = 0; port <= 0xFFFF; ++port)
	{
		const bool expectedReserved = expected.contains (static_cast<uint16_t> (port));
		const bool actualReserved = i2p::util::net::IsPortInReservedRange (static_cast<uint16_t> (port));
		if (expectedReserved != actualReserved)
		{
			std::cerr << "reserved-port mismatch at " << port << ": expected " << expectedReserved << ", got "
					  << actualReserved << std::endl;
			return 1;
		}
	}

	// Keep the Intel AMT boundaries explicit so a future table edit cannot
	// silently widen the reserved range.
	if (i2p::util::net::IsPortInReservedRange (16991) || !i2p::util::net::IsPortInReservedRange (16992)
		|| !i2p::util::net::IsPortInReservedRange (16995) || i2p::util::net::IsPortInReservedRange (16996))
	{
		std::cerr << "Intel AMT reserved-port boundary is wrong" << std::endl;
		return 1;
	}

	return 0;
}
