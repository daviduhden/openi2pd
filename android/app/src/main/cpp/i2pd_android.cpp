/*
* Copyright (c) 2013-2022, The PurpleI2P Project
*
* This file is part of Open I2Pd and licensed under BSD3
*
* See full license text in LICENSE file at top of project tree
*/

#include <jni.h>
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
