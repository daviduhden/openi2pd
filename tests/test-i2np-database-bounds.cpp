/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test I2NP database bounds.
 *
 * Negative vectors for the pure DatabaseSearchReply body parser: missing key,
 * truncated peer list/from hash, count overflow and trailing bytes. The parser
 * deliberately ignores trailing bytes like Java's readMessage().
 *
 */

#include <cassert>
#include <cstdint>
#include <vector>

#include "I2NPProtocol.h"

int main ()
{
	std::vector<uint8_t> buf (65, 0); // key[32], count=0, from[32]
	i2p::DatabaseSearchReply reply;

	assert (!i2p::ParseDatabaseSearchReply (nullptr, 0, reply));
	assert (!i2p::ParseDatabaseSearchReply (buf.data (), 64, reply));
	assert (i2p::ParseDatabaseSearchReply (buf.data (), buf.size (), reply));
	assert (reply.peers.empty ());

	// A declared peer without its 32-byte hash and the trailing "from" hash is
	// rejected instead of reading past the buffer.
	buf[32] = 1;
	assert (!i2p::ParseDatabaseSearchReply (buf.data (), buf.size (), reply));
	assert (!i2p::ParseDatabaseSearchReply (buf.data (), 32 + 1 + 31, reply));

	// One peer requires 65 + 32 bytes; 255 peers cannot fit a short buffer.
	buf.resize (97, 0);
	buf[32] = 1;
	assert (i2p::ParseDatabaseSearchReply (buf.data (), buf.size (), reply));
	assert (reply.peers.size () == 1);
	buf[32] = 255;
	assert (!i2p::ParseDatabaseSearchReply (buf.data (), buf.size (), reply));

	// Trailing bytes are ignored by design (Java parity), but the mandatory
	// key/from layout still has to be present.
	buf.resize (66, 0);
	buf[32] = 0;
	assert (i2p::ParseDatabaseSearchReply (buf.data (), buf.size (), reply));

	return 0;
}
