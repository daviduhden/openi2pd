# Compilation and lint warning inventory

## Revision 176aec32 warning review

Full latest-ten logs, including successful sibling steps, reveal no project
compiler warning in reviewed output. Android run 42690756912 succeeds:
127 WrongCommentType findings are absent; twelve VectorPath and two
ChromeOsAbiSupport Lint findings remain, plus builtInKotlin option
deprecation. These are distinct from native compiler cleanliness. Linux
Landlock ABI 8 messages describe coverage limits. MSYS package setup asks
that other MSYS processes terminate; it is not a source warning. Optional
Perl-quality and Android API 29 device jobs are skipped, not clean.

This file records warning diagnostics from GitHub Actions separately from
compile errors. Repeated header inclusions and template instantiations are
grouped by cause, not counted as independent defects. A successful build can
still contain warnings, and a job stopped by an earlier failure says nothing
about later targets.

## History coverage

The 2026-11-05 snapshot adds eight runs: **263 runs / 52 SHAs / 16 workflow
names**, with 171 failures, 62 cancellations and 30 successes. The eight full
new logs supplement the previous 252 complete logs, one partial and two
unavailable. Successful Android, Linux and packaging logs were reviewed too.
Exact new jobs and SHAs are in [the error inventory](COMPILATION-ERRORS.md).
Raw evidence is under `/tmp/openi2pd-audit-20261105`; larger Windows and
package artifacts are under `/var/tmp/openi2pd-ci-20261105` after a temporary
download hit the `/tmp` quota. No artifact is installed, executed or checked in.

### CW-020 — MSVC captures and cleanup locals shadow existing names

- **Observed:** Windows [42056732400](https://github.com/daviduhden/openi2pd/actions/runs/42056732400),
  MSVC `127347034968`, SHA `01960093aeff480d24ed8507878355a643da168f`:
  C4458 at `Destination.cpp:1087` (`weak` hides a closure member) and C4457 at
  `ECIESX25519AEADRatchetSession.cpp:1059` (`receive` hides a parameter).
- **Correction:** rename the nested verification owner to `verificationWeak`
  and the cleanup lock to `expiredReceive`. Preserve all captures, lock
  boundaries and retention of receive owners until after mutex release.
- **Global review:** inspected sibling nested callbacks and ratchet cleanup
  scopes; separate methods/branches are legitimate scopes. Implemented /
  static; Pending MSVC CI compilation. No cast or suppression was added.

### CW-021 — Successful Android configure emitted wrapper warnings

- **Observed:** Android [42056748811](https://github.com/daviduhden/openi2pd/actions/runs/42056748811),
  `127347095759`, same SHA: no direct `project()` and minimum-version ordering
  warnings, plus five unused legacy Boost include/library cache hints.
- **Correction:** declare `project(OpenI2PdAndroid LANGUAGES C CXX)` immediately
  after the wrapper's minimum version. Remove unused hints from both arm64
  and x86_64 workflow environments; retain prefix/root config discovery.
  Reviewed other CMake entrypoints and kept genuine subdirectories unchanged.
- **Regression/state:** the Java-25 configuration source guard pins the
  project declaration. Implemented / static; Pending Android CI compilation.
  The old successful APK/Javac/dex/lint result does not validate this change,
  and its optional API 29 device lane was skipped.

### CW-022 — Successful Debian package lacked native-tool manuals

- **Observed:** Packaging [42056737144](https://github.com/daviduhden/openi2pd/actions/runs/42056737144),
  Debian `127347051009`, same SHA: fifteen `no-manual-page` warnings and
  `wrong-name-for-upstream-changelog` for `ChangeLog.gz`.
- **Artifact:** exact uploaded `.deb` archive has only router/i2pdctl pages
  and uppercase `ChangeLog.gz`, alongside `changelog.Debian.gz`. Missing
  content and the install graph are the cause, not a guessed helper failure.
- **Correction/global review:** add a native-tool reference and fifteen
  command aliases, install together in Tools, and align Debian and both RPM
  manifests. Install source ChangeLog as canonical lowercase `changelog`;
  keep original source/provenance and adapt package smoke expectations.
- **Regressions/state:** component install checks now require every command
  alias plus its shared target; Debian source contracts and smoke cover the
  payload. Implemented / static; Pending CI tests and package validation.
  External debugedit DWARF, autopkgtest `--force-yes` and action-runtime
  deprecations remain distinct unresolved dependency diagnostics (CW-015).

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

## 2026-11-07 exact-run warning follow-up

All runs below use `14f224e6c7776619c13449759523a87298d4f63a`;
see the compilation inventory for exact job IDs and links.

- **CW-023:** Windows 42402984632 reports `NOMINMAX` redefinition in
  `tests/test-log-lifecycle.cpp`. Added an `#ifndef` guard; inspected the
  other 23 macro definitions for equivalent unguarded definitions.
  Implemented / static; Pending CI compilation.
- **CW-024:** OpenBSD 42402987408 reports a copied range-loop string in
  `tests/test-privsep-openbsd-policy.cpp`. Use a const reference without
  changing test semantics. Implemented / static; Pending CI compilation.
- **CW-003 follow-up:** Android 42402998875 has 19 Lint findings, including
  two `UseCompatTextViewDrawableXml` diagnostics in `fragment_logs.xml`.
  Converted the view to AppCompat with compat drawable attributes. The other
  17 findings (vector paths, ChromeOS ABI, nested weights and overdraw) remain
  open; no suppressions or unsupported ABI claims were introduced. Device
  validation was skipped. Pending CI tests.
- **CW-021 follow-up:** the previous Gradle wrapper warning is absent from
  the successful Android job; this is limited to that executed revision.
- **CW-022 follow-up:** packaging 42402990302 succeeds. Its exact uploaded
  Debian archive includes all 15 tool aliases, the shared manual and canonical
  changelog; uploaded `lintian.log` is empty. This validates that package lane,
  not unexecuted package variants. Sixteen debugedit unknown-DWARF diagnostics
  remain external (debugedit 1:5.0-5build2 / GCC 13.3).
- External action Node-runtime/deprecation notices, autopkgtest force-yes
  debt and Landlock ABI 8 versus runner ABI 10 coverage remain separately
  identified; a successful job does not close them.

## Follow-up on published `68c16e10`

Exact runs/jobs are tabulated in COMPILATION-ERRORS.md. CW-023 NOMINMAX and
CW-024 loop-copy diagnostics are absent from the reviewed new platform logs;
their earlier warnings remain historical evidence. Android 42414230020 has
zero errors and 17 Lint warnings: 12 VectorPath, two ChromeOsAbiSupport, one
NestedWeights and two Overdraw. The two compat-drawable warnings are absent.
Do not remove branded geometry, an inset/background owner or add an ABI
without the corresponding rendering/native artifact evidence. API 29 UI was
skipped, so these source suggestions are not device validation.

Packaging 42414221469 again emits 16 external debugedit unknown-DWARF
messages and an autopkgtest `--force-yes` deprecation. Gradle deprecation and
Node DEP0040/DEP0169/runtime notices remain tool/dependency debt. No warning
suppression was added. Concurrent action-version and container-image edits
by another session are preserved; this audit does not attribute or validate
those independent changes.

## 2026-11-08 — Successful siblings and instrumented dependency

The latest batch uses SHA `8ffd1ad8`; exact run/job links are in the error
inventory. OpenBSD and all four OpenSSL branch builds expose no new project
compiler warning in the reviewed logs. Android 42419370092 retains 17 Lint
warnings: 12 VectorPath, two ChromeOS ABI, one NestedWeights and two Overdraw.
The earlier compat drawable warnings remain absent; API 29 UI is skipped.
Packaging 42419362861 retains the 16 external debugedit DWARF diagnostics and
`--force-yes` debt. These are not first-party C++ compiler diagnostics.

### CW-025 — Clang predefined TSan macro redefined by OpenSSL

Instrumented run 42419378836 emits 18 warnings from
`include/internal/threads_common.h:17`: Clang 23 already defines
`__SANITIZE_THREAD__`, while OpenSSL 4.1.0 defines it empty again. The
subsequent successful 42422405960 still contains the warning because the
patch is not in its SHA. Apply only the matching guard hunk from
[OpenSSL f99bbcd5afff69840f2bfed92458cd5ac092cf91](https://github.com/openssl/openssl/commit/f99bbcd5afff69840f2bfed92458cd5ac092cf91)
to the pinned temporary source tree. Preserve the compiler's value and TSan
annotations, record the applied diff/hash, and keep unrelated upstream changes
out. No global installation or warning suppression is modified.
Implemented / static; Pending CI compilation for the patched library.

## 2026-11-08 — Shared MSVC assertion configuration

### CW-026 — Release assertions caused repeated D9025 command diagnostics

Windows run 42426220784, successful MSVC job 128564962837, SHA c115d386,
emits nine `D9025: overriding '/DNDEBUG' with '/UNDEBUG'` warnings. The
common test helper, libFuzzer targets and tools-getopt all used the same
command-macro override. The older classification as intentional records
historical intent; it does not resolve the warning under the clean-build goal.

The shared test assertion helper now force-includes a source header on MSVC
that undefines NDEBUG before including cassert. Other drivers retain -UNDEBUG.
No compiler diagnostic is disabled, and assertion operands must still execute
in Release. New test-release-assertions is registered in common CTest and the
selective MSVC build/expression. No PCH setup exists; introducing one requires
renewed force-include ordering review. Implemented / static; Pending CI
compilation and Pending CI tests for the changed configuration.

The exact OpenBSD, four OpenSSL lanes, sanitizer and focused crypto logs show
no new compiler warning in the reviewed output. CW-025's macro redefinition
is absent with the applied upstream guard. Android still emits 17 Lint
findings on c115d386; a local wide-layout measurement fix targets only one
NestedWeights finding. Twelve VectorPath, two ChromeOS ABI and two Overdraw
findings remain. Removing root paints would change the theme's color roles.
Debian retains 16 external debugedit diagnostics and deprecated --force-yes;
Gradle and runner capability notices remain separately classified debt.

## 2026-11-08 — c3f5df53 successful-build diagnostics

### CW-027 — Signed peer count compared with a size-derived expectation

OpenSSL 3 run [42431822974](https://github.com/daviduhden/openi2pd/actions/runs/42431822974),
GCC job 128582202338, and OpenSSL 4 run
[42431807205](https://github.com/daviduhden/openi2pd/actions/runs/42431807205),
GCC job 128582150723, emit `-Wsign-compare` at
`tests/test-torrents-verify-parity.cpp:679` on c3f5df53. GetNumPeers returns
int while a peer-byte-length quotient is size_t. Use std::cmp_equal instead
of narrowing either operand. Equivalent count/size comparisons and I2CP key
count types were inspected; no additional demonstrated instance required a
change. Existing parity tests retain their registration. Implemented / static;
Pending CI compilation and Pending CI tests. No diagnostic is suppressed.

### CW-028 — Optional Perl quality lane exposes unresolved profile debt

Validation run [42431903303](https://github.com/daviduhden/openi2pd/actions/runs/42431903303),
job 128582436852, SHA c3f5df53 passes 1230 Test::More assertions in seven
files, then Perl::Critic exits 2. The exact Critic step has 910 reported policy
occurrences across 23 families, including 662 extended-format regex findings,
46 complex regexes and 35 uninitialized localized variables. These are Perl
analyzer diagnostics, not C++ compiler errors or failed functional tests.

All 35 slurp scopes in 17 maintained scripts/test helpers now explicitly set
`local $/ = undef`, preserving whole-file reads and dynamic restoration.
Three diagnostic accumulators unpack arguments and explicitly return the
previous push count; two helper paths have explicit returns, and an unused
catalog hash is removed. Equivalent helper patterns were searched globally.
No /x modifier was added mechanically: literal spaces, hashes and captured
text need individual semantic review. No policy, severity or CI lane was
weakened. The remaining analyzer debt is Correction pending; the implemented
subset is Pending CI tests and has not been rescanned with Perl::Critic.

Android's exact latest Lint artifact reports zero errors and 16 warnings:
12 VectorPath, two ChromeOS ABI and two Overdraw. NestedWeights is absent
in this executed revision. This does not validate later local resource edits.
MSVC D9025 is absent in the latest reviewed job, which now executes the
assertion regression; its separate heap diagnostic still fails (RT-057).

### CW-029 — Successful runner tooling reports unavailable Landlock ABI

Successful Linux run [42431921178](https://github.com/daviduhden/openi2pd/actions/runs/42431921178),
consumer job 128582491902, and focused instrumented run
[42431861964](https://github.com/daviduhden/openi2pd/actions/runs/42431861964),
job 128582316585, report a runner/tool Landlock ABI 10 requirement versus
available ABI 8. Validation build jobs, including the successful fuzz lane,
also emit the notice. This is external runner/tool hardening coverage debt,
not a first-party compiler warning or proof of a daemon sandbox failure.
No global runner installation or suppression was changed. A compatible
runner/tool combination remains Pending; passing product tests do not close
this capability notice. Existing package and Android Lint debt stays open.

## 2026-11-08 — Warning review on 0b7f0ab4

Exact run/job metadata is in COMPILATION-ERRORS.md. Successful sibling jobs,
completed steps in canceled jobs and failed-job build prefixes were reviewed.
New actual configurations include GNU 16.2 / Clang 23.1.5 with OpenSSL 3.5.9
and 4.1.0 Release, OpenBSD Clang 22.1.6 / LibreSSL 4.4.0 Release and Android
Clang 21 / OpenSSL 3.6.5 Release. This is not explicit OpenSSL 4.0.x evidence.

CW-028 repeats in Validation 42440586281, job 128608768953: Test::More passes
1270 assertions across seven files, then Critic exits 2 with 896 occurrences
across 20 families. The prior 35 localized-initialization, three argument-
unpacking and unused-variable findings are absent. Extended-format regex
findings now number 690 as new checker code expanded the reviewed surface;
complex-regex and other analyzer debt remain open.

A bounded local continuation manually reviews 29 Javadoc regex operands,
using extended layout without changing captures or literal replacement
spaces. The new eight-assertion Javadoc corpus covers hashes, whitespace,
inline tags, generic/vararg declarations and negative diagnostics. It is wired
into CTest, syntax/Critic inventories and the manual Perl-quality lane.
No local Critic run or clean-analyzer claim is made. Remaining CW-028 debt is
Correction pending; this subset is Implemented / static, Pending CI tests.

ASN1_STRING_length / SSL_set1_host deprecation and missing mlkem.h diagnostics
in retained configure artifacts occur inside negative feature probes. They
are not new first-party translation-unit warnings. Successful Linux consumer
128608760333 still reports CW-029's external Landlock ABI 10 versus host ABI 8
capability gap. No runner policy or warning suppression was changed.

Android Lint/Java/APK steps were skipped after native/triplet failures, and
MSVC was canceled during compilation. Their earlier Lint, startup and heap
coverage debt remains unresolved; no clean result is inferred from absent
later output. The parity translation unit does compile in OpenSSL 3/4 GCC
jobs 128608740834/128608746972 without CW-027's repeated sign comparison.
That warning correction has executed compilation evidence on 0b7f0ab4;
CE-086 still prevents a successful whole build or parity link/test claim.

## 2026-11-08 — Successful-step warning review on `6390fdea`

The ten new full logs include successful siblings and successful build steps
inside failed jobs. No new first-party native compiler diagnostic or
sanitizer-report marker was found in the reviewed output. Expected negative
feature probes are not source warnings; failed CTest jobs are not clean
matrix results. Exact run/job references are in COMPILATION-ERRORS.md.

Android 42492811416, job 128768636472, still reports zero Lint errors and
16 warnings: twelve VectorPath, two ChromeOsAbiSupport and two Overdraw.
Both activity roots painted colorSurface over a theme window background.
The theme now owns colorSurface and phone/tablet roots are transparent;
Android 12 splash retains its distinct brand background. A prepared device
contract checks both night modes and both layout widths. Implemented /
static; Pending CI compilation and Pending CI tests. Existing executed
Lint output remains at sixteen until CI runs the correction.

The twelve long brand paths cannot be eliminated safely by optional
whitespace removal alone; lossless compaction still exceeds the reported
length threshold. Splitting contours can change fill/hole semantics, and
rasterization or redesign is not an accepted warning fix. The two ABI
warnings need a coherent multi-ABI artifact/dependency strategy, not removal
of ABI checks. Those fourteen findings remain Correction pending.

The same Gradle job repeats `android.builtInKotlin=false` deprecation three
times. The opt-out avoids the documented AGP/Kotlin task-creation failure for
Java 25 even without Kotlin sources. Removing it without a compatible
upstream toolchain would regress the build; no suppression was introduced.
The optional Perl quality job skips, so CW-028's unresolved Critic families
have no new validation. The eight-assertion Javadoc regression does execute
in ordinary CTest, which is separate from passing Perl::Critic.

Debian job 128768602097 succeeds and its Lintian log is empty; external
autopkgtest still emits deprecated `--force-yes`. Repository search found
no maintained invocation with that flag. MSYS2's package updater warns to
terminate other MSYS2 programs. Both are tooling/runner messages, not project
compiler warnings. Existing external and capability findings remain open.

## 70f66977 retrospective: blocked warning coverage

Ten additional runs bring the accessible inventory to 342 runs on 61 SHAs.
Successful sibling steps and full logs were inspected, including packaging,
documentation and agent knowledge. No new own-code compiler warning family
was identified in those logs. CE-088/089 prevent later native coverage;
this is not evidence of a warning-free matrix. Packaging run 42512607518
passes five jobs, with empty Lintian output; external autopkgtest
`--force-yes` deprecation remains historical debt. Earlier Android Lint and
optional quality-lane findings remain open.

## 9f2934f8 warning review

The accessible inventory is 352 runs on 62 SHAs: 222 failures, 64 successes
and 66 cancellations. All ten new full logs and successful sibling steps
were retrieved. No fresh own-code native compiler-warning family was found.
Android arm64 job 128929903685 succeeds with 14 Lint warnings: two
ChromeOsAbiSupport and twelve VectorPath findings; previous Overdraw
warnings are absent. Device job 128929904898 skips. External AGP built-in
Kotlin opt-out deprecation and autopkgtest --force-yes debt remain open.
Packaging's five jobs pass and exact-artifact lintian.log is empty. These
facts do not establish a globally clean warning matrix or eliminate skipped
quality/worker capability coverage.

## 80a487d5 successful-job warning review

History now contains 362 retained runs on 63 SHAs (223 failures, 73
successes, 66 cancellations), with fifteen workflow IDs and eighteen names.
All ten latest full logs, failed logs, metadata and artifact inventories
were retrieved. No fresh own-code native compiler-warning family or
sanitizer signature was identified in these logs.

Android job 128952775574 still reports zero Lint errors and fourteen
warnings (two ChromeOsAbiSupport, twelve VectorPath). Device 128952776731
and optional Critic 128952795496 skip. Packaging passes five jobs and its
exact Lintian file is empty; external autopkgtest --force-yes and MSYS2
updater diagnostics persist. No warning suppression or speculative vector
geometry change was introduced.

### CW-030: documentation coverage after opener-only changes (static)

HEAD fb974bbe changes 1,633 documentation openers from /** to /* across
219 files. This preserves executable tokens but removes Doxygen/Javadoc
recognition. Both parameter checkers harvest documentation blocks only;
Doxyfile disables undocumented-member warnings. Their pass can therefore
coexist with lost API coverage. No generator result at this SHA establishes
coverage. The user's existing style change is preserved; resolving the
coverage policy and checking generated API output remain Pending CI tests.

## d63b6b90: CW-030 becomes an observed Android Lint family

Inventory: 372 retained runs, 64 SHAs, fifteen workflow IDs/eighteen names;
229 failures, 77 successes, 66 cancellations. All ten latest full/failed
logs, metadata and artifact inventories were retrieved, including successful
siblings. No fresh own-code native compiler-warning or sanitizer signature
was identified in the executed configurations.

[Android 42667603361](https://github.com/daviduhden/openi2pd/actions/runs/42667603361),
job 129335343363, succeeds but both debug/release Lint XML report 141
warnings: 127 WrongCommentType, twelve VectorPath and two
ChromeOsAbiSupport. The 127 findings are ordinary comments containing
Javadoc links/block tags, across forty Java files. This is Android Lint,
not Checkstyle. Local correction restores only those 127 openers; all
Java prose, license headers and executable statements are preserved.
The source checker now rejects hidden tags on non-private APIs, with
positive legal/private/code-only comment controls. Implemented / static;
Pending CI tests and doclint. The fourteen other warnings remain open.

Source-documentation job 129335384749 passes, but does not establish native
API coverage after opener demotion. CW-030's C++ coverage remains separate
and unresolved; no broad rollback of the existing style change was made.
Android device 129335345378 skips. Packaging passes five jobs; external
updater/autopkgtest debt and optional analyzer coverage retain their scope.
