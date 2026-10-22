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
  which returns early for that state. This is not a compile/link error and is
  tracked separately for lifecycle correction and device regression.

See `BUILD-018`, `TEST-023` and `ANDROID-017` in `TODO.md`. No task is marked
validated by CI on the basis of this audit.
