<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd Perl inventory.
-->
# Perl scripts and test inventory

Use with `openi2pd-tooling/SKILL.md`. This is a static map; do not execute
`perl`, `prove` or a consumer locally.

## Script matrix

| Script | Purpose | Runtime rule |
|---|---|---|
| `tools/scripts/i2pdctl` | I2PControl JSON-RPC client (`exec`, `raw_info`, `info`); HTTPS-only loopback default, no TUI path | Perl core modules; `IO::Socket::SSL` or documented `openssl s_client` fallback |
| `tools/scripts/validate-branding.pl` | Branding/palette/vector/mirror checks | Perl core modules; reads assets only |
| `tools/scripts/check-doc-links.pl` | Documentation link, anchor and target checks | Perl core modules; repository-contained paths only |
| `tools/scripts/check-doc-nav.pl` | MkDocs navigation/theme/unlisted-page checks | Perl core modules; reads docs config and tree |
| `android/tools/check-android-locales.pl` | en/es/ru/zh-CN resource parity and locale policy | Perl core modules; reads Android sources |
| `android/tools/check-android-jni-symbols.pl` | Java `native` ↔ header ↔ `Java_*` parity | Perl core modules; reads Android sources |
| `i18n/check-i18n-catalogs.pl` | native catalog key/placeholder/plural contract | Perl core modules; reads `i18n/` |
| `tools/tests/maintenance-checkers.t` | negative fixtures for link/nav/JNI checkers | Test::More/File::Temp/Open3; temporary trees only |
| `tests/interop/check-vectors.pl` | Static/differential fixture and provenance checks | Perl core modules; optional read-only Java checkout |

No CPAN, Python, Node, network install or hidden download. No script may write
outside an explicitly assigned temporary output path.

## Test names and labels

- `perl-i2pdctl-syntax`, `perl-syntax-<tool>`, `perl-i2pdctl`,
  `perl-portability`, `perl-validate-branding`, `perl-install-layout`,
  `perl-maintenance-checkers`, `perl-doc-links`, `docs-nav-static`,
  `android-locale-policy`, `android-jni-symbols-static`.
- Optional `perl-critic` only when `-DI2PD_PERL_CRITIC=ON` and the analyzer is
  already installed; absence is not a successful style run.
- `tools-inventory`, `tools-smoke`, `tools-regressions` and
  `test-routerinfo-firewall` cover native tools and firewall output.
- Labels: `perl`, `tools`, `docs`, `android`, `static`, `security` as
  applicable.

## CMake/CI consumers

- `tools/CMakeLists.txt` registers Perl/prove checks and installs
  `i2pdctl` plus `LICENSE.i2pd-tools`.
- `tools/test-perl-install.cmake` checks the installed script/notice layout.
- `tools/test-tools-inventory.cmake` fails on missing upstream manifests or
  unregistered top-level tool sources.
- `build.yml` runs syntax, portability, link/nav, branding, Android policy and
  install-layout checks in the Linux lane. The same interpreter must be used
  for CMake discovery and shell/prove checks.
- `agent-skills-static` validates this reference; it does not execute Perl.

## Review prompts

1. Which script/test/CMake consumer owns the changed behavior?
2. Are only core modules or already-approved runtime modules used?
3. Is the interpreter absolute/pinned consistently with CI?
4. Are paths relative to the script or repository root, and traversal/NUL
   rejected?
5. Does the change alter output consumed by docs, Android or packaging?
6. Are failures non-zero and visible, without `|| true` or skipped assertions?
