/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TransportConnector.
 *
 * Defines the outbound transport-connect strategy used by Transports. Each
 * concrete connector owns one protocol's address selection and session-start
 * semantics; the manager iterates the peer's transport priority and asks the
 * matching strategy to start an attempt.
 *
 */

#ifndef TRANSPORT_CONNECTOR_H__
#define TRANSPORT_CONNECTOR_H__

#include <memory>

#include "NTCP2.h"
#include "RouterInfo.h"
#include "SSU2.h"
#include "util.h"

namespace i2p
{
namespace transport
{
	/**
	 * Inputs shared by outbound transport strategies.
	 *
	 * The context is deliberately a short-lived view: it never owns the
	 * router or the servers and must not outlive one ConnectToPeer call.
	 */
	struct TransportConnectContext
	{
		std::shared_ptr<const i2p::data::RouterInfo> router;
		NTCP2Server * ntcp2;
		SSU2Server * ssu2;
		bool checkReserved;

		bool IsReserved (const boost::asio::ip::address & host) const
		{
			return checkReserved && i2p::util::net::IsInReservedRange (host);
		}

		std::shared_ptr<const i2p::data::RouterInfo::Address> SelectNTCP2Address (
			i2p::data::RouterInfo::SupportedTransports transport) const
		{
			return transport == i2p::data::RouterInfo::eNTCP2V6
				? router->GetPublishedNTCP2V6Address ()
				: router->GetPublishedNTCP2V4Address ();
		}

		std::shared_ptr<const i2p::data::RouterInfo::Address> SelectSSU2Address (
			i2p::data::RouterInfo::SupportedTransports transport) const
		{
			return transport == i2p::data::RouterInfo::eSSU2V6 ? router->GetSSU2V6Address ()
																 : router->GetSSU2V4Address ();
		}
	};

	/**
	 * One outbound protocol strategy.
	 *
	 * `TryConnect` returns true only when this strategy started (or observed)
	 * a connection attempt. False means "not applicable for this address" and
	 * lets Transports try the next priority entry.
	 */
	class ITransportConnector
	{
	public:
		virtual ~ITransportConnector () = default;
		[[nodiscard]] virtual bool TryConnect (
			const TransportConnectContext & context,
			i2p::data::RouterInfo::SupportedTransports transport) const = 0;
	};

	class NTCP2Connector final : public ITransportConnector
	{
	public:
		[[nodiscard]] bool TryConnect (
			const TransportConnectContext & context,
			i2p::data::RouterInfo::SupportedTransports transport) const override
		{
			if (!context.ntcp2) return false;
			auto address = context.SelectNTCP2Address (transport);
			if (!address || context.IsReserved (address->host)) return false;
			auto session = std::make_shared<NTCP2Session> (*context.ntcp2, context.router, address);
			if (context.ntcp2->UsingProxy ())
				context.ntcp2->ConnectWithProxy (session);
			else
				context.ntcp2->Connect (session);
			return true;
		}
	};

	class SSU2Connector final : public ITransportConnector
	{
	public:
		[[nodiscard]] bool TryConnect (
			const TransportConnectContext & context,
			i2p::data::RouterInfo::SupportedTransports transport) const override
		{
			if (!context.ssu2) return false;
			auto address = context.SelectSSU2Address (transport);
			if (!address || context.IsReserved (address->host) || !address->IsReachableSSU ()) return false;
			if (context.ssu2->CreateSession (context.router, address)) return true;
			// CreateSession() also returns false when a session to the same
			// endpoint is already pending. Do not fall through to another
			// transport in that case: Java keeps one establishment state per
			// peer, and a second concurrent NTCP2 attempt would create a
			// duplicate connection to the same router.
			return !address->host.is_unspecified () && address->port
				&& context.ssu2->FindPendingOutgoingSession (
					boost::asio::ip::udp::endpoint (address->host, static_cast<uint16_t> (address->port)));
		}
	};

	class YggdrasilNTCP2Connector final : public ITransportConnector
	{
	public:
		[[nodiscard]] bool TryConnect (
			const TransportConnectContext & context,
			i2p::data::RouterInfo::SupportedTransports /* transport */) const override
		{
			if (!context.ntcp2) return false;
			auto address = context.router->GetYggdrasilAddress ();
			if (!address) return false;
			auto session = std::make_shared<NTCP2Session> (*context.ntcp2, context.router, address);
			context.ntcp2->Connect (session);
			return true;
		}
	};

	[[nodiscard]] inline const ITransportConnector * FindTransportConnector (
		i2p::data::RouterInfo::SupportedTransports transport)
	{
		static const NTCP2Connector ntcp2;
		static const SSU2Connector ssu2;
		static const YggdrasilNTCP2Connector yggdrasil;
		switch (transport)
		{
		case i2p::data::RouterInfo::eNTCP2V4:
		case i2p::data::RouterInfo::eNTCP2V6: return &ntcp2;
		case i2p::data::RouterInfo::eSSU2V4:
		case i2p::data::RouterInfo::eSSU2V6: return &ssu2;
		case i2p::data::RouterInfo::eNTCP2V6Mesh: return &yggdrasil;
		default: return nullptr;
		}
	}
} // transport
} // i2p

#endif
