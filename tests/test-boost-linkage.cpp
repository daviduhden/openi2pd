/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: Boost implementation linkage regression.
 *
 * Requires both compiled torrent RPC dependencies through libi2pdclient's
 * transitive static-library link interface.
 */

#include <cassert>
#include <boost/json.hpp>
#include <boost/url.hpp>

int main ()
{
	const auto value = boost::json::parse (R"({"tracker":"http://tracker.i2p/announce"})");
	assert (value.as_object ().at ("tracker").is_string ());
	assert (!boost::json::serialize (value).empty ());
	const auto tracker = boost::urls::parse_uri ("http://tracker.i2p/announce");
	assert (tracker && tracker->scheme () == "http");
	assert (tracker->host () == "tracker.i2p");
}
