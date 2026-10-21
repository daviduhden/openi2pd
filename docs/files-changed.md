<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: files changed.

Records the file-level changes, documentation updates and static validation
status for the current repository increment.

-->

# Files created, modified and removed

This list covers the refactoring described in the other documents. No commit
was created.

## Created

### Runtime and IPC

- `daemon/TuiServer.h` — local UNIX socket server declaration.
- `daemon/TuiServer.cpp` — bounded read-only IPC, peer credentials, snapshots.
- `libi2pd/TuiProtocol.h` — versioned request/response parser and framing.
- `tui/TuiClient.h`, `tui/TuiClient.cpp` — socket client and option/config
  resolution.
- `tui/main.cpp` — shared views, refresh loop, key handling.
- `tui/Terminal.h`, `tui/TerminalUnix.cpp`, `tui/TerminalWin32.cpp` — terminal
  abstraction and platform backends.
- `tui/TuiClientWin32.cpp`, `tui/TuiWin32Ipc.{h,cpp}` — Windows named-pipe
  client and pipe-name/handle helpers. The listener DACL is built in
  `daemon/TuiServerWin32.cpp`.
- `daemon/TuiServerCommon.{h,cpp}`, `daemon/TuiServerWin32.cpp` — shared
  snapshot/dispatch code and the Windows listener.
- `tests/test-tui-terminal.cpp` — portable terminal key/attribute contract test.
- `tests/test-tui-pipe-name.cpp` — portable product pipe-name derivation test.
- `tests/test-tui-win32-pipe.cpp` — Windows pipe/DACL primitive test.
- `docs/TUI-WINDOWS.md` — Windows TUI/IPC build, security and validation guide.

### Tests and fuzzing

- `tests/test-tui-protocol.cpp` — protocol and framing regressions.
- `tests/fuzz/fuzz-tui-protocol.cpp` — libFuzzer target.
- `tests/fuzz/fuzz-http-parser.cpp` — libFuzzer target.

### Android

- `android/` — Gradle project and Java sources imported from
  `PurpleI2P/i2pd-android`.
- `android/app/src/main/cpp/` — JNI sources and CMake wrapper; replaces
  `ndk-build` and the submodule.
- `android/app/src/main/AndroidManifest.xml` — app-specific storage, no WebView
  console, no broad storage permission; non-exported same-process router
  service with `specialUse`/`dataSync` foreground types.
- `android/app/src/main/cpp/DaemonAndroid.cpp`, `i2pd_android.cpp` — checked
  native start result and failure cleanup, mutex-protected data-dir/language
  state and ordinary-UTF-8-to-UTF-16 JNI string conversion.
- `android/app/src/main/java/org/openi2pd/router/RouterService.java`,
  `RouterRepository.java` — explicit start/stop/restart, timeout shutdown and
  lifecycle-safe binder ownership.
- `android/app/src/androidTest/java/org/openi2pd/router/RouterServiceLifecycleTest.java`
  — bind/unbind instrumentation smoke test; not run.
- `android/app/src/main/assets/certificates` — corrected the dangling
  upstream symlink to the in-tree `contrib/certificates` directory.
- `android/tools/java25-probe/check-config.sh` — compile-free Java 25,
  Gradle/AGP/SDK/NDK and JDK-only-API guard used by CI.
- `android/README.md`, `docs/devs/building/android.md`,
  `docs/android-java25.md`, `docs/android-modernization.md` — integrated build
  instructions and explicit javac/D8/R8/ART limits.
- `MainActivity.java`, `RouterRepository.java`, `RouterViewModel.java`,
  `DashboardFragment.java`, `NetworkFragment.java`, `StatisticsFragment.java`,
  `I2PdQSTileService.java`, `LogAdapter.java` — static GUI/UX pass: branded
  toolbar/rail, detail Up/navigation state, foreground-gated polling and
  snapshot deduplication, quick actions, safer tile lock handling, chart sample
  gating and fewer duplicate TalkBack announcements. `MainActivityNavigationTest`
  is prepared for device navigation/back-stack coverage.
- `res/layout-w600dp/activity_main.xml`, `res/layout/activity_main.xml` — a
  window-width rail for tablets/split-screen and a wrapping bottom navigation
  for phones; the old duplicated `layout-land`/`layout-sw600dp` variants were
  removed.
- Android layouts/styles — section headings, live-region cleanup, tabular
  metric figures, branded card surfaces and a decorative About logo.
- Android backup/data-extraction rules and manifest hardening — explicit
  no-backup domains, Recents screenshot suppression, RTL/back-callback support
  and IME resize behavior.

### Privilege separation

- `privsep/Protocol.h`, `Channel.{h,cpp}`, `LogWorker.{h,cpp}`,
  `Worker.{h,cpp}`, `RunWorker.{h,cpp}` — IPC, log worker and child lifecycle.
- `daemon/i2pd.cpp`, `daemon/Daemon.cpp`, `libi2pd/Log.*` — worker entrypoint,
  spawn/configure/shutdown and external log sink.
- `tests/test-privsep-protocol.cpp`, `tests/test-privsep-log.cpp` — framing/bounds/channel and worker regression.
- `docs/security/privilege-separation.md`, `ipc-protocol.md`,
  `process-capabilities.md`, `threat-model.md`.

### Visual identity

- `assets/branding/` — vector logo masters (`logo.svg`,
  `logo-horizontal.svg`, `symbol.svg`, light/dark/monochrome variants),
  rasterized `logo.png`/`symbol.png`, the supplied `logo-original.png`
  reference, and canonical `colors.md`.
- `docs/branding/` — [overview](branding/README.md),
  [logo usage](branding/logo-usage.md), [colors](branding/colors.md) and
  [asset inventory](branding/assets.md).

### Documentation

- `docs/architecture.md`
- `docs/interop-matrix.md`
- `docs/dependencies.md`
- `docs/cpp23-migration.md`
- `docs/build-system.md`
- `docs/tui-ipc.md`
- `docs/security-audit.md`
- `docs/testing.md`
- `docs/files-changed.md` (this file)
- `docs/attribution-audit.md`
- `docs/upstream-sync.md`
- `docs/INTEROPERABILITY.md`
- `docs/FEATURE-PARITY.md` — functional/administrative parity matrix for
  Java 2.13.0, TUI, Android, torrents and security boundaries
- `docs/android-modernization.md`
- `docs/REBRANDING.md`
- `TODO.md`, `ROADMAP.md`, `PROGRESS.md`, `AGENTS.md`
- `docs/architecture/README.md`
- `contrib/artwork/open-i2pd-icon.svg` (now generated from `assets/branding/symbol.svg`)
- `docs/tools/` (per-tool documentation)
- `docs/developer-guide/`, `docs/protocols/`, `docs/security/` (integrated
  documentation indexes)
- `contrib/upstream/sync.sh`, `contrib/upstream/*-files.txt` (pinned provenance
  and manual comparison)

## Modified — functionality

| File | Change |
|---|---|
| `libi2pd/Base.cpp`, `Base.h` | constexpr strict base64, `std::span`, empty base32 fix |
| `libi2pd/HTTP.cpp` | header OWS/token validation, strict lengths, chunk extensions, URL port, vector buffers |
| `libi2pd/Reseed.cpp` | checked SU3 reads, bounded allocations/signature, ZIP header initialization |
| `libi2pd/NetDb.cpp`, `NetDb.hpp` | locked counters, atomic reseed state and serialization |
| `libi2pd/Transports.cpp`, `Transports.h` | peer count/snapshot with lock, transport bitmask, C++23 `contains` |
| `libi2pd/Log.cpp`, `Log.h` | bounded recent-log ring and thread-safe snapshot accessor |
| `libi2pd/Crypto.cpp`, `Garlic.cpp` | constant-time integrity comparisons |
| `libi2pd/Config.cpp` | removed `[http]` options; added `[tui]`; Android default off |
| `daemon/Daemon.cpp`, `Daemon.h` | removed web console lifecycle; start/stop TUI IPC; OpenBSD path handling |
| `daemon/UnixDaemon.cpp` | unveil TUI socket path (`rwc`) without new pledge promises |
| `daemon/I2PControlHandlers.cpp` | locked peer count |
| `Win32/Win32App.cpp` | removed console menu and URL handler |
| `tests/test-base-64.cpp` | strict decode, padding, span regressions |
| `tests/test-http-req.cpp` | header OWS/token and Content-Length regressions |
| `tests/test-http-merge_chunked.cpp` | extension and malformed-size regressions |
| `tests/test-http-url.cpp` | port validation regressions |
| `build/CMakeLists.txt` | split Unix/Windows TUI and TuiServer sources; Win32 TUI target and install wiring |
| `tests/CMakeLists.txt`, `tests/test-tui-protocol.cpp` | Windows pipe test registration and exact framing/size limits |
| `.github/workflows/build-windows.yml` | build `i2pd-tui` in MSYS2 and add a manual native MSVC/vcpkg job |
| `.gitignore` | ignore local `audit/` notes |
| `docs/TUI-WINDOWS.md`, `docs/tui-ipc.md`, `docs/build-system.md`, `docs/testing.md`, `docs/devs/building/windows.md`, `docs/user-guide/{install,run}.md`, `docs/architecture.md`, `docs/ci.md` | Windows TUI/IPC documentation and validation status |

## Modified — tools and firewall

| File | Change |
|---|---|
| `tools/vain.cpp`, `tools/vanity.hpp` | RAII key buffers wiped on exit, `std::jthread` workers joined before writes, checked key layout/bounds/regex, base32 prefix validation, standard positional parsing |
| `tools/firewall.hpp` | numeric-only address grammar; scope/bracket/link-local/IPv4-compatible/IPv4-mapped/broadcast rejection; outbound-only nft chain validation; display-name tag validation; canonical endpoint/family/transport revalidation at the rendering boundary |
| `tools/routerinfo.cpp` | reject `--nft-*` options when `--firewall=nftables` is not selected; reject malformed/unreadable RouterInfo before hash/address access |
| `tests/test-routerinfo-firewall.cpp`, `tools/test-tools.cmake` | deterministic all-backend rendering, outbound, address, nftables, tag, malformed-input, port-boundary, rendering-boundary and per-backend malformed-CLI regressions |
| `docs/tools/routerinfo.md`, `tools/README.md` | link-local/compatible rejection, outbound guarantees, syntax-validation procedure, malformed-input behavior and explicit `-6` IPv6 selection for every backend |
| `docs/tools/vain.md` | pattern/output naming, validation and key-buffer security notes |

## Modified — C++23 cleanup

The following files lost unreachable C++20 fallback branches or stale Boost
version branches; comment-only changes are included:

`libi2pd/Destination.cpp`, `Destination.h`, `RouterContext.cpp`,
`RouterContext.h`, `RouterInfo.h`, `Tag.h`, `Timestamp.cpp`, `Timestamp.h`,
`Transports.h`, `util.cpp`, `Tunnel.h`, `TunnelPool.h`, `TransitTunnel.h`,
`Streaming.h`, `LeaseSet.h`, `ECIESX25519AEADRatchetSession.h`, `Garlic.h`,
`NTCP2.cpp`, `libi2pd_client/AddressBook.cpp`, `ClientContext.cpp`,
`HTTPProxy.cpp`, `I2CP.cpp`, `I2PService.h`, `SAM.cpp`, `SOCKS.cpp`,
`SOCKS.h`, `i18n/I18N.h`.

## Modified — build, packaging, CI

| File | Change |
|---|---|
| `build/CMakeLists.txt` | rewritten: C++23 hard requirement, explicit sources, target usage requirements, options, install, TUI, CTest, sanitizers |
| `tests/CMakeLists.txt` | test helper function, all tests, TUI test, fuzz option |
| `build/cmake/i2pdConfig.cmake` | imported targets and `find_dependency` |
| `build/cmake_modules/CheckAtomic.cmake` | C++23 probe |
| `Makefile` | POSIX-only CMake/Ninja front-end |
| `tests/Makefile` | POSIX CTest wrapper |
| `build/build_mingw.cmd` | CMake/Ninja, no `make`, no XP path |
| `android/app/build.gradle` | Gradle 9.8.1/AGP 9.4.1, JDK 25 toolchain, CMake JNI, no hard-coded signing keys |
| `android/.../DaemonWrapper.java` | app-specific storage, new package and data directory |
| `android/.../MainActivity.java`, fragments, `RouterService`, `RouterRepository`, `RouterViewModel` | Material 3 UI and service/observable-state architecture |
| `android/.../I2PDActivity.java`, `SettingsActivity.java`, `MainPreferenceActivity.java`, `ForegroundService.java` | removed/replaced by the native UI/service architecture |
| `android/.../i2pd_android.cpp` | bounded status snapshot and recent-log JNI methods |
| `build/win_installer.iss` | removed webconsole files |
| `debian/rules`, `debian/control` | CMake/Ninja build, new deps, no patches |
| `contrib/rpm/i2pd.spec`, `i2pd-git.spec` | `cmake --build`, no make/ninja invocation |
| `contrib/i2pd.conf`, `contrib/openbsd/i2pd.conf` | `[http]` → `[tui]` |
| `contrib/docker/Dockerfile`, `i2pd-docker.conf` | removed console port/config |
| `contrib/openbsd/README.md` | TUI socket, sandbox and rc.d notes |
| `contrib/openbsd/i2pd.rc`, `i2pd.login` | OpenBSD rc.d service/login class from wip-openbsd-ports |
| `README.md`, `docs/README.md` | C++23/CMake/TUI/manual-CI documentation; openi2pd logo, naming and branding links |
| `docs/index.md`, `docs/mkdocs.yml` | openi2pd identity statement, branding links and MkDocs Branding nav |
| `.github/workflows/build.yml`, `build-openbsd.yml`, `build-windows.yml` | manual-only triggers, Ninja, TUI/Perl test targets, pinned actions, no credential persistence |
| `.yamllint.yml` | workflow lint policy |
| `docs/android-java25.md`, `docs/android-modernization.md` | Java 25, ART/D8/R8 and UI/architecture status |
| `docs/ci.md` | workflow security/lint policy |
| `docs/security/openbsd-validation.md` | OpenBSD sandbox/pidfile/signal validation checklist |
| `privsep/*`, `daemon/Daemon.cpp`, `daemon/i2pd.cpp` | bounded log-worker supervision, channel/fork hardening and reattach |
| `docs/torrent/*`, `docs/development/perl-migration.md` | native torrent, IPC, security, TUI, matrix and Perl migration docs |
| `libi2pd_client/TorrentsControl.*`, `TorrentsTunnel.*`, `Torrents.cpp`, `TorrentsRPC.*`, `TuiProtocol.h`, `TuiServer.*`, `tui/*` | native torrent control, security hardening and TUI |
| `tools/scripts/i2pdctl`, `tools/tests/i2pdctl.t` | Perl replacement and regression tests |
| `tests/test-sam-version.sh` | POSIX loop instead of `seq` |
| `libi2pd/LeaseSet.cpp`, `NetDb.cpp`, `RouterInfo.cpp`, `I2NPProtocol.cpp`, `NTCP2.cpp`, `SSU2Session.*` | protocol-bound, identity-binding, fragment and path-response hardening |
| `tests/test-protocol-bounds.cpp`, `tests/fuzz/fuzz-protocol-bounds.cpp` | parser/identity/I2NP regression and fuzz sources |

## Removed

| Path | Reason |
|---|---|
| `daemon/HTTPServer.cpp`, `HTTPServer.h`, `HTTPServerResources.h` | HTTP administrative console |
| `contrib/webconsole/` | Console CSS assets |
| `filelist.mk`, `Makefile.linux`, `Makefile.openbsd`, `Makefile.mingw` | GNU make/build fragments replaced by CMake |
| `debian/patches/` | Patches applied to the deleted GNU make build |
| `contrib/debian/` | Packaging for EOL Ubuntu releases using the deleted build |
| `[http]` options in configs and `Config.cpp` | Console no longer exists |
| `contrib/torrents/web/` | Torrent browser console HTML/CSS/JS; native TUI replaces it |
| `tools/scripts/i2pdctl` Python content | Replaced by Perl; path/name retained for the installed client |
| Win32 `ID_CONSOLE` menu/handler | Console no longer exists |

## Preserved deliberately

- `libi2pd_client/HTTPProxy.*`, HTTP tunnel code and `[httpproxy]`.
- Reseed code, HTTPS/SU3 handling and reseed options.
- `daemon/I2PControl.*` and its certificate/token handling.
- `libi2pd_wrapper/` and public library headers.
- Torrent engine and I2P-only transport code under `libi2pd_client/`.
- Historical `ChangeLog` references to the web console.

## Validation state

- Static link/image path checks were run against the working tree for the
  documentation changes listed above.
- Visual validation is **pending**: no MkDocs build, browser render, SVG/PNG
  rendering, logo clear-space check or packaging/Android asset review was run
  in this environment.

## Upstream documentation integration (static)

- `docs/tutorials/Filesharing/built-in-torrent/built-inTorrent.md` — moved
  the fork-adapted torrent guide into a nested directory (upstream source path
  recorded in `docs/upstream-sync.md`); removed the flat duplicate.
- `docs/tutorials/Filesharing/built-in-torrent/torrents-rpc.md` — imported
  the official JSON-RPC method reference and adapted it to the fork: POST-only,
  loopback-only, bounded JSON, no CORS/preflight, no static web route and no
  browser UI.
- `docs/user-guide/tunnels.md` — restored the official torrents-tunnel
  settings and documented the removed `web` option and optional loopback RPC.
- `docs/upstream-sync.md`, `contrib/upstream/sync.sh`,
  `contrib/upstream/docs-files.txt` — pinned the current upstream documentation
  commit `30ddc4c7c74e055fc858ecff3ae6f69d1a73ab5e`, regenerated the 46-file
  inventory and documented the no-HTML/Markdown-only state.
- `docs/mkdocs.yml`, `docs/README.md` — navigation and links for the nested
  torrent guide and RPC reference.
- `tools/scripts/check-doc-links.pl` — offline relative Markdown/image checker
  with local heading/anchor validation; wired into the manual Linux workflow.
- `docs/user-guide/run.md` — TUI key/views updated to the current ten views,
  including Connections (`0`), Config (`9`), NetDb (`5`) and the torrent view.
- Upstream-derived pages retain their copyright/license; new fork documentation
  follows the fork-only header rule.

## Visual identity additions (openi2pd)

| Path | Change |
|---|---|
| `assets/branding/` | Vector lockup/symbol/horizontal/light/dark/monochrome, PNG masters, original reference and canonical colors |
| `docs/branding/` | Identity overview, usage, canonical-color link and asset inventory |
| `docs/stylesheets/brand.css` | MkDocs semantic palette |
| `tui/Theme.h`, `tui/main.cpp` | Central ncurses theme, semantic roles and openi2pd-visible torrent hint |
| Android `values/colors.xml`, `values-night/colors.xml`, `themes.xml`, `values-v31/themes.xml` | Material 3 light/dark palette and platform splash |
| Android `mipmap-anydpi-v26|v33/ic_launcher.xml`, `mipmap-*/ic_launcher.png`, launcher/notification/logo vectors | Official adaptive/legacy/themed icons |
| `Win32/openi2pd.ico`, `Win32/Resource.rc*`, `build/win_installer.iss` | Windows icon and user-facing metadata |
| `contrib/openi2pd.desktop`, `contrib/artwork/open-i2pd-icon.svg`, `openi2pd-256.png`, `docs/img/logo.png`, `docs/img/favicon.ico` | Desktop, documentation and packaging artwork |
| `assets/branding/colors.md`, `docs/assets/branding/colors.md` | Canonical light/dark semantic palette and exact MkDocs mirror |
| `tools/scripts/validate-branding.pl` | Static file/SVG/PNG/ICO validation, exact SVG master/mirror/copy hashes, derived icon/mipmap dimensions, brand-vector fills and platform-palette/docs-mirror consistency |
| `libi2pd/Config.cpp`, `libi2pd/FS.cpp`, `libi2pd_client/HTTPProxy.cpp` | Visible version/error/page identity and HTTP proxy palette updated to openi2pd tokens |
| `README.md`, `docs/README.md`, `docs/index.md`, `docs/mkdocs.yml` | Brand navigation, logo and naming |
| `docs/tutorials/http.md`, `docs/tutorials/Monero.md`, `docs/tutorials/blinded-addresse.md` | Removed inherited I2PD Browser promotion and personal guidance; use local tool references |
| `tools/autoconf_i2pd.cpp`, `tools/x25519.cpp`, `tools/firewall.hpp`, `tools/README.md` | Fork documentation links, openi2pd firewall display name and local tool guides |
| `contrib/i2pd.conf`, `contrib/openbsd/i2pd.conf`, `contrib/tunnels.conf`, `android/app/src/main/assets/{i2pd,tunnels}.conf`, `contrib/apparmor/docker-i2pd` | Local documentation links and neutral container example |
| `docs/devs/building/cross.md` | Replaced obsolete GNU Make/Boost 1.60/OpenSSL 1.0.2 upstream instructions |

## License header pass

The current header policy uses a short `See the LICENSE file at the top of the project tree...` reference plus a file-specific description in first-party source, configuration and script files. Duplicated project copyright/SPDX blocks were removed from those files. The legal terms remain in `LICENSE`; existing third-party notices remain in place, including the Apache-2.0 Gradle wrapper, BSL-1.0 and New-BSD CMake modules, Windows SDK header, OpenBSD ports copies, fixtures and CC-BY-SA documentation.

- `.clang-format` — C++ and Java formatting profiles for the maintained source tree.

- `man/i2pd.1`, `man/i2pd-tui.1`, `man/i2pdctl.1` — portable mdoc manual pages; CMake/Debian installation updated.

- `tests/interop/README.md`, `tests/interop/check-reference.sh` — pinned Java reference and controlled differential-test harness.

## Java 2.13.0 static-interop pass

- `docs/INTEROPERABILITY.md` — second audit table, corrections and remaining gaps.
- `tests/interop/README.md`, `tests/interop/check-reference.sh` — pinned reference and controlled harness.
- `libi2pd/SSU2Session.cpp/.h`, `libi2pd/SSU2.cpp`, `libi2pd/RouterInfo.cpp` — SSU2 and RouterInfo transport corrections.
- `libi2pd/LeaseSet.cpp/.h`, `libi2pd/NetDb.cpp` — LeaseSet store binding/validation and netDb replay/race fixes.
- `libi2pd/ECIESX25519AEADRatchetSession.cpp`, `libi2pd/Garlic.cpp/.h`, `libi2pd/Elligator.cpp` — ratchet freshness, PQ NSR, replay cache and parser/boundary fixes.
- `libi2pd/Datagram.cpp`, `libi2pd/Streaming.cpp/.h`, `libi2pd/Destination.cpp` — multi-key matching, RESET and Datagram2 minimum.
- `libi2pd_client/SAM.cpp/.h`, `libi2pd_client/I2CP.cpp` — SAM version/connect/ports and I2CP HostLookup/b33 corrections.
- `libi2pd/Transports.cpp`, `libi2pd/RouterContext.cpp` — LibreSSL PQ capability clamping.

- `TODO.md`, `ROADMAP.md`, `PROGRESS.md` — synchronized with stable I2P-001..I2P-013 workstreams and static/dynamic validation distinctions.

- `tests/interop/FIXTURES.md`, `tests/interop/check-vectors.pl`, `tests/interop/vectors/` — executable static fixture matrix and validator (24 static cases, 1 known checksum gap; no Java output captured).
- `docs/INTEROPERABILITY.md` — I2P-001..I2P-013 evidence table with Java/C++ references and explicit static/dynamic status.

- `libi2pd/TransitTunnel.cpp`, `libi2pd/SSU2Session.*`, `libi2pd/LeaseSet.cpp/.h`, `libi2pd_client/SAM.cpp` — security-review fixes for OBGW tunnel ID 0, SSU2 reorder/fragment counts, HolePunch DateTime, Meta LS freshness and invalid SAM SEND.

- `libi2pd/RouterInfo.cpp/.h`, `libi2pd/SSU2Session.cpp`: bounded RouterInfo option parsing and SSU2 Address family validation (NET-116/NET-117).
- `libi2pd/Log.cpp/.h`, `daemon/UnixDaemon.cpp`: synchronized log rotation, additive OpenBSD pledge policies and daemon readiness handshake.
- `libi2pd/TunnelBase.h`, `libi2pd/RouterContext.h/.cpp`: atomic TUI-visible status/creation fields.
- `libi2pd_client/TorrentsRPC.cpp`: bounded torrent RPC service waits.
- `tests/test-su3-bounds.cpp`: malformed/truncated SU3 header regression cases.

## C++23 ownership/modernization pass

- `libi2pd/Streaming.h/.cpp`, `libi2pd/Gzip.cpp` — owning `SendBuffer`, span overloads and stack/vector gzip buffers.
- `libi2pd/LeaseSet.h/.cpp`, `libi2pd/Identity.h/.cpp` — `unique_ptr` lease-set/identity storage with preserved public views and copy semantics.
- `libi2pd/NTCP2.h/.cpp`, `libi2pd/Transports.h/.cpp` — explicit async buffer ownership and `unique_ptr` transport services/timers/servers.
- `libi2pd/Log.h/.cpp`, `libi2pd/NetDb.hpp/.cpp`, `libi2pd/Tunnel.h/.cpp`, `libi2pd/TransitTunnel.cpp`, `libi2pd/Timestamp.cpp`, `libi2pd/util.cpp` — `unique_ptr` thread ownership and `make_unique`.
- `daemon/Daemon.h/.cpp`, `daemon/UPnP.cpp` — owning pimpl and helper/thread construction.
- `libi2pd_client/SAM.cpp/.h`, `libi2pd_client/I2CP.cpp`, `libi2pd_client/ClientContext.cpp` — shared/vector buffer ownership for async writes and temporary key/identity data.
- `libi2pd_client/TorrentsDHT.h/.cpp` — owning bucket-chain links with non-owning traversal cursors.
- `tests/test-streaming-buffer.cpp` — owning/span `SendBuffer` regression.

## Memory/concurrency security audit

- `libi2pd/SSU2.cpp/.h`, `libi2pd/SSU2Session.cpp/.h`, `libi2pd/SSU2OutOfSession.cpp` — short-packet checks, bounded token/peer-test/reorder state, proxy endpoint lifetime and copy-returning session snapshots.
- `libi2pd/RouterInfo.cpp/.h` — signed-body boundary, option/property duplicate/truncation checks and address-family consistency.
- `libi2pd/Crypto.cpp`, `CryptoKey.cpp/.h`, `Blinding.cpp/.h`, `Garlic.cpp/.h`, `Reseed.cpp` — checked BIGNUM/key bounds, key cleansing, strict b33/ECIES parsing, bounded Garlic write and strict reseed deflate/HTTP limits.
- `libi2pd/Log.cpp/.h`, `NetDb.cpp/.hpp`, `Tunnel.cpp/.h`, `RouterContext.cpp`, `daemon/Daemon.cpp`, `UnixDaemon.cpp`, `UPnP.cpp/.h` — atomic lifecycle flags, join-before-clear ordering, restart/teardown and pidfile identity fixes.
- `daemon/TuiServerCommon.cpp`, `TuiServerWin32.cpp`, `privsep/Worker.cpp` — exception containment, continuous pipe-name ownership and close-on-exec/RLIMIT fd hardening.
- `libi2pd_client/Torrents.cpp/.h`, `TorrentsDHT.cpp/.h`, `TorrentsRPC.cpp`, `TorrentsTunnel.cpp` — bounded peer/DHT/RPC state, locked snapshots, piece/file bounds and resume-file validation.
- New tests: `test-crypto-audit-bounds`, `test-blinding-b33-strict`, `test-reseed-zip-bounds`, `test-ssu2-routerinfo-bounds`, `test-torrents-security`, `test-log-lifecycle`, `test-tui-ipc-regression`.
- New fuzzers: `fuzz-base-blinding`, `fuzz-log-record`, `fuzz-privsep-protocol`, `fuzz-torrents-parsers`.

## Dependency-floor cleanup

- `build/CMakeLists.txt` — explicit OpenSSL 3.0.2/LibreSSL 4.0, zlib 1.2.11 and MiniUPnPc 2.2.6/API 17 checks; Boost 1.74 program_options and conditional Boost 1.81 JSON/URL; removed Boost.Atomic/Filesystem and the `WITH_TUI`/`I2PD_BUILD_TESTS` aliases.
- `libi2pd/FS.h`, `FS.cpp`, `libi2pd_client/AddressBook.cpp` — unconditional `std::filesystem`; removed `STD_FILESYSTEM` and Boost.Filesystem paths.
- `libi2pd/RouterInfo.h/.cpp` — unconditional `std::atomic<std::shared_ptr>`; removed the Boost.Atomic fallback.
- `libi2pd_client/Torrents.cpp` — unconditional `std::atomic_ref`; removed the C++20 capability fallback.
- `libi2pd/Crypto.h`, `Crypto.cpp`, `Signature.h/.cpp`, `Reseed.cpp`, `Family.cpp`, `daemon/I2PControl.cpp` — removed OpenSSL-3.0-only guards while retaining LibreSSL/SipHash and PQ capability checks; SigType 8 now uses plain Ed25519.
- `libi2pd_client/TorrentsRPC.cpp` — removed the redundant Boost version check; CMake now guarantees Boost 1.81 for non-Android JSON/URL builds.
- `docs/dependencies.md`, build/TUI docs and `README.md` — updated minimums, canonical option names and the remaining OpenBSD libc++ gaps.


## Java 2.13.0 recheck (2026-10-18)

- `docs/INTEROPERABILITY.md` — latest-stable recheck, NTCP2/SSU2/data/client/
  crypto rows `NET-118..NET-133`, `LS-104..LS-108`, `DB-101..DB-103`,
  `TU-101`, `SAM-105..SAM-109`, `DG-104`, `STR-101..STR-103`,
  `I2CP-103/I2CP-104` and `CRY-201`/`RT-201`/`GAR-201`/`B33-201`/`ID-201`/
  `SIG-201`; explicit static/dynamic status.
- `libi2pd/NTCP2.*` — replay cache, strict handshake/data-phase block
  ordering, `m3p2len` cap, DateTime/size checks and termination counting.
- `libi2pd/SSU2.*`, `SSU2Session.*`, `SSU2OutOfSession.*`, `RouterInfo.*` —
  one-time tokens, header/version/ConnID validation, PeerTest bounds,
  PQ/version Retry, MTU-aware address metadata, fragment ordering and
  ACK-bearing Termination.
- `libi2pd/LeaseSet.*`, `NetDb.*`, `I2NPProtocol.*`, `Tunnel.*`,
  `TransitTunnel.cpp` — lease freshness/flags/P256 keys, store-type mask,
  reply block, exclusion cap and truncated tunnel inputs.
- `libi2pd/Streaming.*`, `Datagram.*`, `libi2pd_client/SAM.*`, `I2CP.*` —
  packet/option bounds, Datagram2/3 options/first contact, SAM 3.x
  UDP/framing/version/signature/protocol handling, I2CP session date/offline
  signature and HostReply options.
- `libi2pd/Crypto.*`, `Gost.cpp`, `Identity.*`, `Blinding.*`, `Signature.*`,
  `Garlic.*`, `ECIESX25519AEADRatchetSession.*` — GOST/ECIES validation,
  NEXT_KEY reuse, handshake ACK, ElGamal/AES tags, b33/key certificates and
  gated Ed25519ph.
- New tests: `test-ntcp2-session-confirmed-payload`,
  `test-datagram-options`, `test-sam-datagram-framing`,
  `test-leaseset-netdb-2.13-freshness`, `test-ssu2-address-negotiation`,
  `test-ssu2-fragment-ordering`, `test-crypto-wire-validation`.
- Existing test corrections: `test-elligator` boundary expectation,
  `test-privsep-protocol`/`test-privsep-log` `U64BE`,
  `test-ssu2-routerinfo-bounds`/`test-ssu2-address-negotiation` mapped-option
  wire encoding, and the `test-streaming-buffer` vexing parse.
- Validation: local Linux/GCC 16.2.0/OpenSSL 4.0.3 build (196 targets) and
  CTest **39/39 passed**. Static reference/fixture harness: 25 cases (24 OK,
  one documented checksum gap). No Java peer/network, sanitizer, fuzzer,
  OpenBSD/LibreSSL, Windows, Android or multi-UID run.

## NTCP2/SSU2 deep transport pass (2026-10-18)

- `libi2pd/NTCP2.cpp/.h` — completion-driven termination/close, queued
  RouterInfo/termination control frames, per-version hybrid padding, X/Y and
  static-key/IV validation, Alice-side SessionCreated replay cache,
  pending-handshake bounds/release, extra-pending-byte rejection and HTTP proxy
  buffer lifetime.
- `libi2pd/SSU2Session.cpp/.h` — MTU-sized 15-fragment SessionConfirmed send,
  exact fragment retransmission, `SendData` success/out-parameter, ACK/NACK
  range validation, duplicate/reassembly fixes, RFC 6298 RTO/backoff,
  PQ SessionCreated MTU clamping, fragment minimums/overflow and post-AEAD
  migration handling.
- `libi2pd/SSU2OutOfSession.cpp/.h` — version-2 HolePunch, PeerTest source
  validation and cross-family/unspecified apparent-address rejection.
- `libi2pd/Transports.cpp/.h`, `libi2pd/RouterContext.cpp/.h` — transport
  thread/timer/server teardown ordering, io_context restart, atomic status
  fields, MTU publication and per-family address/cap refresh.
- `libi2pd/RouterInfo.cpp/.h` — independent v4/v6 copies for dual-cap
  non-published SSU2 addresses and strict `v`/`pq`/MTU metadata parsing.
- New tests: `test-ntcp2-handshake-bounds`,
  `test-ssu2-session-fragment-bounds`, `test-ssu2-control-plane`.
- Validation: local Linux/GCC 16.2.0/OpenSSL 4.0.3 build and **42/42 CTest**
  passed. No Java peer, live relay/PeerTest/MTU, sanitizer, fuzzer,
  OpenSSL/LibreSSL matrix, OpenBSD, Windows or Android run.

## LeaseSet/RouterInfo/netDb Java 2.13 pass (2026-10-18)

- `libi2pd/LeaseSet.cpp/.h` — exact offline-signature bounds, safe store-type
  prefix verification, standard LS2 earliest/latest lease freshness, encrypted
  auth/ciphertext and outer/inner metadata bounds, type-5 revalidation, Meta
  entry/earliest limits, known key lengths/preference, strict property mappings
  and local writer caps.
- `libi2pd/RouterInfo.cpp/.h` — transactional parse/update, optional `iexp`,
  NTCP2 `i`/`v` requirements, local dual-stack NTCP2 split, initialized fields,
  ranked bandwidth caps, dotted versions, family-signature ordering, hidden
  identity certificates, address expiration endianness, introducer limits,
  file-load signature checks, reachability masks and atomic observable state.
- `libi2pd/NetDb.cpp/.hpp` — candidate-first RouterInfo/LeaseSet replacement,
  LS1 earliest-lease freshness, strict DatabaseLookup reply-key handling
  (`0x02`/`0x10`; `0x12` fail-closed because proposal 154's derived KDF is TBD),
  no cleartext reply fallback, locked lifecycle/visitor snapshots,
  floodfill/self/hidden selection rules and persistence future bounds.
- `libi2pd/NetDbRequests.cpp/.h`, `libi2pd/I2NPProtocol.cpp/.h` — direct
  exploratory lookup type, strict DatabaseSearchReply parser, sender binding,
  one-reply-per-peer suppression, peer filtering, bounded pending requests and
  strand-safe drop handling.
- New tests: `test-leaseset2-java213-regression`,
  `test-routerinfo-java213-parsing`, `test-routerinfo-update-transaction`,
  `test-netdb-lookup-wire-bounds`, `test-netdb-dsrm-correlation`,
  `test-netdb-store-lookup-hardening`.
- Validation: local Linux/GCC 16.2.0/OpenSSL 4.0.3 build and **48/48 CTest**
  passed. No Java peer, live floodfill, ASan/UBSan/TSan, fuzzer,
  LibreSSL/OpenBSD, Windows or Android run.

## netDb cross-type freshness (static)

- `libi2pd/LeaseSet.h/.cpp` — added `GetEarliestLeaseDate()` and populated it
  for LS1 and standard LS2, including reused `Update()` objects; encrypted and
  Meta LS2 use the zero sentinel.
- `libi2pd/NetDb.cpp` — unified `isNewer()` behavior: published timestamp for
  LS2/LS2, earliest lease date for any LS1/LS2 mix; invalid, expired, older or
  replayed candidates do not evict a valid cached entry.
- `tests/test-netdb-store-lookup-hardening.cpp` — added LS1→LS2, LS2→LS1 and
  older-LS2 rejection vectors.
- `docs/INTEROPERABILITY.md` — DB-110 updated to the cross-type rule.
- Static-only status: pending manual CI compilation and tests.

## Build-system portability pass (static)

- `Makefile`, `tests/Makefile` — removed non-POSIX `?=` defaults, quoted build
  paths and kept CMake/Ninja as the portable front-end; no GNU make syntax or
  recursive build rules.
- `build/cmake_modules/FindMiniUPnPc.cmake` — replaced hard-coded
  `/usr/include`, `/usr/local/include`, `/opt/local/include`, `SystemDrive` and
  source-relative search paths with normal `find_path`/`find_library` and an
  imported target.
- `build/cmake/i2pdConfig.cmake` — package config now requests only the Boost
  components the tree actually uses (`program_options`, plus optional JSON/URL).
- `build/build_mingw.cmd` — MSYS2 and Inno Setup discovery through environment
  variables/`PATH`; removed hard-coded installation directories and automatic
  `git pull`.
- `tests/check-build-system.cmake`, `tests/CMakeLists.txt` — compile-free CTest
  policy check for POSIX make syntax, no download rules, no hard-coded paths and
  canonical component options.
- `docs/build-system.md` — static-only validation policy and updated front-end
  details.
- No configure, build, install or test was executed; all results are pending
  manual CI.

## Native TUI navigation/statistics pass (static)

- `tui/main.cpp` — added a dedicated Destinations (local b32 connections) view,
  moved NetDb to its own `NETDB` snapshot, expanded Status with transport
  online, 15-second bandwidth and tunnel queues. The original `1`–`7`
  bindings remain unchanged; Destinations is `8` and the later read-only
  Config view is `9`. The TUI remains a separate client with no terminal
  requirement in the daemon.
- `tests/test-tui-protocol.cpp` — added NETDB and DESTINATIONS request parsing
  coverage.
- `docs/tui-ipc.md`, `docs/torrent/tui.md`, `docs/TUI-WINDOWS.md` — documented
  the dedicated destination view, keys and bounded administrative operations.
- Official semantic colors continue to come only from `tui/Theme.h` and
  `assets/branding/colors.md`; Unix ncursesw and Windows console backends share
  the same presentation code.
- No configure/build/test was run; all changes are pending manual CI.

## Native torrent containment/disk sequencing (static)

- `libi2pd_client/TorrentsTunnel.cpp/.h` — `SetTorrentPathWithinRoot` validates
  the sanitized torrent root against the configured download directory before
  inserting it into the session; `ReadTorrentFileForControl` performs bounded
  `.torrent` reads on the dedicated disk service and returns a bounded result
  to the network service. Tracker host checks remain I2P-only.
- `docs/torrent/security.md` — documented the early containment check, disk
  service split and retained process-level separation limitation.
- No browser HTML/CSS/JS console files remain; HTTP protocol services are
  untouched. Pending CI compilation and tests.

## Portable native tools pass (static)

- `tools/common/getopt.h`, `tools/common/getopt.cpp` — minimal MSVC-compatible
  getopt/getopt_long compatibility; POSIX uses the system headers.
- `tools/keyinfo.cpp`, `tools/famtool.cpp`, `tools/routerinfo.cpp`,
  `tools/vain.cpp` — use the shared compatibility header instead of POSIX-only
  includes; CLI spellings are unchanged.
- `tools/i2pbase64.cpp` — C++ stream I/O with binary stdio on Windows; removed
  POSIX fd/open/read/write assumptions.
- `tools/CMakeLists.txt`, `tools/test-tools-inventory.cmake` — every native
  tool target is built by CMake and checked by CTest; `tools-smoke` retained.
- `docs/tools/README.md`, `TODO.md` — inventory, Windows CLI and test status.
- No submodule, external router checkout, GNU make file or download step was
  added. Pending CI compilation on Linux/Windows/OpenBSD.

## Perl migration and validation completion (static)

- `tools/tests/validate-branding.t` — Test::More wrapper for the static
  branding validator.
- `tools/CMakeLists.txt` — registers `perl-i2pdctl-syntax`, `perl-i2pdctl`
  (prove) and `perl-validate-branding` when Perl/prove are available.
- `.perlcriticrc` — optional core-theme Perl::Critic profile; not a runtime or
  build dependency.
- `.github/workflows/build.yml`, `docs/ci.md`,
  `docs/development/perl-migration.md`, `docs/tools/i2pdctl.md` — Perl syntax,
  Test::More and optional style-validation documentation.
- `TODO.md`, `PROGRESS.md` — migration inventory and pending dynamic status.
- No maintained Python script remains. No local build/test was run; CI
  validation is pending.

## Test and CI integration (static)

| Path | Change |
|---|---|
| `tests/CMakeLists.txt` | Register static interoperability, documentation-link and optional pinned-reference tests; add suite labels for security, IPC, TUI, torrent, tools and Perl. |
| `tests/test-privsep-protocol.cpp`, `tests/test-privsep-log.cpp`, `privsep/Protocol.h` | Rename codec methods to `Write*/Read*` to avoid OpenBSD/LibreSSL macro collisions; wire bytes unchanged. |
| `privsep/Worker.cpp` | Include OpenBSD `<stdlib.h>` for `getexecname(3)` after CI exposed the missing declaration. |
| `tests/test-tui-win32-pipe.cpp` | Use the actual process SID in the protected named-pipe DACL, matching `TuiServerWin32`; the CI run showed the `OW`-only descriptor failing before the round trip. |
| `tools/CMakeLists.txt`, `tools/test-tools.cmake` | Label Perl/tool tests and add explicit Windows `mswsock`/`ws2_32` links for static tool consumers. |
| `.github/workflows/build.yml` | Manual Linux Clang/GCC full CTest, Android unit/Lint/APK, pinned-Java interop and ASan+UBSan/TSan sanitizer jobs. |
| `.github/workflows/fuzz.yml` | Manual bounded libFuzzer smoke workflow for all eight targets. |
| `.github/workflows/build-windows.yml` | Full MSYS2/Clang64 build and CTest; focused MSVC TUI/API job retained. |
| `.github/workflows/build-openbsd.yml` | Full OpenBSD CTest remains manual-only. |
| `docs/ci.md`, `docs/testing.md`, `docs/security-audit.md`, `tests/interop/README.md`, `docs/interop-matrix.md` | Document the expanded matrix, labels, static/reference interop split and pending dynamic status. |

## Memory-safety and concurrency corrections (static)

| Path | Change |
|---|---|
| `libi2pd/SSU2Session.*`, `SSU2OutOfSession.*` | Restrict PeerTest payload blocks, guard data-phase path responses, zero data keys, fix RelayIntro/PeerTest packet sizing. |
| `libi2pd/NTCP2.cpp` | Post/await NTCP2 session teardown on the service executor before stopping the IOService. |
| `libi2pd/ECIESX25519AEADRatchetSession.cpp`, `libi2pd/Garlic.cpp` | Bound NEXT_KEY/tag generation, erase tag-map entries before callbacks, validate padding capacity. |
| `libi2pd/I2NPProtocol.cpp`, `libi2pd/Destination.cpp`, `libi2pd/NetDb.cpp` | Own RouterInfo serialization snapshots, copy-on-write remote LeaseSets, lock NetDb startup map mutation. |
| `libi2pd/RouterContext.cpp` | Validate `router.keys`; bound TunnelTest reads; lock floodfill statistics updates. |
| `libi2pd_client/I2CP.cpp`, `Datagram.cpp`, `HTTPProxy.cpp`, `SAM.cpp`, `ClientContext.cpp` | Null-safe I2CP reconfigure, owning async callbacks, shared-destination lifecycle and null-safe client shutdown. |
| `libi2pd_client/Torrents.*`, `TorrentsDHT.cpp` | Exact block validation, zeroed buffers, loading gate, block-budget limit and iterator-safe DHT response dispatch. |
| `privsep/Channel.cpp`, `LogWorker.cpp`, `daemon/Daemon.h` | UID/GID SCM credential check, aligned ancillary buffers, fdopen ownership and daemon PID initialization. |
| `android/app/src/main/cpp/i2pd_android.cpp` | JNI exception boundaries for start/stop/reload/status/data-dir entry points. |
| `tests/test-ssu2-control-plane.cpp`, `test-routerinfo-update-transaction.cpp`, `test-torrents-security.cpp` | Static regressions for PeerTest block policy, owned RouterInfo snapshots and partial torrent blocks. |
| `docs/security/memory-safety-2026.md`, `docs/security/README.md`, `docs/security-audit.md`, `TODO.md`, `ROADMAP.md`, `PROGRESS.md` | Findings, corrections, residual risks and pending validation tracking. |

## RouterContext, torrent containment and TUI config pass (static)

| Path | Change |
|---|---|
| `libi2pd/RouterContext.cpp/.h` | Serialize address/property/caps/floodfill/MTU/bandwidth mutations with signed-buffer rebuilds; add `GetRouterInfoProperty()`; document the remaining live `GetRouterInfo()` readers. |
| `tests/test-routercontext-update-race.cpp` | Concurrent family/property/snapshot regression prepared for TSan. |
| `libi2pd_client/Torrents.cpp/.h`, `TorrentsTunnel.cpp` | Anchored no-follow path traversal, bounded exact file I/O, safe directory creation and resume read/write hardening. |
| `tests/test-torrents-security.cpp` | Traversal, symlink, truncated-part, range-wrap and resume-size regressions. |
| `libi2pd/TuiProtocol.h`, `daemon/TuiServerCommon.cpp`, `tui/main.cpp` | Bounded read-only `CONFIG` allowlist, typed TUI view, restored tab bindings and fixed help scrolling. |
| `tests/test-tui-protocol.cpp`, `docs/tui-ipc.md` | CONFIG request/framing vectors and protocol/UI documentation. |
| `tools/test-tools-regressions.cmake`, `tools/CMakeLists.txt`, `tools/b33offlinekeys.cpp` | Deterministic per-tool CTest and correct write-failure exit status. |
| `tests/test-privsep-linux-ipc.cpp`, `tests/test-privsep-log.cpp`, `tests/test-privsep-protocol.cpp` | Descriptor/credential/framing/queue/close regressions. |
| `TODO.md`, `ROADMAP.md`, `PROGRESS.md`, `docs/security/memory-safety-2026.md` | MEM-019..MEM-022 and static/pending validation tracking. |

No compiler, CTest, sanitizer, fuzzer, Gradle, OpenBSD, Windows or live-peer
run was performed; all additions are pending manual CI.

## C++23 ownership/RAII continuation (static)

| Path | Change |
|---|---|
| `libi2pd/KadDHT.*` | `std::unique_ptr` node/root ownership; no manual tree `new`/`delete`. |
| `tests/test-kad-dht.cpp` | Deterministic insertion, duplicate, promotion, clear and filtered-cleanup ownership regression. |
| `libi2pd/Garlic.*` | `std::vector<SessionTag>` unconfirmed tags, owning tag move and `std::unique_ptr` payload buffer. |
| `libi2pd/Reseed.cpp` | Vector-owned compressed/uncompressed ZIP buffers and zlib teardown before CRC/netDb work. |
| `daemon/UnixDaemon.cpp` | Local move-only readiness/pidfile descriptor guard and early-failure notification. |
| `daemon/I2PControl.cpp` | Scoped OpenSSL/`FILE*` certificate/key owners and checked ASN.1/PEM/signature returns. |
| `libi2pd_client/ClientContext.*`, `AddressBook.*` | `unique_ptr` service/storage ownership, vector key/lookup buffers and bounded key-file reads. |
| `tests/test-addressbook.cpp` | In-memory address/hosts parsing regression; no network or storage. |
| `libi2pd/Base.*`, `tools/x25519.cpp`, `tools/i2pbase64.cpp`, `tools/common/file.hpp` | Span overloads and scoped OpenSSL/stream I/O; CLI and wire behavior unchanged. |
| `libi2pd/Signature.*`, `Gost.cpp`, `Crypto.cpp` | Scoped ED25519/EVP handles, RAII GOST curve/BIGNUM construction and cleansed ElGamal exponent buffer; P-521 ECIES coordinate bound corrected. |
| `tests/CMakeLists.txt`, `docs/testing.md`, `docs/cpp23-migration.md`, `TODO.md`, `ROADMAP.md`, `PROGRESS.md` | Test registration and static/pending validation tracking. |

## Memory/concurrency correction pass (static)

| Path | Change |
|---|---|
| `libi2pd/SSU2Session.*`, `SSU2.*`, `SSU2OutOfSession.*` | Per-state payload whitelist, pre-auth handler guards, bounded token/recently-connected/PeerTest caches, multi-fragment SessionConfirmed, synchronized establishment callbacks, isolated proxy buffer, packet-batch guard, bool/out-param send status and checked X25519 agreement. |
| `libi2pd/Signature.*`, `Crypto.*`, `ECIESX25519AEADRatchetSession.*`, `Garlic.*`, `LeaseSet.*` | Exact ECDSA DER sizing, checked AES wrappers, P-521 ECIES storage, low-order X25519 rejection, ECIES null/return checks, lazy crypto cache synchronization and synchronized incoming tag maps. |
| `libi2pd_client/Torrents.*`, `TorrentsTunnel.*`, `TorrentsRPC.cpp` | 64-bit torrent limits, peer-frame prefix bound, per-piece mutex/corrupt-reset state machine, service-marshalled teardown and bounded resume/bitfield handling. |
| `libi2pd_client/I2CP.*`, `SAM.*`, `ClientContext.*`, `HTTPProxy.*` | Atomic I2CP session IDs, serialized write/termination queues, SAM accept/write serialization, locked destination snapshots, owning HTTP callbacks and a 1 MiB header bound. |
| `android/app/src/main/cpp/i2pd_android.cpp`, `DaemonAndroid.*` | Common JNI exception frontier, serialized native lifecycle, immutable status address snapshot and bounded fallback strings. |
| `libi2pd/TunnelPool.*`, `Tunnel.*`, `TransitTunnel.*` | Deadlock-free replacement creation, locked snapshots, atomic counters/latency/TCSR/RNG, restart clearing and transit lifecycle cleanup. |
| `libi2pd/Timestamp.*`, `util.*`, `RouterContext.*`, `NetDb.*`, `Log.*` | Atomic time offset, idempotent NTP restart, atomic services/counters/static keys, owned futures, serialized NetDb/Logger lifecycle and snapshot readers. |
| `privsep/Channel.*`, `Worker.*`, `LogWorker.*`, `daemon/TuiServer*`, `daemon/Daemon.cpp`, `daemon/UPnP.cpp` | Absolute-deadline IPC I/O, bounded TUI clients and child reaping, safe sink teardown, immutable RouterInfo address snapshots and exception-contained dispatch. |
| `tests/CMakeLists.txt`, `tests/test-ssu2-preauth.cpp`, `test-torrents-concurrency.cpp`, `test-http-proxy-limits.cpp`, `test-i2cp-session-lifecycle.cpp`, `test-clientcontext-registry.cpp`, `test-sam-stream-lifecycle.cpp`, `test-tunnelpool-*`, `test-tunnels-restart.cpp`, `test-timestamp-offset.cpp`, `test-ntp-timesync-lifecycle.cpp`, `test-routercontext-service-lifecycle.cpp`, `test-routercontext-static-keys.cpp`, `tests/fuzz/fuzz-signature-der.cpp` | Static regressions and fuzz target registrations; none executed locally. |
| `docs/security/memory-safety-2026.md`, `TODO.md`, `PROGRESS.md`, `docs/testing.md`, `docs/files-changed.md` | Session 29 findings, residual risks and pending CI validation. |

No compiler, CTest, sanitizer, fuzzer, Gradle, OpenBSD, Windows or live-peer
run was performed; all additions are pending manual CI.

## Dependency-floor and compatibility cleanup (static, session 30)

| Path | Change |
|---|---|
| `build/CMakeLists.txt` | Enforce OpenSSL 3.0.2 / LibreSSL 4.0, require MiniUPnPc 2.2.6, keep UPnP enabled for the Android JNI target with `I2PD_NO_PRIVSEP`, link the imported MiniUPnPc target and remove `OPENSSL_SUPPRESS_DEPRECATED`. |
| `build/cmake_modules/FindMiniUPnPc.cmake` | Parse the installed version/API, reject API < 17 and expose the imported target. |
| `build/cmake_modules/CheckAtomic.cmake` | Remove the unused legacy LLVM atomic-builtin probe. |
| `libi2pd/Crypto.h`, `Crypto.cpp` | Remove the OpenSSL 3.0.0 SipHash exception and the pre-LibreSSL-4.0 in-place ChaCha20-Poly1305 copy. |
| `libi2pd/I2PEndian.h` (and deleted `I2PEndian.cpp`, `LittleBigEndian.h`) | Windows uses C++23 `std::endian`/`std::byteswap`; Unix/Android use the platform `endian(3)` header directly. |
| `libi2pd/util.h`, `util.cpp` | Remove the Android non-Clang `to_string`/`stoi` polyfill, the Android API<24 `ifaddrs` branch and Windows XP `inet_pton_xp`/`inet_ntop_xp`; use `InetPtonA`/`InetNtopA`. |
| `Win32/Win32NetState.*` | Remove the `WINVER == 0x0501` Windows XP branch. |
| `libi2pd/Timestamp.cpp`, `libi2pd_client/SAM.cpp` | Remove MSVC `sprintf_s` branches, use checked `std::snprintf` lengths and reject/clamp oversized protocol replies. |
| `libi2pd_client/TorrentsRPC.cpp`, `daemon/I2PControl.cpp`, `I2PControlHandlers.cpp` | Remove `JSON_SUPPORTED`/old-Boost fallback, `BOOST_ASIO_HAS_STD_ARRAY`, unused lexical_cast and Boost.Bind placeholder workarounds. |
| `libi2pd/IdentMetrics.cpp`, `NTCP2.cpp`, `Family.cpp`, `Ed25519.cpp` | Replace `EVP_MD_CTX_create`/`destroy` aliases with `EVP_MD_CTX_new`/`free`. |
| `daemon/UPnP.cpp` | Remove the MiniUPnPc API<14/10/8 selection branches; API<17 is rejected by the finder and only the API-18 `UPNP_GetValidIGD` signature capability remains. |
| `.github/workflows/*`, `debian/control`, `contrib/docker/Dockerfile`, `contrib/ci/android/vcpkg.json` | Drop redundant Boost.Atomic/Filesystem/JSON/URL dependencies where unused; retain MiniUPnPc for the default-on Android UPnP client. |
| `tests/check-build-system.cmake` | Add compile-free assertions for the new floors and removed branches. |
| `docs/dependencies.md`, `docs/devs/building/{requirements,unix,windows}.md`, `README.md`, `docs/build-system.md`, `TODO.md` | Document floors, removed branches and consumer cleanup. |

## Java 2.13.0 third static pass (2026-10-19)

The official stable release remains `i2p-2.13.0` (`9134f808…`); no newer
stable release exists. The following corrections were made from a
specification/Java comparison and independent subsystem audits. No dynamic
validation was performed.

| Path | Change |
|---|---|
| `libi2pd/SSU2Session.cpp` | Advance the inbound receive baseline from Alice's first data packet 1; remove synthetic ACK advancement during cleanup; ignore ordinary Address observations outside PeerTest. |
| `libi2pd/Transports.cpp` | Create the NTCP2 server independently of proxy parsing so an invalid/unsupported proxy cannot cause a null dereference. |
| `libi2pd/LeaseSet.cpp` | Preserve the decrypted inner `PUBLISHED_ENCRYPTED` bit; record Meta `EarliestLeaseDate`; use Java's `min(latest entry, header expiry)` future bound; emit the inner `UNPUBLISHED | PUBLISHED_ENCRYPTED` flags. |
| `libi2pd/Destination.cpp` | Use the strict DatabaseSearchReply parser with mandatory sender; mask DatabaseStore type bits at the client layer. |
| `libi2pd/Reseed.cpp` | Validate SU3 file version, exact signature length, non-empty signer/content and absence of trailing signature bytes. |
| `libi2pd_client/SAM.cpp/.h` | Add SAM 3.3 `SESSION ADD/REMOVE` `ID=` success/error responses without closing the primary socket; accept `STYLE=PRIMARY`; exact DATAGRAM/2/3 style tokens. |
| `libi2pd/Gzip.*`, `Datagram.cpp` | Distinguish a valid zero-output gzip stream and deliver empty Datagram2/3 payloads; non-empty malformed streams still fail. |
| `libi2pd/Streaming.*` | Apply peer `MAX_PACKET_SIZE` as a lower MTU bound and cap pong payload at 32 bytes. |
| `libi2pd/Garlic.cpp` | Randomize ElGamal/AES block padding instead of encrypting uninitialized heap bytes. |
| `libi2pd/ECIESX25519AEADRatchetSession.*` | Add a bounded, time-limited router incoming-N duplicate-ephemeral cache after AEAD/payload validation. |
| `tests/test-leaseset2-java213-regression.cpp`, `tests/test-su3-bounds.cpp`, `tests/test-datagram-options.cpp` | Regression assertions for Meta earliest date, inner encrypted flag, strict SU3 headers and empty gzip payloads. |
| `docs/INTEROPERABILITY.md`, `tests/interop/FIXTURES.md`, `tests/interop/templates/tunnel-build-time-windows.json`, `tests/interop/vectors/valid/sam-hello-33-only.*` | Third-pass matrix, stale-claim corrections, known-gap marking and SAM 3.3 HELLO fixture. |

## Crypto/ratchet/PQ static audit (2026-10-19)

| Path | Change |
|---|---|
| `libi2pd/Garlic.cpp/.h` | Fixed the `ReceiveRatchetTagSetPtr` type, added a time-bounded 4096-entry NS replay cache, and reserve the incoming-tag budget before advancing the tag chain. |
| `libi2pd/ECIESX25519AEADRatchetSession.cpp/.h` | Added `ValidateRatchetPayload` before NSR/ES side effects; New Session now requires a Garlic clove; full 16-bit tag boundary. |
| `libi2pd/Signature.cpp/.h`, `libi2pd/Identity.cpp` | SigType 8 uses Java-compatible plain Ed25519 over supplied bytes; RedDSA private scalar is cleansed. |
| `libi2pd/PostQuantum.*`, `libi2pd/RouterInfo.cpp`, `libi2pd/Destination.cpp` | Gated SSU2 `pq` publication/selection and hybrid key advertisement on actual ML-KEM support, including LibreSSL’s missing ML-KEM-512. |
| `tests/test-eciesx25519-ratchet-session.cpp`, `tests/test-eciesx25519-ratchet-tagsets.cpp`, `tests/test-crypto-x25519-noise-elligator-mlkem-audit.cpp` | NS/NSR/ES payload validation, tag boundary and Elligator2 boundary-1/high-bit regressions. |
| `docs/INTEROPERABILITY.md`, `TODO.md`, `PROGRESS.md` | CRY-206..CRY-213 matrix/status and residual PQ/browser-vector notes. |

## Client-protocol static pass (2026-10-19)

| Path | Change |
|---|---|
| `libi2pd_client/SAM.cpp/.h` | 49 KiB control and 64 KiB receive buffers; SILENT connect starts I2P receive; STREAM subsession uses `LISTEN_PORT` and per-port `StreamingDestination`; accept queue no longer expires after 3 s; STREAM errors use `STREAM STATUS`; signature types validated/case-insensitive; type 8 exposed. |
| `libi2pd_client/I2CP.cpp/.h`, `libi2pd/LeaseSet.h` | b33 association keyed by unblinded signing key; TYPE_KEY merge preserves TYPE_DEST; HostReply types 2-4 carry standard LeaseSet2 properties. |
| `libi2pd_client/AddressBook.cpp` | Mixed-case `.B32.I2P` suffix handling and terminating suffix check. |
| `libi2pd/Datagram.cpp` | Null session guard, no inbound version mutation, fresh-LeaseSet preference, hash-only Datagram3 delivery and 4-byte data length capacity fix. |
| `libi2pd/Streaming.cpp/.h` | Incoming stream requests a missing LeaseSet asynchronously; inbound SYN MTU applied; offline delegation always authenticated; unsigned CLOSE/RESET without LeaseSet rejected; `ResumeSend` helper. |
| `tests/test-sam-stream-lifecycle.cpp` | Large-frame buffer assertions and non-expiring queued acceptors. |
| `docs/INTEROPERABILITY.md`, `TODO.md`, `PROGRESS.md` | Client-protocol status/limitations and NET-025 tracking. |

## Supervisor-process split (2026-10-19)

| Path | Change |
|---|---|
| `privsep/Supervisor.{h,cpp}` | New exec'ed supervisor process. It owns the log worker, forwards `ConfigureLog`/`LogRecord`, restarts the log child, and never opens the log file itself. |
| `privsep/Worker.{h,cpp}` | Generalized `SpawnWorker(role)` and added `SpawnSupervisor`; the child role is passed through `--privsep-worker` rather than hard-coded to `log`. |
| `privsep/RunWorker.cpp`, `privsep/LogWorker.{h,cpp}` | Supervisor role dispatch and proxy configuration/reattach without local file opens. |
| `daemon/Daemon.cpp`, `build/CMakeLists.txt` | Core starts the supervisor instead of the log worker; bounded core monitor now replaces a dead supervisor. |
| `docs/security/{privilege-separation,ipc-protocol,process-capabilities,threat-model}.md`, `docs/architecture/README.md`, `TODO.md` | State the separate supervisor boundary and explicitly retain storage/network/crypto/core migration as pending. |

## Supervisor process (2026-10-19)

| Path | Change |
|---|---|
| `privsep/Supervisor.{h,cpp}` | New supervisor process owning the log worker, forwarding config/records and restarting the child without opening the log file. |
| `privsep/Worker.{h,cpp}`, `privsep/RunWorker.cpp`, `privsep/LogWorker.*` | Generic worker role spawning, `SpawnSupervisor`, supervisor dispatch, and proxy configure/reattach. |
| `daemon/Daemon.h`, `daemon/Daemon.cpp`, `build/CMakeLists.txt` | Core now starts and monitors the supervisor process rather than the log worker. |
| `docs/security/*`, `docs/architecture/README.md`, `TODO.md` | Updated supervisor boundary/status and explicit remaining storage/network/crypto/core migration. |

## Android English-only cleanup (2026-10-19)

| Path | Change |
|---|---|
| `android/app/src/main/res/values-<language>/` | Deleted all 16 language catalogs; `values/` remains the sole string catalog. |
| `android/fastlane/metadata/android/` | Retained only `en-US`; removed localized store descriptions. |
| `android/app/src/main/res/xml/locales_config.xml`, `AndroidManifest.xml`, `android/app/build.gradle` | Declared only `en` through `localeConfig` and `androidResources.localeFilters`. |
| `android/app/src/main/java/org/openi2pd/router/{DaemonWrapper,I2PD_JNI,UiFormat,LogAdapter,LogsFragment}.java`, `android/app/src/main/cpp/{DaemonAndroid,i2pd_android}.*`, generated JNI header | Removed dead app-locale/native `SetLanguage` plumbing; pinned formatting/case-folding to English/root locale. |
| `android/app/src/androidTest/java/org/openi2pd/router/EnglishOnlyResourceTest.java`, `android/tools/check-english-only.pl`, `.github/workflows/build.yml` | Prepared a device test and compile-free CI guard for English-only resources/locales. |
| `android/README.md`, `docs/android-modernization.md`, `docs/REBRANDING.md`, `TODO.md`, `ROADMAP.md`, `PROGRESS.md` | Documented the policy and pending manual CI/device validation. |

## OpenBSD native privilege separation (2026-10-19)

| Path | Change |
|---|---|
| `privsep/OpenBSDSandbox.{h,cpp}` | Per-role pledge/unveil policies, capability predicates, absolute-path validation, pre-opened descriptor helpers. |
| `privsep/OpenBSDImsg.{h,cpp}` | OpenBSD `imsg` adapter over the versioned frame, bounded polls, `getpeereid`, one `SCM_RIGHTS` descriptor. |
| `privsep/LogWorker.cpp`, `privsep/Supervisor.cpp`, `privsep/RunWorker.cpp`, `privsep/Worker.{h,cpp}` | Log worker uses the policy builder; supervisor pre-resolves the executable and applies its own policy; workers reject euid 0; spawn accepts a pre-resolved executable. |
| `daemon/Daemon.{h,cpp}`, `daemon/UnixDaemon.cpp` | Workers are prepared before the runtime sandbox; startup pledge keeps `proc`/`exec` only for the spawn window; runtime policy cannot re-add them; core log paths are not unveiled when the worker owns the sink. |
| `tests/test-privsep-openbsd-policy.cpp`, `tests/test-privsep-openbsd-imsg.cpp`, `tests/CMakeLists.txt`, `build/CMakeLists.txt` | Prepared OpenBSD negative capability and imsg tests; added the policy source to the daemon build. |
| `docs/security/{ipc-protocol,process-capabilities,privilege-separation,openbsd-validation}.md`, `contrib/openbsd/{README.md,i2pd.conf}`, `TODO.md`, `ROADMAP.md`, `PROGRESS.md` | Documented the new boundary, stale refusal removed and validation still pending. |

## Linux IPC/sandbox backend (2026-10-19)

| Path | Change |
|---|---|
| `privsep/LinuxSandbox.{h,cpp}` | Added AF_INET/AF_INET6 vs AF_UNIX socket policies, optional empty network namespace, capability/ambient/bounding-set drop, cgroup v2 limits, optional cgroup2 probe and role profiles. |
| `privsep/LogWorker.cpp`, `privsep/Supervisor.cpp` | Applied Linux sandboxes; log path is opened before Landlock and adopted by descriptor; supervisor keeps AF_UNIX socketpair/fork/exec. |
| `privsep/Worker.cpp` | `pidfd_send_signal` preferred for bounded supervisor/worker termination, with `kill` fallback. |
| `tests/test-privsep-linux-sandbox.cpp` | Added storage socketpair/no-capability, log AF_INET-deny/AF_UNIX-allow and supervisor socketpair checks. |
| `docs/security/process-capabilities.md`, `TODO.md`, `ROADMAP.md`, `PROGRESS.md` | Documented Linux mechanisms, kernel limits and pending dynamic validation. |

## Windows worker IPC and process policy (2026-10-19)

| Path | Change |
|---|---|
| `privsep/WindowsIpc.{h,cpp}` | Message-mode local Named Pipe transport, protected user/SYSTEM DACL, peer SID/session checks, operation allowlist, restricted-token/Job Object helpers, mitigation spawning and private pipe names. |
| `tests/test-privsep-windows-ipc.cpp`, `tests/CMakeLists.txt`, `build/CMakeLists.txt` | Windows-only DACL/round-trip/malformed/recovery/token/job tests and daemon/test build entries. |
| `docs/security/windows-worker-ipc.md`, `docs/security/{README,ipc-protocol,process-capabilities,privilege-separation}.md`, `docs/mkdocs.yml`, `TODO.md`, `ROADMAP.md`, `PROGRESS.md` | Documented the Windows transport/policy, no-inheritance handle rule, AppContainer/LPAC fail-closed status and pending backend/service validation. |

## CMake/POSIX Make component audit (2026-10-19)

| Path | Change |
|---|---|
| `build/CMakeLists.txt` | Added the `i2pd_privsep` component target, platform link interfaces, `I2PD_INSTALL_DOCS`, Android-only JNI validation and component status output. |
| `Makefile`, `tests/Makefile` | Completed POSIX `configure/build/router/tui/tools/test/check/install` targets; tests configure, build and run CTest; Ninja remains the generator default without GNU make syntax. |
| `tests/CMakeLists.txt` | `test-privsep-log` links the Linux/OpenBSD sandbox sources it calls. |
| `tests/check-build-system.cmake` | Extended compile-free checks for Make targets/syntax, generator defaults, C++23/component markers, source globs, downloads and hard-coded paths across top/tools/tests/Android CMake. |
| `README.md`, `docs/build-system.md`, `docs/devs/building/unix.md`, `docs/files-changed.md`, `TODO.md`, `ROADMAP.md`, `PROGRESS.md` | Documented component dependencies, Make targets, generator portability and static validation status. |

## Native TUI navigation and administrative IPC (2026-10-19)

| Path | Change |
|---|---|
| `libi2pd/TuiProtocol.h` | Added bounded `CONNECTIONS`, `NETDB-ROUTERS`/`NETDB-LEASESETS`, strict typed `ADMIN` parsing and the daemon-owned command queue contract (reseed, shutdown/cancel, reload, bandwidth, share, transit and log level). |
| `libi2pd/Transports.h`, `Transports.cpp` | Added connected-peer count and bounded live-session snapshot accessors. |
| `libi2pd/NetDb.hpp`, `NetDb.cpp` | Added bounded router and LeaseSet snapshot accessors for administrative views. |
| `daemon/TuiServerCommon.cpp` | Added connection/netDb directory payloads, atomic transit and daemon-log-level operations, and `log.level` state reporting. |
| `tui/main.cpp` | Added Connections, router/LeaseSet directories, log search, bandwidth history, narrow-terminal navigation, transit confirmation and daemon log-level control. |
| `tests/test-tui-protocol.cpp` | Added parsing/bounds/negative vectors for the new requests and administrative commands. |
| `tests/test-tui-ipc-regression.cpp` | Dispatches directory views and an atomic daemon log-level update over the real local socket. |
| `docs/tui-ipc.md`, `docs/TUI-WINDOWS.md`, `docs/architecture.md`, `docs/architecture/README.md`, `docs/testing.md`, `docs/security/*`, `docs/user-guide/*`, `docs/torrent/tui.md`, `docs/files-changed.md`, `TODO.md`, `ROADMAP.md`, `PROGRESS.md` | Documented the new views, bounded protocol, only-two administrative writes, Windows frontend status and pending validation. |

## Torrent pause/resume, storage domain and I2P tracker audit (2026-10-19)

| Path | Change |
|---|---|
| `libi2pd_client/TorrentsControl.h` | Added `Pause`/`Resume` command kinds and the shared `IsI2PTrackerHost` validator. |
| `libi2pd_client/TorrentsControl.cpp` | Implemented strict `.i2p` host validation for tracker entry points. |
| `libi2pd_client/Torrents.h`, `Torrents.cpp` | Added the purely lexical `IsPathLexicallyWithinRoot` check for network/control-thread use. |
| `libi2pd_client/TorrentsTunnel.h`, `TorrentsTunnel.cpp` | Added pause/resume mapping, moved startup `.torrent` enumeration/read to storage, applied lexical network-side path checks, stripped non-I2P announces and shared I2P tracker validation. |
| `libi2pd_client/TorrentsRPC.cpp` | `trackerAdd` now returns an error for a rejected/non-I2P tracker instead of reporting success. |
| `libi2pd/TuiProtocol.h`, `daemon/TuiServerCommon.cpp`, `tui/main.cpp` | Added typed `TORRENT-PAUSE`/`TORRENT-RESUME` commands and updated the Torrents key/status text. |
| `tests/test-tui-protocol.cpp`, `tests/test-torrents-security.cpp` | Added pause/resume parsing, tracker-host rejection and lexical containment vectors. |
| `docs/torrent/*`, `docs/files-changed.md`, `TODO.md`, `ROADMAP.md`, `PROGRESS.md` | Documented the parity change, storage execution domain, I2P-only tracker boundary and remaining process-separation work. |

## i2pd-tools inventory, memory and packaging (2026-10-19)

| Path | Change |
|---|---|
| `docs/tools/provenance.md` | Added pinned upstream commit, imported/local-only file inventory, license/provenance notes and per-executable test mapping. |
| `tools/test-tools-inventory.cmake` | Checks executables, sources, shared helpers and every pinned upstream manifest path. |
| `tools/test-tools-regressions.cmake` | Added scripted `autoconf_i2pd` and oversized `b33address` stdin vectors. |
| `tools/common/file.hpp` | Bounded reads preserved; POSIX writes now use `O_NOFOLLOW`, owner-only creation and mode tightening. |
| `tools/b33address.cpp`, `autoconf_i2pd.cpp` | Bounded stdin and output-filename input. |
| `tools/b33offlinekeys.cpp`, `x25519.cpp`, `famtool.cpp` | Private-buffer wiping, secure output permissions and failure-path cleanup. |
| `libi2pd/Identity.h` | `PrivateKeys` and `B33OfflineKeys` destructors cleanse private-key buffers. |
| `debian/rules`, `debian/i2pd.install`, `debian/control` | Install all binaries, enable TUI/tools, add ncurses/Perl dependencies. |
| `contrib/rpm/i2pd.spec`, `i2pd-git.spec` | Build/install TUI, native tools and `i2pdctl`; add ncurses/Perl metadata and file lists. |
| `build/build_mingw.cmd`, `build/win_installer.iss` | Build all targets and include TUI/tools in ZIP and installer payloads. |
| `tools/CMakeLists.txt`, tool docs, `TODO.md`, `ROADMAP.md`, `PROGRESS.md` | Inventory, C++23/test/package status and provenance documentation updates. |

## Perl portability, proxy isolation and install checks (2026-10-19)

| Path | Change |
|---|---|
| `tools/scripts/i2pdctl` | Removed inherited proxy variables for `HTTP::Tiny`, disabled proxy options, restricted `openssl` discovery to absolute `PATH` entries, and added portable path-separator/`.exe` handling. |
| `tools/tests/i2pdctl.t` | Added proxy-environment isolation and repeated-request idempotence regressions. |
| `tools/tests/perl-portability.t` | Added Test::More inventory guard for Python remnants, shebangs, core modules, executable scripts and unsafe Perl patterns. |
| `tools/test-perl-install.cmake` | Added private-prefix Tools installation check for the installed `i2pdctl` shebang, mode and syntax. |
| `tools/CMakeLists.txt`, `build/CMakeLists.txt`, `tests/check-build-system.cmake` | Added all-script syntax checks, `perl-portability`, `perl-install-layout`, optional `I2PD_PERL_CRITIC`/`perl-critic`, and policy markers. |
| `.github/workflows/build.yml` | Extended the Perl syntax/prove validation to the remaining maintained scripts and portability test. |
| `docs/development/perl-migration.md`, `docs/tools/i2pdctl.md`, `docs/testing.md`, `docs/ci.md`, `docs/torrent/testing.md`, `docs/android-modernization.md` | Documented the final inventory, proxy/PATH hardening, install checks, optional Perl::Critic and removal of historical Python test dependencies. |
| `TODO.md`, `ROADMAP.md`, `PROGRESS.md` | Updated Perl security/build status and remaining dynamic validation. |

## Android toolchain refresh and service lifecycle (2026-10-19)

| Path | Change |
|---|---|
| `android/app/build.gradle` | Pinned Build Tools 36.0.0 and NDK r30 LTS `30.0.16248370`; documented current stable AndroidX/Material pins; Java 25 source/target and exact JDK 25 toolchain remain unchanged. |
| `android/tools/java25-probe/check-config.sh` | Checks Build Tools/NDK pins, rejects `java.util.Timer`, and requires the service-owned graceful deadline and explicit `stopping` state. |
| `android/app/src/main/java/org/openi2pd/router/DaemonWrapper.java` | Removed the `java.util.Timer`; added `State.stopping`, deadline ownership and stop-before-native state publication. |
| `android/tools/check-english-only.pl` | Ignores documentation comments when scanning for runtime locale overrides, so explanatory `Locale.setDefault` text does not fail the policy. |
| `RouterService.java` | Added the main-handler graceful deadline, worker-enqueued stop, cancellation on stop/restart/timeout/destroy, and restart-intent persistence. |
| `UiFormat.java`, `UiFormatTest.java` | Added `stopping` presentation mapping and explicit stopping subtitle handling. |
| `RouterStatusParser.java`, `RouterConfigStore.java` | Converted legacy switch statements to exhaustive switch expressions. |
| `android/app/src/main/cpp/CMakeLists.txt`, `DaemonAndroid.h` | Documented the same-process Android JNI graph and aligned the JNI lifecycle comment with the service-owned stop path. |
| `.github/workflows/build.yml` | Installs platform 37, Build Tools 36.0.0 and NDK r30; exports the exact NDK path. |
| `android/README.md`, `docs/android-*.md`, `docs/devs/building/android.md`, `TODO.md`, `ROADMAP.md`, `PROGRESS.md` | Updated the toolchain matrix, explicit `javac`/D8/R8/ART non-claims, same-process boundary and task status. |

## Android GUI, accessibility and control-state second pass (2026-10-19)

| Path | Change |
|---|---|
| `android/app/src/main/java/org/openi2pd/router/MainActivity.java` | Tinted the toolbar mark for light/dark themes, added the service-disconnected banner/Retry action, compared toolbar subtitles before setting them, and moved graceful-stop enablement to `UiFormat.canGraceful`. |
| `android/app/src/main/java/org/openi2pd/router/UiFormat.java`, `RouterSnapshot.java` | Added lifecycle-based control capabilities, live-status detection, unknown/status-failure copy, state colour roles and `setTextIfChanged`; a started router with a failed snapshot remains stoppable. |
| `android/app/src/main/java/org/openi2pd/router/DashboardFragment.java`, `NetworkFragment.java`, `TunnelsFragment.java`, `StatisticsFragment.java` | Guarded zero counters when no native status exists, used 15-second rates, restored the chart cursor, added the graceful progress bar and avoided redundant text updates. |
| `android/app/src/main/java/org/openi2pd/router/LogsFragment.java` | Fixed the `logs_empty` `LinearLayout`/`TextView` crash, added a live empty-state text id, search debounce, result summary and screen-reader hiding of the empty list. |
| `android/app/src/main/java/org/openi2pd/router/TrafficChartView.java`, `MaxWidthLinearLayout.java` | Added a dashed upload series and a capped, centred content width for tablets/unfolded devices. |
| Android layouts/strings/dimens | Added the global service banner, navigation dividers, quick-action/live-metric headings, Connections label, explicit disabled initial controls, vertical rhythm, graceful progress and accessibility live regions. |
| `android/app/src/test/java/org/openi2pd/router/UiFormatTest.java`, `android/app/src/androidTest/java/org/openi2pd/router/MainActivityNavigationTest.java` | Added live-metric/unknown-status/error-stop coverage and a device regression check for the Logs empty state. |
| `docs/android-modernization.md`, `TODO.md` | Documented the second UI pass and its pending dynamic validation; no build, Lint, APK, ART or device result is claimed. |

## Upstream documentation integration and coherence pass (2026-10-19)

| Path | Change |
|---|---|
| `contrib/upstream/docs-path-map.txt` | Added explicit mapping for the two renamed BitTorrent pages. |
| `contrib/upstream/sync.sh` | Added upstream coverage checks, missing-local detection and HTML-source rejection before printing diffs; all comparisons remain read-only. |
| `docs/upstream-sync.md` | Documented the 46-file upstream inventory (44 direct, 2 mapped), the 74 fork-owned local paths (61 Markdown pages plus 13 assets), inline-HTML anchor conversion, optional MkDocs policy and the manual sync procedure. |
| `docs/user-guide/install.md` | Removed unsupported/upstream package installation paths; documents Linux, OpenBSD, Windows, Android and local Docker only. |
| `docs/user-guide/configuration.md` | Corrected `limits.transittunnels` to 25000, documented Android UPnP as enabled by default and the unsupported macOS/service note. |
| `docs/about/contributing.md` | Replaced C++11/GNU-make/`filelist.mk` guidance with C++23, CMake/Ninja, source-list and documentation-sync rules. |
| `docs/devs/building/requirements.md`, `docs/devs/building/unix.md`, `docs/devs/building/android.md` | Added platform/C++23/CMake/Perl/OpenBSD links, removed duplicate package commands and corrected Android JNI/API references. |
| `docs/README.md`, `docs/index.md`, `docs/developer-guide/README.md` | Added a fork-guide index for C++23, IPC, TUI, BitTorrent, Android, CMake, Perl and OpenBSD. |
| `docs/interop-matrix.md` | Removed as a duplicate; `INTEROPERABILITY.md` is the single current matrix. |
| `docs/mkdocs.yml`, `docs/protocols/README.md` | Removed the duplicate matrix navigation and fixed the protocol link; added the memory-safety report to optional publication navigation. |
| `tools/scripts/check-doc-links.pl` | Added local heading/anchor validation in addition to relative Markdown/image checks. |
| `README.md`, `TODO.md`, `ROADMAP.md`, `PROGRESS.md` | Recorded the manual sync/coverage state and updated documentation status. |

## Android UPnP default-on pass (2026-10-19)

| Path | Change |
|---|---|
| `android/app/src/main/cpp/CMakeLists.txt`, `build/CMakeLists.txt` | Enabled `WITH_UPNP` for Android, added the common daemon lifecycle, UPnP, I2PControl and local TUI server sources to `i2pd_jni`, linked `MiniUPnPc::MiniUPnPc` and defined `I2PD_NO_PRIVSEP` for the JNI graph. |
| `daemon/Daemon.{h,cpp}` | Excluded the POSIX privilege-separated log-supervisor path from Android builds without changing desktop Unix/OpenBSD/Windows behavior. |
| `contrib/ci/android/vcpkg.json` | Added `miniupnpc` for the ARM64 Android JNI build. |
| `android/app/src/main/assets/i2pd.conf` | Set `upnp.enabled = true` and `upnp.name = openi2pd` by default. |
| `android/app/src/main/res/xml/preferences_root.xml`, `SettingsFragment.java`, `strings.xml` | Restored the UPnP enable/forward-name controls with default-on state and explanatory summaries. |
| `android/app/src/main/AndroidManifest.xml`, `MainActivity.java` | Declared the Android 17 local-network permission and Wi-Fi multicast permission; request the local-network runtime permission with the notification prompt. |
| `android/app/src/main/java/org/openi2pd/router/RouterService.java` | Holds a Wi-Fi `MulticastLock` while the router is running (when UPnP is enabled) and releases it on stop/failure/destroy. |
| `docs/android-modernization.md`, `docs/user-guide/configuration.md`, `docs/build-system.md`, `docs/dependencies.md`, `docs/devs/building/android.md`, `TODO.md`, `PROGRESS.md` | Documented the default, permission model, JNI dependency and pending dynamic validation. |

## CTest subsystem and manual-CI integration pass (2026-10-19)

| Path | Change |
|---|---|
| `tests/CMakeLists.txt` | Registered Linux sandbox/IPC tests only on Linux and OpenBSD policy/imsg tests only on OpenBSD; added i2p/security/memory/IPC/TUI/torrent/tools/Perl/interop/platform/Android labels, Android static CTest checks and optional one-input `ctest -L fuzz` entries. |
| `tests/check-build-system.cmake` | Added compile-free inventory assertions for every checked-in `test-*.cpp` and `tests/fuzz/*.cpp` source. |
| `.github/workflows/build.yml` | Kept the workflow `workflow_dispatch`-only; added the per-label CTest inventory step and retained Linux GCC/Clang, Android, interop and ASan+UBSan/TSan jobs. |
| `docs/testing.md`, `docs/ci.md`, `TODO.md`, `PROGRESS.md` | Recorded the platform-specific registration, labels, optional fuzz CTest entries, the published-run limitations and the manual dispatch/audit state. |

## Native init services and Debian packaging (2026-10-19)

| Path | Change |
|---|---|
| `contrib/i2pd.service`, `libi2pd/Config.cpp`, `daemon/Daemon.cpp` | Native foreground systemd unit with `i2pd` account, State/RuntimeDirectory, journald, SIGINT stop, bounded restart and IPC-compatible hardening; explicit `--foreground` overrides a local `daemon=true`. |
| `contrib/sysvinit/i2pd`, `debian/i2pd.init` | LSB SysV fallback using `start-stop-daemon --background`, secure dirs, HUP/INT/USR1 handling and full status/reload actions. |
| `contrib/openrc/i2pd.openrc`, `contrib/openrc/conf.d/i2pd` | OpenRC `supervise-daemon` runscript and `/etc/conf.d` defaults. |
| `contrib/runit/i2pd/`, `contrib/s6/i2pd/`, `contrib/s6-rc/source/i2pd/` | Native foreground run scripts, separate runit logger, s6-rc longrun and dependency marker. |
| `contrib/shepherd/i2pd.scm` | Native GNU Shepherd service with foreground start, `i2pd` account, syslog and SIGINT grace. |
| `contrib/dinit/i2pd` | Foreground dinit sample with file logging and bounded restart. |
| `build/CMakeLists.txt` | Added mutually exclusive `I2PD_SERVICE_MANAGER`/`I2PD_SERVICE_DIR`/`I2PD_SERVICE_CONFDIR` install selection and `Service` component. |
| `tests/check-services.cmake`, `tests/check-build-system.cmake`, `tests/CMakeLists.txt` | Static service-contract checks and install-graph assertions; `services-static` test. |
| `debian/control`, `debian/rules`, `debian/*.install`, `debian/*.manpages`, `debian/*.links`, `debian/*.default`, `debian/*.tmpfiles`, `debian/postinst`, `debian/postrm`, `debian/changelog`, `debian/NEWS`, `debian/copyright` | Debhelper 13 packaging, `i2pd`/`i2pd-tui` split, conffiles, service/tmpfiles integration, autopkgtest smoke test and packaging metadata. |
| `.github/workflows/packaging.yml` | Manual service-contract, lintian and Debian autopkgtest workflow. |
| `docs/service-management.md`, `docs/user-guide/run.md`, `docs/user-guide/install.md`, `docs/build-system.md`, `docs/devs/building/unix.md`, `docs/README.md`, `docs/mkdocs.yml`, `docs/security/privilege-separation.md` | Documented native manager operation, Debian packages, CMake `Service` selection, hardening, logs and pending runtime validation. |
| `TODO.md`, `ROADMAP.md`, `PROGRESS.md`, `AGENTS.md` | Recorded SERVICE/DEB work, static-only status and pending CI/package/service validation. |

All entries above are implemented by static analysis only. No build, configure,
package build, lintian, autopkgtest, service start/stop, daemon lifecycle or
systemd/OpenRC/runit/s6/Shepherd/SysV execution was performed locally. Manual
CI and disposable-host validation remain required before claiming runtime
service, package or privilege-separation validation.

## Selective upstream openssl integration (2026-10-19, static)

| Path | Change |
|---|---|
| `libi2pd/SSU2Session.cpp`, `libi2pd/SSU2Session.h`, `tests/test-ssu2-preauth.cpp` | Made the PathChallenge response state explicit (`IsSSU2PathChallengeResponseAllowed`), retained the independent `SendPathResponse()` guard, documented the required `Introduced` RelayResponse exception, and added the state-matrix regression. |
| `libi2pd_client/I2PTunnel.cpp` | Applied the remaining `std::stringstream::view()` conversions for the server response and IRC paths and compacted incomplete header lines to prevent quadratic retention. The existing request-side `view()`/header-count work is preserved. |
| `libi2pd/NetDb.hpp`, `tests/test-netdb-store-lookup-hardening.cpp` | Lowered the general peer floor to 0.9.57 for Debian 12 interoperability while retaining the separate 0.9.62 high-bandwidth floor; added compile-time policy assertions. |
| `libi2pd_client/Torrents.cpp`, `libi2pd_client/Torrents.h`, `tests/test-torrents-security.cpp` | Rejected piece blocks after stop, changed completed-piece persistence to a weak capture plus authoritative session-membership check, and added stopped/in-range block regression coverage. |
| `docs/upstream-sync.md` | Added the verified 11-commit review table for `PurpleI2P/i2pd:openssl`, including adaptation and omission rationale. |
| `TODO.md`, `ROADMAP.md`, `PROGRESS.md`, `AGENTS.md` | Recorded the upstream-review increment and pending CI validation. |

Upstream `942d6907` (browser torrent table) was omitted because the browser
console is intentionally removed; the native TUI already refreshes after
commands and does not hide its table. Upstream `ec19bfb5` (3.0.0 release
metadata) was not imported because it would assert a release identity and
replace fork-owned Debian/RPM/changelog history. No build, CTest, sanitizer,
live peer, packaging or platform validation was performed locally.

## Selective upstream openssl continuation (2026-10-20, static)

| Path | Change |
|---|---|
| `libi2pd/util.cpp`, `tests/test-routercontext-port-range.cpp`, `tests/CMakeLists.txt` | Added Intel AMT ports 16992-16995 to the reserved-port table from upstream `37090fe0` and pinned the complete table plus boundary values in a new CTest regression. |
| `contrib/i2pd.conf`, `contrib/openbsd/i2pd.conf`, `docs/user-guide/configuration.md` | Adapted the reseed URL/built-in-list wording from upstream `21ad7d43`; retained the fork's fail-closed `reseed.verify=true` default and rejected the upstream `false` comment. |
| `contrib/rpm/i2pd.spec`, `contrib/rpm/i2pd-git.spec`, `tests/check-build-system.cmake` | Added guarded Fedora Boost.JSON/URL requirements and static assertions that the specs never invoke make/ninja macros or lose the Fedora guard. The RHEL 9 torrent branch is omitted pending a validated GCC 13 toolset. |
| `docs/upstream-sync.md`, `docs/dependencies.md`, `docs/build-system.md`, `docs/devs/building/unix.md`, `.agents/skills/openi2pd-upstream/references/sync-checklist.md` | Recorded the complete 18–20 October UTC window, timezone/merge rules, dispositions, omitted packaging changes and the RHEL 9 GCC 13 caveat. |
| `TODO.md`, `ROADMAP.md`, `PROGRESS.md` | Recorded the continuation, prepared regression and pending manual CI compilation/tests. |

No build, CTest, sanitizer, package build, rpmbuild, lintian, service start or
live-peer run was performed. The new port-table test and RPM guards remain
pending manual CI.

## String safety and libc compatibility (2026-10-21, static)

| Path | Change |
|---|---|
| `libi2pd/AddressFormat.h`, `libi2pd/Log.h`, `libi2pd/Reseed.cpp` | Added bounded IPv4/IPv6/scope-ID and endpoint formatting and `LogPrint` overloads, avoiding Boost.Asio's unsafe POSIX `sprintf`/`strcat` scope-ID path at Reseed and other address log sites. |
| `libi2pd/util.h`, `libi2pd/util.cpp` | Added the bounded `CopyString` helper and replaced the uninitialised `ifreq`/`strncpy` interface-name copy; switched address logs to the safe formatter. |
| `i18n/I18N.h` | Replaced the two-pass `snprintf`/`front()` pattern with a checked vector-backed formatter and guarded plural indexing. |
| `libi2pd/Family.cpp`, `libi2pd/Log.cpp`, `libi2pd_client/Torrents.cpp` | Hardened fixed-buffer certificate-name extraction, timestamp fallback/termination and torrent hex formatting. |
| `libi2pd_client/SOCKSAddress.h`, `libi2pd_client/SOCKS.cpp` | Moved the SOCKS domain buffer to a bounded helper that rejects zero-length/overflow input and cannot wrap its length byte. |
| `build/CMakeLists.txt`, `tests/check-build-system.cmake` | Selected `BOOST_ASIO_HAS_SNPRINTF=1`, added the TUI header include path, and rejected unbounded C string functions in first-party sources. |
| `tests/test-address-format.cpp`, `tests/test-util-copy-string.cpp`, `tests/test-i18n-format.cpp`, `tests/test-socks-address.cpp` | Added prepared regressions for scope IDs, endpoints, small/zero-capacity buffers, embedded NULs, i18n formatting and SOCKS length boundaries. |

No compiler, CTest, sanitizer, fuzzer, project validator or runtime test was
performed. These changes remain pending manual CI compilation and tests.

## Client and persistence continuation (2026-10-21, static)

| Paths | Change |
|---|---|
| `libi2pd_client/I2PTunnel.cpp`, `I2PTunnel.h`, `I2PTunnelHTTP.h`, `tests/test-i2ptunnel-http.cpp`, `tests/fuzz/fuzz-http-parser.cpp` | Extracted the production bounded incremental transformer; independent request/response state and stable output; fragmentation/bounds regressions and differential segmentation fuzz input. |
| `libi2pd_client/AddressBook.cpp`, `AddressBook.h`, `tests/test-addressbook-case.cpp` | Bound lookup replies to the expected authenticated resolver, checked ports/RNG and capped/expired pending authority; production-registry regressions prepared. |
| `libi2pd_client/Torrents.cpp`, `Torrents.h`, `TorrentsTunnel.cpp`, `TorrentsTunnel.h`, `tests/test-torrents-security.cpp` | Added irreversible removal authority, serialized cleanup/re-add gating, native settings deletion in both backends, final-shutdown snapshot authority and deterministic removal regression cases. |
| `tests/CMakeLists.txt`, `.github/workflows/build.yml` | Registered the HTTP target and security/memory labels; selected the three client/persistence targets explicitly within the existing manual Linux workflow. |
| `AGENTS.md`, `TODO.md`, `ROADMAP.md`, `PROGRESS.md`, `docs/testing.md`, `docs/security/memory-safety-2026.md`, `docs/torrent/local-state.md`, `docs/upstream-sync.md`, owning skill references | Recorded decisions, prepared tests, pending CI and residual limitations. |

No wire or persisted schema changed. No compilation, tests, project validators
or workflows were executed. Windows deletion races, broader client lifecycle,
dedicated shutdown/crash coverage and global process separation remain open.

## Java comparison target update (2026-10-21, static)

The current target is `i2p-2.14.0` at
`3c98503c5f14efe7bdd783528ba7f621f12d8081`. Updated the interoperability,
functional-parity and Snark reference sections; interop skill/index; AGENTS
and tracking; and the test/CI documentation. `check-reference.sh` checks the
new pin, while `check-vectors.pl` keeps the historical 2.13.0 corpus pin and
uses `I2P_JAVA_FIXTURE_REFERENCE`. CMake and the manual interop job separate
the current-target checkout from the historical provenance checkout.

Fixture JSON/hex and their expectations are unchanged. NET-026 tracks the
pending 2.14.0 source re-audit and evidence migration. No build, test,
validator or workflow was executed; existing implementation changes remain
uncommitted and preserved.
