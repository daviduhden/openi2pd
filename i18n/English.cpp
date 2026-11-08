/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: English.
 *
 * Defines the built-in English locale, returning source strings
 * unchanged. Plural fallback uses singular and plural text as keys.
 */

#include <map>
#include <vector>
#include <string>
#include <memory>
#include "I18N.h"

// English text is the canonical lookup key and fallback source catalog.
// There is no need to duplicate every UI literal in a separate table.

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
			return n != 1 && n != -1 ? 1 : 0;
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
