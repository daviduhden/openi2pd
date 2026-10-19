/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: capi.
 *
 * Implements the C entry points by forwarding to i2p::api, keeping C++ types out
 * of the exported ABI and discarding the optional log stream in C_StartI2P.
 *
 */

#include "../libi2pd/api.h"
#include "capi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#ifdef __cplusplus
extern "C"
{
#endif

	void C_InitI2P (int argc, char * argv[], const char * appName)
	{
		std::cout << argv;
		return i2p::api::InitI2P (argc, argv, appName);
	}

	void C_TerminateI2P ()
	{
		return i2p::api::TerminateI2P ();
	}

	void C_StartI2P ()
	{
		std::shared_ptr<std::ostream> logStream;
		return i2p::api::StartI2P (logStream);
	}

	void C_StopI2P ()
	{
		return i2p::api::StopI2P ();
	}

	void C_RunPeerTest ()
	{
		return i2p::api::RunPeerTest ();
	}

#ifdef __cplusplus
}
#endif
