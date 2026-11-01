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
| TF-011 | `test-ssu2-control-plane` | `!parsed.IsUnreachable ()` | the test log echo showed `Unexpected character , instead ';' after 4`: the SSU2 `pq` preference list was serialized as two length-prefixed values around a raw comma, so every reader rejected the RouterInfo | product: one `WriteString("4,3")` value; wire form pinned (TF-038) |
| TF-012 | `test-leaseset2-enc-types` | `!parsed.valid` for `"4,6"` | hybrid+X25519 (`6,4`/`4,6`) is the shipped default and valid; only two hybrids or hybrid+ElGamal are invalid | test moved those to the positive block |
| TF-013 | `test-leaseset-encrypted-freshness` | `olderOuter->IsValid ()` | the fixture appended the inner salt/plaintext outside the region `lenOuterCiphertext` declared, so the signature covered a longer prefix than the reader reconstructs (`LeaseSet2: Verification failed`) | test: reserve the layer-1 region; negative control (TF-039) |
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
| TF-034 | `i2pd-version` | `did not terminate within 12 s` again on 4e776a39, empty output | the GetCommandLineW rewrite did not fix the hang: no byte reached either pipe, so the block is before the handler (pre-main/loader or an unobservable console path) | product: raw-command-line answer at the first WinMain instruction, no AttachConsole, stderr fallback, no CRT argv fallback; driver requires the banner only when captured; Windows workflow adds objdump/ldd/probe diagnostics (RT-010, Pending CI) |
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

## Batch `dc5540ae` / `c1125a75` (runs 4124877xxxx and 41249881405)

The code revision `dc5540ae` is the first with the per-test log echo. Fuzzing
and Packaging passed; Linux/OpenBSD/sanitizers stopped at CE-066 before CTest;
Windows MSYS2 ran 213 tests with 10 failures, and MSVC ran 7 with two heap
corruption exits. `c1125a75` changes only documentation and the static
checker, so its code behaviour is the same for these entries except where
noted. The echo produced the root cause of the two previously open tests.

| ID | Test | First diagnostic | Root cause | Correction |
|---|---|---|---|---|
| TF-036 | `test-sam-control-framing` | long `MIN=3.0.0...0.1` expected `NOVERSION` | Java `VersionComparator` reads that as 3.0, so `OK VERSION=3.3` is correct; the case was written for the retired digit-concatenation comparator, and a sibling `MAX=3.1.5` case wrongly expected 3.2 | test: pin `OK/3.3`, genuine above-range `NOVERSION`, `3.1`/`3.2` for trailing bounds |
| TF-037 | `test-proxy-auth` | `header("00000002") == Accepted` | an RFC 7616 response is bound to `nc`; the test reused the published `nc=1` vector for 2, 3 and 5, so `BadCredentials` was correct and the count was not consumed | test: per-`nc` vectors plus an older-`nc` `Replayed` case |
| TF-038 | `test-ssu2-control-plane` | `Unexpected character , instead ';' after 4` | product wrote the SSU2 PQ preference list as two value strings around a raw comma; Java and `ExtractParam` require one length-prefixed value `"4,3"` | product: one `WriteString`; wire-form assertions |
| TF-039 | `test-leaseset-encrypted-freshness` | `LeaseSet2: Verification failed` | the hand-sealed fixture appended 32+lenInner bytes after the region its `lenOuterCiphertext` declared, so the signature prefix was longer than the reader's reconstruction | test: index inside the reserved region; negative control flips a ciphertext byte |
| TF-040 | `test-tunnel-endpoint-fragments` | `GetNumIncompleteMessages () == 2`; log `Buffer too small ... dropped` | the new endpoint guard compared the *declared* length to a full cell, but `NewI2NPTunnelMessage(true)` reserves the gateway header and declares 6 bytes less; every transit cell was dropped. Capacity is the memory-safety bound | product: capacity first, then a declared-length floor that allows the reserved header; pool-cell regression |
| TF-041 | `test-torrents-verify-parity` | `GetNumPeers (1) == 0` | `ParsePeers` mutated the live tracker slot before the whole bencode document validated, so a truncated response committed a partial list | product: parse into a scratch set and commit only on success; test asserts retained last-good list |
| TF-042 | `tools-cli`/`tools-smoke`/`tools-regressions` | `i2pbase64 ... after padding`; `routerinfo usage ... stderr`; `offlinekeys ... Can't create file` | `i2pbase64` decoded all complete quartets at once so `=` in the same chunk failed with the generic message; the routerinfo test searched for `argv[0]`-bearing text; two regression needles were stale after the stream/atomic-write changes | tools/tests: quartet-by-quartet decode with `data after padding`; path-independent usage assert; stderr/exit-code asserts for every writer; latent famtool cases corrected |
| TF-043 | MSVC `test-privsep-windows-isolation`, `test-privsep-windows-supervisor`, `test-privsep-windows-ipc` | isolation now passes; supervisor/IPC SEGFAULT; TUI `0xc0000374` | live-worker test never sent `WorkerHello`; spawn reuse/containment/stale-pipe holes; the remaining access violations are not explained by the audited ownership chains | product: production worker entry, per-attempt command line, unconditional containment, listener/pipe cleanup; crash needs PageHeap/minidump (RT-011, RT-012) |

Status: Implemented / static; Pending CI compilation; Pending CI tests.

## Batch `4e776a39` (runs 41320539612/45934/42948, 2026-11-01)

| ID | Test | Diagnostic | Root cause | Correction |
|---|---|---|---|---|
| TF-044 | `tools-regressions` | `i2pbase64 decodes NUL: expected hex '00', got ''` | the fixture names `nul.b64`/`nul.bin` are the Win32 NUL device in every directory, so the tool read EOF and wrote nothing; the same trap made the `b33address` NUL case pass vacuously | test: rename to `nul-byte.*`/`nul-input.b64`, pin the fixture bytes, add a reserved-device-name guard, split padding across reads |
| TF-045 | `test-tunnel-endpoint-fragments` | `GetNumIncompleteMessages () == 0`, line 331 | the new pool-cell case crafts a fragmented router-delivery first fragment, which legitimately stays in the incomplete table until a follow-on arrives | test: expect 1 |
| TF-046 | MSVC `test-tui-protocol` | `Exit code 0xc0000374` | unresolved; sources and parser are unchanged from the passing revision, the binary links only `Log.o` | test: guard the unguarded `expected` dereference and pin both bounded-container seams; crash needs PageHeap/minidump (RT-012) |
| TF-047 | `test-datastorage-protocol` | compile error CE-069 | `std::string` passed to a byte span | test: borrowing `Bytes()` helper |

The per-test log echo again identified causes without a debugger. The Windows
version hang remains open with explicit diagnostics. The MSVC access
violations did **not** reproduce on the `41325980409` rerun of the same SHA
(the whole MSVC job passed), so they are recorded as intermittent and still
need PageHeap/minidump evidence if they return. No local build, CTest,
sanitizer or workflow dispatch exists.
