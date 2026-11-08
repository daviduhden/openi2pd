<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: source documentation.

Documents the in-source comment conventions and the manual API-doc lane.
-->

# Source documentation

This page is the implementation guide for the permanent
[source documentation policy](../AGENTS.md) in `AGENTS.md`. It covers the
comments that live next to the code, not the Markdown guides.

## What to document

Document the contract a future maintainer must preserve:

- ownership and lifetime, including who destroys a callback target;
- synchronization: which mutex, executor or thread owns each field;
- preconditions, postconditions, failure modes and cleanup;
- bounded sizes, deadlines, admission limits and overflow behaviour;
- wire formats, offsets, version/type checks and state transitions;
- trust boundaries, validation order, secret handling and replay rules;
- platform differences that change an implementation, not only a build flag;
- why an apparently unusual workaround is required.

Do not restate a getter, a loop or a function name, and do not document
behaviour that the code does not actually implement. When a guarantee is still
pending, describe the current limitation and reference the tracking row
instead of claiming the target design.

Every change updates the comments it invalidates. A comment that contradicts
the code is a defect, not legacy noise.

State the boundary that enforces each guarantee. A validating factory does
not constrain other constructors; ignoring an obsolete completion does not
cancel a write already admitted to an executor. Service-wide filesystem
hardening does not establish separate network and storage capabilities for
each worker. Describe those limits where the corresponding API or directive
is documented, after checking its callers and implementation.

When reflowing a C-style or Doxygen block, preserve its single opening
delimiter and matching `*/`. Continuation lines begin with `*`; do not emit a
new `/*` marker for each wrapped line.

Write for a human maintainer in technical American English; use ASCII unless
an identifier or source format requires otherwise. Keep comment lines to 72
characters or fewer, except where a tool or format imposes a documented
constraint. Explain verified intent and contracts, not the next statement.
Inspect behavior before adding prose. Remove or correct comments that are
stale, ambiguous, duplicated or contradicted by the implementation.

## C++23

Use Doxygen for public headers and non-obvious implementation helpers:

```cpp
/**
 * @brief Apply one authenticated tunnel-layer block.
 * @param iv 16-byte IV; the caller retains ownership.
 * @param data decrypted payload, possibly empty.
 * @return false when the AEAD tag or length check fails; @a data is
 *         cleared and must not be forwarded.
 * @pre The layer key has been installed by the tunnel-build path.
 * @warning Runs on the transport thread; do not hold a netDb lock.
 */
bool ProcessBlock (const uint8_t * iv, std::span<uint8_t> data);
```

Use `///` for short declarations, `@brief`, `@param`, `@return`, `@pre`,
`@post` and `@warning` only when they add information. Prefer internal `//`
comments for algorithms, and explain the invariant rather than narrating each
line. Keep identifiers, line structure and clang-format conventions unchanged.

`@param` belongs to the declaration only. Doxygen merges a member's
declaration and definition documentation, and it aborts on the same parameter
documented twice, so a `.cpp` definition keeps its behavioural prose and
repeats no parameter contract. A class is declared in the header and defined
out of line in the source: document `@param` next to the in-class declaration
and describe the algorithm, ownership and failure path in the definition.
Document `@param` on the definition only when the declaration carries no
parameter documentation at all, for example a file-local helper or a class
whose declaration lives in a third-party header.

Public C++ APIs are collected by `docs/Doxyfile.source` (input:
`libi2pd`, `libi2pd_client`, `privsep`, `daemon`, `tui`, `tools` and the
Android JNI sources). `WARN_AS_ERROR`, `WARN_IF_DOC_ERROR` and the undocumented
warnings are part of the manual `source-docs` job in `validation.yml`, so a
`@param` name that does not match the declaration (or a `@param` on an
unnamed parameter) fails that job. A literal `*/` inside comment prose closes the block early; write
`X-Forwarded-* and *-IP`, not `X-Forwarded-*/`.

`Doxyfile` (without `.source`) is the legacy template and has an empty
`INPUT`; `Doxyfile.source` is the current API generator.

## Java 25 and Android

Use Javadoc for public or lifecycle-relevant classes, interfaces and methods.
Document:

- which component owns a service, process, Binder or `ParcelFileDescriptor`;
- which methods must run on the main thread and which use an executor;
- callback/lifecycle ordering and cancellation;
- AIDL generation, owner binding and admission limits;
- SAF grant lifetime, exact-document authority and cleanup responsibility;
- JNI local/global reference rules, pending exceptions and `noexcept`;
- Java 25 source features versus D8/R8 and ART API availability.

Javadoc runs with the debug Android classpath, package-private APIs and
doclint warnings as errors in the manual Android lane.
Do not lower the Java source level to silence a doc or toolchain warning.

## Perl

Keep the shebang, `use strict; use warnings;` and provenance header. Add a
short POD block when a script has a CLI or reusable output:

```perl
=head1 NAME

check-example.pl - validate the example contract

=head1 SYNOPSIS

  check-example.pl [--root DIR] [--quiet]

=head1 EXIT STATUS

0 when every check passes, 1 on a validation failure, 2 on a usage error.

=head1 DEPENDENCIES

Core Perl only; no CPAN modules are downloaded at run time.
```

Use inline comments only for a non-obvious regular expression, transform,
retry/backoff, atomic write, redaction or portability workaround. State input
and output formats and whether the script is read-only or writes files.

## POSIX shell and service units

Document the operational contract in concise POSIX comments:

- purpose, environment variables, arguments, `$PATH`/`umask` assumptions;
- file creation/replacement, ownership and permissions;
- traps, signal handling, child reaping and cleanup on every exit path;
- error propagation and non-zero exit status;
- `/bin/sh` portability, including OpenBSD base tools and the absence of
  bash arrays, `local -n`, process substitution or `pipefail`;
- service-manager differences: readiness limits, stop budgets, signals,
  identity, sandbox paths and why a directive is required.

Never assume bash in a `/bin/sh` script or in an example. Keep unit ordering
and argv unchanged; a comment must not imply a readiness or isolation
guarantee that the unit does not provide.

## Generated documentation and validation

Generation is manual-only and never part of a local build or default install:

- the source-docs lane creates the gitignored `docs/generated/source-api`
  path before `docs/Doxyfile.source` generates C++ API HTML;
- the Android Javadoc command uses the debug Java compiler and Android SDK
  classpath;
- Doxygen failures (including undocumented-parameter warnings) fail the
  `source-docs` job in `validation.yml`; Javadoc failures fail the Android
  job.

`doxygen-static` (`tools/scripts/check-doxygen-params.pl`) is the
compile-free C++ counterpart. Doxygen reports these defects under
`WARN_IF_DOC_ERROR`, and `WARN_AS_ERROR` makes them fatal; the checker accepts
a block with no `@param` at all (because `WARN_NO_PARAMDOC` is disabled) and
rejects three shapes:

- a documented function that names some parameters but leaves others out;
- the same parameter documented twice inside one block;
- a `.c`/`.cpp` definition that documents parameters while the matching
  declaration in a header already documents them, which Doxygen reports as
  `argument 'x' ... has multiple @param documentation sections`.

The third rule identifies the entity by its enclosing namespace/class chain,
its unqualified name and its parameter count, so a same-named helper with a
different arity or a function in another scope is not reported. Two same-arity
overloads in one scope cannot be distinguished without a C++ parser and remain
a reviewed limit rather than a checker guarantee. A definition whose
declaration is not visible in the scanned roots (a third-party header, or a
declaration inside a function body) is not flagged.

The checker reads the same input roots and file patterns as
`docs/Doxyfile.source`, skips `tools/tests`, and reports the file, line and
affected names. It does not replace the manual lane, which remains the only
dynamic Doxygen evidence.

It also rejects Javadoc inline tags (`{@code ...}`, `{@link ...}`,
`{@literal ...}`, `{@inheritDoc}`) in a C/C++ comment: Doxygen reads the
`@code` as an opening block and aborts at the end of the comment, so those
values belong in backticks. `@code`/`@endcode` and
`@verbatim`/`@endverbatim` must balance for the same reason.

The same checker also requires every `@ref`/`\ref` target to name a class,
struct, union, enum, namespace, typedef, using, macro or callable collected
from the scanned tree. Doxygen only resolves extracted symbols, so a generic
data member or an ordinary word is an unresolved reference and aborts
`WARN_AS_ERROR`; write those in backticks. The rule caught `@ref mutex` and
`@ref uid`, and the negative fixtures keep both the rejected and accepted
forms covered.

`android-javadoc-static` (`android/tools/check-android-javadoc.pl`) is the
compile-free guard for the comment defects that doclint rejects only after
the whole Android job has configured and compiled: unregistered block tags,
an inline tag used as a block tag, raw angle-bracket placeholders in prose
and a `@param` name the declaration does not have. Two rules follow from it.
`@implNote`, `@implSpec` and `@apiNote` are JDK-internal tags, not standard
doclet tags, so the Android task does not register them and they must not be
used; state the implementation note as a prose paragraph instead. Angle
brackets are HTML in Javadoc prose, so `TORRENT-DETAIL <tunnel> <id>` must be
written as `{@code TORRENT-DETAIL <tunnel> <id>}`, whose body is rendered
verbatim, or as `&lt;tunnel&gt;`. The same split applies to Doxygen, which
parses Markdown, so protocol shapes there belong in backticks instead.

Local work is limited to source inspection, link/reference checks that do not
invoke a compiler, and `git diff --check`. Do not run Doxygen, Javadoc, a
compiler or any project test locally. Record generated-documentation evidence
and unresolved warnings as `Pending CI` until the manual lane passes.

## Review checklist

- The comment states a contract that the code actually enforces.
- Parameter names and return/void semantics match the declaration.
- Each `@param` appears once for the member, on the declaration or on the
  definition, never on both.
- Ownership, locking and thread affinity are named where they matter.
- Bounds, deadlines and failure cleanup are explicit.
- Trust-boundary and secret-handling statements are accurate.
- No `*/` appears inside comment prose.
- Comment lines are at most 72 characters, except documented format/tool
  constraints.
- Wording is concise, technically precise, American English and ASCII by
  default; it adds information instead of narrating code.
- No TODO/FIXME marker is added without a tracked task or an explicit
  "unfinished" statement.
- The change contains comments only, unless a separately reviewed defect fix
  is included.
