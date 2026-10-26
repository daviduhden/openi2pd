/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test tunnel inbound queue bound.
 *
 * The tunnel worker queue is bounded so a stalled worker cannot let producers
 * grow memory without limit. Deterministic: the bound and the drop counter are
 * checked without starting the worker thread.
 *
 */

#include <cassert>
#include <cstdint>
#include <list>
#include <memory>

#include "I2NPProtocol.h"
#include "Tunnel.h"
#include "TunnelBase.h"

namespace
{
std::shared_ptr<i2p::I2NPMessage> MakeMessage ()
{
	auto msg = i2p::NewI2NPShortMessage ();
	msg->FillI2NPMessageHeader (i2p::eI2NPData, 1);
	return msg;
}

void TestQueueBound ()
{
	using namespace i2p::tunnel;
	assert (TUNNEL_MESSAGE_QUEUE_MAX_SIZE > 0);
	assert (IsTunnelQueueAdmissionAllowed (0));
	assert (IsTunnelQueueAdmissionAllowed (TUNNEL_MESSAGE_QUEUE_MAX_SIZE - 1));
	// The limit itself is already a full queue.
	assert (!IsTunnelQueueAdmissionAllowed (TUNNEL_MESSAGE_QUEUE_MAX_SIZE));
	assert (!IsTunnelQueueAdmissionAllowed (TUNNEL_MESSAGE_QUEUE_MAX_SIZE + 1024));

	assert (tunnels.GetTunnelMessageQueueLimit () == TUNNEL_MESSAGE_QUEUE_MAX_SIZE);
}

void TestStoppedServiceRejectsWithoutCounting ()
{
	using namespace i2p::tunnel;
	tunnels.Stop ();
	assert (tunnels.GetNumDroppedTunnelMessages () == 0);
	assert (tunnels.GetQueueSize () == 0);

	// A stopped service rejects posts; that is not a queue overflow, so the
	// drop counter must stay untouched.
	assert (!tunnels.PostTunnelData (MakeMessage ()));
	std::list<std::shared_ptr<i2p::I2NPMessage>> batch { MakeMessage (), MakeMessage () };
	assert (!tunnels.PostTunnelData (batch));
	assert (batch.empty ()); // the list overload consumes its input
	assert (tunnels.GetNumDroppedTunnelMessages () == 0);
	assert (tunnels.GetQueueSize () == 0);
}
}

int main ()
{
	TestQueueBound ();
	TestStoppedServiceRejectsWithoutCounting ();
	return 0;
}
