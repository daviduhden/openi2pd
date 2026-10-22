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
class RouterStorageClient
{
public:
	RouterStorageClient ();
	~RouterStorageClient ();
	bool Start (const std::filesystem::path & root, std::string & error);
	bool Write (i2p::data::RouterPersistentFile file, std::span<const uint8_t> bytes);
	void Stop ();

private:
	struct Impl;
	std::unique_ptr<Impl> m_Impl;
};
int RunRouterStorageWorker (int fd, int inheritedRoot = -1);
}
#endif
