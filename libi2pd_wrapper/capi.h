/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: capi.
 *
 * C entry points expose the embedded router lifecycle and a transport
 * peer test to cgo, SWIG and other non-C++ callers.
 *
 */

#ifndef CAPI_H__
#define CAPI_H__

#ifdef __cplusplus
extern "C"
{
#endif

	// The embedding process controls one router lifecycle. Initialize
	// before starting, and stop the router before releasing crypto.
	void C_InitI2P (int argc, char * argv[], const char * appName);
	void C_TerminateI2P ();
	// The wrapper selects the application log file in the data directory.
	void C_StartI2P ();
	void C_StopI2P ();
	// Request a transport peer test after startup and any UPnP setup.
	void C_RunPeerTest ();

#ifdef __cplusplus
}
#endif

#endif
