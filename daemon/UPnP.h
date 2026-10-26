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

// The compile-time stub logs through LogPrint; include the logger directly so
// the header does not depend on the includer's order.
#include "Log.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace i2p
{
namespace transport
{
	/*
	 * Mapping policy shared by the active implementation and its regression.
	 *
	 * An IGD lease is a decimal number of seconds; a mapping added without one
	 * is permanent and survives this process, so every mapping is time-limited
	 * and renewed by the periodic pass. A same-host IGD (one whose external
	 * address is this machine) is refused outright.
	 */
	inline constexpr unsigned UPNP_LEASE_DURATION_SECONDS = 3 * 60 * 60; // 3 hours
	inline constexpr unsigned UPNP_MAX_LEASE_DURATION_SECONDS = 7 * 24 * 60 * 60;

	inline std::string FormatUPnPLeaseDuration (unsigned seconds = UPNP_LEASE_DURATION_SECONDS)
	{
		if (seconds == 0) seconds = UPNP_LEASE_DURATION_SECONDS;
		if (seconds > UPNP_MAX_LEASE_DURATION_SECONDS) seconds = UPNP_MAX_LEASE_DURATION_SECONDS;
		return std::to_string (seconds);
	}

	// True when the discovered IGD reports an address that belongs to this
	// host, or no usable address at all.
	inline bool IsSameHostIGD (std::string_view externalAddress, std::string_view localAddress)
	{
		auto trim = [] (std::string_view value)
		{
			while (!value.empty () && (value.front () == ' ' || value.front () == '\t')) value.remove_prefix (1);
			while (!value.empty () && (value.back () == ' ' || value.back () == '\t')) value.remove_suffix (1);
			return value;
		};
		const auto external = trim (externalAddress);
		const auto local = trim (localAddress);
		if (external.empty ()) return true; // no address: do not claim a mapping
		if (external == "0.0.0.0" || external == "::" || external == "::1") return true;
		if (external == "127.0.0.1" || external == "localhost") return true;
		if (external.size () > 4 && external.substr (0, 4) == "127.") return true;
		return !local.empty () && external == local;
	}

	// True when an existing mapping already points at this exact target, i.e.
	// it can be renewed in place instead of being deleted first.
	inline bool IsOwnUPnPMapping (std::string_view mappingClient, std::string_view mappingPort,
		std::string_view ourAddress, std::string_view ourPort)
	{
		return !mappingClient.empty () && !ourAddress.empty () && mappingClient == ourAddress && !mappingPort.empty ()
			&& mappingPort == ourPort;
	}

	struct UPnPStatus
	{
		bool enabled = false;
		bool mapped = false;
		int lastError = 0;
	};
}
}

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
		void SetStatus (bool mapped, int error);

	public:
		// Bounded administrative view: enabled, whether a mapping is currently
		// believed to be installed, and the last miniupnpc/UPnP result code.
		UPnPStatus GetStatus () const;

	private:
		std::atomic<bool> m_IsRunning;
		std::unique_ptr<std::thread> m_Thread;
		std::condition_variable m_Started;
		std::mutex m_StartedMutex;
		mutable std::mutex m_StatusMutex;
		UPnPStatus m_Status;
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
		UPnPStatus GetStatus () const { return UPnPStatus { }; }
	};
}
}
#endif // USE_UPNP
#endif // __UPNP_H__
