/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: transport-independent router persistence dispatch.
 */
#include "PersistentStorage.h"
#include "FS.h"
#include <fstream>
#include <mutex>
namespace i2p::data
{
namespace
{
	std::mutex writerMutex;
	RouterPersistenceWriter installedWriter;
}
const char * RouterPersistentFileName (RouterPersistentFile file)
{
	switch (file)
	{
	case RouterPersistentFile::RouterKeys: return "router.keys";
	case RouterPersistentFile::RouterInfo: return "router.info";
	case RouterPersistentFile::NTCP2Keys: return "ntcp2.keys";
	case RouterPersistentFile::SSU2Keys: return "ssu2.keys";
	}
	return nullptr;
}
void InstallRouterPersistenceWriter (RouterPersistenceWriter writer)
{
	std::lock_guard lock (writerMutex);
	installedWriter = std::move (writer);
}
bool WriteRouterPersistentFile (RouterPersistentFile file, std::span<const uint8_t> bytes)
{
	const auto name = RouterPersistentFileName (file);
	if (!name || bytes.empty () || bytes.size () > 32768) return false;
	RouterPersistenceWriter writer;
	{
		std::lock_guard lock (writerMutex);
		writer = installedWriter;
	}
	if (writer) return writer (file, bytes);
	std::ofstream output (i2p::fs::CreatePrivateFile (i2p::fs::DataDirPath (name)), std::ios::binary | std::ios::out);
	output.write (reinterpret_cast<const char *> (bytes.data ()), bytes.size ());
	output.close ();
	return !output.fail ();
}
}
