/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: fixed-authority router persistence interface.
 * Keeps protocol code independent of worker and platform transports.
 */
#ifndef I2PD_PERSISTENT_STORAGE_H
#define I2PD_PERSISTENT_STORAGE_H
#include <cstdint>
#include <functional>
#include <span>
namespace i2p::data
{
enum class RouterPersistentFile : uint8_t { RouterKeys = 1, RouterInfo, NTCP2Keys, SSU2Keys };
using RouterPersistenceWriter = std::function<bool (RouterPersistentFile, std::span<const uint8_t>)>;
// Install before router initialization. An installed writer failure never falls
// back to a filesystem write in the network process. Replacement is synchronized.
void InstallRouterPersistenceWriter (RouterPersistenceWriter writer);
bool WriteRouterPersistentFile (RouterPersistentFile file, std::span<const uint8_t> bytes);
const char * RouterPersistentFileName (RouterPersistentFile file);
}
#endif
