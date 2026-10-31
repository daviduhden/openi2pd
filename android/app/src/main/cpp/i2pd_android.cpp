/*
 * See the LICENSE file at the top of the project tree for copyright and
 * license details.
 *
 * openi2pd: i2pd android.
 *
 * JNI entry points for daemon control, bounded status and log
 * snapshots, transport and client-service probes, and the byte-array
 * torrent commands. UTF-8 is converted to UTF-16 explicitly instead of
 * using NewStringUTF, pending JNI exceptions are cleared and turned
 * into fallbacks, and every returned string is bounded. Each process
 * claims exactly one role (router or SAF storage broker) before
 * reaching a router- or document-capable entry point, and a lock-free
 * cancel token lets a stop abort an in-progress start.
 *
 */

#include <jni.h>
#include <atomic>
#include <cstddef>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <cstring>
#include <filesystem>
#include <memory>
#include <span>
#include <set>
#include <stdexcept>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "DaemonAndroid.h"
#include "Config.h"
#include "RouterContext.h"
#include "ClientContext.h"
#include "AddressBook.h"
#include "Transports.h"
#include <sstream>
#include "Tunnel.h"
#include "NetDb.hpp"
#include "Log.h"
#include "TuiProtocol.h"
#include "TuiServerCommon.h"
#include "Torrents.h"
#include "TorrentsControl.h"
#include "HTTP.h"

namespace
{
/**
	 * Exception-safe owner of the VM-owned UTF-16 buffer. JNI requires
	 * ReleaseStringChars on every path, including a C++ allocation failure
	 * in the decoder loop; a raw pointer made that path leak the VM copy.
	 */
class ScopedJStringChars
{
public:
	ScopedJStringChars (JNIEnv * env, jstring text)
		: m_Env (env)
		, m_Text (text)
		, m_Chars (env ? env->GetStringChars (text, nullptr) : nullptr)
	{
	}
	~ScopedJStringChars ()
	{
		if (m_Chars) m_Env->ReleaseStringChars (m_Text, m_Chars);
	}
	ScopedJStringChars (const ScopedJStringChars &) = delete;
	ScopedJStringChars & operator= (const ScopedJStringChars &) = delete;
	explicit operator bool () const { return m_Chars != nullptr; }
	const jchar * data () const { return m_Chars; }

private:
	JNIEnv * m_Env;
	jstring m_Text;
	const jchar * m_Chars;
};

/** Decode Java UTF-16; reject NUL and unpaired surrogates. */
bool Utf8FromJavaString (JNIEnv * env, jstring text, std::string & output, size_t maxChars = SIZE_MAX)
{
	if (!env || !text) return false;
	const jsize length = env->GetStringLength (text);
	if (env->ExceptionCheck ())
	{
		// GetStringLength reports failure only through a pending exception.
		// Returning false with it pending would be a JNI protocol violation.
		env->ExceptionClear ();
		return false;
	}
	if (static_cast<size_t> (length) > maxChars)
	{
		// The Java side already applies its own bound, but a very large
		// internal string must not be expanded threefold before validation.
		if (env->ExceptionCheck ()) env->ExceptionClear ();
		return false;
	}
	ScopedJStringChars chars (env, text);
	if (!chars)
	{
		// GetStringChars reports allocation failure as a pending
		// OutOfMemoryError; clear it before returning the documented "not
		// convertible" fallback to the caller.
		if (env->ExceptionCheck ()) env->ExceptionClear ();
		return false;
	}
	output.clear ();
	// UTF-8 can need at most three bytes per UTF-16 code unit for BMP
	// input; reserve a bounded prefix instead of growing from zero for
	// every call. Compute the cap before multiplying so a 32-bit size_t
	// cannot wrap on a hostile or accidentally huge Java string.
	constexpr size_t MAX_RESERVE = 64 * 1024;
	const auto units = static_cast<size_t> (length);
	output.reserve (units > MAX_RESERVE / 3 ? MAX_RESERVE : units * 3);
	bool valid = true;
	for (jsize i = 0; i < length; ++i)
	{
		uint32_t code = chars.data ()[i];
		if (code == 0)
		{
			valid = false;
			break;
		}
		if (code >= 0xd800 && code <= 0xdbff)
		{
			if (i + 1 >= length)
			{
				valid = false;
				break;
			}
			const uint32_t low = chars.data ()[++i];
			if (low < 0xdc00 || low > 0xdfff)
			{
				valid = false;
				break;
			}
			code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
		}
		else if (code >= 0xdc00 && code <= 0xdfff)
		{
			valid = false;
			break;
		}
		if (code < 0x80)
			output.push_back (static_cast<char> (code));
		else if (code < 0x800)
		{
			output.push_back (static_cast<char> (0xc0 | (code >> 6)));
			output.push_back (static_cast<char> (0x80 | (code & 0x3f)));
		}
		else if (code < 0x10000)
		{
			output.push_back (static_cast<char> (0xe0 | (code >> 12)));
			output.push_back (static_cast<char> (0x80 | ((code >> 6) & 0x3f)));
			output.push_back (static_cast<char> (0x80 | (code & 0x3f)));
		}
		else
		{
			output.push_back (static_cast<char> (0xf0 | (code >> 18)));
			output.push_back (static_cast<char> (0x80 | ((code >> 12) & 0x3f)));
			output.push_back (static_cast<char> (0x80 | ((code >> 6) & 0x3f)));
			output.push_back (static_cast<char> (0x80 | (code & 0x3f)));
		}
	}
	if (!valid) output.clear ();
	return valid;
}

/** RAII wrapper for a bounded, NUL-safe UTF-8 copy of a Java string. */
class ScopedUtfChars
{
public:
	ScopedUtfChars (JNIEnv * env, jstring text, size_t maxChars = SIZE_MAX)
		: m_Valid (Utf8FromJavaString (env, text, m_Value, maxChars))
	{
	}
	const char * c_str () const { return m_Valid ? m_Value.c_str () : nullptr; }
	std::string_view view () const { return m_Valid ? std::string_view (m_Value) : std::string_view (); }
	explicit operator bool () const { return m_Valid; }

private:
	bool m_Valid = false;
	std::string m_Value;
};

/**
 * RAII local reference. One owner deletes the reference exactly once,
 * also when the enclosing operation throws or returns early after a
 * failed JNI array/object access. Deleting a local reference is legal
 * while no exception is pending; every failure path below clears the
 * JNI exception first.
 */
template <typename T> class ScopedLocalRef
{
public:
	ScopedLocalRef (JNIEnv * env, T value) noexcept
		: m_Env (env)
		, m_Value (value)
	{
	}
	~ScopedLocalRef ()
	{
		if (m_Env && m_Value) m_Env->DeleteLocalRef (m_Value);
	}

	ScopedLocalRef (const ScopedLocalRef &) = delete;
	ScopedLocalRef & operator= (const ScopedLocalRef &) = delete;

	T get () const noexcept { return m_Value; }
	/** Give the reference to the caller without deleting it. */
	T release () noexcept
	{
		T value = m_Value;
		m_Value = nullptr;
		return value;
	}
	explicit operator bool () const noexcept { return m_Value != nullptr; }

private:
	JNIEnv * m_Env;
	T m_Value;
};

/**
	 * Convert ordinary UTF-8 to UTF-16 and call JNI NewString.
	 * NewStringUTF expects modified UTF-8 (CESU-8 plus 0xC0 0x80 for NUL),
	 * which is not the encoding used by C++ log/config/error strings.
	 * Decoding here preserves embedded NUL, supplementary characters and
	 * arbitrary log text, and replaces malformed sequences with U+FFFD
	 * instead of failing the call.
	 *
	 * A VM allocation failure or a native conversion failure clears the
	 * pending JNI exception and returns nullptr; Java must treat a null
	 * result as "no snapshot available". The JNI boundary below retries
	 * with a small fixed fallback, but under a permanent OOM that fallback
	 * can also be null: no C++ code can manufacture a Java string without
	 * allocating one.
	 */
inline jstring NewStringChecked (JNIEnv * env, std::string_view value) noexcept
{
	try
	{
		std::vector<jchar> utf16;
		utf16.reserve (value.size ());
		size_t i = 0;
		while (i < value.size ())
		{
			const auto b0 = static_cast<uint8_t> (value[i]);
			uint32_t codePoint;
			size_t length;
			if (b0 < 0x80)
			{
				codePoint = b0;
				length = 1;
			}
			else if ((b0 & 0xE0) == 0xC0)
			{
				codePoint = b0 & 0x1F;
				length = 2;
			}
			else if ((b0 & 0xF0) == 0xE0)
			{
				codePoint = b0 & 0x0F;
				length = 3;
			}
			else if ((b0 & 0xF8) == 0xF0)
			{
				codePoint = b0 & 0x07;
				length = 4;
			}
			else
			{
				codePoint = 0xFFFD;
				length = 1;
			}

			if (length > 1)
			{
				bool valid = i + length <= value.size ();
				for (size_t j = 1; valid && j < length; ++j)
				{
					const auto next = static_cast<uint8_t> (value[i + j]);
					if ((next & 0xC0) != 0x80)
					{
						valid = false;
						break;
					}
					codePoint = (codePoint << 6) | (next & 0x3F);
				}
				if (!valid || (length == 2 && codePoint < 0x80) || (length == 3 && codePoint < 0x800)
					|| (length == 4 && codePoint < 0x10000) || codePoint > 0x10FFFF
					|| (codePoint >= 0xD800 && codePoint <= 0xDFFF))
				{
					codePoint = 0xFFFD;
					length = 1;
				}
			}

			if (codePoint <= 0xFFFF)
				utf16.push_back (static_cast<jchar> (codePoint));
			else
			{
				codePoint -= 0x10000;
				utf16.push_back (static_cast<jchar> (0xD800 + (codePoint >> 10)));
				utf16.push_back (static_cast<jchar> (0xDC00 + (codePoint & 0x3FF)));
			}
			i += length;
		}

		if (utf16.size () > static_cast<size_t> (std::numeric_limits<jsize>::max ())) return nullptr;
		// A zero-length NewString must never receive a null jchar pointer:
		// the JNI specification only allows a null pointer for a zero length
		// on some VMs and CheckJNI aborts on it. A stable static element
		// makes the empty case explicit instead of relying on that ambiguity.
		static constexpr jchar kEmptyUtf16 = 0;
		jstring result
			= env->NewString (utf16.empty () ? &kEmptyUtf16 : utf16.data (), static_cast<jsize> (utf16.size ()));
		if (!result && env->ExceptionCheck ()) env->ExceptionClear ();
		return result;
	}
	catch (...)
	{
		return nullptr;
	}
}

/**
	 * Drop a pending java.lang.OutOfMemoryError or other JNI exception
	 * before returning a fallback. Returning to ART with an exception
	 * pending while also returning a value would be a JNI protocol
	 * violation.
	 */
inline void ClearJniException (JNIEnv * env) noexcept
{
	if (env && env->ExceptionCheck ()) env->ExceptionClear ();
}

/** Common try/catch frontier for JNI entries returning void. */
template <typename Callback> void JniVoidCall (JNIEnv * env, Callback && callback) noexcept
{
	try
	{
		callback ();
	}
	catch (...)
	{
		ClearJniException (env);
	}
}

/**
	 * Common try/catch frontier for JNI entries returning a string. A null
	 * result from the callback (for example NewString OOM) is retried with
	 * the bounded fallback text. If even that allocation fails the return
	 * is null; Java treats that as "unavailable" rather than as a valid
	 * snapshot.
	 */
template <typename Callback>
jstring JniStringCall (JNIEnv * env, Callback && callback, std::string_view fallback) noexcept
{
	try
	{
		if (jstring result = callback ()) return result;
	}
	catch (...)
	{
	}
	ClearJniException (env);
	jstring fallbackValue = NewStringChecked (env, fallback);
	if (!fallbackValue) ClearJniException (env);
	return fallbackValue;
}

/** Common try/catch frontier for JNI entries returning a scalar. */
template <typename T, typename Callback> T JniScalarCall (JNIEnv * env, Callback && callback, T fallback) noexcept
{
	try
	{
		const T result = callback ();
		if (env->ExceptionCheck ())
		{
			ClearJniException (env);
			return fallback;
		}
		return result;
	}
	catch (...)
	{
		ClearJniException (env);
		return fallback;
	}
}

/** Maximum number of log records returned in one call (contract). */
constexpr size_t MAX_RECENT_LOG_RECORDS = 256;

/**
	 * UTF-16 bounds applied before any Java string is converted to UTF-8.
	 * A value longer than its operation's bound is rejected without
	 * allocating its UTF-8 form; the address-book and protocol layers
	 * apply the same limits again on their side of the boundary.
	 */
constexpr size_t MAX_TUNNEL_NAME_UNITS = 64;
constexpr size_t MAX_ADDRESS_NAME_UNITS = 255;
constexpr size_t MAX_ADDRESS_DESTINATION_UNITS = 1024;
constexpr size_t MAX_SUBSCRIPTION_UNITS = 512;
constexpr size_t MAX_ROUTER_HASH_UNITS = 44; // base64 of a 32-byte hash
constexpr size_t MAX_BANDWIDTH_CLASS_UNITS = 1;

/**
	 * True when `text` is a non-empty Java string of at most `maximum`
	 * UTF-16 units. The length is read from the VM, so an oversized value
	 * never reaches the UTF-8 conversion.
	 */
bool JavaStringWithin (JNIEnv * env, jstring text, size_t maximum) noexcept
{
	if (!env || !text) return false;
	const jsize length = env->GetStringLength (text);
	if (env->ExceptionCheck ())
	{
		ClearJniException (env);
		return false;
	}
	return length > 0 && static_cast<size_t> (length) <= maximum;
}

/** Bounded phase token for the reseed status record. */
std::string_view ReseedPhaseToken (i2p::data::ReseedPhase phase) noexcept
{
	switch (phase)
	{
	case i2p::data::ReseedPhase::Idle: return "idle";
	case i2p::data::ReseedPhase::Running: return "running";
	case i2p::data::ReseedPhase::Succeeded: return "succeeded";
	case i2p::data::ReseedPhase::Failed: return "failed";
	case i2p::data::ReseedPhase::Disabled: return "disabled";
	default: return "idle";
	}
}

/**
	 * Submit one typed administrative command and translate its result
	 * into the closed outcome vocabulary Java consumes. This is the only
	 * place the app boundary touches the daemon-owned admin queue, and it
	 * can only build the fixed command kinds listed by the JNI entry
	 * points; there is no generic request/verb string.
	 */
std::string SubmitAdminOutcome (
	i2p::tui::AdminCommandKind kind, std::string_view name, std::string_view destination, bool enabled,
	int32_t value = 0)
{
	i2p::tui::AdminCommand command;
	command.kind = kind;
	command.enabled = enabled;
	command.value = value;
	if (!name.empty ())
		std::snprintf (command.name, sizeof (command.name), "%.*s", static_cast<int> (name.size ()), name.data ());
	if (!destination.empty ())
		std::snprintf (command.destination, sizeof (command.destination), "%.*s",
			static_cast<int> (destination.size ()), destination.data ());
	const auto result = i2p::tui::SubmitAdminCommand (command, i2p::tui::ADMIN_COMMAND_TIMEOUT);
	if (result.outcome != i2p::tui::AdminOutcome::None)
		return std::string (i2p::tui::AdminOutcomeName (result.outcome));
	switch (result.code)
	{
	case i2p::tui::AdminResultCode::Ok: return "ok";
	case i2p::tui::AdminResultCode::Accepted: return "accepted";
	case i2p::tui::AdminResultCode::QueueFull: return "busy";
	case i2p::tui::AdminResultCode::Unavailable: return "unavailable";
	case i2p::tui::AdminResultCode::Invalid: return "invalid";
	default: return "failed";
	}
}

/**
	 * Exact, case-sensitive closed set of runtime log level names accepted
	 * by the in-process logger. Log::SetLogLevel() lowercases its argument
	 * and only logs an unknown name, so validating here keeps an invalid
	 * value from reaching native state at all and lets Java report a
	 * definite rejection.
	 */
inline bool IsRuntimeLogLevelName (std::string_view level) noexcept
{
	return level == "none" || level == "critical" || level == "error" || level == "warn" || level == "info"
		|| level == "debug";
}

/** Only the classes accepted by the daemon's typed BANDWIDTH verb. */
inline bool IsRuntimeBandwidthClass (std::string_view bandwidth) noexcept
{
	return bandwidth == "K" || bandwidth == "L" || bandwidth == "M" || bandwidth == "N"
		|| bandwidth == "O" || bandwidth == "P" || bandwidth == "X";
}

/**
	 * Fixed, bounded snapshot returned when collecting the real snapshot
	 * throws or while the router is not running. It has the same keys and
	 * ordering as a normal snapshot so that Java can parse it with the
	 * same code.
	 */
constexpr const char * ERROR_SNAPSHOT = "status=Error\n"
										"statusCode=-1\n"
										"error=snapshot-failed\n"
										"errorCode=-1\n"
										"uptime=0\n"
										"online=0\n"
										"ntcp2=0\n"
										"ssu2=0\n"
										"peers=0\n"
										"routers=0\n"
										"inbound=0\n"
										"outbound=0\n"
										"transit=0\n"
										"reseeding=0\n"
										"inRate=0\n"
										"outRate=0\n"
										"inRate15=0\n"
										"outRate15=0\n"
										"totalIn=0\n"
										"totalOut=0\n"
										"totalTransit=0\n"
										"ipv4=\n"
										"ipv6=";

/**
	 * Snapshot returned while the router is not running (including after a
	 * completed stop). It deliberately does not touch RouterContext, so a
	 * status poll that wins the native-call lock after a stop cannot read
	 * torn-down router state. statusCode=-1 keeps Java from reporting the
	 * router as online.
	 */
constexpr const char * STOPPED_SNAPSHOT = "status=Stopped\n"
										  "statusCode=-1\n"
										  "error=None\n"
										  "errorCode=0\n"
										  "uptime=0\n"
										  "online=0\n"
										  "ntcp2=0\n"
										  "ssu2=0\n"
										  "peers=0\n"
										  "routers=0\n"
										  "inbound=0\n"
										  "outbound=0\n"
										  "transit=0\n"
										  "reseeding=0\n"
										  "inRate=0\n"
										  "outRate=0\n"
										  "inRate15=0\n"
										  "outRate15=0\n"
										  "totalIn=0\n"
										  "totalOut=0\n"
										  "totalTransit=0\n"
										  "ipv4=\n"
										  "ipv6=";

/**
	 * Snapshot published while stop() has set DaemonLifecycle::Stopping
	 * but the native-call lock is still held by the stop itself. A poll
	 * can read this without waiting for the router to finish tearing down
	 * and without touching RouterContext.
	 */
constexpr const char * STOPPING_SNAPSHOT = "status=Stopping\n"
										   "statusCode=-1\n"
										   "error=None\n"
										   "errorCode=0\n"
										   "uptime=0\n"
										   "online=0\n"
										   "ntcp2=0\n"
										   "ssu2=0\n"
										   "peers=0\n"
										   "routers=0\n"
										   "inbound=0\n"
										   "outbound=0\n"
										   "transit=0\n"
										   "reseeding=0\n"
										   "inRate=0\n"
										   "outRate=0\n"
										   "inRate15=0\n"
										   "outRate15=0\n"
										   "totalIn=0\n"
										   "totalOut=0\n"
										   "totalTransit=0\n"
										   "ipv4=\n"
										   "ipv6=";

constexpr const char * STARTING_SNAPSHOT = "status=Starting\n"
										   "statusCode=-1\n"
										   "error=None\n"
										   "errorCode=0\n"
										   "uptime=0\n"
										   "online=0\n"
										   "ntcp2=0\n"
										   "ssu2=0\n"
										   "peers=0\n"
										   "routers=0\n"
										   "inbound=0\n"
										   "outbound=0\n"
										   "transit=0\n"
										   "reseeding=0\n"
										   "inRate=0\n"
										   "outRate=0\n"
										   "inRate15=0\n"
										   "outRate15=0\n"
										   "totalIn=0\n"
										   "totalOut=0\n"
										   "totalTransit=0\n"
										   "ipv4=\n"
										   "ipv6=";

constexpr const char * STOPPED_SUMMARY = "Status: Stopped | Uptime: 0 s | Peers: 0 | Routers: 0 | "
										 "Tunnels in/out/transit: 0/0/0 | Reseeding: no";

constexpr const char * STOPPING_SUMMARY = "Status: Stopping | Uptime: 0 s | Peers: 0 | Routers: 0 | "
										  "Tunnels in/out/transit: 0/0/0 | Reseeding: no";

constexpr const char * STARTING_SUMMARY = "Status: Starting | Uptime: 0 s | Peers: 0 | Routers: 0 | "
										  "Tunnels in/out/transit: 0/0/0 | Reseeding: no";

const char * StatusName (i2p::RouterStatus status)
{
	switch (status)
	{
	case i2p::eRouterStatusOK: return "OK";
	case i2p::eRouterStatusFirewalled: return "Firewalled";
	case i2p::eRouterStatusUnknown: return "Unknown";
	case i2p::eRouterStatusProxy: return "Proxy";
	case i2p::eRouterStatusMesh: return "Mesh";
	case i2p::eRouterStatusStan: return "Stan";
	default: return "Unknown";
	}
}

const char * ErrorName (i2p::RouterError error)
{
	switch (error)
	{
	case i2p::eRouterErrorNone: return "None";
	case i2p::eRouterErrorClockSkew: return "ClockSkew";
	case i2p::eRouterErrorOffline: return "Offline";
	case i2p::eRouterErrorSymmetricNAT: return "SymmetricNAT";
	case i2p::eRouterErrorFullConeNAT: return "FullConeNAT";
	case i2p::eRouterErrorNoDescriptors: return "NoDescriptors";
	default: return "Unknown";
	}
}

const char * TransportName (i2p::data::RouterInfo::TransportStyle style)
{
	switch (style)
	{
	case i2p::data::RouterInfo::eTransportNTCP2: return "NTCP2";
	case i2p::data::RouterInfo::eTransportSSU2: return "SSU2";
	default: return nullptr;
	}
}

/**
	 * Immutable copy of one address. RouterInfo::GetAddresses() returns
	 * the live local addresses; the Address objects and the array can be
	 * replaced concurrently under RouterContext::m_RouterInfoMutex.
	 * Copying the fields while that lock is held (through
	 * CopyRouterInfoBuffer and a private RouterInfo parse) keeps JNI from
	 * iterating live state.
	 */
struct AddressSnapshot
{
	const char * transport = nullptr;
	bool ipv6 = false;
	bool unspecified = true;
	std::string host;
	int port = 0;
	bool published = false;
};

/**
	 * Build an immutable address list from the same signed RouterInfo
	 * buffer the router publishes. The buffer copy is taken under the
	 * RouterInfo mutex by RouterContext::CopyRouterInfoBuffer(); parsing
	 * it into a local RouterInfo means no reference into the router's live
	 * Address objects is retained after this function returns.
	 */
std::vector<AddressSnapshot> CopyAddressSnapshot ()
{
	std::vector<AddressSnapshot> result;
	auto buffer = i2p::context.CopyRouterInfoBuffer ();
	if (!buffer) return result;

	const size_t length = buffer->GetBufferLen ();
	if (!length) return result;

	i2p::data::RouterInfo snapshot (std::move (buffer), length);
	auto addresses = snapshot.GetAddresses ();
	if (!addresses) return result;

	result.reserve (addresses->size ());
	for (const auto & address : *addresses)
	{
		if (!address) continue;
		const char * transport = TransportName (address->transportStyle);
		if (!transport) continue;

		AddressSnapshot item;
		item.transport = transport;
		item.ipv6 = address->host.is_v6 ();
		item.unspecified = address->host.is_unspecified ();
		item.host = i2p::util::net::FormatAddress (address->host);
		item.port = address->port;
		item.published = address->published;
		result.push_back (std::move (item));
	}
	return result;
}

/**
	 * Append one address record as transport|host|port|published. IPv6
	 * hosts are enclosed in brackets so the value stays unambiguous.
	 * Unspecified hosts (0.0.0.0 / ::) are omitted: they are local
	 * bindings, not observed addresses.
	 */
void AppendAddress (std::string & value, bool & first, const AddressSnapshot & address)
{
	if (!address.transport || address.unspecified) return;

	if (!first) value.push_back (';');
	first = false;

	value.append (address.transport);
	value.push_back ('|');
	if (address.ipv6)
	{
		value.push_back ('[');
		value.append (address.host);
		value.push_back (']');
	}
	else
		value.append (address.host);
	value.push_back ('|');
	value.append (std::to_string (address.port));
	value.push_back ('|');
	value.push_back (address.published ? '1' : '0');
}

void AppendField (std::string & out, const char * key, const std::string & value)
{
	out.append (key);
	out.push_back ('=');
	out.append (value);
	out.push_back ('\n');
}

void AppendField (std::string & out, const char * key, const char * value)
{
	out.append (key);
	out.push_back ('=');
	out.append (value);
	out.push_back ('\n');
}

/**
	 * Collect a status snapshot. Values are read from the existing const
	 * accessors only; no keys, pointers or private router material are
	 * exposed. The snapshot is not a single atomic instant: individual
	 * counters are internally synchronized but may be sampled at slightly
	 * different times. Addresses come from an immutable copy, never from a
	 * live GetAddresses() iteration.
	 */
std::string BuildStatusSnapshot ()
{
	std::string out;
	out.reserve (1024);

	const auto status = i2p::context.GetStatus ();
	const auto error = i2p::context.GetError ();
	const auto & transports = i2p::transport::transports;
	const auto & netdb = i2p::data::netdb;
	const auto & tunnels = i2p::tunnel::tunnels;

	AppendField (out, "status", StatusName (status));
	AppendField (out, "statusCode", std::to_string (static_cast<int> (status)));
	AppendField (out, "error", ErrorName (error));
	AppendField (out, "errorCode", std::to_string (static_cast<int> (error)));
	AppendField (out, "uptime", std::to_string (i2p::context.GetUptime ()));
	AppendField (out, "online", transports.IsOnline () ? "1" : "0");
	AppendField (out, "ntcp2", transports.IsBoundNTCP2 () ? "1" : "0");
	AppendField (out, "ssu2", transports.IsBoundSSU2 () ? "1" : "0");
	AppendField (out, "peers", std::to_string (transports.GetNumPeers ()));
	AppendField (out, "routers", std::to_string (netdb.GetNumRouters ()));
	AppendField (out, "inbound", std::to_string (tunnels.CountInboundTunnels ()));
	AppendField (out, "outbound", std::to_string (tunnels.CountOutboundTunnels ()));
	AppendField (out, "transit", std::to_string (tunnels.CountTransitTunnels ()));
	AppendField (out, "reseeding", netdb.IsReseeding () ? "1" : "0");
	AppendField (out, "inRate", std::to_string (transports.GetInBandwidth ()));
	AppendField (out, "outRate", std::to_string (transports.GetOutBandwidth ()));
	AppendField (out, "inRate15", std::to_string (transports.GetInBandwidth15s ()));
	AppendField (out, "outRate15", std::to_string (transports.GetOutBandwidth15s ()));
	AppendField (out, "totalIn", std::to_string (transports.GetTotalReceivedBytes ()));
	AppendField (out, "totalOut", std::to_string (transports.GetTotalSentBytes ()));
	AppendField (out, "totalTransit", std::to_string (transports.GetTotalTransitTransmittedBytes ()));

	std::string ipv4, ipv6;
	bool firstV4 = true, firstV6 = true;
	for (const auto & address : CopyAddressSnapshot ())
	{
		if (!address.ipv6)
			AppendAddress (ipv4, firstV4, address);
		else
			AppendAddress (ipv6, firstV6, address);
	}
	AppendField (out, "ipv4", ipv4);
	AppendField (out, "ipv6", ipv6);

	// Keep exactly one line per field and no trailing newline.
	out.pop_back ();
	return out;
}

/**
	 * Copy recent log records. The text field is the bounded, sanitized
	 * copy made by Log::Append. Each record is sequence|timestampSeconds|
	 * numericLevel|text and records are separated by a single newline.
	 */
std::string BuildRecentLogs (uint64_t after, size_t limit)
{
	auto records = i2p::log::Logger ().GetRecent (after, limit);
	std::string out;
	out.reserve (records.size () * 96);
	for (const auto & record : records)
	{
		if (!out.empty ()) out.push_back ('\n');
		out.append (std::to_string (record.sequence));
		out.push_back ('|');
		out.append (std::to_string (static_cast<long long> (record.timestamp)));
		out.push_back ('|');
		out.append (std::to_string (static_cast<int> (record.level)));
		out.push_back ('|');
		out.append (record.text);
	}
	return out;
}

/**
	 * Release every still-live SAF document handle and tree capability
	 * while the daemon and this JNI thread are alive. Defined by the SAF
	 * section below; it is a no-op when the torrent engine is compiled
	 * out. Calling it after the daemon has stopped keeps document cleanup
	 * off the payload destructors, which may run on a native worker thread
	 * or during process teardown where no JNI attachment is permitted.
	 */
void DetachSafPayloads () noexcept;
}

JNIEXPORT jstring JNICALL Java_org_openi2pd_router_I2PD_1JNI_getABICompiledWith (JNIEnv * env, jclass clazz)
{
#if defined(__arm__)
#if defined(__ARM_ARCH_7A__)
#if defined(__ARM_NEON__)
#if defined(__ARM_PCS_VFP)
#define ABI "armeabi-v7a/NEON (hard-float)"
#else
#define ABI "armeabi-v7a/NEON"
#endif
#else
#if defined(__ARM_PCS_VFP)
#define ABI "armeabi-v7a (hard-float)"
#else
#define ABI "armeabi-v7a"
#endif
#endif
#else
#define ABI "armeabi"
#endif
#elif defined(__i386__)
#define ABI "x86"
#elif defined(__x86_64__)
#define ABI "x86_64"
#elif defined(__mips64) /* mips64el-* toolchain defines __mips__ too */
#define ABI "mips64"
#elif defined(__mips__)
#define ABI "mips"
#elif defined(__aarch64__)
#define ABI "arm64-v8a"
#else
#define ABI "unknown"
#endif

	return JniStringCall (env, [&] () { return NewStringChecked (env, ABI); }, "unknown");
}

/**
 * Start the daemon for a process that already claimed the router role.
 *
 * A SAF storage-broker process that reaches this entry point is refused
 * with the fixed PROCESS_ROLE_CONFLICT token before any router state
 * exists. The whole transition runs under the native-call guard; a
 * start-cancellation request may still abort it because that token is
 * lock-free.
 *
 * @return "ok", a bounded diagnostic, START_CANCELED_RESULT, or the
 * fixed role-conflict token
 */
JNIEXPORT jstring JNICALL Java_org_openi2pd_router_I2PD_1JNI_startDaemon (JNIEnv * env, jclass clazz)
{
	return JniStringCall (
		env,
		[&] () -> jstring
		{
			// Only a process that already claimed the router role may start the
			// daemon. The SAF storage broker runs in its own process with the
			// same UID, so identity alone cannot separate them; a broker that
			// reached this entry point must be refused before any router state
			// is created, and the fixed token carries no private detail.
			if (!i2p::android::HasProcessRole (i2p::android::ProcessRole::Router))
				return NewStringChecked (env, i2p::android::PROCESS_ROLE_CONFLICT);
			i2p::android::NativeCallGuard call;
			return NewStringChecked (env, i2p::android::start ());
		},
		"Daemon start failed");
}

/**
	 * Claim this process role. A claim is accepted once and is
	 * irreversible; a conflicting or unknown role returns false and must
	 * keep the caller from starting a router or obtaining document
	 * authority.
	 */
extern "C" JNIEXPORT jboolean JNICALL Java_org_openi2pd_router_I2PD_1JNI_claimProcessRole (
	JNIEnv * env, jclass, jint role) noexcept
{
	try
	{
		const bool claimed = i2p::android::ClaimProcessRole (static_cast<i2p::android::ProcessRole> (role));
		return claimed ? JNI_TRUE : JNI_FALSE;
	}
	catch (...)
	{
		ClearJniException (env);
		return JNI_FALSE;
	}
}

/** Process role: 0 none, 1 router, 2 storage broker. */
extern "C" JNIEXPORT jint JNICALL Java_org_openi2pd_router_I2PD_1JNI_processRole (JNIEnv *, jclass) noexcept
{
	return static_cast<jint> (i2p::android::CurrentProcessRole ());
}

/**
	 * Publish a start-cancellation request. This deliberately takes no
	 * native lock: a start transition owns the native-call guard for its
	 * whole duration, so a guarded stop request could never reach it.
	 */
extern "C" JNIEXPORT void JNICALL Java_org_openi2pd_router_I2PD_1JNI_requestStopNative (JNIEnv *, jclass) noexcept
{
	i2p::android::RequestStartCancel ();
}

/**
 * Recorded application data directory of the router.
 *
 * Reads only the stateMutex-protected string, so the settings editor
 * can ask for it without waiting behind a start that is copying
 * packaged assets.
 */
JNIEXPORT jstring JNICALL Java_org_openi2pd_router_I2PD_1JNI_getDataDir (JNIEnv * env, jclass clazz)
{
	return JniStringCall (
		env,
		[&] ()
		{
			// Only reads the stateMutex-protected string. It deliberately does
			// not take the native-call lock: the settings editor may ask for it
			// on the UI thread while startDaemon() is waiting for the packaged
			// assets.
			return NewStringChecked (env, i2p::android::GetDataDir ());
		},
		"");
}

/**
 * Ordered native stop under the native-call guard.
 *
 * Java publishes its own "stopping" state first and calls this only
 * from the RouterService worker. A repeated stop is a no-op, and the
 * remaining SAF payload capabilities are released on this attached JNI
 * thread so no JNI runs from a payload destructor.
 */
JNIEXPORT void JNICALL Java_org_openi2pd_router_I2PD_1JNI_stopDaemon (JNIEnv * env, jclass clazz)
{
	JniVoidCall (env,
		[] ()
		{
			// The guard serializes this against start/status. Java is expected
			// to publish its own "stopping" state and to call this only from the
			// RouterService worker; the native lifecycle mirror cannot enforce
			// the Java thread.
			i2p::android::NativeCallGuard call;
			i2p::android::stop ();
			// The daemon just joined its torrent workers, so no tunnel is using
			// a document handle any more. Releasing the remaining SAF
			// capabilities here, on this attached JNI thread, keeps JNI out of
			// the payload destructors, which may run later on a worker thread or
			// during process teardown.
			DetachSafPayloads ();
		});
}

JNIEXPORT void JNICALL Java_org_openi2pd_router_I2PD_1JNI_stopAcceptingTunnels (JNIEnv * env, jclass clazz)
{
	JniVoidCall (env,
		[] ()
		{
			i2p::android::NativeCallGuard call;
			if (!call.IsRunning ()) return;
			i2p::context.SetAcceptsTunnels (false);
		});
}

JNIEXPORT void JNICALL Java_org_openi2pd_router_I2PD_1JNI_startAcceptingTunnels (JNIEnv * env, jclass clazz)
{
	JniVoidCall (env,
		[] ()
		{
			i2p::android::NativeCallGuard call;
			if (!call.IsRunning ()) return;
			i2p::context.SetAcceptsTunnels (true);
		});
}

JNIEXPORT void JNICALL Java_org_openi2pd_router_I2PD_1JNI_reloadTunnelsConfigs (JNIEnv * env, jclass clazz)
{
	JniVoidCall (env,
		[] ()
		{
			i2p::android::NativeCallGuard call;
			if (!call.IsRunning ()) return;
			i2p::client::context.ReloadConfig ();
		});
}

/**
	 * Ask the running netDb to reseed. Never blocks:
	 * NetDb::RequestReseed() starts the owned, cancellable job and returns
	 * immediately, so the service worker (and therefore the UI) is not
	 * held for the length of a bootstrap.
	 *
	 * @return one of the closed outcome tokens {@code started}, {@code
	 * already-running}, {@code disabled}, {@code accepted}, {@code busy}
	 * or {@code unavailable}. No URL, path or option is accepted from
	 * Java.
	 */
JNIEXPORT jstring JNICALL Java_org_openi2pd_router_I2PD_1JNI_requestReseed (JNIEnv * env, jclass clazz)
{
	return JniStringCall (
		env,
		[&] () -> jstring
		{
			i2p::android::NativeCallGuard call;
			if (!call.IsRunning ()) return NewStringChecked (env, "unavailable");
			return NewStringChecked (env, SubmitAdminOutcome (i2p::tui::AdminCommandKind::Reseed, { }, { }, false));
		},
		"failed");
}

/** Change transit acceptance through the daemon-owned typed queue. */
JNIEXPORT jstring JNICALL Java_org_openi2pd_router_I2PD_1JNI_setTransit (
	JNIEnv * env, jclass clazz, jboolean enabled)
{
	return JniStringCall (
		env,
		[&] () -> jstring
		{
			if (enabled != JNI_TRUE && enabled != JNI_FALSE) return NewStringChecked (env, "invalid");
			i2p::android::NativeCallGuard call;
			if (!call.IsRunning ()) return NewStringChecked (env, "unavailable");
			return NewStringChecked (env,
				SubmitAdminOutcome (i2p::tui::AdminCommandKind::Transit, { }, { }, enabled == JNI_TRUE));
		},
		"failed");
}

/** Apply one closed bandwidth class without writing configuration. */
JNIEXPORT jstring JNICALL Java_org_openi2pd_router_I2PD_1JNI_setBandwidth (
	JNIEnv * env, jclass clazz, jstring bandwidth)
{
	return JniStringCall (
		env,
		[&] () -> jstring
		{
			if (!JavaStringWithin (env, bandwidth, MAX_BANDWIDTH_CLASS_UNITS))
				return NewStringChecked (env, "invalid");
			ScopedUtfChars bandwidthUtf8 (env, bandwidth);
			if (!bandwidthUtf8 || !IsRuntimeBandwidthClass (bandwidthUtf8.view ()))
				return NewStringChecked (env, "invalid");
			i2p::android::NativeCallGuard call;
			if (!call.IsRunning ()) return NewStringChecked (env, "unavailable");
			const auto token = static_cast<int32_t> (bandwidthUtf8.view ().front ());
			return NewStringChecked (env,
				SubmitAdminOutcome (i2p::tui::AdminCommandKind::Bandwidth, { }, { }, false, token));
		},
		"failed");
}

/** Apply a bounded share ratio without writing config. */
JNIEXPORT jstring JNICALL Java_org_openi2pd_router_I2PD_1JNI_setShareRatio (
	JNIEnv * env, jclass clazz, jint percent)
{
	return JniStringCall (
		env,
		[&] () -> jstring
		{
			if (percent < 0 || percent > 100) return NewStringChecked (env, "invalid");
			i2p::android::NativeCallGuard call;
			if (!call.IsRunning ()) return NewStringChecked (env, "unavailable");
			return NewStringChecked (env, SubmitAdminOutcome (
				i2p::tui::AdminCommandKind::Share, { }, { }, false, static_cast<int32_t> (percent)));
		},
		"failed");
}

/**
	 * Bounded, non-secret reseed status: {@code
	 * Fields are phase, enabled, attempts, succeeded, lastAttemptMs,
	 * lastSuccessMs and lastError, separated by {@code |}.
	 * The record never waits for the job and never carries a URL or
	 * credential.
	 */
JNIEXPORT jstring JNICALL Java_org_openi2pd_router_I2PD_1JNI_getReseedStatus (JNIEnv * env, jclass clazz)
{
	return JniStringCall (
		env,
		[&] () -> jstring
		{
			i2p::android::NativeCallGuard call;
			if (!call.IsRunning ()) return NewStringChecked (env, "");
			const auto state = i2p::data::netdb.GetReseedState ();
			std::string out (ReseedPhaseToken (state.phase));
			out.push_back ('|');
			out.push_back (state.enabled ? '1' : '0');
			out.push_back ('|');
			out.append (std::to_string (state.attempts));
			out.push_back ('|');
			out.append (std::to_string (state.succeeded));
			out.push_back ('|');
			out.append (std::to_string (state.lastAttemptMs));
			out.push_back ('|');
			out.append (std::to_string (state.lastSuccessMs));
			out.push_back ('|');
			std::string lastError (state.lastError.substr (0, i2p::data::RESEED_MAX_ERROR_LENGTH));
			for (auto & ch : lastError)
				if (ch == '|' || ch == '\n' || ch == '\r' || static_cast<unsigned char> (ch) < 0x20 || ch == 0x7f)
					ch = ' ';
			out.append (lastError);
			return NewStringChecked (env, out);
		},
		"");
}

/**
	 * Configured client and server tunnels, one bounded record per line:
	 * {@code name|kind|running|localEndpoint|destination}. Only public
	 * labels cross the boundary; no private key, identity file or storage
	 * path.
	 */
JNIEXPORT jstring JNICALL Java_org_openi2pd_router_I2PD_1JNI_getTunnelServices (JNIEnv * env, jclass clazz)
{
	return JniStringCall (
		env,
		[&] () -> jstring
		{
			i2p::android::NativeCallGuard call;
			if (!call.IsRunning ()) return NewStringChecked (env, "");
			// A configuration value may contain the row separator or a control
			// character; replace those so one field cannot forge another row.
			auto clean = [] (std::string text)
			{
				constexpr size_t MAX_FIELD = 256;
				if (text.size () > MAX_FIELD) text.resize (MAX_FIELD);
				for (auto & ch : text)
					if (ch == '|' || ch == '\n' || ch == '\r' || static_cast<unsigned char> (ch) < 0x20 || ch == 0x7f)
						ch = ' ';
				return text;
			};
			std::string out;
			for (const auto & service : i2p::client::context.GetTunnelServices ())
			{
				if (out.size () > 48 * 1024) break;
				out.append (clean (service.name));
				out.push_back ('|');
				out.append (clean (service.kind));
				out.push_back ('|');
				out.push_back (service.running ? '1' : '0');
				out.push_back ('|');
				out.append (clean (service.localEndpoint));
				out.push_back ('|');
				out.append (clean (service.destination));
				out.push_back ('\n');
			}
			return NewStringChecked (env, out);
		},
		"");
}

/** Start one configured tunnel by its bounded service name. */
JNIEXPORT jstring JNICALL Java_org_openi2pd_router_I2PD_1JNI_startTunnel (JNIEnv * env, jclass clazz, jstring name)
{
	return JniStringCall (
		env,
		[&] () -> jstring
		{
			if (!JavaStringWithin (env, name, MAX_TUNNEL_NAME_UNITS)) return NewStringChecked (env, "invalid");
			ScopedUtfChars nameUtf8 (env, name);
			if (!nameUtf8 || nameUtf8.view ().empty ()) return NewStringChecked (env, "invalid");
			i2p::android::NativeCallGuard call;
			if (!call.IsRunning ()) return NewStringChecked (env, "unavailable");
			return NewStringChecked (
				env, SubmitAdminOutcome (i2p::tui::AdminCommandKind::TunnelStart, nameUtf8.view (), { }, false));
		},
		"failed");
}

/** Stop one configured tunnel by its bounded service name. */
JNIEXPORT jstring JNICALL Java_org_openi2pd_router_I2PD_1JNI_stopTunnel (JNIEnv * env, jclass clazz, jstring name)
{
	return JniStringCall (
		env,
		[&] () -> jstring
		{
			if (!JavaStringWithin (env, name, MAX_TUNNEL_NAME_UNITS)) return NewStringChecked (env, "invalid");
			ScopedUtfChars nameUtf8 (env, name);
			if (!nameUtf8 || nameUtf8.view ().empty ()) return NewStringChecked (env, "invalid");
			i2p::android::NativeCallGuard call;
			if (!call.IsRunning ()) return NewStringChecked (env, "unavailable");
			return NewStringChecked (
				env, SubmitAdminOutcome (i2p::tui::AdminCommandKind::TunnelStop, nameUtf8.view (), { }, false));
		},
		"failed");
}

/**
	 * Add or replace one address-book name.
	 *
	 * @param replace {@code false} never re-points an existing name and
	 * reports {@code exists}; {@code true} is the explicit overwrite.
	 * @return {@code ok}, {@code exists}, {@code invalid}, {@code
	 * unavailable} or {@code failed}
	 */
JNIEXPORT jstring JNICALL Java_org_openi2pd_router_I2PD_1JNI_addAddressWithResult (
	JNIEnv * env, jclass clazz, jstring name, jstring destination, jboolean replace)
{
	return JniStringCall (
		env,
		[&] () -> jstring
		{
			if (!JavaStringWithin (env, name, MAX_ADDRESS_NAME_UNITS)
				|| !JavaStringWithin (env, destination, MAX_ADDRESS_DESTINATION_UNITS))
				return NewStringChecked (env, "invalid");
			ScopedUtfChars nameUtf8 (env, name);
			ScopedUtfChars destinationUtf8 (env, destination);
			if (!nameUtf8 || !destinationUtf8) return NewStringChecked (env, "invalid");
			i2p::android::NativeCallGuard call;
			if (!call.IsRunning ()) return NewStringChecked (env, "unavailable");
			const auto outcome = i2p::client::context.GetAddressBook ().AddAddressChecked (
				nameUtf8.view (), destinationUtf8.view (), replace == JNI_TRUE);
			switch (outcome)
			{
			case i2p::client::AddressAddResult::Ok: return NewStringChecked (env, "ok");
			case i2p::client::AddressAddResult::Exists: return NewStringChecked (env, "exists");
			case i2p::client::AddressAddResult::InvalidName:
			case i2p::client::AddressAddResult::InvalidDestination: return NewStringChecked (env, "invalid");
			case i2p::client::AddressAddResult::Unavailable: return NewStringChecked (env, "unavailable");
			default: return NewStringChecked (env, "failed");
			}
		},
		"failed");
}

/** Address-book subscription sources, one bounded URL per line. */
JNIEXPORT jstring JNICALL Java_org_openi2pd_router_I2PD_1JNI_getSubscriptions (JNIEnv * env, jclass clazz)
{
	return JniStringCall (
		env,
		[&] () -> jstring
		{
			i2p::android::NativeCallGuard call;
			if (!call.IsRunning ()) return NewStringChecked (env, "");
			std::string out;
			for (const auto & url : i2p::client::context.GetAddressBook ().GetSubscriptions (32))
			{
				if (out.size () + url.size () + 1 > 32 * 1024) break;
				out.append (url);
				out.push_back ('\n');
			}
			return NewStringChecked (env, out);
		},
		"");
}

/** Add a validated subscription source through a typed daemon verb. */
JNIEXPORT jstring JNICALL Java_org_openi2pd_router_I2PD_1JNI_addSubscription (JNIEnv * env, jclass clazz, jstring url)
{
	return JniStringCall (
		env,
		[&] () -> jstring
		{
			if (!JavaStringWithin (env, url, MAX_SUBSCRIPTION_UNITS)) return NewStringChecked (env, "invalid");
			ScopedUtfChars urlUtf8 (env, url);
			if (!urlUtf8 || urlUtf8.view ().empty ()) return NewStringChecked (env, "invalid");
			i2p::android::NativeCallGuard call;
			if (!call.IsRunning ()) return NewStringChecked (env, "unavailable");
			return NewStringChecked (
				env, SubmitAdminOutcome (i2p::tui::AdminCommandKind::AddressSubAdd, { }, urlUtf8.view (), false));
		},
		"failed");
}

/** Remove one subscription source by its exact URL. */
JNIEXPORT jstring JNICALL Java_org_openi2pd_router_I2PD_1JNI_removeSubscription (
	JNIEnv * env, jclass clazz, jstring url)
{
	return JniStringCall (
		env,
		[&] () -> jstring
		{
			if (!JavaStringWithin (env, url, MAX_SUBSCRIPTION_UNITS)) return NewStringChecked (env, "invalid");
			ScopedUtfChars urlUtf8 (env, url);
			if (!urlUtf8 || urlUtf8.view ().empty ()) return NewStringChecked (env, "invalid");
			i2p::android::NativeCallGuard call;
			if (!call.IsRunning ()) return NewStringChecked (env, "unavailable");
			return NewStringChecked (
				env, SubmitAdminOutcome (i2p::tui::AdminCommandKind::AddressSubRemove, { }, urlUtf8.view (), false));
		},
		"failed");
}

/** Ban one router identity (base64) until the router restarts. */
JNIEXPORT jstring JNICALL Java_org_openi2pd_router_I2PD_1JNI_banRouter (JNIEnv * env, jclass clazz, jstring hash)
{
	return JniStringCall (
		env,
		[&] () -> jstring
		{
			if (!JavaStringWithin (env, hash, MAX_ROUTER_HASH_UNITS)) return NewStringChecked (env, "invalid");
			ScopedUtfChars hashUtf8 (env, hash);
			if (!hashUtf8 || hashUtf8.view ().empty ()) return NewStringChecked (env, "invalid");
			i2p::android::NativeCallGuard call;
			if (!call.IsRunning ()) return NewStringChecked (env, "unavailable");
			return NewStringChecked (
				env, SubmitAdminOutcome (i2p::tui::AdminCommandKind::NetDbBan, hashUtf8.view (), { }, false));
		},
		"failed");
}

/** Remove one in-memory router ban. */
JNIEXPORT jstring JNICALL Java_org_openi2pd_router_I2PD_1JNI_unbanRouter (JNIEnv * env, jclass clazz, jstring hash)
{
	return JniStringCall (
		env,
		[&] () -> jstring
		{
			if (!JavaStringWithin (env, hash, MAX_ROUTER_HASH_UNITS)) return NewStringChecked (env, "invalid");
			ScopedUtfChars hashUtf8 (env, hash);
			if (!hashUtf8 || hashUtf8.view ().empty ()) return NewStringChecked (env, "invalid");
			i2p::android::NativeCallGuard call;
			if (!call.IsRunning ()) return NewStringChecked (env, "unavailable");
			return NewStringChecked (
				env, SubmitAdminOutcome (i2p::tui::AdminCommandKind::NetDbUnban, hashUtf8.view (), { }, false));
		},
		"failed");
}

/**
	 * Apply one runtime log level to the running logger.
	 *
	 * Only the exact closed set IsRuntimeLogLevelName() accepts is
	 * forwarded; the value is validated before the native-call lock is
	 * taken so an unknown name cannot change any router state. Nothing is
	 * written to i2pd.conf and no restart is required.
	 */
JNIEXPORT jboolean JNICALL Java_org_openi2pd_router_I2PD_1JNI_setRuntimeLogLevel (
	JNIEnv * env, jclass clazz, jstring jlevel)
{
	return JniScalarCall<jboolean> (
		env,
		[&] () -> jboolean
		{
			ScopedUtfChars level (env, jlevel, 64);
			if (!level)
			{
				ClearJniException (env);
				return JNI_FALSE;
			}
			const std::string_view name (level.c_str ());
			if (!IsRuntimeLogLevelName (name)) return JNI_FALSE;

			i2p::android::NativeCallGuard call;
			if (!call.IsRunning ()) return JNI_FALSE;
			i2p::log::Logger ().SetLogLevel (std::string (name));
			return JNI_TRUE;
		},
		JNI_FALSE);
}

/**
 * Publish the platform connectivity state to the transports.
 *
 * Java calls this from the ConnectivityManager callback rather than the
 * service worker, so the guard is the defensive boundary: it serializes
 * the update against start/stop and ignores it once the router is not
 * Running.
 */
JNIEXPORT void JNICALL Java_org_openi2pd_router_I2PD_1JNI_onNetworkStateChanged (
	JNIEnv * env, jclass clazz, jboolean isConnected)
{
	JniVoidCall (env,
		[isConnected] ()
		{
			// Java invokes this from the ConnectivityManager callback, not from
			// the RouterService worker. The native guard is the defensive
			// boundary: it serializes the update against start/stop and ignores
			// it once the router is no longer Running.
			i2p::android::NativeCallGuard call;
			if (!call.IsRunning ()) return;
			i2p::transport::transports.SetOnline (isConnected != JNI_FALSE);
		});
}

/**
 * Record the application data directory for the next start.
 *
 * State-only, bounded to 4096 UTF-16 units by ScopedUtfChars, and
 * deliberately outside the native-call lock: the daemon start worker
 * sets it while the router may still be waiting for packaged assets.
 */
JNIEXPORT void JNICALL Java_org_openi2pd_router_I2PD_1JNI_setDataDir (JNIEnv * env, jclass clazz, jstring jdataDir)
{
	JniVoidCall (env,
		[&] ()
		{
			// State-only update (see getDataDir). It must not wait behind a
			// multi-second start while the Java caller prepares the daemon.
			ScopedUtfChars dataDir (env, jdataDir, 4096);
			if (!dataDir)
			{
				ClearJniException (env);
				return;
			}
			i2p::android::SetDataDir (dataDir.c_str ());
		});
}

JNIEXPORT jint JNICALL Java_org_openi2pd_router_I2PD_1JNI_getTransitTunnelsCount (JNIEnv * env, jclass clazz)
{
	return JniScalarCall<jint> (
		env,
		[] ()
		{
			i2p::android::NativeCallGuard call;
			if (!call.IsRunning ()) return static_cast<jint> (0);
			return static_cast<jint> (i2p::tunnel::tunnels.CountTransitTunnels ());
		},
		0);
}

JNIEXPORT jstring JNICALL Java_org_openi2pd_router_I2PD_1JNI_getStatusSummary (JNIEnv * env, jclass clazz)
{
	return JniStringCall (
		env,
		[&] () -> jstring
		{
			// Do not wait behind a stop that already published Stopping: the
			// status must be observable even though the native-call lock is held
			// by the teardown.
			const auto lifecycle = i2p::android::GetDaemonLifecycle ();
			if (lifecycle == i2p::android::DaemonLifecycle::Stopping) return NewStringChecked (env, STOPPING_SUMMARY);
			if (lifecycle == i2p::android::DaemonLifecycle::Starting) return NewStringChecked (env, STARTING_SUMMARY);

			i2p::android::NativeCallGuard call;
			if (!call.IsRunning ()) return NewStringChecked (env, STOPPED_SUMMARY);

			std::string status;
			switch (i2p::context.GetStatus ())
			{
			case i2p::eRouterStatusOK: status = "OK"; break;
			case i2p::eRouterStatusFirewalled: status = "Firewalled"; break;
			case i2p::eRouterStatusUnknown: status = "Unknown"; break;
			case i2p::eRouterStatusProxy: status = "Proxy"; break;
			case i2p::eRouterStatusMesh: status = "Mesh"; break;
			case i2p::eRouterStatusStan: status = "Stan"; break;
			default: status = "Unknown";
			}
			std::ostringstream out;
			out << "Status: " << status << " | Uptime: " << i2p::context.GetUptime () << " s"
				<< " | Peers: " << i2p::transport::transports.GetNumPeers ()
				<< " | Routers: " << i2p::data::netdb.GetNumRouters ()
				<< " | Tunnels in/out/transit: " << i2p::tunnel::tunnels.CountInboundTunnels () << "/"
				<< i2p::tunnel::tunnels.CountOutboundTunnels () << "/" << i2p::tunnel::tunnels.CountTransitTunnels ()
				<< " | Reseeding: " << (i2p::data::netdb.IsReseeding () ? "yes" : "no");
			return NewStringChecked (env, out.str ());
		},
		"Status unavailable");
}

JNIEXPORT jstring JNICALL Java_org_openi2pd_router_I2PD_1JNI_getStatusSnapshot (JNIEnv * env, jclass clazz)
{
	return JniStringCall (
		env,
		[&] () -> jstring
		{
			const auto lifecycle = i2p::android::GetDaemonLifecycle ();
			if (lifecycle == i2p::android::DaemonLifecycle::Stopping) return NewStringChecked (env, STOPPING_SNAPSHOT);
			if (lifecycle == i2p::android::DaemonLifecycle::Starting) return NewStringChecked (env, STARTING_SNAPSHOT);

			i2p::android::NativeCallGuard call;
			if (!call.IsRunning ()) return NewStringChecked (env, STOPPED_SNAPSHOT);
			try
			{
				return NewStringChecked (env, BuildStatusSnapshot ());
			}
			catch (const std::exception &)
			{
				return NewStringChecked (env, ERROR_SNAPSHOT);
			}
			catch (...)
			{
				return NewStringChecked (env, ERROR_SNAPSHOT);
			}
		},
		ERROR_SNAPSHOT);
}

JNIEXPORT jstring JNICALL Java_org_openi2pd_router_I2PD_1JNI_getRecentLogs (
	JNIEnv * env, jclass clazz, jlong afterSequence, jint limit)
{
	return JniStringCall (
		env,
		[&] () -> jstring
		{
			i2p::android::NativeCallGuard call;
			const uint64_t after = afterSequence > 0 ? static_cast<uint64_t> (afterSequence) : 0;
			size_t maxRecords = limit > 0 ? static_cast<size_t> (limit) : 1;
			if (maxRecords > MAX_RECENT_LOG_RECORDS) maxRecords = MAX_RECENT_LOG_RECORDS;
			return NewStringChecked (env, BuildRecentLogs (after, maxRecords));
		},
		"");
}

JNIEXPORT jstring JNICALL Java_org_openi2pd_router_I2PD_1JNI_getAddressBook (JNIEnv * env, jclass)
{
	return JniStringCall (
		env,
		[&] ()
		{
			i2p::android::NativeCallGuard call;
			if (!call.IsRunning ()) return NewStringChecked (env, "");
			std::string result;
			const auto entries = i2p::client::context.GetAddressBook ().GetEntries (200);
			for (const auto & entry : entries)
			{
				if (result.size () + entry.name.size () + entry.destination.size () + 4 > 64 * 1024) break;
				result.append (entry.name);
				result.push_back ('|');
				result.append (entry.destination);
				result.push_back ('|');
				result.push_back (entry.blinded ? '1' : '0');
				result.push_back ('\n');
			}
			return NewStringChecked (env, result);
		},
		"");
}

JNIEXPORT jboolean JNICALL Java_org_openi2pd_router_I2PD_1JNI_removeAddress (JNIEnv * env, jclass, jstring name)
{
	return JniScalarCall<jboolean> (
		env,
		[&] ()
		{
			if (!JavaStringWithin (env, name, MAX_ADDRESS_NAME_UNITS)) return JNI_FALSE;
			ScopedUtfChars nameUtf8 (env, name);
			if (!nameUtf8) return JNI_FALSE;
			i2p::android::NativeCallGuard call;
			if (!call.IsRunning ()) return JNI_FALSE;
			return i2p::client::context.GetAddressBook ().RemoveAddress (nameUtf8.c_str ())
					== i2p::client::AddressBookMutationResult::Ok
				? JNI_TRUE
				: JNI_FALSE;
		},
		JNI_FALSE);
}

JNIEXPORT jboolean JNICALL Java_org_openi2pd_router_I2PD_1JNI_blockAddress (JNIEnv * env, jclass, jstring name)
{
	return JniScalarCall<jboolean> (
		env,
		[&] ()
		{
			if (!JavaStringWithin (env, name, MAX_ADDRESS_NAME_UNITS)) return JNI_FALSE;
			ScopedUtfChars nameUtf8 (env, name);
			if (!nameUtf8) return JNI_FALSE;
			i2p::android::NativeCallGuard call;
			if (!call.IsRunning ()) return JNI_FALSE;
			return i2p::client::context.GetAddressBook ().BlockAddress (nameUtf8.c_str ())
					== i2p::client::AddressBookMutationResult::Ok
				? JNI_TRUE
				: JNI_FALSE;
		},
		JNI_FALSE);
}

JNIEXPORT jboolean JNICALL Java_org_openi2pd_router_I2PD_1JNI_unblockAddress (JNIEnv * env, jclass, jstring name)
{
	return JniScalarCall<jboolean> (
		env,
		[&] ()
		{
			if (!JavaStringWithin (env, name, MAX_ADDRESS_NAME_UNITS)) return JNI_FALSE;
			ScopedUtfChars nameUtf8 (env, name);
			if (!nameUtf8) return JNI_FALSE;
			i2p::android::NativeCallGuard call;
			if (!call.IsRunning ()) return JNI_FALSE;
			return i2p::client::context.GetAddressBook ().UnblockAddress (nameUtf8.c_str ())
					== i2p::client::AddressBookMutationResult::Ok
				? JNI_TRUE
				: JNI_FALSE;
		},
		JNI_FALSE);
}

JNIEXPORT jstring JNICALL Java_org_openi2pd_router_I2PD_1JNI_getBlockedAddresses (JNIEnv * env, jclass)
{
	return JniStringCall (
		env,
		[&] ()
		{
			i2p::android::NativeCallGuard call;
			if (!call.IsRunning ()) return NewStringChecked (env, "");
			std::string result;
			const auto patterns = i2p::client::context.GetAddressBook ().GetDeletedHosts (200);
			for (const auto & pattern : patterns)
			{
				if (result.size () + pattern.size () + 1 > 64 * 1024) break;
				result.append (pattern);
				result.push_back ('\n');
			}
			return NewStringChecked (env, result);
		},
		"");
}

/**
 * Run one torrent-only control request from the Android UI.
 *
 * The request is a bounded standard-UTF-8 I2PDTUI/1 line, parsed by the
 * daemon-owned admin queue. Only the torrent types allowed in the
 * switch below are accepted: filesystem import and router-wide
 * administration are excluded because SAF import has its own storage
 * broker and the Android surface must stay a closed typed set. A
 * refused or unknown request is answered with a protocol error, never
 * an exception, and the router must be Running.
 */
JNIEXPORT jstring JNICALL Java_org_openi2pd_router_I2PD_1JNI_torrentCommand (JNIEnv * env, jclass, jbyteArray request)
{
	return JniStringCall (
		env,
		[&] () -> jstring
		{
			if (!request || static_cast<size_t> (env->GetArrayLength (request)) > i2p::tui::MAX_REQUEST_LINE)
				return NewStringChecked (env, "I2PDTUI/1 ERR 4 15\ninvalid request");
			// Standard UTF-8, rather than JNI modified UTF-8, preserves comment
			// supplementary characters and rejects embedded NUL at the parser.
			const auto length = env->GetArrayLength (request);
			std::string line (static_cast<size_t> (length), '\0');
			env->GetByteArrayRegion (request, 0, length, reinterpret_cast<jbyte *> (line.data ()));
			if (env->ExceptionCheck ()) return nullptr;
			auto parsed = i2p::tui::ParseRequest (line);
			// This embedded bridge deliberately excludes filesystem import and
			// all router-wide administration. SAF import needs its own storage
			// broker.
			if (!parsed) return NewStringChecked (env, "I2PDTUI/1 ERR 4 15\ninvalid request");
			switch (parsed->type)
			{
			case i2p::tui::RequestType::Torrents:
			case i2p::tui::RequestType::TorrentTunnels:
			case i2p::tui::RequestType::TorrentDetail:
			case i2p::tui::RequestType::TorrentDht:
			case i2p::tui::RequestType::TorrentSession:
			case i2p::tui::RequestType::TorrentStart:
			case i2p::tui::RequestType::TorrentStop:
			case i2p::tui::RequestType::TorrentPause:
			case i2p::tui::RequestType::TorrentResume:
			case i2p::tui::RequestType::TorrentReannounce:
			case i2p::tui::RequestType::TorrentRecheck:
			case i2p::tui::RequestType::TorrentRemove:
			case i2p::tui::RequestType::TorrentRemoveAll:
			case i2p::tui::RequestType::TorrentStartAll:
			case i2p::tui::RequestType::TorrentStopAll:
			case i2p::tui::RequestType::TorrentReannounceAll:
			case i2p::tui::RequestType::TorrentAddMagnet:
			case i2p::tui::RequestType::TorrentAddTracker:
			case i2p::tui::RequestType::TorrentRemoveTracker:
			case i2p::tui::RequestType::TorrentCreateV1:
			case i2p::tui::RequestType::TorrentCreateCancel:
			case i2p::tui::RequestType::TorrentCreate:
			case i2p::tui::RequestType::TorrentCommentsEnable:
			case i2p::tui::RequestType::TorrentCommentAdd:
			case i2p::tui::RequestType::TorrentCommentHide:
			case i2p::tui::RequestType::TorrentCommentUnhide:
			case i2p::tui::RequestType::TorrentFilePriority:
			case i2p::tui::RequestType::TorrentSetComment:
			case i2p::tui::RequestType::TorrentSetCreatedBy: break;
			default: return NewStringChecked (env, "I2PDTUI/1 ERR 4 15\ninvalid request");
			}
			i2p::android::NativeCallGuard call;
			if (!call.IsRunning ()) return NewStringChecked (env, "I2PDTUI/1 ERR 6 14\nrouter stopped");
			return NewStringChecked (env, i2p::tui::Dispatch (line).response);
		},
		"I2PDTUI/1 ERR 6 14\ncontrol failed");
}

/**
 * Import already-read metainfo bytes into one torrent tunnel.
 *
 * The Java side reads the provider document; only the bytes cross here,
 * never a path or display name. The size is bounded to 8 MiB, a stopped
 * router is refused with the fixed protocol error, and the result is a
 * protocol response.
 */
JNIEXPORT jstring JNICALL Java_org_openi2pd_router_I2PD_1JNI_importTorrent (
	JNIEnv * env, jclass, jint tunnelIndex, jbyteArray metadata)
{
	return JniStringCall (
		env,
		[&] () -> jstring
		{
			constexpr jsize MAX_IMPORT_BYTES = 8 * 1024 * 1024;
			if (tunnelIndex < 0 || !metadata) return NewStringChecked (env, "I2PDTUI/1 ERR 4 15\ninvalid request");
			const auto size = env->GetArrayLength (metadata);
			if (size <= 0 || size > MAX_IMPORT_BYTES)
				return NewStringChecked (env, "I2PDTUI/1 ERR 4 15\ninvalid request");
			std::vector<uint8_t> content (static_cast<size_t> (size));
			env->GetByteArrayRegion (metadata, 0, size, reinterpret_cast<jbyte *> (content.data ()));
			if (env->ExceptionCheck ()) return nullptr;
			i2p::android::NativeCallGuard call;
			if (!call.IsRunning ()) return NewStringChecked (env, "I2PDTUI/1 ERR 6 14\nrouter stopped");
			return NewStringChecked (
				env, i2p::tui::ImportTorrent (static_cast<uint32_t> (tunnelIndex), content).response);
		},
		"I2PDTUI/1 ERR 6 14\ncontrol failed");
}

JNIEXPORT jboolean JNICALL Java_org_openi2pd_router_I2PD_1JNI_getHTTPProxyState (JNIEnv * env, jclass clazz)
{
	return JniScalarCall<jboolean> (
		env,
		[] ()
		{
			i2p::android::NativeCallGuard call;
			return (call.IsRunning () && i2p::client::context.GetHttpProxy ()) ? JNI_TRUE : JNI_FALSE;
		},
		JNI_FALSE);
}

JNIEXPORT jboolean JNICALL Java_org_openi2pd_router_I2PD_1JNI_getSOCKSProxyState (JNIEnv * env, jclass clazz)
{
	return JniScalarCall<jboolean> (
		env,
		[] ()
		{
			i2p::android::NativeCallGuard call;
			return (call.IsRunning () && i2p::client::context.GetSocksProxy ()) ? JNI_TRUE : JNI_FALSE;
		},
		JNI_FALSE);
}

JNIEXPORT jboolean JNICALL Java_org_openi2pd_router_I2PD_1JNI_getBOBState (JNIEnv * env, jclass clazz)
{
	return JniScalarCall<jboolean> (
		env,
		[] ()
		{
			i2p::android::NativeCallGuard call;
			return (call.IsRunning () && i2p::client::context.GetBOBCommandChannel ()) ? JNI_TRUE : JNI_FALSE;
		},
		JNI_FALSE);
}

JNIEXPORT jboolean JNICALL Java_org_openi2pd_router_I2PD_1JNI_getSAMState (JNIEnv * env, jclass clazz)
{
	return JniScalarCall<jboolean> (
		env,
		[] ()
		{
			i2p::android::NativeCallGuard call;
			return (call.IsRunning () && i2p::client::context.GetSAMBridge ()) ? JNI_TRUE : JNI_FALSE;
		},
		JNI_FALSE);
}

JNIEXPORT jboolean JNICALL Java_org_openi2pd_router_I2PD_1JNI_getI2CPState (JNIEnv * env, jclass clazz)
{
	return JniScalarCall<jboolean> (
		env,
		[] ()
		{
			i2p::android::NativeCallGuard call;
			return (call.IsRunning () && i2p::client::context.GetI2CPServer ()) ? JNI_TRUE : JNI_FALSE;
		},
		JNI_FALSE);
}

extern "C" JNIEXPORT jbyteArray JNICALL Java_org_openi2pd_router_I2PD_1JNI_buildTorrentMetadata (JNIEnv * env, jclass,
	jbyteArray name, jobjectArray paths, jlongArray lengths, jint pieceLength, jbyteArray hashes, jobjectArray trackers,
	jboolean privateTorrent)
{
#ifndef NO_TORRENTS
	try
	{
		if (!env || !name || !paths || !lengths || !hashes || !trackers || pieceLength < 16384
			|| pieceLength > 4 * 1024 * 1024 || (pieceLength & (pieceLength - 1)))
			return nullptr;
		const auto count = env->GetArrayLength (paths);
		if (env->ExceptionCheck ())
		{
			ClearJniException (env);
			return nullptr;
		}
		if (count < 1 || count > 10000 || env->GetArrayLength (lengths) != count) return nullptr;
		// Every failure below clears a pending JNI exception before it
		// returns or throws: Java treats a null result as "metadata creation
		// failed", so the entry point must never leave ART with an exception
		// pending.
		auto bytes = [&] (jbyteArray value, size_t maximum) -> std::string
		{
			if (!value) throw std::runtime_error ("missing field");
			const auto size = env->GetArrayLength (value);
			if (env->ExceptionCheck ())
			{
				ClearJniException (env);
				throw std::runtime_error ("JNI length failed");
			}
			if (size < 0 || static_cast<size_t> (size) > maximum) throw std::runtime_error ("oversized field");
			std::string result (size, '\0');
			env->GetByteArrayRegion (value, 0, size, reinterpret_cast<jbyte *> (result.data ()));
			if (env->ExceptionCheck ())
			{
				ClearJniException (env);
				throw std::runtime_error ("JNI read failed");
			}
			return result;
		};
		auto title = bytes (name, 1024);
		std::vector<jlong> sizes (count);
		env->GetLongArrayRegion (lengths, 0, count, sizes.data ());
		if (env->ExceptionCheck ())
		{
			ClearJniException (env);
			return nullptr;
		}
		std::vector<std::pair<std::filesystem::path, size_t>> files;
		uint64_t total = 0;
		for (jsize i = 0; i < count; ++i)
		{
			if (sizes[i] < 0 || static_cast<uint64_t> (sizes[i]) > 64ULL * 1024 * 1024 * 1024 - total) return nullptr;
			total += sizes[i];
			ScopedLocalRef<jbyteArray> path (env, static_cast<jbyteArray> (env->GetObjectArrayElement (paths, i)));
			if (env->ExceptionCheck ())
			{
				ClearJniException (env);
				return nullptr;
			}
			if (!path) return nullptr;
			const std::string text = bytes (path.get (), 8192);
			if (static_cast<uint64_t> (sizes[i]) > std::numeric_limits<size_t>::max ()) return nullptr;
			files.emplace_back (std::filesystem::path (text), static_cast<size_t> (sizes[i]));
		}
		auto rawHashes = bytes (hashes, 5 * 1024 * 1024);
		if (rawHashes.size () % 20) return nullptr;
		std::vector<i2p::torrents::Torrent::InfoHash> pieces (rawHashes.size () / 20);
		for (size_t i = 0; i < pieces.size (); ++i) std::memcpy (pieces[i].data (), rawHashes.data () + i * 20, 20);
		const auto trackerCount = env->GetArrayLength (trackers);
		if (env->ExceptionCheck ())
		{
			ClearJniException (env);
			return nullptr;
		}
		if (trackerCount > 32 || (privateTorrent && !trackerCount)) return nullptr;
		i2p::torrents::Torrent::TrackerTiers tiers;
		for (jsize i = 0; i < trackerCount; ++i)
		{
			ScopedLocalRef<jbyteArray> tracker (
				env, static_cast<jbyteArray> (env->GetObjectArrayElement (trackers, i)));
			if (env->ExceptionCheck ())
			{
				ClearJniException (env);
				return nullptr;
			}
			if (!tracker) return nullptr;
			std::string text = bytes (tracker.get (), 2048);
			i2p::http::URL url (text);
			if (text.empty () || text.find_first_of ("\r\n\t ") != std::string::npos
				|| !i2p::torrents::IsI2PTrackerHost (url.host) || !url.user.empty () || !url.pass.empty () || url.ipv6
				|| (url.schema != "http" && url.schema != "udp"))
				return nullptr;
			tiers.push_back ({ std::move (text) });
		}
		auto torrent
			= i2p::torrents::Torrent::CreateV1 (title, files, pieceLength, pieces, tiers, privateTorrent == JNI_TRUE);
		if (!torrent) return nullptr;
		auto metadata = torrent->CreateTorrentFileContent ();
		if (metadata.size () > 8 * 1024 * 1024) return nullptr;
		ScopedLocalRef<jbyteArray> result (env, env->NewByteArray (static_cast<jsize> (metadata.size ())));
		if (!result)
		{
			// NewByteArray reports OOM through a pending exception. The Java
			// caller treats null as a rejected metadata request, so never leave
			// the VM with an exception pending on that return path.
			ClearJniException (env);
			return nullptr;
		}
		env->SetByteArrayRegion (result.get (), 0, static_cast<jsize> (metadata.size ()),
			reinterpret_cast<const jbyte *> (metadata.data ()));
		if (env->ExceptionCheck ())
		{
			ClearJniException (env);
			return nullptr;
		}
		return result.release ();
	}
	catch (...)
	{
		ClearJniException (env);
		return nullptr;
	}
#else
	return nullptr;
#endif
}

#ifndef NO_TORRENTS
#include "TorrentsTunnel.h"
#include "TorrentPayloadStorage.h"
#include <mutex>
#include <set>
namespace
{
/**
	 * True while JNI may still be used from this process. It is flipped
	 * once by a guard constructed when the SAF bridge is installed, so it
	 * is destroyed before the program-initialized statics that can own
	 * payloads. A payload destructor that runs after that point performs
	 * no JNI work at all.
	 */
std::atomic<bool> safJniLifetimeActive { true };

struct SafJniLifetimeGuard
{
	~SafJniLifetimeGuard () { safJniLifetimeActive.store (false, std::memory_order_release); }
};

/**
	 * One bridge call's JNI environment. `Attach::Never` is the destructor
	 * policy: a destructor must never attach a thread, because it can run
	 * on a native worker or during process teardown. Without an existing
	 * attachment the scope reports "no VM" and the caller falls back to
	 * its error string.
	 */
struct SafEnv
{
	enum class Attach
	{
		IfNeeded,
		Never
	};

	JavaVM * vm;
	JNIEnv * env = nullptr;
	bool attached = false, frame = false, failed = false;
	explicit SafEnv (JavaVM * value, Attach mode = Attach::IfNeeded)
		: vm (value)
	{
		// Document callbacks are a router-process capability only. A process
		// bound to the SAF storage broker, or one that has not claimed a
		// role, never obtains document authority through this helper.
		if (i2p::android::CurrentProcessRole () != i2p::android::ProcessRole::Router)
		{
			failed = true;
			return;
		}
		if (!vm || !safJniLifetimeActive.load (std::memory_order_acquire)) return;
		const jint status = vm->GetEnv (reinterpret_cast<void **> (&env), JNI_VERSION_1_6);
		if (status == JNI_EDETACHED)
		{
			if (mode == Attach::Never) return;
			attached = vm->AttachCurrentThread (&env, nullptr) == JNI_OK;
			if (!attached)
			{
				env = nullptr;
				failed = true;
				return;
			}
		}
		else if (status != JNI_OK)
		{
			env = nullptr;
			failed = true;
			return;
		}
		// Each bridge call is one local-reference scope. PopLocalFrame also
		// releases a reference that a throwing provider made hard to inspect.
		// PushLocalFrame throws OutOfMemoryError on failure; clear it here so
		// a pending exception never leaks into the next JNI call on this
		// thread.
		if (env)
		{
			frame = env->PushLocalFrame (32) == 0;
			if (!frame)
			{
				env->ExceptionClear ();
				failed = true;
			}
		}
	}
	~SafEnv ()
	{
		if (env && frame)
		{
			if (env->ExceptionCheck ()) env->ExceptionClear ();
			env->PopLocalFrame (nullptr);
		}
		if (attached) vm->DetachCurrentThread ();
	}
	bool Good ()
	{
		if (!env || failed) return false;
		if (env->ExceptionCheck ())
		{
			env->ExceptionClear ();
			return false;
		}
		return true;
	}
};

/**
	 * Process-lifetime JNI bridge to the SAF storage service.
	 *
	 * The bridge owns one JNI global class reference that is deliberately
	 * never released: deleting it from a static destructor or an arbitrary
	 * stop thread is not a legal JNI operation, and process exit reclaims
	 * it with the VM. The destructor therefore performs no JNI work at
	 * all.
	 */
struct SafBridge
{
	JavaVM * vm = nullptr;
	jclass type = nullptr;
	jmethodID configured = nullptr, root = nullptr, open = nullptr, read = nullptr, write = nullptr, close = nullptr,
			  release = nullptr, remove = nullptr;
	~SafBridge () = default;
};
class SafUnavailable final : public i2p::torrents::TorrentPayloadStorage
{
public:
	bool UsesFinalNames () const override { return true; }
	uint64_t Open (const std::filesystem::path &, uint64_t, bool, std::string & error) override
	{
		error = "SAF bridge unavailable";
		return 0;
	}
	bool Read (uint64_t, uint64_t, std::span<uint8_t>, std::string & error) override
	{
		error = "SAF bridge unavailable";
		return false;
	}
	bool Write (uint64_t, uint64_t, std::span<const uint8_t>, std::string & error) override
	{
		error = "SAF bridge unavailable";
		return false;
	}
	bool Close (uint64_t, std::string &) override { return false; }
};
class SafPayload;

/**
	 * Live payload backends, owned by the torrent engine. The registry
	 * holds weak references only; it exists so the daemon stop path can
	 * release documents on the attached JNI thread instead of leaving that
	 * work to a destructor that may run on a worker thread or during
	 * teardown.
	 */
struct SafPayloadRegistry
{
	std::mutex lock;
	std::vector<std::weak_ptr<SafPayload>> slots;
};

/** Upper bound: one tracked backend per configured torrent tunnel. */
constexpr size_t MAX_SAF_PAYLOADS = 256;

SafPayloadRegistry & PayloadRegistry ()
{
	static SafPayloadRegistry registry;
	return registry;
}

void RegisterSafPayload (const std::shared_ptr<SafPayload> & payload);

/// Adapts one torrent's payload operations to Java SAF capabilities.
/// The shared bridge keeps JNI method IDs and the class reference
/// alive; `lock` serializes the cached root and handles with reads,
/// writes and detach. The paths passed to Java are relative to the
/// selected tree, not local paths.
class SafPayload final : public i2p::torrents::TorrentPayloadStorage
{
	std::shared_ptr<SafBridge> bridge;
	std::string tunnel, hash;
	jlong rootId = 0;
	std::mutex lock;
	std::set<uint64_t> handles;
	void DropHandleLocked (SafEnv & e, uint64_t handle)
	{
		if (e.env && handle && handle <= static_cast<uint64_t> (INT64_MAX))
			e.env->CallStaticVoidMethod (bridge->type, bridge->close, static_cast<jlong> (handle));
		handles.erase (handle);
		e.Good ();
	}
	void CloseHandlesLocked (SafEnv & e)
	{
		while (!handles.empty ()) DropHandleLocked (e, *handles.begin ());
	}
	bool ResolveRootLocked (SafEnv & e, std::string & error)
	{
		if (!e.env || e.failed)
		{
			error = "SAF VM unavailable";
			return false;
		}
		auto t = NewStringChecked (e.env, tunnel), h = NewStringChecked (e.env, hash);
		jlong resolved = 0;
		if (t && h) resolved = e.env->CallStaticLongMethod (bridge->type, bridge->root, t, h);
		const bool ok = e.Good ();
		if (t) e.env->DeleteLocalRef (t);
		if (h) e.env->DeleteLocalRef (h);
		if (!ok || resolved <= 0)
		{
			error = "SAF root unavailable or permission revoked";
			return false;
		}
		if (resolved != rootId)
		{
			// A new selection/generation must not reuse documents from the
			// released tree. Close every cached capability before switching.
			CloseHandlesLocked (e);
			if (rootId) e.env->CallStaticVoidMethod (bridge->type, bridge->release, rootId);
			e.Good ();
			rootId = resolved;
		}
		return true;
	}

public:
	SafPayload (std::shared_ptr<SafBridge> b, std::string t, std::string h)
		: bridge (std::move (b))
		, tunnel (std::move (t))
		, hash (std::move (h))
	{
	}
	~SafPayload () override { ReleaseWithoutAttach (); }

	/**
	 * Best-effort document release that never attaches a thread and never
	 * pushes a local frame: a destructor may run on a native torrent/disk
	 * worker thread or while the process tears down its statics, where a
	 * thread attach/detach pair is not a legal operation. When the
	 * destroying thread is not already attached to a live VM this returns
	 * immediately and the provider reclaims the document when the broker
	 * or the process ends. The daemon calls Detach() through
	 * DetachSafPayloads() on the attached JNI thread after Daemon.stop(),
	 * so this is only the fallback for a torrent that disappears while
	 * running.
	 */
	void ReleaseWithoutAttach () noexcept
	{
		try
		{
			if (!bridge || !bridge->vm) return;
			if (!safJniLifetimeActive.load (std::memory_order_acquire)) return;
			std::lock_guard guard (lock);
			if (!rootId && handles.empty ()) return;
			JNIEnv * env = nullptr;
			if (bridge->vm->GetEnv (reinterpret_cast<void **> (&env), JNI_VERSION_1_6) != JNI_OK || !env) return;
			while (!handles.empty ())
			{
				const uint64_t handle = *handles.begin ();
				handles.erase (handle);
				if (handle <= static_cast<uint64_t> (INT64_MAX) && bridge->type && bridge->close)
					env->CallStaticVoidMethod (bridge->type, bridge->close, static_cast<jlong> (handle));
				if (env->ExceptionCheck ()) env->ExceptionClear ();
			}
			if (rootId)
			{
				const jlong released = rootId;
				rootId = 0;
				if (bridge->type && bridge->release)
					env->CallStaticVoidMethod (bridge->type, bridge->release, released);
				if (env->ExceptionCheck ()) env->ExceptionClear ();
			}
		}
		catch (...)
		{
			// A destructor path never propagates: a failed cleanup only leaves
			// the provider to reclaim the document when the process ends.
		}
	}

	/**
	 * Release every cached document handle and the tree capability. Called
	 * while the daemon is stopping and this thread is attached to the VM,
	 * so the objects can be dropped without JNI work in a destructor. It
	 * is idempotent and keeps working afterwards: a later Open()
	 * re-resolves the root and reopens the document.
	 */
	void Detach () noexcept
	{
		try
		{
			std::lock_guard guard (lock);
			if (!bridge || !bridge->vm) return;
			SafEnv e (bridge->vm, SafEnv::Attach::Never);
			if (!e.env || e.failed) return;
			CloseHandlesLocked (e);
			if (rootId)
			{
				if (bridge->type && bridge->release)
					e.env->CallStaticVoidMethod (bridge->type, bridge->release, rootId);
				e.Good ();
				rootId = 0;
			}
		}
		catch (...)
		{
		}
	}
	bool UsesFinalNames () const override { return true; }
	uint64_t Open (const std::filesystem::path & path, uint64_t length, bool create, std::string & error) override
	{
		std::lock_guard guard (lock);
		SafEnv e (bridge->vm);
		if (!e.env || e.failed || length > INT64_MAX)
		{
			error = "SAF VM unavailable or length overflow";
			return 0;
		}
		if (!ResolveRootLocked (e, error)) return 0;
		auto p = NewStringChecked (e.env, path.generic_string ());
		jlong id = 0;
		if (e.Good () && rootId && p)
			id = e.env->CallStaticLongMethod (
				bridge->type, bridge->open, rootId, p, static_cast<jlong> (length), create ? JNI_TRUE : JNI_FALSE);
		if (p) e.env->DeleteLocalRef (p);
		if (!e.Good () || id <= 0)
		{
			error = "SAF document unavailable, permission revoked or seek unsupported";
			return 0;
		}
		handles.insert (static_cast<uint64_t> (id));
		return static_cast<uint64_t> (id);
	}
	bool Read (uint64_t handle, uint64_t offset, std::span<uint8_t> bytes, std::string & error) override
	{
		std::lock_guard guard (lock);
		SafEnv e (bridge->vm);
		if (!e.env || e.failed || !Valid (handle, offset, bytes.size ()))
		{
			error = "Invalid SAF read";
			return false;
		}
		auto result = static_cast<jbyteArray> (e.env->CallStaticObjectMethod (bridge->type, bridge->read,
			static_cast<jlong> (handle), static_cast<jlong> (offset), static_cast<jint> (bytes.size ())));
		if (!e.Good ())
		{
			// The return value is undefined when the callee threw; do not call
			// GetArrayLength/DeleteLocalRef with a pending exception.
			error = "SAF read failed or document changed";
			DropHandleLocked (e, handle);
			return false;
		}
		bool ok = result && e.env->GetArrayLength (result) == static_cast<jsize> (bytes.size ());
		if (ok)
			e.env->GetByteArrayRegion (
				result, 0, static_cast<jsize> (bytes.size ()), reinterpret_cast<jbyte *> (bytes.data ()));
		if (result) e.env->DeleteLocalRef (result);
		ok = e.Good () && ok;
		if (!ok)
		{
			error = "SAF read failed or document changed";
			DropHandleLocked (e, handle);
		}
		return ok;
	}
	bool Write (uint64_t handle, uint64_t offset, std::span<const uint8_t> bytes, std::string & error) override
	{
		std::lock_guard guard (lock);
		SafEnv e (bridge->vm);
		if (!e.env || e.failed || !Valid (handle, offset, bytes.size ()))
		{
			error = "Invalid SAF write";
			return false;
		}
		auto data = e.env->NewByteArray (static_cast<jsize> (bytes.size ()));
		bool ok = false;
		if (data)
		{
			e.env->SetByteArrayRegion (
				data, 0, static_cast<jsize> (bytes.size ()), reinterpret_cast<const jbyte *> (bytes.data ()));
			if (e.Good ())
				ok = e.env->CallStaticBooleanMethod (
						 bridge->type, bridge->write, static_cast<jlong> (handle), static_cast<jlong> (offset), data)
					== JNI_TRUE;
			e.env->DeleteLocalRef (data);
		}
		ok = e.Good () && ok;
		if (!ok)
		{
			error = "SAF write failed, permission revoked or storage full";
			DropHandleLocked (e, handle);
		}
		return ok;
	}
	bool Remove (const std::filesystem::path & path, uint64_t length, std::string & error) override
	{
		std::lock_guard guard (lock);
		SafEnv e (bridge->vm);
		if (!e.env || e.failed || length > INT64_MAX)
		{
			error = "SAF deletion capability unavailable";
			return false;
		}
		if (!ResolveRootLocked (e, error)) return false;
		auto p = NewStringChecked (e.env, path.generic_string ());
		bool ok = false;
		if (p && e.Good ())
			ok = e.env->CallStaticBooleanMethod (bridge->type, bridge->remove, rootId, p, static_cast<jlong> (length))
				== JNI_TRUE;
		if (p) e.env->DeleteLocalRef (p);
		ok = e.Good () && ok;
		if (!ok) error = "SAF document deletion failed or permission revoked";
		return ok;
	}
	bool Close (uint64_t handle, std::string & error) override
	{
		std::lock_guard guard (lock);
		SafEnv e (bridge->vm);
		if (!e.env || e.failed || !handle || handle > INT64_MAX) return false;
		e.env->CallStaticVoidMethod (bridge->type, bridge->close, static_cast<jlong> (handle));
		handles.erase (handle);
		bool ok = e.Good ();
		if (!ok) error = "SAF close failed";
		return ok;
	}

private:
	static bool Valid (uint64_t handle, uint64_t offset, size_t count)
	{
		return handle && handle <= INT64_MAX && offset <= INT64_MAX && count <= MaxTransfer
			&& count <= INT64_MAX - offset;
	}
};

void RegisterSafPayload (const std::shared_ptr<SafPayload> & payload)
{
	if (!payload) return;
	auto & registry = PayloadRegistry ();
	std::lock_guard guard (registry.lock);
	// Reclaim slots whose backend is already gone, then respect the bound.
	// An untracked payload still works; it only loses the explicit detach
	// hint and falls back to its best-effort destructor.
	std::erase_if (registry.slots, [] (const auto & slot) { return slot.expired (); });
	if (registry.slots.size () >= MAX_SAF_PAYLOADS) return;
	registry.slots.push_back (payload);
}

/**
	 * Release every live payload's document handles and tree capability.
	 * Runs on the JNI thread that is already attached to the VM; it never
	 * attaches one and never runs under the registry lock while calling a
	 * provider.
	 */
void DetachSafPayloads () noexcept
{
	try
	{
		std::vector<std::shared_ptr<SafPayload>> live;
		{
			auto & registry = PayloadRegistry ();
			std::lock_guard guard (registry.lock);
			live.reserve (registry.slots.size ());
			for (auto & slot : registry.slots)
				if (auto payload = slot.lock ()) live.push_back (std::move (payload));
			std::erase_if (registry.slots, [] (const auto & slot) { return slot.expired (); });
		}
		for (auto & payload : live) payload->Detach ();
	}
	catch (...)
	{
	}
}
}
#else
namespace
{
/** The torrent engine is absent, so no SAF payload can exist. */
void DetachSafPayloads () noexcept { }
}
#endif
extern "C" JNIEXPORT jboolean JNICALL Java_org_openi2pd_router_I2PD_1JNI_installSafStorageBridge (
	JNIEnv * env, jclass, jclass type)
{
#ifndef NO_TORRENTS
	try
	{
		// Document authority is a router-process capability. A process bound
		// to the SAF storage broker must never install a bridge (it would
		// then be able to drive document IO from the process that is supposed
		// to expose no router state) and an unclaimed process must first
		// claim Router.
		if (!i2p::android::HasProcessRole (i2p::android::ProcessRole::Router)) return JNI_FALSE;
		// Construct the JNI lifetime guard late (once), so it is destroyed
		// before program-initialized statics that may own payload backends.
		static SafJniLifetimeGuard lifetimeGuard;
		(void)lifetimeGuard;
		// Install denial before any JNI allocation/lookup. Failed setup
		// cannot leave a selected document torrent on the native filesystem
		// backend.
		i2p::torrents::SetTorrentPayloadStorageFactory (
			[] (const std::string &, const std::string &) { return std::make_shared<SafUnavailable> (); });
		auto fail = [] () -> jboolean
		{
			// No bridge means no SAF authority. Restore the ordinary native
			// backend; DaemonWrapper refuses startup if a SAF grant exists.
			i2p::torrents::SetTorrentPayloadStorageFactory ({ });
			return JNI_FALSE;
		};
		// Reclaim document handles from a previous incarnation before
		// replacing the backend on a restart.
		DetachSafPayloads ();
		// The bridge is a deliberate process-lifetime object: its destructor
		// performs no JNI work, so one bridge is reused across daemon
		// restarts. Creating a new global class reference on every start
		// leaked one ref per restart. A class identity change (in-place
		// update/test) creates a second bridge; the old holder stays alive
		// for its payloads.
		static std::mutex bridgeMutex;
		static std::shared_ptr<SafBridge> processBridge;
		JavaVM * vm = nullptr;
		if (!env || env->GetJavaVM (&vm) != JNI_OK || !type) return fail ();
		std::shared_ptr<SafBridge> bridge;
		{
			std::lock_guard<std::mutex> lock (bridgeMutex);
			if (processBridge && processBridge->vm == vm && processBridge->type
				&& env->IsSameObject (processBridge->type, type))
				bridge = processBridge;
		}
		if (!bridge)
		{
			auto candidate = std::make_shared<SafBridge> ();
			candidate->vm = vm;
			candidate->type = static_cast<jclass> (env->NewGlobalRef (type));
			if (!candidate->type)
			{
				if (env->ExceptionCheck ()) env->ExceptionClear ();
				return fail ();
			}
			auto method = [&] (const char * name, const char * signature) -> jmethodID
			{ return env->ExceptionCheck () ? nullptr : env->GetStaticMethodID (type, name, signature); };
			candidate->configured = method ("configured", "(Ljava/lang/String;Ljava/lang/String;)Z");
			candidate->root = method ("root", "(Ljava/lang/String;Ljava/lang/String;)J");
			candidate->open = method ("open", "(JLjava/lang/String;JZ)J");
			candidate->read = method ("read", "(JJI)[B");
			candidate->write = method ("write", "(JJ[B)Z");
			candidate->remove = method ("remove", "(JLjava/lang/String;J)Z");
			candidate->close = method ("close", "(J)V");
			candidate->release = method ("release", "(J)V");
			if (env->ExceptionCheck () || !candidate->configured || !candidate->root || !candidate->open
				|| !candidate->read || !candidate->write || !candidate->close || !candidate->release
				|| !candidate->remove)
			{
				env->ExceptionClear ();
				env->DeleteGlobalRef (candidate->type);
				candidate->type = nullptr;
				return fail ();
			}
			std::lock_guard<std::mutex> lock (bridgeMutex);
			if (processBridge && processBridge->vm == vm && processBridge->type
				&& env->IsSameObject (processBridge->type, type))
			{
				// A concurrent install won the race. Keep its bridge; the candidate
				// holds no factory yet and its global ref is freed.
				env->DeleteGlobalRef (candidate->type);
				candidate->type = nullptr;
				bridge = processBridge;
			}
			else
			{
				processBridge = candidate;
				bridge = candidate;
			}
		}
		i2p::torrents::SetTorrentPayloadStorageFactory (
			[bridge] (const std::string & tunnel,
				const std::string & hash) -> std::shared_ptr<i2p::torrents::TorrentPayloadStorage>
			{
				SafEnv e (bridge->vm);
				if (!e.env || e.failed)
				{
					// No VM, no role, or an unusable local frame: fail closed with an
					// explicit unavailable backend instead of handing back a payload
					// that can never open a document.
					return std::make_shared<SafUnavailable> ();
				}
				auto t = NewStringChecked (e.env, tunnel), h = NewStringChecked (e.env, hash);
				const bool strings = t && h;
				bool configured = false;
				if (strings)
					configured = e.env->CallStaticBooleanMethod (bridge->type, bridge->configured, t, h) == JNI_TRUE;
				const bool good = e.Good ();
				if (t) e.env->DeleteLocalRef (t);
				if (h) e.env->DeleteLocalRef (h);
				if (!good || !strings)
				{
					// The configured() query failed (provider exception or OOM): the
					// grant state is unknown, so do not create a backend that would
					// write through a half-trusted path.
					return std::make_shared<SafUnavailable> ();
				}
				// configured() == false is a definite "this torrent is not backed
				// by a selected document tree"; the engine keeps its native storage
				// for it.
				if (!configured) return nullptr;
				auto payload = std::make_shared<SafPayload> (bridge, tunnel, hash);
				RegisterSafPayload (payload);
				return payload;
			});
		return JNI_TRUE;
	}
	catch (...)
	{
		ClearJniException (env);
		i2p::torrents::SetTorrentPayloadStorageFactory ({ });
		return JNI_FALSE;
	}
#else
	// The torrent engine is absent; claiming a usable SAF bridge would
	// make Java believe payload storage exists. Report failure instead.
	return JNI_FALSE;
#endif
}

#ifdef __ANDROID__
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <linux/audit.h>
#include <dirent.h>
#include <sys/socket.h>
#include <cstdlib>
#include <climits>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <cerrno>
#include <cstddef>
#endif
extern "C" JNIEXPORT jboolean JNICALL Java_org_openi2pd_router_I2PD_1JNI_restrictSafStorageProcess (
	JNIEnv * env, jclass)
{
	// This is the storage broker's first and only router-capable native
	// call. Claim the broker role here, before the sandbox, so a process
	// that already claimed Router (for example a router that was asked to
	// restrict itself) fails closed instead of being sandboxed as a
	// broker: the Java caller stops the service when this returns false.
	// The claim is irreversible.
	if (!i2p::android::ClaimProcessRole (i2p::android::ProcessRole::StorageBroker))
	{
		ClearJniException (env);
		return JNI_FALSE;
	}
#ifdef __ANDROID__
	// Applied TSYNC before publishing Binder. Binder document-provider IPC
	// is retained; creating or using external sockets and
	// privilege-recovery APIs is denied for every current and future
	// thread. No router runs here. Zygote/ART infrastructure may retain
	// Unix sockets. Only close inherited external-network sockets; do not
	// disturb Binder or local runtime FDs.
	DIR * descriptors = opendir ("/proc/self/fd");
	if (!descriptors) return JNI_FALSE;
	while (auto * entry = readdir (descriptors))
	{
		char * end = nullptr;
		long value = strtol (entry->d_name, &end, 10);
		if (!end || *end || value < 0 || value > INT_MAX || value == dirfd (descriptors)) continue;
		sockaddr_storage address { };
		socklen_t size = sizeof (address);
		if (!getsockname (static_cast<int> (value), reinterpret_cast<sockaddr *> (&address), &size)
			&& (address.ss_family == AF_INET || address.ss_family == AF_INET6))
			close (static_cast<int> (value));
	}
	closedir (descriptors);
	const sock_filter filter[] = { BPF_STMT (BPF_LD | BPF_W | BPF_ABS, offsetof (seccomp_data, arch)),
#if defined(__aarch64__)
		BPF_JUMP (BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_AARCH64, 1, 0),
#elif defined(__arm__)
		BPF_JUMP (BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_ARM, 1, 0),
#elif defined(__x86_64__)
		BPF_JUMP (BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_X86_64, 1, 0),
#elif defined(__i386__)
		BPF_JUMP (BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_I386, 1, 0),
#else
#error Unsupported Android seccomp architecture
#endif
		BPF_STMT (BPF_RET | BPF_K, SECCOMP_RET_KILL), BPF_STMT (BPF_LD | BPF_W | BPF_ABS, offsetof (seccomp_data, nr)),
#if defined(__x86_64__)
		BPF_JUMP (BPF_JMP | BPF_JSET | BPF_K, 0x40000000, 0, 1), BPF_STMT (BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM),
#endif
#define SAF_DENY(number)                                                                                               \
	BPF_JUMP (BPF_JMP | BPF_JEQ | BPF_K, number, 0, 1), BPF_STMT (BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM)
#ifdef __NR_socketcall
		SAF_DENY (__NR_socketcall),
#endif
		SAF_DENY (__NR_socket), SAF_DENY (__NR_connect), SAF_DENY (__NR_sendto), SAF_DENY (__NR_sendmsg),
		SAF_DENY (__NR_ptrace),
#ifdef __NR_sendmmsg
		SAF_DENY (__NR_sendmmsg),
#endif
#ifdef __NR_io_uring_setup
		SAF_DENY (__NR_io_uring_setup),
#endif
#ifdef __NR_io_uring_enter
		SAF_DENY (__NR_io_uring_enter),
#endif
#ifdef __NR_io_uring_register
		SAF_DENY (__NR_io_uring_register),
#endif
#ifdef __NR_open_by_handle_at
		SAF_DENY (__NR_open_by_handle_at),
#endif
#ifdef __NR_process_vm_readv
		SAF_DENY (__NR_process_vm_readv),
#endif
#ifdef __NR_process_vm_writev
		SAF_DENY (__NR_process_vm_writev),
#endif
#ifdef __NR_bpf
		SAF_DENY (__NR_bpf),
#endif
#ifdef __NR_perf_event_open
		SAF_DENY (__NR_perf_event_open),
#endif
#ifdef __NR_userfaultfd
		SAF_DENY (__NR_userfaultfd),
#endif
#ifdef __NR_kcmp
		SAF_DENY (__NR_kcmp),
#endif
#ifdef __NR_pidfd_getfd
		SAF_DENY (__NR_pidfd_getfd),
#endif
		BPF_STMT (BPF_RET | BPF_K, SECCOMP_RET_ALLOW)
#undef SAF_DENY
	};
	sock_fprog program { static_cast<unsigned short> (sizeof (filter) / sizeof (filter[0])),
		const_cast<sock_filter *> (filter) };
	if (prctl (PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)) return JNI_FALSE;
	return syscall (__NR_seccomp, SECCOMP_SET_MODE_FILTER, SECCOMP_FILTER_FLAG_TSYNC, &program) == 0 ? JNI_TRUE
																									 : JNI_FALSE;
#else
	return JNI_FALSE;
#endif
}
