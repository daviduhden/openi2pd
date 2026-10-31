/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: English.
 *
 * Defines the built-in English locale, which returns source strings
 * unchanged and supplies fallback plural forms keyed by singular and
 * plural text.
 */

#include <map>
#include <vector>
#include <string>
#include <memory>
#include "I18N.h"

// English localization file
// This is an example translation file without strings in it.

namespace i2p
{
namespace i18n
{
	namespace english // language namespace
	{
		// language name in lowercase
		static std::string language = "english";

		// See for language plural forms here:
		// Reference: Localization guide, plural forms and categories.
		static int plural (int n)
		{
			return n != 1 ? 1 : 0;
		}

		// Right to Left language?
		static bool rtl = false;

		static const LocaleStrings strings {
			{ "", "" },
		};

		static std::map<std::string, std::vector<std::string>> plurals {
			{ "", { "", "" } },
		};

		std::shared_ptr<const i2p::i18n::Locale> GetLocale ()
		{
			return std::make_shared<i2p::i18n::Locale> (
				language, rtl, strings, plurals, [] (int n) -> int { return plural (n); });
		}

	} // language
} // i18n
} // i2p
