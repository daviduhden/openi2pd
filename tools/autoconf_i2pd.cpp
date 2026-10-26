/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: autoconf i2pd.
 *
 * Interactive i2pd.conf generator. It prompts in Russian or English for
 * clearnet or Yggdrasil-only mode and the individual paths and options, then
 * writes the assembled configuration to a chosen file. Terminal input is
 * length-bounded.
 *
 */

#ifndef AUTOCONFC
#define AUTOCONFC
#include <array>
#include <iostream>
#include <sstream>
#include <map>
#include <string>
#include <fstream>
#include <limits>
#include <regex>
#include <iomanip>
#include <cstddef>
#include <cstring>

#include "I18N.h"
#include "FS.h"

namespace AutoConf
{
constexpr std::size_t MAX_INPUT_LENGTH = 4096;

// Discard the rest of the current input line without letting a terminal
// EOF leave the stream in a state that would make the next read hang.
inline void ClearInputLine (void) noexcept
{
	std::cin.clear ();
	std::cin.ignore (std::numeric_limits<std::streamsize>::max (), '\n');
	if (std::cin.eof ()) std::cin.clear (std::ios::goodbit);
}

// Read a single whitespace-delimited token with an explicit upper bound.
// Returns false on EOF or when the token does not fit in MAX_INPUT_LENGTH.
inline bool ReadToken (std::string & token)
{
	token.clear ();
	if (!(std::cin >> std::setw (static_cast<int> (MAX_INPUT_LENGTH) + 1) >> token))
	{
		ClearInputLine ();
		return false;
	}
	if (token.size () >= MAX_INPUT_LENGTH && std::cin.peek () != '\n'
		&& std::cin.peek () != std::char_traits<char>::eof ())
	{
		ClearInputLine ();
		return false;
	}
	ClearInputLine ();
	return true;
}

//
namespace Regexps
{
	//const std::regex port("\\d{1,5}");
	std::regex path (R"([a-zA-Z0-9_\.\/\\]+)"); // (\w|\.|\\|\/)+
	//std::regex proxy("\\w+:\\/\\/(\\w|\\d|\\.|\\-)+\\:\\d+");
	std::regex any (".*");

}
namespace PreInitConfigs
{
	constexpr const char * yggOnlyConf = "ipv4=false\r\n"
										 "ipv6=false\r\n"
										 "ssu=false\r\n"
										 "ntcp2.enabled=false\r\n"
										 "ssu2.enabled=false\r\n"
										 "meshnets.yggdrasil=true\r\n";
}

// Functions
bool AskYN (void) noexcept
{
	for (;;)
	{
		// Read a token rather than one byte: the localized Spanish and Russian
		// answers are multibyte UTF-8 sequences.
		std::string answ;
		std::cout << i2p::i18n::tr (" ? (y/n) ");
		if (!ReadToken (answ))
		{
			return false;
		}
		if (answ == "y" || answ == "Y" || answ == "s" || answ == "S" || answ == "д" || answ == "Д")
			return true;
		if (answ == "n" || answ == "N" || answ == "н" || answ == "Н")
			return false;
		// keep asking instead of consuming the stack with recursion
	}
}
void GetLanguage () noexcept
{
	// The shared catalogs carry every supported language, so the tool no
	// longer ships a private ru/en map. An explicit choice overrides the
	// environment for this process only.
	for (;;)
	{
		std::string lang;
		std::cout << "Language/Язык: ru, en, es, zh-Hans\r\n";
		if (!ReadToken (lang))
		{
			std::cerr << i2p::i18n::tr ("Input error, using English") << std::endl;
			i2p::i18n::SetLanguage ("en");
			return;
		}
		if (lang == "ru" || lang == "en" || lang == "es" || lang == "zh" || lang == "zh-Hans")
		{
			i2p::i18n::SetLanguage (lang);
			return;
		}
		std::cerr << i2p::i18n::tr ("Unsupported language, try again") << std::endl;
	}
}

bool IsOnlyYggdrasil () noexcept
{
	for (;;)
	{
		std::string answ;
		std::cout << i2p::i18n::tr ("Hello. Select type of config\r\n1 - clearnet\r\n2 - only yggdrasil")
				  << std::endl;
		if (!ReadToken (answ))
		{
			// keep the default clearnet configuration on input failure
			return false;
		}
		if (answ == "1") return false;
		if (answ == "2") return true;
	}
}

}

int main (void)
{
	std::cout << "https://github.com/daviduhden/openi2pd/blob/main/docs/user-guide/configuration.md\r\n"
				 "https://github.com/daviduhden/openi2pd/blob/main/contrib/i2pd.conf\r\n";
	std::ostringstream conf;
	AutoConf::GetLanguage ();
	auto isOnlyYgg = AutoConf::IsOnlyYggdrasil ();
	if (isOnlyYgg)
	{
#ifndef _WIN32
		conf << "daemon=true\r\n";
#endif
		conf << AutoConf::PreInitConfigs::yggOnlyConf;
	}
	else
	{
		// Asks
		using namespace AutoConf;
		[] (std::ostringstream & conf)
		{
#define ASKYN_MACRO(A, B, C, REGEX)                                                                                    \
	{                                                                                                                  \
		std::cout << i2p::i18n::tr (A);                                                                 \
		if (AskYN ())                                                                                                  \
		{                                                                                                              \
			for (;;)                                                                                                   \
			{                                                                                                          \
				std::cout << i2p::i18n::tr (B) << "\r\n";                                               \
				std::string inp;                                                                                       \
				if (!AutoConf::ReadToken (inp))                                                                        \
				{                                                                                                      \
					std::cerr << i2p::i18n::tr ("No correct input") << std::endl;                                                      \
					break;                                                                                             \
				}                                                                                                      \
				std::smatch bmatch;                                                                                    \
				if (std::regex_match (inp, bmatch, REGEX) && !bmatch.empty () && bmatch[0].matched)                    \
				{                                                                                                      \
					conf << C "=" << inp << "\r\n";                                                                    \
					break;                                                                                             \
				}                                                                                                      \
				else                                                                                                   \
				{                                                                                                      \
					std::cerr << i2p::i18n::tr ("No correct input") << std::endl;                                                      \
				}                                                                                                      \
			}                                                                                                          \
		}                                                                                                              \
	};
			ASKYN_MACRO ("Use a different path for tunnels?", "Enter path for tunnels", "tunconf", AutoConf::Regexps::path);
			ASKYN_MACRO ("Use a different path for the tunnels folder?", "Enter path for the tunnels folder", "tunnelsdir", AutoConf::Regexps::path);
			ASKYN_MACRO ("Use a different path for the certificates folder?", "Enter path for the certificates folder", "certsdir", AutoConf::Regexps::path);
			ASKYN_MACRO ("Use a different path for the PID file?", "Enter path for pidfile", "pidfile", AutoConf::Regexps::path);
			//ASKYN_MACRO("logYN","log","log",AutoConf::Regexps::any);
			ASKYN_MACRO ("Do you need to change the default log level?", "Enter log level (warn, info, none, critical, error, debug)", "loglevel", AutoConf::Regexps::any); // TODO: word type
#define ASK_BOOL(A, B)                                                                                                 \
	{                                                                                                                  \
		std::cout << i2p::i18n::tr (A) << std::endl;                                                    \
		bool v = AskYN ();                                                                                             \
		conf << B "=" << (v ? "true" : "false") << "\r\n";                                                             \
	}
			ASK_BOOL ("Use ipv6?", "ipv6");
			ASK_BOOL ("Use ipv4?", "ipv4");
			ASK_BOOL ("Use full CFL format for date in logs? Default is only time.", "logclftime");
			ASK_BOOL ("Use daemon mode?", "daemon");
// TODO: - to constexpr or just const and use this const in text formating
#define ASK_TEXT(A, B)                                                                                                 \
	{                                                                                                                  \
		std::cout << i2p::i18n::tr (A) << std::endl;                                                    \
		std::string inp;                                                                                               \
		if (AutoConf::ReadToken (inp))                                                                                 \
		{                                                                                                              \
			if (inp != "-")                                                                                            \
			{                                                                                                          \
				conf << B "=" << inp << "\r\n";                                                                        \
			}                                                                                                          \
		}                                                                                                              \
		else                                                                                                           \
		{                                                                                                              \
			std::cerr << i2p::i18n::tr ("No correct input") << std::endl;                                                              \
		}                                                                                                              \
	}
			ASK_TEXT ("Enter your netfamily or just hit -.", "family");
			ASK_BOOL ("Be a floodfill?", "floodfill");
			ASK_BOOL ("Disable transit? (this will reduce anonymity)", "notransit");
			ASK_TEXT ("Write bandwidth (- for default) [L-32kbs,O-256kbs,P-2048kbs,X-unlimited]", "bandwidth");
			ASK_TEXT ("Share percents (- for default) [0-100]", "share");
			///// With sections
			conf << "[ntcp2]\r\n";
			ASK_BOOL ("Use NTCP?", "enabled");
			ASK_BOOL ("Publish IP in NTCP?", "published");
			ASK_TEXT ("NTCP Port or - for auto port (random)", "port");
			ASK_TEXT ("NTCP Proxy, example (socks://localhost:4545) or - for default", "proxy");
			conf << "[ssu2]\r\n";
			ASK_BOOL ("Use SSU?", "enabled");
			ASK_TEXT ("SSU Port or - for auto port (random)", "port");
			ASK_TEXT ("SSU Proxy, example (socks://localhost:4545) or - for default", "proxy");
#undef ASK_TEXT
#undef ASK_BOOL
#undef ASKYN_MACRO
		}(conf);
	}
	std::cout << i2p::i18n::tr ("Config: ") << std::endl;
	std::cout << conf.str () << std::endl;
	//TODO: To Constexpr
	std::cout << "Save File: (\"i2pd_.conf\"):";
	std::array<char, AutoConf::MAX_INPUT_LENGTH + 2> nameBuffer { };
	std::cin.clear ();
	std::cin.getline (nameBuffer.data (), nameBuffer.size ());
	if (std::cin.fail () && !std::cin.eof ())
	{
		std::cerr << "file name too long" << std::endl;
		return 1;
	}
	const auto count = static_cast<size_t> (std::cin.gcount ());
	// gcount() also counts the extracted delimiter, which is not stored in the
	// buffer. Construct from the actual C string so a trailing NUL never
	// becomes part of the path or truncates it silently.
	std::string outFileName (nameBuffer.data (), ::strnlen (nameBuffer.data (), count));
	while (!outFileName.empty () && (outFileName.back () == '\n' || outFileName.back () == '\r'))
		outFileName.pop_back ();
	if (outFileName.find ('\0') != std::string::npos)
	{
		std::cerr << "file name contains a NUL byte" << std::endl;
		return 1;
	}
	if (outFileName.empty ()) outFileName = "i2pd_.conf";
	// Publish the configuration through the same staged writer the daemon uses
	// instead of truncating an existing file in place.
	if (!i2p::fs::AtomicWrite (outFileName, conf.str ()))
	{
		std::cerr << "cannot write " << outFileName << std::endl;
		return 1;
	}
	return 0;
}
#endif
