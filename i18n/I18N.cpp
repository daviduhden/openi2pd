/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: I18N.
 *
 * Implements language selection, string and plural translation through the
 * active client locale, and forces LC_NUMERIC to the C locale.
 *
 */

#include <clocale>
#include "ClientContext.h"
#include "I18N_langs.h"
#include "I18N.h"

namespace i2p
{
namespace i18n
{
	void SetLanguage (const std::string &)
	{
		i2p::client::context.SetLanguage (english::GetLocale ());
		setlocale (LC_NUMERIC, "C");
	}

	std::string_view translate (std::string_view arg)
	{
		return i2p::client::context.GetLanguage ()->GetString (arg);
	}

	std::string translate (const std::string & arg, const std::string & arg2, const int n)
	{
		return i2p::client::context.GetLanguage ()->GetPlural (arg, arg2, n);
	}
} // i18n
} // i2p
