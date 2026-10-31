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
