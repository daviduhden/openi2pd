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

## C++23

Use Doxygen for public headers and non-obvious implementation helpers:

```cpp
/**
 * @brief Apply one authenticated tunnel-layer block.
 * @param iv 16-byte IV; the caller retains ownership.
 * @param data decrypted payload, possibly empty.
 * @return false when the AEAD tag or length check fails; @a data is then
 *         cleared and must not be forwarded.
 * @pre The layer key has been installed by the tunnel-build path.
 * @warning Runs on the transport thread; never call with a netDb lock held.
 */
bool ProcessBlock (const uint8_t * iv, std::span<uint8_t> data);
```

Use `///` for short declarations, `@brief`, `@param`, `@return`, `@pre`,
`@post` and `@warning` only when they add information. Prefer internal `//`
comments for algorithms, and explain the invariant rather than narrating each
line. Keep identifiers, line structure and clang-format conventions unchanged.

Public C++ APIs are collected by `docs/Doxyfile.source` (input:
`libi2pd`, `libi2pd_client`, `privsep`, `daemon`, `tui`, `tools` and the
Android JNI sources). `WARN_AS_ERROR`, `WARN_IF_DOC_ERROR` and the undocumented
warnings are part of the manual `source_docs` lane, so a `@param` name that
does not match the declaration (or a `@param` on an unnamed parameter) fails
that lane. A literal `*/` inside comment prose closes the block early; write
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
doclint warnings as errors in the manual `source_docs`/Android lanes.
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
- Doxygen and Javadoc failures (including doclint and undocumented-parameter
  warnings) fail the `source_docs` job.

Local work is limited to source inspection, link/reference checks that do not
invoke a compiler, and `git diff --check`. Do not run Doxygen, Javadoc, a
compiler or any project test locally. Record generated-documentation evidence
and unresolved warnings as `Pending CI` until the manual lane passes.

## Review checklist

- The comment states a contract that the code actually enforces.
- Parameter names and return/void semantics match the declaration.
- Ownership, locking and thread affinity are named where they matter.
- Bounds, deadlines and failure cleanup are explicit.
- Trust-boundary and secret-handling statements are accurate.
- No `*/` appears inside comment prose.
- No TODO/FIXME marker is added without a tracked task or an explicit
  "unfinished" statement.
- The change contains comments only, unless a separately reviewed defect fix
  is included.
