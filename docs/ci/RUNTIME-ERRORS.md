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
