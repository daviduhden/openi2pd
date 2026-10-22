/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: i2pd.
 *
 * Process entry point: a QT build delegates to RunQT, otherwise a validated
 * --privsep-worker invocation is dispatched to the isolated worker runner and
 * normal execution runs the Daemon init/start/run/stop sequence. The Windows
 * WinMain wrapper forwards to main.
 *
 */

#include <stdlib.h>
#include <string_view>
#include "Daemon.h"
#ifndef _WIN32
#include "privsep/RunWorker.h"
#endif

#if defined(QT_GUI_LIB)
namespace i2p
{
namespace qt
{
	int RunQT (int argc, char * argv[]);
}
}

namespace
{
int RunI2PD (int argc, char * argv[]) { return i2p::qt::RunQT (argc, argv); }
} // namespace
#else
namespace
{
int RunI2PD (int argc, char * argv[])
{
#ifndef _WIN32
	if (argc == 4 && std::string_view (argv[1]) == "--privsep-worker")
		return i2p::privsep::RunWorker (argv[2], argv[3]);
#endif
	if (!Daemon.init (argc, argv)) return EXIT_FAILURE;
	if (!Daemon.start ()) return EXIT_FAILURE;
	Daemon.run ();
	Daemon.stop ();
	return EXIT_SUCCESS;
}
} // namespace
#endif

int main (int argc, char * argv[])
{
	return RunI2PD (argc, argv);
}

#ifdef _WIN32
#include <windows.h>

int CALLBACK WinMain (
	_In_ HINSTANCE /*hInstance*/, _In_ HINSTANCE /*hPrevInstance*/, _In_ LPSTR /*lpCmdLine*/, _In_ int /*nCmdShow*/)
{
	return RunI2PD (__argc, __argv);
}
#endif
