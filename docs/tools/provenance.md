<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: provenance.

Documents project documentation.

-->

# Tool provenance and inventory

The native utilities in `tools/` are adapted from
[PurpleI2P/i2pd-tools](https://github.com/PurpleI2P/i2pd-tools) at pinned
commit `ca7ece8da86aa80b58fce6fa64375bfd75cbf843`. They are not a Git
submodule, do not fetch an external router checkout and do not use a separate
Makefile. The source is maintained in this repository and links the in-tree `libi2pd`
library; `autoconf_i2pd` additionally links `libi2pdlang`.

The imported-file manifest is
[`contrib/upstream/tools-files.txt`](../../contrib/upstream/tools-files.txt).
`contrib/upstream/sync.sh --tools` clones the pinned upstream commit into a
temporary directory and prints diffs for manual review. It never overwrites
the working tree.

## Licence and notices

- Upstream tool provenance is recorded in [`tools/LICENSE`](../../tools/LICENSE),
  the import manifest and repository history. Project-standard source headers
  point to the repository `LICENSE` instead of duplicating copyright/SPDX
  boilerplate.
- `tools/LICENSE` is the upstream BSD-3-Clause text. Its copyright-holder field
  is empty and clause 3 names `i2pd-reseed`, both inherited from the import;
  the fork records this rather than inventing a holder or silently rewriting
  the notice. See `docs/attribution-audit.md`.
- Third-party files that carry their own license terms or attribution
  requirements retain those notices.
- New compatibility, validation, test and CMake files use the same short
  `LICENSE` reference and a file-specific description.
- The installed tools do not bundle third-party fonts, browser assets or
  downloaded binaries.

## Native executable inventory

| Executable | Source | Main test coverage |
|---|---|---|
| `autoconf_i2pd` | `tools/autoconf_i2pd.cpp` | `tools-regressions` scripted non-interactive transcript |
| `b33address` | `tools/b33address.cpp` | `tools-regressions`: Ed25519/RedDSA, invalid/oversized stdin and non-zero failures |
| `b33offlinekeys` | `tools/b33offlinekeys.cpp` | `tools-regressions`: day bounds, output parseability, read-only key rejection |
| `famtool` | `tools/famtool.cpp` | `tools-regressions`: generation, family-name bounds, negative options |
| `i2pbase64` | `tools/i2pbase64.cpp` | smoke and regressions: binary, UTF-8, NUL, I2P alphabet and malformed input |
| `keygen` | `tools/keygen.cpp` | smoke and regressions: Ed25519, RedDSA and P-256; unsupported generators and `--help` fail without writing |
| `keyinfo` | `tools/keyinfo.cpp` | regressions: all output modes, offline batches and malformed keys |
| `offlinekeys` | `tools/offlinekeys.cpp` | regressions: defaults, bounds, output parsed by `keyinfo` |
| `regaddr` | `tools/regaddr.cpp` | regressions plus `verifyhost` cross-check |
| `regaddr_3ld` | `tools/regaddr_3ld.cpp` | three-step records plus `verifyhost` cross-check |
| `regaddralias` | `tools/regaddralias.cpp` | alias records plus `verifyhost` cross-check |
| `routerinfo` | `tools/routerinfo.cpp` | `test-routerinfo-firewall` (iptables/ip6tables, nftables comments, Windows stable names and PF labels), `tools-smoke` CLI conflicts and legacy `-f` |
| `vain` | `tools/vain.cpp` | regressions: CLI bounds only; no long search |
| `verifyhost` | `tools/verifyhost.cpp` | regressions: valid, tampered and malformed records |
| `x25519` | `tools/x25519.cpp` | `tools-smoke`: key-pair output and unknown-option rejection |

`tools/scripts/i2pdctl` is the Perl I2PControl client. It is installed with the
same `Tools` component and has separate syntax and `prove` checks. It is not a
native executable and does not use the tool key format. Upstream shipped this
client in Python; the Perl 5 rewrite is a fork addition and is therefore not an
upstream manifest entry.

## Fork-only shared files

These files are maintained for the integrated tree and are not upstream
imports:

- `tools/common/file.hpp` — bounded reads, private-by-default writes and
  secret-buffer wiping.
- `tools/common/getopt.cpp` / `getopt.h` — MSVC-compatible `getopt`/
  `getopt_long` surface.
- `tools/firewall.hpp` — endpoint model and firewall renderers.
- `tools/common/keys.hpp` — one load/store authority for destination key files
  (exact consumption, durable private replacement, same-file refusal).
- `tools/common/crypto.hpp` — RAII crypto lifetime for key-generating tools.
- `tools/CMakeLists.txt`, `tools/test-tools*.cmake`,
  `tools/test-perl-install.cmake`, `tools/tests/*.t` — CMake/CTest integration.
- `tools/scripts/check-doc-links.pl`, `tools/scripts/check-doc-nav.pl`,
  `tools/scripts/validate-branding.pl` — offline repository checks.
- `android/tools/check-android-locales.pl`,
  `android/tools/check-android-jni-symbols.pl` and
  `i18n/check-i18n-catalogs.pl` — compile-free Android resource/JNI and
  native-catalog contracts.
- `tools/tests/routerinfo-fixture.cpp` — generates the signed RouterInfo used
  by the manual tool tests; no keys or binary fixture are checked in.

## Build and installation

All 15 native targets are created by `tools/CMakeLists.txt` with C++23 enabled
and are installed to `CMAKE_INSTALL_BINDIR` under the `Tools` component when
`I2PD_BUILD_TOOLS=ON` (the default). `LICENSE.i2pd-tools` is installed to
`CMAKE_INSTALL_DOCDIR` in the same component. `I2PD_BUILD_TOOLS=OFF` removes both the
targets and the tool tests. The router, TUI, libraries and tools are configured
by the same CMake graph; the POSIX Make front-end only invokes CMake/Ninja.

CTest registers:

- `tools-inventory` — every built native artifact exists.
- `tools-inventory-static` — CMake/docs/packaging inventory parity (compile-free).
- `tools-cli` — exact exit codes, stream separation and same-file refusal.
- `tools-getopt` — host regression for the MSVC getopt shim.
- `tools-smoke` — basic key, base64, registration, offline-key and CLI checks.
- `tools-regressions` — per-tool deterministic checks plus the scripted
  `autoconf_i2pd` transcript.
- `perl-i2pdctl-syntax`, `perl-i2pdctl`, `perl-portability`,
  `perl-maintenance-checkers`, `perl-validate-branding`,
  `android-jni-symbols-static`, `i18n-static` and `perl-install-layout` when
  Perl is available
  (`perl-i2pdctl` uses `prove` when present and otherwise runs the TAP file
  directly); `perl-critic` is opt-in with `-DI2PD_PERL_CRITIC=ON`.

No test contacts the I2P network, starts a long vanity search or uses a
download step. The pinned upstream commit was reviewed when the manifest was
created and has not been re-verified since; `sync.sh --tools` performs the
comparison only when a maintainer invokes it.

## Security review boundaries

- Input files are bounded to 16 MiB. Key outputs are written with owner-only
  permissions on POSIX and best-effort owner-read/write permissions on Windows.
- `b33address` bounds stdin before decoding.
- `autoconf_i2pd` bounds tokens and the output filename.
- `b33offlinekeys`, `keygen`, `offlinekeys`, `vain` and `x25519` wipe temporary
  private-key buffers where the common buffer helper owns them.
- `famtool` writes its unencrypted EC private key with owner-only permissions on
  POSIX; it remains the administrator's responsibility to protect and back up
  the file.
- No tool changes the on-disk I2P key format: destination keys use
  `PrivateKeys::ToBuffer`, RouterInfo files remain RouterInfo buffers and
  family certificates remain PEM.

Dynamic Windows permissions, ASan/UBSan/TSan execution and live key-format
interoperability remain pending manual CI.
