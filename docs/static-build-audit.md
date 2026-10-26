<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: Static compilation and linkage audit.
-->

# Static compilation and linkage audit

Audit baseline: `e669765d`, following the 31-commit publication series based
on `284544a5`. Status: **Implemented / static**, **Pending CI compilation**,
**Pending CI tests**. No compiler, configurator, build, test, linter, project
validator or project executable was run. No workflow was dispatched, and no
commit or push was made for this audit.

## Coverage and method

Six specialized reconnaissance workstreams inspected the recent changes,
then independently implemented demonstrated corrections. The coordinator
reviewed the shared interfaces and CMake/workflow integration. A second test
pass traced newly changed fixture declarations, signatures and assertions.

| Area | Static review boundary |
|---|---|
| C++23 core | `Modern.h`, `Expected.h`, `AtomicSharedPtr.h`, Queue/util/Log, Tunnel/TransitTunnel/Pool/Config, I2NP, Destination/Garlic/Streaming/Datagram and their consumers |
| Protocols and crypto | Crypto/CryptoKey, signature/identity hierarchies, NTCP2/SSU2, transport Strategy, Family/Reseed/PQ, RouterInfo/netDb/LeaseSet, SAM/I2CP, address book and HTTP/SOCKS/UDP clients |
| Processes and IPC | Channel/Worker/Supervisor, role dispatch, router/torrent storage, Linux/OpenBSD/Windows guards, imsg and Named Pipe interfaces, daemon adapters |
| Torrents, TUI and tools | Engine/creator/comments/DHT/RPC/control/state/storage, terminal/client backends, shared TUI dispatch, locale helpers and native key/firewall tools |
| Android | Java declarations/imports and recent typed commands, JNI exported names/header/callback descriptors, AIDL implementations, resource references and configured source/API levels |
| Build and platforms | Explicit source membership, conditional targets, static archive exports, atomic/crypto/Boost dependencies, Android JNI subset, Windows/Unix flags, tests and manual workflows |

The review used Git diffs, source searches, declaration/definition/caller
tracing, dependency documentation and source inspection. A complete dynamic
link or language-availability proof cannot be inferred from this review.

## Demonstrated compile and link corrections

| ID | Cause and affected configuration | Correction and prepared regression |
|---|---|---|
| SB-001 | `TorrentsRPC.cpp` uses out-of-line Boost.JSON/URL APIs, but CMake discovered only headers and linked neither implementation. No project source includes their `src.hpp`. All torrent-enabled native/JNI links are affected. | Discover `program_options json url`, link `Boost::json`/`Boost::url` to `libi2pdclient`, re-discover the enabled components in the installed config, and ship Docker runtime libraries. New `test-boost-linkage` and the installed-consumer job exercise both libraries. |
| SB-002 | `tui/main.cpp` and `test-i18n-locale.cpp` qualify `i2p::i18n::tr`/`ntr`, while the templates were defined only globally. | Define the canonical templates inside `i2p::i18n`; global using aliases preserve existing callers. Locale regression covers qualified/global formatting and plural overloads. |
| SB-003 | `test-torrents-engine-interop.cpp` constructs `splitTrackers` with `info` before declaring it. | Extract the original info bytes before the first consumer; keep the existing tracker and malformed-metainfo regressions. |
| SB-005 | Linux builds with older UAPI headers reference absent Landlock REFER/TRUNCATE/TCP constants and `handled_access_net`. | Use the stable two-u64 extensible ruleset layout and documented runtime-ABI-gated bit positions. Static assertions compare installed UAPI declarations when present. Missing base headers produce an explicit sandbox failure, never an unenforced success. Existing sandbox/storage tests remain prepared. |
| SB-006 | Non-MSVC Windows builds with `WITH_HARDENING=ON` receive Unix ELF `-z relro/-z now` options. | Select PE `--dynamicbase`/`--nxcompat` on MinGW/Clang64 and retain ELF flags on Unix. The manual MSYS2 job now enables hardening. |

Boost's implementation requirement is documented in the official
[JSON overview](https://www.boost.org/latest/libs/json/doc/html/index.html)
and [URL repository](https://github.com/boostorg/url). Landlock constants and
layout are checked against the
[Linux 6.7 UAPI](https://github.com/torvalds/linux/blob/v6.7/include/uapi/linux/landlock.h);
newer optional UDP bits remain selected only when the build headers expose
them. PE and ELF linker switches are separate contracts in the
[GNU linker manual](https://sourceware.org/binutils/docs/ld/Options.html).

## Additional integration corrections

- `I18N_langs.h` now includes `<memory>` directly for `std::shared_ptr`;
  `test-i18n-locale` includes that header first. Router and torrent storage
  implementations include `<stdio.h>` directly for POSIX `renameat`.
  These remove library/header-order assumptions; default-toolchain failure
  was not demonstrated for every previous transitive include path.
- Windows SDK macros (`NOMINMAX`, `WIN32_LEAN_AND_MEAN`, Windows 10 API floor)
  are PUBLIC core usage requirements so header inline code sees one contract
  in clients, tools, tests and installed consumers. `WIN32_APP` stays PRIVATE.
  `shell32` explicitly supplies `FS.cpp`'s `SHGetFolderPathW` import; default
  CMake Windows standard libraries may already supply it, so this is explicit
  dependency closure rather than a claimed failure of every default build.
- `test-privsep-linux-ipc` asserted both success and failure of the same outer
  `expected`. Its second assertion now checks the empty inner `optional`,
  matching `ReceiveWithFd`. This is a demonstrated regression-test logic
  error, separate from compilation/linkage.
- The build/packaging static contracts now require compiled Boost dependencies
  and Docker libraries instead of enforcing the incorrect header-only model.

## Disproved hypothesis

SB-004: **Dropped (pinned API supports overload)**. The initial Android pass
suspected four `TextInputLayout.setHint(R.string...)` calls. Primary-source
verification found both the `CharSequence` and `@StringRes int` overloads in
[pinned Material 1.14.0](https://github.com/material-components/material-components-android/blob/1.14.0/lib/java/com/google/android/material/textfield/TextInputLayout.java).
The four calls are valid; the audit's unnecessary edits were removed before
handoff. No Android source compilation defect was demonstrated by this pass.

## Reviewed interfaces without an additional demonstrated defect

Changed encryption return types and overrides, offline-signature argument
lists, RouterContext caller-owned Noise state, tunnel-hop ownership, response
timer overrides, transport connectors, I2CP key-length APIs, SAM accept
management, JNI exports and SAF callback descriptors were traced through their
consumers. Explicit native source lists and platform-specific daemon/TUI/IPC
members were accounted for. No missing source or additional undefined method
was demonstrated in those paths.

The legacy EC implementation in `Signature.h` is inside the LibreSSL branch;
it must not be described as unconditional OpenSSL 4 code. Provider and legacy
branches still require the actual configured dependency/toolchain CI lanes.

## Pending validation and separate findings

- Compile and link Linux GCC 13+/Clang 17+, OpenBSD libc++/LibreSSL,
  OpenSSL 3.x/4.x, MSVC 19.35+ and MSYS2/Clang64, with torrents both enabled
  and disabled. Test 32-/64-bit and static dependency configurations where
  advertised; this review provides no evidence of their success.
- Run `test-boost-linkage`, `test-i18n-locale`, torrent engine interop and Linux
  IPC/sandbox/storage regressions, plus the installed-package consumer and
  Docker/package checks. They are prepared, not executed.
- Exercise Linux old/new UAPI header configurations and runtime Landlock ABIs,
  including absent base headers and the fail-closed path. The existing CI
  matrix does not establish coverage of every historical header combination.
- Preserve Java 25/minSdk 29. JDK/AGP/D8/R8/NDK compatibility and all four JNI
  ABIs need the manual Android build; AAPT/Lint/device behavior is unverified.
  No toolchain limitation was converted into a Java source downgrade.
- A separate Android runtime lifecycle concern was observed: the outer native
  `start()` publishes `Starting` before calling `DaemonAndroidImpl::start()`,
  which returned early for that state. ANDROID-017 now has a separate static
  correction: the owner transition is shared with a deterministic host
  regression, and the adapter invokes the real daemon start. This is not
  compile/link or device evidence; those manual CI checks remain pending.

See `BUILD-018`, `TEST-023` and `ANDROID-017` in `TODO.md`. No task is marked
validated by CI on the basis of this audit.

## 2026-10-25 multi-subsystem audit update

Six read-only passes rechecked the tree after `2ae36284`. Confirmed defects
were corrected without compilation:

- `daemon/I2PControl.cpp`: a stray closing brace closed `namespace client`
  before the handler definitions, and `steady_timer::cancel(error_code)` was
  called on Boost.Asio releases that only provide `cancel()`. Includes for
  `<cstdio>`, `Crypto.h`, `<openssl/bn.h>` and `<openssl/rsa.h>` are now
  explicit; the throwing `remote_endpoint()` log and the shutdown/handshake
  conflation were removed. `ip::port_type` became `unsigned short` so the
  documented Boost 1.74 floor still works.
- Dead declarations removed: `NetDb::ManageRequests` and the undefined client
  `I2PUDPClientTunnel` cleanup-timer pair.
- Warnings removed from the last completed package run: unused `str_rmatch`,
  PQ-only `token`, unused `MAX_RESEED_ENTRIES` (now the 400-entry SU3 bound),
  redundant `deny` capture, and the deprecated `SSL_set1_host` selection.
- Build graph: a `CheckLanguage(RC)` probe enables the Windows resource when a
  resource compiler exists (platform modules normally do this already), with
  `version.h` on the `i2pd` include path; the ncurses fallback probes
  `CURSES_LIBRARIES` with `wadd_wch`;
  `I2PD_HAVE_SSL_SET1_DNSNAME`/`I2PD_HAVE_OPENSSL_MLKEM_H` have `Crypto.h`
  defaults and static-contract markers; `I2PD_INSTALL_CONFIG` keeps the
  OpenBSD port from installing a second configuration tree; `WITH_HARDENING`
  warns on MSVC; the Android APK job asserts packaged reseed certificates.
- Torrent/TUI: administrative posts hold `weak_from_this().lock()`; the
  no-destination shutdown stops owned storage workers; generated metainfo
  values use `std::deque`; POSIX startup retries a UTF-8 `LC_CTYPE` and the
  terminal undefines every `KEY_Fn` macro it maps.

Residual items require design work or dynamic evidence and were not changed:
`TorrentsTunnel::InitTorrentFiles` mutates session objects from the disk
worker; OpenBSD `CMAKE_INSTALL_SYSCONFDIR` needs a port build (WANTLIB/libutil
and the `i2pd_privsep` capability macro set are corrected statically); Android
D8/R8 Java 25 dexing is now invoked by the manual lane but remains unexecuted; the PARITY-014 feature backlog is
not compile-audit work.

This update is static analysis only. No compiler, configure, CTest, Gradle,
validator or workflow was executed, and nothing here is CI validation.

## 2026-10-26 compile/link continuation

Six read-only passes rechecked the current uncommitted tree: core C++23 and
headers; transports/crypto/netDb/client protocols; IPC/privsep; BitTorrent/
TUI/tools; Android/JNI; CMake/linkage. The confirmed defects were corrected
without invoking a compiler, configurator or test runner.

- `libi2pd/TunnelPool.cpp`: `TunnelExpired` still used the removed
  `std::pair` shape (`.first`/`.second`) after `m_Tests` became a
  `std::map<uint32_t, TunnelTest>`; it now uses `.outbound`/`.inbound`.
- `libi2pd/Streaming.cpp`: the `Stop()` wait referenced
  `STOP_ON_SERVICE_TIMEOUT` unqualified from `i2p::stream`, but the constant
  lives in `i2p::client`; both uses are qualified.
- `privsep/WindowsIpc.h`: `WindowsChannel` declared `m_Role` and `m_RoleSet`
  twice; the duplicate pair is removed and the `Admin` default retained.
  `WindowsSupervisor.cpp` now defines the declared
  `IsCurrentProcessInJobObject()` test seam, which the Windows supervisor test
  links.
- `daemon/Daemon.cpp`: the init-rollback guards used `_ANDROID__`, which no
  toolchain defines, while the members are compiled out by `I2PD_NO_PRIVSEP`;
  both guards use `__ANDROID__`.
- `tui/main.cpp`: multiple status/title builders concatenated the
  `std::string_view` returned by one-argument `i2p::i18n::tr` with
  `std::string`/`const char*`, which C++23 does not provide. Every such
  operand is materialised as `std::string`; `check-i18n.cmake` now rejects the
  unwrapped pattern.
- `libi2pd/Crypto.cpp` and `libi2pd/Log.cpp` include `<openssl/hmac.h>` and
  `<cctype>` explicitly; `SSU2Session.h` includes `<cstring>` and
  `<unordered_map>`; other public headers gained the standard header for the
  containers/utilities they declare.
- `libi2pd_client/HTTPProxy.cpp` includes `<cctype>` and casts the
  `Connection` byte to `unsigned char` before `std::tolower`.
- Windows IPC test: `AcceptBound`/`SpawnWindowsWorker` take a wide nonce hex
  (`std::wstring`). The test now keeps the wide value instead of narrowing it;
  it narrows only for `DecodeWindowsWorkerNonce`.
- `test-i2pcontrol-socket.cpp` uses the same
  `BOOST_ASIO_HAS_LOCAL_SOCKETS && !_WIN32` guard as the production helpers,
  so an Asio build without AF_UNIX is a skip rather than an ill-formed TU.
- CMake: the combined linked OpenSSL probe includes `<openssl/bn.h>`; preset
  schema 5 declares the required CMake 3.25; `i2pd_privsep` and both TUI
  backends inherit `${I2PD_EXTRA_LIBS}`; the exported `i2pd_privsep` archive
  publishes its `libi2pd` dependency; `tools/LICENSE` is installed from one
  component; the header-only `test-privsep-{supervisor,authorization,
  capabilities}` suites link the IPC archive.

Residual and not claimed: probe-dependent header branches (`HKDFContext`
members, `OPENSSL_SIPHASH`/`OPENSSL_PQ`) are layout-compatible today but an
installed consumer still compiles its own macro fallback; no compiler,
configure, CTest, Gradle, device or workflow was executed. Compilation and
linkage remain pending manual CI.
