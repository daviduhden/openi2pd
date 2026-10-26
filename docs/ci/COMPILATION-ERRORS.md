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
| 40411690766 | Linux and Android | `88ac1e92` | failure (several jobs) |
| 40411699490 | OpenBSD | `88ac1e92` | failure |
| 40411695544 | Windows | `88ac1e92` | failure (MinGW and MSVC jobs) |
| 40411703834 | Fuzzing | `88ac1e92` | failure |
| 40411709057 | Packaging and service validation | `88ac1e92` | failure |

Earlier runs (`b9cbdaa8`, `a2b8dc48`, `2ae36284`, `d5d0bd6f`) were cancelled
or failed for older trees; they are historical evidence only and do not
validate the local corrections.

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
