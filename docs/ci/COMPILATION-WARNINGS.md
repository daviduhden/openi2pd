# Compilation and lint warning inventory

This file records warning diagnostics from GitHub Actions separately from
compile errors. Repeated header inclusions and template instantiations are
grouped by cause, not counted as independent defects. A successful build can
still contain warnings, and a job stopped by an earlier failure says nothing
about later targets.

## History coverage

On 2026-10-31, GitHub Actions retained 201 runs across 11 current and former
workflow names, dated 2026-10-17 through 2026-10-31. Complete logs were
retrieved for 198 runs. Logs for job IDs `39058841124`, `39058430980` and
`39058346321` were no longer available (`log not found`); the remaining three
log retrievals completed without timeout. The artifact API listed 54
unexpired artifacts from 30 runs. Relevant Android, source-documentation,
Windows CTest, Debian and fuzz artifacts were downloaded for inspection under
`/tmp/openi2pd-history/diagnostics/`; no CI artifacts are stored in the
repository.

The current warning comparison is against published `main` SHA
`45f5f739aa47ed87aa7462e9b040c0db601bc667`. Local changes below that SHA have
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

- **Evidence:** OpenBSD run [41222229438](https://github.com/daviduhden/openi2pd/actions/runs/41222229438), job `124599019529`, SHA `45f5f739aa47ed87aa7462e9b040c0db601bc667`.
- **Diagnostic:** `ld` reports `strcat()` in
  `boost::asio::detail::socket_ops::inet_ntop`, instantiated in `SOCKS.cpp.o`.
- **Classification:** external dependency implementation included into a
  project translation unit; the project sources contain no direct `strcat`
  call. Investigate a maintained Boost fix or supported platform adaptation;
  do not attribute the warning to project code or suppress it globally.
- **Status:** External warning; solution pending dependency/toolchain review.

### CW-005 — Toolchain and runner messages

The retained logs also contain MSYS2 package reinstall notices, `libtool
--finish`, archive creation notices, Docker Buildx output-cache advice,
OpenBSD SSH host-key notices, Debian `update-alternatives` and
`dpkg-shlibdeps` warnings, and GitHub Actions Node runtime notices. These are
not compiler warnings from project sources. Keep them classified by their
origin; update pinned actions or runner configuration only when a supported
replacement is available and the message is actionable.

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
complete; do not count repeated inclusions as separate warnings. The latest
failed build stops at a test comment, so it cannot establish that downstream
source and link targets are warning-free.

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
