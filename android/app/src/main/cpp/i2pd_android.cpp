/*
* Copyright (c) 2013-2022, The PurpleI2P Project
*
* This file is part of Open I2Pd and licensed under BSD3
*
* See full license text in LICENSE file at top of project tree
*/

#include <jni.h>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <string_view>
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

namespace
{
	/** RAII wrapper for GetStringUTFChars/ReleaseStringUTFChars. */
	class ScopedUtfChars
	{
		public:
			ScopedUtfChars (JNIEnv * env, jstring text): m_Env (env), m_Text (text)
			{
				if (m_Text) m_Chars = m_Env->GetStringUTFChars (m_Text, nullptr);
			}
			~ScopedUtfChars ()
			{
				if (m_Chars) m_Env->ReleaseStringUTFChars (m_Text, m_Chars);
			}
			ScopedUtfChars (const ScopedUtfChars&) = delete;
			ScopedUtfChars& operator= (const ScopedUtfChars&) = delete;
			const char * c_str () const { return m_Chars; }
			explicit operator bool () const { return m_Chars != nullptr; }

		private:
			JNIEnv * m_Env;
			jstring m_Text;
			const char * m_Chars = nullptr;
	};

	inline jstring NewString (JNIEnv * env, const std::string& value)
	{
		return env->NewStringUTF (value.c_str ());
	}

	/**
	 * NewStringUTF that never leaves a pending JNI exception behind. If the
	 * VM cannot allocate the string (OOM or invalid modified UTF-8 was
	 * rejected), the exception is cleared and the caller receives nullptr.
	 * Java must treat a null result as "no snapshot available".
	 */
	inline jstring NewStringChecked (JNIEnv * env, const char * value)
	{
		jstring result = env->NewStringUTF (value);
		if (!result && env->ExceptionCheck ())
			env->ExceptionClear ();
		return result;
	}

	inline jstring NewStringChecked (JNIEnv * env, const std::string& value)
	{
		return NewStringChecked (env, value.c_str ());
	}

	/** Maximum number of log records returned in one call (contract). */
	constexpr size_t MAX_RECENT_LOG_RECORDS = 256;

	/**
	 * Fixed, bounded snapshot returned when collecting the real snapshot
	 * throws. It has the same keys and ordering as a normal snapshot so that
	 * Java can parse it with the same code.
	 */
	constexpr const char * ERROR_SNAPSHOT =
		"status=Error\n"
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
	 * Append one address record as transport|host|port|published. IPv6 hosts
	 * are enclosed in brackets so the value stays unambiguous. Unspecified
	 * hosts (0.0.0.0 / ::) are omitted: they are local bindings, not observed
	 * addresses.
	 */
	void AppendAddress (std::string& value, bool& first,
		const i2p::data::RouterInfo::Address& address)
	{
		const char * transport = TransportName (address.transportStyle);
		if (!transport || address.host.is_unspecified ()) return;

		if (!first) value.push_back (';');
		first = false;

		value.append (transport);
		value.push_back ('|');
		if (address.host.is_v6 ())
		{
			value.push_back ('[');
			value.append (address.host.to_string ());
			value.push_back (']');
		}
		else
			value.append (address.host.to_string ());
		value.push_back ('|');
		value.append (std::to_string (address.port));
		value.push_back ('|');
		value.push_back (address.published ? '1' : '0');
	}

	void AppendField (std::string& out, const char * key, const std::string& value)
	{
		out.append (key);
		out.push_back ('=');
		out.append (value);
		out.push_back ('\n');
	}

	void AppendField (std::string& out, const char * key, const char * value)
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
	 * different times.
	 */
	std::string BuildStatusSnapshot ()
	{
		std::string out;
		out.reserve (1024);

		const auto status = i2p::context.GetStatus ();
		const auto error = i2p::context.GetError ();
		const auto& transports = i2p::transport::transports;
		const auto& netdb = i2p::data::netdb;
		const auto& tunnels = i2p::tunnel::tunnels;

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
		auto addresses = i2p::context.GetRouterInfo ().GetAddresses ();
		if (addresses)
		{
			for (const auto& address : *addresses)
			{
				if (!address) continue;
				if (address->host.is_v4 ())
					AppendAddress (ipv4, firstV4, *address);
				else if (address->host.is_v6 ())
					AppendAddress (ipv6, firstV6, *address);
			}
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
		for (const auto& record : records)
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

JNIEXPORT jstring JNICALL Java_org_openi2pd_router_I2PD_1JNI_getABICompiledWith
	(JNIEnv *env, jclass clazz) {
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
	#elif defined(__mips64)  /* mips64el-* toolchain defines __mips__ too */
	#define ABI "mips64"
	#elif defined(__mips__)
	#define ABI "mips"
	#elif defined(__aarch64__)
	#define ABI "arm64-v8a"
	#else
	#define ABI "unknown"
#endif

	return env->NewStringUTF(ABI);
}

JNIEXPORT jstring JNICALL Java_org_openi2pd_router_I2PD_1JNI_startDaemon
	(JNIEnv *env, jclass clazz) {
	return NewString (env, i2p::android::start ());
}

JNIEXPORT jstring JNICALL Java_org_openi2pd_router_I2PD_1JNI_getDataDir
	(JNIEnv *env, jclass clazz) {
	return NewString (env, i2p::android::GetDataDir ());
}

JNIEXPORT void JNICALL Java_org_openi2pd_router_I2PD_1JNI_stopDaemon
	(JNIEnv *env, jclass clazz) {
	i2p::android::stop();
}

JNIEXPORT void JNICALL Java_org_openi2pd_router_I2PD_1JNI_stopAcceptingTunnels
	(JNIEnv *env, jclass clazz) {
	i2p::context.SetAcceptsTunnels (false);
}

JNIEXPORT void JNICALL Java_org_openi2pd_router_I2PD_1JNI_startAcceptingTunnels
	(JNIEnv *env, jclass clazz) {
	i2p::context.SetAcceptsTunnels (true);
}

JNIEXPORT void JNICALL Java_org_openi2pd_router_I2PD_1JNI_reloadTunnelsConfigs
	(JNIEnv *env, jclass clazz) {
	i2p::client::context.ReloadConfig();
}

JNIEXPORT void JNICALL Java_org_openi2pd_router_I2PD_1JNI_onNetworkStateChanged
	(JNIEnv *env, jclass clazz, jboolean isConnected) {
	bool isConnectedBool = (bool) isConnected;
	i2p::transport::transports.SetOnline (isConnectedBool);
}

JNIEXPORT void JNICALL Java_org_openi2pd_router_I2PD_1JNI_setDataDir
	(JNIEnv *env, jclass clazz, jstring jdataDir) {
	ScopedUtfChars dataDir (env, jdataDir);
	if (!dataDir) return;
	i2p::android::SetDataDir (dataDir.c_str ());
}

JNIEXPORT jint JNICALL Java_org_openi2pd_router_I2PD_1JNI_getTransitTunnelsCount
	(JNIEnv *env, jclass clazz) {
	return i2p::tunnel::tunnels.CountTransitTunnels();
}

JNIEXPORT void JNICALL Java_org_openi2pd_router_I2PD_1JNI_setLanguage
	(JNIEnv *env, jclass clazz, jstring jlanguage) {
	ScopedUtfChars language (env, jlanguage);
	if (!language) return;
	i2p::android::SetLanguage (language.c_str ());
}

JNIEXPORT jstring JNICALL Java_org_openi2pd_router_I2PD_1JNI_getStatusSummary
	(JNIEnv *env, jclass clazz) {
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
	out << "Status: " << status
		<< " | Uptime: " << i2p::context.GetUptime () << " s"
		<< " | Peers: " << i2p::transport::transports.GetNumPeers ()
		<< " | Routers: " << i2p::data::netdb.GetNumRouters ()
		<< " | Tunnels in/out/transit: " << i2p::tunnel::tunnels.CountInboundTunnels ()
		<< "/" << i2p::tunnel::tunnels.CountOutboundTunnels ()
		<< "/" << i2p::tunnel::tunnels.CountTransitTunnels ()
		<< " | Reseeding: " << (i2p::data::netdb.IsReseeding () ? "yes" : "no");
	return NewString (env, out.str ());
}

JNIEXPORT jstring JNICALL Java_org_openi2pd_router_I2PD_1JNI_getStatusSnapshot
	(JNIEnv *env, jclass clazz) {
	try
	{
		return NewStringChecked (env, BuildStatusSnapshot ());
	}
	catch (const std::exception&)
	{
		return NewStringChecked (env, ERROR_SNAPSHOT);
	}
	catch (...)
	{
		return NewStringChecked (env, ERROR_SNAPSHOT);
	}
}

JNIEXPORT jstring JNICALL Java_org_openi2pd_router_I2PD_1JNI_getRecentLogs
	(JNIEnv *env, jclass clazz, jlong afterSequence, jint limit) {
	try
	{
		const uint64_t after = afterSequence > 0 ? static_cast<uint64_t> (afterSequence) : 0;
		size_t maxRecords = limit > 0 ? static_cast<size_t> (limit) : 1;
		if (maxRecords > MAX_RECENT_LOG_RECORDS) maxRecords = MAX_RECENT_LOG_RECORDS;
		return NewStringChecked (env, BuildRecentLogs (after, maxRecords));
	}
	catch (const std::exception&)
	{
		return NewStringChecked (env, "");
	}
	catch (...)
	{
		return NewStringChecked (env, "");
	}
}

JNIEXPORT jboolean JNICALL Java_org_openi2pd_router_I2PD_1JNI_getHTTPProxyState
	(JNIEnv *, jclass) {
	return i2p::client::context.GetHttpProxy () ? true : false;
}

JNIEXPORT jboolean JNICALL Java_org_openi2pd_router_I2PD_1JNI_getSOCKSProxyState
	(JNIEnv *, jclass) {
	return i2p::client::context.GetSocksProxy() ? true : false;
}

JNIEXPORT jboolean JNICALL Java_org_openi2pd_router_I2PD_1JNI_getBOBState
	(JNIEnv *, jclass) {
	return i2p::client::context.GetBOBCommandChannel() ? true : false;
}

JNIEXPORT jboolean JNICALL Java_org_openi2pd_router_I2PD_1JNI_getSAMState
	(JNIEnv *, jclass) {
	return i2p::client::context.GetSAMBridge() ? true : false;
}

JNIEXPORT jboolean JNICALL Java_org_openi2pd_router_I2PD_1JNI_getI2CPState
	(JNIEnv *, jclass) {
	return i2p::client::context.GetI2CPServer() ? true : false;
}
