/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test i18n locale.
 *
 * Exercises the process-local language registry, fallback, UTF-8 catalog
 * values, formatting and plural fallback. It does not test terminal rendering
 * or Android resources.
 */

#include "I18N_langs.h"
#include <cassert>
#include <string>
#include <string_view>

#include "I18N.h"

int main ()
{
	i2p::i18n::SetLanguage ("es");
	assert (i2p::i18n::GetLanguageTag () == "es");
	assert (i2p::i18n::tr ("Status") == "Estado");
	assert (i2p::i18n::tr ("Peers") == "Pares");

	i2p::i18n::SetLanguage ("ru-RU");
	assert (i2p::i18n::GetLanguageTag () == "ru");
	assert (i2p::i18n::tr ("Status") == "Статус");
	assert (i2p::i18n::tr ("Tunnels") == "Туннели");

	i2p::i18n::SetLanguage ("zh_CN");
	assert (i2p::i18n::GetLanguageTag () == "zh-Hans");
	assert (i2p::i18n::tr ("Status") == "状态");
	assert (i2p::i18n::tr ("Tunnels") == "隧道");

	i2p::i18n::SetLanguage ("zh-TW");
	assert (i2p::i18n::GetLanguageTag () == "en");
	assert (i2p::i18n::tr ("Status") == "Status");

	i2p::i18n::SetLanguage ("zz-ZZ");
	assert (i2p::i18n::tr ("Status") == "Status");
	assert (i2p::i18n::tr ("unknown-key") == "unknown-key");
	assert (i2p::i18n::ntr ("one item", "%d items", 1) == "one item");
	assert (i2p::i18n::ntr ("one item", "%d items", 2) == "%d items");
	assert (i2p::i18n::tr ("%s/%d%%", "x", 5) == "x/5%");
	// Historical global helpers and qualified UI helpers share one overload set.
	assert (::tr ("Status") == i2p::i18n::tr ("Status"));
	assert (::tr ("%s/%d%%", "x", 5) == "x/5%");
	assert (::ntr ("one item", "%d items", 2, 2) == "2 items");

	// Catalog values remain byte-identical UTF-8, not locale-converted.
	i2p::i18n::SetLanguage ("es");
	assert (i2p::i18n::tr ("Español") == "Español");
	assert (i2p::i18n::tr ("Русский") == "Русский");
	return 0;
}
