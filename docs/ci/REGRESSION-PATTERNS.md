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
