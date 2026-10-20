/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: i2pd android.
 *
 * JNI entry points for daemon control, bounded status and log snapshots,
 * transport and client-service probes, and the byte-array torrent commands.
 * UTF-8 is converted to UTF-16 explicitly instead of using NewStringUTF,
 * pending JNI exceptions are cleared and turned into fallbacks, and every
 * returned string is bounded.
 *
 */

#include <jni.h>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <cstring>
#include <stdexcept>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "org_openi2pd_router_I2PD_JNI.h"
#include "DaemonAndroid.h"
#include "Config.h"
#include "RouterContext.h"
#include "ClientContext.h"
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
/** RAII wrapper for GetStringUTFChars/ReleaseStringUTFChars. */
class ScopedUtfChars
{
public:
	ScopedUtfChars (JNIEnv * env, jstring text)
		: m_Env (env)
		, m_Text (text)
	{
		if (m_Text) m_Chars = m_Env->GetStringUTFChars (m_Text, nullptr);
	}
	~ScopedUtfChars ()
	{
		if (m_Chars) m_Env->ReleaseStringUTFChars (m_Text, m_Chars);
	}
	ScopedUtfChars (const ScopedUtfChars &) = delete;
	ScopedUtfChars & operator= (const ScopedUtfChars &) = delete;
	const char * c_str () const { return m_Chars; }
	explicit operator bool () const { return m_Chars != nullptr; }

private:
	JNIEnv * m_Env;
	jstring m_Text;
	const char * m_Chars = nullptr;
};

/**
	 * Convert ordinary UTF-8 to UTF-16 and call JNI NewString. NewStringUTF
	 * expects modified UTF-8 (CESU-8 plus 0xC0 0x80 for NUL), which is not the
	 * encoding used by C++ log/config/error strings. Decoding here preserves
	 * embedded NUL, supplementary characters and arbitrary log text, and
	 * replaces malformed sequences with U+FFFD instead of failing the call.
	 *
	 * A VM allocation failure or a native conversion failure clears the
	 * pending JNI exception and returns nullptr; Java must treat a null result
	 * as "no snapshot available". The JNI boundary below retries with a small
	 * fixed fallback, but under a permanent OOM that fallback can also be
	 * null: no C++ code can manufacture a Java string without allocating one.
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
		jstring result = env->NewString (utf16.empty () ? nullptr : utf16.data (), static_cast<jsize> (utf16.size ()));
		if (!result && env->ExceptionCheck ()) env->ExceptionClear ();
		return result;
	}
	catch (...)
	{
		return nullptr;
	}
}

/**
	 * Drop a pending java.lang.OutOfMemoryError or other JNI exception before
	 * returning a fallback. Returning to ART with an exception pending while
	 * also returning a value would be a JNI protocol violation.
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
	 * result from the callback (for example NewString OOM) is retried with the
	 * bounded fallback text. If even that allocation fails the return is null;
	 * Java treats that as "unavailable" rather than as a valid snapshot.
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
		return callback ();
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
	 * Exact, case-sensitive closed set of runtime log level names accepted by
	 * the in-process logger. Log::SetLogLevel() lowercases its argument and
	 * only logs an unknown name, so validating here keeps an invalid value from
	 * reaching native state at all and lets Java report a definite rejection.
	 */
inline bool IsRuntimeLogLevelName (std::string_view level) noexcept
{
	return level == "none" || level == "critical" || level == "error" || level == "warn"
		|| level == "info" || level == "debug";
}

/**
	 * Fixed, bounded snapshot returned when collecting the real snapshot
	 * throws or while the router is not running. It has the same keys and
	 * ordering as a normal snapshot so that Java can parse it with the same
	 * code.
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
	 * Snapshot published while stop() has set DaemonLifecycle::Stopping but
	 * the native-call lock is still held by the stop itself. A poll can read
	 * this without waiting for the router to finish tearing down and without
	 * touching RouterContext.
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

constexpr const char * STOPPED_SUMMARY = "Status: Stopped | Uptime: 0 s | Peers: 0 | Routers: 0 | "
										 "Tunnels in/out/transit: 0/0/0 | Reseeding: no";

constexpr const char * STOPPING_SUMMARY = "Status: Stopping | Uptime: 0 s | Peers: 0 | Routers: 0 | "
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
	 * Immutable copy of one address. RouterInfo::GetAddresses() returns the
	 * live local addresses; the Address objects and the array can be replaced
	 * concurrently under RouterContext::m_RouterInfoMutex. Copying the fields
	 * while that lock is held (through CopyRouterInfoBuffer and a private
	 * RouterInfo parse) keeps JNI from iterating live state.
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
	 * Build an immutable address list from the same signed RouterInfo buffer
	 * the router publishes. The buffer copy is taken under the RouterInfo
	 * mutex by RouterContext::CopyRouterInfoBuffer(); parsing it into a local
	 * RouterInfo means no reference into the router's live Address objects is
	 * retained after this function returns.
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
		item.host = address->host.to_string ();
		item.port = address->port;
		item.published = address->published;
		result.push_back (std::move (item));
	}
	return result;
}

/**
	 * Append one address record as transport|host|port|published. IPv6 hosts
	 * are enclosed in brackets so the value stays unambiguous. Unspecified
	 * hosts (0.0.0.0 / ::) are omitted: they are local bindings, not observed
	 * addresses.
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
	 * Copy recent log records. The text field is the bounded, sanitized copy
	 * made by Log::Append. Each record is sequence|timestampSeconds|
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

JNIEXPORT jstring JNICALL Java_org_openi2pd_router_I2PD_1JNI_startDaemon (JNIEnv * env, jclass clazz)
{
	return JniStringCall (
		env,
		[&] ()
		{
			i2p::android::NativeCallGuard call;
			return NewStringChecked (env, i2p::android::start ());
		},
		"Daemon start failed");
}

JNIEXPORT jstring JNICALL Java_org_openi2pd_router_I2PD_1JNI_getDataDir (JNIEnv * env, jclass clazz)
{
	return JniStringCall (
		env,
		[&] ()
		{
			// Only reads the stateMutex-protected string. It deliberately
			// does not take the native-call lock: the settings editor may ask
			// for it on the UI thread while startDaemon() is waiting for the
			// packaged assets.
			return NewStringChecked (env, i2p::android::GetDataDir ());
		},
		"");
}

JNIEXPORT void JNICALL Java_org_openi2pd_router_I2PD_1JNI_stopDaemon (JNIEnv * env, jclass clazz)
{
	JniVoidCall (env,
		[] ()
		{
			// The guard serialises this against start/status. Java is expected
			// to publish its own "stopping" state and to call this only from
			// the RouterService worker; the native lifecycle mirror cannot
			// enforce the Java thread.
			i2p::android::NativeCallGuard call;
			i2p::android::stop ();
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
	 * Ask the running netDb to reseed from the configured reseed servers.
	 *
	 * NetDb::Reseed() is single-flight and synchronous: it performs the
	 * certificate load and the SU3 bootstrap on the calling thread, which can
	 * take minutes on a poor connection. Java must therefore call this only
	 * from the RouterService worker, never from the Android main thread. A
	 * request while one is already active returns false without starting a
	 * second bootstrap, and no URL, path or option is accepted from Java.
	 */
JNIEXPORT jboolean JNICALL Java_org_openi2pd_router_I2PD_1JNI_requestReseed (JNIEnv * env, jclass clazz)
{
	return JniScalarCall<jboolean> (
		env,
		[] () -> jboolean
		{
			i2p::android::NativeCallGuard call;
			if (!call.IsRunning ()) return JNI_FALSE;
			if (i2p::data::netdb.IsReseeding ()) return JNI_FALSE;
			i2p::data::netdb.Reseed ();
			return JNI_TRUE;
		},
		JNI_FALSE);
}

/**
	 * Apply one runtime log level to the running logger.
	 *
	 * Only the exact closed set IsRuntimeLogLevelName() accepts is forwarded;
	 * the value is validated before the native-call lock is taken so an
	 * unknown name cannot change any router state. Nothing is written to
	 * i2pd.conf and no restart is required.
	 */
JNIEXPORT jboolean JNICALL Java_org_openi2pd_router_I2PD_1JNI_setRuntimeLogLevel (
	JNIEnv * env, jclass clazz, jstring jlevel)
{
	return JniScalarCall<jboolean> (
		env,
		[&] () -> jboolean
		{
			ScopedUtfChars level (env, jlevel);
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

JNIEXPORT void JNICALL Java_org_openi2pd_router_I2PD_1JNI_onNetworkStateChanged (
	JNIEnv * env, jclass clazz, jboolean isConnected)
{
	JniVoidCall (env,
		[isConnected] ()
		{
			// Java invokes this from the ConnectivityManager callback, not
			// from the RouterService worker. The native guard is the
			// defensive boundary: it serialises the update against start/stop
			// and ignores it once the router is no longer Running.
			i2p::android::NativeCallGuard call;
			if (!call.IsRunning ()) return;
			i2p::transport::transports.SetOnline (isConnected != JNI_FALSE);
		});
}

JNIEXPORT void JNICALL Java_org_openi2pd_router_I2PD_1JNI_setDataDir (JNIEnv * env, jclass clazz, jstring jdataDir)
{
	JniVoidCall (env,
		[&] ()
		{
			// State-only update (see getDataDir). It must not wait behind a
			// multi-second start while the Java caller prepares the daemon.
			ScopedUtfChars dataDir (env, jdataDir);
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
			// status must be observable even though the native-call lock is
			// held by the teardown.
			if (i2p::android::GetDaemonLifecycle () == i2p::android::DaemonLifecycle::Stopping)
				return NewStringChecked (env, STOPPING_SUMMARY);

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
			if (i2p::android::GetDaemonLifecycle () == i2p::android::DaemonLifecycle::Stopping)
				return NewStringChecked (env, STOPPING_SNAPSHOT);

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

JNIEXPORT jstring JNICALL Java_org_openi2pd_router_I2PD_1JNI_torrentCommand (JNIEnv * env, jclass, jbyteArray request)
{
	return JniStringCall (
		env,
		[&] () -> jstring
		{
			if (!request || env->GetArrayLength (request) > i2p::tui::MAX_REQUEST_LINE)
				return NewStringChecked (env, "I2PDTUI/1 ERR 4 15\ninvalid request");
			// Standard UTF-8, rather than JNI modified UTF-8, preserves comment
			// supplementary characters and rejects embedded NUL at the parser.
			const auto length = env->GetArrayLength (request);
			std::string line (static_cast<size_t> (length), '\0');
			env->GetByteArrayRegion (request, 0, length, reinterpret_cast<jbyte *> (line.data ()));
			if (env->ExceptionCheck ()) return nullptr;
			auto parsed = i2p::tui::ParseRequest (line);
			// This embedded bridge deliberately excludes filesystem import and all
			// router-wide administration. SAF import needs its own storage broker.
			if (!parsed) return NewStringChecked (env, "I2PDTUI/1 ERR 4 15\ninvalid request");
			switch (parsed->type)
			{
			case i2p::tui::RequestType::Torrents:
			case i2p::tui::RequestType::TorrentTunnels:
			case i2p::tui::RequestType::TorrentDetail:
			case i2p::tui::RequestType::TorrentDht:
			case i2p::tui::RequestType::TorrentSession:
			case i2p::tui::RequestType::TorrentPause:
			case i2p::tui::RequestType::TorrentResume:
			case i2p::tui::RequestType::TorrentRemove:
			case i2p::tui::RequestType::TorrentAddMagnet:
			case i2p::tui::RequestType::TorrentAddTracker:
			case i2p::tui::RequestType::TorrentRemoveTracker:
			case i2p::tui::RequestType::TorrentCreateV1:
			case i2p::tui::RequestType::TorrentCreateCancel:
			case i2p::tui::RequestType::TorrentCreate:
			case i2p::tui::RequestType::TorrentCommentsEnable:
			case i2p::tui::RequestType::TorrentCommentAdd:
			case i2p::tui::RequestType::TorrentCommentHide:
			case i2p::tui::RequestType::TorrentFilePriority: break;
			default: return NewStringChecked (env, "I2PDTUI/1 ERR 4 15\ninvalid request");
			}
			i2p::android::NativeCallGuard call;
			if (!call.IsRunning ()) return NewStringChecked (env, "I2PDTUI/1 ERR 6 14\nrouter stopped");
			return NewStringChecked (env, i2p::tui::Dispatch (line).response);
		},
		"I2PDTUI/1 ERR 6 14\ncontrol failed");
}

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

extern "C" JNIEXPORT jbyteArray JNICALL Java_org_openi2pd_router_I2PD_1JNI_buildTorrentMetadata (
    JNIEnv * env, jclass, jbyteArray name, jobjectArray paths, jlongArray lengths,
    jint pieceLength, jbyteArray hashes, jobjectArray trackers, jboolean privateTorrent)
{
#ifndef NO_TORRENTS
    try
    {
        if (!name || !paths || !lengths || !hashes || !trackers || pieceLength < 16384 || pieceLength > 4 * 1024 * 1024
            || (pieceLength & (pieceLength - 1))) return nullptr;
        const auto count = env->GetArrayLength (paths);
        if (count < 1 || count > 10000 || env->GetArrayLength (lengths) != count) return nullptr;
        auto bytes = [&] (jbyteArray value, size_t maximum) -> std::string
        {
            if (!value) throw std::runtime_error ("missing field");
            const auto size = env->GetArrayLength (value);
            if (size < 0 || static_cast<size_t> (size) > maximum) throw std::runtime_error ("oversized field");
            std::string result (size, '\0');
            env->GetByteArrayRegion (value, 0, size, reinterpret_cast<jbyte *> (result.data ()));
            if (env->ExceptionCheck ()) throw std::runtime_error ("JNI read failed");
            return result;
        };
        auto title = bytes (name, 1024);
        std::vector<jlong> sizes (count);
        env->GetLongArrayRegion (lengths, 0, count, sizes.data ());
        if (env->ExceptionCheck ()) return nullptr;
        std::vector<std::pair<std::filesystem::path, size_t>> files;
        uint64_t total = 0;
        for (jsize i = 0; i < count; ++i)
        {
            if (sizes[i] < 0 || static_cast<uint64_t> (sizes[i]) > 64ULL * 1024 * 1024 * 1024 - total)
                return nullptr;
            total += sizes[i];
            auto path = static_cast<jbyteArray> (env->GetObjectArrayElement (paths, i));
            if (env->ExceptionCheck ()) return nullptr;
            std::string text;
            try { text = bytes (path, 8192); }
            catch (...) { if (path) env->DeleteLocalRef (path); throw; }
            env->DeleteLocalRef (path);
            if (static_cast<uint64_t> (sizes[i]) > std::numeric_limits<size_t>::max ()) return nullptr;
            files.emplace_back (std::filesystem::u8path (text), static_cast<size_t> (sizes[i]));
        }
        auto rawHashes = bytes (hashes, 5 * 1024 * 1024);
        if (rawHashes.size () % 20) return nullptr;
        std::vector<i2p::torrents::Torrent::InfoHash> pieces (rawHashes.size () / 20);
        for (size_t i = 0; i < pieces.size (); ++i)
            std::memcpy (pieces[i].data (), rawHashes.data () + i * 20, 20);
        const auto trackerCount = env->GetArrayLength (trackers);
        if (trackerCount > 32 || (privateTorrent && !trackerCount)) return nullptr;
        i2p::torrents::Torrent::TrackerTiers tiers;
        for (jsize i = 0; i < trackerCount; ++i)
        {
            auto tracker = static_cast<jbyteArray> (env->GetObjectArrayElement (trackers, i));
            if (env->ExceptionCheck ()) return nullptr;
            std::string text;
            try { text = bytes (tracker, 2048); }
            catch (...) { if (tracker) env->DeleteLocalRef (tracker); throw; }
            env->DeleteLocalRef (tracker);
            i2p::http::URL url (text);
            if (text.empty () || text.find_first_of ("\r\n\t ") != std::string::npos
                || !i2p::torrents::IsI2PTrackerHost (url.host) || !url.user.empty () || !url.pass.empty ()
                || url.ipv6 || (url.schema != "http" && url.schema != "udp")) return nullptr;
            tiers.push_back ({ std::move (text) });
        }
        auto torrent = i2p::torrents::Torrent::CreateV1 (title, files, pieceLength, pieces, tiers,
            privateTorrent == JNI_TRUE);
        if (!torrent) return nullptr;
        auto metadata = torrent->CreateTorrentFileContent ();
        if (metadata.size () > 8 * 1024 * 1024) return nullptr;
        auto result = env->NewByteArray (static_cast<jsize> (metadata.size ()));
        if (result) env->SetByteArrayRegion (result, 0, static_cast<jsize> (metadata.size ()),
            reinterpret_cast<const jbyte *> (metadata.data ()));
        return result;
    }
    catch (...) { return nullptr; }
#else
    return nullptr;
#endif
}

#ifndef NO_TORRENTS
#include "TorrentsTunnel.h"
#include "TorrentPayloadStorage.h"
#include <mutex>
namespace {
struct SafEnv {
    JavaVM * vm; JNIEnv * env = nullptr; bool attached = false;
    explicit SafEnv (JavaVM * value): vm(value) {
        if (!vm) return;
        if (vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) == JNI_EDETACHED)
            attached = vm->AttachCurrentThread(&env, nullptr) == JNI_OK;
    }
    ~SafEnv () { if (attached) vm->DetachCurrentThread(); }
    bool Good () { if (!env) return false; if (env->ExceptionCheck()) { env->ExceptionClear(); return false; } return true; }
};
struct SafBridge {
    JavaVM * vm = nullptr; jclass type = nullptr;
    jmethodID configured = nullptr, root = nullptr, open = nullptr, read = nullptr, write = nullptr, close = nullptr, release = nullptr, remove = nullptr;
    ~SafBridge () { if (!vm) return; SafEnv e(vm); if (e.env && type) e.env->DeleteGlobalRef(type); }
};
class SafUnavailable final: public i2p::torrents::TorrentPayloadStorage {
public:
    bool UsesFinalNames() const override { return true; }
    uint64_t Open(const std::filesystem::path&,uint64_t,bool,std::string& error) override {error="SAF bridge unavailable";return 0;}
    bool Read(uint64_t,uint64_t,std::span<uint8_t>,std::string& error) override {error="SAF bridge unavailable";return false;}
    bool Write(uint64_t,uint64_t,std::span<const uint8_t>,std::string& error) override {error="SAF bridge unavailable";return false;}
    bool Close(uint64_t,std::string&) override {return false;}
};
class SafPayload final: public i2p::torrents::TorrentPayloadStorage {
    std::shared_ptr<SafBridge> bridge; std::string tunnel, hash; jlong rootId = 0; std::mutex lock;
public:
    SafPayload(std::shared_ptr<SafBridge> b, std::string t, std::string h): bridge(std::move(b)), tunnel(std::move(t)), hash(std::move(h)) {}
    ~SafPayload() override { SafEnv e(bridge->vm); if(e.env && rootId) { e.env->CallStaticVoidMethod(bridge->type,bridge->release,rootId); e.Good(); } }
    bool UsesFinalNames() const override { return true; }
    uint64_t Open(const std::filesystem::path& path,uint64_t length,bool create,std::string& error) override {
        std::lock_guard guard(lock); SafEnv e(bridge->vm);
        if (!e.env || length > INT64_MAX) { error="SAF VM unavailable or length overflow"; return 0; }
        if (!rootId) {
            // Freeze tree authority for this torrent backend. Administrative
            // selection changes take effect only for a new engine binding.
            auto t=NewStringChecked(e.env,tunnel), h=NewStringChecked(e.env,hash);
            if(t&&h) rootId=e.env->CallStaticLongMethod(bridge->type,bridge->root,t,h);
            if(t)e.env->DeleteLocalRef(t); if(h)e.env->DeleteLocalRef(h);
        }
        auto p=NewStringChecked(e.env,path.generic_string()); jlong id=0;
        if(e.Good() && rootId && p) id=e.env->CallStaticLongMethod(bridge->type,bridge->open,rootId,p,static_cast<jlong>(length),create?JNI_TRUE:JNI_FALSE);
        if(p)e.env->DeleteLocalRef(p);
        if(!e.Good()||id<=0) { error="SAF document unavailable, permission revoked or seek unsupported"; return 0; }
        return static_cast<uint64_t>(id);
    }
    bool Read(uint64_t handle,uint64_t offset,std::span<uint8_t> bytes,std::string& error) override {
        std::lock_guard guard(lock); SafEnv e(bridge->vm);
        if(!e.env || !Valid(handle,offset,bytes.size())) {error="Invalid SAF read";return false;}
        auto result=static_cast<jbyteArray>(e.env->CallStaticObjectMethod(bridge->type,bridge->read,static_cast<jlong>(handle),static_cast<jlong>(offset),static_cast<jint>(bytes.size())));
        bool ok=e.Good()&&result&&e.env->GetArrayLength(result)==static_cast<jsize>(bytes.size());
        if(ok)e.env->GetByteArrayRegion(result,0,static_cast<jsize>(bytes.size()),reinterpret_cast<jbyte*>(bytes.data()));
        if(result)e.env->DeleteLocalRef(result); ok=e.Good()&&ok;
        if(!ok)error="SAF read failed or document changed"; return ok;
    }
    bool Write(uint64_t handle,uint64_t offset,std::span<const uint8_t> bytes,std::string& error) override {
        std::lock_guard guard(lock); SafEnv e(bridge->vm);
        if(!e.env || !Valid(handle,offset,bytes.size())) {error="Invalid SAF write";return false;}
        auto data=e.env->NewByteArray(static_cast<jsize>(bytes.size())); bool ok=false;
        if(data) {e.env->SetByteArrayRegion(data,0,static_cast<jsize>(bytes.size()),reinterpret_cast<const jbyte*>(bytes.data()));
            if(e.Good())ok=e.env->CallStaticBooleanMethod(bridge->type,bridge->write,static_cast<jlong>(handle),static_cast<jlong>(offset),data)==JNI_TRUE;
            e.env->DeleteLocalRef(data);}
        ok=e.Good()&&ok; if(!ok)error="SAF write failed, permission revoked or storage full"; return ok;
    }
    bool Remove(const std::filesystem::path& path,uint64_t length,std::string& error) override {
        std::lock_guard guard(lock); SafEnv e(bridge->vm);
        if(!e.env || length>INT64_MAX) {error="SAF deletion capability unavailable";return false;}
        if (!rootId) {
            auto t=NewStringChecked(e.env,tunnel), h=NewStringChecked(e.env,hash);
            if(t&&h)rootId=e.env->CallStaticLongMethod(bridge->type,bridge->root,t,h);
            if(t)e.env->DeleteLocalRef(t); if(h)e.env->DeleteLocalRef(h);
        }
        if (!e.Good() || !rootId) {error="SAF deletion capability unavailable";return false;}
        auto p=NewStringChecked(e.env,path.generic_string()); bool ok=false;
        if(p && e.Good()) ok=e.env->CallStaticBooleanMethod(bridge->type,bridge->remove,rootId,p,static_cast<jlong>(length))==JNI_TRUE;
        if(p)e.env->DeleteLocalRef(p); ok=e.Good()&&ok;
        if(!ok)error="SAF document deletion failed or permission revoked";return ok;
    }
    bool Close(uint64_t handle,std::string& error) override {
        std::lock_guard guard(lock); SafEnv e(bridge->vm); if(!e.env || !handle || handle>INT64_MAX)return false;
        e.env->CallStaticVoidMethod(bridge->type,bridge->close,static_cast<jlong>(handle));
        bool ok=e.Good();if(!ok)error="SAF close failed";return ok;
    }
private:
    static bool Valid(uint64_t handle,uint64_t offset,size_t count) {return handle&&handle<=INT64_MAX&&offset<=INT64_MAX&&count<=MaxTransfer&&count<=INT64_MAX-offset;}
};
}
#endif
extern "C" JNIEXPORT jboolean JNICALL Java_org_openi2pd_router_I2PD_1JNI_installSafStorageBridge(JNIEnv * env,jclass,jclass type) {
#ifndef NO_TORRENTS
    try {
        // Install denial before any JNI allocation/lookup. Failed setup cannot
        // leave a selected document torrent on the native filesystem backend.
        i2p::torrents::SetTorrentPayloadStorageFactory([](const std::string&,const std::string&) { return std::make_shared<SafUnavailable>(); });
        auto bridge=std::make_shared<SafBridge>(); if (env->GetJavaVM(&bridge->vm) != JNI_OK || !type) return JNI_FALSE;
        bridge->type=static_cast<jclass>(env->NewGlobalRef(type)); if(!bridge->type) { if(env->ExceptionCheck())env->ExceptionClear(); return JNI_FALSE; }
        auto method = [&](const char * name,const char * signature)->jmethodID {
            return env->ExceptionCheck() ? nullptr : env->GetStaticMethodID(type,name,signature);
        };
        bridge->configured=method("configured","(Ljava/lang/String;Ljava/lang/String;)Z");
        bridge->root=method("root","(Ljava/lang/String;Ljava/lang/String;)J");
        bridge->open=method("open","(JLjava/lang/String;JZ)J");
        bridge->read=method("read","(JJI)[B"); bridge->write=method("write","(JJ[B)Z");
        bridge->remove=method("remove","(JLjava/lang/String;J)Z");
        bridge->close=method("close","(J)V");bridge->release=method("release","(J)V");
        if(env->ExceptionCheck()||!bridge->configured||!bridge->root||!bridge->open||!bridge->read||!bridge->write||!bridge->close||!bridge->release||!bridge->remove) {env->ExceptionClear();return JNI_FALSE;}
        i2p::torrents::SetTorrentPayloadStorageFactory([bridge](const std::string& tunnel,const std::string& hash)->std::shared_ptr<i2p::torrents::TorrentPayloadStorage> {
            SafEnv e(bridge->vm);if(!e.env)return std::make_shared<SafPayload>(bridge,tunnel,hash);
            auto t=NewStringChecked(e.env,tunnel),h=NewStringChecked(e.env,hash); bool configured=true;
            if(t&&h)configured=e.env->CallStaticBooleanMethod(bridge->type,bridge->configured,t,h)==JNI_TRUE;
            bool good=e.Good();if(t)e.env->DeleteLocalRef(t);if(h)e.env->DeleteLocalRef(h);
            if(good&&!configured)return nullptr;
            return std::make_shared<SafPayload>(bridge,tunnel,hash);
        });
        return JNI_TRUE;
    }catch(...){if(env->ExceptionCheck())env->ExceptionClear();return JNI_FALSE;}
#else
    return JNI_TRUE;
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
extern "C" JNIEXPORT jboolean JNICALL Java_org_openi2pd_router_I2PD_1JNI_restrictSafStorageProcess(JNIEnv *,jclass) {
#ifdef __ANDROID__
    // Applied TSYNC before publishing Binder. Binder document-provider IPC is
    // retained; creating or using external sockets and privilege-recovery APIs
    // is denied for every current and future thread. No router runs here.
    // Zygote/ART infrastructure may retain Unix sockets. Only close inherited
    // external-network sockets; do not disturb Binder or local runtime FDs.
    DIR * descriptors = opendir("/proc/self/fd");
    if (!descriptors) return JNI_FALSE;
    while (auto * entry = readdir(descriptors)) {
        char * end = nullptr; long value = strtol(entry->d_name,&end,10);
        if (!end || *end || value < 0 || value > INT_MAX || value == dirfd(descriptors)) continue;
        sockaddr_storage address {}; socklen_t size = sizeof(address);
        if (!getsockname(static_cast<int>(value),reinterpret_cast<sockaddr*>(&address),&size)
            && (address.ss_family == AF_INET || address.ss_family == AF_INET6)) close(static_cast<int>(value));
    }
    closedir(descriptors);
    const sock_filter filter[] = {
        BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(seccomp_data,arch)),
#if defined(__aarch64__)
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,AUDIT_ARCH_AARCH64,1,0),
#elif defined(__arm__)
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,AUDIT_ARCH_ARM,1,0),
#elif defined(__x86_64__)
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,AUDIT_ARCH_X86_64,1,0),
#elif defined(__i386__)
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,AUDIT_ARCH_I386,1,0),
#else
#error Unsupported Android seccomp architecture
#endif
        BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_KILL),
        BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(seccomp_data,nr)),
#if defined(__x86_64__)
        BPF_JUMP(BPF_JMP|BPF_JSET|BPF_K,0x40000000,0,1),
        BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ERRNO|EPERM),
#endif
#define SAF_DENY(number) BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,number,0,1), BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ERRNO|EPERM)
#ifdef __NR_socketcall
        SAF_DENY(__NR_socketcall),
#endif
        SAF_DENY(__NR_socket), SAF_DENY(__NR_connect), SAF_DENY(__NR_sendto), SAF_DENY(__NR_sendmsg),
        SAF_DENY(__NR_ptrace),
#ifdef __NR_sendmmsg
        SAF_DENY(__NR_sendmmsg),
#endif
#ifdef __NR_io_uring_setup
        SAF_DENY(__NR_io_uring_setup),
#endif
#ifdef __NR_pidfd_getfd
        SAF_DENY(__NR_pidfd_getfd),
#endif
        BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ALLOW)
#undef SAF_DENY
    };
    sock_fprog program {static_cast<unsigned short>(sizeof(filter)/sizeof(filter[0])),const_cast<sock_filter*>(filter)};
    if(prctl(PR_SET_NO_NEW_PRIVS,1,0,0,0))return JNI_FALSE;
    return syscall(__NR_seccomp,SECCOMP_SET_MODE_FILTER,SECCOMP_FILTER_FLAG_TSYNC,&program)==0?JNI_TRUE:JNI_FALSE;
#else
    return JNI_FALSE;
#endif
}
