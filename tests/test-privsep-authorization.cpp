/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: privsep authorization.
 *
 * Compile-free authorization contract: the message registry is the only
 * authority for which role may receive which type, a worker never accepts a
 * reply and the request-issuing side never accepts a request, per-type version
 * and payload ceilings hold, the descriptor policy is explicit, and a
 * malformed or downgraded frame is refused without being reinterpreted.
 *
 */

#include <cassert>
#include <cstdint>
#include <string>
#include <string_view>

#include "privsep/Channel.h"
#include "privsep/Protocol.h"

using namespace i2p::privsep;

namespace
{
	constexpr EndpointRole kWorkers[] = { EndpointRole::Supervisor, EndpointRole::Log,
		EndpointRole::RouterStorage, EndpointRole::TorrentStorage, EndpointRole::StorageBroker,
		EndpointRole::DataStorage };

	std::string Frame (uint16_t version, uint16_t type, uint32_t requestId, std::string_view payload)
	{
		PayloadWriter writer;
		writer.WriteU32 (PROTOCOL_MAGIC);
		writer.WriteU16 (version);
		writer.WriteU16 (type);
		writer.WriteU32 (requestId);
		writer.WriteU32 (static_cast<uint32_t> (payload.size ()));
		writer.WriteRaw (payload);
		return writer.Data ();
	}

	void TestRegistry()
	{
		static_assert (ValidateMessageRegistry (), "registry must be complete and ordered");
		// Every declared value in the contiguous range has exactly one row.
		for (uint16_t value = 1; value <= MAX_MESSAGE_TYPE; ++value)
			assert (FindMessageSpec (value) != nullptr);
		assert (FindMessageSpec (MAX_MESSAGE_TYPE + 1) == nullptr);
	}

	void TestRoleDirections()
	{
		for (uint16_t value = 1; value <= MAX_MESSAGE_TYPE; ++value)
		{
			const auto type = static_cast<MessageType> (value);
			for (auto role : kWorkers)
			{
				if (!AllowsOperation (role, type)) continue;
				// Both are also clients: the supervisor issues spawn requests
				// to the broker and the broker issues worker operations. They
				// may therefore receive the correlated reply on their client
				// channel. Every other worker is a pure request receiver.
				if (role == EndpointRole::Supervisor)
				{
					assert (IsRequestType (type) || type == MessageType::Error);
					continue;
				}
				if (role == EndpointRole::StorageBroker)
				{
					assert (IsRequestType (type) || type == MessageType::Error
						|| type == MessageType::TorrentStorageSupervisorReply);
					continue;
				}
				// A pure worker receives requests and notifications, never a
				// reply: a worker cannot inject a reply into the core.
				assert (IsRequestType (type));
			}
			if (AllowsOperation (EndpointRole::Core, type))
			{
				// The requesting side receives replies only; a request type on
				// the core would let a worker drive the router. WorkerHello is
				// the one pre-dispatch spawn proof the server must read before
				// a role exists (Windows named-pipe backend).
				assert (IsReplyType (type) || type == MessageType::WorkerHello);
			}
		}
		assert (!AllowsOperation (EndpointRole::Count, MessageType::Ping));
		// Replies are not globally accepted: a worker cannot inject one.
		assert (!AllowsOperation (EndpointRole::DataStorage, MessageType::DataStorageReply));
		assert (!AllowsOperation (EndpointRole::RouterStorage, MessageType::RouterStorageReply));
		assert (!AllowsOperation (EndpointRole::TorrentStorage, MessageType::TorrentStorageReply));
		assert (!AllowsOperation (EndpointRole::Core, MessageType::DataStorageRequest));
		// Descriptor authority is narrow.
		assert (AllowsOperation (EndpointRole::Log, MessageType::ConfigureLogFd));
		assert (!AllowsOperation (EndpointRole::Supervisor, MessageType::ConfigureLogFd));
		assert (!AllowsOperation (EndpointRole::DataStorage, MessageType::ConfigureLogFd));
		// Cancel is a storage-only advisory.
		for (auto role : { EndpointRole::RouterStorage, EndpointRole::TorrentStorage, EndpointRole::DataStorage })
			assert (AllowsOperation (role, MessageType::Cancel));
		assert (!AllowsOperation (EndpointRole::Log, MessageType::Cancel));
		assert (!AllowsOperation (EndpointRole::Core, MessageType::Cancel));
	}

	void TestDescriptorPolicy()
	{
		assert (!DescriptorPresenceAllowed (MessageType::Ping, true));
		assert (DescriptorPresenceAllowed (MessageType::Ping, false));
		assert (DescriptorPresenceAllowed (MessageType::ConfigureLogFd, true));
		assert (!DescriptorPresenceAllowed (MessageType::ConfigureLogFd, false));
		assert (DescriptorPresenceAllowed (MessageType::ConfigureRouterStorage, false));
		assert (DescriptorPresenceAllowed (MessageType::ConfigureRouterStorage, true));
		assert (DescriptorPolicyFor (MessageType::DataStorageRequest) == DescriptorPolicy::None);
		assert (DescriptorPolicyFor (MessageType::TorrentStorageSupervisorReply) == DescriptorPolicy::OptionalSocket);
	}

	void TestVersionAndLimits()
	{
		// Cancel is version 4; the same frame in an older header is refused.
		const std::string cancelPayload = EncodeCancel (7);
		assert (Decode (Frame (PROTOCOL_VERSION, static_cast<uint16_t> (MessageType::Cancel), 9, cancelPayload)));
		auto downgraded
			= Decode (Frame (3, static_cast<uint16_t> (MessageType::Cancel), 9, cancelPayload));
		assert (!downgraded);
		assert (downgraded.error ().code == ErrorCode::UnsupportedVersion);
		// Data-storage frames require version 3.
		auto storageV2 = Decode (Frame (2, static_cast<uint16_t> (MessageType::DataStorageRequest), 1, ""));
		assert (!storageV2 && storageV2.error ().code == ErrorCode::UnsupportedVersion);
		// The global frame bound is not the per-type bound.
		const std::string oneByte (1, 'x');
		auto pingOver = Decode (Frame (PROTOCOL_VERSION, static_cast<uint16_t> (MessageType::Ping), 1, oneByte));
		assert (!pingOver && pingOver.error ().code == ErrorCode::TooLarge);
		// Encode applies the same ceiling.
		auto encoded = Encode (Message { MessageType::Ping, 1, oneByte });
		assert (!encoded && encoded.error ().code == ErrorCode::TooLarge);
		// Unknown type is a typed refusal, not an accepted frame.
		auto unknown = Decode (Frame (PROTOCOL_VERSION, MAX_MESSAGE_TYPE + 1, 1, ""));
		assert (!unknown && unknown.error ().code == ErrorCode::UnknownType);
	}

	void TestCancelCodec()
	{
		const auto encoded = EncodeCancel (42);
		auto decoded = DecodeCancel (encoded);
		assert (decoded && *decoded == 42);
		assert (!DecodeCancel (std::string (3, '\0')));
		std::string trailing = encoded;
		trailing.push_back ('x');
		assert (!DecodeCancel (trailing));
		assert (!DecodeCancel (EncodeCancel (0)));
	}

	void TestInspection()
	{
		const std::string frame
			= Frame (PROTOCOL_VERSION, static_cast<uint16_t> (MessageType::Ping), 0xdeadbeef, "");
		const auto inspection = InspectFrame (frame);
		assert (inspection.headerPresent && inspection.magicValid && inspection.lengthConsistent);
		assert (inspection.rawType == static_cast<uint16_t> (MessageType::Ping));
		assert (inspection.requestId == 0xdeadbeef);
		auto shortFrame = InspectFrame ("x");
		assert (!shortFrame.headerPresent);
		auto badMagic = frame;
		badMagic[0] = 0;
		assert (!InspectFrame (badMagic).magicValid);
		// Only an attributable record may carry a recoverable error reply.
		assert (IsRecoverableFrameFault (ProtocolError { ErrorCode::UnknownType, "" }));
		assert (IsRecoverableFrameFault (ProtocolError { ErrorCode::UnsupportedVersion, "" }));
		assert (!IsRecoverableFrameFault (ProtocolError { ErrorCode::NotAuthorized, "" }));
	}
}

int main ()
{
	TestRegistry ();
	TestRoleDirections ();
	TestDescriptorPolicy ();
	TestVersionAndLimits ();
	TestCancelCodec ();
	TestInspection ();
	return 0;
}
