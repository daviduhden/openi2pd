<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd translation identifier boundary.
-->
# Never translate

- `i2pd`, `i2pd-tui`, `i2pdctl`, `openi2pd`, package/application IDs, JNI symbols
  and file/data paths.
- I2P protocol names and tokens: `I2P`, `NetDb`, `NTCP2`, `SSU2`, `I2NP`,
  `LeaseSet`, `SAM`, `BOB`, `I2CP`, `DHT`, `PEX`, `UPnP`, `SSDP`, `.i2p`,
  `.b32.i2p`, `bencode`, `SHA-1`, `base64`.
- TUI/IPC field names and verbs (`router.hash`, `STATE`, `NETDB-ROUTERS`,
  `ADMIN ...`, `TORRENT-*`), error codes and typed confirmation words
  (`SHUTDOWN`, `YES`) where the grammar compares them.
- Configuration keys/values and persisted tokens: `i2pd.conf` keys,
  `bandwidth` values `L/O/P/X`, log levels `none/critical/error/warn/info/debug`,
  tracker/peer state tokens, transport names.
- User-generated content: torrent names, file names, `.i2p` host names,
  destinations, comments, tracker URLs and log payload text.
- Formats, placeholder syntax, bencode/metainfo keys, MIME types and URLs.

Presentation may wrap these values in translated sentences, but the value
itself must be inserted unchanged as a placeholder. Native error text may stay
English inside a translated wrapper until a typed error-code mapping exists.

- `open-i2pd` is a frozen technical spelling (syslog identity, Android data
  directory, Windows installer identity, legacy artwork filename), not the
  product name; never translate or rebrand it.
- Torrent/LeaseSet status wire tokens stay English. A display helper may map
  them to a translated label, but the stored/logged value and IPC field must
  remain the exact token.
