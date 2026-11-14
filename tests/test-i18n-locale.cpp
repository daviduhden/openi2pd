/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test i18n locale.
 *
 * Exercises the process-local language registry, UTF-8 catalog values,
 * formatting and plural fallback.
 *
 * Terminal rendering and Android resources have separate tests.
 */

#include "I18N_langs.h"
#include <array>
#include <cassert>
#include <clocale>
#include <cstdlib>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "I18N.h"

namespace
{
std::optional<std::string> ReadEnvironment (const char * name)
{
#ifdef _MSC_VER
	char * raw = nullptr;
	size_t length = 0;
	assert (_dupenv_s (&raw, &length, name) == 0);
	std::unique_ptr<char, decltype (&std::free)> owned (raw, &std::free);
	if (owned) return std::string (owned.get ());
#else
	if (const auto * value = std::getenv (name)) return std::string (value);
#endif
	return std::nullopt;
}

void WriteEnvironment (const char * name, const char * value)
{
#ifdef _WIN32
	assert (_putenv_s (name, value ? value : "") == 0);
#else
	assert ((value ? setenv (name, value, 1) : unsetenv (name)) == 0);
#endif
}

// Isolate environment mutation before the concurrent selection test.
// Restore all three values to avoid leaking fixture policy.
class EnvironmentScope
{
	struct Entry
	{
		const char * name;
		std::optional<std::string> value;
	};
	std::array<Entry, 3> m_Entries { Entry { "LC_ALL", ReadEnvironment ("LC_ALL") },
		Entry { "LC_MESSAGES", ReadEnvironment ("LC_MESSAGES") }, Entry { "LANG", ReadEnvironment ("LANG") } };

public:
	~EnvironmentScope ()
	{
		for (const auto & entry : m_Entries)
			WriteEnvironment (entry.name, entry.value ? entry.value->c_str () : nullptr);
	}
};

void TestEnvironmentPrecedence ()
{
	EnvironmentScope restore;
	WriteEnvironment ("LC_ALL", nullptr);
	WriteEnvironment ("LC_MESSAGES", nullptr);
	WriteEnvironment ("LANG", "ru.UTF-8");
	// This must be the first registry access: tag lookup and translation
	// initialize from the same environment before explicit selection.
	assert (i2p::i18n::GetLanguageTag () == "ru");
	assert (i2p::i18n::tr ("Status") == "Статус");
	WriteEnvironment ("LC_MESSAGES", "es.UTF-8");
	i2p::i18n::SetLanguage ("");
	assert (i2p::i18n::GetLanguageTag () == "es");
	WriteEnvironment ("LC_ALL", "zh_CN.UTF-8");
	i2p::i18n::SetLanguage ("");
	assert (i2p::i18n::GetLanguageTag () == "zh-Hans");
	// An explicit selection overrides all environment variables.
	i2p::i18n::SetLanguage ("en");
	assert (i2p::i18n::GetLanguageTag () == "en");
	// C and unsupported explicit locales select the English fallback.
	WriteEnvironment ("LC_ALL", "C.UTF-8");
	i2p::i18n::SetLanguage ("");
	assert (i2p::i18n::GetLanguageTag () == "en");
	i2p::i18n::SetLanguage ("unsupported");
	assert (i2p::i18n::GetLanguageTag () == "en");
	assert (std::string_view (std::localeconv ()->decimal_point) == ".");
}

void TestLocaleSpellings ()
{
	for (const auto & [input, expected] : std::initializer_list<std::pair<const char *, const char *>> {
			 { "es.UTF-8", "es" }, { "es_ES.UTF-8@euro", "es" }, { "ru.UTF-8", "ru" }, { "RU_ru.KOI8-R", "ru" },
			 { "zh.UTF-8", "zh-Hans" }, { "zh_CN.UTF-8", "zh-Hans" }, { "zh-Hans", "zh-Hans" }, { "zh-SG", "zh-Hans" },
			 { "zh-Hant", "en" }, { "zh-Hant-CN", "en" }, { "zh-TW", "en" }, { "zh-HK", "en" }, { "zh-MO", "en" },
			 { "zh-Hans-TW", "zh-Hans" }, { "zh-Hans-HK", "zh-Hans" }, { "zh-CN-Hant", "en" }, { "zh-HansBogus", "en" },
			 { "zh-CNonsense", "en" }, { "zh-Hans-Hant", "en" }, { "en_US.UTF-8", "en" } })
	{
		i2p::i18n::SetLanguage (input);
		assert (i2p::i18n::GetLanguageTag () == expected);
	}
	for (const auto * input : { "zh@traditional", "zh_CN.UTF-8@hant" })
	{
		i2p::i18n::SetLanguage (input);
		assert (i2p::i18n::GetLanguageTag () == "en");
	}
}

void TestConcurrentSelection ()
{
	constexpr std::array<const char *, 4> languages { "en", "es", "ru", "zh-Hans" };
	std::vector<std::jthread> workers;
	for (const auto * language : languages)
		workers.emplace_back (
			[language]
			{
				for (int iteration = 0; iteration < 128; ++iteration)
				{
					i2p::i18n::SetLanguage (language);
					const auto tag = i2p::i18n::GetLanguageTag ();
					assert (tag == "en" || tag == "es" || tag == "ru" || tag == "zh-Hans");
					const auto status = i2p::i18n::tr ("Status");
					assert (status == "Status" || status == "Estado" || status == "Статус" || status == "状态");
					assert (i2p::i18n::tr ("unknown-key") == "unknown-key");
					const auto nodes = i2p::i18n::ntr ("node", "nodes", 2);
					assert (nodes == "nodes" || nodes == "nodos" || nodes == "узла" || nodes == "节点");
				}
			});
	// Every jthread joins before later locale assertions or process exit.
}
}

int main ()
{
	TestEnvironmentPrecedence ();
	TestLocaleSpellings ();
	TestConcurrentSelection ();
	i2p::i18n::SetLanguage ("es");
	assert (i2p::i18n::GetLanguageTag () == "es");
	assert (i2p::i18n::tr ("Status") == "Estado");
	assert (i2p::i18n::tr ("Peers") == "Pares");

	i2p::i18n::SetLanguage ("ru-RU");
	assert (i2p::i18n::GetLanguageTag () == "ru");
	assert (i2p::i18n::tr ("Status") == "Статус");
	assert (i2p::i18n::tr ("Tunnels") == "Туннели");
	assert (i2p::i18n::tr ("Uptime") == "Время работы");
	// Russian uses one/few/many instead of English's two-form fallback.
	assert (i2p::i18n::ntr ("node", "nodes", 1) == "узел");
	assert (i2p::i18n::ntr ("node", "nodes", 2) == "узла");
	assert (i2p::i18n::ntr ("node", "nodes", 5) == "узлов");
	assert (i2p::i18n::ntr ("node", "nodes", 21) == "узел");
	assert (i2p::i18n::ntr ("node", "nodes", 11) == "узлов");
	assert (i2p::i18n::ntr ("node", "nodes", 0) == "узлов");
	assert (i2p::i18n::ntr ("node", "nodes", 12) == "узлов");
	assert (i2p::i18n::ntr ("node", "nodes", 22) == "узла");
	assert (i2p::i18n::ntr ("node", "nodes", 101) == "узел");
	assert (i2p::i18n::ntr ("node", "nodes", 111) == "узлов");
	assert (i2p::i18n::ntr ("node", "nodes", -1) == "узел");
	assert (i2p::i18n::ntr ("node", "nodes", -2) == "узла");
	assert (i2p::i18n::ntr ("node", "nodes", -5) == "узлов");
	assert (i2p::i18n::ntr ("node", "nodes", -21) == "узел");
	assert (i2p::i18n::ntr ("node", "nodes", std::numeric_limits<int>::min ()) == "узлов");

	i2p::i18n::SetLanguage ("zh_CN");
	assert (i2p::i18n::GetLanguageTag () == "zh-Hans");
	assert (i2p::i18n::tr ("Status") == "状态");
	assert (i2p::i18n::tr ("Tunnels") == "隧道");
	assert (i2p::i18n::tr ("Uptime") == "运行时间");
	// Simplified Chinese has one plural form for every count.
	assert (i2p::i18n::ntr ("node", "nodes", 1) == "节点");
	assert (i2p::i18n::ntr ("node", "nodes", 5) == "节点");

	i2p::i18n::SetLanguage ("zh-TW");
	assert (i2p::i18n::GetLanguageTag () == "en");
	assert (i2p::i18n::tr ("Status") == "Status");

	i2p::i18n::SetLanguage ("zz-ZZ");
	assert (i2p::i18n::tr ("Status") == "Status");
	assert (i2p::i18n::tr ("unknown-key") == "unknown-key");
	assert (i2p::i18n::ntr ("one item", "%d items", 1) == "one item");
	assert (i2p::i18n::ntr ("one item", "%d items", 2) == "%d items");
	assert (i2p::i18n::tr ("%s/%d%%", "x", 5) == "x/5%");
	// Global and qualified UI helpers share the historical overload set.
	assert (::tr ("Status") == i2p::i18n::tr ("Status"));
	assert (::tr ("%s/%d%%", "x", 5) == "x/5%");
	assert (::ntr ("one item", "%d items", 2, 2) == "2 items");

	// Catalog values are UTF-8 literals, not locale-converted bytes.
	i2p::i18n::SetLanguage ("es");
	assert (i2p::i18n::tr ("Uptime") == "Tiempo activo");
	assert (i2p::i18n::ntr ("node", "nodes", -1) == "nodo");
	assert (i2p::i18n::ntr ("node", "nodes", -2) == "nodos");
	assert (i2p::i18n::ntr ("node", "nodes", std::numeric_limits<int>::min ()) == "nodos");
	assert (i2p::i18n::tr ("unknown-key") == "unknown-key");
	// Localized placeholder strings retain the exact printf conversions.
	i2p::i18n::SetLanguage ("es");
	assert (i2p::i18n::tr ("Remove \"%s\"? [y/N] ", "x") == "¿Eliminar \"x\"? [s/N] ");
	i2p::i18n::SetLanguage ("ru");
	assert (i2p::i18n::tr ("Remove \"%s\"? [y/N] ", "x") == "Удалить \"x\"? [д/Н] ");
	return 0;
}
