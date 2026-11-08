<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: compilation error inventory.

Tracks compile/configure/link failures reported by manual GitHub Actions.
-->

# Compilation error inventory

This inventory links manual-CI failures and statically found compile risks to
their first diagnostic, root cause, local correction and validation state.
A local correction is not a CI result: rows stay **Pending CI compilation**
until a manual run on the corrected revision passes.

Compile-free contract failures are recorded separately from compiler
diagnostics. They can expose stale source assumptions, but do not establish a
compile or link failure.

## Runs reviewed

### Published revision reviewed on 2026-11-05

This batch compiled `01960093aeff480d24ed8507878355a643da168f`. The source
fixes below remain local and are not validated by these runs.

| Run | Exact jobs / result |
|---|---|
| [42056740314](https://github.com/daviduhden/openi2pd/actions/runs/42056740314) | OpenSSL 3 GCC `127347064556`, Clang `127347065517`: CE-076 |
| [42056745588](https://github.com/daviduhden/openi2pd/actions/runs/42056745588) | OpenSSL 4 GCC `127347082633`, Clang `127347083394`: CE-076 |
| [42056734723](https://github.com/daviduhden/openi2pd/actions/runs/42056734723) | OpenBSD `127347040995`: CE-076 |
| [42056732400](https://github.com/daviduhden/openi2pd/actions/runs/42056732400) | MinGW `127347034154`: CE-076; MSVC `127347034968`: CW-020 and RT-032 recurrence |
| [42056754181](https://github.com/daviduhden/openi2pd/actions/runs/42056754181) | UBSan `127347117917`, TSan `127347118420`, ASan/UBSan `127347118453`: CE-076; fuzz `127347117313`, docs `127347117881`, static contracts `127347117983`: success; optional Perl quality skipped |
| [42056748811](https://github.com/daviduhden/openi2pd/actions/runs/42056748811) | Android `127347095759`: success with CW-021; API 29 UI skipped |
| [42056751743](https://github.com/daviduhden/openi2pd/actions/runs/42056751743) | Linux consumer `127347106870`, interop `127347107515`: success |
| [42056737144](https://github.com/daviduhden/openi2pd/actions/runs/42056737144) | service `127347050200`, Debian `127347051009`, service matrix `127347051108`, container `127347051182`, musl `127347051190`: success with CW-022 |

### CE-076 — Unqualified EdDSA constants invalidate regression extents

- **Diagnostic:** `test-eddsa.cpp:129/131/177/178` cannot resolve
  `EDDSA25519_PRIVATE_KEY_LENGTH`, `EDDSA25519_SIGNATURE_LENGTH` and
  `EDDSA25519_PUBLIC_KEY_LENGTH`. GCC's undeclared output/expanded/blinded
  arrays are consequences of invalid extents, not separate crypto failures.
- **Platforms:** GCC 16.2.0 / Clang 23.1.5 with OpenSSL 3.5.9 and 4.1.0;
  OpenBSD Clang 22.1.6 / LibreSSL 4.4.0; MinGW and sanitizer compilation.
- **Correction/global review:** qualify all five references with
  `i2p::crypto::`. Equivalent uses in signature-identity-hardening have an
  explicit local namespace import; native implementations are already in the
  owning namespace. Preserve all positive and negative EdDSA assertions.
- **State:** Implemented / static; Pending CI compilation / Pending CI tests.
  Existing test-eddsa is the direct regression. Sanitizer lanes never reach
  CTest, so RT-027/031 remain open. Their artifacts contain missing-image
  manifests rather than new sanitizer execution evidence.

The separately requested Windows diagnostic run
[42061501515](https://github.com/daviduhden/openi2pd/actions/runs/42061501515)
also compiles published `01960093`: MinGW `127364143189` repeats CE-076;
MSVC `127364220253` is cancelled during compilation. The missing diagnostic
binary reported afterward is a consequence of that incomplete build, not
evidence of a wrong install path or a new compiler/linker error.

### CE-077 — Local source checker invented names for unnamed parameters

- **Observed locally, compile-free only:** check-doxygen-params.pl reported
  `i2pd_android.cpp:1243: undocumented parameter(s): jclass`. The JNI class
  parameter is intentionally unnamed after CW-018; actual Doxygen accepts
  this declaration. This is a checker defect, not an Android compiler error.
- **Cause/global correction:** the lexical parser treated a lone typedef as
  its own parameter name. Recognize abstract declarators, qualified types,
  cv/elaborated specifiers and builtin type combinations without changing
  signature arity or named/function-pointer parameter checks. Arbitrary C++
  symbol resolution remains outside a source-only lexical checker.
- **Regressions/state:** positive unnamed/JNI/type fixtures plus negative
  actual named-parameter omissions are prepared in maintenance-checkers.t.
  The source checker now passes locally. Behavioral fixtures remain Pending
  CI tests; no warning suppression or fictional @param was added.

### Published revision reviewed on 2026-11-04

All eight new workflows used `d7e8491ce31f46912103e761c6d5ec2cd7f747cf`.
No result below validates the subsequent local corrections.

| Run | Exact jobs and results |
|---|---|
| [41921781843](https://github.com/daviduhden/openi2pd/actions/runs/41921781843) | Linux consumer `126895842477` failed; interop `126895842955` passed |
| [41921782009](https://github.com/daviduhden/openi2pd/actions/runs/41921782009) | fuzz `126895843940`, ASan/UBSan `126895843996`, UBSan `126895844045`, TSan `126895844122` failed compiling; static `126895844279` failed; source docs `126895843425` passed; optional Perl quality skipped |
| [41921782022](https://github.com/daviduhden/openi2pd/actions/runs/41921782022) | Android `126895843020` failed native compilation; API 29 UI skipped |
| [41921782129](https://github.com/daviduhden/openi2pd/actions/runs/41921782129) | Run cancelled; MinGW `126895844115` failed compiling; MSVC `126895844687` cancelled |
| [41921782154](https://github.com/daviduhden/openi2pd/actions/runs/41921782154) | OpenSSL 3 GCC `126895843898` and Clang `126895844275` failed compiling |
| [41921782167](https://github.com/daviduhden/openi2pd/actions/runs/41921782167) | OpenSSL 4 Clang `126895847156` and GCC `126895847621` failed compiling |
| [41921782301](https://github.com/daviduhden/openi2pd/actions/runs/41921782301) | Run cancelled; service install `126895842902` passed; Debian `126895843308` and service contracts `126895843459` failed; container `126895843200` and musl `126895843550` cancelled |
| [41921782377](https://github.com/daviduhden/openi2pd/actions/runs/41921782377) | OpenBSD `126895844550` failed compiling |

### CE-074 — Callback migration redeclared an existing weak owner

- **Evidence:** native compiler lanes above report `Destination.cpp:1022`,
  redefinition/redeclaration of `weak` in `LeaseSetDestination::Publish()`.
  Debian subprocess exit 2 is a consequence, not a separate package defect.
- **Cause/correction:** the on-drop callback already initializes a weak
  destination handle in the enclosing scope. Reuse it for the confirmation
  timer. Both callbacks keep weak ownership and lock before member access.
- **Global review:** equivalent declarations and captures in Destination,
  I2CP, Streaming, BOB and MatchedDestination. Repeated names in distinct
  methods, branches or lambda scopes are legitimate and were preserved.
  Compiler matrices directly cover declaration scope; lifetime contracts
  remain enabled.
- **State:** Implemented / static; **Pending CI compilation**. Later linking,
  tests, Android Java/JNI and MSVC coverage remain unproven. Downloaded
  sanitizer artifacts contain the exact revision and six missing-image
  records; no sanitizer execution or closure of older findings is implied.

### CE-075 — Service contract followed the pre-extraction source

- **Evidence:** Validation static job and Packaging service contracts above
  failed at `check-services.cmake:552`: missing `share/examples/etc/i2pd`.
- **Cause/correction:** paths moved from the OpenBSD port Makefile to
  `install-examples.sh`. Read the helper separately; check all three Makefile
  delegations (`dirs`, `data`, `script`) and the helper's sample/state/NOTICE
  paths. Joined source text would hide a missing delegation.
- **Equivalent review/regressions:** packaging contracts already follow their
  extracted driver; existing mock Make-driver regressions cover OpenBSD phase
  argv. Updated the service contract without weakening the installation rule.
- **State:** Implemented / static; **Pending CI tests**. Local compile-free
  service/packaging source checks pass; no package was built or executed.

| Run | Workflow | Commit | Result |
|---|---|---|---|
| [40411690766](https://github.com/daviduhden/openi2pd/actions/runs/40411690766) | Linux and Android | `88ac1e92` | failure (several jobs) |
| [40411699490](https://github.com/daviduhden/openi2pd/actions/runs/40411699490) | OpenBSD | `88ac1e92` | failure |
| [40411695544](https://github.com/daviduhden/openi2pd/actions/runs/40411695544) | Windows | `88ac1e92` | failure (MinGW and MSVC jobs) |
| [40411703834](https://github.com/daviduhden/openi2pd/actions/runs/40411703834) | Fuzzing | `88ac1e92` | failure |
| [40411709057](https://github.com/daviduhden/openi2pd/actions/runs/40411709057) | Packaging and service validation | `88ac1e92` | failure |
| [40416816523](https://github.com/daviduhden/openi2pd/actions/runs/40416816523) | Linux and Android | `88ac1e92` | failure |
| [40416819852](https://github.com/daviduhden/openi2pd/actions/runs/40416819852) | Windows | `88ac1e92` | failure |
| [40416823112](https://github.com/daviduhden/openi2pd/actions/runs/40416823112) | OpenBSD | `88ac1e92` | failure |
| [40416827033](https://github.com/daviduhden/openi2pd/actions/runs/40416827033) | Fuzzing | `88ac1e92` | failure |
| [40416830825](https://github.com/daviduhden/openi2pd/actions/runs/40416830825) | Packaging and service validation | `88ac1e92` | failure |
| [40419430023](https://github.com/daviduhden/openi2pd/actions/runs/40419430023) | Linux and Android | `0da3aed0` | failure |
| [40419433360](https://github.com/daviduhden/openi2pd/actions/runs/40419433360) | Windows | `0da3aed0` | failure |
| [40419436959](https://github.com/daviduhden/openi2pd/actions/runs/40419436959) | OpenBSD | `0da3aed0` | failure |
| [40419440596](https://github.com/daviduhden/openi2pd/actions/runs/40419440596) | Fuzzing | `0da3aed0` | failure |
| [40419444535](https://github.com/daviduhden/openi2pd/actions/runs/40419444535) | Packaging and service validation | `0da3aed0` | failure |
| [40504357198](https://github.com/daviduhden/openi2pd/actions/runs/40504357198) | Linux and Android | `cec24388` | failure |
| [40504361287](https://github.com/daviduhden/openi2pd/actions/runs/40504361287) | OpenBSD | `cec24388` | failure |
| [40504365634](https://github.com/daviduhden/openi2pd/actions/runs/40504365634) | Windows | `cec24388` | failure (MSYS2 build and MSVC CTest) |
| [40504369803](https://github.com/daviduhden/openi2pd/actions/runs/40504369803) | Fuzzing | `cec24388` | failure |
| [40504374509](https://github.com/daviduhden/openi2pd/actions/runs/40504374509) | Packaging and service validation | `cec24388` | failure |
| [40532383635](https://github.com/daviduhden/openi2pd/actions/runs/40532383635) | Linux and Android | `b25d0c3a` | failure (Linux compile, Android Java test compile) |
| [40532389857](https://github.com/daviduhden/openi2pd/actions/runs/40532389857) | OpenBSD | `b25d0c3a` | failure (shared test compile error) |
| [40532395786](https://github.com/daviduhden/openi2pd/actions/runs/40532395786) | Windows | `b25d0c3a` | failure (MinGW compile; MSVC CTest) |
| [40532402554](https://github.com/daviduhden/openi2pd/actions/runs/40532402554) | Fuzzing | `b25d0c3a` | success |
| [40532412220](https://github.com/daviduhden/openi2pd/actions/runs/40532412220) | Packaging and service validation | `b25d0c3a` | failure (Debian smoke; static-package Lintian) |
| [40594540028](https://github.com/daviduhden/openi2pd/actions/runs/40594540028) | Linux and Android | `b34b2e79` | failure (shared test compile, component consumer, Android APK path assertion) |
| [40594544850](https://github.com/daviduhden/openi2pd/actions/runs/40594544850) | Windows | `b34b2e79` | failure (shared MinGW compile; two MSVC CTest assertions) |
| [40594549892](https://github.com/daviduhden/openi2pd/actions/runs/40594549892) | OpenBSD | `b34b2e79` | failure (shared test compile) |
| [40594560248](https://github.com/daviduhden/openi2pd/actions/runs/40594560248) | Packaging and service validation | `b34b2e79` | failure (Debian smoke path assertion) |
| [40594555055](https://github.com/daviduhden/openi2pd/actions/runs/40594555055) | Fuzzing | `b34b2e79` | success |
| [41207325363](https://github.com/daviduhden/openi2pd/actions/runs/41207325363) | Linux and Android | `46738ddf` | cancelled after failed Linux, Android and docs jobs |
| [41207324984](https://github.com/daviduhden/openi2pd/actions/runs/41207324984) | Windows | `46738ddf` | failure (shared test compile; MSVC CTest) |
| [41207325119](https://github.com/daviduhden/openi2pd/actions/runs/41207325119) | OpenBSD | `46738ddf` | failure (shared test compile) |
| [41207325136](https://github.com/daviduhden/openi2pd/actions/runs/41207325136) | Fuzzing | `46738ddf` | success |
| [41207325472](https://github.com/daviduhden/openi2pd/actions/runs/41207325472) | Packaging and service validation | `46738ddf` | failure (Debian remove/purge check) |
| [41226171242](https://github.com/daviduhden/openi2pd/actions/runs/41226171242) | Linux and Android | `abe09136` | failure (shared test compile; Android locale gate before Gradle) |
| [41226168239](https://github.com/daviduhden/openi2pd/actions/runs/41226168239) | Windows | `abe09136` | MSYS2 failure (shared test compile); MSVC still running at review time |
| [41226173721](https://github.com/daviduhden/openi2pd/actions/runs/41226173721) | OpenBSD | `abe09136` | failure (shared test compile) |
| [41226176399](https://github.com/daviduhden/openi2pd/actions/runs/41226176399) | Fuzzing | `abe09136` | success (contains compiler warnings) |
| [41226179732](https://github.com/daviduhden/openi2pd/actions/runs/41226179732) | Packaging and service validation | `abe09136` | failure (Debian remove-preservation assertion) |
| [41229374061](https://github.com/daviduhden/openi2pd/actions/runs/41229374061) | Linux and Android | `00929cd1` | failure (shared test compile; Android R8 probe) |
| [41229377388](https://github.com/daviduhden/openi2pd/actions/runs/41229377388) | OpenBSD | `00929cd1` | failure (shared test compile) |
| [41229371203](https://github.com/daviduhden/openi2pd/actions/runs/41229371203) | Windows | `00929cd1` | failure (MinGW compile; MSVC test assertions) |
| [41229380998](https://github.com/daviduhden/openi2pd/actions/runs/41229380998) | Fuzzing | `00929cd1` | success |
| [41229383879](https://github.com/daviduhden/openi2pd/actions/runs/41229383879) | Packaging and service validation | `00929cd1` | failure (Debian remove-preservation assertion) |

The newest five runs used branch `main` at full SHA
`00929cd177a981357b73de172ed7c7406a5e397e`. The previous five used
`abe09136c84d1a4df487306dad6a7eb8e2034806`; the paginated Actions API
returned 206 retained runs across 11 workflow names and 43 SHAs. The Linux,
OpenBSD, MSYS2, Android and Debian job logs and available artifacts were
inspected. In the newest batch Android built the APK but its D8/R8 cache probe
could not find the AGP-resolved R8 jar; that tooling failure is separate from
the shared C++ test compile error.

The `b25d0c3a` batch was dispatched on 2026-10-27 between 15:34:52 and
15:35:02 UTC. Each run above is on branch `main`; the full SHA is
`b25d0c3a31a65e13001629522c10db949cdeda2d`.

Earlier runs (`b9cbdaa8`, `a2b8dc48`, `2ae36284`, `d5d0bd6f`) were cancelled
or failed for older trees; they are historical evidence only and do not
validate the local corrections.

For the latest SHA, artifacts downloaded with `gh run download` were:
`source-api-docs/source-doc-todos.txt` from 40419430023;
`windows-msvc-ctest/ctest-msvc.xml` from 40419433360; and
`static-musl-packages/static-file.txt` plus
`debian-packages-and-logs/{lintian.log,*.deb,*.changes,*.buildinfo}` from
40419444535. Runs 40419436959 (OpenBSD) and 40419440596 (Fuzzing) had no valid
downloadable artifacts. First-failure compile logs were retrieved directly
with `gh run view --log-failed` and job logs; no complete credential-bearing
or secret-bearing logs are stored here.

The 2026-10-27 batch at full SHA
`b34b2e79546c929f1f5f788d4905b41c9a92d45d` ran on `main`. The latest Linux
and Android run's relevant jobs were Linux GCC (`122506989737`), Linux Clang
(`122506989824`), Linux OpenSSL 3.x (`122506989994`), Linux OpenSSL 4.0.0
(`122506989946`), Linux OpenSSL 4.0.3 (`122506989829`), Android
(`122506989902`) and installed-component consumer (`122506989963`). Windows
MSYS2/MinGW and MSVC jobs were `122507016243` and `122507015934`; OpenBSD's
build job was `122507021850`; Debian packaging job was `122507055854`.
Logs and exact-run artifacts were downloaded to `/tmp/openi2pd-ci/`; the
`install-components` artifact showed only `libi2pd_privsep.a` among the
exported static libraries. The Debian `.deb` contains
`assets/branding/colors.md.gz`. The Android report artifact contains test and
Lint results but no APK, so its APK output location was not proven by the
artifact. The full Android arm64 native build, JVM tests, Lint and Gradle APK
assembly succeeded before the workflow's output-path assertion failed.

The 2026-10-31 batch ran on `main` at published SHA
`46738ddf6f0f9b8e01e31d7c28bc6396215f82e8`, identical to local `HEAD` at audit
start. Runs were dispatched at 13:49:47 UTC: Linux and Android 41207325363,
Windows 41207324984, OpenBSD 41207325119, Fuzzing 41207325136 and Packaging
and service validation 41207325472. The Linux/Android run was cancelled at
the user's prior stop request after its failed jobs completed; its API 29 UI
job was cancelled and provides no result. Exact failed job IDs:

| Run | Job ID / matrix | First failing stage |
|---|---|---|
| 41207325363 | `124554970150` Linux GCC; `124554970086` Linux Clang; `124554970159` OpenSSL 3.x; `124554970256` OpenSSL 4.0.0; `124554970131` OpenSSL 4.0.3; `124554970125` ASan/UBSan; `124554970194` UBSan; `124554970200` TSan | `tests/test-http-req.cpp:190`, then `tests/test-http-res.cpp:108` while building test targets |
| 41207325363 | `124554969967` Android arm64, JDK 25 / AGP 9.4.1 | Javadoc task configuration after native build, JVM tests, Lint and APK assembly succeeded |
| 41207325363 | `124554970104` C++ source API documentation | Doxygen parsing `privsep/OpenBSDSandbox.h:286` |
| 41207325363 | `124554970154` Perl quality | `tools/tests/perl-portability.t`, assertions 1-2 |
| 41207324984 | `124554970047` Windows MSYS2/Clang64 | Same HTTP test sources as Linux/OpenBSD |
| 41207324984 | `124554969879` Windows Server 2022/MSVC | Selected targets built; two Windows CTest assertions failed |
| 41207325119 | `124554970347` OpenBSD 8.0 x86_64 Clang/LibreSSL | Same HTTP test sources after 141/490 build steps |
| 41207325472 | `124554971078` Debian packages / Ubuntu | Build, Lintian, install, remove-preserve and DEP-8 passed; final remove/purge step exits 1 without naming the failing command |

The Linux GCC log reached `[60/500]` linking `libi2pd.a`, compiled the
I2PSnark sources and linked `libi2pdclient.a` by `[90/500]`, then began TUI
and tool sources before malformed HTTP tests stopped Ninja. The installed
component consumer (`124554970262`) passed; its exact artifact contains all
four exported archives (`i2pd_privsep.a`, `libi2pd.a`, `libi2pdclient.a`,
`libi2pdlang.a`). OpenSSL 3/4 capability probes passed before the shared test
failure, so these logs show no crypto API or production-link diagnostic at
that point. Targets skipped after the test parse errors remain unvalidated.

Compared diagnostic files at this SHA with `git show` and the local worktree:
the HTTP tests, `OpenBSDSandbox.h`, Android Gradle file, Perl inventory test
and Windows assertion tests had no pre-existing local diff. Corrections below
are local-only and have no post-fix CI result. Other local edits in `SSU2.h`,
`Streaming.{h,cpp}` and `HTTPProxy.cpp` are absent from this published SHA;
these runs do not validate them. Earlier batch 405945* stopped at CE-031
before the HTTP tests, so it neither confirms nor disproves this newly
observed test failure. The current MSVC assertions also differ from
historical CE-035/CE-036.

Artifacts were downloaded with `gh run download` to
`/tmp/openi2pd-ci/{41207325363,41207324984,41207325472}`. The exact Debian
`.deb` control archive was inspected without installing it. OpenBSD had no
downloadable artifact; its exact job log was retrieved with `gh`. No logs
containing credentials are retained in the repository.

## Confirmed causes

### CE-001 — Unqualified `I2PTunnelHTTPHeader` in the proxy namespace

- **Workflows/runs:** Linux and Android 40411690766 (gcc, clang, OpenSSL 3.x,
  OpenSSL 4.0.0/4.0.3, all sanitizers, install-consumer, Android), OpenBSD
  40411699490, Fuzzing 40411703834, Packaging 40411709057 (static, container,
  Debian).
- **First diagnostic:** `libi2pd_client/HTTPProxy.cpp:2509: error:
  'I2PTunnelHTTPHeader' does not name a type`; clang adds
  `did you mean 'client::I2PTunnelHTTPHeader'?`. The later
  `m_ResponseHeader was not declared` messages are consequences.
- **Cause:** `HTTPProxy.cpp` is `i2p::proxy`; the type is
  `i2p::client::I2PTunnelHTTPHeader` (`I2PTunnelHTTP.h`). Five uses were
  unqualified.
- **Correction:** qualify the member declaration and the four
  `Result` comparisons with `i2p::client::`.
- **Files:** `libi2pd_client/HTTPProxy.cpp`.
- **State:** Corregido mediante análisis estático; **Pending CI compilation**.

### CE-002 — Non-existent `ip::address::to_string(error_code&)`

- **Workflows/runs:** Linux and Android 40411690766 (gcc, clang, OpenSSL
  4.0.0/4.0.3, sanitizers, install-consumer, Android), OpenBSD 40411699490,
  Fuzzing 40411703834.
- **First diagnostic:** `libi2pd_client/ClientContext.cpp:300: error: no
  matching function for call to
  'boost::asio::ip::address::to_string(boost::system::error_code&)'`; clang:
  `too many arguments to function call, expected 0, have 1`.
- **Cause:** the error-code overload never existed for `ip::address` in the
  supported Boost range. The call was also outside a catch block.
- **Correction:** format through
  `i2p::util::net::FormatAddress(endpoint.address())`, which returns an empty
  string on failure and avoids Asio's unsafe POSIX scope-ID formatter; append
  the port as before.
- **Files:** `libi2pd_client/ClientContext.cpp` (`util.h` already includes
  `AddressFormat.h`).
- **State:** Corregido mediante análisis estático; **Pending CI compilation**.

### CE-003 — Doxygen cannot create the ignored output directory

- **Workflow/run:** C++ source API documentation job in 40411690766.
- **First diagnostic:** `error: tag OUTPUT_DIRECTORY: Output directory
  'docs/generated/source-api' does not exist and cannot be created (warning
  treated as error, aborting now)`.
- **Cause:** `docs/generated` is gitignored and absent in a fresh checkout;
  the workflow invoked Doxygen without creating the configured path.
- **Correction:** create `docs/generated/source-api` in the source-docs step
  before `doxygen docs/Doxyfile.source`; pin the step in
  `check-build-system.cmake`.
- **Files:** `.github/workflows/build.yml`, `tests/check-build-system.cmake`,
  `docs/source-documentation.md`.
- **State:** Corregido mediante análisis estático; **Pending CI**.

### CE-004 — Static OpenSSL archive order in the linked probe

- **Workflow/run:** Packaging 40411709057, Alpine static-musl job.
- **First diagnostic:** `-- Performing Test I2PD_OPENSSL_LINK_OK - Failed`
  followed by `CMake Error at CMakeLists.txt:500 ... The selected OpenSSL
  headers and libraries do not provide one linked modern API surface`. The
  individual compile probes all succeeded.
- **Cause:** the probe passed `libcrypto.a` before `libssl.a`. A single-pass
  static link resolves `libssl`'s references into `libcrypto` only when the
  dependent archive comes first; shared builds tolerated the old order, so
  only the musl lane failed.
- **Correction:** order `OPENSSL_SSL_LIBRARY` before `OPENSSL_CRYPTO_LIBRARY`
  in both probe stages, restore `CMAKE_REQUIRED_*` and the original
  `CMAKE_TRY_COMPILE_TARGET_TYPE` correctly, and pin the order in
  `check-build-system.cmake`.
- **Files:** `build/CMakeLists.txt`,
  `tests/check-build-system.cmake`.
- **State:** Corregido mediante análisis estático; **Pending CI
  compilation**. The `-static` link closure is still not exercised by the
  probe; the final daemon `-static` link remains a separate CI check.

### CE-005 — ShellCheck SC2317 on an unused LSB shim

- **Workflow/run:** Packaging 40411709057, service-contracts job.
- **First diagnostic:** `contrib/sysvinit/i2pd line 84: ... SC2317 (info):
  Command appears to be unreachable`; newer ShellCheck releases report the
  same shim as SC2329 (never invoked). ShellCheck exits non-zero and the job
  stops after the CMake service/build-system checks pass.
- **Cause:** `log_success_msg()` is defined as an `lsb-base` compatibility
  shim but this script never calls it.
- **Correction:** keep the shim and add a documented
  `# shellcheck disable=SC2317,SC2329` directive; pin the shim and directive
  in `check-services.cmake`.
- **Files:** `contrib/sysvinit/i2pd`, `tests/check-services.cmake`.
- **State:** Corregido mediante análisis estático; **Pending CI**.

### CE-006 — Windows MinGW job: same two native roots

- **Workflow/run:** Windows 40411695544, `build` job (MSYS2/Clang64).
- **First diagnostic:** the same
  `'I2PTunnelHTTPHeader'` namespace error and
  `address::to_string(error_code&)` call as CE-001/CE-002, in
  `libi2pd_client`.
- **Correction:** CE-001/CE-002.
- **State:** Corregido mediante análisis estático; **Pending CI
  compilation**.

### CE-007 — MSVC: `pid_t` in `privsep/Worker.h`

- **Workflow/run:** Windows 40411695544, `build-msvc` job.
- **First diagnostic:** `privsep/Worker.h(218,29): error C2061: syntax error:
  identifier 'pid_t'`, followed by `Pid`/`m_Pid` cascade in
  `i2pd_privsep.vcxproj`.
- **Cause:** the public header is included by the Windows daemon, but `pid_t`
  is POSIX-only. The `ChildProcess` class is not used on Windows, yet its
  declarations must still parse.
- **Correction:** guard `<sys/types.h>` and define `worker_pid_t` as `int` on
  Windows, `pid_t` elsewhere; use the alias in the `ChildProcess`
  declarations. POSIX `Worker.cpp` keeps the equivalent `pid_t` definitions.
- **Files:** `privsep/Worker.h`.
- **State:** Corregido mediante análisis estático; **Pending CI
  compilation**.

### CE-008 — MSVC: `basic_waitable_timer::cancel(error_code&)`

- **Workflow/run:** Windows 40411695544, `build-msvc` job.
- **First diagnostic:** `I2CP.cpp(1138,15): error C2660:
  'boost::asio::basic_waitable_timer<...>::cancel': function does not take 1
  argument`, repeated at `I2CP.cpp:1147` and `SAM.cpp:162`.
- **Cause:** the observed Boost headers expose the non-deprecated
  `cancel()`/`cancel_one()` API without the old `error_code` overload.
  Boost 1.74 documented that overload as deprecated; newer releases remove
  it. Earlier stopped targets do not establish successful instantiation.
- **Correction:** call `cancel()` directly in `I2CP::ArmReadTimeout`,
  `I2CP::CancelReadTimeout` and `SAM::CancelControlDeadline`; the commented
  contract (safe without a pending wait, aborted handler still runs) is
  unchanged.
- **Files:** `libi2pd_client/I2CP.cpp`, `libi2pd_client/SAM.cpp`.
- **State:** Corregido mediante análisis estático; **Pending CI
  compilation**.

## Not claimed

No compiler, CMake, CTest, Gradle, Doxygen or package command was run
locally. CI logs are evidence for `88ac1e92`; the local corrections are
static-analysis results and remain pending a new manual dispatch.

## Warning cleanup (BUILD-022)

The same runs exposed warnings after the first errors were removed. Corrected
locally:

| Class | First examples | Correction |
|---|---|---|
| `/*` inside block comment | `I2PTunnelAccessFilter.h`, `Daemon.cpp` tunnel glob | reworded prose; no nested opener |
| unused capture/helper/field | `SSU2Session.cpp` relay-tag lambda, OpenBSD `RoleWantsNetworkNamespace`, `WindowsSupervisor.h m_Started` | remove capture/field; `[[maybe_unused]]` only for the platform-conditional helper |
| hidden virtual | `HTTPProxyClientConnection::Start` | renamed to `StartResponse` |
| OpenSSL 4 deprecation | `X509_NAME_get_text_by_NID` in `Family.cpp`/`Reseed.cpp` | bounded `i2p::crypto::GetX509NameTextByNID` using index/entry/ASN1 accessors |
| Windows CRT deprecation | `getenv`, `strerror`, `_wopen` | `_dupenv_s`, `std::generic_category().message`, `_wsopen_s` |
| narrowing / shadowing / unused parameters | MSVC C4244/C4267/C4456/C4459/C4100 | bounds-checked casts, renamed locals, unnamed formals, widened DHT bit index |

**State:** Corregido mediante análisis estático; **Pending CI compilation**.
No local MSVC/MinGW/OpenSSL 4 build exists, so a clean warning output is not
claimed. One MSVC STL-originated `std::tuple` size_t/uint32_t warning may
remain in the torrent rarity ordering until the next log confirms whether the
explicit casts removed it.

## Second-pass failures (`b8c6473e`)

The b8c6473e dispatch removed the first blockers and exposed the next layer.
Runs: Linux/Android 40416816523, OpenBSD 40416823112, Fuzzing 40416827033,
Packaging 40416830825, Windows 40416819852 (MinGW completed, MSVC still
running when this was written).

### CE-009 — Const-correct X.509 entry/data in `Crypto.h`

- **First diagnostic:** `Crypto.h:115: error: cannot initialize a variable of
  type 'X509_NAME_ENTRY *' with an rvalue of type 'const X509_NAME_ENTRY *'`;
  `Crypto.h:117` has the same for `ASN1_STRING`.
- **Cause:** OpenSSL 4/LibreSSL return const pointers from
  `X509_NAME_get_entry`/`X509_NAME_ENTRY_get_data`; the helper added for the
  deprecated API used mutable locals.
- **Correction:** use `const X509_NAME_ENTRY *` and `const ASN1_STRING *`;
  `ASN1_STRING_to_UTF8` accepts a const input.
- **Files:** `libi2pd/Crypto.h`.
- **State:** Corregido mediante análisis estático; **Pending CI compilation**.

### CE-010 — Unqualified `LoadPrivateKeys` in `b33offlinekeys`

- **First diagnostic:** `tools/b33offlinekeys.cpp:146: error: 'LoadPrivateKeys'
  was not declared in this scope; did you mean 'i2p::tools::LoadPrivateKeys'?`
- **Cause:** the helper lives in namespace `i2p::tools`; the earlier build
  stopped before this tool target.
- **Correction:** qualify the call. The same latent unqualified use existed in
  `regaddr`, `regaddr_3ld`, `regaddralias` and `offlinekeys`; all are
  qualified now.
- **Files:** `tools/{b33offlinekeys,regaddr,regaddr_3ld,regaddralias,offlinekeys}.cpp`.
- **State:** Corregido mediante análisis estático; **Pending CI compilation**.

### CE-011 — `string_view` to `string` copy-initialization in Android JNI

- **First diagnostic:** `i2pd_android.cpp:1060: error: no viable conversion from
  'std::string_view' to 'std::string'`.
- **Cause:** `ReseedPhaseToken()` returns `string_view`; copy-initialization
  selects the explicit `std::string` constructor.
- **Correction:** direct-initialize `std::string out (ReseedPhaseToken (...));`.
- **Files:** `android/app/src/main/cpp/i2pd_android.cpp`.
- **State:** Corregido mediante análisis estático; **Pending CI compilation**.

### CE-012 — Doxygen treats placeholder tags as XML

- **First diagnostic:** `TuiProtocol.h:43: error: Unsupported xml/html tag
  <expected> found (warning treated as error)`.
- **Cause:** comments and included prose contained angle-bracket placeholders
  (`<expected>`, `<COMMAND>`, `<port>`, `<-`-style examples) that Doxygen
  parses as unsupported HTML/XML tags; `WARN_AS_ERROR=YES` aborts.
- **Correction:** escape comment placeholders as `&lt;...&gt;` in the affected
  C++/header prose, including `Modern.h`, `Expected.h` and the protocol/API
  descriptions; no code changed.
- **Files:** `libi2pd/{Modern.h,Expected.h,TuiProtocol.h,FS.h,...}`,
  `libi2pd_client/*.h/.cpp`, `privsep/LogWorker.h`, `daemon/api.h`,
  `tui/TuiWin32Ipc.h` and the other comment sites.
- **State:** Corregido mediante análisis estático; **Pending CI**.

### CE-013 — Warning classes after the first build succeeded

- **First diagnostics:** `SOCKS.cpp:176` missing `override`; unused private
  fields `m_remaining_upstream_data`/`m_command`; `TorrentsDHT.cpp:59/68`
  signed/unsigned compare; `I2PControl.cpp:95` unused
  `AcceptLocalControlPeer`; OpenBSD linker `strcat` warning from Boost.Asio
  endpoint formatting.
- **Correction:** add `override`; remove the unused fields and initializers;
  compare the DHT bit index against a cast bound; mark the platform overload
  `[[maybe_unused]]`; constrain the generic `LogPrint` so Asio endpoints use
  the safe `FormatAddress`/`FormatEndpoint` overloads instead of Boost's
  POSIX `sprintf`/`strcat` scope-ID formatter.
- **Files:** `libi2pd_client/SOCKS.cpp`, `libi2pd_client/TorrentsDHT.cpp`,
  `daemon/I2PControl.cpp`, `libi2pd/Log.h`.
- **State:** Corregido mediante análisis estático; **Pending CI**.

## Second-pass warning cleanup

`BUILD-022` remains the warning umbrella. The second pass additionally removed
the SOCKS/Log/TorrentsDHT warning families listed in CE-013 and the Doxygen
placeholder-tag failure. No local OpenBSD linker, Doxygen, MinGW/MSVC or
Android build exists; the next manual dispatch must confirm the warning set
and the `-static` link closure.

## Third-pass failures (`0da3aed0`, 2026-10-26)

The latest manual dispatch was on the current published `main` commit
`0da3aed0f255d801cf9e85ab1cf155b7175d24e2`, identical to the clean local HEAD
before this correction pass. All five workflows are
`workflow_dispatch`-only. Full run records and first-failure logs are available
from the linked runs: [Linux and Android](https://github.com/daviduhden/openi2pd/actions/runs/40419430023),
[Windows](https://github.com/daviduhden/openi2pd/actions/runs/40419433360),
[OpenBSD](https://github.com/daviduhden/openi2pd/actions/runs/40419436959),
[Fuzzing](https://github.com/daviduhden/openi2pd/actions/runs/40419440596) and
[Packaging and service validation](https://github.com/daviduhden/openi2pd/actions/runs/40419444535).
The Linux matrix's first shared-core error stopped GCC, Clang, OpenSSL 3/4,
sanitizer and installed-consumer targets before their later link/test stages.
OpenBSD compiled and linked the core/client/privsep libraries before the two
shared fixture compilation errors. Windows MSYS2 stopped on those fixtures;
the separate MSVC job compiled and linked its selected targets and then hit
two CTest assertions. No current protocol-specific compile/link failure was
observed. No local compile or test was run.

### CE-014 — Const issuer name passed to mutable X.509 helper

- **Workflow/jobs:** Linux and Android 40419430023 (`Linux (gcc)`, job
  121909863534; `Linux (clang)`, 121909863659; OpenSSL 4.0.0, 121909863608;
  OpenSSL 4.0.3, 121909863861; ASan/UBSan, 121909863431; UBSan, 121909863602;
  TSan, 121909863731; install consumer, 121909863537); Fuzzing 40419440596
  (`fuzz-smoke`, 121909927718). The separate OpenSSL 3.x job failed later at
  CE-015's fixture compile, not this X.509 diagnostic.
- **Commit/platform:** `0da3aed0f255d801cf9e85ab1cf155b7175d24e2`; Linux GCC/Clang
  and sanitizer/fuzz lanes using OpenSSL 4.0.x.
- **First diagnostic:** `libi2pd/Family.cpp:52`, invalid conversion from
  `const X509_NAME*` to `X509_NAME*` when calling
  `i2p::crypto::GetX509NameTextByNID`.
- **Cause:** OpenSSL's issuer accessor is read-only; the bounded helper did
  not express its read-only input in its signature.
- **Correction:** accept `const X509_NAME*`; keep the bounded index/entry/ASN.1
  accessors const. `Reseed.cpp` is the other caller and remains compatible.
- **Files:** `libi2pd/Crypto.h`, `libi2pd/Family.cpp`, `libi2pd/Reseed.cpp`.
- **State:** Corregido mediante análisis estático; **Pending CI compilation**.
  The earlier `Crypto.h:115/117` mutable-child-pointer diagnostic from
  `88ac1e92` is obsolete by the already-applied const-child correction; the
  newest OpenBSD run compiled the helper before reaching the reseed fixtures.

### CE-015 — Reseed fixtures assign through const variables-map lookup

- **Workflow/jobs:** Linux and Android 40419430023 (`Linux OpenSSL 3.x`, job
  121909863501), Windows 40419433360 (`build`, MSYS2/Clang64, job
  121911332256), OpenBSD 40419436959 (`build`, job 121909886795).
- **Commit/platform:** `0da3aed0f255d801cf9e85ab1cf155b7175d24e2`; GCC, Clang,
  MSYS2/Clang64 and OpenBSD Clang.
- **First diagnostics:** `tests/test-reseed-lifecycle.cpp:49` and
  `tests/test-reseed-redaction.cpp:49`; GCC reports assignment through a
  const `boost::program_options::variable_value`, while Clang says no viable
  `operator=`.
- **Cause:** `variables_map::operator[]` exposes a const mapped value. Both
  fixture setters need to insert or replace options that may not already exist.
- **Correction:** use `variables_map::insert_or_assign` in both fixture helpers;
  production configuration and reseed behavior are unchanged.
- **Files:** `tests/test-reseed-lifecycle.cpp`,
  `tests/test-reseed-redaction.cpp`.
- **State:** Corregido mediante análisis estático; **Pending CI compilation**.
  OpenBSD CTest/Make forwarding and Linux CTest were not reached in these runs.

### CE-016 — Windows IPC and restart-budget test assertions

- **Workflow/job:** Windows 40419433360, `build-msvc`, job 121911332781; the
  same IPC assertion recurred in Windows run 40416819852.
- **Commit/platform:** `0da3aed0f255d801cf9e85ab1cf155b7175d24e2`; native MSVC.
- **First diagnostics:** `tests/test-privsep-windows-ipc.cpp:446`, core-side
  `accepted->Send(Pong)` rejected; and
  `tests/test-privsep-windows-supervisor.cpp:135`, cumulative elapsed time
  exceeded the 60-second stable period.
- **Cause:** the IPC test reversed request/reply direction; the supervisor
  test summed the backoff ladder (61.75 seconds) while asserting it was below
  the per-generation uptime threshold.
- **Correction:** model core-to-worker Ping and worker-to-core Pong without
  weakening direction checks; give each replacement 10 ms uptime and assert
  stability per generation.
- **Files:** `tests/test-privsep-windows-ipc.cpp`,
  `tests/test-privsep-windows-supervisor.cpp`.
- **State:** Corrected by static analysis; **Pending CI tests**. The
  MSVC compile/link step succeeded on the recorded SHA; no runtime result on
  the corrected source exists.

### CE-017 — Android test rejects an opaque slash-containing tunnel name

- **Workflow/job:** Linux and Android 40419430023, `android`, job 121909863548.
- **Commit/platform:** `0da3aed0f255d801cf9e85ab1cf155b7175d24e2`; Java 25/Android
  API 29+ unit-test lane and arm64 JNI build.
- **First diagnostic:** `RuntimeAdminPolicyTest.tunnelNameBoundMatchesTheNativeVerb`
  failed at line 44; 112 unit tests ran, one failed.
- **Cause:** the Java test treated `/etc/i2pd/tunnels.conf` as a filesystem
  path, but the native and Java validators treat names as bounded opaque
  printable tokens and do not reject `/`.
- **Correction:** pin the existing native parity by expecting that bounded
  opaque name to pass; do not use the name as filesystem authority.
- **Files:** `android/app/src/test/java/org/openi2pd/router/RuntimeAdminPolicyTest.java`.
- **State:** Corrected by static analysis; **Pending CI tests**. JNI
  compilation passed in this job; Java unit failure stopped later Gradle
  steps. The earlier `string_view` JNI compilation diagnostic in run
  40416816523 is obsolete after direct `std::string` initialization.

### CE-018 — Doxygen missing `len1` parameter documentation

- **Workflow/job:** Linux and Android 40419430023, C++ source API
  documentation, job 121909863226.
- **Commit/platform:** `0da3aed0f255d801cf9e85ab1cf155b7175d24e2`; Ubuntu 24.04,
  Doxygen with warnings as errors.
- **First diagnostic:** `libi2pd/I2NPProtocol.h:447`: parameter `len1` of
  `I2NPMessage::Concat` is undocumented; warning treated as error.
- **Cause:** the new public contract documented `buf1` and the return value
  but omitted the source byte-count parameter.
- **Correction:** document `len1` as the number of available source bytes.
- **Files:** `libi2pd/I2NPProtocol.h`.
- **State:** Corrected by static analysis; **Pending CI tests**.

### CE-019 — ShellCheck SC2015 in the Java 25 configuration probe

- **Workflow/job:** Packaging and service validation 40419444535, Service
  contracts, job 121909912409; reproduced in run 40416830825.
- **Commit/platform:** `0da3aed0f255d801cf9e85ab1cf155b7175d24e2`; Ubuntu 24.04,
  ShellCheck 0.9.0.
- **First diagnostic:** `android/tools/java25-probe/check-config.sh:84`,
  SC2015 on chained `[ -n ] && ... || fail`.
- **Cause:** ShellCheck warns that the fallback command may run when the
  preceding conjunction is true; the service-contract lane treats the info
  diagnostic as a failure.
- **Correction:** use an explicit `if` testing each missing version component.
- **Files:** `android/tools/java25-probe/check-config.sh`.
- **State:** Corrected by static analysis; **Pending CI tests**.

### CE-020 — Static-PIE verification rejects the `file` description

- **Workflow/job:** Packaging and service validation 40419444535, Static musl
  build and package, job 121909911741; same failure in 40416830825.
- **Commit/platform:** `0da3aed0f255d801cf9e85ab1cf155b7175d24e2`; Alpine musl
  static PIE on x86_64.
- **First diagnostic:** the 99/99 object build and link succeeded; verification
  then failed because `file` printed `static-pie linked`, while the workflow
  only matched `statically linked`.
- **Cause:** the verification pattern omitted the valid static-PIE wording.
- **Correction:** accept either `static linked` or `static-pie linked`; the
  requirement remains a static binary.
- **Files:** `.github/workflows/packaging.yml`,
  `tests/check-packaging.cmake`.
- **State:** Corrected by static analysis; **Pending CI tests**.
  This is a verification-script failure, not a compiler or linker failure.

### CE-021 — Debian override file names a different binary package

- **Workflow/job:** Packaging and service validation 40419444535, Debian
  packages, job 121909912161.
- **Commit/platform:** `0da3aed0f255d801cf9e85ab1cf155b7175d24e2`; Ubuntu 24.04,
  Debian package/Lintian lane.
- **First diagnostic:** `E: i2pd: alien-tag i2pd-tui:` at
  `usr/share/lintian/overrides/i2pd:12`.
- **Cause:** the `i2pd-tui` override was stored in the `i2pd` package's
  override file. `lintian --fail-on error` stops on this error. Missing-manual
  page and upstream changelog messages are warnings only; `debugedit`'s
  unknown DWARF forms did not fail `dh_strip` or the package build step.
- **Correction:** move the override to `debian/i2pd-tui.lintian-overrides` and
  pin package ownership in the static packaging contract.
- **Files:** `debian/i2pd.lintian-overrides`,
  `debian/i2pd-tui.lintian-overrides`, `tests/check-packaging.cmake`.
- **State:** Corrected by static analysis; **Pending CI tests**.

The historical `b33offlinekeys` namespace error from SHA `88ac1e92` is
obsolete: current source qualifies `i2p::tools::LoadPrivateKeys`, and the
latest Debian build passed its build step. The latest current-SHA jobs provide
no independent NTCP2/SSU2/ratchet/netDb/client protocol, CMake configure, or
library-link failure. Their coverage is incomplete where an earlier shared
compile failure stopped the job. Downloaded artifacts are under
`/tmp/openi2pd-ci/<run-id>/` for 40419430023, 40419433360 and 40419444535;
40419436959 reported no downloadable artifacts. These files are temporary
diagnostics, not committed evidence.

At the time this third-pass correction was written, its fixes were local and
unpublished and had no validating run. The later published `cec24388` batch is
recorded separately below; do not treat an early stop in that batch as evidence
for downstream targets that it never reached.

## Fourth-pass failures (`cec24388`, 2026-10-27)

The next manual batch ran on published SHA
`cec243883ab41037401c25b6a5be7e64bfd56b66`. The `main` source corrections in
this section are being prepared locally against those exact diagnostics; no
run has yet evaluated them. The first shared Linux/OpenBSD/MSYS2 compile
blocker is a test's protected-member access. Other independent compiler roots
are the zero-argument formatting instantiation and a Clang constexpr
initializer-list rejection. Android source compilation, Java 25 unit tests,
Lint, class-file 69 validation and arm64 JNI linkage passed in this batch; its
Java 25 D8/R8 probe failed because the AGP-selected R8 artifact had not been
resolved before the cache search. CMake graph reconnaissance found no missing
source or link dependency, but the OpenBSD run did not reach all downstream
targets.

### CE-022 — Lifecycle test calls a protected base observer

- **Workflows/runs:** Linux and Android 40504357198 (Linux matrix) and OpenBSD
  40504361287 (build); Windows 40504365634 (MSYS2/Clang64 build).
- **Commit/platform:** `cec243883ab41037401c25b6a5be7e64bfd56b66`; Linux GCC/Clang,
  OpenBSD Clang and Windows MSYS2 Clang64.
- **First diagnostic:** `tests/test-runnableservice-lifecycle.cpp:38` and `:53`
  call `RunnableService::IsRunning()` from `main`; `libi2pd/util.h:249`
  declares it protected.
- **Cause:** the test subclass exposed Start/Stop and the Asio context, but no
  public observation method. Inheritance does not make a protected member
  public to callers using the derived object.
- **Correction:** add a narrow public `Running()` observer on the test-only
  subclass and route both assertions through it; production access remains
  protected.
- **Files:** `tests/test-runnableservice-lifecycle.cpp`, `libi2pd/util.h` (read-only
  declaration).
- **State:** Corrected by static analysis; **Pending CI compilation/tests**.

### CE-023 — Zero-argument `FormatPrintf` still instantiates dynamic `snprintf`

- **Workflow/run:** Windows 40504365634, MSYS2/Clang64 build with
  `WITH_HARDENING`.
- **Commit/platform:** `cec243883ab41037401c25b6a5be7e64bfd56b66`; MinGW/Clang64.
- **First diagnostic:** `i18n/I18N.h:100,104`, instantiated by
  `tests/test-i18n-format.cpp:28`; `-Wformat-security` rejects a nonliteral
  format argument with no matching variadic arguments.
- **Cause:** the zero-argument `if constexpr` returned, but the subsequent
  formatting statements were outside an `else` and remained instantiated.
- **Correction:** place all `snprintf` logic in the `else` branch. The existing
  empty-format regression instantiates the zero-argument specialization.
- **Files:** `i18n/I18N.h`, `tests/test-i18n-format.cpp` (existing regression).
- **State:** Corrected by static analysis; **Pending CI compilation/tests**.

### CE-024 — Fuzzer uses a non-constant `initializer_list` as `constexpr`

- **Workflow/run:** Fuzzing 40504369803, fuzz target build.
- **Commit/platform:** `cec243883ab41037401c25b6a5be7e64bfd56b66`; Clang/libFuzzer.
- **First diagnostic:** `tests/fuzz/fuzz-torrents-extensions.cpp:100`,
  initializer-list backing storage refers to a temporary and is not a constant
  expression.
- **Cause:** the two enum values are used only as a small constexpr iteration
  set; a constexpr `std::initializer_list` is not a portable supported
  constant object for this construction.
- **Correction:** use `constexpr std::array` and include `<array>` directly.
- **Files:** `tests/fuzz/fuzz-torrents-extensions.cpp`.
- **State:** Corrected by static analysis; **Pending CI compilation/tests**.

### CE-025 — Android R8 probe searches before AGP resolves R8

- **Workflow/job:** Linux and Android 40504357198, Android job
  122189741321.
- **Commit/platform:** `cec243883ab41037401c25b6a5be7e64bfd56b66`; Java 25,
  Gradle/AGP, arm64 Android.
- **First diagnostic:** `AGP-resolved R8 jar not found in the Gradle cache` in
  the Java 25 D8/R8 probe. Android native JNI build, Java/resource compilation,
  JVM tests, Lint and class-file major-69 assertion passed before it.
- **Cause:** unit-test and lint tasks did not require AGP's R8 artifact, but the
  workflow searched the Gradle cache before any APK dexing task resolved it.
- **Correction:** run the existing `assembleDebug`/APK step before discovering
  the AGP-selected R8 jar and running the Java 25 probe; do not pin a separate
  R8 version or lower the Java target.
- **Files:** `.github/workflows/build.yml`,
  `.agents/skills/openi2pd-android/SKILL.md`.
- **State:** Corrected by static analysis; **Pending CI validation**. This is a
  workflow dependency-order failure, not a Java/JNI source compilation error.

### CE-026 — Debian smoke test expects an uncompressed upstream changelog

- **Workflow/run:** Packaging and service validation 40504374509, Debian
  package smoke/DEP-8 validation.
- **Commit/platform:** `cec243883ab41037401c25b6a5be7e64bfd56b66`; Debian/Ubuntu
  package install.
- **First diagnostic:** `debian/tests/smoke:201` cannot find
  `/usr/share/doc/i2pd/ChangeLog`; debhelper installs the upstream changelog as
  `/usr/share/doc/i2pd/ChangeLog.gz`.
- **Cause:** the smoke assertion did not account for Debian changelog
  compression, although the CMake and package source manifests correctly name
  the source `ChangeLog`.
- **Correction:** assert the packaged `ChangeLog.gz` and pin that path in the
  packaging static contract; keep the source manifest and normal compression.
- **Files:** `debian/tests/smoke`, `tests/check-packaging.cmake`,
  `.agents/skills/openi2pd-packaging/SKILL.md`.
- **State:** Corrected by static analysis; **Pending CI tests**.

### CE-037 — Comment reflow removed Doxygen block terminators

- **Evidence:** Static review of the uncommitted comment-only worktree after
  the 2026-10-31 run batch. No CI run compiled this comment revision; run IDs
  40594540028/40594544850/40594549892 use their recorded `b34b2e79` commit,
  before these local comment edits.
- **Commit/platform:** Base `HEAD` is
  `b34b2e79546c929f1f5f788d4905b41c9a92d45d`; affected local C++ test
  translation units span Linux, Windows and OpenBSD build configurations.
- **First diagnostic:** No compiler diagnostic is attributed because this
  worktree revision was not run in CI. Static inspection found one-line
  `/** ... */` comments reflowed as repeated `/*` openers without a matching
  closer, causing the remainder of each translation unit to be comment text.
- **Cause:** The comment reflow treated a Doxygen block as ordinary prose and
  emitted an opener for each wrapped line while dropping the original closer.
- **Correction:** Restore one `/**` opener, ` *` continuation lines and one
  ` */` closer for each affected block. A repository-wide source scan now
  finds no repeated `/* *` opener pattern.
- **Files:** `tests/test-android-daemon-lifecycle.cpp:32`,
  `tests/test-datastorage-client.cpp:63`,
  `tests/test-http-proxy-limits.cpp:30,241,331`,
  `tests/test-privsep-log.cpp:189,259,303`,
  `tests/test-privsep-openbsd-policy.cpp:54,544`,
  `tests/test-privsep-worker-identity.cpp:47,136`,
  `tests/test-privsep-windows-ipc.cpp:52,115`,
  `tests/test-privsep-windows-isolation.cpp:87`.
- **State:** Corrected by static analysis; **Pending CI compilation and
  source documentation validation**. No local compilation was run.

### CE-027 — Lintian reports duplicate updaterc calls in generated postrm

- **Workflow/runs:** Packaging 40504374509 and 40532412220, static musl package
  Lintian validation.
- **Commit/platform:** initially `cec243883ab41037401c25b6a5be7e64bfd56b66`,
  reproduced at `b25d0c3a31a65e13001629522c10db949cdeda2d`; Debian packaging.
- **First diagnostic:** `duplicate-updaterc.d-calls-in-postrm` at generated
  `postrm:30`. The source script has one `#DEBHELPER#` marker plus the purge
  data-removal branch; the diagnostic alone does not identify which generated
  maintscript fragment duplicated the call.
- **Cause:** the downloaded static-package `.deb` contains two purge-time
  `update-rc.d i2pd remove` calls: one written in
  `contrib/static/debian/i2pd.postrm`, and one generated by the `#DEBHELPER#`
  expansion from `dh_installinit`. The regular Debian `debian/postrm` does not
  contain the explicit call; the duplicate was specific to the static package.
- **Correction:** remove the manual call from the static package maintainer
  script and leave registration cleanup to `dh_installinit`. Add a
  compile-free packaging contract that forbids the duplicate source call.
- **Files:** `contrib/static/debian/i2pd.postrm`, `tests/check-packaging.cmake`.
- **State:** Corrected by static analysis; **Pending CI validation**. At
  `b25d0c3a`, the static package Lintian job still failed on the duplicate;
  regular Debian Lintian passed. Non-fatal shared-library prerequisite and
  changelog-name warnings are not compile/link failures.

### CE-028 — Tunnel build regression uses a parent-namespace constant unqualified

- **Workflow/jobs:** Linux and Android 40532383635 (GCC, Clang, OpenSSL 3.x/4.x
  and sanitizer builds), OpenBSD 40532389857, and Windows 40532395786
  (MSYS2/Clang).
- **Commit/platform:** `b25d0c3a31a65e13001629522c10db949cdeda2d`; Linux,
  OpenBSD and Windows toolchains.
- **First diagnostic:** `tests/test-tunnel-build-records.cpp` reports
  `SHORT_TUNNEL_BUILD_RECORD_SIZE` undeclared and suggests
  `i2p::SHORT_TUNNEL_BUILD_RECORD_SIZE`. The later diagnostics repeat the same
  identifier at the other record-size expressions.
- **Cause:** the test imports `i2p::tunnel` and `i2p::data`, but the constant
  is declared in parent namespace `i2p`; importing a child namespace does not
  import names from its parent.
- **Correction:** add a narrow `using i2p::SHORT_TUNNEL_BUILD_RECORD_SIZE;`
  in the test's anonymous namespace. No production API or wire constant
  changes.
- **Files:** `tests/test-tunnel-build-records.cpp`.
- **State:** Corrected by static analysis; **Pending CI compilation/tests**.

### CE-029 — Android SAF fixture call is ambiguous between Bundle and String overloads

- **Workflow/job:** Linux and Android 40532383635, Android job
  122288413246, `:i2pd:compileDebugAndroidTestJavaWithJavac`.
- **Commit/platform:** `b25d0c3a31a65e13001629522c10db949cdeda2d`; Java 25,
  Android instrumentation-test source.
- **First diagnostic:** `TorrentSafProviderCancellationTest.java:271:
  reference to queryChildDocuments is ambiguous`; both
  `DocumentsProvider.queryChildDocuments(String,String[],Bundle)` and the
  fixture's `queryChildDocuments(String,String[],String)` match a null third
  argument.
- **Cause:** the test intended the fixture's sort-order overload but passed an
  untyped null, which became ambiguous after the platform API added the Bundle
  overload.
- **Correction:** cast the third argument to `String`; this selects the
  fixture helper without changing provider behavior.
- **Files:** `android/app/src/androidTest/java/org/openi2pd/router/TorrentSafProviderCancellationTest.java`.
- **State:** Corrected by static analysis; **Pending CI compilation/tests**.

### CE-030 — Debian smoke test expects uncompressed Markdown README

- **Workflow/job:** Packaging 40532412220, Debian packages job
  122288513837, install and DEP-8 smoke step.
- **Commit/platform:** `b25d0c3a31a65e13001629522c10db949cdeda2d`; Debian package.
- **First diagnostic:** `FAIL: missing file: /usr/share/doc/i2pd/README.md`.
  The downloaded package contains `/usr/share/doc/i2pd/README.md.gz`; the
  `ChangeLog.gz` check already passes.
- **Cause:** Debian's documentation compression also compresses the Markdown
  README. The source install manifest and package file are correct; the smoke
  assertion expected the pre-compression name.
- **Correction:** assert `README.md.gz` and add it to the compile-free package
  contract while retaining the source manifest's uncompressed `README.md`.
- **Files:** `debian/tests/smoke`, `tests/check-packaging.cmake` and the
  packaging skill.
- **State:** Corrected by static analysis; **Pending CI tests**.

On run 40532395786, MSVC compiled its selected targets and then retained two
CTest assertion failures in worker IPC/supervisor tests; these are runtime
failures, not compile/link failures. On the same SHA, the other Windows lane
failed at CE-028. Run 40532402554 passed its fuzz smoke job. The regular Debian
Lintian step passed on 40532412220, while the static musl Lintian step found
CE-027 and the Debian install smoke found CE-030. No new CMake source-list or
target link-closure defect was demonstrated by static review. Early stops mean
downstream configurations and tests remain unproven. CE-022..CE-030 and the
CE-027 correction remain pending validation on a newer SHA. No local compiler,
CMake configure/build, CTest, Gradle, fuzzer, Lintian, package runtime or
workflow was run.

### CE-031 — Test uses an unqualified nested transport enumerator

- **Workflows/jobs:** Linux and Android 40594540028 (GCC, Clang, OpenSSL 3.x,
  OpenSSL 4.0.0/4.0.3 and sanitizer lanes); Windows 40594544850 (MSYS2/MinGW);
  OpenBSD 40594549892.
- **Commit/platform:** `b34b2e79546c929f1f5f788d4905b41c9a92d45d`; Ubuntu 24.04,
  MSYS2/Clang64 and OpenBSD Clang/libc++.
- **First diagnostic:** `tests/test-tunnelpool-test-timing.cpp:100`,
  `eAllTransports` undeclared; Clang suggests a different nested enumerator.
  `libi2pd/RouterInfo.h:125` declares the correct value as
  `i2p::data::RouterInfo::eAllTransports`.
- **Cause:** importing `i2p::data` does not import enumerators nested inside
  `RouterInfo`. GCC's later `EstablishedPair` brace-conversion diagnostic is
  cascading from the unresolved constructor argument.
- **Correction:** qualify the enumerator in the test fixture; no production
  interface or wire value changes.
- **Files:** `tests/test-tunnelpool-test-timing.cpp`;
  `libi2pd/RouterInfo.h` (declaration only).
- **State:** Corrected by static analysis; **Pending CI compilation and tests**.

### CE-032 — Named Libraries component omits exported archives

- **Workflow/job:** Linux and Android 40594540028, job
  `Install components and consumer package` (`122506989963`), package consumer
  configure after all 133 build steps completed.
- **Commit/platform:** `b34b2e79546c929f1f5f788d4905b41c9a92d45d`; Ubuntu 24.04,
  Clang and CMake package consumer.
- **First diagnostic:** generated `i2pd-targets.cmake:112` reports that imported
  target `i2pd::libi2pd` references `/tmp/.../lib/libi2pd.a`, which does not
  exist. The exact-run `install-components` artifact contains
  `lib/i2pd_privsep.a` but omits the other exported static archives.
- **Cause:** the target install assigned a general component after the
  artifact groups; selecting `Libraries` did not install the archive and
  library artifacts referenced by the export.
- **Correction:** assign `Libraries` explicitly to each ARCHIVE and LIBRARY
  group, and `Runtime` to the RUNTIME group; pin this contract in the
  compile-free build-system checker.
- **Files:** `build/CMakeLists.txt`, `tests/check-build-system.cmake`,
  `.agents/skills/openi2pd-build/SKILL.md`.
- **State:** Corrected by static analysis; **Pending CI compilation and
  installed-consumer validation**.

### CE-033 — Debian compresses the branding palette Markdown

- **Workflow/job:** Packaging and service validation 40594560248, Debian
  packages (`122507055854`), install and DEP-8 smoke step.
- **Commit/platform:** `b34b2e79546c929f1f5f788d4905b41c9a92d45d`; Debian package.
- **First diagnostic:** `debian/tests/smoke:206` reports the absent
  `/usr/share/doc/i2pd/assets/branding/colors.md`. The downloaded `.deb`
  includes `assets/branding/colors.md.gz`.
- **Cause:** debhelper compresses Markdown documentation installed under
  `/usr/share/doc`; the source manifest is correct and the generated package
  path is compressed.
- **Correction:** update installed-package smoke and file-list assertions to
  expect `.md.gz`, retaining the uncompressed source install path; add the
  package-path assertion to the compile-free packaging contract.
- **Files:** `debian/tests/smoke`, `.github/workflows/packaging.yml`,
  `tests/check-packaging.cmake`, `.agents/skills/openi2pd-packaging/SKILL.md`.
- **State:** Corrected by static analysis; **Pending CI package validation**.

### CE-034 — Android APK verification assumes a fixed output directory

- **Workflow/job:** Linux and Android 40594540028, Android job
  (`122506989902`), `Build debug APK for arm64` step.
- **Commit/platform:** `b34b2e79546c929f1f5f788d4905b41c9a92d45d`; Java 25,
  AGP 9.4.1, Android arm64.
- **First diagnostic:** after Gradle reports `BUILD SUCCESSFUL` for
  `assembleDebug`, `assembleDebugAndroidTest`, `assembleRelease` and lint,
  `find app/build/outputs/apk/debug` fails because that directory is absent.
  The uploaded report artifact has no APK, so the concrete output location is
  not known from this run.
- **Cause:** workflow assertions assume the APK resides in a single fixed
  variant directory; the assertion cannot discover ABI-split or universal
  outputs under the AGP output tree.
- **Correction:** search discovered APKs by debug/release variant, exclude
  instrumentation APKs, select app artifacts by their arm64 library/assets and
  DEX contents rather than exact filenames, print all discovered paths on
  selection failure, and reuse the selected debug APK for later checks. A
  dedicated staging step copies discovered APKs into a fixed workspace
  directory for artifact upload; action `with:` expressions cannot consume
  paths exported to later steps through `GITHUB_ENV`.
- **Files:** `.github/workflows/build.yml`,
  `.agents/skills/openi2pd-android/SKILL.md`.
- **State:** Corrected by static analysis; **Pending CI Android APK/D8/R8/JNI
  artifact validation**. Native compilation, JVM tests, Lint and APK assembly
  succeeded on the recorded SHA; package-content and dex-lowering checks did
  not run.

### CE-035 — Windows stable-period regression resets readiness too late

- **Workflow/job:** Windows 40594544850, `build-msvc` (`122507015934`), CTest.
- **Commit/platform:** `b34b2e79546c929f1f5f788d4905b41c9a92d45d`; Windows/MSVC.
- **First diagnostic:**
  `tests/test-privsep-windows-supervisor.cpp:193`,
  `afterStable.delay == LogWorkerRestartBudget::INITIAL_DELAY` assertion fails.
  The same assertion failed on runs 40532395786 (`b25d0c3a`) and 40504365634
  (`cec24388`).
- **Cause:** the fixture called `NoteReady` at the end of the elapsed stable
  period, moving the readiness origin to the failure instant and preventing
  `NoteStable` from observing the full period.
- **Correction:** keep the original ready timestamp and let failure planning
  evaluate the elapsed stable interval.
- **Files:** `tests/test-privsep-windows-supervisor.cpp`.
- **State:** Corrected by static analysis; **Pending CI tests**.

### CE-036 — Windows accepts a second WorkerHello after binding

- **Workflow/job:** Windows 40594544850, `build-msvc` (`122507015934`), CTest.
- **Commit/platform:** `b34b2e79546c929f1f5f788d4905b41c9a92d45d`; Windows named pipe.
- **First diagnostic:** `tests/test-privsep-windows-ipc.cpp:615`,
  `!duplicate->has_value()` fails. The same assertion failed on runs
  40532395786 (`b25d0c3a`) and 40504365634 (`cec24388`).
- **Cause:** the accepted core endpoint authorized `WorkerHello` through the
  generic role registry even after `AcceptBound` had consumed the handshake
  and bound the channel. Its refusal path also tried to send a reply-direction
  `Error` through a request-only send gate.
- **Correction:** refuse `WorkerHello` explicitly on a bound core endpoint and
  allow typed `Error` replies through the Windows direction gate. The existing
  regression verifies a refusal while preserving subsequent ping/pong traffic.
- **Files:** `privsep/WindowsIpc.cpp`, `privsep/WindowsIpc.h`,
  `tests/test-privsep-windows-ipc.cpp` (existing regression).
- **State:** Corrected by static analysis; **Pending CI tests**.

### CE-038 — Nested comments break shared HTTP parser regression sources

- **Workflows/jobs:** Linux and Android 41207325363 (GCC 124554970150,
  Clang 124554970086, OpenSSL 3/4 124554970159/124554970256/124554970131,
  ASan/UBSan/TSan 124554970125/124554970194/124554970200); Windows
  41207324984 MSYS2/Clang64 124554970047; OpenBSD 41207325119
  124554970347.
- **Commit/platform:** 46738ddf6f0f9b8e01e31d7c28bc6396215f82e8; Linux
  GCC/Clang, OpenSSL 3.x/4.x, sanitizers, Windows Clang64 and OpenBSD 8.0
  Clang/libc++.
- **First diagnostic:** tests/test-http-req.cpp:190, expected expression
  (Clang) / stray backslash (GCC), at the Accept: */* sample; later numParts,
  parts and req errors cascade. A second parse break in tests/test-http-res.cpp
  reports response undeclared at line 108.
- **Cause:** four explanatory comments reopen /* without closing the prior
  block. The */ sequence in the HTTP wildcard sample ends the open comment;
  subsequent statements are parsed outside their intended function/block.
- **Correction:** convert the four explanations to concise // comments;
  retain all test statements and HTTP values. The failing source files matched
  the published SHA before correction.
- **Files:** tests/test-http-req.cpp, tests/test-http-res.cpp.
- **State:** Detected in CI; corrected by static analysis; **Pending CI
  compilation** and **Pending CI tests**. Downstream targets remain
  unverified where the test parse failure stopped the build.

### CE-039 — Doxygen reaches EOF inside OpenBSD policy comments

- **Workflow/job:** Linux and Android 41207325363, C++ source API
  documentation job 124554970104.
- **Commit/platform:** 46738ddf6f0f9b8e01e31d7c28bc6396215f82e8; Doxygen.
- **First diagnostic:** privsep/OpenBSDSandbox.h:286: reached EOF inside a
  nested comment, nesting level 3 (probable opener lines 195, 129 and 105).
- **Cause:** four duplicate /** openers left policy declaration comments
  nested and unclosed.
- **Correction:** remove extra openers; keep the contracts attached to their
  declarations.
- **Files:** privsep/OpenBSDSandbox.h.
- **State:** Detected in CI; corrected by static analysis; **Pending CI source
  documentation validation**. No local Doxygen run was performed.

### CE-040 — Android sourceJavadoc uses removed AGP bootClasspath property

- **Workflow/job:** Linux and Android 41207325363, Android 124554969967,
  Generate and check Android Javadoc.
- **Commit/platform:** 46738ddf6f0f9b8e01e31d7c28bc6396215f82e8; JDK 25,
  AGP 9.4.1, Android arm64.
- **First diagnostic:** task creation fails at android/app/build.gradle:222:
  unknown property bootClasspath on ApplicationExtensionImpl.
- **Cause:** the custom Javadoc task reads android.bootClasspath, absent from
  the pinned AGP application DSL.
- **Correction:** use the documented androidComponents.sdkComponents.bootClasspath
  provider; retain the debug Java classpath, Java 25 toolchain, doclint and
  warnings-as-errors.
- **Files:** android/app/build.gradle, .agents/skills/openi2pd-android/SKILL.md.
- **State:** Detected in CI; corrected by static analysis; **Pending CI
  Javadoc validation**. Native build, JVM tests, Lint and APK assembly passed
  on the recorded SHA; no post-fix Gradle run exists.

### CE-041 — Perl portability guard rejects its required shared validator

- **Workflow/job:** Linux and Android 41207325363, Perl quality 124554970154.
- **Commit/platform:** 46738ddf6f0f9b8e01e31d7c28bc6396215f82e8; Ubuntu,
  Perl Test::More.
- **First diagnostic:** tools/tests/perl-portability.t:93-94 fails only the
  no-project-maintained-Python-source and no-Python-shebang assertions,
  identifying scripts/validate-agent-knowledge.py.
- **Cause:** the portability guard predates the canonical Python validator
  and its black-box contract suite, which AGENTS.md requires byte-identical
  across adopters. Other Perl quality subtests passed.
- **Correction:** allowlist only the canonical validator and its mandated
  contract test; retain rejection of all other Python sources/shebangs.
  Clarify the narrow exception in AGENTS.md and the tooling Skill.
- **Files:** tools/tests/perl-portability.t, AGENTS.md,
  .agents/skills/openi2pd-tooling/SKILL.md.
- **State:** Detected in CI; corrected by static analysis; **Pending CI tests**.
  No Perl suite was run locally.

  Complete-migration follow-up: PERL-010/011 supersede the narrow Python
  allowlist with a Perl validator and Test::More contract. The source inventory
  again rejects every maintained Python file/shebang, including these former
  exceptions. This is a new static correction, Pending CI tests; the historical
  run above does not validate the replacement.

### CE-042 — Windows role test contradicts AppContainer policy

- **Workflow/job:** Windows 41207324984, MSVC job 124554969879,
  test-privsep-windows-supervisor CTest.
- **Commit/platform:** 46738ddf6f0f9b8e01e31d7c28bc6396215f82e8; Windows/MSVC.
- **First diagnostic:** assertion !WindowsRoleNeedsAppContainer(Crypto) at
  tests/test-privsep-windows-supervisor.cpp:283.
- **Cause:** the test expected Crypto and Admin not to require AppContainer,
  contradicting the current role isolation table and security policy.
- **Correction:** assert AppContainer is required for both roles; production
  policy is unchanged.
- **Files:** tests/test-privsep-windows-supervisor.cpp.
- **State:** Detected in CI; corrected by static analysis; **Pending CI tests**.
  Selected MSVC targets built before CTest; no local test run exists.

### CE-043 — Windows IPC test reverses the accepted channel direction

- **Workflow/job:** Windows 41207324984, MSVC job 124554969879,
  test-privsep-windows-ipc CTest.
- **Commit/platform:** 46738ddf6f0f9b8e01e31d7c28bc6396215f82e8; Windows
  named-pipe IPC.
- **First diagnostic:** replyRefused.has_value() && !replyRefused->has_value()
  fails at tests/test-privsep-windows-ipc.cpp:680.
- **Cause:** AcceptBound() returns the core-side channel, which receives
  worker replies. The old test rejected DataStorageReply there and then
  expected a worker request on the core endpoint.
- **Correction:** assert valid DataStorageReply receipt and keep the channel
  usable with Pong after expected LogRecord/ConfigureLogFd refusals.
  Production authorization is unchanged.
- **Files:** tests/test-privsep-windows-ipc.cpp,
  .agents/skills/openi2pd-linux-windows/SKILL.md,
  .agents/skills/openi2pd-privsep/SKILL.md.
- **State:** Detected in CI; corrected by static analysis; **Pending CI tests**.
  This is a runtime test failure, not a compile/link failure.

### CE-044 — Debian remove/purge step exits without naming its failing check

- **Workflow/job:** Packaging and service validation 41207325472, Debian
  packages 124554971078, final Remove and preserve, then purge step.
- **Commit/platform:** 46738ddf6f0f9b8e01e31d7c28bc6396215f82e8; Ubuntu 24.04.
- **First diagnostic:** run 41207325472 was ambiguous. Follow-up Packaging run
  [41226179732](https://github.com/daviduhden/openi2pd/actions/runs/41226179732),
  Debian job `124610688820`, identifies
  `FAIL: remove deleted /var/lib/i2pd` after package removal.
- **Cause:** Exact artifact `i2pd_2.61.0-5_amd64.deb` owns the directory and a
  certificates symlink below it. `remove` unlinks the last package-owned child;
  dpkg prunes the now-empty directory. The step tested empty-directory survival,
  not whether real administrator data survives. The package `postrm` removes
  persistent paths only for `purge`.
- **Correction:** Seed a harmless mode-0600 marker owned by `i2pd` before
  `remove`, assert the marker and conffile survive, then retain the explicit
  purge assertion. No maintainer script or package data-removal behavior is
  changed.
- **Files:** .github/workflows/packaging.yml; package scripts only if later
  evidence points there.
- **State:** Detected in CI; cause identified; regression corrected by static
  analysis; **Pending CI tests/package validation** on a revision containing
  the marker fixture.

### CE-045 — Base64 regression source has an unterminated block comment

- **Workflows/jobs:** Windows [41222224440](https://github.com/daviduhden/openi2pd/actions/runs/41222224440), failing MSYS2 job `124599005640`; Linux and Android [41222227216](https://github.com/daviduhden/openi2pd/actions/runs/41222227216), failing Linux OpenSSL 3 job `124599012964`, Clang job `124599013038`, GCC job `124599013155`, and sanitizer jobs `124599013117`, `124599013184`, `124599013239`; OpenBSD [41222229438](https://github.com/daviduhden/openi2pd/actions/runs/41222229438), job `124599019529`. Fuzzing [41222231944](https://github.com/daviduhden/openi2pd/actions/runs/41222231944) built fuzz targets and passed one-input CTest smoke before the bounded fuzz step was canceled. Packaging [41222233937](https://github.com/daviduhden/openi2pd/actions/runs/41222233937) built static-musl, container and service jobs successfully; Debian was canceled. All five runs used the published SHA below; several matrix jobs were canceled after the user asked to stop following them.
- **Commit/platform:** `45f5f739aa47ed87aa7462e9b040c0db601bc667`, Windows Clang64, Linux GCC/Clang and OpenSSL variants, OpenBSD 8.0 Clang/LibreSSL.
- **First diagnostic:** `tests/test-base-64.cpp:81` unterminated block comment; line 82 reports `-Wcomment`; the closing-brace diagnostic at line 87 is a parser cascade.
- **Cause:** a second `/*` was inserted into an open explanatory block during comment reflow, so the intended test assertions remain inside a comment through EOF.
- **Related pattern:** the same duplicate `/**` reflow defect produced warnings in `privsep/Protocol.h:449`, `privsep/WindowsSupervisor.h:304` and `tools/vanity.hpp:59`. These are one comment-maintenance root cause, not four independent parser incidents. Historical malformed comments in `test-http-req.cpp`, `test-http-res.cpp`, `OpenBSDSandbox.h` and `I2PTunnelAccessFilter.h` are already corrected in later revisions.
- **Correction:** use a single-line comment for the base64 explanation and remove duplicate openers from all three current production files. Added a Perl lexical comment-integrity regression with nested-comment, unclosed-comment, ordinary-comment, string and raw-string fixtures; the manual `skills-static` job invokes it.
- **Files:** `tests/test-base-64.cpp`, `privsep/Protocol.h`, `privsep/WindowsSupervisor.h`, `tools/vanity.hpp`, `tools/tests/cpp-comment-integrity.t`, `.github/workflows/build.yml`.
- **Local status:** corrected by static analysis; regression prepared. CI on this exact SHA is not validation of the local correction.
- **Validation pending:** next manual Windows/Linux/OpenBSD/Fuzzing builds and the compile-free comment-integrity CI step. The latest MSVC job was canceled during dependency installation; it did not validate MSVC.
- **Related warning inventory:** `docs/ci/COMPILATION-WARNINGS.md` CW-001; reusable pattern and search procedure: `docs/ci/COMPILATION-PATTERNS.md`.

### CE-046 — Transit tunnel test uses unqualified protocol constants

- **Workflows/jobs:** Linux and Android [41226171242](https://github.com/daviduhden/openi2pd/actions/runs/41226171242), Linux OpenSSL 3 job `124610663647`; OpenBSD [41226173721](https://github.com/daviduhden/openi2pd/actions/runs/41226173721), job `124610671636`; Windows [41226168239](https://github.com/daviduhden/openi2pd/actions/runs/41226168239), MSYS2 job `124610655800`. All ran SHA `abe09136c84d1a4df487306dad6a7eb8e2034806`.
- **First diagnostic:** `tests/test-transit-tunnel-build-replay.cpp:35` uses `SHORT_TUNNEL_BUILD_RECORD_SIZE` without `i2p::`; Clang/GCC suggest `i2p::SHORT_TUNNEL_BUILD_RECORD_SIZE`. The later unqualified `TUNNEL_BUILD_RECORD_SIZE` uses are the same namespace cause.
- **Cause/search:** Both constants are declared in `i2p` by `libi2pd/I2NPProtocol.h`; the test imports only `i2p::tunnel`. Repository search found no other unqualified use of these constants in this test.
- **Correction:** Import both constants explicitly from `i2p`; numeric test cases and protocol behavior are unchanged.
- **Files:** `tests/test-transit-tunnel-build-replay.cpp`.
- **State:** Corrected by static analysis; Linux, OpenBSD and Windows compilation pending CI. Do not attribute the published failing run to the local edit.

### CE-047 — Android locale guard required an unused Chinese plural quantity

- **Workflow/job:** Linux and Android [41226171242](https://github.com/daviduhden/openi2pd/actions/runs/41226171242), Android job `124610663799`, SHA `abe09136c84d1a4df487306dad6a7eb8e2034806`.
- **First diagnostic:** `check-android-locales.pl` reported missing `quantity="one"` for four zh-CN plurals at `Validate Android Gradle toolchain`; Java 25 configuration passed, but Android SDK setup, native/Gradle compilation, lint and APK steps were skipped.
- **Cause/search:** The checker required English `one`/`other` for Chinese resources even though the maintained `values-zh-rCN` catalogs correctly use `other` only. Restoring synthetic `one` entries would recreate the Lint unused-quantity finding.
- **Correction:** The checker now requires only `other` for zh-CN and rejects extra Chinese quantities. An isolated fixture covers acceptance, missing `other` and unused `one`; locale matrix and Android/i18n guidance are synchronized.
- **Files:** `android/tools/check-android-locales.pl`, `tools/tests/maintenance-checkers.t`, `tests/check-i18n.cmake`, locale documentation and Android/i18n skills.
- **State:** Corrected locally by static analysis; static locale/i18n contracts pass. Fixture, Gradle/JNI/Lint/APK and full Android matrix remain pending CI.

## Third batch (`00929cd1`, 2026-10-31)

Runs: Linux/Android [41229374061](https://github.com/daviduhden/openi2pd/actions/runs/41229374061), OpenBSD [41229377388](https://github.com/daviduhden/openi2pd/actions/runs/41229377388), Windows [41229371203](https://github.com/daviduhden/openi2pd/actions/runs/41229371203), Fuzzing [41229380998](https://github.com/daviduhden/openi2pd/actions/runs/41229380998) (success) and Packaging [41229383879](https://github.com/daviduhden/openi2pd/actions/runs/41229383879).

### CE-048 — Shared test misses the `i2p::client` garlic/DataMessage contract

- **Diagnostic:** `tests/test-i2np-dispatch-policy.cpp:289` uses
  `IsValidDataMessageLength` unqualified; lines 298-301 use
  `MAX_INCOMING_GARLIC_QUEUE`/`IsIncomingGarlicQueueFull`.
- **Cause:** the declarations live in `i2p::client::Destination.h`; the test
  imported only `i2p`.
- **Correction:** include `Destination.h` and import `i2p::client`; no test
  logic changed.
- **State:** Corrected by static analysis; Pending CI compilation.

### CE-049 — Windows IPC direction rejected the core's typed Error

- **Diagnostics:** MSVC `test-privsep-windows-ipc` assertion at line 735,
  `delivered.has_value() && delivered->has_value()`; the test sent a request
  from an unbound worker-side channel and the core refused it, then could not
  send the promised `Error` because core-side `Send` allowed only request
  types.
- **Correction:** the channel already allowed the core to send `Error` for
  attributable refusals; the max-payload case was sending a request from the
  worker side and expecting the core to deliver it. The test now sends that
  request from the core to the worker, while the oversized-frame case keeps
  exercising the typed Error path.
- **State:** Corrected by static analysis; Pending CI tests.

### CE-050 — Windows supervisor created the AppContainer before root validation

- **Diagnostic:** MSVC `test-privsep-windows-supervisor` assertion at line 344,
  handle count above baseline after a Storage role with no delegated root.
- **Cause:** `Start()` created the AppContainer profile before checking
  `persistentWriteRoot`; the one-time profile/runtime allocations survived the
  terminal failure, violating the test's fail-closed/no-handle contract.
- **Correction:** validate the root before creating the container; the
  container is released on the remaining failure paths as before.
- **State:** Corrected by static analysis; Pending CI tests.

### CE-051 — Debian remove fixture used an unowned root-level marker

- **Diagnostic:** Packaging 41229383879, Debian job, `FAIL: remove deleted
  persistent state under /var/lib/i2pd` after `apt-get remove`.
- **Cause:** the test seeded a root-level file in the package-owned
  `/var/lib/i2pd`; the package/dpkg removal of the package-owned directory can
  prune an otherwise empty tree, so the fixture did not represent router state.
- **Correction:** seed a real `netDb/.ci-remove-preservation` file under the
  state directory, assert it before removal, and assert it (and the conffile)
  after remove; purge still requires the state tree to disappear. The checker
  pin follows the new paths.
- **State:** Corrected by static analysis; Pending CI package
  validation.

### CE-052 — OpenBSD linker warning from SOCKS Asio address formatting

- **Diagnostic:** OpenBSD 41229377388, linker warning
  `strcat() is almost always misused` in
  `boost::asio::detail::socket_ops::inet_ntop` from `SOCKS.cpp`.
- **Cause:** two `address_v4/address_v6::to_string()` calls instantiated
  Boost's POSIX scope-ID formatter; the generic `LogPrint` fix does not cover
  direct `to_string` calls.
- **Correction:** use `i2p::util::net::FormatAddress` for both address results.
- **State:** Corrected by static analysis; Pending OpenBSD CI.

### CE-054 — Android R8 probe assumed one Gradle cache layout

- **Run/job:** Linux and Android `41229374061`, Android job `124620143074`.
- **First diagnostic:** `AGP-resolved R8 jar not found in the Gradle cache
  after assembleDebug`; `assembleDebug` itself succeeded and produced the APK.
- **Cause:** the probe searched only
  `~/.gradle/caches/modules-2/files-2.1/com.android.tools/**/r8/*`. AGP 9.4
  may resolve R8 under `com.android.tools.build` or reuse a cache restored
  without that artifact; the failure was tooling discovery, not Java/R8
  lowering.
- **Correction:** search the entire Gradle user home for `r8-*.jar`/`r8.jar`,
  then fall back to the SDK build-tools `r8*.jar`/`d8.jar`; print found paths
  on failure. The D8/R8 `--min-api 29` probe itself is unchanged.
- **Files:** `.github/workflows/build.yml`.
- **State:** Corrected by static analysis; **Pending Android CI**.

### CE-055 — Member call on a `char[]` in the I2PControl socket test

- **Run/job:** Linux and Android `41232673150` (every Linux job), OpenBSD
  `41232678782`, Sanitizers (inside `41232673150`).
- **First diagnostic:** `tests/test-i2pcontrol-socket.cpp:181:18: error:
  member reference base type 'char[32]' is not a structure or union`.
- **Cause:** the test's `char directory[] = "/tmp/...XXXXXX"` is the mutable
  `mkdtemp` buffer, but the cleanup called `rmdir (directory.c_str ())` as if
  it were a `std::string` (the sibling `directoryPath` is one). Compilation
  stopped all Linux/OpenBSD/sanitizer lanes, so their CTest suites never ran.
- **Correction:** pass the array directly; keep `mkdtemp`, `unlink`/`rmdir`
  order and the cleanup semantics. Search of every first-party array
  local/member for a member call found no second instance.
- **Files:** `tests/test-i2pcontrol-socket.cpp`.
- **State:** Corrected by static analysis; pending source-documentation validation.

### CE-056 — Undocumented Doxygen parameters (WARN_AS_ERROR)

- **Run/job:** Linux and Android `41232673150`, job `C++ source API
  documentation`.
- **First diagnostic:** `libi2pd_client/I2PService.h:87: error: The following
  parameter of ... CreateStream(...) is not documented: parameter
  'streamRequestComplete'`.
- **Cause:** the first overload documented `dest`/`port` but not the callback;
  the second overload had no `@param` at all. Doxygen with
  `WARN_AS_ERROR=YES` rejects a partially documented function.
- **Correction:** document every parameter of both overloads with the real
  completion/ownership contract. A repository scan of the Doxyfile inputs
  found the same class in `libi2pd/HTTP.h` (stale `@param data`), `Siphash.h`,
  `Config.h`, `Reseed.cpp`, `privsep/Worker.h`, `tools/common/keys.hpp` and
  the Android JNI entry point; all are fixed.
- **Files:** `libi2pd_client/I2PService.h`, `libi2pd/{HTTP.h,Siphash.h,
  Config.h,Reseed.cpp}`, `privsep/Worker.h`, `tools/common/keys.hpp`,
  `android/app/src/main/cpp/i2pd_android.cpp`.
- **State:** Corrected by static analysis; **Pending CI source-docs lane**.

### CE-057 — Android Javadoc rejects raw angle brackets and JDK-internal tags

- **Run/job:** Linux and Android `41232673150`, job `android` (Javadoc).
- **First diagnostic:** `TorrentControl.java:271: error: unknown tag: tunnel`
  and `unknown tag: id`; `TorrentSafCreator.java:81: error: unknown tag.
  Unregistered custom tag?` (`@implNote`).
- **Cause:** a Javadoc line used backticked protocol shape
  `` `TORRENT-DETAIL <tunnel> <id>` ``; javadoc parses `<tunnel>` as HTML.
  `@implNote` is a JDK-internal tag that the configured doclet does not
  register under `-Xdoclint:all`. A third instance (`@dimen/touch_target` at
  the start of a comment line in `UiFactory.java`) was found by the
  repository search.
- **Correction:** place protocol shapes inside `{@code ...}` (which escapes
  them), replace `@implNote` with a plain paragraph, and escape the resource
  reference. New compile-free `android/tools/check-android-javadoc.pl`
  (registered as `android-javadoc-static`, with negative fixtures in
  `tools/tests/maintenance-checkers.t`) rejects the same patterns.
- **Files:** `android/app/src/main/java/org/openi2pd/router/{TorrentControl,
  TorrentSafCreator,UiFactory}.java`, `android/tools/check-android-javadoc.pl`,
  `tests/CMakeLists.txt`, `tools/tests/maintenance-checkers.t`.
- **State:** Corrected by static analysis; **Pending Android CI**.

### CE-058 — `i18n-static` silently skipped without a CTest-time Perl

- **Run/job:** Linux and Android `41232673150` and Windows `41232676139`.
- **Diagnostic:** the test ran in 0.05 s and printed `Perl not found ...
  skipped` while returning success; the native catalog contract never
  executed (the Windows lane separately reported CRLF in the Android
  catalogs that the checker should also have seen).
- **Cause:** `tests/CMakeLists.txt` registered `i18n-static` outside the
  `PERL_EXECUTABLE` block, and `check-i18n.cmake` re-ran `find_program(perl)`
  at test time, where the CTest PATH can differ from the configure step.
- **Correction:** register the test inside the Perl block with
  `-DPERL_EXECUTABLE=${PERL_EXECUTABLE}` and fail closed when the interpreter
  is missing. `tests/check-i18n.cmake` also pins the `.gitattributes` LF
  policy.
- **Files:** `tests/CMakeLists.txt`, `tests/check-i18n.cmake`.
- **State:** Corrected by static analysis; **Pending CI**.

### CE-059 — Checkers assumed POSIX path separators and pipe semantics

- **Run/job:** Windows `41232676139`, MSYS2 `build` job.
- **Diagnostics:** `perl-portability` compared `android\tools\x.pl` with
  `android/tools/x.pl`; `perl-doc-links` reported every valid relative link
  as `(escapes repository)`; `docs-nav-static` reported every page as
  `unlisted Markdown: about\community.md`; `perl-maintenance-checkers` lost
  all child diagnostics because Win32 `select()` only accepts sockets.
- **Cause:** `File::Spec->abs2rel`/`catdir` return native separators while the
  manifests and `Cwd::abs_path` results used `/`; the containment test
  compared the two spellings with a plain prefix match. The checker helper
  drained separate stdout/stderr pipes with `IO::Select`.
- **Correction:** normalise repository-relative paths through one `splitdir` +
  `join '/'` helper (case-folded comparison on Windows), compare canonical
  containment keys with a component boundary, and merge the child's stderr
  into the captured stream instead of using `select`. `perl-portability` also
  stops treating whole-line comments in `.gitignore`/`.dockerignore` as
  invocations.
- **Files:** `tools/scripts/check-doc-links.pl`,
  `tools/scripts/check-doc-nav.pl`, `tools/tests/perl-portability.t`,
  `tools/tests/maintenance-checkers.t`.
- **State:** Corrected by static analysis; **Pending Windows CI**.

### CE-060 — Man pages were not installed on Windows

- **Run/job:** Windows `41232676139`, `perl-install-layout`.
- **Diagnostic:** `i2pdctl man page was not installed in the Tools component:
  .../share/man/man1/i2pdctl.1`.
- **Cause:** every man-page `install(FILES ...)` rule lived inside
  `if(UNIX AND NOT ANDROID)`, so a Windows install had no `share/man` tree
  even though the component contract requires the page.
- **Correction:** guard the rules with `if(NOT ANDROID)` only (an APK never
  installs man pages). Component ownership is unchanged and
  `tests/check-build-system.cmake` now rejects a UNIX-only guard.
- **Files:** `build/CMakeLists.txt`, `tests/check-build-system.cmake`.
- **State:** Corrected by static analysis; **Pending Windows CI**.

### CE-061 — CRLF checkout invalidated the locale policy

- **Run/job:** Windows `41232676139`, `android-locale-policy`.
- **Diagnostic:** `CRLF in android\app\src\main\res\values\strings.xml` and
  nine other files on an unmodified tree.
- **Cause:** the repository had no `*` line-ending rule, so the runner's
  `core.autocrlf` converted the catalogs; the checker correctly rejected the
  result.
- **Correction:** pin `* text=auto eol=lf`, keep `build/build_mingw.cmd` as
  the only CRLF text file, and mark binary payloads explicitly. The checker
  now reports `CRLF line ending in <path>:<line>` once per file.
- **Files:** `.gitattributes`, `android/tools/check-android-locales.pl`,
  `tests/check-i18n.cmake`.
- **State:** Corrected by static analysis; **Pending Windows CI**.

### CE-062 — `keyinfo` printed partial output and failed on an optional batch

- **Run/job:** Windows `41232676139`, `tools-cli` and `tools-regressions`.
- **Diagnostics:** `keyinfo -b non-blindable stdout: expected empty, got:
  <b32>.b32.i2p`; `keyinfo failed (3)` after printing `b33 offline keys: no
  batch in this key file`.
- **Cause:** the default b32 section was printed before `-b` validated
  blindability, and an absent b33 offline-key batch (the normal case for a
  `keygen` file) returned failure.
- **Correction:** validate every section before the first stdout byte, keep
  diagnostics on stderr, treat a missing batch as a successful summary with
  a stderr note, and leave structural corruption a failure. The same
  stdout/stderr split was corrected in `famtool`, `i2pbase64`, `regaddr_3ld`,
  `b33address` and `b33offlinekeys`; `verifyhost`'s verdict and `vain`'s
  historical prefix diagnostic stay on stdout as documented.
- **Files:** `tools/{keyinfo,routerinfo,famtool,i2pbase64,regaddr_3ld,
  b33address,b33offlinekeys}.cpp`, `tools/common/key.hpp`,
  `tools/test-tools{,-cli,-regressions}.cmake`, `docs/tools/*.md`.
- **State:** Corrected by static analysis; **Pending Windows/Linux CI**.

### CE-063 — `routerinfo` applied reserved-range admission to a file listing

- **Run/job:** Windows `41232676139`, `tools-smoke`.
- **Diagnostic:** `routerinfo -6 -p omitted 'NTCP2: 192.0.2.10:12345'`.
- **Cause:** `RouterInfo` parsing filters hosts through
  `transports.IsInReservedRange()`, an admission rule for dialing peers; the
  signed fixture uses documentation ranges, so the listing showed only the
  hash.
- **Correction:** the inspection tool disables that process-local policy
  before parsing (it never starts a transport), and firewall rendering now
  classifies `ValidateEndpoint` rejections: a well-formed non-public endpoint
  is skipped under the public-endpoint policy, while a malformed one is a
  diagnostic and a non-zero exit.
- **Files:** `tools/routerinfo.cpp`, `tools/firewall.hpp`,
  `tools/test-tools.cmake`, `docs/tools/routerinfo.md`.
- **State:** Corrected by static analysis; **Pending Windows/Linux CI**.

### CE-064 — Debian remove check read a 0700 directory without root

- **Run/job:** Packaging `41232685065`, job `Debian packages`.
- **Diagnostic:** `FAIL: remove deleted persistent netDb state under
  /var/lib/i2pd`, although the pre-remove `sudo test -f` had succeeded.
- **Cause:** `/var/lib/i2pd` is mode 0700 owned by `i2pd`; the post-remove
  `[ ! -f ... ]` ran as the unprivileged runner, so permission denied was
  indistinguishable from a missing file. The package's own maintainer
  scripts only remove state on purge (verified against the generated
  `postrm`). No maintainer-script defect exists.
- **Correction:** run the post-remove existence and post-purge directory
  checks through `sudo`; pin both forms in `tests/check-packaging.cmake`.
- **Files:** `.github/workflows/packaging.yml`, `tests/check-packaging.cmake`.
- **State:** Corrected by static analysis; **Pending CI package validation**.

### CE-065 — Doxygen rejected a partially documented function

- **Run/job:** Linux and Android `41248772071`, job `C++ source API
  documentation`.
- **First diagnostic:** `libi2pd/Elligator.h:33: error: The following
  parameters of i2p::crypto::Elligator2::Encode(...) are not documented:
  parameter 'key', parameter 'encoded'` (warning treated as error).
- **Cause:** the doc block named `highY` and `random` but not the two pointer
  parameters. Doxygen reports a function under `WARN_IF_DOC_ERROR` whenever
  some parameters are named and others are not; `WARN_NO_PARAMDOC` is
  disabled, so a block with no `@param` at all is intentionally accepted.
  The previous scan missed the `///` comment form used by this header and
  looked only at `/** ... */` blocks with an already-documented parameter.
- **Correction:** document `key`/`encoded` (and `encoded`/`key` on the
  adjacent `Decode`) and fix the same defect found by a corrected scan:
  `SSU2Session.h` (`ProcessData`), `LeaseSet.h` (`ExtractClientAuthData`),
  `ECIESX25519AEADRatchetSession.h` (`GetSymmKey`), `Crypto.h`
  (`AEADChaCha20Poly1305`, `HKDF`), `AddressFormat.h`
  (`FormatAddressBytes`), `TorrentsState.h` (encode/decode),
  `TorrentsRPC.h` (`AddTunnel`), `TorrentsCreator.h`
  (`SelectTorrentPieceLength`, `CreateTorrentFromPath`),
  `RouterStorage.h` (`Start`), `tui/main.cpp` (`NextCharacter`,
  `TruncateUtf8`, `PutLine`) and `getopt.cpp` (`getopt`, `getopt_long`),
  including the `@param user/pass` form.
- **Regression:** new `tools/scripts/check-doxygen-params.pl`, registered as
  CTest `doxygen-static`, run by the static Linux lane and the workflow Perl
  step, with negative fixtures in `tools/tests/maintenance-checkers.t` and a
  registration pin in `tests/check-build-system.cmake`. It reads the
  `Doxyfile.source` roots, skips `tools/tests`, and reports file, line and
  missing names.
- **Files:** `libi2pd/Elligator.h`, `SSU2Session.h`, `LeaseSet.h`,
  `ECIESX25519AEADRatchetSession.h`, `Crypto.h`, `AddressFormat.h`,
  `libi2pd_client/{TorrentsState.h,TorrentsRPC.h,TorrentsCreator.h}`,
  `privsep/RouterStorage.h`, `tui/main.cpp`, `tools/common/getopt.cpp`,
  `tools/scripts/check-doxygen-params.pl`, `tools/tests/*`,
  `tests/CMakeLists.txt`, `tests/check-build-system.cmake`,
  `.github/workflows/build.yml`, `docs/source-documentation.md`,
  `docs/development/perl-migration.md`.
- **State:** Corrected by static analysis; **Pending source-docs CI**.

### CE-066 — `std::vector` used as a span, plus unused platform locals

- **Run/job:** Linux and Android `41248772071`/`41249881405` (all Linux and
  sanitizer jobs) and OpenBSD `41248777820`.
- **First diagnostic:** `tests/test-privsep-worker-identity.cpp:324: error:
  'const class std::vector<unsigned char>' has no member named 'subspan'`;
  also `warning: unused parameter 'argv0'` (Linux) and `warning: unused
  variable 'buffer'` (OpenBSD).
- **Cause:** the short-nonce case called `subspan` on the vector returned by
  `Nonce()`; `SelfExecutable(argv0)` reads the parameter only in the
  non-Linux branch, and the 4096-byte `buffer` was declared outside the Linux
  `#if` that uses it. One compile error stopped every Linux/OpenBSD/sanitizer
  job before CTest, so the whole matrix was blocked.
- **Correction:** `std::span<const uint8_t> (nonce).subspan (0, 15)` keeps the
  rejected 15-byte nonce; `[[maybe_unused]]` on `argv0`; the buffer moved into
  the Linux branch. The same array/vector-vs-span and platform-branch search
  found no second instance in the test tree.
- **Files:** `tests/test-privsep-worker-identity.cpp`.
- **State:** Corrected by static analysis; **Pending CI compilation**.

### CE-067 — Doxygen rejected duplicate `@param` from declaration + definition

- **Run/job:** Linux and Android `41249881405` (SHA `c1125a75`), job
  `C++ source API documentation`.
- **First diagnostic:** `libi2pd/Crypto.cpp:545: error: argument 'priv' from
  the argument list of i2p::crypto::X25519Keys::SetPrivateKey has multiple
  @param documentation sections`.
- **Cause:** Doxygen merges a member's declaration documentation with its
  definition documentation. After CE-056/CE-065 both `Crypto.h` and
  `Crypto.cpp` documented `priv`/`calculatePublic`; the same shape existed
  for `ParseCmdline` and `LeaseSet2::ExtractClientAuthData`. The static
  checker detected missing parameters but not duplicates.
- **Correction:** keep the parameter contract on the header declaration and
  behavioural prose on the definition; remove the duplicated `@param` blocks.
  `tools/scripts/check-doxygen-params.pl` now also rejects an in-block repeat
  and a definition that repeats a header's parameter docs (scope + name +
  arity match), with fixtures and a registration pin. The `AEADChaCha20Poly1305`
  public function / file-local 10-argument helper pair is the deliberate
  arity witness.
- **Files:** `libi2pd/{Crypto.cpp,Config.cpp,LeaseSet.cpp}`,
  `tools/scripts/check-doxygen-params.pl`, `tools/tests/*`,
  `tests/check-build-system.cmake`, `docs/source-documentation.md`,
  `docs/development/perl-migration.md`.
- **State:** Corrected by static analysis; **Pending source-docs CI**.

### CE-068 — Doxygen rejected an unresolved `@ref` target

- **Run/job:** Linux and Android `41320539612`, job `C++ source API
  documentation`.
- **First diagnostic:** `privsep/DataStorage.cpp:947: error: unable to resolve
  reference to 'mutex' for \ref command`.
- **Cause:** `@ref mutex` and `@ref uid` were used for data members (or plain
  words). Doxygen resolves a `\ref` only to an extracted symbol; `mutex` is a
  data member and `uid` a struct field, so both are reported under
  `WARN_IF_DOC_ERROR` and abort the lane.
- **Correction:** write the two words in backticks and extend
  `tools/scripts/check-doxygen-params.pl` with a reference rule: a target must
  be a class, struct, union, enum, namespace, typedef, using, macro or
  callable collected from the Doxyfile roots, otherwise the checker reports
  `unresolved @ref target` before the manual lane runs. Negative/positive
  fixtures cover a rejected member name and an accepted enum plus callable;
  the registration pin keeps the fixtures under `build-system-static`.
- **Files:** `privsep/DataStorage.cpp`, `privsep/Channel.h`,
  `tools/scripts/check-doxygen-params.pl`, `tools/tests/maintenance-checkers.t`,
  `tests/check-build-system.cmake`, `docs/source-documentation.md`,
  `docs/development/perl-migration.md`, `AGENTS.md`.
- **State:** Corrected by static analysis; **Pending source-docs CI**.

### CE-069 — `std::string` passed to a byte-span parameter

- **Run/job:** Linux and Android `41320539612` (gcc, clang, OpenSSL 3.x,
  OpenSSL 4.0.0/4.0.3), OpenBSD `41320545934` and the sanitizer jobs.
- **First diagnostic:** `tests/test-datastorage-protocol.cpp:188: error:
  could not convert 'payload' from 'const std::string' to
  'std::span<const unsigned char>'` (also lines 201..208, including
  temporaries).
- **Cause:** `EncodeDataStorageRequest` deliberately takes a byte span
  (`privsep/Protocol.h:921`), but the test passed `std::string` values.
  `std::span` has no conversion from a `char`-element contiguous range, so
  every payload argument was ill-formed. This blocked all Linux, OpenBSD and
  sanitizer targets before CTest.
- **Correction:** a file-local borrowing helper `Bytes(const std::string &)`
  reinterprets the characters as `const uint8_t *` and keeps the full-expression
  lifetime; every payload site uses it, including the two size-cap
  temporaries. Names (already `std::string_view`) and the empty removal span
  are unchanged. A tree-wide scan of the 104 byte-span declarations found no
  other string-to-span call site.
- **Files:** `tests/test-datastorage-protocol.cpp`.
- **State:** Corrected by static analysis; **Pending CI compilation**.

### CE-070 — `SHA256()` out-parameter written through a const view

- **Run/job:** Android `41359794983`; sanitizers `41359794983` (asan-ubsan,
  tsan, ubsan); install components; OpenBSD `41359799518`; Fuzzing
  `41359802036`; Packaging `41359804259` (Debian, static musl, container);
  Windows MSYS2 `41359797195`.
- **SHA:** `4f76f65f`.
- **First diagnostic:** `libi2pd/RouterInfo.cpp:1259: error: no matching
  function for call to 'SHA256'` (Clang/NDK) and `invalid conversion from
  'const uint8_t*' to 'unsigned char*'` (GCC); the candidate is OpenSSL
  `SHA256(const unsigned char*, size_t, unsigned char*)`.
- **Cause:** `RouterInfo::SaveToArtifact` hashed into
  `IdentHash::data()`. `Tag::data()` is a const-only accessor, so the third
  argument was `const uint8_t*`; the non-const implicit conversion exists on
  the `Tag` object but not on `.data()`. This blocked every target that
  compiles `libi2pd`, and hidden later diagnostics remain unverified.
- **Correction:** hash into a local `uint8_t[IdentHash::len]` and compare
  with `std::memcmp`; `<cstring>` is included directly.
- **Files:** `libi2pd/RouterInfo.cpp`.
- **State:** The correction is present at current SHA `56f17c0f`. Linux/OpenSSL 3/4, Android arm64 native, Windows MSYS2/MSVC and OpenBSD builds all compiled the affected library successfully on that SHA, so this translation-unit compile error is **Validated by CI compilation**. The failed Fuzzing run did not reach libFuzzer target construction; fuzz target/link/smoke validation remains pending.

### CE-071 — CMake script used `while(TRUE)` under the old policy

- **Run/job:** Packaging `41359804259`, `Service contracts`, on the Debian
  package's CMake 3.x.
- **SHA:** `4f76f65f`.
- **First diagnostic:** `tests/check-build-system.cmake:634 (message):
  tests/CMakeLists.txt registers no tests`, after
  `while() given incorrect arguments: "TRUE"` and the CMP0012 warning.
- **Cause:** a script run with `cmake -P` has no
  `cmake_minimum_required()`; with CMP0012 unset, `while(TRUE)` treats
  `TRUE` as an undefined variable, so the registration scan never ran. The
  local CMake 4.4 defaults the policy to NEW, which is why static runs did
  not reproduce it.
- **Correction:** use `while(1)` in both scans and declare
  `cmake_policy(SET CMP0012 NEW)` in the standalone scripts that branch on a
  boolean-valued variable (`check-build-system`, `check-binary-version` and
  `test-tools-regressions`). Without the policy, a TRUE-valued variable is
  re-read as an undefined variable name under a CMake 3.x host, which would
  silently skip the fixed-temporary-path check or invert an optional-banner
  or positive-tool expectation.
- **Files:** `tests/check-build-system.cmake`, `tests/check-binary-version.cmake`,
  `tools/test-tools-regressions.cmake`.
- **State:** Corrected by static analysis; **Pending CI tests**.

### CI-003 — branch verifier lost the Homebrew environment

- **Run/job:** Linux OpenSSL 3.x `41359789894` and 4.x `41359792345`,
  both compiler jobs.
- **SHA:** `4f76f65f`.
- **First diagnostic:** `Homebrew formula is not installed: openssl@3`
  (and `openssl@4`) immediately after the toolchain job logged
  `openssl@3 3.5.9` / `openssl@4 4.1.0`.
- **Cause:** `brew-toolchain` evaluates `brew shellenv` only inside its own
  step. The follow-up composite action called plain `brew`, which is not on
  the runner PATH without that evaluation; stderr was suppressed and the
  empty version was classified as a missing formula. The build never ran.
- **Correction:** resolve `/home/linuxbrew/.linuxbrew/bin/brew` with a PATH
  fallback, fail explicitly when it is absent, and use that binary for the
  version query.
- **Files:** `.github/actions/linux-openssl-build/action.yml`.
- **State:** Corrected by static analysis; **Pending CI tests**.

### CI-004 — workflow split broke compile-free consumers and pipefail checks

- **Run/job:** `4f76f65f` and the split revision; Windows MSVC
  `41359797195` completed with only CE-070, confirming no second MSVC
  compile error at that point.
- **Cause and corrections:**
  - `tests/check-tools-inventory.cmake` still read the deleted
    `.github/workflows/build.yml`; the `tools-inventory` CTest and the
    `skills-static` step would fail before any build. It now reads
    `build-linux.yml`, where the install tool loop lives.
  - `tools/tests/perl-portability.t` listed `build.yml` and `fuzz.yml` in
    its Python-consumer scan; both are gone. The list now names
    `build-linux.yml`, `build-android.yml`, `validation.yml` and the two
    OpenSSL branch workflows.
  - APK assertions piped multi-megabyte `unzip`/`nm` output into `grep -q`
    under `pipefail`; the producer's `SIGPIPE` could report a valid APK as
    broken. Extract to a file or use a here-string.
  - `brew outdated --quiet "$package" | grep -qx` could skip the bottle the
    branch lane exists to test, and swallowed a real `brew outdated`
    failure. Capture the list, check its status, then compare names.
  - The merged fuzz job kept `timeout-minutes: 60` while its step budgets
    sum to 90, so a slow healthy run could be cancelled as a fuzz failure.
    Raised to 90.
  - `check-workflows.pl` now validates that every `inputs.NAME` reference is
    declared, parses job ownership from the real `jobs:` block, and pins the
    `android_ui`/`perl_style` opt-in gates.
  - The mandatory Android Javadoc gained its compile-free
    `check-android-javadoc.pl` counterpart, and the Windows
    `i2pd-tui.exe --version` probe is bounded like the router probe.
- **Files:** `tests/check-tools-inventory.cmake`,
  `tools/tests/perl-portability.t`, `.github/workflows/{build-android,
  build-windows,validation}.yml`, `.github/actions/brew-toolchain/action.yml`,
  `tools/scripts/check-workflows.pl`.
- **State:** Corrected by static analysis; **Pending CI tests**. No build was
  performed.

## Current HEAD batch `56f17c0f` (2026-11-02)

### CE-072 — Android Java callbacks did not compile

- **Run/job:** Android [41364864089](https://github.com/daviduhden/openi2pd/actions/runs/41364864089), job `125022760915`.
- **SHA/toolchain:** `56f17c0f47a3d1a328be6c54df7b661c480924f8`; Temurin JDK 25.0.5, Gradle 9.8.1, AGP 9.4.1, NDK r30.
- **Diagnostics:** `RouterService.java:2183` captured a reassigned local `sink`; `AddressBookFragment.java:411` redeclared `expected` in `blockedDialog()`.
- **Cause/correction:** callback values were not effectively final and a nested async generation variable shadowed the outer guard. Capture the validated sink in a final `LongConsumer` and name the inner token `unblockGeneration`.
- **Related pattern:** review nested Java callbacks for effectively-final captures and distinct generation tokens; see the Android skill and regression pattern.
- **State:** corrected by static analysis; **Pending CI compilation**. Gradle/JNI/native/APK/Lint/device steps did not run after Javac failed.

### CE-073 — current sanitizer and platform batch has multiple independent blockers

- **Runs:** Linux/OpenSSL 3 [41364855256](https://github.com/daviduhden/openi2pd/actions/runs/41364855256), Linux/OpenSSL 4 [41364858202](https://github.com/daviduhden/openi2pd/actions/runs/41364858202), Validation [41364867079](https://github.com/daviduhden/openi2pd/actions/runs/41364867079), Windows [41364870388](https://github.com/daviduhden/openi2pd/actions/runs/41364870388), OpenBSD [41364873438](https://github.com/daviduhden/openi2pd/actions/runs/41364873438). All ran the SHA above.
- **Evidence:** Linux GCC/Clang and OpenBSD compiled and linked before CTest failures; the Android Javac errors are CE-072. ASan/UBSan reported an EdDSA stack-buffer underflow and invalid enum values; details and remaining test/runtime diagnostics are in `RUNTIME-ERRORS.md` and `TEST-FAILURES.md`.
- **Platform detail:** Android used Temurin 25.0.5, Gradle 9.8.1, AGP 9.4.1 and NDK r30; native arm64 completed before Javac stopped the job. Windows MSVC 19.44 and MSYS2/MinGW completed compilation/linking, then failed CTest (four of eight MSVC tests, four of 214 MSYS2 tests). OpenBSD 8.0 Clang 22.1.6 / LibreSSL 4.4.0 built all targets before 11 of 220 CTests failed. Linux GCC/Clang OpenSSL 3.x and OpenSSL 4.x likewise compiled/linked before test failures. Successful Linux and packaging jobs on this SHA are separate evidence and do not validate those failing lanes.
- **Static corrections in this worktree:** the two parser-result fixture lifetime defects are fixed by keeping their input strings alive; UDP datagram-version configuration is validated before enum conversion; transport connector lookup accepts a raw integer so rejection tests do not construct invalid enum values. These changes are not present in the run SHA.
- **State:** partially corrected by static analysis; **Pending CI compilation** and **Pending CI tests**. The EdDSA sanitizer finding and remaining current CTest failures still need cause-specific investigation.

## Published revision reviewed on 2026-11-07

Exact SHA: `14f224e6c7776619c13449759523a87298d4f63a`.
The accessible historical snapshot contains 272 runs across 53 SHAs and 16
workflow names: 177 failures, 63 cancellations and 32 successes. Previous
cached evidence and the eight new full logs were reviewed together. Full logs
are available for 269 runs; run 39058430980 is partial (job 117472356821
missing), and 39058841124 / 39058346321 are unavailable. Retention gaps are
not evidence of success. Raw logs and downloaded artifacts remain outside Git.

| Run | Jobs and observed result |
|---|---|
| [42402993215](https://github.com/daviduhden/openi2pd/actions/runs/42402993215) | OpenSSL 3 Clang 128494612836 / GCC 128494613099: CE-078 |
| [42402995976](https://github.com/daviduhden/openi2pd/actions/runs/42402995976) | OpenSSL 4 Clang 128494620750 / GCC 128494620947: CE-078 |
| [42403004301](https://github.com/daviduhden/openi2pd/actions/runs/42403004301) | TSan 128494644892, UBSan 128494644962, ASan/UBSan 128494645005: CE-078; fuzz 128494644758, static 128494644874 and docs 128494644919 succeed; Perl quality skipped |
| [42402987408](https://github.com/daviduhden/openi2pd/actions/runs/42402987408) | OpenBSD 128494594640 builds, then three of 245 CTests fail |
| [42402984632](https://github.com/daviduhden/openi2pd/actions/runs/42402984632) | MSVC 128494586125 and MinGW 128494586377 build, then fail tests |
| [42403001540](https://github.com/daviduhden/openi2pd/actions/runs/42403001540) | Install consumer 128494636785 fails CE-079; interoperability 128494637153 succeeds |
| [42402998875](https://github.com/daviduhden/openi2pd/actions/runs/42402998875) | Android 128494629319 succeeds; API 29 UI 128494630663 skipped |
| [42402990302](https://github.com/daviduhden/openi2pd/actions/runs/42402990302) | All five packaging/service/component jobs succeed; CW-022 archive inspection below |

Uploaded configure logs identify GCC 16.2.0, Clang 23.1.5, OpenSSL 3.5.9 and
OpenSSL 4.1.0. Thus the floating OpenSSL 4 lane does not establish coverage of
the declared 4.0.x floor. OpenBSD uses Clang 22.1.6 / LibreSSL 4.4.0.
No result in this table validates the subsequent local corrections.

### CE-078 — Router storage regression omitted its filesystem header

`tests/test-router-storage.cpp:73`: `i2p::fs` is undeclared in all four
OpenSSL jobs and all three sanitizer jobs above. Added the direct `FS.h`
include after checking other test consumers for the same transitive-header
assumption. Source contract prepared. Implemented / static; Pending CI
compilation and Pending CI tests. Sanitizers never reached execution.

### CE-079 — Consumer asserted an obsolete changelog filename

Linux install consumer 128494636785 expected `share/doc/i2pd/ChangeLog`.
The exact-run install artifact contains the canonical lowercase `changelog`.
Corrected the workflow assertion; reviewed package/static-bundle consumers
without renaming their independently defined payloads. Implemented / static;
Pending CI tests. This is an install assertion, not a linker failure.

### CE-080 — Raw cleanup source guard rejected an equivalent RAII owner

Local compile-free control-security review required literal `BN_clear_free
(r)`, rejecting the new `SecretBN` owner. Updated the guard to require its
explicit secure deleter and owned variable instead. Source guard passes;
this was not an observed compiler diagnostic. Runtime validation remains
pending with RT-038.

## Published revision reviewed after 2026-11-07 dispatch

SHA `68c16e10a72c70da948a3f230acc07b2800d44ac` contains the preceding static
corrections. The refreshed paginated history has 281 runs / 54 SHAs / 16
workflow names: 183 failures, 63 cancellations and 35 successes. This extends,
rather than replaces, the previous historical evidence and retention gaps.
Complete logs from successful siblings and failed jobs were reviewed.

| Run | Exact jobs / result |
|---|---|
| [42414224563](https://github.com/daviduhden/openi2pd/actions/runs/42414224563) | OpenSSL 3 Clang 128528708995 / GCC 128528709468 compile/link, fail Perl portability and Linux IPC assertions |
| [42414227604](https://github.com/daviduhden/openi2pd/actions/runs/42414227604) | OpenSSL 4 Clang 128528716996 / GCC 128528717182 compile/link, same CTest failures |
| [42414235478](https://github.com/daviduhden/openi2pd/actions/runs/42414235478) | TSan 128528743055, ASan/UBSan 128528743083, UBSan 128528743120 compile/link and execute CTest; runtime inventory applies |
| Same Validation run | static 128528742886, fuzz 128528743116 and docs 128528743145 pass; optional Perl quality 128528743772 skipped |
| [42414218464](https://github.com/daviduhden/openi2pd/actions/runs/42414218464) | OpenBSD 128528690422 compiles/links; 245 CTests, only Perl portability fails; three capability skips |
| [42414215883](https://github.com/daviduhden/openi2pd/actions/runs/42414215883) | MSVC 128528682366 / MinGW 128528682471 compile/link, fail runtime tests |
| [42414232964](https://github.com/daviduhden/openi2pd/actions/runs/42414232964) | install consumer 128528734278 and interoperability 128528734554 pass |
| [42414230020](https://github.com/daviduhden/openi2pd/actions/runs/42414230020) | Android 128528724653 passes with 17 Lint warnings; device job 128528725623 skipped |
| [42414221469](https://github.com/daviduhden/openi2pd/actions/runs/42414221469) | static 128528698923, services 128528699048, components 128528699079, Debian 128528699193 and container 128528699229 pass |

CE-078 direct-header blocker is resolved in these executed configurations;
sanitizers now run. CE-079's canonical changelog assertion passes in the exact
install-consumer job. CE-080's source guard passes in the executed static lane.
No new project compiler warning or compile/link blocker was found in the
reviewed SSL/Validation logs. This is not a claim that all optional targets or
toolchain floors were exercised. Local follow-up changes remain Pending CI
compilation and Pending CI tests, with no result attributed to them.

### CE-081 — Local redaction guard confused host names with source URLs

A new compile-free guard initially banned every logged variable named
`address`, including address-book hostnames rather than source URLs. Narrowed
that rule to Reseed's source operands; subscription/outproxy guards pin their
actual URL members and all guards span multiline calls. This is a corrected
local source-contract failure, not a compiler diagnostic. The source guard
passes after correction; dynamic redaction validation remains pending.

## 2026-11-08 — Published audit follow-up

The nine completed batch runs below executed
`8ffd1ad822de9e893f4116496ffc358fb0571019`, not the later local policy or
follow-up changes. The paginated snapshot contains 291 runs, 55 SHAs and 17
workflow names: 189 failures, 63 cancellations and 39 successes. A subsequent
focused run is recorded separately below. Previous missing/partial historical
logs remain evidence gaps; the new batch does not replace that history.

| Run | Exact jobs and result |
|---|---|
| [42419357644](https://github.com/daviduhden/openi2pd/actions/runs/42419357644) | MSVC 128544374044 passes selected tests; MinGW 128544374249 fails Unicode contract and GUI version probe |
| [42419360397](https://github.com/daviduhden/openi2pd/actions/runs/42419360397) | OpenBSD 128544246065 builds and completes 246 CTests without failures; capability skips remain |
| [42419362861](https://github.com/daviduhden/openi2pd/actions/runs/42419362861) | All five packaging/service/component jobs pass |
| [42419365560](https://github.com/daviduhden/openi2pd/actions/runs/42419365560) | OpenSSL 3 Clang 128544260702 / GCC 128544260852 build; each fails only Linux IPC among 251 tests |
| [42419367524](https://github.com/daviduhden/openi2pd/actions/runs/42419367524) | OpenSSL 4 GCC 128544267385 / Clang 128544267552 build; same Linux IPC failure |
| [42419370092](https://github.com/daviduhden/openi2pd/actions/runs/42419370092) | Android 128544275653 passes with 17 Lint warnings; API 29 UI 128544276582 skipped |
| [42419372239](https://github.com/daviduhden/openi2pd/actions/runs/42419372239) | Install 128544281668 and interop-reference 128544281791 pass |
| [42419375465](https://github.com/daviduhden/openi2pd/actions/runs/42419375465) | ASan/UBSan 128544291546, TSan 128544291559 and UBSan 128544291639 build then fail CTest; docs 128544291347, fuzz 128544291525 and static 128544291588 pass; Perl quality 128544292248 skipped |
| [42419378836](https://github.com/daviduhden/openi2pd/actions/runs/42419378836) | Instrumented crypto 128544301805 builds OpenSSL, stops at project configure (CE-082) |

No new first-party compiler warning was found in the reviewed OpenBSD,
OpenSSL branch and Linux logs. CTest failures are classified below rather
than misreported as compilation failures. Toolchain floors, skipped paths
and unexecuted local changes are not certified by these observations.

### CE-082 — Instrumented dependency requires runtime in configure probes

Crypto run 42419378836 stops at CMakeLists.txt:545,
`I2PD_OPENSSL_LINK_OK`: the selected modern API surface fails to link.
The exact uploaded CMakeConfigureLog shows the probe linked instrumented
libcrypto/libssl without `-fsanitize=thread`, producing undefined `__tsan_*`
symbols. Target sanitizer options are installed later than this probe.

Set the diagnostic executable linker flags to include the runtime, retaining
the API probe. The externally published correction is in
`9f5320b2409003cf05df3b1d8968a7a3ebe6949e`; this audit did not create that
commit. Focused run [42422405960](https://github.com/daviduhden/openi2pd/actions/runs/42422405960),
job 128553458708, configures, builds and passes 1/1 tests on that exact SHA.
The full matrix's shared dependency integration and macro patch remain local:
Pending CI compilation and Pending CI tests.

Including the focused run, the snapshot is 292 runs / 56 SHAs, with 40
successes, 189 failures and 63 cancellations. Later local fixes are not
validated by that success.

### CE-083 — Nested requires clause confused a documentation checker

Local source validation initially interpreted a nested requires-expression
as the documented function's parameter list. Naming the byte/word source
concepts makes the contract reusable and avoids that ambiguity without
weakening its exact return-type constraints. Doxygen parameter source checks
pass after the refactor. This is a source-tool limitation, not a compiler
error; broader nested-requires recognition remains a checker improvement.

### CE-084 — Composite extraction retained a malformed workflow tail

Publication review found the new TSan action retained a duplicated workflow
tail after its intended final output. Substring guards had passed despite
invalid YAML. Removed only the duplicated tail; focused project build and
artifact steps remain in the owning workflow. YAML parsing and actionlint
now pass. This was a local source/configuration defect, not an observed CI
failure. The CI skill now requires complete extraction-boundary review and
YAML parsing alongside source guards. Dynamic execution remains pending.

## 2026-11-08 — Executed continuation on c115d386

All ten runs below executed `c115d3864f2f0ce9f899d79ae48edf8e15d605c7`.
The complete paginated snapshot is 302 runs / 57 SHAs / 18 workflow names:
194 failures, 63 cancellations and 45 successes. Full logs and failed-step
logs were retrieved for every new run, including successful siblings.
Historical retention gaps remain; no new gap was found in this batch.

| Run | Jobs and observed result |
|---|---|
| [42426220784](https://github.com/daviduhden/openi2pd/actions/runs/42426220784) | MSVC 128564962837 passes; MinGW 128564963017 fails tools, tracker scheduling, Unicode and version timeout |
| [42426222299](https://github.com/daviduhden/openi2pd/actions/runs/42426222299) | OpenBSD 128564966656 builds; tools and tracker scheduling fail among 246 tests |
| [42426226746](https://github.com/daviduhden/openi2pd/actions/runs/42426226746) | OpenSSL 3 Clang 128564979659 / GCC 128564979810 build; each fails tools, tracker scheduling and Linux IPC among 251 tests |
| [42426228294](https://github.com/daviduhden/openi2pd/actions/runs/42426228294) | OpenSSL 4 Clang 128564984956 / GCC 128564985102 build; same three CTest failures |
| [42426232908](https://github.com/daviduhden/openi2pd/actions/runs/42426232908) | TSan 128564999538, ASan/UBSan 128564999594 and UBSan 128564999718 build; same three failures; docs 128564999749, static 128564999721 and fuzz 128564999658 pass; Perl quality 128565001193 skipped |
| [42426234142](https://github.com/daviduhden/openi2pd/actions/runs/42426234142) | Instrumented crypto 128565003226 passes its focused test |
| [42426229970](https://github.com/daviduhden/openi2pd/actions/runs/42426229970) | Android 128564989720 passes; API 29 UI 128564990988 skipped |
| [42426231401](https://github.com/daviduhden/openi2pd/actions/runs/42426231401) | Install 128564994129 and reference interop 128564994367 pass |
| [42426223641](https://github.com/daviduhden/openi2pd/actions/runs/42426223641) | Static package 128564970772, services 128564970915, Debian 128564970939, container 128564970941 and component matrix 128564970995 pass |
| [42426225051](https://github.com/daviduhden/openi2pd/actions/runs/42426225051) | Knowledge gate 128564974440 passes |

No new compile/link blocker appears in these exact logs. The selected
libraries are OpenSSL 3.5.9 / 4.1.0 and LibreSSL 4.4.0; unavailable LibreSSL
feature probes select compatibility paths rather than proving a source error.
Explicit OpenSSL 4.0.x coverage remains unexecuted. Successful compilation
must not be confused with the runtime failures below or MSVC CW-026.

## 2026-11-08 — Retained history and c3f5df53 continuation

The paginated API inventory now contains 312 runs: 49 successes, 199 failures,
63 cancellations and one in-progress run. It includes 15 historical workflow
IDs and ten current workflows. All 312 log requests were attempted: 290
nonempty logs were retained; 22 were empty, unavailable or incomplete. An
empty response is an evidence gap, not proof of expiration or success.
Raw metadata and retrieval status are outside the repository under
`/tmp/openi2pd-ci/history-nov8`. Some large artifact downloads exhausted the
temporary filesystem; exact Windows and Android reports were recovered
separately. Initial sanitizer/debug-image gaps were subsequently resolved
under /var/tmp, as recorded below.

All latest runs below use `c3f5df5330cbaa8e07176886a7105869fa30ae83`.
Local i18n, documentation and current fixes are not validated by these runs.

| Run | Exact jobs and result |
|---|---|
| [42431903303](https://github.com/daviduhden/openi2pd/actions/runs/42431903303) | TSan 128582436615, UBSan 128582436820 and ASan/UBSan 128582436891 fail tracker/IPC assertions; docs 128582436832, static 128582436835 and fuzz 128582436741 pass; Perl quality 128582436852 fails Critic after passing 1230 tests |
| [42431822974](https://github.com/daviduhden/openi2pd/actions/runs/42431822974) | OpenSSL 3 GCC 128582202338 and Clang 128582202574 build, then fail tracker/IPC tests |
| [42431807205](https://github.com/daviduhden/openi2pd/actions/runs/42431807205) | OpenSSL 4 GCC 128582150723 and Clang 128582151077 build, then fail the same tests |
| [42431852556](https://github.com/daviduhden/openi2pd/actions/runs/42431852556) | OpenBSD 128582289517 builds, then fails tracker scheduling |
| [42431789062](https://github.com/daviduhden/openi2pd/actions/runs/42431789062) | MSVC 128582094948 passes ordinary tests but fails heap diagnostics; MinGW 128582095295 fails tracker, Unicode validator and GUI version tests |
| [42431876464](https://github.com/daviduhden/openi2pd/actions/runs/42431876464) | Android build 128582358937 succeeds; API 29 UI job remains incomplete in the retrieved snapshot |
| [42431921178](https://github.com/daviduhden/openi2pd/actions/runs/42431921178) | Reference interoperability 128582491723 and installed consumer 128582491902 pass |
| [42431888475](https://github.com/daviduhden/openi2pd/actions/runs/42431888475) | Packaging/service jobs 128582393551, 128582393734, 128582393757, 128582393821 and 128582393822 pass |
| [42431861964](https://github.com/daviduhden/openi2pd/actions/runs/42431861964) | Instrumented crypto TSan 128582316585 passes its focused suite |
| [42431928876](https://github.com/daviduhden/openi2pd/actions/runs/42431928876) | Agent knowledge workflow succeeds |

No new compile/link blocker was found in this batch. This does not close
runtime failures, CW-027/CW-028, Android Lint debt or platform coverage gaps.

Per-job fallback resolved 19 of the 22 empty run-log responses: 309 of the
312 retained runs now have log evidence (290 run logs plus 19 recovered job
sets). The earliest run, 39057879349 on 2026-10-17, is recoverable by job ID.
The three remaining gaps are in-progress Android 42431876464 and canceled
runs 39058841124 / 39058346321. Their job endpoints report `log not found`;
the missing-log diagnostic applies to those canceled runs, while Android is
incomplete. No unsupported retention-expiry explanation is inferred. Recovered run
40594540028 has 12 nonempty job logs and three explicitly skipped jobs.

All three latest sanitizer archives and the focused instrumented OpenSSL
archive were recovered under `/var/tmp/openi2pd-ci-history-nov8`. They include
retained executable images, configure/test logs, hashes and exact revision
manifests. The sanitizer configurations are Clang 23.1.5, OpenSSL 4.1.0 and
Debug; this is not explicit OpenSSL 4.0.x coverage. Their LastTest.log files
confirm only TF-089/TF-090, without sanitizer-report markers. Focused XML
records one test and zero failures. No downloaded executable was run locally.
