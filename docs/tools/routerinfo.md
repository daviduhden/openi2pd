<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: routerinfo.

Documents project documentation.

-->

# routerinfo

Print addresses from one or more RouterInfo files and, optionally, generate
outbound firewall rules for the peers those addresses describe.

## Syntax

```sh
routerinfo [-6] [-p] [-y] [-f] routerinfo.dat...
routerinfo [-6] [-y] --firewall=BACKEND [options] routerinfo.dat...
```

| Option | Meaning |
|---|---|
| `-6` | include IPv6 addresses |
| `-p` | print ports in the address listing (not used by firewall output) |
| `-y` | include Yggdrasil addresses when present |
| `-f` | legacy iptables OUTPUT fragments; preserved for compatibility |
| `--firewall=iptables` | full `iptables`/`ip6tables` commands |
| `--firewall=nftables` | `nft add rule` commands |
| `--firewall=windows` | PowerShell `New-NetFirewallRule` commands |
| `--firewall=pf` | OpenBSD `pf.conf` rules |
| `--nft-family=inet\|ip\|ip6` | nftables family (default `inet`) |
| `--nft-table=NAME` | nftables table (default `filter`) |
| `--nft-chain=NAME` | nftables output chain (default `output`) |
| `-h`, `--help` | usage |

Multiple RouterInfo files may be given. Duplicate endpoints across files are
emitted once. `-6` is required to include IPv6 addresses in every output mode,
including `--firewall`; without it only IPv4 addresses are selected. `-y`
separately selects the Yggdrasil mesh address when present for listing; in
firewall mode it requires `-6` because the mesh address is IPv6. The tool never
executes a firewall command and never modifies `/etc/pf.conf` or any other
system firewall configuration.

The `--nft-*` options are only valid together with
`--firewall=nftables`; passing them with another backend (or with no backend)
is an error rather than being silently ignored.

## Address listing

```sh
routerinfo -6 -p routerInfo.dat
```

Output:

```text
Router Hash: <base64>
NTCP2: 192.0.2.10:12345
SSU2: 198.51.100.20:4567
```

The address listing is unchanged from the original tool.

## Legacy iptables output (`-f`)

`-f` keeps historical IPv4 output exactly and identifies IPv6 commands with
the `ip6tables` executable prefix:

```sh
routerinfo -f routerInfo.dat
routerinfo -6 -f routerInfo.dat
```

```text
# <router hash>
 -A OUTPUT -p tcp -d 192.0.2.10 --dport 12345 -j ACCEPT
 -A OUTPUT -p udp -d 198.51.100.20 --dport 4567 -j ACCEPT
```

IPv4 output keeps the historical leading space and rule-fragment shape byte
for byte. IPv6 uses an explicit `ip6tables` prefix so the selected address
family is unambiguous:

```text
# <router hash>
ip6tables -A OUTPUT -p tcp -d 2001:db8::10 --dport 12345 -j ACCEPT
```

A mixed `-6 -f` stream therefore contains IPv4 rule fragments and complete
IPv6 commands; separate the families before applying the IPv4 fragments. The
tool never executes these lines. Non-public or unspecified endpoints are
skipped under the shared public-endpoint policy rather than blindly
reproducing unsafe upstream lines.

## iptables/ip6tables commands

```sh
routerinfo --firewall=iptables routerInfo.dat
routerinfo -6 --firewall=iptables routerInfo.dat
```

```text
# <router hash>
iptables -A OUTPUT -p tcp -d 192.0.2.10 --dport 12345 -j ACCEPT
ip6tables -A OUTPUT -p udp -d 2001:db8::20 --dport 4567 -j ACCEPT
```

The example output assumes the `-6` invocation. The tool chooses `iptables`
for IPv4 and `ip6tables` for IPv6. Review the
commands before applying them. They append to the `OUTPUT` chain and assume the
chain exists.

## nftables

```sh
routerinfo -6 --firewall=nftables \
  --nft-family=inet --nft-table=filter --nft-chain=output \
  routerInfo.dat
```

```text
# <router hash>
nft add rule inet filter output ip daddr 192.0.2.10 tcp dport 12345 accept comment "openi2pd NTCP2 abcdef12"
nft add rule inet filter output ip6 daddr 2001:db8::20 udp dport 4567 accept comment "openi2pd SSU2 abcdef12"
```

- `inet` accepts both families and selects `ip daddr`/`ip6 daddr` per endpoint.
- `--nft-family=ip` skips IPv6 endpoints; `ip6` skips IPv4 endpoints and
  requires `-6`. Skipped endpoints never become permissive rules.
- Table and chain names are restricted to a safe identifier syntax: a letter
  followed by letters, digits, `_`, `-` or `.`, with nft keywords (`table`,
  `chain`, `rule`, `add`, `accept`, `ip`, `ip6`, `inet`, ...) and a leading
  `_` rejected. The chain may be a custom output chain, but the standard
  `input`, `forward`, `prerouting` and `postrouting` hook names are rejected so
  a typo cannot turn the generated rule into an inbound rule.
- The `filter` table and `output` chain are **not** created by the tool. Create
  them in your nftables configuration first, or use names that already exist.
- The generated `nft add rule` commands are text only; the tool does not run
  `nft`. `ip daddr`/`ip6 daddr` in an `inet` table relies on the implicit
  `meta nfproto` dependency of nftables 0.9 and later; older releases should
  use `--nft-family=ip`/`ip6` or validate with `nft -c -f`.
- `accept` is a base-chain verdict. The chain must be an output base chain
  (`type filter hook output`), not a regular chain reached from input.
- Re-running the generator appends duplicate rules. Remove them with
  `nft -a list chain` and the reported handles, or load the output into a
  replaceable anchor.

## Windows Defender Firewall

```sh
routerinfo -6 --firewall=windows routerInfo.dat
```

```powershell
# <router hash>
New-NetFirewallRule -Name 'openi2pd_NTCP2_v4_abcdef12_192.0.2.10_12345' -DisplayName 'openi2pd NTCP2 v4 abcdef12 192.0.2.10:12345' -Group 'openi2pd' -Direction Outbound -Action Allow -Protocol TCP -RemoteAddress '192.0.2.10' -RemotePort 12345 -ErrorAction Stop
```

- Uses the `NetSecurity` module (`New-NetFirewallRule`), not `netsh`.
- Outbound rules only, with `-RemoteAddress` and `-RemotePort`.
- `-Name` is deterministic across runs, `-Group openi2pd` supports bulk
  cleanup, and `-ErrorAction Stop` prevents a batch from silently continuing
  after a failure. Names include the transport, address family, router-hash
  prefix, address and port; a `~` in the base64 prefix is preserved. `-Name`
  is a unique key, so applying the same line twice fails; remove the old rule
  first (`Remove-NetFirewallRule -Name ... -ErrorAction SilentlyContinue`) or
  treat a re-run as an error. The cmdlet also emits an object, so append
  `| Out-Null` in scripts.
- On a default Windows profile outbound traffic is already allowed, so these
  rules matter when outbound is blocked; they do not open the router's inbound
  listener. An explicit Block rule takes precedence over an Allow.
- `-Program` is not set, so the allow applies to any local process; the tool
  cannot know the installed router path. Use `-WhatIf` first in an elevated
  session to inspect the effect.
- Application requires an elevated PowerShell session with permission to
  modify the local firewall. The tool only prints the commands.

## OpenBSD PF

```sh
routerinfo -6 --firewall=pf routerInfo.dat
```

```pf
# <router hash>
pass out quick inet proto tcp to 192.0.2.10 port 12345 keep state label "openi2pd NTCP2 abcdef12"
pass out quick inet6 proto udp to 2001:db8::20 port 4567 keep state label "openi2pd SSU2 abcdef12"
```

These are `pf.conf(5)` rules, not iptables translations. The `label` carries the
public router-hash prefix for attribution and is limited to 64 bytes including
the NUL, so an overlong library tag is refused (the CLI's eight-character hash
prefix is always safe). `quick` is final across the current ruleset and loaded
anchors, so a later `block` cannot override a match. No `on` clause means the
rule also matches `lo0`; no `from` clause is the implied `any`. PF passes by
default, so these rules only matter under a default-deny outbound policy.
`keep state` is valid but redundant on modern PF, and `flags S/SA` must not be
added (it would break UDP). The tool does not run `pfctl` or edit
`/etc/pf.conf`, so stale rules persist until the ruleset is reloaded. An anchor
(`anchor "openi2pd"` plus `load anchor`) is the natural replaceable container.

Validate a candidate file without loading it:

```sh
pfctl -nf candidate.conf
```

## Validation and safety

- Addresses must be numeric literals. Before parsing, the text is restricted
  to `0-9`, `a-f`, `A-F`, `:` and `.`; hostnames, brackets, whitespace, scope
  identifiers (`fe80::1%eth0`), prefixes and injection attempts are rejected.
  Addresses are re-serialized canonically before they reach rule text.
- Numeric IP addresses are parsed with `boost::asio::ip::make_address`.
  Unspecified, loopback, multicast, IPv4 broadcast (`255.255.255.255`),
  IPv4 link-local (`169.254.0.0/16`), IPv6 link-local (`fe80::/10`),
  IPv4 private/CGNAT ranges, benchmark (`198.18.0.0/15`), IETF
  (`192.0.0.0/24`), 6to4 relay (`192.88.99.0/24`), `0.0.0.0/8`,
  `240.0.0.0/4`, IPv6 ULA (`fc00::/7`), deprecated IPv6 site-local
  (`fec0::/10`), IPv4-mapped IPv6, deprecated IPv4-compatible IPv6, NAT64
  (`64:ff9b::/96`), 6to4 (`2002::/16`), Teredo (`2001::/32`), discard
  (`100::/64`) and the `3fff::/20` documentation prefix are rejected.
  The `192.0.2.0/24`, `198.51.100.0/24`, `203.0.113.0/24` and
  `2001:db8::/32` documentation ranges remain selectable because the signed
  test fixture uses them.
- Malformed or unreadable RouterInfo files are reported and produce a non-zero
  exit status; they are never passed to the hash or address accessors.
- SSU2 addresses with no concrete host/port are introducer-only and produce no
  direct rule; they are valid RouterInfo data and are skipped rather than
  reported as a malformed file.
- Ports must be `1..65535`; transport must be NTCP2 (TCP) or SSU2 (UDP).
- `Render` revalidates the endpoint and requires the canonical address and
  family populated by `MakeEndpoint`; a manually assembled invalid aggregate is
  rejected even if it bypasses the CLI.
- Duplicate `(family, transport, address, port)` tuples are removed across all
  input files.
- Every renderer emits an outbound rule: iptables `OUTPUT`, nftables output
  chain, Windows `-Direction Outbound`, and PF `pass out`.
- The total distinct emitted endpoint count is capped at 4096; endpoints
  filtered by the nftables family or rejected by a renderer are not charged.
  Invalid endpoints are skipped with a diagnostic and make generation exit
  non-zero. If a valid run selects no endpoint for the backend/family, a
  diagnostic is written to stderr and the exit status remains zero.
- Output is buffered and written only when every input file parsed. A malformed
  later file leaves stdout empty and exits 1, so a pipeline cannot apply a
  partial rule set. Usage errors go to stderr; `--help` uses stdout.
- `-p` is a listing-only option and is ignored with any firewall backend.
- nftables identifiers are validated (not shell-quoted) and PowerShell
  strings are single-quoted with `''` doubling. The
  Windows display-name tag is restricted to printable identifier characters
  (letters, digits, `+ / = _ - ~ .`) so a tag cannot introduce a quote,
  newline or control character. Router hashes in names are public identifiers
  and contain only base64 characters.
- Missing/invalid address or port never produces an `any` rule, and the
  tool renders no hostnames or DNS-dependent matches.

## Compatibility

All four backends can be generated on any operating system. The target
firewall syntax is selected explicitly; the host OS is never used to choose a
backend. RouterInfo key/address formats are unchanged.

## Tests

`tests/test-routerinfo-firewall.cpp` checks IPv4/IPv6 NTCP2/SSU2 rendering,
byte-stable IPv4 legacy output and explicit `ip6tables` legacy output, nft
family filtering, identifier rejection, PowerShell escaping, PF syntax,
numeric-only address grammar, rejection of scoped/bracketed/link-local/IPv4-
compatible/IPv4-mapped/broadcast addresses, invalid nftables families and
chains, outbound-only output, display-name tag validation, port boundaries
and rendering-boundary endpoint revalidation.
`tools-smoke` creates a valid signed RouterInfo through the production
serializer and checks positive `-6 -p`, `-6 -f`, and explicit iptables CLI
output for both address families. It also checks CLI help for every backend,
legacy `-f`, family selection without `-6`, invalid backends, unsafe table
names, conflicting options, nftables options without a backend, an invalid nft
family, nftables options with a non-nft backend and malformed RouterInfo input
through every backend and `-f`. The checks are deterministic and offline; see
`testing.md`.

External syntax validation is prepared for CI/dedicated hosts, but is not run
by the static test suite:

```sh
# iptables/ip6tables: wrap the command output in a temporary restore file
# containing *filter, :OUTPUT ACCEPT [0:0], the -A OUTPUT rules and COMMIT.
iptables-restore --test candidate.rules
ip6tables-restore --test candidate6.rules

# nftables: place the generated `nft add rule` bodies in a table/chain block.
nft -c -f candidate.nft

# OpenBSD PF (syntax only; does not load the ruleset).
pfctl -nf candidate.conf

# Windows Defender Firewall: validate from an elevated PowerShell session
# with -WhatIf on each generated New-NetFirewallRule command.
```

A Windows host is required for the PowerShell check; `iptables-restore`,
`nft` and `pfctl` are not invoked during local static work.
