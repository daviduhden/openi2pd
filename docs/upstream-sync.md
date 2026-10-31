<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: upstream sync.

Documents project documentation.

-->

# Upstream provenance and synchronization

## Reviewed router-source commits (2026-10-25..31 continuation)

The `openssl` branch advanced from the last audited source commit
`5bcf6101a456dc991e44d3d79cb69994c7bae484` to
`ba38945eefe28eec8d02d63241d4903a8c5145b1`. The range contains 40 commit
objects (34 non-merge changes and six merge commits). The local comparison
baseline was `46738ddf6f0f9b8e01e31d7c28bc6396215f82e8`; changes below were
reviewed against the clean worktree at that revision. No upstream commit was
merged or cherry-picked.

| Upstream SHA | Subject | Decision and local result |
|---|---|---|
| `6083b362c332e65782453fad47cf6025257b51ba` | OpenRC `command_args_background` | Incompatible: upstream assumes daemonizing OpenRC commands; local `supervise-daemon` uses the foreground daemon. |
| `e4ae3b0fc4e6c9cfbf3f681993a55f3696cc92c3` | OpenRC remove `name` | Incompatible: local reload/reopen-log helper messages interpolate `${name}`. |
| `5c11f1adf9a8e8ab72747b4d6e2d0b99c3100e36` | Move Haiku daemon source | Incompatible: Haiku is outside the supported local platform matrix. |
| `8b7ebc6e774b2f64435b5233ff6d0a45e7e547ad` | Torrent verification | Already superseded: local verification is bounded to one piece per job rather than scanning whole torrents asynchronously. |
| `57d9087150cc29eda40e6d4fe625faf26497b63c` | Merge Haiku PR #2614 | Merge-only; its tree changes are represented by `5c11f1ad` and `58f44b0`. |
| `028d53dd331bc44fd6044416e909b8af5bf42149` | Check SU3 signature stream errors | Already integrated: reseed reads validate exact byte counts and stream state for content and signatures. |
| `58f44b06591239f2d87549405282cac852cf8a2b` | Fix Haiku build | Incompatible: Haiku remains unsupported by the local platform matrix. |
| `9d3825624474783126619cdbd6a0cb939b0a315d` | Merge reseed stream-error PR #2615 | Merge-only; disposition is carried by `028d53dd`. |
| `34e7dfd27d79103b6f17423d2fece2e5f52faf73` | Check torrent file read/write and reopen | Already superseded: serialized positional fd IO and exact IO results replace shared `fstream` recovery. |
| `bd2be5cd1c7e2e7a94f0a91b40bd9584026479d0` | Close torrent stream on error | Already superseded by the local fd/storage-worker ownership model and fail-closed IO results. |
| `8ca8189a9e5466a0be13cc68bdd625f95b7db4dc` | Report failed flush on next IO | Already superseded: local writes check completion directly and publish filesystem failure atomically. |
| `ab018c7cb5e938fc339c716334086acd1a1b924b` | Fix Makefile typo | Not applicable: no corresponding maintained upstream Makefile target. |
| `3042fe9d664d72aad65e3de85ecc711cfcb40dcb` | Web torrent table behavior | Incompatible: the browser console is deliberately removed; native TUI is maintained instead. |
| `c4ad7f2de74565593e9b8b71d7e33399272d81e5` | Merge OpenRC remove-name PR #2324 | Merge-only integration of `e4ae3b0f` (and web behavior from `3042fe9d`); the OpenRC hunk is incompatible because local helpers interpolate `${name}`. |
| `09c465d9df55a12cec25b6cf79c7b5a5d6c29a19` | Merge OpenRC command args PR #2265 | Merge-only; disposition is carried by `6083b362`. |
| `f879d4124b24fe8b303dd53cd4a498d2f8e97737` | Do not reannounce stopped torrent | Already integrated: `RequestTorrentTrackers` rejects stopped torrents. |
| `36c060fba39546534986d61999d8d4e225f6247f` | Send relative path in Referer | Applied: after same-origin validation, HTTP proxy rewrites `Referer` to `URL::to_string(true)`, which emits only path/query and avoids origin disclosure. |
| `398cd5b144171754d5f7440f958333480c4fea4a` | Parse DHT arguments as a field map | Already superseded: bounded `DecodeDHTFrame` validates the complete envelope and fields. |
| `4ac60de964745111702c8f0e2950238167710dcb` | Parse DHT response as a field map | Already superseded by the same strict local KRPC decoder and response validation. |
| `274e1518b79dde6cac311735989f73af465eb87c` | Destroy memory pools after messages | Applied: SSU2 pool members now precede `ReceiveService`, so reverse member destruction destroys the service before its backing pools. |
| `9a449ba7f788afc2e9e2c184f3ff427cc93ecf4e` | Delete SSU2/NTCP2 servers after transport thread | Already stronger locally: transport service and servers stop, peers clear while contexts live, then server owners reset. |
| `8dec29963d028ffb68ccc0285c5fbb04d9eb8eed` | Update legacy Makefiles | Not applicable: local build uses CMake/Ninja and a POSIX front-end; these legacy files are not maintained. |
| `67714be7efeeba8e7f54bb99e7a5666ca38ff70f` | Enable torrent RPC on Android | Already integrated: Android builds the native torrent RPC surface. |
| `bbfd34856fd8ec8a4fd0b401a650a256aa05a617` | Reset `from` before saving packet | Applied: stream-level reorder buffering clears the borrowed ratchet-session pointer before retaining a packet. |
| `0c52fc3116955748dd182c6e9ac75c3b55da49c0` | Merge torrent save-error PR #2616 | Merge-only; its torrent IO changes are represented by `34e7dfd`, `bd2be5cd` and `8ca8189a`. |
| `ac3ecb88152ecfe76853681144610d98f49662f8` | Use 64-bit RNG for DHT transaction IDs | Already stronger locally: transaction IDs and tokens use `RAND_bytes`. |
| `7761d85409bd508909f130f95815d575aa18b9f2` | Share KRPC parser for datagrams | Already superseded by the common bounded `DecodeDHTFrame` path. |
| `7bd4c2f43c999212429f2758e3c46c21b11f035d` | Pass infoHash instead of magnet | Incompatible/not needed: this extends the upstream browser-oriented RPC input; the fork has no browser console and retains its typed native control contract. |
| `6ba5fd5c1456f92b4ffb030dbc448bf0b9b7064d` | Return torrentHashes on request | Incompatible/not needed: browser-console statistics extension with no current native consumer. |
| `523c6a68de763edaaa931b8fcea6012b82c22a52` | Web context menu, hotkeys and selection | Incompatible: browser UI was deliberately removed. |
| `e81ffe8b593bb139cdcf2886b7860b92b19ecd12` | Erase previous tag before next message | Already integrated safely: tag/index are copied and erased under lock before callback dispatch. |
| `6ec6e320d8d73bb09a02fc46d18302e08b07daf7` | Use Boost flat map from 1.81 | Not applicable: local dependency declarations do not provide this API at the supported floor; no dependency bump for this optional optimization. |
| `5ddb01175083b9ca907ae3781c4920630d8695f3` | Require Boost 1.83 for flat map | Incompatible with the deliberately supported lower Boost floor; no required behavior depends on flat-map. |
| `7979231279bba9af4318211abc5e810c381731f0` | Dynamic bitset and reset request on reject | Already superseded by explicit request epochs and block state, which release rejected requests without adding a dependency. |
| `8208b890fb14499f004a6ccdbb26f98a1d8f4da6` | Handle late blocks | Incompatible as implemented upstream: fork rejects unsolicited/stale blocks using active request epoch, alignment and exact-length checks. |
| `f43cde419f3860ad2f6676557d52d6c0060b7437` | Revert I2CP reconfigure fix | Already reflected in local reconfigure behavior; absent options preserve current publication state. |
| `95d16ec8b72e32b0ff8b36b47ae00c1ee82f4216` | Merge I2CP revert PR #2623 | Merge-only; disposition is carried by `f43cde41` and `8971381b`. |
| `8971381bfa4611b233baa8825744d094746634d2` | Correct `dontPublishLeaseSet` reconfigure use | Already integrated: publication state changes only when the option is present and applied successfully. |
| `6b214f74d2c1b9d892cda29119f0942097cbfd85` | Avoid re-request after reject without replacement | Already superseded by local outgoing-request release and exact availability/request state checks. |
| `ba38945eefe28eec8d02d63241d4903a8c5145b1` | Use `find_first_off` with Boost 1.90 | Not applicable: optimization requires a newer Boost floor and its dynamic-bitset representation is not used locally. |

The three applied changes are source-level adaptations, not copied commits.
The torrent reorder fix has no isolated regression seam without exposing
private queue behavior; record this as a CI review item and add focused
coverage if the queue can be exercised without widening the public API.
All source changes remain pending manual CI compilation and relevant tests.
This range contains no distinct OpenSSL/LibreSSL, Java/Android, C++23
language-floor, or protocol wire-format change. No local build or tests were
run.

## Imported sources

| Project | URL | Branch | Commit | Imported into |
|---|---|---|---|---|
| Documentation | https://github.com/PurpleI2P/i2pd_docs_en | default (`master`) | `30ddc4c7c74e055fc858ecff3ae6f69d1a73ab5e` | `docs/` (upstream files; fork reports coexist) |
| Tools | https://github.com/PurpleI2P/i2pd-tools | default (`master`) | `ca7ece8da86aa80b58fce6fa64375bfd75cbf843` | `tools/` |
| Router source | https://github.com/PurpleI2P/i2pd | `openssl` | reviewed through `5bcf6101a456dc991e44d3d79cb69994c7bae484` | existing main tree; selective adaptation, no bulk import |

## Reviewed router-source commits (2026-10-18/19/20 UTC)

The following commits were reviewed from `PurpleI2P/i2pd` branch `openssl`.
The window is expressed in UTC and grouped by the author timestamp; the
committer timestamp is used only when it differs, as noted below. Upstream
authors use `-04:00`, `+02:00` and `+03:00`, so a commit's local calendar date
is never a grouping key: the three SSU2 commits dated 18 October `-04:00` are
19 October UTC, while several 20 October `+02:00`/`+03:00` commits are
19 October UTC. `de32c8c3` was the observed branch tip at the end of the first
review; the continuation section below covers the later commits through
`9725b8b2`. This is a review record, not a merge base or a claim that the fork
is at upstream 3.0.0.

Do not enumerate the window with `git log --since/--until` alone: it filters
on the committer timestamp in the local timezone and reproduces the off-by-one
grouping. Use `%H %aI %cI %s`, convert both timestamps with `date -u`, and
account for merge commits separately. The table retains eight earlier
18 October rows for context; **15 commits** fall in this first table (21 in the
19–20 October UTC window once the continuation section is included).

| Upstream commit | UTC date | Subject | Local result |
|---|---|---|---|
| `18340106ec7623544e0811192c2acd8439d5d626` | 2026-10-18T01:19:38Z | Use `stringstream::view` and check `getline` EOF | Already present through local `9c54986d` in the request-side HTTP tunnel paths. |
| `475f40f8d925afc7519ffdfa59144a62b2a39017` | 2026-10-18T01:55:50Z | Use `view` in the server tunnel and count headers for the connection | Already present through local `9c54986d`; the fork keeps the member header counter and 8192-byte limit. |
| `7dfc05c098cc62a4e3144d2bb54cf985c14c2c68` | 2026-10-18T13:38:10Z | Do not save a piece of a removed torrent | Adapted: weak capture plus authoritative session-membership checks. The 2026-10-21 local follow-up adds irreversible instance tombstones, serialized cleanup and a hash reservation before re-add; regressions are prepared, pending manual CI. |
| `77ebf7516ea44cfe0960831c9161d20983f64130` | 2026-10-18T14:40:57Z | Do not save piece blocks for a stopped torrent | Applied through `Torrent::CanAcceptPieceBlock`, preserving the fork's exact offset/length checks; the piece-level mutex remains in `Piece::BlockReceived`. |
| `9c3a742dc0ae8e5aa4c104683f604185fa3639cb` | 2026-10-18T14:46:15Z | Allow connections from the Debian 12 i2pd package | Applied: the general peer floor is 0.9.57; `NETDB_MIN_HIGHBANDWIDTH_VERSION` remains 0.9.62. The policy is test-pinned; its rationale is recorded in this sync record. |
| `942d6907d2b2cfa73dceaf117457329d251f4191` | 2026-10-18T14:49:01Z | Display the torrents table after the first added torrent | Not applicable: upstream changes `contrib/torrents/web/main.js`; this fork deliberately removed the browser console. The native TUI already keeps the table visible and refreshes after commands. |
| `e3856a38fa0fe310726a39faa9df16f035c1bf69` | 2026-10-18T15:01:02Z | Use `stringstream::view` in the server response and IRC paths | Applied the response/IRC lifetime conversions; the incomplete-header compaction is covered by the `475f40f8` row. The 2026-10-21 local follow-up extracts the production HTTP header parser and prepares `test-i2ptunnel-http` plus fuzz coverage; socket/IRC validation remains pending. |
| `ec19bfb55d9fb8f4c9f35f3637c8b3c1ca33152c` | 2026-10-18T21:49:36Z | Upstream 3.0.0 release metadata | Not imported as-is. The fork remains 2.61.0/0.9.70 and owns its Debian/RPM/changelog history; changing the advertised version is a separate release-identity decision. |
| `452da43bbee0bbf0cb14489bdacce7a08c1936e9` | 2026-10-19T01:21:14Z | Reject RelayRequest from a non-established SSU2 session | Already present and stronger: `HandleRelayRequest` requires `IsEstablished`, plus the fork's state whitelist and bounded validation. |
| `54068be9ca73bfb677617edc330b01eb8f983f37` | 2026-10-19T01:38:25Z | Reject RelayIntro, RelayResponse and PeerTest from a non-established session | Already present and stronger. RelayResponse deliberately permits the `Introduced` hole-punch state; PeerTest additionally requires a remote identity. |
| `472ef88b0eb3fbc3eba5fe8e0b9336a9b8df4499` | 2026-10-19T01:56:37Z | Reject PathChallenge from a non-established session | Adapted: explicit dispatch guard and state helper. `SendPathResponse()` keeps its independent established/address guard. |
| `37090fe09f7effbf65af371e71d3f4df0393514f` | 2026-10-19T11:51:09Z | Add Intel AMT ports to the reserved-port list | Applied: 16992-16995 added to `IsPortInReservedRange`; `test-routercontext-port-range` pins the complete table and the Intel AMT boundaries. |
| `38cf542b13c5d0a39ec42a38522fe08835f6cb71` | 2026-10-19T21:55:52Z | Lower the Debian Boost floor to 1.81 | Not applied as-is: the fork deliberately retains Boost.ProgramOptions 1.74 and already requires Boost.JSON/URL 1.81. Raising the program-options floor adds no needed API and would narrow supported Debian builds; GCC 13 remains the real Debian floor. |
| `f8a12d9dc757a4d225922d8c1b5ec63732ccbf4e` | 2026-10-19T22:14:46Z | Re-add pinned Boost 1.81 alternatives for bookworm | Not applicable: Debian bookworm ships GCC 12, below the fork's GCC 13/C++23 baseline. Alternative Boost package names do not solve the compiler floor. |
| `357fcc14d5b273fbaa67ef8e468a32a8d39ec7e4` | 2026-10-19T22:37:43Z | Add Ubuntu jammy packaging and refresh the UPnP patch | Omitted: `contrib/debian/` and `debian/patches/` were deliberately removed; the patch targets the old GNU make/`filelist.mk` build. Jammy's GCC 11 also cannot build this C++23 tree. The maintained Debian packaging is CMake/Ninja based. |
| `db761a5455e6ca89c1fcc6d135ff634cf17c5dd8` | 2026-10-19T23:31:36Z | Add a CMake torrents switch and RPM dependency gating | CMake half already present and stronger: `WITH_TORRENTS=ON`, target-scoped `NO_TORRENTS`, and Boost.JSON/URL gated on the option. RPM half adapted for Fedora `boost-json`/`boost-url`. The RHEL 9 `-DWITH_TORRENTS=OFF` branch is omitted: its default GCC 11 cannot build this C++23 tree and no validated `gcc-toolset-13` RPM path exists. |
| `a8915cd1d682d168d1a285a465fb2d8296176497` | 2026-10-19T23:56:57Z | `[rpm] fix typo` (missing line continuation) | Not applicable: the fork has no RHEL torrent block after the preceding omission; both specs retain valid `cmake --build` invocations and `build-system-static` rejects direct make/ninja macros. |
| `21ad7d43c81021877a1c01f1bb3e6dc0a11ab708` | 2026-10-20T01:35:49Z | Fix reseed defaults in the sample config | Partially adapted: the fork rejects the `reseed.verify=false` wording because it is fail-closed (`reseed.verify=true`, tested). Applied the built-in reseed-list wording and current URL example in `contrib/i2pd.conf` and `contrib/openbsd/i2pd.conf`; Android has no stale URL example. |
| `e34db4f7cbb3a2732d8742fe5874e8e7ff059e22` | 2026-10-20T01:53:51Z | Merge PR #2604 | Merge only; no tree change (`git diff e34db4f7 21ad7d43` is empty). Disposition is carried by the `21ad7d43` row. |
| `2598a94e4b64cf57eebb33987e6025beefe05b5e` | 2026-10-20T08:44:14Z (committer 08:44:37Z) | Require Boost.URL only on Fedora | Adapted in form: the fork declares Boost.URL only under a defined Fedora guard through F43. The 23-second author/committer skew is retained here; both timestamps are on the same UTC day. |
| `86dd211a08a12b982abab422925dd407f27e7936` | 2026-10-20T10:03:28Z | Do not install Boost.JSON/URL on Mageia | Covered by the Fedora-only Boost condition: Mageia does not install those separate packages. Mageia 9 is below the fork's GCC 13 floor regardless. |
| `233e5111ad5b9b0d4f8636fef6909435ab66dda5` | 2026-10-20T10:10:24Z | Use Ninja on Mageia 9+ | Omitted: the fork uses `cmake --build . --parallel`; it never invokes `make` or `ninja` directly. `%cmake` owns generator selection, and `build-system-static` rejects `_smp_mflags`, `%make_build`, `%ninja_build` and `%ninja` in both specs. |
| `de32c8c3f580c2c43ca09154ecfe6a20db80d19f` | 2026-10-20T10:18:46Z | Use Ninja on Mageia 9 and 11+ | Omitted for the same reason as `233e5111`; this was the previous review high-water mark. |

## Reviewed router-source commits (2026-10-20 UTC continuation)

The branch tip moved from `de32c8c3` to
`9725b8b227293a796c069ac270fa6b5b47cf9a13`. Six commits are in this
continuation. `35e25db2` is not a descendant of `de32c8c3`; its merge base is
`a8915cd1`, and it entered the branch through merge `8710fd89`. The merge
commit adds no independent tree change.

| Upstream commit | UTC date | Subject | Classification and local result |
|---|---|---|---|
| `35e25db2008a45c3beb6fba139976571999e17e3` | 2026-10-20T01:26:38Z | Use proper HTTP status codes for proxy errors | Applicable, implemented by static analysis. Proxy-generated 400/500/502/504 pages now use typed status codes; the fork's 1 MiB header-limit paths use 431 and `HTTPCodeToStatus(431)` was added. `HostNotFound` is 502, upstream failures 502, missing I2 stream 504. `GenericProxyInfo` keeps the default 500 as upstream does. Existing nil-socket and bounded-buffer guards were preserved. |
| `137dde2f433b2142eec7a2307608d7d082be086f` | 2026-10-20T15:10:48Z | `[rpm] update release spec` | No applicable hunk. The Fedora-only Boost.JSON/URL guards predate this commit (`2598a94e`/`86dd211a`); the Ninja/Mageia matrix is obsolete under `%cmake` + `cmake --build`; the `0%{?rhel} <= 9` condition is false-positive when `%rhel` is undefined and incompatible with the Boost 1.81 torrent floor. |
| `c5e8e10a6ca88089756a306386bbed7bce761cac` | 2026-10-20T17:33:44Z | `[build] win: add torrent web ui files to package` | Incompatible and obsolete. It packages `contrib/torrents/web`, which the fork deliberately removed; there is no browser console or matching Windows installer structure to update. Copyright-year edits also conflict with the short LICENSE-reference header policy. |
| `8710fd894136e80afc769a30138ffe99ba461467` | 2026-10-20T19:07:44Z | Merge pull request #2603 from pobregat0/proxy-error-codes | Merge-only. Its tree change is exactly `35e25db2`; disposition is carried by that row. |
| `032b8a61cec906a01c728da2b9ee6135dac89e38` | 2026-10-20T19:55:14Z | Use `istarts_with` instead of `iequals` | Integrated (superseded by a stronger parser). The fork's `I2PTunnelHTTP.h` already uses an ASCII case-insensitive `StartsWith` and the bounded parser; mixed-case Host/Connection/Keep-Alive/X-I2P and lowercase Upgrade regressions were added to `test-i2ptunnel-http`. |
| `9725b8b227293a796c069ac270fa6b5b47cf9a13` | 2026-10-20T22:29:04Z | Get peers and announce torrent to our node if in our bucket | Applicable, implemented by static analysis with fork guards. `DHTTorrent::GetPeers()`/`RemovePeer()`, the reserved local rendezvous slot and `RoutingTable::IsInOurBucket()` empty-leaf fallback support the path; private/stopped/removed suppression, `MAX_DHT_TORRENTS`, per-torrent peer caps, unannounce-on-stop and the fork's seed bit for `noseed` queries are preserved. |

Dependency/impact review: the HTTP commit depends only on the existing
`HTTPRes`/`HTTPCodeToStatus` stack; the `istarts_with` cleanup is already
absorbed by the fork's header-only bounded parser; the DHT change reuses the
existing `TorrentsDHT`, `TorrentsTunnel`, identity and peer-origin APIs with no
new library or wire format. The RPM/Windows rows are packaging-only. The fork
keeps C++23, CMake/Ninja through `cmake --build`, native TUI/Android and no
browser console.

The source changes are implemented by static analysis only and are pending
manual CI compilation and tests. The previous continuation adds the Intel AMT
reserved-port table and its regression, adapts the reseed sample wording
without changing the fail-closed verification default, and carries the
applicable RPM Fedora Boost guard. The 2026-10-20 continuation adds the proxy
error status classification (including the fork-only 431 header-limit path),
the DHT rendezvous self-announce with fork caps/seed semantics, and mixed-case
HTTP tunnel header regressions. No upstream file was copied wholesale and no
merge or cherry-pick was used. `docs/files-changed.md` records the affected
local files; `TODO.md` tracks the remaining NetDb, I2PTunnel, torrent,
packaging and device validation.

The pinned upstream documentation tree contains **46 files**: 39 Markdown
documents, the two branding images (`img/logo.png`, `img/favicon.ico`) and five
tutorial images, with **no `.html` or `.htm` files**. There is therefore no
HTML source to convert at this pin. The only inline HTML in the upstream tree
was the anchor form used by the torrent RPC reference; those anchors were
converted to Markdown heading attributes (`{#auth}`, `{#methods}`,
`{#torrent-fields}`, `{#peer-object}`, `{#file-object}`,
`{#file-stats-object}`, `{#tracker-entry}`, `{#tracker-stats-entry}` and
`{#units}`) and `attr_list` was added to the optional MkDocs configuration.

Coverage of the 46 upstream files is:

- **44 direct imports** under the same path;
- **2 renamed imports** for the nested BitTorrent guide and RPC reference:
  `tutorials/Filesharing/I2PD Built-in Torrent/` upstream maps to
  `tutorials/Filesharing/built-in-torrent/` locally. The mapping is recorded
  once in `contrib/upstream/docs-path-map.txt` and used by `sync.sh`;
- **74 fork-owned local paths** (61 Markdown pages plus 13 assets; C++23, IPC,
  TUI, torrent architecture, Android, CMake/POSIX make, Perl, OpenBSD, security,
  branding and reports) coexist in `docs/` but are not upstream imports.
  Together with the two renamed torrent pages they account for the 76
  local-only paths after the 44 direct imports.

The five tutorial diagrams under `tutorials/media/` are byte-identical to the
pinned upstream tree. `docs/img/logo.png` and `docs/img/favicon.ico` are
intentionally replaced by the official openi2pd assets; upstream branding is
not treated as the fork's identity. License text is preserved unchanged in
`LICENSE` and `about/license.md`.

The old flat `tutorials/Filesharing/built-inTorrent.md` page was removed; it
was a duplicate of the upstream nested guide. The current comparison page
`INTEROPERABILITY.md` replaces the earlier `interop-matrix.md`; the superseded
file was removed to avoid two competing matrices.

If a future upstream commit introduces HTML, convert it manually to Markdown
before importing, preserve headings, tables, diagrams/images, code blocks,
anchors and links, and record the source URL/commit in this document. Do not
add an HTML converter, JavaScript runtime or download step to the daemon build.

## Imported-file rules

- Upstream license and copyright notices are preserved:
  `docs/LICENSE`, `docs/about/license.md`,
  `tools/LICENSE`, and copyright headers in source files.
- Tool provenance, imported/local-only files and per-executable coverage are
  recorded in `docs/tools/provenance.md`; `tools/test-tools-inventory.cmake`
  checks the manifest and source inventory.
- Documentation provenance is split into three explicit inventories:
  `contrib/upstream/docs-files.txt` (upstream paths),
  `contrib/upstream/docs-path-map.txt` (renamed upstream paths) and
  `contrib/upstream/docs-extra-files.txt` (LICENSE/README/MkDocs). Fork-owned
  pages are not copied from upstream and must not be added to these manifests.
- The upstream `mkdocs.yml` was adapted to `docs_dir: .`, the fork name and the
  integrated navigation; MkDocs is optional for publishing static docs and is
  not a daemon, build or runtime dependency. It is not invoked by CMake, the
  router, the TUI or any test that the daemon needs to run.
- `tools/Makefile`, `tools/CMakeLists.txt`, `dependencies.sh`,
  `.gitmodules` and the `i2pd` submodule directory were **not** imported.
  `tools/CMakeLists.txt` is a new in-tree build definition.
- Tool sources were adapted to C++23 and the in-tree libraries. Functional
  changes are listed in `security-audit.md` and `files-changed.md`.
- Fork-specific user guides (C++23, multiprocess IPC, TUI, native BitTorrent,
  Android, CMake/POSIX make, Perl and OpenBSD) are maintained locally and are
  not overwritten by a bulk upstream copy.

## Fork documentation map

| Area | Local source of truth |
|---|---|
| C++23 and ownership | `docs/cpp23-migration.md`, `docs/devs/building/requirements.md` |
| Multiprocess IPC and privilege separation | `docs/security/privilege-separation.md`, `docs/security/ipc-protocol.md`, `docs/security/process-capabilities.md`, `docs/security/windows-worker-ipc.md` |
| TUI | `docs/tui-ipc.md`, `docs/TUI-WINDOWS.md`, `docs/user-guide/run.md`, `tui/` |
| BitTorrent | `docs/torrent/`, `docs/tutorials/Filesharing/built-in-torrent/`, `libi2pd_client/Torrents*` |
| Android | `docs/android-modernization.md`, `docs/android-java25.md`, `docs/android-jni-status.md`, `docs/devs/building/android.md`, `android/` |
| CMake and POSIX make | `docs/build-system.md`, `docs/dependencies.md`, `docs/devs/building/unix.md` |
| Perl | `docs/development/perl-migration.md`, `docs/tools/i2pdctl.md`, `tools/scripts/` |
| OpenBSD | `docs/security/openbsd-validation.md`, `contrib/openbsd/`, `privsep/OpenBSDSandbox.*` |
| Current Java interoperability | `docs/INTEROPERABILITY.md` (the legacy `interop-matrix.md` was removed as a duplicate) |

## Reproducible comparison

```sh
# Confirm whether the pinned upstream branch has moved (no tracked writes).
git ls-remote https://github.com/PurpleI2P/i2pd_docs_en HEAD refs/heads/master

# Router source: fetch into a read-only tracking ref without touching the
# worktree, then verify the pin used by this document.
git fetch --no-tags https://github.com/PurpleI2P/i2pd.git \
  openssl:refs/remotes/upstream/openssl
git rev-parse upstream/openssl

# Documentation only: coverage checks plus read-only diffs.
contrib/upstream/sync.sh --docs

# Tools only
contrib/upstream/sync.sh --tools

# Both
contrib/upstream/sync.sh --all
```

The script:

1. Clones the exact pinned commits into a temporary directory.
2. Checks that every file in the upstream `docs/` tree is represented in
   `docs-files.txt`; reports any new upstream file as `UNTRACKED UPSTREAM`.
3. Rejects a pin that contains `.html`/`.htm` files until they are converted
   manually to Markdown.
4. Resolves the two renamed BitTorrent paths through
   `contrib/upstream/docs-path-map.txt` and verifies every local target exists.
5. Prints a unified diff for every file listed in `docs-files.txt`,
   `docs-extra-files.txt` and `tools-files.txt`.
6. Removes the temporary directory on exit.
7. **Never writes to the repository.** It exits non-zero for an unclassified
   upstream file, an HTML source or a missing local target; normal content
   differences are printed for manual review and do not fail the run.

Review each diff manually. Upstream changes that conflict with fork-specific
requirements must be rejected or adapted. In particular, never reintroduce:

- GNU make files or submodule instructions;
- the HTTP administrative console, `[http]` options or browser torrent UI;
- C++17/C++20-only build paths;
- hidden downloads during configure/build;
- external `i2pd` copies instead of the in-tree libraries.

### Link and content check

The documentation is Markdown; `docs/mkdocs.yml` is an optional publication
configuration. Before accepting a sync, check relative links, images and local
heading/anchor fragments with the offline checker, and review code examples
against the local tree and the real source (MkDocs itself is not required):

```sh
perl tools/scripts/check-doc-links.pl docs
perl tools/scripts/validate-branding.pl
git diff --check -- docs contrib/upstream
```

- `docs/about/contributing.md` and `docs/testing.md` describe the static checks.
- `docs/index.md` is the published documentation home and `docs/README.md` is its source-tree pointer; `docs/developer-guide/README.md`
  is the developer entry point; `docs/mkdocs.yml` is publication-only.
- The upstream pages that mention the removed web console are adapted locally
  to the TUI and optional loopback RPC. Do not restore a browser UI.
- Torrent pages must match `libi2pd_client/TorrentsRPC.cpp` and
  `libi2pd_client/TorrentsTunnel.cpp`: POST + `application/json`, loopback-only
  RPC, no CORS/preflight, no static web route and no clearnet fallback.
  `docs/torrent/ipc.md` must match `daemon/TuiServerCommon.cpp` and
  `libi2pd/TuiProtocol.h`.
- TUI keys and views must match `tui/main.cpp` (`TAB_NAMES`, `TAB_KEYS`,
  `HandleKey`): eleven views, `1`-`9`, `v` for Services and `0` for
  Connections, `t` transit confirmation, `l` local log threshold, `L` daemon
  log level, `n` NetDb directory and `/` filtering.
- Android build instructions must match `android/app/build.gradle`,
  `android/gradle/wrapper/gradle-wrapper.properties`, the Android CMake graph
  and `android/tools/java25-probe/check-config.sh`.
- C++23, IPC, Android, CMake, Perl and OpenBSD guides remain fork-owned; merge
  only upstream technical corrections that still apply.
- `docs/user-guide/install.md` must list only supported platforms
  (Linux, OpenBSD, Windows, Android); macOS, FreeBSD and Solaris are explicitly
  unsupported by the CMake entry point and are not advertised as installable.
- `docs/devs/building/requirements.md`, `docs/build-system.md` and
  `docs/devs/building/unix.md` are the CMake/toolchain source of truth. Do not
  reintroduce GNU make, the removed `[http]` console options or upstream
  submodule instructions.

## After a manual sync

1. Update the commit hashes in this document and in
   `contrib/upstream/sync.sh`, then regenerate `docs-files.txt` from the pinned
   upstream tree. Do not add fork-owned pages to that manifest.
2. Add a row to `docs-path-map.txt` when an upstream path is renamed locally;
   remove stale rows. Run `contrib/upstream/sync.sh --docs` and require
   `Coverage OK`.
3. Re-apply C++23, CMake target, TUI, Android, BitTorrent, Perl and security
   changes. Keep the fork-specific pages listed above authoritative.
4. Remove duplicate upstream/locally adapted pages instead of retaining two
   sources of truth; update the MkDocs nav and every index link.
5. Run the relative-link/image check, branding validation and
   `git diff --check`. Review anchors after any heading rename.
6. Update `docs/files-changed.md`, `docs/attribution-audit.md`,
   `docs/android-modernization.md` (if Android changed), `ROADMAP.md`,
   `PROGRESS.md` and `TODO.md`.
7. Let manual CI perform configure/build/CTest and Markdown publication checks;
   automatic CI remains `workflow_dispatch` only. MkDocs is still optional and
   is not added to the daemon build or runtime.

## Post-high-water verification (2026-10-21)

A fresh read-only `git ls-remote` and clone check on 2026-10-21 confirmed that
`9725b8b227293a796c069ac270fa6b5b47cf9a13` was the `openssl` branch tip at
that time; the continuation section above supersedes this historical claim.
`git rev-list --all --not 9725b8b2 --count` was **0** at that time; fifteen
further commits now exist up to `5bcf6101` and are classified in the
continuation section above. The six commits in `de32c8c3..9725b8b2` remain the
first continuation table; no upstream file was copied wholesale and no
merge/cherry-pick was used.

The verification pass closed fork-side integration gaps exposed by that
continuation:

- `9725b8b2` now removes our own rendezvous peer on stop/remove
  (`DHTTorrent::RemovePeer`, `TorrentsDHT::Unannounce`), reserves one local
  slot when remote announcers fill `MAX_DHT_PEERS_PER_TORRENT`, checks
  `Torrent::IsRemoved`, and matches upstream's empty-bucket fallback in
  `RoutingTable::IsInOurBucket`. The DHT interop/security regressions pin the
  multi-bucket predicate, local reserved slot and removal.
- `35e25db2` status constants moved to `HTTPProxy.h` with
  `HTTPProxyFailureStatus()`; `test-http-proxy-limits` pins 400/431/500/502/504
  and `test-http-res` serializes each proxy-generated status line.
- The bounded `I2PTunnelHTTP` parser now rejects obsolete obs-fold
  (leading SP/HTAB) header continuations, with mixed-case
  `Proxy-Connection` and folded-header regressions.
- The 137dde2f row records why even its RHEL conditional is unsafe to copy
  (`0 <= 9` when undefined), not merely why this fork omits it.

These are static-only changes pending manual CI compilation/tests; no new
upstream commit or dynamic interoperability evidence is claimed.


## Continuation 9725b8b2..5bcf6101 (2026-10-21..24 UTC)

A fresh `git ls-remote https://github.com/PurpleI2P/i2pd refs/heads/openssl`
returns `5bcf6101a456dc991e44d3d79cb69994c7bae484`. `9725b8b2` is no longer
the tip: 15 commits are in the continuation. The range touches only seven
files (`Makefile.linux`, a Debian UPnP patch, `libi2pd/Signature.cpp` and the
`libi2pd_client/Torrents*` files); no SSU2/NTCP2/I2NP/netDb/Garlic change,
dependency-floor change, web-console restoration or bulk import is involved.
`git diff --stat 9725b8b2..5bcf6101` is 7 files, +128/-43. All rows below are
static dispositions; no workflow was dispatched.

| SHA | Subject | Disposition |
|---|---|---|
| `b31431ba` | check DSA signing result instead of dereferencing NULL signature | **Equivalent (fork stronger).** `Signature.cpp` already checks `MDCtxPtr`, `EVP_DigestSignInit`/`EVP_DigestSign`, DER decode, `r`/`s`, and zeroes the signature on every failure; the LibreSSL `DSA_do_sign` NULL path is guarded too. No hunk applied. |
| `b77073bf` | Merge PR #2606 | **Merge-only**; disposition carried by `b31431ba`. |
| `05d05ff6` | eliminate extra copy of block to send | **Integrated with adaptation.** `SendPieceMsg` now builds the 13-byte header in `std::array` and passes a header/payload buffer pair to `SendBuffer`, which copies once synchronously before `Stream::Send` posts the owning buffer. The caller's `Piece::CopyBlock` vector therefore still outlives only the call, as before. `m_NumPieces` and the completion/abort handler are unchanged; `PIECE_MSG_LENGTH` added. |
| `1a5c0bde` | handle cancel msg | **Integrated with fork divergence.** Added BEP 3 message id 8 and exact 12-byte parser; removes only a queued request and does not interrupt an in-flight send. Unlike upstream, the reject is gated on `m_IsFast` to preserve BEP 6 behavior. |
| `18a73d64` | send not interested when complete | **Integrated.** Added `PeerConnection::TorrentComplete()`: close seeds, reset stats, release outgoing requests and send *not interested* to remaining peers; the tunnel completion closure now calls it. |
| `e7660d0b` | not interested only if peer doesn't have more pieces | **Integrated.** `GetNextBlockToRequest` now checks `HasIncompletePieces(m_RemoteBitfield)`; choke and remote-not-interested handlers clear the corresponding rate. The fork's epoch-based `ReleaseOutgoingRequests()` remains the request-accounting implementation. |
| `c87decbe` | set datagram tracker port to 6969 if omitted | **Integrated with a deliberate difference.** Added `DATAGRAM_TRACKER_DEFAULT_PORT` and applies it at both datagram send/connect use sites. The stored tracker URL is not rewritten, because `IsTorrentTracker` compares the exact `URL::to_string()` identity against the torrent announce. |
| `bb302d89` | set filesystem error if can't create directories/part files | **Integrated with fork adaptation.** Added `eTorrentErrorFilesystem` and `"Filesystem error"`, made `Torrent::m_Error` atomic because `InitTorrentFiles` runs on the disk worker, and now checks `CreateAndReserveFile` failure (which also covers missing directories) without ever falling back to parent filesystem IO, and the initial tracker announce moved behind `InitTorrentFiles` on the disk worker so a torrent whose files could not be created is never announced as a live peer. A freshly created part file still does not mark its file `present`. The fork goes further than upstream: the add-time disk job and its announce continuation carry a `weak_from_this()` owner reference instead of raw `this`, every announce path (initial, periodic, start, reannounce) skips a torrent with a non-zero error, and a later verified piece write clears `eTorrentErrorFilesystem` so a transient preparation failure is not permanent. `TorrentErrorStr` stays English. |
| `a646ac2c` | check if received block was applied | **Integrated (security/memory).** `Piece::BlockReceived` now returns true only for a newly applied block; `HandlePieceMsg` gates completion/hash/save and download accounting on that result. This prevents duplicate/unsolicited blocks on a complete piece from forcing a re-hash, disk rewrite or invalidation. The fork's stricter alignment/length checks are retained. |
| `e64e5109` | close torrent's files on stop | **Integrated.** The existing disk-service resume post now closes every `TorrentFile` before `SaveTorrentResumeFile`; it captures the strong `shared_ptr<Torrent>` and never raw `this`. Payload/SAF handles are released on stop rather than waiting for the 90-second inactivity sweep. |
| `5bcf6101` | reset piece if not able to save | **Equivalent (fork stronger).** `Piece::Dump` already returns `bool`, refuses unverified writes and the caller invalidates the piece on failure. Only the small `m_UploadRate = 0` reset on remote *not interested* was adopted. |
| `14261bb7` | fix Makefile.linux logic | **Omitted: no GNU make.** `Makefile.linux` does not exist; GCC 13/Clang 17 are hard floors. Boost.JSON/URL, `NO_TORRENTS`, static-Boost discovery and atomics are already modelled in CMake, which fails configure instead of silently disabling torrents. Do not import the compiler-version heuristic. |
| `b1a30e99` | fix UPnP patch | **Omitted: obsolete packaging path.** `debian/patches/` was deliberately removed; CMake `WITH_UPNP` defaults ON and every maintained package supplies MiniUPnPc. `daemon/UPnP.cpp` and `FindMiniUPnPc.cmake` did not change in this window and already exceed the legacy patch. |
| `20e30320` | Merge PR #2610 | **Merge-only**; no independent tree change. |
| `597ef7a5` | Merge PR #2609 | **Merge-only**; no independent tree change. |

Fork guards retained in the adapted torrent paths: private-swarm/DHT/PEX
suppression, `IsStopped()`/`IsRemoved()` checks, pending-write withholding,
worker-mediated storage authority, bounded queues and the no-fallback rule for
isolated/SAF payload storage. The web console, browser RPC assets, GNU make and
the Debian quilt series remain absent. `b31431ba` confirms that a provider
without DSA can return a null signature; openi2pd already fails closed there,
but `Signer::Sign` remains `void` and all-zero publication is still a tracked
future interface change rather than a validated fix.

The old "post-high-water verification (2026-10-21)" section below is retained
as history. Its tip claim is superseded by this continuation.
