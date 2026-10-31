/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: transports pre-start send guard.
 *
 * A component may try to send before Transports::Start() created its
 * io_context. The online flag defaults to true, so the guard must test
 * the service itself; otherwise boost::asio::post dereferences a null
 * unique_ptr and the process crashes. Both send entry points must report
 * an invalid future and drop the message, exactly like the offline path.
 *
 */

#include <cassert>
#include <cstdint>
#include <list>
#include <memory>

#include "Identity.h"
#include "I2NPProtocol.h"
#include "Transports.h"

int main ()
{
	i2p::transport::Transports & transports = i2p::transport::transports;

	// Transports are online by default but not running yet: Start() is
	// what creates the service, and this test never calls it.
	assert (transports.IsOnline ());
	assert (!transports.IsRunning ());

	i2p::data::IdentHash peer;
	peer.Fill (0x11);

	auto message = i2p::NewI2NPShortMessage ();
	assert (message);

	auto single = transports.SendMessage (peer, message);
	assert (!single.valid ());

	std::list<std::shared_ptr<i2p::I2NPMessage>> batch;
	batch.push_back (message);
	auto many = transports.SendMessages (peer, std::move (batch));
	assert (!many.valid ());

	// An explicit offline switch keeps the same contract.
	transports.SetOnline (false);
	assert (!transports.SendMessage (peer, message).valid ());

	return 0;
}
