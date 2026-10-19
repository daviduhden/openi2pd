/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: UPnP.
 *
 * Declares the optional miniupnpc-backed IGD discovery and port-mapping
 * helper, with a compile-time stub when USE_UPNP is disabled. The active
 * interface exposes a threaded start/stop lifecycle and IPv4 mapping for
 * router addresses.
 *
 */

#ifndef __UPNP_H__
#define __UPNP_H__

#ifdef USE_UPNP
#include <atomic>
#include <string>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <memory>

#include <miniupnpc/miniwget.h>
#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/upnpcommands.h>
#include <miniupnpc/upnperrors.h>

#include <boost/asio.hpp>

namespace i2p
{
namespace transport
{
	const int UPNP_RESPONSE_TIMEOUT = 2000; // in milliseconds
	const int UPNP_PORT_FORWARDING_INTERVAL = 20; // in minutes

	enum
	{
		UPNP_IGD_NONE = 0,
		UPNP_IGD_VALID_CONNECTED = 1,
		UPNP_IGD_VALID_NOT_CONNECTED = 2,
		UPNP_IGD_INVALID = 3
	};

	class UPnP
	{
	public:
		UPnP ();
		~UPnP ();
		void Close ();

		void Start ();
		void Stop ();

	private:
		void Discover ();
		int CheckMapping (const char * port, const char * type);
		void PortMapping ();
		void TryPortMapping (std::shared_ptr<i2p::data::RouterInfo::Address> address);
		void CloseMapping ();
		void CloseMapping (std::shared_ptr<i2p::data::RouterInfo::Address> address);

		void Run ();
		std::string GetProto (std::shared_ptr<i2p::data::RouterInfo::Address> address);

	private:
		std::atomic<bool> m_IsRunning;
		std::unique_ptr<std::thread> m_Thread;
		std::condition_variable m_Started;
		std::mutex m_StartedMutex;
		boost::asio::io_context m_Service;
		boost::asio::steady_timer m_Timer;
		bool m_upnpUrlsInitialized = false;
		struct UPNPUrls m_upnpUrls;
		struct IGDdatas m_upnpData;

		// For miniupnpc
		struct UPNPDev * m_Devlist = 0;
		char m_NetworkAddr[64];
		char m_externalIPAddress[40];
	};
}
}

#else // USE_UPNP
namespace i2p
{
namespace transport
{
	/* class stub */
	class UPnP
	{
	public:
		UPnP () { };
		~UPnP () { };
		void Start () { LogPrint (eLogWarning, "UPnP: this module was disabled at compile-time"); }
		void Stop () { };
	};
}
}
#endif // USE_UPNP
#endif // __UPNP_H__
