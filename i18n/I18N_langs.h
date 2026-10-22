/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: I18N langs.
 *
 * Declares the locale accessor for each compiled-in language unit.
 *
 */

#ifndef __I18N_LANGS_H__
#define __I18N_LANGS_H__

#include "I18N.h"

namespace i2p
{
namespace i18n
{
	namespace english
	{
		std::shared_ptr<const Locale> GetLocale ();
	}
	namespace spanish
	{
		std::shared_ptr<const Locale> GetLocale ();
	}
	namespace russian
	{
		std::shared_ptr<const Locale> GetLocale ();
	}
	namespace chinese_simplified
	{
		std::shared_ptr<const Locale> GetLocale ();
	}

} // i18n
} // i2p

#endif // __I18N_LANGS_H__
