/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: UPnP.
 *
 * Implements IGD discovery, external-address publication and IPv4 port
 * mapping every 20 minutes on a dedicated Boost.Asio thread. Mapping iterates
 * an immutable RouterInfo snapshot so transport address updates cannot race,
 * every mapping is added with a three-hour lease and renewed by the periodic
 * pass, a same-host IGD is refused, and Start/Stop support a clean restart.
 * A small status snapshot is published for administrative readers.
 *
 */

#ifdef USE_UPNP
#include <string>
#include <thread>

#include "Log.h"

#include "RouterContext.h"
#include "UPnP.h"
#include "NetDb.hpp"
#include "util.h"
#include "RouterInfo.h"
#include "Config.h"

#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/upnpcommands.h>

namespace i2p
{
namespace transport
{
	UPnP::UPnP ()
		: m_IsRunning (false)
		, m_Thread (nullptr)
		, m_Timer (m_Service)
	{
	}

	void UPnP::SetStatus (bool mapped, int error)
	{
		std::lock_guard<std::mutex> lock (m_StatusMutex);
		m_Status.mapped = mapped;
		m_Status.lastError = error;
	}

	UPnPStatus UPnP::GetStatus () const
	{
		std::lock_guard<std::mutex> lock (m_StatusMutex);
		return m_Status;
	}

	void UPnP::Stop ()
	{
		if (m_IsRunning.exchange (false, std::memory_order_relaxed))
		{
			LogPrint (eLogInfo, "UPnP: Stopping");
			// io_context::stop() is safe to call while Run() is executing.
			m_Service.stop ();
			if (m_Thread)
			{
				if (m_Thread->joinable ()) m_Thread->join ();
				m_Thread.reset (nullptr);
			}
			// The service thread is gone now, so touching the timer is safe.
			// cancel() only retires a pending wait; the aborted handler is
			// discarded on the next restart.
			m_Timer.cancel ();
			CloseMapping ();
			Close ();
			{
				std::lock_guard<std::mutex> lock (m_StatusMutex);
				m_Status.enabled = false;
				m_Status.mapped = false;
			}
		}
	}

	void UPnP::Start ()
	{
		if (m_IsRunning.exchange (true, std::memory_order_relaxed)) return;
		LogPrint (eLogInfo, "UPnP: Starting");
		{
			std::lock_guard<std::mutex> lock (m_StatusMutex);
			m_Status.enabled = true;
			m_Status.mapped = false;
			m_Status.lastError = 0;
		}
		// Stop() leaves the io_context stopped; restart it so a second
		// Start() actually runs discovery again.
		m_Service.restart ();
		boost::asio::post (m_Service, std::bind (&UPnP::Discover, this));
		std::unique_lock<std::mutex> l (m_StartedMutex);
		m_Thread = std::make_unique<std::thread> (std::bind (&UPnP::Run, this));
		m_Started.wait_for (l, std::chrono::seconds (5)); // 5 seconds maximum
	}

	UPnP::~UPnP ()
	{
		Stop ();
	}

	void UPnP::Run ()
	{
		i2p::util::SetThreadName ("UPnP");

		while (m_IsRunning.load (std::memory_order_relaxed))
		{
			try
			{
				m_Service.run ();
				// Discover failed
				break; // terminate the thread
			}
			catch (std::exception & ex)
			{
				LogPrint (eLogError, "UPnP: Runtime exception: ", ex.what ());
				PortMapping ();
			}
		}
	}

	void UPnP::Discover ()
	{
		int err = UPNPDISCOVER_SUCCESS;
		m_Devlist = upnpDiscover (UPNP_RESPONSE_TIMEOUT, NULL, NULL, 0, 0, 2, &err);
		{
			// notify starting thread
			std::unique_lock<std::mutex> l (m_StartedMutex);
			m_Started.notify_all ();
		}

		if (err != UPNPDISCOVER_SUCCESS)
		{
			LogPrint (eLogError, "UPnP: Unable to discover Internet Gateway Devices: error ", err);
			SetStatus (false, err);
			return;
		}

#if (MINIUPNPC_API_VERSION >= 18)
		err = UPNP_GetValidIGD (m_Devlist, &m_upnpUrls, &m_upnpData, m_NetworkAddr, sizeof (m_NetworkAddr),
			m_externalIPAddress, sizeof (m_externalIPAddress));
#else
		err = UPNP_GetValidIGD (m_Devlist, &m_upnpUrls, &m_upnpData, m_NetworkAddr, sizeof (m_NetworkAddr));
#endif
		m_upnpUrlsInitialized = err != 0;
		if (err == UPNP_IGD_VALID_CONNECTED)
		{
#if (MINIUPNPC_API_VERSION < 18)
			err = UPNP_GetExternalIPAddress (m_upnpUrls.controlURL, m_upnpData.first.servicetype, m_externalIPAddress);
			if (err != UPNPCOMMAND_SUCCESS)
			{
				LogPrint (eLogError, "UPnP: Unable to get external address: error ", err);
				SetStatus (false, err);
				return;
			}
#endif
			LogPrint (eLogError, "UPnP: Found Internet Gateway Device ", m_upnpUrls.controlURL);
			if (!m_externalIPAddress[0])
			{
				LogPrint (eLogError, "UPnP: Found Internet Gateway Device doesn't know our external address");
				SetStatus (false, 0);
				return;
			}
			// An IGD that reports this host's own address is either a
			// misdetected device or the host itself: mapping would be
			// meaningless and could publish a private listener.
			if (IsSameHostIGD (m_externalIPAddress, m_NetworkAddr))
			{
				LogPrint (eLogWarning, "UPnP: Refusing a same-host Internet Gateway Device");
				SetStatus (false, UPNP_IGD_INVALID);
				return;
			}
		}
		else
		{
			LogPrint (eLogError, "UPnP: Unable to find valid Internet Gateway Device: error ", err);
			SetStatus (false, err);
			return;
		}

		// UPnP discovered
		LogPrint (eLogDebug, "UPnP: ExternalIPAddress is ", m_externalIPAddress);
		i2p::context.UpdateAddress (boost::asio::ip::make_address (m_externalIPAddress));
		// port mapping
		PortMapping ();
	}

	int UPnP::CheckMapping (const char * port, const char * type)
	{
		int err = UPNP_GetSpecificPortMappingEntry (
			m_upnpUrls.controlURL, m_upnpData.first.servicetype, port, type, NULL, NULL, NULL, NULL, NULL, NULL);
		return err;
	}

	void UPnP::PortMapping ()
	{
		// UPnP runs on its own timer thread. Do not iterate the live local
		// RouterInfo while transport/address updates can replace its Address
		// vectors; use the immutable signed-buffer snapshot instead.
		auto snapshot = context.GetRouterInfoSnapshot ();
		if (!snapshot) return;
		auto a = snapshot->GetAddresses ();
		if (!a) return;
		for (const auto & address : *a)
		{
			if (address && !address->host.is_v6 () && address->port) TryPortMapping (address);
		}
		m_Timer.expires_after (std::chrono::minutes (UPNP_PORT_FORWARDING_INTERVAL)); // every 20 minutes
		m_Timer.async_wait (
			[this] (const boost::system::error_code & ecode)
			{
				if (ecode != boost::asio::error::operation_aborted) PortMapping ();
			});
	}

	void UPnP::TryPortMapping (std::shared_ptr<i2p::data::RouterInfo::Address> address)
	{
		std::string strType (GetProto (address)), strPort (std::to_string (address->port));
		std::string strDesc;
		i2p::config::GetOption ("upnp.name", strDesc);

		// Inspect the current mapping. A lease-bound mapping is renewed by
		// re-adding it, but a mapping that points at another host or port must
		// be deleted first so this router never inherits a stale forward.
		char intClient[16] = { 0 }, intPort[6] = { 0 }, description[80] = { 0 }, enabled[4] = { 0 };
		char leaseDuration[16] = { 0 };
		int err = UPNP_GetSpecificPortMappingEntry (m_upnpUrls.controlURL, m_upnpData.first.servicetype,
			strPort.c_str (), strType.c_str (), nullptr, intClient, intPort, description, enabled, leaseDuration);
		if (err == UPNPCOMMAND_SUCCESS && !IsOwnUPnPMapping (intClient, intPort, m_NetworkAddr, strPort))
		{
			LogPrint (eLogWarning, "UPnP: Replacing a port mapping owned by another host");
			UPNP_DeletePortMapping (
				m_upnpUrls.controlURL, m_upnpData.first.servicetype, strPort.c_str (), strType.c_str (), nullptr);
			err = UPNPCOMMAND_SUCCESS; // deleted: fall through to a fresh add
		}
		if (err != UPNPCOMMAND_SUCCESS)
			LogPrint (eLogDebug, "UPnP: Port ", strPort, " is possibly not forwarded: return code ", err);

		const auto lease = FormatUPnPLeaseDuration ();
		err = UPNP_AddPortMapping (m_upnpUrls.controlURL, m_upnpData.first.servicetype, strPort.c_str (),
			strPort.c_str (), m_NetworkAddr, strDesc.c_str (), strType.c_str (), nullptr, lease.c_str ());
		if (err != UPNPCOMMAND_SUCCESS)
		{
			LogPrint (
				eLogError, "UPnP: Port forwarding to ", m_NetworkAddr, ":", strPort, " failed: return code ", err);
			SetStatus (false, err);
			return;
		}
		LogPrint (eLogInfo, "UPnP: Port successfully forwarded (", m_externalIPAddress, ":", strPort, " type ", strType,
			" -> ", m_NetworkAddr, ":", strPort, ", lease ", lease, " s)");
		SetStatus (true, UPNPCOMMAND_SUCCESS);
	}

	void UPnP::CloseMapping ()
	{
		auto snapshot = context.GetRouterInfoSnapshot ();
		if (!snapshot) return;
		auto a = snapshot->GetAddresses ();
		if (!a) return;
		for (const auto & address : *a)
		{
			if (address && !address->host.is_v6 () && address->port) CloseMapping (address);
		}
	}

	void UPnP::CloseMapping (std::shared_ptr<i2p::data::RouterInfo::Address> address)
	{
		if (!m_upnpUrlsInitialized)
		{
			return;
		}
		std::string strType (GetProto (address)), strPort (std::to_string (address->port));
		int err = UPNPCOMMAND_SUCCESS;

		err = CheckMapping (strPort.c_str (), strType.c_str ());
		if (err == UPNPCOMMAND_SUCCESS)
		{
			err = UPNP_DeletePortMapping (
				m_upnpUrls.controlURL, m_upnpData.first.servicetype, strPort.c_str (), strType.c_str (), NULL);
			LogPrint (eLogError, "UPnP: DeletePortMapping() returned : ", err);
		}
		SetStatus (false, err);
	}

	void UPnP::Close ()
	{
		freeUPNPDevlist (m_Devlist);
		m_Devlist = 0;
		if (m_upnpUrlsInitialized)
		{
			FreeUPNPUrls (&m_upnpUrls);
			m_upnpUrlsInitialized = false;
		}
	}

	std::string UPnP::GetProto (std::shared_ptr<i2p::data::RouterInfo::Address> address)
	{
		switch (address->transportStyle)
		{
		case i2p::data::RouterInfo::eTransportNTCP2: return "TCP"; break;
		case i2p::data::RouterInfo::eTransportSSU2:
		default: return "UDP";
		}
	}
}
}
#else /* USE_UPNP */
namespace i2p
{
namespace transport
{
}
}
#endif /* USE_UPNP */
