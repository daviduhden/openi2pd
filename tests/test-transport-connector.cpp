/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test transport connector strategy.
 *
 * Verifies the static transport-to-strategy mapping and the null-server
 * short-circuit without opening a socket or creating a session. The test is
 * prepared for manual CI.
 */

#include <cassert>
#include <type_traits>

#include <boost/asio/ip/address.hpp>

#include "TransportConnector.h"

namespace
{
using i2p::data::RouterInfo;
}

int main ()
{
	static_assert (std::is_abstract_v<i2p::transport::ITransportConnector>);

	const auto * ntcp2v4 = i2p::transport::FindTransportConnector (RouterInfo::eNTCP2V4);
	const auto * ntcp2v6 = i2p::transport::FindTransportConnector (RouterInfo::eNTCP2V6);
	const auto * ssu2v4 = i2p::transport::FindTransportConnector (RouterInfo::eSSU2V4);
	const auto * ssu2v6 = i2p::transport::FindTransportConnector (RouterInfo::eSSU2V6);
	const auto * mesh = i2p::transport::FindTransportConnector (RouterInfo::eNTCP2V6Mesh);

	assert (ntcp2v4 != nullptr);
	assert (ntcp2v4 == ntcp2v6);
	assert (ssu2v4 != nullptr);
	assert (ssu2v4 == ssu2v6);
	assert (mesh != nullptr);
	assert (mesh != ntcp2v4);
	assert (i2p::transport::FindTransportConnector (static_cast<RouterInfo::SupportedTransports> (0x7fff))
		== nullptr);

	i2p::transport::TransportConnectContext context { nullptr, nullptr, nullptr, false };
	assert (!context.IsReserved (boost::asio::ip::make_address ("127.0.0.1")));
	assert (!ntcp2v4->TryConnect (context, RouterInfo::eNTCP2V4));
	assert (!ssu2v6->TryConnect (context, RouterInfo::eSSU2V6));
	assert (!mesh->TryConnect (context, RouterInfo::eNTCP2V6Mesh));

	i2p::transport::TransportConnectContext checked { nullptr, nullptr, nullptr, true };
	assert (checked.IsReserved (boost::asio::ip::make_address ("127.0.0.1")));

	return 0;
}
