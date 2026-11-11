/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * Separates registered defaults from explicitly supplied credentials.
 * The informational classifier and real parser share this policy.
 */

#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>
#include "Config.h"
#include "Log.h"

namespace
{
constexpr const char * CredentialError =
	"Proxy authentication credentials must be set in the configuration file, not on the command line";
constexpr const char * ArgumentError = "Error while parsing arguments";
constexpr const char * ConfigError = "Error while parsing config file";
constexpr const char * MissingConfigError = "Missing or unreadable config file";
constexpr const char * CredentialOptions[] = { "httpproxy.auth.username",
	"httpproxy.auth.password", "socksproxy.auth.username",
	"socksproxy.auth.password" };

class Arguments
{
public:
	explicit Arguments (std::vector<std::string> values)
		: m_Values (std::move (values))
	{
		for (auto & value : m_Values) m_Arguments.push_back (value.data ());
		m_Arguments.push_back (nullptr);
	}

	int Count () const { return static_cast<int> (m_Values.size ()); }
	char ** Data () { return m_Arguments.data (); }

private:
	std::vector<std::string> m_Values;
	std::vector<char *> m_Arguments;
};

class DiagnosticCapture
{
public:
	DiagnosticCapture () : m_Previous (i2p::log::GetThrowFunction ())
	{
		i2p::log::SetThrowFunction ([this] (const std::string & message)
			{ messages.push_back (message); });
	}

	~DiagnosticCapture () { i2p::log::SetThrowFunction (std::move (m_Previous)); }
	DiagnosticCapture (const DiagnosticCapture &) = delete;
	DiagnosticCapture & operator= (const DiagnosticCapture &) = delete;
	std::vector<std::string> messages;

private:
	i2p::log::ThrowFunction m_Previous;
};

i2p::config::CmdlineOutcome Classify (std::vector<std::string> values)
{
	Arguments arguments (std::move (values));
	return i2p::config::ClassifyInformationalRequest (
		arguments.Count (), arguments.Data ());
}

void ExpectFatalDiagnostic (const char * expected)
{
	i2p::log::SetThrowFunction ([expected] (const std::string & message)
	{
		// Match the public outcome without echoing arbitrary diagnostics.
		if (message == expected)
			std::fputs (expected, stderr);
		else
			std::fputs ("Unexpected fatal policy diagnostic", stderr);
		std::fputc ('\n', stderr);
	});
}

std::vector<std::string> MalformedArguments (const std::string & kind)
{
	if (kind == "unknown")
		return { "i2pd", "--command-line-policy-sentinel" };
	if (kind == "numeric")
		return { "i2pd", "--port=command-line-policy-sentinel" };
	if (kind == "credential-syntax")
		return { "i2pd", "--httpproxy.auth.password=", "--help" };
	if (kind == "credential-duplicate")
		return { "i2pd", "--httpproxy.auth.password",
			"command-line-policy-sentinel", "--httpproxy.auth.password",
			"second-policy-sentinel", "--version" };
	return {};
}

int RejectMalformed (int argc, char * argv[])
{
	if (argc < 3) return 2;
	const std::string kind (argv[2]);
	i2p::config::Init ();
	if (kind == "config" || kind == "missing-config")
	{
		if (argc != 4) return 2;
		ExpectFatalDiagnostic (kind == "config" ? ConfigError : MissingConfigError);
		i2p::config::ParseConfig (argv[3]);
	}
	else
	{
		if (argc != 3) return 2;
		auto values = MalformedArguments (kind);
		if (values.empty ()) return 2;
		ExpectFatalDiagnostic (ArgumentError);
		Arguments arguments (std::move (values));
		i2p::config::ParseCmdline (arguments.Count (), arguments.Data ());
	}
	std::fputs ("Malformed input unexpectedly continued\n", stderr);
	return 0;
}

// Closed fixture modes never accept arbitrary credential operands.
int RejectExplicit (int argc, char * argv[])
{
	if (argc != 5) return 2;
	const std::string index (argv[2]), kind (argv[3]), request (argv[4]);
	if (index.size () != 1 || index[0] < '0' || index[0] > '3'
		|| (kind != "empty" && kind != "value")
		|| (request != "normal" && request != "help" && request != "version"))
		return 2;
	ExpectFatalDiagnostic (CredentialError);
	std::vector<std::string> values { "i2pd",
		std::string ("--") + CredentialOptions[index[0] - '0'],
		kind == "empty" ? "" : "command-line-policy-sentinel" };
	if (request != "normal") values.push_back ("--" + request);
	Arguments arguments (std::move (values));
	i2p::config::Init ();
	i2p::config::ParseCmdline (arguments.Count (), arguments.Data ());
	std::fputs ("Credential parse unexpectedly continued\n", stderr);
	return 0;
}
}

int main (int argc, char * argv[])
{
	if (argc > 1 && std::string (argv[1]) == "--reject-explicit")
		return RejectExplicit (argc, argv);
	if (argc > 1 && std::string (argv[1]) == "--reject-malformed")
		return RejectMalformed (argc, argv);
	if (argc != 1) return 2;
	using i2p::config::CmdlineOutcome;
	DiagnosticCapture diagnostics;
	assert (Classify ({ "i2pd", "--version" }) == CmdlineOutcome::Version);
	assert (Classify ({ "i2pd", "--help" }) == CmdlineOutcome::Help);
	assert (Classify ({ "i2pd", "-version" }) == CmdlineOutcome::Version);
	assert (Classify ({ "i2pd", "-help" }) == CmdlineOutcome::Help);
	assert (Classify ({ "i2pd", "--ipv6" }) == CmdlineOutcome::Proceed);
	assert (diagnostics.messages.empty ());
	for (const auto * kind : { "unknown", "numeric", "credential-syntax", "credential-duplicate" })
	{
		assert (Classify (MalformedArguments (kind)) == CmdlineOutcome::Proceed);
		assert (diagnostics.messages.empty ());
	}

	// Defaults materialize in the real parser, but are not CLI operands.
	i2p::config::Init ();
	Arguments ordinary ({ "i2pd", "--ipv6" });
	i2p::config::ParseCmdline (ordinary.Count (), ordinary.Data ());
	assert (diagnostics.messages.empty ());
	for (const char * option : CredentialOptions)
	{
		const auto found = i2p::config::m_Options.find (option);
		assert (found != i2p::config::m_Options.end ());
		assert (found->second.defaulted ());
	}

	// A separate empty argv value is valid Boost string-option syntax;
	// --option= is instead rejected as an empty adjacent parameter.
	const std::string sentinel = "command-line-policy-sentinel";
	for (const char * option : CredentialOptions)
	{
		const std::string flag = std::string ("--") + option;
		for (const auto & value : { std::string {}, sentinel })
		{
			assert (Classify ({ "i2pd", "--version", flag, value })
				== CmdlineOutcome::Proceed);
			assert (Classify ({ "i2pd", "--help", flag, value })
				== CmdlineOutcome::Proceed);
			assert (diagnostics.messages.empty ());
		}
	}
	i2p::config::Init ();
	return 0;
}
