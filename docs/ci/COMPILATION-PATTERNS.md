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
