<!--
See the LICENSE file at the top of the project tree for copyright
and license details.
-->

# Test failure inventory

## Batch `63c42ca4` (run 41232676139, MSYS2) and 41232673150 (Linux)

This is the first batch that executed the newly added suites: the Linux lanes
had stopped at CE-055 and the Windows MSYS2 lane ran CTest. Twenty-five
distinct assertions, three crashes and three C++ assertion classes are
recorded here; each was root-caused and the correct side (product or test)
was fixed. All entries are **Pending CI compilation** and **Pending CI
tests**.

| ID | Test | First diagnostic | Root cause | Correction |
|---|---|---|---|---|
| TF-001 | `test-tunnel-build-records` | segfault, no output | `Tunnel::Build` posts through `Transports::SendMessages`; `m_Service` is created only by `Start()`, while `m_IsOnline` defaults true | product: null-`m_Service` guard in `SendMessage`/`SendMessages` (RT-004); tests call `transports.SetOnline(false)` |
| TF-002 | `test-tunnel-endpoint-fragments` | segfault | same pre-`Start()` send from the router-delivery path; additionally the checksum-IV suffix write was unguarded | product: capacity guard before the IV copy (RT-005); test offline switch and exact incomplete-message counts |
| TF-003 | `test-tunnelpool-build-backoff` | segfault | same pre-`Start()` send | test offline switch |
| TF-004 | `test-tunnelpool-zero-hop` | `zero-hop replacement tunnels were not published` | a zero-hop outbound path has no reply hop but creation/`Recreate` required an established inbound tunnel | product: shared `OutboundPathNeedsInboundTunnel()` predicate |
| TF-005 | `test-tunnelpool-reuse-policy` | `!old->IsHighBandwidth ()` | test built the "old, low-bandwidth" peer with caps `O` (128-256 KBps, high) | test uses `L`; no product flag needed |
| TF-006 | `test-routerinfo-java213-parsing` | `router.IsUnreachable ()` | test expected descriptor rejection for a signature-valid RI whose only address is unusable; DB-119 keeps it parseable/storable for 54 minutes | test asserts `!HasValidAddresses()`, `!IsPublished()`, `!IsReachableBy()` |
| TF-007 | `test-routerinfo-update-transaction` | `!router.Update (emptyBuffer, 0)` | the "no usable transport" body was signature-valid; `Update()` mirrors the parse level, and the address policy belongs to netDb | test expects acceptance, then pins a genuinely malformed body |
| TF-008 | `test-netdb-persistence` | `ClassifyRouterInfoFile ("/data/netDb/r/<hash>.dat") == Load` | the fixture omitted the canonical `routerInfo-` prefix | test uses the canonical name; a prefix-less name is now asserted `Foreign` |
| TF-009 | `test-artifact-storage` | `WriteArtifact (RouterInfoCache, ...)` | the direct fallback had no data directory, so the no-follow write failed before the fixture | test initialises `i2p::fs::DetectDataDir`; three delegated-write cases were previously vacuous |
| TF-010 | `test-ssu2-address-negotiation` | `router->IsUnreachable ()` | test expected whole-descriptor rejection for an unusable SSU2 address/MTU | test asserts the address-level contract; descriptor stays parseable |
| TF-011 | `test-ssu2-control-plane` | `!parsed.IsUnreachable ()` | locally produced RouterInfo fails its own re-parse; the exact rule is not visible in the unit test | **open**: bounded recent-log dump added before the assertion; `CI failure` |
| TF-012 | `test-leaseset2-enc-types` | `!parsed.valid` for `"4,6"` | hybrid+X25519 (`6,4`/`4,6`) is the shipped default and valid; only two hybrids or hybrid+ElGamal are invalid | test moved those to the positive block |
| TF-013 | `test-leaseset-encrypted-freshness` | `olderOuter->IsValid ()` | hand-rolled inner sealer; the product path works for product-sealed buffers | **open**: bounded recent-log dump added; `CI failure` |
| TF-014 | `test-i2cp-b33-lookup` | `addr` null | `AddressBook::GetAddress()` read the final label after the `.i2p` suffix only, so `<b32>.b33.i2p` yielded the literal `b33` | product: strip an explicit `.b33` before `GetFinalLabel` |
| TF-015 | `test-i2ptunnel-http` | `parser.Output () == expected` | (a) `pgrade` byte search missed mixed-case `uPgRaDe`; (b) the `Proxy-Connection`-only expectation omitted the synthesised `Connection: close` | product: `HasConnectionUpgradeToken()` in the tunnel parser and `HTTPProxy`; test pins both |
| TF-016 | `test-sam-control-framing` | `!ParseSAMParams ("ID=a ID=b").duplicate` | assertion was inverted; `Mapping::Insert` already reports the duplicate | test |
| TF-017 | `test-streaming-sendstate` | `result.called` | one `poll()` pass was assumed to deliver the posted completion | test drains the io_context within a bound |
| TF-018 | `test-proxy-auth` | wrong-opaque returned `BadNonce` | the same function had erased the nonce in an earlier expired-nonce case | test reorders and pins `StaleNonce` before the opaque comparison |
| TF-019 | `test-http-proxy-limits` | `origin.IsValid ()` | `ParseAuthority` returned `BadPort` for a port-less `Host` (origin form), rejecting ordinary proxy requests | product: port defaults to 80; CONNECT still requires one |
| TF-020 | `test-addressbook-actions` | `!fake->identities.contains (feedHash)` | the retracted destination is still referenced by two other names, whose identity file must be retained | test; new case pins deletion of an unshared destination |
| TF-021 | `test-torrents-security` | `have != false` | Boost tribool `!=`/`!` yield tribool, not bool | test uses `indeterminate()` + explicit conversion |
| TF-022 | `test-torrents-verify-parity` | `!boost::logic::indeterminate (have) && !have` | same tribool misuse | test |
| TF-023 | `test-torrents-tracker-schedule` | `ScheduledWait (...) <= 60 * 1000` | a `failure reason` response that also carried `interval` overwrote the 15 s-30 min ladder | product: schedule only in the success tail |
| TF-024 | `test-crypto-x25519-noise-elligator-mlkem-audit` | `GetPubicKey ()` mismatch | the test reused buffers that the fail-closed null-sibling branch had zeroed, so it built a decryptor from an all-zero scalar | test uses scratch buffers; product rejects an all-zero scalar on load |
| TF-025 | `test-eciesx25519-ratchet-session` | `offset + size <= innerLen` | the test parsed AEAD ciphertext as plaintext block headers | test mirrors the receiver key schedule and decrypts first |
| TF-026 | `test-routerinfo-firewall` | `!IsSafeIdentifier ("accept")` | `accept` is an nft verdict but was missing from the reserved list | product: reserved words extended; malformed vs non-public firewall endpoints separated |
| TF-027 | `test-privsep-authorization` | `IsReplyType (type) \|\| type == WorkerHello` | `Ping` was declared receivable by the core and `Error` only by three roles | product: registry rows and compile-time direction invariant |
| TF-028 | `test-privsep-supervisor` | `elapsed < STABLE_PERIOD * 1000` | the eight-wait ladder totals 61.75 s (capped eighth wait), not <60 s; the test copied an inaccurate class comment | product: `TotalLadderDelay()` + static_asserts, terminal `NoteStable`; test pins the real bound |
| TF-029 | `test-privsep-windows-ipc` (MSVC) | `channel.Send (WorkerHello).has_value ()` | the test writes the hello while the server is rejecting on PID/session and closing the pipe; MSVC loses that race, MSYS2 won it | test tolerates the racing write and keeps asserting the server rejection |
| TF-030 | `perl-portability` / `perl-doc-links` / `docs-nav-static` / `perl-maintenance-checkers` | path separators and empty diagnostics | CE-059 | checkers normalise and merge streams |
| TF-031 | `android-locale-policy` | `CRLF in ...` | CE-061 | `.gitattributes` LF pinning |
| TF-032 | `perl-install-layout` | missing `i2pdctl.1` | CE-060 | man-page guard widened to non-Android |
| TF-033 | `tools-cli` / `tools-smoke` / `tools-regressions` | CE-062, CE-063 | product CLI output and reserved-range filtering | tools and docs corrected |
| TF-034 | `i2pd-version` | `***Timeout 30.02 sec` | a headless CTest launch of the Windows GUI-subsystem router produced no output and no exit | **open**: bounded `check-binary-version.cmake` driver reports exit status/output and fails a real hang; the underlying Windows launch behaviour remains `CI failure` |
| TF-035 | Debian `Remove and preserve, then purge` | CE-064 | unprivileged `test -f` inside a 0700 state directory | workflow and static contract use `sudo test` |

Executed evidence for this batch: MSYS2 passed 172/210 tests, MSVC passed 6/7
(one skip), Fuzzing passed its bounded smoke run. Linux/OpenBSD/sanitizer
lanes stopped at CE-055, so their CTest results are unknown. The table is not
a claim that the corrected revision passes.


## Current batch (`00929cd1`, run 41229371203)

| Test | Diagnostic | Root cause | State |
|---|---|---|---|
| `test-privsep-windows-ipc` | assert `delivered.has_value() && delivered->has_value()`, exit `0xc0000409` | core could not send the typed `Error` for a refused worker frame; max-payload test sent the wrong direction | corrected locally, RT-001 |
| `test-privsep-windows-supervisor` | assert `HandleCount () == baseline`, exit `0xc0000409` | root check ran after AppContainer creation, leaving one-time handles | corrected locally, RT-002 |

The other five MSVC tests passed (`test-tui-protocol`, `test-tui-terminal`,
`test-tui-pipe-name`, `test-tui-win32-pipe`,
`test-privsep-windows-isolation`). Linux/OpenBSD/MSYS2 did not reach CTest
because `test-i2np-dispatch-policy` failed to compile (CE-048); Fuzzing passed
its bounded smoke run.

## Packaging and integration

The Debian lifecycle failure is recorded as CE-051/RT-003. It is a package
verification failure, not a C++ unit test, but it must be rerun with the same
artifact discipline: inspect the exact `.deb` control archive before changing
maintainer scripts.

## Historical evidence

Older test failures (Windows IPC restart assertions, Debian remove/purge,
shared test namespace errors) remain in `COMPILATION-ERRORS.md` and
`COMPILATION-WARNINGS.md` with their run IDs. They are not evidence for the
current local corrections and must not be counted as current passes.
