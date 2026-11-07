# Compilation diagnostic patterns and repository-wide review

This is the reusable search guide for compiler and linker diagnostics. It
complements the observed run records in
[`COMPILATION-ERRORS.md`](COMPILATION-ERRORS.md) and
[`COMPILATION-WARNINGS.md`](COMPILATION-WARNINGS.md). CI observations and
static hypotheses must remain explicitly distinguished.

## Required triage sequence

The 2026-11-05 review adds namespace-owned array extents (CE-076), closure
member shadowing on MSVC (CW-020), real CMake wrapper entrypoints and obsolete
cache hints (CW-021), and exact-archive manual/changelog evidence (CW-022).
Follow the [runtime regression guide](REGRESSION-PATTERNS.md) for the resulting
publication, cancellation, provider-failure and accepted-handle searches.
Sanitizer configure/build stderr is now preserved independently of CTest;
negative workflow fixtures pin capture, failure propagation and upload.
CE-077 also pins a lexical tooling boundary: unnamed parameter types are not
documentation names. Keep arity, real named operands and function-pointer
checks intact when handling abstract C++ declarators.

The 2026-11-04 continuation adds these reusable checks (CE-074/075,
CW-016/017), distinct from historical runtime findings:

- Callback migrations: inspect the entire enclosing lexical scope before
  introducing a weak handle. Reuse an existing owner when authority matches;
  similarly named handles in separate lambdas or branches are not duplicates.
- Early returns: review following indentation and braces in lambda bodies.
  Correct misleading presentation without changing the guarded operation.
- Extracted Make recipes: verify delegation and helper contents separately.
  A stale check against the original file can fail; concatenating sources can
  incorrectly pass when the helper is no longer invoked.
- CMake script mode: choose recursive symlink and quoted-operand policy
  semantics explicitly. A project's configure baseline does not cover `-P`.
- JNI definitions: omit unused ABI-mandated argument names, preserving their
  types, order and exported symbols. Check actual bodies before removing a
  name and keep Doxygen parameter descriptions consistent.
- Uninitialized random output: compiler dataflow can miss a provider failure
  through an external API. SSU2 incoming-token generation now checks RAND_bytes
  before caching and propagates a zero failure sentinel through all four
  consumers. This is a static security finding, not an observed warning.
  TunnelConfig/DHT initialization still needs complete failure-contract work;
  predictable fallback values are not acceptable substitutes for entropy.
- Early compiler failures: preserve combined stdout/stderr with pipefail and
  always-upload configure/build logs, cache and configure-probe evidence.
  Missing CTest output does not imply no warnings. Negative workflow fixtures
  now pin both capture and upload boundaries independently.

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

## Additional Doxygen patterns from the `41320539612` continuation

| Pattern | Search | Contract |
|---|---|---|
| Javadoc inline tag in a C/C++ comment | Search `{@code`, `{@link`, `{@literal`, `{@inheritDoc}` in every Doxyfile input; Doxygen may read `@code` as an opening block with no `@endcode`. | Use backticks in C/C++ comments; Javadoc inline tags are Java-only. |
| Unbalanced command block | Count `@code`/`@endcode` and `@verbatim`/`@endverbatim` per documentation block. | Every opening command needs its closing command; an unbalanced block aborts at the end of the comment. |
| Documented name not in the declaration | For every `@param` name, check the following declaration's parameter list; array parameters (`char * argv[]`) must yield `argv`, not be dropped. | Doxygen reports "argument ... is not found in the argument list"; document only names that exist. |

## Patterns from the `4e776a39` batch

| Pattern | Search | Contract |
|---|---|---|
| `char` container passed as a byte span | Every `std::span<const uint8_t>`/`std::span<uint8_t>` parameter and caller; `std::string`, `std::string_view`, `std::array<char,N>`, literals. | `std::span` only converts from the same element type: use a borrowing `reinterpret_cast` helper with full-expression lifetime, never an implicit conversion. |
| Win32 device name as a fixture | Every test fixture basename whose stem is `NUL`, `CON`, `AUX`, `PRN`, `COM1-9` or `LPT1-9`, with any extension. | Such a path is the device in every directory, so the tool reads EOF and writes nothing; use a compound stem and pin the fixture bytes. |
| Reused `CreateProcess` mutable buffer | Every `CreateProcessW`/`CreateProcessAsUserW` retry path. | The API may modify the command-line buffer; build one buffer per attempt. |
| Containment disabled by policy | Every spawn path that conditions Job Object creation/assignment on a flag or handle validity. | A worker must never run outside its Job Object; refuse the policy or fail closed on an invalid handle. |
| Test worker bypassing the production entry | Every supervisor/IPC test that implements its own worker loop. | The worker must go through `RunWindowsWorker` (handshake, nonce, role bind) or the test can silently skip the required protocol. |
| GUI-subsystem informational flags | Windows entry points, CRT `__argc/__argv`, console attach, `exit()` teardown. | Answer from the raw command line before any argv/console state; never fall back to an empty CRT vector and start a router. |

## Preventive patterns from the 2026-11-01 subsystem audit

These were found by static review, not by a compiler. Each is a pattern to
grep for before the next dispatch.

| Pattern | Why it matters | Search |
|---|---|---|
| Deferred handler holds raw `this` on an io_context that survives `Stop()` | `io_context::restart()` replays the queue, and destruction can precede it. Ownership must be a weak/shared token plus a generation/stopped check. | `boost::asio::post`, `async_wait`, `steady_timer` in `Transports`, `I2PControl`, streaming/session code |
| Buffer resized inside a receive loop | A tolerated refusal that continues the loop shrinks the next `recvmsg()` capacity and turns a later valid record into a fatal truncation. Keep one fixed-capacity storage and decode a view. | every `ReceiveWithFd`/loop-local `std::string`/vector used as a `msghdr`/`iovec` target |
| Refusal that names an unknown enum | A typed `Error` whose offending-type field has no registry row cannot be decoded by the peer. Attributability requires type validity, not just magic/version/length. | `RejectFrame`, `EncodeError`, both POSIX and Windows channels |
| Frame type chosen independently of the capability | A `RequiredFile` type sent without a descriptor is refused before parsing. Derive the type from whether a descriptor is attached. | `ConfigureLogFd`/`ConfigureLog`, any `RequiredFile`/`RequiredSocket` message |
| `std::string`/`string_view`/`char[]` passed to `std::span<uint8_t>` | No element-type conversion exists; the error blocks every platform. Use one borrowing helper with full-expression lifetime. | every byte-span parameter |
| Interpolated format string | `sprintf("%.${n}f", …)` becomes a conversion bug when the value changes; use `%.*f`. | all Perl/shell format calls that embed a variable in the template |
| Byte length used as a column budget | UTF-8 text must be measured with `terminal::text::Width`/`DisplayWidth`; a byte budget truncates Cyrillic/CJK labels and tears the layout. | every `Put(text, limit)` caller and every width computation |
| Duplicate platform table | Two copies of a width table or address classifier drift. Keep one owner and include it from both branches. | `tui/main.cpp` vs `TerminalWin32.cpp`; `HTTPProxy` vs `SOCKS` |
| `GetFileType`/`GetLastError` sequence | `GetFileType` does not reset the last error on success; set `SetLastError(NO_ERROR)` before the probe or a stale code rejects a valid handle. | every Win32 handle probe |
| Missing direct include for a new standard type | Transitive Asio/Boost includes can disappear; each changed TU must name the headers it uses (`<chrono>`, `<cctype>`, `<span>`, `<thread>`, `<functional>`, `<sys/time.h>`). `daemon/UPnP.cpp` relied on `Log.h` for `std::bind`, and `tui/TuiClient.cpp` on a transitive `struct timeval`. | new helper in a large TU |
| MSVC C4996 from test-only CRT calls | A source linked by many test executables can repeat one warning across the matrix; inspect the shared translation unit once, then search sibling test helpers for the same deprecated CRT use. | Use the supported secure CRT API with explicit ownership on Windows and retain the portable API on POSIX; do not globally disable C4996. |
| Hardening option accepted without equivalent platform flags | Compare workflow options against every compiler branch; do not silently omit hardening on a supported platform or apply ELF flags to PE images. | Keep compiler and linker mitigations paired, architecture guards explicit, and a compile-free contract for the intended option set. |
| `defined(DOXYGEN)` without a Doxyfile `PREDEFINED` | The declaration is stripped from the generated API docs. Add the predicate or drop the branch. | `docs/Doxyfile.source` against `DOXYGEN`/`_WIN32` guards |

## Warnings that would appear under a stricter lane

- `Torrents.cpp::ParseTrackerResponse`'s lambda parameter `buf` shadows the
  function parameter (only `-Wshadow`).
- `daemon/TuiServer.cpp` compared `EAGAIN` and `EWOULDBLOCK`, the same
  constant on Linux (only `-Wlogical-op`); the duplicate spelling is removed
  from both sites.
- No `-Werror` exists in the tree today; these are recorded rather than
  suppressed, and a stricter lane must fix or justify them first.

| Record signature length is the identity's, not the signer's | An offline/transient signer of a different type writes past a buffer sized by the identity. Refuse an offline signer (or a length mismatch) for RouterInfo/LS1 instead of publishing an unverifiable record. | `GetSignatureLen()`/`Sign()` pairs; `LocalRouterInfo::CreateBuffer`, `CreateNewLeaseSet`, `LocalLeaseSet` |
| Mutable store type selects a wire layout | A decrypted record overwrites `m_StoreType`, so a layout decision based on it reads ciphertext. Keep an immutable outer-layer marker and never read `GetBuffer()`/`GetStoreType()` without `IsValid()`. | `LeaseSet2::Update`, `ExtractPublishedTimestamp`, `ReadFromBufferEncrypted` |
| Raw client line in a diagnostic | A tab-separated/mistyped control command can carry `DESTINATION=<private keys>` or `PASSWORD=`. Log only length/field count, never the line. | every `LogPrint` that receives a request buffer |
| Shared `shared_ptr` member reset across threads | `if (m_X)` is not synchronization. Guard with a leaf mutex and take a shared copy per use. | `I2CPDestination::m_Owner`, `I2PServerTunnel::m_Resolver`, service members touched from admin threads |
| Fail-open configuration upgrade | An old-format whitelist whose entries all fail to parse must deny, not open. | `accesslist` migration, `filterDefinition`, `targetForPort` |

## Patterns from the `4f76f65f` batch

| Pattern | Why it matters | Search |
|---|---|---|
| `.data()` on a const view passed as a writable out-parameter | `Tag`/`IdentHash` expose only a const `data()`, while the OpenSSL `SHA*` functions write through their third argument. The implicit `Tag` conversion is non-const; selecting `.data()` is ill-formed. Use a mutable array or the object conversion. | every `SHA256`/`SHA1`/`SHA512` call whose output is a `Tag`, `IdentHash` or `.data()` |
| CMake script relies on a policy default | `cmake -P` has no `cmake_minimum_required()`, so CMP0012 OLD makes `while(TRUE)` false and re-reads a TRUE-valued variable as an undefined name, silently skipping or inverting a check. Use `while(1)`/`if(1)` and set `cmake_policy(SET CMP0012 NEW)` in any script that branches on a boolean variable. | `tests/*.cmake`, `tools/*.cmake` invoked with `-P` |
| Tool installed by one composite action used in the next | A shell-local `brew shellenv`, virtualenv or exported PATH does not survive into another composite step. Resolve the binary by absolute path or re-establish the environment, and do not suppress the lookup error. | every `brew`, `python`, `perl` or tool invocation in a follow-up action |
| Platform guard removes the only use of a parameter | Android/Windows branches can turn a shared signature into an unused parameter warning. Mark it `[[maybe_unused]]` with the platform reason instead of deleting the parameter. | `#if !defined(ANDROID)`, `#ifdef _WIN32`, `WITH_BINARY=OFF` paths |

| Guard added after an early `return` | The new branch is unreachable and its regression fails. Place the check before every early exit and keep a positive control that proves the guard is reached. | every new `if` in a classifier/parser with preceding `return false`/`return` |
| Optional snapshot dereferenced in a test | `status.Get(...)->field` is UB when the publisher did not create the entry and stays green in a `/UNDEBUG` build. Use a checked accessor. | every `->` on `std::optional`/`i2p::expected` in tests |
| All-slash path accepted as a policy root | `unveil("/", "rwc")` grants the whole filesystem. Reject `//`, `///` and `/` in both the builder and the applier. | unveil/root construction and hand-assembled policy objects |

## Published revision `56f17c0f` (2026-11-02)

| Pattern | Why it matters | Search |
|---|---|---|
| Parser returns `string_view` into caller-owned input | A temporary concatenation can die at the end of the call while a parsed result still exposes views. Keep the input in a named owner for the full result lifetime; distinguish this test-fixture defect from production consumers that parse and consume synchronously. | parser APIs and results containing `string_view`, especially blocklist, DHT, IPC and config tests |
| Convert untrusted integers to enums before validating the domain | Loading an arbitrary integer directly into an enum can trigger UBSan and lets invalid values reach switch logic. Validate as an integer and return an optional/expected enum only for supported values. | integer config parsing, wire/parser conversions, `static_cast<enum>` and enum lookup APIs |
| Reopen a security-sensitive path after a separate chmod/create step | Path replacement between validation and open can redirect secret output. Use the single validated atomic no-follow writer and inspect all fallbacks, not only the normal path. | private key/router state creation and every `CreatePrivateFile`/ofstream fallback |
| Index a normalized identity before checking it is non-empty | Public storage helpers can receive an empty value through a future caller even when current identity producers are canonical. Return a defined failure value before indexing and pin the empty-input contract. | `HashedStorage::Path` and all shard/path helpers indexing caller-provided strings |
| Dependency policy differs between build and installed consumers | A clean configure does not guarantee a clean `find_package()` consumer. Set policies only when available in the project and repeat dependency-discovery policy in the exported config include; preserve the consumer's surrounding scope. | every generated/exported `*Config.cmake` that calls `find_dependency()` |

### Timer cancellation and minimum-version assumptions

CE-008's deprecated timer error-code overload reappeared in local RPC/SOCKS
changes. Boost 1.74 documents it as deprecated, while current Asio exposes
parameterless cancel only. The API present at the minimum version is not
necessarily portable across the complete supported range. Use cancel() for
timers, retaining socket error-code overloads. The expanded source contract
covers I2CP, SAM, SOCKS and RPC. These static corrections await compilation;
no old remote run validates them.

## 2026-11-07 preventive review

- CE-078: require direct headers at every filesystem namespace consumer;
  transitive includes vary with platform and test support translation units.
- CE-079 / CW-022: compare an assertion with the exact-run uploaded payload
  before changing install authority. Canonical package and static-bundle names
  can intentionally differ.
- CE-080: a source guard must recognize secure RAII ownership, not insist on
  the removed raw cleanup spelling. Pin deleter and owner together.
- CW-023/024: review platform macro definitions and value-copy range loops
  across siblings, including successful compiler output.
- Floating dependency names do not pin coverage: the OpenSSL 4 configure
  artifact now identifies 4.1.0, leaving explicit 4.0.x coverage pending.
- Host Git availability does not imply VM or CTest PATH availability. Declare
  the fixture dependency and preserve its interpreter-specific environment.
