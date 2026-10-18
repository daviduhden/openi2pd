/*
* Copyright (c) 2021-2025, The PurpleI2P Project
* Copyright (c) 2026, David Uhden Collado
* SPDX-License-Identifier: BSD-3-Clause
*
* This file is part of openi2pd and licensed under BSD-3-Clause
*
* See full license text in LICENSE file at top of project tree
*/

#include <clocale>
#include "ClientContext.h"
#include "I18N_langs.h"
#include "I18N.h"

namespace i2p
{
namespace i18n
{
	void SetLanguage(const std::string&)
	{
		i2p::client::context.SetLanguage (english::GetLocale());
		setlocale(LC_NUMERIC, "C");
	}

	std::string_view translate (std::string_view arg)
	{
		return i2p::client::context.GetLanguage ()->GetString (arg);
	}

	std::string translate (const std::string& arg, const std::string& arg2, const int n)
	{
		return i2p::client::context.GetLanguage ()->GetPlural (arg, arg2, n);
	}
} // i18n
} // i2p
