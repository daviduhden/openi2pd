/*
* Copyright (c) 2013-2025, The PurpleI2P Project
* Copyright (c) 2026, David Uhden Collado
* SPDX-License-Identifier: BSD-3-Clause
*
* This file is part of openi2pd and licensed under BSD-3-Clause
*
* See full license text in LICENSE file at top of project tree
*/

#ifndef WIN32APP_H__
#define WIN32APP_H__

#define I2PD_WIN32_CLASSNAME "i2pd main window"

namespace i2p
{
namespace win32
{
	bool StartWin32App (bool isWinService);
	void StopWin32App ();
	int RunWin32App ();
	bool GracefulShutdown ();
	bool StopGracefulShutdown ();
	int GetGracefulShutdownRemainingTime ();
}
}
#endif // WIN32APP_H__
