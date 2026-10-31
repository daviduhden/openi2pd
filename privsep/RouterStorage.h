/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: fixed-name isolated router persistence transport.
 */
#ifndef I2PD_PRIVSEP_ROUTER_STORAGE_H
#define I2PD_PRIVSEP_ROUTER_STORAGE_H
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include "../libi2pd/PersistentStorage.h"
namespace i2p::privsep
{
/// Owns the worker and validated directory capability for fixed router
/// files. Calls are serialized. A lost response makes a write's outcome
/// ambiguous; the client may restart a worker for a later call but
/// never replays that write. OpenBSD cannot respawn this worker after
/// runtime pledge and fails closed.
/// Scope: exactly the four fixed names (`router.keys`, `router.info`,
/// `ntcp2.keys`, `ssu2.keys`). A request carries no path field, so the
/// worker cannot be redirected; netDb RouterInfo files, peer profiles,
/// destination keys, the address book and the configuration stay with
/// their own writers. This class is not thread-safe for its own
/// lifetime management: Start, Write and Stop serialize on one internal
/// mutex, but the object must not be destroyed while another thread is
/// inside a call. Stop (also run by the destructor) closes the retained
/// root and the channel.
class RouterStorageClient
{
public:
	RouterStorageClient ();
	~RouterStorageClient ();
	/// Open and validate the administrator-selected root and configure a
	/// worker.
	/// The root is resolved to an absolute, non-symlink directory that
	/// must be owned by the worker uid and free of group/world write
	/// permission; a core without a configured worker identity is refused
	/// instead of falling back to an unseparated writer.
	/// @param error Receives a non-secret failure description; cleared on
	/// entry.
	/// @return true only when the worker accepted its configuration.
	bool Start (const std::filesystem::path & root, std::string & error);
	/// Atomically replace one of the fixed router files with 1..32768
	/// bytes.
	/// @return true on worker confirmation; false may include a lost reply
	/// after the filesystem change, so the caller must not assume
	/// rollback.
	bool Write (i2p::data::RouterPersistentFile file, std::span<const uint8_t> bytes);
	/// Close the retained root capability and stop or detach the worker.
	void Stop ();

private:
	struct Impl;
	std::unique_ptr<Impl> m_Impl;
};
/// Serve fixed-name writes on the worker channel until shutdown or
/// failure.
int RunRouterStorageWorker (int fd, int inheritedRoot = -1);
}
#endif
