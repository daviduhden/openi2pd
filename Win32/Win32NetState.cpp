/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Win32NetState.
 *
 * Subscribes to INetworkListManagerEvents through COM and drives the transport
 * online state from connectivity and internet-capability changes. It performs
 * one initial connectivity probe and releases the connection point and COM
 * objects when unsubscribing.
 *
 */

#include "Win32NetState.h"
#include <windows.h>
#include "Log.h"

IUnknown * pUnknown = nullptr;
INetworkListManager * pNetworkListManager = nullptr;
IConnectionPointContainer * pCPContainer = nullptr;
IConnectionPoint * pConnectPoint = nullptr;
CNetworkListManagerEvent * pNetEvent = nullptr;
DWORD Cookie = 0;

void SubscribeToEvents ()
{
	LogPrint (eLogInfo, "NetState: Trying to subscribe to NetworkListManagerEvents");
	CoInitialize (NULL);

	HRESULT Result = CoCreateInstance (CLSID_NetworkListManager, NULL, CLSCTX_ALL, IID_IUnknown, (void **)&pUnknown);
	if (SUCCEEDED (Result))
	{
		Result = pUnknown->QueryInterface (IID_INetworkListManager, (void **)&pNetworkListManager);
		if (SUCCEEDED (Result))
		{
			VARIANT_BOOL IsConnect = VARIANT_FALSE;
#if defined(_MSC_VER)
			Result = pNetworkListManager->get_IsConnectedToInternet (&IsConnect);
#else
			Result = pNetworkListManager->IsConnectedToInternet (&IsConnect);
#endif
			if (SUCCEEDED (Result))
			{
				i2p::transport::transports.SetOnline (true);
				LogPrint (
					eLogInfo, "NetState: Current state: ", IsConnect == VARIANT_TRUE ? "connected" : "disconnected");
			}

			Result = pNetworkListManager->QueryInterface (IID_IConnectionPointContainer, (void **)&pCPContainer);
			if (SUCCEEDED (Result))
			{
				Result = pCPContainer->FindConnectionPoint (IID_INetworkListManagerEvents, &pConnectPoint);
				if (SUCCEEDED (Result))
				{
					pNetEvent = new CNetworkListManagerEvent;
					Result = pConnectPoint->Advise ((IUnknown *)pNetEvent, &Cookie);
					if (SUCCEEDED (Result))
						LogPrint (eLogInfo, "NetState: Successfully subscribed to NetworkListManagerEvent messages");
					else
						LogPrint (eLogError, "NetState: Unable to subscribe to NetworkListManagerEvent messages");
				}
				else
					LogPrint (eLogError, "NetState: Unable to find interface connection point");
			}
			else
				LogPrint (eLogError, "NetState: Unable to query NetworkListManager interface");
		}
		else
			LogPrint (eLogError, "NetState: Unable to query global interface");
	}
	else
		LogPrint (eLogError, "NetState: Unable to create INetworkListManager interface");
}

void UnSubscribeFromEvents ()
{
	LogPrint (eLogInfo, "NetState: Unsubscribing from NetworkListManagerEvents");
	try
	{
		if (pConnectPoint)
		{
			pConnectPoint->Unadvise (Cookie);
			pConnectPoint->Release ();
		}

		if (pNetEvent)
		{
			pNetEvent->Release ();
		}

		if (pCPContainer)
		{
			pCPContainer->Release ();
		}

		if (pNetworkListManager)
		{
			pNetworkListManager->Release ();
		}

		if (pUnknown)
		{
			pUnknown->Release ();
		}

		CoUninitialize ();
	}
	catch (std::exception & ex)
	{
		LogPrint (eLogError, "NetState: Received exception: ", ex.what ());
	}
}
