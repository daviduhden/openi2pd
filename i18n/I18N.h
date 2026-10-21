/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: I18N.
 *
 * Declares the Locale container (string table, plural table and plural
 * formula), the translation helpers, and current-language accessors used by
 * configuration, daemon and TUI code.
 *
 */

#ifndef __I18N_H__
#define __I18N_H__

#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>
#include <map>
#include <utility>
#include <functional>
#include <vector>

namespace i2p
{
namespace i18n
{
	typedef std::map<std::string_view, std::string_view> LocaleStrings;
	class Locale
	{
	public:
		Locale (const std::string & language, const bool & rtl, const LocaleStrings & strings,
			const std::map<std::string, std::vector<std::string>> & plurals, std::function<int (int)> formula)
			: m_Language (language)
			, m_RTL (rtl)
			, m_Strings (strings)
			, m_Plurals (plurals)
			, m_Formula (formula) { };

		// Get activated language name for administrative views
		std::string GetLanguage () const { return m_Language; }

		bool GetRTL () const { return m_RTL; }

		std::string_view GetString (std::string_view arg) const
		{
			const auto it = m_Strings.find (arg);
			if (it == m_Strings.end ())
			{
				return arg;
			}
			else
			{
				return it->second;
			}
		}

		std::string GetPlural (const std::string & arg, const std::string & arg2, int n) const
		{
			const auto it = m_Plurals.find (arg2);
			if (it == m_Plurals.end ()) // not found, fallback to english
			{
				return n == 1 ? arg : arg2;
			}
			else
			{
				int form = m_Formula (n);
				if (form < 0 || static_cast<std::size_t> (form) >= it->second.size ()) form = 0;
				return it->second.empty () ? (n == 1 ? arg : arg2) : it->second[form];
			}
		}

	private:
		const std::string m_Language;
		const bool m_RTL;
		const LocaleStrings m_Strings;
		const std::map<std::string, std::vector<std::string>> m_Plurals;
		std::function<int (int)> m_Formula;
	};

	// Retained for native Android integration; the local TUI always uses English.
	void SetLanguage (const std::string &);
	std::string_view translate (std::string_view arg);
	std::string translate (const std::string & arg, const std::string & arg2, int n);

	namespace detail
	{
		template <typename... TArgs>
		std::string FormatPrintf (std::string_view format, TArgs &&... args)
		{
			if constexpr (sizeof... (TArgs) == 0) return std::string (format);
			const std::string formatString (format);
			const int required
				= std::snprintf (nullptr, 0, formatString.c_str (), std::forward<TArgs> (args)...);
			if (required < 0) return {};
			std::vector<char> buffer (static_cast<std::size_t> (required) + 1);
			const int written = std::snprintf (
				buffer.data (), buffer.size (), formatString.c_str (), std::forward<TArgs> (args)...);
			if (written < 0 || written > required) return {};
			return std::string (buffer.data (), static_cast<std::size_t> (written));
		}
	} // detail
} // i18n
} // i2p

/**
 * @brief Get translation of string
 * @param arg String with message
 */
template <typename TValue> std::string_view tr (TValue && arg)
{
	return i2p::i18n::translate (std::forward<TValue> (arg));
}

/**
 * @brief Get translation of string and format it
 * @param arg String with message
 * @param args Array of arguments for string formatting
*/
template <typename TValue, typename... TArgs> std::string tr (TValue && arg, TArgs &&... args)
{
	return i2p::i18n::detail::FormatPrintf (
		i2p::i18n::translate (std::forward<TValue> (arg)), std::forward<TArgs> (args)...);
}

/**
 * @brief Get translation of string with plural forms
 * @param arg String with message in singular form
 * @param arg2 String with message in plural form
 * @param n Integer, used for selection of form
 */
template <typename TValue, typename TValue2> std::string ntr (TValue && arg, TValue2 && arg2, int n)
{
	return i2p::i18n::translate (std::forward<TValue> (arg), std::forward<TValue2> (arg2), std::forward<int> (n));
}

/**
 * @brief Get translation of string with plural forms and format it
 * @param arg String with message in singular form
 * @param arg2 String with message in plural form
 * @param n Integer, used for selection of form
 * @param args Array of arguments for string formatting
 */
template <typename TValue, typename TValue2, typename... TArgs>
std::string ntr (TValue && arg, TValue2 && arg2, int n, TArgs &&... args)
{
	return i2p::i18n::detail::FormatPrintf (
		i2p::i18n::translate (std::forward<TValue> (arg), std::forward<TValue2> (arg2), std::forward<int> (n)),
		std::forward<TArgs> (args)...);
}

#endif // __I18N_H__
