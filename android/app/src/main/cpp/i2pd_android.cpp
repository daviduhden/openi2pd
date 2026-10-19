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
