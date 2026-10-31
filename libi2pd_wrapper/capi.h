/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: capi.
 *
 * C entry points expose lifecycle and peer tests after UPnP.
 * They serve cgo, SWIG and other non-C++ callers.
 *
 */

#ifndef CAPI_H__
#define CAPI_H__

#ifdef __cplusplus
extern "C"
{
#endif

	// initialization start and stop
	void C_InitI2P (int argc, char * argv[], const char * appName);
	//void C_InitI2P (int argc, char** argv, const char * appName);
	void C_TerminateI2P ();
	void C_StartI2P ();
	// Writes the system log to logStream, or <appName>.log by default.
	void C_StopI2P ();
	void C_RunPeerTest (); // should be called after UPnP

#ifdef __cplusplus
}
#endif

#endif
