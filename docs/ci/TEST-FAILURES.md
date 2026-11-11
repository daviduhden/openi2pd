<!--
See the LICENSE file at the top of the project tree for copyright
and license details.
-->

# Test failure inventory

## c25a0b08 — TF-103 infrastructure acquisition failure

Snapshot before diagnostic dispatch: 424 completed runs / 69 SHAs;
112 success, 246 failure, 66 cancelled. Exactly ten new c25 runs give seven
successful workflows and three failures. Metadata was paginated; latest-ten
full successful/failed logs and artifact inventories were retrieved.
Historical unavailable logs remain gaps, not clean results.
Every result below executes `c25a0b081aa3d3b968e043d968b904a40b48e4f6`.

- [Validation 43037707365](https://github.com/daviduhden/openi2pd/actions/runs/43037707365):
  source-docs job 130580894102 fails apt with exit 100 after DNS resolution
  failure for azure.archive.ubuntu.com. Doxygen never starts. ASan+UBSan
  130580893127, UBSan 130580893120 and TSan 130580893264 each pass 261 tests;
  full logs contain no sanitizer diagnostic. Fuzz 130580892747 passes fifteen
  targets; Skills 130580893318 passes; Perl quality 130580895121 skips.
- [Packaging 43037666619](https://github.com/daviduhden/openi2pd/actions/runs/43037666619):
  service install 130580746855 and contracts 130580747343 fail the same apt
  DNS acquisition before configure/contracts. Debian 130580748977 is cancelled
  during dependency acquisition; no regular-package artifact or maintainer
  script validation exists. Container 130580749537 and static-musl
  130580747141 pass. Exact static-musl artifact contains the static PIE package;
  that separate artifact does not validate the unbuilt regular package.
- OpenSSL 3 [43037679443](https://github.com/daviduhden/openi2pd/actions/runs/43037679443)
  jobs 130580792543/130580793026 and OpenSSL 4
  [43037685671](https://github.com/daviduhden/openi2pd/actions/runs/43037685671)
  130580814605/130580815136 each pass 261 main and 10/3/9 focused tests.
- OpenBSD [43037659437](https://github.com/daviduhden/openi2pd/actions/runs/43037659437)
  job 130580718405: 256 registered, 253 pass and three non-root-required skips.
- Android [43037693624](https://github.com/daviduhden/openi2pd/actions/runs/43037693624)
  130580862404 succeeds; 174 JVM tests pass, JNI parity covers 46 declarations,
  Java major 69 / DEX 039 checks pass. Device 130580865228 skips; Lint remains.
- Windows [43037652402](https://github.com/daviduhden/openi2pd/actions/runs/43037652402)
  130580697443: 245 registered / 243 pass / one failure / one skip;
  130580697869: 19 registered / 18 pass / one failure. Startup causes remain open.
- Linux [43037699387](https://github.com/daviduhden/openi2pd/actions/runs/43037699387)
  consumer 130580864054 and reference/vector interop 130580864479 pass; not
  live Java handshake evidence. Agent gate 43037672749 / 130580768127 passes.
  Instrumented OpenSSL TSan 43037714920 / 130580926326 passes one registry test.

TF-103 is infrastructure, not a compiler or test assertion regression. Do not
add retries, broad package allowances or disable validators to hide it.
Skipped generator/service/package coverage remains Pending CI tests. New
RT-081/082 and root-metadata controls are prepared, not executed. Separate
manual diagnostic 43055686363 was launched after this historical snapshot;
no remote result can validate the new local implementation.

## Revision 830c65ab: latest ten and historical coverage

Paginated metadata now covers 414 completed runs on 68 SHAs: 105 success,
243 failure and 66 cancelled. Latest ten are nine success and Windows
failure. The earlier incomplete diagnostic 42745408718 is now failure.
Metadata spans back to run 39057879349; historical unavailable job logs
39058430980/117472356821 and runs 39058841124/39058346321 remain gaps.
All latest-ten full logs, job metadata and artifact inventories were retrieved;
Windows, OpenBSD and Android artifacts were inspected. No complete historical
artifact/log coverage is claimed.

Every row below executes `830c65ab1aaed6db9d6edeb7d0a91918ac446e21`.

| Run / workflow | Jobs | Observed scope |
|---|---|---|
| [42843157728 OpenSSL 3](https://github.com/daviduhden/openi2pd/actions/runs/42843157728) | 129923464005 / 129923464466 | GCC/Clang each 261 main tests plus 10/3/9 focused tests pass |
| [42843164455 OpenSSL 4](https://github.com/daviduhden/openi2pd/actions/runs/42843164455) | 129923487225 / 129923487479 | Same executed main/focused scopes pass |
| [42843184621 Validation](https://github.com/daviduhden/openi2pd/actions/runs/42843184621) | UBSan 129923558543, TSan 129923558600, ASan+UBSan 129923558655 | Each 261 pass; no sanitizer diagnostic found in reviewed full logs |
| Same Validation run | Fuzz 129923558436, docs 129923557845, Skills 129923558654 | Fifteen smoke targets, source docs and Skills pass; Perl quality 129923560250 skipped |
| [42843137520 OpenBSD](https://github.com/daviduhden/openi2pd/actions/runs/42843137520) | 129923394440 | JUnit: 256 registered, 253 pass, three non-root-required skips |
| [42843171562 Android](https://github.com/daviduhden/openi2pd/actions/runs/42843171562) | 129923514053 | JUnit: 25 suites / 174 JVM tests pass; device job 129923566413 skipped; Lint warnings persist |
| [42843132412 Windows](https://github.com/daviduhden/openi2pd/actions/runs/42843132412) | CLANG64 129923376820 / MSVC 129923377257 | JUnit: 243 pass / one failure / one skip out of 245; MSVC 18 pass / one failure out of 19 |
| [42843191661 Instrumented OpenSSL TSan](https://github.com/daviduhden/openi2pd/actions/runs/42843191661) | 129923581632 | One registry test passes, not the complete crypto suite |
| [42843178138 Linux](https://github.com/daviduhden/openi2pd/actions/runs/42843178138) | Consumer 129923534333 / interop 129923535177 | Install/consumer and reference/vector interop pass; not live Java handshake evidence |
| [42843143279 Packaging](https://github.com/daviduhden/openi2pd/actions/runs/42843143279) | Services 129923412035/129923412036; container 129923412052; Debian 129923412115; musl 129923412271 | Pass, with external tool/runner debt tracked separately |
| [42843150302 Agent Knowledge](https://github.com/daviduhden/openi2pd/actions/runs/42843150302) | 129923436556 | Pass |

Published CW-031/032 removal and prepared encoder regressions now have CI
execution evidence in the supported lanes that ran. Published RT-078 source
contracts and ordinary storage tests pass; allocator unwind remains untested
dynamically. TF-102 still exposes Windows startup failure. OpenBSD skips say
worker identity/credentials require non-root and storage workers refuse root;
MSYS2 skips the POSIX-only I2PControl socket test.

New RT-079/080, startup-root RAII and CDB caller diagnostics are local:
Pending CI compilation / Pending CI tests. Existing test-eddsa,
test-signature-identity-hardening, control-security-static and
perl-maintenance-checkers registrations cover the prepared increments.

## EA333 retrospective snapshot

The paginated inventory contains 404 runs on 67 SHAs: 96 success, 241 failure,
66 cancelled and one incomplete at collection. This is metadata coverage,
not a claim that all historical logs or artifacts remain available.

At `ea3335843fc9073fbbaa0c5d3530c9e9387b85b8`:

- OpenSSL 3 run 42739900479 and OpenSSL 4 run 42739905544 each pass 261
  main tests in both GCC and Clang jobs, plus their focused suites.
- Validation 42739919919 passes 261 tests in each ASan/UBSan, UBSan and TSan
  lane and fifteen fuzzer smoke targets. Reviewed logs contain no sanitizer
  report. Instrumented OpenSSL TSan 42739924109 exercises one registry test.
- OpenBSD 42739887270 registers 256 tests: 253 pass and three skip because
  worker/credential checks require non-root execution. Skips are evidence gaps.
- Android 42739910723 passes 174 JVM tests in 25 suites. Device execution is
  skipped; fourteen Lint warnings remain (twelve vector paths, two ChromeOS
  ABI warnings). APK construction does not validate device behavior.
- TF-101 source-root correction now passes in executed native lanes. TF-102
  classification correctly exposes a real Windows worker startup failure.
  Windows 42739881700 MSVC has 18 pass/one failure; CLANG64 has 243 pass,
  one failure/one skip out of 245. Diagnostic 42745408718 reproduces them:
  version timeout and worker 0xC0000142, respectively. See RT inventory for
  the newly captured modal marker; neither startup problem is resolved.

Current override, bounded-length, encoding and storage-unwind corrections
have source contracts/regressions prepared but remain Pending CI compilation
and Pending CI tests. Published results above apply only to their exact SHA.


## Published revision `01960093` (2026-11-05)

| ID | Exact evidence | Disposition |
|---|---|---|
| TF-046 / TF-062 | Windows 42056732400, MSVC `127347034968`: test-tui-protocol heap exception `0xc0000374`, empty LastTest output | Unresolved RT-032; constant phase markers prepared, published PageHeap run 42061501515 dispatched |
| TF-072 | Same Windows job: test-privsep-windows-supervisor skipped after `worker pipe accept timeout` at startup | Capability/worker-start evidence gap; no claim that handshake/restart/stop passed; child-liveness diagnostics prepared |
| TF-073 | Validation 42056754181 sanitizer jobs `127347117917`, `127347118420`, `127347118453` never reach CTest | CE-076 compile blocker; absence of new reports does not resolve old RT-027/031 |

Linux consumer/interop 42056751743, Android 42056748811, Packaging 42056737144
and Validation's fuzz/docs/static siblings pass at the full SHA recorded in
the compilation inventory. Optional Android device and Perl quality lanes
were skipped. These results validate only the operations actually executed,
not local fixes or the full supported matrix.

Prepared regressions: test-stream-creation-sync (both synchronous callback
paths), malformed Windows hello/re-arm in the existing IPC suite, extended
Tools manpage install checks and negative sanitizer-evidence fixtures.
Source-only checks pass separately; all behavioral execution remains pending
manual CI on a revision containing these local changes.

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

## Current published revision (`56f17c0f`, runs 4136485xxxx)

These runs describe published SHA `56f17c0f47a3d1a328be6c54df7b661c480924f8`.
They do not validate the later local worktree changes. See linked run IDs in
the project tracking entry and runtime inventory for job-level details.

| ID | Test/job | First diagnostic | Root cause | Correction/status |
|---|---|---|---|---|
| TF-048 | Linux GCC/Clang, OpenSSL 3.x, run [41364855256](https://github.com/daviduhden/openi2pd/actions/runs/41364855256), SHA `56f17c0f47a3d1a328be6c54df7b661c480924f8`; jobs 125022735576/125022735731 | `tools/test-tools-regressions.cmake:743`: `data after padding` | fixture repeated `qg==` (already padded) 1023 times, then appended another value; decoder correctly rejected concatenated padded values | Fixture now uses 1023 valid `qqqq` quartets and one terminal `AA==`, preserving the 4096-byte cross-read boundary; expected output is 3069 `0xaa` bytes plus `0x00`; Implemented / static, Pending CI |
| TF-049 | Linux GCC/Clang, OpenSSL 3.x, run [41364855256](https://github.com/daviduhden/openi2pd/actions/runs/41364855256), SHA `56f17c0f47a3d1a328be6c54df7b661c480924f8`; jobs 125022735576/125022735731 | `test-streaming-sendstate.cpp:99`: second `Write()` never completes; log `Can't send packets, missing remote LeaseSet` | the fixture reused an `io_context` after `poll()` drained it and stopped it; queued work on a stopped context does not run until `restart()` | Test now restarts a stopped context before subsequent posts and replaces fixed poll-count loops with one ready-queue drain; read cancellation phases use the same guarded lifecycle; Implemented / static, Pending CI |
| TF-050 | Linux/OpenSSL 3/4/OpenBSD `test-blocklist-parse` | reason assertion mismatch | test fixture passed a temporary string to a parser returning borrowed views; view outlived backing storage | test fixture now names backing string; pending CI |
| TF-051 | Linux/OpenSSL 3/4/OpenBSD/sanitizers `test-i2pcontrol-socket` | three socket identity/replacement assertions | fixture closed the old bound listener before unlink/rebind; the filesystem could reuse its device/inode, falsely matching the replacement | fixture keeps original listener open through replacement cleanup; pending CI |
| TF-052 | Linux/OpenSSL 3/4 `test-datastorage-protocol` | expected relative-path rejection | encoder checked only framing bounds while decoder enforced absolute-root semantics | encoder now rejects empty/relative roots and fixture covers both; pending CI |
| TF-053 | Linux/OpenSSL 3 `test-datastorage-client` | restart retry expected to remain Busy after 250 ms, but write succeeded | `Configure()` reset the shared backoff on every successful worker handshake, so consecutive fast worker deaths never doubled their delay | removed reset from data/router worker configuration; Start and the quiet window still reset the policy; existing 250→500 ms regression pins it; pending CI |
| TF-054 | Linux `test-privsep-linux-ipc` | test expected empty record carrying SCM_RIGHTS to be accepted | Channel correctly closes the unauthorized descriptor and reports a protocol error for descriptor-bearing empty records; the fixture expected tolerance | regression now expects the error, checks descriptor count and channel reuse; pending CI |
| TF-055 | Linux `test-torrents-dht-persistence` | saved state differs from one peer record | the tunnel base constructor creates a local destination when passed null, so `BuildStateSnapshot()` persists its self record before the peer; malformed reload correctly retains both | test now validates the self record, peer bytes and retained snapshot; pending CI |
| TF-056 | Linux Clang/OpenSSL 4 run [41364858202](https://github.com/daviduhden/openi2pd/actions/runs/41364858202), job 125022744749, SHA `56f17c0f47a3d1a328be6c54df7b661c480924f8` | `test-i2cp-session-lifecycle.cpp:106`: `destination.use_count() == 1` timed out after 30.1447s; sibling OpenSSL 3 and GCC/OpenSSL 4 lanes passed | Destination and I2CP member timers, posts and stored request callbacks could retain an owner through its stopped context/request map | Timers, owner posts, stored lookup completions, dropped-message retries, I2CP lookups, Streaming lookup callbacks, BOB lookup/ping/inbound-receiver callbacks and SAM b33 datagram continuations now use weak ownership. The test checks expiry after timer self-stop and a queued LeaseSet callback stranded in a stopped destination context; `check-control-security.cmake` pins the native callback seams. A raw-`this` session snapshot future was also changed to weak ownership. Request maps stay executor-owned; external Stop does not mutate them, and final callbacks complete in destruction. Implemented / static, Pending CI; there is no focused live SAM/BOB/Streaming lifetime regression, and I2CPServer lifecycle posts remain open |
| TF-057 | OpenBSD VM/LibreSSL 4.4.0 run [41364873438](https://github.com/daviduhden/openi2pd/actions/runs/41364873438), job 125022789000, SHA `56f17c0f47a3d1a328be6c54df7b661c480924f8` | `test-gost-sig.cpp:49`: BN context setup assertion | The test called `BN_CTX_get()` without the required `BN_CTX_start()` frame; other native GOST functions already bracket context temporaries | Added a balanced `BN_CTX_start()`/`BN_CTX_end()` pair and searched all direct test-side `BN_CTX_get()` call sites; Implemented / static, Pending CI |
| TF-058 | OpenBSD VM/LibreSSL 4.4.0 run [41364873438](https://github.com/daviduhden/openi2pd/actions/runs/41364873438), job 125022789000, SHA `56f17c0f47a3d1a328be6c54df7b661c480924f8` | `test-clientcontext-registry.cpp:97`: `snapshots.load() > 0` | readers were launched, then deletion/shutdown happened before the scheduler guaranteed any reader ran; the assertion was scheduling-dependent | Test now barriers after each reader has checked a complete snapshot and before mutation/shutdown; Implemented / static, Pending CI |
| TF-059 | OpenBSD VM/LibreSSL 4.4.0 run [41364873438](https://github.com/daviduhden/openi2pd/actions/runs/41364873438), job 125022789000, SHA `56f17c0f47a3d1a328be6c54df7b661c480924f8` | `test-tui-ipc-regression.cpp:472`: 16 immediate connections did not produce `queueFull` | The fixture assumed `listen(fd, 4)` yields a fixed effective backlog; backlog is advisory and kernel-capped, while the client retries nonblocking connects | Replaced queue saturation with `RetryUnixConnectUntil` using fake clock/connect/sleep inputs. Regression pins deadline expiry, EINTR retry and immediate nonretryable errors; the real test retains successful connect/request/reconnect coverage. Implemented / static, Pending CI; no attempt count or timeout was increased |
| TF-060 | OpenBSD VM/LibreSSL 4.4.0 run [41364873438](https://github.com/daviduhden/openi2pd/actions/runs/41364873438), job 125022789000, SHA `56f17c0f47a3d1a328be6c54df7b661c480924f8` | `test-privsep-protocol.cpp:427`: maximum `RouterStorageRequest` transport send failed; exact send error was not emitted and run has no diagnostic artifact | The fixture used a 32,785-byte AF_UNIX `SOCK_SEQPACKET` record despite testing only that post-refusal receive capacity exceeds the 16-byte header. Kernel atomic-record limits vary; this is a portability hypothesis, not confirmed `EMSGSIZE` | Recovery fixture now sends a 17-byte frame, which detects accidental header-only capacity; max-payload framing remains independently tested by the codec boundary cases. Implemented / static, Pending OpenBSD CI; exact original errno remains unknown |
| TF-067 | OpenBSD `test-datastorage-protocol` | relative-path rejection assertion | same producer/consumer semantic mismatch as TF-052 | encoder fix applies; pending OpenBSD CI |
| TF-068 | Linux/OpenSSL 3/4 `test-datastorage-client`, OpenSSL 3 run [41364855256](https://github.com/daviduhden/openi2pd/actions/runs/41364855256) job `125022735731`, OpenSSL 4 run [41364858202](https://github.com/daviduhden/openi2pd/actions/runs/41364858202) job `125022744749`, SHA `56f17c0f47a3d1a328be6c54df7b661c480924f8` | `test-datastorage-client.cpp:169`: a request unexpectedly succeeded 250 ms after the second worker death, where the fixture expects the doubled 500 ms backoff | Exact-SHA source still resets the budget in DataStorage Configure; removing that reset exists only in the local diff. The published failure therefore matches TF-053 rather than contradicting the corrected local flow | Implemented / static: preserve the local reset removal, add stable-generation reply authority and deterministic policy regressions; Pending CI compilation; Pending CI tests |
| TF-069 | Linux GCC/OpenSSL 3/4 `test-eddsa`, runs [41364855256](https://github.com/daviduhden/openi2pd/actions/runs/41364855256) job `125022735576` and [41364858202](https://github.com/daviduhden/openi2pd/actions/runs/41364858202) job `125022744601`; OpenBSD [41364873438](https://github.com/daviduhden/openi2pd/actions/runs/41364873438) job `125022789000`; SHA `56f17c0f47a3d1a328be6c54df7b661c480924f8` | Positive custom Ed25519 verification assertion at `tests/test-eddsa.cpp:170` fails on GCC and OpenBSD; Linux Clang OpenSSL 3/4 passes the same vector | Exact-SHA fixture supplied 32 bytes to a 64-byte SHA-512 writer and verifier reader; corrected locally to return an owning 64-byte array. Primitive code remains unchanged; association with historical failures is pending CI | Implemented / static; Pending CI compilation; Pending CI tests. Rerun the corrected fixture on all affected lanes and retain symbolized artifacts if the failure persists |
| TF-061 | Windows MSVC and MSYS2 `test-tui-win32-pipe`, run 41364870388, SHA 56f17c0f | Both jobs fail the first-instance ERROR_ACCESS_DENIED assertion; original numeric error absent | Instance quota can confound exclusivity, but this is a static hypothesis | Fixture now leaves capacity for a control instance and captures/prints the immediate error; exact denial assertion preserved. Implemented / static; Pending CI tests |
| TF-062 | Windows MSVC `test-privsep-windows-isolation`, `test-tui-protocol`, `test-tui-pipe-name` | heap corruption / fast-fail (`0xc0000374`, `0xc0000409`) | intermittent or deterministic allocator corruption is not attributable from ordinary CTest output | Open; PageHeap/minidump required |
| TF-063 | Windows MSYS2 `tools-cli`, `tools-regressions`, `test-tui-win32-pipe` | failures; `i2pd-version` timeout | tools-cli exact output 414141410d0a contradicted LF-only 414141410a; other failures remain independent | NUL fixture now uses newline-free 41414141; pipe follow-up TF-061. Implemented / static; Pending CI tests; version timeout and remaining failures stay open |
| TF-064 | Sanitizer `test-blocklist-parse`, `test-torrents-dht-protocol` | ASan/TSan borrowed-view use-after-lifetime in test fixtures | temporary concatenated strings backed parser results containing string views | fixtures now retain backing strings; pending sanitizer CI |
| TF-065 | Validation `C++ source API documentation` | Doxygen: explicit link request to `bind` at `libi2pd/util.h:91` could not be resolved; warnings are fatal | plain `std::bind` was interpreted as an unresolved explicit link | changed to `\c std::bind`; pending documentation CI |
| TF-066 | Validation `Perl quality` | Perl::Critic exits 2 with repeated `RequireExtendedFormatting`, `ProhibitComplexRegexes`, `RequireFinalReturn`, I/O and local initialization findings | first-party findings across tracked Perl scripts/tests; published output omitted filenames, so exact source attribution is unavailable | `.perlcriticrc` now prints file/line/column/policy; pending CI to map and correct findings |

The table intentionally preserves unresolved causes as unresolved. Runs are
historical evidence only; none exercised the local corrections.

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

TF-069 static follow-up (2026-11-02): exact-SHA source inspection found
that the test supplied a 32-byte digest array to a 64-byte SHA-512 writer
and verifier reader. The fixture now obtains an owning 64-byte result.
This is a demonstrated fixture defect, not evidence of a primitive failure.
The assertion and RT-027 remain open pending manual CI on the correction;
see the RT-027 static follow-up for the equivalent-pattern review.

TF-070 (2026-11-04 static): TSan `test-log-lifecycle` lost
`local-after-sink` during `SetSink(nullptr)` → `SendTo(stream)` →
`Start/Stop`; no product defect was demonstrated by inspection. The remaining
suspects are the 1 s `Queue::Wait` wakeup and the sticky `m_StartRequested`
flag. TSan `test-runnableservice-lifecycle` and
`test-i2cp-session-lifecycle` also hit 2–5 s join/`use_count` budgets; the I2CP
implementation changed after the published run, so they require a rerun before
classification. No test was executed for this note.

TF-071 (2026-11-04 static): TF-065's `\c std::bind` did not reliably remove
the Doxygen autolink token. The source comment now says "type-erased deleter"
with no `std::` token; documentation CI remains the evidence gate.

TF-070 executor follow-up (2026-11-04): source comparison against
56f17c0f demonstrates an unguarded empty-run race in RunnableService and the
same launch-before-post ordering in NTP. Per-run work ownership, normal-return
exit and NTP queue retirement are Implemented / static. Existing registered
lifecycle targets now prepare idle-gap/restart/drain/exception and deterministic
empty-run regressions. No timing budget was increased. This does not resolve
the separate logger assertion, I2CP ownership or historical leaks; see RT-031.

TF-070 logger publication follow-up (2026-11-04): the exact-SHA Log::Start
never resets m_StartRequested between joined writer generations. A new Run
can therefore pass its condition wait on the old true bit, observe the still
false running flag, drain an empty queue and exit before Start publishes true
and its caller appends local-after-sink. Start now re-arms the signal before
launch under lifecycle/start locks. Repeated destination-change regressions
and a source-order contract are prepared. This demonstrated source
interleaving explains the observed symptom; runtime confirmation is Pending
CI. The queue wakeup and other lifecycle hypotheses are not declared resolved
by this static correction.

## Published revision `14f224e6` (2026-11-07)

Exact run/job links and SHA are recorded in COMPILATION-ERRORS.md. Local
corrections are Implemented / static, Pending CI compilation and Pending CI
tests; no local behavioral suite was executed.

| ID | Observed evidence | Correction / remaining validation |
|---|---|---|
| TF-074 | OpenBSD 42402987408 / MinGW 42402984632: EdDSA assertion at line 145 | RT-038 full nonce reduction; strict comparison retained |
| TF-075 | Same platforms: capability fixture expects a grant for forbidden Exec | Assert zero Exec grants and complete withholding; production capability masks unchanged |
| TF-076 | Windows 42402984632: POSIX WIFEXITED/WIFSIGNALED unimplemented in native Perl | Decode Perl wait words portably in three suites; check full pipe status in vector/branding helpers; signal-with-valid-JSON fixtures prepared |
| TF-077 | OpenBSD 42402987408: validator fixture cannot execute git init -b main | Install Git in VM; declare Git 2.28 test dependency and put its directory in that CTest environment |
| TF-078 | OpenBSD failing action provides no usable CTest artifact | Pinned action exits before copyback; separate prepare action and documented remote custom shell preserve copyback and original failure status |

The custom shell behavior was checked against the pinned
[vmactions implementation](https://github.com/vmactions/openbsd-vm/blob/f1ca8bc8ecc5b6ef7218e5b914836e067e1546d3/index.js)
and its documented custom-shell interface. Negative workflow fixtures pin
failure-preserving upload structure and prohibit workflow_call triggers.
TF-073 recurs in all three sanitizer jobs of 42403004301 through CE-078.
No new sanitizer runtime result is available. Optional Perl quality and API 29
UI lanes are skipped; those capabilities remain unvalidated. Existing MSVC
heap failures and MinGW startup timeout remain open under RT-032.

## Follow-up on published `68c16e10`

| ID | Exact observed failure | Correction / status |
|---|---|---|
| TF-079 | Perl portability test 210 across SSL3/4, OpenBSD and all sanitizer jobs | Scanner matched `open` inside quoted `openbsd`; require the complete keyword, retain unsafe pipe fixtures and add a safe literal control. Implemented / static; Pending CI tests |
| TF-080 | Linux IPC TestPeerExpectation line 299 in SSL3/4 and ASan | Fixture set configured=false before asking explicit UID rejection. Use designated configured fields; preserve UID/GID negatives and add the legacy kernel-authenticated round trip. Production checks unchanged. Implemented / static; Pending CI tests |
| TF-081 | PageHeap 42407544860 emits syntax error for CDB `q 1` after the useful stack | Use supported `q`; independently fail on OPENI2PD_CRASH or missing debugger log, with negative source fixtures. Implemented / static; Pending CI tests |

Validation 42414235478 ASan/UBSan has six failing tests: TF-079, TF-080,
i2ptunnel-stop, i2ptunnel-targets, streaming-sendstate and torrents-security
(RT-042/043). TSan has TF-079 plus RT-044; UBSan has TF-079 only. These are
runtime/test failures after successful compilation, not compile blockers.

Earlier TF-074/075/076/077 now pass the corresponding executed crypto,
capability and Perl/Git fixture paths. OpenBSD TF-078 artifact copyback is
confirmed: exact-run artifact `13591246747` contains configure.log, build.log,
JUnit and Testing data despite CTest failure. Three tests are skipped in JUnit:
worker identity and credentials under root, plus torrent storage pending an
exec-free broker. LastTest's summary wording must not override JUnit notrun.
Skipped worker startup gives no later handshake/shutdown evidence.

### TF-082 — Native Win32 fixture output changed LF into CRLF

Windows 42414215883 MinGW job `128528682471`, perl-maintenance-checkers
assertions 243/244: generated i18n catalogs fail escaped/newline-key controls.
The shared fixture writer used text output; native Windows inserted CRLF.
Use raw writes to preserve intended bytes, retaining explicit CRLF-negative
fixtures. Implemented / static; Pending CI tests.

### TF-083 — Native Win32 path and symlink capability boundaries

Same run/job, perl-agent-knowledge-contract: Unicode worktree-root subtest 36
fails resolution and JSON PASS assertions; subtest 39 runs no assertions when
core symlink throws an unimplemented-operation exception. Trap only symlink
creation and report an exact capability skip, preserving checks on capable
hosts. Implemented / static; Pending CI tests for that capability handling.

Unicode worktree paths remain **Blocked**. Git UTF-8 output, decoded Perl
strings and native Win32 filesystem/process paths require a lossless bridge.
Core GetANSIPathName can substitute question marks when short aliases are
unavailable; silently applying it would jeopardize path-boundary checks.
The test stays enabled and adds bounded failure-only diagnostics for its
private fixture paths. No CLI coercion, CPAN dependency or blanket skip was
introduced. Required follow-up: inspect those exact byte/path diagnostics and
implement a verified round-trip native-path/process bridge before claiming
Unicode portability. Reference: [Perl Win32 filename API](https://perldoc.perl.org/5.40.3/Win32#Win32::GetANSIPathName(FILENAME)).

## 2026-11-08 — Published `8ffd1ad8` and focused `9f5320b2`

### TF-084 — Unknown-type fixture reused a now-registered IPC value

OpenSSL 3/4 jobs and all three sanitizer jobs fail
`test-privsep-linux-ipc.cpp:465`, TestMalformedFrames. Type 7 is
ConfigureTorrentStorage, so a raw framing channel correctly accepts it.
Derive the first unknown value from MAX_MESSAGE_TYPE and check the registry;
retain reserved-zero and 0xffff rejection plus channel-reuse controls. Add
explicit acceptance of known framing, without changing endpoint authorization
or production decoding. Implemented / static; Pending CI tests.

The earlier configured-peer expectation case runs before this assertion and
passes; this does not mark the whole IPC binary validated. Ordinary TSan also
fails the separate provider-cache case, while ASan/UBSan and UBSan have only
TF-084 in the new batch. OpenBSD completes all 246 CTest entries without a
failure, but worker/broker capability skips remain unexecuted paths.

### TF-085 — New failure regressions prepared, not executed

Existing bencode and verification suites now cover recognized field types,
transactional handshake/tracker failures and peer-ID provider failure. The
RPC metadata fixture additionally checks internal error 4 maps to existing
wire error 3 plus a string, and control snapshots retain that string.
All are local prepared regressions, not newly observed CI failures.

### TF-086 — Seed uniqueness assertion admitted legitimate random failures

Static probability review found the old 512-sample test required all 32-bit
seeds to differ; a real birthday collision could fail it without a defect.
Replace that requirement with deterministic source/boundary/failure/cleansing
regressions and a provider non-constant control that permits collisions.
No historical failure is attributed to this hypothesis. Implemented / static;
Pending CI tests. No retry or timeout hides it.

TF-079 portability scanning, TF-082 raw catalog fixtures and narrow symlink
capability handling pass their executed new paths. TF-083 Unicode resolution
still fails in Windows 42419357644: the retained diagnostics show intended
UTF-8 `c3a9` versus Git's `c383c2a9` representation at the failing root.
That identifies a boundary mismatch, not a safe license to coerce paths.
The Unicode test remains enabled; a lossless native path/process bridge is
still Blocked. MinGW's other failure is the GUI informational timeout.

Focused TSan 42422405960 on `9f5320b2` passes 1/1; it does not validate the
local full-matrix dependency refactor, provider failure changes or parser fixes.

## 2026-11-08 — Tests after the c115d386 continuation

### TF-087 — Scheduling fixture expected malformed known types to succeed

The tracker-schedule assertion at line 167 fails in OpenBSD, MinGW, all four
OpenSSL jobs and all three sanitizer jobs (run/job table in the compilation
inventory). Its empty byte string interval contradicts the now-executed typed
parser contract. The fixture now asserts malformed response, retained prior
interval and bounded failure scheduling. Negative integer intervals still
exercise valid typed clamping. The parser is not weakened to satisfy the old
expectation. Equivalent known-type fixtures, DHT, PEX and metadata handlers
were inspected. Implemented / static; Pending CI tests.

### TF-088 — Release assertion evaluation needs a direct regression

A successful MSVC suite still emitted CW-026. A shared source-level assertion
setup removes the command override while retaining evaluation. The new
common/selected test fails if NDEBUG remains defined or an assert operand
stops executing. Normal tests, tool tests and fuzzers use the same helper.
Prepared / not executed; Pending CI compilation and Pending CI tests.

RT-051 and RT-052 remain real product/CLI failure-path defects rather than
fixture expectations. Their regression extensions stay enabled. The older
unknown-opcode case now passes far enough to expose the independent EOF case.
MinGW Unicode and GUI version timeout remain unresolved; MSVC still skips
supervisor authentication after worker status 0xC0000142. API 29 remains
skipped. No passing sibling closes those coverage gaps.

## 2026-11-08 — Failures exposed after prior fixture corrections

### TF-089 — EOF regressions violated the Ping payload contract

Validation 42431903303 jobs 128582436615, 128582436820 and 128582436891,
and both OpenSSL compiler
matrices (run/job links in COMPILATION-ERRORS.md), SHA c3f5df53, fail
`test-privsep-linux-ipc.cpp:649`: sending Ping with payload "last" is rejected.
The registry permits zero payload bytes for Ping, Pong and Shutdown. A valid
empty-payload frame still has a nonempty transport record containing its
header. The test failed before reaching buffered EOF authentication.

Both buffered-close and wrong-identity-close fixtures now send a valid Ping.
The first explicitly rejects the old illegal payload, then sends a valid
frame on the same channel and checks type, request ID and empty payload.
Global searches found exactly these two nonempty control-message fixtures.
Production encoding, authentication, descriptor and EOF checks are unchanged.
Implemented / static; Pending CI tests. RT-052/RT-054 negative paths must
still execute on the revised source before closing their validation tasks.

### TF-090 — Compact destination fixture used the torrent info-hash width

The same sanitizer/OpenSSL runs, OpenBSD 42431852556 job 128582289517 and
MinGW 42431789062 job 128582095295 fail tracker-schedule.cpp:197 on c3f5df53.
The fixture allocated 20 bytes from infoHash.size(), but compact I2P peers
carry 32-byte destination hashes. RT-055 correctly rejects that malformed
range before committing the expected interval.

Use a typed IdentHash fixture. Added explicit 20-byte rejection checks retain
prior interval/counters and bound retry scheduling; a subsequent valid reply
clears the failure and restores normal scheduling. Valid minimum/maximum
interval cases remain. Equivalent HTTP/datagram compact-peer fixtures were
inspected, including existing tail-length rejection coverage. No production
parser tolerance is widened. Implemented / static; Pending CI tests.

The latest Perl lane passes 1230 assertions but fails its separate analyzer
step (CW-028). Windows Unicode conversion, GUI version timeout and worker
initialization remain unresolved; API 29 UI results are incomplete. Existing
negative tests and diagnostic failures remain enabled.

## 2026-11-08 — 0b7f0ab4 fixture and packaging outcomes

### TF-091 — Documentation growth changed debhelper compression

Packaging run [42440572453](https://github.com/daviduhden/openi2pd/actions/runs/42440572453),
Debian job 128608728103, SHA 0b7f0ab4 builds and installs both packages, then
DEP-8 fails `missing file: /usr/share/doc/i2pd/docs/i18n.md`. The exact uploaded
Debian data archive contains docs/i18n.md.gz and not the plain form. Its control
archive was inspected; this is not a maintainer-script failure or missing
CMake install payload.

Smoke now accepts plain or .gz representations for compressible Markdown
policy/README/palette documentation. Missing both forms still fails. License
paths and canonical changelog.gz remain exact. Analogous fixed compressed
expectations were reviewed, and the packaging source contract pins the
paired existence check. No compression policy or install manifest is weakened.
Implemented / static; Pending CI tests on the corrected package smoke path.

### TF-092 — Compilation and cancellation hide runtime verification

The latest sanitizer/OpenSSL lanes stop at CE-086; Android/OpenBSD/MSYS2 stop
at CE-085/CE-087. Therefore TF-089/TF-090 and RT-058/RT-059 runtime paths are
not validated by this batch. New atomic ownership, signed bit/encoding bounds,
Javadoc corpus, Android guard fixtures and Windows deadline controls are
Prepared / not executed locally and Pending CI compilation / Pending CI tests.

Android UI emulator/instrumentation never starts after its failed triplet
configure. Cancellation happens later; its cause is not inferred. MSVC is
canceled while compiling, and its always-run heap step finds no diagnostic
binary. The 294-byte artifact contains only debugger commands, not an executed
heap trace. Earlier 0xC0000142 and MinGW Unicode/version failures stay open.

The source-only static job and documentation/fuzz sibling jobs pass on the
remote SHA; Perl's seven executed suites pass 1270 assertions. Those results
do not run the new local eight-assertion corpus or resolve the failing Critic
step and cannot be attributed to later edits.

## 2026-11-08 — Executed `6390fdea` test inventory

Exact run/job/SHA references are recorded in
[the compilation continuation](COMPILATION-ERRORS.md).

### TF-093 — Standalone Perl contract omitted from independent discovery

OpenSSL 3/4, OpenBSD and all three sanitizer lanes fail the portability
inventory equality assertions at tools/tests/perl-portability.t:329/339
(assertions 609/610 on the sanitizer runners). MSYS2 reports the same cause
at its platform-dependent assertion numbers. CMake includes
`tests/android-javadoc-regex.t`, but both the explicit maintained inventory
and independent source discovery omit top-level `tests/*.t`. The first
mismatch is that path versus `tests/interop/check-vectors.pl`.

The explicit inventory now includes the Javadoc contract and independent
discovery scans top-level `.t` files. Exact bidirectional equality remains;
no test is removed or relaxed. Analogous script/module/subdirectory
inventories and both syntax/Critic consumers were reviewed. Existing CTest
and manual sanitizer/platform lanes exercise the correction. Implemented /
static; Pending CI tests. No local Test::More suite was executed.

### TF-094 — Native Windows paths bypass trusted source harvesting

MSYS2 42492792058, job 128768581923, fails eight assertions in
`perl-maintenance-checkers`: four missing-label fixtures return success
instead of `missing source literal`. `harvest()` in the native catalog
checker matches `/tui/main.cpp` and terminal suffixes directly against
File::Spec native paths. Backslash separators bypass tab/help/option/error
collection while ordinary catalog parity still passes.

The checker now derives a repository-relative slash identity with
abs2rel/splitdir solely for source selection. Native read paths and rendered
user operands remain unchanged. Both terminal adapters and an explicit
untranslated rendered-data control are covered in the existing fixture
suite. Related path-sensitive checkers were searched; no additional
same-form defect was demonstrated. Implemented / static; Pending CI tests.

The same MSYS2 job still fails the historical Unicode validator fixture and
an empty-output GUI version probe after 12 seconds. These remain separate,
unresolved findings; increasing budgets or skipping fixtures is not a fix.
Optional Perl quality job 128768654939 and Android UI job 128768637533 skip,
so no new Critic or device-validation result is available.

## 70f66977 blocked regression execution

CE-088 blocks the newly registered readiness and downstream native tests;
CE-089 independently blocks both TSan builds. Android unit/Lint/device steps
were not reached. Historical runtime families and optional-lane gaps remain
open. Packaging 42512607518 passes its five distinct jobs, and documentation
and agent-knowledge siblings pass at the executed SHA. Those results do not
validate local source changes or the later ab021821 launcher-only commit.

## TF-095: core Carp omitted after expanded Perl discovery

Observed at `9f2934f81edd16c6bcb9b3ebbac91474c7b318a1` in
[Validation 42545638623](https://github.com/daviduhden/openi2pd/actions/runs/42545638623),
Linux OpenSSL 3/4, OpenBSD 42545614356 and MSYS2 42545610680.
The perl-portability import assertion rejects `Carp`, imported by the
previously uncovered tests/android-javadoc-regex.t. Carp is a core Perl
module; the checker allowlist was incomplete, not the dependency policy.
The local allowlist correction retains the existing per-import regression.
Global inspection found one owning module allowlist. Implemented / static;
Pending CI tests. No dependency was added or interpreter requirement changed.

## TF-096: slow log-peer fixture assumes per-record readiness

At the same SHA, OpenSSL 3/4 GCC/Clang and Validation ASan+UBSan, TSan and
UBSan fail `test-privsep-log.cpp:953`, assertion `sink.IsOpen ()`.
Validation jobs are 128929923766, 128929923845 and 128929923871.
A peer draining one record per 400 ms does not guarantee POLLOUT before the
production one-second timeout: several records can be required by the
kernel readiness threshold. LogSink correctly detaches on timeout; this
assertion alone is not evidence of a production mutex or memory defect.

The prepared fixture fills the queue via actual zero-budget send failure,
parks the peer behind a control pipe, checks concurrent counter access and
then drains all queued records. Futures are joined and the child reaped
before final outcome assertions. The production timeout is unchanged.
Implemented / static; Pending CI tests. Scheduling-sensitive test behavior
must still be assessed by the same runner/toolchain lanes.

## 9f2934f8 positive evidence and remaining platform gaps

Readiness, deadline-mutex and torrent verification regressions pass all
three executed sanitizer configurations and OpenBSD. MSVC's selected suite
passes. Android job 128929903685 has DisplayTextTest 3/3 and confirmation
13/13, no failures; device job 128929904898 skips. OpenBSD's three worker
capability skips provide no later handshake evidence and emit no reason.
Windows still has Unicode fixture and GUI-version failures: the Git root
round-trip turns UTF-8 resume-with-accents into double-encoded mojibake,
then native abs_path fails; the console control reports its version while
the GUI probe times out with empty output. These remain separate open
findings, not resolved by passing MSVC sibling tests.

## TF-095/096: validated in the executed 80a487d5 lanes

[Validation 42553097403](https://github.com/daviduhden/openi2pd/actions/runs/42553097403)
passes all three sanitizer jobs. Perl portability and the controlled logger
fixture execute successfully; OpenSSL 3/4 and OpenBSD workflows also pass.
Child-stop budget, torrent availability and rate-scheduler controls pass in
the executed suites. TF-095/096 are Validated by CI at this SHA; pure budget
arithmetic is not evidence for every kernel shutdown path.

## TF-097: MSYS2 lane discovers an unrelated native Perl

[Windows 42553068552](https://github.com/daviduhden/openi2pd/actions/runs/42553068552),
job 128952711384, at the same SHA fails perl-agent-knowledge-contract's
Unicode Git-root round trip. The configure log finds
C:/Strawberry/perl/bin/perl.exe 5.32.1; the MSYS package inventory does not
contain Perl. Native Perl's CP_ACP filesystem/process boundary does not
match the UTF-8 contract, and the fixture also includes Chinese names that
cannot be repaired by a CP1252-only adapter. Decoding Git output twice or
skipping these cases would hide the mismatch.

The local workflow declares the MSYS perl package and passes its cygpath
interpreter path to CMake. Every i2pdctl TAP invocation now uses the selected
Perl directly, removing incidental prove-wrapper interpreter selection while
retaining the same Test::More assertions. Source guards pin the interpreter
boundary. Implemented / static; Pending CI tests. Native MSWin32 Unicode
support remains a separate limitation; an MSYS pass will not validate it.

The GUI i2pd-version timeout remains open. The TUI control does not link the
router libraries and cannot exclude router loader/static-initializer issues.
The exact artifact lacked the GUI image and matching DLLs. The prepared
workflow now retains them with PE/import/runtime resolution and CMake cache
before probing. This improves attribution; it is not a GUI startup fix.

## TF-098: YAML package metadata misclassified as interpreter stdin

Observed at `d63b6b9075e510d469cff1aab2dcdd6e664ea4d5` in
[knowledge 42667583150](https://github.com/daviduhden/openi2pd/actions/runs/42667583150),
job 129335273970, and OpenBSD, OpenSSL 3/4, Windows MSYS2 and all three
Validation sanitizer jobs. Diagnostic: build-windows.yml:49, interpreter
source from standard input is forbidden. The line is the literal package
name perl inside setup-msys2's install scalar, not executed code.

The local scanner masks only literal package lists in direct action inputs,
preserving line numbers. It continues checking executable run blocks,
unknown actions, nested maps and executable-looking package operands.
Accepted/rejected YAML fixtures pin these boundaries and secret-free
source diagnostics. No workflow workaround or warning suppression.
Implemented / static; Pending CI tests.

## TF-099: Cygwin executable suffix hides an earlier POSIX TLS shim

Windows MSYS2 job 129335208335 in
[42667563721](https://github.com/daviduhden/openi2pd/actions/runs/42667563721)
now uses MSYS Perl 5.42.3, exposing the i2pdctl TLS fallback fixture. The
runtime reports Config::_exe as .exe, while the intended fake OpenSSL is
an extensionless POSIX executable. The resolver skipped it and selected
real system OpenSSL, causing connection errors rather than parser results.

The local resolver tries the native image first within each absolute PATH
directory, then the extensionless POSIX wrapper. Native MSWin32 retains its
image-only behavior. Existing forty fallback controls remain enabled and
pin the intentionally extensionless shim. No shell or dependency added.
Implemented / static; Pending CI tests.

## TF-097 continuation: matching MSYS Git and genuine symlinks

The original Unicode repository case now passes, but newline/metacharacter
path and symlink cases expose mismatched host tools. Exact package inventory
has MSYS Perl but no MSYS Git. Native Git sees physical filename mappings
where Cygwin/MSYS exposes logical POSIX names. The local workflow explicitly
installs MSYS Git, pins its native CMake path and records bare-git resolution.
This is host-tool closure, not a native MSWin32 Unicode fix. See the primary
[MSYS Git guidance](https://www.msys2.org/docs/git/) and
[Cygwin filename mapping](https://cygwin.org/cygwin-ug-net/using-specialnames.html).

Default MSYS symlink deepcopy can report creation success without a link.
The local job requests strict native symlinks; the fixture verifies -l and
readlink before relying on it, retaining its prior platform failure/skip
boundary. No successful copy is treated as containment coverage. See
[MSYS symlink modes](https://www.msys2.org/docs/symlinks/).
Implemented / static; Pending CI tests.

## TF-100: missing-target doc links use an unsafe lexical projection

The same MSYS2 maintenance-checkers output rejects an outside missing link
but calls it merely missing. check-doc-links fell back from abs_path failure
to canonpath, which deliberately preserves parent components. The local
resolver walks the longest existing physical prefix, then normalizes missing
suffixes, following symlinks before parents and bounding link expansion.
Original-path existence remains mandatory: missing/../existing and regular
file/.. paths cannot become valid links through normalization. Added nested
missing and real-symlink escape fixtures. Global Perl search found no other
abs_path-to-canonpath failure fallback. Implemented / static; Pending CI tests.

The GUI version timeout remains unattributed. The exact GUI image and six
CLANG64 DLLs are now retained; imports resolve and main invokes global
constructors before RunI2PD. This narrows hypotheses but is not stack/entry
proof. Do not change RNG or startup behavior without that evidence.

## TF-100 continuation: fixture grammar bypasses containment

All latest runs execute `176aec32c6ac9deeae36f5142d05a9c284f5e94a`.
[Validation 42690770393](https://github.com/daviduhden/openi2pd/actions/runs/42690770393)
jobs 129409600455, 129409600562 and 129409600759 fail assertions 413–418
in `perl-maintenance-checkers`. Four OpenSSL compiler lanes, OpenBSD and
CLANG64 repeat the same failure, nine lanes total. Original diagnostic:
`physical symlink prefix cannot escape through a missing target`.

The fixture uses reference-style Markdown, outside the documented grammar
of `check-doc-links.pl`. No target reached the resolver; this is a fixture
defect, not evidence against physical-prefix resolution. Global search
found this fixture form only in that symlink loop. The local correction
uses inline links, absolute symlink authority and an existing outside-file
control before all three missing/parent-component cases. Existing six
assertions remain; two controls are added. Implemented / static; Pending CI
tests. Production grammar is unchanged.

At this SHA, MSYS Git/Perl closure, extensionless TLS wrappers and YAML
package scanner regressions pass. CLANG64 still separately fails
`i2pd-version` after 12 seconds without output. MSVC reports 17 tests with
one supervisor skip: worker exit 3221225794 (0xC0000142) before authentication.
OpenBSD reports 255 tests, one fixture failure and three capability skips.
Skipped handshake/shutdown paths remain unvalidated.

### Latest-ten evidence inventory

All rows use the exact SHA above. Full logs, job metadata and artifact
inventories were retrieved; failed logs and relevant artifacts supplement
the review. Historical run metadata totals 382; unavailable historical
diagnostics remain evidence gaps as previously recorded.

| Run | Workflow | Observed result |
|---|---|---|
| [42690778943](https://github.com/daviduhden/openi2pd/actions/runs/42690778943) | Instrumented OpenSSL TSan | Success |
| [42690770393](https://github.com/daviduhden/openi2pd/actions/runs/42690770393) | Validation | Three sanitizer lanes fail TF-100; fuzz/docs/static pass; Perl quality skipped |
| [42690762752](https://github.com/daviduhden/openi2pd/actions/runs/42690762752) | Linux | Install/consumer and I2P Java interoperability pass |
| [42690756912](https://github.com/daviduhden/openi2pd/actions/runs/42690756912) | Android | Arm64 build and 174 JVM tests pass; API 29 UI skipped |
| [42690751293](https://github.com/daviduhden/openi2pd/actions/runs/42690751293) | OpenSSL 4 | GCC/Clang each fail TF-100 |
| [42690742396](https://github.com/daviduhden/openi2pd/actions/runs/42690742396) | OpenSSL 3 | GCC/Clang each fail TF-100 |
| [42690736472](https://github.com/daviduhden/openi2pd/actions/runs/42690736472) | Agent knowledge | Success |
| [42690730284](https://github.com/daviduhden/openi2pd/actions/runs/42690730284) | Packaging/services | Success |
| [42690723606](https://github.com/daviduhden/openi2pd/actions/runs/42690723606) | OpenBSD 8.0 | TF-100 plus three capability skips |
| [42690715945](https://github.com/daviduhden/openi2pd/actions/runs/42690715945) | Windows | CLANG64 TF-100/startup timeout; MSVC success with worker startup skip |

## TF-101: registered source root points to the CMake entry

All rows below execute `029011279d1b7a454571fb600621f11d62f8a142`.
The first failure is `check-addressbook-rearm-lifetime.cmake:10 (file)`:
`file failed to open for reading (No such file or directory)` at
`build/libi2pd_client/AddressBook.cpp`. The script receives
`-DSOURCE_DIR=${PROJECT_SOURCE_DIR}` from `tests/CMakeLists.txt`; that variable
names build/, whereas PROJECT_ROOT names the repository. Full logs and
LastTest/JUnit artifacts confirm the same root cause in nine lanes.

| Run | Jobs | Result and configuration |
|---|---|---|
| [42699000692](https://github.com/daviduhden/openi2pd/actions/runs/42699000692) | 129438426292 / 129438426514 / 129438426539 | ASan+UBSan / UBSan / TSan: 260 of 261 pass; only root inspection fails |
| [42698967451](https://github.com/daviduhden/openi2pd/actions/runs/42698967451) | 129438302355 / 129438302720 | OpenSSL 3 GCC/Clang: same sole failure |
| [42698975123](https://github.com/daviduhden/openi2pd/actions/runs/42698975123) | 129438329139 / 129438329828 | OpenSSL 4 GCC/Clang: same sole failure |
| [42698942155](https://github.com/daviduhden/openi2pd/actions/runs/42698942155) | 129438207819 | OpenBSD 8.0: same root failure; worker identity/credentials/storage skip on root runner |
| [42698933379](https://github.com/daviduhden/openi2pd/actions/runs/42698933379) | 129438186341 | CLANG64: root failure plus independent GUI version timeout |

The previous TF-100 link fixtures pass. The direct workflow invocation
correctly passes $PWD and passes, which did not cover the registered argv.
Global search finds no other wrong root-variable registration. The local fix
quotes SOURCE_DIR=${PROJECT_ROOT}, pins that add_test command in the existing
build-system contract and adds an explicit root precondition in the script.
The registered CTest remains enabled everywhere; no test or coverage removed.
Implemented / static; Pending CI tests. This is not a sanitizer report.

Successful siblings include fifteen fuzz smoke targets, source documentation,
Skill checks, packaging/services, Linux installed consumers and reference
interoperability checks. Instrumented OpenSSL TSan run 42699008722 executes
only test-clientcontext-registry; it does not establish complete crypto
coverage. Linux interop run 42698992642 verifies Java reference/provenance and
vectors, not a live Java/C++ handshake. Optional Perl-quality/device jobs skip.

## TF-102: unexpected Windows worker exit was reported as capability skip

In run 42698933379, successful MSVC job 129438185880 reports
`worker pipe accept timeout (worker exited before authentication; code
3221225794)` then returns 77. This is 0xC0000142, an unattributed loader
failure. The same nested UI reduction occurs in the CLANG64 sibling before
its successful worker restart, so that reduction does not establish cause.

The local fixture classifies live startup/Ping/restart failures as failures.
Only directly observed nested-job assignment access denial can grant a
backstop capability skip. Independent live work still runs; failure dominates
skip. Checked outcomes preserve destruction and final handle verification.
Classification controls include the loader code and genuine denial cases;
source guards forbid skip/abort exits from the live cleanup scope.
Implemented / static; Pending CI tests. No production isolation is weakened.

Published diagnostic run [42719195489](https://github.com/daviduhden/openi2pd/actions/runs/42719195489)
was dispatched with heap_diagnostics=true on 02901127. It can provide missing
loader/entry evidence; it cannot validate any local correction described here.
