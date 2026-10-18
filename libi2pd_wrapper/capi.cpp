/*
* Copyright (c) 2021-2022, The PurpleI2P Project
* Copyright (c) 2026, David Uhden Collado
* SPDX-License-Identifier: BSD-3-Clause
*
* This file is part of openi2pd and licensed under BSD-3-Clause
*
* See full license text in LICENSE file at top of project tree
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
