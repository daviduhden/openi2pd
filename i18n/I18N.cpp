/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: I18N.
 *
 * Implements process-local language selection, string and plural
 * translation, environment fallback and LC_NUMERIC pinning. The
 * registry is independent of the router client context so the TUI and
 * tools can translate without linking libi2pdclient.
 *
 */

#include <algorithm>
#include <cctype>
#include <clocale>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>

#include "I18N_langs.h"
#include "I18N.h"

namespace i2p
{
namespace i18n
{
	namespace
	{
		std::shared_ptr<const Locale> g_Locale;
		std::string g_LanguageTag = "en";

		std::string Normalize (std::string_view tag)
		{
			std::string value;
			value.reserve (tag.size ());
			for (char ch : tag)
			{
				if (ch == '_')
					value.push_back ('-');
				else
					value.push_back (static_cast<char> (std::tolower (static_cast<unsigned char> (ch))));
			}
			return value;
		}

		std::string EnvironmentLanguage ()
		{
			for (const char * name : { "LC_ALL", "LC_MESSAGES", "LANG" })
			{
#ifdef _MSC_VER
				// The CRT marks getenv deprecated; _dupenv_s returns an owned
				// copy that is freed immediately after the value is copied.
				char * raw = nullptr;
				size_t rawLength = 0;
				if (_dupenv_s (&raw, &rawLength, name) == 0 && raw)
				{
					std::string value (raw);
					std::free (raw);
					if (!value.empty ()) return value;
				}
#else
				const char * value = std::getenv (name);
				if (value && *value) return value;
#endif
			}
			return { };
		}

		const Locale & ActiveLocale ()
		{
			// Standalone tools do not call SetLanguage themselves. Resolve the
			// environment once on first use so LC_ALL/LC_MESSAGES/LANG selects a
			// catalog without every tool duplicating the initialization.
			if (!g_Locale) SetLanguage ({ });
			if (g_Locale) return *g_Locale;
			static const auto fallback = english::GetLocale ();
			return *fallback;
		}
	} // namespace

	void SetLanguage (const std::string & tag)
	{
		const std::string requested = Normalize (tag.empty () ? EnvironmentLanguage () : tag);
		if (requested.empty () || requested == "en" || requested == "en-us" || requested == "en-gb"
			|| requested.starts_with ("en-"))
		{
			g_Locale = english::GetLocale ();
			g_LanguageTag = "en";
		}
		else if (requested == "es" || requested.starts_with ("es-"))
		{
			g_Locale = spanish::GetLocale ();
			g_LanguageTag = "es";
		}
		else if (requested == "ru" || requested.starts_with ("ru-"))
		{
			g_Locale = russian::GetLocale ();
			g_LanguageTag = "ru";
		}
		else if (requested == "zh" || requested.starts_with ("zh-hans") || requested.starts_with ("zh-cn")
			|| requested.starts_with ("zh-sg"))
		{
			g_Locale = chinese_simplified::GetLocale ();
			g_LanguageTag = "zh-Hans";
		}
		else
		{
			// Traditional Chinese and unknown locales intentionally fall back to
			// English rather than showing Simplified glyphs as Traditional text.
			g_Locale = english::GetLocale ();
			g_LanguageTag = "en";
		}
		setlocale (LC_NUMERIC, "C");
	}

	std::string GetLanguageTag ()
	{
		return g_LanguageTag;
	}

	std::string_view translate (std::string_view arg)
	{
		return ActiveLocale ().GetString (arg);
	}

	std::string translate (const std::string & arg, const std::string & arg2, const int n)
	{
		return ActiveLocale ().GetPlural (arg, arg2, n);
	}
} // i18n
} // i2p
