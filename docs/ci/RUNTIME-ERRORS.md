<!--
See the LICENSE file at the top of the project tree for copyright
and license details.
-->

# Runtime error inventory

## 2026-11-05 preventive and historical continuation

### RT-034 — Static cross-thread publication and cancellation defects

- **Origin:** preventive equivalent-pattern review, not a newly executed
  sanitizer report. Latest sanitizer jobs stop at CE-076.
- **Confirmed code:** `ClientDestination::CreateStreamSync` used volatile
  completion and accessed its stream outside a common mutex. AddressBook's
  volatile `m_IsLoaded` crosses subscription/admin threads. Reseed's separate
  notify-only stop callback could miss cancellation between predicate and park.
- **Correction:** publish stream and completion under one mutex and use a
  predicate wait; keep notification inside the lock so the caller-local CV
  cannot be destroyed before notification. Make the address-book flag atomic;
  this alone does not grant synchronized access to storage. Use the standard
  stop-token-aware condition_variable_any wait in the reseed retry interval.
- **Global review/regression:** searched volatile flags, condition variables,
  callback publication and cancellation. New test-stream-creation-sync covers
  both production overloads with a live executor and unready destination;
  existing reseed lifecycle tests and source contracts remain enabled.
  Own-executor synchronous calls now fail before posting instead of deadlocking;
  contracts direct those callers to callback overloads and the regression
  covers both rejected synchronous forms. Implemented / static; Pending CI
  compilation / Pending CI tests. Complete storage lifetime authority remains
  separate work.

### RT-035 — Static unchecked DHT identity entropy failure

- **Confirmed code:** TorrentsDHT constructor ignored RAND_bytes failure
  before routing-table creation. This is a static provider-failure defect,
  not evidence of a generated weak identity in a real run.
- **Correction/consumers:** clear failed partial IDs, leave routing table
  absent, reject Start before persisted-state loading or receiver/timer setup,
  and publish only initialized DHT candidates from TorrentsTunnel. No predictable
  fallback, capability or port advertisement is introduced.
- **Regressions/state:** source contracts pin generation/start/publication;
  provider-failure injection remains Pending CI tests. TunnelConfig unchecked
  entropy remains a separate open construction/failure-contract investigation.

### RT-036 — Recovered older sanitizer families

- [39222148542](https://github.com/daviduhden/openi2pd/actions/runs/39222148542),
  SHA `0cf32519b5841f995bbd6b77849664caae0b225f`, ASan/UBSan
  `117959731699`: test-http-res leaks 248 bytes / two allocations; test-eddsa
  has an OpenSSL allocation leak of 1,048 bytes / two allocations; the
  signature-identity-hardening truncated-certificate fixture writes two bytes
  beyond its old five-byte extension. Current fixture reserves six bytes.
- [39215938005](https://github.com/daviduhden/openi2pd/actions/runs/39215938005),
  SHA `e37a4325da4de96e09a5f881185b5dba84032e07`: TSan `117939888841`
  reports NSS crashes in tunnel-pool tests; null identity observations belong
  to these historical fixture/lifecycle families. Do not infer a current root
  from the external frame alone.
- **Later exact evidence:** Validation 41364867079, SHA `56f17c0f`, ASan/UBSan
  `125022771190` and TSan `125022771257` pass test-http-res,
  test-signature-identity-hardening and test-tunnelpool-zero-hop. Preserve those
  observations without attributing them to today's source. They do not close
  the distinct EdDSA provider leak, RT-027 or current RT-031 ownership reports.

### RT-037 — Static Windows accepted-pipe authority and deadline defects

- **Confirmed code:** a candidate WindowsChannel borrowed the listener's
  owning HANDLE. Terminal Receive errors closed it while WindowsServer still
  retained the same value. The server could disconnect/close a stale handle.
  WorkerHello also received a fresh five-second budget after connection and
  peer inspection, exceeding the outer accept deadline.
- **Correction/global review:** duplicate accepted handles in both Accept
  paths; candidates own only their duplicate and the server retains its
  listener for rejection/re-arm. On success close the original and transfer
  the candidate alone. Bound hello, rejection backoff and connect retry waits
  by the original remaining budget. Rejected candidates no longer Release
  borrowed ownership. Privilege, identity and nonce checks remain mandatory.
- **Regressions/state:** malformed transport hello followed by a valid
  reconnect/Ping/Pong exercises terminal-close and re-arm. Source contracts
  pin ownership/deadline seams. Supervisor failures now retain only a fixed
  child-liveness outcome or numeric exit code before teardown, never nonce or
  command-line operands. Implemented / static; Pending Windows CI tests.
  This finding is not yet the demonstrated cause of RT-032 or the observed
  supervisor startup skip.

Runtime diagnostics are separated from compile/link failures. A process that
exits zero can still have produced a sanitizer report; a fail-fast test exit
is not a compile error.

## Current observations (`00929cd1`)

### RT-001 — Windows IPC accepted-frame assertion

- **Run/job:** Windows `41229371203`, MSVC job `124620132332`.
- **Test:** `test-privsep-windows-ipc`, assertion at line 735.
- **Diagnostic:** `Assertion failed: delivered.has_value() &&
  delivered->has_value()`; process terminated with `0xc0000409`.
- **Cause:** the test sent a request from an unbound worker-side channel; the
  core refused the frame but could not send the promised typed `Error`
  because core-side `Send` only allowed request types. The max-payload case
  also exercised the wrong direction.
- **Correction:** the channel already permitted the core-side `Error`; the
  max-payload request is now sent from the core to the worker (CE-049).
- **State:** corrected by static analysis; **Pending CI tests**.

### RT-002 — Windows supervisor handle-count assertion

- **Run/job:** Windows `41229371203`, MSVC job `124620132332`.
- **Test:** `test-privsep-windows-supervisor`, assertion at line 344.
- **Diagnostic:** `Assertion failed: HandleCount () == baseline` after a
  Storage role without a delegated write root.
- **Cause:** `WindowsWorkerSupervisor::Start` created the AppContainer before
  validating the write root; one-time profile/runtime handles remained in the
  process after the terminal failure.
- **Correction:** validate the write root before creating the container; the
  terminal path still releases every owned object (CE-050).
- **State:** corrected by static analysis; **Pending CI tests**.

### RT-003 — Debian remove/purge fixture

- **Run/job:** Packaging `41229383879`, Debian packages job.
- **Diagnostic:** `FAIL: remove deleted persistent state under /var/lib/i2pd`.
- **Cause:** the fixture created an unowned root-level file in the
  package-owned state directory; dpkg can prune an empty package-owned tree,
  so this did not represent router state.
- **Correction:** seed `netDb/.ci-remove-preservation` and assert it before
  and after `remove`; purge still requires the tree to disappear (CE-051).
- **State:** corrected by static analysis; **Pending CI package validation**.

## Historical and unverified

No current ASan/UBSan/TSan CTest result exists for this revision: the Linux
matrix stopped at the shared test compile error, and the Fuzzing workflow
passed its bounded smoke run. Windows TUI/worker tests are the only executed
runtime suite in the reviewed batch. Do not infer sanitizer or live-peer
cleanliness from this inventory.

## Batch `63c42ca4` (Windows 41232676139)

### RT-004 — Tunnel sends dereference a null io_context before `Start()`

- **Diagnostic:** `test-tunnel-build-records`, `test-tunnelpool-build-backoff`
  and `test-tunnel-endpoint-fragments` exit with `***Exception: SegFault`
  after 0.02-0.03 s with no printed output.
- **Cause:** `Transports::SendMessage` gated only on `m_IsOnline`, which
  defaults to `true`, while `m_Service` is a `std::unique_ptr` created in
  `Transports::Start()`. `Build()`/the endpoint path calls
  `transports.SendMessages()` and `boost::asio::post(*m_Service, ...)`
  dereferences null. This is reachable outside the tests (any component that
  sends before transports start), not a test-only artefact.
- **Correction:** `SendMessage`/`SendMessages` return an invalid future when
  `m_Service` is null, exactly like the offline path; no message is queued and
  no capability is bypassed. The affected tests also call
  `transports.SetOnline(false)` so their "no transport" premise is explicit.
- **Files:** `libi2pd/Transports.cpp`,
  `tests/test-tunnel-{build-records,endpoint-fragments}.cpp`,
  `tests/test-tunnelpool-build-backoff.cpp`.
- **State:** Implemented / static; Pending CI compilation and tests.

### RT-005 — Tunnel checksum IV could be written past the cell storage

- **Diagnostic:** no observed corruption in this batch (the crash came from
  RT-004), but the write at `payload + TUNNEL_DATA_MSG_SIZE` was unguarded and
  the header only documented a precondition.
- **Cause:** the checksum pass copies the 16-byte IV directly after the cell
  so one SHA256 range covers fragments plus IV; a buffer that ends at the
  payload turned that into a 16-byte overwrite.
- **Correction:** `TunnelEndpoint::HandleDecryptedTunnelDataMsg` now validates
  `offset`, the declared length and `maxLen - offset >= 16 + cell + IV` before
  the copy and drops the cell otherwise. A bounded regression feeds a buffer
  without the suffix and accepts the exact boundary.
- **Files:** `libi2pd/TunnelEndpoint.cpp`, `libi2pd/TunnelEndpoint.h`,
  `tests/test-tunnel-endpoint-fragments.cpp`.
- **State:** Implemented / static; Pending CI compilation and tests.

### RT-006 — MSVC abort while the server rejects a hello (test race)

- **Diagnostic:** `test-privsep-windows-ipc:182` asserts that sending
  `WorkerHello` succeeds, exit `0xc0000409`, on MSVC only.
- **Cause:** the wrong-PID/wrong-session cases send the hello while
  `AcceptBound` is rejecting the candidate and disconnecting the pipe; the
  writer can observe `ERROR_NO_DATA`. MSYS2 happened to win the race. The
  early rejection is the required security behaviour.
- **Correction:** a test helper documents and tolerates the racing write; the
  server-side rejection remains asserted. The channel also now refuses a
  second handshake after `SetRole` and answers a refused duplicate with a
  legal `Pong`.
- **Files:** `tests/test-privsep-windows-ipc.cpp`, `privsep/WindowsIpc.cpp`.
- **State:** Implemented / static; Pending Windows CI.

### Not observed

No ASan, UBSan or TSan report exists for this batch: every Linux sanitizer job
stopped at CE-055 before CTest, and Fuzzing passed its bounded smoke run.
Absence of a sanitizer report in these logs is not sanitizer evidence.

## Batch `dc5540ae` (Windows, 41248774956)

### RT-007 — Dangling `PROC_THREAD_ATTRIBUTE_*` values

- **Diagnostic:** MSVC `test-privsep-windows-supervisor` and the supervisor
  log: `CreateProcess(worker): The parameter is incorrect.` for every image,
  including the intentionally missing one.
- **Cause:** `UpdateProcThreadAttribute` stores the *pointer* and the kernel
  reads the value during `CreateProcessW`/`CreateProcessAsUserW`. The
  mitigation flags, `SECURITY_CAPABILITIES` and the low-privilege policy were
  block locals whose scope ended before the call, so the kernel validated
  overwritten stack bytes. A rejection before image lookup proves the fault
  was in the attributes, not the path.
- **Correction:** all attribute values live in the `AttributeList` owner that
  spans `CreateProcess` and is destroyed exactly once; the list is rebuilt
  for the single retry. No handle list is passed because
  `bInheritHandles=FALSE`.
- **State:** Implemented / static; Pending Windows CI.

### RT-008 — AppContainer profile SID freed with `LocalFree`

- **Diagnostic:** both MSVC isolation tests exit `0xc0000374`
  (`STATUS_HEAP_CORRUPTION`) during teardown.
- **Cause:** `CreateAppContainerProfile`/`DeriveAppContainerSidFromAppContainerName`
  document that their SID must be released with `FreeSid`; the code used
  `LocalFree` on the profile SID. Both allocate from the process heap with
  different block layouts, so the corruption surfaced later.
- **Correction:** `SidPtr`/`SidDeleter` owns every profile SID and calls
  `FreeSid`; capability SIDs remain `LocalAlloc`/`LocalFree`. A regression
  keeps the SID alive across reuse, heap churn, a move and a DACL grant.
- **State:** Implemented / static; Pending Windows CI.

### RT-009 — Nested Job Object rejected the UI restrictions

- **Diagnostic:** the `ERROR_INVALID_PARAMETER` spawn failures above also
  occur when the runner already owns the test in a Job Object.
- **Cause:** a process already in a job may only join another job when
  neither sets UI limits. `CreateWorkerJobObject` always set
  `JobObjectBasicUIRestrictions`, so `PROC_THREAD_ATTRIBUTE_JOB_LIST` (and the
  suspended fallback) could not nest.
- **Correction:** the UI restrictions are applied only when the process is not
  already in a job; `KILL_ON_JOB_CLOSE`, `DIE_ON_UNHANDLED_EXCEPTION`, memory
  and active-process limits remain unconditional and are never traded for
  nesting. The reduction is reported once. If the job attribute is rejected,
  one bounded retry creates the suspended child without it and assigns the
  job before resuming; a failed assignment terminates the child.
- **State:** Implemented / static; Pending Windows CI.

### RT-010 — Windows `--version` never terminated

- **Diagnostic:** `i2pd-version` fails with `did not terminate within 20 s`
  and empty captured output.
- **Cause:** `--version` was answered only after the GUI/daemon startup
  (`DaemonWin32::init` console code page, fatal-error reporter, CRT
  `__argc/__argv` in a `SUBSYSTEM:WINDOWS` image, router teardown). The child
  emitted no byte before blocking, so the failure was before the banner.
- **Correction (first attempt):** `ClassifyInformationalRequest`/
  `PublishInformationalResponse` answered `--version`/`--help` before
  `Daemon.init` from the `GetCommandLineW()` vector and terminated with
  `std::_Exit`. The `41320542948` run still timed out with empty output, so
  the block is before that path.
- **Correction (second attempt):** `WinMain` now answers from a raw wide
  command-line scan before any argv, `Init`, console or daemon state; the
  write path uses only inherited `STD_OUTPUT_HANDLE`/`STD_ERROR_HANDLE` (no
  `AttachConsole`/`CONOUT$`), and the CRT `__argc/__argv` fallback is gone.
  The Windows driver requires termination and exit status, and the banner
  only when output was captured. The Windows workflow prints the PE
  subsystem, imports, `ldd` and a bounded pre-CTest probe.
- **State:** Implemented / static; Pending Windows CI.

### Residual findings (not corrected in this batch)

- `Tunnels::NewI2NPTunnelMessage(true)` reserves
  `TUNNEL_GATEWAY_HEADER_SIZE` ahead of the I2NP header without extending
  `len`, so the endpoint's declared-length floor must tolerate that layout.
  Making the length honest would let the tolerance be removed, but it is a
  tunnel-pool change with no dynamic evidence yet.
- `ParseTrackerResponse` still commits the interval, minimum interval,
  tracker id, seeder and leecher display fields before the whole document
  validates. Only the peer table is transactional; the remaining fields are
  cosmetic and bounded.
- `ProxyDigestAuth::Verify` reports `Replayed` before authenticating the
  digest, and the nonce map lookup is not constant-time. Both match the local
  proxy threat model and are recorded rather than changed.
- The SAM version parser accepts a six-digit final component while a seventh
  digit is rejected; a bound of that size selects nothing, matching Java's
  "unbounded MAX" behaviour.
- A parent Job Object that itself sets UI limits still makes nesting
  impossible; the worker fails closed instead of running outside its job.
- LPAC live-worker cases may still report 77 where the runner cannot read the
  image from the test path; the skip is reported, not hidden.

### RT-011 — Windows live-worker test never proved its identity

- **Diagnostic:** MSVC `test-privsep-windows-supervisor` spends 10 s and then
  fails authentication (`worker pipe accept timeout`); the process later
  exits with an access violation.
- **Cause:** the test's `RunPingWorker` connected and answered Ping but never
  sent the `WorkerHello` spawn proof or bound a role, while `AcceptBound`
  requires both. The live-worker case was therefore unreachable on every host
  (the same 10 s in the clang lane ends in a skip), and the early `return 77`
  bypassed the handle-leak assertion. The `Admin` AppContainer role also
  cannot read the test image from the runner path. Related product holes were
  found in the spawn path: `CreateProcessW` may rewrite its mutable command
  line, so reusing one buffer for the job-less retry could start the wrong
  arguments; `useJobObject=false` or an invalid job handle silently skipped
  both `JOB_LIST` and `AssignProcessToJobObject`; and a released generation
  kept its pipe name and listener.
- **Correction:** the live worker now enters through the production
  `RunWindowsWorker` with the restricted-token supervisor role and a fresh
  command line per attempt; containment is unconditional (an uncontained
  policy or invalid job fails closed); `ReleaseWorker`/`Stop` drop the
  listener and pipe name; `WindowsChannel::Close` closes its handle exactly
  once and only the server side disconnects.
- **State:** Implemented / static; Pending Windows CI.

### RT-012 — MSVC privsep and TUI access violations after teardown

- **Diagnostic:** MSVC run `41320542948`: `test-privsep-windows-ipc` and
  `test-privsep-windows-supervisor` SEGFAULT, and `test-tui-protocol` exits
  `0xc0000374` (heap corruption). The isolation test now passes.
- **Evidence:** the TUI test sources and the TUI parser are byte-identical to
  the revision where the test passed; its executable links only `Log.o` from
  the product. The privsep ownership audit found no double-close or
  use-after-free in the spawn, server or supervisor chains. The previous
  `FreeSid`/`LocalFree` mismatch (RT-008) showed this lane converting a heap
  corruption into a crash in whichever allocation ran next, so a single
  process-wide fault across these binaries cannot be excluded from static
  evidence alone.
- **Correction:** containment/ownership defects above are fixed; the TUI
  test now guards its one unguarded `expected` dereference and pins the
  wire-integer-to-container and wire-limit-to-fixed-storage seams. The crash
  itself is **not root-caused**.
- **Rerun evidence:** the MSVC job of `41325980409` (same SHA `4e776a39`,
  same runner image) **passed all seven tests**, including the three that had
  crashed. The failure is intermittent/nondeterministic, consistent with a
  process-wide corruption whose detection depends on heap layout and runner
  timing rather than a deterministic TUI/privsep defect.
- **Next evidence:** if it recurs, enable PageHeap
  (`gflags /p /enable <test>.exe /full`) or collect a minidump from the
  runner and upload it; do not change product code from the exit code alone.
- **State:** Intermittent; `CI failure` on the first run, pass on the rerun.
  Correction pending a PageHeap/minidump reproduction.

## Preventive static audit (2026-11-01)

The entries below were **not observed in a CI run**. They were found by the
subsystem audits that follow every batch, are corrected in the worktree, and
are recorded here so a future failure can be matched to a known root cause.
Each remains Implemented / static, Pending CI compilation and Pending CI tests.

### RT-013 — privilege-separation channel and sink contract

- **Frame buffer shrink:** `Channel::ReceiveWithFd` resized its receive
  storage to the refused record's length and continued the loop after a
  tolerated violation. Every later record was truncated by the kernel
  (`MSG_TRUNC`) and reported as a fatal ancillary-data fault, so one bad frame
  made the endpoint deaf to all larger frames. The storage now keeps its
  fixed capacity and the record is decoded through a `string_view`.
- **Undecodable refusal:** an attributable frame whose type had no registry
  row was answered with a typed `Error` naming that unknown type. The peer's
  `DecodeError` rejects an unknown offending type, so the refusal could never
  be read; attributability now also requires `IsKnownMessageType`, otherwise
  the frame is fatal.
- **LogSink frame type:** `LogSink::Configure` always sent `ConfigureLogFd`,
  a `RequiredFile` type, even for a syslog or foreground sink. The worker's
  descriptor policy refused it before parsing, so the sink never configured.
  The frame type now follows the descriptor actually attached.
- **Stale-reply drain:** the broker reply drain reset a fresh 5 s deadline on
  every stale frame, so an endless run of stale ids blocked a torrent-tunnel
  start forever. It is bounded by `MAX_DRAINED_STALE_REPLIES` and classified
  as a run fault.
- **Removal frame:** `PayloadWriter::WriteBytes` passed a null pointer with a
  zero count to `std::string::append` for the empty removal payload; it now
  writes nothing.
- **Files:** `privsep/Channel.{h,cpp}`, `privsep/LogWorker.{h,cpp}`,
  `privsep/Protocol.h`, `privsep/Supervisor.{h,cpp}`,
  `tests/test-privsep-{protocol,log,worker-identity}.cpp`,
  `tests/test-datastorage-protocol.cpp`.

### RT-014 — daemon, control channel and admin queue lifecycle

- **`I2PControlService::Stop` detached its own thread** when called from a
  control handler, then dropped the thread object. The daemon destroyed the
  service while the detached thread still read `m_IsRunning` and executed
  raw-`this` handlers. A control-thread `Stop()` now keeps the thread object
  and the owner's `Stop()`/destructor performs the join; `Start()` reclaims a
  deferred thread and refuses a re-entrant restart.
- **Configured I2PControl failure was skipped** on POSIX: `ThrowFatal` has no
  throw function there, so a bind/address/certificate failure logged one line
  and the router started without the administration channel while reporting
  success. The daemon now tears the started services down and returns false.
  On Android the lifecycle helper owns the single rollback stop.
- **Unusable TLS certificate:** a control listener whose certificate/key could
  not be loaded still bound and accepted, so every handshake failed while the
  service looked started. Construction now fails.
- **SIGINT drain bypassed the shared bookkeeping:** a later
  `ADMIN SHUTDOWN CANCEL` restored the default transit policy instead of the
  policy in force. The signal now calls `BeginGracefulShutdown()`.
- **Admin queue stop did not wake a waiter:** a caller waited the full 2 s
  timeout after its queued command had already been retired. The wait now
  observes the retired slot and the stopping flag, returns `Unavailable`, and
  never reports a dropped queued command as accepted.
- **Optional TUI thread failure escaped:** a `std::jthread` construction error
  propagated out of the daemon start and aborted the router while leaving the
  listener bound. It now releases the endpoint and continues without TUI IPC.
- **Files:** `daemon/I2PControl.{h,cpp}`, `daemon/Daemon.cpp`,
  `daemon/UnixDaemon.cpp`, `daemon/TuiServer.cpp`,
  `daemon/TuiServerCommon.cpp`, `tests/test-tui-ipc-regression.cpp`.

### RT-015 — transports deferred timers, trusted set and SSU2 proxy

- **Peer-test timers survived restart:** `Transports::Stop()` stops the
  io_context and `Start()` restarts it, so queued stagger timers ran against
  the next instance and dereferenced a replaced or absent `m_SSU2Server`.
  Every handler now captures the run generation, a weak lifetime token and
  checks running/SSU2 state; `Start()` publishes all handler-visible members
  before creating the transports thread.
- **Unsynchronised trusted-router read:** `IsTrustedRouter` tested
  `m_TrustedRouters.empty()` before taking the mutex while the startup/admin
  thread replaced the set. It now locks first.
- **SSU2 SOCKS5 control exchange had no deadline:** a proxy that accepted the
  socket and stopped reading disabled the SSU2 UDP transport forever. A 30 s
  deadline now covers connect plus all four control reads/writes, cancelled
  on success and in `Stop()`, with reconnect on the dead-end branches.
- **`SetOnline` ran `PeerTest()` on a foreign thread:** the admin queue and
  the `SIGCONT` handler called it directly. It is now posted to the service
  with the generation/lifetime fence.
- **Files:** `libi2pd/Transports.{h,cpp}`, `libi2pd/SSU2.{h,cpp}`,
  `tests/test-transports-prestart-send.cpp`.

### RT-016 — streaming blocking read lifetime

- **`Stream::Receive` posted an async read into the caller's stack frame** and
  waited with a bounded sleep. If the service thread was busy or the context
  stopped, the handler later wrote into a destroyed mutex/condition variable
  and copied stream bytes into the returned buffer. Completion state is now
  heap-owned by the stream and the abandoned-read handler hands bytes back
  only while the stream is live.
- **Files:** `libi2pd/Streaming.{h,cpp}`, `tests/test-streaming-sendstate.cpp`.

### RT-017 — shared AEAD context and garlic tag file

- **One cipher context served every garlic session:** `GarlicDestination`
  keeps one encryptor and one decryptor for all of its ratchet sessions, and
  each wrapper owned a single stateful `EVP_CIPHER_CTX` with no lock. Two
  threads wrapping to different peers corrupted each other's key/nonce/tag.
  The wrappers are now stateless and use a thread-local context that every
  call re-initializes.
- **Garlic tag file was not atomically replaced:** the secret tag set was
  written through `CreatePrivateFile` with truncation, which chmods a
  pathname and reopens it. It is now built under the tag lock and replaced by
  one private atomic write.
- **Files:** `libi2pd/Crypto.{h,cpp}`, `libi2pd/Garlic.{h,cpp}`,
  `tests/test-crypto-x25519-noise-elligator-mlkem-audit.cpp`,
  `tests/test-eciesx25519-ratchet-session.cpp`.

### RT-018 — tracker transactionality and payload names

- **Tracker fields committed before validation:** interval, minimum interval,
  tracker id and swarm counts were written from the dictionary callback, so a
  truncated response kept a prefix of hostile values. Every field is staged
  and committed only after the whole document validates; a validated
  failure response still updates the fields it carried.
- **Win32 device-name spellings:** the reserved-device check missed the
  trailing-space/dot forms Win32 resolves (`CON .txt`, `AUX..txt`). One
  shared predicate now covers them and is pinned platform-independently.
- **Files:** `libi2pd_client/Torrents.{h,cpp}`,
  `tests/test-torrents-verify-parity.cpp`, `tests/test-torrents-security.cpp`.

### RT-019 — proxy target classification and client parsers

- **IPv4-embedded IPv6 bypass:** `::ffff:127.0.0.1` and `::ffff:10.0.0.5`
  passed both proxy checks because Asio classifies only the 16-byte form, so
  the outproxy could connect to its own loopback/RFC 1918 host. One shared
  predicate now decodes v4-mapped/v4-compatible spellings and adds CGNAT,
  `192.0.0/24`, benchmarking and `240/4`.
- **Address-resolver map race:** the constructor registers a datagram
  receiver on the destination thread while startup/admin adds local names to
  the same map. A leaf mutex now guards both and the answer is copied before
  send; the peer-supplied name is no longer logged.
- **Invalid jump link persisted:** a `.b32.i2p` jump link was stored without
  checking that it decoded to a destination. It must be valid before it
  replaces the entry or rewrites the index.
- **BOB buffer contract:** the identity copy was bounded by the stream buffer
  size instead of the destination buffer size and overflowed into a stale
  forward. The guard now matches the buffer that is filled and closes on
  overflow.
- **SAM reflected client input:** one `RESULT=I2P_ERROR MESSAGE="…"` quoted
  the client-supplied `SIGNATURE_TYPE`, breaking the reply grammar. The
  reason is now fixed text.
- **Files:** `libi2pd_client/{HTTPProxy,SOCKS,AddressBook,BOB,SAM}.*`,
  `tests/test-proxy-auth.cpp`, `tests/test-addressbook-merge.cpp`.

### RT-020 — Windows console text and pipe recovery

- **`Put` charged UTF-8 bytes as terminal columns:** with es/ru/zh-Hans
  catalogs, Cyrillic and CJK labels truncated and misaligned on Windows while
  ncurses rendered them fully. The documented column budget is now applied.
- **UTF-8 decoder accepted non-scalars:** overlong forms, encoded surrogates
  and values above U+10FFFF were forwarded to the console. A strict decoder
  yields one U+FFFD unit for each.
- **High surrogate survived an intervening key:** arrows/F-keys/Esc returned
  before the clear, so a later orphaned low surrogate could pair into a wrong
  code point. The clear is now a property of every event.
- **Unbounded pipe-recreation logging:** a squatted derived pipe name logged a
  warning every second forever. A 250 ms→5 s backoff logs one warning per
  distinct failure and one recovery line.
- **Files:** `tui/TerminalText.h` (new), `tui/TerminalWin32.cpp`,
  `tui/main.cpp`, `daemon/TuiServerWin32.cpp`,
  `tests/test-tui-terminal.cpp`, `tests/test-tui-win32-pipe.cpp`.

### RT-021 — Android boot intent and SAF threading

- **Boot broadcast resurrected a stopped router:** `BOOT_COMPLETED` persisted
  `Running` before reconciling, overwriting an operator stop and turning an
  expired graceful drain into a fresh run. A broadcast now only requests a
  restore when the persisted intent is non-stopped (and, for boot, the
  autostart marker is set).
- **SAF status read on the main thread:** the torrents screen read grants,
  persisted URI permissions and broker retry state on the main handler. A
  service-owned `safWorker` lane now delivers the status.
- **Unfenced picker results:** export/create results and the blocked-address
  dialog touched fragments without a generation or `isAdded()` check.
- **Null progress consumer:** a SAF creation with no progress callback failed
  a valid request by dereferencing it on the worker.
- **Files:** `android/app/src/main/java/**`,
  `android/app/src/test/java/**`, `android/app/src/androidTest/**`,
  `docs/android-modernization.md`, `.agents/skills/openi2pd-android/**`.

### RT-022 — RouterInfo/Identity signature length and artifact binding

- **Offline signer overflow:** `LocalRouterInfo::CreateBuffer` reserved the
  trailing signature with the identity's `GetSignatureLen()` but signed with
  `PrivateKeys::Sign()`, which uses the offline/transient signer when the keys
  file carries one. An Ed25519 `router.keys` without a transient fits; a
  `router.keys` with a longer transient (for example P-521) wrote the
  transient signature past the 4096-byte buffer. The buffer now refuses an
  offline or foreign signature length, signs into a scratch buffer and
  rejects an all-zero signature before installing the body.
  `ClientDestination::CreateNewLeaseSet` applies the same rule to LS1: the
  wire signature field is sized by the identity, so an offline signer or a
  length mismatch refuses the LS1 instead of publishing an unverifiable one.
- **Artifact name binding:** `RouterInfo::SaveToArtifact` now decodes the
  requested name and compares it with the parsed identity hash before writing,
  so a caller cannot store one router's descriptor under another router's key.
- **Identity invalidation:** `IdentityEx::FromBuffer` clears the previous
  standard-identity keys and hash on every parse, and `operator=` drops the
  extended length together with its storage, so an invalid object cannot
  expose old keys or a null extended buffer.
- **Files:** `libi2pd/RouterInfo.{h,cpp}`, `libi2pd/Identity.{h,cpp}`,
  `libi2pd/LeaseSet.cpp`, `libi2pd/Destination.cpp`,
  `tests/test-routerinfo-update-transaction.cpp`.

### RT-023 — LeaseSet2 encrypted outer layout

- **Wrong parser after decryption:** a successful decryption overwrites
  `m_StoreType` with the inner type, while `Update()` and
  `ExtractPublishedTimestamp()` keyed the outer layout on that value. A
  decrypted type-5 record could therefore be re-parsed with the standard
  layout (invalid) and its published time read from ciphertext (arbitrary
  replay decision). An explicit immutable outer-layer marker now answers both
  questions; readers must still check `IsValid()` before touching
  `GetBuffer()`/`GetStoreType()`.
- **Files:** `libi2pd/LeaseSet.{h,cpp}`,
  `tests/test-leaseset-encrypted-freshness.cpp`.

### RT-024 — SAM/I2CP/tunnel session lifecycle

- **SAM leaked secrets:** an unrecognised control line was logged verbatim.
  A tab-separated or mistyped `SESSION CREATE ... DESTINATION=<private keys>`
  therefore wrote the private keys to the error log; the diagnostic now
  reports only the line length and field count.
- **SAM subsession identifiers:** `SESSION ADD`/`REMOVE` did not apply the
  `SESSION CREATE` name rule and `SESSION STATUS ... ID="%s"` reflected the
  raw value. The name rule is now shared, and both reply helpers sanitize
  defensively; error text no longer repeats the identifier.
- **Legacy `accesslist` opened the tunnel:** a value whose every entry failed
  to parse installed no filter, turning an operator typo into an open server
  tunnel. `ParseLegacyAccessList` now always returns the deny-by-default
  definition; only an absent value means "no whitelist".
- **I2CP owner race:** `I2CPDestination::m_Owner` was a `shared_ptr` reset by
  `Stop()` while garlic/destination/posted handlers read it, and one
  `AddRoutingSession` call was unguarded. `GetOwner()`/`ResetOwner()` use a
  leaf mutex and every caller takes a shared copy; a missing owner skips the
  routing-session cache but still sends.
- **Terminal I2CP status:** `DESTROY SESSION` closed the socket before the
  terminal `SESSION STATUS` write completed. `closeAfterFlush` drains the
  queue and closes once, bounded by the existing reply deadline.
- **Tunnel resolver race:** `I2PServerTunnel::m_Resolver` was assigned by
  `Stop()` and the destination thread. It is now mutex-guarded with
  `CancelResolver()`, and a stale completion cannot clear a newer resolver.
- **Files:** `libi2pd_client/{SAM.*,I2CP.*,I2PTunnel.*,I2PTunnelAccessFilter.*,ClientContext.cpp}`,
  `tests/{test-sam-control-framing,test-i2ptunnel-filter,test-i2ptunnel-stop}.cpp`.

### RT-025 — recognized SAM commands, partial base32 and NAT64

These were found by the static audit that followed the compile correction;
none was observed in CI.

- **SAM parameter logging:** `SESSION ADD`/`SESSION REMOVE` logged the whole
  parameter list before the `DESTINATION` refusal, so a request carrying
  `DESTINATION=<private keys>` reached the debug log; `PING` echoed arbitrary
  client text. Both now log only the request length. This extends the
  unknown-command rule to recognized commands.
- **Partial base32 jump link:** `Address` accepted any non-zero decode, so a
  short `.b32.i2p` label published an index row whose remaining hash bytes
  were indeterminate. The constructor now requires exactly 32 decoded bytes;
  the merge regression covers 4/8/16-character labels.
- **NAT64:** the proxy contract promised to refuse NAT64 spellings of private
  hosts but the classifier only decoded `::/96` and `::ffff:0:0/96`.
  `64:ff9b::/96` now applies the IPv4 table to its embedded address and the
  local-use `64:ff9b:1::/48` is refused entirely; public NAT64 literals stay
  allowed. The first placement was below the `zeroPrefix` early return, so it
  was unreachable and the new test would have failed; the guard now runs
  before that return. `test-proxy-auth` pins both directions.
- **Accessor hiding:** `I2CPDestination::GetOwner()` hid
  `GarlicDestination::GetOwner()` with a different return type. Renamed to
  `GetSessionOwner()` so a future `destination->GetOwner()` cannot silently
  select the session.
- **Comment hygiene:** the ECIES optimal-payload comment used `/*` inside
  `//` lines; replaced with plain parentheses.
- **Files:** `libi2pd_client/{SAM.cpp,AddressBook.cpp,HTTPProxy.cpp,I2CP.*}`,
  `libi2pd/ECIESX25519AEADRatchetSession.h`,
  `tests/{test-addressbook-merge,test-proxy-auth}.cpp`.
- **State:** Implemented / static; **Pending CI compilation and tests**.

### RT-026 — platform-audit corrections for Windows and OpenBSD

Found by the static audit that followed CE-070; none was observed in CI.

- **OpenBSD unveil root:** `BuildOpenBSDPolicy` accepted an all-slash root
  (`/`, `//`, `///`) and emitted `unveil("/", "rwc")`, granting a storage
  worker the whole filesystem. It now refuses any all-slash path, and
  `ApplyOpenBSDPolicy` re-checks a hand-assembled policy before `unveil(2)`.
- **Pledge promise authority:** `IsKnownPledgePromiseName()` is exported and
  used by `BuildOpenBSDPolicy`, `ParseOpenBSDPledgeFile` and the regression,
  so an unknown promise fails before `pledge(2)` aborts a worker without a
  diagnostic. `test-privsep-openbsd-policy` walks the exhaustive
  role × unveil matrix and rejects a root/unbounded unveil rule.
- **Supervisor snapshot reads:** the Windows supervisor test dereferenced
  `SupervisorStatus::Get()` without checking the optional in seven places;
  a checked accessor makes a missing publication a diagnostic instead of
  undefined behaviour. The generic supervisor test received the same guard.
- **Win32 tray flags:** `AddTrayIcon` assigned `nid.uFlags` twice, so the
  `notify` branch was dead. The unconditional assignment is removed.
- **Missing direct includes:** `Win32App.cpp` (`<sstream>`, `<string>`),
  `DaemonWin32.cpp` (`<chrono>`), `Win32NetState.cpp` (`<exception>`),
  `daemon/UPnP.cpp` (`<chrono>`, `<exception>`, `<functional>`) and
  `tui/TuiClient.cpp` (`<sys/time.h>` for `struct timeval`) now name what they
  use. `daemon/TuiServer.cpp` also spells only `EAGAIN`, removing the
  duplicate `EWOULDBLOCK` comparison that a `-Wlogical-op` lane reports.
- **MSVC lane coverage:** `test-win32-shutdown-deadline` is now built and
  selected by the MSVC job.
- **Files:** `privsep/OpenBSDSandbox.{h,cpp}`, `contrib/openbsd/i2pd.rc`,
  `tests/test-privsep-openbsd-policy.cpp`, `tests/test-privsep-*-supervisor.cpp`,
  `Win32/{Win32App,DaemonWin32,Win32NetState}.cpp`, `daemon/UPnP.cpp`,
  `.github/workflows/build-windows.yml`.
- **State:** Implemented / static; **Pending CI compilation and tests**. The
  `imsg_free` descriptor ownership and the `recvfd` control-buffer question
  remain open until a live OpenBSD run.

## Current exact-SHA observations (`56f17c0f`, 2026-11-02)

### RT-027 — ASan EdDSA stack-buffer underflow; undersized fixture found

- **Run/job:** Validation [41364867079](https://github.com/daviduhden/openi2pd/actions/runs/41364867079), ASan+UBSan job `125022771190`.
- **Test/platform:** `test-eddsa`, Ubuntu Linux, Clang 23.1.4 with ASan/UBSan.
- **Diagnostic:** one-byte read at stack offset 2175, immediately before stack object `point` at offsets 2176..2208 in `tests/test-eddsa.cpp:166`. Raw frames are `test-eddsa+0xbe98e7`, `+0xbe41bd`, `+0xbd56da`, and `+0xae25b4`; no symbolizer path was configured. Three expected invalid-point logs precede it; the valid canonical-signature assertion also fails on Linux/OpenSSL 3/4 and OpenBSD.
- **Artifacts:** the exact run uploaded CTest XML and `Testing` logs, but not the executable, DWARF or core. Sibling UBSan job `125022771171` and TSan job `125022771257` passed this test on the same SHA; they do not clear the ASan finding.
- **Static follow-up:** the exact-SHA fixture declares `digest` as a
  32-byte array, passes it to SHA-512 (64-byte output), then passes it to
  `Ed25519::Verify`, whose `DecodeBN<64>` reads 64 bytes. This is a
  confirmed fixture write/read bounds defect. The helper now returns an
  owning 64-byte array. Global SHA-512 searches reviewed the other
  Ed25519 digest buffers and generic signature hash sizing; no equivalent
  undersized output was demonstrated there. This defect may explain both
  reports, but the unsymbolized PC cannot establish that association.
  **Implemented / static; Pending CI compilation; Pending CI tests.**
- **Cause/state:** not established. The stack frame identifies the test frame, not the function/expression that read before `point`; do not infer `DecodePoint` as the cause. The manual sanitizer workflow now locates `llvm-symbolizer` and retains the `test-eddsa` executable with its debug information. Re-dispatch a published revision and inspect the symbolized run before changing crypto code. **Correction pending; Pending CI tests.**
- **Related functional failure:** the same SHA fails the canonical positive-vector assertion at line 170 on GCC/OpenSSL 3 and GCC/OpenSSL 4 and on OpenBSD Clang/LibreSSL 4.4.0; Linux Clang/OpenSSL 3 and 4 pass that assertion. This cross-compiler behavior is a distinct observation from the ASan underflow and is recorded as TF-069. It does not identify the underflowing call or establish a cryptographic root cause.

### RT-028 — invalid DatagramVersion created from configuration

- **Run/job:** Validation `41364867079`, UBSan job `125022771171`.
- **Diagnostic:** `Datagram.cpp:126` loads enum value 9 in `test-datagram-version-dispatch`.
- **Cause:** the regression directly cast 9 to `DatagramVersion`; a repository-wide search also found `ClientContext.cpp` casting an arbitrary configured integer before validation.
- **Correction in current worktree:** add `ParseDatagramVersion(int)` and reject invalid tunnel configuration before enum construction; remove the invalid-enum test and pin invalid raw integers through the parser. **Pending CI compilation/tests.**

### RT-029 — invalid transport enum used by rejection test

- **Run/job:** Validation `41364867079`, UBSan job `125022771171`.
- **Diagnostic:** `TransportConnector.h:136` reads invalid `SupportedTransports` value 32767 in `test-transport-connector`.
- **Cause/correction:** the test created an invalid enum merely to exercise the default branch. Current worktree changes lookup to accept the raw integer value and tests `0x7fffU` directly; valid transport values still select the same connector. **Pending CI compilation/tests.**

### RT-030 — test parsers returned views into temporary strings

- **Run/job:** Validation `41364867079`, ASan+UBSan `125022771190` and TSan `125022771257`.
- **Tests/diagnostics:** `test-blocklist-parse` read `OperatorBlocklistEntry::reason` after passing a temporary concatenated line; ASan and TSan report use-after-free in `memcmp`/`string_view::compare` at test line 150. `test-torrents-dht-protocol` read `DHTFrame::transaction` after decoding a temporary encoded frame at test line 299; ASan reports use-after-free.
- **Cause:** both APIs intentionally return views into caller-owned input. Their production consumers keep the input packet/string alive for immediate use; the fixtures did not.
- **Correction in current worktree:** store each encoded input in a named `std::string` for the full view lifetime. Search all parser fixtures that retain a result built from a temporary. **Pending CI tests.**

### RT-031 — sanitizer suite also has leaks and lifecycle failures

- **Run/job:** Validation `41364867079`, ASan+UBSan job `125022771190` and TSan job `125022771257`.
- **Diagnostics:** LeakSanitizer reports 842,968 bytes/79 allocations in `test-i2ptunnel-stop`, 290,072/63 in `test-i2ptunnel-targets`, and 6,880/24 in `test-eciesx25519-ratchet-session`. TSan separately reports failed completion/timeouts in `test-runnableservice-lifecycle` and `test-i2cp-session-lifecycle`, plus test assertions in logging/streaming.
- **Cause/state:** allocations and ownership stacks must be mapped from the adjacent complete sanitizer output before assigning a product root. The test failures do not by themselves prove races, but TSan reports are actionable findings. **Correction pending; Pending CI tests.**

### RT-032 — intermittent MSVC heap corruption recurred

- **2026-11-05 recurrence:** [42056732400](https://github.com/daviduhden/openi2pd/actions/runs/42056732400),
  MSVC `127347034968`, SHA `01960093aeff480d24ed8507878355a643da168f`:
  `test-tui-protocol` exits `0xc0000374` after 0.28 seconds. Exact uploaded
  LastTest.log has empty test output. Six selected tests pass; worker
  supervisor skips at startup (`worker pipe accept timeout`). These results
  do not clear the corruption or validate live-worker supervision.
- **Current investigation:** reviewed parser ownership, expected values,
  tracker indices, framing and comment slicing without a demonstrated root.
  Added constant flushed phase markers, preserving assertions and avoiding
  inputs/credentials. Dispatched published Windows run
  [42061501515](https://github.com/daviduhden/openi2pd/actions/runs/42061501515)
  with `heap_diagnostics=true`; it runs the published SHA, not local markers.
  MSVC job `127364220253` was cancelled during compilation; the always-run
  diagnostic then reports missing test-tui-protocol.exe. The exact artifact
  contains only debugger commands, no PageHeap stack or produced test image.
  MinGW `127364143189` still fails CE-076 on that unchanged published SHA.
  Cancellation's external cause is not established. No automatic retry was
  used. Symbol-only PageHeap/CDB evidence is still required. No speculative product
  rewrite or timeout inflation is used to hide this finding.

- **Run/job:** Windows [41364870388](https://github.com/daviduhden/openi2pd/actions/runs/41364870388), MSVC job `125022780835`, SHA `56f17c0f47a3d1a328be6c54df7b661c480924f8`.
- **Tests:** `test-tui-protocol`, `test-tui-pipe-name` and `test-privsep-windows-isolation` exit `0xc0000374`; `test-tui-win32-pipe` asserts `GetLastError() == ERROR_ACCESS_DENIED` and exits `0xc0000409`.
- **Cause/state:** unresolved and intermittent; earlier same-SHA passes do not erase the failures. Obtain exact CTest/minidump/PageHeap evidence before attributing to production code or fixture. **Correction pending; Pending Windows CI tests.**

### RT-033 — I2PControl socket replacement failure came from inode reuse in the fixture

- **Runs/SHA:** Linux/OpenSSL 3 `41364855256`, Linux/OpenSSL 4 `41364858202`, Validation/sanitizers `41364867079`, OpenBSD `41364873438`; all used `56f17c0f47a3d1a328be6c54df7b661c480924f8`.
- **Test/diagnostic:** `test-i2pcontrol-socket` failed the stale-identity, replacement-survives and own-identity-removes assertions.
- **Cause:** the fixture closed the original bound listener before unlinking and rebinding its pathname. The filesystem could reuse the device/inode, so the replacement falsely matched the recorded identity. The service keeps its original listener descriptor open during path cleanup.
- **Correction:** keep the old listener open until replacement cleanup completes, matching production lifetime. Product code was unchanged. **Implemented / static; Pending CI tests.**

## Artifact availability for the 2026-11-02 batch

The OpenBSD run `41364873438` and failed historical Fuzzing run `41359802036`
have no artifacts in GitHub's artifact API. Validation `41364867079` retains
ASan/UBSan artifact `13107582638`, TSan `13107507801`, UBSan `13106474670`,
fuzz corpus/smoke `13106877727` and source docs `13106732442`. The current
Windows run `41364870388` retains MSVC CTest `13107223402` and MSYS2 CTest
`13107091983`; Android `41364864089` has none. Linux/packaging artifact IDs
are recorded in the CI investigation notes. `gh run download` could not
extract the retained archives because the local temporary filesystem quota
was exhausted; this is a retrieval limitation, not evidence that GitHub
expired those artifacts. Exact run logs remain available through `gh run
view --job JOB_ID --log`.

### Windows exact-artifact continuation (TF-061/TF-062/TF-063)

Run 41364870388, SHA 56f17c0f47a3d1a328be6c54df7b661c480924f8:
MSVC job 125022780835 artifact 13107223402 has heap-corruption exits for
TUI protocol, pipe-name and Windows isolation with no stack. MSYS2 job
125022781025 artifact 13107091983 passes protocol and pipe-name. Do not
classify these MSVC failures as parser assertions without evidence.
Both artifact logs retain the pipe first-instance error assertion, but not
the original numeric GetLastError. The prepared fixture separates capacity
from exclusivity and prints that error; ERROR_ACCESS_DENIED is still
required. This is not a proved runtime cause. Heap reports remain open for
PageHeap/minidump evidence. No local runtime validation was performed.

### Prepared Windows heap reproduction evidence (CONT-012)

TF-062 remains unresolved. The manual Windows workflow now retains optimized
Release EXE/PDB artifacts and offers heap_diagnostics for code-only CDB/PageHeap
reproduction of the three affected MSVC images. It does not dump memory or
request arguments/locals. Missing installed tools fail that optional step;
ordinary CTest remains authoritative. No new run has executed this revision,
and no historical status is reclassified as resolved by this preparation.

### 2026-11-04 static leak attribution

The ASan `test-i2ptunnel-stop`/`test-i2ptunnel-targets` reports retain
65,536-byte blocks allocated by `deflateInit2`; those blocks end only in
`GzipInflator`/`GzipDeflator` destructors. The confirmed structural cycle is
`StreamingDestination::m_Streams` holding strong `Stream` while
`Stream::m_LocalDestinationOwner` holds the destination, so an unregistered
stream keeps both alive to `exit()`. This is a product lifetime defect, not
only a fixture issue; breaking the reverse edge or unregistering in
`Stream::CleanUp()` is the durable fix. The three fixture targets must also
close every connection they open. `test-eciesx25519-ratchet-session` retains
a separate 6,880-byte/24-allocation owner and is not explained by this cycle.
No sanitizer run has executed the proposed correction; status remains open.

### RT-031 static executor follow-up (2026-11-04)

Validation run 41364867079, TSan job 125022771257, SHA
56f17c0f47a3d1a328be6c54df7b661c480924f8 failed
`test-runnableservice-lifecycle.cpp:50` after five seconds waiting for its
posted completion. Comparing that exact source with the current tree found
an unguarded empty-run race: run returns with the context stopped, then the
outer running loop repeatedly calls run without restart; later posts cannot
execute. The base now owns a per-run optional work guard and exits on normal
run completion. Stop releases the guard for cancellation or drain; Run enters
once even if a draining Stop precedes thread entry. Focused restart, idle-gap,
exception and drain regressions are prepared. This is a demonstrated source
cause consistent with the timeout, not new runtime evidence.

Equivalent-wrapper review found NTP launching before posting Sync; that order
is reversed, normal exhaustion exits, and shutdown drains queued callbacks
after joining under revoked authority. UPnP already posts before launch and
breaks on normal return; I2PControl registers Accept before launch; Transports
owns a work guard. Their broader lifetime and stopped-loop risks remain open.
Self-destruction/detach and overlapping control-thread lifecycle calls are not
resolved by the base guard. RT-031's three historical LeakSanitizer reports
remain unresolved; future manual artifacts retain their exact executables.

The earlier artifact-extraction limitation is historical: Validation
41364867079 logs and retained artifacts were recovered under
`/tmp/openi2pd-ci/41364867079` during the continuation. Raw PCs without the
corresponding historical images still cannot be assigned source frames.

### RT-031 / TF-070 logger publication follow-up

Validation 41364867079, TSan job 125022771257, SHA
56f17c0f47a3d1a328be6c54df7b661c480924f8 reported the missing
local-after-sink record at test-log-lifecycle.cpp:144. The unchanged startup
predicate was reused across generations: Run could pass its wait before the
new Start published m_IsRunning, drain nothing and terminate before enqueue.
The local correction resets the predicate before thread creation under its
startup mutex and preserves the join barrier between generations. Prepared
restart/destination and negative source regressions remain Pending CI. This
classification does not attribute or close the historical ratchet/tunnel leaks
or the separate MSVC heap corruption.

### RT-031 ratchet correlation cycle follow-up

Static review found a concrete ownership cycle: a session's pending reply
entry strongly retained a receive tagset whose session edge was also strong.
The pending edge is now weak; the live inbound tag map still retains session
authority. Successful weak locks outlive the pending mutex so their final
destructor cannot reenter that lock. A regression prepares destination teardown
with unanswered replies while retaining lost-first/lost-last reply controls.

This is Implemented / static, Pending CI compilation and Pending CI tests.
It does not attribute or close the historical 6880-byte/24-allocation report:
the exact sanitizer image/stack and a fresh manual run are still needed.

## 2026-11-07 static corrections and observed recurrences

Observed run evidence uses SHA `14f224e6c7776619c13449759523a87298d4f63a`;
links/jobs are in the compilation inventory. Every correction below is
Implemented / static, Pending CI compilation and Pending CI tests.

### RT-038 — Compatibility Ed25519 nonce used half the digest

OpenBSD 42402987408 and MinGW 42402984632 fail the strict RFC signature
comparison at test-eddsa.cpp:145; provider signing passes. The compatibility
signer decoded only 32 of the 64 SHA-512 nonce bytes. Decode the whole digest,
reduce modulo L and encode the scalar before multiplication, as required by
[RFC 8032 section 5.1.6](https://www.rfc-editor.org/rfc/rfc8032.html#section-5.1.6).
Existing signatures remain valid: this corrects deterministic output, not the
wire encoding or verification rules. Retained strict known-answer checks and
added direct compatibility/overlapping-buffer cases. Secure BIGNUM ownership
and constant-time division flags cover the new reduction and analogous
RedDSA path; this is not a claim of end-to-end constant-time fallback code.

### RT-039 — Failed Windows wait could release pending OVERLAPPED storage

Static review of `WindowsIpc.cpp` found WAIT_FAILED returned without cancelling
and reaping I/O. Preserve the original wait error, request cancellation and
wait for completion before releasing stack storage. Reviewed sibling TUI
cancellation paths and prepared a source contract. No exact CI stack ties
this defect to RT-032; that heap-corruption diagnosis remains unresolved.

### RT-040 — Retired DHT work could overwrite the final persistence snapshot

Static review found reused generation authority and periodic writes admitted
before shutdown. Start now allocates fresh authority; timers reject retired
generations. A persistence barrier orders admitted writes before final Save,
and queued writes recheck revocation under that barrier. RAII releases flight
claims on submission/execution failures. An in-memory regression exercises
old/new queued saves, final snapshots and claim isolation. Lifecycle mutation
still requires the owner executor or a quiescent service; this does not claim
safe arbitrary concurrent destruction.

### RT-041 — Bencode dangling keys could be accepted as dictionary endings

Static parser review found failed value parsing followed by `break`, allowing
an `e` byte to close a dictionary with a missing value. Reject immediately in
ParseDictionary and ExtractDictionary, including malformed nested/list values.
Prepared direct, nested and metainfo regressions with valid controls in the
existing bencode suite. No production crash was observed for this finding.

### RT-032 recurrence and sanitizer evidence gaps

Windows 42402984632 reports heap status `0xc0000374` in tui-protocol,
pipe-name and supervisor tests. TUI phase markers reach completion: this
localizes detection, not necessarily the original invalid write. The worker
exits `3221225794` (`0xC0000142`, DLL initialization failure). Neither fact
proves a CRT ABI mismatch or a logger-destructor cause. Diagnostic manual run
[42407544860](https://github.com/daviduhden/openi2pd/actions/runs/42407544860)
uses the published SHA, not these local fixes; its PageHeap evidence remains
pending. MinGW daemon `--version` still times out with empty output while
TUI version succeeds; no timeout increase masks the unresolved startup issue.
RT-027/031 sanitizer families remain open: current sanitizer builds stop at
CE-078, and unsanitized sibling successes cannot close those reports.

## Exact sanitizer and PageHeap follow-up

### RT-032 — PageHeap identifies profile static-destruction order

[42407544860](https://github.com/daviduhden/openi2pd/actions/runs/42407544860),
MSVC job `128508397079`, SHA `14f224e6c7776619c13449759523a87298d4f63a`,
provides the missing code-only stack in the exact uploaded diagnostic images:
`std::list::_Free_non_head -> DiscardPostponedRouterProfileUpdates ->
NetDb::Stop -> NetDb::~NetDb -> execute_onexit_table`, Profiling.cpp:851.
All three targeted images finish their checks before this access violation.
The namespace-static profile queue had already been destroyed when the global
NetDb destructor called Stop. This establishes a concrete destruction-order
cause, unlike the earlier detection-only phase markers.

The local correction groups profile state under one function-local runtime,
constructed before destructor-capable producers complete construction. It
preserves ordered shutdown rather than leaking process state or bypassing
cleanup. Cross-review also found that cached/external RouterProfiles could
outlive their NetDb-owned recycling pool: profile deleters now retain pool
authority. Regressions cover late destruction and external profile release.
Implemented / static; Pending CI compilation and Pending CI tests. The
separate worker DLL startup failure and MinGW informational timeout remain
unresolved: informational handling uses `_Exit`, so this destructor correction
cannot be claimed to fix the empty-output timeout.

### RT-042 — Streaming fixture completion borrowed an expired stack result

Validation [42414235478](https://github.com/daviduhden/openi2pd/actions/runs/42414235478),
ASan/UBSan job `128528743083`, SHA
`68c16e10a72c70da948a3f230acc07b2800d44ac`: test-streaming-sendstate reports
stack-use-after-scope through SendBuffer::Cancel / Stream destruction into
Write's callback and the former SendResult at line 139. Use shared completion
state, retain all prior state/error checks, drop the lexical owner before the
stream and assert weak expiry after cancellation. This fixes fixture lifetime;
it does not remove the half-close path. Implemented / static; Pending CI tests.

### RT-043 — Fixture-owned destination cycles and unserviced disk work

Same ASan job: test-i2ptunnel-stop leaks 843016 bytes / 79 allocations;
test-i2ptunnel-targets leaks 290088 / 63. Those fixtures create registered
streaming destinations without performing ClientDestination::Stop. Teardown
now stops the destination on the quiescent executor, drains cancellations and
asserts weak-owner expiry. Real streaming ownership remains intact.

The distinct 25913-byte / 139-allocation leak belongs to
**test-torrents-security**, TestRPCMetadataFields, not ratchet-session. Its
unstarted tunnel disk queue retained its owner through queued admin-state
loads. The fixture now runs both executor domains through final Stop, drains
disk tasks and asserts tunnel release. Implemented / static; Pending CI tests.
The exact ratchet test passes all three sanitizer lanes on this SHA; that
narrow result revisits historical RT-031 without attributing unrelated leaks.

### RT-044 — OpenSSL property-cache TSan report needs instrumented dependency

Same run, TSan job `128528743055`: test-clientcontext-registry reports strcmp
in `ossl_method_store_atomic_find_in_list`, reading a block allocated through
CRYPTO_malloc by a sibling concurrent PeerOrdering constructor. The dependency
is the Homebrew OpenSSL 4.1.0 bottle; its atomics are not instrumented by the
project's compiler flags. Official source initializes `prop_query` before
atomic publication, and the pointer-load ordering changes under TSan builds.
This supports an instrumentation hypothesis, not proof of a harmless report
or an actual upstream race. Do not serialize the eight creators or suppress it.

Primary references: [OpenSSL property cache](https://github.com/openssl/openssl/blob/openssl-4.1.0/crypto/property/property.c),
[OpenSSL pthread atomics](https://github.com/openssl/openssl/blob/openssl-4.1.0/crypto/threads_pthread.c),
[TSan non-instrumented-code requirements](https://github.com/google/sanitizers/wiki/ThreadSanitizerCppManual#non-instrumented-code).
Required evidence is a same-Clang TSan build of the exact library/providers,
with configure/build logs and image identity, followed by the unchanged
registry regression. State: In progress; dynamic attribution remains pending.

### RT-045 — Static NTCP2 proxy-error logs exposed configured credentials

Preventive review found unsupported/invalid proxy URL diagnostics formatted
the entire `ntcp2proxy` operand, which can contain credentials. Remove the
operand and retain operation/outcome only; search sibling transport URL logs
and prepare a negative source contract. No CI log is asserted to contain a
secret. Implemented / static; Pending CI tests for redaction behavior.

RT-044 validation preparation: `crypto-tsan.yml` adds a separate manual
instrumented-library diagnostic, preserving the existing matrix. It pins
OpenSSL 4.1.0 commit `2f4e3a5fec05394068cfa7379890ab552483a8a6`, builds
library/providers and the project with the same Clang TSan instrumentation,
checks the linked library path, and runs the unchanged registry regression.
Source/build/install stay under RUNNER_TEMP; always-run collection stages
exact images, hashes, build IDs and logs into the workspace for upload.
This workflow exists only locally and has not been executed. The upstream
library's native Make is isolated CI dependency tooling, not a new project
build requirement or local validation route.

Exact latest Windows distinction: run 42414215883 has a single MSVC isolation
segfault; six other selected tests pass and supervisor is skipped after worker
`0xC0000142` before authentication. MinGW's four failures are Perl portability,
maintenance fixtures, knowledge validator and informational version timeout.
Do not equate the new isolation crash with the older PageHeap stack without
new image evidence. Manual diagnostic
[42417372545](https://github.com/daviduhden/openi2pd/actions/runs/42417372545)
was dispatched once on published `68c16e10` for that attribution; its results
remain pending and cannot validate the local corrections.

RT-045 equivalent-pattern search also found raw URL operands in address-book
subscription start/parse errors and reseed download-start/parse errors.
Those operands can carry userinfo or query credentials even when parsing
fails. Removed the operands, including the outproxy debug URL (userinfo-only
redaction cannot establish that query data is non-secret). Retained operation
and outcome diagnostics, existing redaction APIs/tests, and added multiline
source guards across all affected components. No protocol behavior changed.

## 2026-11-08 — Runtime evidence and preventive provider review

### RT-032 / RT-042 / RT-043 — Exact corrected-image results

Historical PageHeap 42417372545, job 128538275404, SHA `68c16e10`, confirms
that Windows isolation had the same Profiling.cpp:851 list-destruction stack
through NetDb::Stop as the other targeted images. Latest MSVC 42419357644,
job 128544374044, passes isolation and test-profile-static-lifetime on
`8ffd1ad8`. This validates those executed exit paths, not the skipped worker
handshake. Supervisor still skips after pre-authentication `0xC0000142`.
MinGW GUI informational output still times out while the console counterpart
passes; that startup issue remains separate from global destruction.

Validation 42419375465 on `8ffd1ad8` passes i2ptunnel-stop,
i2ptunnel-targets, streaming-sendstate and torrents-security in all three
sanitizer lanes. No ASan/LeakSanitizer/UB runtime report appears in the complete
new logs. EdDSA, ratchet, bencode and DHT protocol tests also pass in those
executed lanes. These narrow results revisit the earlier families without
asserting that every sanitizer configuration is clean.

### RT-044 — Focused instrumented-provider evidence

The ordinary TSan job 128544291559 still reports strcmp versus CRYPTO_malloc
inside the prebuilt OpenSSL property cache. Focused diagnostic 42422405960,
job 128553458708, SHA `9f5320b2409003cf05df3b1d8968a7a3ebe6949e`, passes
1/1 registry concurrency tests in 0.08 seconds. Its linked libcrypto.so.4
resolves to the pinned, same-Clang, TSan-instrumented temporary installation.
The total job lasts 7m14s. This supports the dependency-instrumentation
classification; one passing run is not a proof that arbitrary races are absent.

A reusable local composite now prepares that same dependency for the normal
TSan row and focused diagnostic. Full sanitizer CTests remain enabled; only
TSan uses the instrumented prefix, while ASan/UBSan and the GCC/Clang bottle
branch lanes remain intact. Configure probes receive the TSan runtime too.
Logs, source/patch identities and matching libraries/providers are staged for
failure-time collection. The shared full-matrix implementation and upstream
macro guard are local and Pending CI compilation / Pending CI tests.

### RT-046 — Recognized metainfo types could fall through generic skipping

Static review found ParseDictionary's callback-zero convention reused as a
typed-field failure: a malformed known value was generically skipped and
some fields retained default values. A wrong-type file length could become
zero beside a valid sibling, and wrong-type top-level length could bypass the
length/files exclusion. Recognized typed extractors now record malformed
state before returning zero. Preserve valid integer-zero files and unknown
extension keys. Primary announce also uses the existing 2048-byte engine cap,
a local resource policy rather than a new wire limit. Existing bencode tests
cover wrong types, mixed siblings, valid zero and announce boundaries.
Reference: [BEP 3](https://www.bittorrent.org/beps/bep_0003.html).
Implemented / static; Pending CI tests.

### RT-047 — Peer identity and seed fallback weakened provider failure

Static review found torrent peer-ID generation falling back to GetRngSeed,
which itself fell back to a monotonic clock if the provider failed. That
output was not unpredictable. Peer-ID failure now clears all bytes, keeps
readiness false and stops the torrent; later resume/error reset cannot make
it publishable. HTTP/UDP tracker, DHT, magnet insertion and peer handshake
paths require readiness. Internal error 4 is appended; existing values 0–3
and RPC's generic local-error value 3 remain unchanged. Private per-object
failure tests avoid altering the process-global provider. RPC/control tests
preserve the existing string field and numeric mapping.

GetRngSeed now aborts initialization/operation with an exception instead of
using clocks. Zero remains valid. Global-constructor provider failure can
terminate before main; graceful recovery is not claimed for every caller.
Deterministic source tests pin partial/empty failures and valid boundaries.
Implemented / static; Pending CI compilation and Pending CI tests.

### RT-048 — Unchecked provider output across protocol constructors

Global equivalent-pattern review found RandomUInt32 returning zero on
provider failure. RandomBelow could then loop indefinitely for a rejection
threshold or return a deterministic choice. The common checked byte-fill
helper bounds the provider count, skips genuinely empty requests, cleanses
partial output and propagates failure. Bounded-choice failure escapes instead
of reusing a failure sentinel; a successful zero word remains valid.

Previously unchecked fills in identity padding, router transport material,
tunnel IDs/records/tests, I2NP IDs, peer selection, NTCP2/SSU2 packet padding,
connection IDs/nonces and streaming IDs now use that contract. Candidate
private keys are cleansed on exceptions before persistence/publication.
Recognized key-generation failure no longer retries a different algorithm;
explicit legacy requests and separate unsupported-selection compatibility
mapping remain distinct. No primitive, I2P wire format or capability grant is
changed. Provider failure is a local operation failure, not a peer fault.

Tunnel construction discards phony hops and marks failed builds; pending
reply entries roll back only the matching attempt, releasing ownership outside
the mutex. Test encryption is staged before correlation/onDrop publication,
and key/tag buffers are always cleansed. Phony-hop ownership insertion now
precedes publishing its link so allocation failure cannot dangle the chain.
Source contracts and focused helper regressions are prepared; no provider
failure was injected into production locally. Implemented / static;
Pending CI compilation and Pending CI tests.

### RT-049 — Handshake and tracker typed failures committed partial state

The same callback-zero pattern existed in BEP 10 and tracker replies. Known
handshake fields could be skipped, then negotiation marked received; malformed
tracker values could reset the failure ladder and commit other fields.
Malformed flags now prevent all transactional commits. Unknown fields, opaque
tracker IDs and extension ID zero retain their documented behavior. Existing
peer/tracker tests cover preceding valid fields, invalid typed values, retry
availability, failure accounting and known disabled/unknown extensions.
References: [BEP 10](https://www.bittorrent.org/beps/bep_0010.html),
[BEP 3](https://www.bittorrent.org/beps/bep_0003.html).
Implemented / static; Pending CI tests.

RT-048 transaction review: SSU2 clears cached outbound handshake state before
replacement construction and discards incomplete replacements on failure.
Unauthenticated Retry input does not revoke a valid cached request; an
accepted Retry revokes it before rebuilding. Candidate transport keys are
cleansed on failure, and Tag::Randomize callers must check status before
publication. Legacy supported key generation no longer retries with a weaker
algorithm after provider failure. Existing unsupported-selection mapping is
a separate compatibility policy, not a newly validated capability.

### RT-050 — Worker initialization evidence has a credential boundary

MSVC run 42419357644, job 128544374044, SHA 8ffd1ad8 skips supervisor
handshake coverage after worker initialization status 0xC0000142. Parent-only
CDB diagnostics are prepared with -noshell and without child-following -o.
The worker command line carries a bootstrap credential, so automatic child
creation output must not be enabled. A negative workflow fixture rejects
child-following. Worker-stack attribution remains Blocked until a non-secret
loader probe or an out-of-band bootstrap design is available. No sandbox,
ACL or process mitigation is relaxed. Pending CI tests for parent diagnostics.

## 2026-11-08 — Continuation failures and preventive trust boundaries

Exact run/job/SHA references for this batch are in COMPILATION-ERRORS.md.
Complete logs reveal no ASan, UBSan or TSan report in the executed suites.
The full TSan registry regression now passes with instrumented OpenSSL;
focused run 42426234142 / 128565003226 passes 1/1 in 0.12 seconds. This
validates those exercised dependency paths, not a clean full suite. RNG,
metainfo, verification parity and profile lifetime regressions pass in the
executed sanitizer lanes. Historical unexecuted hypotheses remain open.

### RT-051 — Keygen did not contain the new fail-closed generation exception

All OpenSSL/sanitizer lanes, OpenBSD and MinGW fail tools-regressions at
its RSA keygen invocation (tools/test-tools-regressions.cmake:260).
`Identity: Signing key generation failed` escapes main and aborts instead
of returning the documented unsupported-generator status. The library's
fail-closed behavior is retained. Keygen now catches at the CLI boundary,
returns 2, emits the existing non-secret refusal and creates no output file.
All three RSA names and numeric aliases pin status, empty stdout and no file.
Vain's enclosing failure boundary and the supported router fixture were
reviewed as equivalent consumers. Implemented / static; Pending CI tests.

### RT-052 — EOF was authenticated as a live empty IPC record

Linux IPC fails test-privsep-linux-ipc.cpp:623 in all four OpenSSL and three
sanitizer jobs. A zero recvmsg with hangup lacks SCM_CREDENTIALS, so the old
ordering reported missing record credentials instead of terminal close.
Ancillary errors and unexpected descriptors still reject first; a terminal
empty read is now closed before live-empty-record authentication. Buffered
nonempty records still authenticate before delivery. Added buffered-close,
wrong-identity-close and empty-close controls. OpenBSD imsg's buffered-drain
and read-zero path was inspected separately. Pending CI tests.

### RT-053 — Retry token/version state preceded complete validation

Preventive source review, not an observed CI crash: ProcessRetry cached the
header token before AEAD and payload validation. It also changed version-bound
crypto state after only checking the first DateTime block. Full Retry-state
block grammar now validates before version replacement, and token insertion
follows complete payload/termination/nonzero-token checks. A valid prefix plus
forbidden trailing blocks is covered by the existing preauth regression;
source-order contracts pin the real handler. Other NewToken, HolePunch and
RelayResponse update sites were reviewed for validation authority. No I2P wire
format changes. Implemented / static; Pending CI tests.

### RT-054 — IPC refusals preceded per-record authority and shared deadlines

Preventive follow-up to RT-052 found nonempty frame decoding could generate
Error replies or consume the violation budget before SCM_CREDENTIALS checks.
Authentication now precedes recoverable refusals. Wrong-identity malformed
frames and descriptor-bearing frames must produce no response or FD leak;
matching identity still receives typed refusals and can recover.
Refusal sends also used a fresh 250ms budget inside a shorter receive call.
Their send budget must be bounded by the remaining absolute receive deadline;
source ordering and backpressure regressions retain this obligation.
Implemented / static; Pending CI tests. No endpoint capability is relaxed.

### RT-055 — Compact tracker tails were committed as successful peers

Preventive equivalent-pattern review found HTTP compact peer parsing ignored
trailing bytes after complete 32-byte hashes. The datagram consumer also
updated scheduling, counters and peers before validating the hash range.
Both now reject incomplete tails before publication; the public datagram API
also rejects a null nonempty range. Empty replies and existing peer caps stay
valid. Tests cover 0/1/31/32/33/63/64 bytes and preserve prior state on malformed
responses in both paths. This is stricter rejection of malformed input, not a
new wire format or a claim of identical malformed-input tolerance to Java.
References: [I2P BitTorrent](https://www.i2p.net/en/docs/applications/bittorrent/)
and [UDP tracker specification](https://i2p.net/en/docs/specs/udp-announces/).
Implemented / static; Pending CI tests.

### RT-056 — Reused tunnel-test IDs could grant stale callback authority

Preventive concurrency review found tunnel tests keyed only by random message
ID. Live insertion replaced an existing attempt, and a retained old onDrop
or rollback erased solely by ID and could penalize its replacement's tunnel.
Pending tests now retain unique local attempt authority, reject live-ID
collisions and retire by both ID and authority. Detached owners and final
cleanup run outside the correlation mutex. A stale callback after reply,
removal or reuse is inert; only a matching drop penalizes the outbound tunnel.
The existing timing regression reuses the exact same ID, pair and deadline
and exercises repeated callbacks. CreateTunnel's separate pointer-qualified
rollback was reviewed as the equivalent pattern. Implemented / static;
Pending CI compilation and Pending CI tests. Wire IDs remain unchanged.

## 2026-11-08 — Exact diagnostic evidence after c3f5df53

### RT-057 — PageHeap lane stops on worker initialization, not a crash marker

Windows run [42431789062](https://github.com/daviduhden/openi2pd/actions/runs/42431789062),
MSVC job 128582094948, SHA c3f5df53, passes ordinary tests but the heap
step fails on supervisor debugger exit 77. The exact uploaded PageHeap log
reports worker initialization failure 3221225794 (0xC0000142). It contains
no standalone OPENI2PD_CRASH event; echoed debugger command text is not an
exception event. The handshake and shutdown paths remain unexecuted after
this startup failure. Classification: unresolved startup/capability gap,
not a newly demonstrated heap-corruption cause and not heap-clean evidence.

Exact Windows artifacts were inspected under
`/var/tmp/openi2pd-ci-windows-42431789062`; incomplete large sanitizer artifact
downloads initially exhausted /tmp but were recovered under /var/tmp with
logs, XML, images and hashes. The workflow remains
fail-closed on debugger exit 77; no skip, timeout increase or exception
suppression was added. Correction pending; a nonsecret child-initialization
probe and a successful worker-start lane are still needed.

Newest sanitizer failures are TF-089/TF-090 assertions. No ASan/UBSan/TSan
report marker was found in the reviewed latest job logs. Earlier sanitizer
families remain historical evidence; this observation neither validates
local changes nor proves the absence of races or memory errors.

### RT-058 — Datagram swarm counters narrowed before saturation

Preventive finding derived from the tracker-width review, not an observed
CI assertion. TorrentsTunnel.cpp decodes UDP seeders/leechers as uint32_t,
then passed them to int parameters of HandleDatagramTrackerResponse. On the
supported 32-bit int toolchains, values above INT_MAX could become negative
before ClampTrackerCount and be reported as zero. HTTP already retains
int64_t through the same saturation helper.

The internal declaration and definition now accept int64_t. The sole
production caller promotes its uint32_t values losslessly; negative internal
callers retain clamping to zero. No wire or persistence format changes.
TestDatagramTrackerCountBounds compares HTTP/datagram results at INT64_MIN,
-1, zero, INT_MAX, INT_MAX+1, UINT32_MAX and INT64_MAX, and passes an actual
uint32_t wire-typed operand. Main invokes it in the already-registered parity
suite. Counts and consumers were reviewed independently for platform/type
consistency. Implemented / static; Pending CI compilation and Pending CI tests.

### RT-059 — Nested IPC refusal rebased an already-consumed deadline

Preventive equivalent-pattern review of RT-054, not a new CI timeout. POSIX
ReceiveWithFd calculated relative milliseconds remaining, then RejectFrame
called SendWithFd, which started a new absolute budget. Preemption between
those operations could extend the original receive deadline.

The public send converts its relative timeout once and delegates to a private
SendWithFdUntil carrying an absolute steady-clock time point. Both refusal
branches retain min(original finite deadline, now+250ms), including through
Error encoding. Infinite receives retain the historical 250ms refusal cap.
Public signatures, endpoint authorization, descriptor policy and EOF behavior
are preserved. The compile-free source contract pins both calls and rejects
relative recomputation inside the absolute sender. Existing backpressure and
zero-timeout regressions remain enabled. Implemented / static; Pending CI
compilation and Pending CI tests.

Equivalent Windows handoffs were inspected: WindowsIpc.cpp's helloBudget
passes a relative remaining budget into ReceiveWorkerHello/Receive, while
WindowsSupervisor.cpp's shutdown send derives a relative remaining budget.
These separate APIs remain a static deadline-propagation follow-up; no observed
CI failure is attributed to that precision risk. The POSIX fix does not claim
to resolve Windows timing or startup coverage.

## 2026-11-08 — Preventive lifetime, bounds and Windows deadline review

### RT-060 — Mutex publication retired the last owner while locked

Static finding while fixing CE-085, not an observed sanitizer report. The
existing AtomicSharedPtr fallback Store assigned its old shared pointer under
the internal mutex. A final deleter reentering Load could deadlock. Store now
swaps ownership and retires it after unlocking; the new failed-CAS path also
replaces expected outside the lock. Strong CAS matches both raw pointer and
control-block ownership rather than operator== alone. Explicit fallback tests
cover distinct owners/aliases and deterministic reentrant Store/failed-CAS
retirement while the wrapper remains alive. Existing RouterInfo, Tunnel,
RouterContext and Blocklist consumers were reviewed. Wrapper lifetime still
requires quiescence before destruction. Implemented / static; Pending CI
compilation and Pending CI tests, including selective MSVC coverage.

### RT-061 — Public Tag bit and encoding extents were incompletely bounded

Static finding from CE-086's equivalent-type review, not a demonstrated remote
exploit. GetBit rejected upper offsets but a negative int could address before
m_Buf. It now rejects negatives before conversion and preserves MSB-first mask
results for valid indices. Kademlia's actual production recursion begins at
zero; no negative network-driven caller was shown.

ToBase64/ToBase32 also accepted caller lengths greater than the fixed storage,
allowing a raw encoder read beyond the buffer. They now throw a generic
length_error before reading on an invalid extent. The sole nondefault
production Base64 caller uses 16/32 bytes of Tag<32>; no argument-taking
production Base32 caller was found. Valid prefixes, wire encodings and stored
formats are unchanged. Kademlia tests cover masks, negative/extreme indices,
widths 8/32, zero/short/full encodings, oversized/SIZE_MAX/converted-negative
lengths and valid recovery. Ordered source guards pin rejection before access.
Implemented / static; Pending CI compilation and Pending CI tests.

### RT-062 — Windows nested operations restarted relative wait budgets

Preventive continuation of RT-059, not an observed timing failure. Absolute
Windows deadlines now flow through accept, WorkerHello, overlapped send/read,
both 250ms refusal branches and supervisor shutdown/graceful/forced waits.
Legacy DWORD timeout APIs convert once and delegate; INFINITE has an explicit
max-time-point sentinel, and finite conversions cannot become INFINITE.

Worker Stop previously waited its full budget then granted it again to forced
termination. StopUntil/TerminateAndWaitUntil share the original deadline;
supervisor graceful waits use WaitUntil too. Existing authentication, nonce,
role, DACL and re-arm checks remain. CancelIoEx still drains kernel-owned
OVERLAPPED state with GetOverlappedResult before releasing stack storage.
Cancellation cleanup may outlast the wait deadline; lifecycle mutex admission
is also outside that budget. No strict whole-call wall-clock claim is made.
Expired receive/reuse and absolute roundtrip regressions retain the legacy
relative controls; ordered source guards pin propagation. Implemented / static;
Pending Windows CI compilation and Pending CI tests. This does not resolve
0xC0000142 startup coverage or canceled MSVC diagnostics.

## 2026-11-08 — Executed `6390fdea` runtime continuation

### RT-063 — Writable polling suppresses a fitting nonblocking send

OpenSSL 3 run 42492805461 (Clang/GCC 128768619632/128768619750), OpenSSL 4
42492808412 (GCC/Clang 128768627826/128768627990) and Validation 42492817128
(UBSan/TSan/ASan+UBSan 128768653761/128768653799/128768653853) fail
`TestSendDeadlineAfterDrain` at test-privsep-linux-ipc.cpp:785. Each reports
`post-drain send failed after 7 queued frames: send timeout`. The LogRecord
fixture satisfies the production schema. Linux can retain a conservative
POLLOUT threshold after one record drains, although the smaller record fits.
Channel::SendWithFdUntil polls before making that nonblocking attempt.

The channel now attempts MSG_DONTWAIT first and polls only after EAGAIN,
retaining the same absolute deadline through interruption and retries.
Timeout zero allows one immediate attempt; a genuinely full queue still
fails immediately without changing records. The focused drain and saturation
regressions pin both behaviors. Global review identified the same pre-write
poll ordering in OpenBSD imsg: its nonblocking write now precedes polling,
partial progress does not renew the deadline, and ambiguous queued frames
close on failure. This OpenBSD change is preventive, not an observed imsg
failure. Header contracts and source-order guards were updated. Implemented /
static; Pending CI compilation and Pending CI tests.

The related TUI stream writer still polls before writing. No demonstrated
stream failure is attributed to it. HTTP CONNECT and SOCKS upstream success
handlers additionally use synchronous sends in completion handlers. Safely
replacing those requires serialized handler resources, bounded full writes
and admission atomic with I2PService shutdown: AddHandler currently inserts
independently of Stop/ClearHandlers' snapshot. A partial async substitution
would preserve that race. This structural follow-up remains Pending; it has
not been hidden by a new timeout or weaker shutdown behavior.

Named RNG, SSU2 preauth/payload, EdDSA, ratchet, torrent verification,
tracker and atomic/Tag regressions pass in the three executed sanitizer lanes.
No sanitizer-error marker appears in their full logs, but both failed test
families keep each job red. This supplies specific regression evidence, not
an absence-of-vulnerabilities claim. Earlier MSYS2 Unicode/version and MSVC
worker startup remain unresolved: the exact MSVC artifact reports exit
3221225794 (0xC0000142) before authentication. Its supervisor skips; PageHeap
also skips, so there is no new heap stack or shutdown validation. Three
OpenBSD worker capability skips provide no explicit startup reason in the
retained LastTest output; that evidence gap stays open.

### RT-064 — Extracted torrent acceptors borrowed a raw tunnel pointer

Preventive source finding, not a crash observed in the new CI logs.
TorrentsTunnel::Accept retained raw `this`, while StreamingDestination copied
its acceptor under a mutex and invoked it after unlocking. Deregistration
cannot revoke that extracted copy. Destruction without a completed Stop is
explicitly supported by the tunnel destructor, so raw callback ownership
was insufficient even though normal service-thread shutdown reduces exposure.

The callback now locks a weak owner and a distinct shared registration
generation. Registration and handler admission each share a barrier with
revocation. Stop closes registration synchronously before marshalling
service teardown; a separate protected stopped bit prevents fresh factory
publication while m_Stopping still awaits the service thread. Start revokes
prior generations, and destruction revokes them without shared_from_this.
Final registration publication also checks the same barrier. Existing peer
limits and handshake initialization are retained.

The existing torrent-security target now prepares copied-callback stop,
restart, active admission, concurrent revocation, closed registration and
final owner-release controls without live workers/listeners. Its common
CTest/security labels already include sanitizer lanes. Header interfaces
and callback/dispatcher locking were reviewed, including I2PServerTunnel,
BOB and SAM acceptors; those already carry weak/generation or shared owner
authority. This bounded correction does not resolve the separate general
I2PService forwarder-admission follow-up described above. Implemented / static;
Pending CI compilation and Pending CI tests.

## Integral continuation — static findings after `6390fdea`

No later remote run validates the following local changes.

- **RT-065 / CONT-045:** I2PService readiness invoked callbacks while iterating
  its vector, allowing reentrant registration/erasure to invalidate traversal.
  Retained service callbacks also survived stop/restart despite their promised
  authority. Distinct lifecycle/wait states, detached invocation, bounded
  pending callbacks and weak destination ownership now address that readiness
  seam. Seven controls include executor destruction and stale canceled waits.
  GuardStreamCompletion suppresses stale results at its check; it does not
  close the separate concurrent Stop-to-AddHandler publication window.
- **RT-066 / CONT-046:** a storage-supervisor stale-reply loop renewed five
  seconds on every ReceiveWithFd, after another relative send and unbounded
  mutex admission. One absolute deadline now spans timed admission, output
  and stale draining, including initial broker send/reply. Relative APIs stay
  supported. POSIX ChildProcess shutdown still composes relative phase waits;
  that related follow-up remains Pending rather than silently closed.
- **RT-067 / CONT-047:** reload-local destination lookup/insertion treated
  transient marker strings like reusable persisted identities in some endpoint
  paths. One cache follows the loader's full transient prefix family for all
  consumers; file-backed reuse and failed-creation misses are retained.
- **RT-068 / CONT-047:** an ECIES-origin LeaseSet1 reached parsing/cache update
  despite lacking a static ECIES identity binding. Classification now rejects
  it before that boundary, retaining masked-type handling and signed plaintext
  acceptance. The prepared fixture checks both rejection and old-cache identity.

All four are Implemented / static, Pending CI compilation and Pending CI tests.
The upstream provenance and source comparisons are in upstream-sync.md and
INTEROPERABILITY.md; they do not establish live Java interoperability.

## 70f66977 runtime evidence boundary

Validation 42512621915 and focused TSan 42512623978 stop during compilation
(CE-088/089). Uploaded diagnostics contain no built TSan executables; the
OpenBSD artifact likewise ends at configure/build. These runs neither close
nor reproduce earlier IPC, heap, worker capability or shutdown findings.
Local torrent interest reconciliation is a preventive source finding:
periodic and peer-availability paths previously retained stale interest or
asserted interest despite skipped files. Active requests now retain interest
until retired, with stop/removal overriding it. A focused selection/request
regression is prepared in test-torrents-verify-parity; Pending CI tests.

## RT-069: POSIX worker phase and interruption budgets (static finding)

Source review found ChildProcess::Stop renewed advisory-send, graceful,
TERM and final-reap relative waits. ChildStopBudget now records all absolute
phase boundaries once; Until waits preserve them, and unreaped children keep
PID/pidfd ownership. A pure boundary regression plus existing child-reap
fixture and negative source contract are prepared. Startup hello reads also
become nonblocking before the first byte and share one five-second deadline
across fragments and EINTR. Zero-budget liveness probes defer interrupted
calls rather than spinning. Implemented / static; Pending CI compilation /
Pending CI tests. Scheduler/kernel stalls can exceed wall time, and detached
intermediary spawn waits remain a distinct pending review.

## RT-070: authoritative torrent availability (static finding)

HAVE_NONE reset the peer bitfield without reconciling interest. Equal-size
BITFIELD updates preserved prior set bits. Both now replace availability
and reconcile interest, retaining active requests until retirement. The
existing verification fixture drives actual message handlers. Implemented /
static; Pending CI tests. Earlier passing interest tests did not cover these
new cases.

At 9f2934f8, focused instrumented OpenSSL TSan run 42545641719 passes;
Validation's logger assertion is TF-096, not a sanitizer report. No new
sanitizer family was found in the retrieved full logs. Historical Windows,
heap, capability and shutdown findings retain their individual evidence
requirements; passing sibling lanes do not close them globally.

## RT-071: idle timer retains service and destination (static finding)

I2PService's periodic idle timer captured shared_from_this. The service owns
the timer and retains its destination, so a stopped/non-drained executor can
preserve the cycle. The local correction uses weak ownership, a distinct
IdleWait identity and one gate for timer mutation/revocation. Stop and
destruction revoke before cancellation; timer destruction occurs outside
the gate. Stale successful callbacks cannot affect a rearmed/restarted wait.
Prepared controls cover owner/destination release and owned-executor
teardown without polling. Idle configuration/activity publication and
cross-thread destination idle-flag access are reviewed together.
Implemented / static; Pending CI compilation / Pending CI tests.

Full ServiceAcceptor/global AddHandler admission remains a separate
architectural task; these timer changes do not make handler publication
atomic with Stop/ClearHandlers. Windows GUI startup and native MSWin32
Unicode failures retain their exact-run evidence requirements.

## 80a487d5 capability evidence

OpenBSD job 128952719567 passes with worker identity, credentials and torrent
supervisor capability skips. Existing skipped branches now emit a fixed
non-secret reason before code 77: root runner, missing account data,
unsupported kernel control, or unimplemented exec-free provisioning.
No skip condition or security restriction changed. These source changes are
Pending CI tests; the historical run still has no startup reason output.

The RT-071 follow-up also replaces GarlicDestination's advisory idle bool
with atomic value access after tracing service and tunnel-pool callers.
It does not publish unrelated destination data. Oversized idle settings can
overflow clock-unit conversion/addition; IdleDeadline now bounds both steps
and deterministic near-max/UINT64_MAX controls are prepared. A synchronized
two-thread test exercises actual idle APIs for TSan. No local dynamic
validation was performed.

Independent review of RT-071 found atomic timestamps alone did not serialize
fresh activity with the timer's idle decision. Both now share the idle gate;
Resume runs after unlock to avoid reacquiring the same mutex recursively.
A fresh-activity/rearmed-authority regression and source-order guard are
prepared. Broader concurrent Resume/key-replacement and global handler
admission remain separate lifecycle review work, not guarantees of this fix.

## RT-071 validation at d63b6b90

Idle/readiness tests, including owner/executor teardown, extreme deadlines
and cross-thread idle access, pass all three executed sanitizer jobs in
Validation 42667615188 and the OpenSSL suites. The full jobs fail TF-098,
not these regressions. This validates the executed timer slice, not the
separate global handler-admission or concurrent Resume architecture.

## RT-072: parser and remote-error operands reach diagnostics (static)

Source review traced complete failed destinations, unknown first tokens,
ignored SAM parameters, rejected numeric values and UDP header buffers into
LogPrint in I2PService, SAM and BOB. Equivalent unrecognized RPC methods,
DHT query names and BEP 15 error payloads were also logged. Expected public
schemas do not sanitize arbitrary rejected input; no real user secret or
exploit observation is asserted.

Local corrections log fixed operation/outcome summaries and retain validated
length/numeric metadata. BEP 15 transaction-port validation precedes the
outcome log; existing bounded tracker reports and protocol replies remain
unchanged. A credential-marker regression asserts a live logger sentinel,
one fixed outcome, no operand markers and exactly one nullptr completion.
Multiline negative source guards cover all affected sinks. Implemented /
static; Pending CI compilation / Pending CI tests.

## RT-073: tracker error dispatch checks expiry before locking (static)

The old expired check followed by a fresh lock()->ReportTrackerFailure does
not itself grant lifetime; lock may return empty. The local dispatch retains
one strong owner before use. Repository-wide variants in SSU2 already check
the locked pointer; cleanup-only expired checks do not dereference values.
A compile-free source contract pins the retained owner. Implemented / static;
Pending CI tests. No CI crash is attributed to this preventive finding.

Previously tracked CONC-028 still includes UDP cleanup/statistics/keepalive
raw-this timer callbacks beyond ACKs; canceled-but-queued success needs an
owner/generation architecture. PRIVSEP-017 likewise remains open: retained
unreaped PID/pidfd ownership can be lost when ChildProcess is destroyed or
move-assigned after failed Stop. RT-069 only guarantees retention while the
same owner survives; phase-budget passes do not validate retirement.

## RT-074: posted AddressBook re-arm has check/use lifetime gap (static)

`AddressBook::PostSubscriptionsRearm` captured raw this, checked an atomic
alive token, then invoked a member. Stop/destruction could interleave after
the check; the token did not retain lifetime authority. This is preventive
source analysis, not an attribution of a CI crash. The local correction
captures immutable shared admission state plus the generation token.
Stop revokes owner access under the same gate held throughout the member
call; Start invalidates older tokens before publishing fresh authority.
Lock order is admission then subscription mutex. The admitted member must
not invoke lifecycle methods or external callbacks synchronously.

`addressbook-rearm-lifetime-static` pins capture, gate ordering and revocation
for manual CI. Implemented / static; Pending CI compilation / Pending CI
tests. Separate raw timer callbacks remain under review; this is not a claim
of complete AddressBook shutdown safety.

Global equivalent review found no second exact token-guarded raw-owner post.
Existing raw callbacks remain CONC-003/005/022/028. Six TorrentsDHT timer
callbacks also pair raw this with service-state checks; whether executor
serialization closes their teardown interval remains an unresolved static
hypothesis, not a demonstrated defect. Inspect Stop/owner-thread ordering
before changing them. The DHT receiver's registration separately drains
admitted dispatch, so it must not be grouped by superficial capture shape.

### Windows startup attribution at 176aec32

[Windows run 42690715945](https://github.com/daviduhden/openi2pd/actions/runs/42690715945)
job 129409411723 still times out on router version output; MSVC sibling job
129409410995 skips supervisor startup after worker exit 0xC0000142. No stack
attributes either failure. Prepared opt-in loader/entry CDB evidence uses
the exact retained router image and runtime DLLs, a 20-second deadline and
code-only output. It does not guarantee a stack at timeout or resolve the
startup fault. Original CTest runs independently of this diagnostic step.

## RT-075: tracker callbacks can mutate a successor admission (static)

At 02901127, tracker correlation uses only infoHash/tracker slot. Cancellation
allows immediate reuse while old successful dial/write/read/deadline callbacks
may remain queued. Those callbacks could release the successor, close its
stream, report failures or consume an obsolete successful response. No CI
crash is attributed to this preventive source finding.

`TrackerInflightRegistry` now assigns bounded monotonic uint64 authority to
each admission; zero is invalid and exhaustion refuses new work. Every
asynchronous stage must match it before state access/release; stale successful
bodies return before statistics and peer parsing. Retirement does not reset
authority. Existing tracker-dispatch tests add reuse/restart/torrent-retirement
controls, with source contracts pinning actual callback consumers. All helper
declarations/callers were reviewed; no wire/persisted format changes.
Implemented / static; Pending CI compilation / Pending CI tests.

## RT-076 / CRY-236: incompatible Ed25519 pairs were accepted (static)

Compatibility construction treated any supplied public-key mismatch as a
historical scalar and never checked the second derivation. The old regression
even required an arbitrary mismatch to sign under a different derived key.
Global consumer review found pair publication in Identity factories and the
equivalent Ed25519ph online path. The local correction requires modern or
matching historical type-7 public keys, exposes explicit readiness and refuses
invalid fallback publication. Type 8 requires a matching provider key; its
readiness cannot authorize a type-7-only fallback through a base reference.
Online serialized pair rejection resets partially parsed state. RSA
verification-only parsing is unchanged. Expanded secrets and BN contexts
are released on rejected/exceptional construction paths.

Prepared tests distinguish valid modern/RFC, genuine historical and unrelated
pairs; types 7/8 also cover corrupted serialized seeds, cleared prior identity,
successful reparsing, null factory inputs and zero rejected output. This is
preventive analysis, not an observed sanitizer crash. Implemented / static;
Pending CI compilation / Pending CI tests. Void Sign failure propagation,
offline transient pair validation and LeaseSet self-verification remain open.
Expansion now returns checked success: a failed SHA-512 or absent input
clears all 64 output bytes and prevents signer/blinding use. All consumers
were adapted, with null-input/output and positive controls. Public-point
encoding still discards its encoder status; broader primitive/allocation
and RedDSA readiness remain part of the unfinished signing-failure family.

### Residual lifetime and retirement review

The AddressBook cache timer still captures raw this; Stop cancellation does
not drain an already successful completion in the published revision.
The local RT-074 continuation now uses an independent cache admission gate
and fresh shared identity for every wait. Start/ResetTimers/Stop serialize
timer ownership; callbacks reject stale/revoked authority before owner use.
The locked rearm helper avoids recursive acquisition. Reviewed filesystem
cleanup and four fixture overrides do not call book lifecycle; the virtual
contract now prohibits synchronous lifecycle/reset reentry. The existing
source regression pins capture, per-wait identity, teardown and lock ordering.
Implemented / static; Pending CI compilation / Pending CI tests. Subscription
timer callbacks remain a separate gap, not covered by this correction.

Six DHT timers also borrow raw this plus weak service-state guards. Normal
tunnel shutdown serializes them on the destination executor, so that path
does not demonstrate a race. Concurrent destruction with an active executor
has no timer admission drain; the persistence mutex covers disk IO only.
Their receiver registration separately drains dispatch. These residuals need
deterministic teardown regressions; they are not causes established by CI.
PRIVSEP-017's deferred PID/pidfd ownership remains open; closing a leaked fd
alone would discard reap authority rather than repair it.

## RT-077: peer-dial callback can outlive its tunnel or publish after Stop

The published ConnectToPeer callback captures raw this. I2PService still
invokes the user completion with nullptr after its generation retires, so
that callback can reach FinishDial after owner destruction. Weak capture
alone would not serialize AddHandler/Connect against Stop. This is preventive
source analysis, not attribution of a CI sanitizer report.

The local correction creates a separate dial-run admission even when a
shared destination already accepts streams. Inbound acceptor registration
can be absent or replaced independently and cannot serve as run identity.
The production callback factory reserves a bounded dial, retains only weak
owner authority, matches the pending torrent instance and holds generation
admission through handler publication/Connect. Stop revokes that same gate
synchronously. Revoked/stale/orphaned successes close their streams; IDs
fail closed after exhaustion and are not reset by restart.

Friend regressions exercise the actual callback factory without network:
no inbound registration, shutdown barrier, retired generation/reservation,
replaced owner, exhaustion across restart and expired tunnel. Source
contracts pin the production capture and callback consumer. Reviewed Connect
posts IO and does not synchronously reenter lifecycle under the gate.
Implemented / static; Pending CI compilation / Pending CI tests.

### Published Windows diagnostic: CRT entry reached, cause still open

Run 42719195489, SHA 02901127, CLANG64 job 129508512808 completed and its
windows-msys2-ctest artifact was inspected while MSVC remained in progress.
loader-entry.txt contains OPENI2PD_LOADER and OPENI2PD_ENTRY, then the
20-second deadline marker, without STARTUP_EXCEPTION. Exact-image COFF
inspection identifies that entry as WinMainCRTStartup, not WinMain.
CRT/static initialization still precedes application dispatch. Later ZLIB1
loading from Service Fabric does not identify a cause. No stalled stack
was captured, and missing checksum/symbol information limits attribution.

The local diagnostic now adds MessageBoxA/W code-only breakpoints to identify
modal callers without collecting message text/arguments. Its command allowlist
and a negative privacy mutation are updated. This is Pending CI tests; the
published run does not include those new commands or local production fixes.
