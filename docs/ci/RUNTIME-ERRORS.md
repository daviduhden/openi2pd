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
