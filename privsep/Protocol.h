/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Protocol.
 *
 * Defines the versioned worker frame, bounded payload and text codecs, the
 * typed log/storage/status encodings shared by every transport and the
 * per-endpoint operation allowlist. Decoders reject bad magic, versions,
 * types, oversized lengths, truncation, trailing bytes and embedded NULs in
 * path fields. Encoders are fallible for path fields so a value is never
 * silently truncated to a different filesystem object.
 *
 * Version 3 adds the data-storage frame family (configure/request/reply) for
 * hash-named runtime artifacts. Version 4 adds the shared MessageSpec registry
 * (per-type roles, direction, minimum version, payload ceiling and descriptor
 * policy) and the advisory Cancel frame. Version 5 adds Windows WorkerHello.
 * Version 1..4 frames remain decodable;
 * a type whose minimum version is newer than the frame is rejected rather than
 * interpreted with different semantics.
 *
 */

#ifndef I2PD_PRIVSEP_PROTOCOL_H__
#define I2PD_PRIVSEP_PROTOCOL_H__

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include "Expected.h"
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace i2p::privsep
{
inline constexpr uint32_t PROTOCOL_MAGIC = 0x49503250; // "IP2P"
// Version 2 adds the typed Error message and the shared role allowlist, while
// version 3 adds the data-storage frames. Version 4 adds the advisory Cancel
// and version 5 the Windows worker identity handshake. Older frames stay
// readable so a worker from an earlier build cannot be silently starved of its
// own protocol.
inline constexpr uint16_t MIN_PROTOCOL_VERSION = 1;
inline constexpr uint16_t PROTOCOL_VERSION = 5;
/// Header fields are big-endian: magic at 0 (u32), version at 4 (u16),
/// type at 6 (u16), request id at 8 (u32), and payload length at 12 (u32).
/// One transport record carries exactly this header and its declared payload;
/// see docs/security/ipc-protocol.md for per-type authority and descriptor rules.
inline constexpr size_t HEADER_SIZE = 16;
inline constexpr size_t MAX_MESSAGE_SIZE = 64 * 1024;
inline constexpr size_t MAX_TEXT_SIZE = 8 * 1024;
inline constexpr size_t MAX_PATH_SIZE = 4096;
inline constexpr size_t MAX_ERROR_DETAIL = 256;
/**
 * Artifact names and payloads for the data-storage worker. A name is either a
 * 44-character canonical I2P-base64 identity hash or a 52-character canonical
 * base32 identity plus ".keyType", so 80 bytes is a generous fixed ceiling. The
 * payload bound matches the 16 KiB metadata chunk the router already uses for
 * isolated persistence.
 */
inline constexpr size_t MAX_ARTIFACT_NAME_SIZE = 80;
inline constexpr size_t MAX_ARTIFACT_PAYLOAD_SIZE = 16 * 1024;

enum class MessageType : uint16_t
{
	ConfigureLog = 1,
	LogRecord = 2,
	Shutdown = 3,
	Ping = 4,
	Pong = 5,
	/**
	 * Same payload as ConfigureLog, but a file sink MUST be accompanied by
	 * exactly one SCM_RIGHTS descriptor for an already-open log file. This
	 * lets the worker drop wpath/cpath and retain only stdio/rpath.
	 */
	ConfigureLogFd = 6,
	ConfigureTorrentStorage = 7,
	TorrentStorageRequest = 8,
	TorrentStorageReply = 9,
	SpawnTorrentStorage = 10,
	TorrentStorageSupervisorReply = 11,
	ConfigureRouterStorage = 12,
	RouterStorageRequest = 13,
	RouterStorageReply = 14,
	/**
	 * Typed rejection/status reply. The frame requestId identifies the
	 * rejected request; the payload is u16 offending-type, u8 ErrorCode and
	 * one bounded UTF-8/byte detail string. It never carries a descriptor.
	 */
	Error = 15,
	/**
	 * OpenBSD broker configuration for a delegated storage subdirectory. The
	 * worker inherits the broker data root at fd 4 and walks the relative
	 * path itself before applying its policy. Payload: absolute delegated
	 * root path, relative path, expected dev, expected ino.
	 */
	ConfigureTorrentStorageSupervised = 16,
	/**
	 * Data-storage configuration. Payload: absolute delegated root path,
	 * expected dev, expected ino, u32 client generation. On the Linux
	 * transport exactly one SCM_RIGHTS descriptor for the validated root is
	 * attached; the OpenBSD worker receives the root as inherited fd 4.
	 */
	ConfigureDataStorage = 17,
	/**
	 * One artifact operation. Payload: u8 kind, u8 nameLen, name bytes,
	 * u32 payloadLen, payload bytes. A zero payloadLen is a removal request;
	 * a zero-length write is not representable and is not meaningful.
	 */
	DataStorageRequest = 18,
	/**
	 * Reply to ConfigureDataStorage or DataStorageRequest. Payload:
	 * u8 status (0 ok, 1 rejected, 2 busy), u8 artifact kind (0 for the
	 * configuration reply) and u32 generation echoed from the configure
	 * request so a client can fence replies that belong to an older worker.
	 */
	DataStorageReply = 19,
	/**
	 * Abort one in-flight request. Payload: u32 requestId. It is advisory:
	 * the worker may have completed the side effect already, so a client must
	 * never treat a cancel as a rollback and must never replay an ambiguous
	 * write. `Busy`/`Error` remain the only replies.
	 */
	Cancel = 20,
	/**
	 * First frame a Windows worker sends after connecting to its supervisor.
	 * Payload: u8 worker role, u8 nonce length (32), 32 CSPRNG nonce bytes that
	 * the spawner generated and passed on the worker command line. The nonce
	 * binds the accepted pipe connection to a spawn. It is passed on the command
	 * line and is not secret from a same-user process; PID/session and SID checks
	 * remain part of authorization. The receiver is always the core: no worker
	 * endpoint may receive a handshake.
	 */
	WorkerHello = 21
};

inline constexpr uint16_t MAX_MESSAGE_TYPE = static_cast<uint16_t> (MessageType::WorkerHello);

/// Range check only: it answers "this build has a row for the type", never
/// "this endpoint may receive it". Authorization is `AllowsOperation`, which
/// reads the registry below.
inline bool IsKnownMessageType (uint16_t type)
{
	return type >= static_cast<uint16_t> (MessageType::ConfigureLog) && type <= MAX_MESSAGE_TYPE;
}

/**
 * One decoded frame. `payload` owns its bytes, so a handler may keep a Message
 * after the wire buffer is released; the field accessors of `PayloadReader`
 * only borrow, so a decoded sub-field must be copied to outlive it.
 *
 * `requestId` is echoed unchanged in the reply: a reply whose id does not match
 * the request the sender is waiting for is dropped (or refused) by the client,
 * and `0` is reserved for fire-and-forget frames such as `Shutdown` and
 * `LogRecord`.
 */
struct Message
{
	MessageType type = MessageType::Ping;
	uint32_t requestId = 0;
	std::string payload;
};

enum class ErrorCode : uint8_t
{
	None = 0,
	Malformed = 1,
	TooLarge = 2,
	UnsupportedVersion = 3,
	UnknownType = 4,
	BadField = 5,
	NotAuthorized = 6,
	/** A transient worker-side failure (allocation/IO/child spawn). */
	Internal = 7,
	/** A bounded resource or concurrency limit was reached. */
	Busy = 8
};

inline bool IsKnownErrorCode (uint8_t code)
{
	return code <= static_cast<uint8_t> (ErrorCode::Busy);
}

/**
 * Per-endpoint operation allowlist. It is deliberately defined next to the
 * frame type so a role cannot silently gain an operation because a dispatch
 * switch changed. Clients use their own reply checks.
 */
enum class EndpointRole : uint8_t
{
	Supervisor,
	Log,
	RouterStorage,
	TorrentStorage,
	StorageBroker,
	DataStorage,
	/** The request-issuing side (router core or a supervisor client). */
	Core,
	Count
};

/** Direction of a message from the sender's point of view. */
enum class MessageDirection : uint8_t
{
	Request = 0,
	Reply = 1
};

/**
 * Kind and requiredness of a side-channel descriptor. A descriptor is part of
 * the capability boundary, so it is declared per message type here instead of
 * being re-derived by each handler.
 */
enum class DescriptorPolicy : uint8_t
{
	None = 0,
	OptionalFile,
	RequiredFile,
	OptionalDirectory,
	RequiredDirectory,
	OptionalSocket,
	RequiredSocket
};

constexpr uint16_t RoleBit (EndpointRole role)
{
	return static_cast<uint16_t> (1u << static_cast<unsigned> (role));
}

constexpr uint16_t kAllWorkersMask = RoleBit (EndpointRole::Supervisor) | RoleBit (EndpointRole::Log)
	| RoleBit (EndpointRole::RouterStorage) | RoleBit (EndpointRole::TorrentStorage)
	| RoleBit (EndpointRole::StorageBroker) | RoleBit (EndpointRole::DataStorage);
constexpr uint16_t kAnyReceiverMask = kAllWorkersMask | RoleBit (EndpointRole::Core);

/**
 * One row of the protocol registry. It is the single authority for whether a
 * type exists, which role may receive it, its direction, the minimum protocol
 * version that may carry it, its payload ceiling (never the global 64 KiB) and
 * the descriptor kind it may carry. Encoders, decoders, channel descriptor
 * checks, worker loops and tests all read this table.
 */
struct MessageSpec
{
	MessageType type;
	uint16_t minVersion;
	MessageDirection direction;
	uint16_t receivers;
	size_t maxPayload;
	DescriptorPolicy descriptors;
};

inline constexpr MessageSpec kMessageSpecs[] = {
	{ MessageType::ConfigureLog, 1, MessageDirection::Request,
		RoleBit (EndpointRole::Supervisor) | RoleBit (EndpointRole::Log), 12297, DescriptorPolicy::None },
	{ MessageType::LogRecord, 1, MessageDirection::Request,
		RoleBit (EndpointRole::Supervisor) | RoleBit (EndpointRole::Log), 8205, DescriptorPolicy::None },
	{ MessageType::Shutdown, 1, MessageDirection::Request, kAllWorkersMask, 0, DescriptorPolicy::None },
	{ MessageType::Ping, 1, MessageDirection::Request, kAnyReceiverMask, 0, DescriptorPolicy::None },
	{ MessageType::Pong, 1, MessageDirection::Reply, RoleBit (EndpointRole::Core), 0, DescriptorPolicy::None },
	{ MessageType::ConfigureLogFd, 1, MessageDirection::Request, RoleBit (EndpointRole::Log), 12297,
		DescriptorPolicy::RequiredFile },
	{ MessageType::ConfigureTorrentStorage, 1, MessageDirection::Request,
		RoleBit (EndpointRole::StorageBroker) | RoleBit (EndpointRole::TorrentStorage), 8200,
		DescriptorPolicy::OptionalDirectory },
	{ MessageType::TorrentStorageRequest, 1, MessageDirection::Request, RoleBit (EndpointRole::TorrentStorage), 16401,
		DescriptorPolicy::None },
	{ MessageType::TorrentStorageReply, 1, MessageDirection::Reply, RoleBit (EndpointRole::Core), 16385,
		DescriptorPolicy::None },
	{ MessageType::SpawnTorrentStorage, 1, MessageDirection::Request, RoleBit (EndpointRole::StorageBroker), 4116,
		DescriptorPolicy::None },
	// Dual use: the broker acknowledges its own configuration without a
	// descriptor, and the supervisor hands one accepted worker socket to the
	// core. The core requires the descriptor itself before adopting it.
	{ MessageType::TorrentStorageSupervisorReply, 1, MessageDirection::Reply,
		RoleBit (EndpointRole::Core) | RoleBit (EndpointRole::StorageBroker), 0, DescriptorPolicy::OptionalSocket },
	{ MessageType::ConfigureRouterStorage, 1, MessageDirection::Request, RoleBit (EndpointRole::RouterStorage), 4116,
		DescriptorPolicy::OptionalDirectory },
	{ MessageType::RouterStorageRequest, 1, MessageDirection::Request, RoleBit (EndpointRole::RouterStorage), 32769,
		DescriptorPolicy::None },
	{ MessageType::RouterStorageReply, 1, MessageDirection::Reply, RoleBit (EndpointRole::Core), 1,
		DescriptorPolicy::None },
	{ MessageType::Error, 2, MessageDirection::Reply,
		RoleBit (EndpointRole::Core) | RoleBit (EndpointRole::Supervisor) | RoleBit (EndpointRole::StorageBroker), 263,
		DescriptorPolicy::None },
	{ MessageType::ConfigureTorrentStorageSupervised, 1, MessageDirection::Request,
		RoleBit (EndpointRole::TorrentStorage), 8300, DescriptorPolicy::None },
	{ MessageType::ConfigureDataStorage, 3, MessageDirection::Request, RoleBit (EndpointRole::DataStorage), 4200,
		DescriptorPolicy::OptionalDirectory },
	{ MessageType::DataStorageRequest, 3, MessageDirection::Request, RoleBit (EndpointRole::DataStorage), 16470,
		DescriptorPolicy::None },
	{ MessageType::DataStorageReply, 3, MessageDirection::Reply, RoleBit (EndpointRole::Core), 6,
		DescriptorPolicy::None },
	// Only the single-flight storage workers can honour a cancel; a log or
	// supervisor endpoint has no request table to abort.
	{ MessageType::Cancel, 4, MessageDirection::Request,
		RoleBit (EndpointRole::RouterStorage) | RoleBit (EndpointRole::TorrentStorage)
			| RoleBit (EndpointRole::DataStorage),
		4, DescriptorPolicy::None },
	// The Windows worker handshake: one role byte, one length byte and the
	// 32-byte spawn nonce. Only the accepting core receives it.
	{ MessageType::WorkerHello, 5, MessageDirection::Request, RoleBit (EndpointRole::Core), 34,
		DescriptorPolicy::None },
};

constexpr const MessageSpec * FindMessageSpec (uint16_t type)
{
	for (const auto & spec : kMessageSpecs)
		if (static_cast<uint16_t> (spec.type) == type) return &spec;
	return nullptr;
}

constexpr bool IsRequestType (MessageType type)
{
	const auto * spec = FindMessageSpec (static_cast<uint16_t> (type));
	return spec && spec->direction == MessageDirection::Request;
}

constexpr bool IsReplyType (MessageType type)
{
	const auto * spec = FindMessageSpec (static_cast<uint16_t> (type));
	return spec && spec->direction == MessageDirection::Reply;
}

constexpr size_t MaxPayloadFor (MessageType type)
{
	const auto * spec = FindMessageSpec (static_cast<uint16_t> (type));
	return spec ? spec->maxPayload : 0;
}

constexpr uint16_t MinVersionFor (MessageType type)
{
	const auto * spec = FindMessageSpec (static_cast<uint16_t> (type));
	return spec ? spec->minVersion : PROTOCOL_VERSION;
}

constexpr DescriptorPolicy DescriptorPolicyFor (MessageType type)
{
	const auto * spec = FindMessageSpec (static_cast<uint16_t> (type));
	return spec ? spec->descriptors : DescriptorPolicy::None;
}

/**
 * Presence half of the descriptor policy, exposed so tests and backends can
 * assert it without an OS call. Kind validation (regular file, directory,
 * socket) stays in the transport because it needs `fstat`.
 */
constexpr bool DescriptorPresenceAllowed (MessageType type, bool present)
{
	switch (DescriptorPolicyFor (type))
	{
	case DescriptorPolicy::None: return !present;
	case DescriptorPolicy::OptionalFile:
	case DescriptorPolicy::OptionalDirectory:
	case DescriptorPolicy::OptionalSocket: return true;
	case DescriptorPolicy::RequiredFile:
	case DescriptorPolicy::RequiredDirectory:
	case DescriptorPolicy::RequiredSocket: return present;
	}
	return false;
}

/**
 * Authorize one received frame for a receiving role. A sender cannot claim a
 * different role on the wire, and a reply type is never accepted by a worker
 * role because the registry gives replies to the requesting side only.
 */
constexpr bool AllowsOperation (EndpointRole role, MessageType type)
{
	if (role == EndpointRole::Count) return false;
	const auto * spec = FindMessageSpec (static_cast<uint16_t> (type));
	return spec && (spec->receivers & RoleBit (role)) != 0;
}

constexpr bool ValidateMessageRegistry ()
{
	size_t expected = static_cast<size_t> (MessageType::ConfigureLog);
	for (const auto & spec : kMessageSpecs)
	{
		if (static_cast<size_t> (spec.type) != expected) return false;
		if (spec.maxPayload > MAX_MESSAGE_SIZE - HEADER_SIZE) return false;
		if (spec.minVersion < MIN_PROTOCOL_VERSION || spec.minVersion > PROTOCOL_VERSION) return false;
		++expected;
	}
	return expected == static_cast<size_t> (MAX_MESSAGE_TYPE) + 1;
}

static_assert (ValidateMessageRegistry (), "message registry is incomplete or out of order");
static_assert (MaxPayloadFor (MessageType::RouterStorageRequest) <= MAX_MESSAGE_SIZE - HEADER_SIZE,
	"per-type ceiling exceeds the frame maximum");

struct ProtocolError
{
	ErrorCode code;
	std::string_view text;
};

/// Owns encoded payload bytes. Multi-byte integers use network byte order.
/// Data() borrows the string; later writes change its contents and can
/// invalidate views or iterators into its storage.
class PayloadWriter
{
public:
	void WriteU8 (uint8_t value) { m_Data.push_back (static_cast<char> (value)); }
	void WriteU16 (uint16_t value)
	{
		WriteU8 (value >> 8);
		WriteU8 (value & 0xff);
	}
	void WriteU32 (uint32_t value)
	{
		WriteU16 (static_cast<uint16_t> (value >> 16));
		WriteU16 (static_cast<uint16_t> (value & 0xffff));
	}
	void WriteU64BE (uint64_t value)
	{
		WriteU32 (static_cast<uint32_t> (value >> 32));
		WriteU32 (static_cast<uint32_t> (value & 0xffffffff));
	}
	// Bounded by MAX_TEXT_SIZE. Log records therefore truncate rather
	// than copy unbounded text supplied by a router thread.
	void WriteString (std::string_view value)
	{
		const auto size = std::min (value.size (), MAX_TEXT_SIZE);
		WriteU32 (static_cast<uint32_t> (size));
		m_Data.append (value.substr (0, size));
	}
	/** Fallible bounded string; returns false instead of truncating. */
	bool WriteStringBounded (std::string_view value, size_t limit)
	{
		if (value.size () > limit) return false;
		WriteU32 (static_cast<uint32_t> (value.size ()));
		m_Data.append (value);
		return true;
	}
	/** Path fields carry no embedded NUL and are bounded before framing. */
	bool WritePath (std::string_view value)
	{
		return value.find ('\0') == std::string_view::npos && WriteStringBounded (value, MAX_PATH_SIZE);
	}
	/**
	 * Raw bytes with no length prefix. Used by the data-storage request
	 * family, whose name/payload lengths are already explicit u8/u32 fields.
	 * The caller is responsible for the bound; the encoder checks it.
	 */
	void WriteBytes (std::span<const uint8_t> bytes)
	{
		m_Data.append (reinterpret_cast<const char *> (bytes.data ()), bytes.size ());
	}
	/**
	 * Unchecked append for values whose length prefix was written separately.
	 * Nothing here enforces a bound, so the caller must have validated the size
	 * against the type's registry ceiling first; a wrong count desynchronises
	 * the decoder instead of being reported.
	 */
	void WriteRaw (std::string_view bytes) { m_Data.append (bytes); }
	const std::string & Data () const { return m_Data; }

private:
	std::string m_Data;
};

/// Cursor over a borrowed frame or payload. Views returned by ReadString and
/// ReadRaw borrow the original buffer, which must outlive both reader and view.
/// A failed multi-byte read can advance the cursor; abandon the reader on error.
class PayloadReader
{
public:
	explicit PayloadReader (std::string_view data)
		: m_Data (data)
	{
	}

	i2p::expected<uint8_t, ProtocolError> ReadU8 ()
	{
		if (m_Pos >= m_Data.size ()) return Bad ("truncated u8");
		return static_cast<uint8_t> (m_Data[m_Pos++]);
	}
	i2p::expected<uint16_t, ProtocolError> ReadU16 ()
	{
		auto hi = ReadU8 ();
		if (!hi) return i2p::unexpected (hi.error ());
		auto lo = ReadU8 ();
		if (!lo) return i2p::unexpected (lo.error ());
		return static_cast<uint16_t> ((*hi << 8) | *lo);
	}
	i2p::expected<uint32_t, ProtocolError> ReadU32 ()
	{
		auto hi = ReadU16 ();
		if (!hi) return i2p::unexpected (hi.error ());
		auto lo = ReadU16 ();
		if (!lo) return i2p::unexpected (lo.error ());
		return (static_cast<uint32_t> (*hi) << 16) | *lo;
	}
	i2p::expected<uint64_t, ProtocolError> ReadU64BE ()
	{
		auto hi = ReadU32 ();
		if (!hi) return i2p::unexpected (hi.error ());
		auto lo = ReadU32 ();
		if (!lo) return i2p::unexpected (lo.error ());
		return (static_cast<uint64_t> (*hi) << 32) | *lo;
	}
	i2p::expected<std::string_view, ProtocolError> ReadString ()
	{
		auto size = ReadU32 ();
		if (!size) return i2p::unexpected (size.error ());
		if (*size > MAX_TEXT_SIZE || *size > m_Data.size () - m_Pos) return Bad ("invalid string length");
		auto value = m_Data.substr (m_Pos, *size);
		m_Pos += *size;
		return value;
	}
	/** Raw bytes of an explicitly declared length, never beyond the buffer. */
	i2p::expected<std::string_view, ProtocolError> ReadRaw (size_t length)
	{
		if (length > Remaining ()) return Bad ("truncated field");
		auto value = m_Data.substr (m_Pos, length);
		m_Pos += length;
		return value;
	}
	size_t Remaining () const { return m_Data.size () - m_Pos; }
	bool Done () const { return m_Pos == m_Data.size (); }

private:
	static i2p::unexpected<ProtocolError> Bad (std::string_view text)
	{
		return i2p::unexpected (ProtocolError { ErrorCode::BadField, text });
	}
	std::string_view m_Data;
	size_t m_Pos = 0;
};

/**
 * Encode only the 16-byte header. A transport that can scatter/gather sends
 * the header and the caller-owned payload as two iovecs, so the hot path does
 * not concatenate a second copy of every frame. `Encode` remains the
 * contiguous form used by tests and the native adapters.
 */
inline i2p::expected<std::string, ProtocolError> EncodeHeader (const Message & message)
{
	const auto * spec = FindMessageSpec (static_cast<uint16_t> (message.type));
	if (!spec) return i2p::unexpected (ProtocolError { ErrorCode::UnknownType, "unknown message type" });
	if (message.payload.size () > spec->maxPayload)
		return i2p::unexpected (ProtocolError { ErrorCode::TooLarge, "message too large for its type" });
	if (message.payload.size () > MAX_MESSAGE_SIZE - HEADER_SIZE)
		return i2p::unexpected (ProtocolError { ErrorCode::TooLarge, "message too large" });

	PayloadWriter header;
	header.WriteU32 (PROTOCOL_MAGIC);
	header.WriteU16 (PROTOCOL_VERSION);
	header.WriteU16 (static_cast<uint16_t> (message.type));
	header.WriteU32 (message.requestId);
	header.WriteU32 (static_cast<uint32_t> (message.payload.size ()));
	return header.Data ();
}

inline i2p::expected<std::string, ProtocolError> Encode (const Message & message)
{
	auto header = EncodeHeader (message);
	if (!header) return i2p::unexpected (header.error ());
	std::string out = std::move (*header);
	out.append (message.payload);
	return out;
}

/**
 * Header-level view of a received record. It exists so a transport can answer a
 * recoverable framing fault with a typed `Error` reply - correlated by
 * `requestId` - instead of tearing down the channel. Only a record whose magic,
 * version and declared length are consistent may be answered; anything else is
 * unattributable and stays fatal.
 */
struct FrameInspection
{
	bool headerPresent = false;
	bool magicValid = false;
	uint16_t version = 0;
	uint16_t rawType = 0;
	uint32_t requestId = 0;
	uint32_t declaredLength = 0;
	bool lengthConsistent = false;
};

inline FrameInspection InspectFrame (std::string_view frame)
{
	FrameInspection out;
	if (frame.size () < HEADER_SIZE) return out;
	PayloadReader reader (frame.substr (0, HEADER_SIZE));
	auto magic = reader.ReadU32 ();
	auto version = reader.ReadU16 ();
	auto type = reader.ReadU16 ();
	auto request = reader.ReadU32 ();
	auto length = reader.ReadU32 ();
	if (!magic || !version || !type || !request || !length) return out;
	out.headerPresent = true;
	out.magicValid = *magic == PROTOCOL_MAGIC;
	out.version = *version;
	out.rawType = *type;
	out.requestId = *request;
	out.declaredLength = *length;
	out.lengthConsistent = out.magicValid && out.declaredLength == frame.size () - HEADER_SIZE;
	return out;
}

/**
 * True for a decode failure that describes the frame rather than the peer
 * connection. A transport may answer it once, keep the channel, and apply a
 * bounded violation budget; a credential or ancillary-data failure is never
 * recoverable.
 */
inline bool IsRecoverableFrameFault (const ProtocolError & error)
{
	switch (error.code)
	{
	case ErrorCode::UnknownType:
	case ErrorCode::UnsupportedVersion:
	case ErrorCode::TooLarge:
	case ErrorCode::Malformed: return true;
	default: return false;
	}
}

inline i2p::expected<Message, ProtocolError> Decode (std::string_view frame)
{
	if (frame.size () < HEADER_SIZE) return i2p::unexpected (ProtocolError { ErrorCode::Malformed, "short frame" });
	PayloadReader reader (frame.substr (0, HEADER_SIZE));
	auto magic = reader.ReadU32 ();
	if (!magic) return i2p::unexpected (magic.error ());
	auto version = reader.ReadU16 ();
	if (!version) return i2p::unexpected (version.error ());
	auto type = reader.ReadU16 ();
	if (!type) return i2p::unexpected (type.error ());
	auto request = reader.ReadU32 ();
	if (!request) return i2p::unexpected (request.error ());
	auto length = reader.ReadU32 ();
	if (!length) return i2p::unexpected (length.error ());
	if (*magic != PROTOCOL_MAGIC) return i2p::unexpected (ProtocolError { ErrorCode::Malformed, "bad magic" });
	if (*version < MIN_PROTOCOL_VERSION || *version > PROTOCOL_VERSION)
		return i2p::unexpected (ProtocolError { ErrorCode::UnsupportedVersion, "bad version" });
	const auto * spec = FindMessageSpec (*type);
	if (!spec) return i2p::unexpected (ProtocolError { ErrorCode::UnknownType, "unknown type" });
	// A type introduced in a later version must not arrive in an older frame:
	// accepting it would let a peer downgrade the header while using the new
	// semantics.
	if (*version < spec->minVersion)
		return i2p::unexpected (ProtocolError { ErrorCode::UnsupportedVersion, "type requires a newer version" });
	if (*length > spec->maxPayload)
		return i2p::unexpected (ProtocolError { ErrorCode::TooLarge, "payload too large for its type" });
	if (*length > MAX_MESSAGE_SIZE - HEADER_SIZE)
		return i2p::unexpected (ProtocolError { ErrorCode::TooLarge, "payload too large" });
	// One SOCK_SEQPACKET record is one frame. A short/long frame is a
	// framing error, not a size limit violation.
	if (*length != frame.size () - HEADER_SIZE)
		return i2p::unexpected (ProtocolError { ErrorCode::Malformed, "length mismatch" });
	return Message { static_cast<MessageType> (*type), *request, std::string (frame.substr (HEADER_SIZE, *length)) };
}

/**
 * Log-record payload: `u8 level | u64 timestamp (Unix seconds) | text`.
 * The text is bounded and truncated by `WriteString`, so an encoder can never
 * produce a record above the registry ceiling; the level is a transport value
 * that the worker validates, not a translated label.
 */
inline std::string EncodeLogRecord (uint8_t level, int64_t timestamp, std::string_view text)
{
	PayloadWriter writer;
	writer.WriteU8 (level);
	writer.WriteU64BE (static_cast<uint64_t> (timestamp));
	writer.WriteString (text);
	return writer.Data ();
}

/// Decode one log record. Trailing bytes are a framing fault, so a decoder
/// never silently drops part of a record.
inline i2p::expected<std::tuple<uint8_t, int64_t, std::string>, ProtocolError> DecodeLogRecord (
	std::string_view payload)
{
	PayloadReader reader (payload);
	auto level = reader.ReadU8 ();
	if (!level) return i2p::unexpected (level.error ());
	auto timestamp = reader.ReadU64BE ();
	if (!timestamp) return i2p::unexpected (timestamp.error ());
	auto text = reader.ReadString ();
	if (!text) return i2p::unexpected (text.error ());
	if (!reader.Done ()) return i2p::unexpected (ProtocolError { ErrorCode::BadField, "trailing log data" });
	return std::tuple { *level, static_cast<int64_t> (*timestamp), std::string (*text) };
}

inline i2p::expected<std::string, ProtocolError> EncodeLogConfig (
	bool useSyslog, std::string_view path, std::string_view tag)
{
	PayloadWriter writer;
	writer.WriteU8 (useSyslog ? 1 : 0);
	if (!writer.WritePath (path))
		return i2p::unexpected (ProtocolError { ErrorCode::BadField, "log path too large or NUL" });
	if (tag.find ('\0') != std::string_view::npos || !writer.WriteStringBounded (tag, MAX_TEXT_SIZE))
		return i2p::unexpected (ProtocolError { ErrorCode::BadField, "log tag too large or NUL" });
	return writer.Data ();
}

inline i2p::expected<std::tuple<bool, std::string, std::string>, ProtocolError> DecodeLogConfig (
	std::string_view payload)
{
	PayloadReader reader (payload);
	auto syslog = reader.ReadU8 ();
	if (!syslog) return i2p::unexpected (syslog.error ());
	auto path = reader.ReadString ();
	if (!path) return i2p::unexpected (path.error ());
	auto tag = reader.ReadString ();
	if (!tag) return i2p::unexpected (tag.error ());
	if (!reader.Done () || *syslog > 1)
		return i2p::unexpected (ProtocolError { ErrorCode::BadField, "invalid log config" });
	// These become null-terminated path/ident arguments. An embedded NUL
	// would make the worker act on a different path than the parent meant.
	if (path->size () > MAX_PATH_SIZE || tag->find ('\0') != std::string_view::npos
		|| path->find ('\0') != std::string_view::npos)
		return i2p::unexpected (ProtocolError { ErrorCode::BadField, "invalid log path/tag" });
	return std::tuple { *syslog != 0, std::string (*path), std::string (*tag) };
}
/**
 * Typed error payload: `u16 offending type | u8 ErrorCode | bounded detail`.
 * The detail is clipped to MAX_ERROR_DETAIL by the encoder, so a caller cannot
 * put a path, a payload or a credential operand of arbitrary length on the
 * wire; the request id of the frame is the correlation key, and the frame never
 * carries a descriptor.
 */
inline std::string EncodeError (MessageType offendingType, ErrorCode code, std::string_view detail)
{
	PayloadWriter writer;
	writer.WriteU16 (static_cast<uint16_t> (offendingType));
	writer.WriteU8 (static_cast<uint8_t> (code));
	writer.WriteStringBounded (detail.substr (0, MAX_ERROR_DETAIL), MAX_ERROR_DETAIL);
	return writer.Data ();
}

/// Decode a typed error. An unknown offending type, an unknown code, a detail
/// above the bound or trailing bytes are rejected, because a receiver must not
/// act on a refusal it cannot attribute or classify.
inline i2p::expected<std::tuple<MessageType, ErrorCode, std::string>, ProtocolError> DecodeError (
	std::string_view payload)
{
	if (payload.size () < 3) return i2p::unexpected (ProtocolError { ErrorCode::Malformed, "short error payload" });
	PayloadReader reader (payload);
	auto type = reader.ReadU16 ();
	if (!type) return i2p::unexpected (type.error ());
	auto code = reader.ReadU8 ();
	if (!code) return i2p::unexpected (code.error ());
	auto detail = reader.ReadString ();
	if (!detail) return i2p::unexpected (detail.error ());
	if (!reader.Done () || !IsKnownMessageType (*type) || !IsKnownErrorCode (*code)
		|| detail->size () > MAX_ERROR_DETAIL)
		return i2p::unexpected (ProtocolError { ErrorCode::BadField, "invalid error payload" });
	return std::tuple { static_cast<MessageType> (*type), static_cast<ErrorCode> (*code), std::string (*detail) };
}

/**
 * Wire status of one data-storage operation. `Busy` never means the worker is
 * dead: it is a bounded local or worker-side refusal that leaves the channel
 * usable (request-id space exhausted, single-flight slot busy).
 */
enum class DataStorageStatus : uint8_t
{
	Ok = 0,
	Rejected = 1,
	Busy = 2
};

inline bool IsKnownDataStorageStatus (uint8_t status)
{
	return status <= static_cast<uint8_t> (DataStorageStatus::Busy);
}

struct DataStorageRequestFrame
{
	uint8_t kind = 0;
	std::string name;
	std::string payload;
};

inline i2p::expected<std::string, ProtocolError> EncodeConfigureDataStorage (
	std::string_view path, uint64_t device, uint64_t inode, uint32_t generation)
{
	if (generation == 0) return i2p::unexpected (ProtocolError { ErrorCode::BadField, "zero data storage generation" });
	PayloadWriter writer;
	if (!writer.WritePath (path))
		return i2p::unexpected (ProtocolError { ErrorCode::BadField, "data storage root too large or NUL" });
	writer.WriteU64BE (device);
	writer.WriteU64BE (inode);
	writer.WriteU32 (generation);
	return writer.Data ();
}

inline i2p::expected<std::tuple<std::string, uint64_t, uint64_t, uint32_t>, ProtocolError> DecodeConfigureDataStorage (
	std::string_view payload)
{
	PayloadReader reader (payload);
	auto path = reader.ReadString ();
	if (!path) return i2p::unexpected (path.error ());
	auto device = reader.ReadU64BE ();
	if (!device) return i2p::unexpected (device.error ());
	auto inode = reader.ReadU64BE ();
	if (!inode) return i2p::unexpected (inode.error ());
	auto generation = reader.ReadU32 ();
	if (!generation) return i2p::unexpected (generation.error ());
	if (!reader.Done () || path->empty () || path->front () != '/' || path->size () > MAX_PATH_SIZE
		|| path->find ('\0') != std::string_view::npos || *generation == 0)
		return i2p::unexpected (ProtocolError { ErrorCode::BadField, "invalid data storage configuration" });
	return std::tuple { std::string (*path), *device, *inode, *generation };
}

/**
 * Encode one artifact operation. Structural bounds only: the canonical
 * name/kind semantics belong to the data-storage layer so the framing codec
 * stays independent of the libi2p artifact enum.
 */
inline i2p::expected<std::string, ProtocolError> EncodeDataStorageRequest (
	uint8_t kind, std::string_view name, std::span<const uint8_t> payload)
{
	if (kind == 0) return i2p::unexpected (ProtocolError { ErrorCode::BadField, "zero artifact kind" });
	if (name.empty () || name.size () > MAX_ARTIFACT_NAME_SIZE)
		return i2p::unexpected (ProtocolError { ErrorCode::BadField, "artifact name length" });
	if (name.find ('\0') != std::string_view::npos)
		return i2p::unexpected (ProtocolError { ErrorCode::BadField, "artifact name NUL" });
	if (payload.size () > MAX_ARTIFACT_PAYLOAD_SIZE)
		return i2p::unexpected (ProtocolError { ErrorCode::TooLarge, "artifact payload too large" });
	PayloadWriter writer;
	writer.WriteU8 (kind);
	writer.WriteU8 (static_cast<uint8_t> (name.size ()));
	writer.WriteRaw (name);
	writer.WriteU32 (static_cast<uint32_t> (payload.size ()));
	writer.WriteBytes (payload);
	return writer.Data ();
}

inline i2p::expected<DataStorageRequestFrame, ProtocolError> DecodeDataStorageRequest (std::string_view payload)
{
	PayloadReader reader (payload);
	auto kind = reader.ReadU8 ();
	if (!kind) return i2p::unexpected (kind.error ());
	auto nameLength = reader.ReadU8 ();
	if (!nameLength) return i2p::unexpected (nameLength.error ());
	if (*kind == 0) return i2p::unexpected (ProtocolError { ErrorCode::BadField, "zero artifact kind" });
	if (*nameLength == 0 || *nameLength > MAX_ARTIFACT_NAME_SIZE)
		return i2p::unexpected (ProtocolError { ErrorCode::BadField, "artifact name length" });
	auto name = reader.ReadRaw (*nameLength);
	if (!name) return i2p::unexpected (name.error ());
	if (name->find ('\0') != std::string_view::npos)
		return i2p::unexpected (ProtocolError { ErrorCode::BadField, "artifact name NUL" });
	auto payloadLength = reader.ReadU32 ();
	if (!payloadLength) return i2p::unexpected (payloadLength.error ());
	if (*payloadLength > MAX_ARTIFACT_PAYLOAD_SIZE)
		return i2p::unexpected (ProtocolError { ErrorCode::TooLarge, "artifact payload too large" });
	// Exact-length: the declared payload must be the whole remainder, so a
	// truncated or trailing-byte request is a framing error, not a short write.
	if (reader.Remaining () != *payloadLength)
		return i2p::unexpected (ProtocolError { ErrorCode::BadField, "artifact length mismatch" });
	auto bytes = reader.ReadRaw (*payloadLength);
	if (!bytes) return i2p::unexpected (bytes.error ());
	if (!reader.Done ()) return i2p::unexpected (ProtocolError { ErrorCode::BadField, "trailing artifact data" });
	return DataStorageRequestFrame { *kind, std::string (*name), std::string (*bytes) };
}

/// Fixed six-byte reply: `u8 status | u8 kind | u32 generation`. The
/// generation is echoed from the configure request so a client can fence a
/// reply that belongs to a replaced worker; the decoder rejects generation 0,
/// so the encoder must never be called with an unset generation.
inline std::string EncodeDataStorageReply (uint8_t status, uint8_t kind, uint32_t generation)
{
	PayloadWriter writer;
	writer.WriteU8 (status);
	writer.WriteU8 (kind);
	writer.WriteU32 (generation);
	return writer.Data ();
}

inline std::string EncodeCancel (uint32_t requestId)
{
	PayloadWriter writer;
	writer.WriteU32 (requestId);
	return writer.Data ();
}

inline i2p::expected<uint32_t, ProtocolError> DecodeCancel (std::string_view payload)
{
	if (payload.size () != 4) return i2p::unexpected (ProtocolError { ErrorCode::BadField, "invalid cancel length" });
	PayloadReader reader (payload);
	auto requestId = reader.ReadU32 ();
	if (!requestId) return i2p::unexpected (requestId.error ());
	if (*requestId == 0 || !reader.Done ())
		return i2p::unexpected (ProtocolError { ErrorCode::BadField, "invalid cancel request id" });
	return *requestId;
}

inline i2p::expected<std::tuple<uint8_t, uint8_t, uint32_t>, ProtocolError> DecodeDataStorageReply (
	std::string_view payload)
{
	if (payload.size () != 6)
		return i2p::unexpected (ProtocolError { ErrorCode::BadField, "invalid data storage reply length" });
	PayloadReader reader (payload);
	auto status = reader.ReadU8 ();
	if (!status) return i2p::unexpected (status.error ());
	auto kind = reader.ReadU8 ();
	if (!kind) return i2p::unexpected (kind.error ());
	auto generation = reader.ReadU32 ();
	if (!generation) return i2p::unexpected (generation.error ());
	if (!reader.Done () || !IsKnownDataStorageStatus (*status) || *generation == 0)
		return i2p::unexpected (ProtocolError { ErrorCode::BadField, "invalid data storage reply" });
	return std::tuple { *status, *kind, *generation };
}

}

#endif
