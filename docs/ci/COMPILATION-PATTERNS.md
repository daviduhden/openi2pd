# Compilation diagnostic patterns and repository-wide review

This is the reusable search guide for compiler and linker diagnostics. It
complements the observed run records in
[`COMPILATION-ERRORS.md`](COMPILATION-ERRORS.md) and
[`COMPILATION-WARNINGS.md`](COMPILATION-WARNINGS.md). CI observations and
static hypotheses must remain explicitly distinguished.

## Required triage sequence

1. Capture workflow, run, job, URL, full commit SHA, toolchain, platform and
   matrix entry. Retrieve the failed job log and successful sibling logs;
   successful builds can still report warnings.
2. Identify the first significant diagnostic for each affected target.
   Collapse parser/template cascades into their initiating cause. Separate
   source diagnostics from dependency, runner, permission, timeout and
   packaging output.
3. Compare the diagnosed file at the run SHA with the current tree. Mark an
   old diagnosis obsolete when later code changed its cause; do not transfer
   remote evidence to local-only changes.
4. Search for semantic variants across source, headers, tests, generated
   inputs maintained by the project, platform branches, overloads, templates
   and macros. Inspect declarations, definitions and consumers, not just
   textual matches.
5. Correct demonstrable instances and prepare a focused positive and negative
   regression. Review target membership and platform reach before declaring
   the change complete.
6. Record the source fix as statically reviewed until a manual CI run on that
   exact revision passes. Do not claim a warning-free matrix from a partial
   job or from a job stopped before the relevant target.

## Reusable patterns from retained CI history

| Diagnostic family | Search beyond the reported line | Preventive review |
|---|---|---|
| Unterminated/nested C or C++ comment (`-Wcomment`, parser cascade) | Scan all first-party C/C++ source and headers, including tests and platform-only files; distinguish comment delimiters from quoted and raw strings. | Keep one block opener/closer; exercise a lexical checker on nested, unclosed and valid-string fixtures. |
| Enum switch warning | Find every switch over the enum and all enumerators, including tests and platform branches. | Keep switches exhaustive without a catch-all that hides future values; add a regression for new lifecycle states. |
| Signed/unsigned comparison | Follow the value's source type, arithmetic range and sentinel/wrap behavior through consumers. Search relational comparisons and integer promotions, not only warning text. | Use a type/range that represents the domain; preserve wrap semantics explicitly and test boundaries. |
| `-Wfree-nonheap-object` in a standard-library trace | Trace inlined vector/string operations back to the owning project operation and allocation shape; inspect bounds and lifetime before assuming a compiler false positive. | Prefer a construction matching final size when that is semantically exact; require the affected compiler lane to decide whether the warning is gone. |
| Deprecated crypto API | Search all equivalent API families and compile-time capability branches; compare OpenSSL and LibreSSL declarations and ownership rules. | Route through a maintained compatibility boundary; preserve verification and error checks on every provider path. |
| Missing or unresolved symbol | Compare declarations, definitions, visibility, source lists, target link interfaces and conditional compilation for every consuming target. | Validate installed consumers and static/shared variants; header compilation alone does not establish link closure. |
| Android Lint warning | Inspect lifecycle/context ownership, every localized resource variant, accessibility descriptions and resource references. | Fix semantic causes; do not suppress, remove translations or alter platform support to silence a report. |
| External linker/compiler warning | Resolve the full inlining/template origin and dependency version; identify whether project code instantiates or calls it. | Track separately from owned source warnings; pursue upstream or narrow adaptation, never global suppression. |
| Unused asynchronous completion argument | Trace each callback argument through the handler and downstream calls. Distinguish unused counts from transferred-byte values that are forwarded, and distinguish the handler's unused copy of a captured request from the capture that owns its lifetime. | Omit only unused parameter names; preserve callback types, captures, lifetime authority and forwarded values. Add a static consumer review to avoid creating an undeclared use while clearing adjacent warnings. |
| Platform callback or test helper unused on one target | Follow API-mandated callback signatures and fixture consumers through their platform guards; compare Windows and POSIX branches. | Keep required ABI types and omit unused names; place platform-only test helpers under the same condition as their consumers, without dropping the test on supported platforms. |
| Test uses an unqualified domain helper | Compare the test's `using namespace`/includes with the declaration namespace and all sibling tests; scan for constants/functions from `i2p::client`, `i2p::tunnel`, `i2p::data`. | Import the owning namespace or qualify the symbol; keep the test logic unchanged and add it to the same build matrix. |
| Directional IPC endpoint rejects the reply it must send | Trace `Send`/`Receive` direction checks against the protocol registry and every error/refusal path. | Permit only the typed refusal reply in both endpoint directions; keep requests/replies otherwise role-bound and test core-to-worker plus worker-to-core frames. |
| Direct dependency formatter instantiates an unsafe dependency branch | Search all direct `address::to_string`, endpoint streaming and `LogPrint` call sites, not only the warning's object file. | Route through the project address formatter; verify the linker warning disappears on the affected platform. |
| Removal test seeds an unowned root file | Compare the fixture path with package-owned directories and the documented persistence guarantee. | Seed realistic state in a subdirectory that dpkg does not own as an empty-dir prune target; assert before and after remove. |
| CMake developer policy warning | Record the exact CMake version and policy at the call site; check whether the policy is newer than the project's minimum and which package discovery semantics its NEW behavior selects. | Guard policy selection with `if(POLICY CMPxxxx)` and verify the config-package dependency/target closure across minimum and current CMake lanes. Do not globally disable developer warnings. |

## Warning scope and policy

Treat compiler warnings, Android Lint, linker warnings, static check failures
and infrastructure notices as separate categories. Attribute each warning to
first-party code, a dependency, a toolchain or a runner. Broad warning
suppression, warning-level reduction and casts without a semantic conversion
are not fixes. Enable warnings-as-errors only after the relevant supported
matrix is clean and third-party headers remain outside the project warning
scope.

For each root cause, record its search scope and any unreviewed platform or
configuration in the appropriate inventory. Keep historical findings even
after correction so repeated patterns can be recognized; annotate their
current disposition rather than deleting their evidence.

The normative requirement is owned by
[`AGENTS.md`](../../AGENTS.md#permanent-priority-policy-static-only-local-work).
This document provides the diagnostic-family search procedure only.

## Patterns from the `63c42ca4` batch

| Pattern | Search | Contract |
|---|---|---|
| Member call on a C array (`.c_str()` on `char[]`) | Inventory every `char`/`uint8_t` array local/member and every `name.member(`/`name->member(` where `name` is the array; distinguish a same-named `std::string`. | The `mkdtemp` buffer is already a C string; never call a member on it. |
| Partially undocumented Doxygen function | For every documented public function, compare the declared parameter names with the `@param` set; also catch a `@param` naming a parameter the declaration lacks. | `WARN_AS_ERROR=YES` needs every parameter documented, not just the interesting ones. |
| Raw `<placeholder>` in Javadoc | Scan comments for `<...>` outside `{@code}`/`{@literal}`/HTML; inventory unregistered block tags (`@implNote`, `@implSpec`, `@apiNote`, resource ids at line start). | Javadoc parses `<tunnel>` as HTML; `{@code}` escapes it. |
| Test-time `find_program(perl)` | Every CTest registration whose script re-discovers a tool; every `message(STATUS "... skipped")` followed by success. | Pass the configure-time interpreter and fail closed; a silently skipped contract is a false pass. |
| Native path separator in a comparison | Search `abs2rel`, `abs_path`, `catdir`, `catfile`, `rel2abs`, `split m{/}`, prefix `index()` checks and `File::Find` results in first-party Perl/CMake. | Normalise to `/` (and case-fold on Windows) in one helper; compare canonical containment keys with a component boundary. |
| Two-pipe capture on Windows | Every helper that drains stdout and stderr with `IO::Select`/`select`. | Win32 `select()` accepts sockets only; merge stderr into one captured stream. |
| `UNIX` guard on a portable install rule | Every `install(FILES|DIRECTORY)` and `if(UNIX ...)` around components. | Man pages and headers belong to their component on every non-Android platform. |
| Unversioned `core.autocrlf` checkout | `.gitattributes` has a `*` rule; CRLF-sensitive checkers report file:line. | Pin `text=auto eol=lf`, list binary exceptions and the one CRLF script. |
| Reserved word missing from a renderer allow-list | Every lexical guard that decides whether caller text may enter generated command/router text. | An nft verdict such as `accept` must never be usable as a table/chain name. |
| Privilege-dependent existence check in a workflow | Every `test -f`/`-d`/`ls` against a service-owned 0700 path and the `sudo` usage around it. | Either run the check as root or test a path the runner can traverse; do not read EACCES as "missing". |
| GUI-subsystem binary under headless CTest | Every CTest that launches a `WIN32` executable. | Bound the wait, print captured output and keep the exit status authoritative; a modal dialog must be a diagnostic, not a 30 s timeout. |

## Patterns from the `dc5540ae` / `c1125a75` batch

| Pattern | Search | Contract |
|---|---|---|
| Span member on a container | Every `.subspan(`/`.first(`/`.last(` call: is the receiver a `std::span` or a `std::vector`/`std::string`? | Wrap the container in `std::span` before a span operation; a vector has no `subspan`. |
| Platform-only local or parameter | Every `#if` in a test/source: is a local or parameter used only in one branch? | Move the declaration into the branch or mark the parameter `[[maybe_unused]]`; never add a warning suppression. |
| `@param` in both declaration and definition | Every member with a documentation block in a header and another block on its `.cpp` definition. | Parameters belong on the declaration only; Doxygen merges both and reports duplicate sections. |
| RouterInfo option list encoding | Every `WriteString (key)` followed by a raw delimiter and another `WriteString`. | One length-prefixed value per option; commas stay inside the value (`pq=4,3`). |
| Declared length vs backing capacity | Every guard that compares `len` to a protocol size before reading a fixed cell. | Capacity (`maxLen - offset`) is the memory-safety bound; a declared length may reserve a header and must get its own floor. |
| Parser mutating live state before validation | Every tracker/metainfo/peer parser: is the destination container cleared/filled before the enclosing document validates? | Parse into scratch storage and commit only after full validation; a malformed document keeps the last good state. |
| GUI-subsystem informational flags | Every Windows entry point that answers `--help`/`--version`; CRT `__argc/__argv`, console code page, MessageBox, `exit()` teardown. | Answer before any console/GUI/daemon state, take `GetCommandLineW()` as authority, write the inherited handle (or borrow the parent console), and `_Exit`. |
| `PROC_THREAD_ATTRIBUTE_*` value lifetime | Every `UpdateProcThreadAttribute` call. | The value storage must live until `CreateProcess` returns; block locals are a dangling pointer. |
| Userenv profile SID ownership | Every `CreateAppContainerProfile`/`DeriveAppContainerSidFromAppContainerName` result. | Release with `FreeSid`; only `LocalAlloc` capability SIDs use `LocalFree`. |
| Nested Job Object | Every `CreateJobObject`/`JOB_LIST`/`AssignProcessToJobObject` path. | A nested job cannot set UI limits; containment and resource limits are mandatory, UI restrictions are optional and must be reported when dropped. |
| `argv[0]` in a CLI test needle | Every CMake tool test that greps for a usage line. | Assert `usage: ` plus a synopsis token, never the executable path. |
| Base64 padding across reads | Every streaming base64 decoder and its `=` latch. | Decode per quartet so padding terminates the stream; report `data after padding` distinctly, including across a read boundary. |
| Dotted version comparison | Every SAM/version parser and test. | Follow Java `VersionComparator`: the depth is not fixed, any in-range dotted bound is legal, only non-digit components are malformed; a response vector is bound to the nonce count. |
