/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: I18N.
 *
 * Implements process-local language selection, string and plural
 * translation, environment fallback and LC_NUMERIC pinning.
 *
 * The registry is independent of the router client context, allowing
 * the TUI and tools to translate without linking libi2pdclient.
 *
 */

#include <algorithm>
#include <cctype>
#include <clocale>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include "I18N_langs.h"
#include "I18N.h"
#include "AtomicSharedPtr.h"

namespace i2p
{
namespace i18n
{
	namespace
	{
		struct Selection
		{
			std::shared_ptr<const Locale> locale;
			std::string tag;
		};
		// Readers retain the complete immutable selection through lookup.
		// A concurrent language switch cannot destroy their plural table.
		i2p::util::AtomicSharedPtr<const Selection> g_Selection;
		std::once_flag g_NumericLocale;

		std::string Normalize (std::string_view tag)
		{
			// POSIX codesets do not select a different message catalog.
			//
			// Modifiers are handled separately so Chinese script intent is
			// never silently discarded with an encoding suffix.
			tag = tag.substr (0, tag.find_first_of (".@"));
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

		bool IsSimplifiedChinese (std::string_view tag)
		{
			if (tag == "zh") return true;
			if (!tag.starts_with ("zh-")) return false;
			tag.remove_prefix (3);
			const auto boundary = tag.find ('-');
			const auto first = tag.substr (0, boundary);
			// Match complete subtags, never zh-HansBogus or zh-CNonsense.
			if (first != "hans" && first != "cn" && first != "sg") return false;
			while (boundary != std::string_view::npos && !tag.empty ())
			{
				const auto next = tag.find ('-');
				if (next == std::string_view::npos) break;
				tag.remove_prefix (next + 1);
				const auto part = tag.substr (0, tag.find ('-'));
				if (part == "hant" || part == "hans") return false;
			}
			return true;
		}

		bool ValidTag (std::string_view tag)
		{
			std::size_t length = 0;
			for (const unsigned char ch : tag)
			{
				if (ch == '-')
				{
					if (!length) return false;
					length = 0;
				}
				else
				{
					if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) || ++length > 8)
						return false;
				}
			}
			return length != 0;
		}

		std::shared_ptr<const Selection> Select (const std::string & value)
		{
			const auto requested = Normalize (value);
			const auto base = requested.substr (0, requested.find ('-'));
			std::shared_ptr<const Locale> locale = english::GetLocale ();
			std::string tag = "en";
			if (ValidTag (requested))
			{
				if (base == "es") { locale = spanish::GetLocale (); tag = "es"; }
				else if (base == "ru") { locale = russian::GetLocale (); tag = "ru"; }
				else if (IsSimplifiedChinese (requested))
				{
					// An explicit Traditional modifier overrides generic zh.
					// Do not present Simplified text as Traditional Chinese.
					const auto modifier = value.find ('@');
					const auto script = modifier == std::string::npos ? std::string { }
						: Normalize (std::string_view (value).substr (modifier + 1));
					if (script != "hant" && script != "traditional")
					{
						locale = chinese_simplified::GetLocale ();
						tag = "zh-Hans";
					}
				}
			}
			return std::make_shared<const Selection> (Selection { std::move (locale), std::move (tag) });
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

		std::shared_ptr<const Selection> ActiveSelection ()
		{
			// Standalone tools do not call SetLanguage themselves. Resolve the
			// environment once on first use so LC_ALL/LC_MESSAGES/LANG selects a
			// catalog without every tool duplicating the initialization.
			auto selection = g_Selection.Load ();
			if (selection) return selection;
			std::call_once (g_NumericLocale, [] { setlocale (LC_NUMERIC, "C"); });
			auto initial = Select (EnvironmentLanguage ());
			if (g_Selection.CompareExchangeStrong (selection, initial)) return initial;
			return selection;
		}
	} // namespace

	void SetLanguage (const std::string & tag)
	{
		std::call_once (g_NumericLocale, [] { setlocale (LC_NUMERIC, "C"); });
		g_Selection.Store (Select (tag.empty () ? EnvironmentLanguage () : tag));
	}

	std::string GetLanguageTag ()
	{
		return ActiveSelection ()->tag;
	}

	std::string_view translate (std::string_view arg)
	{
		return ActiveSelection ()->locale->GetString (arg);
	}

	std::string translate (const std::string & arg, const std::string & arg2, const int n)
	{
		return ActiveSelection ()->locale->GetPlural (arg, arg2, n);
	}
} // i18n
} // i2p
