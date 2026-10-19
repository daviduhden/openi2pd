/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: test torrents magnet.
 *
 * Parses BTIH magnets in hex and Base32, normalizing names, deduplicating trackers, rejecting unsafe schemes, fragments, escapes and oversized arguments.
 *
 */

#include <cassert>
#include <string>
#include "TorrentsControl.h"
#ifndef NO_TORRENTS
using namespace i2p::torrents;
int main ()
{
	const std::string topic = "magnet:?xt=urn:btih:" + std::string (40, '0');
	auto parsed = ParseTorrentMagnet (topic
		+ "&dn=hello%20world&tr=http%3A%2F%2Fa.i2p%2Fa"
		  "&tr=http%3A%2F%2Fa.i2p%2Fb&tr=http%3A%2F%2Fa.i2p%2Fa&tr=http%3A%2F%2Fpublic.example%2Fa");
	assert (parsed && parsed->name == "hello world");
	assert (parsed->trackers.size () == 2);
	assert (parsed->trackers[0] == "http://a.i2p/a");
	assert (parsed->trackers[1] == "http://a.i2p/b");
	auto base32 = ParseTorrentMagnet ("magnet:?xt=urn%3Abtih%3A" + std::string (32, 'A'));
	assert (base32 && base32->infoHash == parsed->infoHash);
	assert (ParseTorrentMagnet (topic + "&xt=urn:btih:" + std::string (32, 'a')));
	assert (!ParseTorrentMagnet (topic + "&xt=urn:btih:" + std::string (40, '1')));
	assert (!ParseTorrentMagnet (topic + "&xt=urn:btih:bad"));
	assert (!ParseTorrentMagnet (topic + "&dn=%"));
	assert (!ParseTorrentMagnet (topic + "&dn=%0Ainjection"));
	assert (!ParseTorrentMagnet (topic + "&dn=%00"));
	assert (!ParseTorrentMagnet (topic + "#fragment"));
	assert (!ParseTorrentMagnet ("magnet:?xt=urn:btmh:1220abcd"));
	assert (!ParseTorrentMagnet ("magnet:?xt=urn:btih:" + std::string (32, '8')));
	assert (!ParseTorrentMagnet (topic + "&dn=" + std::string (MAX_CONTROL_ARGUMENT, 'x')));
	auto unsafe = ParseTorrentMagnet (topic
		+ "&tr=http://user@tracker.i2p/a&tr=https://tracker.i2p/a"
		  "&tr=udp://tracker.i2p:6881/a");
	assert (unsafe && unsafe->trackers.size () == 1);
	return 0;
}
#else
int main ()
{
	return 0;
}
#endif
