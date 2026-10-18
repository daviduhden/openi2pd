/*
 * Copyright (c) 2013-2026, The PurpleI2P Project
 * Copyright (c) 2026, David Uhden Collado
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef AUTOCONFC
#define AUTOCONFC
#include <iostream>
#include <sstream>
#include <map>
#include <string>
#include <fstream>
#include <limits>
#include <regex>
#include <iomanip>
#include <cstddef>

#define HTTP_SUPPORTS_LANGUAGE                                                                                         \
	"german, italian, polish, portuguese, russian, spanish, turkish, turkmen, ukrainian, uzbek"
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

//	Texts
using AsksT = std::map<std::string, std::string>;
const std::map<std::string, AsksT> Texts = // maybe vector better
	{ { "ru",
		  { { "WelcomeText", "Привет. Выбери тип конфига\r\n1 - клирнет\r\n2 - только yggdrasil" },
			  // Without section
			  { "TunConfYN", "Использовать другой путь для туннелей?" }, { "TunConf", "Введите путь для туннелей" },
			  { "TunnelsDirYN", "Использовать другой путь для папки туннелей?" },
			  { "TunnelsDir", "Введите путь для папки с туннелями" },
			  { "certsdirYN", "Использовать другой путь для папки с сертификатами?" },
			  { "certsdir", "Введите путь для папки с сертификатами" },
			  { "pidfileYN", "использовать другой путь для PID файла?" }, { "pidfile", "Введите путь для pidfile" },
			  { "logYN", "Нужно ли изменить путь лога?" }, { "log", "Введите тип лога(file,stdout,syslog)" },
			  { "logfileYN", "Нужно ли изменить путь logfile?" }, { "logfile", "Введите путь logfile" },
			  { "loglevelYN", "Нужно ли изменить стандартный уровень логирования?" },
			  { "loglevel", "Введите уровень лога(warn,info,none,critical,error,debug)" },
			  { "logCFLYN", "Использовать полный CFL-форму даты в логах? ПО умолчанию только время" },
			  { "daemonYN", "Использовать режим демона?" }, { "FamilyUsing", "Введите название фамилии или -" },
			  //TODO: an another
			  { "UseIPv6", "Использовать ipv6?" }, { "UseIPv4", "Использовать ipv4?" },
			  { "BeFloodfillYN", "Быть флудфиллом?" },
			  { "NoTransitYN", "Отключить транзит? (это уменьшит анонимность)" },
			  { "Bandwidth",
				  "Напиши пропускную способность (- для по умолчанию) [L-32kbs,O-256kbs,P-2048kbs,X-unlimited]" },
			  { "Share", "Процент шары (- для по умолчанию) [0-100]" },
			  //
			  { "NTCPEnabledYN", "Использовать NTCP?" }, { "NTCPPublishedYN", "Опубликовать IP В NTCP?" },
			  { "NTCPPPort", "NTCP Порт. Либо - для пропуска" },
			  { "NTCPPProxy", "NTCP Proxy, пример (socks://localhost:4545)  или - для по умолчанию (неиспользуется)" },
			  { "SSUEnabledYN", "Использовать SSU?" }, { "SSUPPort", "SSU Порт. Либо - для пропуска" },
			  { "SSUProxy", "SSU Proxy, пример (socks://localhost:4545)  или - для по умолчанию (неиспользуется)" },
			  { "HTTPLang",
				  "Выбрите язык веб-интерфейса, либо - для опции по умолчанию (" HTTP_SUPPORTS_LANGUAGE ")" } } },
		{ "en",
			{ { "WelcomeText", "Hello. Select type of config\r\n1 - clearnet\r\n2 - only yggdrasil" },
				{ "TunConfYN", "Use a different path for tunnels?" }, { "TunConf", "Enter path for tunnels" },
				{ "TunnelsDirYN", "Use a different path for the tunnels folder?" },
				{ "TunnelsDir", "Enter path for the tunnels folder" },
				{ "certsdirYN", "Use a different path for the certificates folder?" },
				{ "certsdir", "Enter path for the certificates folder" },
				{ "pidfileYN", "Use a different path for the PID file?" }, { "pidfile", "Enter path for pidfile" },
				{ "logYN", "Do you need to change the log path?" }, { "log", "Enter log type (file, stdout, syslog)" },
				{ "logfileYN", "Do you need to change the logfile path?" }, { "logfile", "Enter path for logfile" },
				{ "loglevelYN", "Do you need to change the default log level?" },
				{ "loglevel", "Enter log level (warn, info, none, critical, error, debug)" },
				{ "logCFLYN", "Use full CFL format for date in logs? Default is only time." },
				{ "daemonYN", "Use daemon mode?" }, { "FamilyUsing", "Enter your netfamily or just hit -." },
				//TODO: an another
				{ "UseIPv6", "Use ipv6?" }, { "UseIPv4", "Use ipv4?" }, { "BeFloodfillYN", "Be a floodfill?" },
				{ "NoTransitYN", "Disable transit? (this will reduce anonymity)" },
				{ "Bandwidth", "Write bandwidth (- for default) [L-32kbs,O-256kbs,P-2048kbs,X-unlimited]" },
				{ "Share", "Share percents (- for default) [0-100]" },
				{ "HTTPLang", "Select Web-interface language or - for default (" HTTP_SUPPORTS_LANGUAGE ")" },
				//
				{ "NTCPEnabledYN", "Use NTCP?" }, { "NTCPPublishedYN", "Publish IP in NTCP?" },
				{ "NTCPPPort", "NTCP Port or - for auto port (random)" },
				{ "NTCPPProxy", "NTCP Proxy, example (socks://localhost:4545) or - for default" },
				{ "SSUEnabledYN", "Use SSU?" }, { "SSUPPort", "SSU Port or - for auto port (random)" },
				{ "SSUProxy", "SSU Proxy, example (socks://localhost:4545) or - for default" }

			} } };

// Functions
bool AskYN (void) noexcept
{
	for (;;)
	{
		char answ;
		std::cout << " ? (y/n) ";
		if (!(std::cin >> answ))
		{
			ClearInputLine ();
			return false;
		}
		ClearInputLine ();
		switch (answ)
		{
		case 'y':
		case 'Y': return true;
		case 'n':
		case 'N': return false;
		default:
			// keep asking instead of consuming the stack with recursion
			break;
		}
	}
}
std::string GetLanguage (void) noexcept
{
	for (;;)
	{
		std::string lang;
		std::cout << "Language/Язык:\r\nru - русский\r\nen - английский\r\n";
		if (!ReadToken (lang))
		{
			std::cerr << "Input error, using English" << std::endl;
			return "en";
		}
		if (Texts.find (lang) != Texts.end ())
		{
			return lang;
		}
		std::cerr << "Not correct language, try again" << std::endl;
	}
}

bool IsOnlyYggdrasil (const std::string & lang) noexcept
{
	for (;;)
	{
		std::string answ;
		std::cout << AutoConf::Texts.at (lang).at ("WelcomeText") << std::endl;
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
	std::cout << "https://i2pd.readthedocs.io/en/latest/user-guide/configuration/\r\nhttps://github.com/PurpleI2P/i2pd/"
				 "blob/openssl/contrib/i2pd.conf\r\n";
	std::ostringstream conf;
	auto lang = AutoConf::GetLanguage ();
	auto isOnlyYgg = AutoConf::IsOnlyYggdrasil (lang);
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
		[] (std::ostringstream & conf, const std::string & lang)
		{
#define ASKYN_MACRO(A, B, C, REGEX)                                                                                    \
	{                                                                                                                  \
		std::cout << AutoConf::Texts.at (lang).at (A);                                                                 \
		if (AskYN ())                                                                                                  \
		{                                                                                                              \
			for (;;)                                                                                                   \
			{                                                                                                          \
				std::cout << AutoConf::Texts.at (lang).at (B) << "\r\n";                                               \
				std::string inp;                                                                                       \
				if (!AutoConf::ReadToken (inp))                                                                        \
				{                                                                                                      \
					std::cerr << "No correct input" << std::endl;                                                      \
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
					std::cerr << "No correct input" << std::endl;                                                      \
				}                                                                                                      \
			}                                                                                                          \
		}                                                                                                              \
	};
			ASKYN_MACRO ("TunConfYN", "TunConf", "tunconf", AutoConf::Regexps::path);
			ASKYN_MACRO ("TunnelsDirYN", "TunnelsDir", "tunnelsdir", AutoConf::Regexps::path);
			ASKYN_MACRO ("certsdirYN", "certsdir", "certsdir", AutoConf::Regexps::path);
			ASKYN_MACRO ("pidfileYN", "pidfile", "pidfile", AutoConf::Regexps::path);
			//ASKYN_MACRO("logYN","log","log",AutoConf::Regexps::any);
			ASKYN_MACRO ("loglevelYN", "loglevel", "loglevel", AutoConf::Regexps::any); // TODO: word type
#define ASK_BOOL(A, B)                                                                                                 \
	{                                                                                                                  \
		std::cout << AutoConf::Texts.at (lang).at (A) << std::endl;                                                    \
		bool v = AskYN ();                                                                                             \
		conf << B "=" << (v ? "true" : "false") << "\r\n";                                                             \
	}
			ASK_BOOL ("UseIPv6", "ipv6");
			ASK_BOOL ("UseIPv4", "ipv4");
			ASK_BOOL ("logCFLYN", "logclftime");
			ASK_BOOL ("daemonYN", "daemon");
// TODO: - to constexpr or just const and use this const in text formating
#define ASK_TEXT(A, B)                                                                                                 \
	{                                                                                                                  \
		std::cout << AutoConf::Texts.at (lang).at (A) << std::endl;                                                    \
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
			std::cerr << "No correct input" << std::endl;                                                              \
		}                                                                                                              \
	}
			ASK_TEXT ("FamilyUsing", "family");
			ASK_BOOL ("BeFloodfillYN", "floodfill");
			ASK_BOOL ("NoTransitYN", "notransit");
			ASK_TEXT ("Bandwidth", "bandwidth");
			ASK_TEXT ("Share", "share");
			///// With sections
			conf << "[ntcp2]\r\n";
			ASK_BOOL ("NTCPEnabledYN", "enabled");
			ASK_BOOL ("NTCPPublishedYN", "published");
			ASK_TEXT ("NTCPPPort", "port");
			ASK_TEXT ("NTCPPProxy", "proxy");
			conf << "[ssu2]\r\n";
			ASK_BOOL ("SSUEnabledYN", "enabled");
			ASK_TEXT ("SSUPPort", "port");
			ASK_TEXT ("SSUProxy", "proxy");
			conf << "[http]\r\n";
			ASK_TEXT ("HTTPLang", "lang");
#undef ASK_TEXT
#undef ASK_BOOL
#undef ASKYN_MACRO
		}(conf, lang);
	}
	std::cout << "Config: " << std::endl;
	std::cout << conf.str () << std::endl;
	//TODO: To Constexpr
	std::cout << "Save File: (\"i2pd_.conf\"):";
	std::string outFileName;
	std::cin.clear ();
	if (!std::getline (std::cin, outFileName)) outFileName.clear ();
	if (outFileName.size () > AutoConf::MAX_INPUT_LENGTH)
	{
		std::cerr << "file name too long" << std::endl;
		return 1;
	}
	//TODO: to constxpr
	if (outFileName.length () == 0) outFileName = "i2pd_.conf";
	std::ofstream confFile (outFileName, std::ios::binary | std::ios::trunc);
	if (!confFile)
	{
		std::cerr << "cannot open " << outFileName << std::endl;
		return 1;
	}
	confFile << conf.str ();
	if (!confFile)
	{
		std::cerr << "cannot write " << outFileName << std::endl;
		return 1;
	}
	confFile.close ();
	if (!confFile)
	{
		std::cerr << "cannot close " << outFileName << std::endl;
		return 1;
	}
	return 0;
}
#endif
