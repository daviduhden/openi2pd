<!--
See the LICENSE file at the top of the project tree for copyright
and license details.
-->

# Regression patterns

## Scope published regression success to the exercised failure boundary

At b001b969, RT-092/093 bounded copy and ElGamal controls pass, including
sanitizer lanes. Malformed-input and output-overlap tests do not inject a
provider allocation, digest or arithmetic failure. Keep those missing failure
seams explicit while extending ECIES checks; do not attribute the older
published tests to the new local code. For startup probes, require the script
marker and child initializer evidence before drawing conclusions from a
missing authenticated handshake or a debugger driver's own failure.


## Preserved geometry, failure publication and lifetime admission

- Allocation tier selection must include a preserved prefix, not just the
  logical payload. Bound source geometry and destination offset before
  subtraction. Test valid large offsets as well as malformed SIZE_MAX inputs;
  a well-formed source can still exceed a smaller destination's prefix space.
- Inspect each arithmetic intermediate through failure. Retire partial
  precomputed results, stage plaintext until integrity success and cleanse
  private-dependent context/point values before release. Positive round trips
  and input refusal tests do not substitute for provider/allocator injection.
- A failed debugger driver or a child exit code without a captured initializer
  stack leaves attribution open. Keep nonsecret probe authority distinct from
  normal authenticated workers, preserve required failure status and retain
  evidence before diagnosing tokens, mitigations or memory budgets.
- A weak generation token and stopping check are not owner admission. DHT/UDP
  timer redesign must cover per-wait correlation and Stop/restart barriers,
  while preserving independent disk-task ownership and reviewed lock order.
  These lifecycle changes remain Pending, not implemented in this increment.


## Deferred ownership, deterministic death and diagnostic privacy

- Compare a lifecycle assertion with its API ownership contract before
  changing product behavior. A retained PID after channel retirement requires
  later reap assertions, not hiding the PID or restoring fresh cleanup waits.
- Observe killed-child exit without consuming status before a failure RPC.
  A loop of mutating RPCs is neither a harmless liveness poll nor a fixed
  wall-clock budget; preserve the product's ownership of the wait status.
- CDB child tracing is allowed only for a closed nonsecret probe sharing
  production containment. Probe modes return before normal tests. Validate
  every command file, fixed argument list, deadlines and positive outcome;
  ordinary authenticated worker tests remain parent-only and mandatory.
- Factory tier selection must resist overhead overflow as well as append
  arithmetic. Oversized requests retain bounded fixed-capacity behavior;
  empty accepted input must not pass a null pointer to zero-byte memcpy.


## Storage deadline propagation and uncertain commits

Review Open/Begin, every chunk, final commit/release and abort under one outer
deadline; a bounded helper with two fresh phase budgets is still cumulative.
Until channel APIs retain immediate-at-expiry semantics, so RPC owners gate
expiry themselves and reject late success. Keep deferred child ownership while
revoking channel authority; do not introduce a fresh reap wait or claim rollback
when a commit acknowledgement is missing. Scope ordinary versus slow-peer and
allocator-injection coverage separately.

## Native success and platform startup remain separate

The 6d0127b7 Linux sanitizer successes cover executed ACK/CLI/storage controls;
they do not explain MSVC worker initialization. Inspect child startup handle
contracts against both CreateProcess paths and preserve an explicit allowlist
when transferring standard handles. Never infer absence of inherited secrets
from a comment or from merely supplying handle values in STARTUPINFO.
Capability skips retain exact artifact reasons and no post-skip claims.


## Entry parsing, explicit provenance and failure outcomes

- Resolve diagnostic PCs against the exact image/load base. A GUI subsystem
  does not establish whether main or WinMain ran; a modal stack is not proof
  of a loader/provider failure. Keep unrelated worker initialization separate.
- Registered defaults are not explicit CLI input. Test all credential keys,
  explicit empty values, flag order and help/version through one full parser.
  A reporter returning does not authorize continuing after refusal.
- Scan command/INI parse exceptions and unreadable-path diagnostics together.
  Require trusted outcomes and no synthetic sentinel in both output channels.
- Positive executable control plus exact refusal status prevents unrelated
  startup failures from masquerading as policy success. Sanitizer output fails
  even when its process returns the same code as deliberate refusal.
- Revoke timer authority before cancellation; preserve cancellation diagnostics
  without allocating during unwind. Guard expiry update and async registration
  as one transaction. Negative source fixtures vary names/whitespace/receiver
  spelling and retain valid socket controls. Execution remains pending CI.

## Transactional signing failure (RT-086)

Check every intermediate arithmetic result before publishing either component.
A successful zero nonce permits bounded retry; provider failure aborts. Cover
missing outputs and aliases without erasing borrowed inputs, and clear context
secrets before retiring the frame. Ordinary vectors and invalid-input tests
complement source contracts; they do not replace provider/allocation injection.

## Secret-state transitions and ACK admission

- Classify each intermediate at every arithmetic step. A final public scalar
  may contain private-dependent state on an earlier failure; clear-free its
  owner and keep verification's public inputs distinct in source guards.
- Retained timer success must carry owner authority and per-wait identity.
  Packet sequence zero cannot stand in for active admission. Test cancellation,
  successor waits, restart, final owner release without executor drain and Stop
  admission independently. Retire an empty ACK window's probe as well.
  Revoke fast-cache authority with registry removal; an ACK-stopped cached
  session must not be reused. Close socket receive admission before retirement
  completes, checking failed drains and nullable lookups in sibling paths.
- Stage allocating root metadata before publishing the corresponding file
  descriptor. Exercise both storage siblings; descriptor ownership alone does
  not guarantee coherent path/device/inode state after an exception.
- A failed dependency install does not exercise a prepared regression. Keep
  generator and package gaps pending while preserving passing sanitizer results.

## Provider, constructor and startup authority continuations

- When making a helper nullable, inspect initialization as well as parser
  callers. Reject missing coordinates before arithmetic; balance BN_CTX
  frames on every path and release constructor resources before throwing.
  Deep copies must be complete or empty, never partially authoritative.
- Test unsupported size/curve combinations with tiny real buffers, including
  SIZE_MAX. Invalid claimed lengths do not grant permission even to clear
  output. Keep positive supported-width controls and both provider branches.
- Extend exception reviews before the first IPC transaction: path conversion
  can throw after opening a directory capability. Scope the descriptor at
  acquisition and revoke partially adopted worker state on unwind, while
  preserving an already-running worker rejected before admission.
- A backtrace that drops addresses can hide every unresolved application
  frame. Preserve code return PCs with a closed diagnostic command list;
  negative fixtures must reject locals/arguments, child following, unbounded
  waits, absent caller PCs and lost CTest execution. Keep pre-prolog unwind
  limits and exact-image symbol resolution explicit.

All new execution and allocator-fault coverage remain pending manual CI.

## EA333 families requiring continued coverage

- Exercise invalid encoding inputs, non-invertible projective coordinates,
  null outputs and successful modern/historical encodings. A factory must
  reject invalid readiness rather than publish an object with zero key bytes.
  Keep output clearing and secret cleansing distinct from readiness.
- Trace remote handle admission through every subsequent allocation, including
  intervals between RPC exchanges. Exception retirement closes the channel
  before cleanup that can allocate, preserves the exception and cannot imply
  rollback. Source guards pin all four storage transaction seams; allocator
  fault injection remains a separate pending regression.
- Group repeated warnings by shared header/translation unit. Audit inherited
  methods across every backend when adding one override; constrain fixed-size
  OpenSSL int operands at compile time and reuse the bound for storage extents.
- Read JUnit skip entries before reporting execution counts. A successful
  root-run OpenBSD suite can omit worker authentication entirely. A Windows
  modal marker proves a dialog call, not its source caller or an RNG failure.


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

## 2026-11-08 — Distinguish fixture admission from the tested transition

- Before asserting buffered-close authentication, send a registry-valid
  frame. Retain a rejection control for the previous invalid operand and
  prove the endpoint still accepts a valid record afterward.
- Test valid 32-byte compact peer records, rejected 20-byte info hashes,
  unchanged state after rejection and recovery through a later valid reply.
  Keep malformed tails distinct from scheduling floor/ceiling behavior.
- A debugger's explicit crash marker, a nonzero test exit and a worker
  capability skip are separate outcomes. Inspect exact artifacts before
  classifying a heap diagnostic; lack of a marker does not certify startup.
- Fetch retained history with pagination and record empty log responses as
  gaps. Large artifact download failures must not erase already recovered
  logs or turn missing evidence into a clean result.

- Saturation must precede a narrowing boundary, not follow it. Preserve a
  uint32_t tracker count through an int64_t API and compare HTTP/datagram
  behavior at INT_MAX+1 and UINT32_MAX using the actual wire operand type.
  Negative/internal callers need separate controls so a widening does not
  silently change their established clamp-to-zero behavior.

- Carry a finite deadline as an absolute steady-clock time point across
  nested encoding/refusal operations. Recomputing milliseconds remaining and
  then starting a fresh public relative timeout can rebase the caller's
  budget after preemption. Source guards can pin propagation; runtime queue
  saturation tests are still needed and do not prove arbitrary scheduling.

## 2026-11-08 — Ownership retirement and bounded composition

- Test final-deleter reentrance while the synchronization wrapper is alive.
  Both retired stored values and failed-CAS expected owners must release after
  unlock; pointer equality alone does not prove shared ownership identity.
- Pair signed-index rejection with valid masks at multiple fixed widths.
  Encoding extents need zero/short/full controls, oversized and converted-
  negative rejection, plus valid recovery without altered bytes.
- Preserve one absolute Windows deadline through composed operations. Keep
  expired-but-immediately-completable I/O distinct from newly permitted waits,
  and retain cancellation reaping even when cleanup exceeds the wait budget.
- An overall canceled run can include an earlier real build failure. An
  always-run diagnostic missing its binary after cancellation supplies no
  heap-crash evidence; inspect the actual artifact and executed steps.
- Extended Perl regex layout must retain literal replacement spaces and
  opaque inline-tag bodies containing hashes, spaces and angle brackets.
  Keep positive, CRLF, generic/vararg and negative diagnostic cases together.

## 2026-11-08 — Readiness, inventory and platform source identities

- RT-063: readiness thresholds are hints, not proof that a smaller
  nonblocking write cannot succeed. Exercise a one-record drain with zero
  budget alongside a genuinely saturated failure. Poll only after retryable
  write failure and retain one absolute deadline, including partial imsg
  progress. Do not extend the test timeout to conceal a send admission bug.
- TF-093: discover a new test source class independently before comparing
  explicit CMake syntax/style lists. Preserve exact equality and registration
  of every maintained script; do not solve a manifest failure by dropping
  the new helper from quality coverage.
- TF-094: path-gated source analysis must separate native filesystem operands
  from canonical repository identities. Exercise missing trusted labels in
  each terminal adapter and a rendered-data control that must stay outside
  catalog harvesting. Catalog parity alone cannot detect skipped source
  tables on Windows.
- Android Overdraw: give opaque content a single background owner, check
  light/dark and phone/tablet qualifiers, and retain the splash role. Resource
  parsing establishes structure; Lint and device rendering remain separate
  manual-CI validations.
- Successful selected suites do not cover skipped supervisor startup,
  optional UI, Perl quality or PageHeap paths. Retain exact capability reasons
  where present and record missing skip reasons as an evidence gap.

RT-064 extends copied-callback review to inbound torrent accepts. A weak
owner prevents use-after-destruction, but only a distinct generation rejects
callbacks from an earlier registration after restart. Share admission with
revocation through handler publication, and close registration synchronously
before queued teardown. Test the registration gap while m_Stopping is still
false as well as destruction after deferred work drains.

### Integral continuation: generations and transaction-wide budgets

- Detach readiness callbacks before invoking arbitrary completions. Reentrant
  registration must not mutate an iterated vector, and a canceled old wait
  must not consume a replacement wait's queue. Test both external executor
  drainage and destruction of an executor owned by the destination.
- Bound synchronous refusal as well as the admitted queue: posting one error
  callback for every rejected request merely moves unbounded state elsewhere.
- Carry absolute IPC deadlines through admission, output and stale-reply loops.
  A per-message count cap does not bound repeated relative waits in time.
- Share a transient-key classifier between identity creation and cache reuse;
  exact-literal filtering is insufficient when the loader accepts a prefix.
- Test provenance rejection before parsing/cache mutation using a signed
  positive control, including upper DatabaseStore option bits.
- Initial torrent discovery needs current-generation durable state, not merely
  successful insertion. Coalesce two immutable writes and prove the earlier
  acknowledgement cannot release the later generation's network authority.

## Admission and presentation-state regression seams

CE-089's portable gate regression checks held refusal, immediate admission,
release handoff and four-thread exclusion without scheduling sleeps. Keep
absolute steady deadlines through admission and the subsequent transaction.
Torrent interest tests must distinguish wanted availability from active
requests: selection withdrawal cannot invalidate responses in flight, while
stop/removal revokes authority. Exercise periodic and HAVE/BITFIELD consumers
as well as explicit selection changes; successful compilation alone does not
validate these state transitions.

## Controlled saturation and bounded phase propagation

TF-096 replaces elapsed-send inference with an actual zero-budget saturation
failure and a separately controlled peer drain. A successful small send and
POLLOUT readiness have different thresholds. Keep the production deadline,
join borrowing threads, and reap children before final fixture assertions.
RT-069 adds absolute child-stop phase boundaries, signal-interruption review
and nonblocking startup reads. Pair pure budget arithmetic with the existing
child-reap fixture and the source guard; no pure test proves kernel shutdown.
BITFIELD replacement regressions must start with already-set bits of the same
size; an initially empty bitfield cannot expose stale availability.

## Idle timer ownership and exact capability evidence

A cancellation request does not drain a stopped executor. Test owner and
destination expiration before polling, and discard queued work only after
proving release. Distinct wait tokens must reject old successful timer
completions after rearm and restart; weak ownership alone does not establish
that generation invariant. Test extreme timer durations without overflowing
steady-clock arithmetic.

Capability skips require their original branch conditions and a fixed
non-secret reason. Returning 77 must not be confused with subsequent worker
handshake success. A console control with a smaller link graph cannot rule
out static initializers or DLL faults in the GUI router; retain the exact
image/dependency closure before making that inference.

## Host emulation, physical containment and confidential diagnostics

Host interpreter, Git and executable-wrapper semantics form one closure.
Test real filename/symlink behavior; symlink success without -l/readlink can
be deepcopy emulation. Physical containment and original-path existence are
separate checks: normalizing missing/../existing must not validate it.

Negative confidentiality controls require a positive logger sentinel and
fixed-outcome assertion, so disabled logging cannot pass them vacuously.
Unknown tokens and malformed numeric operands are arbitrary bytes, not safe
closed labels. Inspect multiline sinks and remote error payloads globally;
retain a weak owner once rather than checking expired before lock use.
## Negative fixtures must enter the intended parser

TF-100 at 176aec32 repeats across nine lanes because reference-style links
are ignored by design. Preserve supported inline grammar and assert an
existing physical escape before malformed/missing-target variants. A failing
negative assertion does not establish that production validation ran.

## Admission identity survives cancellation and correlation-key reuse

RT-075 gives every tracker request a monotonic nonzero authority. A cancelled
operation can still have successful callbacks queued; matching infoHash and
tracker slot alone can retire the next request. Carry authority through dial,
write, read and deadline completion, reject stale successful bodies before
processing, and preserve counters across restart. Registry tests cover
release/reacquire and source contracts pin every production callback stage.

## Validate key pairs and classify worker failures before reporting success

CRY-236 pair tests distinguish modern keys, genuine historical scalar keys
and arbitrary mismatches. The old fixture deliberately accepted an unrelated
public key; replace that expectation with invalid-backend/cleared-output
assertions plus successful controls. Check serialized pair rejection too.

TF-102 reserves capability skips for observed native denial with matching
topology. A successful spawn followed by loader exit, failed Ping or failed
replacement must fail the regression. Run independent live work even after
a helper capability denial, let failures dominate skips, and drain ownership
before final handle assertions. A job returning green via 77 proves no
subsequent handshake/shutdown behavior.
