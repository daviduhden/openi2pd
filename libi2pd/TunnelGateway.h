/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: TunnelGateway.
 *
 * Declares the tunnel gateway buffer that packs I2NP messages and
 * random padding into tunnel data messages, and TunnelGateway, which
 * queues and sends those messages through its tunnel.
 *
 */

#ifndef TUNNEL_GATEWAY_H__
#define TUNNEL_GATEWAY_H__

#include <inttypes.h>
#include <array>
#include <vector>
#include <memory>
#include <random>
#include "I2NPProtocol.h"
#include "TunnelBase.h"

namespace i2p
{
namespace tunnel
{
	/// Packs delivery instructions and I2NP messages into tunnel-sized
	/// cells. Large messages span a first fragment and numbered follow-on
	/// fragments; each completed cell has nonzero padding, a delimiter, IV
	/// and checksum.
	/// @see https://i2p.net/en/docs/specs/tunnel-message/
	class TunnelGatewayBuffer
	{
	public:
		TunnelGatewayBuffer ();
		~TunnelGatewayBuffer ();
		/// Append a message, completing cells as needed.
		/// @pre block.data is non-null and contains a valid I2NP header.
		void PutI2NPMsg (const TunnelMessageBlock & block);
		const std::vector<std::shared_ptr<const I2NPMessage>> & GetTunnelDataMsgs () const { return m_TunnelDataMsgs; };
		void ClearTunnelDataMsgs ();
		void CompleteCurrentTunnelDataMessage ();

	private:
		void CreateCurrentTunnelDataMessage ();

	private:
		std::vector<std::shared_ptr<const I2NPMessage>> m_TunnelDataMsgs;
		std::shared_ptr<I2NPMessage> m_CurrentTunnelDataMsg;
		size_t m_RemainingSize;
		std::unique_ptr<std::array<uint8_t, TUNNEL_DATA_MAX_PAYLOAD_SIZE + 1>> m_NonZeroRandomBuffer;
		std::unique_ptr<std::mt19937> m_Rng;
	};

	/// Buffered sender borrowing the tunnel through which completed cells
	/// travel. The referenced TunnelBase must outlive the gateway.
	class TunnelGateway
	{
	public:
		TunnelGateway (TunnelBase & tunnel)
			: m_Tunnel (tunnel)
			, m_NumSentBytes (0) { };
		void SendTunnelDataMsg (const TunnelMessageBlock & block);
		void PutTunnelDataMsg (const TunnelMessageBlock & block);
		void SendBuffer ();
		size_t GetNumSentBytes () const { return m_NumSentBytes; };
		const std::unique_ptr<TunnelTransportSender> & GetSender () const { return m_Sender; };

	private:
		TunnelBase & m_Tunnel;
		TunnelGatewayBuffer m_Buffer;
		size_t m_NumSentBytes;
		std::unique_ptr<TunnelTransportSender> m_Sender;
	};
}
}

#endif
