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
- **Correction:** `ClassifyInformationalRequest`/`PublishInformationalResponse`
  answer `--version`/`--help` before `Daemon.init`, from the authoritative
  `GetCommandLineW()` vector, writing to the inherited standard handle (or
  `AttachConsole` for this banner only) and terminating with `std::_Exit`.
  `SetErrorMode` suppresses unattended error dialogs for later failures. The
  driver now requires the banner on every platform.
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
