<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: compilation error inventory.

Tracks compile/configure/link failures reported by manual GitHub Actions.
-->

# Compilation error inventory

This inventory links each confirmed manual-CI failure to its first diagnostic,
root cause, local correction and validation state. A local correction is not a
CI result: rows stay **Pending CI compilation** until a manual run on the
corrected revision passes.

## Runs reviewed

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
- **Cause:** `basic_waitable_timer` exposes `cancel()`/`cancel_one()` but no
  `error_code` overload on the supported Boost range. The call compiled only
  where the template was not instantiated before the earlier errors stopped
  the build.
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
- **State:** Corregido mediante análisis estático; **Pending CI tests**. The
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
- **State:** Corregido mediante análisis estático; **Pending CI tests**. JNI
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
- **State:** Corregido mediante análisis estático; **Pending CI tests**.

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
- **State:** Corregido mediante análisis estático; **Pending CI tests**.

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
- **State:** Corregido mediante análisis estático; **Pending CI tests**.
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
- **State:** Corregido mediante análisis estático; **Pending CI tests**.

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
