<!--
See the LICENSE file at the top of the project tree for copyright
and license details.
-->

# Test failure inventory

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
