<!--
See the LICENSE file at the top of the project tree for copyright
and license details.
-->

# Regression patterns

Use these patterns when a CI diagnostic is fixed; add a focused test rather
than relying on the original failure to recur.

## Source and compile regressions

- **Namespace ownership:** a test that uses domain constants must include the
  declaring header and import or qualify the actual namespace. Search sibling
  tests for the same symbol family.
- **Dependency formatter:** never call Asio `address::to_string()` directly on
  POSIX; route through `i2p::util::net::FormatAddress`/`FormatEndpoint` and
  add a compile-free search for direct calls.
- **Directional IPC:** pair every `Send` direction assertion with a matching
  `Receive` test for core-to-worker and worker-to-core traffic; the typed
  `Error` refusal must be sendable by both sides.
- **Doxygen:** comment-only edits need the source-docs lane; escape
  placeholders and keep `@param` names equal to declarations.

## Runtime and package regressions

- **Windows handle baseline:** a fail-closed test must validate configuration
  before creating profiles/tokens/jobs and compare handle counts after every
  terminal path.
- **Package remove/purge:** seed realistic state in a non-empty administrator
  subdirectory, assert before removal, then assert conffile and state survival;
  purge must remove only the owned persistent tree.
- **Sanitizer/fuzzer:** do not mark a suite passed merely because the process
  exited zero; scan the log for sanitizer reports and upload the exact
  artifact.

## Process

Each correction records the exact run/job/SHA and the first diagnostic, then
searches for semantic equivalents across platforms, overloads, headers and
tests. Keep historical evidence separate from local static fixes; a skipped
target or a build that stopped early is not a pass.

## Patterns added by the `63c42ca4` batch

- **Component send before start:** any singleton that owns a service/context
  must reject work before that context exists. Pin it with a test that calls
  the send path on an unstarted instance and expects a dropped message rather
  than a crash, in addition to the offline flag.
- **Zero-hop paths:** an outbound path with no reply hop is created and
  recreated without an inbound tunnel. Assert the shared
  `OutboundPathNeedsInboundTunnel()` predicate in both directions, so a future
  refactor cannot reintroduce the inbound requirement in only one call site.
- **Buffer suffix writes:** a checksum/IV concatenation next to a fixed-size
  cell needs a negative test where the backing storage ends exactly at the
  payload and a positive test at the exact required boundary.
- **Address validity vs descriptor validity:** a signature-valid RouterInfo
  with no usable address is parseable and storable for the Java grace window.
  Assert `!HasValidAddresses()`, `!IsPublished()`, `!IsReachableBy()` rather
  than `IsUnreachable()`.
- **Artifact names:** every cache fixture builds the canonical name
  (`<shard>/routerInfo-<b64>.dat`, `profile-<b64>.txt`,
  `destinations/<b32>.<type>.dat`); the prefix-less spelling is asserted
  `Foreign`, never `Load`.
- **Direct fallback storage:** a test that exercises the no-follow atomic write
  must first call `i2p::fs::DetectDataDir`; otherwise the write fails before
  the behaviour under test and the case is vacuous.
- **Tribool:** read Boost `tribool` through `indeterminate()` plus an explicit
  `static_cast<bool>`, never through `!=`/`!`, and split "not indeterminate"
  from "is true" so a failure names the state.
- **CLI streams:** a tool validates every section before its first stdout byte;
  `-h` is stdout/0, usage and runtime errors are stderr/non-zero, and optional
  sections succeed with a stderr note instead of a failure.
- **Reserved-range admission in inspection tools:** a tool that reports what a
  file publishes disables the dialing admission policy for its process and
  keeps the public-endpoint policy only where it generates rules.
- **Windows rejected-handshake race:** when the accepting side rejects on
  identity before reading, the client's write can fail; the test tolerates the
  write and asserts the server-side rejection, not both.
- **Restart ladder arithmetic:** quote the ladder total from one
  `TotalLadderDelay()` authority and pin the bounds; never hand-sum the
  constants in a test or a comment.

## Patterns added by the `dc5540ae` / `c1125a75` batch

- **Wire-form, not only round-trip:** a RouterInfo regression should assert the
  exact length-prefixed option bytes (`pq=4,3` as one value), so a future
  reader that tolerates a split encoding cannot hide a writer regression.
- **Fixture geometry:** a hand-sealed lease-set fixture asserts
  `buffer.size() == header + lenOuterCiphertext` before signing and includes a
  negative control that flips a ciphertext byte.
- **Capacity vs declared length:** a cell test covers the pool layout
  (`TUNNEL_GATEWAY_HEADER_SIZE` reserved), the received full-cell layout and a
  short declared length, so a guard cannot reject a legitimate transit cell.
- **Transactional parsers:** a truncated tracker response must leave the last
  known good peer list, and a fresh slot must stay empty; assert both.
- **Windows attribute ownership:** a spawn regression runs under an existing
  Job Object and pins that kill/die/process/memory limits are always set,
  that a missing root/image fails closed, that the SID survives reuse, heap
  churn, a move and a DACL grant, and that the reduced UI mitigation is
  reported rather than silently accepted.
- **Informational flags:** the version driver checks both `--version` and
  `--help`, requires termination and exit status on every platform, requires
  a captured banner to be complete, documents the Windows empty-answer
  observability limit, and keeps each probe's budget below the CTest
  timeout.
- **Digest nonce count:** every expected RFC 7616 response is computed for the
  `nc` it is used with, and an older `nc` with a valid digest is asserted
  `Replayed`.

## Doxygen comment regressions from the `41320539612` continuation

- A C/C++ documentation fixture now covers Javadoc inline tags
  (`{@code ...}` must be rejected, backticks accepted), balanced and
  unbalanced `@code`/`@verbatim` blocks, and a documented parameter name that
  the declaration does not have.
- Array parameters are a positive fixture (`char * argv[]` yields `argv`), so
  the completeness and undeclared-name rules stay compatible with `main`-like
  signatures.
- `check-build-system.cmake` pins the fixture markers, so the rules cannot lose
  their only static proof.

## Patterns added by the `4e776a39` batch

- **Byte-span helper:** a test that calls a byte-span API from string data
  keeps one borrowing helper and uses it at every call site, including
  temporaries, so the element-type boundary is explicit.
- **Fixture-name guard:** `test-tools-regressions` rejects a Win32 reserved
  device stem before running a tool, and every written fixture is pinned with
  `assert_file_hex` so a discarded write cannot look like a decode failure.
  Binary tool stdout goes to a file, never into a CMake variable.
- **Bounded container seams:** `test-tui-protocol` pins the wire-integer to
  tracker-tier index, the maximum tier table, and every protocol limit plus
  its terminator against the daemon's fixed `AdminCommand` arrays.
- **Live Windows worker:** the supervisor test spawns the test binary through
  `RunWindowsWorker` with the restricted-token supervisor role, exercises
  the nonce/role handshake, and checks the handle count on pass *and* skip.
- **Windows informational path:** the version driver probes both flags with
  bounded budgets; the Windows workflow prints subsystem/imports/ldd and a
  pre-CTest probe so a loader-level hang is distinguishable from a router
  start.

## Preventive regressions added by the 2026-11-01 audit

- **Channel refusal recovery:** `test-privsep-protocol` sends a header-only
  frame with a forbidden descriptor, requires a decodable request-id-correlated
  `Error`, then sends a maximum-size request and asserts it arrives intact
  (the old code shrank its buffer to 16 bytes). It also pins that an
  unknown-type frame is fatal with no answer.
- **Descriptor-free log sink:** `test-privsep-log` configures a real forked
  worker through `LogSink::Configure(true, "", …)` and requires the handshake
  to succeed, which fails if the sink sends `ConfigureLogFd` without a file.
- **Bounded stale drain:** `test-privsep-worker-identity` feeds
  `ClassifySupervisorReply` more stale ids than the budget and requires a
  `Busy` fault instead of an unbounded loop.
- **Remove-frame shape:** `test-datastorage-protocol` pins the exact 50-byte
  removal frame and that `Error` is receivable by the worker while a reply or
  a core-side request is not.
- **Transports lifecycle:** `test-transports-prestart-send` pins the
  generation truth table and drives a two-thread
  `RestrictRoutesToRouters`/`IsTrustedRouter` loop so TSan sees a
  re-introduced unsynchronised emptiness test.
- **Admin queue stop:** `test-tui-ipc-regression` blocks the executor, queues
  a second command, stops the queue, and requires the waiter to be released
  with `Unavailable` in under 10 s of a deliberately 30 s budget; it also
  pins `Start`/`Stop` idempotence and restart.
- **Windows text policy:** `test-tui-terminal` pins strict UTF-8 rejection,
  encoding and the column budget through the portable `TerminalText.h`, so
  the rule is checkable on every host, not only Windows.
- **Proxy classification:** `test-proxy-auth` pins v4-mapped/compatible IPv6
  spellings, the extended private table, and allowed public literals/names.
- **Jump-link validation:** `test-addressbook-merge` requires an undecodable
  `.b32.i2p` jump link to be neither stored nor persisted.
- **Android boot policy:** `BootResumePolicyTest` pins that a broadcast never
  resumes a stopped intent and that package-replace ignores the autostart
  marker; the instrumentation test sends an explicit `BOOT_COMPLETED` and
  requires the persisted intent to stay stopped.

- **Record signature authority:** the routerinfo transaction test round-trips
  an offline P-521 `router.keys`, requires `CreateBuffer` to install nothing,
  and requires `SaveToArtifact` to refuse a foreign/empty/non-hash name
  before touching the filesystem.
- **Encrypted outer layout:** the encrypted-freshness test keeps a decrypted
  type-5 object's outer marker, asserts freshness against the outer published
  time, and re-parses a new type-5 buffer as an outer layer.
- **SAM input secrecy:** the control-framing test pins the unexpected-message
  diagnostic (length/field count only) and the shared session-name rule.
- **Legacy accesslist:** the filter test requires an all-invalid value to
  produce the deny-by-default definition and a blank value to stay absent.
- **Tunnel resolver lifecycle:** the stop test arms a named upstream, stops,
  and restarts while checking that a stale completion cannot clear the new
  resolver.
