/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: fuzz protocol bounds.
 *
 * Exercises IdentityEx, LeaseSet, I2NP length, RouterInfo and SSU2
 * payload-block parsing on untrusted bytes, requiring truncation and
 * overruns to fail without a running router.
 *
 */

#include <cstddef>
#include <cstdint>
#include <memory>

#include "I2NPProtocol.h"
#include "Identity.h"
#include "LeaseSet.h"
#include "RouterInfo.h"
#include "SSU2Session.h"

// Exercises the untrusted parsers that do not require a running router.
// Intended for the WITH_FUZZERS libFuzzer targets.
extern "C" int LLVMFuzzerTestOneInput (const uint8_t * data, size_t size)
{
	i2p::data::IdentityEx identity;
	(void)identity.FromBuffer (data, size);

	uint64_t expires = 0;
	(void)i2p::data::LeaseSetBufferValidate (data, size, expires);
	(void)i2p::GetI2NPMessageLength (data, size);

	i2p::data::RouterInfo routerInfo (data, size);
	(void)routerInfo.IsUnreachable ();

	// Pure SSU2 payload framing/state whitelist. No socket, session or
	// router state is touched, so this can run on arbitrary input.
	using i2p::transport::SSU2SessionState;
	const SSU2SessionState states[] = {
		i2p::transport::eSSU2SessionStateTokenRequestReceived,
		i2p::transport::eSSU2SessionStateSessionRequestReceived,
		i2p::transport::eSSU2SessionStateSessionCreatedReceived,
		i2p::transport::eSSU2SessionStateTokenReceived,
		i2p::transport::eSSU2SessionStateSessionCreatedSent,
		i2p::transport::eSSU2SessionStateEstablished,
	};
	for (auto state : states) (void)i2p::transport::ValidateSSU2PayloadBlocks (state, data, size);
	return 0;
}
