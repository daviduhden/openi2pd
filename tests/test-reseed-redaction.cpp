/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: reseed credential redaction regression.
 *
 * The configured reseed proxy may carry `user:pass@host`. Only the endpoint may
 * reach a log record or the administrative reseed state; a credential must
 * never appear in a reason string. Deterministic: no socket, no thread beyond
 * the owned reseed job and no sleep.
 *
 */

#include <cassert>
#include <chrono>
#include <string>
#include <thread>

#include <boost/program_options/variables_map.hpp>

#include "Config.h"
#include "NetDb.hpp"
#include "Reseed.h"
#include "util.h"

namespace
{
using namespace i2p;
using namespace i2p::data;

constexpr const char * CREDENTIAL_PROXY = "http://user:secret@127.0.0.1:1";
// An unsupported proxy scheme is rejected before any socket is opened, which
// exercises the real HTTPS error path without a network.
constexpr const char * UNSUPPORTED_PROXY = "ftp://user:secret@127.0.0.1:1";

template <typename Predicate> bool WaitFor (Predicate predicate, std::chrono::milliseconds budget)
{
	const auto deadline = std::chrono::steady_clock::now () + budget;
	while (std::chrono::steady_clock::now () < deadline)
	{
		if (predicate ()) return true;
		std::this_thread::sleep_for (std::chrono::milliseconds (2));
	}
	return predicate ();
}

void SetConfigValue (const std::string & name, const std::string & value)
{
	i2p::config::m_Options[name] = boost::program_options::variable_value (value, false);
}

bool ContainsSecret (const std::string & text)
{
	return text.find ("secret") != std::string::npos || text.find ("user:") != std::string::npos;
}
}

int main ()
{
	using namespace i2p;
	using namespace i2p::data;

	// The redaction helper keeps only scheme://host:port and never returns the
	// credential-bearing part of the value.
	assert (RedactReseedProxyForLog (CREDENTIAL_PROXY) == "http://127.0.0.1:1");
	assert (RedactReseedProxyForLog ("socks://user:secret@127.0.0.1:9050/path?x=1#fragment")
		== "socks://127.0.0.1:9050");
	assert (RedactReseedProxyForLog ("http://[::1]:8080") == "http://[::1]:8080");
	assert (RedactReseedProxyForLog ("127.0.0.1:1080") == "127.0.0.1:1080");
	assert (RedactReseedProxyForLog ("user:secret@127.0.0.1") == "127.0.0.1");
	assert (RedactReseedProxyForLog ("") == "");
	// An unparsable value is replaced, not echoed.
	assert (RedactReseedProxyForLog ("http://user:secret@") == "invalid");
	assert (RedactReseedProxyForLog ("://user:secret@") == "invalid");
	for (const char * value : { CREDENTIAL_PROXY, "socks://user:secret@127.0.0.1:9050/path?x=1#fragment",
			 "http://user:secret@", "://user:secret@", "user:secret@127.0.0.1" })
		assert (!ContainsSecret (RedactReseedProxyForLog (value)));

	// A failing reseed reports a fixed reason and never the configured proxy.
	SetConfigValue ("reseed.proxy", CREDENTIAL_PROXY);

	ReseedTestHooks hooks;
	hooks.overrideSources = true; // an empty source list ends the pass immediately
	NetDb database;
	database.SetReseedTestHooks (hooks);
	assert (database.RequestReseed ());
	assert (WaitFor ([&] { return !database.IsReseeding (); }, std::chrono::seconds (5)));
	{
		const auto state = database.GetReseedState ();
		assert (state.phase == ReseedPhase::Failed);
		assert (state.enabled);
		assert (state.lastError == "no server");
		assert (state.lastError.size () <= RESEED_MAX_ERROR_LENGTH);
		assert (!ContainsSecret (state.lastError));
	}

	// A transport failure in the real HTTPS path (an unsupported proxy scheme is
	// rejected before any socket is opened) is reported the same way.
	SetConfigValue ("reseed.proxy", UNSUPPORTED_PROXY);
	SetConfigValue ("reseed.file", std::string ("https://reseed.invalid/i2pseeds.su3"));
	database.SetReseedTestHooks (ReseedTestHooks { }); // no fetch hook: real transport
	assert (database.RequestReseed ());
	assert (WaitFor ([&] { return !database.IsReseeding (); }, std::chrono::seconds (5)));
	{
		const auto state = database.GetReseedState ();
		assert (state.phase == ReseedPhase::Failed);
		assert (state.lastError == "proxy");
		assert (state.lastError.size () <= RESEED_MAX_ERROR_LENGTH);
		assert (!ContainsSecret (state.lastError));
	}

	// The blocking bootstrap path returns the same bounded reason.
	{
		Reseeder reseeder;
		const auto result = reseeder.Bootstrap ();
		assert (result.imported == 0);
		assert (result.skippedStale == 0);
		assert (result.error == "proxy");
		assert (!ContainsSecret (result.error));
		assert (result.error.size () <= RESEED_MAX_ERROR_LENGTH);
	}

	database.Stop ();
	return 0;
}
