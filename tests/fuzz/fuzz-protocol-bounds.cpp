/*
* Copyright (c) 2026, The PurpleI2P Project
*
* This file is part of Open I2Pd and licensed under BSD3
*
* See full license text in LICENSE file at top of project tree
*/

#include <cstddef>
#include <cstdint>
#include <memory>

#include "I2NPProtocol.h"
#include "Identity.h"
#include "LeaseSet.h"
#include "RouterInfo.h"

// Exercises the untrusted parsers that do not require a running router.
// Intended for the WITH_FUZZERS libFuzzer targets.
extern "C" int LLVMFuzzerTestOneInput (const uint8_t * data, size_t size)
{
	i2p::data::IdentityEx identity;
	(void) identity.FromBuffer (data, size);

	uint64_t expires = 0;
	(void) i2p::data::LeaseSetBufferValidate (data, size, expires);
	(void) i2p::GetI2NPMessageLength (data, size);

	i2p::data::RouterInfo routerInfo (data, size);
	(void) routerInfo.IsUnreachable ();
	return 0;
}
