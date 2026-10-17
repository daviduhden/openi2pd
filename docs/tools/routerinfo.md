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
emitted once. The tool never executes a firewall command and never modifies
`/etc/pf.conf` or any other system firewall configuration.

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

## Legacy iptables fragments (`-f`)

`-f` keeps the historical output exactly:

```sh
routerinfo -f routerInfo.dat
```

```text
# <router hash>
 -A OUTPUT -p tcp -d 192.0.2.10 --dport 12345 -j ACCEPT
 -A OUTPUT -p udp -d 198.51.100.20 --dport 4567 -j ACCEPT
```

This is a rule fragment, not a complete command. It can be appended to an
`iptables`/`ip6tables` rule file or pasted after the appropriate command. The
leading space and lack of `iptables` prefix are intentional compatibility.

## iptables/ip6tables commands

```sh
routerinfo --firewall=iptables routerInfo.dat
```

```text
# <router hash>
iptables -A OUTPUT -p tcp -d 192.0.2.10 --dport 12345 -j ACCEPT
ip6tables -A OUTPUT -p udp -d 2001:db8::20 --dport 4567 -j ACCEPT
```

The tool chooses `iptables` for IPv4 and `ip6tables` for IPv6. Review the
commands before applying them. They append to the `OUTPUT` chain and assume the
chain exists.

## nftables

```sh
routerinfo --firewall=nftables \
  --nft-family=inet --nft-table=filter --nft-chain=output \
  routerInfo.dat
```

```text
# <router hash>
nft add rule inet filter output ip daddr 192.0.2.10 tcp dport 12345 accept
nft add rule inet filter output ip6 daddr 2001:db8::20 udp dport 4567 accept
```

- `inet` accepts both families and selects `ip daddr`/`ip6 daddr` per endpoint.
- `--nft-family=ip` skips IPv6 endpoints; `ip6` skips IPv4 endpoints. Skipped
  endpoints never become permissive rules.
- Table and chain names are restricted to a safe identifier syntax.
- The `filter` table and `output` chain are **not** created by the tool. Create
  them in your nftables configuration first, or use names that already exist.
- The generated `nft add rule` commands are text only; the tool does not run
  `nft`.

## Windows Defender Firewall

```sh
routerinfo --firewall=windows routerInfo.dat
```

```powershell
# <router hash>
New-NetFirewallRule -DisplayName 'i2pd NTCP2 abcdef12 192.0.2.10:12345' -Direction Outbound -Action Allow -Protocol TCP -RemoteAddress '192.0.2.10' -RemotePort 12345
```

- Uses the `NetSecurity` module (`New-NetFirewallRule`), not `netsh`.
- Outbound rules only, with `-RemoteAddress` and `-RemotePort`.
- Display names include the transport, a router-hash prefix, address and port
  so rules from multiple RouterInfo files remain distinguishable.
- Application requires an elevated PowerShell session with permission to
  modify the local firewall. The tool only prints the commands.

## OpenBSD PF

```sh
routerinfo --firewall=pf routerInfo.dat
```

```pf
# <router hash>
pass out quick inet proto tcp to 192.0.2.10 port 12345 keep state
pass out quick inet6 proto udp to 2001:db8::20 port 4567 keep state
```

These are `pf.conf(5)` rules, not iptables translations. They can be placed in
the main ruleset or in an anchor loaded from it. Keep the surrounding rule
order in mind because `quick` terminates evaluation on match. The tool does not
run `pfctl` or edit `/etc/pf.conf`.

Validate a candidate file without loading it:

```sh
pfctl -nf candidate.conf
```

## Validation and safety

- Numeric IP addresses are parsed with `boost::asio::ip::make_address`.
  Hostnames, malformed strings, unspecified/loopback/multicast addresses and
  injection attempts are rejected.
- Ports must be `1..65535`; transport must be NTCP2 (TCP) or SSU2 (UDP).
- Duplicate `(family, transport, address, port)` tuples are removed across all
  input files.
- The total endpoint count is capped at 4096; invalid endpoints are skipped
  with a diagnostic.
- nftables identifiers and PowerShell strings are escaped/validated. Router
  hashes in names are public identifiers and contain only base64 characters.
- Missing/invalid address or port never produces an `any` rule.

## Compatibility

All four backends can be generated on any operating system. The target
firewall syntax is selected explicitly; the host OS is never used to choose a
backend. RouterInfo key/address formats are unchanged.

## Tests

`tests/test-routerinfo-firewall.cpp` checks IPv4/IPv6 NTCP2/SSU2 rendering,
legacy output stability, nft family filtering, identifier rejection,
PowerShell escaping and PF syntax. `tools-smoke` checks CLI help, invalid
backends, unsafe table names and conflicting options. Dynamic validation with
`iptables-restore --test`, `nft -c`, `pfctl -nf` and a Windows host is pending;
these tools are not executed during the static test run.
