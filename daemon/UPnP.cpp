/*
* Copyright (c) 2013-2024, The PurpleI2P Project
* Copyright (c) 2026, David Uhden Collado
* SPDX-License-Identifier: BSD-3-Clause
*
* This file is part of openi2pd and licensed under BSD-3-Clause
*
* See full license text in LICENSE file at top of project tree
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
		}
	}

	void UPnP::Start ()
	{
		if (m_IsRunning.exchange (true, std::memory_order_relaxed)) return;
		LogPrint (eLogInfo, "UPnP: Starting");
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
				return;
			}
#endif
			LogPrint (eLogError, "UPnP: Found Internet Gateway Device ", m_upnpUrls.controlURL);
			if (!m_externalIPAddress[0])
			{
				LogPrint (eLogError, "UPnP: Found Internet Gateway Device doesn't know our external address");
				return;
			}
		}
		else
		{
			LogPrint (eLogError, "UPnP: Unable to find valid Internet Gateway Device: error ", err);
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
		int err = UPNPCOMMAND_SUCCESS;

		// check for existing mapping
		err = CheckMapping (strPort.c_str (), strType.c_str ());
		if (err != UPNPCOMMAND_SUCCESS) // if mapping not found
		{
			LogPrint (eLogDebug, "UPnP: Port ", strPort, " is possibly not forwarded: return code ", err);

			err = UPNP_AddPortMapping (m_upnpUrls.controlURL, m_upnpData.first.servicetype, strPort.c_str (),
				strPort.c_str (), m_NetworkAddr, strDesc.c_str (), strType.c_str (), NULL, NULL);
			if (err != UPNPCOMMAND_SUCCESS)
			{
				LogPrint (
					eLogError, "UPnP: Port forwarding to ", m_NetworkAddr, ":", strPort, " failed: return code ", err);
				return;
			}
			else
			{
				LogPrint (eLogInfo, "UPnP: Port successfully forwarded (", m_externalIPAddress, ":", strPort, " type ",
					strType, " -> ", m_NetworkAddr, ":", strPort, ")");
				return;
			}
		}
		else
		{
			LogPrint (eLogDebug, "UPnP: External forward from ", m_NetworkAddr, ":", strPort,
				" exists on current Internet Gateway Device");
			return;
		}
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
