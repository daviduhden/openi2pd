# Compilation and lint warning inventory

This file records warning diagnostics from GitHub Actions separately from
compile errors. Repeated header inclusions and template instantiations are
grouped by cause, not counted as independent defects. A successful build can
still contain warnings, and a job stopped by an earlier failure says nothing
about later targets.

## History coverage

The 2026-11-04 refresh paginated the complete accessible API: **255 runs**,
**51 SHAs**, **16 current/former workflow names**, dated 2026-10-17 through
2026-11-04 (166 failed, 62 cancelled, 27 successful). Retrieved full logs for
252 runs, including successful siblings and successful steps in failed jobs.
Run `39058430980` has partial logs but job `117472356821` is unavailable;
`39058841124` and `39058346321` have no retrievable logs. These retention
gaps are not clean evidence. Logs and paginated
metadata remain outside Git under `/tmp/openi2pd-audit-20261104`.

The eight newest runs use `d7e8491ce31f46912103e761c6d5ec2cd7f747cf`; see
[exact jobs and results](COMPILATION-ERRORS.md). Validation artifacts contain
source documentation and three sanitizer evidence manifests. Packaging has
no downloadable artifacts; generated packages were not inspected for that run.
Historical findings below remain preserved.

### CW-016 — I2CP callback indentation misrepresented its guard

- **Observed:** [Packaging 41921782301](https://github.com/daviduhden/openi2pd/actions/runs/41921782301),
  container `126895843200` and musl `126895843550`, at the SHA above:
  `I2CP.cpp:890`, `-Wmisleading-indentation`.
- **Correction:** align two lambda-body statements following an early return;
  control flow and lifetime authority are unchanged.
- **Equivalent review:** early-return/deeper-indentation seams across native
  core, clients, daemon and privsep; no further demonstrated instance found.
  Status: Implemented / static; Pending CI compilation.

### CW-017 — Standalone CMake checks relied on unset policies

- **Observed:** [Validation 41921782009](https://github.com/daviduhden/openi2pd/actions/runs/41921782009),
  `126895844279`: legal-metadata passed with CMP0009 warning; upstream-pins
  passed with three CMP0054 warnings before the later CE-075 failure.
- **Correction/global review:** set CMP0009 NEW in recursive legal-metadata,
  build-system and packaging inventories, preventing directory-symlink
  traversal. Set CMP0054 NEW in upstream-pins so quoted discriminator names
  remain literals even when same-named variables exist. Script mode does not
  inherit the project's policy baseline.
- **Validation:** these four compile-free source checks pass locally without
  these warnings. Remote CMake lanes remain Pending CI tests. No diagnostic
  suppression or global policy change was introduced.

### CW-018 — JNI definition parameters named but unused

- **Observed:** [Android 41364864089](https://github.com/daviduhden/openi2pd/actions/runs/41364864089),
  job `125022760915`, SHA `56f17c0f47a3d1a328be6c54df7b661c480924f8`:
  33 `-Wunused-parameter` diagnostics for `jclass clazz` in
  `i2pd_android.cpp`. The newest Android run stops before this target and
  therefore does not establish that these warnings disappeared.
- **Correction/global review:** inspect JNI definition bodies, omit unused
  class parameter names while preserving the JNI parameter types and ABI,
  and adjust the one affected Doxygen description. Declaration-only names
  do not trigger this diagnostic and remain valid.
- **State:** Implemented / static; Pending CI compilation. Native/JNI, Java,
  Android lint, D8/R8 and API 29 device coverage are still pending.

### CW-019 — Historical logger initialization order and date truncation

- **Observed:** successful [Packaging 39220635893](https://github.com/daviduhden/openi2pd/actions/runs/39220635893),
  SHA `b5fa95529f3b78fd925702485f6c904b635dec3d`, Debian job logs:
  `Log.h:105` / `Log.cpp:78` initialization order (`-Wreorder`), and
  `Timestamp.cpp:314` `%04i` into a nine-byte date buffer
  (`-Wformat-truncation`). Success did not imply warning-free compilation.
- **Current disposition:** logger initializer order matches declarations;
  the date formatter bounds the year and explicitly writes eight digits.
  Preserve these causes as historical evidence; no additional source patch
  is justified by the old diagnostic. Broader current matrix validation
  remains pending and these observations do not validate future revisions.

### External diagnostics remaining open

Latest Android/OpenBSD/package logs include action-runtime `punycode` and
`url.parse()` deprecations; Windows includes an MSYS2 updater instruction to
terminate other programs. These are dependency/runtime messages, not native
compiler warnings. Successful historical packaging `41364876230` also exposes
Node 20 action metadata and autopkgtest `--force-yes` deprecations (CW-015).
They remain external debt; no global installation or suppression was changed.
Expected negative CMake feature probes are capability detection, not product
compilation failures.

`I2PD_WARNINGS_AS_ERRORS=ON` now requests `-Werror` or `/WX` on project targets.
Default OFF remains appropriate until lane-specific cleanliness is demonstrated.
Imported dependency builds are unaffected; headers included in project units
can still emit diagnostics. This option supplies no clean-matrix evidence.
Both manual OpenSSL workflows expose `warnings_as_errors` with default false;
the shared action validates its ON/OFF value before passing it to CMake.
The workflow source guard and prepared negative fixtures pin the complete
forwarding chain and the separate configure/build capture and upload paths.

On 2026-10-31, GitHub Actions retained 206 runs across 11 current and former
workflow names, 43 distinct commit SHAs, dated 2026-10-17 through
2026-10-31. Complete logs were retrieved for 202 completed runs. Logs for
job IDs `39058841124`, `39058430980` and
`39058346321` were no longer available (`log not found`); the remaining three
older log retrievals completed without timeout. The newest Windows run
`41226168239` was still in progress at review time. The artifact API listed 54
unexpired artifacts from 30 runs. Relevant Android, source-documentation,
Windows CTest, Debian and fuzz artifacts were downloaded for inspection under
`/tmp/openi2pd-history/diagnostics/`; no CI artifacts are stored in the
repository.

The prior 201-run history is retained below. The five newest runs all used
published `main` SHA `abe09136c84d1a4df487306dad6a7eb8e2034806`: Linux and
Android `41226171242`, Windows `41226168239`, OpenBSD `41226173721`, Fuzzing
`41226176399` and Packaging `41226179732`. Local changes below that SHA have
only static review; the recorded runs do not validate them.

## Current project warnings

### CW-001 — Nested block-comment openers

- **Evidence:** Windows [41222224440](https://github.com/daviduhden/openi2pd/actions/runs/41222224440), job `124599005640`; Linux and Android [41222227216](https://github.com/daviduhden/openi2pd/actions/runs/41222227216); OpenBSD [41222229438](https://github.com/daviduhden/openi2pd/actions/runs/41222229438), job `124599019529`; Fuzzing [41222231944](https://github.com/daviduhden/openi2pd/actions/runs/41222231944); Packaging [41222233937](https://github.com/daviduhden/openi2pd/actions/runs/41222233937).
- **Commit:** `45f5f739aa47ed87aa7462e9b040c0db601bc667`.
- **Diagnostics:** `-Wcomment` at `privsep/Protocol.h:449`,
  `privsep/WindowsSupervisor.h:304` and `tools/vanity.hpp:59`; all are
  repeated through their consumers. `tests/test-base-64.cpp:82` produced the
  same warning immediately before its unterminated-comment compile error.
- **Cause:** comment reflow left a second `/**` inside an open block. In the
  base64 test, the duplicate opener also comments out the test body and leaves
  the file unterminated.
- **Correction:** removed the duplicate openers and converted the base64
  explanation to a line comment. Added a compile-free Perl lexical scan with
  fixtures for nested comments, unterminated comments, quoted strings and raw
  strings; the manual Linux workflow runs it.
- **Status:** Corrected by static analysis; pending manual CI compilation and
  static regression validation.

### CW-002 — `-Wfree-nonheap-object` during LeaseSet2 signature buffer growth

- **Evidence:** static-musl/package and container jobs in Packaging run
  [41222233937](https://github.com/daviduhden/openi2pd/actions/runs/41222233937),
  SHA `45f5f739aa47ed87aa7462e9b040c0db601bc667`, GCC 15.2 / libstdc++ 15.2.
- **Diagnostic:** libstdc++ `new_allocator.h:172` warns that sized delete is
  called on a pointer with a nonzero offset. The inlining trace ends at
  `LeaseSet2::VerifySignature` in `libi2pd/LeaseSet.cpp:606-609`, specifically
  `vector::reserve()` followed by `push_back()` and range insertion. It repeats
  for verifier instantiations and package targets.
- **Cause:** under investigation. The input and signature bounds are checked;
  the warning appears during vector reallocation analysis, but the exact GCC
  false-positive status is not yet confirmed.
- **Correction:** construct the final-sized signed-data vector and copy the
  bounded message bytes into it, avoiding the reserve/reallocate/append path.
- **Status:** Corrected by static analysis; pending GCC 15 CI confirmation and
  existing LeaseSet2 signature regression results.

### CW-003 — Android Lint findings

- **Evidence:** Android Lint job in [41222227216](https://github.com/daviduhden/openi2pd/actions/runs/41222227216), SHA `45f5f739aa47ed87aa7462e9b040c0db601bc667`; recurring on [41207325363](https://github.com/daviduhden/openi2pd/actions/runs/41207325363), SHA `46738ddf6f0f9b8e01e31d7c28bc6396215f82e8`.
- **Artifact:** `android-arm64-reports`, artifact ID `13050747638`, contains
  `lint-results-debug.xml` with 51 warning instances across 17 issue IDs.
- **Static disposition:** 34 instances have been corrected or narrowly
  accounted for: lifecycle/context typing, localized string/plural use,
  genuinely unused resources, API-level annotations, typography, drawable
  composition, and the deliberate compact/wide navigation replacement.
  The pre-33 icon's monochrome warning is scoped because the API-33-qualified
  icon supplies the monochrome layer. No broad lint suppression was added.
- **Remaining observed findings:** 17 instances: `ChromeOsAbiSupport` (2),
  `VectorPath` (12), `NestedWeights` (1) and `Overdraw` (2). The ABI warning
  needs an x86_64 dependency prefix/artifact decision. Vector path geometry is
  retained to preserve the supplied brand. The nested weights and overdraw
  remain under review because layout changes could alter screen behavior or
  visual surfaces.
- **Status:** Other edits are corrected by static analysis; the 17 retained
  findings and all Android changes await Android CI. XML parsing, resource-key
  inspection and diff review do not substitute for Gradle/Lint/APK validation.

## External and infrastructure warnings

### CW-004 — OpenBSD linker reports Boost.Asio `strcat`

- **Evidence:** OpenBSD runs [41222229438](https://github.com/daviduhden/openi2pd/actions/runs/41222229438), job `124599019529`, SHA `45f5f739aa47ed87aa7462e9b040c0db601bc667`, and [41226173721](https://github.com/daviduhden/openi2pd/actions/runs/41226173721), job `124610671636`, SHA `abe09136c84d1a4df487306dad6a7eb8e2034806`.
- **Diagnostic:** `ld` reports `strcat()` in
  `boost::asio::detail::socket_ops::inet_ntop`, instantiated in `SOCKS.cpp.o`.
- **Classification:** external dependency implementation included into a
  project translation unit; the project sources contain no direct `strcat`
  call. Investigate a maintained Boost fix or supported platform adaptation;
  do not attribute the warning to project code or suppress it globally.
- **Status:** External warning; solution pending dependency/toolchain review.

### CW-006 — Unused asynchronous completion parameters

- **Evidence:** Linux and Android [41226171242](https://github.com/daviduhden/openi2pd/actions/runs/41226171242), Linux OpenSSL 3 job `124610663647`; Windows [41226168239](https://github.com/daviduhden/openi2pd/actions/runs/41226168239), MSYS2 job `124610655800`; OpenBSD [41226173721](https://github.com/daviduhden/openi2pd/actions/runs/41226173721), job `124610671636`; successful Fuzzing [41226176399](https://github.com/daviduhden/openi2pd/actions/runs/41226176399); successful Debian, static-musl and Container build jobs in Packaging [41226179732](https://github.com/daviduhden/openi2pd/actions/runs/41226179732). All used SHA `abe09136c84d1a4df487306dad6a7eb8e2034806`.
- **Diagnostics:** `-Wunused-parameter` in `TorrentsRPC.cpp:1116,1200,1218`, `TorrentsTunnel.cpp:2913,2915,2950`; OpenBSD also reports `BoostStream.h:116` (`role`).
- **Cause/search:** Asio/Beast completion signatures require these parameter types, but the handler bodies do not consume these values. The tracker write count at `TorrentsTunnel.cpp:2828` is consumed by a downstream completion and must remain named; do not remove it while fixing adjacent callbacks.
- **Correction:** Omit only unused parameter names while preserving callback types, captures, shared ownership and the one forwarded byte count.
- **Status:** Corrected by static analysis in the current worktree; pending CI compilation on Linux, OpenBSD, Windows, fuzz and package lanes.

### CW-007 — CMake warns that CMP0167 is unset

- **Evidence:** Debian build job `124610688820` in Packaging [41226179732](https://github.com/daviduhden/openi2pd/actions/runs/41226179732), SHA `abe09136c84d1a4df487306dad6a7eb8e2034806`; configure log at `build/CMakeLists.txt:255`.
- **Diagnostic:** The Debian runner's CMake emits a developer warning because policy CMP0167 is unset around `find_package(Boost)`; CMP0167 removes the legacy `FindBoost` module in favor of Boost's config package.
- **Cause:** The project minimum is CMake 3.24, predating the policy, while current Debian CI uses a newer CMake.
- **Correction:** Set CMP0167 to NEW only when the policy exists. Old supported CMake versions retain their bundled module; newer versions use Boost's maintained config package.
- **Status:** Corrected by static analysis; pending CMake configure validation across Debian, OpenBSD, Windows and Android dependency environments.

### CW-008 — Unused Win32 console callback argument

- **Evidence:** Windows [41226168239](https://github.com/daviduhden/openi2pd/actions/runs/41226168239), MSYS2 job `124610655800`, SHA `abe09136c84d1a4df487306dad6a7eb8e2034806`.
- **Diagnostic:** Clang `-Wunused-parameter` at `tui/main.cpp:68` for the `DWORD type` argument to `HandleConsoleEvent`.
- **Cause/search:** Win32 requires the callback's `DWORD` signature, but the handler intentionally treats every console event identically and only sets the stop flag. Other Win32 service callbacks consume their event codes in dispatch.
- **Correction:** Keep the ABI-required parameter type and omit only its unused name.
- **Status:** Corrected by static analysis; pending Windows CI compilation and TUI validation.

### CW-009 — POSIX-only address-book test helpers compiled on Windows

- **Evidence:** Windows [41226168239](https://github.com/daviduhden/openi2pd/actions/runs/41226168239), MSYS2 job `124610655800`, SHA `abe09136c84d1a4df487306dad6a7eb8e2034806`.
- **Historical recurrence:** MSYS2 runs [40532395786](https://github.com/daviduhden/openi2pd/actions/runs/40532395786) and [40594544850](https://github.com/daviduhden/openi2pd/actions/runs/40594544850) reported the same unused `VICTIM_CONTENT`/`WriteFile` definitions in earlier revisions. The current guard correction addresses that repeated platform-conditional pattern.
- **Diagnostics:** Clang reports `VICTIM_CONTENT` as an unused constant and `WriteFile` as an unused function in `tests/test-addressbook-persistence.cpp:35,44`.
- **Cause/search:** These helpers only support POSIX symlink checks but were outside the existing `_WIN32` guard, so the Windows test translation unit compiled unused definitions. No equivalent fixture helper outside this test was found.
- **Correction:** Place both helpers under the same non-Windows guard as their symlink-only consumers; shared persistence coverage remains enabled on Windows.
- **Status:** Corrected by static analysis; pending Windows and POSIX CTest validation.

### CW-005 — Toolchain and runner messages

The retained logs also contain MSYS2 package reinstall notices, `libtool
--finish`, archive creation notices, Docker Buildx output-cache advice,
OpenBSD SSH host-key notices, Debian `update-alternatives` and
`dpkg-shlibdeps` warnings, and GitHub Actions Node runtime notices. These are
not compiler warnings from project sources. Keep them classified by their
origin; update pinned actions or runner configuration only when a supported
replacement is available and the message is actionable.

### Static first-party string API sweep (2026-10-31)

The source scan for `strcat`, `strcpy`, `sprintf` and `strncpy` found no direct
first-party C/C++ calls to the first three. The only match was
`std::strncpy` in `tests/test-privsep-linux-sandbox.cpp:368`, which copies a
short fixed test socket path into a value-initialized `sockaddr_un` buffer with
an explicit `sizeof - 1` bound. No corresponding CI warning was observed, and
the path fits supported POSIX `sun_path` sizes; no behavior change is justified.

## Historical project warnings and disposition

| Pattern | Historical evidence | Current source disposition |
|---|---|---|
| Deprecated free `atomic_load/store/exchange(shared_ptr)` | Repeated GCC/libstdc++ warnings across Linux, Windows and OpenBSD runs `39207577253` through `39222156302`, on older SHAs including `4dfba879` and `0cf32519`. | No free shared-pointer atomic calls remain in the current tree; ownership now uses the project `AtomicSharedPtr` wrapper/specialization. Historical warning resolved by later modernization. |
| `uint8_t` compared against 256 | Clang `-Wtautological-constant-out-of-range-compare` in `libi2pd/Reseed.cpp:344`, runs `392075...` through `405945...`. | Current parser no longer uses that comparison. Historical warning resolved; do not apply the obsolete line-number fix. |
| OpenSSL EC low-level APIs in `famtool` | GCC deprecation warnings in runs `39211573349` through `39222148542`. | Modern OpenSSL feature paths use EVP APIs; low-level EC access remains in compatibility fallbacks. No current warning evidence on the latest SHA; retain fallback until OpenBSD/LibreSSL coverage proves replacement parity. |
| OpenSSL X.509 subject accessor | `X509_NAME_get_text_by_NID` warnings in runs `40411690766`, `40411695544` and `40411709057`, SHA `88ac1e9297e2`. | Replaced by the capability-aware `GetX509NameTextByNID` helper; historical warning resolved. |
| `DaemonInitPhase::DataStorage` omitted from test switch | `-Wswitch` in Linux and Windows runs `40504357198`, `40504365634`, `40532383635`, `40532395786` and `40594540028`, older source. | The current enum added `DataStorage` after those diagnostics; the regression switch now explicitly rolls back the phase and tests its LIFO position. Pending CI confirmation. |
| Unsigned Win32 countdown compared with zero | Successful MinGW CMake jobs inside run `39057882152`, job `117469523419`, SHA `2146ee127fbd59ab116bfcff84d44ab16db1a53e`; warning at old `Win32App.cpp:396`. | The same ineffective comparison remained in current code. Replaced with bounded modulo tick arithmetic and a portable regression for expiry and counter wrap. Pending Windows CI. |

Older logs also report unused helpers/captures and an unused HTTP reverse-match
helper. Keep these historical until each exact-SHA source comparison is
complete; do not count repeated inclusions as separate warnings. The newest
Linux/OpenBSD/MSYS2 builds stop at a shared test namespace error; their earlier
compiler output and successful sibling Fuzzing/container/static-musl builds
still expose the warning families above. No partial or failed job establishes
that skipped downstream targets are warning-free.

| Direct Asio address formatting (`strcat` linker warning) | OpenBSD run `41229377388`, `libi2pdclient.a(SOCKS.cpp)` referencing `boost::asio::detail::socket_ops::inet_ntop`; historical warning in `SSU2.cpp` and `SOCKS.cpp`. | `LogPrint` excludes Asio endpoint types and SOCKS uses `i2p::util::net::FormatAddress`; direct `to_string()` calls must be searched globally. Pending OpenBSD CI. |
| Windows core-side typed Error send | MSVC run `41229371203`, `test-privsep-windows-ipc` direction assertion. | `WindowsChannel::Send` already permitted `Error` from both directions; the frame-bound test was corrected to send requests from the core. Pending Windows CI. |

## Warning-policy status

`i2pd_warnings` currently uses GCC/Clang `-Wall -Wextra -Wpedantic` and MSVC
`/W4 /permissive-`. It previously disabled `-Wunused-parameter` and
`-Wuninitialized` across all non-MSVC targets; those broad suppressions have
been removed so newly exposed diagnostics can be addressed at their sources.
Format-security remains an explicit error under the existing hardening flags.
There is no general `-Werror` or MSVC `/WX` policy yet: recent CI contains
unresolved project and external warnings, so a global warnings-as-errors gate
would currently obscure classification by turning every warning into an
untriaged build failure. Reassess after a full matrix reports zero owned
warnings, then enable strict enforcement without applying it to third-party
headers.

## Third/fourth batch current warnings (`00929cd1`)

- **SOCKS Asio formatter:** OpenBSD run 41229377388 reported `strcat()` from
  `libi2pdclient.a(SOCKS.cpp)`'s Boost.Asio `inet_ntop`; two direct
  `address_v4/address_v6::to_string()` calls now use
  `i2p::util::net::FormatAddress`. Pending OpenBSD CI.
- **MSVC D9025 (`/DNDEBUG` overridden by `/UNDEBUG`):** intentional for the
  assert-based Windows test lane; not a source defect. The test target keeps
  assertions and documents the override.
- **Lintian `no-manual-page` for native tools:** observed in Debian run
  41229383879 for the fifteen tools. These are real documentation gaps, not
  suppressed: track them as packaging documentation debt and add man pages or
  an explicit release decision before declaring the packaging lane
  warning-free.
- **Static package changelog warnings:** `wrong-name-for-upstream-changelog`
  and `syntax-error-in-debian-changelog` come from shipping the repository
  `ChangeLog` in the standalone package. The static-package override now
  records this intentional upstream-changelog choice.
- **`shared-library-lacks-prerequisites` on the static binary:** the workflow
  verifies `statically linked`; the static-package override now records the
  self-contained-binary classification. Revisit if the artifact inspection
  shows a real missing dependency.
- **Autopkgtest `--force-yes` deprecation:** external tool output from the
  released autopkgtest, not a project source warning. Track the runner/tool
  upgrade rather than changing package behavior.

No current compiler warning is claimed fixed by CI until a new manual dispatch
reports it absent. Lintian and autopkgtest messages are kept in the packaging
category.

### CE-053 — Remaining MSVC narrowing/macro warnings

- **Run/job:** Windows 41229371203, MSVC job 124620132332.
- **Diagnostics:** `std::tuple<uint32_t,uint32_t,uint32_t>` initialized from
  `size_t` in `Torrents.cpp:3082`; `uint64_t` to `uint8_t` for
  `SetFilePriority`; `int64_t` to `long` in `tui/main.cpp`; `wchar_t` to
  `char` in `test-privsep-windows-ipc.cpp`; `WIN32_LEAN_AND_MEAN`/`NOMINMAX`
  redefinition in `test-tui-win32-pipe.cpp`.
- **Correction:** explicit bounded casts at the wire/test sites, `NarrowHex`
  for ASCII nonce hex, `long` for `time_t`, and macro guards.
- **State:** corrected by static analysis; Pending Windows CI.

## Batch `63c42ca4` (2026-10-31)

- **MSYS2 (`41232676139`):** the build completed and its log contains no
  warning from first-party C++ sources. That covers the GCC/Clang warning
  classes corrected in the `00929cd1` batch (tuple/narrowing conversions,
  macro redefinition, Asio formatter) on that lane.
- **Linux (`41232673150`):** every job stopped at CE-055, so the lanes after
  the failing test target were not compiled; a green warning summary cannot be
  claimed for them.
- **OpenBSD (`41232678782`):** stopped at the same error.
- No new warning class was observed in the inspected logs; this is a
  "nothing new observed" statement, not a warning-free proof.

## Batch `dc5540ae` (2026-11-01)

- `test-privsep-worker-identity.cpp` produced `-Wunused-parameter` (`argv0`,
  Linux) and `-Wunused-variable` (`buffer`, OpenBSD) because the platform
  branch that uses each was not the only place it existed. Both are fixed in
  CE-066; no suppression was added.
- The MSYS2 build log contains no first-party warning. Linux/OpenBSD did not
  reach the compiler for later targets (CE-066), so their warning summaries
  are not evidence.

## Batch `4e776a39` (2026-11-01)

No first-party compiler warning was observed: the Windows MSYS2 build is
clean and the Linux/OpenBSD/sanitizer lanes stopped at CE-069 before the
compiler could report later targets. This is not a warning-free proof for
those lanes.

## Preventive review of the uncommitted 2026-11-01 tree

No new first-party warning is known, but the audit found warning-adjacent
risks that a stricter lane would report: a missing direct `<chrono>` include
in `SSU2.cpp` (fixed), missing `<functional>` in `daemon/UPnP.cpp` and
`<sys/time.h>` in `tui/TuiClient.cpp` (fixed), a `-Wshadow` lambda parameter
in `Torrents.cpp::ParseTrackerResponse`, and `EAGAIN`/`EWOULDBLOCK` in
`daemon/TuiServer.cpp` (now reduced to `EAGAIN`; identical on Linux, only
`-Wlogical-op`). No suppression or
cast was added. The Windows `-W4` lane and the GCC/Clang `-Wall -Wextra
-Wpedantic` contract remain the evidence; nothing in this entry is compiled.

## Batch `4f76f65f` (2026-11-02)

### CW-010 — Android compiled out the only use of `isService`

- **Run/job:** Linux and Android `41359794983`, `android` job, before the
  CE-070 stop: `libi2pd/FS.cpp:124: warning: unused parameter 'isService'`.
- **Cause:** `DetectDataDir` guards the entire service branch with
  `#if !defined(ANDROID)`, so the parameter has no use in an Android build.
  The shared signature is required, and no suppression is appropriate.
- **Correction:** mark the parameter `[[maybe_unused]]` with the platform
  reason. The non-Android path still uses it.
- **Files:** `libi2pd/FS.cpp`.
- **State:** Corrected by static analysis; **Pending CI compilation**. This
  is a warning only: the tree has no `-Werror` (only
  `-Werror=format-security`), so it did not stop the build. A compiler
  stopped by CE-070 reported no later warnings, so Android targets after
  `FS.cpp` remain unverified.

### CW-011 — CMake package-consumer policy warnings

- **Evidence:** successful Linux workflow [41364861389](https://github.com/daviduhden/openi2pd/actions/runs/41364861389), install/consumer job; same policy warning also appears in Linux/OpenSSL 3/4 runs `41364855256` and `41364858202`, SHA `56f17c0f47a3d1a328be6c54df7b661c480924f8`.
- **Diagnostic:** project configure reports CMP0144 unset at `build/CMakeLists.txt:262`; the installed-package consumer reports CMP0144 and CMP0167 from `i2pdConfig.cmake`'s Boost dependency lookup.
- **Cause:** the project and exported dependency config do not select the relevant CMake policy behavior. The consumer still configures and builds successfully, so this is a developer warning, not a compiler failure.
- **Correction:** set CMP0144 when the project CMake supports it and scope CMP0144/CMP0167 selection to the installed package-config include so Boost discovery matches the project without changing the caller's surrounding policy state. The minimum CMake version remains 3.24.
- **State:** corrected by static analysis; the build-system static contract pins both policy guards. **Pending CI configure and installed-consumer validation** on the current tree.

### CW-012 — Android DSL option deprecation

- **Evidence:** Android [41364864089](https://github.com/daviduhden/openi2pd/actions/runs/41364864089), job `125022760915`, SHA `56f17c0f47a3d1a328be6c54df7b661c480924f8`.
- **Diagnostic:** `android.builtInKotlin=false` is deprecated under AGP 9.4.1. Javac failed later with CE-072; no Lint result exists.
- **Cause/state:** Gradle DSL deprecation, not a Java compiler warning. This option is pinned by two toolchain probes; verify behavior before removing it and update both probes together.

## Batch `56f17c0f` (2026-11-02)

GitHub API pagination returned 247 accessible runs across the retained
workflow history (160 failures, 60 cancellations, 27 successes), from
2026-10-17 through 2026-11-02. This is the accessible run count, not a claim
that every historical job log remains available. Current successful sibling
logs were inspected alongside failed jobs; local changes after the published
SHA remain unvalidated.

### CW-013 — MSVC deprecates `getenv` in shared test logging support

- **Evidence:** Windows MSVC run [41364870388](https://github.com/daviduhden/openi2pd/actions/runs/41364870388), job `125022780835`, SHA `56f17c0f47a3d1a328be6c54df7b661c480924f8`; the successful MSVC sibling [41325980409](https://github.com/daviduhden/openi2pd/actions/runs/41325980409), job `124905881821`, SHA `4e776a39ac8498faceda52d1337e16324ae535f4`, also reported it.
- **Diagnostic:** MSVC C4996 at `tests/log-support.cpp:80` for `std::getenv`, repeated while compiling each test executable that links this shared source.
- **Cause/search:** the test-only environment setting was read through the portable CRT spelling that MSVC marks deprecated. This helper is common to the C++ test targets, so one source warning fans out across the target matrix; no product call site is implicated.
- **Correction:** use `_dupenv_s` with `unique_ptr`/`free` ownership on Windows and retain `std::getenv` on POSIX. The selected value and default behavior are unchanged.
- **State:** Implemented / static; **Pending CI compilation** on MSVC. No local build or test was run.

### CW-014 — MSVC hardening option reports an unsupported implementation

- **Evidence:** Windows MSVC run [41364870388](https://github.com/daviduhden/openi2pd/actions/runs/41364870388), job `125022780835`, SHA `56f17c0f47a3d1a328be6c54df7b661c480924f8`.
- **Diagnostic:** CMake warns that `WITH_HARDENING` is not implemented for MSVC and asks for explicit MSVC linker flags; the workflow configures this option as ON.
- **Cause:** the Unix hardening flag set is not compatible with MSVC, and the Windows lane requested the option without an equivalent policy. This was a first-party build/security configuration warning, not a compiler diagnostic.
- **Correction:** the MSVC branch now adds `/GS`, `/sdl` and `/guard:cf` at compile time, `/guard:cf /DYNAMICBASE /NXCOMPAT` at link time, and `/HIGHENTROPYVA` for 64-bit targets. The compile-free build-system contract pins all three groups.
- **State:** Implemented / static; **Pending CI compilation** with MSVC 19.35+ and the Windows target matrix. This does not claim binary mitigation verification.

### CW-015 — Successful packaging logs still contain non-compiler warnings

- **Evidence:** successful Packaging [41364876230](https://github.com/daviduhden/openi2pd/actions/runs/41364876230) and Linux [41364861389](https://github.com/daviduhden/openi2pd/actions/runs/41364861389), SHA `56f17c0f47a3d1a328be6c54df7b661c480924f8`.
- **Diagnostics:** Lintian warns that installed tools lack manual pages and the upstream changelog filename is `ChangeLog.gz`; Debian DEP-8 emits deprecated `apt --force-yes`. GitHub action runtimes report Node 20 and transitive `punycode`/`url.parse` deprecations. The Linux sandbox reports Landlock ABI 8 where full network denial requires ABI 10.
- **Classification:** package-quality, test-command, action-runtime and runner-capability messages; none is a first-party compiler warning. Lintian findings remain package-quality follow-up; the Landlock result means that runner did not validate the full deny-network policy.
- **State:** external/tooling findings remain classified; package and sandbox coverage limits must remain visible in CI reports.

Successful Linux/packaging jobs also contain Node.js 20 action-runtime notices
and Node `punycode`/`url.parse()` deprecations from action tooling. They are
third-party/runtime notices, not first-party C++ warnings. Do not count them as
compiler-clean evidence or suppress them in project compiler flags.
