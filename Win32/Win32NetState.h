/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Win32NetState.
 *
 * Declares the COM Network List Manager event sink that maps Windows
 * connectivity changes to the router's online or offline state. The sink
 * tracks IPv4 and IPv6 internet capability and releases itself through
 * reference counting.
 *
 */

#ifndef WIN_32_NETSTATE_H__
#define WIN_32_NETSTATE_H__

#include <netlistmgr.h>
#include <ocidl.h>
#include "Log.h"
#include "Transports.h"

class CNetworkListManagerEvent final : public INetworkListManagerEvents
{
public:
	CNetworkListManagerEvent ()
		: m_ref (1)
	{
	}
	~CNetworkListManagerEvent () { }

	HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, void ** ppvObject)
	{
		if (IsEqualIID (riid, IID_IUnknown))
		{
			*ppvObject = (IUnknown *)this;
		}
		else if (IsEqualIID (riid, IID_INetworkListManagerEvents))
		{
			*ppvObject = (INetworkListManagerEvents *)this;
		}
		else
		{
			return E_NOINTERFACE;
		}
		AddRef ();
		return S_OK;
	}

	ULONG STDMETHODCALLTYPE AddRef () { return (ULONG)InterlockedIncrement (&m_ref); }

	ULONG STDMETHODCALLTYPE Release ()
	{
		LONG Result = InterlockedDecrement (&m_ref);
		if (Result == 0) delete this;
		return (ULONG)Result;
	}

	virtual HRESULT STDMETHODCALLTYPE ConnectivityChanged (NLM_CONNECTIVITY newConnectivity)
	{
		if (newConnectivity == NLM_CONNECTIVITY_DISCONNECTED)
		{
			i2p::transport::transports.SetOnline (false);
			LogPrint (eLogInfo, "NetState: disconnected from network");
		}

		if (((int)newConnectivity & (int)NLM_CONNECTIVITY_IPV4_INTERNET) != 0)
		{
			i2p::transport::transports.SetOnline (true);
			LogPrint (eLogInfo, "NetState: connected to internet with IPv4 capability");
		}

		if (((int)newConnectivity & (int)NLM_CONNECTIVITY_IPV6_INTERNET) != 0)
		{
			i2p::transport::transports.SetOnline (true);
			LogPrint (eLogInfo, "NetState: connected to internet with IPv6 capability");
		}

		if ((((int)newConnectivity & (int)NLM_CONNECTIVITY_IPV4_INTERNET) == 0)
			&& (((int)newConnectivity & (int)NLM_CONNECTIVITY_IPV6_INTERNET) == 0))
		{
			i2p::transport::transports.SetOnline (false);
			LogPrint (eLogInfo, "NetState: connected without internet access");
		}

		return S_OK;
	}

private:
	LONG m_ref;
};

void SubscribeToEvents ();
void UnSubscribeFromEvents ();

#endif
