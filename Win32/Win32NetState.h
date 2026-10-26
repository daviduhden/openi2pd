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

/**
 * COM sink for Windows Network List Manager connectivity events.
 *
 * The object is created with a reference count of one, advised on the
 * `INetworkListManagerEvents` connection point and released by
 * UnSubscribeFromEvents(); Release() deletes the instance at zero, so it must
 * never live on the stack. Callbacks arrive on the COM apartment of the thread
 * that called SubscribeToEvents (the GUI/message-loop thread); they only publish
 * the transport online flag and never touch router sessions, sockets or
 * configuration.
 *
 * Only ConnectivityChanged is implemented; cost/usage, proxy and per-network
 * changes are not observed. The sink tracks capability bits rather than
 * interface identity, so any IPv4 or IPv6 Internet capability marks the router
 * online, and the loss of both marks it offline.
 */
class CNetworkListManagerEvent final : public INetworkListManagerEvents
{
public:
	CNetworkListManagerEvent ()
		: m_ref (1)
	{
	}
	~CNetworkListManagerEvent () { }

	/** Standard COM identity: only IUnknown and INetworkListManagerEvents (with AddRef). */
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

	/**
	 * Instance-count management. Release() deletes the object at zero, so the
	 * count must be balanced exactly once by the subscriber.
	 */
	ULONG STDMETHODCALLTYPE AddRef () { return (ULONG)InterlockedIncrement (&m_ref); }

	ULONG STDMETHODCALLTYPE Release ()
	{
		LONG Result = InterlockedDecrement (&m_ref);
		if (Result == 0) delete this;
		return (ULONG)Result;
	}

	/**
	 * Map one connectivity observation onto the transport online flag.
	 *
	 * IPv4 and/or IPv6 Internet capability means online; no Internet capability
	 * (including a plain disconnected state) means offline. Called on the
	 * subscribing thread; it performs no blocking or router work.
	 *
	 * @param newConnectivity NLM_CONNECTIVITY bit set for the current state.
	 * @return S_OK always; the SCM/COM layer has no failure path to report.
	 */
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

/**
 * Create the Network List Manager COM object, publish its current state and
 * advise the connectivity sink.
 *
 * Initializes COM on the calling thread (STA) and stores the objects in
 * file-scope pointers that UnSubscribeFromEvents() releases. A partial failure is
 * logged and leaves the transport online state untouched; there is no retry and
 * no second subscription. Must be called once, from the thread that runs the
 * message loop, and never in service mode.
 */
void SubscribeToEvents ();
/**
 * Unadvise the connectivity sink and release every COM object and the apartment.
 *
 * Releasing the sink is what deletes it (reference count one), so the pointers
 * must be considered dangling afterwards; this function is not idempotent and
 * must be called at most once per SubscribeToEvents(). It does not reset the
 * transport online flag; transports shutdown owns that state.
 */
void UnSubscribeFromEvents ();

#endif
