<!--
See the LICENSE file at the top of the project tree for copyright
and license details.
-->

# Runtime error inventory

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
