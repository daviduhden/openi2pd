<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd tooling and firewall checklist.
-->
# Tooling, firewall and Perl checklist

Use with `SKILL.md`. This is a static map.

## Native tools

| Tool | Source | Review focus |
|---|---|---|
| `keygen`, `keyinfo`, `offlinekeys`, `b33address`, `b33offlinekeys` | `tools/*.cpp` | key formats, destination encoding, file permissions |
| `i2pbase64`, `x25519`, `verifyhost`, `famtool` | `tools/*.cpp` | encoding, length checks, output stability |
| `regaddr`, `regaddr_3ld`, `regaddralias` | `tools/*.cpp` | URL/address validation, path handling |
| `routerinfo`, `vain`, `autoconf_i2pd` | `tools/*.cpp` | untrusted RouterInfo, firewall output, option parsing |
| `i2pdctl` | `tools/scripts/i2pdctl` | bounded local TUI/I2PControl client |

Shared helpers: `tools/common/getopt.*`, `file.hpp`, `key.hpp`. Do not
duplicate ad-hoc parsers.

## Firewall rules

- Endpoint model in `tools/firewall.hpp`; CLI in `tools/routerinfo.cpp`.
- Backends: `iptables`, `nftables`, `windows`, `pf`; legacy `-f` output stays
  compatible.
- Validate address/port before rendering; deduplicate multi-file input;
  reject malformed endpoint records.
- Never execute a rule. Tests compare rendered text; no root or network.
- Preserve `--nft-family/table/chain` scoping and quoting.

## Perl scripts

| Script | Purpose | Dependency rule |
|---|---|---|
| `tools/scripts/i2pdctl` | local admin client | core Perl + documented runtime modules |
| `tools/scripts/validate-branding.pl` | asset/palette checks | core modules; reads only |
| `tools/scripts/check-doc-links.pl` | docs link/reference check | core modules; reads only |
| `android/tools/check-english-only.pl` | Android resource policy | core modules; reads only |

No CPAN install, Python fallback, network fetch or hidden download. CMake
`find_package(Perl)` and CI must use the same interpreter for syntax/install
checks.

## Test map

- `tools/test-tools-inventory.cmake`: every expected tool/target.
- `tools/test-tools.cmake`, `tools/test-tools-regressions.cmake`: smoke and
  CLI behavior.
- `tests/test-routerinfo-firewall.cpp`: endpoint/firewall renderers.
- `tools/tests/*.t`: Perl syntax/portability/client/branding/install.
- `tools/test-perl-install.cmake`: installed layout.
- Labels `tools`, `perl`, `security`.

## CI notes

The Linux job builds all tools and runs inventory/smoke/regressions. The
Windows and OpenBSD jobs cover platform-specific tools. Perl syntax, `prove`,
branding, doc links and install layout run in the Linux jobs. No tool or Perl
test may be run locally by an implementation agent.
