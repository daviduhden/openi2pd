/*
 * Copyright (c) 2026, David Uhden Collado
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Bounds fuzzer for the privilege-separated worker framing. It feeds the same
 * bytes to the frame decoder and both payload decoders, covering magic,
 * version, type, declared-length, string-length and trailing-byte checks.
 * No socket, fork or filesystem operation is performed.
 */

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "privsep/Protocol.h"

extern "C" int LLVMFuzzerTestOneInput (const uint8_t * data, size_t size)
{
	std::string_view input (reinterpret_cast<const char *> (data), size);
	(void)i2p::privsep::Decode (input);
	(void)i2p::privsep::DecodeLogRecord (input);
	(void)i2p::privsep::DecodeLogConfig (input);
	return 0;
}
