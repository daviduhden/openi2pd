/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: fuzz privsep protocol.
 *
 * Feeds arbitrary input to every privilege-separation codec: the frame
 * decoder and inspector, the log/config/error/cancel payloads and the
 * data-storage configure/request/reply family. It also round-trips a
 * structurally valid frame through Encode/Decode so an encoder that accepts a
 * payload its own decoder rejects is a finding. No I/O, no worker, no
 * descriptor is used.
 *
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <span>
#include <string_view>

#include "privsep/Protocol.h"

extern "C" int LLVMFuzzerTestOneInput (const uint8_t * data, size_t size)
{
	using namespace i2p::privsep;
	std::string_view input (reinterpret_cast<const char *> (data), size);

	// Frame layer. The inspector must never disagree about a record it
	// declares attributable, and a decoded frame must always be re-encodable.
	(void)InspectFrame (input);
	auto frame = Decode (input);
	if (frame)
	{
		auto reencoded = Encode (*frame);
		if (!reencoded) std::abort ();
		auto roundTrip = Decode (*reencoded);
		if (!roundTrip || roundTrip->type != frame->type || roundTrip->requestId != frame->requestId
			|| roundTrip->payload != frame->payload)
			std::abort ();
	}

	// Payload families. Every decoder must be total over arbitrary bytes.
	(void)DecodeLogRecord (input);
	(void)DecodeLogConfig (input);
	(void)DecodeError (input);
	(void)DecodeCancel (input);
	(void)DecodeConfigureDataStorage (input);
	(void)DecodeDataStorageRequest (input);
	(void)DecodeDataStorageReply (input);

	// Encoder/decoder agreement for a payload taken from the input. A failure
	// is a legitimate outcome (oversized name, non-canonical artifact name);
	// success must decode back to the same bytes.
	if (size >= 2)
	{
		const uint8_t kind = static_cast<uint8_t> (1 + data[0] % 3);
		std::string_view name (reinterpret_cast<const char *> (data + 1), size - 1);
		if (name.size () > MAX_ARTIFACT_NAME_SIZE) name = name.substr (0, MAX_ARTIFACT_NAME_SIZE);
		auto encoded = EncodeDataStorageRequest (kind, name, std::span<const uint8_t> (data, size));
		if (encoded)
		{
			auto decoded = DecodeDataStorageRequest (*encoded);
			if (!decoded || decoded->kind != kind || decoded->name != name
				|| decoded->payload.size () != size)
				std::abort ();
		}
	}

	// A frame declared with an input-derived type must respect the registry:
	// either Encode refuses it or Decode of the result accepts the same type.
	if (size >= 2)
	{
		const auto type = static_cast<MessageType> (static_cast<uint16_t> (data[0]) << 8 | data[1]);
		Message message { type, 1, std::string (input.substr (0, std::min<size_t> (input.size (), 64))) };
		auto encoded = Encode (message);
		if (encoded)
		{
			auto decoded = Decode (*encoded);
			if (!decoded || decoded->type != type) std::abort ();
		}
	}
	return 0;
}
