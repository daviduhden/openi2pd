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

- **Volatile publication:** inspect both the predicate and result under one
  mutex; volatile is not cross-thread synchronization. A callback notifying
  a caller-local CV must finish notification before the waiting caller can
  destroy that CV. Test both async-to-sync overloads with a live executor.
- **Stop notification:** an atomic stop flag plus a notify-only callback can
  still miss a wakeup between predicate evaluation and parking. Prefer the
  standard stop-token-aware wait and distinguish timeout from cancellation.
- **Accepted resource ownership:** a temporary channel cannot own the same
  HANDLE as a reusable listener. Exercise a terminal malformed-frame close
  followed by successful re-authentication; semantic role rejection alone
  does not reach the transport-close path.
- **Shared deadline:** peer inspection, first-frame authentication, rejection
  backoff and retry sleeps consume one original budget. Never grant a fresh
  phase timeout when the outer deadline is almost exhausted.
- **Failed provider output:** constructor candidates must remain private until
  entropy succeeds; persisted state must not revive failed initialization.
- **Package warnings:** inspect the exact archive, provide actual manuals,
  and test both alias and shared-target ownership. A successful Lintian step
  configured to fail only on errors may still report project warning debt.

- **Windows handle baseline:** a fail-closed test must validate configuration
  before creating profiles/tokens/jobs and compare handle counts after every
  terminal path.
- **Package remove/purge:** seed realistic state in a non-empty administrator
  subdirectory, assert before removal, then assert conffile and state survival;
  purge must remove only the owned persistent tree.
- **Sanitizer/fuzzer:** do not mark a suite passed merely because the process
  exited zero; scan the log for sanitizer reports and upload the exact
  artifact.
- **Shared test-support warning:** a warning in a translation unit linked by
  every test target can appear repeatedly without representing separate
  defects. Fix the owning helper once, search equivalent APIs in sibling
  helpers, and keep the warning pending until the matching compiler lane runs.
- **Platform hardening matrix:** when a manual job enables `WITH_HARDENING`,
  assert the compiler and linker mitigations for that platform, with
  architecture-specific flags guarded explicitly. The Windows contract must
  distinguish MinGW PE linker flags from MSVC stack/SDL/CFG and ASLR/DEP
  options; a configure warning indicates a missing security policy even when
  compilation succeeds.

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
- **Service start/stop ordering:** serialize duplicate Start, concurrent
  Start/Stop, accept rearming and owner-thread teardown with one lifecycle
  state. A timeout must not fall back to mutating socket-owning session state
  from another thread; regressions should control the interleaving with
  barriers instead of relying on scheduler timing.
- **Perl checker subprocesses:** fail `check-vectors.pl` when any
  `UNEXPECTED-PASS` reveals a stale known-gap marker. Decode `waitpid` with
  `WIFEXITED`/`WIFSIGNALED`, retry `EINTR`, and report wait errors separately;
  shifting the status byte alone can map a crashed checker to success.
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

## Patterns from the current sanitizer and platform batch (2026-11-02)

- **Borrowed parser results:** if a parser returns `string_view` or spans into
  its input, fixture storage must outlive every assertion on the parsed
  result. Keep a named input owner and search all tests retaining parsed
  objects constructed from temporary strings.
- **Integer-to-enum validation:** fuzz/configuration and rejection tests must
  pass the raw integer through a validator before constructing an enum. Do
  not create an invalid enum merely to reach a default switch branch.
- **Sanitizer stack triage:** record the first invalid access and complete
  stack per test. A sanitizer finding in one test does not classify other
  assertion failures in the same CTest lane; preserve their independent
  causes and evidence.
- **TSan lifecycle deadlines:** distinguish a timeout under TSan from a
  demonstrated data race. Capture lifecycle phases and executor state; do not
  raise timeout budgets or infer production races from `use_count()` alone.
- **Sequential `io_context::poll()` fixtures:** `poll()` drains all ready
  handlers and may leave the context stopped after the last work item. Before
  posting another operation, call `restart()` only if `stopped()` is true;
  do not use repeated fixed-count polls as a substitute. Pin completion and
  cancellation through explicit ready-queue drains (`TF-049`).
- **Concurrent reader tests:** thread creation does not prove a reader ran.
  Synchronize after its required snapshot/observation and before the writer
  mutates shared state or requests shutdown; do not use sleeps or iteration
  counts to manufacture overlap (`TF-058`).
- **EdDSA sanitizer failure:** preserve the current one-byte stack
  underflow as unresolved until a symbolized stack identifies the read and
  its caller. Keep valid-signature verification as a positive control and
  malformed-signature cases as separate negative controls.
- **IPC encode/decode symmetry:** semantic path constraints must be enforced
  by both the encoder and decoder. Pair positive absolute-path round trips
  with empty/relative rejection on both sides.
- **Ancillary data without a frame:** consume zero-length seqpacket records
  without treating them as EOF, but reject and close any descriptor attached
  to an empty record. Assert descriptor-count stability and a subsequent
  valid exchange on the same channel.
- **SOCK_SEQPACKET recovery bounds:** test frame-buffer restoration with the
  smallest record that exceeds the protocol header; do not use maximum
  protocol payloads to assume a kernel's maximum atomic Unix-socket record.
  Keep maximum encoded-payload checks at the codec layer (`TF-060`).
- **Append-only log sinks:** validate the opened handle, not a chmod-then-
  reopen pathname. Reject symlink/reparse, non-regular and multi-link targets;
  apply private mode/DACL on that same handle, and test rotation by reopening
  a distinct ordinary file without losing records (`SEC-034`).
- **Sanitizer source attribution:** retain the exact failing test executable
  with debug information and configure the matching LLVM symbolizer alongside
  CTest XML/logs. A stack frame's nearest stack object does not identify the
  source expression that accessed it; leave the cause unresolved until the
  exact artifact yields symbols (`RT-027`).
- **Filesystem identity tests:** keep the old socket descriptor alive while
  unlinking and rebinding its pathname so inode reuse cannot collapse the
  old and replacement identities into one fixture state.
- **Worker crash-loop backoff:** a setup handshake is not a stability
  interval. Preserve retry count/backoff across successful replacement
  handshakes; pin successive short-lived worker failures and a quiet-window
  reset separately.

### SHA-512 fixture output must own 64 bytes

RT-027/TF-069 exposed a test helper accepting an unbounded output pointer
while its caller grouped the digest with 32-byte public-key arrays. Return
an algorithm-sized array from reference hash helpers, and inspect both
provider output and downstream scalar-decoder lengths. An ASan location
beside a point object can originate in an adjacent undersized digest.
The corrected EdDSA fixture remains pending manual sanitizer/platform CI.

### Compare restart failures with the executed source revision

TF-068 repeated TF-053 because published `56f17c0f` still reset the restart
budget during configuration; the removal existed only in the local diff.
Check `git show SHA:path` before interpreting a repeat as evidence against
an unpublished fix. Elapsed backoff or an idle dead worker must not earn
stability credit; matched authenticated replies establish continued service.

### Destructor teardown cannot acquire its final shared owner

CONC-024 found `SocketsPipe` destruction calling `shared_from_this()` through
Terminate. The equivalent SOCKS destructor also called registry notification
that way. Split resource release from notification or use optional weak-self
locking; use error-code descriptor closure on destructor-reachable paths.
Prepare unstarted destruction and repeated termination coverage. These are
source findings, distinct from the unsymbolized historical leak reports.

### Isolate named-pipe exclusivity from instance capacity

A first-instance flag regression must leave capacity for a second ordinary
instance and verify that control succeeds without the flag. Capture the
failing API error immediately and print its numeric value before asserting
the documented denial. Do not broaden accepted errors when an exact-run
artifact omitted the value. Keep MSVC heap-corruption exits separate from
ordinary assertions; another toolchain passing does not close them.

### A callback queue bound must exist in the executor

The service SAF lane was documented as bounded but used an unbounded
single-thread executor. A blocked provider could retain arbitrarily many
URI and callback tasks. The new factory admits one running task and sixteen
queued tasks, rejects excess work and never runs provider code on the UI
submitter. The prepared JVM test holds the worker behind a latch and checks
rejection, recovery and shutdown. Interruption remains a request, not proof
that a provider stopped.

## Reused startup publication across generations

A condition-variable predicate left true after join can authorize the next
worker before its own running state is published. Trace reset, launch,
publication and first enqueue as separate events. Re-arm only after the old
worker has joined, under the same lock used by its wait. The logger's TF-070
fix prepares both repeated restart/destination assertions and a compile-free
source-order guard; neither substitutes for manual sanitizer execution.

## 2026-11-07 cross-component regression families

- RT-038: valid signatures can still violate deterministic known-answer
  output. Compare provider and fallback signers against the complete RFC
  vector; never weaken equality to hide nonce derivation differences.
- RT-039: every overlapped wait exit must either observe completion or cancel
  and reap before stack buffers/events disappear; WAIT_FAILED is an exit too.
- RT-040: stopped flags reused across restarts can revive old tasks. Use fresh
  authority and serialize already-admitted persistence before final snapshots.
  Exercise queued stale tasks and teardown, not only successful callbacks.
- RT-041: parser failure cannot double as container termination. Require a
  value after every dictionary key, including nested/list/metainfo consumers.
- TF-076: shifting a wait word discards signal failure. Check the complete
  status and test a producer that emits valid data before abnormal termination.
- TF-078: always-upload cannot recover files left inside a failed VM action.
  Inspect the pinned action's failure/copyback ordering and preserve exit codes.
- RT-032: phase-marker completion localizes detection, not the corrupting
  instruction. Require exact stack/image evidence before attributing heap
  damage or closing a historical sanitizer family.

## Post-publication lifetime and authority review

- Namespace-static containers called by another translation unit's destructor
  require explicit construction/destruction dependencies. Review all producer
  destructors, not only the first crash stack; keep persistence shutdown real.
- Moving a cache to longer-lived state also moves the lifetime of its values.
  Borrowing pool deleters must not reference a shorter-lived owner. Retain
  pool authority in external profile handles and test release after owner exit.
- Fixture teardown is part of the ownership graph. A registered streaming
  destination needs explicit Stop; a queued disk task needs a running/drained
  executor. Assert weak expiry without removing production ownership edges.
- Deferred cancellation may complete after lexical callback state disappears.
  Completion state must carry authority through buffer/stream destruction.
- Separate configured IPC topology rejection from the unconfigured predicate's
  deferred kernel authentication; keep negative tests for the explicit policy.
- A successful ratchet test cannot close an unrelated torrent leak. Preserve
  exact test names, allocator totals and stack origins when revisiting history.

- Byte-oriented fixtures must open output in raw mode on native Win32;
  implicit newline translation can invalidate catalogs before parsing starts.
- Core portability operations may throw unsupported stubs. Probe the narrow
  symlink operation, retain all other cases and record the capability reason.
  Do not convert a filename-encoding defect into a capability skip.
- Native path conversion is security-sensitive: decoded Git UTF-8 is not
  automatically a valid Win32 filesystem/process operand. Reject lossy
  question-mark substitutions and retain Unicode regressions while blocked.

## 2026-11-08 — Failure publication and parser transactions

- Separate callback skip semantics from recognized-field validation. A typed
  extractor's zero must record malformed state before generic fallback can
  skip it. Review metainfo, extension maps, trackers, DHT and PEX together.
- Parse negotiation and tracker responses into scratch state. Commit only
  after the entire document validates; malformed updates cannot consume the
  handshake slot or reset peer/failure accounting.
- A provider failure is not a valid random zero. Use independent readiness,
  wipe partial buffers and stop publication; never substitute a clock seed.
- Exercise byte-source failure per invocation, without a process-global
  provider hook. Pin exact result types so unsupported C API values cannot
  accidentally convert into success.
- Rejection samplers must propagate failure immediately. Do not require every
  sample from a finite random space to be unique in a regression.
- Stage nested framing/encryption randomness before publishing correlation
  and onDrop authority. Roll back only the matching pending owner on exception,
  and release final owners outside correlation mutexes.
- Allocating a phony hop must finish before exposing its raw chain link;
  failed allocation must leave the existing topology intact.
- A successful instrumented dependency lane supports attribution of an
  uninstrumented-library report, but does not certify a different matrix or
  prove that all races are absent. Keep full CTest coverage and exact images.

## 2026-11-08 — Ordering at transaction boundaries

- EOF is not an unauthenticated data frame. Reject ancillary anomalies first,
  classify terminal zero reads, then authenticate live records. Buffered data
  plus hangup must still follow the data-frame authentication path.
- Authenticate before a refusal response or violation-counter update. A
  deadline belongs to the whole receive operation, including error replies;
  nested sends must use its remaining budget, not a new fixed wait.
- Validate an entire Retry payload before version, crypto or token mutation.
  A valid first block does not authorize forbidden trailing blocks.
- Validate fixed-width peer ranges before updating any tracker statistics.
  Exercise malformed tails after a complete hash, empty input, valid multiple
  hashes and the same public API reached through both tracker transports.
- A correlation key is not callback authority. Retain a unique local attempt
  identity across delayed completions, match it on rollback/drop and refuse
  live key collisions. Reuse the same ID, peer pair and timestamp in the
  regression so correlated field comparisons cannot accidentally pass it.
